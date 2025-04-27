/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <merge_and_snap.h>
#include <context.h>
#include <map.h>
#include <job.h>

static
I32 bufMeshArrGetVertCount(const BufMeshArr *pBufMeshes) {
	I32 total = 0;
	for (I32 i = 0; i < pBufMeshes->count; ++i) {
		total += pBufMeshes->arr[i].inOrMapVerts.count;
		total += pBufMeshes->arr[i].onEdgeVerts.count;
		total += pBufMeshes->arr[i].overlapVerts.count;
		total += pBufMeshes->arr[i].intersectVerts.count;
	}
	return total;
}

void stucVertMergeTableInit(
	const MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	HTable *pTable
) {
	I32 vertTotal = bufMeshArrGetVertCount(pInPieces->pBufMeshes);
	vertTotal += bufMeshArrGetVertCount(pInPiecesClip->pBufMeshes);
	PIX_ERR_ASSERT(
		"mapToMesh should have returned before this func if empty",
		vertTotal > 0
	);
	stucHTableInit(
		&pBasic->pCtx->alloc,
		pTable,
		vertTotal / 4 + 1,
		(I32Arr){
			.pArr = (I32[]) {sizeof(VertMerge), sizeof(VertMergeIntersect)},
			.count = 2
		},
		NULL
	);
}

static
void mergeTableEntryInit(
	void *pUserData,
	HTableEntryCore *pEntryCore,
	const void *pKeyData,
	void *pInitInfo,
	I32 linIdx
) {
	VertMerge *pEntry = (VertMerge *)pEntryCore;
	const MergeTableKey *pKey = pKeyData;
	pEntry->key = *pKey;
	pEntry->linIdx = linIdx;
	pEntry->cornerCount = 1;
	VertMergeCorner *pBufCorner = pInitInfo;
	pEntry->bufCorner = *pBufCorner;
}

static
void initInOrMapKey(
	const MapToMeshBasic *pBasic,
	I32 mapFace,
	const MergeTableInitInfoVert *pInitInfoVert,
	MergeTableKey *pKey
) {
	switch (pInitInfoVert->inOrMap.in.type) {
		case STUC_BUF_VERT_SUB_TYPE_IN: {
			pKey->type = STUC_BUF_VERT_SUB_TYPE_IN;
			const InVert *pVert = &pInitInfoVert->inOrMap.in;
			FaceCorner inCorner = {.face = pVert->inFace, .corner = pVert->inCorner};
			pKey->key.inOrMap.in.inVert =
				stucGetMeshVert(&pBasic->pInMesh->core, inCorner);
			pKey->key.inOrMap.in.mapFace = mapFace;
			break;
		}
		case STUC_BUF_VERT_SUB_TYPE_MAP: {
			pKey->type = STUC_BUF_VERT_SUB_TYPE_MAP;
			const MapVert *pVert = &pInitInfoVert->inOrMap.map;
			FaceCorner mapCorner = {.face = mapFace, .corner = pVert->mapCorner};
			pKey->key.inOrMap.map.mapVert =
				stucGetMeshVert(&pBasic->pMap->pMesh->core, mapCorner);
			pKey->key.inOrMap.map.inFace = pVert->inFace;
			break;
		}
		default:
			PIX_ERR_ASSERT("invalid in-or-map buf vert type", false);
	}
}

static
void initOnEdgeKey(
	const MapToMeshBasic *pBasic,
	I32 mapFace,
	const MergeTableInitInfoVert *pInitInfoVert,
	MergeTableKey *pKey
) {
	switch (pInitInfoVert->onEdge.in.type) {
		case STUC_BUF_VERT_SUB_TYPE_EDGE_IN: {
			pKey->type = STUC_BUF_VERT_SUB_TYPE_EDGE_IN;
			const EdgeInVert *pVert = &pInitInfoVert->onEdge.in;
			FaceCorner inCorner = {.face = pVert->inFace, .corner = pVert->inCorner};
			pKey->key.onEdge.in.inVert =
				stucGetMeshVert(&pBasic->pInMesh->core, inCorner);
			FaceCorner mapCorner = {.face = mapFace, .corner = pVert->mapCorner};
			pKey->key.onEdge.in.mapEdge =
				(U64)stucGetMeshEdge(&pBasic->pMap->pMesh->core, mapCorner);
			break;
		}
		case STUC_BUF_VERT_SUB_TYPE_EDGE_MAP: {
			pKey->type = STUC_BUF_VERT_SUB_TYPE_EDGE_MAP;
			const EdgeMapVert *pVert = &pInitInfoVert->onEdge.map;
			FaceCorner mapCorner = {.face = mapFace, .corner = pVert->mapCorner};
			pKey->key.onEdge.map.mapVert =
				stucGetMeshVert(&pBasic->pMap->pMesh->core, mapCorner);
			FaceCorner inCorner = {.face = pVert->inFace, .corner = pVert->inCorner};
			pKey->key.onEdge.map.inEdge =
				(U64)stucGetMeshEdge(&pBasic->pInMesh->core, inCorner);
			break;
		}
		default:
			PIX_ERR_ASSERT("invalid edge buf vert type", false);
	}
}

static
void mergeTableInitKey(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	BufVertType type,
	const MergeTableInitInfoVert *pInitInfoVert,
	MergeTableKey *pKey
) {
	I32 mapFace = pInPiece->pList->mapFace;
	*pKey = (MergeTableKey) {
		.type = type,
		.tile = pInPiece->pList->tile
	};
	switch (type) {
		case STUC_BUF_VERT_IN_OR_MAP:
			initInOrMapKey(pBasic, mapFace, pInitInfoVert, pKey);
			break;
		case STUC_BUF_VERT_ON_EDGE:
			initOnEdgeKey(pBasic, mapFace, pInitInfoVert, pKey);
			break;
		case STUC_BUF_VERT_OVERLAP: {
			const OverlapVert *pVert = &pInitInfoVert->overlap;
			FaceCorner inCorner = {.face = pVert->inFace, .corner = pVert->inCorner};
			FaceCorner mapCorner = {.face = mapFace, .corner = pVert->mapCorner};
			pKey->key.overlap.inVert = stucGetMeshVert(&pBasic->pInMesh->core, inCorner);
			pKey->key.overlap.mapVert = stucGetMeshVert(&pBasic->pMap->pMesh->core, mapCorner);
			break;
		}
		case STUC_BUF_VERT_INTERSECT: {
			const IntersectVert *pVert = &pInitInfoVert->intersect;
			FaceCorner inCorner = {.face = pVert->inFace, .corner = pVert->inCorner};
			FaceCorner mapCorner = {.face = mapFace, .corner = pVert->mapCorner};
			pKey->key.intersect.inEdge = stucGetMeshEdge(&pBasic->pInMesh->core, inCorner);
			pKey->key.intersect.mapEdge = stucGetMeshEdge(&pBasic->pMap->pMesh->core, mapCorner);
			break;
		}
		default:
			PIX_ERR_ASSERT("invalid vert type", false);
	}
}

static
MergeTableInitInfoVert mergeTableGetBufVert(const BufMesh *pBufMesh, FaceCorner corner) {
	BufFace bufFace = pBufMesh->faces.pArr[corner.face];
	BufCorner bufCorner = pBufMesh->corners.pArr[bufFace.start + corner.corner];
	switch (bufCorner.type) {
		case STUC_BUF_VERT_IN_OR_MAP:
			return (MergeTableInitInfoVert) {
				.inOrMap = pBufMesh->inOrMapVerts.pArr[bufCorner.vert]
			};
		case STUC_BUF_VERT_ON_EDGE:
			return (MergeTableInitInfoVert) {
				.onEdge = pBufMesh->onEdgeVerts.pArr[bufCorner.vert]
			};
		case STUC_BUF_VERT_OVERLAP:
			return (MergeTableInitInfoVert) {
				.overlap = pBufMesh->overlapVerts.pArr[bufCorner.vert]
			};
		case STUC_BUF_VERT_INTERSECT:
			return (MergeTableInitInfoVert) {
				.intersect = pBufMesh->intersectVerts.pArr[bufCorner.vert]
			};
		default:
			PIX_ERR_ASSERT("invalid buf vert type", false);
			return (MergeTableInitInfoVert) {0};
	}
}

static
void mergeTableAddVert(
	HTable *pTable,
	const MergeTableKey *pKey,
	VertMergeCorner *pInitInfo
) {
	VertMerge *pEntry = NULL;
	SearchResult result = stucHTableGet(
		pTable,
		pKey->type == STUC_BUF_VERT_INTERSECT,
		pKey,
		&pEntry,
		true, pInitInfo,
		mergeTableMakeKey, NULL, mergeTableEntryInit, mergeTableEntryCmp
	);
	PIX_ERR_ASSERT("", result == STUC_SEARCH_ADDED || result == STUC_SEARCH_FOUND);
	if (result == STUC_SEARCH_FOUND) {
		pEntry->cornerCount++;
	}
}

void stucMergeTableGetVertKey(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	MergeTableKey *pKey
) {
	MergeTableInitInfoVert vertInfo = mergeTableGetBufVert(pBufMesh, bufCorner);
	mergeTableInitKey(
		pBasic,
		pInPiece,
		bufMeshGetType(pBufMesh, bufCorner),
		&vertInfo,
		pKey
	);
}

static
void mergeTableAddVerts(
	const MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	bool clipped,
	I32 bufMeshIdx,
	FaceCorner bufCorner,
	HTable *pTable
) {
	const BufMesh *pBufMesh = pInPieces->pBufMeshes->arr + bufMeshIdx;
	const InPiece *pInPiece = bufFaceGetInPiece(pBufMesh, bufCorner.face, pInPieces);
	I32 bufCornerIdx = pBufMesh->faces.pArr[bufCorner.face].start + bufCorner.corner;
	VertMergeCorner initInfo = {
		.pBufCorner = pBufMesh->corners.pArr + bufCornerIdx,
		.corner = bufCorner,
		.bufMesh = bufMeshIdx,
		.clipped = clipped
	};
	MergeTableKey key = {0};
	stucMergeTableGetVertKey(pBasic, pInPiece, pBufMesh, bufCorner, &key);
	mergeTableAddVert(pTable, &key, &initInfo);
}

void stucMergeVerts(
	const MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	bool clipped,
	HTable *pTable
) {
	for (I32 i = 0; i < pInPieces->pBufMeshes->count; ++i) {
		const BufMesh *pBufMesh = pInPieces->pBufMeshes->arr + i;
		for (I32 j = 0; j < pBufMesh->faces.count; ++j) {
			for (I32 k = 0; k < pBufMesh->faces.pArr[j].size; ++k) {
				mergeTableAddVerts(
					pBasic,
					pInPieces,
					clipped,
					i,
					(FaceCorner) {.face = j, .corner = k},
					pTable
				);
			}
		}
	}
}

typedef struct SnapJobArgs {
	JobArgs core;
	PixalcLinAlloc *pIntersectAlloc;
	const InPieceArr *pInPieces;
	const InPieceArr *pInPiecesClip;
	I32 snappedCount;
} SnapJobArgs;

typedef struct SnapJobInitInfo {
	const InPieceArr *pInPieces;
	const InPieceArr *pInPiecesClip;
	HTable *pMergeTable;
} SnapJobInitInfo;

static
I32 snapJobsGetRange(const MapToMeshBasic *pBasic, void *pInitInfoVoid) {
	SnapJobInitInfo *pInitInfo = pInitInfoVoid;
	const PixalcLinAlloc *pIntersectAlloc = stucHTableAllocGet(pInitInfo->pMergeTable, 1);
	return pixalcLinAllocGetCount(pIntersectAlloc);
}

static
void snapJobInit(MapToMeshBasic *pBasic, void *pInitInfoVoid, void *pEntryVoid) {
	SnapJobArgs *pEntry = pEntryVoid;
	SnapJobInitInfo *pInitInfo = pInitInfoVoid;
	pEntry->pIntersectAlloc = stucHTableAllocGet(pInitInfo->pMergeTable, 1);
	pEntry->pInPieces = pInitInfo->pInPieces;
	pEntry->pInPiecesClip = pInitInfo->pInPiecesClip;
}

static
void snapIntersectVert(const SnapJobArgs *pArgs, VertMergeIntersect *pVert) {
	//snap
}

static
StucErr snapIntersectVertsInRange(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	SnapJobArgs *pArgs = pArgsVoid;
	PixalcLinAllocIter iter = {0};
	pixalcLinAllocIterInit(pArgs->pIntersectAlloc, pArgs->core.range, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		VertMergeIntersect *pVert = pixalcLinAllocGetItem(&iter);
		snapIntersectVert(pArgs, pVert);
	}
	return err;
}

StucErr stucSnapIntersectVerts(
	MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	HTable *pMergeTable,
	I32 *pSnappedVerts
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	SnapJobArgs jobArgs[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pBasic,
		&jobCount, jobArgs, sizeof(SnapJobArgs),
		&(SnapJobInitInfo) {
			.pInPieces = pInPieces,
			.pInPiecesClip = pInPiecesClip,
			.pMergeTable = pMergeTable
		},
		snapJobsGetRange, snapJobInit);
	err = stucDoJobInParallel(
		pBasic,
		jobCount, jobArgs, sizeof(SnapJobArgs),
		snapIntersectVertsInRange
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	*pSnappedVerts = 0;
	for (I32 i = 0; i < jobCount; ++i) {
		*pSnappedVerts += jobArgs[i].snappedCount;
	}
	return err;
}

