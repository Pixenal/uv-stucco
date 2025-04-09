/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include <float.h>

#include <context.h>
#include <map_to_job_mesh.h>
#include <map.h>
#include <math_utils.h>
#include <attrib_utils.h>
#include <utils.h>
#include <error.h>
#include <alloc.h>

#define STUC_ON_LINE_SNAP_THRESHOLD .00001f

typedef struct TriXyz {
	V3_F32 a;
	V3_F32 b;
	V3_F32 c;
} TriXyz;

typedef struct TriUv {
	V2_F32 a;
	V2_F32 b;
	V2_F32 c;
} TriUv;

typedef struct InFaceCacheEntry {
	HTableEntryCore core;
	FaceRange face;
	V2_F32 fMin;
	V2_F32 fMax;
	bool wind;
} InFaceCacheEntry;

typedef struct InFaceCacheEntryIntern {
	InFaceCacheEntry faceEntry;
	HalfPlane *pCorners;
} InFaceCacheEntryIntern;

/*
typedef struct {
	InFaceCacheEntryIntern *pList;
} InFaceCacheBucket;

typedef struct {
	InFaceCacheBucket *pTable;
	I32 size;
	bool initBounds;
} InFaceCache;
*/

/*
typedef struct {
	MapIslandCorner *pArr;
	I32 count;
	I32 size;
} CornerAncestors;

typedef struct {
	MapIsland *pIsland;
	MapIsland *pPending;
	I32 corner;
} IslandIdxPair;
*/

typedef struct InsideCache {
	int8_t status; // uses InsideStatus
	bool markAsOnLine;
} InsideCache;

typedef struct Segments {
	F32 *pArr;
	I32 *pIndices;
	I32 size;
	I32 count;
} Segments;

#ifndef TEMP_DISABLE
static
bool checkIfOnVert(MapIsland *pMapIsland, I32 i, I32 iNext) {
	return
		(
			pMapIsland->pCorners[i].inCorner == pMapIsland->pCorners[iNext].inCorner ||
			pMapIsland->pCorners[i].onInVert || pMapIsland->pCorners[iNext].onInVert
		) &&
		(
			(!pMapIsland->pCorners[i].isMapCorner && !pMapIsland->pCorners[iNext].isMapCorner) ||
			(
				(pMapIsland->pCorners[i].onLine || pMapIsland->pCorners[iNext].onLine) &&
				(pMapIsland->pCorners[i].isMapCorner ^ pMapIsland->pCorners[iNext].isMapCorner)
			) ||
			(
				pMapIsland->pCorners[i].onLine && pMapIsland->pCorners[iNext].onLine &&
				pMapIsland->pCorners[i].isMapCorner && pMapIsland->pCorners[iNext].isMapCorner
			)
		);
}

//this corner already resided on a previous base edge,
//it must then reside on a base vert, rather than an edge
static
void handleOnInVert(
	MapIslandCorner *pNewEntry,
	MapIsland *pMapIsland,
	I32 idx,
	InCornerCache *pInCorner,
	bool mapFaceWind, bool inFaceWind
) {
	I32 lastBaseCorner = mapFaceWind ? pInCorner->idx - 1 : pInCorner->idx + 1;
	bool whichVert = pMapIsland->pCorners[idx].inCorner == lastBaseCorner;
	if (inFaceWind) {
		pNewEntry->inCorner = whichVert ?
			pInCorner->idx : pInCorner->idxNext;
		if (!whichVert) {
			//if the corner maintains it's existing incorner,
			//then ensure the segment also carries over.
			// This is redundant if called on an insideCorner,
			// as whole corner buf entry is copied
			pNewEntry->segment = pMapIsland->pCorners[idx].segment;
		}
	}
	else {
		pNewEntry->inCorner = whichVert ?
			pInCorner->idxPrev : pInCorner->idx;
		if (whichVert) {
			pNewEntry->segment = pMapIsland->pCorners[idx].segment;
		}
	}
	pNewEntry->onInVert = true;
}

static
I32 appendToAncestors(
	const StucAlloc *pAlloc,
	CornerAncestors *pAncestors,
	MapIslandCorner *pCorner
) {
	STUC_ASSERT("", pAncestors->count <= pAncestors->size);
	if (pAncestors->count == pAncestors->size) {
		pAncestors->size *= 2;
		pAncestors->pArr =
			pAlloc->fpRealloc(pAncestors->pArr, sizeof(MapIslandCorner) * pAncestors->size);
	}
	I32 idx = pAncestors->count;
	pAncestors->pArr[idx] = *pCorner;
	pAncestors->count++;
	return idx;
}

static
void getMapVertsFromBufCorners(
	StucMap pMap,
	V2_F32 fTileMin,
	MapIslandCorner *pCorner, MapIslandCorner *pCornerNext,
	FaceRange *pMapFace,
	V3_F32 *pVertA, V3_F32 *pVertB
) {
	const Mesh *pMapMesh = pMap->pMesh;
	if (pCorner->isMapCorner) {
		*pVertA = pCorner->corner;
		I32 mapCornerNext = (pCorner->mapCorner + 1) % pMapFace->size;
		*pVertB =
			pMapMesh->pPos[pMapMesh->core.pCorners[pMapFace->start + mapCornerNext]];
		_((V2_F32 *)pVertB V2ADDEQL fTileMin);
	}
	else if (pCornerNext->isMapCorner) {
		*pVertB = pCornerNext->corner;
		I32 mapCornerPrev = pCornerNext->mapCorner ?
			pCornerNext->mapCorner - 1 : pMapFace->size - 1;
		*pVertA =
			pMapMesh->pPos[pMapMesh->core.pCorners[pMapFace->start + mapCornerPrev]];
		_((V2_F32 *)pVertA V2ADDEQL fTileMin);
	}
	else {
		V3_F32 mapVert =
			pMapMesh->pPos[pMapMesh->core.pCorners[pMapFace->start + pCorner->mapCorner]];
		I32 mapCornerNext = (pCorner->mapCorner + 1) % pMapFace->size;
		V3_F32 mapVertNext =
			pMapMesh->pPos[pMapMesh->core.pCorners[pMapFace->start + mapCornerNext]];
		F32 dot = _(
			_(pCornerNext->corner V3SUB pCorner->corner) V3DOT
			_(pCorner->corner V3SUB mapVert)
		);
		bool which = dot >= .0f;
		*pVertA = which ? mapVert : mapVertNext;
		*pVertB = which ? mapVertNext : mapVert;
		_((V2_F32 *)pVertA V2ADDEQL fTileMin);
		_((V2_F32 *)pVertB V2ADDEQL fTileMin);
	}
}

static
bool areCornersOnSameMapEdge(
	MapIslandCorner *pCorner, MapIslandCorner *pCornerNext,
	FaceRange *pMapFace
) {
	I32 mapCorner = pCorner->mapCorner;
	I32 mapCornerNext = pCornerNext->mapCorner;
	return !(
		pCorner->onInVert && !pCorner->onLine ||
		pCornerNext->onInVert && !pCornerNext->onLine ||
		pCorner->isMapCorner && pCornerNext->isMapCorner ||
		(pCorner->isMapCorner || !pCorner->isMapCorner && !pCornerNext->isMapCorner) &&
				mapCorner != mapCornerNext ||
		pCornerNext->isMapCorner &&
			stucGetCornerPrev(mapCornerNext, pMapFace) != mapCorner
	);
}

static
void addIntersectionToBuf(
	MapToMeshBasic *pBasic,
	V2_F32 fTileMin,
	FaceRange *pMapFace,
	MapIsland *pNewMapIsland,
	MapIsland *pMapIsland,
	I32 i,
	I32 iNext,
	InCornerCache *pInCorner,
	IslandIdxPair *pIntersectCache,
	F32 *ptBuf,
	I32 *pCount,
	bool mapFaceWind, bool inFaceWind,
	CornerAncestors *pAncestors
) {
	pIntersectCache[*pCount].pIsland = pNewMapIsland;
	pIntersectCache[*pCount].corner = pNewMapIsland->count;
	MapIslandCorner *pCorner = pMapIsland->pCorners + i;
	MapIslandCorner *pCornerNext = pMapIsland->pCorners + iNext;
	MapIslandCorner *pNewEntry = pNewMapIsland->pCorners + pNewMapIsland->count;

	if (areCornersOnSameMapEdge(pCorner, pCornerNext, pMapFace)) {
		stucCalcIntersection(
			pCorner->corner, pCornerNext->corner,
			pInCorner->uv, pInCorner->dir,
			NULL,
			&pNewEntry->tMapEdge, &pNewEntry->tInEdge
		);
		V3_F32 mapVertA = {0};
		V3_F32 mapVertB = {0};
		getMapVertsFromBufCorners(
			pBasic->pMap,
			fTileMin,
			pCorner, pCornerNext,
			pMapFace,
			&mapVertA, &mapVertB
		);
		stucCalcIntersection(
			mapVertA,
			mapVertB,
			pInCorner->uv, pInCorner->dir,
			&pNewEntry->corner,
			NULL, NULL
		);
	}
	else {
		stucCalcIntersection(
			pCorner->corner, pCornerNext->corner,
			pInCorner->uv, pInCorner->dir,
			&pNewEntry->corner,
			&pNewEntry->tMapEdge, &pNewEntry->tInEdge
		);
	}
	pNewEntry->tInEdge *= -1.0f;
	//this attrib is lerped here instead of later like other attribs,
	//as it's needed to transform from uvw to xyz
	//TODO is this still necessary? or is it obsolete?
	pNewEntry->normal =
		v3F32Lerp(pCorner->normal, pCornerNext->normal, pNewEntry->tMapEdge);
	//pNewEntry->normal = pMapIsland->pCorners[i].normal;
	//V3_F32 up = {.0f, .0f, 1.0f};
	//pNewEntry->normal = up;
	if (checkIfOnVert(pMapIsland, i, iNext)) {
		handleOnInVert(
			pNewEntry,
			pMapIsland, i,
			pInCorner,
			mapFaceWind, inFaceWind
		);
	}
	else {
		pNewEntry->inCorner = (I8)pInCorner->idx;
		pNewEntry->onInVert = false;
	}
	pNewEntry->ancestor =
		appendToAncestors(&pBasic->pCtx->alloc, pAncestors, pMapIsland->pCorners + i);
	pNewEntry->ancestorNext =
		appendToAncestors(&pBasic->pCtx->alloc, pAncestors, pMapIsland->pCorners + iNext);
	pNewEntry->isMapCorner = false;
	pNewEntry->mapCorner = pMapIsland->pCorners[i].mapCorner;
	if (pMapIsland->pCorners[i].onLine && pMapIsland->pCorners[iNext].onLine) {
		pNewEntry->onLine = true;
	}
	ptBuf[*pCount] = pNewEntry->tInEdge;
	++*pCount;
	pNewMapIsland->count++;
}

static
void initPendingMerge(const StucAlloc *pAlloc, MapIsland *pIsland) {
	pIsland->mergeSize = 3;
	pIsland->pPendingMerge = pAlloc->fpCalloc(pIsland->mergeSize, sizeof(void *));
	pIsland->pPendingMerge[0] = -1;
	pIsland->mergeCount = 1;
}

static
Result addToPendingMerge(const StucAlloc *pAlloc, MapIsland *pIsland, I32 value) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", pIsland->pPendingMerge);
	STUC_ASSERT("", pIsland->mergeSize > 0);
	pIsland->pPendingMerge[pIsland->mergeCount] = value;
	pIsland->mergeCount++;
	if (pIsland->mergeCount == pIsland->mergeSize) {
		pIsland->mergeSize *= 2;
		pIsland->pPendingMerge = pAlloc->fpRealloc(
			pIsland->pPendingMerge,
			pIsland->mergeSize * sizeof(void *)
		);
	}
	return err;
}

static
Result destroyPendingMerge(const StucAlloc *pAlloc, MapIsland *pIsland) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", pIsland->mergeSize > 0);
	STUC_ASSERT("", pIsland->mergeCount > 0);
	if (pIsland->pPendingMerge) {
		pAlloc->fpFree(pIsland->pPendingMerge);
		pIsland->pPendingMerge = NULL;
		pIsland->mergeSize = 0;
		pIsland->mergeCount = 0;
	}
	return err;
}

static
Result addIslandToPendingMerge(
	const StucAlloc *pAlloc,
	IslandIdxPair *pCornerPair,
	IslandIdxPair *pCornerPairNext,
	I32 realiNext,
	IslandIdxPair *pIntersectCache,
	I32 cacheCount
) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", pCornerPair->pIsland->count > 0);
	STUC_ASSERT("", pCornerPairNext->pIsland->count > 0);
	MapIsland *pIsland = pCornerPair->pPending ?
		pCornerPair->pPending : pCornerPair->pIsland;
	if (pCornerPairNext->pPending == pIsland) {
		//already listed
		return err;
	}
	MapIsland *pIslandNext = pCornerPairNext->pIsland;
	if (!pIsland->pPendingMerge) {
		initPendingMerge(pAlloc, pIsland);
	}
	err = addToPendingMerge(pAlloc, pIsland, realiNext);
	STUC_RETURN_ERR_IFNOT(err, "");
	if (pIslandNext->pPendingMerge) {
		for (I32 i = 1; i < pIslandNext->mergeCount; ++i) {
			err = addToPendingMerge(pAlloc, pIsland, pIslandNext->pPendingMerge[i]);
			STUC_RETURN_ERR_IFNOT(err, "");
		}
		err = destroyPendingMerge(pAlloc, pIslandNext);
		STUC_RETURN_ERR_IFNOT(err, "");
	}
	pCornerPairNext->pIsland->invalid = true;
	//update references to invalid entry in list
	for (I32 i = 0; i < cacheCount; ++i) {
		if (pIntersectCache[i].pIsland == pIslandNext) {
			pIntersectCache[i].pPending = pIsland;
		}
	}
	return err;
}

static
Result mergeIslands(MapIsland *pIsland, IslandIdxPair *pIntersectCache) {
	Result err = STUC_SUCCESS;
	//17 is the max islands possible with a 32 vert map face
	I32 idxTable[18] = {-1};
	if (pIsland->mergeCount > 2) {
		stucInsertionSort(
			idxTable + 1,
			pIsland->mergeCount - 1,
			pIsland->pPendingMerge + 1
		);
	}
	for (I32 i = 1; i < pIsland->mergeCount; ++i) {
		I32 idxPending = pIsland->pPendingMerge[idxTable[i] + 1];
		MapIsland *pIslandPending = pIntersectCache[idxPending].pIsland;
		STUC_ASSERT("", pIslandPending->invalid);
		STUC_ASSERT("", pIslandPending->count > 0);
		for (I32 j = 0; j < pIslandPending->count; ++j) {
			pIsland->buf[pIsland->count + j] = pIslandPending->buf[j];
		}
		pIsland->count += pIslandPending->count;
	}
	return err;
}

static
void setSegment(IslandIdxPair *pCornerPair, Segments *pSegments, I32 inCorner) {
	MapIslandCorner *pCorner = pCornerPair->pIsland->buf + pCornerPair->corner;
	if (pCorner->inCorner == inCorner) {
		pCorner->segment = (I8)pSegments[inCorner].count;
	}
}

static
void setSegments(
	const StucAlloc *pAlloc,
	F32 *ptBuf,
	Segments *pSegments,
	IslandIdxPair *pIntersectCache,
	I32 inCorner,
	I32 reali,
	I32 realiNext
) {
	Segments *pSegEntry = pSegments + inCorner;
	IslandIdxPair *pCorner = pIntersectCache + reali;
	IslandIdxPair *pCornerNext = pIntersectCache + realiNext;
	if (ptBuf[reali] > .0f || ptBuf[realiNext] > .0f) {
		setSegment(pCorner, pSegments, inCorner);
		setSegment(pCornerNext, pSegments, inCorner);
		pSegEntry->pArr[pSegEntry->count] = ptBuf[reali];
		pSegEntry->count++;
		if (pSegEntry->count == pSegEntry->size) {
			I32 oldSize = pSegEntry->size;
			pSegEntry->size *= 2;
			pSegEntry->pArr =
				pAlloc->fpRealloc(pSegEntry->pArr, pSegEntry->size * sizeof(F32));
			pSegEntry->pIndices =
				pAlloc->fpRealloc(pSegEntry->pIndices, pSegEntry->size * sizeof(I32));
			memset(pSegEntry->pArr + pSegEntry->count, 0, sizeof(F32) * oldSize);
			memset(pSegEntry->pIndices + pSegEntry->count, 0, sizeof(I32) * oldSize);
		}
	}
}

static
void MapIslandCornerDecrementBaseCorners(MapIsland* pMapIsland, FaceRange* pInFace) {
	for (int i = 0; i < pMapIsland->count; ++i) {
		I8* pBaseCorner = &pMapIsland->pCorners[i].inCorner;
		*pBaseCorner = *pBaseCorner ? *pBaseCorner - (I8)1 : (I8)(pInFace->size - 1);
	}
}

static
void mergeDupCorners(
	MapIsland *pMapIsland,
	InCornerCache *pInCornerCache,
	bool mapFaceWind, bool inFaceWind
) {
	for (I32 i = 0; i < pMapIsland->count; ++i) {
		I32 iNext = (i + 1) % pMapIsland->count;
		I32 iPrev = i ? i - 1 : pMapIsland->count - 1;
		MapIslandCorner *pCorner = pMapIsland->pCorners + i;
		MapIslandCorner *pCornerNext = pMapIsland->pCorners + iNext;
		MapIslandCorner *pCornerPrev = pMapIsland->pCorners + iPrev;
		bool merge = false;
		if (!pCorner->isMapCorner && pCorner->onInVert && !pCornerNext->onInVert) {
			InCornerCache *pInCornerPrev =
				pInCornerCache + pInCornerCache[pCorner->inCorner].idxPrev;
			InsideStatus status =
				stucIsPointInHalfPlane(
					pCornerNext,
					pInCornerPrev,
					mapFaceWind, inFaceWind,
					false
				);
			merge = status == STUC_INSIDE_STATUS_ON_LINE || _(pCorner->corner V3EQL pCornerNext->corner);
		}
		else if (!pCornerNext->isMapCorner && pCornerNext->onInVert && !pCorner->onInVert) {
			InCornerCache *pInCornerNext =
				pInCornerCache + pInCornerCache[pCorner->inCorner].idxNext;
			 InsideStatus status =
				 stucIsPointInHalfPlane(
					 pCorner,
					 pInCornerNext,
					 mapFaceWind, inFaceWind,
					 false
				 );
			 merge = status == STUC_INSIDE_STATUS_ON_LINE || _(pCorner->corner V3EQL pCornerNext->corner);
		}
		if (!merge) {
			continue;
		}
		for (I32 j = i; j < pMapIsland->count - 1; ++j) {
			pMapIsland->pCorners[j] = pMapIsland->pCorners[j + 1];
		}
		pMapIsland->count--;
		i--;
	}
}

static
V3_F32 getCornerRealNormal(Mesh *pMesh, FaceRange *pFace, I32 corner) {
	I32 a = corner == 0 ? pFace->size - 1 : corner - 1;
	I32 c = (corner + 1) % pFace->size;
	I32 aIdx = pMesh->core.pCorners[pFace->start + a];
	I32 bIdx = pMesh->core.pCorners[pFace->start + corner];
	I32 cIdx = pMesh->core.pCorners[pFace->start + c];
	V3_F32 ba = _(pMesh->pPos[aIdx] V3SUB pMesh->pPos[bIdx]);
	V3_F32 bc = _(pMesh->pPos[cIdx] V3SUB pMesh->pPos[bIdx]);
	return v3F32Normalize(_(ba V3CROSS bc));
}

static
void lerpIntersect(
	MapIslandCorner *pCorner,
	Attrib *pDestAttrib,
	I32 destIdx,
	const Attrib *pMapAttrib,
	const FaceRange *pMapFace,
	CornerAncestors *pAncestors
) {
	STUC_ASSERT("", pMapAttrib->core.type != STUC_ATTRIB_STRING);
	if (pCorner->isMapCorner) {
		stucCopyAttrib(pDestAttrib, destIdx, pMapAttrib,
		           pMapFace->start + pCorner->mapCorner);
	}
	else {
		U8 dataA[32] = {0}; //enough for a V4 8 byte type
		U8 dataB[32] = {0}; //enough for a V4 8 byte type
		Attrib attribA = *pMapAttrib;
		Attrib attribB = *pMapAttrib;
		attribA.core.pData = dataA;
		attribB.core.pData = dataB;
		I32 idxA = 0;
		I32 idxB = 0;
		MapIslandCorner *pAncestor = pAncestors->pArr + pCorner->ancestor;
		MapIslandCorner *pAncestorNext = pAncestors->pArr + pCorner->ancestorNext;
		lerpIntersect(pAncestor, &attribA, 0, pMapAttrib, pMapFace, pAncestors);
		lerpIntersect(pAncestorNext, &attribB, 0, pMapAttrib, pMapFace, pAncestors);
		//TODO probably will need to invert this depending on map wind order
		stucLerpAttrib(
			pDestAttrib,
			destIdx,
			&attribA,
			idxA,
			&attribB,
			idxB,
			pCorner->tMapEdge
		);
	}
}

static
void blendMapAndInFaceAttribs(
	MappingJobState *pState,
	MapIslandCorner *pMapIsland,
	I32 dataIdx,
	FaceRange *pInFace,
	FaceRange *pMapFace
) {
	blendMapAndInAttribs(
		pState,
		STUC_DOMAIN_FACE,
		pMapIsland,
		0,
		dataIdx,
		pMapFace->idx,
		pInFace->idx,
		pInFace,
		pMapFace,
		NULL
	);
}

static
void blendMapAndInCornerAttribs(
	MappingJobState *pState,
	MapIslandCorner *pMapIsland,
	I32 MapIslandCornerIdx,
	I32 dataIdx,
	FaceRange *pInFace,
	FaceRange *pMapFace,
	CornerAncestors *pAncestors
) {
	blendMapAndInAttribs(
		pState,
		STUC_DOMAIN_CORNER,
		pMapIsland,
		MapIslandCornerIdx,
		dataIdx,
		pMapFace->start + pMapIsland[MapIslandCornerIdx].mapCorner,
		pInFace->start,
		pInFace,
		pMapFace,
		pAncestors
	);
}

static
void blendMapAndInVertAttribs(
	MappingJobState *pState,
	MapIslandCorner *pMapIsland,
	I32 MapIslandCornerIdx,
	I32 dataIdx,
	FaceRange *pInFace,
	FaceRange *pMapFace
) {
	blendMapAndInAttribs(
		pState,
		STUC_DOMAIN_VERT,
		pMapIsland,
		MapIslandCornerIdx,
		dataIdx,
		pMapIsland[MapIslandCornerIdx].mapCorner,
		0,
		pInFace,
		pMapFace,
		NULL
	);
}

static
void simpleCopyAttribs(
	AttribArray *pDestAttribs,
	const AttribArray *pMapAttribs,
	const AttribArray *pMeshAttribs,
	I32 destDataIdx,
	I32 srcDataIdx,
	I32 idxOrigin
) {
	for (I32 i = 0; i < pDestAttribs->count; ++i) {
		switch (pDestAttribs->pArr[i].origin) {
			case STUC_ATTRIB_ORIGIN_COMMON: {
				const StucAttrib *pSrcAttrib;
				if (idxOrigin) {
					pSrcAttrib = stucGetAttribInternConst(
						pDestAttribs->pArr[i].core.name,
						pMapAttribs,
						false, NULL, NULL
					);
				}
				else {
					pSrcAttrib = stucGetAttribInternConst(
						pDestAttribs->pArr[i].core.name,
						pMeshAttribs,
						false, NULL, NULL
					);
				}
				break;
			}
			case STUC_ATTRIB_ORIGIN_MAP: {
				if (!idxOrigin) {
					//idx is a meshIn idx
					continue;
				}
				const StucAttrib *pMapAttrib =
					stucGetAttribInternConst(
						pDestAttribs->pArr[i].core.name,
						pMapAttribs,
						false, NULL, NULL
					);
				memcpy(stucAttribAsVoid(&pDestAttribs->pArr[i].core, destDataIdx),
				       stucAttribAsVoidConst(&pMapAttrib->core, srcDataIdx),
				       stucGetAttribSizeIntern(pMapAttrib->core.type));
				break;
			}
			case STUC_ATTRIB_ORIGIN_MESH_IN: {
				if (idxOrigin) {
					//idx is a map idx
					continue;
				}
				const StucAttrib *pMeshAttrib =
					stucGetAttribInternConst(
						pDestAttribs->pArr[i].core.name,
						pMeshAttribs,
						false, NULL, NULL
					);
				memcpy(stucAttribAsVoid(&pDestAttribs->pArr[i].core, destDataIdx),
				       stucAttribAsVoidConst(&pMeshAttrib->core, srcDataIdx),
				       stucGetAttribSizeIntern(pMeshAttrib->core.type));
				break;
			}
		}
	}
}

static
void initEdgeTableEntry(
	MappingJobState *pState,
	LocalEdge *pEntry,
	I32 *pBufEdge,
	BufMesh *pBufMesh,
	I32 refEdge,
	I32 refFace,
	I32 isMapEdge
) {
	bool realloced = false;
	BufMeshIdx edge = stucBufMeshAddEdge(
		pState->pBasic->pCtx,
		pBufMesh,
		!isMapEdge,
		&realloced
	);
	*pBufEdge = edge.idx;
	pEntry->edge = edge.idx;
	simpleCopyAttribs(
		&pBufMesh->mesh.core.edgeAttribs,
		&pState->pBasic->pMap->pMesh->core.edgeAttribs,
		&pState->pBasic->pInMesh->core.edgeAttribs,
		edge.realIdx,
		refEdge,
		isMapEdge
	);
	pEntry->refEdge = refEdge;
	pEntry->refFace = refFace;
}

static
I32 getRefEdge(
	MappingJobState *pState,
	FaceRange *pMapFace,
	FaceRange *pInFace,
	MapIslandCorner *pMapIsland,
	I32 MapIslandCornerIdx
) {
	if (pMapIsland[MapIslandCornerIdx].isMapCorner) {
		I32 stucCorner = pMapIsland[MapIslandCornerIdx].mapCorner;
		return pState->pBasic->pMap->pMesh->core.pEdges[pMapFace->start + stucCorner];
	}
	else {
		I32 baseCorner = pMapIsland[MapIslandCornerIdx].inCorner;
		return pState->pBasic->pInMesh->core.pEdges[pInFace->start + baseCorner];
	}
}

static
void addEdge(
	MappingJobState *pState,
	I32 MapIslandCornerIdx,
	BufMesh *pBufMesh,
	MapIslandCorner *pMapIsland,
	I32 refFace,
	I32 *pBufEdge,
	FaceRange *pInFace,
	FaceRange *pMapFace
) {
	I32 refEdge = getRefEdge(pState, pMapFace, pInFace, pMapIsland, MapIslandCornerIdx);
	I32 isMapEdge = pMapIsland[MapIslandCornerIdx].isMapCorner;
	I32 key = isMapEdge ? refEdge : (refEdge + 1) * -1;
	I32 hash = stucFnvHash((U8 *)&key, 4, pState->localTables.edgeTableSize);
	LocalEdge *pEntry = pState->localTables.pEdgeTable + hash;
	I32 depth = 0;
	do {
		if (!pEntry->cornerCount) {
			initEdgeTableEntry(
				pState,
				pEntry,
				pBufEdge,
				pBufMesh,
				refEdge,
				refFace,
				isMapEdge
			);
			break;
		}
		I32 match = pEntry->refEdge == refEdge && pEntry->refFace == refFace;
		if (match) {
			*pBufEdge = pEntry->edge;
			break;
		}
		if (!pEntry->pNext) {
			stucLinAlloc(pState->localTables.pEdgeTableAlloc, &pEntry->pNext, 1);
			pEntry = pEntry->pNext;
			initEdgeTableEntry(
				pState,
				pEntry,
				pBufEdge,
				pBufMesh,
				refEdge,
				refFace,
				isMapEdge
			);
			break;
		}
		STUC_ASSERT("", pEntry->pNext && pEntry->pNext->pNext <= (LocalEdge *)1000000000000000);
		pEntry = pEntry->pNext;
		STUC_ASSERT("", depth >= 0 && depth < 1000);
		depth++;
	} while(1);
	pEntry->cornerCount++;
}

static
void addNewCornerAndOrVert(
	MappingJobState *pState,
	I32 MapIslandCornerIdx,
	I32 *pBufVert,
	BufMesh *pBufMesh,
	MapIslandCorner *pMapIsland,
	FaceRange *pInFace,
	FaceRange *pMapFace
) {
		bool realloced = false;
		BufMeshIdx vert = stucBufMeshAddVert(
			pState->pBasic->pCtx,
			pBufMesh,
			true,
			&realloced
		);
		*pBufVert = vert.idx;
		pBufMesh->mesh.pPos[vert.realIdx] = pMapIsland[MapIslandCornerIdx].corner;
		//TODO temporarily setting mesh data idx to 0, as it's only needed if interpolation is disabled
		blendMapAndInVertAttribs(
			pState,
			pMapIsland,
			MapIslandCornerIdx,
			vert.realIdx,
			pInFace,
			pMapFace
		);
}

static
void initMapVertTableEntry(
	MappingJobState *pState,
	I32 MapIslandCornerIdx,
	I32 *pBufVert,
	MapIslandCorner *pMapIsland,
	LocalVert *pEntry,
	FaceRange *pInFace,
	I32 stucVert,
	FaceRange *pMapFace,
	V2_I32 tile
) {
	bool realloced = false;
	BufMeshIdx vert = stucBufMeshAddVert(
		pState->pBasic->pCtx,
		&pState->bufMesh,
		false,
		&realloced
	);
	*pBufVert = vert.idx;
	pState->bufMesh.mesh.pPos[vert.realIdx] = pMapIsland[MapIslandCornerIdx].corner;
	pEntry->vert = vert.idx;
	pEntry->mapVert = stucVert;
	pEntry->inFace = pInFace->idx;
	pEntry->tile = tile;
	blendMapAndInVertAttribs(
		pState,
		pMapIsland,
		MapIslandCornerIdx,
		vert.realIdx,
		pInFace,
		pMapFace
	);
}

static
void addStucCornerAndOrVert(
	MappingJobState *pState,
	I32 MapIslandCornerIdx,
	I32 *pBufVert,
	MapIslandCorner *pMapIslandEntry,
	FaceRange *pInFace,
	FaceRange *pMapFace,
	V2_I32 tile
) {
	I32 stucCorner = pMapFace->start + pMapIslandEntry[MapIslandCornerIdx].mapCorner;
	U32 uStucVert = pState->pBasic->pMap->pMesh->core.pCorners[stucCorner];
	I32 hash =
		stucFnvHash((U8 *)&uStucVert, 4, pState->localTables.vertTableSize);
	LocalVert *pEntry = pState->localTables.pVertTable + hash;
	do {
		if (!pEntry->cornerSize) {
			initMapVertTableEntry(
				pState,
				MapIslandCornerIdx,
				pBufVert,
				pMapIslandEntry,
				pEntry,
				pInFace,
				uStucVert,
				pMapFace,
				tile
			);
			break;
		}
		I32 match =
			pEntry->mapVert == (I32)uStucVert &&
			pEntry->inFace == pInFace->idx &&
			pEntry->tile.d[0] == tile.d[0] &&
			pEntry->tile.d[1] == tile.d[1]; //TODO int vector ops don't current have macros
		if (match) {
			*pBufVert = pEntry->vert;
			break;
		}
		if (!pEntry->pNext) {
			stucLinAlloc(pState->localTables.pVertTableAlloc, &pEntry->pNext, 1);
			pEntry = pEntry->pNext;
			initMapVertTableEntry(
				pState,
				MapIslandCornerIdx,
				pBufVert,
				pMapIslandEntry,
				pEntry,
				pInFace,
				uStucVert,
				pMapFace,
				tile
			);
			break;
		}
		pEntry = pEntry->pNext;
	} while (1);
	pEntry->cornerSize++;
}

static
void initBorderTableEntry(
	MappingJobState *pState,
	I32 bufFace,
	BorderFace *pEntry,
	FaceRange *pMapFace,
	V2_I32 tile,
	MapIsland *pMapIsland,
	FaceRange *pInFace,
	bool inFaceWind, bool mapFaceWind,
	I32 memType,
	Segments *pSegments
) {
	pEntry->memType = memType;
	pEntry->bufFace = bufFace;
	pEntry->mapFace = pMapFace->idx;
	pEntry->tileX = tile.d[0];
	pEntry->tileY = tile.d[1];
	pEntry->job = pState->id;
	pEntry->inFace = pInFace->idx;
	pEntry->inOrient = inFaceWind;
	pEntry->mapOrient = mapFaceWind;

	BorderFaceBitArrs bitArrs = {0};
	stucGetBorderFaceBitArrs(pEntry, &bitArrs);

	STUC_ASSERT("", pMapIsland->count <= 64);
	for (I32 i = 0; i < pMapIsland->count; ++i) {
		MapIslandCorner *pCorner = pMapIsland->pCorners + i;
		if (pCorner->onLine != 0) {
			stucSetBitArr(bitArrs.pOnLine, i, true, 1);
		}
		if (pCorner->isMapCorner) {
			stucSetBitArr(bitArrs.pIsStuc, i, true, 1);
		}
		if (pCorner->mapCorner) {
			stucSetBorderFaceMapAttrib(pEntry, bitArrs.pStucCorner, i, pCorner->mapCorner);
		}
		if (pCorner->onInVert) {
			stucSetBitArr(bitArrs.pOnInVert, i, true, 1);
		}
		if (!pCorner->isMapCorner && pCorner->segment) {
			I32 idx = pCorner->segment - 1;
			I32 j = 1;
			//if map face orientation is inverted, then the position
			//of each incorner in the segments array is offset by 1.
			//This is because pCorner->inCorner is decremented at the end of
			//clipping to account for the inverted wind order
			I32 segIdx = mapFaceWind ?
				pCorner->inCorner : (pCorner->inCorner + 1) % pInFace->size;
			for (j; j < pSegments[segIdx].count; ++j) {
				if (pSegments[segIdx].pIndices[j] == idx) {
					break;
				}
			}
			j--;
			if (j) {
				stucSetBorderFaceMapAttrib(pEntry, bitArrs.pSegment, i, j);
			}
		}
		// Only add basecorner for stuc if online, otherwise value will
		// will not fit within 2 bits
		if ((!pCorner->isMapCorner || pCorner->onLine) && pCorner->inCorner) {
			stucSetBitArr(bitArrs.pBaseCorner, i, pCorner->inCorner, 2);
		}
	}
}

static
void walkBorderTable(
	MappingJobState *pState,
	I32 bufFace,
	MapIsland *pMapIsland,
	FaceRange *pMapFace,
	V2_I32 tile,
	FaceRange *pInFace,
	bool inFaceWind, bool mapFaceWind,
	Segments *pSegments,
	BorderBucket *pBucket,
	BorderFace *pEntry,
	I32 memType
) {
	do {
		if (pEntry->mapFace == pMapFace->idx) {
			if (pBucket->pTail) {
				pEntry = pBucket->pTail;
			}
			stucAllocBorderFace(memType, &pState->borderTableAlloc, &pEntry->pNext);
			pEntry = pEntry->pNext;
			pBucket->pTail = pEntry;
			initBorderTableEntry(
				pState,
				bufFace,
				pEntry,
				pMapFace,
				tile,
				pMapIsland,
				pInFace,
				inFaceWind, mapFaceWind,
				memType,
				pSegments
			);
			break;
		}
		if (!pBucket->pNext) {
			pBucket = pBucket->pNext = pState->pBasic->pCtx->alloc.fpCalloc(1, sizeof(BorderBucket));
			stucAllocBorderFace(memType, &pState->borderTableAlloc, &pBucket->pEntry);
			pEntry = pBucket->pEntry;
			initBorderTableEntry(
				pState,
				bufFace,
				pEntry,
				pMapFace,
				tile,
				pMapIsland,
				pInFace,
				inFaceWind, mapFaceWind,
				memType,
				pSegments
			);
			break;
		}
		pBucket = pBucket->pNext;
		pEntry = pBucket->pEntry;
	} while (1);
}

static
void addFaceToBorderTable(
	MappingJobState *pState,
	I32 bufFace,
	MapIsland *pMapIsland,
	FaceRange *pMapFace,
	V2_I32 tile,
	FaceRange *pInFace,
	bool inFaceWind, bool mapFaceWind,
	Segments *pSegments
) {
	I32 memType = stucGetBorderFaceMemType(pMapFace->size, pMapIsland->count);
	I32 hash = stucFnvHash((U8 *)&pMapFace->idx, 4, pState->borderTable.size);
	BorderBucket *pBucket = pState->borderTable.pTable + hash;
	BorderFace *pEntry = pBucket->pEntry;
	if (!pEntry) {
		stucAllocBorderFace(memType, &pState->borderTableAlloc, &pBucket->pEntry);
		pEntry = pBucket->pEntry;
		initBorderTableEntry(
			pState,
			bufFace,
			pEntry,
			pMapFace,
			tile,
			pMapIsland,
			pInFace,
			inFaceWind, mapFaceWind,
			memType,
			pSegments
		);
	}
	else {
		walkBorderTable(
			pState,
			bufFace,
			pMapIsland,
			pMapFace,
			tile,
			pInFace,
			inFaceWind, mapFaceWind,
			pSegments,
			pBucket,
			pEntry,
			memType
		);
	}
}

static
void addClippedFaceToBufMesh(
	MappingJobState *pState,
	MapIsland *pMapIsland,
	FaceRange *pMapFace,
	V2_I32 tile,
	FaceRange *pInFace,
	bool inFaceWind, bool mapFaceWind,
	Segments *pSegments,
	CornerAncestors *pAncestors
) {
	bool realloced = false;
	I32 bufCornerStart = 0;
	I32 bufFace = 0;
	I32 bufEdge = 0;
	I32 bufVert = 0;
	BufMesh *pBufMesh = &pState->bufMesh;
	bool isBorderFace = pMapIsland->edgeFace || pMapIsland->onLine;
	bool invert = !inFaceWind && !isBorderFace;
	I32 size = pMapIsland->count;
	for (I32 i = invert ? size - 1 : 0;
		invert ? i >= 0 : i < size;
		invert ? --i : ++i) {

		I32 refFace;
		I32 isStuc = pMapIsland->pCorners[i].isMapCorner;
		if (!isStuc || pMapIsland->pCorners[i].onLine) {
			//TODO these only add verts, not corners. Outdated name?
			addNewCornerAndOrVert(
				pState,
				i,
				&bufVert,
				&pState->bufMesh,
				pMapIsland->pCorners,
				pInFace,
				pMapFace
			);
			refFace = pMapFace->idx;
		}
		else {
			addStucCornerAndOrVert(
				pState,
				i,
				&bufVert,
				pMapIsland->pCorners,
				pInFace,
				pMapFace,
				tile
			);
			refFace = pInFace->idx;
		}
		BufMeshIdx corner = stucBufMeshAddCorner(
			pState->pBasic->pCtx,
			pBufMesh,
			isBorderFace,
			&realloced
		);
		if (invert ? i == size - 1 : !i) {
			bufCornerStart = corner.idx;
		}
		pBufMesh->pW[corner.realIdx] = pMapIsland->pCorners[i].uvw.d[2];
		pBufMesh->pInNormal[corner.realIdx] = pMapIsland->pCorners[i].projNormal;
		pBufMesh->pInTangent[corner.realIdx] = pMapIsland->pCorners[i].inTangent;
		pBufMesh->pAlpha[corner.realIdx] = pMapIsland->pCorners[i].tInEdge;
		pBufMesh->pInTSign[corner.realIdx] = pMapIsland->pCorners[i].inTSign;
		pBufMesh->mesh.core.pCorners[corner.realIdx] = bufVert;
		pBufMesh->mesh.pNormals[corner.realIdx] = pMapIsland->pCorners[i].normal;
		pBufMesh->mesh.pUvs[corner.realIdx] = pMapIsland->pCorners[i].uv;
		//TODO add an intermediate function to shorten the arg lists in blendattrib functions
		blendMapAndInCornerAttribs(
			pState,
			pMapIsland->pCorners,
			i,
			corner.realIdx,
			pInFace,
			pMapFace,
			pAncestors
		);
#ifndef STUC_DISABLE_EDGES_IN_BUF
		addEdge(
			pState,
			i,
			&pState->bufMesh,
			pMapIsland->pCorners,
			refFace,
			&bufEdge,
			pInFace,
			pMapFace
		);
		pBufMesh->mesh.core.pEdges[corner.realIdx] = bufEdge;
#endif
	}
	BufMeshIdx face = stucBufMeshAddFace(
		pState->pBasic->pCtx,
		pBufMesh,
		isBorderFace,
		&realloced
	);
	if (pState->pBasic->pInFaceTable) {
		//this is only true when mapping usg squares
		pBufMesh->pInMapFacePair[face.realIdx].d[0] = pInFace->idx;
		pBufMesh->pInMapFacePair[face.realIdx].d[1] = pMapFace->idx;
	}
	bufFace = face.idx;
	pBufMesh->mesh.core.pFaces[face.realIdx] = bufCornerStart;
	blendMapAndInFaceAttribs(
		pState,
		pMapIsland->pCorners,
		face.realIdx,
		pInFace,
		pMapFace
	);
	if (isBorderFace) {
		addFaceToBorderTable(
			pState,
			bufFace,
			pMapIsland,
			pMapFace,
			tile,
			pInFace,
			inFaceWind, mapFaceWind,
			pSegments
		);
	}
}

static
bool isOnLine(MapIsland *pMapIsland) {
	for (I32 i = 0; i < pMapIsland->count; ++i) {
		if (pMapIsland->pCorners[i].onLine) {
			return true;
		}
	}
	return false;
}

static
bool snapPointToEdgeIntern(
	V2_F32 a, V2_F32 b,
	V2_F32 ab, F32 abLen,
	V2_F32 *pPoint
) {
	if (v2F32AproxEqualThres(*pPoint, a, STUC_ON_LINE_SNAP_THRESHOLD)) {
		*pPoint = a;
		return true;
	}
	else if (
		v2F32AproxEqualThres(*pPoint, b, STUC_ON_LINE_SNAP_THRESHOLD)
		) {
		*pPoint = b;
		return true;
	}
	//TODO should this threshold be lower than the one for verts?
	//incase a position is too far from a vert to be snapped, but close
	// enough to the edge to be snapped to that?
	V2_F32 dirUnit = _(_(*pPoint V2SUB a) V2DIVS abLen);
	F32 t = _(dirUnit V2DOT _(ab V2DIVS abLen));
	if (t >= .0f && t <= 1.0f) {
		V2_F32 projPt = _(a V2ADD _(ab V2MULS t));
		F32 perpDist = v2F32Len(_(projPt V2SUB *pPoint));
		if (perpDist < STUC_ON_LINE_SNAP_THRESHOLD) {
			*pPoint = projPt;
			return true;
		}
	}
	return false;
}

static
bool snapPointToEdge(V2_F32 a, V2_F32 b, V2_F32 *pPoint) {
	V2_F32 ab = _(b V2SUB a);
	F32 len = v2F32Len(ab);
	return snapPointToEdgeIntern(a, b, ab, len, pPoint);
}

static
void snapInVertsToMapEdges(
	const FaceRange *pInFace, InCornerCache *pInCorners,
	const MapIsland *pMapCorners
) {
	for (I32 i = 0; i < pMapCorners->count; ++i) {
		I32 iNext = (i + 1) % pMapCorners->count;
		for (I32 j = 0; j < pInFace->size; ++j) {
			snapPointToEdge(
				*(V2_F32 *)&pMapCorners->buf[i].corner,
				*(V2_F32 *)&pMapCorners->buf[iNext].corner,
				&pInCorners[j].uv
			);
		}
	}
	for (I32 i = 0; i < pInFace->size; ++i) {
		pInCorners[i].uvNext = pInCorners[pInCorners[i].idxNext].uv;
		pInCorners[i].dir = _(pInCorners[i].uvNext V2SUB pInCorners[i].uv);
		pInCorners[i].len = v2F32Len(pInCorners[i].dir);
		pInCorners[i].dirUnit = _(pInCorners[i].dir V2DIVS pInCorners[i].len);
		pInCorners[i].halfPlane = v2F32LineNormal(pInCorners[i].dir);
	}
}

static
void sortSegments(Segments *pSegments, FaceRange *pInFace) {
	for (I32 i = 0; i < pInFace->size; ++i) {
		if (pSegments[i].count > 2) {
			stucFInsertionSort(pSegments[i].pIndices + 1, pSegments[i].count - 1,
				pSegments[i].pArr + 1);
		}
	}
}

static
void addOrDiscardClippedFaces(
	MappingJobState *pState,
	V2_F32 fTileMin,
	MapIsland *pMapIsland,
	V2_I32 tile,
	Segments *pSegments,
	CornerAncestors *pAncestors,
	FaceRange *pInFace,
	FaceRange *pMapFace,
	const BaseTriVerts *pInTri,
	bool inFaceWind, bool mapFaceWind
) {
	STUC_ASSERT("", mapFaceWind % 2 == mapFaceWind && inFaceWind % 2 == inFaceWind);
	MapIsland *pMapIslandPtr = pMapIsland;
	do{
		if (!pMapIslandPtr->invalid) {
			if (pMapIslandPtr->count >= 3) {
				//TODO move this after addClippedFaceToBufMesh,
				//     that way, you can skip merged stuc verts,
				//     and you can also remove the use of MapIslandCorner,
				//     and turn the func into a generic (generic for BufMesh)
				//     one that can be used on deffered corners in MergeBoundsFaces.c
				// Followup - Very low priority, the perf increase is minimal
				// (tested by commenting out the func)
				transformClippedFaceFromUvToXyz(
					pMapIslandPtr,
					pMapFace,
					pInFace,
					pInTri,
					pState,
					fTileMin,
					pState->pBasic->wScale
				);
				addClippedFaceToBufMesh(
					pState,
					pMapIslandPtr,
					pMapFace,
					tile,
					pInFace,
					inFaceWind, mapFaceWind,
					pSegments,
					pAncestors
				);
			}
		}
		pMapIslandPtr = pMapIslandPtr->pNext;
	} while(pMapIslandPtr);
}

static
void addInsideCornerToBuf(
	MapToMeshBasic *pBasic,
	MapIsland *pNewMapIsland,
	MapIsland *pMapIsland,
	InsideCache *pInside,
	I32 i,
	I32 iNext,
	I32 iPrev,
	InCornerCache *pInCorner,
	IslandIdxPair *pIntersectCache,
	F32 *ptBuf,
	I32 *pCount,
	bool mapFaceWind, bool inFaceWind
) {
	MapIslandCorner *pNewEntry = pNewMapIsland->pCorners + pNewMapIsland->count;
	pNewMapIsland->pCorners[pNewMapIsland->count] = pMapIsland->pCorners[i];
	//using += so that base corners can be determined. ie, if an stuc
	//vert has a dot of 0 twice, then it is sitting on a base vert,
	//but if once, then it's sitting on an edge.
	if (pInside[i].status == STUC_INSIDE_STATUS_ON_LINE) {
		//is on line
		if ((pInside[iPrev].status != STUC_INSIDE_STATUS_OUTSIDE) ^ (pInside[iNext].status != STUC_INSIDE_STATUS_OUTSIDE)) {
			//add to intersection buf
			pIntersectCache[*pCount].pIsland = pNewMapIsland;
			pIntersectCache[*pCount].corner = pNewMapIsland->count;
			MapIslandCorner *pCorner = pMapIsland->pCorners + i;
			MapIslandCorner *pCornerNext = pMapIsland->pCorners + iNext;
			stucCalcIntersection(
				pCorner->corner,
				pCornerNext->corner,
				pInCorner->uv,
				pInCorner->dir,
				NULL,
				NULL,
				&pNewEntry->tInEdge
			);
			pNewEntry->tInEdge *= -1.0f;
			++*pCount;
		}
		if (pMapIsland->pCorners[i].onLine || !pMapIsland->pCorners[i].isMapCorner) {
			handleOnInVert(
				pNewEntry,
				pMapIsland, i,
				pInCorner,
				mapFaceWind, inFaceWind
			);
		}
		else if (pMapIsland->pCorners[i].isMapCorner) {
			//resides on base edge
			pNewEntry->inCorner = pInCorner->idx;
		}
		pNewMapIsland->onLine = true;
		pNewEntry->onLine = true;
	}
	else if (pInside[i].markAsOnLine) {
		handleOnInVert(
			pNewEntry,
			pMapIsland, i,
			pInCorner,
			mapFaceWind, inFaceWind
		);
		pNewEntry->onLine = true;
	}
	pNewMapIsland->count++;
}

static
Result setIsland(
	const BufMeshInitJobArgs *pArgs,
	MapIsland **ppIsland,
	MapIsland *pRoot,
	bool *pIn,
	I32 inCorner,
	bool mapFaceWind
) {
	Result err = STUC_SUCCESS;
	if (!*pIn) {
		if (!*ppIsland) {
			*ppIsland = pRoot;
		}
		else {
			while ((*ppIsland)->pNext) {
				*ppIsland = (*ppIsland)->pNext;
			}
			err = stucLinAlloc(pArgs->pMapIslandAlloc, &(*ppIsland)->pNext, 1);
			STUC_RETURN_ERR_IFNOT(err, "");
			*ppIsland = (*ppIsland)->pNext;
			(*ppIsland)->lastInCorner = inCorner;
			(*ppIsland)->lastInCorner += mapFaceWind ? 1 : -1;
		}
		*pIn = true;
	}
	return err;
}
#endif

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

typedef struct MapCornerCacheEntry {
	V3_F32 pos;
	I32 InFace;
} MapCornerCacheEntry;

typedef struct MapCornerCache {
	MapCornerCacheEntry *pArr;
} MapCornerCache;

/*
typedef struct {
	MapIsland *pIsland;
	MapIsland *pPrev;
	IntersectListArr intersectLists;
	void *pAlloc;
} Islands;
*/

typedef enum IntersectType {
	STUC_INTERSECT_TYPE_NONE,
	STUC_INTERSECT_TYPE_INTERSECT,
	STUC_INTERSECT_TYPE_ON_EDGE,
	STUC_INTERSECT_TYPE_ON_VERT
} IntersectType;

typedef struct IntersectResult {
	V3_F32 pos;
	I32 mapCorner;
	F32 tMapEdge;
	F32 tInEdge;
	I8 travelDir;
	I8 type;
} IntersectResult;

static
const IntersectCorner *addIntersectCorner(
	const MapToMeshBasic *pBasic,
	IntersectArr *pArr,
	I32 borderIdx,
	I32 mapCorner,
	I32 inEdge,
	FaceCorner inCorner,
	const IntersectResult *pResult
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	STUC_ASSERT("", pResult->type != STUC_INTERSECT_TYPE_NONE);
	STUC_ASSERT("", pArr->count <= pArr->size);
	if (!pArr->size) {
		pArr->size = 2;
		pArr->pArr = pAlloc->fpMalloc(pArr->size * sizeof(IntersectCorner));
	}
	else if (pArr->count == pArr->size) {
		pArr->size *= 2;
		pArr->pArr = pAlloc->fpRealloc(pArr->pArr, pArr->size * sizeof(IntersectCorner));
	}
	IntersectCorner *pNewCorner = pArr->pArr + pArr->count;
	pArr->count++;
	*pNewCorner = (IntersectCorner) {
		.core.type = STUC_CORNER_INTERSECT,
		.type = pResult->type,
		.borderIdx = borderIdx,
		.mapCorner = mapCorner,
		.borderEdge = inEdge,
		.inCorner = inCorner,
		.tInEdge = pResult->tInEdge,
		.travelDir = pResult->travelDir,
	};
	if (pResult->type == STUC_INTERSECT_TYPE_INTERSECT) {
		pNewCorner->pos = pResult->pos;
		pNewCorner->tMapEdge = pResult->tMapEdge;
	}
	return pNewCorner;
}

/*
static
void initIntersectList(const MapToMeshBasic *pBasic, IntersectList *pList) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	stucLinAllocInit(pAlloc, &pList->pListAlloc, sizeof(IntersectCorner), 2);
	stucLinAllocInit(pAlloc, &pList->pNodeAlloc, sizeof(IntersectTreeNode), 2);
}

static
void destroyIntersectList(IntersectList *pList) {
	stucLinAllocDestroy(pList->pListAlloc);
	stucLinAllocDestroy(pList->pNodeAlloc);
	*pList = (IntersectList) {0};
}
*/

typedef struct TrisCacheEntry {
	struct TrisCacheEntry *pNext;
	FaceTriangulated tris;
	I32 face;
} TrisCacheEntry;

typedef struct TrisCacheBucket {
	TrisCacheEntry *pList;
} TrisCacheBucket;

typedef struct TrisCache {
	TrisCacheBucket *pTable;
	LinAlloc entryAlloc;
	LinAlloc triAlloc;
	I32 size;
} TrisCache;

/*
static
void initTrisCache(const StucAlloc *pAlloc, TrisCache *pCache, I32 size) {
	pCache->size = size;
	pCache->pTable = pAlloc->fpCalloc(size, sizeof(TrisCacheBucket));
	stucLinAllocInit(pAlloc, &pCache->entryAlloc, sizeof(TrisCacheEntry), 2, true);
	stucLinAllocInit(pAlloc, &pCache->triAlloc, 1, 8, true);
}

static
void destroyTrisCache(const StucAlloc *pAlloc, TrisCache *pCache) {
	pAlloc->fpFree(pCache->pTable);
	stucLinAllocDestroy(&pCache->entryAlloc);
	stucLinAllocDestroy(&pCache->triAlloc);
	*pCache = (TrisCache) {0};
}
*/


/*
static
TrisCacheEntry *initTrisCacheEntry(
	const StucAlloc *pAlloc,
	TrisCache *pCache,
	const Mesh *pMesh,
	const FaceRange *pFace
) {
	TrisCacheEntry *pEntry = NULL;
	stucLinAlloc(&pCache->entryAlloc, &pEntry, 1);
	pEntry->face = pFace->idx;
	I32 triCount = pFace->size - 2;
	stucLinAlloc(&pCache->triAlloc, &pEntry->tris.pTris, triCount * 3);
	stucTriangulateFaceFromVerts(pAlloc, pFace, pMesh, &pEntry->tris);
	return pEntry;
}

static
const FaceTriangulated *getFaceTris(
	const StucAlloc *pAlloc,
	TrisCache *pCache,
	const Mesh *pMesh,
	const FaceRange *pFace
) {
	STUC_ASSERT(
		"cache wasn't initialized?",
		pCache->pTable && pCache->size >= 0
	);
	U32 key = pFace->idx;
	U32 hash = stucFnvHash((U8 *)&key, sizeof(key), pCache->size);
	TrisCacheBucket *pBucket = pCache->pTable + hash;
	if (!pBucket->pList) {
		pBucket->pList = initTrisCacheEntry(pAlloc, pCache, pMesh, pFace);
		return &pBucket->pList->tris;
	}
	TrisCacheEntry *pEntry = pBucket->pList;
	do {
		if (pEntry->face == pFace->idx) {
			return &pEntry->tris;
		}
		if (!pEntry->pNext) {
			pEntry->pNext = initTrisCacheEntry(pAlloc, pCache, pMesh, pFace);
			return &pEntry->pNext->tris;
		}
		pEntry = pEntry->pNext;
	} while(true);
}
*/

typedef struct InFaceCorner {
	InFaceCacheEntry *pFace;
	I32 corner;
} InFaceCorner;

typedef struct InFaceCacheInitInfo {
	bool wind;
} InFaceCacheInitInfo;

/*
static
InFaceCacheBucket *inFaceCacheEntryInit(InFaceCache *pCache, I32 face) {
	U32 key = face;
	U32 hash = stucFnvHash((U8 *)&key, sizeof(key), pCache->size);
	return pCache->pTable + hash;
}
*/

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
		stucGetFaceBounds(&bounds, pState->pBasic->pInMesh->pUvs, pEntry->face);
		pEntry->fMin = bounds.fMin;
		pEntry->fMax = bounds.fMax;
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

/*
static
bool inFaceCacheAddPredicate(
	const void *pUserData,
	const void *pKeyData,
	const void *pInitInfoVoid
) {
	EncasingInFaceArr *pInFaces =
		&((InFaceCacheState *)pUserData)->pInPiece->pList->inFaces;
	for (I32 i = 0; i < pInFaces->count; ++i) {
		if (*(I32 *)pKeyData == pInFaces->pArr[i].idx) {
			break;
		}
	}
}
*/

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

/*
static
bool isInFaceInCache(InFaceCache *pCache, I32 face) {
	InFaceCacheBucket *pBucket = inFaceCacheEntryInit(pCache, face);
	if (!pBucket->pList) {
		return false;
	}
	InFaceCacheEntryIntern *pEntry = pBucket->pList;
	do {
		if (pEntry->core.face.idx == face) {
			return true;
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
	return false;
}
*/


static
InFaceCorner getAdjFaceInPiece(
	const MapToMeshBasic *pBasic,
	HTable *pInFaceCache,
	InFaceCorner corner
) {
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

/*
InFaceCorner getExteriorInPieceCorner(
	MapToMeshBasic *pBasic,
	EncasingInFaceArr *pInFaces,
	InFaceCache *pInFaceCache
) {
	for (I32 i = 0; i < pInFaces->count; ++i) {
		I32 faceIdx = pInFaces->pArr[i].idx;
		FaceRange face = stucGetFaceRange(&pBasic->pInMesh->core, faceIdx);
		for (I32 j = 0; j < face.size; ++j) {
			InFaceCorner adjCorner = getAdjFaceInPiece(
				pBasic,
				pInFaceCache,
				(FaceCorner) {.face = faceIdx, .corner = j}
			);
			if (!adjCorner.pFace) {
				return adjCorner;
			}
		}
	}
	return (InFaceCorner) {.corner = -1};
}
*/

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

static
void testAgainstInCorner(
	const MapToMeshBasic *pBasic,
	const FaceRange *pMapFace,
	I32 mapCorner,
	InFaceCorner inCorner,
	IntersectResult *pResult
) {
	HalfPlane *pInCornerCacheArr = getInCornerCache(pBasic, inCorner.pFace);
	HalfPlane *pInCornerCache = pInCornerCacheArr + inCorner.corner;
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	I32 mapCornerNext = stucGetCornerNext(mapCorner, pMapFace);
	V3_F32 mapVert =
		pMapMesh->pPos[pMapMesh->core.pCorners[pMapFace->start + mapCorner]];
	V3_F32 mapVertNext =
		pMapMesh->pPos[pMapMesh->core.pCorners[pMapFace->start + mapCornerNext]];
	InsideStatus status = stucIsPointInHalfPlane(
		*(V2_F32 *)&mapVert,
		pInCornerCache->uv,
		pInCornerCache->halfPlane,
		inCorner.pFace->wind
	);
	InsideStatus nextStatus = stucIsPointInHalfPlane(
		*(V2_F32 *)&mapVertNext,
		pInCornerCache->uv,
		pInCornerCache->halfPlane,
		inCorner.pFace->wind
	);
	if (status == STUC_INSIDE_STATUS_ON_LINE) {
		I32 mapCornerPrev = stucGetCornerPrev(mapCorner, pMapFace);
		V3_F32 mapVertPrev =
			pMapMesh->pPos[pMapMesh->core.pCorners[pMapFace->start + mapCornerPrev]];
		InsideStatus statusPrev = stucIsPointInHalfPlane(
			*(V2_F32 *)&mapVertPrev,
			pInCornerCache->uv,
			pInCornerCache->halfPlane,
			inCorner.pFace->wind
		);
		if ((statusPrev == STUC_INSIDE_STATUS_OUTSIDE) ==
			(nextStatus == STUC_INSIDE_STATUS_OUTSIDE)
		) {
			pResult->type = STUC_INTERSECT_TYPE_NONE;
			return;
		}
		F32 tInEdge = stucGetT(
			*(V2_F32 *)&mapVertPrev,
			pInCornerCache->uv,
			pInCornerCache->dirUnit,
			pInCornerCache->len
		);
		if (tInEdge < .0f || tInEdge > 1.0f) {
			pResult->type = STUC_INTERSECT_TYPE_NONE;
			return;
		}
		bool onInVert = tInEdge == .0f || tInEdge == 1.0f;
		*pResult = (IntersectResult){
			.type = onInVert ? STUC_INTERSECT_TYPE_ON_VERT : STUC_INTERSECT_TYPE_ON_EDGE,
			.mapCorner = mapCorner,
			.tInEdge = tInEdge,
			.travelDir = nextStatus == STUC_INSIDE_STATUS_OUTSIDE ? OUTBOUND : INBOUND
		};
		return;
	}
	if ((status == STUC_INSIDE_STATUS_OUTSIDE) ==
		(nextStatus == STUC_INSIDE_STATUS_OUTSIDE)
	) {
		pResult->type = STUC_INTERSECT_TYPE_NONE;
		return;
	}
	V3_F32 newPoint = {0};
	F32 tMapEdge = .0f;
	F32 tInEdge = .0f;
	stucCalcIntersection(
		mapVert, mapVertNext,
		pInCornerCache->uv, pInCornerCache->dir,
		&newPoint,
		&tMapEdge, &tInEdge
	);
	if (tInEdge <= .0f || tInEdge >= 1.0f) {
		pResult->type = STUC_INTERSECT_TYPE_NONE;
		return;
	}
	*pResult = (IntersectResult) {
		.type = STUC_INTERSECT_TYPE_INTERSECT,
		.pos = newPoint,
		.tMapEdge = tMapEdge,
		.tInEdge = tInEdge,
		.travelDir = nextStatus == STUC_INSIDE_STATUS_OUTSIDE ? OUTBOUND : INBOUND
	};
}

/*
static
void beginInFaceIsland(
	const MapToMeshBasic *pBasic,
	InFaceIsland **ppInIsland,
	MapIsland *pMapIsland
) {
	pMapIsland->pInIslandNext = *ppInIsland =
		pBasic->pCtx->alloc.fpCalloc(1, sizeof(InFaceIsland));
}

static
void addToInFaceIsland(
	const MapToMeshBasic *pBasic,
	InFaceIsland *pInIsland,
	I32 inFace,
	I32 inCorner
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	STUC_ASSERT("", pInIsland->count <= pInIsland->size);
	if (!pInIsland->pCorners) {
		pInIsland->size = 2;
		pInIsland->pCorners = pAlloc->fpMalloc(pInIsland->size * sizeof(FaceCorner));
	}
	else if (pInIsland->count == pInIsland->size) {
		pInIsland->size *= 2;
		pInIsland->pCorners =
			pAlloc->fpRealloc(pInIsland->pCorners, pInIsland->size * sizeof(FaceCorner));
	}
	pInIsland->pCorners[pInIsland->count].face = inFace;
	pInIsland->pCorners[pInIsland->count].corner = inCorner;
	pInIsland->count++;
}

static
void endInFaceIsland(InFaceIsland **ppInIsland, MapIsland *pMapIsland) {
	pMapIsland->pInIslandPrev = *ppInIsland;
	*ppInIsland = NULL;
}
*/
/*
static
void endIsland(
	Islands *pIslands,
	I32 borderIdx,
	I32 borderEdge,
	InFaceCorner inCorner,
	const IntersectResult *pResult
) {
	pIslands->pIsland->pOutbound = addIntersectCorner(
		pIslands->intersectLists.pArr + borderIdx,
		borderEdge,
		(FaceCorner) {.face = inCorner.pFace->face.idx, .corner = inCorner.corner},
		pResult,
		pIslands->pIsland
	);
	pIslands->pPrev = pIslands->pIsland;
	pIslands->pIsland = NULL;
}

static
void beginIsland(
	Islands *pIslands,
	I32 borderIdx,
	I32 borderEdge,
	InFaceCorner inCorner,
	const IntersectResult *pResult
) {
	STUC_ASSERT("", !pIslands->pIsland && pIslands->pPrev);
	stucLinAlloc(pIslands->pAlloc, &pIslands->pIsland, 1);
	pIslands->pPrev = NULL;
}
*/

static
void testAgainstInEdge(
	const MapToMeshBasic *pBasic,
	IntersectArr *pIntersectArr,
	I32 borderIdx,
	const FaceRange *pMapFace,
	I32 mapCorner,
	InFaceCorner inCorner,
	I32 borderEdge
) {
	I32 inEdge =
		pBasic->pInMesh->core.pEdges[inCorner.pFace->face.start + inCorner.corner];
	if (couldInEdgeIntersectMapFace(pBasic->pInMesh, inEdge)) {
		IntersectResult result = {0};
		testAgainstInCorner(pBasic, pMapFace, mapCorner, inCorner, &result);
		if (result.type != STUC_INSIDE_STATUS_NONE) {
			addIntersectCorner(
				pBasic,
				pIntersectArr,
				borderIdx,
				mapCorner,
				borderEdge,
				(FaceCorner) {.face = inCorner.pFace->face.idx, .corner = inCorner.corner},
				&result
			);
		}
	}
}

static
void intersectWithPiecePerim(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const FaceRange *pMapFace,
	I32 mapCorner,
	HTable *pInFaceCache,
	IntersectArr *pIntersectArr
) {
	for (I32 i = 0; i < pInPiece->borderArr.count; ++i) {
		FaceCorner startCorner = pInPiece->borderArr.pArr[i].start;
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
		do {
			if (borderEdge != 0 &&
				inCorner.pFace->face.idx == startCorner.face &&
				inCorner.corner == startCorner.corner
				) {
				break; //full loop
			}
			InFaceCorner adjInCorner = getAdjFaceInPiece(
				pBasic,
				pInFaceCache,
				inCorner
			);
			testAgainstInEdge(
				pBasic,
				pIntersectArr,
				i,
				pMapFace,
				mapCorner,
				inCorner,
				borderEdge
			);
			if (adjInCorner.pFace) {
				inCorner = adjInCorner; //edge is internal, move to adj face
			}
			else {
				//edge is on boundary, continue on this face
				inCorner.corner = stucGetCornerNext(inCorner.corner, &inCorner.pFace->face);
				borderEdge++;
			}
		} while (true);
	}
}

/*
static
void addCornerToIsland(const MapToMeshBasic *pBasic, MapIsland *pIsland, I32 corner) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	STUC_ASSERT("", pIsland->count >= 0 && pIsland->count <= pIsland->size);
	if (!pIsland->pCorners) {
		pIsland->size = 2;
		pIsland->pCorners = pAlloc->fpMalloc(pIsland->size * sizeof(I32));
	}
	else if (pIsland->count == pIsland->size) {
		pIsland->size *= 2;
		pIsland->pCorners =
			pAlloc->fpRealloc(pIsland->pCorners, pIsland->size * sizeof(I32));
	}
	pIsland->pCorners[pIsland->count] = corner;
	pIsland->count++;
}
*/

/*
static
void initIslands(
	const MapToMeshBasic *pBasic,
	Islands *pIslands,
	const InPiece *pInPiece
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	IntersectListArr *pIntersectLists = &pIslands->intersectLists;
	pIntersectLists->count = pInPiece->borderArr.count;
	pIntersectLists->pArr = pAlloc->fpCalloc(pIntersectLists->count, sizeof(IntersectList));
	for (I32 i = 0; i < pIntersectLists->count; ++i) {
		initIntersectList(pBasic, pIntersectLists->pArr + i);
	}
	stucLinAllocInit(&pBasic->pCtx->alloc, &pIslands->pAlloc, sizeof(MapIsland), 2);
}

static
void destroyIslands(const MapToMeshBasic *pBasic, Islands *pIslands) {
	for (I32 i = 0; i < pIslands->intersectLists.count; ++i) {
		destroyIntersectList(pIslands->intersectLists.pArr + i);
	}
	pBasic->pCtx->alloc.fpFree(pIslands->intersectLists.pArr);
	stucLinAllocDestroy(pIslands->pAlloc);
	*pIslands = (Islands) {0};
}
*/

static
StucCompare compareIntersectMap(const void *pData, I32 idxA, I32 idxB) {
	const IntersectArr *pArr = pData;
	STUC_ASSERT(
		"",
		idxA >= -1 && idxA < pArr->count && idxB >= -1 && idxB < pArr->count
	);
	if (idxA == -1) {
		return STUC_COMPARE_LESS;
	}
	if (idxB == -1) {
		return STUC_COMPARE_GREAT;
	}
	const IntersectCorner *pA = pArr->pArr + idxA;
	const IntersectCorner *pB = pArr->pArr + idxB;
	if (pA->mapCorner < pB->mapCorner) {
		return STUC_COMPARE_LESS;
	}
	else if (pA->mapCorner > pB->mapCorner) {
		return STUC_COMPARE_GREAT;
	}
	else {
		STUC_ASSERT("", pA->tMapEdge != pB->tMapEdge);
		return pA->tMapEdge < pB->tMapEdge ? STUC_COMPARE_LESS : STUC_COMPARE_GREAT;
	}
}

static
StucCompare compareIntersectIn(const void *pData, I32 idxA, I32 idxB) {
	const IntersectArr *pArr = pData;
	STUC_ASSERT(
		"",
		idxA >= -1 && idxA < pArr->count && idxB >= -1 && idxB < pArr->count
	);
	if (idxA == -1) {
		return STUC_COMPARE_LESS;
	}
	if (idxB == -1) {
		return STUC_COMPARE_GREAT;
	}
	const IntersectCorner *pA = pArr->pArr + idxA;
	const IntersectCorner *pB = pArr->pArr + idxB;
	if (pA->borderIdx < pB->borderIdx) {
		return STUC_COMPARE_LESS;
	}
	else if (pA->borderIdx > pB->borderIdx) {
		return STUC_COMPARE_GREAT;
	}
	if (pA->borderEdge < pB->borderEdge) {
		return STUC_COMPARE_LESS;
	}
	else if (pA->borderEdge > pB->borderEdge) {
		return STUC_COMPARE_GREAT;
	}
	else {
		STUC_ASSERT("", pA->tInEdge != pB->tInEdge);
		return pA->tInEdge < pB->tInEdge ? STUC_COMPARE_LESS : STUC_COMPARE_GREAT;
	}
}

static
void destroyIntersectArr(const MapToMeshBasic *pBasic, IntersectArr *pArr) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	if (pArr->pArr) {
		STUC_ASSERT("", pArr->size);
		pAlloc->fpFree(pArr->pArr);
	}
	if (pArr->pBorderRanges) {
		pAlloc->fpFree(pArr->pBorderRanges);
	}
	*pArr = (IntersectArr) {0};
}

static
void initClippedArr(const MapToMeshBasic *pBasic, ClippedArr *pArr) {
	stucLinAllocInit(&pBasic->pCtx->alloc, &pArr->rootAlloc, sizeof(ClippedRoot), 1, true);
	stucLinAllocInit(&pBasic->pCtx->alloc, &pArr->mapAlloc, sizeof(MapCorner), 4, true);
}

static
void newClippedFace(
	CornerCore *pCornerToLink,
	ClippedArr *pArr
) {
	ClippedRoot *pRoot = NULL;
	stucLinAlloc(&pArr->rootAlloc, (void **)&pRoot, 1);
	pCornerToLink->pPrev = &pRoot->root;
	pRoot->root = (CornerCore){.pNext = pCornerToLink, .type = STUC_CORNER_ROOT};
}

static
void linkInboundOutboundPairs(const InPiece *pInPiece, IntersectArr *pIntersect) {
	const I32 *pSortedMap = pIntersect->pSortedMap;
	I32 offset = pIntersect->pArr[pSortedMap[0]].travelDir != INBOUND;
	for (I32 i = offset; i < pIntersect->count; i += 2) {
		I32 iNext = (i + 1) % pIntersect->count;
		IntersectCorner *pCornerA = &pIntersect->pArr[pSortedMap[i]];
		IntersectCorner *pCornerB = &pIntersect->pArr[pSortedMap[iNext]];
		STUC_ASSERT(
			"",
			pCornerA->travelDir == INBOUND && pCornerB->travelDir == OUTBOUND
		);
		pCornerA->core.pNext = &pCornerB->core;
		pCornerB->core.pPrev = &pCornerA->core;
	}
}

static
CornerCore *isThisAStartingCorner(
	IntersectCorner *pA,
	const IntersectCorner *pB
) {
	/*
	if (pA->travelDir == INBOUND) {
		STUC_ASSERT("", pB->travelDir == OUTBOUND);
		STUC_ASSERT(
			"these corners are an in-out-bound pair, so they should be linked",
			pA->core.pNext == &pB->core && pB->core.pPrev == &pA->core
		);
		if (!pA->core.pPrev) { //is this in-out-bound pair in a list?
			return &pA->core;
		}
	}
	else {
	*/
	STUC_ASSERT(
		"end and start of 2 separate in-out-bound pairs,\
			so these shouldn't be linked (unless prev is root)",
		!pA->core.pNext && (!pB->core.pPrev || pB->core.pPrev->type == STUC_CORNER_ROOT)
	);
	STUC_ASSERT("prev in-out-bound pair isn't linked?", pA->core.pPrev);

	if (!pA->core.pPrev->pPrev) { //is the prev in-out-bound pair in a list?
		return pA->core.pPrev;
	}
	else {
		return NULL;
	}
}

//TODO
//--skip clipping if no border in inPiece obviously
//--add uv mirroring to pSeamEdge predicate
//--linearize border edges into an array to avoid needing to walk multiple times over
//  (linearize as the start of this func, waste of memory to do it in splitPieces)
//--test and ensure onEdge verts work. Is snapping still necessary? won't know until I can map to the testGeo

//DONE OR SKIPPED
//--only link to root once you've done a full loop, to avoid multiple roots for the same face
//--make sure to invert link direction on out-in pairs that align with in-out pairs, do wind order isn't broken


static
void createClippedFaces(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const IntersectArr *pIntersect,
	ClippedArr *pClipped
) {
	const I32 *pSortedIn = pIntersect->pSortedIn;
	for (I32 i = 0; i < pInPiece->borderArr.count; ++i) {
		Range range = pIntersect->pBorderRanges[i];
		I32 offset = pIntersect->pArr[pSortedIn[range.start]].travelDir != OUTBOUND;
		I32 size = range.end - range.start;
		for (I32 j = offset; j < size; j += 2) {
			I32 jNext = (j + 1) % size;
			IntersectCorner *pCornerA = pIntersect->pArr + pSortedIn[range.start + j];
			IntersectCorner *pCornerB = pIntersect->pArr + pSortedIn[range.start + jNext];
			STUC_ASSERT(
				"",
				pCornerA->travelDir == OUTBOUND && pCornerB->travelDir == INBOUND
			);
			CornerCore *pCornerToLink = isThisAStartingCorner(pCornerA, pCornerB);
			if (pCornerToLink) {
				newClippedFace(pCornerToLink, pClipped);
			}
			pCornerA->core.pNext = &pCornerB->core;
			pCornerB->core.pPrev = &pCornerA->core;
		}
	}
}

static
void initIntersectArr(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	IntersectArr *pArr
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	pArr->pBorderRanges = pAlloc->fpCalloc(pInPiece->borderArr.count, sizeof(Range));
}

static
void setBorderRanges(IntersectArr *pArr) {
	I32 border = 0;
	for (I32 i = 0; i < pArr->count; ++i) {
		const IntersectCorner *pCorner = pArr->pArr + pArr->pSortedIn[i];
		if (pCorner->borderIdx != border) {
			STUC_ASSERT("", pCorner->borderIdx > border);
			pArr->pBorderRanges[border].end = i;
			pArr->pBorderRanges[pCorner->borderIdx].start = i;
			border = pCorner->borderIdx;
		}
	}
	pArr->pBorderRanges[border].end = pArr->count;
}

static
void removeInvalidRoots(ClippedArr *pArr) {
	LinAllocIter iter = {0};
	stucLinAllocIterInit(&pArr->rootAlloc, (Range) {0, INT32_MAX}, &iter);
	for (; !stucLinAllocIterAtEnd(&iter); stucLinAllocIterInc(&iter)) {
		ClippedRoot *pRoot = stucLinAllocGetItem(&iter);
		CornerCore *pStartCorner = pRoot->root.pNext;
		STUC_ASSERT("", pStartCorner);
		if (pStartCorner->checked) {
			//root points to a previous root's list, so remove it
			pRoot->root.pNext = NULL;
			continue;
		}
		CornerCore *pCorner = pStartCorner;
		do {
			STUC_ASSERT("", !pCorner->checked);
			pCorner->checked = true;
		} while(pCorner = pCorner->pNext, pCorner != pStartCorner);
	}
}

static
I32 getCornerListLen(const CornerCore *pRoot) {
	STUC_ASSERT("", pRoot->type == STUC_CORNER_ROOT);
	if (!pRoot->pNext) {
		return 0;
	}
	const CornerCore *pCorner = pRoot->pNext;
	I32 len = 0;
	do {
		len++;
	} while (pCorner = pCorner->pNext, pCorner != pRoot->pNext);
	return len;
}

static
void sortAlongInPieceBoundary(I32 *pSortedIn, const IntersectArr *pIntersect) {
	stucInsertionSort(
		pSortedIn + 1,
		pIntersect->count,
		pIntersect,
		compareIntersectIn
	);
}

static
void sortAlongMapFace(I32 *pSortedMap, const IntersectArr *pIntersect) {
	stucInsertionSort(
		pSortedMap + 1,
		pIntersect->count,
		pIntersect,
		compareIntersectMap
	);
}

static
Result clipMapEdgeAgainstInPiece(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	HTable *pInFaceCache,
	const FaceRange *pMapFace,
	bool mapFaceWind,
	IntersectArr *pIntersect,
	ClippedArr *pClippedFaces
) {
	Result err = STUC_SUCCESS;
	for (I32 i = 0; i < pMapFace->size; ++i) {
		/*
		if (islands.pIsland) {
			addCornerToIsland(pBasic, islands.pIsland, i);
		}
		*/
		intersectWithPiecePerim(
			pBasic,
			pInPiece,
			pMapFace,
			i,
			pInFaceCache,
			pIntersect
		);
	}
	if (!pIntersect->count) {
		ClippedRoot *pRoot = NULL;
		stucLinAlloc(&pClippedFaces->rootAlloc, (void **)&pRoot, 1);
		pRoot->noIntersect = true;
		return err;
	}
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	I32 *pSortedIn = pAlloc->fpMalloc((pIntersect->count + 1) * sizeof(I32));
	I32 *pSortedMap = pAlloc->fpMalloc((pIntersect->count + 1) * sizeof(I32));
	pSortedIn[0] = pSortedMap[0] = -1;
	sortAlongInPieceBoundary(pSortedIn, pIntersect);
	sortAlongMapFace(pSortedMap, pIntersect);
	pIntersect->pSortedIn = pSortedIn + 1;
	pIntersect->pSortedMap = pSortedMap + 1;
	setBorderRanges(pIntersect);
	linkInboundOutboundPairs(pInPiece, pIntersect);
	createClippedFaces(pBasic, pInPiece, pIntersect, pClippedFaces);
	removeInvalidRoots(pClippedFaces);
	pAlloc->fpFree(pSortedIn);
	pAlloc->fpFree(pSortedMap);
	pIntersect->pSortedIn = NULL;
	pIntersect->pSortedMap = NULL;
	return err;
}

#ifndef TEMP_DISABLE
static
Result clipMapFaceAgainstInFace(
	MapToMeshBasic *pBasic,
	EncasingInFaceArr *pInFaces,
	InFaceCache *pInFaceCache,
	MapIsland *pMapIsland,
	FaceRange *pMapFace,
	bool mapFaceWind,
	bool testAll
) {
	Result err = STUC_SUCCESS;
	MapIsland newMapIslandCorner = {
		.edgeFace = pMapIsland->edgeFace,
		.onLine = pMapIsland->onLine
	};
	//clip func was here
	if (newMapIslandCorner.count <= 2) {
		pMapIsland->count = newMapIslandCorner.count;
		return err;
	}
	MapIsland *pTail = &newMapIslandCorner;
	while (pTail->pNext) {
		pTail = pTail->pNext;
	}
	pTail->pNext = pMapIsland->pNext;
	*pMapIsland = newMapIslandCorner;
	return err;
}

static
Result clipMapFaceIntoFaces(
	MapToMeshBasic *pBasic,
	EncasingInFaceArr *pInFaces,
	InFaceCache *pInFaceCache,
	MapIsland *pMapIsland,
	FaceRange *pMapFace,
	bool mapFaceWind
) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", mapFaceWind % 2 == mapFaceWind);
	MapIsland *pMapIslandPtr = pMapIsland;
	//InCornerCache inCornerCache[4] = {0};
	//buildInCornerCache(pState->pBasic, inCornerCache, pInFace, mapFaceWind);
	//snapInVertsToMapEdges(pInFace, inCornerCache, pMapIsland);
	do {
		if (!pMapIslandPtr->invalid) {
			err = clipMapFaceAgainstInFace(
				pBasic
				pInFaces,
				pInFaceCache,
				pMapIslandPtr,
				&mapFace,
				mapFaceWind
			);
			STUC_RETURN_ERR_IFNOT(err, "");
		}
		pMapIslandPtr = pMapIslandPtr->pNext;
	} while (pMapIslandPtr);
	return err;
}
#endif

/*
static
Result allocBufMesh(BufMeshInitJobArgs *pArgs) {
	Result err = STUC_SUCCESS;
	const MapToMeshBasic *pBasic = pArgs->core.pBasic;
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	Mesh *pMesh = &pArgs->bufMesh.mesh;
	pMesh->core.type.type = STUC_OBJECT_DATA_MESH_BUF;
	pMesh->faceBufSize = pArgs->core.range.end - pArgs->core.range.start;
	pMesh->cornerBufSize = pMesh->faceBufSize * 2;
	pMesh->edgeBufSize = pMesh->cornerBufSize;
	pMesh->vertBufSize = pMesh->faceBufSize;
	pMesh->core.pFaces = pAlloc->fpMalloc(sizeof(I32) * pMesh->faceBufSize);
	pMesh->core.pCorners = pAlloc->fpMalloc(sizeof(I32) * pMesh->cornerBufSize);
	pMesh->core.pEdges = pAlloc->fpMalloc(sizeof(I32) * pMesh->edgeBufSize);
	pArgs->bufMesh.vertInfo.pArr = pAlloc->fpMalloc(pMesh->vertBufSize * sizeof(BufVertInfo));
	pArgs->bufMesh.vertInfo.pTypeArr = pAlloc->fpMalloc(pMesh->vertBufSize);
	//in-mesh is the active src,
	// unmatched active map attribs will not be marked active in the buf mesh
	const Mesh *srcs[2] = {pBasic->pInMesh, pBasic->pMap->pMesh};
	err = stucAllocAttribsFromMeshArr(
		pBasic->pCtx,
		pMesh,
		2,
		srcs,
		0,
		true, true, false
	);
	STUC_THROW_IFNOT(err, "", 0);
	err = stucAssignActiveAliases(
		pBasic->pCtx,
		pMesh,
		STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) {
			STUC_ATTRIB_USE_POS,
			STUC_ATTRIB_USE_UV,
			STUC_ATTRIB_USE_NORMAL,
			STUC_ATTRIB_USE_WSCALE
		})),
		STUC_DOMAIN_NONE
	);
	STUC_THROW_IFNOT(err, "", 0);
	stucSetAttribCopyOpt(
		pBasic->pCtx,
		&pMesh->core,
		STUC_ATTRIB_DONT_COPY,
		0x1 << STUC_ATTRIB_USE_WSCALE
	);

	STUC_CATCH(0, err,
		stucMeshDestroy(pBasic->pCtx, &pMesh->core);
	;);
	return err;
}
*/

static
void getInPieceBounds(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	HTable *pInFaceCache
	//V2_F32 *pFMin, V2_F32 *pFMax
) {
	EncasingInFaceArr *pInFaces = &pInPiece->pList->inFaces;
	//pFMin->d[0] = pFMin->d[1] = FLT_MAX;
	//pFMax->d[0] = pFMax->d[1] = -FLT_MAX;
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
		/*
		if (pCacheEntry->fMin.d[0] < pFMin->d[0]) {
			pFMin->d[0] = pCacheEntry->fMin.d[0];
		}
		if (pCacheEntry->fMin.d[1] < pFMin->d[1]) {
			pFMin->d[1] = pCacheEntry->fMin.d[1];
		}
		if (pCacheEntry->fMax.d[0] > pFMax->d[0]) {
			pFMax->d[0] = pCacheEntry->fMax.d[0];
		}
		if (pCacheEntry->fMax.d[1] > pFMax->d[1]) {
			pFMax->d[1] = pCacheEntry->fMax.d[1];
		}
		*/
	}
	//STUC_ASSERT("", _(*pFMax V2GREAT *pFMin));
}

#ifndef TEMP_DISABLE
static
void initMapIslandCorner(
	const StucMap pMap,
	V2_F32 fTileMin,
	MapIsland *pMapIsland,
	const FaceRange *pInFace,
	const FaceRange *pMapFace,
	bool mapFaceWind
) {
	STUC_ASSERT("", mapFaceWind % 2 == mapFaceWind);
	pMapIsland->count = pMapFace->size;
	for (I32 k = 0; k < pMapFace->size; ++k) {
		I32 vertIdx = pMap->pMesh->core.pCorners[pMapFace->start + k];
		MapIslandCorner *pCorner = pMapIsland->pCorners + k;
		pCorner->isMapCorner = 1;
		pCorner->corner = pMap->pMesh->pPos[vertIdx];
		_((V2_F32 *)&pCorner->corner V2ADDEQL fTileMin);
		pCorner->mapCorner = (I8)k;
		pCorner->normal = pMap->pMesh->pNormals[pMapFace->start + k];
	}
	pMapIsland->lastInCorner = mapFaceWind ? 0 : pInFace->size - 1;
}
#endif

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
		if (!_(vert V2GREAT pInFaceEntry->fMin) || !_(vert V2LESS pInFaceEntry->fMax)) {
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
void destroyClippedArr(ClippedArr *pArr) {
	stucLinAllocDestroy(&pArr->rootAlloc);
	stucLinAllocDestroy(&pArr->mapAlloc);
	*pArr = (ClippedArr) {0};
}

static
I32 getCornersMapCorner(const CornerCore *pCorner) {
	switch (pCorner->type) {
	case STUC_CORNER_MAP:
		return ((MapCorner *)pCorner)->corner;
	case STUC_CORNER_INTERSECT:
		return ((IntersectCorner *)pCorner)->mapCorner;
	default:
		STUC_ASSERT("invalid corner type for this func", false);
		return -1;
	}
}

static
I32 getNextMapCorner(
	const FaceRange *pMapFace,
	const CornerCore *pCorner
) {
	I32 mapCorner = 0;
	I32 mapCornerNext = 0;
	if (pCorner->type == STUC_CORNER_ROOT) {
		if (!pCorner->pNext) {
			return 0;
		}
		return -1;
	}
	if (pCorner->type == STUC_CORNER_INTERSECT &&
		((IntersectCorner *)pCorner)->travelDir == OUTBOUND
	) {
		return -1;
	}
	mapCorner = getCornersMapCorner(pCorner);
	STUC_ASSERT("", pCorner->pNext);
	mapCornerNext = getCornersMapCorner(pCorner->pNext);
	STUC_ASSERT("", pCorner->pNext->type == STUC_CORNER_INTERSECT);
	if (mapCorner == mapCornerNext) {
		return -1;
	}
	return stucGetCornerNext(mapCorner, pMapFace);
}

static
void insertCornerIntoList(CornerCore *pListCorner, CornerCore *pNewCorner) {
	STUC_ASSERT("", pListCorner->pNext && pListCorner->pNext->pPrev == pListCorner);
	pNewCorner->pNext = pListCorner->pNext;
	pNewCorner->pPrev = pListCorner;
	pListCorner->pNext = pNewCorner;
	pNewCorner->pNext->pPrev = pNewCorner;
}

static
void insertNewMapCornerIntoList(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	HTable *pInFaceCache,
	const FaceRange *pMapFace, I32 mapCorner,
	CornerCore *pListCorner,
	ClippedArr *pClippedFaces
) {
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	V2_F32 pos = *(V2_F32 *)&pMapMesh->pPos[
		pMapMesh->core.pCorners[pMapFace->start + mapCorner]
	];
	I32 encasingInFace = getFaceEncasingVert(pBasic, pos, pInPiece, pInFaceCache);
	STUC_ASSERT(
		"an exterior map corner shouldn't have been passed to this func",
		encasingInFace != -1
	);
	MapCorner *pNewCorner = NULL;
	stucLinAlloc(&pClippedFaces->mapAlloc, (void**)&pNewCorner, 1);
	insertCornerIntoList(pListCorner, &pNewCorner->core);
	pNewCorner->core.type = STUC_CORNER_MAP;
	pNewCorner->corner = mapCorner;
	pNewCorner->inFace = encasingInFace;
}

static
void insertMapCornersIntoList(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	HTable *pInFaceCache,
	const FaceRange *pMapFace,
	ClippedArr *pClippedFaces,
	CornerCore *pStartCorner
) {
	STUC_ASSERT("", pStartCorner->type != STUC_CORNER_ROOT);
	CornerCore *pCorner = pStartCorner;
	do {
		I32 mapCorner = getNextMapCorner(pMapFace, pCorner);
		if (mapCorner == -1) {
			continue;
		}
		insertNewMapCornerIntoList(
			pBasic,
			pInPiece,
			pInFaceCache,
			pMapFace, mapCorner,
			pCorner,
			pClippedFaces
		);
	} while(pCorner = pCorner->pNext, pCorner != pStartCorner);
}

/*
static
V3_F32 getCornerPos(
	const MapToMeshBasic *pBasic,
	const FaceRange *pMapFace,
	const CornerCore *pCorner
) {
	const Mesh *pMapMesh = pBasic->pMap->pMesh;
	switch (pCorner->type) {
		case STUC_CORNER_MAP:
			return pMapMesh->pPos[pMapMesh->core.pCorners[
				pMapFace->start + ((MapCorner *)pCorner)->corner
			]];
		case STUC_CORNER_INTERSECT:
			return ((IntersectCorner *)pCorner)->pos;
		default:
			STUC_ASSERT("invalid corner type for this", false);
			return (V3_F32) {0};
	}
}
*/

#define BUF_MESH_ADD(t, pBasic, pBufArr, newIdx) \
	STUC_ASSERT("", pBufArr->count <= pBufArr->size);\
	if (!pBufArr->size) {\
		STUC_ASSERT("", !pBufArr->pArr);\
		pBufArr->size = 4;\
		pBufArr->pArr = pBasic->pCtx->alloc.fpMalloc(\
			pBufArr->size * sizeof(t)\
		);\
	}\
	else if (pBufArr->count == pBufArr->size) {\
		pBufArr->size *= 2;\
		pBufArr->pArr = pBasic->pCtx->alloc.fpRealloc(\
			pBufArr->pArr,\
			pBufArr->size * sizeof(t)\
		);\
	}\
	newIdx = pBufArr->count;\
	pBufArr->count++;


static
I32 bufMeshAddInOrMapVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertInOrMapArr *pVertArr = &pBufMesh->inOrMapVerts;
	I32 newVert = -1;
	BUF_MESH_ADD(InOrMapVert, pBasic, pVertArr, newVert);
	STUC_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAddOnEdgeVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOnEdgeArr *pVertArr = &pBufMesh->onEdgeVerts;
	I32 newVert = -1;
	BUF_MESH_ADD(BufVertOnEdge, pBasic, pVertArr, newVert);
	STUC_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAddOverlapVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertOverlapArr *pVertArr = &pBufMesh->overlapVerts;
	I32 newVert = -1;
	BUF_MESH_ADD(OverlapVert, pBasic, pVertArr, newVert);
	STUC_ASSERT("", newVert >= 0);
	return newVert;
}

static
I32 bufMeshAddIntersectVert(const MapToMeshBasic *pBasic, BufMesh *pBufMesh) {
	BufVertIntersectArr *pVertArr = &pBufMesh->intersectVerts;
	I32 newVert = -1;
	BUF_MESH_ADD(IntersectVert, pBasic, pVertArr, newVert);
	STUC_ASSERT("", newVert >= 0);
	return newVert;
}

static
void setIntersectBufVertInfo(
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const CornerCore *pCorner,
	BufVertType *pType,
	I32 *pVert
) {
	IntersectCorner *pCornerCast = (IntersectCorner *)pCorner;
	FaceCorner mapCorner = (FaceCorner){
		.face = pMapFace->idx,
		.corner = pCornerCast->mapCorner
	};
	switch (pCornerCast->type) {
		case STUC_INTERSECT_TYPE_INTERSECT:
			*pType = STUC_BUF_VERT_INTERSECT;
			*pVert = bufMeshAddIntersectVert(pBasic, pBufMesh);
			pBufMesh->intersectVerts.pArr[*pVert] =
				(IntersectVert){
					.pos = *(V2_F32 *)&pCornerCast->pos,
					.inFace = pCornerCast->inCorner.face,
					.inCorner = pCornerCast->inCorner.corner,
					.mapCorner = mapCorner.corner,
					.tInEdge = pCornerCast->tInEdge,
					.tMapEdge = pCornerCast->tMapEdge,
				};
			break;
		case STUC_INTERSECT_TYPE_ON_EDGE:
			*pType = STUC_BUF_VERT_ON_EDGE;
			*pVert = bufMeshAddOnEdgeVert(pBasic, pBufMesh);
			pBufMesh->onEdgeVerts.pArr[*pVert].map =
				(EdgeMapVert) {
					.type = STUC_BUF_VERT_SUB_TYPE_EDGE_MAP,
					.mapCorner = pCornerCast->mapCorner,
					.inCorner = pCornerCast->inCorner.corner,
					.inFace = pCornerCast->inCorner.face
				};
			break;
		case STUC_INTERSECT_TYPE_ON_VERT:
			*pType = STUC_BUF_VERT_OVERLAP;
			*pVert = bufMeshAddOverlapVert(pBasic, pBufMesh);
			pBufMesh->overlapVerts.pArr[*pVert] =
				(OverlapVert) {
					.inFace = pCornerCast->inCorner.face,
					.inCorner = pCornerCast->inCorner.corner,
					.mapCorner = pCornerCast->mapCorner
				};
			break;
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
	BUF_MESH_ADD(BufFace, pBasic, (&pBufMesh->faces), newIdx);
	STUC_ASSERT("", newIdx != -1);
	pBufMesh->faces.pArr[newIdx].start = start;
	pBufMesh->faces.pArr[newIdx].size = faceSize;
	pBufMesh->faces.pArr[newIdx].inPiece = inPieceOffset;
}

static
void bufMeshAddVert(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const FaceRange *pMapFace,
	const CornerCore *pCorner,
	BufMesh *pBufMesh
) {
	BufCornerArr *pCorners = &pBufMesh->corners;
	I32 newCorner = -1;
	BUF_MESH_ADD(BufCorner, pBasic, pCorners, newCorner);
	STUC_ASSERT("", newCorner >= 0);
	BufVertType type = 0;
	I32 vert = -1;
	switch (pCorner->type) {
		case STUC_CORNER_MAP: {
			type = STUC_BUF_VERT_IN_OR_MAP;
			vert = bufMeshAddInOrMapVert(pBasic, pBufMesh);
			pBufMesh->inOrMapVerts.pArr[vert].map = (MapVert){
				.type = STUC_BUF_VERT_SUB_TYPE_MAP,
				.mapCorner = ((MapCorner *)pCorner)->corner,
				.inFace = ((MapCorner *)pCorner)->inFace
			};
			break;
		}
		case STUC_CORNER_IN:
			type = STUC_BUF_VERT_IN_OR_MAP;
			vert = bufMeshAddInOrMapVert(pBasic, pBufMesh);
			pBufMesh->inOrMapVerts.pArr[vert].in = (InVert){
				.type = STUC_BUF_VERT_SUB_TYPE_IN,
				.inCorner = ((InCorner *)pCorner)->corner,
				.inFace = ((InCorner *)pCorner)->inFace
			};
			break;
		case STUC_CORNER_INTERSECT:
			setIntersectBufVertInfo(pBasic, pBufMesh, pMapFace, pCorner, &type, &vert);
			break;
		default:
			STUC_ASSERT("invalid corner type", false);
	}
	STUC_ASSERT("", vert != -1);
	pCorners->pArr[newCorner].type = type;
	pCorners->pArr[newCorner].vert = vert;
}

static
void addFaceToBufMesh(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	BufMesh *pBufMesh,
	const FaceRange *pMapFace,
	const CornerCore *pRoot,
	I32 cornerCount
) {
	STUC_ASSERT("", pRoot->pNext && pRoot->type == STUC_CORNER_ROOT);
	const CornerCore *pStartCorner = pRoot->pNext;
	const CornerCore *pCorner = pStartCorner;
	bufMeshAddFace(pBasic, inPieceOffset, pBufMesh, pBufMesh->corners.count, cornerCount);
	do {
		bufMeshAddVert(pBasic, inPieceOffset, pMapFace, pCorner, pBufMesh);
	} while(pCorner = pCorner->pNext, pCorner != pStartCorner);
}

static
void addFacesToBufMesh(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	HTable *pInFaceCache,
	const FaceRange *pMapFace,
	ClippedArr *pClippedFaces,
	IntersectArr *pIntersect
) {
	STUC_ASSERT("", pIntersect->size);
	LinAllocIter iter = {0};
	stucLinAllocIterInit(&pClippedFaces->rootAlloc, (Range) {0, INT32_MAX}, &iter);
	for (; !stucLinAllocIterAtEnd(&iter); stucLinAllocIterInc(&iter)) {
		ClippedRoot *pRoot = stucLinAllocGetItem(&iter);
		if (!pRoot->root.pNext) {
			continue; //invalid root
		}
		CornerCore *pStartCorner = pRoot->root.pNext;
		insertMapCornersIntoList(
			pBasic,
			pInPiece,
			pInFaceCache,
			pMapFace,
			pClippedFaces,
			pStartCorner
		);
		I32 len = getCornerListLen(&pRoot->root);
		if (len < 3) {
			continue;
		}
		addFaceToBufMesh(pBasic, inPieceOffset, pBufMesh, pMapFace, &pRoot->root, len);
	}
}

static
void addMapVertToBufMesh(
	const MapToMeshBasic *pBasic,
	BufMesh *pBufMesh,
	I32 mapCorner,
	I32 encasingInFace
) {
	BufCornerArr *pCorners = &pBufMesh->corners;
	I32 newCorner = -1;
	BUF_MESH_ADD(BufCorner, pBasic, pCorners, newCorner);
	STUC_ASSERT("", newCorner >= 0);
	I32 newVert = bufMeshAddInOrMapVert(pBasic, pBufMesh);
	pBufMesh->inOrMapVerts.pArr[newVert].map = (MapVert){
		.type = STUC_BUF_VERT_SUB_TYPE_MAP,
		.mapCorner = mapCorner,
		.inFace = encasingInFace
	};
	pCorners->pArr[newCorner].type = STUC_BUF_VERT_IN_OR_MAP;
	pCorners->pArr[newCorner].vert = newVert;
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
			if (!i) {
				//mapface is outside piece - skip
				return err;
			}
			STUC_ASSERT("non-clipped map faces must be fully in or out", false);
		}
		addMapVertToBufMesh(pBasic, pBufMesh, i, encasingInFace);
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

Result stucClipMapFace(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh
) {
	Result err = STUC_SUCCESS;
	FaceRange mapFace = 
		stucGetFaceRange(&pBasic->pMap->pMesh->core, pInPiece->pList->mapFace);

	bool mapFaceWind = stucCalcFaceWindFromVerts(&mapFace, pBasic->pMap->pMesh);
	//MapCornerCache mapCornerCache = {0}; //TODO change this to a different map corner arr. Stop using MapIslands
	//initMapCornerCache(pBasic->pMap, &mapCornerCache, &mapFace, mapFaceWind);
	//I32 insideCount = 0;
	/*
	for (I32 i = 0; i < mapFace.size; ++i) {
		if (_(*(V2_F32 *)&MapIslandCorner.buf[i].corner V2LESS fPieceMin) ||
			_(*(V2_F32 *)&MapIslandCorner.buf[i].corner V2GREAT fPieceMax)
		) {
			MapIslandCorner.buf[i].inFace = -1;
			continue;
		}
		InFaceCorner inCorner = getFaceEncasingVert(
			pBasic,
			*(V2_F32 *)&MapIslandCorner.buf[i].corner,
			pInPiece,
			pInFaceCache
		);
		if (!inCorner.pFace) {
			continue;
		}
		mapCorners.pCorners[i].inFace = inCorner.pFace->face.idx;
		MapIslandCorner.buf[i].inEdge =
			pBasic->pInMesh->core.pEdges[inCorner.pFace->face.start + inCorner.corner];
		insideCount++;
	}
	*/
	InFaceCacheState inFaceCacheState = {.pBasic = pBasic, .initBounds = true};
	HTable inFaceCache = {0};
	stucHTableInit(
		&pBasic->pCtx->alloc,
		&inFaceCache,
		pInPiece->faceCount / 2 + 1,
		(I32Arr) {.pArr = (I32[]) {sizeof(InFaceCacheEntryIntern)}, .count = 1 },
		&inFaceCacheState
	);
	//V2_F32 fPieceMin = {0};
	//V2_F32 fPieceMax = {0};
	getInPieceBounds(pBasic, pInPiece, &inFaceCache);

	IntersectArr intersectArr = {0};
	ClippedArr clippedFaces = {0};
	initIntersectArr(pBasic, pInPiece, &intersectArr);
	initClippedArr(pBasic, &clippedFaces);
	err = clipMapEdgeAgainstInPiece(
		pBasic,
		pInPiece,
		&inFaceCache,
		&mapFace,
		mapFaceWind,
		&intersectArr,
		&clippedFaces
	);
	STUC_RETURN_ERR_IFNOT(err, "");
	STUC_ASSERT("", stucLinAllocGetCount(&clippedFaces.rootAlloc));
	if (((ClippedRoot *)stucLinAllocIdx(&clippedFaces.rootAlloc, 0))->noIntersect) {
		//no edges clipped the mapface, treat as a non-clip inPiece
		addNonClipInPieceToBufMesh(
			pBasic,
			inPieceOffset,
			pInPiece,
			pBufMesh,
			&inFaceCache
		);
	}
	else {
		addFacesToBufMesh(
			pBasic,
			inPieceOffset,
			pInPiece,
			pBufMesh,
			&inFaceCache,
			&mapFace,
			&clippedFaces,
			&intersectArr
		);
	}
	destroyClippedArr(&clippedFaces);
	destroyIntersectArr(pBasic, &intersectArr);
	inFaceCacheDestroy(pBasic, &inFaceCache);

	/*
	addOrDiscardClippedFaces(
		pState,
		fTileMin,
		&MapIslandCorner,
		tile,
		pInFace,
		&mapFace,
		pInTriConst,
		inFaceWind, mapFaceWind
	);
	*/
	STUC_CATCH(1, err, 
		err = STUC_SUCCESS; //skipping this face, reset err
	);
	//err = stucLinAllocClear(pState->pMapIslandAlloc, true);
	STUC_THROW_IFNOT(err, "", 0);
	STUC_CATCH(0, err, ;);
	return err;
}

Result stucAddMapFaceToBufMesh(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh
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
	STUC_RETURN_ERR_IFNOT(err, "");
	inFaceCacheDestroy(pBasic, &inFaceCache);
	return err;
}

Result stucBufMeshInit(void *pArgsVoid) {
	Result err = STUC_SUCCESS;
	BufMeshInitJobArgs *pArgs = pArgsVoid;

	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	for (I32 i = 0; i < rangeSize; ++i) {
		I32 inPieceIdx = pArgs->core.range.start + i;
		pArgs->fpAddPiece(
			pArgs->core.pBasic,
			i,
			pArgs->pInPiecesSplit->pArr + inPieceIdx,
			&pArgs->bufMesh
		);
	}
	return err;
}

/*
static
EncasedMapFaceBucket *getEncasedFacesBucket(
	FindEncasedFacesJobArgs *pArgs,
	const FaceRange *pMapFace,
	V2_I16 tile
) {
	U32 hash = stucGetEncasedFaceHash(pMapFace->idx, tile, pArgs->encasedFaces.size);
	return pArgs->encasedFaces.pTable + hash;
}
*/

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

/*
static
InsideStatus isPointInFace(
	V2_F32 point,
	InCornerCache *pCorners,
	I32 faceSize,
	bool wind
) {
	STUC_ASSERT(
		"is this face a convex hull? Wont work on concave faces",
		faceSize == 3 || faceSize == 4
	);
	for (I32 i = 0; i < faceSize; ++i) {
		InsideStatus status =
			stucIsPointInHalfPlane(point, pCorners[i].uv, pCorners[i].halfPlane, wind);
		if (status != STUC_INSIDE_STATUS_INSIDE) {
			return status;
		}
	}
	return STUC_INSIDE_STATUS_INSIDE;
}
*/

/*
//note, this is used later for vert merging, and attirb interpolation.
//the inface is added to the mapface's entry regardless of this func
static
void recordEncasedMapCorners(
	const MapToMeshBasic *pBasic,
	const FaceRange *pInFace,
	bool inFaceWind,
	EncasedMapFace *pEntry,
	const Mesh *pMapMesh,
	const FaceRange *pMapFace,
	const FaceBounds *pBounds
) {
	for (I32 i = 0; i < pMapFace->size; ++i) {
		V2_F32 vert =
			*(V2_F32 *)&pMapMesh->pPos[pMapMesh->core.pCorners[pMapFace->start + i]];
		if (_(vert V2LESS pBounds->fMin) || _(vert V2GREAT pBounds->fMax)) {
			continue;
		}
		InCornerCache inCorners[4] = {0};
		initInCornerCache(pBasic, pInFace, &inCorners);
		InsideStatus status = isPointInFace(vert, inCorners, pInFace->size, inFaceWind);
		if (status == STUC_INSIDE_STATUS_OUTSIDE) {
			continue;
		}
		if (!pEntry->encasedCorners.pArr) {
			const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
			pEntry->encasedCorners.pArr =
				pAlloc->fpCalloc(pMapFace->size, sizeof(EncasedMapCorner));
		}
		EncasedMapCorner *pCornerEntry = pEntry->encasedCorners.pArr + i;
		pEntry->encasedCorners.count++;
		pCornerEntry->inFace = pInFace->idx;
		if (status == STUC_INSIDE_STATUS_INSIDE) {
			STUC_ASSERT(
				"point cant be inside 2 faces?", 
				pCornerEntry->status == STUC_INSIDE_STATUS_NONE
			);
			pCornerEntry->status = STUC_INSIDE_STATUS_INSIDE;
		}
		else if (status == STUC_INSIDE_STATUS_ON_LINE) {
			STUC_ASSERT(
				"",
				pCornerEntry->status == STUC_INSIDE_STATUS_ON_LINE ||
				pCornerEntry->status == STUC_INSIDE_STATUS_NONE
			);
			pCornerEntry->status = STUC_INSIDE_STATUS_ON_LINE;
		}
		else {
			STUC_ASSERT("invalid status?", false);
		}
	}
}
*/

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
	stucGetFaceBounds(&bounds, pInMesh->pUvs, *pInFace);

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
			FaceRange mapFace =
				stucGetFaceRange(&pMap->pMesh->core, pCellFaces[j]);
			if (!stucCheckFaceIsInBounds(
				_(bounds.fMin V2SUB fTileMin),
				_(bounds.fMax V2SUB fTileMin),
				mapFace,
				pMap->pMesh)
				) {
				continue;
			}
			addToEncasedFaces(pArgs, pInFace, inFaceWind, &mapFace, tile);
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

typedef struct SrcFaces {
	I32 in;
	I32 map;
} SrcFaces;

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
	const U8 *pTri = stucGetTri(pBasic->pMap->triCache.pArr + mapFace.idx, mapTri);
	V2_F32 inUv = pBasic->pInMesh->pUvs[inFace.start + inCorner];
	pCache->triMap.bc =
		stucGetBarycentricInTriFromVerts(pBasic->pMap->pMesh, &mapFace, pTri, inUv);
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

void getBufMeshForVertMergeEntry(
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
		if (pArgs->intersect) {
			VertMergeIntersect *pIntersect = (VertMergeIntersect *)pEntry;
			if (pIntersect->pSnapTo) {
				continue; //vert was snapped to another - skip
			}
		}
		const InPiece *pInPiece = NULL;
		const BufMesh *pBufMesh = NULL;
		getBufMeshForVertMergeEntry(
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
		getBufMeshForVertMergeEntry(
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

static
SrcFaces getSrcFacesForBufCorner(
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
		getBufMeshForVertMergeEntry(
			pArgs->pInPieces, pArgs->pInPiecesClip,
			pVertEntry,
			&pInPiece,
			&pBufMesh
		);
		SrcFaces srcFaces = {0};
		getSrcFacesForBufCorner(
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
