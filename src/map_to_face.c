/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <context.h>
#include <map_to_job_mesh.h>
#include <map.h>
#include <attrib_utils.h>
#include <clip.h>

static
void initInTri(
	BaseTriVerts *pInTri,
	Mesh *pInMesh,
	FaceRange *pInFace,
	V2_F32 fTileMin
) {
	for (I32 i = 0; i < pInFace->size; ++i) {
		I32 corner = pInFace->start + i;
		pInTri->uv[i] = _(pInMesh->pUvs[corner] V2SUB fTileMin);
		pInTri->xyz[i] = pInMesh->pPos[pInMesh->core.pCorners[corner]];
	}
	stucGetTriScale(pInFace->size, pInTri);
}

static
void getCellMapFaces(
	const MapToMeshBasic *pBasic,
	const FaceCells *pFaceCellsEntry,
	I32 faceCellsIdx,
	const I32 **ppCellFaces,
	Range *pRange
) {
	I32 cellIdx = pFaceCellsEntry->pCells[faceCellsIdx];
	const Cell *pCell = pBasic->pMap->quadTree.cellTable.pArr + cellIdx;
	STUC_ASSERT("", pCell->localIdx >= 0 && pCell->localIdx < 4);
	STUC_ASSERT("", pCell->initialized % 2 == pCell->initialized);
	Range range = {0};
	if (pFaceCellsEntry->pCellType[faceCellsIdx]) {
		*ppCellFaces = pCell->pEdgeFaces;
		range = pFaceCellsEntry->pRanges[faceCellsIdx];
	}
	else if (pFaceCellsEntry->pCellType[faceCellsIdx] != 1) {
		*ppCellFaces = pCell->pFaces;
		range.start = 0;
		range.end = pCell->faceSize;
	}
	else {
		*ppCellFaces = NULL;
	}
	*pRange = range;
}

static
bool isTriDegenerate(const BaseTriVerts *pTri, const FaceRange *pFace) {
	if (pFace->size == 4) {
		if (v2F32DegenerateTri(pTri->uv[0], pTri->uv[2], pTri->uv[1], .0f) ||
			v3F32DegenerateTri(pTri->xyz[0], pTri->xyz[2], pTri->xyz[1], .0f) ||
			v2F32DegenerateTri(pTri->uv[1], pTri->uv[3], pTri->uv[2], .0f) ||
			v3F32DegenerateTri(pTri->xyz[1], pTri->xyz[3], pTri->xyz[2], .0f) ||
			v2F32DegenerateTri(pTri->uv[2], pTri->uv[0], pTri->uv[3], .0f) ||
			v3F32DegenerateTri(pTri->xyz[2], pTri->xyz[0], pTri->xyz[3], .0f) ||
			v2F32DegenerateTri(pTri->uv[3], pTri->uv[1], pTri->uv[0], .0f) ||
			v3F32DegenerateTri(pTri->xyz[3], pTri->xyz[1], pTri->xyz[0], .0f)
		) {
			return true;
		}
	}
	else {
		if (v2F32DegenerateTri(pTri->uv[0], pTri->uv[1], pTri->uv[2], .0f) ||
			v3F32DegenerateTri(pTri->xyz[0], pTri->xyz[1], pTri->xyz[2], .0f)
		) {
			return true;
		}
	}
	return false;
}

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
	if (couldInEdgeIntersectMapFace(pBasic->pInMesh, edge)) {
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
Result walkInPieceBorder(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	HTable *pInFaceCache,
	FaceCorner startCorner,
	bool (* fpFunc)(
		const MapToMeshBasic *, void *, InFaceCorner, InFaceCorner, I32, I32, bool
	),
	void *pFuncArgs
) {
	Result err = STUC_SUCCESS;
	InFaceCorner inCorner = {.corner = startCorner.corner};
	inFaceCacheGet(
		pInFaceCache,
		startCorner.face,
		false,
		&(InFaceCacheInitInfo) {.wind = 0},
		&inCorner.pFace
	);
	STUC_ASSERT("", inCorner.pFace);
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
		STUC_RETURN_ERR_IFNOT_COND(
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
		STUC_ASSERT("", pCacheEntry);
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
	STUC_DYN_ARR_ADD(InOrMapVert, &pBasic->pCtx->alloc, pVertArr, newVert);
	STUC_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAddOnEdgeVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOnEdgeArr *pVertArr = &pBufMesh->onEdgeVerts;
	I32 newVert = -1;
	STUC_DYN_ARR_ADD(BufVertOnEdge, &pBasic->pCtx->alloc, pVertArr, newVert);
	STUC_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAddOverlapVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOverlapArr *pVertArr = &pBufMesh->overlapVerts;
	I32 newVert = -1;
	STUC_DYN_ARR_ADD(OverlapVert, &pBasic->pCtx->alloc, pVertArr, newVert);
	STUC_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAddIntersectVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertIntersectArr *pVertArr = &pBufMesh->intersectVerts;
	I32 newVert = -1;
	STUC_DYN_ARR_ADD(IntersectVert, &pBasic->pCtx->alloc, pVertArr, newVert);
	STUC_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 getInFaceFromClipCorner(const BorderCache *pBorderCache, ClipCornerIdx idx) {
	return pBorderCache->pBorders[idx.boundary].pArr[idx.corner].pFace->face.idx;
}

static
void setIntersectBufVertInfo(
	const MapToMeshBasic *pBasic,
	const BorderCache *pBorderCache,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const ClipCorner *pCorner,
	BufVertType *pType,
	I32 *pVert
) {
	//IntersectCorner *pCornerCast = (IntersectCorner *)pCorner;
	FaceCorner mapCorner = (FaceCorner){
		.face = pMapFace->idx,
	};
	switch (pCorner->type) {
		case CLIP_INTERSECT: {
			*pType = STUC_BUF_VERT_INTERSECT;
			*pVert = bufMeshAddIntersectVert(pBasic, pBufMesh);
			const ClipInfoIntersect *pInfo = &pCorner->info.intersect;
			pBufMesh->intersectVerts.pArr[*pVert] = (IntersectVert){
				.pos = *(V2_F32 *)&pCorner->pos,
				.inFace = getInFaceFromClipCorner(pBorderCache, pInfo->clipCorner),
				.inCorner = pInfo->clipCorner.corner,
				.mapCorner = pInfo->subjCorner.corner,
				.tInEdge = pInfo->clipAlpha,
				.tMapEdge = pInfo->subjAlpha,
			};
			break;
		}
		case CLIP_ON_CLIP_EDGE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			*pVert = bufMeshAddOnEdgeVert(pBasic, pBufMesh);
			const ClipInfoOnEdge *pInfo = &pCorner->info.onEdge;
			pBufMesh->onEdgeVerts.pArr[*pVert].in = (EdgeInVert) {
				.type = STUC_BUF_VERT_SUB_TYPE_EDGE_IN,
				.mapCorner = pInfo->vertCorner.corner,
				.inCorner = pInfo->edgeCorner.corner,
				.inFace = getInFaceFromClipCorner(pBorderCache, pInfo->edgeCorner),
				.tMapEdge = pInfo->alpha
			};
			break;
		}
		case CLIP_ON_SUBJECT_EDGE: {
			*pType = STUC_BUF_VERT_ON_EDGE;
			*pVert = bufMeshAddOnEdgeVert(pBasic, pBufMesh);
			const ClipInfoOnEdge *pInfo = &pCorner->info.onEdge;
			pBufMesh->onEdgeVerts.pArr[*pVert].map = (EdgeMapVert) {
				.type = STUC_BUF_VERT_SUB_TYPE_EDGE_MAP,
				.mapCorner = pInfo->edgeCorner.corner,
				.inCorner = pInfo->vertCorner.corner,
				.inFace = getInFaceFromClipCorner(pBorderCache, pInfo->vertCorner),
				.tInEdge = pInfo->alpha
			};
			break;
		}
		case CLIP_ON_VERT: {
			*pType = STUC_BUF_VERT_OVERLAP;
			*pVert = bufMeshAddOverlapVert(pBasic, pBufMesh);
			const ClipInfoOnVert *pInfo = &pCorner->info.onVert;
			pBufMesh->overlapVerts.pArr[*pVert] = (OverlapVert) {
				.inFace = getInFaceFromClipCorner(pBorderCache, pInfo->clipCorner),
				.inCorner = pInfo->clipCorner.corner,
				.mapCorner = pInfo->subjCorner.corner
			};
			break;
		}
		default:
			STUC_ASSERT("invalid intersect corner type", false);
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
	STUC_DYN_ARR_ADD(BufFace, &pBasic->pCtx->alloc, (&pBufMesh->faces), newIdx);
	STUC_ASSERT("", newIdx != -1);
	pBufMesh->faces.pArr[newIdx].start = start;
	pBufMesh->faces.pArr[newIdx].size = faceSize;
	pBufMesh->faces.pArr[newIdx].inPiece = inPieceOffset;
}

static
Result findEncasingInPieceFace(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	HTable *pInFaceCache,
	const FaceRange *pMapFace,
	I32 mapCorner,
	I32 *pInFace
) {
	Result err = STUC_SUCCESS;
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	V2_F32 pos = *(V2_F32 *)&pMapMesh->pPos[
		pMapMesh->core.pCorners[pMapFace->start + mapCorner]
	];
	*pInFace = getFaceEncasingVert(pBasic, pos, pInPiece, pInFaceCache);
	STUC_RETURN_ERR_IFNOT_COND(
		err,
		*pInFace != -1,
		"an exterior map corner shouldn't have been passed to this func"
	);
	return err;
}

static
Result bufMeshAddVert(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BorderCache *pBorderCache,
	HTable *pInFaceCache,
	I32 inPieceOffset,
	const FaceRange *pMapFace,
	const ClipCorner *pCorner,
	BufMesh *pBufMesh
) {
	Result err = STUC_SUCCESS;
	BufCornerArr *pCorners = &pBufMesh->corners;
	I32 newCorner = -1;
	STUC_DYN_ARR_ADD(BufCorner, &pBasic->pCtx->alloc, pCorners, newCorner);
	STUC_ASSERT("", newCorner >= 0);
	BufVertType type = 0;
	I32 vert = -1;
	switch (pCorner->type) {
		case CLIP_ORIGIN_SUBJECT: {
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
			STUC_THROW_IFNOT(err, "", 0);
			pBufMesh->inOrMapVerts.pArr[vert].map = (MapVert){
				.type = STUC_BUF_VERT_SUB_TYPE_MAP,
				.mapCorner = pCorner->info.origin.corner.corner,
				.inFace = inFace
			};
			break;
		}
		case CLIP_ORIGIN_CLIP:
			type = STUC_BUF_VERT_IN_OR_MAP;
			vert = bufMeshAddInOrMapVert(pBasic, pBufMesh);
			pBufMesh->inOrMapVerts.pArr[vert].in = (InVert){
				.type = STUC_BUF_VERT_SUB_TYPE_IN,
				.inCorner = pCorner->info.origin.corner.corner,
				.inFace =
					getInFaceFromClipCorner(pBorderCache, pCorner->info.origin.corner)
			};
			break;
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
	STUC_ASSERT("", vert != -1);
	pCorners->pArr[newCorner].type = type;
	pCorners->pArr[newCorner].vert = vert;
	STUC_CATCH(0, err,
		--pCorners->count;
	);
	return err;
}

static
Result addFaceToBufMesh(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BorderCache *pBorderCache,
	HTable *pInFaceCache,
	I32 inPieceOffset,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const ClipFaceRoot *pFace
) {
	Result err = STUC_SUCCESS;
	I32 faceCountPrior = pBufMesh->faces.count;
	bufMeshAddFace(pBasic, inPieceOffset, pBufMesh, pBufMesh->corners.count, pFace->size);
	const ClipCorner *pCorner = pFace->pRoot;
	I32 i = 0;
	do {
		STUC_THROW_IFNOT_COND(err, i < pFace->size, "infinite or astray loop", 0);
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
		STUC_THROW_IFNOT(err, "", 0);
	} while(++i, pCorner = pCorner->pNext, pCorner);
	STUC_CATCH(0, err,
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
	const ClipFaceArr *pFaces
) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", pFaces->count);
	for (I32 i = 0; i < pFaces->count; ++i) {
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
		STUC_THROW_IFNOT(err, "", 0);
		STUC_CATCH(0, err, ;
			err = STUC_SUCCESS; //reset err (skipping this face)
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
	STUC_DYN_ARR_ADD(BufCorner, &pBasic->pCtx->alloc, pCorners, newCorner);
	STUC_ASSERT("", newCorner >= 0);
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
Result addNonClipInPieceToBufMesh(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	HTable *pInFaceCache
) {
	Result err = STUC_SUCCESS;
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	FaceRange mapFace = stucGetFaceRange(&pMapMesh->core, pInPiece->pList->mapFace);
	I32 bufFaceStart = pBufMesh->corners.count;
	for (I32 i = 0; i < mapFace.size; ++i) {
		V2_F32 vert =
			*(V2_F32 *)&pMapMesh->pPos[pMapMesh->core.pCorners[mapFace.start + i]];
		I32 encasingInFace = getFaceEncasingVert(pBasic, vert, pInPiece, pInFaceCache);
		if (encasingInFace == -1) {
			STUC_RETURN_ERR_IFNOT_COND(
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
	LinAlloc *pAlloc = stucHTableAllocGet(pTable, 0);
	LinAllocIter iter = {0};
	stucLinAllocIterInit(pAlloc, (Range) {0, INT32_MAX}, &iter);
	for (; !stucLinAllocIterAtEnd(&iter); stucLinAllocIterInc(&iter)) {
		InFaceCacheEntryIntern *pEntry = stucLinAllocGetItem(&iter);
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
		STUC_DYN_ARR_ADD(InFaceCorner, &pBasic->pCtx->alloc, pBorder, newIdx);
		STUC_ASSERT("", newIdx != -1);
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
		STUC_ASSERT("", pCache->size);
		for (I32 i = 0; i < pCache->size; ++i) {
			if (pCache->pBorders[i].pArr) {
				STUC_ASSERT("", pCache->pBorders[i].size);
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
	ClipInput input,
	I32 boundary,
	I32 corner
) {
	const MapToMeshBasic *pBasic = pUserData;
	const BorderCache *pBorderCache = pMesh;
	InFaceCorner *pInCorner = pBorderCache->pBorders[boundary].pArr + corner;
	return stucGetUvPos(pBasic->pInMesh, &pInCorner->pFace->face, pInCorner->corner);
}

static
V3_F32 getMapCornerPos(
	const void *pUserData,
	const void *pMeshVoid,
	ClipInput input,
	I32 boundary,
	I32 corner
) {
	const Mesh *pMesh = ((MapToMeshBasic *)pUserData)->pMap->pMesh;
	const FaceRange *pMapFace = input.pUserData;
	return stucGetVertPos(pMesh, pMapFace, corner);
}

Result stucClipMapFace(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache
) {
	Result err = STUC_SUCCESS;
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

	ClipInput inInput = {.boundaries = pInPiece->borderArr.count};
	inInput.pSizes = pBasic->pCtx->alloc.fpMalloc(inInput.boundaries * sizeof(I32));
	for (I32 i = 0; i < inInput.boundaries; ++i) {
		inInput.pSizes[i] = pInPiece->borderArr.pArr[i].len;
	}
	ClipInput mapInput = {.pSizes = &mapFace.size, .boundaries = 1, .pUserData = &mapFace};
	ClipFaceArr out = {0};
	stucClip(
		&pBasic->pCtx->alloc,
		pBasic,
		pBorderCache, inInput, getBorderCornerPos,
		NULL, mapInput, getMapCornerPos,
		&out
	);
	//TODO TEMP throwing err for now
	STUC_THROW_IFNOT_COND(err, out.count, "", 0);
	/*
	if (not clipped) {
		//no edges clipped the mapface, treat as a non-clip inPiece
		err = addNonClipInPieceToBufMesh(
			pBasic,
			inPieceOffset,
			pInPiece,
			pBufMesh,
			&inFaceCache
		);
		STUC_THROW_IFNOT(err, "", 0);
	}
	else {
	*/
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
	//}
	STUC_CATCH(0, err, 
		err = STUC_SUCCESS; //skipping this face, reset err
	);
	inFaceCacheDestroy(pBasic, &inFaceCache);
	return err;
}

Result stucAddMapFaceToBufMesh(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache
) {
	Result err = STUC_SUCCESS;

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
	STUC_THROW_IFNOT(err, "", 0);
	STUC_CATCH(0, err,
		err = STUC_SUCCESS; //reset err (skipping face)
	);
	inFaceCacheDestroy(pBasic, &inFaceCache);
	return err;
}

Result stucBufMeshInit(void *pArgsVoid) {
	Result err = STUC_SUCCESS;
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

typedef struct EncasedMapFaceInitInfo {
	const FaceRange *pInFace;
	bool inFaceWind;
} EncasedMapFaceInitInfo;

static
void encasedMapFaceInit(
	void * pUserData,
	HTableEntryCore *pEntryCore,
	const void *pKeyData,
	void *pInitInfoVoid,
	I32 linAlloc
) {
	EncasedMapFaceTableState *pState = pUserData;
	const StucAlloc *pAlloc = &pState->pBasic->pCtx->alloc;
	const InPieceKey *pKey = pKeyData;
	EncasedMapFaceInitInfo *pInitInfo = pInitInfoVoid;
	EncasedMapFace *pEntry = (EncasedMapFace *)pEntryCore;
	pEntry->mapFace = pKey->mapFace;
	pEntry->tile = pKey->tile;
	pEntry->inFaces.count = pEntry->inFaces.size = 1;
	pEntry->inFaces.pArr = pAlloc->fpMalloc(pEntry->inFaces.size * sizeof(EncasingInFace));
	pEntry->inFaces.pArr[0].idx = (U32)pInitInfo->pInFace->idx;
	pEntry->inFaces.pArr[0].wind = pInitInfo->inFaceWind;
}

static
bool encasedMapFaceCmp(
	const HTableEntryCore *pEntry,
	const void *pKeyData,
	const void *pInitInfo
) {
	return 
		((EncasedMapFace *)pEntry)->mapFace == ((InPieceKey *)pKeyData)->mapFace &&
		_(((EncasedMapFace *)pEntry)->tile V2I16EQL ((InPieceKey *)pKeyData)->tile);
}

static
void appendToEncasedEntry(
	const MapToMeshBasic *pBasic,
	EncasedMapFace *pEntry,
	const FaceRange *pInFace,
	bool wind
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	EncasingInFaceArr *pInFaces = &pEntry->inFaces;
	STUC_ASSERT(
		"",
		pInFaces->count > 0 && pInFaces->count <= pInFaces->size
	);
	if (pInFaces->count == pInFaces->size) {
		pInFaces->size *= 2;
		pInFaces->pArr =
			pAlloc->fpRealloc(pInFaces->pArr, pInFaces->size * sizeof(EncasingInFace));
	}
	pInFaces->pArr[pInFaces->count].idx = (U32)pInFace->idx;
	pInFaces->pArr[pInFaces->count].wind = wind;
	pInFaces->count++;
}

static
EncasedMapFace *addToEncasedFaces(
	FindEncasedFacesJobArgs *pArgs,
	const FaceRange *pInFace,
	bool inFaceWind,
	const FaceRange *pMapFace,
	V2_I16 tile
) {
	EncasedMapFace *pEntry = NULL;
	SearchResult result = stucHTableGet(
		&pArgs->encasedFaces,
		0,
		&(InPieceKey) {.mapFace = pMapFace->idx, .tile = tile},
		(void**)&pEntry,
		true, &(EncasedMapFaceInitInfo) {.pInFace = pInFace, .inFaceWind = inFaceWind},
		stucInPieceMakeKey, NULL, encasedMapFaceInit, encasedMapFaceCmp
	);
	if (result == STUC_SEARCH_FOUND) {
		STUC_ASSERT("", pEntry);
		appendToEncasedEntry(pArgs->core.pBasic, pEntry, pInFace, inFaceWind);
	}
	return pEntry;
}

//TODO merge this with stucCalcIntersection, & add quick exit to clipping
static
bool intersectTest(V2_F32 a, V2_F32 ab, V2_F32 c, V2_F32 cd) {
	V2_F32 ac = _(c V2SUB a);
	F32 det2 = _(ab V2DET cd);
	if (det2 == .0f) {
		return false;
	}
	F32 tMapEdge = _(ac V2DET cd) / det2;
	if (tMapEdge < .0f || tMapEdge > 1.0f) {
		return false;
	}
	det2 = _(cd V2DET ab);
	if (det2 == .0f) {
		return false;
	}
	V2_F32 ca = _(a V2SUB c);
	F32 tInEdge = _(ca V2DET ab) / det2;
	return (tInEdge >= .0f && tInEdge <= 1.0f);
}

typedef enum OverlapType {
	STUC_FACE_OVERLAP_NONE,
	STUC_FACE_OVERLAP_INTERSECT,
	STUC_FACE_OVERLAP_IN_INSIDE_MAP,
	STUC_FACE_OVERLAP_MAP_INSIDE_IN
} OverlapType;

static
OverlapType doInAndMapFacesOverlap(
	const Mesh *pInMesh, const FaceRange *pInFace, HalfPlane *pInCorners,
	const Mesh *pMapMesh, const FaceRange *pMapFace,
	bool inFaceWind
) {
	V2_F32 windLine = { .d = {.0f, 2.0f - pInCorners[0].uv.d[1]}};
	I32 windNum = 0;
	bool halfPlaneOnly = false;
	bool allInside = true;
	for (I32 i = 0; i < pMapFace->size; ++i) {
		I32 iNext = stucGetCornerNext(i, pMapFace);
		V2_F32 a = stucGetVertPosAsV2(pMapMesh, pMapFace, i);
		V2_F32 b = stucGetVertPosAsV2(pMapMesh, pMapFace, iNext);
		V2_F32 ab = _(b V2SUB a);
		bool inside = true;
		for (I32 j = 0; j < pInFace->size; ++j) {
			InsideStatus status = stucIsPointInHalfPlane(
				a,
				pInCorners[j].uv,
				pInCorners[j].halfPlane,
				inFaceWind
			);
			if (status == STUC_INSIDE_STATUS_OUTSIDE){
				if (halfPlaneOnly) {
					//a previous map vert was inside
					return STUC_FACE_OVERLAP_INTERSECT;
				}
				inside = false;
				allInside = false;
			}
			if (!halfPlaneOnly &&
				intersectTest(a, ab, pInCorners[j].uv, pInCorners[j].dir)
			) {
				return STUC_FACE_OVERLAP_INTERSECT;
			}
		}
		if (inside) {
			if (!allInside) {
				//a previous map corner was outside
				return STUC_FACE_OVERLAP_INTERSECT;
			}
			halfPlaneOnly = true;//continue, but only perform halfplane tests
			continue;
		}
		if (!halfPlaneOnly) {
			if (!ab.d[0] || a.d[0] == pInCorners[0].uv.d[0]) {
				//colinear (if b is on windLine, it will be counted as 1 intersection)
				continue;
			}
			if (intersectTest(a, ab, pInCorners[0].uv, windLine)) {
				windNum++;
			}
		}
	}
	if (halfPlaneOnly) {
		STUC_ASSERT("should have returned earlier if not", allInside);
		return STUC_FACE_OVERLAP_MAP_INSIDE_IN;
	}
	return windNum % 2 ? STUC_FACE_OVERLAP_IN_INSIDE_MAP : STUC_FACE_OVERLAP_NONE;
}

Result stucGetEncasedFacesPerFace(
	FindEncasedFacesJobArgs *pArgs,
	FaceCellsTable *pFaceCellsTable,
	V2_I16 tile,
	FaceRange *pInFace
) {
	Result err = STUC_SUCCESS;
	V2_F32 fTileMin = {(F32)tile.d[0], (F32)tile.d[1]};
	STUC_ASSERT("", pInFace->size == 3 || pInFace->size == 4);
	const Mesh *pInMesh = pArgs->core.pBasic->pInMesh;
	const StucMap pMap = pArgs->core.pBasic->pMap;
	FaceBounds bounds = {0};
	stucGetInFaceBounds(&bounds, pInMesh->pUvs, *pInFace);
	_(&bounds.fBBox.min V2SUBEQL fTileMin);
	_(&bounds.fBBox.max V2SUBEQL fTileMin);

	HalfPlane inCorners[4] = {0};
	initHalfPlaneLookup(pInMesh, pInFace, inCorners);

	BaseTriVerts inTri = {0};
	//const qualifier is cast away from in-mesh here, to init inTri
	initInTri(&inTri, (Mesh *)pInMesh, pInFace, fTileMin);
	//pInTriConst will be used for access to resolve this
	const BaseTriVerts *pInTriConst = &inTri;

	if (isTriDegenerate(pInTriConst, pInFace)) {
		return err; //skip
	}
	I32 inFaceWind = stucCalcFaceWindFromUvs(pInFace, pInMesh);
	if (inFaceWind == 2) {
		//face is degenerate - skip
		return err;
	}
	FaceCells *pFaceCellsEntry =
		stucIdxFaceCells(pFaceCellsTable, pInFace->idx, pArgs->core.range.start);
	for (I32 i = 0; i < pFaceCellsEntry->cellSize; ++i) {
		const I32 *pCellFaces = NULL;
		Range range = {0};
		getCellMapFaces(pArgs->core.pBasic, pFaceCellsEntry, i, &pCellFaces, &range);
		for (I32 j = range.start; j < range.end; ++j) {
			if (!stucIsBBoxInBBox(bounds.fBBox, pMap->pFaceBBoxes[pCellFaces[j]])) {
				continue;
			}
			FaceRange mapFace = stucGetFaceRange(&pMap->pMesh->core, pCellFaces[j]);
			OverlapType overlap = doInAndMapFacesOverlap(
				pInMesh, pInFace, inCorners,
				pMap->pMesh, &mapFace,
				inFaceWind
			);
			if (overlap != STUC_FACE_OVERLAP_NONE) {
				addToEncasedFaces(pArgs, pInFace, inFaceWind, &mapFace, tile);
			}
			/*
			recordEncasedMapCorners(
				pArgs->core.pBasic,
				pInFace,
				inFaceWind,
				pEntry,
				pMap->pMesh,
				&mapFace,
				&bounds
			);
			*/
		}
	}
	STUC_CATCH(0, err, ;);
	return STUC_SUCCESS;
}

typedef struct BufVertTransformInfo {
	FaceRange inFace;
	V3_F32 uvw;
	I32 mapCorner;
	bool isMapCorner;
} BufVertTransformInfo;

typedef enum InterpCacheActive {
	STUC_INTERP_CACHE_NONE,
	STUC_INTERP_CACHE_COPY_IN,
	STUC_INTERP_CACHE_COPY_MAP,
	STUC_INTERP_CACHE_LERP_IN,
	STUC_INTERP_CACHE_LERP_MAP,
	STUC_INTERP_CACHE_TRI_IN,
	STUC_INTERP_CACHE_TRI_MAP
} InterpCacheActive;

typedef struct InterpCacheCopy {
	InterpCacheActive active;
	FaceRange mapFace;
	I32 inFace;
	I32 a;//corner or vert
} InterpCacheCopy;

typedef struct InterpCacheLerp {
	InterpCacheActive active;
	I32 a;
	I32 b;
	F32 t;
} InterpCacheLerp;

typedef struct InterpCacheTri {
	InterpCacheActive active;
	I32 triReal[3];
	V3_F32 bc;
} InterpCacheTri;

typedef union InterpCache {
	InterpCacheActive active;
	InterpCacheCopy copyIn;
	InterpCacheCopy copyMap;
	InterpCacheLerp lerpIn;
	InterpCacheLerp lerpMap;
	InterpCacheTri triIn;
	InterpCacheTri triMap;
} InterpCache;

typedef struct InterpCacheLimited {
	const StucDomain domain;
	const AttribOrigin origin;
	InterpCache cache;
} InterpCacheLimited;

typedef struct InterpCaches {
	InterpCacheLimited in;
	InterpCacheLimited map;
} InterpCaches;

static
void interpCacheUpdateCopyIn(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 inFaceIdx, I32 inCorner,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_COPY_IN;
	FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, inFaceIdx);
	pCache->copyIn.a = inFace.start + inCorner;
	if (domain == STUC_DOMAIN_VERT) {
		pCache->copyIn.a = pBasic->pInMesh->core.pCorners[pCache->copyIn.a];
	}
	pCache->copyIn.inFace = inFace.idx;
}

static
void interpCacheUpdateCopyMap(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 mapFaceIdx, I32 mapCorner,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_COPY_MAP;
	FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaceIdx);
	pCache->copyMap.a = mapFace.start + mapCorner;
	if (domain == STUC_DOMAIN_VERT) {
		pCache->copyMap.a = pBasic->pMap->pMesh->core.pCorners[pCache->copyMap.a];
	}
	pCache->copyMap.mapFace = mapFace;
}

static
void interpCacheUpdateLerpIn(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 inFaceIdx, I32 inCorner,
	F32 t,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_LERP_IN;
	FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, inFaceIdx);
	pCache->lerpIn.a = inFace.start + inCorner;
	pCache->lerpIn.b = inFace.start + stucGetCornerNext(inCorner, &inFace);
	if (domain == STUC_DOMAIN_VERT) {
		pCache->lerpIn.a = pBasic->pInMesh->core.pCorners[pCache->lerpIn.a];
		pCache->lerpIn.b = pBasic->pInMesh->core.pCorners[pCache->lerpIn.b];
	}
	pCache->lerpIn.t = t;
}

static
void interpCacheUpdateLerpMap(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 mapFaceIdx, I32 mapCorner,
	F32 t,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_LERP_MAP;
	FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaceIdx);
	pCache->lerpMap.a = mapFace.start + mapCorner;
	pCache->lerpMap.b =
		mapFace.start + stucGetCornerNext(mapCorner, &mapFace);
	if (domain == STUC_DOMAIN_VERT) {
		pCache->lerpMap.a = pBasic->pMap->pMesh->core.pCorners[pCache->lerpMap.a];
		pCache->lerpMap.b = pBasic->pMap->pMesh->core.pCorners[pCache->lerpMap.b];
	}
	pCache->lerpMap.t = t;
}

static
void interpCacheUpdateTriIn(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 mapFaceIdx, I32 mapCorner,
	I32 inFaceIdx,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_TRI_IN;
	FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaceIdx);
	FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, inFaceIdx);
	V2_F32 mapVertPos = *(V2_F32 *)&pBasic->pMap->pMesh->pPos[
		pBasic->pMap->pMesh->core.pCorners[mapFace.start + mapCorner]
	];
	I8 tri[3] = { 0 };
	pCache->triIn.bc =
		stucGetBarycentricInFaceFromUvs(pBasic->pInMesh, &inFace, tri, mapVertPos);
	for (I32 i = 0; i < 3; ++i) {
		pCache->triIn.triReal[i] = inFace.start + tri[i];
		if (domain == STUC_DOMAIN_VERT) {
			pCache->triIn.triReal[i] =
				pBasic->pInMesh->core.pCorners[pCache->triIn.triReal[i]];
		}
	}
}

static
void interpCacheUpdateTriMap(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 inFaceIdx, I32 inCorner,
	I32 mapFaceIdx, I8 mapTri,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_TRI_MAP;
	FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, inFaceIdx);
	FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaceIdx);
	const U8 *pTri = stucGetTri(pBasic->pMap->triCache.pArr, mapFace.idx, mapTri);
	V2_F32 inUv = pBasic->pInMesh->pUvs[inFace.start + inCorner];
	I8 triBuf[3] = {0};
	if (pTri) {
		pCache->triMap.bc =
			stucGetBarycentricInTriFromVerts(pBasic->pMap->pMesh, &mapFace, pTri, inUv);
	}
	else {
		pCache->triMap.bc =
			stucGetBarycentricInFaceFromVerts(pBasic->pMap->pMesh, &mapFace, triBuf, inUv);
		pTri = triBuf;
	}
	for (I32 i = 0; i < 3; ++i) {
		pCache->triMap.triReal[i] = mapFace.start + pTri[i];
		if (domain == STUC_DOMAIN_VERT) {
			pCache->triMap.triReal[i] =
				pBasic->pMap->pMesh->core.pCorners[pCache->triMap.triReal[i]];
		}
	}
}

static
void interpBufVertIn(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	InOrMapVert *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_IN) {
				interpCacheUpdateCopyIn(
					pBasic,
					pInterpCache->domain,
					pVert->in.inFace, pVert->in.inCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyIn.a);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_TRI_MAP) {
				interpCacheUpdateTriMap(
					pBasic,
					pInterpCache->domain,
					pVert->in.inFace, pVert->in.inCorner,
					pInPiece->pList->mapFace, pVert->in.tri,
					&pInterpCache->cache
				);
			}
			stucTriInterpolateAttrib(
				pDest, iDest,
				pSrc,
				pInterpCache->cache.triMap.triReal[0],
				pInterpCache->cache.triMap.triReal[1],
				pInterpCache->cache.triMap.triReal[2],
				pInterpCache->cache.triMap.bc
			);
			break;
		default:
			STUC_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertMap(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	InOrMapVert *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_TRI_IN) {
				interpCacheUpdateTriIn(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->map.mapCorner,
					pVert->map.inFace,
					&pInterpCache->cache
				);
			}
			stucTriInterpolateAttrib(
				pDest, iDest,
				pSrc, 
				pInterpCache->cache.triIn.triReal[0],
				pInterpCache->cache.triIn.triReal[1],
				pInterpCache->cache.triIn.triReal[2],
				pInterpCache->cache.triIn.bc
			);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_MAP) {
				interpCacheUpdateCopyMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->map.mapCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyMap.a);
			break;
		default:
			STUC_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertEdgeIn(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	BufVertOnEdge *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_IN) {
				interpCacheUpdateCopyIn(
					pBasic,
					pInterpCache->domain,
					pVert->in.inFace, pVert->in.inCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyIn.a);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_LERP_MAP) {
				interpCacheUpdateLerpMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->in.mapCorner,
					pVert->in.tMapEdge,
					&pInterpCache->cache
				);
			}
			stucLerpAttrib(
				pDest, iDest,
				pSrc, pInterpCache->cache.lerpMap.a,
				pSrc, pInterpCache->cache.lerpMap.b,
				pVert->in.tMapEdge
			);
			break;
		default:
			STUC_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertEdgeMap(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	BufVertOnEdge *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_LERP_IN) {
				interpCacheUpdateLerpIn(
					pBasic,
					pInterpCache->domain,
					pVert->map.inFace, pVert->map.inCorner,
					pVert->map.tInEdge,
					&pInterpCache->cache
				);
			}
			stucLerpAttrib(
				pDest, iDest,
				pSrc, pInterpCache->cache.lerpIn.a,
				pSrc, pInterpCache->cache.lerpIn.b,
				pVert->map.tInEdge
			);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_MAP) {
				interpCacheUpdateCopyMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->map.mapCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyMap.a);
			break;
		default:
			STUC_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertOverlap(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	OverlapVert *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_IN) {
				interpCacheUpdateCopyIn(
					pBasic,
					pInterpCache->domain,
					pVert->inFace, pVert->inCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyIn.a);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_MAP) {
				interpCacheUpdateCopyMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->mapCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyMap.a);
			break;
		default:
			STUC_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertIntersect(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	IntersectVert *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_LERP_IN) {
				interpCacheUpdateLerpIn(
					pBasic,
					pInterpCache->domain,
					pVert->inFace, pVert->inCorner,
					pVert->tInEdge,
					&pInterpCache->cache
				);
			}
			stucLerpAttrib(
				pDest, iDest,
				pSrc, pInterpCache->cache.lerpIn.a,
				pSrc, pInterpCache->cache.lerpIn.b,
				pVert->tInEdge
			);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_LERP_MAP) {
				interpCacheUpdateLerpMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->mapCorner,
					pVert->tMapEdge,
					&pInterpCache->cache
				);
			}
			stucLerpAttrib(
				pDest, iDest,
				pSrc, pInterpCache->cache.lerpMap.a,
				pSrc, pInterpCache->cache.lerpMap.b,
				pVert->tMapEdge
			);
			break;
		default:
			STUC_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufAttrib(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner corner,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	StucDomain domain = pInterpCache->domain;
	AttribOrigin origin = pInterpCache->origin;
	STUC_ASSERT(
		"not interpolating face attribs",
		domain == STUC_DOMAIN_CORNER || domain == STUC_DOMAIN_VERT
	);
	STUC_ASSERT(
		"only in or map are valid for origin override",
		origin == STUC_ATTRIB_ORIGIN_MESH_IN || origin == STUC_ATTRIB_ORIGIN_MAP
	);
	BufFace bufFace = pBufMesh->faces.pArr[corner.face];
	BufCorner bufCorner = pBufMesh->corners.pArr[bufFace.start + corner.corner];
	switch (bufCorner.type) {
		case STUC_BUF_VERT_IN_OR_MAP: {
			InOrMapVert *pVert = pBufMesh->inOrMapVerts.pArr + bufCorner.vert;
			switch (pVert->in.type) {
				case STUC_BUF_VERT_SUB_TYPE_IN:
					interpBufVertIn(
						pBasic,
						pInPiece,
						pVert,
						pDest, iDest,
						pSrc,
						pInterpCache
					);
					break;
				case STUC_BUF_VERT_SUB_TYPE_MAP:
					interpBufVertMap(
						pBasic,
						pInPiece,
						pVert,
						pDest, iDest,
						pSrc,
						pInterpCache
					);
					break;
			}
			break;
		}
		case STUC_BUF_VERT_ON_EDGE: {
			BufVertOnEdge *pVert = pBufMesh->onEdgeVerts.pArr + bufCorner.vert;
			switch (pVert->in.type) {
				case STUC_BUF_VERT_SUB_TYPE_EDGE_IN:
					interpBufVertEdgeIn(
						pBasic,
						pInPiece,
						pVert,
						pDest, iDest,
						pSrc,
						pInterpCache
					);
					break;
				case STUC_BUF_VERT_SUB_TYPE_EDGE_MAP:
					interpBufVertEdgeMap(
						pBasic,
						pInPiece,
						pVert,
						pDest, iDest,
						pSrc,
						pInterpCache
					);
					break;
			}
			break;
		}
		case STUC_BUF_VERT_OVERLAP: {
			OverlapVert *pVert = pBufMesh->overlapVerts.pArr + bufCorner.vert;
			interpBufVertOverlap(
				pBasic,
				pInPiece,
				pVert,
				pDest, iDest,
				pSrc,
				pInterpCache
			);
			break;
		}
		case STUC_BUF_VERT_INTERSECT: {
			IntersectVert *pVert = pBufMesh->intersectVerts.pArr + bufCorner.vert;
			interpBufVertIntersect(
				pBasic,
				pInPiece,
				pVert,
				pDest, iDest,
				pSrc,
				pInterpCache
			);
			break;
		}
	}
}

static
UsgInFace *findUsgForMapCorners(
	const MapToMeshBasic *pBasic,
	const FaceRange *pMapFace,
	I32 inFace,
	V3_F32 mapUvw,
	Usg **ppUsg
) {
	StucMap pMap = pBasic->pMap;
	for (I32 i = 0; i < pMapFace->size; ++i) {
		I32 mapVert = pMap->pMesh->core.pCorners[pMapFace->start + i];
		if (!pMap->pMesh->pUsg) {
			continue;
		}
		I32 usgIdx = pMap->pMesh->pUsg[mapVert];
		if (!usgIdx) {
			continue;
		}
		usgIdx = abs(usgIdx) - 1;
		*ppUsg = pMap->usgArr.pArr + usgIdx;
		if (stucIsPointInsideMesh(&pBasic->pCtx->alloc, mapUvw, (*ppUsg)->pMesh)) {
			//passing NULL for above cutoff,
			// we don't need to know cause using flatcutoff eitherway here
			UsgInFace *pUsgEntry = stucGetUsgForCorner(
				i,
				pMap,
				pMapFace,
				inFace,
				NULL
			);
			if (pUsgEntry) {
				return pUsgEntry;
			}
		}
	}
	return NULL;
}

static
void getUsgEntry(
	const MapToMeshBasic *pBasic,
	V3_F32 mapUvw,
	const InterpCacheLimited *pMapInterpCache,
	UsgInFace **ppUsgEntry,
	bool *pAboveCutoff
) {
	if (pMapInterpCache->cache.active == STUC_INTERP_CACHE_COPY_MAP) {
		*ppUsgEntry = stucGetUsgForCorner(
			pMapInterpCache->cache.copyMap.a,
			pBasic->pMap,
			&pMapInterpCache->cache.copyMap.mapFace,
			pMapInterpCache->cache.copyMap.inFace,
			pAboveCutoff
		);
	}
	else {
		Usg *pUsg = NULL;
		*ppUsgEntry = findUsgForMapCorners(
			pBasic,
			&pMapInterpCache->cache.copyMap.mapFace,
			pMapInterpCache->cache.copyMap.inFace,
			mapUvw,
			&pUsg
		);
		if (*ppUsgEntry) {
			STUC_ASSERT("", pUsg);
			bool insideUsg =
				stucIsPointInsideMesh(&pBasic->pCtx->alloc, mapUvw, pUsg->pFlatCutoff);
			*pAboveCutoff = pUsg->pFlatCutoff && insideUsg;
		}
	}
}

static
Result interpActiveAttrib(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	InterpCacheLimited *pInterpCache,
	void *pData,
	AttribType type,
	AttribUse use
) {
	Result err = STUC_SUCCESS;
	AttribCore attribWrap = { .pData = pData, .type = type};
	const StucMesh *pSrcMesh = NULL;
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN:
			pSrcMesh = &pBasic->pInMesh->core;
			break;
		case STUC_ATTRIB_ORIGIN_MAP:
			pSrcMesh = &pBasic->pMap->pMesh->core;
			break;
		default:
			STUC_ASSERT("invalid origin override", false);
	}
	const Attrib *pSrcAttrib =
		stucGetActiveAttribConst(pBasic->pCtx, pSrcMesh, use);
	STUC_RETURN_ERR_IFNOT_COND(err, pSrcAttrib, "active attrib not found");
	interpBufAttrib(
		pBasic,
		pInPiece,
		pBufMesh,
		bufCorner,
		&attribWrap, 0,
		&pSrcAttrib->core,
		pInterpCache
	);
	return err;
}

static
Result getInterpolatedTbn(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	InterpCacheLimited *pInInterpCache,
	Mat3x3 *pTbn
) {
	Result err = STUC_SUCCESS;
	V3_F32 tangent = {0};
	V3_F32 normal = {0};
	F32 tSign = .0f;
	err = interpActiveAttrib(
		pBasic,
		pInPiece,
		pBufMesh,
		bufCorner,
		pInInterpCache,
		&normal,
		STUC_ATTRIB_V3_F32,
		STUC_ATTRIB_USE_NORMAL
	);
	STUC_RETURN_ERR_IFNOT(err, "");
	err = interpActiveAttrib(
		pBasic,
		pInPiece,
		pBufMesh,
		bufCorner,
		pInInterpCache,
		&tangent,
		STUC_ATTRIB_V3_F32,
		STUC_ATTRIB_USE_TANGENT
	);
	STUC_RETURN_ERR_IFNOT(err, "");
	err = interpActiveAttrib(
		pBasic,
		pInPiece,
		pBufMesh,
		bufCorner,
		pInInterpCache,
		&tSign,
		STUC_ATTRIB_F32,
		STUC_ATTRIB_USE_TSIGN
	);
	STUC_RETURN_ERR_IFNOT(err, "");
	V3_F32 bitangent = _(_(normal V3CROSS tangent) V3MULS tSign);
	*(V3_F32 *)&pTbn->d[0] = tangent;
	*(V3_F32 *)&pTbn->d[1] = bitangent;
	*(V3_F32 *)&pTbn->d[2] = normal;
	return err;
}

static
Result mapUvwToXyzFlat(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	V3_F32 mapUvw,
	InterpCacheLimited *pInInterpCache,
	V3_F32 *pXyzFlat,
	Mat3x3 *pTbn
) {
	Result err = STUC_SUCCESS;
	F32 inVertWScale = 1.0;
	if (pBasic->pInMesh->pWScale) {
		InterpCacheLimited inVertInterpCache = {
			.domain = STUC_DOMAIN_VERT,
			.origin = STUC_ATTRIB_ORIGIN_MESH_IN
		};
		err = interpActiveAttrib(
			pBasic,
			pInPiece,
			pBufMesh,
			bufCorner,
			&inVertInterpCache,
			&inVertWScale,
			STUC_ATTRIB_F32,
			STUC_ATTRIB_USE_WSCALE
		);
		STUC_RETURN_ERR_IFNOT(err, "");
		mapUvw.d[2] *= inVertWScale;
	}

	err = getInterpolatedTbn(
		pBasic,
		pInPiece,
		pBufMesh,
		bufCorner,
		pInInterpCache,
		pTbn
	);
	STUC_RETURN_ERR_IFNOT(err, "");

	const Mesh *pInMesh = pBasic->pInMesh;
	const InterpCache *pCache = &pInInterpCache->cache;
	switch (pCache->active) {
		case STUC_INTERP_CACHE_COPY_IN: {
			*pXyzFlat = pInMesh->pPos[pInMesh->core.pCorners[pCache->copyIn.a]];
			break;
		}
		case STUC_INTERP_CACHE_LERP_IN: {
			V3_F32 aPos = pInMesh->pPos[pInMesh->core.pCorners[pCache->lerpIn.a]];
			V3_F32 bPos = pInMesh->pPos[pInMesh->core.pCorners[pCache->lerpIn.b]];
			*pXyzFlat = v3F32Lerp(aPos, bPos, pCache->lerpIn.t);
			break;
		}
		case STUC_INTERP_CACHE_TRI_IN: {
			V3_F32 inXyz[3] = {0};
			for (I32 i = 0; i < 3; ++i) {
				I32 vert = pInMesh->core.pCorners[pCache->triIn.triReal[i]];
				inXyz[i] = pInMesh->pPos[vert];
			}
			*pXyzFlat = stucBarycentricToCartesian(inXyz, pCache->triIn.bc);
			break;
		}
		default:
			STUC_ASSERT(
				"invalid interp cache state,\
				this should have been set while interpolating tbn",
				false
			);
	}
	return err;
}

static
Result xformVertFromUvwToXyz(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	InterpCaches *pInterpCaches,
	V3_F32 *pPos,
	Mat3x3 *pTbn
) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT(
		"",
		pInterpCaches->in.domain == STUC_DOMAIN_CORNER &&
		pInterpCaches->map.domain == STUC_DOMAIN_VERT
	);
	V3_F32 mapUvw = {0};
	err = interpActiveAttrib(
		pBasic,
		pInPiece,
		pBufMesh,
		bufCorner,
		&pInterpCaches->map,
		&mapUvw,
		STUC_ATTRIB_V3_F32,
		STUC_ATTRIB_USE_POS
	);
	STUC_RETURN_ERR_IFNOT(err, "");
	V2_F32 fTileMin = {.d = {pInPiece->pList->tile.d[0], pInPiece->pList->tile.d[1]}};
	_((V2_F32 *)&mapUvw V2SUBEQL fTileMin);
	bool aboveCutoff = false;
	UsgInFace *pUsgEntry = NULL;
	if (pBasic->pMap->pMesh->pUsg) {
		getUsgEntry(pBasic, mapUvw, &pInterpCaches->map, &pUsgEntry, &aboveCutoff);
	}
	V3_F32 xyzFlat = {0};
	Mat3x3 tbn = {0};
	if (pUsgEntry && aboveCutoff) {
		V2_F32 uv = *(V2_F32 *)&mapUvw;
		stucUsgVertTransform(pUsgEntry, uv, &xyzFlat, pBasic->pInMesh, fTileMin, &tbn);
	}
	else {
		err = mapUvwToXyzFlat(
			pBasic,
			pInPiece,
			pBufMesh,
			bufCorner,
			mapUvw,
			&pInterpCaches->in,
			&xyzFlat,
			&tbn
		);
		STUC_RETURN_ERR_IFNOT(err, "");
	}
	*pPos = _(xyzFlat V3ADD _(*(V3_F32 *)&tbn.d[2] V3MULS mapUvw.d[2] * pBasic->wScale));
	*pTbn = tbn;
	return err;
}

static
void blendCommonAttrib(
	const MapToMeshBasic *pBasic,
	const Attrib *pInAttrib,
	const Attrib *pMapAttrib,
	Attrib *pOutAttrib,
	I32 dataIdx,
	StucDomain domain
) {
	//TODO should this be using name even for active attribs?
	const StucCommonAttrib *pCommon = stucGetCommonAttribFromDomain(
		pBasic->pCommonAttribList,
		pOutAttrib->core.name,
		domain
	);
	StucBlendConfig blendConfig = {0};
	if (pCommon) {
		blendConfig = pCommon->blendConfig;
	}
	else {
		StucTypeDefault *pDefault =
			stucGetTypeDefaultConfig(&pBasic->pCtx->typeDefaults, pOutAttrib->core.type);
		blendConfig = pDefault->blendConfig;
	}
	const StucAttrib *orderTable[2] = {0};
	I8 order = blendConfig.order;
	orderTable[0] = order ? pMapAttrib : pInAttrib;
	orderTable[1] = !order ? pMapAttrib : pInAttrib;
	stucBlendAttribs(
		&pOutAttrib->core, dataIdx,
		&orderTable[0]->core, 0,
		&orderTable[1]->core, 0,
		blendConfig
	);
}

static
void interpAndBlendAttribs(
	const MapToMeshBasic *pBasic,
	Mesh *pOutMesh,
	I32 dataIdx,
	StucDomain domain,
	const InPiece *pInPiece,//corners or verts
	const BufMesh *pBufMesh,//corners or verts
	const VertMerge *pVertEntry,//corners or verts
	InterpCaches *pInterpCaches,//corners or verts
	const SrcFaces *pSrcFaces//faces
) {
	Result err = STUC_SUCCESS;
	if (domain == STUC_DOMAIN_FACE) {
		STUC_ASSERT("", pSrcFaces);
	}
	else if (domain == STUC_DOMAIN_CORNER || domain == STUC_DOMAIN_VERT) {
		STUC_ASSERT("", pInPiece && pBufMesh && pVertEntry && pInterpCaches);
	}
	else {
		STUC_ASSERT("invalid domain for this func", false);
	}
	AttribArray *pOutAttribArr = stucGetAttribArrFromDomain(&pOutMesh->core, domain);
	const AttribArray *pMapAttribArr =
		stucGetAttribArrFromDomainConst(&pBasic->pMap->pMesh->core, domain);
	const AttribArray *pInAttribArr =
		stucGetAttribArrFromDomainConst(&pBasic->pInMesh->core, domain);
	for (I32 i = 0; i < pOutAttribArr->count; ++i) {
		Attrib *pOutAttrib = pOutAttribArr->pArr + i;
		AttribType type = pOutAttrib->core.type;
		AttribUse use = pOutAttrib->core.use;
		STUC_ASSERT(
			"string attribs are only for internal use. This needs to be caught earlier",
			type != STUC_ATTRIB_STRING
		);
		if (pOutAttrib ==
			stucGetActiveAttrib(pBasic->pCtx, &pOutMesh->core, STUC_ATTRIB_USE_POS)
		) {
			continue;
		}
		const StucAttrib *pInAttrib = NULL;
		err = stucGetMatchingAttribConst(
			pBasic->pCtx,
			&pBasic->pInMesh->core, pInAttribArr,
			&pOutMesh->core, pOutAttrib,
			true,
			false,
			&pInAttrib
		);
		STUC_ASSERT("", err == STUC_SUCCESS);
		const StucAttrib *pMapAttrib = NULL;
		stucGetMatchingAttribConst(
			pBasic->pCtx,
			&pBasic->pMap->pMesh->core, pMapAttribArr,
			&pOutMesh->core, pOutAttrib,
			true,
			false,
			&pMapAttrib
		);
		STUC_ASSERT("", err == STUC_SUCCESS);

		U64 inBuf[4] = {0};
		Attrib inAttribWrap = {
			.core = {.pData = inBuf, .type = type, .use = use},
			.interpolate = true
		};
		U64 mapBuf[4] = {0};
		Attrib mapAttribWrap = {
			.core = {.pData = mapBuf, .type = type, .use = use},
			.interpolate = true
		};

		bool interpIn = false;
		bool interpMap = false;
		switch (pOutAttrib->origin) {
			case STUC_ATTRIB_ORIGIN_COMMON:
				interpIn = interpMap = true;
				break;
			case STUC_ATTRIB_ORIGIN_MESH_IN:
				interpIn = true;
				break;
			case STUC_ATTRIB_ORIGIN_MAP:
				interpMap = true;
				break;
			default:
				STUC_ASSERT("invalid attrib origin", false);
		}
		if (interpIn) {
			STUC_ASSERT("", pInAttrib->core.type == type && pInAttrib->core.use == use);
			if (domain == STUC_DOMAIN_FACE) {
				stucCopyAttribCore(
					&inAttribWrap.core, 0,
					&pInAttrib->core, pSrcFaces->in
				);
			}
			else {
				interpBufAttrib(
					pBasic,
					pInPiece,
					pBufMesh,
					pVertEntry->bufCorner.corner,
					&inAttribWrap.core, 0,
					&pInAttrib->core,
					&pInterpCaches->in
				);
			}
		}
		if (interpMap) {
			STUC_ASSERT("", pMapAttrib->core.type == type && pMapAttrib->core.use == use);
			if (domain == STUC_DOMAIN_FACE) {
				stucCopyAttribCore(
					&mapAttribWrap.core, 0,
					&pMapAttrib->core, pSrcFaces->map
				);
			}
			else {
				interpBufAttrib(
					pBasic,
					pInPiece,
					pBufMesh,
					pVertEntry->bufCorner.corner,
					&mapAttribWrap.core, 0,
					&pMapAttrib->core,
					&pInterpCaches->map
				);
			}
		}

		switch (pOutAttrib->origin) {
			case STUC_ATTRIB_ORIGIN_COMMON:
				blendCommonAttrib(
					pBasic,
					&inAttribWrap,
					&mapAttribWrap,
					pOutAttrib, dataIdx,
					domain
				);
				break;
			case STUC_ATTRIB_ORIGIN_MESH_IN:
				stucCopyAttribCore(&pOutAttrib->core, dataIdx, &inAttribWrap.core, 0);
				break;
			case STUC_ATTRIB_ORIGIN_MAP:
				stucCopyAttribCore(&pOutAttrib->core, dataIdx, &mapAttribWrap.core, 0);
				break;
			default:
				STUC_ASSERT("invalid origin override", false);
		}
	}
}

static
void xformNormals(StucMesh *pMesh, I32 idx, const Mat3x3 *pTbn, StucDomain domain) {
	AttribArray *pAttribArr = stucGetAttribArrFromDomain(pMesh, domain);
	for (I32 i = 0; i < pAttribArr->count; ++i) {
		Attrib *pAttrib = pAttribArr->pArr + i;
		if (pAttrib->core.use == STUC_ATTRIB_USE_NORMAL) {
			if (pAttrib->core.type != STUC_ATTRIB_V3_F32) {
				//wrong type
				//TODO when warnings are implemented, warn about this
				continue;
			}
			V3_F32 *pNormal = stucAttribAsVoid(&pAttrib->core, idx);
			*pNormal = _(*pNormal V3MULM3X3 pTbn);
		}
	}
}

void stucGetBufMeshForVertMergeEntry(
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	const VertMerge *pVert,
	const InPiece **ppInPiece,
	const BufMesh **ppBufMesh
) {
	const InPieceArr *pArr = pVert->bufCorner.clipped ? pInPiecesClip : pInPieces;
	*ppBufMesh = pArr->pBufMeshes->arr + pVert->bufCorner.bufMesh;
	I32 inPieceIdx = (*ppBufMesh)->faces.pArr[pVert->bufCorner.corner.face].inPiece;
	*ppInPiece = pArr->pArr + inPieceIdx;
}

Result stucXformAndInterpVertsInRange(void *pArgsVoid) {
	Result err = STUC_SUCCESS;
	xformAndInterpVertsJobArgs *pArgs = pArgsVoid;
	const MapToMeshBasic *pBasic = pArgs->core.pBasic;
	LinAllocIter iter = {0};
	stucLinAllocIterInit(pArgs->pVertAlloc, pArgs->core.range, &iter);
	for (; !stucLinAllocIterAtEnd(&iter); stucLinAllocIterInc(&iter)) {
		VertMerge *pEntry = stucLinAllocGetItem(&iter);
		STUC_ASSERT(
			"",
			!(pArgs->intersect ^ (pEntry->key.type == STUC_BUF_VERT_INTERSECT))
		);
		if (pEntry->removed) {
			continue;
		}
		if (pArgs->intersect) {
			VertMergeIntersect *pIntersect = (VertMergeIntersect *)pEntry;
			if (pIntersect->pSnapTo) {
				continue; //vert was snapped to another - skip
			}
		}
		const InPiece *pInPiece = NULL;
		const BufMesh *pBufMesh = NULL;
		stucGetBufMeshForVertMergeEntry(
			pArgs->pInPieces, pArgs->pInPiecesClip,
			pEntry,
			&pInPiece,
			&pBufMesh
		);
		InterpCaches interpCaches = {
			.in = {.domain = STUC_DOMAIN_CORNER, .origin = STUC_ATTRIB_ORIGIN_MESH_IN},
			.map = {.domain = STUC_DOMAIN_VERT, .origin = STUC_ATTRIB_ORIGIN_MAP}
		};
		xformVertFromUvwToXyz(
			pBasic,
			pInPiece,
			pBufMesh,
			pEntry->bufCorner.corner,
			&interpCaches,
			pArgs->pOutMesh->pPos + pEntry->outVert,
			&pEntry->transform.tbn
		);
		interpAndBlendAttribs(
			pBasic,
			pArgs->pOutMesh,
			pEntry->outVert,
			STUC_DOMAIN_VERT,
			pInPiece,
			pBufMesh,
			pEntry,
			&interpCaches,
			NULL
		);
		xformNormals(
			&pArgs->pOutMesh->core,
			pEntry->outVert,
			&pEntry->transform.tbn,
			STUC_DOMAIN_VERT
		);
	}
	return err;
}

static
const VertMerge *getVertMergeFromIdx(const InterpAttribsJobArgs *pArgs, I32 corner) {
	I32 vertLinIdx = pArgs->pOutMesh->core.pCorners[corner];
	bool intersect = vertLinIdx & -0x80000000;
	if (intersect) {
		vertLinIdx ^= -0x80000000;
	}
	const LinAlloc *pLinAlloc = stucHTableAllocGetConst(pArgs->pMergeTable, intersect);
	STUC_ASSERT("", pLinAlloc);
	const VertMerge *pEntry = stucLinAllocIdxConst(pLinAlloc, vertLinIdx);
	STUC_ASSERT("", pEntry);
	return pEntry;
}

Result stucInterpCornerAttribs(void *pArgsVoid) {
	Result err = STUC_SUCCESS;
	InterpAttribsJobArgs *pArgs = pArgsVoid;
	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	for (I32 i = 0; i < rangeSize; ++i) {
		I32 corner = pArgs->core.range.start + i;
		const VertMerge *pVertEntry = getVertMergeFromIdx(pArgs, corner);

		const InPiece *pInPiece = NULL;
		const BufMesh *pBufMesh = NULL;
		stucGetBufMeshForVertMergeEntry(
			pArgs->pInPieces, pArgs->pInPiecesClip,
			pVertEntry,
			&pInPiece,
			&pBufMesh
		);
		
		InterpCaches interpCaches = {
			.in = {.domain = STUC_DOMAIN_CORNER, .origin = STUC_ATTRIB_ORIGIN_MESH_IN},
			.map = {.domain = STUC_DOMAIN_CORNER, .origin = STUC_ATTRIB_ORIGIN_MAP}
		};
		interpAndBlendAttribs(
			pArgs->core.pBasic,
			pArgs->pOutMesh,
			corner,
			STUC_DOMAIN_CORNER,
			pInPiece,
			pBufMesh,
			pVertEntry,
			&interpCaches,
			NULL
		);
		xformNormals(
			&pArgs->pOutMesh->core,
			corner,
			&pVertEntry->transform.tbn,
			STUC_DOMAIN_CORNER
		);
		pArgs->pOutMesh->core.pCorners[corner] = pVertEntry->outVert;
	}
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
					STUC_ASSERT("invalid in-or-map buf vert sub-type", false);
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
					STUC_ASSERT("invalid on-edge buf vert sub-type", false);
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
			STUC_ASSERT("invalid buf vert type", false);
	}
	return faces;
}

Result stucInterpFaceAttribs(void *pArgsVoid) {
	Result err = STUC_SUCCESS;
	InterpAttribsJobArgs *pArgs = pArgsVoid;
	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	for (I32 i = 0; i < rangeSize; ++i) {
		I32 face = pArgs->core.range.start + i;
		I32 corner = pArgs->pOutMesh->core.pFaces[face];
		const VertMerge *pVertEntry = getVertMergeFromIdx(pArgs, corner);

		const InPiece *pInPiece = NULL;
		const BufMesh *pBufMesh = NULL;
		stucGetBufMeshForVertMergeEntry(
			pArgs->pInPieces, pArgs->pInPiecesClip,
			pVertEntry,
			&pInPiece,
			&pBufMesh
		);
		SrcFaces srcFaces = {0};
		stucGetSrcFacesForBufCorner(
			pArgs->core.pBasic,
			pInPiece,
			pBufMesh,
			pVertEntry->bufCorner.corner
		);
		//not actually interpolating faces,
		//just copying
		interpAndBlendAttribs(
			pArgs->core.pBasic,
			pArgs->pOutMesh,
			face,
			STUC_DOMAIN_FACE,
			NULL, NULL, NULL, NULL,
			&srcFaces
		);
		//crude normal transform, not interpolating tbn's across face atm
		xformNormals(
			&pArgs->pOutMesh->core,
			face,
			&pVertEntry->transform.tbn,
			STUC_DOMAIN_FACE
		);
	}
	return err;
}
