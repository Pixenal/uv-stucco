/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <poly_cutout.h>

#include <in_piece.h>
#include <quadtree.h>
#include <context.h>
#include <map.h>
#include <utils.h>

typedef struct EncasedMapFaceInitInfo {
	const FaceRange *pInFace;
	bool inFaceWind;
} EncasedMapFaceInitInfo;

typedef struct EncasedMapFaceTableState {
	const struct MapToMeshBasic *pBasic;
} EncasedMapFaceTableState;

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
	PIX_ERR_ASSERT("", pCell->localIdx >= 0 && pCell->localIdx < 4);
	PIX_ERR_ASSERT("", pCell->initialized % 2 == pCell->initialized);
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
		if (pixmV2F32DegenerateTri(pTri->uv[0], pTri->uv[2], pTri->uv[1], .0f) ||
			pixmV3F32DegenerateTri(pTri->xyz[0], pTri->xyz[2], pTri->xyz[1], .0f) ||
			pixmV2F32DegenerateTri(pTri->uv[1], pTri->uv[3], pTri->uv[2], .0f) ||
			pixmV3F32DegenerateTri(pTri->xyz[1], pTri->xyz[3], pTri->xyz[2], .0f) ||
			pixmV2F32DegenerateTri(pTri->uv[2], pTri->uv[0], pTri->uv[3], .0f) ||
			pixmV3F32DegenerateTri(pTri->xyz[2], pTri->xyz[0], pTri->xyz[3], .0f) ||
			pixmV2F32DegenerateTri(pTri->uv[3], pTri->uv[1], pTri->uv[0], .0f) ||
			pixmV3F32DegenerateTri(pTri->xyz[3], pTri->xyz[1], pTri->xyz[0], .0f)
		) {
			return true;
		}
	}
	else {
		if (pixmV2F32DegenerateTri(pTri->uv[0], pTri->uv[1], pTri->uv[2], .0f) ||
			pixmV3F32DegenerateTri(pTri->xyz[0], pTri->xyz[1], pTri->xyz[2], .0f)
		) {
			return true;
		}
	}
	return false;
}

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
	PIX_ERR_ASSERT(
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
		PIX_ERR_ASSERT("", pEntry);
		appendToEncasedEntry(pArgs->core.pBasic, pEntry, pInFace, inFaceWind);
	}
	return pEntry;
}

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
V2_F32 getInCornerPos(
	const void *pUserData,
	const void *pMeshVoid,
	PlycutInput input,
	I32 boundary,
	I32 corner,
	bool *pCantIntersect
) {
	const Mesh *pMesh = ((MapToMeshBasic *)pUserData)->pInMesh;
	const FaceRange *pInFace = input.pUserData;
	return stucGetUvPos(pMesh, pInFace, corner);
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

static
OverlapType doInAndMapFacesOverlap(
	const MapToMeshBasic *pBasic,
	const Mesh *pInMesh, FaceRange *pInFace,
	const Mesh *pMapMesh, FaceRange *pMapFace
) {
	PlycutInput inInput =
		{.pSizes = &pInFace->size, .boundaries = 1, .pUserData = pInFace};
	PlycutInput mapInput =
		{.pSizes = &pMapFace->size, .boundaries = 1, .pUserData = pMapFace};
	bool overlaps = false;
	PixErr err = plycutClip(
		&pBasic->pCtx->alloc,
		pBasic,
		NULL, inInput, getInCornerPos,
		NULL, mapInput, getMapCornerPos,
		NULL,
		&overlaps
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	if (overlaps) {
		return STUC_FACE_OVERLAP_INTERSECT;
	}
	PIX_ERR_CATCH(0, err, ;);
	return STUC_FACE_OVERLAP_NONE;
	/*
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
		PIX_ERR_ASSERT("should have returned earlier if not", allInside);
		return STUC_FACE_OVERLAP_MAP_INSIDE_IN;
	}
	return windNum % 2 ? STUC_FACE_OVERLAP_IN_INSIDE_MAP : STUC_FACE_OVERLAP_NONE;
	*/
}

static
StucErr getEncasedFacesPerFace(
	FindEncasedFacesJobArgs *pArgs,
	FaceCellsTable *pFaceCellsTable,
	V2_I16 tile,
	FaceRange *pInFace
) {
	StucErr err = PIX_ERR_SUCCESS;
	V2_F32 fTileMin = {(F32)tile.d[0], (F32)tile.d[1]};
	PIX_ERR_ASSERT("", pInFace->size == 3 || pInFace->size == 4);
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
			OverlapType overlap =
				doInAndMapFacesOverlap(
				pArgs->core.pBasic,
				pInMesh, pInFace,
				pMap->pMesh, &mapFace
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
	PIX_ERR_CATCH(0, err, ;);
	return PIX_ERR_SUCCESS;
}

static
StucErr getEncasedFacesPerTile(
	FindEncasedFacesJobArgs *pArgs,
	FaceRange *pInFace,
	FaceCellsTable *pFaceCellsTable,
	I32 faceIdx
) {
	StucErr err = PIX_ERR_SUCCESS;
	FaceBounds *pFaceBounds = 
		&stucIdxFaceCells(pFaceCellsTable, faceIdx, pArgs->core.range.start)->faceBounds;
	for (I32 j = pFaceBounds->min.d[1]; j <= pFaceBounds->max.d[1]; ++j) {
		for (I32 k = pFaceBounds->min.d[0]; k <= pFaceBounds->max.d[0]; ++k) {
			if (j < INT16_MIN || j > INT16_MAX || k < INT16_MIN || k > INT16_MAX) {
				continue;
			}
			V2_I16 tile = {k, j};
			err = getEncasedFacesPerFace(
				pArgs,
				pFaceCellsTable,
				tile,
				pInFace
			);
			PIX_ERR_RETURN_IFNOT(err, "");
		}
	}
	return err;
}

static
StucErr getEncasedFaces(FindEncasedFacesJobArgs *pArgs, FaceCellsTable *pFaceCellsTable) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("record stores tiles with 16 bits earch", STUC_TILE_BIT_LEN <= 16);
	const MapToMeshBasic *pBasic = pArgs->core.pBasic;
	for (I32 i = pArgs->core.range.start; i < pArgs->core.range.end; ++i) {
		if (pBasic->maskIdx != -1 && pBasic->pInMesh->pMatIdx &&
		    pBasic->pInMesh->pMatIdx[i] != pBasic->maskIdx) {

			continue;
		}
		FaceRange inFace = {0};
		inFace.start = pBasic->pInMesh->core.pFaces[i];
		inFace.end = pBasic->pInMesh->core.pFaces[i + 1];
		inFace.size = inFace.end - inFace.start;
		inFace.idx = i;
		bool skipped = false;
		if (inFace.size <= 4) {
			err = getEncasedFacesPerTile(pArgs, &inFace, pFaceCellsTable, i);
		}
		else {
			skipped = true;
		}
		if (!skipped) {
			FaceCells *pFaceCellsEntry =
				stucIdxFaceCells(pFaceCellsTable, i, pArgs->core.range.start);
			stucDestroyFaceCellsEntry(&pBasic->pCtx->alloc, pFaceCellsEntry);
		}
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	return err;
}

StucErr stucFindEncasedFaces(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	FindEncasedFacesJobArgs *pArgs = pArgsVoid;
	PIX_ERR_ASSERT("", pArgs);
	StucContext pCtx = pArgs->core.pBasic->pCtx;

	FaceCellsTable faceCellsTable = {0};
	I32 averageMapFacesPerFace = 0;
	stucGetEncasingCells(
		&pCtx->alloc,
		pArgs->core.pBasic->pMap,
		pArgs->core.pBasic->pInMesh,
		pArgs->core.pBasic->maskIdx,
		pArgs->core.range,
		&faceCellsTable,
		&averageMapFacesPerFace
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	EncasedMapFaceTableState tableState =  {.pBasic = pArgs->core.pBasic};
	stucHTableInit(
		&pArgs->core.pBasic->pCtx->alloc,
		&pArgs->encasedFaces,
		faceCellsTable.uniqueFaces / 4 + 1,
		(I32Arr) {.pArr = (I32[]) {sizeof(EncasedMapFace)}, .count = 1},
		&tableState
	);
	err = getEncasedFaces(pArgs, &faceCellsTable);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err, ;);
	stucDestroyFaceCellsTable(&pCtx->alloc, &faceCellsTable, pArgs->core.range);
	return err;
}

static
I32 encasedTableJobsGetRange(const MapToMeshBasic *pBasic, void *pInitInfo) {
	return pBasic->pInMesh->core.faceCount;
}

typedef struct InPieceInitInfo {
	EncasedMapFace *pMapFace;
	InPieceArr *pInPieceArr;
} InPieceInitInfo;

static
void inPieceInit (
	void *pUserData,
	HTableEntryCore *pIdxEntryCore,
	const void *pKeyData,
	void *pInitInfoVoid,
	I32 linIdx
) {
	EncasedEntryIdx *pIdxEntry = (EncasedEntryIdx *)pIdxEntryCore;
	InPieceArr *pInPieceArr = ((InPieceInitInfo *)pInitInfoVoid)->pInPieceArr;
	const InPieceKey *pKey = pKeyData;
	pIdxEntry->mapFace = pKey->mapFace;
	pIdxEntry->tile = pKey->tile;
	pIdxEntry->entryIdx = pInPieceArr->count;

	EncasedMapFace *pMapFace = ((InPieceInitInfo *)pInitInfoVoid)->pMapFace;
	InPiece *pInPiece = pInPieceArr->pArr + pInPieceArr->count;
	pInPiece->pList = pMapFace;
	pInPiece->faceCount = pMapFace->inFaces.count;
	pInPieceArr->count++;
}

static
bool inPieceCmp(
	const HTableEntryCore *pIdxEntryCore,
	const void *pKeyData,
	const void *pInitInfo
) {
	const EncasedEntryIdx *pIdxEntry = (EncasedEntryIdx *)pIdxEntryCore;
	const InPieceKey *pMapFace = pKeyData;
	return
		pIdxEntry->mapFace == pMapFace->mapFace &&
		_(pIdxEntry->tile V2I16EQL pMapFace->tile);
}

static
void appendEncasedEntryToInPiece(
	EncasedMapFace *pEntry,
	EncasedEntryIdx *pIdxEntry,
	InPieceArr *pInPieceArr
) {
	pInPieceArr->pArr[pIdxEntry->entryIdx].faceCount += pEntry->inFaces.count;
	HTableEntryCore *pInPiece = (HTableEntryCore *)pInPieceArr->pArr[pIdxEntry->entryIdx].pList;
	while (pInPiece->pNext) {
		pInPiece = pInPiece->pNext;
	}
	pInPiece->pNext = (HTableEntryCore *)pEntry;
}

static
void addEncasedEntryToInPieceArr(
	const MapToMeshBasic *pBasic,
	HTable *pIdxTable,
	InPieceArr *pInPieceArr,
	EncasedMapFace *pMapFace
) {
	EncasedEntryIdx *pIdxEntry = NULL;
	SearchResult result = stucHTableGet(
		pIdxTable,
		0,
		&(InPieceKey) {.mapFace = pMapFace->mapFace, .tile = pMapFace->tile},
		(void **)&pIdxEntry,
		true, &(InPieceInitInfo) {.pMapFace = pMapFace, .pInPieceArr = pInPieceArr},
		stucInPieceMakeKey, NULL, inPieceInit, inPieceCmp
	);
	if (result == STUC_SEARCH_FOUND) {
		appendEncasedEntryToInPiece(pMapFace, pIdxEntry, pInPieceArr);
	}
}

static
void linkEncasedTableEntries(
	const MapToMeshBasic *pBasic,
	I32 jobCount,
	FindEncasedFacesJobArgs *pJobArgs,
	InPieceArr *pInPieceArr,
	bool *pEmpty
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	pInPieceArr->size = pInPieceArr->count = 0;
	for (I32 i = 0; i < jobCount; ++i) {
		PixalcLinAlloc *pTableAlloc = stucHTableAllocGet(&pJobArgs[i].encasedFaces, 0);
		pInPieceArr->size += pixalcLinAllocGetCount(pTableAlloc);
	}
	if (pInPieceArr->size == 0) {
		*pEmpty = true;
		return;
	}
	pInPieceArr->pArr = pAlloc->fpCalloc(pInPieceArr->size, sizeof(InPiece));
	HTable idxTable = {0};
	stucHTableInit(
		pAlloc,
		&idxTable,
		pInPieceArr->size / 4 + 1,
		(I32Arr) {.pArr = (I32[]) {sizeof(EncasedEntryIdx)}, .count = 1},
		NULL
	);

	for (I32 i = 0; i < jobCount; ++i) {
		PixalcLinAlloc *pTableAlloc = stucHTableAllocGet(&pJobArgs[i].encasedFaces, 0);
		PixalcLinAllocIter iter = {0};
		pixalcLinAllocIterInit(pTableAlloc, (Range) {0, INT32_MAX}, &iter);
		for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
			EncasedMapFace *pEntry = pixalcLinAllocGetItem(&iter);
			addEncasedEntryToInPieceArr(pBasic, &idxTable, pInPieceArr, pEntry);
			pEntry->core.pNext = NULL;
		}
	}
	stucHTableDestroy(&idxTable);
	*pEmpty = false;
}

StucErr stucInPieceArrInit(
	MapToMeshBasic *pBasic,
	InPieceArr *pInPieces,
	I32 *pJobCount, FindEncasedFacesJobArgs *pJobArgs,
	bool *pEmpty
) {
	StucErr err = PIX_ERR_SUCCESS;
	stucMakeJobArgs(
		pBasic,
		pJobCount, pJobArgs, sizeof(FindEncasedFacesJobArgs),
		NULL,
		encasedTableJobsGetRange, NULL
	);
	err = stucDoJobInParallel(
		pBasic,
		*pJobCount, pJobArgs, sizeof(FindEncasedFacesJobArgs),
		stucFindEncasedFaces
	);
	PIX_ERR_RETURN_IFNOT(err, "");

	linkEncasedTableEntries(
		pBasic,
		*pJobCount, pJobArgs,
		pInPieces,
		pEmpty
	);
	return err;
}