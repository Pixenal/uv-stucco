/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <string.h>

#include <poly_cutout.h>

#include <uv_stucco_intern.h>
#include <utils.h>

typedef enum CornerType {
	STUC_CORNER_NONE,
	STUC_CORNER_ROOT,
	STUC_CORNER_MAP,
	STUC_CORNER_IN,
	STUC_CORNER_INTERSECT
} CornerType;

typedef enum IntersectType {
	STUC_INTERSECT_TYPE_NONE,
	STUC_INTERSECT_TYPE_INTERSECT,
	STUC_INTERSECT_TYPE_ON_EDGE,
	STUC_INTERSECT_TYPE_ON_VERT
} IntersectType;

typedef struct InFaceCacheInitInfo {
	V2_I16 tile;
	bool wind;
} InFaceCacheInitInfo;

typedef struct InFaceCacheState {
	const MapToMeshBasic *pBasic;
} InFaceCacheState;

static
void inFaceCacheEntryInit(
	void *pUserData,
	PixuctHTableEntryCore *pEntryVoid,
	const void *pKeyData,
	void *pInitInfoVoid,
	I32 linAlloc
) {
	InFaceCacheState *pState = pUserData;
	InFaceCacheEntry *pEntry = (void *)pEntryVoid;
	*pEntry = (InFaceCacheEntry){
		.face = stucGetFaceRange(&pState->pBasic->pInMesh->core, *(I32 *)pKeyData),
		.idx = linAlloc
	};
}

static
void inFaceCacheEntryBb(
	const MapToMeshBasic *pBasic,
	InFaceCacheEntry *pEntry,
	I32 faceIdx,
	V2_I16 tile
) {
	PixmshV2Bb bounds = {0};
	stucGetInFaceBounds(&bounds, pBasic->pInMesh->pUvs, pEntry->face);
	V2_F32 fTile = {.d = {(F32)tile.d[0], (F32)tile.d[1]}};
	pEntry->fMin = _(bounds.min V2SUB fTile);
	pEntry->fMax = _(bounds.max V2SUB fTile);
}

static
bool inFaceCacheEntryCmp(
	const PixuctHTableEntryCore *pEntry,
	const void *pKeyData,
	const void *pDataToAdd
) {
	return ((InFaceCacheEntry *)pEntry)->face.idx == *(I32 *)pKeyData;
}

static
SearchResult inFaceCacheGet(
	const MapToMeshBasic *pBasic,
	InFaceCache *pCache,
	const InPiece *pInPiece,
	I32 face,
	bool addEntry,
	InFaceCacheEntry **ppEntry
) {
	InFaceCacheEntry *pEntry = NULL;
	pixuctHTableGet(
		&pCache->table,
		0,
		&face,
		(void **)&pEntry,
		addEntry,
		NULL,
		NULL,
		pixuctKeyFromI32, NULL, inFaceCacheEntryInit, inFaceCacheEntryCmp
	);
	if (pEntry->valid) {
		PIX_ERR_ASSERT("", pEntry->face.idx == face);
		*ppEntry = pEntry;
		return PIX_SEARCH_FOUND;
	}
	if (addEntry) {
		inFaceCacheEntryBb(pBasic, pEntry, face, pInPiece->tile);
		I32 newCount = pCache->corners.count + pEntry->face.range.size;
		PIXALC_DYN_ARR_RESIZE(&pBasic->pCtx->alloc, &pCache->corners, newCount);
		pEntry->cornersStart = pCache->corners.count;
		initHalfPlaneLookup(
			pBasic->pInMesh,
			&pEntry->face,
			pInPiece->tile,
			pCache->corners.pArr + pCache->corners.count
		);
		pCache->corners.count = newCount;
		*ppEntry = pEntry;
		return PIX_SEARCH_ADDED;
	}
	return PIX_SEARCH_NOT_FOUND;
}

static
const HalfPlane *getInCornerCache(
	const InFaceCache *pCache,
	const InFaceCacheEntry *pCacheEntry
) {
	PIX_ERR_ASSERT(
		"",
		(I32)pCacheEntry->cornersStart + pCacheEntry->face.range.size <=
		pCache->corners.count
	);
	return pCache->corners.pArr + pCacheEntry->cornersStart;
}

static
InsideStatus isPointInFaceConvex(
	bool wind,
	I32 faceSize,
	const HalfPlane *pCorners,
	V2_F32 point,
	I32 *pOnCorner
) {
	I32 onEdge[2] = {-1, -1};
	for (I32 i = 0; i < faceSize; ++i) {
		InsideStatus status = stucIsPointInHalfPlane(
			point,
			pCorners[i].uv,
			pixmV2F32LineNormal(_(pCorners[(i + 1) % faceSize].uv V2SUB pCorners[i].uv)),
			wind
		);
		if (status == STUC_INSIDE_STATUS_OUTSIDE) {
			return STUC_INSIDE_STATUS_OUTSIDE;
		}
		if (status == STUC_INSIDE_STATUS_ON_LINE) {
			PIX_ERR_ASSERT("on 3 edges?", onEdge[0] == -1 || onEdge[1] == -1);
			onEdge[onEdge[0] != -1] = i;
		}
	}
	if (onEdge[1] != -1) {
		*pOnCorner = !onEdge[0] && onEdge[1] == faceSize - 1 ? 0 : onEdge[1];
		return STUC_INSIDE_STATUS_ON_VERT;
	}
	if (onEdge[0] != -1) {
		*pOnCorner = onEdge[0];
		return STUC_INSIDE_STATUS_ON_LINE;
	}
	return STUC_INSIDE_STATUS_INSIDE;
}

static
bool isQuadConcave(
	bool wind,
	I32 faceSize,
	const HalfPlane *pCorners,
	V2_F32 point,
	I32 *pIdx
) {
	for (I32 i = 0; i < faceSize; ++i) {
		I32 iNext = (i + 1) % faceSize;
		InsideStatus status = stucIsPointInHalfPlane(
			pCorners[(i + 2) % faceSize].uv,
			pCorners[i].uv,
			pixmV2F32LineNormal(_(pCorners[iNext].uv V2SUB pCorners[i].uv)),
			wind
		);
		if (status != STUC_INSIDE_STATUS_INSIDE) {
			if (pIdx) {
				*pIdx = iNext;
			}
			return true;
		}
	}
	return false;
}

static
InsideStatus testQuadAsTri(
	I32 start,
	bool wind,
	const HalfPlane *pCorners,
	V2_F32 point,
	I32 *pOnCorner
) {
	I32 last = (start + 2) % 4;
	HalfPlane tri[3] = {
		pCorners[start],
		pCorners[(start + 1) % 4],
		pCorners[last]
	};
	I32 onCorner = 0;
	InsideStatus status = isPointInFaceConvex(wind, 3, tri, point, &onCorner);
	switch (status) {
		case STUC_INSIDE_STATUS_ON_LINE:
			if (onCorner == last) {
				return STUC_INSIDE_STATUS_INSIDE;
			}
			//v fallthrough v
		case STUC_INSIDE_STATUS_ON_VERT:
			if (pOnCorner) {
				*pOnCorner = onCorner;
			}
			//v fallthrough v
		default:
			return status;
	}
}

static
InsideStatus isPointInFace(
	bool wind,
	I32 faceSize,
	const HalfPlane *pCorners,
	V2_F32 point,
	I32 *pOnCorner
) {
	InsideStatus status = STUC_INSIDE_STATUS_NONE;
	if (faceSize >= 4) {
		PIX_ERR_ASSERT("", faceSize == 4);
		I32 corner = 0;
		if (isQuadConcave(wind, faceSize, pCorners, point, &corner)) {
			status = testQuadAsTri(corner, wind, pCorners, point, pOnCorner);
			if (status == STUC_INSIDE_STATUS_INSIDE) {
				return STUC_INSIDE_STATUS_INSIDE;
			}
			corner = (corner + 2) % faceSize;
			return testQuadAsTri(corner, wind, pCorners, point, pOnCorner);
		}
	}
	return isPointInFaceConvex(wind, faceSize, pCorners, point, pOnCorner);
}

static
InsideStatus isVertInFace(
	const MapToMeshBasic *pBasic,
	V2_F32 vert,
	I32 inFace,
	const InPiece *pInPiece,
	InFaceCache *pInFaceCache,
	InFaceCorner *pCorner,
	bool wind,
	I32 count
) {
	InFaceCacheEntry *pInFaceEntry = NULL;
	inFaceCacheGet(
		pBasic,
		pInFaceCache,
		pInPiece,
		inFace,
		true,
		&pInFaceEntry
	);
	if (!_(vert V2GREATEQL pInFaceEntry->fMin) ||
		!_(vert V2LESSEQL pInFaceEntry->fMax)
	) {
		return STUC_INSIDE_STATUS_OUTSIDE;
	}
	InsideStatus status = STUC_INSIDE_STATUS_INSIDE;
	if (count < pInPiece->inFaceCount - 1) {
		const HalfPlane *pInCornerCache = getInCornerCache(pInFaceCache, pInFaceEntry);
		status = isPointInFace(
			wind,
			pInFaceEntry->face.range.size,
			pInCornerCache,
			vert,
			&pCorner->corner
		);
		if (status == STUC_INSIDE_STATUS_OUTSIDE) {
			return status;;
		}
	}
	pCorner->pFace = pInFaceEntry;
	return status;
}

static
I32 faceIdxInPiece(const InFaceCache *pCache, I32 face) {
	const PixalcLinAlloc *pAlloc =
		pixuctHTableAllocGetConst(&pCache->table, 0);
	const InFaceCacheEntry *pEntry = pixalcLinAllocIdxConst(pAlloc, face);
	return pEntry->face.idx;
}

typedef struct InFaceIter {
	const BorderCache *pBorderCache;
	const InFaceMemArr *pInFaceArr;
	const EncasedMapFace *pEntry;
	I32 idx;
	I32 total;
	bool subIsland;
} InFaceIter;

static
void inFaceIterInit(
	const BorderCache *pBorderCache,
	const InPiece *pInPiece,
	const InFaceMemArr *pInFaceArr,
	InFaceIter *pIter
) {
	*pIter = (InFaceIter){
		.subIsland = pBorderCache->pieceIslands.count,
		.pBorderCache = pBorderCache,
		.pInFaceArr = pInFaceArr
	};
	pIter->pEntry = pIter->subIsland ? NULL : pInPiece->pList;
}

static
void inFaceIterInc(InFaceIter *pIter) {
	++pIter->total;
	++pIter->idx;
	if (pIter->subIsland) {
		return;
	}
	I32 job = pIter->pEntry->job;
	I32 faceCount = pIter->pInFaceArr->arr[job].pArr[pIter->pEntry->inFaces].count;
	if (pIter->idx < faceCount) {
		return;
	}
	pIter->idx = 0;
	pIter->pEntry = (void *)pIter->pEntry->core.pNext;
}

static
bool inFaceIterAtEnd(InFaceIter *pIter) {
	if (pIter->subIsland) {
		const StucInIsland *pSubIsland =
			pIter->pBorderCache->pieceIslands.pArr + pIter->pBorderCache->activeIsland;
		return pIter->idx >= pSubIsland->core.faces.end - pSubIsland->core.faces.start;
	}
	return !pIter->pEntry;
}

static
I32 inFaceIterGetFace(InFaceIter *pIter) {
	if (pIter->subIsland) {
		const StucInIsland *pSubIsland =
			pIter->pBorderCache->pieceIslands.pArr + pIter->pBorderCache->activeIsland;
		I32 face = pSubIsland->core.faces.start + pIter->idx;
		face = pIter->pBorderCache->pieceIslands.faces.pArr[face];
		return faceIdxInPiece(&pIter->pBorderCache->inFaceCache, face);
	}
	I32 job = pIter->pEntry->job;
	return pIter->pInFaceArr->arr[job].pArr[pIter->pEntry->inFaces].pArr[pIter->idx].idx;
}

static
InsideStatus getFaceEncasingVert(
	const MapToMeshBasic *pBasic,
	const InFaceMemArr *pInFaceArr,
	V2_F32 vert,
	const InPiece *pInPiece,
	BorderCache *pBorderCache,
	InFaceCorner *pCorner,
	bool wind
) {
	const EncasedMapFace *pEntry = pInPiece->pList;
	PIX_ERR_ASSERT("", pEntry);
	I32 count = 0;
	InFaceIter iter = {0};
	inFaceIterInit(pBorderCache, pInPiece, pInFaceArr, &iter);
	for (; !inFaceIterAtEnd(&iter); inFaceIterInc(&iter)) {
		InsideStatus status = isVertInFace(
			pBasic,
			vert,
			inFaceIterGetFace(&iter),
			pInPiece,
			&pBorderCache->inFaceCache,
			pCorner,
			wind,
			iter.total
		);
		if (status == STUC_INSIDE_STATUS_OUTSIDE) {
			continue;
		}
		return status;
	}
	return STUC_INSIDE_STATUS_OUTSIDE;
}

static
I32 bufMeshAllocInOrMapVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertInOrMapArr *pVertArr = &pBufMesh->inOrMapVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(&pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAllocOnEdgeVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOnEdgeArr *pVertArr = &pBufMesh->onEdgeVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(&pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAllocOverlapVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOverlapArr *pVertArr = &pBufMesh->overlapVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(&pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAllocIntersectVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertIntersectArr *pVertArr = &pBufMesh->intersectVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(&pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

typedef struct BorderCacheEdge {
	PixuctAvlNodeCore core;
	U32 idx : 31;
	U32 cantIntersect : 1;
} BorderCacheEdge;

static
FaceCorner getInCornerFromPlycut(
	const Mesh *pInMesh,
	const BorderCache *pBorderCache,
	const PixtyI32Arr *pOrderCache,
	I32 boundary,
	const PlycutCorner *pCorner
) {
	const StucInIsland *pPieceIsland =
		pBorderCache->pieceIslands.pArr + pBorderCache->activeIsland;
	const Border *pBorder = pPieceIsland->core.borders.pArr + boundary;
	I32 idx = (I32)pCorner->userData.clip;
	FaceCorner faceCorner = pBorder->arr.pArr[idx].corner;
	faceCorner.face = faceIdxInPiece(&pBorderCache->inFaceCache, faceCorner.face);
	return faceCorner;
}

static
PixErr logBufClipCornerAndVert(
	StucCark *pCark,
	I32 thread,
	I32 inFace,
	BufVertType type,
	I32 inst,
	I32 corner,
	I32 vert,
	PixtyV3_F32 pos
) {
	PixErr err = PIX_ERR_SUCCESS;
	CarkLog log = {0};
	err = CARK_LOG_START(*pCark, thread, STUC_STAGE_BUFMESH_INIT, 1, inst, corner, log);
	PIX_ERR_RETURN_IFNOT(err, "");
	I32 structIdx = 0;
	CarkRefOverrideArr refArr = {.count = 1};
	switch (type) {
		case STUC_BUF_VERT_INTERSECT:
			structIdx = 2;
			refArr.count = 0;
			break;
		case STUC_BUF_VERT_ON_EDGE:
			structIdx = 3;
			refArr.arr[0].structIdx = (CarkOverride){.val = structIdx, .override = true};
			break;
		case STUC_BUF_VERT_IN_OR_MAP:
			structIdx = 4;
			refArr.arr[0].structIdx = (CarkOverride){.val = structIdx, .override = true};
			break;
		case STUC_BUF_VERT_OVERLAP:
			structIdx = 5;
			refArr.arr[0].structIdx = (CarkOverride){.val = structIdx, .override = true};
			break;
		default:
			PIX_ERR_ASSERT("invalid buf-mesh vert type", false);
	}
	err = carkOutLogComp(&log, 0, &refArr, &vert);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = carkOutLogComp(&log, 1, NULL, &inFace);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = carkOutLogEnd(&log);
	PIX_ERR_RETURN_IFNOT(err, "");

	StucStage stage = STUC_STAGE_BUFMESH_INIT;
	err = CARK_LOG_START(*pCark, thread, stage, structIdx, inst, vert, log);
	PIX_ERR_RETURN_IFNOT(err, "");
	for (I32 i = 0; i < 3; ++i) {
		err = carkOutLogComp(&log, i, NULL, pos.d + i);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	err = carkOutLogEnd(&log);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

static
I32 addIntersectVert(
	JobArgs *pArgs,
	const PixtyI32Arr *pOrderCache,
	const BorderCache *pBorderCache,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner
) {
	const MapToMeshBasic *pBasic = pArgs->pShared;
	I32 vert = bufMeshAllocIntersectVert(pBasic, pBufMesh);
	const PlycutInfoIntersect *pInfo = &pCorner->info.intersect;
	FaceCorner inCorner = getInCornerFromPlycut(
		pBasic->pInMesh,
		pBorderCache,
		pOrderCache,
		pInfo->clipCorner.boundary,
		pCorner
	);
	pBufMesh->intersectVerts.pArr[vert] = (IntersectVert){
		.inFace = inCorner.face,
		.inCorner = inCorner.corner,
		.mapCorner = pInfo->subjCorner.corner,
		.tInEdge = pInfo->clipAlpha,
		.tMapEdge = pInfo->subjAlpha,
	};
	return vert;
}

static
I32 addOnInEdgeVert(
	JobArgs *pArgs,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	FaceCorner inCorner
) {
	const MapToMeshBasic *pBasic = pArgs->pShared;
	I32 vert = bufMeshAllocOnEdgeVert(pBasic, pBufMesh);
	I32 mapCorner = pCorner->info.onEdge.vertCorner.corner;
	pBufMesh->onEdgeVerts.pArr[vert].map = (EdgeMapVert) {
		.type = STUC_BUF_VERT_SUB_TYPE_EDGE_MAP,
		.mapCorner = mapCorner,
		.inCorner = inCorner.corner,
		.inFace = inCorner.face,
		.tInEdge = pCorner->info.onEdge.alpha
	};
	return vert;
}

static
I32 addOnMapEdgeVert(
	JobArgs *pArgs,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	FaceCorner inCorner
) {
	const MapToMeshBasic *pBasic = pArgs->pShared;
	I32 vert = bufMeshAllocOnEdgeVert(pBasic, pBufMesh);
	pBufMesh->onEdgeVerts.pArr[vert].in = (EdgeInVert) {
		.type = STUC_BUF_VERT_SUB_TYPE_EDGE_IN,
		.mapCorner = pCorner->info.onEdge.edgeCorner.corner,
		.inCorner = inCorner.corner,
		.inFace = inCorner.face,
		.tMapEdge = pCorner->info.onEdge.alpha
	};
	return vert;
}

static
I32 addOnVertVert(
	JobArgs *pArgs,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner,
	FaceCorner inCorner
) {
	const MapToMeshBasic *pBasic = pArgs->pShared;
	I32 vert = bufMeshAllocOverlapVert(pBasic, pBufMesh);
	pBufMesh->overlapVerts.pArr[vert] = (OverlapVert) {
		.inFace = inCorner.face,
		.inCorner = inCorner.corner, 
		.mapCorner = pCorner->info.onVert.subjCorner.corner
	};
	return vert;
}

static
bool inCornerPredicate(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	I32 boundary,
	const PlycutCorner *pCorner
) {
	//TODO
	//const BorderCacheEdge *pNode = NULL;
	//I32 idx = (I32)pCorner->userData.clip;
	return true;
}

static
void setIntersectBufVertInfo(
	JobArgs *pArgs,
	const BorderCache *pBorderCache,
	const PixtyI32Arr *pOrderCache,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	BufVertType *pType,
	I32 *pVert
) {
	const MapToMeshBasic *pBasic = pArgs->pShared;
	switch (pCorner->type) {
		case PLYCUT_INTERSECT:
			*pType = STUC_BUF_VERT_INTERSECT;
			*pVert =
				addIntersectVert(
					pArgs,
					pOrderCache,
					pBorderCache,
					pBufMesh,
					pCorner
				);
			break;
		case PLYCUT_ON_CLIP_EDGE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			FaceCorner inCorner =
				getInCornerFromPlycut(
					pBasic->pInMesh,
					pBorderCache,
					pOrderCache,
					pCorner->info.onEdge.edgeCorner.boundary,
					pCorner
				);
			*pVert = addOnInEdgeVert(pArgs, pBufMesh, pMapFace, pCorner, inCorner);
			break;
		}
		case PLYCUT_ON_SUBJECT_EDGE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			PlycutCornerIdx vertCorner = pCorner->info.onEdge.vertCorner;
			if (!pCorner->cross &&
				!inCornerPredicate(pBasic, pBorderCache, vertCorner.boundary, pCorner)
			) {
				break;
			}
			FaceCorner inCorner =
				getInCornerFromPlycut(
					pBasic->pInMesh,
					pBorderCache,
					pOrderCache,
					pCorner->info.onEdge.vertCorner.boundary,
					pCorner
				);
			*pVert = addOnMapEdgeVert(pArgs, pBufMesh, pMapFace, pCorner, inCorner);
			break;
		}
		case PLYCUT_ON_VERT: {
			*pType = STUC_BUF_VERT_OVERLAP;
			FaceCorner inCorner =
				getInCornerFromPlycut(
					pBasic->pInMesh,
					pBorderCache,
					pOrderCache,
					pCorner->info.onVert.clipCorner.boundary,
					pCorner
				);
			*pVert = addOnVertVert(pArgs, pBufMesh, pCorner, inCorner);
			break;
		}
		default:
			PIX_ERR_ASSERT("invalid intersect corner type", false);
	}
}

static
PixErr bufMeshAddFace(
	JobArgs *pArgs,
	V2_I16 tile,
	I32 cluster,
	bool wind,
	BufMesh *pBufMesh,
	I32 start,
	I32 faceSize,
	I32 mapFace
) {
	PixErr err = PIX_ERR_SUCCESS;
	I32 newIdx = -1;
	PIXALC_DYN_ARR_ADD(&pArgs->pCtx->alloc, (&pBufMesh->faces), newIdx);
	PIX_ERR_ASSERT("", newIdx != -1);
	pBufMesh->faces.pArr[newIdx].start = start;
	pBufMesh->faces.pArr[newIdx].size = faceSize;
	pBufMesh->faces.pArr[newIdx].mapFace = mapFace;
	pBufMesh->faces.pArr[newIdx].tile = tile;
	pBufMesh->faces.pArr[newIdx].wind = wind;

	if (!pArgs->pCark->valid) {
		return err;
	}
	StucStage stage = STUC_STAGE_BUFMESH_INIT;
	CarkLog log = {0};
	err = CARK_LOG_START(*pArgs->pCark, pArgs->threadId, stage, 0, pArgs->logInst, newIdx, log);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = carkOutLogComp(&log, 0, NULL, &start);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = carkOutLogComp(&log, 1, NULL, &faceSize);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = carkOutLogComp(&log, 2, NULL, &cluster);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = carkOutLogEnd(&log);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

static
I32 addInsideMapVert(
	JobArgs *pArgs,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	I32 inFace
) {
	const MapToMeshBasic *pBasic = pArgs->pShared;
	I32 vert = bufMeshAllocInOrMapVert(pBasic, pBufMesh);
	pBufMesh->inOrMapVerts.pArr[vert].map = (MapVert){
		.type = STUC_BUF_VERT_SUB_TYPE_MAP,
		.mapCorner = pCorner->info.origin.corner.corner,
		.inFace = inFace
	};
	return vert;
}

static
I32 addInVert(
	JobArgs *pArgs,
	BufMesh *pBufMesh,
	FaceCorner inCorner
) {
	const MapToMeshBasic *pBasic = pArgs->pShared;
	I32 vert = bufMeshAllocInOrMapVert(pBasic, pBufMesh);
	pBufMesh->inOrMapVerts.pArr[vert].in = (InVert){
		.type = STUC_BUF_VERT_SUB_TYPE_IN,
		.inCorner = inCorner.corner,
		.inFace = inCorner.face
	};
	return vert;
}

static
InsideStatus findEncasingInPieceFace(
	const MapToMeshBasic *pBasic,
	const InFaceMemArr *pInFaceArr,
	const InPiece *pInPiece,
	BorderCache *pBorderCache,
	const FaceRange *pMapFace,
	I32 mapCorner,
	FaceCorner *pCorner,
	F32 *pAlpha,
	bool wind
) {
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	V2_F32 pos = *(V2_F32 *)&pMapMesh->pPos[
		pMapMesh->core.pCorners[pMapFace->range.start + mapCorner]
	];
	InFaceCorner inCorner = {0};
	InsideStatus status = getFaceEncasingVert(
		pBasic,
		pInFaceArr,
		pos,
		pInPiece,
		pBorderCache,
		&inCorner,
		wind
	);
	if (status == STUC_INSIDE_STATUS_ON_LINE) {
		const HalfPlane *pInCornerCache =
			getInCornerCache(&pBorderCache->inFaceCache, inCorner.pFace);
		I32 corner = inCorner.corner;
		V2_F32 uv = pInCornerCache[corner].uv;
		I32 cornerNext = stucGetCornerNext(corner, &inCorner.pFace->face);
		V2_F32 uvNext = pInCornerCache[cornerNext].uv;
		V2_F32 dirUnit = _(_(uvNext V2SUB uv) V2DIVS pInCornerCache[corner].len);
		*pAlpha = stucGetT(pos, uv, dirUnit, pInCornerCache[corner].len);
	}
	if (!inCorner.pFace) {
		PIX_ERR_ASSERT("", status == STUC_INSIDE_STATUS_OUTSIDE);
		return status;
	}
	*pCorner = (FaceCorner){.face = inCorner.pFace->face.idx, .corner = inCorner.corner};
	return status;
}

static
I32 addMapVert(
	JobArgs *pArgs,
	const InFaceMemArr *pInFaceArr,
	BorderCache *pBorderCache,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	I32 mapCorner,
	BufVertType *pType,
	bool wind,
	I32 *pInFace
) {
	const MapToMeshBasic *pBasic = pArgs->pShared;
	FaceCorner corner = {0};
	F32 alpha = .0f;
	InsideStatus status = findEncasingInPieceFace(
		pBasic,
		pInFaceArr,
		pInPiece,
		pBorderCache,
		pMapFace,
		mapCorner,
		&corner,
		&alpha,
		wind
	);
	if (status == STUC_INSIDE_STATUS_OUTSIDE) {
		return -1;
	}
	if (pInFace) {
		*pInFace = corner.face;
	}
	I32 vert = 0;
	switch (status) {
		case STUC_INSIDE_STATUS_INSIDE: {
			*pType = STUC_BUF_VERT_IN_OR_MAP;
			PlycutCorner fake = {
				.info.origin.corner.corner = mapCorner,
			};
			vert = addInsideMapVert(pArgs, pBufMesh, pMapFace, &fake, corner.face);
			break;
		}
		case STUC_INSIDE_STATUS_ON_LINE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			PlycutCorner fake = {
				.info.onEdge.alpha = alpha,
				.info.onEdge.vertCorner.corner = mapCorner,
			};
			vert = addOnInEdgeVert(pArgs, pBufMesh, pMapFace, &fake, corner);
			break;
		}
		case STUC_INSIDE_STATUS_ON_VERT: {
			*pType = STUC_BUF_VERT_OVERLAP;
			PlycutCorner fake = {
				.info.onVert.subjCorner.corner = mapCorner
			};
			vert = addOnVertVert(pArgs, pBufMesh, &fake, corner);
			break;
		}
		default:
			PIX_ERR_ASSERT("invalid inside status", false);
	}
	return vert;
}

static
PixErr bufMeshAddCorner(
	JobArgs *pArgs,
	BufMesh *pBufMesh,
	BufVertType type,
	I32 vert,
	I32 *pBufCorner
) {
	PixErr err = PIX_ERR_SUCCESS;
	const MapToMeshBasic *pBasic = pArgs->pShared;
	BufCornerArr *pCorners = &pBufMesh->corners;
	I32 newCorner = -1;
	PIXALC_DYN_ARR_ADD(&pBasic->pCtx->alloc, pCorners, newCorner);
	PIX_ERR_ASSERT("", newCorner >= 0);
	pCorners->pArr[newCorner].type = type;
	pCorners->pArr[newCorner].vert = vert;
	*pBufCorner = newCorner;
	return err;
}

static
StucErr bufMeshAddVert(
	JobArgs *pArgs,
	const InFaceMemArr *pInFaceArr,
	const InPiece *pInPiece,
	BorderCache *pBorderCache,
	const PixtyI32Arr *pOrderCache,
	I32 inPieceOffset,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	BufMesh *pBufMesh,
	bool wind
) {
	StucErr err = PIX_ERR_SUCCESS;
	const MapToMeshBasic *pBasic = pArgs->pShared;
	BufVertType type = 0;
	I32 vert = -1;
	I32 inFace = -1;//passed back here for logging corner
	switch (pCorner->type) {
		case PLYCUT_ORIGIN_SUBJECT:
			vert = addMapVert(
				pArgs,
				pInFaceArr,
				pBorderCache,
				pInPiece,
				pBufMesh,
				pMapFace, pCorner->info.origin.corner.corner,
				&type,
				wind,
				&inFace
			);
			PIX_ERR_RETURN_QUIET_IFNOT_COND(
				err,
				vert != -1,
				"an exterior map corner shouldn't have been passed to this func"
			);
			break;
		case PLYCUT_ORIGIN_CLIP: {
			type = STUC_BUF_VERT_IN_OR_MAP;
			if (!inCornerPredicate(
				pBasic,
				pBorderCache,
				pCorner->info.origin.corner.boundary,
				pCorner
			)) {
				break;
			}
			FaceCorner inCorner =
				getInCornerFromPlycut(
					pBasic->pInMesh,
					pBorderCache,
					pOrderCache,
					pCorner->info.origin.corner.boundary,
					pCorner
				);
			vert = addInVert(pArgs, pBufMesh, inCorner);
			break;
		}
		default:
			setIntersectBufVertInfo(
				pArgs,
				pBorderCache,
				pOrderCache,
				pBufMesh,
				pMapFace,
				pCorner,
				&type, &vert
			);
	}
	if (vert != -1) {
		I32 bufCorner = 0;
		bufMeshAddCorner(pArgs, pBufMesh, type, vert, &bufCorner);
		if (pArgs->pCark->valid) {
			err = logBufClipCornerAndVert(
				pArgs->pCark,
				pArgs->threadId,
				inFace,
				type,
				pArgs->logInst,
				bufCorner,
				vert,
				pCorner->pos
			);
			PIX_ERR_RETURN_IFNOT(err, "");
		}
	}
	return err;
}

static
StucErr addFaceToBufMesh(
	JobArgs *pArgs,
	const InFaceMemArr *pInFaceArr,
	const InPiece *pInPiece,
	BorderCache *pBorderCache,
	const PixtyI32Arr *pOrderCache,
	I32 inPieceOffset,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutFaceRoot *pFace,
	bool wind
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 faceStart = pBufMesh->corners.count;
	const PlycutCorner *pCorner = pFace->pRoot;
	I32 i = 0;
	do {
		PIX_ERR_RETURN_IFNOT_COND(err, i < pFace->size, "infinite or astray loop");
		err = bufMeshAddVert(
			pArgs,
			pInFaceArr,
			pInPiece,
			pBorderCache,
			pOrderCache,
			inPieceOffset,
			pMapFace,
			pCorner,
			pBufMesh,
			wind
		);
		PIX_ERR_RETURN_IFNOT(err, "");
	} while(++i, pCorner = pCorner->pNext, pCorner);
	I32 faceSize = pBufMesh->corners.count - faceStart;
	err = bufMeshAddFace(
		pArgs,
		pInPiece->tile,
		pInPiece->pList->cluster,
		wind,
		pBufMesh,
		faceStart,
		faceSize,
		pMapFace->idx
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

static
void addFacesToBufMesh(
	JobArgs *pArgs,
	const InFaceMemArr *pInFaceArr,
	BorderCache *pBorderCache,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	const PixtyI32Arr *pOrderCache,
	const FaceRange *pMapFace,
	const PlycutFaceArr *pFaces,
	bool wind
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("", pFaces->count);
	for (I32 i = 0; i < pFaces->count; ++i) {
		if (pFaces->pArr[i].isHole) {
			continue; //not adding holes
		}
		err = addFaceToBufMesh(
			pArgs,
			pInFaceArr,
			pInPiece,
			pBorderCache,
			pOrderCache,
			inPieceOffset,
			pBufMesh,
			pMapFace,
			pFaces->pArr + i,
			wind
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		PIX_ERR_CATCH(0, err, ;
			err = PIX_ERR_SUCCESS; //reset err (skipping this face)
		);
	}
}

static
StucErr inFaceCacheBuild(
	InFaceCache *pCache,
	const InFaceMemArr *pInFaceArr,
	const InPiece *pInPiece
) {
	StucErr err = PIX_ERR_SUCCESS;
	pCache->corners.count = 0;
	EncasedMapFace *pEntry = pInPiece->pList;
	PIX_ERR_ASSERT("", pEntry);
	do {
		const InFaceIdxArr *pInFaces =
			pInFaceArr->arr[pEntry->job].pArr + pEntry->inFaces;
		for (I32 i = 0; i < pInFaces->count; ++i) {
			I32 faceIdx = (I32)pInFaces->pArr[i].idx;
			InFaceCacheEntry *pFaceEntry = NULL;
			SearchResult result = pixuctHTableGet(
				&pCache->table,
				0,
				&faceIdx,
				(void **)&pFaceEntry,
				true,
				NULL,
				NULL,
				pixuctKeyFromI32, NULL, inFaceCacheEntryInit, inFaceCacheEntryCmp
			);
			PIX_ERR_ASSERT("", result == PIX_SEARCH_ADDED);
		}
	} while((pEntry = (void *)pEntry->core.pNext));
	return err;
}

static
PixtyV3_F32 mapPosGet(const StucMap *pMap, const FaceRange *pMapFace, I32 corner) {
	I32 vert = pMap->pMesh->core.pCorners[pMapFace->range.start + corner];
	return pMap->pMesh->pPos[vert];
}

static
StucErr addNonClipInPieceToBufMesh(
	JobArgs *pArgs,
	const InFaceMemArr *pInFaceArr,
	const FaceRange *pMapFace,
	BorderCache *pBorderCache,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	bool wind
) {
	StucErr err = PIX_ERR_SUCCESS;
	const MapToMeshBasic *pBasic = pArgs->pShared;
	I32 bufFaceStart = pBufMesh->corners.count;
	I32 inFace = -1;//passed back here for logging corner
	for (I32 i = 0; i < pMapFace->range.size; ++i) {	
		BufVertType type = 0;
		I32 vert = 0;
		vert = addMapVert(
			pArgs,
			pInFaceArr,
			pBorderCache,
			pInPiece,
			pBufMesh,
			pMapFace,
			i,
			&type,
			wind,
			&inFace
		);
		if (vert != -1) {
			I32 bufCorner = 0;
			bufMeshAddCorner(pArgs, pBufMesh, type, vert, &bufCorner);
			if (pArgs->pCark->valid) {
				err = logBufClipCornerAndVert(
					pArgs->pCark,
					pArgs->threadId,
					inFace,
					type,
					pArgs->logInst,
					bufCorner,
					vert,
					mapPosGet(pBasic->pMap, pMapFace, i)
				);
				PIX_ERR_RETURN_IFNOT(err, "");
			}
			continue;
		}
		PIX_ERR_RETURN_QUIET_IFNOT_COND(
			err,
			!i,
			"non-clipped map faces must be fully in or out"
		);
		return err;
	}
	err = bufMeshAddFace(
		pArgs,
		pInPiece->tile,
		pInPiece->pList->cluster,
		wind,
		pBufMesh,
		bufFaceStart,
		pMapFace->range.size,
		pMapFace->idx
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

static
void borderCacheDestroy(const StucAlloc *pAlloc, BorderCache *pCache) {
	for (I32 i = 0; i < pCache->arr.size; ++i) {
		PIXALC_DYN_ARR_DESTROY(pAlloc, pCache->ordered.pArr + i);
	}
	PIXALC_DYN_ARR_DESTROY(pAlloc, &pCache->arr);
	PIXALC_DYN_ARR_DESTROY(pAlloc, &pCache->ordered);
	if (pCache->alloc.valid) {
		pixalcLinAllocDestroy(&pCache->alloc);
	}
	PIXALC_DYN_ARR_DESTROY(pAlloc, &pCache->inFaceCache.corners);
	PIXALC_DYN_ARR_DESTROY(pAlloc, &pCache->borderSizes);
	*pCache = (BorderCache){0};
}

static
V2_F32 getBorderCornerPos(
	const void *pUserData,
	void *pMesh,
	PlycutInput input,
	I32 boundary,
	I32 corner,
	bool *pCantIntersect,
	U32 *pCornerUserData
) {
	const MapToMeshBasic *pBasic = pUserData;
	BorderCache *pCache = pMesh;
	const StucInIsland *pPieceIsland = input.pUserData;
	FaceCorner faceCorner = pPieceIsland->core.borders.pArr[boundary].arr.pArr[corner].corner;
	faceCorner.face = faceIdxInPiece(&pCache->inFaceCache, faceCorner.face);
	FaceRange face = stucGetFaceRange(&pBasic->pInMesh->core, faceCorner.face);
	*pCornerUserData = (U32)corner;
	V2_F32 pos = stucGetUvPos(
		pBasic->pInMesh,
		face.range,
		faceCorner.corner
	);
	V2_I16 tile = pCache->pInPiece->pList->tile;
	V2_F32 fTile = {.d = {(F32)tile.d[0], (F32)tile.d[1]}};
	return _(pos V2SUB fTile);
}

static
V3_F32 getMapCornerPos(
	const void *pUserData,
	void *pMeshVoid,
	PlycutInput input,
	I32 boundary,
	I32 corner,
	bool *pCantIntersect,
	U32 *pCornerUserData
) {
	const Mesh *pMesh = ((MapToMeshBasic *)pUserData)->pMap->pMesh;
	const FaceRange *pMapFace = input.pUserData;
	return stucGetVertPos(pMesh, pMapFace->range, corner);
}

static
StucErr inPieceGetFaces(
	const StucMap *pMap,
	const InPiece *pInPiece,
	ClutreFaceRange *pMapFaces
) {
	StucErr err = PIX_ERR_SUCCESS;
	const ClutreNode *pCluster = NULL;
	err = clutreIdx(&pMap->clustTree, pInPiece->pList->cluster, &pCluster);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = clutreFacesGet(&pMap->clustTree, pCluster, pMapFaces);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

typedef struct PieceMesh {
	const Mesh *pMesh;
	const InPiece *pInPiece;
	const InFaceMemArr *pInFaceArr;
	InFaceCache *pInFaceCache;
} PieceMesh;

static
I32 getEdge(const void *pMeshRaw, FaceCorner corner) {
	corner.face = faceIdxInPiece(((PieceMesh *)pMeshRaw)->pInFaceCache, corner.face);
	return stucGetMeshEdge(&((PieceMesh *)pMeshRaw)->pMesh->core, corner);
}

static
void validateFaceInPiece(InFaceCache *pInFaceCache, FaceCorner *pCorner) {
	if (pCorner->face == -1) {
		return;
	}
	InFaceCacheEntry *pEntry = NULL;
	SearchResult result = pixuctHTableGet(
		&pInFaceCache->table,
		0,
		&pCorner->face,
		(void **)&pEntry,
		false,
		NULL,
		NULL,
		pixuctKeyFromI32, NULL, NULL, inFaceCacheEntryCmp
	);
	if (result == PIX_SEARCH_NOT_FOUND) {
		*pCorner = (FaceCorner){.face = -1, .corner = -1};
	}
	else {
		PIX_ERR_ASSERT("", pEntry->face.idx == pCorner->face);
		pCorner->face = pEntry->idx;
	}
}

static
PixmshEdgeCorners getEdgeCorners(const void *pMeshRaw, I32 edge) {
	const PieceMesh *pMesh = pMeshRaw;
	PIX_ERR_ASSERT("", edge >= 0 && edge < pMesh->pMesh->core.edgeCount);
	PixmshEdgeCorners corners = {.corners = {
		{.face = pMesh->pMesh->pEdgeFaces[edge].d[0], },
		{.face = pMesh->pMesh->pEdgeFaces[edge].d[1], }
	}};
	for (I32 i = 0; i < 2; ++i) {
		validateFaceInPiece(pMesh->pInFaceCache, corners.corners + i);
		if (corners.corners[i].face != -1) {
			corners.corners[i].corner = pMesh->pMesh->pEdgeCorners[edge].d[i];
		}
	}
	return corners;
}

static
FaceCorner getAdjPieceCorner(const void *pMeshRaw, FaceCorner corner) {
	FaceCorner adj = {0};
	corner.face = faceIdxInPiece(((PieceMesh *)pMeshRaw)->pInFaceCache, corner.face);
	stucGetAdjCorner(((PieceMesh *)pMeshRaw)->pMesh, corner, &adj);
	validateFaceInPiece(((PieceMesh *)pMeshRaw)->pInFaceCache, &adj);
	return adj;
}

static
PixtyV2_F32 stucPieceUv(const void *pMeshRaw, I32 corner) {
	const Mesh *pMesh = ((PieceMesh *)pMeshRaw)->pMesh;
	PIX_ERR_ASSERT("", pMesh->pUvs && corner >= 0 && corner < pMesh->core.cornerCount);
	return pMesh->pUvs[corner];
}

static
PixmshFaceRange pieceFaceRange(const void *pMeshRaw, I32 face) {
	const PieceMesh *pMesh = pMeshRaw;
	face = faceIdxInPiece(pMesh->pInFaceCache, face);
	PIX_ERR_ASSERT("", face >= 0 && face < pMesh->pMesh->core.faceCount);
	I32 start = pMesh->pMesh->core.pFaces[face];
	return (PixmshFaceRange) {
		.start = start,
		.size = pMesh->pMesh->core.pFaces[face + 1] - start
	};
}

static
StucErr inPieceSplit(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	BorderCache *pBorderCache,
	bool makeBorders
) {
	StucErr err = PIX_ERR_SUCCESS;
	pBorderCache->pieceIslands.count = 0;
	pBorderCache->pieceIslands.faceCount = 0;
	PixmshSplitIntfOut splitIslands = {
		.pUserData = &pBorderCache->pieceIslands,
		.fpBorderInit = stucInIslandBorderInit,
		.fpBorderAddEdge = stucInIslandBorderAddEdge,
		.fpFacesInit = stucInIslandFacesInit,
		.fpIslandAdd = stucInIslandAdd,
		.fpRangeSet = stucInIslandRangeSet,
		.fpBorderMarkAsOuter = stucInIslandBorderMarkAsOuter
	};
	PieceMesh pieceMesh = {
		.pMesh = pBasic->pInMesh,
		.pInFaceCache = &pBorderCache->inFaceCache
	};
	PixmshSplitIntfIn splitMesh = {
		.pUserData = &pieceMesh,
		.faceCount = pInPiece->inFaceCount,
		.fpFaceRange = pieceFaceRange,
		.fpEdge = getEdge,
		.fpPos = stucPieceUv,
		.fpEdgeCorners = getEdgeCorners,
		.fpAdjCorner = getAdjPieceCorner
	};
	err = pixmshSplitToIslands(
		&pBasic->pCtx->alloc,
		pBorderCache->pSplitMem,
		&splitMesh,
		&splitIslands,
		makeBorders,
		NULL
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

StucErr stucClipMapFace(
	JobArgs *pArgs,
	const InFaceMemArr *pInFaceArr,
	const IslandClustArr *pClustArr,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache,
	void *pPlycutAlc,
	void *pClipOut,
	PixtyI32Arr *pOrderCache
) {
	StucErr err = PIX_ERR_SUCCESS;
	const MapToMeshBasic *pBasic = pArgs->pShared;

	err = inPieceSplit(pBasic, pInPiece, pBorderCache, true);
	PIX_ERR_RETURN_IFNOT(err, "");
	ClutreFaceRange mapFaces = {0};
	err = inPieceGetFaces(pBasic->pMap, pInPiece, &mapFaces);
	for (I32 i = 0; i < pBorderCache->pieceIslands.count; ++i) {
		pBorderCache->activeIsland = i;
		StucInIsland *pPieceIsland = pBorderCache->pieceIslands.pArr + i;
		I32 borderCount = pPieceIsland->core.borders.count;
		PIXALC_DYN_ARR_RESIZE(
			&pBasic->pCtx->alloc,
			&pBorderCache->borderSizes,
			borderCount
		);
		for (I32 j = 0; j < pPieceIsland->core.borders.count; ++j) {
			pBorderCache->borderSizes.pArr[j] =
				pPieceIsland->core.borders.pArr[j].arr.count;
		}
		PlycutInput inInput = {
			.pUserData = pPieceIsland,
			.boundaries = borderCount,
			.pSizes = pBorderCache->borderSizes.pArr
		};
		for (I32 j = 0; j < mapFaces.size; ++j) {
			FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaces.pArr[j]);
			PlycutInput mapInput = {
				.pSizes = &mapFace.range.size,
				.boundaries = 1,
				.pUserData = &mapFace
			};
			pBorderCache->activeBorder = -1;
			err = plycutClip(
				&pBasic->pCtx->alloc,
				pBasic,
				pBorderCache, inInput, getBorderCornerPos,
				NULL, mapInput, getMapCornerPos,
				pClipOut,
				NULL,
				pPlycutAlc
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			if (((PlycutFaceArr *)pClipOut)->count) {
				addFacesToBufMesh(
					pArgs,
					pInFaceArr,
					pBorderCache,
					inPieceOffset,
					pInPiece,
					pBufMesh,
					pOrderCache,
					&mapFace,
					pClipOut,
					pClustArr->pIsland->wind
				);
			}
			PIX_ERR_CATCH(0, err, 
				err = PIX_ERR_SUCCESS; //skipping this face, reset err
			);
			plycutFaceArrClear(pClipOut);
		}
	}
	return err;
}

StucErr stucAddMapFaceToBufMesh(
	JobArgs *pArgs,
	const InFaceMemArr *pInFaceArr,
	const IslandClustArr *pClustArr,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache,
	void *pPlycutAlc, //unused, needed for function callback
	void *pClipOut, // same as ^
	PixtyI32Arr *pOrderCache //same as ^
) {
	StucErr err = PIX_ERR_SUCCESS;
	const MapToMeshBasic *pBasic = pArgs->pShared;
	
	err = inPieceSplit(pBasic, pInPiece, pBorderCache, false);
	PIX_ERR_RETURN_IFNOT(err, "");
	ClutreFaceRange mapFaces = {0};
	err = inPieceGetFaces(pBasic->pMap, pInPiece, &mapFaces);
	for (I32 i = 0; i < pBorderCache->pieceIslands.count; ++i) {
		pBorderCache->activeIsland = i;
		for (I32 j = 0; j < mapFaces.size; ++j) {
			FaceRange mapFace =
				stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaces.pArr[j]);
			err = addNonClipInPieceToBufMesh(
				pArgs,
				pInFaceArr,
				&mapFace,
				pBorderCache,
				inPieceOffset,
				pInPiece,
				pBufMesh,
				pClustArr->pIsland->wind
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}
	}
	PIX_ERR_CATCH(0, err,
		err = PIX_ERR_SUCCESS; //reset err (skipping face)
	);
	return err;
}

typedef struct BufMeshInitJobArgs {
	JobArgs core;
	const IslandClustArr *pClustArr;
	const InFaceMemArr *pInFaceArr;
	StucErr (* fpAddPiece)(
		JobArgs *,
		const InFaceMemArr *,
		const IslandClustArr *,
		I32,
		const InPiece *,
		BufMesh *,
		BorderCache *,
		void *,
		void *,
		PixtyI32Arr *
	);
	const InPieceArr *pInPiecesSplit;
	BufMesh bufMesh;
	JobArgsFoot foot;
} BufMeshInitJobArgs;

StucErr stucBufMeshInit(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	BufMeshInitJobArgs *pArgs = pArgsVoid;

	if (pArgs->core.pCark->valid) {
		//init new log instance for bufmesh
		stucLogStageInstAdd(&pArgs->core, STUC_STAGE_BUFMESH_INIT);
	}

	PixmshSplitMem splitMem = {0};
	BorderCache borderCache = {.pSplitMem = &splitMem};

	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	const MapToMeshBasic *pBasic = (const MapToMeshBasic *)pArgs->core.pShared;
	PixuctHTableMem hTableAlc = {0};
	PlycutMem plycutAlc = {0};
	PixtyI32Arr orderCache = {0};
	PlycutFaceArr clipOut = {0};
	for (I32 i = 0; i < rangeSize; ++i) {
		I32 inPieceIdx = pArgs->core.range.start + i;
		const InPiece *pInPiece = pArgs->pInPiecesSplit->pArr + inPieceIdx;

		borderCache.inFaceCache.corners.count = 0;
		InFaceCacheState inFaceCacheState = {.pBasic = pBasic};
		pixuctHTableInit(
			&pBasic->pCtx->alloc,
			&borderCache.inFaceCache.table,
			pInPiece->inFaceCount / 2 + 1,
			(I32Arr) {
				.pArr = (I32[]){sizeof(InFaceCacheEntry)},
				.count = 1
			},
			&hTableAlc,
			&inFaceCacheState,
			false
		);
		err = inFaceCacheBuild(&borderCache.inFaceCache, pArgs->pInFaceArr, pInPiece);
		PIX_ERR_RETURN_IFNOT(err, "");
		borderCache.pInPiece = pInPiece;
		pArgs->fpAddPiece(
			&pArgs->core,
			pArgs->pInFaceArr,
			pArgs->pClustArr,
			inPieceIdx,
			pInPiece,
			&pArgs->bufMesh,
			&borderCache,
			&plycutAlc,
			&clipOut,
			&orderCache
		);
		pixuctHTableDestroy(&borderCache.inFaceCache.table);
		pixuctHTableMemClear(&borderCache.pieceIslands.tableMem);
	}
	plycutFaceArrDestroy(&pBasic->pCtx->alloc, &clipOut);
	pixmshSplitMemDestroy(&pBasic->pCtx->alloc, &splitMem);
	stucInIslandsDestroy(pBasic->pCtx, &borderCache.pieceIslands);
	pixuctHTableMemDestroy(&pBasic->pCtx->alloc, &hTableAlc);
	plycutMemDestroy(&pBasic->pCtx->alloc, &plycutAlc);
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	borderCacheDestroy(pAlloc, &borderCache);
	PIXALC_DYN_ARR_DESTROY(pAlloc, &orderCache);
	return err;
}


SrcFaces stucGetSrcFacesForBufCorner(
	const BufMesh *pBufMesh,
	FaceCorner corner
) {
	BufFace bufFace = pBufMesh->faces.pArr[corner.face];
	BufCorner bufCorner = pBufMesh->corners.pArr[bufFace.start + corner.corner];
	SrcFaces faces = {.map = bufFace.mapFace};
	switch (bufCorner.type) {
		case STUC_BUF_VERT_IN_OR_MAP: {
			const InOrMapVert *pVert = pBufMesh->inOrMapVerts.pArr + bufCorner.vert;
			switch (pVert->in.type) {
				case STUC_BUF_VERT_SUB_TYPE_IN:
					faces.in = pVert->in.inFace;
					break;
				case STUC_BUF_VERT_SUB_TYPE_MAP:
					faces.in = pVert->map.inFace;
					break;
				default:
					PIX_ERR_ASSERT("invalid in-or-map buf vert sub-type", false);
			}
			break;
		}
		case STUC_BUF_VERT_ON_EDGE: {
			const BufVertOnEdge *pVert = pBufMesh->onEdgeVerts.pArr + bufCorner.vert;
			switch (pVert->in.type) {
				case STUC_BUF_VERT_SUB_TYPE_EDGE_IN:
					faces.in = pVert->in.inFace;
					break;
				case STUC_BUF_VERT_SUB_TYPE_EDGE_MAP:
					faces.in = pVert->map.inFace;
					break;
				default:
					PIX_ERR_ASSERT("invalid on-edge buf vert sub-type", false);
			}
			break;
		}
		case STUC_BUF_VERT_OVERLAP: {
			const OverlapVert *pVert = pBufMesh->overlapVerts.pArr + bufCorner.vert;
			faces.in = pVert->inFace;
			break;
		}
		case STUC_BUF_VERT_INTERSECT: {
			const IntersectVert *pVert = pBufMesh->intersectVerts.pArr + bufCorner.vert;
			faces.in = pVert->inFace;
			break;
		}
		default:
			PIX_ERR_ASSERT("invalid buf vert type", false);
	}
	return faces;
}

typedef struct BufMeshJobInitInfo {
	const InFaceMemArr *pInFaceArr;
	const IslandClustArr *pClustArr;
	InPieceArr *pInPiecesSplit;
	StucErr (* fpAddPiece)(
		JobArgs *,
		const InFaceMemArr *,
		const IslandClustArr *,
		I32,
		const InPiece *,
		BufMesh *,
		BorderCache *,
		void *,
		void *,
		PixtyI32Arr *
	);
} BufMeshJobInitInfo;

static
I32 bufMeshInitJobsGetRange(const StucCtx *pCtx, const void *pShared, void *pInitInfoVoid) {
	return ((BufMeshJobInitInfo *)pInitInfoVoid)->pInPiecesSplit->count;
}

static
void bufMeshInitJobInit(
	const StucCtx *pCtx,
	const void *pShared,
	void *pInitInfoVoid,
	void *pEntryVoid
) {
	BufMeshInitJobArgs *pEntry = pEntryVoid;
	BufMeshJobInitInfo *pInitInfo = pInitInfoVoid;
	pEntry->pInPiecesSplit = pInitInfo->pInPiecesSplit;
	pEntry->fpAddPiece = pInitInfo->fpAddPiece;
	pEntry->pInFaceArr = pInitInfo->pInFaceArr;
	pEntry->pClustArr = pInitInfo->pClustArr;
}


static
void bufMeshArrMoveToInPieces(
	const PixalcFPtrs *pAlloc,
	BufMeshArr *pBufMeshArr,
	BufMeshInitJobArgs *pJobArgs,
	I32 jobCount
) {
	if (jobCount) {
		PIXALC_DYN_ARR_RESIZE(
			pAlloc,
			pBufMeshArr,
			pBufMeshArr->count + jobCount
		);
		for (I32 i = 0; i < jobCount; ++i) {
			pBufMeshArr->pArr[pBufMeshArr->count + i] = pJobArgs[i].bufMesh;
		}
		pBufMeshArr->count += jobCount;
	}
}

StucErr stucInPieceArrInitBufMeshes(
	const MapToMeshBasic *pBasic,
	I32 threadId,
	StucCark *pCark,
	const IslandClustArr *pClustArr,
	const InFaceMemArr *pInFaceArr,
	InPieceArr *pInPieces,
	StucErr (* fpAddPiece)(
		JobArgs *,
		const InFaceMemArr *,
		const IslandClustArr *,
		I32,
		const InPiece *,
		BufMesh *,
		BorderCache *,
		void *,
		void *,
		PixtyI32Arr *
	)
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	BufMeshInitJobArgs jobArgs[PIXTH_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pBasic->pCtx,
		pCark,
		pBasic,
		&jobCount, jobArgs, sizeof(BufMeshInitJobArgs),
		&(BufMeshJobInitInfo) {
			.pInFaceArr = pInFaceArr,
			.pClustArr = pClustArr,
			.pInPiecesSplit = pInPieces,
			.fpAddPiece = fpAddPiece
		},
		bufMeshInitJobsGetRange, bufMeshInitJobInit);
	err = stucDoJobInParallel(
		pBasic->pCtx,
		threadId,
		jobCount, jobArgs, sizeof(BufMeshInitJobArgs),
		stucBufMeshInit
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	bufMeshArrMoveToInPieces(
		&pBasic->pCtx->alloc,
		pInPieces->pBufMeshes,
		jobArgs, jobCount
	);
	return err;
}

void stucBufMeshArrDestroy(StucCtx *pCtx, BufMeshArr *pArr) {
	if (!pArr->pArr) {
		*pArr = (BufMeshArr){0};
		return;
	}
	for (I32 i = 0; i < pArr->count; ++i) {
		PIXALC_DYN_ARR_DESTROY(&pCtx->alloc, &pArr->pArr[i].faces);
		PIXALC_DYN_ARR_DESTROY(&pCtx->alloc, &pArr->pArr[i].corners);
		PIXALC_DYN_ARR_DESTROY(&pCtx->alloc, &pArr->pArr[i].inOrMapVerts);
		PIXALC_DYN_ARR_DESTROY(&pCtx->alloc, &pArr->pArr[i].onEdgeVerts);
		PIXALC_DYN_ARR_DESTROY(&pCtx->alloc, &pArr->pArr[i].overlapVerts);
		PIXALC_DYN_ARR_DESTROY(&pCtx->alloc, &pArr->pArr[i].intersectVerts);
	}
	PIXALC_DYN_ARR_DESTROY(&pCtx->alloc, pArr);
}

typedef struct BorderInfo {
	const Mesh *pMesh;
	const Border *pBorder;
} BorderInfo;

static
PixtyV2_F32 stucBorderPos(const void *pBorderRaw, I32 idx) {
	const BorderInfo *pInfo = pBorderRaw;
	FaceCorner corner = pInfo->pBorder->arr.pArr[idx].corner;
	FaceRange faceRange = stucGetFaceRange(&pInfo->pMesh->core, corner.face);
	PIX_ERR_ASSERT(
		"",
		pInfo->pMesh->pUvs && corner.corner >= 0 && corner.corner < faceRange.range.size
	);
	return pInfo->pMesh->pUvs[faceRange.range.start + corner.corner];
}

static
PixtyV2_F32 stucBorderPosForWind(
	const void *pMesh,
	const PixmshFaceRange face,
	int32_t corner
) {
	return stucBorderPos(pMesh, corner);
}

static
PixErr stucIslandClustAdd(
	const PixalcFPtrs *pAlloc,
	void *pArrRaw,
	int32_t idx,
	ClutreIntersect status,
	V2_I32 tile
) {
	StucErr err = PIX_ERR_SUCCESS;
	IslandClustArr *pArr = pArrRaw;
	PIX_ERR_ASSERT(
		"",
		tile.d[0] > (I32)INT16_MIN && tile.d[0] < (I32)INT16_MAX &&
		tile.d[1] > (I32)INT16_MIN && tile.d[1] < (I32)INT16_MAX
	);
	PIX_ERR_ASSERT("", status > 0 && status < 4);
	V2_I16 tile16 = {tile.d[0], tile.d[1]};
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(pAlloc, pArr, newIdx);
	pArr->pArr[newIdx] = (ClustIdx){.idx = (U32)idx, .type = (U32)status};
	if (!pArr->tiles.count ||
		!_(pArr->tiles.pArr[pArr->tiles.count - 1].tile V2I16EQL tile16)
	) {
		I32 tileIdx = 0;
		PIXALC_DYN_ARR_ADD(pAlloc, &pArr->tiles, tileIdx);
		pArr->tiles.pArr[tileIdx] = (TileRange){
			.tile = tile16,
			.range = {.start = newIdx, .end = newIdx + 1}
		};
	}
	else {
		pArr->tiles.pArr[pArr->tiles.count - 1].range.end = pArr->count;
	}
	return err;
}

static
PixErr stucIslandClustAddStart(
	const PixalcFPtrs *pAlloc,
	void *pArrRaw,
	int32_t idx,
	ClutreIntersect status,
	V2_I32 tile
) {
	StucErr err = PIX_ERR_SUCCESS;
	IslandClustArr *pArr = pArrRaw;
	PIX_ERR_ASSERT(
		"",
		tile.d[0] > (I32)INT16_MIN && tile.d[0] < (I32)INT16_MAX &&
		tile.d[1] > (I32)INT16_MIN && tile.d[1] < (I32)INT16_MAX
	);
	V2_I32 size = {
		pArr->start.end.d[0] - pArr->start.start.d[0],
		pArr->start.end.d[1] - pArr->start.start.d[1]
	};
	V2_I32 tileNorm = {
		tile.d[0] - pArr->start.start.d[0],
		tile.d[1] - pArr->start.start.d[1]
	};
	PIX_ERR_ASSERT(
		"",
		tileNorm.d[0] >= 0 && tileNorm.d[0] < size.d[0] &&
		tileNorm.d[1] >= 0 && tileNorm.d[1] < size.d[1]
	);
	I32 arrIdx = tileNorm.d[1] * size.d[0] + tileNorm.d[0];
	PIX_ERR_ASSERT("", arrIdx >= 0 && arrIdx < pArr->start.arr.size);
	pArr->start.arr.pArr[arrIdx] = (ClutreValidIdx){.idx = idx, .valid = true};
	return err;
}

static
bool bufMeshArrIsEmpty(const BufMeshArr *pBufMeshArr) {
	for (I32 i = 0; i < pBufMeshArr->count; ++i) {
		if (pBufMeshArr->pArr[i].faces.count) {
			return false;
		}
	}
	return true;
}

static
void clustArrDestroy(const PixalcFPtrs *pAlloc, IslandClustArr *pClustArr) {
	PIXALC_DYN_ARR_DESTROY(pAlloc, &pClustArr->start.arr);
	PIXALC_DYN_ARR_DESTROY(pAlloc, &pClustArr->tiles);
	PIXALC_DYN_ARR_DESTROY(pAlloc, pClustArr);
}

static
void findEncasedFacesJobArgsDestroy(
	const PixalcFPtrs *pAlloc,
	FindEncasedFacesJobArgs *pArgs,
	I32 maxJobs
) {
	for (I32 i = 0; i < maxJobs; ++i) {
		pixuctHTableMemDestroy(pAlloc, &pArgs[i].encasedFacesMem);
		InFaceMem *pInFaces = &pArgs[i].inFaces;
		for (I32 j = 0; j < pInFaces->initCount; ++j) {
			PIX_ERR_ASSERT(
				"within init-count but not initialised?",
				pInFaces->pArr[j].pArr
			);
			pAlloc->fpFree(pInFaces->pArr[j].pArr);
		}
		PIXALC_DYN_ARR_DESTROY(pAlloc, pInFaces);
	}
	pAlloc->fpFree(pArgs);
}

StucErr stucMapMeshForIsland(void *pArgsRaw) {
	StucErr err = PIX_ERR_SUCCESS;
	MapMeshForIslandJobArgs *pArgs = pArgsRaw;
	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	const MapToMeshBasic *pBasic = pArgs->core.pShared;
	const PixalcFPtrs *pAlloc = &pBasic->pCtx->alloc;
	FindEncasedFacesJobArgs *pFindEncasedJobArgs = pAlloc->fpCalloc(
		PIXTH_MAX_SUB_MAPPING_JOBS,
		sizeof(FindEncasedFacesJobArgs)
	);

	InPieceArr inPieceArr = {0};
	InPieceArr inPieceClipArr = {0};
	IslandClustArr clustArr = {0};
	for (I32 i = 0; i < rangeSize; ++i) {
		PIX_ERR_ASSERT("", i < pBasic->pInIslands->count);
		inPieceArr = (InPieceArr){
			.pArr = inPieceArr.pArr,
			.size = inPieceArr.size,
			.pBufMeshes = &pArgs->bufMeshArr
		};
		inPieceClipArr = (InPieceArr){
			.pArr = inPieceClipArr.pArr,
			.size = inPieceClipArr.size,
			.pBufMeshes = &pArgs->bufMeshClipArr
		};
		const StucInIsland *pIsland =
			pBasic->pInIslands->pArr + pArgs->core.range.start + i;
		const Border *pBorder = pIsland->core.borders.pArr + pIsland->core.borders.outer;
		BorderInfo borderInfo = {.pMesh = pBasic->pInMesh, .pBorder = pBorder};
		ClutreFace clustFace = {
			.pUserData = &borderInfo,
			.fpPos = stucBorderPos,
			.size = pBorder->arr.count
		};
		clustFace.wind = pixmshCalcFaceWind(
			(PixmshFaceRange){.start = 0, .size = pBorder->arr.count},
			&borderInfo,
			stucBorderPosForWind
		);
		PIX_ERR_RETURN_IFNOT_COND(err, clustFace.wind != 2, "island border is degenerate");
		clustArr.count = 0;
		clustArr.tiles.count = 0;
		clustArr.pIsland = NULL;
		clustArr.start.start = (V2_I32){
			(I32)floorf(pIsland->bb.min.d[0]),
			(I32)floorf(pIsland->bb.min.d[1])
		};
		clustArr.start.end = (V2_I32){
			(I32)floorf(pIsland->bb.max.d[0] + 1),
			(I32)floorf(pIsland->bb.max.d[1] + 1)
		};
		I32 arrSize = clutreStartArrSize(&clustArr.start);
		{
			I32 oldSize = clustArr.start.arr.size;
			PIXALC_DYN_ARR_RESIZE(pAlloc, &clustArr.start.arr, arrSize);
			if (oldSize < clustArr.start.arr.size) {
				memset(
					clustArr.start.arr.pArr + oldSize,
					0,
					sizeof(ClutreValidIdx) * (clustArr.start.arr.size - oldSize)
				);
			}
		}
		//TODO rename structs/ vars like this access or interface or something
		ClutreArr clustArrInfo = {
			.pUserData = &clustArr,
			.fpAdd = stucIslandClustAddStart
		};
		err = clutreSampleForFace(
			&pBasic->pMap->clustTree,
			NULL,
			&clustFace,
			&clustArrInfo,
			true
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		clustArrInfo.fpAdd = stucIslandClustAdd;
		err = clutreSampleForFace(
			&pBasic->pMap->clustTree,
			&clustArr.start,
			&clustFace,
			&clustArrInfo,
			false
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);

		clustArr.pIsland = pIsland;
		I32 findEncasedJobCount = 0;
		err = stucInPieceArrInit(
			pBasic,
			pArgs->core.threadId,
			&clustArr,
			&inPieceArr,
			&inPieceClipArr,
			&findEncasedJobCount, pFindEncasedJobArgs
		);
		//TODO return early if empty here
		if (findEncasedJobCount > pArgs->maxJobs) {
			pArgs->maxJobs = findEncasedJobCount;
		}
		PIX_ERR_THROW_IFNOT(err, "", 0);
		InFaceMemArr inFaceArr = {.count = findEncasedJobCount};
		for (I32 j = 0; j < findEncasedJobCount; ++j) {
			inFaceArr.arr[j] = pFindEncasedJobArgs[j].inFaces;
		}

		err = stucInPieceArrInitBufMeshes(
			pBasic,
			pArgs->core.threadId,
			pArgs->core.pCark,
			&clustArr,
			&inFaceArr,
			&inPieceClipArr,
			stucClipMapFace
		);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = stucInPieceArrInitBufMeshes(
			pBasic,
			pArgs->core.threadId,
			pArgs->core.pCark,
			&clustArr,
			&inFaceArr,
			&inPieceArr,
			stucAddMapFaceToBufMesh
		);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	pArgs->empty =
		bufMeshArrIsEmpty(&pArgs->bufMeshArr) &&
		bufMeshArrIsEmpty(&pArgs->bufMeshClipArr);
	PIX_ERR_CATCH(0, err, ;);
	clustArrDestroy(pAlloc, &clustArr);
	findEncasedFacesJobArgsDestroy(pAlloc, pFindEncasedJobArgs, pArgs->maxJobs);
	inPieceArrDestroy(pBasic->pCtx, &inPieceArr);
	inPieceArrDestroy(pBasic->pCtx, &inPieceClipArr);
	return err;
}
