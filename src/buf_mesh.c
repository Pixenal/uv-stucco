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
} InFaceCacheInitInfo;

typedef struct InFaceCacheState {
	const MapToMeshBasic *pBasic;
	bool initBounds;
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
	InFaceCacheInitInfo *pInitInfo = pInitInfoVoid;
	((InFaceCacheEntryIntern *)pEntryVoid)->pCorners = NULL;
	InFaceCacheEntry *pEntry = (void *)pEntryVoid;
	*pEntry = (InFaceCacheEntry){
		.face = stucGetFaceRange(&pState->pBasic->pInMesh->core, *(I32 *)pKeyData)
	};
	if (pState->initBounds) {
		FaceBounds bounds = { 0 };
		stucGetInFaceBounds(&bounds, pState->pBasic->pInMesh->pUvs, pEntry->face);
		V2_F32 fTile = {.d = {(F32)pInitInfo->tile.d[0], (F32)pInitInfo->tile.d[1]}};
		pEntry->fMin = _(bounds.fBBox.min V2SUB fTile);
		pEntry->fMax = _(bounds.fBBox.max V2SUB fTile);
	}
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
	PixuctHTable *pCache,
	const InPiece *pInPiece,
	I32 face,
	bool addEntry,
	InFaceCacheEntry **ppEntry
) {
	return pixuctHTableGet(
		pCache,
		0,
		&face,
		(void**)ppEntry,
		addEntry,
		&(InFaceCacheInitInfo) {.tile = pInPiece->pList->tile},
		stucKeyFromI32, NULL, inFaceCacheEntryInit, inFaceCacheEntryCmp
	);
}

static
InFaceCorner getAdjFaceInPiece(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	PixuctHTable *pInFaceCache,
	InFaceCorner corner
) {
	I32 edge = stucGetMeshEdge(
		&pBasic->pInMesh->core, (FaceCorner) {
			.face = corner.pFace->face.idx,
			.corner = corner.corner
		}
	);
	FaceCorner adjCorner = {0};
	stucGetAdjCorner(
		pBasic->pInMesh,
		(FaceCorner) {.face = corner.pFace->face.idx, .corner = corner.corner },
		&adjCorner
	);
	if (adjCorner.face == -1) {
		return (InFaceCorner) {.pFace = NULL, .corner = -1};
	}
	InFaceCacheEntry *pAdjEntry = NULL;
	inFaceCacheGet(
		pInFaceCache,
		pInPiece,
		adjCorner.face,
		false,
		&pAdjEntry
	);
	if (!pAdjEntry ||
		//if returns 0 or 2 (2 indicating unsplit preserve), then edge is internal
		stucCouldInEdgeIntersectMapFace(pBasic->pInMesh, edge) == 1
	) {
		return (InFaceCorner) {.pFace = NULL, .corner = -1};
	}
	adjCorner.corner = stucGetCornerNext(adjCorner.corner, &pAdjEntry->face);
	return (InFaceCorner) {.pFace = pAdjEntry, .corner = adjCorner.corner};
}

static
HalfPlane *getInCornerCache(
	const MapToMeshBasic *pBasic,
	PixalcLinAlloc *pHalfPlaneAlc,
	const InPiece *pInPiece,
	InFaceCacheEntry *pInFaceCacheEntry
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	InFaceCacheEntryIntern *pFaceEntry = (InFaceCacheEntryIntern *)pInFaceCacheEntry;
	if (!pFaceEntry->pCorners) {
		pixalcLinAlloc(
			pHalfPlaneAlc,
			&pFaceEntry->pCorners,
			pFaceEntry->faceEntry.face.size
		);
		initHalfPlaneLookup(
			pBasic->pInMesh,
			&pFaceEntry->faceEntry.face,
			pInPiece->pList->tile,
			pFaceEntry->pCorners
		);
	}
	return pFaceEntry->pCorners;
}

static
StucErr walkInPieceBorder(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	PixuctHTable *pInFaceCache,
	FaceCorner startCorner,
	bool (* fpFunc)(
		const MapToMeshBasic *, void *, InFaceCorner, InFaceCorner, I32, I32, bool
	),
	void *pFuncArgs
) {
	StucErr err = PIX_ERR_SUCCESS;
	InFaceCorner inCorner = {.corner = startCorner.corner};
	inFaceCacheGet(
		pInFaceCache,
		pInPiece,
		startCorner.face,
		false,
		&inCorner.pFace
	);
	PIX_ERR_ASSERT("", inCorner.pFace);
	I32 borderEdge = 0;
	I32 j = 0;
	bool adj = false;//value is invalid on first iteration
	do {
		if (borderEdge != 0 &&
			inCorner.pFace->face.idx == startCorner.face &&
			inCorner.corner == startCorner.corner
			) {
			break; //full loop
		}
		PIX_ERR_RETURN_IFNOT_COND(
			err,
			j < pInPiece->faceCount * 4,
			"stuck in loop"
		);
		InFaceCorner adjInCorner = getAdjFaceInPiece(
			pBasic,
			pInPiece,
			pInFaceCache,
			inCorner
		);
		if (fpFunc(pBasic, pFuncArgs, inCorner, adjInCorner, borderEdge, j, adj)) {
			break;
		}
		if (adjInCorner.pFace) {
			inCorner = adjInCorner; //edge is internal, move to adj face
			adj = true;
		}
		else {
			//edge is on boundary, continue on this face
			inCorner.corner = stucGetCornerNext(inCorner.corner, &inCorner.pFace->face);
			borderEdge++;
			adj = false;
		}
	} while (j++, true);
	return err;
}

static
void getInPieceBounds(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	PixuctHTable *pInFaceCache
) {
	EncasingInFaceArr *pInFaces = &pInPiece->pList->inFaces;
	for (I32 i = 0; i < pInFaces->count; ++i) {
		InFaceCacheEntry *pCacheEntry = NULL;
		inFaceCacheGet(
			pInFaceCache,
			pInPiece,
			pInFaces->pArr[i].idx,
			true,
			&pCacheEntry
		);
		PIX_ERR_ASSERT("", pCacheEntry);
	}
}

static
InsideStatus isPointInFaceConvex(
	bool wind,
	I32 faceSize,
	HalfPlane *pCorners,
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
	HalfPlane *pCorners,
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
	HalfPlane *pCorners,
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
	HalfPlane *pCorners,
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
InsideStatus getFaceEncasingVert(
	const MapToMeshBasic *pBasic,
	V2_F32 vert,
	const InPiece *pInPiece,
	PixuctHTable *pInFaceCache,
	InFaceCorner *pCorner
) {
	EncasingInFaceArr *pInFaces = &pInPiece->pList->inFaces;
	PixalcLinAlloc *pHalfPlaneAlc = pixuctHTableAllocGet(pInFaceCache, 1);
	for (I32 i = 0; i < pInFaces->count; ++i) {
		InFaceCacheEntry *pInFaceEntry = NULL;
		inFaceCacheGet(
			pInFaceCache,
			pInPiece,
			pInFaces->pArr[i].idx,
			true,
			&pInFaceEntry
		);
		if (!_(vert V2GREATEQL pInFaceEntry->fMin) ||
			!_(vert V2LESSEQL pInFaceEntry->fMax)
		) {
			continue;
		}
		HalfPlane *pInCornerCache =
			getInCornerCache(pBasic, pHalfPlaneAlc, pInPiece, pInFaceEntry);
		InsideStatus status = isPointInFace(
			pInFaces->pArr[i].wind,
			pInFaceEntry->face.size,
			pInCornerCache,
			vert,
			&pCorner->corner
		);
		if (status == STUC_INSIDE_STATUS_OUTSIDE) {
			continue;
		}
		pCorner->pFace = pInFaceEntry;
		return status;
	}
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

static
InFaceCorner getInCornerFromPlycut(
	const BorderCache *pBorderCache,
	PlycutCornerIdx idx
) {
	return pBorderCache->pBorders[idx.boundary].pArr[idx.corner];
}

static
I32 addIntersectVert(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner
) {
	I32 vert = bufMeshAllocIntersectVert(pBasic, pBufMesh);
	const PlycutInfoIntersect *pInfo = &pCorner->info.intersect;
	InFaceCorner inCorner =
		getInCornerFromPlycut(pBorderCache, pInfo->clipCorner);
	pBufMesh->intersectVerts.pArr[vert] = (IntersectVert){
		.inFace = inCorner.pFace->face.idx,
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
	InFaceCorner inCorner
) {
	I32 vert = bufMeshAllocOnEdgeVert(pBasic, pBufMesh);
	pBufMesh->onEdgeVerts.pArr[vert].map = (EdgeMapVert) {
		.type = STUC_BUF_VERT_SUB_TYPE_EDGE_MAP,
		.mapCorner = pCorner->info.onEdge.vertCorner.corner,
		.inCorner = inCorner.corner,
		.inFace = inCorner.pFace->face.idx,
		.tInEdge = pCorner->info.onEdge.alpha
	};
	return vert;
}

static
I32 addOnMapEdgeVert(
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner,
	InFaceCorner inCorner
) {
	I32 vert = bufMeshAllocOnEdgeVert(pBasic, pBufMesh);
	pBufMesh->onEdgeVerts.pArr[vert].in = (EdgeInVert) {
		.type = STUC_BUF_VERT_SUB_TYPE_EDGE_IN,
		.mapCorner = pCorner->info.onEdge.edgeCorner.corner,
		.inCorner = inCorner.corner,
		.inFace = inCorner.pFace->face.idx,
		.tMapEdge = pCorner->info.onEdge.alpha
	};
	return vert;
}

static
I32 addOnVertVert(
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	const PlycutCorner *pCorner,
	InFaceCorner inCorner
) {
	I32 vert = bufMeshAllocOverlapVert(pBasic, pBufMesh);
	pBufMesh->overlapVerts.pArr[vert] = (OverlapVert) {
		.inFace = inCorner.pFace->face.idx,
		.inCorner = inCorner.corner, 
		.mapCorner = pCorner->info.onVert.subjCorner.corner
	};
	return vert;
}

static
bool inCornerPredicate(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	PlycutCornerIdx idx
) {
	const InFaceCornerArr *pBorder = pBorderCache->pBorders + idx.boundary;
	I32 iPrev = idx.corner ? idx.corner - 1 : pBorder->count - 1;
	const InFaceCorner corner = pBorder->pArr[idx.corner];
	const InFaceCorner cornerPrev = pBorder->pArr[iPrev];
	bool single = corner.pFace == cornerPrev.pFace;
	I32 vert = pBasic->pInMesh->core.pCorners[
		corner.pFace->face.start + corner.corner
	];
	return single || stucCheckIfVertIsPreserve(pBasic->pInMesh, vert);
}

static
void setIntersectBufVertInfo(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	BufVertType *pType,
	I32 *pVert
) {
	switch (pCorner->type) {
		case PLYCUT_INTERSECT:
			*pType = STUC_BUF_VERT_INTERSECT;
			*pVert = addIntersectVert(pBasic, pBorderCache, pBufMesh, pCorner);
			break;
		case PLYCUT_ON_CLIP_EDGE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			InFaceCorner inCorner =
				getInCornerFromPlycut(pBorderCache, pCorner->info.onEdge.edgeCorner);
			*pVert = addOnInEdgeVert(pBasic, pBufMesh, pCorner, inCorner);
			break;
		}
		case PLYCUT_ON_SUBJECT_EDGE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			PlycutCornerIdx vertCorner = pCorner->info.onEdge.vertCorner;
			if (!pCorner->cross && !inCornerPredicate(pBasic, pBorderCache, vertCorner)) {
				break;
			}
			InFaceCorner inCorner =
				getInCornerFromPlycut(pBorderCache, pCorner->info.onEdge.vertCorner);
			*pVert = addOnMapEdgeVert(pBasic, pBufMesh, pCorner, inCorner);
			break;
		}
		case PLYCUT_ON_VERT: {
			*pType = STUC_BUF_VERT_OVERLAP;
			InFaceCorner inCorner =
				getInCornerFromPlycut(pBorderCache, pCorner->info.onVert.clipCorner);
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
	I32 inPieceOffset,
	BufMesh *pBufMesh,
	I32 start,
	I32 faceSize
) {
	I32 newIdx = -1;
	PIXALC_DYN_ARR_ADD(BufFace, &pBasic->pCtx->alloc, (&pBufMesh->faces), newIdx);
	PIX_ERR_ASSERT("", newIdx != -1);
	pBufMesh->faces.pArr[newIdx].start = start;
	pBufMesh->faces.pArr[newIdx].size = faceSize;
	pBufMesh->faces.pArr[newIdx].inPiece = inPieceOffset;
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
	InFaceCorner inCorner
) {
	I32 vert = bufMeshAllocInOrMapVert(pBasic, pBufMesh);
	pBufMesh->inOrMapVerts.pArr[vert].in = (InVert){
		.type = STUC_BUF_VERT_SUB_TYPE_IN,
		.inCorner = inCorner.corner,
		.inFace = inCorner.pFace->face.idx
	};
	return vert;
}

static
InsideStatus findEncasingInPieceFace(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	PixuctHTable *pInFaceCache,
	const FaceRange *pMapFace,
	I32 mapCorner,
	InFaceCorner *pInCorner,
	F32 *pAlpha
) {
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	V2_F32 pos = *(V2_F32 *)&pMapMesh->pPos[
		pMapMesh->core.pCorners[pMapFace->start + mapCorner]
	];
	InsideStatus status =
		getFaceEncasingVert(pBasic, pos, pInPiece, pInFaceCache, pInCorner);
	if (status == STUC_INSIDE_STATUS_ON_LINE) {
		HalfPlane *pInCornerCache = getInCornerCache(
			pBasic,
			pixuctHTableAllocGet(pInFaceCache, 1),
			pInPiece,
			pInCorner->pFace
		);
		I32 corner = pInCorner->corner;
		V2_F32 uv = pInCornerCache[corner].uv;
		I32 cornerNext = stucGetCornerNext(corner, &pInCorner->pFace->face);
		V2_F32 uvNext = pInCornerCache[cornerNext].uv;
		V2_F32 dirUnit = _(_(uvNext V2SUB uv) V2DIVS pInCornerCache[corner].len);
		*pAlpha = stucGetT(pos, uv, dirUnit, pInCornerCache[corner].len);
	}
	return status;
}

static
I32 addMapVert(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	const InPiece *pInPiece, PixuctHTable *pInPieceCache,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace, I32 mapCorner,
	BufVertType *pType
) {
	InFaceCorner inCorner = {0};
	F32 alpha = .0f;
	InsideStatus status = findEncasingInPieceFace(
		pBasic,
		pInPiece, pInPieceCache,
		pMapFace, mapCorner,
		&inCorner,
		&alpha
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
			I32 inFace = inCorner.pFace->face.idx;
			vert = addInsideMapVert(pBasic, pBufMesh, &fake, inFace);
			break;
		}
		case STUC_INSIDE_STATUS_ON_LINE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			PlycutCorner fake = {
				.info.onEdge.alpha = alpha,
				.info.onEdge.vertCorner.corner = mapCorner,
			};
			vert = addOnInEdgeVert(pBasic, pBufMesh, &fake, inCorner);
			break;
		}
		case STUC_INSIDE_STATUS_ON_VERT: {
			*pType = STUC_BUF_VERT_OVERLAP;
			PlycutCorner fake = {
				.info.onVert.subjCorner.corner = mapCorner
			};
			vert = addOnVertVert(pBasic, pBufMesh, &fake, inCorner);
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
	const InPiece *pInPiece,
	const BorderCache *pBorderCache,
	PixuctHTable *pInFaceCache,
	I32 inPieceOffset,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	BufMesh *pBufMesh
) {
	StucErr err = PIX_ERR_SUCCESS;
	BufVertType type = 0;
	I32 vert = -1;
	switch (pCorner->type) {
		case PLYCUT_ORIGIN_SUBJECT:
			vert = addMapVert(
				pBasic,
				pBorderCache,
				pInPiece, pInFaceCache,
				pBufMesh,
				pMapFace, pCorner->info.origin.corner.corner,
				&type
			);
			PIX_ERR_RETURN_IFNOT_COND(
				err,
				vert != -1,
				"an exterior map corner shouldn't have been passed to this func"
			);
			break;
		case PLYCUT_ORIGIN_CLIP: {
			type = STUC_BUF_VERT_IN_OR_MAP;
			if (!inCornerPredicate(pBasic, pBorderCache, pCorner->info.origin.corner)) {
				break;
			}
			InFaceCorner inCorner =
				getInCornerFromPlycut(pBorderCache, pCorner->info.origin.corner);
			vert = addInVert(pBasic, pBufMesh, inCorner);
			break;
		}
		default:
			setIntersectBufVertInfo(
				pBasic,
				pBorderCache,
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
	const InPiece *pInPiece,
	const BorderCache *pBorderCache,
	PixuctHTable *pInFaceCache,
	I32 inPieceOffset,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutFaceRoot *pFace
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 faceStart = pBufMesh->corners.count;
	const PlycutCorner *pCorner = pFace->pRoot;
	I32 i = 0;
	do {
		PIX_ERR_RETURN_IFNOT_COND(err, i < pFace->size, "infinite or astray loop");
		err = bufMeshAddVert(
			pBasic,
			pInPiece,
			pBorderCache,
			pInFaceCache,
			inPieceOffset,
			pMapFace,
			pCorner,
			pBufMesh
		);
		PIX_ERR_RETURN_IFNOT(err, "");
	} while(++i, pCorner = pCorner->pNext, pCorner);
	I32 faceSize = pBufMesh->corners.count - faceStart;
	bufMeshAddFace(pBasic, inPieceOffset, pBufMesh, faceStart, faceSize);
	return err;
}

static
void addFacesToBufMesh(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	PixuctHTable *pInFaceCache,
	const FaceRange *pMapFace,
	const PlycutFaceArr *pFaces
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
				pInPiece,
				pBorderCache,
				pInFaceCache,
				inPieceOffset,
				pBufMesh,
				pMapFace,
				pFaces->pArr + i
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

static
StucErr addNonClipInPieceToBufMesh(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	PixuctHTable *pInFaceCache
) {
	StucErr err = PIX_ERR_SUCCESS;
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	FaceRange mapFace = stucGetFaceRange(&pMapMesh->core, pInPiece->pList->mapFace);
	I32 bufFaceStart = pBufMesh->corners.count;
	for (I32 i = 0; i < mapFace.size; ++i) {	
		BufVertType type = 0;
		I32 vert = 0;
		vert = addMapVert(
			pBasic,
			pBorderCache,
			pInPiece, pInFaceCache,
			pBufMesh,
			&mapFace, i,
			&type
		);
		if (vert != -1) {
			bufMeshAddCorner(pBasic, pBufMesh, type, vert);
			continue;
		}
		PIX_ERR_RETURN_IFNOT_COND(
			err,
			!i,
			"non-clipped map faces must be fully in or out"
		);
		return err;
	}
	bufMeshAddFace(pBasic, inPieceOffset, pBufMesh, bufFaceStart, mapFace.size);
	return err;
}

static
void inFaceCacheDestroy(const MapToMeshBasic *pBasic, PixuctHTable *pTable) {
	pixuctHTableDestroy(pTable);
}

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
void borderCacheAlloc(
	const StucAlloc *pAlloc,
	const BorderArr *pBorderArr,
	BorderCache *pCache
) {
	if (!pCache->size) {
		pCache->size = pBorderArr->count;
		pCache->pBorders = pAlloc->fpCalloc(pCache->size, sizeof(InFaceCornerArr));
	}
	else if (pBorderArr->count > pCache->size) {
		pCache->pBorders = pAlloc->fpRealloc(
			pCache->pBorders,
			pBorderArr->count * sizeof(InFaceCornerArr)
		);
		I32 diff = pBorderArr->count - pCache->size;
		memset(pCache->pBorders + pCache->size, 0, diff * sizeof(InFaceCornerArr));
		pCache->size = pBorderArr->count;
	}
	pCache->count = pBorderArr->count;
}

static
void borderCacheInit(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	PixuctHTable *pInFaceCache,
	BorderCache *pBorderCache
) {
	pBorderCache->pInPiece = pInPiece;
	borderCacheAlloc(&pBasic->pCtx->alloc, &pInPiece->borderArr, pBorderCache);
	for (I32 i = 0; i < pInPiece->borderArr.count; ++i) {
		InFaceCornerArr *pBorder = pBorderCache->pBorders + i;
		pBorder->count = 0;
		walkInPieceBorder(
			pBasic,
			pInPiece,
			pInFaceCache,
			pInPiece->borderArr.pArr[i].start,
			borderCacheAdd, pBorder 
		);
		PIX_ERR_ASSERT("", pBorder->count == pInPiece->borderArr.pArr[i].len);
	}
}

static
void borderCacheDestroy(const StucAlloc *pAlloc, BorderCache *pCache) {
	if (pCache->pBorders) {
		PIX_ERR_ASSERT("", pCache->size);
		for (I32 i = 0; i < pCache->size; ++i) {
			if (pCache->pBorders[i].pArr) {
				PIX_ERR_ASSERT("", pCache->pBorders[i].size);
				pAlloc->fpFree(pCache->pBorders[i].pArr);
			}
		}
		pAlloc->fpFree(pCache->pBorders);
	}
	*pCache = (BorderCache) {0};
}

static
V2_F32 getBorderCornerPos(
	const void *pUserData,
	const void *pMesh,
	PlycutInput input,
	I32 boundary,
	I32 corner,
	bool *pCantIntersect
) {
	const MapToMeshBasic *pBasic = pUserData;
	const BorderCache *pBorderCache = pMesh;
	InFaceCorner inCorner = pBorderCache->pBorders[boundary].pArr[corner];
	V2_F32 pos = stucGetUvPos(pBasic->pInMesh, &inCorner.pFace->face, inCorner.corner);
	V2_I16 tile = pBorderCache->pInPiece->pList->tile;
	V2_F32 fTile = {.d = {(F32)tile.d[0], (F32)tile.d[1]}};
	return _(pos V2SUB fTile);
}

static
V3_F32 getMapCornerPos(
	const void *pUserData,
	const void *pMeshVoid,
	PlycutInput input,
	I32 boundary,
	I32 corner,
	bool *pCantIntersect
) {
	const Mesh *pMesh = ((MapToMeshBasic *)pUserData)->pMap->pMesh;
	const FaceRange *pMapFace = input.pUserData;
	return stucGetVertPos(pMesh, pMapFace, corner);
}

StucErr stucClipMapFace(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache,
	void *pHTableAlc,
	void *pPlycutAlc
) {
	StucErr err = PIX_ERR_SUCCESS;
	FaceRange mapFace = 
		stucGetFaceRange(&pBasic->pMap->pMesh->core, pInPiece->pList->mapFace);

	InFaceCacheState inFaceCacheState = {.pBasic = pBasic, .initBounds = true};
	PixuctHTable inFaceCache = {0};
	pixuctHTableInit(
		&pBasic->pCtx->alloc,
		&inFaceCache,
		pInPiece->faceCount + 1,
		(I32Arr) {
			.pArr = (I32[]){sizeof(InFaceCacheEntryIntern), sizeof(HalfPlane)},
			.count = 2
		},
		pHTableAlc,
		&inFaceCacheState,
		false
	);
	getInPieceBounds(pBasic, pInPiece, &inFaceCache);

	borderCacheInit(pBasic, pInPiece, &inFaceCache, pBorderCache);

	PlycutInput inInput = {.boundaries = pInPiece->borderArr.count};
	inInput.pSizes = pBasic->pCtx->alloc.fpMalloc(inInput.boundaries * sizeof(I32));
	for (I32 i = 0; i < inInput.boundaries; ++i) {
		inInput.pSizes[i] = pInPiece->borderArr.pArr[i].len;
	}
	PlycutInput mapInput = {.pSizes = &mapFace.size, .boundaries = 1, .pUserData = &mapFace};
	PlycutFaceArr out = {0};
	plycutClip(
		&pBasic->pCtx->alloc,
		pBasic,
		pBorderCache, inInput, getBorderCornerPos,
		NULL, mapInput, getMapCornerPos,
		&out,
		NULL,
		pPlycutAlc
	);
	if (out.count) {
		addFacesToBufMesh(
			pBasic,
			pBorderCache,
			inPieceOffset,
			pInPiece,
			pBufMesh,
			&inFaceCache,
			&mapFace,
			&out
		);
	}
	PIX_ERR_CATCH(0, err, 
		err = PIX_ERR_SUCCESS; //skipping this face, reset err
	);
	plycutFaceArrDestroy(&pBasic->pCtx->alloc, &out);
	inFaceCacheDestroy(pBasic, &inFaceCache);
	return err;
}

StucErr stucAddMapFaceToBufMesh(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache,
	void *pHTableAlc,
	void *pPlycutAlc //unused, needed for function callback
) {
	StucErr err = PIX_ERR_SUCCESS;

	InFaceCacheState inFaceCacheState = {.pBasic = pBasic, .initBounds = true};
	PixuctHTable inFaceCache = {0};
	pixuctHTableInit(
		&pBasic->pCtx->alloc,
		&inFaceCache,
		pInPiece->faceCount + 1,
		(I32Arr) {
			.pArr = (I32[]){sizeof(InFaceCacheEntryIntern), sizeof(HalfPlane)},
			.count = 2
		},
		pHTableAlc,
		&inFaceCacheState,
		false
	);

	err = addNonClipInPieceToBufMesh(
		pBasic,
		pBorderCache,
		inPieceOffset,
		pInPiece,
		pBufMesh,
		&inFaceCache
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err,
		err = PIX_ERR_SUCCESS; //reset err (skipping face)
	);
	inFaceCacheDestroy(pBasic, &inFaceCache);
	return err;
}

typedef struct BufMeshInitJobArgs {
	JobArgs core;
	StucErr (* fpAddPiece)(
		const MapToMeshBasic *,
		I32,
		const InPiece *,
		BufMesh *,
		BorderCache *,
		void *,
		void *
	);
	const InPieceArr *pInPiecesSplit;
	BufMesh bufMesh;
} BufMeshInitJobArgs;

StucErr stucBufMeshInit(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	BufMeshInitJobArgs *pArgs = pArgsVoid;

	BorderCache borderCache = {0};

	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	const MapToMeshBasic *pBasic = (const MapToMeshBasic *)pArgs->core.pShared;
	PixuctHTableMem hTableAlc = {0};
	PlycutMem plycutAlc = {0};
	for (I32 i = 0; i < rangeSize; ++i) {
		I32 inPieceIdx = pArgs->core.range.start + i;
		pArgs->fpAddPiece(
			pBasic,
			inPieceIdx,
			pArgs->pInPiecesSplit->pArr + inPieceIdx,
			&pArgs->bufMesh,
			&borderCache,
			&hTableAlc,
			&plycutAlc
		);
	}
	pixuctHTableMemDestroy(&hTableAlc);
	plycutMemDestroy(&plycutAlc);
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	borderCacheDestroy(pAlloc, &borderCache);
	return err;
}


SrcFaces stucGetSrcFacesForBufCorner(
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner corner
) {
	BufFace bufFace = pBufMesh->faces.pArr[corner.face];
	BufCorner bufCorner = pBufMesh->corners.pArr[bufFace.start + corner.corner];
	SrcFaces faces = {.map = pInPiece->pList->mapFace};
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
	InPieceArr *pInPiecesSplit;
	StucErr (* fpAddPiece)(
		const MapToMeshBasic *,
		I32,
		const InPiece *,
		BufMesh *,
		BorderCache *,
		void *,
		void *
	);
} BufMeshJobInitInfo;

static
I32 bufMeshInitJobsGetRange(StucContext pCtx, const void *pShared, void *pInitInfoVoid) {
	return ((BufMeshJobInitInfo *)pInitInfoVoid)->pInPiecesSplit->count;
}

static
void bufMeshInitJobInit(StucContext pCtx, void *pShared, void *pInitInfoVoid, void *pEntryVoid) {
	BufMeshInitJobArgs *pEntry = pEntryVoid;
	BufMeshJobInitInfo *pInitInfo = pInitInfoVoid;
	pEntry->pInPiecesSplit = pInitInfo->pInPiecesSplit;
	pEntry->fpAddPiece = pInitInfo->fpAddPiece;
}


static
void bufMeshArrMoveToInPieces(
	const InPieceArr *pInPieces,
	const BufMeshInitJobArgs *pJobArgs,
	I32 jobCount
) {
	BufMeshArr *pBufMeshes = pInPieces->pBufMeshes;
	pBufMeshes->count = jobCount;
	for (I32 i = 0; i < jobCount; ++i) {
		pBufMeshes->arr[i] = pJobArgs[i].bufMesh;
	}
}

StucErr stucInPieceArrInitBufMeshes(
	MapToMeshBasic *pBasic,
	InPieceArr *pInPieces,
	StucErr (* fpAddPiece)(
		const MapToMeshBasic *,
		I32,
		const InPiece *,
		BufMesh *,
		BorderCache *,
		void *,
		void *
	)
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	BufMeshInitJobArgs jobArgs[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pBasic->pCtx,
		pBasic,
		&jobCount, jobArgs, sizeof(BufMeshInitJobArgs),
		&(BufMeshJobInitInfo) {.pInPiecesSplit = pInPieces, .fpAddPiece = fpAddPiece},
		bufMeshInitJobsGetRange, bufMeshInitJobInit);
	err = stucDoJobInParallel(
		pBasic->pCtx,
		jobCount, jobArgs, sizeof(BufMeshInitJobArgs),
		stucBufMeshInit
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	bufMeshArrMoveToInPieces(pInPieces, jobArgs, jobCount);
	return err;
}

void stucBufMeshArrDestroy(StucContext pCtx, BufMeshArr *pArr) {
	for (I32 i = 0; i < pArr->count; ++i) {
		if (pArr->arr[i].faces.pArr) {
			pCtx->alloc.fpFree(pArr->arr[i].faces.pArr);
		}
		if (pArr->arr[i].corners.pArr) {
			pCtx->alloc.fpFree(pArr->arr[i].corners.pArr);
		}
		if (pArr->arr[i].inOrMapVerts.pArr) {
			pCtx->alloc.fpFree(pArr->arr[i].inOrMapVerts.pArr);
		}
		if (pArr->arr[i].onEdgeVerts.pArr) {
			pCtx->alloc.fpFree(pArr->arr[i].onEdgeVerts.pArr);
		}
		if (pArr->arr[i].overlapVerts.pArr) {
			pCtx->alloc.fpFree(pArr->arr[i].overlapVerts.pArr);
		}
		if (pArr->arr[i].intersectVerts.pArr) {
			pCtx->alloc.fpFree(pArr->arr[i].intersectVerts.pArr);
		}
		pArr->arr[i] = (BufMesh) {0};
	}
}

