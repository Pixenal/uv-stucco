/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <string.h>

#include <in_piece.h>
#include <map.h>
#include <utils.h>

typedef struct SplitInPiecesJobArgs {
	JobArgs core;
	const InPieceArr *pInPieceArr;
	InPieceArr newInPieces;
	InPieceArr newInPiecesClip;
	SplitInPiecesAlloc alloc;
	JobArgsFoot foot;
} SplitInPiecesJobArgs;

void splitInPiecesJobInit(StucContext pCtx, void *pShared, void *pInitInfo, void *pEntryVoid) {
	SplitInPiecesJobArgs *pEntry = pEntryVoid;
	pEntry->pInPieceArr = pInitInfo;
}

static
I32 stucCouldInEdgeIntersectMapFace(const Mesh *pMesh, I32 edge) {
	bool preserve = stucGetIfPreserveEdge(pMesh, edge);
	bool ret = stucGetIfSeamEdge(pMesh, edge) || stucGetIfMatBorderEdge(pMesh, edge);
	return preserve && !ret ? 2 : preserve || ret;
}

static
bool borderPredicate(const void *pMeshRaw, I32 edge) {
	return stucCouldInEdgeIntersectMapFace(pMeshRaw, edge);
}

static
bool isEdgeInternal(
	const void *pMeshRaw,
	const ClustSplitFaceIdx *pAdj,
	I32 edge
) {
	const Mesh *pMesh = pMeshRaw;
	I32 canIntersect = stucCouldInEdgeIntersectMapFace(pMesh, edge);
	//if edge is preserve, and adj is pending remove, edge is internal, so ignore
	return canIntersect == 1 || canIntersect == 2 && !pAdj->pendingRemove;
}

typedef enum ReceiveStatus {
	STUC_RECEIVE_NONE,
	STUC_RECEIVE_SOME,
	STUC_RECEIVE_ALL
} ReceiveStatus;

typedef struct MapCornerLookup {
	HalfPlane *pHalfPlanes;
	ReceiveStatus receive;
} MapCornerLookup;

typedef enum ReceiveIntersectResult {
	STUC_NO_INTERSECT,
	STUC_INTERSECTS_RECEIVE,
	STUC_INTERSECTS_NON_RECEIVE
} ReceiveIntersectResult;

static
ReceiveIntersectResult doesCornerIntersectReceive(
	const MapToMeshBasic *pBasic,
	const FaceRange *pMapFace, const MapCornerLookup *pMapCorners,
	const FaceRange *pInFace, FaceCorner inCorner
) {
	PIX_ERR_ASSERT(
		"check this before calling",
		pMapCorners->receive == STUC_RECEIVE_SOME ||
		pMapCorners->receive == STUC_RECEIVE_ALL
	);
	FaceCorner inCornerNext = {
		.face = pInFace->idx,
		.corner = stucGetCornerNext(inCorner.corner, pInFace)
	};
	V3_F32 inVert =
		pBasic->pInMesh->pPos[stucGetMeshVert(&pBasic->pInMesh->core, inCorner)];
	V3_F32 inVertNext =
		pBasic->pInMesh->pPos[stucGetMeshVert(&pBasic->pInMesh->core, inCornerNext)];
	for (I32 i = 0; i < pMapFace->size; ++i) {
		bool receive = true;
		if (pMapCorners->receive == STUC_RECEIVE_SOME &&
			!stucCheckIfEdgeIsReceive(
				pBasic->pMap->pMesh,
				pMapCorners->pHalfPlanes[i].edge,
				pBasic->receiveLen
		)) {
			receive = false;
		}
		F32 tMapEdge = 0;
		F32 tInEdge = 0;
		V2_F32 mapUv = pMapCorners->pHalfPlanes[i].uv;
		V2_F32 mapUvNext = pMapCorners->pHalfPlanes[stucGetCornerNext(i, pMapFace)].uv;
		stucCalcIntersection(
			inVert, inVertNext,
			pMapCorners->pHalfPlanes[i].uv, _(mapUvNext V2SUB mapUv),
			NULL,
			&tInEdge, &tMapEdge
		);
		if (tInEdge >= .0f && tInEdge <= 1.0f &&
			tMapEdge >= .0f && tMapEdge <= 1.0f
		) {
			return receive ? STUC_INTERSECTS_RECEIVE : STUC_INTERSECTS_NON_RECEIVE;
		}
	}
	return STUC_NO_INTERSECT;
}

typedef struct SplitArgs {
	const MapToMeshBasic *pBasic;
	const FaceRange *pMapFace;
	const MapCornerLookup *pMapCorners;
	const InPiece *pInPiece;
	InPieceArr *pNewInPieces;
	InPieceArr *pNewInPiecesClip;
	InPiece *pNewInPiece;
	SplitInPiecesAlloc *pLinAlloc;
} SplitArgs;

static
bool isEdgeValidPreserve(
	const void *pArgsRaw,
	const Mesh *pInMesh,
	const FaceRange *pInFace,
	FaceCorner inCorner
) {
	const SplitArgs *pArgs = pArgsRaw;
	I32 edge = stucGetMeshEdge(&pInMesh->core, inCorner);
	if (pArgs->pMapCorners->receive != STUC_RECEIVE_NONE &&
		stucGetIfPreserveEdge(pInMesh, edge)
	) {
		ReceiveIntersectResult result = doesCornerIntersectReceive(
			pArgs->pBasic,
			pArgs->pMapFace, pArgs->pMapCorners,
			pInFace, inCorner
		);
		if (result == STUC_NO_INTERSECT || result == STUC_INTERSECTS_RECEIVE) {
			return true;
		}
		PIX_ERR_ASSERT("", result == STUC_INTERSECTS_NON_RECEIVE);
	}
	return false;
}

static
ReceiveStatus getMapFaceReceiveStatus(
	const MapToMeshBasic *pBasic,
	const FaceRange *pFace
) {
	I32 count = 0;
	for (I32 i = 0; i < pFace->size; ++i) {
		I32 edge = stucGetMeshEdge(
			&pBasic->pMap->pMesh->core,
			(FaceCorner) {.face = pFace->idx, .corner = i}
		);
		if (stucCheckIfEdgeIsReceive(pBasic->pMap->pMesh, edge, pBasic->receiveLen)) {
			count++;
		}
	}
	if (!count) {
		return STUC_RECEIVE_NONE;
	}
	else if (count != pFace->size) {
		return STUC_RECEIVE_SOME;
	}
	return STUC_RECEIVE_ALL;
}

static
void newInPieceVerify(SplitArgs *pArgs) {
	if (pArgs->pNewInPiece) {
		return;
	}
	pixalcLinAlloc(&pArgs->pLinAlloc->encased, (void **)&pArgs->pNewInPiece->pList, 1);
	pArgs->pNewInPiece->pList->cluster = pArgs->pInPiece->pList->cluster;
	pArgs->pNewInPiece->pList->tile = pArgs->pInPiece->pList->tile;
}

static
StucErr facesAdd(void *pArgsRaw, const ClustSplitFaceBuf *pBuf) {
	StucErr err = PIX_ERR_SUCCESS;
	SplitArgs *pArgs = pArgsRaw;
	newInPieceVerify(pArgs);
	pArgs->pNewInPiece->pList->inFaces.count = pBuf->count;
	pixalcLinAlloc(
		&pArgs->pLinAlloc->inFace,
		(void **)&pArgs->pNewInPiece->pList->inFaces.pArr,
		pArgs->pNewInPiece->pList->inFaces.count
	);
	pArgs->pNewInPiece->faceCount = pBuf->count;
	for (I32 i = 0; i < pBuf->count; ++i) {
		PIX_ERR_ASSERT("", pBuf->ppArr[i]->pendingRemove);
		pArgs->pNewInPiece->pList->inFaces.pArr[i] = pBuf->ppArr[i]->face;
	}
	InPieceArr *pNewInPieces = pArgs->pNewInPiece->borderArr.count ?
		pArgs->pNewInPiecesClip : pArgs->pNewInPieces;
	PIX_ERR_ASSERT("", pNewInPieces->count <= pNewInPieces->size);
	if (pNewInPieces->count == pNewInPieces->size) {
		pNewInPieces->size *= 2;
		pNewInPieces->pArr = pArgs->pBasic->pCtx->alloc.fpRealloc(
			pNewInPieces->pArr,
			pNewInPieces->size * sizeof(InPiece)
		);
	}
	pNewInPieces->pArr[pNewInPieces->count] = *pArgs->pNewInPiece;
	pNewInPieces->count++;
	return err;
}

static
StucErr borderAdd(void *pArgsRaw, const ClustBorderBuf *pBuf) {
	StucErr err = PIX_ERR_SUCCESS;
	SplitArgs *pArgs = pArgsRaw;
	newInPieceVerify(pArgs);
	pArgs->pNewInPiece->borderArr.count = pBuf->arr.count;
	pixalcLinAlloc(
		&pArgs->pLinAlloc->border,
		(void **)&pArgs->pNewInPiece->borderArr.pArr,
		pArgs->pNewInPiece->borderArr.count
	);
	memcpy(
		pArgs->pNewInPiece->borderArr.pArr,
		pBuf->arr.pArr,
		pBuf->arr.count * sizeof(Border)
	);
	return err;
}

static
PixErr buildIdxTable(
	void *pArgsRaw,
	void *pIdxTable,
	void (*fpBuild)(void *, const PixtyI32Arr *)
) {
	StucErr err = PIX_ERR_SUCCESS;
	SplitArgs *pArgs = pArgsRaw;
	EncasedMapFace *pInFaces = pArgs->pInPiece->pList;
	do {
		fpBuild(pIdxTable, &pInFaces->inFaces);
		pInFaces = (EncasedMapFace *)pInFaces->core.pNext;
	} while (pInFaces);
	return err;
}

static
I32 getEdge(const void *pMeshRaw, ClustFaceCorner corner) {
	return stucGetMeshEdge(
		pMeshRaw,
		(FaceCorner){
			.face = corner.face,
			.corner = corner.corner
		}
	);
}

static
ClustFaceCorner getAdjCorner(const void *pMeshRaw, ClustFaceCorner corner) {
	FaceCorner adj = {0};
	stucGetAdjCorner(
		pMeshRaw,
		(FaceCorner){
			.face = corner.face,
			.corner = corner.corner
		},
		&adj
	);
	return (ClustFaceCorner){.face = adj.face, .corner = adj.corner};
}

static
StucErr splitInPieceEntry(
	SplitInPiecesJobArgs *pArgs,
	const InPiece *pInPiece,
	ClustSplitFaceBuf *pInFaceBuf,
	ClustBorderBuf *pBorderBuf
) {
	StucErr err = PIX_ERR_SUCCESS;
	const MapToMeshBasic *pBasic = pArgs->core.pShared;
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;

	PIX_ERR_ASSERT("", pInPiece->faceCount > 0);
	if (!pInFaceBuf->size) {
		pInFaceBuf->size = pInPiece->faceCount;
		pInFaceBuf->ppArr = pAlloc->fpMalloc(pInFaceBuf->size * sizeof(void *));
	}
	else if (pInFaceBuf->size < pInPiece->faceCount) {
		pInFaceBuf->size = pInPiece->faceCount;
		pInFaceBuf->ppArr =
			pAlloc->fpRealloc(pInFaceBuf->ppArr, pInFaceBuf->size * sizeof(void *));
	}

	//TODO replace with cluster border
	FaceRange mapFace =
		stucGetFaceRange(&pBasic->pMap->pMesh->core, pInPiece->pList->mapFace);
	MapCornerLookup mapCorners = {
		.pHalfPlanes = pAlloc->fpCalloc(mapFace.size, sizeof(HalfPlane)),
		.receive = getMapFaceReceiveStatus(pBasic, &mapFace)
	};
	initHalfPlaneLookup(
		pBasic->pMap->pMesh,
		&mapFace,
		(V2_I16) {0},
		mapCorners.pHalfPlanes
	);
	SplitArgs subArgs = {
		.pBasic = pBasic,
		.pInPiece = pInPiece,
		.pLinAlloc = &pArgs->alloc,
		.pMapCorners = &mapCorners,
		.pMapFace = &mapFace
	};
	SplitMesh clustMesh = {
		.pUserData = pBasic->pInMesh,
		.faceCount = pBasic->pInMesh->core.faceCount,
		.fpFaceRange = stucClustFaceRange,
		.fpVert = stucClustVert,
		.fpPos = stucClustPos,
		.fpAdjCorner = getAdjCorner,
		.fpEdge = getEdge
	};
	ClustSplitCallbacks clustCallbacks = {
		.fpIdxTableBuild = buildIdxTable,
		.fpSplitPredicate = isEdgeValidPreserve,
		.fpBorderAdd = borderAdd,
		.fpFacesAdd = facesAdd,
		.fpIsEdgeIntern = isEdgeInternal,
		.fpBorderPredicate = borderPredicate
	};
	err = clustSplitIslands(
		pAlloc,
		&clustMesh,
		&subArgs,
		&clustCallbacks,
		pInPiece->faceCount,
		pInFaceBuf,
		pBorderBuf
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err, ;);
	pAlloc->fpFree(mapCorners.pHalfPlanes);
}

static
StucErr splitInPieces(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	SplitInPiecesJobArgs *pArgs = pArgsVoid;
	const StucAlloc *pAlloc = &pArgs->core.pCtx->alloc;
	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	pArgs->newInPieces.size = rangeSize;
	pArgs->newInPiecesClip.size = rangeSize;
	pArgs->newInPieces.pArr = pAlloc->fpMalloc(pArgs->newInPieces.size * sizeof(InPiece));
	pArgs->newInPiecesClip.pArr =
		pAlloc->fpMalloc(pArgs->newInPiecesClip.size * sizeof(InPiece));
	pixalcLinAllocInit(pAlloc, &pArgs->alloc.encased, sizeof(EncasedMapFace), rangeSize, true);
	pixalcLinAllocInit(pAlloc, &pArgs->alloc.inFace, sizeof(I32), rangeSize, true);
	pixalcLinAllocInit(pAlloc, &pArgs->alloc.border, sizeof(Border), rangeSize, true);
	ClustSplitFaceBuf inFaceBuf = {0};
	ClustBorderBuf borderBuf = {0};
	for (I32 i = pArgs->core.range.start; i < pArgs->core.range.end; ++i) {
		err = splitInPieceEntry(pArgs, pArgs->pInPieceArr->pArr + i, &inFaceBuf, &borderBuf);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	PIX_ERR_CATCH(0, "", ;);
	if (inFaceBuf.ppArr) {
		pAlloc->fpFree(inFaceBuf.ppArr);
	}
	if (borderBuf.arr.pArr) {
		pAlloc->fpFree(borderBuf.arr.pArr);
	}
	return err;
}

static
void appendNewPiecesToArr(
	const MapToMeshBasic *pBasic,
	InPieceArr *pInPiecesSplit,
	I32 jobCount,
	const SplitInPiecesJobArgs *pJobArgs,
	const InPieceArr *(* getNewInPieceArr) (const SplitInPiecesJobArgs *)
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	pInPiecesSplit->count = pInPiecesSplit->size = 0;
	for (I32 i = 0; i < jobCount; ++i) {
		const InPieceArr *pNewInPieces = getNewInPieceArr(pJobArgs + i);
		pInPiecesSplit->size += pNewInPieces->count;
	}
	PIX_ERR_ASSERT("", pInPiecesSplit->size >= 0);
	pInPiecesSplit->pArr = pAlloc->fpCalloc(pInPiecesSplit->size, sizeof(InPiece));
	for (I32 i = 0; i < jobCount; ++i) {
		const InPieceArr *pNewInPieces = getNewInPieceArr(pJobArgs + i);
		memcpy(
			pInPiecesSplit->pArr + pInPiecesSplit->count,
			pNewInPieces->pArr,
			pNewInPieces->count * sizeof(InPiece)
		);
		pInPiecesSplit->count += pNewInPieces->count;
	}
}

static
const InPieceArr *getNewInPieces(const SplitInPiecesJobArgs *pJobArgs) {
	return &pJobArgs->newInPieces;
}

static
const InPieceArr *getNewInPiecesClip(const SplitInPiecesJobArgs *pJobArgs) {
	return &pJobArgs->newInPiecesClip;
}

I32 inPiecesJobsGetRange(StucContext pCtx, const void *pShared, void *pInitEntry) {
	return ((InPieceArr *)pInitEntry)->count;
}

StucErr stucInPieceArrSplit(
	MapToMeshBasic *pBasic,
	I32 threadId,
	InPieceArr *pInPieces,
	InPieceArr *pInPiecesSplit,
	InPieceArr *pInPiecesSplitClip,
	SplitInPiecesAllocArr *pSplitAlloc
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	SplitInPiecesJobArgs jobArgs[PIXTH_MAX_SUB_MAPPING_JOBS] = { 0 };
	stucMakeJobArgs(
		pBasic->pCtx,
		pBasic,
		&jobCount, jobArgs, sizeof(SplitInPiecesJobArgs),
		pInPieces,
		inPiecesJobsGetRange, splitInPiecesJobInit
	);
	err = stucDoJobInParallel(
		pBasic->pCtx,
		threadId,
		jobCount, jobArgs, sizeof(SplitInPiecesJobArgs),
		splitInPieces
	);
	PIX_ERR_RETURN_IFNOT(err, "");

	inPieceArrDestroy(pBasic->pCtx, pInPieces);
	*pInPieces = (InPieceArr) {0};

	appendNewPiecesToArr(pBasic, pInPiecesSplit, jobCount, jobArgs, getNewInPieces);
	appendNewPiecesToArr(pBasic, pInPiecesSplitClip, jobCount, jobArgs, getNewInPiecesClip);

	for (I32 i = 0; i < jobCount; ++i) {
		pSplitAlloc->pArr[i] = jobArgs[i].alloc;
		inPieceArrDestroy(pBasic->pCtx, &jobArgs[i].newInPieces);
		inPieceArrDestroy(pBasic->pCtx, &jobArgs[i].newInPiecesClip);
	}
	pSplitAlloc->count = jobCount;
	return err;
}
