/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

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
	bool wind;
} InFaceCacheInitInfo;

typedef struct InFaceCacheState {
	const MapToMeshBasic *pBasic;
	bool initBounds;
} InFaceCacheState;

static
void inFaceCacheEntryInit(
	void *pUserData,
	HTableEntryCore *pEntryVoid,
	const void *pKeyData,
	void *pInitInfoVoid,
	I32 linAlloc
) {
	InFaceCacheState *pState = pUserData;
	InFaceCacheInitInfo *pInitInfo = pInitInfoVoid;
	InFaceCacheEntry *pEntry = (InFaceCacheEntry *)pEntryVoid;
	pEntry->face = stucGetFaceRange(&pState->pBasic->pInMesh->core, *(I32 *)pKeyData);
	if (pState->initBounds) {
		FaceBounds bounds = { 0 };
		stucGetInFaceBounds(&bounds, pState->pBasic->pInMesh->pUvs, pEntry->face);
		pEntry->fMin = bounds.fBBox.min;
		pEntry->fMax = bounds.fBBox.max;
	}
	pEntry->wind = pInitInfo->wind;
}

static
bool inFaceCacheEntryCmp(
	const HTableEntryCore *pEntry,
	const void *pKeyData,
	const void *pDataToAdd
) {
	return ((InFaceCacheEntry *)pEntry)->face.idx == *(I32 *)pKeyData;
}

static
SearchResult inFaceCacheGet(
	HTable *pCache,
	I32 face,
	bool addEntry,
	bool wind,
	InFaceCacheEntry **ppEntry
) {
	return stucHTableGet(
		pCache,
		0,
		&face,
		(void**)ppEntry,
		addEntry, &(InFaceCacheInitInfo) {.wind = wind},
		stucKeyFromI32, NULL, inFaceCacheEntryInit, inFaceCacheEntryCmp
	);
}

static
InFaceCorner getAdjFaceInPiece(
	const MapToMeshBasic *pBasic,
	HTable *pInFaceCache,
	InFaceCorner corner
) {
	I32 edge = stucGetMeshEdge(
		&pBasic->pInMesh->core, (FaceCorner) {
			.face = corner.pFace->face.idx,
			.corner = corner.corner
		}
	);
	/*
	if (stucCouldInEdgeIntersectMapFace(pBasic->pInMesh, edge)) {
		return (InFaceCorner) {.pFace = NULL, .corner = -1};
	}
	*/
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
		adjCorner.face,
		false,
		&(InFaceCacheInitInfo) {.wind = 0},
		&pAdjEntry
	);
	if (!pAdjEntry) {
		return (InFaceCorner) {.pFace = NULL, .corner = -1};
	}
	adjCorner.corner = stucGetCornerNext(adjCorner.corner, &pAdjEntry->face);
	return (InFaceCorner) {.pFace = pAdjEntry, .corner = adjCorner.corner};
}

static
HalfPlane *getInCornerCache(
	const MapToMeshBasic *pBasic,
	InFaceCacheEntry *pInFaceCacheEntry
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	InFaceCacheEntryIntern *pFaceEntry = (InFaceCacheEntryIntern *)pInFaceCacheEntry;
	if (!pFaceEntry->pCorners) {
		pFaceEntry->pCorners =
			pAlloc->fpCalloc(pFaceEntry->faceEntry.face.size, sizeof(HalfPlane));
		initHalfPlaneLookup(
			pBasic->pInMesh,
			&pFaceEntry->faceEntry.face,
			pFaceEntry->pCorners
		);
	}
	return pFaceEntry->pCorners;
}

//TODO remove and replace references with bordercache loop (add adj info to bordercache?)
static
StucErr walkInPieceBorder(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	HTable *pInFaceCache,
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
		startCorner.face,
		false,
		&(InFaceCacheInitInfo) {.wind = 0},
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
	HTable *pInFaceCache
) {
	EncasingInFaceArr *pInFaces = &pInPiece->pList->inFaces;
	for (I32 i = 0; i < pInFaces->count; ++i) {
		InFaceCacheEntry *pCacheEntry = NULL;
		inFaceCacheGet(
			pInFaceCache,
			pInFaces->pArr[i].idx,
			true,
			&(InFaceCacheInitInfo) {.wind = pInFaces->pArr[i].wind},
			&pCacheEntry
		);
		PIX_ERR_ASSERT("", pCacheEntry);
	}
}

static
I32 getFaceEncasingVert(
	const MapToMeshBasic *pBasic,
	V2_F32 vert,
	const InPiece *pInPiece,
	HTable *pInFaceCache
) {
	EncasingInFaceArr *pInFaces = &pInPiece->pList->inFaces;
	for (I32 i = 0; i < pInFaces->count; ++i) {
		InFaceCacheEntry *pInFaceEntry = NULL;
		inFaceCacheGet(
			pInFaceCache,
			pInFaces->pArr[i].idx,
			true,
			&(InFaceCacheInitInfo) {.wind = pInFaces->pArr[i].wind},
			&pInFaceEntry
		);
		if (!_(vert V2GREATEQL pInFaceEntry->fMin) || !_(vert V2LESSEQL pInFaceEntry->fMax)) {
			continue;
		}
		HalfPlane *pInCornerCache = getInCornerCache(pBasic, pInFaceEntry);
		bool inside = true;
		for (I32 j = 0; j < pInFaceEntry->face.size; ++j) {
			InsideStatus status = stucIsPointInHalfPlane(
				vert,
				pInCornerCache[j].uv,
				pInCornerCache[j].halfPlane,
				1
			);
			if (status == STUC_INSIDE_STATUS_OUTSIDE) {
				inside = false;
				break;
			}
		}
		if (inside) {
			return pInFaceEntry->face.idx;
		}
	}
	return -1;
}

static
I32 bufMeshAddInOrMapVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertInOrMapArr *pVertArr = &pBufMesh->inOrMapVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(InOrMapVert, &pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAddOnEdgeVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOnEdgeArr *pVertArr = &pBufMesh->onEdgeVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(BufVertOnEdge, &pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAddOverlapVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOverlapArr *pVertArr = &pBufMesh->overlapVerts;
	I32 newVert = -1;
	PIXALC_DYN_ARR_ADD(OverlapVert, &pBasic->pCtx->alloc, pVertArr, newVert);
	PIX_ERR_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAddIntersectVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
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
void setIntersectBufVertInfo(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	BufVertType *pType,
	I32 *pVert
) {
	//IntersectCorner *pCornerCast = (IntersectCorner *)pCorner;
	FaceCorner mapCorner = (FaceCorner){
		.face = pMapFace->idx,
	};
	switch (pCorner->type) {
		case PLYCUT_INTERSECT: {
			*pType = STUC_BUF_VERT_INTERSECT;
			*pVert = bufMeshAddIntersectVert(pBasic, pBufMesh);
			const PlycutInfoIntersect *pInfo = &pCorner->info.intersect;
			InFaceCorner inCorner =
				getInCornerFromPlycut(pBorderCache, pInfo->clipCorner);
			pBufMesh->intersectVerts.pArr[*pVert] = (IntersectVert){
				.pos = *(V2_F32 *)&pCorner->pos,
				.inFace = inCorner.pFace->face.idx,
				.inCorner = inCorner.corner,
				.mapCorner = pInfo->subjCorner.corner,
				.tInEdge = pInfo->clipAlpha,
				.tMapEdge = pInfo->subjAlpha,
			};
			break;
		}
		case PLYCUT_ON_CLIP_EDGE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			*pVert = bufMeshAddOnEdgeVert(pBasic, pBufMesh);
			const PlycutInfoOnEdge *pInfo = &pCorner->info.onEdge;
			InFaceCorner inCorner =
				getInCornerFromPlycut(pBorderCache, pInfo->edgeCorner);
			pBufMesh->onEdgeVerts.pArr[*pVert].map = (EdgeMapVert) {
				.type = STUC_BUF_VERT_SUB_TYPE_EDGE_MAP,
				.mapCorner = pInfo->vertCorner.corner,
				.inCorner = inCorner.corner,
				.inFace = inCorner.pFace->face.idx,
				.tInEdge = pInfo->alpha
			};
			break;
		}
		case PLYCUT_ON_SUBJECT_EDGE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			*pVert = bufMeshAddOnEdgeVert(pBasic, pBufMesh);
			const PlycutInfoOnEdge *pInfo = &pCorner->info.onEdge;
			InFaceCorner inCorner =
				getInCornerFromPlycut(pBorderCache, pInfo->vertCorner);
			pBufMesh->onEdgeVerts.pArr[*pVert].in = (EdgeInVert) {
				.type = STUC_BUF_VERT_SUB_TYPE_EDGE_IN,
				.mapCorner = pInfo->edgeCorner.corner,
				.inCorner = inCorner.corner,
				.inFace = inCorner.pFace->face.idx,
				.tMapEdge = pInfo->alpha
			};
			break;
		}
		case PLYCUT_ON_VERT: {
			*pType = STUC_BUF_VERT_OVERLAP;
			*pVert = bufMeshAddOverlapVert(pBasic, pBufMesh);
			const PlycutInfoOnVert *pInfo = &pCorner->info.onVert;
			InFaceCorner inCorner =
				getInCornerFromPlycut(pBorderCache, pInfo->clipCorner);
			pBufMesh->overlapVerts.pArr[*pVert] = (OverlapVert) {
				.inFace = inCorner.pFace->face.idx,
				.inCorner = inCorner.corner, 
				.mapCorner = pInfo->subjCorner.corner
			};
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
StucErr findEncasingInPieceFace(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	HTable *pInFaceCache,
	const FaceRange *pMapFace,
	I32 mapCorner,
	I32 *pInFace
) {
	StucErr err = PIX_ERR_SUCCESS;
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	V2_F32 pos = *(V2_F32 *)&pMapMesh->pPos[
		pMapMesh->core.pCorners[pMapFace->start + mapCorner]
	];
	*pInFace = getFaceEncasingVert(pBasic, pos, pInPiece, pInFaceCache);
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		*pInFace != -1,
		"an exterior map corner shouldn't have been passed to this func"
	);
	return err;
}

static
StucErr bufMeshAddVert(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BorderCache *pBorderCache,
	HTable *pInFaceCache,
	I32 inPieceOffset,
	const FaceRange *pMapFace,
	const PlycutCorner *pCorner,
	BufMesh *pBufMesh
) {
	StucErr err = PIX_ERR_SUCCESS;
	BufCornerArr *pCorners = &pBufMesh->corners;
	I32 newCorner = -1;
	PIXALC_DYN_ARR_ADD(BufCorner, &pBasic->pCtx->alloc, pCorners, newCorner);
	PIX_ERR_ASSERT("", newCorner >= 0);
	BufVertType type = 0;
	I32 vert = -1;
	switch (pCorner->type) {
		case PLYCUT_ORIGIN_SUBJECT: {
			type = STUC_BUF_VERT_IN_OR_MAP;
			vert = bufMeshAddInOrMapVert(pBasic, pBufMesh);
			I32 inFace = 0;
			err = findEncasingInPieceFace(
				pBasic,
				pInPiece,
				pInFaceCache,
				pMapFace, pCorner->info.origin.corner.corner,
				&inFace
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			pBufMesh->inOrMapVerts.pArr[vert].map = (MapVert){
				.type = STUC_BUF_VERT_SUB_TYPE_MAP,
				.mapCorner = pCorner->info.origin.corner.corner,
				.inFace = inFace
			};
			break;
		}
		case PLYCUT_ORIGIN_CLIP: {
			type = STUC_BUF_VERT_IN_OR_MAP;
			vert = bufMeshAddInOrMapVert(pBasic, pBufMesh);
			InFaceCorner inCorner =
				getInCornerFromPlycut(pBorderCache, pCorner->info.origin.corner);
			pBufMesh->inOrMapVerts.pArr[vert].in = (InVert){
				.type = STUC_BUF_VERT_SUB_TYPE_IN,
				.inCorner = inCorner.corner,
				.inFace = inCorner.pFace->face.idx
			};
			break;
		}
		default:
			setIntersectBufVertInfo(
				pBasic,
				pBorderCache,
				pBufMesh,
				pMapFace,
				pCorner,
				&type,
				&vert
			);
			break;
	}
	PIX_ERR_ASSERT("", vert != -1);
	pCorners->pArr[newCorner].type = type;
	pCorners->pArr[newCorner].vert = vert;
	PIX_ERR_CATCH(0, err,
		--pCorners->count;
	);
	return err;
}

static
StucErr addFaceToBufMesh(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BorderCache *pBorderCache,
	HTable *pInFaceCache,
	I32 inPieceOffset,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const PlycutFaceRoot *pFace
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 faceCountPrior = pBufMesh->faces.count;
	bufMeshAddFace(pBasic, inPieceOffset, pBufMesh, pBufMesh->corners.count, pFace->size);
	const PlycutCorner *pCorner = pFace->pRoot;
	I32 i = 0;
	do {
		PIX_ERR_THROW_IFNOT_COND(err, i < pFace->size, "infinite or astray loop", 0);
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
		PIX_ERR_THROW_IFNOT(err, "", 0);
	} while(++i, pCorner = pCorner->pNext, pCorner);
	PIX_ERR_CATCH(0, err,
		--pBufMesh->faces.count;
	);
	return err;
}

static
bool inCornerPredicate(
	const MapToMeshBasic *pBasic,
	InFaceCorner inCorner,
	InFaceCorner adjInCorner,
	I32 walkIdx,
	bool adj
) {
	if (walkIdx && adj) {//adj isn't set on first walk iteration
		return false;
	}
	bool single = !adjInCorner.pFace;
	I32 vert = pBasic->pInMesh->core.pCorners[
		inCorner.pFace->face.start + inCorner.corner
	];
	return single || stucCheckIfVertIsPreserve(pBasic->pInMesh, vert);
}

static
void addFacesToBufMesh(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	HTable *pInFaceCache,
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

static
void addInOrMapVertToBufMesh(
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	IntersectType type,
	I32 corner,
	I32 inFace
) {
	BufCornerArr *pCorners = &pBufMesh->corners;
	I32 newCorner = -1;
	PIXALC_DYN_ARR_ADD(BufCorner, &pBasic->pCtx->alloc, pCorners, newCorner);
	PIX_ERR_ASSERT("", newCorner >= 0);
	I32 newVert = bufMeshAddInOrMapVert(pBasic, pBufMesh);
	switch (type) {
		case STUC_CORNER_IN:
			pBufMesh->inOrMapVerts.pArr[newVert].in = (InVert){
				.type = STUC_BUF_VERT_SUB_TYPE_IN,
				.inCorner = corner,
				.inFace = inFace
			};
			break;
		case STUC_CORNER_MAP:
			pBufMesh->inOrMapVerts.pArr[newVert].map = (MapVert){
				.type = STUC_BUF_VERT_SUB_TYPE_MAP,
				.mapCorner = corner,
				.inFace = inFace 
			};
			break;
	}
	pCorners->pArr[newCorner].type = STUC_BUF_VERT_IN_OR_MAP;
	pCorners->pArr[newCorner].vert = newVert;
}

typedef struct GetExteriorBorderArgs {
	I32 border;
	I32 lowestBorder;
	V2_F32 lowestPos;
} GetExteriorBorderArgs;

static
bool getLowestBorder(
	const MapToMeshBasic *pBasic,
	void *pArgsVoid,
	InFaceCorner inCorner,
	InFaceCorner adjInCorner,
	I32 borderEdge,
	I32 walkIdx,
	bool adj
) {
	GetExteriorBorderArgs *pArgs = pArgsVoid;
	if (!walkIdx || (!adj && !adjInCorner.pFace)) {
		V2_F32 pos =
			stucGetUvPos(pBasic->pInMesh, &inCorner.pFace->face, inCorner.corner);
		if (pos.d[0] <= pArgs->lowestPos.d[0] &&
			pos.d[1] < pArgs->lowestPos.d[1]
		) {
			pArgs->lowestPos = pos;
			pArgs->lowestBorder = pArgs->border;
		}
	}
	return false;
}

static
I32 getExteriorBorder(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	HTable *pInFaceCache
) {
	GetExteriorBorderArgs args = {.lowestPos = {.d = {FLT_MAX, FLT_MAX}}};
	for (I32 i = 0; i < pInPiece->borderArr.count; ++i) {
		args.border = i;
		walkInPieceBorder(
			pBasic,
			pInPiece,
			pInFaceCache,
			pInPiece->borderArr.pArr[i].start,
			getLowestBorder, &args
		);
	}
	return args.lowestBorder;
}

static
bool addInCornerIfValid(
	const MapToMeshBasic *pBasic,
	void *pArgsVoid,
	InFaceCorner inCorner,
	InFaceCorner adjInCorner,
	I32 borderEdge,
	I32 walkIdx,
	bool adj
) {
	BufMesh *pBufMesh = pArgsVoid;
	if (inCornerPredicate(pBasic, inCorner, adjInCorner, walkIdx, adj)) {
		addInOrMapVertToBufMesh(
			pBasic,
			pBufMesh,
			STUC_CORNER_IN,
			inCorner.corner, inCorner.pFace->face.idx
		);
	}
	return false;
}

static
StucErr addNonClipInPieceToBufMesh(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	HTable *pInFaceCache
) {
	StucErr err = PIX_ERR_SUCCESS;
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	FaceRange mapFace = stucGetFaceRange(&pMapMesh->core, pInPiece->pList->mapFace);
	I32 bufFaceStart = pBufMesh->corners.count;
	for (I32 i = 0; i < mapFace.size; ++i) {
		V2_F32 vert =
			*(V2_F32 *)&pMapMesh->pPos[pMapMesh->core.pCorners[mapFace.start + i]];
		I32 encasingInFace = getFaceEncasingVert(pBasic, vert, pInPiece, pInFaceCache);
		if (encasingInFace == -1) {
			PIX_ERR_RETURN_IFNOT_COND(
				err,
				!i,
				"non-clipped map faces must be fully in or out"
			);
			//map-face encloses in-faces
			I32 border = getExteriorBorder(pBasic, pInPiece, pInFaceCache);
			walkInPieceBorder(
				pBasic,
				pInPiece,
				pInFaceCache,
				pInPiece->borderArr.pArr[border].start,
				addInCornerIfValid, pBufMesh
			);
			break;
		}
		addInOrMapVertToBufMesh(pBasic, pBufMesh, STUC_CORNER_MAP, i, encasingInFace);
	}
	bufMeshAddFace(pBasic, inPieceOffset, pBufMesh, bufFaceStart, mapFace.size);
	return err;
}

static
void inFaceCacheDestroy(const MapToMeshBasic *pBasic, HTable *pTable
) {
	PixalcLinAlloc *pAlloc = stucHTableAllocGet(pTable, 0);
	PixalcLinAllocIter iter = {0};
	pixalcLinAllocIterInit(pAlloc, (Range) {0, INT32_MAX}, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		InFaceCacheEntryIntern *pEntry = pixalcLinAllocGetItem(&iter);
		if (pEntry->pCorners) {
			pBasic->pCtx->alloc.fpFree(pEntry->pCorners);
			pEntry->pCorners = NULL;
		}
	}
	stucHTableDestroy(pTable);
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
	HTable *pInFaceCache,
	BorderCache *pBorderCache
) {
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
	I32 edge = pBasic->pInMesh->core.pEdges[inCorner.pFace->face.start + inCorner.corner];
	*pCantIntersect = !stucCouldInEdgeIntersectMapFace(pBasic->pInMesh, edge);
	return stucGetUvPos(pBasic->pInMesh, &inCorner.pFace->face, inCorner.corner);
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
	BorderCache *pBorderCache
) {
	StucErr err = PIX_ERR_SUCCESS;
	FaceRange mapFace = 
		stucGetFaceRange(&pBasic->pMap->pMesh->core, pInPiece->pList->mapFace);

	bool mapFaceWind = stucCalcFaceWindFromVerts(&mapFace, pBasic->pMap->pMesh);

	InFaceCacheState inFaceCacheState = {.pBasic = pBasic, .initBounds = true};
	HTable inFaceCache = {0};
	stucHTableInit(
		&pBasic->pCtx->alloc,
		&inFaceCache,
		pInPiece->faceCount / 2 + 1,
		(I32Arr) {.pArr = (I32[]) {sizeof(InFaceCacheEntryIntern)}, .count = 1 },
		&inFaceCacheState
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
		&out
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
	inFaceCacheDestroy(pBasic, &inFaceCache);
	return err;
}

StucErr stucAddMapFaceToBufMesh(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache
) {
	StucErr err = PIX_ERR_SUCCESS;

	InFaceCacheState inFaceCacheState = {.pBasic = pBasic, .initBounds = true};
	HTable inFaceCache = {0};
	stucHTableInit(
		&pBasic->pCtx->alloc,
		&inFaceCache,
		pInPiece->faceCount / 2 + 1,
		(I32Arr) {.pArr = (I32[]) {sizeof(InFaceCacheEntryIntern)}, .count = 1 },
		&inFaceCacheState
	);

	err = addNonClipInPieceToBufMesh(
		pBasic,
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
		BorderCache *
	);
	const InPieceArr *pInPiecesSplit;
	BufMesh bufMesh;
} BufMeshInitJobArgs;

StucErr stucBufMeshInit(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	BufMeshInitJobArgs *pArgs = pArgsVoid;

	BorderCache borderCache = {0};

	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	for (I32 i = 0; i < rangeSize; ++i) {
		I32 inPieceIdx = pArgs->core.range.start + i;
		pArgs->fpAddPiece(
			pArgs->core.pBasic,
			inPieceIdx,
			pArgs->pInPiecesSplit->pArr + inPieceIdx,
			&pArgs->bufMesh,
			&borderCache
		);
	}
	const StucAlloc *pAlloc = &pArgs->core.pBasic->pCtx->alloc;
	borderCacheDestroy(pAlloc, &borderCache);
	return err;
}


SrcFaces stucGetSrcFacesForBufCorner(
	const MapToMeshBasic *pBasic,
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
		BorderCache *
	);
} BufMeshJobInitInfo;

static
I32 bufMeshInitJobsGetRange(const MapToMeshBasic *pBasic, void *pInitInfoVoid) {
	return ((BufMeshJobInitInfo *)pInitInfoVoid)->pInPiecesSplit->count;
}

static
void bufMeshInitJobInit(MapToMeshBasic *pBasic, void *pInitInfoVoid, void *pEntryVoid) {
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
		BorderCache *
	)
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	BufMeshInitJobArgs jobArgs[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pBasic,
		&jobCount, jobArgs, sizeof(BufMeshInitJobArgs),
		&(BufMeshJobInitInfo) {.pInPiecesSplit = pInPieces, .fpAddPiece = fpAddPiece},
		bufMeshInitJobsGetRange, bufMeshInitJobInit);
	err = stucDoJobInParallel(
		pBasic,
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

