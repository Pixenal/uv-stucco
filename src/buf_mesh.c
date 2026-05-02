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
		PIXALC_DYN_ARR_RESIZE(HalfPlane, &pBasic->pCtx->alloc, &pCache->corners, newCount);
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

/*
typedef struct CheckInFaceArgs {
	const MapToMeshBasic *pBasic;
	V2_F32 vert;
	I32 inFace;
	const InPiece *pInPiece;
	InFaceCache *pInFaceCache;
	InFaceCorner *pCorner;
	bool wind;
	I32 *pCount;
} CheckInFaceArgs;

static
PixErr checkInFace(
	const PixalcFPtrs *pAlloc,
	void *pUserData,
	I32 idx,
	ClutreIntersect bbStatus,
	V2_I32 tile
) {
	CheckInFaceArgs *pArgs = pUserData;
	InsideStatus status = isVertInFace(
		pArgs->pBasic,
		pArgs->vert,
		//pass count,
		pArgs->pInPiece,
		pArgs->pInFaceCache,
		pArgs->pCorner,
		pArgs->wind,
		*pArgs->pCount
	);
	++*pArgs->pCount;
	PIX_ERR_RETURN_QUIET_IFNOT_COND(
		(StucErr){PIX_ERR_SUCCESS},
		status == STUC_INSIDE_STATUS_OUTSIDE,
		""
	);
	return PIX_ERR_SUCCESS;
}
*/

static
InsideStatus getFaceEncasingVert(
	const MapToMeshBasic *pBasic,
	const InFaceMemArr *pInFaceArr,
	V2_F32 vert,
	const InPiece *pInPiece,
	InFaceCache *pInFaceCache,
	InFaceCorner *pCorner,
	bool wind
) {
	/*
	if (pInPiece->inFaceCount > 4) {
		ClutreArr clutreArr = {
			.pUserData = NULL,
			.fpAdd = checkInFace
		};
		StucErr err = clutreSampleForPoint(&pInFaceCache->tree, NULL, vert, &clutreArr);
		//TODO return err instead of InsideStatus,
		//& replace this assert with PIX_ERR_RETURN_IFNOT
		PIX_ERR_ASSERT("", err == PIX_ERR_QUIET || err == PIX_ERR_SUCCESS);
		if (PIX_ERR_SUCCESS) {
			return STUC_INSIDE_STATUS_OUTSIDE;
		}
	}
	*/
	const EncasedMapFace *pEntry = pInPiece->pList;
	PIX_ERR_ASSERT("", pEntry);
	I32 count = 0;
	do {
		const InFaceIdxArr *pInFaces =
			pInFaceArr->arr[pEntry->job].pArr + pEntry->inFaces;
		//PixalcLinAlloc *pHalfPlaneAlc = pixuctHTableAllocGet(pInFaceCache, 1);
		for (I32 i = 0; i < pInFaces->count; ++count, ++i) {
			InsideStatus status = isVertInFace(
				pBasic,
				vert,
				pInFaces->pArr[i].idx,
				pInPiece,
				pInFaceCache,
				pCorner,
				wind,
				count
			);
			if (status == STUC_INSIDE_STATUS_OUTSIDE) {
				continue;
			}
			return status;
		}
	} while((pEntry = (void *)pEntry->core.pNext));
	return STUC_INSIDE_STATUS_OUTSIDE;
}

static
I32 bufMeshAllocInOrMapVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertInOrMapArr *pVertArr = &pBufMesh->inOrMapVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(InOrMapVert, &pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAllocOnEdgeVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOnEdgeArr *pVertArr = &pBufMesh->onEdgeVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(BufVertOnEdge, &pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAllocOverlapVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOverlapArr *pVertArr = &pBufMesh->overlapVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(OverlapVert, &pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAllocIntersectVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertIntersectArr *pVertArr = &pBufMesh->intersectVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(IntersectVert, &pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

typedef struct BorderCacheEdge {
	PixuctAvlNodeCore core;
	U32 idx : 31;
	U32 cantIntersect : 1;
} BorderCacheEdge;

/*
static
I32 bstIdxCmp(const PixuctAvlNodeCore *pNode, const void *pKey) {
	I32 idx = ((BorderCacheEdge *)pNode)->idx;
	return idx == *(I32 * )pKey ? 2 : idx > *(I32 *)pKey;
}

static
bool bstIdxCmpEql(const PixuctAvlNodeCore *pNode, const void *pKey) {
	I32 idx = ((BorderCacheEdge *)pNode)->idx;
	return idx == *(I32 *)pKey;
}
*/

static
I32 faceIdxInPiece(const InFaceCache *pCache, I32 face) {
	/*
	I32 count = 0;
	EncasedMapFace *pEntry = pMesh->pInPiece->pList;
	PIX_ERR_ASSERT("", pEntry);
	do {
		PIX_ERR_ASSERT("", face >= count);
		const InFaceIdxArr *pInFaces =
			pMesh->pInFaceArr->arr[pEntry->job].pArr + pEntry->inFaces;
		if (face < count + pInFaces->count) {
			I32 idx = face - count;
			PIX_ERR_ASSERT("", idx >= 0 && idx < pInFaces->count);
			return (I32)pInFaces->pArr[face - count].idx;
		}
		count += pInFaces->count;
	} while(pEntry = pEntry->core.pNext);
	PIX_ERR_ASSERT("", false);
	return 0;
	*/
	const PixalcLinAlloc *pAlloc =
		pixuctHTableAllocGetConst(&pCache->table, 0);
	const InFaceCacheEntry *pEntry = pixalcLinAllocIdxConst(pAlloc, face);
	return pEntry->face.idx;
}

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
	//InFaceIdx faceIdx = {.idx = faceCorner.face};
	//InFaceCacheEntry *pEntry;
	//inFaceCacheGet(pInFaceCache, pBorderCache->pInPiece, faceIdx, true, &pEntry);
	//return (InFaceCorner){.corner = faceCorner.corner, .pFace = pEntry};
}

static
I32 addIntersectVert(
	const MapToMeshBasic *pBasic,
	const PixtyI32Arr *pOrderCache,
	const BorderCache *pBorderCache,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner
) {
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
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner,
	FaceCorner inCorner
) {
	I32 vert = bufMeshAllocOnEdgeVert(pBasic, pBufMesh);
	pBufMesh->onEdgeVerts.pArr[vert].map = (EdgeMapVert) {
		.type = STUC_BUF_VERT_SUB_TYPE_EDGE_MAP,
		.mapCorner = pCorner->info.onEdge.vertCorner.corner,
		.inCorner = inCorner.corner,
		.inFace = inCorner.face,
		.tInEdge = pCorner->info.onEdge.alpha
	};
	return vert;
}

static
I32 addOnMapEdgeVert(
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner,
	FaceCorner inCorner
) {
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
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner,
	FaceCorner inCorner
) {
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
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	const PixtyI32Arr *pOrderCache,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	BufVertType *pType,
	I32 *pVert
) {
	switch (pCorner->type) {
		case PLYCUT_INTERSECT:
			*pType = STUC_BUF_VERT_INTERSECT;
			*pVert =
				addIntersectVert(pBasic, pOrderCache, pBorderCache, pBufMesh, pCorner);
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
			*pVert = addOnInEdgeVert(pBasic, pBufMesh, pCorner, inCorner);
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
			*pVert = addOnMapEdgeVert(pBasic, pBufMesh, pCorner, inCorner);
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
			*pVert = addOnVertVert(pBasic, pBufMesh, pCorner, inCorner);
			break;
		}
		default:
			PIX_ERR_ASSERT("invalid intersect corner type", false);
	}
}

static
void bufMeshAddFace(
	const MapToMeshBasic *pBasic,
	V2_I16 tile,
	bool wind,
	BufMesh *pBufMesh,
	I32 start,
	I32 faceSize,
	I32 mapFace
) {
	I32 newIdx = -1;
	PIXALC_DYN_ARR_ADD(BufFace, &pBasic->pCtx->alloc, (&pBufMesh->faces), newIdx);
	PIX_ERR_ASSERT("", newIdx != -1);
	pBufMesh->faces.pArr[newIdx].start = start;
	pBufMesh->faces.pArr[newIdx].size = faceSize;
	pBufMesh->faces.pArr[newIdx].mapFace = mapFace;
	pBufMesh->faces.pArr[newIdx].tile = tile;
	pBufMesh->faces.pArr[newIdx].wind = wind;
}

static
I32 addInsideMapVert(
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner,
	I32 inFace
) {
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
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	FaceCorner inCorner
) {
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
	InFaceCache *pInFaceCache,
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
		pInFaceCache,
		&inCorner,
		wind
	);
	if (status == STUC_INSIDE_STATUS_ON_LINE) {
		const HalfPlane *pInCornerCache = getInCornerCache(pInFaceCache, inCorner.pFace);
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
	const MapToMeshBasic *pBasic,
	const InFaceMemArr *pInFaceArr,
	BorderCache *pBorderCache,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	I32 mapCorner,
	BufVertType *pType,
	bool wind
) {
	FaceCorner corner = {0};
	F32 alpha = .0f;
	InsideStatus status = findEncasingInPieceFace(
		pBasic,
		pInFaceArr,
		pInPiece,
		&pBorderCache->inFaceCache,
		pMapFace,
		mapCorner,
		&corner,
		&alpha,
		wind
	);
	if (status == STUC_INSIDE_STATUS_OUTSIDE) {
		return -1;
	}
	I32 vert = 0;
	switch (status) {
		case STUC_INSIDE_STATUS_INSIDE: {
			*pType = STUC_BUF_VERT_IN_OR_MAP;
			PlycutCorner fake = {
				.info.origin.corner.corner = mapCorner,
			};
			vert = addInsideMapVert(pBasic, pBufMesh, &fake, corner.face);
			break;
		}
		case STUC_INSIDE_STATUS_ON_LINE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			PlycutCorner fake = {
				.info.onEdge.alpha = alpha,
				.info.onEdge.vertCorner.corner = mapCorner,
			};
			vert = addOnInEdgeVert(pBasic, pBufMesh, &fake, corner);
			break;
		}
		case STUC_INSIDE_STATUS_ON_VERT: {
			*pType = STUC_BUF_VERT_OVERLAP;
			PlycutCorner fake = {
				.info.onVert.subjCorner.corner = mapCorner
			};
			vert = addOnVertVert(pBasic, pBufMesh, &fake, corner);
			break;
		}
		default:
			PIX_ERR_ASSERT("invalid inside status", false);
	}
	return vert;
}

static
void bufMeshAddCorner(
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	BufVertType type,
	I32 vert
) {
	BufCornerArr *pCorners = &pBufMesh->corners;
	I32 newCorner = -1;
	PIXALC_DYN_ARR_ADD(BufCorner, &pBasic->pCtx->alloc, pCorners, newCorner);
	PIX_ERR_ASSERT("", newCorner >= 0);
	pCorners->pArr[newCorner].type = type;
	pCorners->pArr[newCorner].vert = vert;
}

static
StucErr bufMeshAddVert(
	const MapToMeshBasic *pBasic,
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
	BufVertType type = 0;
	I32 vert = -1;
	switch (pCorner->type) {
		case PLYCUT_ORIGIN_SUBJECT:
			vert = addMapVert(
				pBasic,
				pInFaceArr,
				pBorderCache,
				pInPiece,
				pBufMesh,
				pMapFace, pCorner->info.origin.corner.corner,
				&type,
				wind
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
			vert = addInVert(pBasic, pBufMesh, inCorner);
			break;
		}
		default:
			setIntersectBufVertInfo(
				pBasic,
				pBorderCache,
				pOrderCache,
				pBufMesh,
				pMapFace,
				pCorner,
				&type, &vert
			);
	}
	if (vert != -1) {
		bufMeshAddCorner(pBasic, pBufMesh, type, vert);
	}
	return err;
}

static
StucErr addFaceToBufMesh(
	const MapToMeshBasic *pBasic,
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
			pBasic,
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
	bufMeshAddFace(pBasic, pInPiece->tile, wind, pBufMesh, faceStart, faceSize, pMapFace->idx);
	return err;
}

static
void addFacesToBufMesh(
	const MapToMeshBasic *pBasic,
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
		err =
			addFaceToBufMesh(
				pBasic,
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

typedef struct GetExteriorBorderArgs {
	I32 border;
	I32 lowestBorder;
	V2_F32 lowestPos;
} GetExteriorBorderArgs;

/*
typedef struct BstFaceEntry {
	PixuctAvlNodeCore core;
	InFaceIdx idx;
} BstFaceEntry;

static
I32 bstFaceIdxCmp(const PixuctAvlNodeCore *pNode, const void *pKeyRaw) {
	I32 idx = (I32)((BstFaceEntry *)pNode)->idx.idx;
	const InFaceIdx *pKey = pKeyRaw;
	return idx == (I32)pKey->idx ? 2 : idx > (I32)pKey->idx;
}

static
StucErr bstFromFaces(
	PixuctAvl *pBst,
	PixalcLinAlloc *pAlloc,
	const InFaceMemArr *pInFaceArr,
	const InPiece *pInPiece
) {
	StucErr err = PIX_ERR_SUCCESS;
	EncasedMapFace *pEntry = pInPiece->pList;
	PIX_ERR_ASSERT("", pEntry);
	if (pBst->pAlloc) {
		pixuctAvlClear(pBst);
	}
	else {
		err = pixuctAvlInit(pBst, pAlloc);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	do {
		const InFaceIdxArr *pInFaces =
			pInFaceArr->arr[pEntry->job].pArr + pEntry->inFaces;
		for (I32 i = 0; i < pInFaces->count; ++i) {
			BstFaceEntry *pNode = NULL;
			InFaceIdx idx = pInFaces->pArr[i];
			I32 nodeIdx = 0;
			err = pixuctAvlAdd(pBst, (void **)&pNode, &nodeIdx, &idx, bstFaceIdxCmp);
			PIX_ERR_RETURN_IFNOT(err, "");
			pNode->idx = idx;
		}
	} while((pEntry = (void *)pEntry->core.pNext));
	return err;
}
*/

static
StucErr inFaceCacheBuild(
	const PixalcFPtrs *pAlloc,
	InFaceCache *pCache,
	const InFaceMemArr *pInFaceArr,
	const InPiece *pInPiece,
	Range *pRanges
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
				pixuctKeyFromI32, NULL, inFaceCacheEntryInit, inFaceCacheEntryCmp
			);
			PIX_ERR_ASSERT("", result == PIX_SEARCH_ADDED);
		}
	} while((pEntry = (void *)pEntry->core.pNext));
	return err;
}

static
StucErr addNonClipInPieceToBufMesh(
	const MapToMeshBasic *pBasic,
	const InFaceMemArr *pInFaceArr,
	const FaceRange *pMapFace,
	BorderCache *pBorderCache,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	bool wind
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 bufFaceStart = pBufMesh->corners.count;
	for (I32 i = 0; i < pMapFace->range.size; ++i) {	
		BufVertType type = 0;
		I32 vert = 0;
		vert = addMapVert(
			pBasic,
			pInFaceArr,
			pBorderCache,
			pInPiece,
			pBufMesh,
			pMapFace,
			i,
			&type,
			wind
		);
		if (vert != -1) {
			bufMeshAddCorner(pBasic, pBufMesh, type, vert);
			continue;
		}
		PIX_ERR_RETURN_QUIET_IFNOT_COND(
			err,
			!i,
			"non-clipped map faces must be fully in or out"
		);
		return err;
	}
	bufMeshAddFace(
		pBasic,
		pInPiece->tile,
		wind,
		pBufMesh,
		bufFaceStart,
		pMapFace->range.size,
		pMapFace->idx
	);
	return err;
}

/*
static
bool borderCacheAdd(
	const MapToMeshBasic *pBasic,
	void *pArgsVoid,
	InFaceCorner inCorner,
	InFaceCorner adjInCorner,
	I32 borderEdge,
	I32 walkIdx,
	bool adj
) {
	if (!adjInCorner.pFace) {
		InFaceCornerArr *pBorder = pArgsVoid;
		I32 newIdx = -1;
		PIXALC_DYN_ARR_ADD(InFaceCorner, &pBasic->pCtx->alloc, pBorder, newIdx);
		PIX_ERR_ASSERT("", newIdx != -1);
		pBorder->pArr[newIdx] = inCorner;
	}
	return false;
}

static
StucErr borderCacheAddInGap(PixuctAvl *pBorder, I32 idx, I32 offset, I32 fullCount) {
	StucErr err = PIX_ERR_SUCCESS;
	BorderCacheEdge *pNew = NULL;
	I32 key = (idx + offset) % fullCount;
	err = pixuctAvlAdd(pBorder, (void **)&pNew, NULL, &key, bstIdxCmp);
	PIX_ERR_RETURN_IFNOT(err, "");
	pNew->idx = key;
	pNew->cantIntersect = true;
	return err;
}

static
void arrFromAvl(
	const PixalcFPtrs *pAlloc,
	PixuctAvl *pAvl,
	PixtyValidIdxArr *pArr,
	bool intersectOnly
) {
	pArr->count = 0;
	PixuctAvlIter iter = {0};
	pixuctAvlIterInit(pAvl, &iter);
	BorderCacheEdge *pStart = (void *)pixuctAvlIterGetItem(&iter);
	for (; !pixuctAvlIterAtEnd(&iter); pixuctAvlIterInc(&iter)) {
		BorderCacheEdge *pNode = (void *)pixuctAvlIterGetItem(&iter);
		if (intersectOnly && pNode->cantIntersect) {
			continue;
		}
		I32 newIdx = 0;
		PIXALC_DYN_ARR_ADD(PixtyValidIdx, pAlloc, pArr, newIdx);
		pArr->pArr[newIdx].idx = pNode->idx;
		pArr->pArr[newIdx].valid = pNode->cantIntersect;
	}
}

static
PixErr borderCacheInit(
	const MapToMeshBasic *pBasic,
	const IslandClustArr *pClustArr,
	const InFaceMemArr *pInFaceArr,
	const InPiece *pInPiece,
	BorderCache *pCache
) {
	PixErr err = PIX_ERR_SUCCESS;
	const PixalcFPtrs *pAlloc = &pBasic->pCtx->alloc;
	if (pCache->alloc.valid) {
		PIX_ERR_ASSERT("", pCache->arr.size > 0 && pCache->ordered.size > 0);
		for (I32 i = 0; i < pCache->arr.size; ++i) {
			if (pCache->arr.pArr[i].count) {
				pixuctAvlClear(pCache->arr.pArr + i);
			}
		}
		pixalcLinAllocClear(&pCache->alloc);
	}
	else {
		pixalcLinAllocInit(pAlloc, &pCache->alloc, sizeof(BorderCacheEdge), 6, false);
	}
	pCache->pInPiece = pInPiece;
	pCache->pClustArr = pClustArr;
	pCache->borderCount = 0;
	const EncasedMapFace *pEntry = pInPiece->pList;
	PIX_ERR_ASSERT("", pEntry);
	do {
		PIX_ERR_ASSERT("", pEntry->job < pInFaceArr->count);
		const InFaceIdxArr *pInFaces =
			pInFaceArr->arr[pEntry->job].pArr + pEntry->inFaces;
		for (I32 i = 0; i < pInFaces->count; ++i)  {
			InFaceIdx idx = pInFaces->pArr[i];
			if (!idx.border) {
				continue;
			}
			FaceRange face = stucGetFaceRange(&pBasic->pInMesh->core, idx.idx);
			for (I32 j = 0; j < face.range.size; ++j) {
				const StucBorderTable *pTableEntry = NULL;
				if (!stucIsInCornerOnBorder(pBasic->pInMesh, pClustArr, &face, j, &pTableEntry)) {
					continue;
				}
				const Border *pBorder = pClustArr->pIsland->core.borders.pArr + pTableEntry->border;
				FaceCorner corner = pBorder->arr.pArr[pTableEntry->idx].corner;
				PIX_ERR_ASSERT("", face.idx == corner.face && j == corner.corner);
				if (pTableEntry->border >= pCache->arr.size) {
					I32 oldSize = pCache->arr.size;
					PIXALC_DYN_ARR_RESIZE(
						PixuctAvl,
						pAlloc,
						&pCache->arr,
						pTableEntry->border + 1
					);
					memset(
						pCache->arr.pArr + oldSize,
						0,
						sizeof(PixuctAvl) * (pCache->arr.size - oldSize)
					);
				}
				PixuctAvl *pPieceBorder = pCache->arr.pArr + pTableEntry->border;
				if (!pPieceBorder->pAlloc) {
					pixuctAvlInit(pPieceBorder, &pCache->alloc);
				}
				BorderCacheEdge *pNode = NULL;
				I32 key = pTableEntry->idx;
				err = pixuctAvlAdd(pPieceBorder, (void **)&pNode, NULL, &key, bstIdxCmp);
				PIX_ERR_RETURN_IFNOT(err, "");
				pNode->idx = key;
				pNode->cantIntersect = false;
			}
		}
	} while((pEntry = (void *)pEntry->core.pNext));
	{
		I32 oldSize = pCache->ordered.size;
		PIXALC_DYN_ARR_RESIZE(
			PixtyValidIdxArr,
			pAlloc,
			&pCache->ordered,
			pCache->arr.size
		);
		if (oldSize < pCache->ordered.size) {
			memset(
				pCache->ordered.pArr + oldSize,
				0,
				sizeof(PixtyValidIdxArr) * (pCache->ordered.size - oldSize)
			);
		}
	}
	for (I32 i = 0; i < pCache->arr.size; ++i) {
		PixtyValidIdxArr *pOrdered = pCache->ordered.pArr + i;
		if (!pCache->arr.pArr[i].count) {
			continue;
		}
		++pCache->borderCount;
		I32 fullCount = pClustArr->pIsland->core.borders.pArr[i].arr.count;
		PixuctAvl *pBorder = pCache->arr.pArr + i;
		arrFromAvl(pAlloc, pBorder, pOrdered, true);
		for (I32 j = 0; j < pOrdered->count; ++j) {
			I32 idx = pOrdered->pArr[j].idx;
			I32 idxNext;
			I32 gap;
			if (pOrdered->count == 1) {
				idxNext = idx;
				gap = fullCount;
			}
			else {
				idxNext = pOrdered->pArr[(j + 1) % pOrdered->count].idx;
				gap = idxNext + (idxNext > idx ? 0 : fullCount) - idx;
			}
			if (gap == 1) {
				continue;
			}
			PIX_ERR_ASSERT("", gap > 0);
			err = borderCacheAddInGap(pBorder, idx, 1, fullCount);
			PIX_ERR_RETURN_IFNOT(err, "");
			if (gap == 2) {
				continue;
			}
			err = borderCacheAddInGap(pBorder, idx, 2, fullCount);
			PIX_ERR_RETURN_IFNOT(err, "");
			if (gap == 3) {
				continue;
			}
			I32 offset = (idxNext ? idxNext - 1 : fullCount - 1) - idx;
			err = borderCacheAddInGap(pBorder, idx, offset, fullCount);
			PIX_ERR_RETURN_IFNOT(err, "");
		}
		arrFromAvl(pAlloc, pBorder, pOrdered, false);
	}
	return err;
}
*/

static
void borderCacheDestroy(const StucAlloc *pAlloc, BorderCache *pCache) {
	for (I32 i = 0; i < pCache->arr.size; ++i) {
		if (pCache->ordered.pArr[i].pArr) {
			pAlloc->fpFree(pCache->ordered.pArr[i].pArr);
		}
	}
	if (pCache->arr.pArr) {
		pAlloc->fpFree(pCache->arr.pArr);
	}
	if (pCache->ordered.pArr) {
		pAlloc->fpFree(pCache->ordered.pArr);
	}
	if (pCache->alloc.valid) {
		pixalcLinAllocDestroy(&pCache->alloc);
	}
	if (pCache->inFaceCache.corners.pArr) {
		pAlloc->fpFree(pCache->inFaceCache.corners.pArr);
	}
	if (pCache->borderSizes.pArr) {
		pAlloc->fpFree(pCache->borderSizes.pArr);
	}
	*pCache = (BorderCache){0};
}

/*
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
	const IslandClustArr *pClustArr = pCache->pClustArr;
	boundary = ((I32 *)input.pUserData)[boundary];
	PIX_ERR_ASSERT("", boundary >= 0 && boundary < pCache->arr.size);
	if (boundary != pCache->activeBorder) {
		pCache->activeBorder = boundary;
		//pixuctAvlIterInit(pCache->arr.pArr + pCache->activeBorder, &pCache->iter);
	}
	else {
		//PIX_ERR_ASSERT("", !pixuctAvlIterAtEnd(&pCache->iter));
		//pixuctAvlIterInc(&pCache->iter);
	}
	//BirderCacheEdge *pNode = (void *)pixuctAvlIterGetItem(&pCache->iter);
	PixtyValidIdx idx = pCache->ordered.pArr[boundary].pArr[corner];
	*pCornerUserData = idx.idx;
	*pCantIntersect = idx.valid;
	FaceCorner faceCorner =
		pClustArr->pIsland->core.borders.pArr[boundary].arr.pArr[idx.idx].corner;
	FaceRange face = stucGetFaceRange(&pBasic->pInMesh->core, faceCorner.face);
	V2_F32 pos = stucGetUvPos(pBasic->pInMesh, &face, faceCorner.corner);
	V2_I16 tile = pCache->pInPiece->pList->tile;
	V2_F32 fTile = {.d = {(F32)tile.d[0], (F32)tile.d[1]}};
	return _(pos V2SUB fTile);
}
*/

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
	const StucMap pMap,
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
StucErr islandFacesInit(
	const PixalcFPtrs *pAlloc,
	void *pIslandsRaw,
	I32 count,
	I32 **ppOut
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslands = pIslandsRaw;
	PIXALC_DYN_ARR_RESIZE(I32, pAlloc, &pIslands->faces, count);
	*ppOut = pIslands->faces.pArr;
	return err;
}

static
StucErr borderInit(const PixalcFPtrs *pAlloc, void *pIslandsRaw, I32 island, I32 *pIdx) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslandArr = pIslandsRaw;
	StucInIsland *pIsland = pIslandArr->pArr + island;
	I32 oldSize = pIsland->core.borders.size;
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(Border, pAlloc, &pIsland->core.borders, newIdx);
	if (newIdx >= oldSize) {
		memset(
			pIsland->core.borders.pArr + oldSize,
			0,
			sizeof(Border) * (pIsland->core.borders.size - oldSize)
		);
	}
	else {
		pIsland->core.borders.pArr[newIdx].arr.count = 0;
	}
	*pIdx = newIdx;
	return err;
}

static
StucErr borderMarkAsOuter(
	void *pIslandsRaw,
	I32 island,
	I32 border,
	const PixmshV2Bb *pBb
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslandArr = pIslandsRaw;
	pIslandArr->pArr[island].core.borders.outer = border;
	pIslandArr->pArr[island].bb = (ClutreBb){.min = pBb->min, .max = pBb->max};
	return err;
}

typedef struct BorderKey {
	I32 border;
	I32 edge;
	I32 idx;
} BorderKey;

static
PixuctKey borderMakeKey(const void *pKeyRaw) {
	return (PixuctKey){.pKey = &((BorderKey *)pKeyRaw)->edge, .size = sizeof(I32)};
}

static
void borderInitEntry(
	void *pUserData,
	PixuctHTableEntryCore *pEntryCore,
	const void *pKeyRaw,
	void *pInitInfoRaw,
	I32 idx
) {
	StucBorderTable *pEntry = (void *)pEntryCore;
	const BorderKey *pKey = pKeyRaw;
	pEntry->border = pKey->border;
	pEntry->edge = pKey->edge;
	pEntry->idx = pKey->idx;
}

static
bool borderCmpEntry(
	const PixuctHTableEntryCore *pEntryCore,
	const void *pKeyRaw,
	const void *pInitInfoRaw
) {
	const StucBorderTable *pEntry = (void *)pEntryCore;
	const BorderKey *pKey = pKeyRaw;
	return pKey->edge == pEntry->edge;
}

static
StucErr borderAddEdge(
	const PixalcFPtrs *pAlloc,
	void *pIslandsRaw,
	I32 island,
	I32 adjIsland,
	I32 border,
	FaceCorner corner,
	I32 edge
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslandArr = pIslandsRaw;
	StucInIsland *pIsland = pIslandArr->pArr + island;
	Border *pBorder = pIsland->core.borders.pArr + border;
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(BorderEdge, pAlloc, &pBorder->arr, newIdx);
	pBorder->arr.pArr[newIdx] = (BorderEdge){.corner = corner, .adjIsland = adjIsland};
	pixuctHTableGet(
		&pIsland->borderTable,
		0,
		&(BorderKey){.border = border, .edge = edge, .idx = newIdx},
		NULL,
		true,
		NULL,
		borderMakeKey, NULL, borderInitEntry, borderCmpEntry
	);
	return err;
}

static
StucErr islandAdd(const PixalcFPtrs *pAlloc, void *pIslandsRaw, I32 splitTotal, I32 *pIdx) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslands = pIslandsRaw;
	I32 newIdx = 0;
	I32 oldSize = pIslands->size;
	PIXALC_DYN_ARR_ADD(StucInIsland, pAlloc, pIslands, newIdx);
	if (oldSize < pIslands->size) {
		memset(
			pIslands->pArr + oldSize,
			0,
			sizeof(StucInIsland) * (pIslands->size - oldSize)
		);
	}
	*pIdx = newIdx;
	StucInIsland *pIsland = pIslands->pArr + newIdx;
	pIsland->core.borders.count = 0;
	if (pIsland->borderTable.pTable) {
		pixuctHTableDestroy(&pIsland->borderTable);
	}
	pixuctHTableInit(
		pAlloc,
		&pIsland->borderTable,
		splitTotal / 2 + 2,
		(PixtyI32Arr){.pArr = (I32[]){sizeof(StucBorderTable)}, .count = 1},
		&pIslands->tableMem, NULL, false
	);
	return err;
}

static
StucErr islandRangeSet(void *pIslandsRaw, I32 island, PixtyRange range) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslands = pIslandsRaw;
	pIslands->pArr[island].core.faces = range;
	return err;
}

static
FaceCorner getAdjPieceCorner(const void *pMeshRaw, FaceCorner corner) {
	FaceCorner adj = {0};
	corner.face = faceIdxInPiece(((PieceMesh *)pMeshRaw)->pInFaceCache, corner.face);
	stucGetAdjCorner(((PieceMesh *)pMeshRaw)->pMesh, corner, &adj);
	validateFaceInPiece(((PieceMesh *)pMeshRaw)->pInFaceCache, &adj);
	return adj;
}

static inline
PixtyV2_F32 stucPieceUv(const void *pMeshRaw, I32 corner) {
	const Mesh *pMesh = ((PieceMesh *)pMeshRaw)->pMesh;
	PIX_ERR_ASSERT("", pMesh->pUvs && corner >= 0 && corner < pMesh->core.cornerCount);
	return pMesh->pUvs[corner];
}

static inline
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

StucErr stucClipMapFace(
	const MapToMeshBasic *pBasic,
	const InFaceMemArr *pInFaceArr,
	const IslandClustArr *pClustArr,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache,
	void *pHTableAlc,
	void *pPlycutAlc,
	PixtyI32Arr *pOrderCache
) {
	StucErr err = PIX_ERR_SUCCESS;
	pBorderCache->inFaceCache.corners.count = 0;
	InFaceCacheState inFaceCacheState = {.pBasic = pBasic};
	pixuctHTableInit(
		&pBasic->pCtx->alloc,
		&pBorderCache->inFaceCache.table,
		pInPiece->inFaceCount / 2 + 1,
		(I32Arr) {
			.pArr = (I32[]){sizeof(InFaceCacheEntry)},
			.count = 1
		},
		pHTableAlc,
		&inFaceCacheState,
		false
	);
	Range pieceRanges[PIXTH_MAX_SUB_MAPPING_JOBS] = {0};
	err = inFaceCacheBuild(
		&pBasic->pCtx->alloc,
		&pBorderCache->inFaceCache,
		pInFaceArr,
		pInPiece,
		pieceRanges
	);
	PIX_ERR_RETURN_IFNOT(err, "");

	{
		pBorderCache->pieceIslands.count = 0;
		pBorderCache->pieceIslands.faceCount = 0;
		PixmshSplitIntfOut splitIslands = {
			.pUserData = &pBorderCache->pieceIslands,
			.fpBorderInit = borderInit,
			.fpBorderAddEdge = borderAddEdge,
			.fpFacesInit = islandFacesInit,
			.fpIslandAdd = islandAdd,
			.fpRangeSet = islandRangeSet,
			.fpBorderMarkAsOuter = borderMarkAsOuter
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
			NULL
		);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	//return err;

	/*
	borderCacheInit(pBasic, pClustArr, pInFaceArr, pInPiece, pBorderCache);
	PlycutInput inInput = {.boundaries = pBorderCache->borderCount};
	I32 boundaryRedir = 0;
	I32 boundarySize = 0;
	I32 *pInputMem = NULL;
	if (pBorderCache->borderCount > 1) {
		pInputMem = pBasic->pCtx->alloc.fpMalloc(inInput.boundaries * 2 * sizeof(I32));
		inInput.pUserData = pInputMem;
		inInput.pSizes = pInputMem + inInput.boundaries;
	}
	else {
		inInput.pUserData = &boundaryRedir;
		inInput.pSizes = &boundarySize;
	}
	{
		I32 idx = 0;
		for (I32 i = 0; i < pBorderCache->arr.size; ++i) {
			if (!pBorderCache->arr.pArr[i].count) {
				continue;
			}
			inInput.pSizes[idx] = pBorderCache->arr.pArr[i].count;
			((I32 *)inInput.pUserData)[idx] = i;
			++idx;
		}
		PIX_ERR_ASSERT("", idx == pBorderCache->borderCount);
	}
	*/
	pBorderCache->pInPiece = pInPiece;
	ClutreFaceRange mapFaces = {0};
	err = inPieceGetFaces(pBasic->pMap, pInPiece, &mapFaces);
	for (I32 i = 0; i < pBorderCache->pieceIslands.count; ++i) {
		pBorderCache->activeIsland = i;
		StucInIsland *pPieceIsland = pBorderCache->pieceIslands.pArr + i;
		I32 borderCount = pPieceIsland->core.borders.count;
		PIXALC_DYN_ARR_RESIZE(
			I32,
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
			PlycutFaceArr out = {0};
			pBorderCache->activeBorder = -1;
			err = plycutClip(
				&pBasic->pCtx->alloc,
				pBasic,
				pBorderCache, inInput, getBorderCornerPos,
				NULL, mapInput, getMapCornerPos,
				&out,
				NULL,
				pPlycutAlc
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			if (out.count) {
				addFacesToBufMesh(
					pBasic,
					pInFaceArr,
					pBorderCache,
					inPieceOffset,
					pInPiece,
					pBufMesh,
					pOrderCache,
					&mapFace,
					&out,
					pClustArr->pIsland->wind
				);
			}
			PIX_ERR_CATCH(0, err, 
				err = PIX_ERR_SUCCESS; //skipping this face, reset err
			);
			plycutFaceArrDestroy(&pBasic->pCtx->alloc, &out);
		}
	}
	pixuctHTableDestroy(&pBorderCache->inFaceCache.table);
	/*
	if (pBorderCache->borderCount > 1) {
		PIX_ERR_ASSERT(
			"",
			inInput.pSizes != &boundarySize && inInput.pUserData != &boundaryRedir
		);
		pBasic->pCtx->alloc.fpFree(pInputMem);
	}
	*/
	return err;
}

StucErr stucAddMapFaceToBufMesh(
	const MapToMeshBasic *pBasic,
	const InFaceMemArr *pInFaceArr,
	const IslandClustArr *pClustArr,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache,
	void *pHTableAlc,
	void *pPlycutAlc, //unused, needed for function callback
	PixtyI32Arr *pOrderCache //same as ^
) {
	StucErr err = PIX_ERR_SUCCESS;

	pBorderCache->inFaceCache.corners.count = 0;
	InFaceCacheState inFaceCacheState = {.pBasic = pBasic};
	pixuctHTableInit(
		&pBasic->pCtx->alloc,
		&pBorderCache->inFaceCache.table,
		pInPiece->inFaceCount / 2 + 1,
		(I32Arr) {
			.pArr = (I32[]){sizeof(InFaceCacheEntry)},
			.count = 1
		},
		pHTableAlc,
		&inFaceCacheState,
		false
	);

	/*
	if (pInPiece->inFaceCount > 4) {
		PieceMesh pieceMesh = {
			.pMesh = pBasic->pInMesh,
			.pInFaceCache = &pBorderCache->inFaceCache
		};
		ClutreMesh clutreMesh = {
			.pUserData = &pieceMesh,
			.faceCount = pInPiece->inFaceCount,
			.fpFaceRange = pieceFaceRange,
			.fpPos = stucPieceUv,
			.fpVert = stucPieceVert
		};
		err = clutreTreeInit(
			&pBasic->pCtx->alloc,
			&clutreMesh,
			&pBorderCache->inFaceCache.tree,
			1
		);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	*/
	
	ClutreFaceRange mapFaces = {0};
	err = inPieceGetFaces(pBasic->pMap, pInPiece, &mapFaces);
	for (I32 i = 0; i < mapFaces.size; ++i) {
		FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaces.pArr[i]);
		err = addNonClipInPieceToBufMesh(
			pBasic,
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
	PIX_ERR_CATCH(0, err,
		err = PIX_ERR_SUCCESS; //reset err (skipping face)
	);
	pixuctHTableDestroy(&pBorderCache->inFaceCache.table);
	return err;
}

static
void stucInIslandsBorderArrDestroy(StucContext pCtx, BorderArr *pArr) {
	if (pArr->pArr) {
		for (I32 i = 0; i < pArr->count; ++i) {
			if (pArr->pArr[i].arr.pArr) {
				pCtx->alloc.fpFree(pArr->pArr[i].arr.pArr);
			}
		}
		pCtx->alloc.fpFree(pArr->pArr);
	}
}

static
void stucInIslandsDestroy(StucContext pCtx, StucInIslandArr *pArr) {
	if (pArr->faces.pArr) {
		pCtx->alloc.fpFree(pArr->faces.pArr);
	}
	if (pArr->pFaceTable) {
		pCtx->alloc.fpFree(pArr->pFaceTable);
	}
	if (!pArr->pArr) {
		*pArr = (StucInIslandArr){0};
		return;
	}
	for (I32 i = 0; i < pArr->size; ++i) {
		if (!pArr->pArr[i].core.faces.end) {
			break;
		}
		stucInIslandsBorderArrDestroy(pCtx, &pArr->pArr[i].core.borders);
		if (pArr->pArr[i].borderTable.pTable) {
			pixuctHTableDestroy(&pArr->pArr[i].borderTable);
		}
		StucSubIslandArr *pSub = &pArr->pArr[i].sub;
		for (I32 j = 0; j < pSub->count; ++j) {
			stucInIslandsBorderArrDestroy(pCtx, &pSub->pArr[j].core.borders);
		}
		if (pSub->pFaces) {
			pCtx->alloc.fpFree(pSub->pFaces);
		}
		if (pSub->pArr) {
			pCtx->alloc.fpFree(pSub->pArr);
		}
	}
	pixuctHTableMemDestroy(&pCtx->alloc, &pArr->tableMem);
	pCtx->alloc.fpFree(pArr->pArr);
	*pArr = (StucInIslandArr){0};
}

typedef struct BufMeshInitJobArgs {
	JobArgs core;
	const IslandClustArr *pClustArr;
	const InFaceMemArr *pInFaceArr;
	StucErr (* fpAddPiece)(
		const MapToMeshBasic *,
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

	PixmshSplitMem splitMem = {0};
	BorderCache borderCache = {.pSplitMem = &splitMem};

	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	const MapToMeshBasic *pBasic = (const MapToMeshBasic *)pArgs->core.pShared;
	PixuctHTableMem hTableAlc = {0};
	PlycutMem plycutAlc = {0};
	PixtyI32Arr orderCache = {0};
	for (I32 i = 0; i < rangeSize; ++i) {
		I32 inPieceIdx = pArgs->core.range.start + i;
		pArgs->fpAddPiece(
			pBasic,
			pArgs->pInFaceArr,
			pArgs->pClustArr,
			inPieceIdx,
			pArgs->pInPiecesSplit->pArr + inPieceIdx,
			&pArgs->bufMesh,
			&borderCache,
			&hTableAlc,
			&plycutAlc,
			&orderCache
		);
		pixuctHTableMemClear(&borderCache.pieceIslands.tableMem);
	}
	pixmshSplitMemDestroy(&pBasic->pCtx->alloc, &splitMem);
	stucInIslandsDestroy(pBasic->pCtx, &borderCache.pieceIslands);
	pixuctHTableMemDestroy(&pBasic->pCtx->alloc, &hTableAlc);
	plycutMemDestroy(&plycutAlc);
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	borderCacheDestroy(pAlloc, &borderCache);
	if (orderCache.pArr) {
		pAlloc->fpFree(orderCache.pArr);
	}
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
		const MapToMeshBasic *,
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
I32 bufMeshInitJobsGetRange(const StucContext pCtx, const void *pShared, void *pInitInfoVoid) {
	return ((BufMeshJobInitInfo *)pInitInfoVoid)->pInPiecesSplit->count;
}

static
void bufMeshInitJobInit(
	const StucContext pCtx,
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
			BufMesh,
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
	const IslandClustArr *pClustArr,
	const InFaceMemArr *pInFaceArr,
	InPieceArr *pInPieces,
	StucErr (* fpAddPiece)(
		const struct MapToMeshBasic *,
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

void stucBufMeshArrDestroy(StucContext pCtx, BufMeshArr *pArr) {
	if (!pArr->pArr) {
		*pArr = (BufMeshArr){0};
		return;
	}
	for (I32 i = 0; i < pArr->count; ++i) {
		if (pArr->pArr[i].faces.pArr) {
			pCtx->alloc.fpFree(pArr->pArr[i].faces.pArr);
		}
		if (pArr->pArr[i].corners.pArr) {
			pCtx->alloc.fpFree(pArr->pArr[i].corners.pArr);
		}
		if (pArr->pArr[i].inOrMapVerts.pArr) {
			pCtx->alloc.fpFree(pArr->pArr[i].inOrMapVerts.pArr);
		}
		if (pArr->pArr[i].onEdgeVerts.pArr) {
			pCtx->alloc.fpFree(pArr->pArr[i].onEdgeVerts.pArr);
		}
		if (pArr->pArr[i].overlapVerts.pArr) {
			pCtx->alloc.fpFree(pArr->pArr[i].overlapVerts.pArr);
		}
		if (pArr->pArr[i].intersectVerts.pArr) {
			pCtx->alloc.fpFree(pArr->pArr[i].intersectVerts.pArr);
		}
		pArr->pArr[i] = (BufMesh) {0};
	}
	pCtx->alloc.fpFree(pArr->pArr);
	*pArr = (BufMeshArr){0};
}

