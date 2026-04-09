/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <poly_cutout.h>

#include <in_piece.h>
#include <quadtree.h>
#include <map.h>
#include <utils.h>

typedef struct EncasedMapFaceInitInfo {
	InFaceMem *pInFaces;
	const FaceRange *pInFace;
	I32 job;
	bool inFaceWind;
	bool border;
} EncasedMapFaceInitInfo;

typedef struct EncasedMapFaceTableState {
	const struct MapToMeshBasic *pBasic;
} EncasedMapFaceTableState;

static
void initInTri(
	BaseTriVerts *pInTri,
	const Mesh *pInMesh,
	FaceRange *pInFace
) {
	for (I32 i = 0; i < pInFace->size; ++i) {
		I32 corner = pInFace->start + i;
		pInTri->uv[i] = pInMesh->pUvs[corner];
		pInTri->xyz[i] = pInMesh->pPos[pInMesh->core.pCorners[corner]];
	}
	stucGetTriScale(pInFace->size, pInTri);
}

#ifdef STUC_QUADTREE_ENABLE
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
#endif

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
	PixuctHTableEntryCore *pEntryCore,
	const void *pKeyData,
	void *pInitInfoVoid,
	I32 linAlloc
) {
	EncasedMapFaceTableState *pState = pUserData;
	const StucAlloc *pAlloc = &pState->pBasic->pCtx->alloc;
	const InPieceKey *pKey = pKeyData;
	EncasedMapFaceInitInfo *pInitInfo = pInitInfoVoid;
	EncasedMapFace *pEntry = (EncasedMapFace *)pEntryCore;
	pEntry->cluster = pKey->cluster;
	pEntry->job = pInitInfo->job;
	pEntry->clip = pKey->clip;
	pEntry->tile = pKey->tile;
	pEntry->border = pInitInfo->border;
	InFaceMem *pInFaces = pInitInfo->pInFaces;
	PIXALC_DYN_ARR_ADD(InFaceIdxArr, pAlloc, pInFaces, pEntry->inFaces);
	if (pEntry->inFaces >= pInFaces->initCount) {
		PIX_ERR_ASSERT("", pEntry->inFaces == pInFaces->initCount);
		pInFaces->pArr[pEntry->inFaces] = (InFaceIdxArr){0};
		++pInFaces->initCount;
	}
	else {
		pInFaces->pArr[pEntry->inFaces].count = 0;
	}
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(I32, pAlloc, pInFaces->pArr + pEntry->inFaces, newIdx);
	pInFaces->pArr[pEntry->inFaces].pArr[newIdx] = (InFaceIdx){
		.idx = (U32)pInitInfo->pInFace->idx,
		.wind = pInitInfo->inFaceWind,
		.border = pInitInfo->border
	};
}

static
bool encasedMapFaceCmp(
	const PixuctHTableEntryCore *pEntryRaw,
	const void *pKeyData,
	const void *pInitInfo
) {
	const EncasedMapFace *pEntry = (void *)pEntryRaw;
	const InPieceKey *pKey = pKeyData;
	return 
		pEntry->cluster == pKey->cluster &&
		pEntry->clip == pKey->clip &&
		_(pEntry->tile V2I16EQL pKey->tile);
}

static
void appendToEncasedEntry(
	FindEncasedFacesJobArgs *pArgs,
	EncasedMapFace *pEntry,
	const FaceRange *pInFace,
	bool wind,
	bool border
) {
	const StucAlloc *pAlloc = &((const MapToMeshBasic *)pArgs->core.pShared)->pCtx->alloc;
	InFaceIdxArr *pInFaces = pArgs->inFaces.pArr + pEntry->inFaces;
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(I32, pAlloc, pInFaces, newIdx);
	pInFaces->pArr[newIdx] = (InFaceIdx){
		.idx = (U32)pInFace->idx,
		.wind = wind,
		.border = border
	};
}

static
EncasedMapFace *addToEncasedFaces(
	FindEncasedFacesJobArgs *pArgs,
	const FaceRange *pInFace,
	bool inFaceWind,
	I32 cluster,
	bool clip,
	V2_I16 tile,
	bool border
) {
	EncasedMapFace *pEntry = NULL;
	SearchResult result = pixuctHTableGet(
		&pArgs->encasedFaces,
		clip,
		&(InPieceKey) {.cluster = cluster, .clip = clip, .tile = tile},
		(void**)&pEntry,
		true,
		&(EncasedMapFaceInitInfo) {
			.pInFaces = &pArgs->inFaces,
			.pInFace = pInFace,
			.job = pArgs->core.id,
			.inFaceWind = inFaceWind,
			.border = border
		},
		stucInPieceMakeKey, NULL, encasedMapFaceInit, encasedMapFaceCmp
	);
	if (result == PIX_SEARCH_FOUND) {
		PIX_ERR_ASSERT("", pEntry);
		appendToEncasedEntry(
			pArgs,
			pEntry,
			pInFace,
			inFaceWind,
			border
		);
	}
	return pEntry;
}

typedef enum OverlapType {
	STUC_FACE_OVERLAP_NONE,
	STUC_FACE_OVERLAP_INTERSECT,
	STUC_FACE_OVERLAP_IN_INSIDE_MAP,
	STUC_FACE_OVERLAP_MAP_INSIDE_IN
} OverlapType;

//TODO put a generic void * to const void * struct in pixenals-types, & replace this
typedef struct InCornerPtr {
	const HalfPlane *pInCorners;
} InCornerPtr;

static
V2_F32 getInCornerPos(
	const void *pUserData,
	void *pMeshVoid,
	PlycutInput input,
	I32 boundary,
	I32 corner,
	bool *pCantIntersect
) {
	return ((const HalfPlane *)((InCornerPtr *)pMeshVoid)->pInCorners)[corner].uv;
}

static
V3_F32 getMapCornerPos(
	const void *pUserData,
	void *pMeshVoid,
	PlycutInput input,
	I32 boundary,
	I32 corner,
	bool *pCantIntersect
) {
	const Mesh *pMesh = ((MapToMeshBasic *)pUserData)->pMap->pMesh;
	const FaceRange *pMapFace = input.pUserData;
	return stucGetVertPos(pMesh, pMapFace, corner);
}

#if false
static
OverlapType doInAndMapFacesOverlap(
	const MapToMeshBasic *pBasic,
	const HalfPlane *pInCorners, FaceRange *pInFace,
	const Mesh *pMapMesh, FaceRange *pMapFace,
	PlycutMem *pPlycutAlc
) {
	PlycutInput inInput =
		{.pSizes = &pInFace->size, .boundaries = 1, .pUserData = pInFace};
	PlycutInput mapInput =
		{.pSizes = &pMapFace->size, .boundaries = 1, .pUserData = pMapFace};
	bool overlaps = false;
	PixErr err = plycutClip(
		&pBasic->pCtx->alloc,
		pBasic,
		&(InCornerPtr){.pInCorners = pInCorners}, inInput, getInCornerPos,
		NULL, mapInput, getMapCornerPos,
		NULL,
		&overlaps,
		pPlycutAlc
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	if (overlaps) {
		return STUC_FACE_OVERLAP_INTERSECT;
	}
	PIX_ERR_CATCH(0, err, ;);
	return STUC_FACE_OVERLAP_NONE;
}
#endif

typedef struct InPieceClust {
	FindEncasedFacesJobArgs *pArgs;
	FaceRange inFace;
	I32 inFaceWind;
	bool borderFace;
} InPieceClust;

typedef struct FaceMesh {
	const Mesh *pMesh;
	FaceRange range;
} FaceMesh;

static
bool isClustOnBorder(
	FindEncasedFacesJobArgs *pArgs,
	I32 cluster,
	V2_I32 tile
) {
	const MapToMeshBasic *pBasic = pArgs->core.pShared;
	PixtyRange faces = {0};
	clutreFaceRangeGet(&pBasic->pMap->clustTree, cluster, &faces);
	V2_I16 tile16 = {tile.d[0], tile.d[1]};
	const IslandClustArr *pArr = pArgs->pClustArr;
	PixtyRange tileRange = {0};
	for (I32 i = 0; i < pArr->tiles.count; ++i) {
		if (_(tile16 V2I16EQL pArr->tiles.pArr[i].tile)) {
			tileRange = pArr->tiles.pArr[i].range;
			break;
		}
	}
	PIX_ERR_ASSERT("", tileRange.start >= 0 && tileRange.end > tileRange.start);
	PixtyRange range = tileRange;
	I32 rangeSize;
	while ((rangeSize = range.end - range.start) / 2) {
		I32 mid = range.start + rangeSize / 2;
		PixtyRange midFaces = {0};
		clutreFaceRangeGet(&pBasic->pMap->clustTree, pArr->pArr[mid].idx, &midFaces);
		if (faces.start >= midFaces.start) {
			range.start = mid;
		}
		else {
			range.end = mid;
		}
	};
	PixtyRange parentFaces = {0};
	clutreFaceRangeGet(
		&pBasic->pMap->clustTree,
		pArr->pArr[range.start].idx,
		&parentFaces
	);
	PIX_ERR_ASSERT("", faces.start >= parentFaces.start && faces.end <= parentFaces.end);
	ClutreIntersect type = (ClutreIntersect)pArr->pArr[range.start].type;
	return type == CLUTRE_INTERSECT || type == CLUTRE_ENCLOSED;
}

static
PixErr inPieceAddFace(
	const PixalcFPtrs *pAlloc,
	void *pInfoRaw,
	I32 idx,
	ClutreIntersect status,
	V2_I32 tile
) {
	PixErr err = PIX_ERR_SUCCESS;
	InPieceClust *pInfo = pInfoRaw;
	addToEncasedFaces(
		pInfo->pArgs,
		&pInfo->inFace,
		pInfo->inFaceWind,
		idx,
		isClustOnBorder(pInfo->pArgs, idx, tile),
		(V2_I16){(I16)tile.d[0], (I16)tile.d[1]},
		pInfo->borderFace
	);
	return err;
}

typedef struct FaceInfo {
	const Mesh *pMesh;
	FaceRange face;
} FaceInfo;

static
PixtyV2_F32 stucClustFaceUv(const void *pFaceRaw, I32 localCorner) {
	const FaceInfo *pFace = pFaceRaw;
	I32 corner = pFace->face.start + localCorner;
	PIX_ERR_ASSERT(
		"",
		pFace->pMesh->pUvs && corner >= 0 && corner < pFace->pMesh->core.cornerCount
	);
	return pFace->pMesh->pUvs[corner];
}

static
StucErr getEncasedFacesPerFace(FindEncasedFacesJobArgs *pArgs, FaceRange *pInFace) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("", pInFace->size == 3 || pInFace->size == 4);
	const MapToMeshBasic *pBasic = (const MapToMeshBasic *)pArgs->core.pShared;

	BaseTriVerts inTri = {0};
	initInTri(&inTri, pBasic->pInMesh, pInFace);
	if (isTriDegenerate(&inTri, pInFace)) {
		return err; //skip
	}
	I32 inFaceWind = stucCalcFaceWindFromUvs(pInFace, pBasic->pInMesh);
	if (inFaceWind == 2) {
		//face is degenerate - skip
		return err;
	}
	FaceMesh faceMesh = {.pMesh = pBasic->pInMesh, .range = *pInFace};
	ClutreFace clustFace = {
		.pUserData = &faceMesh,
		.fpPos = stucClustFaceUv,
		.size = pInFace->size
	};
	InPieceClust clustInfo = {
		.pArgs = pArgs,
		.inFace = *pInFace,
		.inFaceWind = inFaceWind,
		stucIsInFaceOnBorder(pBasic->pInMesh, pArgs->pClustArr, pInFace, NULL)
	};
	ClutreArr clustArr = {.pUserData = &clustInfo, .fpAdd = inPieceAddFace};
	err = clutreSampleForFace(
		&pBasic->pMap->clustTree,
		&pArgs->pClustArr->start,
		&clustFace,
		&clustArr,
		false
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	PIX_ERR_CATCH(0, err, ;);
	return err;
}


#ifdef STUC_QUADTREE_ENABLE
static
StucErr getEncasedFacesPerTile(
	FindEncasedFacesJobArgs *pArgs,
	FaceRange *pInFace,
	FaceCellsTable *pFaceCellsTable,
	I32 faceIdx,
	PlycutMem *pPlycutAlc
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
				pInFace,
				pPlycutAlc
			);
			PIX_ERR_RETURN_IFNOT(err, "");
		}
	}
	return err;
}
#endif

static
StucErr getEncasedFaces(FindEncasedFacesJobArgs *pArgs) {
	StucErr err = PIX_ERR_SUCCESS;
	const MapToMeshBasic *pBasic = pArgs->core.pShared;
	const StucInIsland *pIsland = pArgs->pClustArr->pIsland;
	for (I32 i = pArgs->core.range.start; i < pArgs->core.range.end; ++i) {
		I32 face = pBasic->pInIslands->pFaces[pIsland->core.faces.start + i];
		if (pBasic->maskIdx != -1 && pBasic->pInMesh->pMatIdx &&
		    pBasic->pInMesh->pMatIdx[face] != pBasic->maskIdx) {

			continue;
		}
		FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, face);
		if (inFace.size <= 4) {
			err = getEncasedFacesPerFace(pArgs, &inFace);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}
	}
	PIX_ERR_CATCH(0, err, ;);
	return err;
}

StucErr stucFindEncasedFaces(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	FindEncasedFacesJobArgs *pArgs = pArgsVoid;
	PIX_ERR_ASSERT("", pArgs);
	const MapToMeshBasic *pBasic = pArgs->core.pShared;
	StucContext pCtx = pBasic->pCtx;
	
	EncasedMapFaceTableState tableState =  {.pBasic = pBasic};
	pixuctHTableInit(
		&pBasic->pCtx->alloc,
		&pArgs->encasedFaces,
		clutreTreeCount(&pBasic->pMap->clustTree) / 4 + 2,
		(I32Arr) {
			.pArr = (I32[]) {sizeof(EncasedMapFace), sizeof(EncasedMapFace)},
			.count = 2
		},
		NULL,
		&tableState,
		true
	);
	err = getEncasedFaces(pArgs);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err, ;);
	return err;
}

typedef struct FindEncasedJobInit {
	const IslandClustArr *pClustArr;
} FindEncasedJobInit;

static
I32 encasedTableJobsGetRange(
	const StucContext pCtx,
	const void *pShared,
	void *pInitInfo
) {
	Range faces = ((FindEncasedJobInit *)pInitInfo)->pClustArr->pIsland->core.faces;
	return faces.end - faces.start;
}

typedef struct InPieceInitInfo {
	const InFaceMem *pInFaces;
	EncasedMapFace *pCluster;
	InPieceArr *pInPieceArr;
} InPieceInitInfo;

static
void inPieceInit (
	void *pUserData,
	PixuctHTableEntryCore *pIdxEntryCore,
	const void *pKeyData,
	void *pInitInfoVoid,
	I32 linIdx
) {
	EncasedEntryIdx *pIdxEntry = (EncasedEntryIdx *)pIdxEntryCore;
	InPieceInitInfo *pInitInfo = pInitInfoVoid;
	InPieceArr *pInPieceArr = pInitInfo->pInPieceArr;
	const InPieceKey *pKey = pKeyData;
	pIdxEntry->cluster = pKey->cluster;
	pIdxEntry->clip = pKey->clip;
	pIdxEntry->tile = pKey->tile;
	pIdxEntry->entryIdx = pInPieceArr->count;

	EncasedMapFace *pCluster = ((InPieceInitInfo *)pInitInfoVoid)->pCluster;
	InPiece *pInPiece = pInPieceArr->pArr + pInPieceArr->count;
	pInPiece->pList = pCluster;
	pInPiece->tile = pKey->tile;
	pInPiece->faceCount = pInitInfo->pInFaces->pArr[pCluster->inFaces].count;
	pInPieceArr->count++;
}

static
bool inPieceCmp(
	const PixuctHTableEntryCore *pIdxEntryCore,
	const void *pKeyData,
	const void *pInitInfo
) {
	const EncasedEntryIdx *pIdxEntry = (EncasedEntryIdx *)pIdxEntryCore;
	const InPieceKey *pKey = pKeyData;
	return
		pIdxEntry->cluster == pKey->cluster &&
		pIdxEntry->clip == pKey->clip &&
		_(pIdxEntry->tile V2I16EQL pKey->tile);
}

static
void appendEncasedEntryToInPiece(
	const InFaceMem *pInFaces,
	EncasedMapFace *pEntry,
	EncasedEntryIdx *pIdxEntry,
	InPieceArr *pInPieceArr
) {
	pInPieceArr->pArr[pIdxEntry->entryIdx].faceCount +=
		pInFaces->pArr[pEntry->inFaces].count;
	PixuctHTableEntryCore *pInPiece =
		(PixuctHTableEntryCore *)pInPieceArr->pArr[pIdxEntry->entryIdx].pList;
	while (pInPiece->pNext) {
		pInPiece = pInPiece->pNext;
	}
	pInPiece->pNext = (PixuctHTableEntryCore *)pEntry;
}

static
void addEncasedEntryToInPieceArr(
	const FindEncasedFacesJobArgs *pArgs,
	PixuctHTable *pIdxTable,
	InPieceArr *pInPieceArr,
	EncasedMapFace *pCluster
) {
	EncasedEntryIdx *pIdxEntry = NULL;
	SearchResult result = pixuctHTableGet(
		pIdxTable,
		0,
		&(InPieceKey) {
			.cluster = pCluster->cluster, .clip = pCluster->clip, .tile = pCluster->tile
		},
		(void **)&pIdxEntry,
		true,
		&(InPieceInitInfo) {
			.pInFaces = &pArgs->inFaces, .pCluster = pCluster, .pInPieceArr = pInPieceArr
		},
		stucInPieceMakeKey, NULL, inPieceInit, inPieceCmp
	);
	if (result == PIX_SEARCH_FOUND) {
		appendEncasedEntryToInPiece(&pArgs->inFaces, pCluster, pIdxEntry, pInPieceArr);
	}
}

static
void iterAndAddJobPieces(
	I32 job,
	FindEncasedFacesJobArgs *pJobArgs,
	InPieceArr *pInPieceArr,
	PixuctHTable *pIdxTable,
	bool clip
) {
	PixalcLinAlloc *pAlloc = pixuctHTableAllocGet(&pJobArgs[job].encasedFaces, clip);
	PixalcLinAllocIter iter = {0};
	pixalcLinAllocIterInit(pAlloc, (Range) {0, INT32_MAX}, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		EncasedMapFace *pEntry = pixalcLinAllocGetItem(&iter);
		addEncasedEntryToInPieceArr(pJobArgs + job, pIdxTable, pInPieceArr, pEntry);
		pEntry->core.pNext = NULL;
	}
}

static
void linkEncasedTableEntries(
	const MapToMeshBasic *pBasic,
	I32 jobCount,
	FindEncasedFacesJobArgs *pJobArgs,
	InPieceArr *pInPieceArr,
	InPieceArr *pInPieceClipArr,
	bool *pEmpty
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	for (I32 i = 0; i < jobCount; ++i) {
		PixalcLinAlloc *pAlloc = pixuctHTableAllocGet(&pJobArgs[i].encasedFaces, 0);
		pInPieceArr->size += pixalcLinAllocGetCount(pAlloc);
		pAlloc = pixuctHTableAllocGet(&pJobArgs[i].encasedFaces, 1);
		pInPieceClipArr->size += pixalcLinAllocGetCount(pAlloc);
	}
	if (!pInPieceArr->size && !pInPieceClipArr->size) {
		*pEmpty = true;
		return;
	}
	if (pInPieceArr->size) {
		pInPieceArr->pArr = pAlloc->fpCalloc(pInPieceArr->size, sizeof(InPiece));
	}
	if (pInPieceClipArr->size) {
		pInPieceClipArr->pArr = pAlloc->fpCalloc(pInPieceClipArr->size, sizeof(InPiece));
	}
	PixuctHTable idxTable = {0};
	pixuctHTableInit(
		pAlloc,
		&idxTable,
		(pInPieceArr->size + pInPieceClipArr->size) / 4 + 1,
		(I32Arr) {.pArr = (I32[]) {sizeof(EncasedEntryIdx)}, .count = 1},
		NULL,
		NULL,
		true
	);

	for (I32 i = 0; i < jobCount; ++i) {
		iterAndAddJobPieces(i, pJobArgs, pInPieceArr, &idxTable, 0);
		iterAndAddJobPieces(i, pJobArgs, pInPieceClipArr, &idxTable, 1);
	}
	pixuctHTableDestroy(&idxTable);
	*pEmpty = false;
}


static
void encasedTableJobsInitArg(
	const StucContext pCtx,
	const void *pShared,
	void *pInitInfoRaw,
	void *pArgsRaw
) {
	FindEncasedFacesJobArgs *pArgs = pArgsRaw;
	InFaceMem inFaces = pArgs->inFaces;
	*pArgs = (FindEncasedFacesJobArgs){0};
	inFaces.count = 0;
	pArgs->inFaces = inFaces;
	pArgs->pClustArr = ((FindEncasedJobInit *)pInitInfoRaw)->pClustArr;
}

StucErr stucInPieceArrInit(
	const MapToMeshBasic *pBasic,
	I32 threadId,
	const IslandClustArr *pClustArr,
	InPieceArr *pInPieces,
	InPieceArr *pInPiecesClip,
	I32 *pJobCount, FindEncasedFacesJobArgs *pJobArgs,
	bool *pEmpty
) {
	StucErr err = PIX_ERR_SUCCESS;
	const PixalcFPtrs *pAlloc = &pBasic->pCtx->alloc;
	stucMakeJobArgs(
		pBasic->pCtx,
		pBasic,
		pJobCount, pJobArgs, sizeof(FindEncasedFacesJobArgs),
		&(FindEncasedJobInit){.pClustArr = pClustArr},
		encasedTableJobsGetRange, encasedTableJobsInitArg
	);
	err = stucDoJobInParallel(
		pBasic->pCtx,
		threadId,
		*pJobCount, pJobArgs, sizeof(FindEncasedFacesJobArgs),
		stucFindEncasedFaces
	);
	PIX_ERR_RETURN_IFNOT(err, "");

	linkEncasedTableEntries(
		pBasic,
		*pJobCount, pJobArgs,
		pInPieces,
		pInPiecesClip,
		pEmpty
	);
	return err;
}
