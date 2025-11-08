/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <string.h>

#include <in_piece.h>
#include <context.h>
#include <map.h>
#include <utils.h>

typedef struct SplitInPiecesJobArgs {
	JobArgs core;
	const InPieceArr *pInPieceArr;
	InPieceArr newInPieces;
	InPieceArr newInPiecesClip;
	SplitInPiecesAlloc alloc;
} SplitInPiecesJobArgs;

void splitInPiecesJobInit(MapToMeshBasic *pBasic, void *pInitInfo, void *pEntryVoid) {
	SplitInPiecesJobArgs *pEntry = pEntryVoid;
	pEntry->pInPieceArr = pInitInfo;
}

typedef struct PieceFaceIdx {
	HTableEntryCore core;
	const EncasingInFace *pInFace;
	bool removed;
	bool pendingRemove;
	bool preserve[4];
} PieceFaceIdx;

static
void initPieceFaceIdxEntry(
	void *pUserData,
	HTableEntryCore *pEntry,
	const void *pKeyData,
	void *pInitInfo,
	I32 linIdx
) {
	EncasingInFace *pInFace = pInitInfo;
	((PieceFaceIdx *)pEntry)->pInFace = pInFace;
}

static
bool cmpPieceFaceIdxEntry(
	const HTableEntryCore *pEntry,
	const void *pKeyData,
	const void *pInitInfo
) {
	return ((PieceFaceIdx *)pEntry)->pInFace->idx == *(I32 *)pKeyData;
}

static
void buildPieceFaceIdxTable(
	HTable *pTable,
	const EncasingInFaceArr *pInFaces
) {
	for (I32 i = 0; i < pInFaces->count; ++i) {
		const EncasingInFace *pInFace = pInFaces->pArr + i;
		PieceFaceIdx *pEntry = NULL;
		I32 faceIdx = pInFace->idx;
		SearchResult result =
			stucHTableGetConst(
				pTable,
				0,
				&faceIdx,
				(void **)&pEntry,
				true, pInFace,
				stucKeyFromI32, NULL, initPieceFaceIdxEntry, cmpPieceFaceIdxEntry
			);
		PIX_ERR_ASSERT("", result == STUC_SEARCH_ADDED);
	}
}

static
SearchResult pieceFaceIdxTableGet(HTable *pTable, I32 face, void **ppEntry) {
	return stucHTableGet(
		pTable,
		0,
		&face,
		ppEntry,
		false, NULL,
		stucKeyFromI32, NULL, NULL, cmpPieceFaceIdxEntry
	);
}

static
bool isEdgeInternal(
	const MapToMeshBasic *pBasic,
	const PieceFaceIdx *pAdj,
	I32 edge
) {
	I32 canIntersect = stucCouldInEdgeIntersectMapFace(pBasic->pInMesh, edge);
	//if edge is preserve, and adj is pending remove, edge is internal, so ignore
	return canIntersect == 1 || canIntersect == 2 && !pAdj->pendingRemove;
}

static
PieceFaceIdx *getAdjFaceInPiece(
	const MapToMeshBasic *pBasic,
	HTable *pIdxTable,
	FaceCorner corner,
	I32 *pAdjCorner
) {
	FaceCorner adj = {0};
	stucGetAdjCorner(pBasic->pInMesh, corner, &adj);
	if (adj.corner == -1) {
		return NULL;
	}
	PIX_ERR_ASSERT("", adj.corner >= 0);
	PieceFaceIdx *pAdjIdxEntry = NULL;
	pieceFaceIdxTableGet(pIdxTable, adj.face, (void **)&pAdjIdxEntry);
	if (!pAdjIdxEntry) {
		if (pAdjCorner) {
			*pAdjCorner = -1;
		}
		return NULL;
	}
	I32 edge = stucGetMeshEdge(&pBasic->pInMesh->core, corner);
	if (pAdjIdxEntry->removed || isEdgeInternal(pBasic, pAdjIdxEntry, edge)) {
		if (pAdjCorner) {
			*pAdjCorner = -1;
		}
		return NULL;
	}
	if (pAdjCorner) {
		FaceRange adjFaceRange =
			stucGetFaceRange(&pBasic->pInMesh->core, pAdjIdxEntry->pInFace->idx);
		adj.corner = stucGetCornerNext(adj.corner, &adjFaceRange);
		if (pAdjIdxEntry->preserve[adj.corner]) {
			if (pAdjCorner) {
				*pAdjCorner = -1;
			}
			return NULL;
		}
		*pAdjCorner = adj.corner;
	}
	return pAdjIdxEntry;
}

typedef struct BorderEdgeTableEntry {
	HTableEntryCore core;
	FaceCorner corner;
	bool checked;
} BorderEdgeTableEntry;

static
void borderEdgeInit(
	void *pUserData,
	HTableEntryCore *pEntry,
	const void *pKeyData,
	void *pInitInfo,
	I32 linIdx
) {
	((BorderEdgeTableEntry *)pEntry)->corner = *(FaceCorner *)pKeyData;
}

static
bool borderEdgeCmp(
	const HTableEntryCore *pEntryCore,
	const void *pKeyData,
	const void *pInitInfo
) {
	BorderEdgeTableEntry *pEntry = (BorderEdgeTableEntry *)pEntryCore;
	return
		pEntry->corner.face == ((FaceCorner *)pKeyData)->face &&
		pEntry->corner.corner == ((FaceCorner *)pKeyData)->corner;
}

static
StucKey borderEdgeMakeKey(const void *pKeyData) {
	return (StucKey){.pKey = pKeyData, .size = sizeof(FaceCorner)};
}

static
BorderEdgeTableEntry *borderEdgeAddOrGet(
	HTable *pBorderTable,
	FaceCorner corner,
	bool add
) {
	BorderEdgeTableEntry *pEntry = NULL;
	SearchResult result = stucHTableGet(
		pBorderTable,
		0,
		&corner,
		(void **)&pEntry,
		add, &corner,
		borderEdgeMakeKey, NULL, borderEdgeInit, borderEdgeCmp
	);
	PIX_ERR_ASSERT(
		"there shouldn't be an existing entry if adding",
		!(add ^ (result == STUC_SEARCH_ADDED))
	);
	return pEntry;
}

static
void addBorderToArr(const MapToMeshBasic *pBasic, BorderArr *pArr, Border border) {
	StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	PIX_ERR_ASSERT("", pArr->count <= pArr->size);
	if (!pArr->size) {
		pArr->size = 2;
		pArr->pArr = pBasic->pCtx->alloc.fpMalloc(pArr->size * sizeof(Border));
	}
	else if (pArr->count == pArr->size) {
		pArr->size *= 2;
		pArr->pArr = pAlloc->fpRealloc(pArr->pArr, pArr->size * sizeof(Border));
	}
	pArr->pArr[pArr->count] = border;
	pArr->count++;
}

I32 stucCouldInEdgeIntersectMapFace(const Mesh *pInMesh, I32 edge) {
	bool preserve = stucGetIfPreserveEdge(pInMesh, edge);
	bool ret = stucGetIfSeamEdge(pInMesh, edge) || stucGetIfMatBorderEdge(pInMesh, edge);
	return preserve && !ret ? 2 : preserve || ret;
}

typedef struct BorderBuf {
	BorderArr arr;
	BorderArr preserveRoots;
} BorderBuf;

static
bool findAndAddBorder(
	const MapToMeshBasic *pBasic,
	BorderBuf *pBorderBuf,
	HTable *pIdxTable,
	HTable *pBorderEdgeTable,
	I32 edgesMax,
	BorderEdgeTableEntry *pStart
) {
	const Mesh *pInMesh = pBasic->pInMesh;
	Border border = {.start = pStart->corner};
	FaceCorner corner = pStart->corner;
	BorderEdgeTableEntry *pEntry  = pStart;
	//bool wind = getPieceFaceIdxEntry(pIdxTable, corner.face)->pInFace->wind;
	do {
		if (border.len != 0) {//dont run this on first edge
			if (
				corner.face == pStart->corner.face &&
				corner.corner == pStart->corner.corner
			) {
				break;//full loop
			}
			pEntry = borderEdgeAddOrGet(pBorderEdgeTable, corner, false);
		}
		if (pEntry) {
			PIX_ERR_ASSERT("", pEntry->checked == false);
			pEntry->checked = true;
			border.len++;
		}
		PIX_ERR_ASSERT("", border.len <= edgesMax);
		I32 adjCorner = 0;
		//this is using the table for the pre-split piece.
		//this is fine, as faces arn't marked removed until the end of this func
		const PieceFaceIdx *pAdjFace = getAdjFaceInPiece(
			pBasic,
			pIdxTable,
			corner,
			&adjCorner
		);
		PIX_ERR_ASSERT(
			"if edge isn't in border arr, there should be an adj face",
			!pEntry ^ !pAdjFace
		);
		if (pAdjFace) {
			//edge is internal, move to next adjacent
			corner.face = pAdjFace->pInFace->idx;
			corner.corner = adjCorner;
			//wind = pAdjFace->pInFace->wind;
		}
		else {
			FaceRange faceRange = stucGetFaceRange(&pInMesh->core, corner.face);
			corner.corner = stucGetCornerNext(corner.corner, &faceRange);
		}
	} while(true);
	addBorderToArr(pBasic, &pBorderBuf->arr, border);
	return true;
}

typedef struct InFaceBuf {
	PieceFaceIdx **ppArr;
	I32 size;
	I32 count;
} InFaceBuf;

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
		stucCalcIntersection(
			inVert, inVertNext,
			pMapCorners->pHalfPlanes[i].uv, pMapCorners->pHalfPlanes[i].dir,
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

static
bool isEdgeValidPreserve(
	const MapToMeshBasic *pBasic,
	const FaceRange *pMapFace,
	const MapCornerLookup *pMapCorners,
	const FaceRange *pInFace,
	FaceCorner inCorner
) {
	I32 edge = stucGetMeshEdge(&pBasic->pInMesh->core, inCorner);
	if (pMapCorners->receive != STUC_RECEIVE_NONE &&
		stucGetIfPreserveEdge(pBasic->pInMesh, edge)
	) {
		ReceiveIntersectResult result = doesCornerIntersectReceive(
			pBasic,
			pMapFace, pMapCorners,
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
void addAdjFaces(
	const MapToMeshBasic *pBasic,
	const FaceRange *pMapFace,
	const MapCornerLookup *pMapCorners,
	InFaceBuf *pInFaceBuf,
	HTable *pIdxTable,
	HTable *pBorderEdges,
	PieceFaceIdx *pFace
) {
	FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, pFace->pInFace->idx);
	for (I32 i = 0; i < inFace.size; ++i) {
		FaceCorner corner = {.face = inFace.idx, .corner = i};
		I32 adjCorner = -1;
		PieceFaceIdx *pAdjFace = getAdjFaceInPiece(
			pBasic,
			pIdxTable,
			corner,
			&adjCorner
		);
		if (!pAdjFace) {
			borderEdgeAddOrGet(pBorderEdges, corner, true);
			continue;
		}
		else if (pAdjFace->pendingRemove) {
			//already added to this piece
			continue; 
		}
		else if (isEdgeValidPreserve(pBasic, pMapFace, pMapCorners, &inFace, corner)) {
			pFace->preserve[i] = true;
			PIX_ERR_ASSERT("", adjCorner != -1);
			pAdjFace->preserve[adjCorner] = true;
			borderEdgeAddOrGet(pBorderEdges, corner, true);
			continue;
		}
		PIX_ERR_ASSERT("", pAdjFace->pInFace);

		pAdjFace->pendingRemove = true;
		PIX_ERR_ASSERT("", pInFaceBuf->count < pInFaceBuf->size);
		pInFaceBuf->ppArr[pInFaceBuf->count] = pAdjFace;
		pInFaceBuf->count++;
	}
}

static
PieceFaceIdx *getFirstRemainingFace(HTable *pIdxTable) {
	PixalcLinAlloc *pTableAlloc = stucHTableAllocGet(pIdxTable, 0);
	PixalcLinAllocIter iter = {0};
	pixalcLinAllocIterInit(pTableAlloc, (Range) { 0, INT32_MAX }, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		PieceFaceIdx *pEntry = pixalcLinAllocGetItem(&iter);
		PIX_ERR_ASSERT("", pEntry);
		if (!pEntry->removed) {
			return pEntry;
		}
	}
	PIX_ERR_ASSERT("this func shouldn't have been called if no faces remained", false);
	return NULL;
}

static
void fillBorderBuf(
	const MapToMeshBasic *pBasic,
	BorderBuf *pBorderBuf,
	HTable *pIdxTable,
	HTable *pBorderEdges
) {
	PixalcLinAlloc *pAlloc = stucHTableAllocGet(pBorderEdges, 0);
	pBorderBuf->arr.count = 0;
	pBorderBuf->preserveRoots.count = 0;
	I32 edgeCount = pixalcLinAllocGetCount(pAlloc);
	PixalcLinAllocIter iter = {0};
	pixalcLinAllocIterInit(pAlloc, (Range) { 0, INT32_MAX }, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		BorderEdgeTableEntry *pEntry = pixalcLinAllocGetItem(&iter);
		if (pEntry->checked) {
			continue;
		}
		I32 edge = stucGetMeshEdge(&pBasic->pInMesh->core, pEntry->corner);
		if (stucCouldInEdgeIntersectMapFace(pBasic->pInMesh, edge)) {
			findAndAddBorder(
				pBasic,
				pBorderBuf,
				pIdxTable,
				pBorderEdges,
				edgeCount,
				pEntry
			);
		}
	}
}

static
void splitAdjFacesIntoPiece(
	SplitInPiecesJobArgs *pArgs,
	const FaceRange *pMapFace,
	MapCornerLookup *pMapCorners,
	InFaceBuf *pInFaceBuf,
	BorderBuf *pBorderBuf,
	const InPiece *pInPiece,
	HTable *pIdxTable,
	InPiece *pNewInPiece,
	I32 *pFacesRemaining
) {
	const MapToMeshBasic *pBasic = pArgs->core.pBasic;
	HTable borderEdges = {0};
	stucHTableInit(
		&pBasic->pCtx->alloc,
		&borderEdges,
		*pFacesRemaining / 2 + 1,
		(I32Arr) {.pArr = (I32[]) {sizeof(BorderEdgeTableEntry)}, .count = 1},
		NULL
	);
	pInFaceBuf->count = 0;
	{
		PieceFaceIdx *pStartFace = getFirstRemainingFace(pIdxTable);
		pInFaceBuf->ppArr[0] = pStartFace;
		pStartFace->pendingRemove = true;
		pInFaceBuf->count++;
		I32 i = 0;
		do {
			PieceFaceIdx *pIdxEntry = NULL;
			pieceFaceIdxTableGet(
				pIdxTable,
				pInFaceBuf->ppArr[i]->pInFace->idx,
				(void **)&pIdxEntry
			);
			addAdjFaces(
				pBasic,
				pMapFace, pMapCorners,
				pInFaceBuf,
				pIdxTable,
				&borderEdges,
				pIdxEntry
			);
		} while (i++, i < pInFaceBuf->count);
	}
	pNewInPiece->faceCount = pInFaceBuf->count;
	fillBorderBuf(pBasic, pBorderBuf, pIdxTable, &borderEdges);
	if (pBorderBuf->arr.count) {
		pNewInPiece->borderArr.count = pBorderBuf->arr.count;
		pixalcLinAlloc(
			&pArgs->alloc.border,
			(void **)&pNewInPiece->borderArr.pArr,
			pNewInPiece->borderArr.count
		);
		memcpy(
			pNewInPiece->borderArr.pArr,
			pBorderBuf->arr.pArr,
			pBorderBuf->arr.count * sizeof(Border)
		);
	}
	stucHTableDestroy(&borderEdges);
	
	// copy buf into new in-piece, & mark in-faces as removed in idx-table
	pNewInPiece->pList->inFaces.count = pInFaceBuf->count;
	pixalcLinAlloc(
		&pArgs->alloc.inFace,
		(void **)&pNewInPiece->pList->inFaces.pArr,
		pNewInPiece->pList->inFaces.count
	);
	for (I32 i = 0; i < pInFaceBuf->count; ++i) {
		PIX_ERR_ASSERT("", pInFaceBuf->ppArr[i]->pendingRemove);
		pNewInPiece->pList->inFaces.pArr[i] = *pInFaceBuf->ppArr[i]->pInFace;
		pInFaceBuf->ppArr[i]->removed = true;
		pInFaceBuf->ppArr[i]->pendingRemove = false;
	}
	*pFacesRemaining -= pInFaceBuf->count;
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
void splitInPieceEntry(
	SplitInPiecesJobArgs *pArgs,
	const InPiece *pInPiece,
	InFaceBuf *pInFaceBuf,
	BorderBuf *pBorderBuf
) {
	const MapToMeshBasic *pBasic = pArgs->core.pBasic;
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;

	HTable idxTable = {0};
	stucHTableInit(
		pAlloc,
		&idxTable,
		pInPiece->faceCount / 4 + 1,
		(I32Arr) {.pArr = (I32[]) {sizeof(PieceFaceIdx)}, .count = 1},
		NULL
	);
	{
		EncasedMapFace *pInFaces = pInPiece->pList;
		do {
			buildPieceFaceIdxTable(&idxTable, &pInFaces->inFaces);
			pInFaces = (EncasedMapFace *)pInFaces->core.pNext;
		} while (pInFaces);
	}

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

	I32 facesRemaining = pInPiece->faceCount;
	do {
		InPiece newInPiece = {0};
		pixalcLinAlloc(&pArgs->alloc.encased, (void **)&newInPiece.pList, 1);
		newInPiece.pList->mapFace = pInPiece->pList->mapFace;
		newInPiece.pList->tile = pInPiece->pList->tile;
		splitAdjFacesIntoPiece(
			pArgs,
			&mapFace,
			&mapCorners,
			pInFaceBuf,
			pBorderBuf,
			pInPiece,
			&idxTable,
			&newInPiece,
			&facesRemaining
		);
		InPieceArr *pNewInPieces = newInPiece.borderArr.count ?
			&pArgs->newInPiecesClip : &pArgs->newInPieces;
		PIX_ERR_ASSERT("", pNewInPieces->count <= pNewInPieces->size);
		if (pNewInPieces->count == pNewInPieces->size) {
			pNewInPieces->size *= 2;
			pNewInPieces->pArr = pAlloc->fpRealloc(
				pNewInPieces->pArr,
				pNewInPieces->size * sizeof(InPiece)
			);
		}
		pNewInPieces->pArr[pNewInPieces->count] = newInPiece;
		pNewInPieces->count++;
		PIX_ERR_ASSERT("", facesRemaining >= 0 && facesRemaining < pInPiece->faceCount);
	} while(facesRemaining);
	pAlloc->fpFree(mapCorners.pHalfPlanes);
	stucHTableDestroy(&idxTable);
}

static
StucErr splitInPieces(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	SplitInPiecesJobArgs *pArgs = pArgsVoid;
	const StucAlloc *pAlloc = &pArgs->core.pBasic->pCtx->alloc;
	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	pArgs->newInPieces.size = rangeSize;
	pArgs->newInPiecesClip.size = rangeSize;
	pArgs->newInPieces.pArr = pAlloc->fpMalloc(pArgs->newInPieces.size * sizeof(InPiece));
	pArgs->newInPiecesClip.pArr =
		pAlloc->fpMalloc(pArgs->newInPiecesClip.size * sizeof(InPiece));
	pixalcLinAllocInit(pAlloc, &pArgs->alloc.encased, sizeof(EncasedMapFace), rangeSize, true);
	pixalcLinAllocInit(pAlloc, &pArgs->alloc.inFace, sizeof(EncasingInFace), rangeSize, true);
	pixalcLinAllocInit(pAlloc, &pArgs->alloc.border, sizeof(Border), rangeSize, true);
	InFaceBuf inFaceBuf = {0};
	BorderBuf borderBuf = {0};
	for (I32 i = pArgs->core.range.start; i < pArgs->core.range.end; ++i) {
		splitInPieceEntry(pArgs, pArgs->pInPieceArr->pArr + i, &inFaceBuf, &borderBuf);
	}
	if (inFaceBuf.ppArr) {
		pAlloc->fpFree(inFaceBuf.ppArr);
	}
	if (borderBuf.arr.pArr) {
		pAlloc->fpFree(borderBuf.arr.pArr);
	}
	if (borderBuf.preserveRoots.pArr) {
		pAlloc->fpFree(borderBuf.preserveRoots.pArr);
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

I32 inPiecesJobsGetRange(const MapToMeshBasic *pBasic, void *pInitEntry) {
	return ((InPieceArr *)pInitEntry)->count;
}

StucErr stucInPieceArrSplit(
	MapToMeshBasic *pBasic,
	InPieceArr *pInPieces,
	InPieceArr *pInPiecesSplit,
	InPieceArr *pInPiecesSplitClip,
	SplitInPiecesAllocArr *pSplitAlloc
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	SplitInPiecesJobArgs jobArgs[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = { 0 };
	stucMakeJobArgs(
		pBasic,
		&jobCount, jobArgs, sizeof(SplitInPiecesJobArgs),
		pInPieces,
		inPiecesJobsGetRange, splitInPiecesJobInit
	);
	err = stucDoJobInParallel(
		pBasic,
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
