#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include <float.h>

#include <Context.h>
#include <MapToJobMesh.h>
#include <MapFile.h>
#include <MathUtils.h>
#include <AttribUtils.h>
#include <Utils.h>
#include <Error.h>
#include <Alloc.h>

#define FLOAT_BC_MARGIN .0001f

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

typedef struct {
	V2_F32 vert;
	V2_F32 vertNext;
	V2_F32 dir;
	V2_F32 dirBack;
	I32 idx;
	I32 edgeIdx;
	I32 idxNext;
	I8 localIdx;
	I8 localIdxPrev;
	I8 localIdxNext;
	bool flipEdgeDir;
} CornerInfo;

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

static
bool checkIfOnVert(CornerBufWrap *pCornerBuf, I32 i, I32 iNext) {
	return (pCornerBuf->buf[i].baseCorner == pCornerBuf->buf[iNext].baseCorner ||
	        pCornerBuf->buf[i].isBaseCorner || pCornerBuf->buf[iNext].isBaseCorner) &&
	        !pCornerBuf->buf[i].isStuc && !pCornerBuf->buf[iNext].isStuc;
}

static
void addInsideCornerToBuf(
	CornerBufWrap *pNewCornerBuf,
	CornerBufWrap *pCornerBuf,
	I32 *pInsideBuf,
	I32 i,
	I32 iNext,
	I32 iPrev,
	CornerInfo *pBaseCorner,
	IslandIdxPair *pIntersectCache,
	F32 *ptBuf,
	I32 *pCount
) {
	CornerBuf *pNewEntry = pNewCornerBuf->buf + pNewCornerBuf->size;
	pNewCornerBuf->buf[pNewCornerBuf->size] = pCornerBuf->buf[i];
	if (pInsideBuf[i] == 2) {
		//pNewCornerBuf->buf[pNewCornerBuf->size].onLine = true;
	}
	//using += so that base corners can be determined. ie, if an stuc
	//vert has a dot of 0 twice, then it is sitting on a base vert,
	//but if once, then it's sitting on an edge.
	if (pInsideBuf[i] < 0) {
		//is on line
		if ((pInsideBuf[iPrev] == 0 && pInsideBuf[iNext] == 1) ||
			(pInsideBuf[iPrev] == 1 && pInsideBuf[iNext] == 0)) {
			//add to intersection but
			pIntersectCache[*pCount].pIsland = pNewCornerBuf;
			pIntersectCache[*pCount].corner = pNewCornerBuf->size;
			CornerBuf *pCorner = pCornerBuf->buf + i;
			CornerBuf *pCornerNext = pCornerBuf->buf + iNext;
			stucCalcIntersection(
				pCorner->corner,
				pCornerNext->corner,
				pBaseCorner->vert,
				pBaseCorner->dir,
				NULL,
				NULL,
				&pNewEntry->alpha
			);
			pNewEntry->alpha *= -1.0f;
			ptBuf[*pCount] = pNewEntry->alpha;
			++*pCount;
		}
		if (pCornerBuf->buf[i].onLine) {
			//this corner already resided on a previous base edge,
			//it must then reside on a base vert, rather than an edge.
			//determine which vert in the edge it sits on:
			I32 onLineBase;
			if (pCornerBuf->buf[i].corner.d[0] == pBaseCorner->vert.d[0] &&
			    pCornerBuf->buf[i].corner.d[1] == pBaseCorner->vert.d[1]) {

				//on base vert
				onLineBase = pBaseCorner->localIdx;
			}
			else {
				//on next base vert
				onLineBase = pBaseCorner->localIdxNext;
			}
			pNewEntry->baseCorner = (I8)onLineBase;
			pNewEntry->isBaseCorner = true;
		}
		else if (pCornerBuf->buf[i].isStuc) {
			//resides on base edge
			pNewEntry->baseCorner = pBaseCorner->localIdx;
		}
		pNewCornerBuf->onLine = true;
		pNewEntry->onLine = 1;
	}
	pNewCornerBuf->size++;
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
void addIntersectionToBuf(
	const StucAlloc *pAlloc,
	CornerBufWrap *pNewCornerBuf,
	CornerBufWrap *pCornerBuf,
	I32 i,
	CornerInfo *pBaseCorner,
	I32 iNext,
	IslandIdxPair *pIntersectCache,
	F32 *ptBuf,
	I32 *pCount,
	I32 mapFaceWindDir,
	I32 faceWindDir,
	CornerAncestors *pAncestors
) {
	pIntersectCache[*pCount].pIsland = pNewCornerBuf;
	pIntersectCache[*pCount].corner = pNewCornerBuf->size;
	CornerBuf *pCorner = pCornerBuf->buf + i;
	CornerBuf *pCornerNext = pCornerBuf->buf + iNext;
	CornerBuf *pNewEntry = pNewCornerBuf->buf + pNewCornerBuf->size;
	stucCalcIntersection(
		pCorner->corner,
		pCornerNext->corner,
		pBaseCorner->vert,
		pBaseCorner->dir,
		&pNewEntry->corner,
		&pNewEntry->mapAlpha,
		&pNewEntry->alpha
	);
	if (true) {
		pNewEntry->alpha *= -1.0f;
	}
	//this attrib is lerped here instead of later like other attribs,
	//as it's needed to transform from uvw to xyz
	//TODO is this still necessary? or is it obsolete?
	pNewEntry->normal = v3F32Lerp(pCorner->normal, pCornerNext->normal, pNewEntry->mapAlpha);
	//pNewEntry->normal = pCornerBuf->buf[i].normal;
	//V3_F32 up = {.0f, .0f, 1.0f};
	//pNewEntry->normal = up;
	if (checkIfOnVert(pCornerBuf, i, iNext)) {
		I32 lastBaseCorner = mapFaceWindDir ?
			pBaseCorner->idx - 1 : pBaseCorner->idx + 1;
		bool whichVert = pCornerBuf->buf[i].baseCorner == lastBaseCorner;
		if (faceWindDir) {
			pNewEntry->baseCorner = whichVert ?
				pBaseCorner->localIdx : pBaseCorner->localIdxNext;
			if (!whichVert) {
				pNewEntry->segment = pCornerBuf->buf[i].segment;
			}
		}
		else {
			pNewEntry->baseCorner = whichVert ?
				pBaseCorner->localIdxPrev : pBaseCorner->localIdx;
			if (whichVert) {
				pNewEntry->segment = pCornerBuf->buf[i].segment;
			}
		}
		//if the corner maintains it's existing incorner,
		//then ensure the segment also carries over
		pNewEntry->isBaseCorner = true;
	}
	else {
		pNewEntry->baseCorner = (I8)pBaseCorner->idx;
		pNewEntry->isBaseCorner = false;
	}
	pNewEntry->ancestor = appendToAncestors(pAlloc, pAncestors, pCornerBuf->buf + i);
	pNewEntry->ancestorNext =
		appendToAncestors(pAlloc, pAncestors, pCornerBuf->buf + iNext);
	pNewEntry->isStuc = false;
	pNewEntry->stucCorner = pCornerBuf->buf[i].stucCorner;
	ptBuf[*pCount] = pNewEntry->alpha;
	++*pCount;
	pNewCornerBuf->size++;
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
	STUC_ASSERT("", pCornerPair->pIsland->size > 0);
	STUC_ASSERT("", pCornerPairNext->pIsland->size > 0);
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
		STUC_ASSERT("", pIslandPending->size > 0);
		for (I32 j = 0; j < pIslandPending->size; ++j) {
			pIsland->buf[pIsland->size + j] = pIslandPending->buf[j];
		}
		pIsland->size += pIslandPending->size;
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
	bool mapFaceWindDir
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
			(*ppIsland)->lastInCorner += mapFaceWindDir ? 1 : -1;
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
	if (pCorner->baseCorner == inCorner) {
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

static
Result clipMapFaceAgainstCorner(
	MappingJobState *pState,
	CornerBufWrap *pCornerBuf,
	CornerBufWrap *pNewCornerBuf,
	I32 *pInsideBuf,
	CornerInfo *pBaseCorner,
	V2_F32 baseCornerCross,
	I32 mapFaceWindDir,
	I32 faceWindDir,
	Segments *pSegments,
	CornerAncestors *pAncestors
) {
	Result err = STUC_SUCCESS;
	for (I32 i = 0; i < pCornerBuf->size; ++i) {
		V2_F32 stucVert = *(V2_F32 *)&pCornerBuf->buf[i].corner;
		V2_F32 uvStucDir = _(stucVert V2SUB pBaseCorner->vert);
		F32 dot = _(baseCornerCross V2DOT uvStucDir);
		bool onLine = dot == .0f;
		pInsideBuf[i] = onLine ? -1 : (dot < .0f) ^ ((bool)mapFaceWindDir ^ (bool)faceWindDir);
	}
	bool in = false;
	CornerBufWrap *pIsland = NULL;
	//32 is the max intersections possible with a 32 vert map face
	IslandIdxPair intersectCache[32] = {0};
	F32 tBuf[33] = {-FLT_MAX}; //first element must be low for later sorting
	F32 *ptBuf = tBuf + 1;
	I32 count = 0;
	for (I32 i = 0; i < pCornerBuf->size; ++i) {
		I32 iNext = (i + 1) % pCornerBuf->size;
		I32 iPrev = i ? i - 1 : pCornerBuf->size - 1;
		if (pInsideBuf[i]) {
			//point is inside, or on the line
			err = setIsland(
				pState,
				&pIsland,
				pNewCornerBuf,
				&in,
				pBaseCorner->localIdx,
				mapFaceWindDir
			);
			STUC_RETURN_ERR_IFNOT(err, "");
			addInsideCornerToBuf(
				pIsland,
				pCornerBuf,
				pInsideBuf,
				i,
				iNext,
				iPrev,
				pBaseCorner,
				intersectCache,
				ptBuf,
				&count
			);
		}
		else if (in) {
			in = false;
		}
		if ((pInsideBuf[i] != 0) ^ (pInsideBuf[iNext] != 0) &&
		    pInsideBuf[i] >= 0 && pInsideBuf[iNext] >= 0) {

			//the current point is inside, but the next is not (or visa versa),
			//so calc intersection point. The != and ^ are to account for the
			//fact that insideBuf can be negative if the point is on the line.
			//The != converts the value to absolute, thus ignoring this.
			err = setIsland(
				pState,
				&pIsland,
				pNewCornerBuf,
				&in,
				pBaseCorner->localIdx,
				mapFaceWindDir
			);
			STUC_RETURN_ERR_IFNOT(err, "");
			addIntersectionToBuf(
				&pState->pBasic->pCtx->alloc,
				pIsland,
				pCornerBuf,
				i,
				pBaseCorner,
				iNext,
				intersectCache,
				ptBuf,
				&count,
				mapFaceWindDir,
				faceWindDir,
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
	if (pBaseCorner->flipEdgeDir) {
		for (I32 i = 0; i < count; ++i) {
			ptBuf[i] = 1.0f - ptBuf[i];
		}
	}
	stucFInsertionSort(pIdxTable, count, ptBuf);
	I32 inCorner = pBaseCorner->localIdx;
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
	for (int i = 0; i < pCornerBuf->size; ++i) {
		I8* pBaseCorner = &pCornerBuf->buf[i].baseCorner;
		*pBaseCorner = *pBaseCorner ? *pBaseCorner - (I8)1 : (I8)(pInFace->size - 1);
	}
}

static
Result clipMapFaceAgainstInFace(
	MappingJobState *pState,
	FaceRange *pInFace,
	CornerBufWrap *pCornerBuf,
	I32 faceWindingDir,
	I32 mapFaceWindDir,
	Segments *pSegments,
	CornerAncestors *pAncestors
) {
	Result err = STUC_SUCCESS;
	I32 start = pCornerBuf->lastInCorner;
	for (I32 i = start; mapFaceWindDir ? i < pInFace->size : i >= 0; mapFaceWindDir ? ++i : --i) {
		STUC_ASSERT("", i >= 0 && i < pInFace->size);
		CornerInfo baseCorner = {0};
		//why is both this and idxLocal local? Shouldn't this be absolute?
		baseCorner.idx = i;
		baseCorner.vert = pState->pBasic->pInMesh->pUvs[i + pInFace->start];
		I32 uvNextIdxLocal;
		I32 uvPrevIdxLocal;
		I32 edgeCorner;
		if (mapFaceWindDir) {
			uvNextIdxLocal = ((i + 1) % pInFace->size);
			uvPrevIdxLocal = i ? i - 1 : pInFace->size - 1;
			edgeCorner = pInFace->start + i;
		}
		else {
			uvNextIdxLocal = i ? i - 1 : pInFace->size - 1;
			uvPrevIdxLocal = ((i + 1) % pInFace->size);
			edgeCorner = pInFace->start + uvNextIdxLocal;
		}
		baseCorner.edgeIdx = pState->pBasic->pInMesh->core.pEdges[edgeCorner];
		V2_I32 edgeCorners = pState->pBasic->pInMesh->pEdgeCorners[baseCorner.edgeIdx];
		STUC_ASSERT("", edgeCorners.d[0] == edgeCorner || edgeCorners.d[1] == edgeCorner);
		baseCorner.flipEdgeDir = edgeCorner != edgeCorners.d[0];
		I32 uvNextIdx = uvNextIdxLocal + pInFace->start;
		baseCorner.vertNext = pState->pBasic->pInMesh->pUvs[uvNextIdx];
		baseCorner.idxNext = uvNextIdxLocal;
		baseCorner.localIdx = (I8)i;
		baseCorner.localIdxPrev = (I8)uvPrevIdxLocal;
		baseCorner.localIdxNext = (I8)uvNextIdxLocal;
		baseCorner.dir = _(baseCorner.vertNext V2SUB baseCorner.vert);
		baseCorner.dirBack = _(baseCorner.vert V2SUB baseCorner.vertNext);
		CornerBufWrap newCornerBuf = {
			.edgeFace = pCornerBuf->edgeFace,
			.onLine = pCornerBuf->onLine
		};
		I32 insideBuf[65] = {0};
		V2_F32 baseCornerCross = v2F32Cross(baseCorner.dir);
		err = clipMapFaceAgainstCorner(
			pState,
			pCornerBuf,
			&newCornerBuf,
			insideBuf,
			&baseCorner,
			baseCornerCross,
			mapFaceWindDir,
			faceWindingDir,
			pSegments,
			pAncestors
		);
		STUC_RETURN_ERR_IFNOT(err, "");

		if (newCornerBuf.size <= 2) {
			pCornerBuf->size = newCornerBuf.size;
			return err;
		}
		CornerBufWrap *pTail = &newCornerBuf;
		while (pTail->pNext) {
			pTail = pTail->pNext;
		}
		pTail->pNext = pCornerBuf->pNext;
		*pCornerBuf = newCornerBuf;
	}
	if (!mapFaceWindDir) {
		cornerBufDecrementBaseCorners(pCornerBuf, pInFace);
	}
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
		pCorner->stucCorner,
		pState->pBasic->pMap,
		pMapFace,
		pInFace->idx + pState->inFaceRange.start,
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
	for (I32 j = 0; j < pCornerBuf->size; ++j) {
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
		if (pMapMesh->pUsg && pCorner->isStuc) {
			handleVertIfInUsg(pState, pCorner, pMapFace, pInFace, tileMin, &aboveCutoff);
		}
		if (!aboveCutoff) {
			pCorner->cornerFlat = barycentricToCartesian(vertsXyz, &vertBc);
		}
		if (pCorner->isStuc) {
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
	if (pCorner->isStuc) {
		stucCopyAttrib(pDestAttrib, destIdx, pMapAttrib,
		           pMapFace->start + pCorner->stucCorner);
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
			&pInAttrib
		);
		STUC_ASSERT("", err == STUC_SUCCESS);
		const StucAttrib *pMapAttrib = NULL;
		stucGetMatchingAttribConst(
			pState->pBasic->pCtx,
			&pState->pBasic->pMap->pMesh->core, pMapAttribArr,
			&pBufMesh->mesh.core, pBufAttrib,
			true,
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
		pMapFace->start + pCornerBuf[cornerBufIdx].stucCorner,
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
		pCornerBuf[cornerBufIdx].stucCorner,
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
	if (pCornerBuf[cornerBufIdx].isStuc) {
		I32 stucCorner = pCornerBuf[cornerBufIdx].stucCorner;
		return pState->pBasic->pMap->pMesh->core.pEdges[pMapFace->start + stucCorner];
	}
	else {
		I32 baseCorner = pCornerBuf[cornerBufIdx].baseCorner;
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
	I32 isMapEdge = pCornerBuf[cornerBufIdx].isStuc;
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
	I32 stucCorner = pMapFace->start + pCornerBufEntry[cornerBufIdx].stucCorner;
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
void setBorderFaceMapAttrib(BorderFace *pEntry, UBitField8 *pArr, I32 corner, I32 value) {
	I32 len = 3 + pEntry->memType;
	stucSetBitArr(pArr, corner, value, len);
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
	bool faceWindDir,
	bool mapFaceWindDir,
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
	pEntry->inOrient = faceWindDir;
	pEntry->mapOrient = mapFaceWindDir;

	BorderFaceBitArrs bitArrs = {0};
	stucGetBorderFaceBitArrs(pEntry, &bitArrs);

	STUC_ASSERT("", pCornerBuf->size <= 64);
	for (I32 i = 0; i < pCornerBuf->size; ++i) {
		CornerBuf *pCorner = pCornerBuf->buf + i;
		if (pCorner->onLine != 0) {
			stucSetBitArr(bitArrs.pOnLine, i, true, 1);
		}
		if (pCorner->isStuc) {
			stucSetBitArr(bitArrs.pIsStuc, i, true, 1);
		}
		if (pCorner->stucCorner) {
			setBorderFaceMapAttrib(pEntry, bitArrs.pStucCorner, i, pCorner->stucCorner);
		}
		if (pCorner->isBaseCorner) {
			stucSetBitArr(bitArrs.pOnInVert, i, true, 1);
		}
		if (!pCorner->isStuc && pCorner->segment) {
			I32 idx = pCorner->segment - 1;
			I32 j = 1;
			//if map face orientation is inverted, then the position
			//of each incorner in the segments array is offset by 1.
			//This is because pCorner->baseCorner is decremented at the end of
			//clipping to account for the inverted wind order
			I32 segIdx = mapFaceWindDir ?
				pCorner->baseCorner : (pCorner->baseCorner + 1) % pInFace->size;
			for (j; j < pSegments[segIdx].count; ++j) {
				if (pSegments[segIdx].pIndices[j] == idx) {
					break;
				}
			}
			j--;
			if (j) {
				setBorderFaceMapAttrib(pEntry, bitArrs.pSegment, i, j);
			}
		}
		// Only add basecorner for stuc if online, otherwise value will
		// will not fit within 2 bits
		if ((!pCorner->isStuc || pCorner->onLine) && pCorner->baseCorner) {
			stucSetBitArr(bitArrs.pBaseCorner, i, pCorner->baseCorner, 2);
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
	bool faceWindDir,
	bool mapFaceWindDir,
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
				faceWindDir,
				mapFaceWindDir,
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
				faceWindDir,
				mapFaceWindDir,
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
	bool faceWindDir,
	bool mapFaceWindDir,
	Segments *pSegments
) {
	I32 memType = stucGetBorderFaceMemType(pMapFace->size, pCornerBuf->size);
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
			faceWindDir,
			mapFaceWindDir,
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
			faceWindDir,
			mapFaceWindDir,
			pSegments,
			pBucket,
			pEntry,
			memType
		);
	}
}

static
void addInFace(MappingJobState *pState, I32 face, FaceRange *pInFace, FaceRange *pMapFace) {
	InFaceArr *pInFaceEntry = pState->pInFaces + face;
	pInFaceEntry->pArr = pState->pBasic->pCtx->alloc.pMalloc(sizeof(I32));
	*pInFaceEntry->pArr = pInFace->idx + pState->inFaceRange.start;
	pInFaceEntry->count = 1;
	pInFaceEntry->usg = pMapFace->idx;
	I32 faceCount = pState->bufMesh.mesh.core.faceCount;
	STUC_ASSERT("", faceCount <= pState->inFaceSize);
	if (pState->inFaceSize == faceCount) {
		pState->inFaceSize *= 2;
		pState->pInFaces =
			pState->pBasic->pCtx->alloc.pRealloc(pState->pInFaces, sizeof(InFaceArr) * pState->inFaceSize);
	}
}

static
void addClippedFaceToBufMesh(
	MappingJobState *pState,
	CornerBufWrap *pCornerBuf,
	FaceRange *pMapFace,
	V2_I32 tile,
	FaceRange *pInFace,
	bool faceWindDir,
	bool mapFaceWindDir,
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
	bool invert = !faceWindDir && !isBorderFace;
	I32 size = pCornerBuf->size;
	for (I32 i = invert ? size - 1 : 0;
		invert ? i >= 0 : i < size;
		invert ? --i : ++i) {

		I32 refFace;
		I32 isStuc = pCornerBuf->buf[i].isStuc;
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
	if (pState->pBasic->ppInFaceTable && !isBorderFace) {
		addInFace(pState, face.idx, pInFace, pMapFace);
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
			faceWindDir,
			mapFaceWindDir,
			pSegments
		);
	}
}

static
bool isOnLine(CornerBufWrap *pCornerBuf) {
	for (I32 i = 0; i < pCornerBuf->size; ++i) {
		if (pCornerBuf->buf[i].onLine) {
			return true;
		}
	}
	return false;
}

static
bool isTriDegenerate(const BaseTriVerts *pTri, const FaceRange *pFace) {
	if (v2F32DegenerateTri(pTri->uv[0], pTri->uv[1], pTri->uv[2], .0f) ||
		v3F32DegenerateTri(pTri->xyz[0], pTri->xyz[1], pTri->xyz[2], .0f)) {
		return true;
	}
	if (pFace->size == 4) {
		if (v2F32DegenerateTri(pTri->uv[2], pTri->uv[3], pTri->uv[0], .0f) ||
			v3F32DegenerateTri(pTri->xyz[2], pTri->xyz[3], pTri->xyz[0], .0f)) {
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
	I32 mapFaceWindDir
) {
	pCornerBuf->size = pMapFace->size;
	for (I32 k = 0; k < pMapFace->size; ++k) {
		I32 vertIdx = pMap->pMesh->core.pCorners[pMapFace->start + k];
		CornerBuf *pCorner = pCornerBuf->buf + k;
		pCorner->isStuc = 1;
		pCorner->baseCorner = (I8)((vertIdx + 1) * -1);
		pCorner->corner = pMap->pMesh->pVerts[vertIdx];
		pCorner->corner.d[0] += fTileMin.d[0];
		pCorner->corner.d[1] += fTileMin.d[1];
		pCorner->stucCorner = (I8)k;
		pCorner->normal = pMap->pMesh->pNormals[pMapFace->start + k];
	}
	pCornerBuf->lastInCorner = mapFaceWindDir ? 0 : pInFace->size - 1;
}

static
Result clipMapFaceIntoFaces(
	MappingJobState *pState,
	CornerBufWrap *pCornerBuf,
	FaceRange *pInFace,
	I32 inFaceWind,
	I32 mapFaceWind,
	Segments *pSegments,
	CornerAncestors *pAncestors
) {
	Result err = STUC_SUCCESS;
	CornerBufWrap *pCornerBufPtr = pCornerBuf;
	do {
		if (!pCornerBufPtr->invalid) {
			err = clipMapFaceAgainstInFace(
				pState,
				pInFace,
				pCornerBufPtr,
				inFaceWind,
				mapFaceWind,
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
	I32 inFaceWind,
	I32 mapFaceWind
) {
	CornerBufWrap *pCornerBufPtr = pCornerBuf;
	do{
		if (!pCornerBufPtr->invalid) {
			if (pCornerBufPtr->size >= 3) {
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
					inFaceWind,
					mapFaceWind,
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
			CornerBufWrap cornerBuf = {0};
			initCornerBuf(pMap, fTileMin, &cornerBuf, pInFace, &mapFace, mapFaceWind);
			ancestors.count = 0;
			err = clipMapFaceIntoFaces(
				pState,
				&cornerBuf,
				pInFace,
				inFaceWind,
				mapFaceWind,
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
				inFaceWind,
				mapFaceWind
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
