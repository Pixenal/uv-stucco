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

typedef struct {
	V3_F32 a;
	V3_F32 b;
	V3_F32 c;
} TriXyz;

typedef struct {
	V2_F32 a;
	V2_F32 b;
	V2_F32 c;
} TriUv;

typedef struct InCornerCache {
	V2_F32 uv;
	V2_F32 uvNext;
	V2_F32 dir;
	V2_F32 dirUnit;
	V2_F32 cross;
	F32 len;
	I8 idx;
	I8 idxPrev;
	I8 idxNext;
	bool flipEdgeDir;
} InCornerCache;

typedef struct {
	CornerBuf *pArr;
	I32 count;
	I32 size;
} CornerAncestors;

typedef struct {
	CornerBufWrap *pIsland;
	CornerBufWrap *pPending;
	I32 corner;
} IslandIdxPair;

typedef enum {
	OUTSIDE,
	INSIDE,
	ON_LINE
} InsideStatus;

typedef struct {
	int8_t status; // uses InsideStatus
	bool markAsOnLine;
} InsideCache;

static
bool checkIfOnVert(CornerBufWrap *pCornerBuf, I32 i, I32 iNext) {
	return
		(
			pCornerBuf->buf[i].inCorner == pCornerBuf->buf[iNext].inCorner ||
			pCornerBuf->buf[i].onInVert || pCornerBuf->buf[iNext].onInVert
		) &&
		(
			(!pCornerBuf->buf[i].isMapCorner && !pCornerBuf->buf[iNext].isMapCorner) ||
			(
				(pCornerBuf->buf[i].onLine || pCornerBuf->buf[iNext].onLine) &&
				(pCornerBuf->buf[i].isMapCorner ^ pCornerBuf->buf[iNext].isMapCorner)
			) ||
			(
				pCornerBuf->buf[i].onLine && pCornerBuf->buf[iNext].onLine &&
				pCornerBuf->buf[i].isMapCorner && pCornerBuf->buf[iNext].isMapCorner
			)
		);
}

//this corner already resided on a previous base edge,
//it must then reside on a base vert, rather than an edge
static
void handleOnInVert(
	CornerBuf *pNewEntry,
	CornerBufWrap *pCornerBuf,
	I32 idx,
	InCornerCache *pInCorner,
	bool mapFaceWind, bool inFaceWind
) {
	I32 lastBaseCorner = mapFaceWind ? pInCorner->idx - 1 : pInCorner->idx + 1;
	bool whichVert = pCornerBuf->buf[idx].inCorner == lastBaseCorner;
	if (inFaceWind) {
		pNewEntry->inCorner = whichVert ?
			pInCorner->idx : pInCorner->idxNext;
		if (!whichVert) {
			//if the corner maintains it's existing incorner,
			//then ensure the segment also carries over.
			// This is redundant if called on an insideCorner,
			// as whole corner buf entry is copied
			pNewEntry->segment = pCornerBuf->buf[idx].segment;
		}
	}
	else {
		pNewEntry->inCorner = whichVert ?
			pInCorner->idxPrev : pInCorner->idx;
		if (whichVert) {
			pNewEntry->segment = pCornerBuf->buf[idx].segment;
		}
	}
	pNewEntry->onInVert = true;
}

static
void addInsideCornerToBuf(
	CornerBufWrap *pNewCornerBuf,
	CornerBufWrap *pCornerBuf,
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
	CornerBuf *pNewEntry = pNewCornerBuf->buf + pNewCornerBuf->count;
	pNewCornerBuf->buf[pNewCornerBuf->count] = pCornerBuf->buf[i];
	//using += so that base corners can be determined. ie, if an stuc
	//vert has a dot of 0 twice, then it is sitting on a base vert,
	//but if once, then it's sitting on an edge.
	if (pInside[i].status == ON_LINE) {
		//is on line
		if ((pInside[iPrev].status != OUTSIDE) ^ (pInside[iNext].status != OUTSIDE)) {
			//add to intersection buf
			pIntersectCache[*pCount].pIsland = pNewCornerBuf;
			pIntersectCache[*pCount].corner = pNewCornerBuf->count;
			CornerBuf *pCorner = pCornerBuf->buf + i;
			CornerBuf *pCornerNext = pCornerBuf->buf + iNext;
			stucCalcIntersection(
				pCorner->corner,
				pCornerNext->corner,
				pInCorner->uv,
				pInCorner->dir,
				NULL,
				NULL,
				&pNewEntry->alpha
			);
			pNewEntry->alpha *= -1.0f;
			ptBuf[*pCount] = pNewEntry->alpha;
			++*pCount;
		}
		if (pCornerBuf->buf[i].onLine || !pCornerBuf->buf[i].isMapCorner) {
			handleOnInVert(
				pNewEntry,
				pCornerBuf, i,
				pInCorner,
				mapFaceWind, inFaceWind
			);
		}
		else if (pCornerBuf->buf[i].isMapCorner) {
			//resides on base edge
			pNewEntry->inCorner = pInCorner->idx;
		}
		pNewCornerBuf->onLine = true;
		pNewEntry->onLine = true;
	}
	else if (pInside[i].markAsOnLine) {
		handleOnInVert(
			pNewEntry,
			pCornerBuf, i,
			pInCorner,
			mapFaceWind, inFaceWind
		);
		pNewEntry->onLine = true;
	}
	pNewCornerBuf->count++;
}

static
I32 appendToAncestors(
	const StucAlloc *pAlloc,
	CornerAncestors *pAncestors,
	CornerBuf *pCorner
) {
	STUC_ASSERT("", pAncestors->count <= pAncestors->size);
	if (pAncestors->count == pAncestors->size) {
		pAncestors->size *= 2;
		pAncestors->pArr =
			pAlloc->pRealloc(pAncestors->pArr, sizeof(CornerBuf) * pAncestors->size);
	}
	I32 idx = pAncestors->count;
	pAncestors->pArr[idx] = *pCorner;
	pAncestors->count++;
	return idx;
}

static
void getMapVertsFromBufCorners(
	StucMap pMap,
	CornerBuf *pCorner, CornerBuf *pCornerNext,
	FaceRange *pMapFace,
	V3_F32 *pVertA, V3_F32 *pVertB
) {
	const Mesh *pMapMesh = pMap->pMesh;
	if (pCorner->isMapCorner) {
		*pVertA = pCorner->corner;
		I32 mapCornerNext = (pCorner->mapCorner + 1) % pMapFace->size;
		*pVertB =
			pMapMesh->pVerts[pMapMesh->core.pCorners[pMapFace->start + mapCornerNext]];
	}
	else if (pCornerNext->isMapCorner) {
		*pVertB = pCornerNext->corner;
		I32 mapCornerPrev = pCornerNext->mapCorner ?
			pCornerNext->mapCorner - 1 : pMapFace->size - 1;
		*pVertA =
			pMapMesh->pVerts[pMapMesh->core.pCorners[pMapFace->start + mapCornerPrev]];
	}
	else {
		V3_F32 mapVert =
			pMapMesh->pVerts[pMapMesh->core.pCorners[pMapFace->start + pCorner->mapCorner]];
		I32 mapCornerNext = (pCorner->mapCorner + 1) % pMapFace->size;
		V3_F32 mapVertNext =
			pMapMesh->pVerts[pMapMesh->core.pCorners[pMapFace->start + mapCornerNext]];
		F32 dot = _(
			_(pCornerNext->corner V3SUB pCorner->corner) V3DOT
			_(pCorner->corner V3SUB mapVert)
		);
		bool which = dot >= .0f;
		*pVertA = which ? mapVert : mapVertNext;
		*pVertB = which ? mapVertNext : mapVert;
	}
}

static
bool areCornersOnSameMapEdge(
	CornerBuf *pCorner, CornerBuf *pCornerNext,
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
	FaceRange *pMapFace,
	CornerBufWrap *pNewCornerBuf,
	CornerBufWrap *pCornerBuf,
	I32 i,
	I32 iNext,
	InCornerCache *pInCorner,
	IslandIdxPair *pIntersectCache,
	F32 *ptBuf,
	I32 *pCount,
	bool mapFaceWind, bool inFaceWind,
	CornerAncestors *pAncestors
) {
	pIntersectCache[*pCount].pIsland = pNewCornerBuf;
	pIntersectCache[*pCount].corner = pNewCornerBuf->count;
	CornerBuf *pCorner = pCornerBuf->buf + i;
	CornerBuf *pCornerNext = pCornerBuf->buf + iNext;
	CornerBuf *pNewEntry = pNewCornerBuf->buf + pNewCornerBuf->count;

	if (areCornersOnSameMapEdge(pCorner, pCornerNext, pMapFace)) {
		stucCalcIntersection(
			pCorner->corner, pCornerNext->corner,
			pInCorner->uv, pInCorner->dir,
			NULL,
			&pNewEntry->mapAlpha, &pNewEntry->alpha
		);
		V3_F32 mapVertA = {0};
		V3_F32 mapVertB = {0};
		getMapVertsFromBufCorners(
			pBasic->pMap,
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
			&pNewEntry->mapAlpha, &pNewEntry->alpha
		);
	}
	pNewEntry->alpha *= -1.0f;
	//this attrib is lerped here instead of later like other attribs,
	//as it's needed to transform from uvw to xyz
	//TODO is this still necessary? or is it obsolete?
	pNewEntry->normal =
		v3F32Lerp(pCorner->normal, pCornerNext->normal, pNewEntry->mapAlpha);
	//pNewEntry->normal = pCornerBuf->buf[i].normal;
	//V3_F32 up = {.0f, .0f, 1.0f};
	//pNewEntry->normal = up;
	if (checkIfOnVert(pCornerBuf, i, iNext)) {
		handleOnInVert(
			pNewEntry,
			pCornerBuf, i,
			pInCorner,
			mapFaceWind, inFaceWind
		);
	}
	else {
		pNewEntry->inCorner = (I8)pInCorner->idx;
		pNewEntry->onInVert = false;
	}
	pNewEntry->ancestor =
		appendToAncestors(&pBasic->pCtx->alloc, pAncestors, pCornerBuf->buf + i);
	pNewEntry->ancestorNext =
		appendToAncestors(&pBasic->pCtx->alloc, pAncestors, pCornerBuf->buf + iNext);
	pNewEntry->isMapCorner = false;
	pNewEntry->mapCorner = pCornerBuf->buf[i].mapCorner;
	if (pCornerBuf->buf[i].onLine && pCornerBuf->buf[iNext].onLine) {
		pNewEntry->onLine = true;
	}
	ptBuf[*pCount] = pNewEntry->alpha;
	++*pCount;
	pNewCornerBuf->count++;
}

static
void initPendingMerge(const StucAlloc *pAlloc, CornerBufWrap *pIsland) {
	pIsland->mergeSize = 3;
	pIsland->pPendingMerge = pAlloc->pCalloc(pIsland->mergeSize, sizeof(void *));
	pIsland->pPendingMerge[0] = -1;
	pIsland->mergeCount = 1;
}

static
Result addToPendingMerge(const StucAlloc *pAlloc, CornerBufWrap *pIsland, I32 value) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", pIsland->pPendingMerge);
	STUC_ASSERT("", pIsland->mergeSize > 0);
	pIsland->pPendingMerge[pIsland->mergeCount] = value;
	pIsland->mergeCount++;
	if (pIsland->mergeCount == pIsland->mergeSize) {
		pIsland->mergeSize *= 2;
		pIsland->pPendingMerge = pAlloc->pRealloc(
			pIsland->pPendingMerge,
			pIsland->mergeSize * sizeof(void *)
		);
	}
	return err;
}

static
Result destroyPendingMerge(const StucAlloc *pAlloc, CornerBufWrap *pIsland) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", pIsland->mergeSize > 0);
	STUC_ASSERT("", pIsland->mergeCount > 0);
	if (pIsland->pPendingMerge) {
		pAlloc->pFree(pIsland->pPendingMerge);
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
	CornerBufWrap *pIsland = pCornerPair->pPending ?
		pCornerPair->pPending : pCornerPair->pIsland;
	if (pCornerPairNext->pPending == pIsland) {
		//already listed
		return err;
	}
	CornerBufWrap *pIslandNext = pCornerPairNext->pIsland;
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
Result mergeIslands(CornerBufWrap *pIsland, IslandIdxPair *pIntersectCache) {
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
		CornerBufWrap *pIslandPending = pIntersectCache[idxPending].pIsland;
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
Result setIsland(
	const MappingJobState *pState,
	CornerBufWrap **ppIsland,
	CornerBufWrap *pRoot,
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
			err = stucLinAlloc(pState->pCornerBufWrapAlloc, &(*ppIsland)->pNext, 1);
			STUC_RETURN_ERR_IFNOT(err, "");
			*ppIsland = (*ppIsland)->pNext;
			(*ppIsland)->lastInCorner = inCorner;
			(*ppIsland)->lastInCorner += mapFaceWind ? 1 : -1;
		}
		*pIn = true;
	}
	return err;
}

typedef struct {
	F32 *pArr;
	I32 *pIndices;
	I32 size;
	I32 count;
} Segments;

static
void setSegment(IslandIdxPair *pCornerPair, Segments *pSegments, I32 inCorner) {
	CornerBuf *pCorner = pCornerPair->pIsland->buf + pCornerPair->corner;
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
				pAlloc->pRealloc(pSegEntry->pArr, pSegEntry->size * sizeof(F32));
			pSegEntry->pIndices =
				pAlloc->pRealloc(pSegEntry->pIndices, pSegEntry->size * sizeof(I32));
			memset(pSegEntry->pArr + pSegEntry->count, 0, sizeof(F32) * oldSize);
			memset(pSegEntry->pIndices + pSegEntry->count, 0, sizeof(I32) * oldSize);
		}
	}
}

//set inside to same as last non on-line corner
static
void modifyOnLineStatus(
	CornerBufWrap *pCornerBuf,
	InsideCache *pInside,
	I32 corner
) {
	I32 prev = corner ? corner - 1 : pCornerBuf->count - 1;
	I32 next = (corner + 1) % pCornerBuf->count;
	if (pInside[prev].status != ON_LINE && pInside[next].status == ON_LINE) {
		pInside[corner].status = pInside[prev].status;
		pInside[corner].markAsOnLine = true;
	}
	else if (pInside[prev].status == ON_LINE && pInside[next].status != ON_LINE) {
		pInside[corner].status = pInside[next].status;
		pInside[corner].markAsOnLine = true;
	}
	else if (pInside[prev].status != ON_LINE) {
		//neither prev nor next are on-line
		//note we don't set markAsOnLine to true here. 
		if (pInside[prev].status == pInside[next].status) {
			//equal, so set to either
			pInside[corner].status = pInside[prev].status;
		}
		else {
			return;
			//one is out and the other is in
			//set to previous
			if (pCornerBuf->buf[next].isMapCorner) {
				pInside[corner].status = pInside[next].status;
			}
			else {
				pInside[corner].status = pInside[prev].status;
			}
			
		}
	}
	else {
		//if no cases are true, both prev and next are on-line, so keep corner as is
		return;
	}
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
bool snapPointToInEdge(const InCornerCache *pInCorner, V2_F32 *pPoint) {
	return snapPointToEdgeIntern(
		pInCorner->uv, pInCorner->uvNext,
		pInCorner->dir, pInCorner->len,
		pPoint
	);
}

static
InsideStatus isCornerInOrOut(
	CornerBuf *pCornerBuf,
	const InCornerCache *pInCorner,
	bool mapFaceWind, bool inFaceWind,
	bool snap
) {
	V2_F32 vert = *(V2_F32 *)&pCornerBuf->corner;
	V2_F32 dir = _(vert V2SUB pInCorner->uv);
	V2_F32 dirUnit = _(dir V2DIVS pInCorner->len);
	F32 t = _(dirUnit V2DOT pInCorner->dirUnit);
	V2_F32 projPt = _(pInCorner->uv V2ADD _(pInCorner->dir V2MULS t));
	F32 len = v2F32Len(_(vert V2SUB projPt));
	if (len < .000001) {
		return ON_LINE;
	}
	F32 dot = _(pInCorner->cross V2DOT dir);
	if (dot == .0f) {
		return ON_LINE;
	}
	else {
		return (dot < .0f) ^ ((bool)mapFaceWind ^ (bool)inFaceWind) ?
			INSIDE : OUTSIDE;
	}
}

static
Result clipMapFaceAgainstCorner(
	MappingJobState *pState,
	FaceRange *pMapFace,
	CornerBufWrap *pCornerBuf,
	CornerBufWrap *pNewCornerBuf,
	InsideCache *pInside,
	InCornerCache *pInCorner,
	bool mapFaceWind, bool inFaceWind,
	Segments *pSegments,
	CornerAncestors *pAncestors
) {
	Result err = STUC_SUCCESS;
	for (I32 i = 0; i < pCornerBuf->count; ++i) {
		pInside[i].status = 
			isCornerInOrOut(
				pCornerBuf->buf + i,
				pInCorner,
				mapFaceWind, inFaceWind,
				true
			);
	}
	for (I32 i = 0; i < pCornerBuf->count; ++i) {
		if (pInside[i].status == ON_LINE &&
			!pCornerBuf->buf[i].isMapCorner &&
			pCornerBuf->buf[i].inCorner != pInCorner->idxPrev
		) {
			//only map corners can be on-line
			//modifyOnLineStatus(pCornerBuf, pInside, i);
		}
	}
	bool in = false;
	CornerBufWrap *pIsland = NULL;
	//32 is the max intersections possible with a 32 vert map face
	IslandIdxPair intersectCache[32] = {0};
	F32 tBuf[33] = {-FLT_MAX}; //first element must be low for later sorting
	F32 *ptBuf = tBuf + 1;
	I32 count = 0;
	for (I32 i = 0; i < pCornerBuf->count; ++i) {
		I32 iNext = (i + 1) % pCornerBuf->count;
		I32 iPrev = i ? i - 1 : pCornerBuf->count - 1;
		if (pInside[i].status & (INSIDE | ON_LINE)) {
			//point is inside, or on the line
			err = setIsland(
				pState,
				&pIsland,
				pNewCornerBuf,
				&in,
				pInCorner->idx,
				mapFaceWind
			);
			STUC_RETURN_ERR_IFNOT(err, "");
			addInsideCornerToBuf(
				pIsland,
				pCornerBuf,
				pInside,
				i, iNext, iPrev,
				pInCorner,
				intersectCache,
				ptBuf,
				&count,
				mapFaceWind, inFaceWind
			);
		}
		else if (in) {
			in = false;
		}
		if ((pInside[i].status != OUTSIDE) ^ (pInside[iNext].status != OUTSIDE) &&
			pInside[i].status != ON_LINE && pInside[iNext].status != ON_LINE) {

			//the current point is inside, but the next is not (or visa versa),
			//so calc intersection point. The != account for the
			//fact that insideBuf can be negative if the point is on the line.
			err = setIsland(
				pState,
				&pIsland,
				pNewCornerBuf,
				&in,
				pInCorner->idx,
				mapFaceWind
			);
			STUC_RETURN_ERR_IFNOT(err, "");
			addIntersectionToBuf(
				pState->pBasic,
				pMapFace,
				pIsland,
				pCornerBuf,
				i,
				iNext,
				pInCorner,
				intersectCache,
				ptBuf,
				&count,
				mapFaceWind, inFaceWind,
				pAncestors
			);
			pIsland->edgeFace = true;
		}
	}
	pIsland = pNewCornerBuf; //reset to root
	if (!pIsland || count == 0) {
		return err;
	}
	STUC_ASSERT("", count >= 2);
	STUC_ASSERT("should be even", !(count % 2));
	I32 idxTable[65] = {-1}; //first element to point to first tbuf element
	I32 *pIdxTable = idxTable + 1;
	if (pInCorner->flipEdgeDir) {
		for (I32 i = 0; i < count; ++i) {
			ptBuf[i] = 1.0f - ptBuf[i];
		}
	}
	stucFInsertionSort(pIdxTable, count, ptBuf);
	I32 inCorner = pInCorner->idx;
	if (!pIsland->pNext) {
		setSegments(
			&pState->pBasic->pCtx->alloc,
			ptBuf,
			pSegments,
			intersectCache,
			inCorner,
			pIdxTable[0],
			pIdxTable[1]
		);
		return err;
	}
	for (I32 i = 0; i < count; i += 2) {
		I32 reali = pIdxTable[i];
		I32 realiNext = pIdxTable[i + 1];
		IslandIdxPair *pCorner = intersectCache + reali;
		IslandIdxPair *pCornerNext = intersectCache + realiNext;
		setSegments(
			&pState->pBasic->pCtx->alloc,
			ptBuf,
			pSegments,
			intersectCache,
			inCorner,
			reali,
			realiNext
		);
		if (pCorner->pIsland != pCornerNext->pIsland) {
			bool flip = reali > realiNext;
			if (flip) {
				IslandIdxPair *pBuf = pCornerNext;
				pCornerNext = pCorner;
				pCorner = pBuf;
				realiNext = reali;
			}
			err = addIslandToPendingMerge(
				&pState->pBasic->pCtx->alloc,
				pCorner,
				pCornerNext,
				realiNext,
				intersectCache,
				count
			);
			STUC_RETURN_ERR_IFNOT(err, "");
		}
	}
	do {
		if (!pIsland->invalid && pIsland->pPendingMerge) {
			err = mergeIslands(pIsland, intersectCache);
			STUC_RETURN_ERR_IFNOT(err, "");
			err = destroyPendingMerge(&pState->pBasic->pCtx->alloc, pIsland);
			STUC_RETURN_ERR_IFNOT(err, "");
		}
		pIsland = pIsland->pNext;
	} while(pIsland);
	return err;
}

static
void cornerBufDecrementBaseCorners(CornerBufWrap* pCornerBuf, FaceRange* pInFace) {
	for (int i = 0; i < pCornerBuf->count; ++i) {
		I8* pBaseCorner = &pCornerBuf->buf[i].inCorner;
		*pBaseCorner = *pBaseCorner ? *pBaseCorner - (I8)1 : (I8)(pInFace->size - 1);
	}
}

static
void mergeDupCorners(
	CornerBufWrap *pCornerBuf,
	InCornerCache *pInCornerCache,
	bool mapFaceWind, bool inFaceWind
) {
	for (I32 i = 0; i < pCornerBuf->count; ++i) {
		I32 iNext = (i + 1) % pCornerBuf->count;
		I32 iPrev = i ? i - 1 : pCornerBuf->count - 1;
		CornerBuf *pCorner = pCornerBuf->buf + i;
		CornerBuf *pCornerNext = pCornerBuf->buf + iNext;
		CornerBuf *pCornerPrev = pCornerBuf->buf + iPrev;
		bool merge = false;
		if (!pCorner->isMapCorner && pCorner->onInVert && !pCornerNext->onInVert) {
			InCornerCache *pInCornerPrev =
				pInCornerCache + pInCornerCache[pCorner->inCorner].idxPrev;
			InsideStatus status =
				isCornerInOrOut(
					pCornerNext,
					pInCornerPrev,
					mapFaceWind, inFaceWind,
					false
				);
			merge = status == ON_LINE || _(pCorner->corner V3EQL pCornerNext->corner);
		}
		else if (!pCornerNext->isMapCorner && pCornerNext->onInVert && !pCorner->onInVert) {
			InCornerCache *pInCornerNext =
				pInCornerCache + pInCornerCache[pCorner->inCorner].idxNext;
			 InsideStatus status =
				 isCornerInOrOut(
					 pCorner,
					 pInCornerNext,
					 mapFaceWind, inFaceWind,
					 false
				 );
			 merge = status == ON_LINE || _(pCorner->corner V3EQL pCornerNext->corner);
		}
		if (!merge) {
			continue;
		}
		for (I32 j = i; j < pCornerBuf->count - 1; ++j) {
			pCornerBuf->buf[j] = pCornerBuf->buf[j + 1];
		}
		pCornerBuf->count--;
		i--;
	}
}

static
Result clipMapFaceAgainstInFace(
	MappingJobState *pState,
	FaceBounds *pInBounds,
	FaceRange *pMapFace,
	FaceRange *pInFace,
	InCornerCache *pInCornerCache,
	CornerBufWrap *pCornerBuf,
	bool mapFaceWind, bool inFaceWind,
	Segments *pSegments,
	CornerAncestors *pAncestors
) {
	Result err = STUC_SUCCESS;
	I32 start = pCornerBuf->lastInCorner;
	for (I32 i = start; mapFaceWind ? i < pInFace->size : i >= 0; mapFaceWind ? ++i : --i) {
		CornerBufWrap newCornerBuf = {
			.edgeFace = pCornerBuf->edgeFace,
			.onLine = pCornerBuf->onLine
		};
		InsideCache insideCache[65] = {0};
		err = clipMapFaceAgainstCorner(
			pState,
			pMapFace,
			pCornerBuf,
			&newCornerBuf,
			insideCache,
			pInCornerCache + i,
			mapFaceWind, inFaceWind,
			pSegments,
			pAncestors
		);
		STUC_RETURN_ERR_IFNOT(err, "");

		if (newCornerBuf.count <= 2) {
			pCornerBuf->count = newCornerBuf.count;
			return err;
		}
		CornerBufWrap *pTail = &newCornerBuf;
		while (pTail->pNext) {
			pTail = pTail->pNext;
		}
		pTail->pNext = pCornerBuf->pNext;
		*pCornerBuf = newCornerBuf;
	}
	if (!mapFaceWind) {
		cornerBufDecrementBaseCorners(pCornerBuf, pInFace);
	}
	mergeDupCorners(pCornerBuf, pInCornerCache, mapFaceWind, inFaceWind);
	return err;
}

static
V3_F32 getCornerRealNormal(Mesh *pMesh, FaceRange *pFace, I32 corner) {
	I32 a = corner == 0 ? pFace->size - 1 : corner - 1;
	I32 c = (corner + 1) % pFace->size;
	I32 aIdx = pMesh->core.pCorners[pFace->start + a];
	I32 bIdx = pMesh->core.pCorners[pFace->start + corner];
	I32 cIdx = pMesh->core.pCorners[pFace->start + c];
	V3_F32 ba = _(pMesh->pVerts[aIdx] V3SUB pMesh->pVerts[bIdx]);
	V3_F32 bc = _(pMesh->pVerts[cIdx] V3SUB pMesh->pVerts[bIdx]);
	return v3F32Normalize(_(ba V3CROSS bc));
}

static
void handleVertIfInUsg(
	MappingJobState *pState,
	CornerBuf *pCorner,
	FaceRange *pMapFace,
	FaceRange *pInFace,
	V2_F32 tileMin,
	bool *pAboveCutoff
) {
	UsgInFace *pUsgEntry = stucGetUsgForCorner(
		pCorner->mapCorner,
		pState->pBasic->pMap,
		pMapFace,
		pInFace->idx,
		pAboveCutoff
	);
	if (pUsgEntry) {
		if (*pAboveCutoff) {
			stucUsgVertTransform(
				pUsgEntry,
				pCorner->uvw,
				&pCorner->cornerFlat,
				pState->pBasic->pInMesh,
				tileMin,
				&pCorner->tbn
			);
		}
		stucUsgVertSetNormal(pUsgEntry, &pCorner->projNormal);
	}
}

static
void transformClippedFaceFromUvToXyz(
	CornerBufWrap *pCornerBuf,
	FaceRange *pMapFace,
	FaceRange *pInFace,
	const BaseTriVerts *pInTri,
	MappingJobState *pState,
	V2_F32 tileMin,
	F32 wScale
) {
	const Mesh *pMapMesh = pState->pBasic->pMap->pMesh;
	//replace j, k, l, etc, in code that was moved to a func, but not updated,
	//eg, the below corner should use i, not j
	for (I32 j = 0; j < pCornerBuf->count; ++j) {
		CornerBuf *pCorner = pCornerBuf->buf + j;
		pCorner->uvw = pCorner->corner;
		//uv is just the vert position before transform, so set that here
		pCorner->uv = *(V2_F32 *)&pCorner->uvw;
		//find enclosing triangle
		_((V2_F32 *)&pCorner->uvw V2SUBEQL tileMin);
		V3_F32 vertBc = stucGetBarycentricInFace(
			pInTri->uv,
			pCorner->triCorners,
			pInFace->size,
			*(V2_F32 *)&pCorner->uvw
		);
		I8 *pTriCorners = pCorner->triCorners;
		V3_F32 vertsXyz[3] = {0};
		I32 inVerts[3] = {0};
		for (I32 i = 0; i < 3; ++i) {
			inVerts[i] = pState->pBasic->pInMesh->core.pCorners[pInFace->start + pTriCorners[i]];
			vertsXyz[i] = pState->pBasic->pInMesh->pVerts[inVerts[i]];
		}
		pCorner->bc = vertBc;
		pCorner->tbn = stucGetInterpolatedTbn(pState->pBasic->pInMesh, pInFace, pTriCorners, vertBc);
		F32 inVertsWScaleMul = 1.0;
		if (pState->pBasic->pInMesh->pWScale) {
			Attrib wScaleWrap = {
				.core.pData = &inVertsWScaleMul,
				.interpolate = true,
				.core.type = STUC_ATTRIB_F32
			};
			stucTriInterpolateAttrib(
				&wScaleWrap,
				0,
				stucGetActiveAttribConst(
					pState->pBasic->pCtx,
					&pState->pBasic->pInMesh->core,
					STUC_ATTRIB_USE_WSCALE
				),
				inVerts[0],
				inVerts[1],
				inVerts[2],
				vertBc
			);
		}
		pCorner->uvw.d[2] *= inVertsWScaleMul;
		pCorner->inTangent = *(V3_F32 *)&pCorner->tbn.d[0];
		pCorner->projNormal = *(V3_F32 *)&pCorner->tbn.d[2];
		pCorner->inTSign =
			pState->pBasic->pInMesh->pTSigns[pInFace->start + pTriCorners[0]];
		bool aboveCutoff = false;
		if (pMapMesh->pUsg && pCorner->isMapCorner) {
			handleVertIfInUsg(pState, pCorner, pMapFace, pInFace, tileMin, &aboveCutoff);
		}
		if (!aboveCutoff) {
			pCorner->cornerFlat = barycentricToCartesian(vertsXyz, &vertBc);
		}
		if (pCorner->isMapCorner) {
			pCorner->corner =
				_(pCorner->cornerFlat V3ADD
				_(pCorner->projNormal V3MULS pCorner->uvw.d[2] * wScale));
			pCornerBuf->buf[j].normal = _(pCornerBuf->buf[j].normal V3MULM3X3 &pCorner->tbn);
		}
		else {
			//offset and normal transform will be deferred to combine stage,
			//to allow for interpolation of usg normals.
			//W will be added to the corner in the add-to-face function after this func
			pCorner->corner = pCorner->cornerFlat;
		}
	}
}

static
void lerpIntersect(
	CornerBuf *pCorner,
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
		CornerBuf *pAncestor = pAncestors->pArr + pCorner->ancestor;
		CornerBuf *pAncestorNext = pAncestors->pArr + pCorner->ancestorNext;
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
			pCorner->mapAlpha
		);
	}
}

static
void blendCommonAttrib(
	MappingJobState *pState,
	StucDomain domain,
	Attrib *pBufAttrib,
	const Attrib *pInAttrib,
	const Attrib *pMapAttrib,
	CornerBuf *pCornerBuf,
	I32 cornerBufIdx,
	I32 dataIdx,
	I32 mapDataIdx,
	I32 inDataIdx,
	FaceRange *pInFace
) {
	STUC_ASSERT("", pInAttrib && pMapAttrib);
	U8 mapDataBuf[STUC_ATTRIB_STRING_MAX_LEN] = {0};
	StucAttrib mapBuf = {.core.pData = mapDataBuf, .core.type = pMapAttrib->core.type};
	U8 inDataBuf[STUC_ATTRIB_STRING_MAX_LEN] = {0};
	StucAttrib inBuf = {.core.pData = inDataBuf, .core.type = pInAttrib->core.type};
	//TODO remove 'false' once interpolation is implemented for map attribs
	if (false && pMapAttrib->interpolate) {
		//TODO add correct map interpolation. to do this, you'll need
		//to triangulate the face, like with the Mesh in face, and you''
		//need to get baerycentry coords for in corners (not necessary
		//for intersection points, can just lerp in the clipping function).
		//so to summarise, only in corners will be interpolated here,
		//intersection corners will be lerped at clipping stage,
		//and map corners obviously don't need interpolation
	}
	else {
		memcpy(mapBuf.core.pData, stucAttribAsVoidConst(&pMapAttrib->core, mapDataIdx),
		       stucGetAttribSizeIntern(pMapAttrib->core.type));
	}
	if (pInAttrib->interpolate) {
		//TODO skip interlopation if in corner? is it worth it? profile.
		stucTriInterpolateAttrib(
			&inBuf,
			0,
			pInAttrib,
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[0],
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[1],
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[2],
			pCornerBuf[cornerBufIdx].bc
		);
	}
	else {
		memcpy(inBuf.core.pData, stucAttribAsVoidConst(&pInAttrib->core, inDataIdx),
		       stucGetAttribSizeIntern(pInAttrib->core.type));
	}
	const StucCommonAttrib *pCommon = stucGetCommonAttribFromDomain(
		pState->pBasic->pCommonAttribList,
		pBufAttrib->core.name,
		domain
	);
	StucBlendConfig blendConfig = {0};
	if (pCommon) {
			blendConfig = pCommon->blendConfig;
	}
	else {
		StucTypeDefault *pDefault = stucGetTypeDefaultConfig(
			&pState->pBasic->pCtx->typeDefaults,
			pInAttrib->core.type
		);
		blendConfig = pDefault->blendConfig;
	}
	StucAttrib *orderTable[2] = {0};
	I8 order = blendConfig.order;
	orderTable[0] = order ? &mapBuf : &inBuf;
	orderTable[1] = !order ? &mapBuf : &inBuf;
	stucBlendAttribs(
		&pBufAttrib->core,
		dataIdx,
		&orderTable[0]->core,
		0,
		&orderTable[1]->core,
		0,
		blendConfig
	);
}

static
void interpolateMapAttrib(
	StucDomain domain,
	Attrib *pBufAttrib,
	const Attrib *pMapAttrib,
	CornerBuf *pCorner,
	I32 dataIdx,
	const FaceRange *pMapFace,
	CornerAncestors *pAncestors,
	I32 mapDataIdx
) {
	STUC_ASSERT("", pMapAttrib);
	if (pMapAttrib->interpolate && domain == STUC_DOMAIN_CORNER) {
		lerpIntersect(pCorner, pBufAttrib, dataIdx, pMapAttrib, pMapFace, pAncestors);
	}
	else {
		memcpy(stucAttribAsVoid(&pBufAttrib->core, dataIdx),
		       stucAttribAsVoidConst(&pMapAttrib->core, mapDataIdx),
		       stucGetAttribSizeIntern(pMapAttrib->core.type));
	}
}

static
void interpolateInMeshAttrib(
	Attrib *pBufAttrib,
	const Attrib *pInAttrib,
	I32 dataIdx,
	const FaceRange *pInFace,
	CornerBuf *pCornerBuf,
	I32 cornerBufIdx,
	I32 inDataIdx
) {
	STUC_ASSERT("", pInAttrib);
	if (pInAttrib->interpolate) {
		//TODO skip interlopation is in corner? is it worth it? profile.
		stucTriInterpolateAttrib(
			pBufAttrib,
			dataIdx,
			pInAttrib,
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[0],
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[1],
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[2],
			pCornerBuf[cornerBufIdx].bc
		);
	}
	else {
		memcpy(stucAttribAsVoid(&pBufAttrib->core, dataIdx),
		       stucAttribAsVoidConst(&pInAttrib->core, inDataIdx),
		       stucGetAttribSizeIntern(pInAttrib->core.type));
	}
}

//NOTE map and mesh date idx params are only used if interpolation is not enabled
//for the attrib. This is always the case on faces.
//Except for right now, because I havn't implemented map triangulation and interpolation,
//so the map data idx is used temporarily until that's done.
static
void blendMapAndInAttribs(
	MappingJobState *pState,
	StucDomain domain,
	CornerBuf *pCornerBuf,
	I32 cornerBufIdx,
	I32 dataIdx,
	I32 mapDataIdx,
	I32 inDataIdx,
	FaceRange *pInFace,
	FaceRange *pMapFace,
	CornerAncestors *pAncestors
) {
	StucContext pCtx = pState->pBasic->pCtx;
	BufMesh *pBufMesh = &pState->bufMesh;
	CornerBuf *pCorner = pCornerBuf + cornerBufIdx;
	AttribArray *pBufAttribArr =
		stucGetAttribArrFromDomain(&pBufMesh->mesh.core, domain);
	const AttribArray *pMapAttribArr =
		stucGetAttribArrFromDomainConst(&pState->pBasic->pMap->pMesh->core, domain);
	const AttribArray *pInAttribArr =
		stucGetAttribArrFromDomainConst(&pState->pBasic->pInMesh->core, domain);
	for (I32 i = 0; i < pBufAttribArr->count; ++i) {
		Attrib *pBufAttrib = pBufAttribArr->pArr + i;
		if (pBufAttrib == stucGetActiveAttrib(pCtx, &pBufMesh->mesh.core, STUC_ATTRIB_USE_POS) ||
			pBufAttrib == stucGetActiveAttrib(pCtx, &pBufMesh->mesh.core, STUC_ATTRIB_USE_UV) ||
			pBufAttrib == stucGetActiveAttrib(pCtx, &pBufMesh->mesh.core, STUC_ATTRIB_USE_NORMAL)) {

			//these active attribs shouldn't be blended or interpolated,
			//they're processed/interpolated manually
			//TODO maybe uv and normal should be blended, to allow one to choose between transformed
			//map normals, or uvs (rather than having a separate Map_UVMap attrib).
			//the blend system needs to be overhualed regardless to take account of use,
			//so non-active normal attributes are transformed correctly for instance.
			//once that's done, you should be able to just send the active normal through here
			continue;
		}
		Result err = STUC_SUCCESS;
		const StucAttrib *pInAttrib = NULL;
		err = stucGetMatchingAttribConst(
			pState->pBasic->pCtx,
			&pState->pBasic->pInMesh->core, pInAttribArr,
			&pBufMesh->mesh.core, pBufAttrib,
			true,
			false,
			&pInAttrib
		);
		STUC_ASSERT("", err == STUC_SUCCESS);
		const StucAttrib *pMapAttrib = NULL;
		stucGetMatchingAttribConst(
			pState->pBasic->pCtx,
			&pState->pBasic->pMap->pMesh->core, pMapAttribArr,
			&pBufMesh->mesh.core, pBufAttrib,
			true,
			false,
			&pMapAttrib
		);
		STUC_ASSERT("", err == STUC_SUCCESS);
		switch (pBufAttrib->origin) {
			case STUC_ATTRIB_ORIGIN_COMMON:
				blendCommonAttrib(
					pState,
					domain,
					pBufAttrib,
					pInAttrib,
					pMapAttrib,
					pCornerBuf,
					cornerBufIdx,
					dataIdx,
					mapDataIdx,
					inDataIdx,
					pInFace
				);
				break;
			case STUC_ATTRIB_ORIGIN_MAP:
				interpolateMapAttrib(
					domain,
					pBufAttrib,
					pMapAttrib,
					pCorner,
					dataIdx,
					pMapFace,
					pAncestors,
					mapDataIdx
				);
				break;
			case STUC_ATTRIB_ORIGIN_MESH_IN:
				interpolateInMeshAttrib(
					pBufAttrib,
					pInAttrib,
					dataIdx,
					pInFace,
					pCornerBuf,
					cornerBufIdx,
					inDataIdx
				);
				break;
		}
	}
}

static
void blendMapAndInFaceAttribs(
	MappingJobState *pState,
	CornerBuf *pCornerBuf,
	I32 dataIdx,
	FaceRange *pInFace,
	FaceRange *pMapFace
) {
	blendMapAndInAttribs(
		pState,
		STUC_DOMAIN_FACE,
		pCornerBuf,
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
	CornerBuf *pCornerBuf,
	I32 cornerBufIdx,
	I32 dataIdx,
	FaceRange *pInFace,
	FaceRange *pMapFace,
	CornerAncestors *pAncestors
) {
	blendMapAndInAttribs(
		pState,
		STUC_DOMAIN_CORNER,
		pCornerBuf,
		cornerBufIdx,
		dataIdx,
		pMapFace->start + pCornerBuf[cornerBufIdx].mapCorner,
		pInFace->start,
		pInFace,
		pMapFace,
		pAncestors
	);
}

static
void blendMapAndInVertAttribs(
	MappingJobState *pState,
	CornerBuf *pCornerBuf,
	I32 cornerBufIdx,
	I32 dataIdx,
	FaceRange *pInFace,
	FaceRange *pMapFace
) {
	blendMapAndInAttribs(
		pState,
		STUC_DOMAIN_VERT,
		pCornerBuf,
		cornerBufIdx,
		dataIdx,
		pCornerBuf[cornerBufIdx].mapCorner,
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
	CornerBuf *pCornerBuf,
	I32 cornerBufIdx
) {
	if (pCornerBuf[cornerBufIdx].isMapCorner) {
		I32 stucCorner = pCornerBuf[cornerBufIdx].mapCorner;
		return pState->pBasic->pMap->pMesh->core.pEdges[pMapFace->start + stucCorner];
	}
	else {
		I32 baseCorner = pCornerBuf[cornerBufIdx].inCorner;
		return pState->pBasic->pInMesh->core.pEdges[pInFace->start + baseCorner];
	}
}

static
void addEdge(
	MappingJobState *pState,
	I32 cornerBufIdx,
	BufMesh *pBufMesh,
	CornerBuf *pCornerBuf,
	I32 refFace,
	I32 *pBufEdge,
	FaceRange *pInFace,
	FaceRange *pMapFace
) {
	I32 refEdge = getRefEdge(pState, pMapFace, pInFace, pCornerBuf, cornerBufIdx);
	I32 isMapEdge = pCornerBuf[cornerBufIdx].isMapCorner;
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
	I32 cornerBufIdx,
	I32 *pBufVert,
	BufMesh *pBufMesh,
	CornerBuf *pCornerBuf,
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
		pBufMesh->mesh.pVerts[vert.realIdx] = pCornerBuf[cornerBufIdx].corner;
		//TODO temporarily setting mesh data idx to 0, as it's only needed if interpolation is disabled
		blendMapAndInVertAttribs(
			pState,
			pCornerBuf,
			cornerBufIdx,
			vert.realIdx,
			pInFace,
			pMapFace
		);
}

static
void initMapVertTableEntry(
	MappingJobState *pState,
	I32 cornerBufIdx,
	I32 *pBufVert,
	CornerBuf *pCornerBuf,
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
	pState->bufMesh.mesh.pVerts[vert.realIdx] = pCornerBuf[cornerBufIdx].corner;
	pEntry->vert = vert.idx;
	pEntry->mapVert = stucVert;
	pEntry->inFace = pInFace->idx;
	pEntry->tile = tile;
	blendMapAndInVertAttribs(
		pState,
		pCornerBuf,
		cornerBufIdx,
		vert.realIdx,
		pInFace,
		pMapFace
	);
}

static
void addStucCornerAndOrVert(
	MappingJobState *pState,
	I32 cornerBufIdx,
	I32 *pBufVert,
	CornerBuf *pCornerBufEntry,
	FaceRange *pInFace,
	FaceRange *pMapFace,
	V2_I32 tile
) {
	I32 stucCorner = pMapFace->start + pCornerBufEntry[cornerBufIdx].mapCorner;
	U32 uStucVert = pState->pBasic->pMap->pMesh->core.pCorners[stucCorner];
	I32 hash =
		stucFnvHash((U8 *)&uStucVert, 4, pState->localTables.vertTableSize);
	LocalVert *pEntry = pState->localTables.pVertTable + hash;
	do {
		if (!pEntry->cornerSize) {
			initMapVertTableEntry(
				pState,
				cornerBufIdx,
				pBufVert,
				pCornerBufEntry,
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
				cornerBufIdx,
				pBufVert,
				pCornerBufEntry,
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
	CornerBufWrap *pCornerBuf,
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

	STUC_ASSERT("", pCornerBuf->count <= 64);
	for (I32 i = 0; i < pCornerBuf->count; ++i) {
		CornerBuf *pCorner = pCornerBuf->buf + i;
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
	CornerBufWrap *pCornerBuf,
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
				pCornerBuf,
				pInFace,
				inFaceWind, mapFaceWind,
				memType,
				pSegments
			);
			break;
		}
		if (!pBucket->pNext) {
			pBucket = pBucket->pNext = pState->pBasic->pCtx->alloc.pCalloc(1, sizeof(BorderBucket));
			stucAllocBorderFace(memType, &pState->borderTableAlloc, &pBucket->pEntry);
			pEntry = pBucket->pEntry;
			initBorderTableEntry(
				pState,
				bufFace,
				pEntry,
				pMapFace,
				tile,
				pCornerBuf,
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
	CornerBufWrap *pCornerBuf,
	FaceRange *pMapFace,
	V2_I32 tile,
	FaceRange *pInFace,
	bool inFaceWind, bool mapFaceWind,
	Segments *pSegments
) {
	I32 memType = stucGetBorderFaceMemType(pMapFace->size, pCornerBuf->count);
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
			pCornerBuf,
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
			pCornerBuf,
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
	CornerBufWrap *pCornerBuf,
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
	bool isBorderFace = pCornerBuf->edgeFace || pCornerBuf->onLine;
	bool invert = !inFaceWind && !isBorderFace;
	I32 size = pCornerBuf->count;
	for (I32 i = invert ? size - 1 : 0;
		invert ? i >= 0 : i < size;
		invert ? --i : ++i) {

		I32 refFace;
		I32 isStuc = pCornerBuf->buf[i].isMapCorner;
		if (!isStuc || pCornerBuf->buf[i].onLine) {
			//TODO these only add verts, not corners. Outdated name?
			addNewCornerAndOrVert(
				pState,
				i,
				&bufVert,
				&pState->bufMesh,
				pCornerBuf->buf,
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
				pCornerBuf->buf,
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
		pBufMesh->pW[corner.realIdx] = pCornerBuf->buf[i].uvw.d[2];
		pBufMesh->pInNormal[corner.realIdx] = pCornerBuf->buf[i].projNormal;
		pBufMesh->pInTangent[corner.realIdx] = pCornerBuf->buf[i].inTangent;
		pBufMesh->pAlpha[corner.realIdx] = pCornerBuf->buf[i].alpha;
		pBufMesh->pInTSign[corner.realIdx] = pCornerBuf->buf[i].inTSign;
		pBufMesh->mesh.core.pCorners[corner.realIdx] = bufVert;
		pBufMesh->mesh.pNormals[corner.realIdx] = pCornerBuf->buf[i].normal;
		pBufMesh->mesh.pUvs[corner.realIdx] = pCornerBuf->buf[i].uv;
		//TODO add an intermediate function to shorten the arg lists in blendattrib functions
		blendMapAndInCornerAttribs(
			pState,
			pCornerBuf->buf,
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
			pCornerBuf->buf,
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
		pCornerBuf->buf,
		face.realIdx,
		pInFace,
		pMapFace
	);
	if (isBorderFace) {
		addFaceToBorderTable(
			pState,
			bufFace,
			pCornerBuf,
			pMapFace,
			tile,
			pInFace,
			inFaceWind, mapFaceWind,
			pSegments
		);
	}
}

static
bool isOnLine(CornerBufWrap *pCornerBuf) {
	for (I32 i = 0; i < pCornerBuf->count; ++i) {
		if (pCornerBuf->buf[i].onLine) {
			return true;
		}
	}
	return false;
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

static
void getCellMapFaces(
	MappingJobState *pState,
	FaceCells *pFaceCellsEntry,
	I32 faceCellsIdx,
	I32 **ppCellFaces,
	Range *pRange
) {
	STUC_ASSERT("", &pState->bufMesh.mesh.core.faceCount >= 0);
	STUC_ASSERT("",
		&pState->bufMesh.mesh.core.faceCount <
		&pState->bufMesh.mesh.faceBufSize
	);
	I32 cellIdx = pFaceCellsEntry->pCells[faceCellsIdx];
	Cell *pCell = pState->pBasic->pMap->quadTree.cellTable.pArr + cellIdx;
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
void resetSegments(Segments *pSegments, FaceRange *pInFace) {
	for (I32 i = 0; i < pInFace->size; ++i) {
		if (pSegments[i].count > 1) {
			memset(pSegments[i].pIndices + 1, 0, pSegments[i].count - 1);
			pSegments[i].count = 1;
		}
	}
}

static
void initCornerBuf(
	const StucMap pMap,
	V2_F32 fTileMin,
	CornerBufWrap *pCornerBuf,
	const FaceRange *pInFace,
	const FaceRange *pMapFace,
	bool mapFaceWind
) {
	STUC_ASSERT("", mapFaceWind % 2 == mapFaceWind);
	pCornerBuf->count = pMapFace->size;
	for (I32 k = 0; k < pMapFace->size; ++k) {
		I32 vertIdx = pMap->pMesh->core.pCorners[pMapFace->start + k];
		CornerBuf *pCorner = pCornerBuf->buf + k;
		pCorner->isMapCorner = 1;
		pCorner->inCorner = (I8)((vertIdx + 1) * -1);
		pCorner->corner = pMap->pMesh->pVerts[vertIdx];
		pCorner->corner.d[0] += fTileMin.d[0];
		pCorner->corner.d[1] += fTileMin.d[1];
		pCorner->mapCorner = (I8)k;
		pCorner->normal = pMap->pMesh->pNormals[pMapFace->start + k];
	}
	pCornerBuf->lastInCorner = mapFaceWind ? 0 : pInFace->size - 1;
}

static
void buildInCornerCache(
	MapToMeshBasic *pBasic,
	InCornerCache *pCache,
	FaceRange *pInFace,
	bool mapFaceWind
) {
	for (I32 i = 0; i < pInFace->size; ++i) {
		pCache[i].uv = pBasic->pInMesh->pUvs[pInFace->start + i];
		I32 idxNext = 0;
		I32 idxPrev = 0;
		I32 edgeCorner = 0;
		if (mapFaceWind) {
			idxNext = ((i + 1) % pInFace->size);
			idxPrev = i ? i - 1 : pInFace->size - 1;
			edgeCorner = pInFace->start + i;
		}
		else {
			idxNext = i ? i - 1 : pInFace->size - 1;
			idxPrev = ((i + 1) % pInFace->size);
			edgeCorner = pInFace->start + idxNext;
		}
		I32 edgeIdx = pBasic->pInMesh->core.pEdges[edgeCorner];
		V2_I32 edgeCorners = pBasic->pInMesh->pEdgeCorners[edgeIdx];
		STUC_ASSERT(
			"",
			edgeCorners.d[0] == edgeCorner || edgeCorners.d[1] == edgeCorner
		);
		pCache[i].flipEdgeDir = edgeCorner != edgeCorners.d[0];
		pCache[i].idx = (I8)i;
		pCache[i].idxPrev = (I8)idxPrev;
		pCache[i].idxNext = (I8)idxNext;
	}
}

static
void snapInVertsToMapEdges(
	const FaceRange *pInFace, InCornerCache *pInCorners,
	const CornerBufWrap *pMapCorners
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
		pInCorners[i].cross = v2F32Cross(pInCorners[i].dir);
	}
}

static
Result clipMapFaceIntoFaces(
	MappingJobState *pState,
	FaceBounds *pInBounds,
	CornerBufWrap *pCornerBuf,
	FaceRange *pMapFace,
	FaceRange *pInFace,
	bool mapFaceWind, bool inFaceWind,
	Segments *pSegments,
	CornerAncestors *pAncestors
) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", mapFaceWind % 2 == mapFaceWind && inFaceWind % 2 == inFaceWind);
	CornerBufWrap *pCornerBufPtr = pCornerBuf;
	InCornerCache inCornerCache[4] = {0};
	buildInCornerCache(pState->pBasic, inCornerCache, pInFace, mapFaceWind);
	snapInVertsToMapEdges(pInFace, inCornerCache, pCornerBuf);
	do {
		if (!pCornerBufPtr->invalid) {
			err = clipMapFaceAgainstInFace(
				pState,
				pInBounds,
				pMapFace,
				pInFace,
				inCornerCache,
				pCornerBufPtr,
				mapFaceWind, inFaceWind,
				pSegments,
				pAncestors
			);
			STUC_RETURN_ERR_IFNOT(err, "");
		}
		pCornerBufPtr = pCornerBufPtr->pNext;
	} while (pCornerBufPtr);
	return err;
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
	CornerBufWrap *pCornerBuf,
	V2_I32 tile,
	Segments *pSegments,
	CornerAncestors *pAncestors,
	FaceRange *pInFace,
	FaceRange *pMapFace,
	const BaseTriVerts *pInTri,
	bool inFaceWind, bool mapFaceWind
) {
	STUC_ASSERT("", mapFaceWind % 2 == mapFaceWind && inFaceWind % 2 == inFaceWind);
	CornerBufWrap *pCornerBufPtr = pCornerBuf;
	do{
		if (!pCornerBufPtr->invalid) {
			if (pCornerBufPtr->count >= 3) {
				//TODO move this after addClippedFaceToBufMesh,
				//     that way, you can skip merged stuc verts,
				//     and you can also remove the use of CornerBuf,
				//     and turn the func into a generic (generic for BufMesh)
				//     one that can be used on deffered corners in MergeBoundsFaces.c
				// Followup - Very low priority, the perf increase is minimal
				// (tested by commenting out the func)
				transformClippedFaceFromUvToXyz(
					pCornerBufPtr,
					pMapFace,
					pInFace,
					pInTri,
					pState,
					fTileMin,
					pState->pBasic->wScale
				);
				addClippedFaceToBufMesh(
					pState,
					pCornerBufPtr,
					pMapFace,
					tile,
					pInFace,
					inFaceWind, mapFaceWind,
					pSegments,
					pAncestors
				);
			}
		}
		pCornerBufPtr = pCornerBufPtr->pNext;
	} while(pCornerBufPtr);
}

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
		pInTri->xyz[i] = pInMesh->pVerts[pInMesh->core.pCorners[corner]];
	}
	stucGetTriScale(pInFace->size, pInTri);
}

Result stucMapToSingleFace(
	MappingJobState *pState,
	FaceCellsTable *pFaceCellsTable,
	V2_I32 tile,
	FaceRange *pInFace
) {
	Result err = STUC_SUCCESS;
	V2_F32 fTileMin = {(F32)tile.d[0], (F32)tile.d[1]};
	STUC_ASSERT("", pInFace->size == 3 || pInFace->size == 4);
	const StucAlloc *pAlloc = &pState->pBasic->pCtx->alloc;
	const Mesh *pInMesh = pState->pBasic->pInMesh;
	const StucMap pMap = pState->pBasic->pMap;
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
	I32 inFaceWind = stucCalcFaceOrientation(pInMesh, pInFace, true);
	if (inFaceWind == 2) {
		//face is degenerate - skip
		return err;
	}
	Segments *pSegments = pAlloc->pCalloc(pInFace->size, sizeof(Segments));
	for (I32 i = 0; i < pInFace->size; ++i) {
		pSegments[i].size = 3;
		pSegments[i].pArr = pAlloc->pCalloc(pSegments[i].size, sizeof(F32));
		pSegments[i].pIndices = pAlloc->pCalloc(pSegments[i].size, sizeof(I32));
		pSegments[i].pArr[0] = -FLT_MAX;
		pSegments[i].pIndices[0] = -1;
		pSegments[i].count = 1;
	}
	CornerAncestors ancestors = {.size = 2};
	ancestors.pArr = pAlloc->pMalloc(sizeof(CornerBuf) * ancestors.size);
	FaceCells *pFaceCellsEntry =
		stucIdxFaceCells(pFaceCellsTable, pInFace->idx, pState->inFaceRange.start);
	for (I32 i = 0; i < pFaceCellsEntry->cellSize; ++i) {
		I32 *pCellFaces = NULL;
		Range range = {0};
		getCellMapFaces(pState, pFaceCellsEntry, i, &pCellFaces, &range);
		for (I32 j = range.start; j < range.end; ++j) {
			pState->facesChecked++;
			FaceRange mapFace =
				stucGetFaceRange(&pMap->pMesh->core, pCellFaces[j], false);
			if (!stucCheckFaceIsInBounds(
				_(bounds.fMin V2SUB fTileMin),
				_(bounds.fMax V2SUB fTileMin),
				mapFace,
				pMap->pMesh)
				) {
				continue;
			}
			pState->facesUsed++;
			resetSegments(pSegments, pInFace);
			I32 mapFaceWind = stucCalcFaceOrientation(pMap->pMesh, &mapFace, false);
			STUC_THROW_IFNOT_COND(
				err,
				mapFaceWind != 2,
				"invalid map face, this should have been caught in export/ import",
				0
			);
			CornerBufWrap cornerBuf = {0};
			initCornerBuf(pMap, fTileMin, &cornerBuf, pInFace, &mapFace, mapFaceWind);
			ancestors.count = 0;
			err = clipMapFaceIntoFaces(
				pState,
				&bounds,
				&cornerBuf,
				&mapFace,
				pInFace,
				mapFaceWind, inFaceWind,
				pSegments,
				&ancestors
			);
			STUC_THROW_IFNOT(err, "", 0);
			sortSegments(pSegments, pInFace);
			addOrDiscardClippedFaces(
				pState,
				fTileMin,
				&cornerBuf,
				tile,
				pSegments,
				&ancestors,
				pInFace,
				&mapFace,
				pInTriConst,
				inFaceWind, mapFaceWind
			);
			err = stucLinAllocClear(pState->pCornerBufWrapAlloc, true);
			STUC_THROW_IFNOT(err, "", 0);
		}
	}
	STUC_CATCH(0, err, ;);
	pAlloc->pFree(ancestors.pArr);
	for (I32 i = 0; i < pInFace->size; ++i) {
		pAlloc->pFree(pSegments[i].pArr);
		pAlloc->pFree(pSegments[i].pIndices);
	}
	pAlloc->pFree(pSegments);
	return STUC_SUCCESS;
}
