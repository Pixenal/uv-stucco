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
	int32_t index;
	int32_t edgeIndex;
	int32_t indexNext;
	int8_t localIndex;
	int8_t localIndexPrev;
	int8_t localIndexNext;
	V2_F32 vert;
	V2_F32 vertNext;
	V2_F32 dir;
	V2_F32 dirBack;
	bool flipEdgeDir;
} LoopInfo;

typedef struct {
	LoopBuf *pArr;
	int32_t count;
	int32_t size;
} LoopAncestors;

typedef struct {
	int32_t loopStart;
	int32_t face;
	int32_t loop;
	int32_t  edge;
	int32_t  vert;
} AddClippedFaceVars;

typedef struct {
	LoopBufWrap *pIsland;
	LoopBufWrap *pPending;
	int32_t loop;
} IslandIndexPair;

static
bool checkIfOnVert(LoopBufWrap *pLoopBuf, int32_t i, int32_t iNext) {
	return (pLoopBuf->buf[i].baseLoop == pLoopBuf->buf[iNext].baseLoop ||
	        pLoopBuf->buf[i].isBaseLoop || pLoopBuf->buf[iNext].isBaseLoop) &&
		    !pLoopBuf->buf[i].isRuvm && !pLoopBuf->buf[iNext].isRuvm;
}

static
void addInsideLoopToBuf(LoopBufWrap *pNewLoopBuf, LoopBufWrap *pLoopBuf,
                        int32_t *pInsideBuf, int32_t i, int32_t iNext,
                        int32_t iPrev, LoopInfo *pBaseLoop,
						IslandIndexPair *pIntersectCache, float *ptBuf,
                        int32_t *pCount, bool faceWindDir) {
	LoopBuf *pNewEntry = pNewLoopBuf->buf + pNewLoopBuf->size;
	pNewLoopBuf->buf[pNewLoopBuf->size] = pLoopBuf->buf[i];
	if (pInsideBuf[i] == 2) {
		//pNewLoopBuf->buf[pNewLoopBuf->size].onLine = true;
	}
	//using += so that base loops can be determined. ie, if an ruvm
	//vert has a dot of 0 twice, then it is sitting on a base vert,
	//but if once, then it's sitting on an edge.
	if (pInsideBuf[i] < 0) {
		//is on line
		if ((pInsideBuf[iPrev] == 0 && pInsideBuf[iNext] == 1) ||
			(pInsideBuf[iPrev] == 1 && pInsideBuf[iNext] == 0)) {
			//add to intersection but
			pIntersectCache[*pCount].pIsland = pNewLoopBuf;
			pIntersectCache[*pCount].loop = pNewLoopBuf->size;
			LoopBuf *pLoop = pLoopBuf->buf + i;
			LoopBuf *pLoopNext = pLoopBuf->buf + iNext;
			calcIntersection(pLoop->loop, pLoopNext->loop, pBaseLoop->vert,
							 pBaseLoop->dir, NULL, NULL, &pNewEntry->alpha);
			pNewEntry->alpha *= -1.0f;
			ptBuf[*pCount] = pNewEntry->alpha;
			++*pCount;
		}
		if (pLoopBuf->buf[i].onLine) {
			//this loop already resided on a previous base edge,
			//it must then reside on a base vert, rather than an edge.
			//determine which vert in the edge it sits on:
			int32_t onLineBase;
			if (pLoopBuf->buf[i].loop.d[0] == pBaseLoop->vert.d[0] &&
				pLoopBuf->buf[i].loop.d[1] == pBaseLoop->vert.d[1]) {
				//on base vert
				onLineBase = pBaseLoop->localIndex;
			}
			else {
				//on next base vert
				onLineBase = pBaseLoop->localIndexNext;
			}
			pNewEntry->baseLoop = onLineBase;
			pNewEntry->isBaseLoop = true;
		}
		else if (pLoopBuf->buf[i].isRuvm) {
			//resides on base edge
			pNewEntry->baseLoop =
				pBaseLoop->localIndex;
		}
		pNewLoopBuf->onLine = true;
		pNewEntry->onLine = 1;
	}
	pNewLoopBuf->size++;
}

static
int32_t appendToAncestors(RuvmAlloc *pAlloc, LoopAncestors *pAncestors, LoopBuf *pLoop) {
	RUVM_ASSERT("", pAncestors->count <= pAncestors->size);
	if (pAncestors->count == pAncestors->size) {
		pAncestors->size *= 2;
		pAncestors->pArr = pAlloc->pRealloc(pAncestors->pArr, sizeof(LoopBuf) *
		                                    pAncestors->size);
	}
	int32_t index = pAncestors->count;
	pAncestors->pArr[index] = *pLoop;
	pAncestors->count++;
	return index;
}

static
void addIntersectionToBuf(RuvmAlloc *pAlloc, LoopBufWrap *pNewLoopBuf, LoopBufWrap *pLoopBuf,
                          int32_t i, LoopInfo *pBaseLoop,
						  int32_t iNext, bool flippedWind,
                          IslandIndexPair *pIntersectCache, float *ptBuf,
                          int32_t *pCount, int32_t mapFaceWindDir, int32_t faceWindDir,
                          LoopAncestors *pAncestors) {
	pIntersectCache[*pCount].pIsland = pNewLoopBuf;
	pIntersectCache[*pCount].loop = pNewLoopBuf->size;
	LoopBuf *pLoop = pLoopBuf->buf + i;
	LoopBuf *pLoopNext = pLoopBuf->buf + iNext;
	LoopBuf *pNewEntry = pNewLoopBuf->buf + pNewLoopBuf->size;
	calcIntersection(pLoop->loop, pLoopNext->loop, pBaseLoop->vert,
	                 pBaseLoop->dir, &pNewEntry->loop, &pNewEntry->mapAlpha, &pNewEntry->alpha);
	if (true) {
		pNewEntry->alpha *= -1.0f;
	}
	//this attrib is lerped here instead of later like other attribs,
	//as it's needed to transform from uvw to xyz
	pNewEntry->normal = v3Lerp(pLoop->normal, pLoopNext->normal, pNewEntry->mapAlpha);
	//pNewEntry->normal = pLoopBuf->buf[i].normal;
	//V3_F32 up = {.0f, .0f, 1.0f};
	//pNewEntry->normal = up;
	if (checkIfOnVert(pLoopBuf, i, iNext)) {
		int32_t lastBaseLoop = mapFaceWindDir ?
			pBaseLoop->index - 1 : pBaseLoop->index + 1;
		bool whichVert = pLoopBuf->buf[i].baseLoop == lastBaseLoop;
		if (faceWindDir) {
			pNewEntry->baseLoop = whichVert ?
				pBaseLoop->localIndex : pBaseLoop->localIndexNext;
			if (!whichVert) {
				pNewEntry->segment = pLoopBuf->buf[i].segment;
			}
		}
		else {
			pNewEntry->baseLoop = whichVert ?
				pBaseLoop->localIndexPrev : pBaseLoop->localIndex;
			if (whichVert) {
				pNewEntry->segment = pLoopBuf->buf[i].segment;
			}
		}
		//if the loop maintains it's existing inloop,
		//then ensure the segment also carries over
		pNewEntry->isBaseLoop = true;
	}
	else {
		pNewEntry->baseLoop = pBaseLoop->index;
		pNewEntry->isBaseLoop = false;
	}
	pNewEntry->ancestor =
		appendToAncestors(pAlloc, pAncestors, pLoopBuf->buf + i);
	pNewEntry->ancestorNext =
		appendToAncestors(pAlloc, pAncestors, pLoopBuf->buf + iNext);
	pNewEntry->isRuvm = false;
	pNewEntry->ruvmLoop = pLoopBuf->buf[i].ruvmLoop;
	ptBuf[*pCount] = pNewEntry->alpha;
	++*pCount;
	pNewLoopBuf->size++;
}

static
LoopBufWrap *createNewLoopBuf(RuvmAlloc *pAlloc) {
	LoopBufWrap *pLoopBuf = pAlloc->pCalloc(1, sizeof(LoopBufWrap));
	return pLoopBuf;
}

static
void initPendingMerge(RuvmAlloc *pAlloc, LoopBufWrap *pIsland) {
	pIsland->mergeSize = 3;
	pIsland->pPendingMerge =
		pAlloc->pCalloc(pIsland->mergeSize, sizeof(void *));
	pIsland->pPendingMerge[0] = -1;
	pIsland->mergeCount = 1;
}

static
void addToPendingMerge(RuvmAlloc *pAlloc,
                       LoopBufWrap *pIsland, int32_t value) {
	RUVM_ASSERT("", pIsland->pPendingMerge);
	RUVM_ASSERT("", pIsland->mergeSize > 0);
	pIsland->pPendingMerge[pIsland->mergeCount] = value;
	pIsland->mergeCount++;
	if (pIsland->mergeCount == pIsland->mergeSize) {
		pIsland->mergeSize *= 2;
		pIsland->pPendingMerge =
			pAlloc->pRealloc(pIsland->pPendingMerge,
			                 pIsland->mergeSize * sizeof(void *));
	}
}

static
void destroyPendingMerge(RuvmAlloc *pAlloc, LoopBufWrap *pIsland) {
	RUVM_ASSERT("", pIsland->mergeSize > 0);
	RUVM_ASSERT("", pIsland->mergeCount > 0);
	if (pIsland->pPendingMerge) {
		pAlloc->pFree(pIsland->pPendingMerge);
		pIsland->pPendingMerge = NULL;
		pIsland->mergeSize = 0;
		pIsland->mergeCount = 0;
	}
}

static
void addIslandToPendingMerge(RuvmAlloc *pAlloc, IslandIndexPair *pLoopPair,
                             IslandIndexPair *pLoopPairNext, int32_t realiNext,
                             IslandIndexPair *pIntersectCache, int32_t cacheCount) {
	RUVM_ASSERT("", pLoopPair->pIsland->size > 0);
	RUVM_ASSERT("", pLoopPairNext->pIsland->size > 0);
	LoopBufWrap *pIsland = pLoopPair->pPending ?
		pLoopPair->pPending : pLoopPair->pIsland;
	if (pLoopPairNext->pPending == pIsland) {
		//already listed
		return;
	}
	LoopBufWrap *pIslandNext = pLoopPairNext->pIsland;
	if (!pIsland->pPendingMerge) {
		initPendingMerge(pAlloc, pIsland);
	}
	addToPendingMerge(pAlloc, pIsland, realiNext);
	if (pIslandNext->pPendingMerge) {
		for (int32_t i = 1; i < pIslandNext->mergeCount; ++i) {
			addToPendingMerge(pAlloc, pIsland, pIslandNext->pPendingMerge[i]);
		}
		destroyPendingMerge(pAlloc, pIslandNext);
	}
	pLoopPairNext->pIsland->invalid = true;
	//update references to invalid entry in list
	for (int32_t i = 0; i < cacheCount; ++i) {
		if (pIntersectCache[i].pIsland == pIslandNext) {
			pIntersectCache[i].pPending = pIsland;
		}
	}
}

static
void mergeIslands(LoopBufWrap *pIsland, IslandIndexPair *pIntersectCache) {
	//17 is the max islands possible with a 32 vert map face
	int32_t indexTable[18] = {-1};
	if (pIsland->mergeCount > 2) {
		insertionSort(indexTable + 1, pIsland->mergeCount - 1,
		              pIsland->pPendingMerge + 1);
	}
	for (int32_t i = 1; i < pIsland->mergeCount; ++i) {
		int32_t indexPending = pIsland->pPendingMerge[indexTable[i] + 1];
		LoopBufWrap *pIslandPending = pIntersectCache[indexPending].pIsland;
		RUVM_ASSERT("", pIslandPending->invalid);
		RUVM_ASSERT("", pIslandPending->size > 0);
		for (int32_t j = 0; j < pIslandPending->size; ++j) {
			pIsland->buf[pIsland->size + j] = pIslandPending->buf[j];
		}
		pIsland->size += pIslandPending->size;
	}
}

static
void setIsland(RuvmAlloc *pAlloc, LoopBufWrap **ppIsland,
               LoopBufWrap *pRoot, bool *pIn, int32_t inLoop,
               bool mapFaceWindDir) {
	if (!*pIn) {
		if (!*ppIsland) {
			*ppIsland = pRoot;
		}
		else {
			while ((*ppIsland)->pNext) {
				*ppIsland = (*ppIsland)->pNext;
			}
			*ppIsland = (*ppIsland)->pNext = createNewLoopBuf(pAlloc);
			(*ppIsland)->lastInLoop = inLoop;
			(*ppIsland)->lastInLoop += mapFaceWindDir ? 1 : -1;
		}
		*pIn = true;
	}
}

typedef struct {
	float *pArr;
	int32_t *pIndices;
	int32_t size;
	int32_t count;
} Segments;

static
void setSegment(IslandIndexPair *pLoopPair, Segments *pSegments, int32_t inLoop) {
	LoopBuf *pLoop = pLoopPair->pIsland->buf + pLoopPair->loop;
	if (pLoop->baseLoop == inLoop) {
		pLoop->segment = pSegments[inLoop].count;
	}
}

static
void setSegments(RuvmAlloc *pAlloc, float *ptBuf, Segments *pSegments,
                 IslandIndexPair *pIntersectCache, int32_t inLoop,
                 int32_t reali, int32_t realiNext) {
	Segments *pSegEntry = pSegments + inLoop;
	IslandIndexPair *pLoop = pIntersectCache + reali;
	IslandIndexPair *pLoopNext = pIntersectCache + realiNext;
	if (ptBuf[reali] > .0f || ptBuf[realiNext] > .0f) {
		setSegment(pLoop, pSegments, inLoop);
		setSegment(pLoopNext, pSegments, inLoop);
		pSegEntry->pArr[pSegEntry->count] = ptBuf[reali];
		pSegEntry->count++;
		if (pSegEntry->count == pSegEntry->size) {
			int32_t oldSize = pSegEntry->size;
			pSegEntry->size *= 2;
			pSegEntry->pArr =
				pAlloc->pRealloc(pSegEntry->pArr, pSegEntry->size * sizeof(float));
			pSegEntry->pIndices =
				pAlloc->pRealloc(pSegEntry->pIndices, pSegEntry->size * sizeof(int32_t));
			memset(pSegEntry->pArr + pSegEntry->count, 0, sizeof(float) * oldSize);
			memset(pSegEntry->pIndices + pSegEntry->count, 0, sizeof(int32_t) * oldSize);
		}
	}
}

static
void clipRuvmFaceAgainstSingleLoop(MappingJobVars *pVars, LoopBufWrap *pLoopBuf,
                                   LoopBufWrap *pNewLoopBuf, int32_t *pInsideBuf,
                                   FaceRange *pInFace, LoopInfo *pBaseLoop,
								   V2_F32 baseLoopCross,
								   bool flippedWind,
								   int32_t mapFaceWindDir,
                                   int32_t faceWindDir, Segments *pSegments,
                                   LoopAncestors *pAncestors) {
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		V2_F32 ruvmVert = *(V2_F32 *)&pLoopBuf->buf[i].loop;
		V2_F32 uvRuvmDir = _(ruvmVert V2SUB pBaseLoop->vert);
		float dot = _(baseLoopCross V2DOT uvRuvmDir);
		bool onLine = dot == .0f;
		pInsideBuf[i] = onLine ? -1 : (dot < .0f) ^ ((bool)mapFaceWindDir ^ (bool)faceWindDir);
	}
	bool in = false;
	LoopBufWrap *pIsland = NULL;
	//32 is the max intersections possible with a 32 vert map face
	IslandIndexPair intersectCache[32] = {0};
	float tBuf[33] = {-FLT_MAX}; //first element must be low for later sorting
	float *ptBuf = tBuf + 1;
	int32_t count = 0;
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		int32_t iNext = (i + 1) % pLoopBuf->size;
		int32_t iPrev = i ? i - 1 : pLoopBuf->size - 1;
		if (pInsideBuf[i]) {
			//point is inside, or on the line
			setIsland(&pVars->alloc, &pIsland, pNewLoopBuf, &in,
			          pBaseLoop->localIndex, mapFaceWindDir);
			addInsideLoopToBuf(pIsland, pLoopBuf, pInsideBuf, i, iNext, iPrev,
			                   pBaseLoop, intersectCache, ptBuf, &count,
			                   faceWindDir);
		}
		else if (in) {
			in = false;
		}
		if (pInsideBuf[i] != 0 ^ pInsideBuf[iNext] != 0 &&
		    pInsideBuf[i] >= 0 && pInsideBuf[iNext] >= 0) {
			//the current point is inside, but the next is not (or visa versa),
			//so calc intersection point. The != and ^ are to account for the
			//fact that insideBuf can be negative if the point is on the line.
			//The != converts the value to absolute, thus ignoring this.
			setIsland(&pVars->alloc, &pIsland, pNewLoopBuf, &in,
			          pBaseLoop->localIndex, mapFaceWindDir);
			addIntersectionToBuf(&pVars->alloc, pIsland, pLoopBuf, i, pBaseLoop,
			                     iNext, flippedWind,
			                     intersectCache, ptBuf, &count,
			                     mapFaceWindDir, faceWindDir,
			                     pAncestors);
			pIsland->edgeFace = true;
		}
	}
	pIsland = pNewLoopBuf; //reset to root
	if (!pIsland || count == 0) {
		return;
	}
	RUVM_ASSERT("", count >= 2);
	RUVM_ASSERT("should be even", !(count % 2));
	int32_t indexTable[65] = {-1}; //first element to point to first tbuf element
	int32_t *pIndexTable = indexTable + 1;
	if (pBaseLoop->flipEdgeDir) {
		for (int32_t i = 0; i < count; ++i) {
			ptBuf[i] = 1.0f - ptBuf[i];
		}
	}
	fInsertionSort(pIndexTable, count, ptBuf);
	int32_t inLoop = pBaseLoop->localIndex;
	if (!pIsland->pNext) {
		setSegments(&pVars->alloc, ptBuf, pSegments, intersectCache, inLoop,
		            pIndexTable[0], pIndexTable[1]);
		return;
	}
	for (int32_t i = 0; i < count; i += 2) {
		int32_t reali = pIndexTable[i];
		int32_t realiNext = pIndexTable[i + 1];
		IslandIndexPair *pLoop = intersectCache + reali;
		IslandIndexPair *pLoopNext = intersectCache + realiNext;
		setSegments(&pVars->alloc, ptBuf, pSegments, intersectCache, inLoop,
		            reali, realiNext);
		//RUVM_ASSERT("", !pLoop->pIsland->invalid && !pLoopNext->pIsland->invalid);
		if (pLoop->pIsland != pLoopNext->pIsland) {
			bool flip = reali > realiNext;
			if (flip) {
				IslandIndexPair *pBuf = pLoopNext;
				pLoopNext = pLoop;
				pLoop = pBuf;
				realiNext = reali;
			}
			addIslandToPendingMerge(&pVars->alloc, pLoop, pLoopNext,
			                        realiNext, intersectCache, count);
		}
	}
	do {
		if (!pIsland->invalid && pIsland->pPendingMerge) {
			mergeIslands(pIsland, intersectCache);
			destroyPendingMerge(&pVars->alloc, pIsland);
		}
		pIsland = pIsland->pNext;
	} while(pIsland);
}

static
void loopBufDecrementBaseLoops(LoopBufWrap* pLoopBuf, FaceRange* pBaseFace) {
	for (int i = 0; i < pLoopBuf->size; ++i) {
		int8_t* pBaseLoop = &pLoopBuf->buf[i].baseLoop;
		*pBaseLoop = *pBaseLoop ? *pBaseLoop - 1 : pBaseFace->size - 1;
	}
}

static
void clipRuvmFaceAgainstBaseFace(MappingJobVars *pVars, FaceRange baseFace,
                                 LoopBufWrap *pLoopBuf,
								 int32_t faceWindingDir, int32_t mapFaceWindDir,
                                 Segments *pSegments, LoopAncestors *pAncestors) {
	bool flippedWind = !faceWindingDir || !mapFaceWindDir;
	int32_t start = pLoopBuf->lastInLoop;
	for (int32_t i = start; mapFaceWindDir ? i < baseFace.size : i >= 0; mapFaceWindDir ? ++i : --i) {
		RUVM_ASSERT("", i >= 0 && i < baseFace.size);
		LoopInfo baseLoop = {0};
		//why is both this and indexLocal local? Shouldn't this be absolute?
		baseLoop.index = i;
		baseLoop.vert = pVars->mesh.pUvs[i + baseFace.start];
		int32_t uvNextIndexLocal;
		int32_t uvPrevIndexLocal;
		int32_t edgeLoop;
		if (mapFaceWindDir) {
			uvNextIndexLocal = ((i + 1) % baseFace.size);
			uvPrevIndexLocal = i ? i - 1 : baseFace.size - 1;
			edgeLoop = baseFace.start + i;
		}
		else {
			uvNextIndexLocal = i ? i - 1 : baseFace.size - 1;
			uvPrevIndexLocal = ((i + 1) % baseFace.size);
			edgeLoop = baseFace.start + uvNextIndexLocal;
		}
		baseLoop.edgeIndex = pVars->mesh.mesh.pEdges[edgeLoop];
		//TODO rename verts in pEdgeVerts to loops. They're not verts anymore.
		int32_t *pEdgeLoops = pVars->pEdgeVerts[baseLoop.edgeIndex].verts;
		RUVM_ASSERT("", pEdgeLoops[0] == edgeLoop || pEdgeLoops[1] == edgeLoop);
		baseLoop.flipEdgeDir = edgeLoop != pEdgeLoops[0];
		int32_t uvNextIndex = uvNextIndexLocal + baseFace.start;
		baseLoop.vertNext = pVars->mesh.pUvs[uvNextIndex];
		baseLoop.indexNext = uvNextIndexLocal;
		baseLoop.localIndex = i;
		baseLoop.localIndexPrev = uvPrevIndexLocal;
		baseLoop.localIndexNext = uvNextIndexLocal;
		baseLoop.dir = _(baseLoop.vertNext V2SUB baseLoop.vert);
		baseLoop.dirBack = _(baseLoop.vert V2SUB baseLoop.vertNext);
		LoopBufWrap newLoopBuf = {
			.edgeFace = pLoopBuf->edgeFace,
			.onLine = pLoopBuf->onLine
		};
		int32_t insideBuf[65] = {0};
		V2_F32 baseLoopCross = v2Cross(baseLoop.dir);
		clipRuvmFaceAgainstSingleLoop(pVars, pLoopBuf, &newLoopBuf, insideBuf,
		         						&baseFace, &baseLoop, baseLoopCross,
										flippedWind,
		                                mapFaceWindDir, faceWindingDir, pSegments,
		                                pAncestors);

		if (newLoopBuf.size <= 2) {
			pLoopBuf->size = newLoopBuf.size;
			return;
		}
		LoopBufWrap *pTail = &newLoopBuf;
		while (pTail->pNext) {
			pTail = pTail->pNext;
		}
		pTail->pNext = pLoopBuf->pNext;
		*pLoopBuf = newLoopBuf;
	}
	if (!mapFaceWindDir) {
		loopBufDecrementBaseLoops(pLoopBuf, &baseFace);
	}
}

static
V3_F32 getLoopRealNormal(Mesh *pMesh, FaceRange *pFace, int32_t loop) {
	int32_t a = loop == 0 ? pFace->size - 1 : loop - 1;
	int32_t c = (loop + 1) % pFace->size;
	int32_t aIndex = pMesh->mesh.pLoops[pFace->start + a];
	int32_t bIndex = pMesh->mesh.pLoops[pFace->start + loop];
	int32_t cIndex = pMesh->mesh.pLoops[pFace->start + c];
	V3_F32 ba = _(pMesh->pVerts[aIndex] V3SUB pMesh->pVerts[bIndex]);
	V3_F32 bc = _(pMesh->pVerts[cIndex] V3SUB pMesh->pVerts[bIndex]);
	return v3Normalize(_(ba V3CROSS bc));
}

static
void transformClippedFaceFromUvToXyz(LoopBufWrap *pLoopBuf, FaceRange ruvmFace,
									 FaceRange inFace, BaseTriVerts *pBaseTri,
									 MappingJobVars *pVars, V2_F32 tileMin, float wScale) {
	Mesh *pMapMesh = &pVars->pMap->mesh;
	//replace j, k, l, etc, in code that was moved to a func, but not updated,
	//eg, the below loop should use i, not j
	for (int32_t j = 0; j < pLoopBuf->size; ++j) {
		LoopBuf *pLoop = pLoopBuf->buf + j;
		pLoop->uvw = pLoop->loop;
		//uv is just the vert position before transform, so set that here
		pLoop->uv = *(V2_F32 *)&pLoop->uvw;
		//find enclosing triangle
		_((V2_F32 *)&pLoop->uvw V2SUBEQL tileMin);
		V3_F32 vertBc = getBarycentricInFace(pBaseTri->uv, pLoop->triLoops,
		                                     inFace.size, *(V2_F32 *)&pLoop->uvw);
		int8_t *pTriLoops = pLoop->triLoops;
		V3_F32 vertsXyz[3];
		int32_t inVerts[3];
		for (int32_t i = 0; i < 3; ++i) {
			inVerts[i] =
				pVars->mesh.mesh.pLoops[inFace.start + pTriLoops[i]];
			vertsXyz[i] = pVars->mesh.pVerts[inVerts[i]];
		}
		pLoop->bc = vertBc;
		pLoop->tbn = getInterpolatedTbn(&pVars->mesh, &inFace, pTriLoops, vertBc);
		float inVertsWScaleMul = 1.0;
		if (pVars->mesh.pWScale) {
			Attrib wScaleWrap = {
				.pData = &inVertsWScaleMul,
				.interpolate = true,
				.type = RUVM_ATTRIB_F32
			};
			triInterpolateAttrib(&wScaleWrap, 0, pVars->mesh.pWScaleAttrib,
								 inVerts[0], inVerts[1], inVerts[2], vertBc);
		}
		pLoop->uvw.d[2] *= inVertsWScaleMul;
		pLoop->inTangent = *(V3_F32 *)&pLoop->tbn.d[0];
		pLoop->projNormal = *(V3_F32 *)&pLoop->tbn.d[2];
		pLoop->inTSign = pVars->mesh.pTSigns[inFace.start + pTriLoops[0]];
		if (pMapMesh->pUsg && pLoop->isRuvm) {
			V3_F32 usgBc = {0};
			sampleUsg(pLoop->ruvmLoop, pLoop->uvw, &pLoop->loopFlat,
			          &pLoop->transformed, &usgBc, ruvmFace, pVars->pMap,
			          inFace.index + pVars->inFaceOffset, &pVars->mesh,
			          &pLoop->projNormal, tileMin, false, false, &pLoop->tbn);
		}
		if (!pLoop->transformed) {
			pLoop->loopFlat = barycentricToCartesian(vertsXyz, &vertBc);
		}
		if (pLoop->isRuvm) {
			pLoop->loop =
				_(pLoop->loopFlat V3ADD _(pLoop->projNormal V3MULS pLoop->uvw.d[2] * wScale));
			pLoopBuf->buf[j].normal = _(pLoopBuf->buf[j].normal V3MULM3X3 &pLoop->tbn);
		}
		else {
			//offset and normal transform will be deferred to combine stage,
			//to allow for interpolation of usg normals.
			//W will be add to the loop in the add to face function after this func
			pLoop->loop = pLoop->loopFlat;
		}
	}
}

static
lerpIntersect(LoopBuf *pLoop, Attrib *pDestAttrib, int32_t destIndex,
              Attrib *pMapAttrib, FaceRange *pMapFace, LoopAncestors *pAncestors) {
	RUVM_ASSERT("", pMapAttrib->type != RUVM_ATTRIB_STRING);
	if (pLoop->isRuvm) {
		copyAttrib(pDestAttrib, destIndex, pMapAttrib, pMapFace->start + pLoop->ruvmLoop);
	}
	else {
		uint8_t dataA[32] = {0}; //enough for a V4 8 byte type
		uint8_t dataB[32] = {0}; //enough for a V4 8 byte type
		Attrib attribA = *pMapAttrib;
		Attrib attribB = *pMapAttrib;
		attribA.pData = dataA;
		attribB.pData = dataB;
		int32_t indexA = 0;
		int32_t indexB = 0;
		LoopBuf *pAncestor = pAncestors->pArr + pLoop->ancestor;
		LoopBuf *pAncestorNext = pAncestors->pArr + pLoop->ancestorNext;
		lerpIntersect(pAncestor, &attribA, 0, pMapAttrib, pMapFace, pAncestors);
		lerpIntersect(pAncestorNext, &attribB, 0, pMapAttrib, pMapFace, pAncestors);
		//TODO probably will need to invert this depending on map wind order
		lerpAttrib(
			pDestAttrib,
			destIndex,
			&attribA,
			indexA,
			&attribB,
			indexB,
			pLoop->mapAlpha
		);
	}
}

//NOTE map and mesh date index params are only used if interpolation is not enabled
//for the attrib. This is always the case on faces.
//Except for right now, because I havn't implemented map triangulation and interpolation,
//so the map data index is used temporarily until that's done.
static
void blendMapAndInAttribs(BufMesh *pBufMesh, AttribArray *pDestAttribs,
                          AttribArray *pMapAttribs, AttribArray *pMeshAttribs,
						  LoopBuf *pLoopBuf, int32_t loopBufIndex,
						  int32_t dataIndex, int32_t mapDataIndex,
						  int32_t meshDataIndex, RuvmCommonAttrib *pCommonAttribs,
						  int32_t commonAttribCount, FaceRange *pBaseFace,
                          FaceRange *pMapFace, RuvmDomain domain,
                          LoopAncestors *pAncestors) {
	LoopBuf *pLoop = pLoopBuf + loopBufIndex;
	//TODO make naming for MeshIn consistent
	for (int32_t i = 0; i < pDestAttribs->count; ++i) {
		RuvmAttrib *pDestAttrib = pDestAttribs->pArr + i;
		if (pDestAttribs->pArr[i].origin == RUVM_ATTRIB_ORIGIN_COMMON) {
			if (pDestAttrib == asMesh(pBufMesh)->pVertAttrib ||
			    pDestAttrib == asMesh(pBufMesh)->pUvAttrib ||
				pDestAttrib == asMesh(pBufMesh)->pNormalAttrib) {

				continue;
			}
			RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs->pArr[i].name,
											   pMapAttribs);
			RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs->pArr[i].name,
											      pMeshAttribs);
			RuvmAttribType type = pDestAttribs->pArr[i].type;
			uint8_t mapDataBuf[RUVM_ATTRIB_STRING_MAX_LEN];
			RuvmAttrib mapBuf = {.pData = mapDataBuf, .type = type};
			uint8_t meshDataBuf[RUVM_ATTRIB_STRING_MAX_LEN];
			RuvmAttrib meshBuf = {.pData = meshDataBuf, .type = type};
			if (pMapAttrib->interpolate) {
				//TODO add correct map interpolation. to do this, you'll need
				//to triangulate the face, like with the Mesh in face, and you''
				//need to get baerycentry coords for baseLoops (not necessary
				//for intersection points, can just lerp in the clipping function).
				//so to summarise, only base loops will be interpolated here,
				//intersection loops will be lerped at clipping stage,
				//and map loops obviously don't need interpolation
				
				//temp memcpy until the above todo is handled
				memcpy(mapBuf.pData, attribAsVoid(pMapAttrib, mapDataIndex),
				       getAttribSize(pMapAttrib->type));
			}
			if (pMeshAttrib->interpolate) {
				//TODO skip interlopation if base loop? is it worth it? profile.
				triInterpolateAttrib(
					&meshBuf,
					0,
					pMeshAttrib,
				    pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[0],
					pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[1],
					pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[2],
					pLoopBuf[loopBufIndex].bc
				);
			}
			RuvmCommonAttrib *pCommon =
				getCommonAttrib(pCommonAttribs, commonAttribCount,
			                    pDestAttribs->pArr[i].name);
			RuvmAttrib *orderTable[2];
			int8_t order = pCommon->blendConfig.order;
			orderTable[0] = order ? &mapBuf : &meshBuf;
			orderTable[1] = !order ? &mapBuf : &meshBuf;
			blendAttribs(pDestAttrib, dataIndex, orderTable[0], 0,
			             orderTable[1], 0, pCommon->blendConfig);
		}
		else if (pDestAttribs->pArr[i].origin == RUVM_ATTRIB_ORIGIN_MAP) {
			RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs->pArr[i].name,
											   pMapAttribs);
			if (pMapAttrib->interpolate && domain == RUVM_DOMAIN_LOOP) {
				RuvmAttrib *pMapAttrib =
					getAttrib(pDestAttribs->pArr[i].name, pMapAttribs);
				//memcpy(attribAsVoid(pDestAttrib, dataIndex),
					//attribAsVoid(pMapAttrib, mapDataIndex),
					//getAttribSize(pMapAttrib->type));
				lerpIntersect(pLoop, pDestAttrib, dataIndex, pMapAttrib,
					            pMapFace, pAncestors);
			}
			else {
				memcpy(attribAsVoid(pDestAttrib, dataIndex),
				       attribAsVoid(pMapAttrib, mapDataIndex),
				       getAttribSize(pMapAttrib->type));
			}
		}
		else if (pDestAttribs->pArr[i].origin == RUVM_ATTRIB_ORIGIN_MESH_IN) {
			RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs->pArr[i].name,
											      pMeshAttribs);
			if (pMeshAttrib->interpolate) {
				//TODO skip interlopation is base loop? is it worth it? profile.
				triInterpolateAttrib(
					pDestAttrib,
					dataIndex,
					pMeshAttrib,
				    pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[0],
					pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[1],
					pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[2],
					pLoopBuf[loopBufIndex].bc
				);
			}
			else {
				memcpy(attribAsVoid(pDestAttrib, dataIndex),
				       attribAsVoid(pMeshAttrib, meshDataIndex),
				       getAttribSize(pMeshAttrib->type));
			}
		}
	}
}

static
void simpleCopyAttribs(AttribArray *pDestAttribs, AttribArray *pMapAttribs,
					   AttribArray *pMeshAttribs, int32_t destDataIndex,
					   int32_t srcDataIndex, int32_t indexOrigin) {
	for (int32_t i = 0; i < pDestAttribs->count; ++i) {
		switch (pDestAttribs->pArr[i].origin) {
			case (RUVM_ATTRIB_ORIGIN_COMMON): {
				RuvmAttrib *pSrcAttrib;
				if (indexOrigin) {
					pSrcAttrib =
						getAttrib(pDestAttribs->pArr[i].name, pMapAttribs);
				}
				else {
					pSrcAttrib =
						getAttrib(pDestAttribs->pArr[i].name, pMeshAttribs);
				}
				break;
			}
			case (RUVM_ATTRIB_ORIGIN_MAP): {
				if (!indexOrigin) {
					//index is a meshIn index
					continue;
				}
				RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs->pArr[i].name,
												   pMapAttribs);
				memcpy(attribAsVoid(pDestAttribs->pArr + i, destDataIndex),
					   attribAsVoid(pMapAttrib, srcDataIndex),
					   getAttribSize(pMapAttrib->type));
				break;
			}
			case (RUVM_ATTRIB_ORIGIN_MESH_IN): {
				if (indexOrigin) {
					//index is a map index
					continue;
				}
				RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs->pArr[i].name,
													  pMeshAttribs);
				memcpy(attribAsVoid(pDestAttribs->pArr + i, destDataIndex),
					   attribAsVoid(pMeshAttrib, srcDataIndex),
					   getAttribSize(pMeshAttrib->type));
				break;
			}
		}
	}
}

static
void initEdgeTableEntry(MappingJobVars *pVars, LocalEdge *pEntry,
                        AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						int32_t refEdge, int32_t refFace, int32_t isMapEdge) {
	bool realloced = false;
	BufMeshIndex edge = bufMeshAddEdge(&pVars->alloc, pBufMesh, !isMapEdge, pVars->pDpVars, &realloced);
	pAcfVars->edge = edge.index;
	pEntry->edge = edge.index;
	simpleCopyAttribs(&asMesh(pBufMesh)->mesh.edgeAttribs,
	                  &pVars->pMap->mesh.mesh.edgeAttribs,
					  &pVars->mesh.mesh.edgeAttribs,
					  edge.realIndex, refEdge, isMapEdge);
	pEntry->refEdge = refEdge;
	pEntry->refFace = refFace;
}

static
int32_t getRefEdge(MappingJobVars *pVars, FaceRange *pRuvmFace,
                   FaceRange *pBaseFace, LoopBuf *pLoopBuf,
				   int32_t loopBufIndex) {
	if (pLoopBuf[loopBufIndex].isRuvm) {
		int32_t ruvmLoop = pLoopBuf[loopBufIndex].ruvmLoop;
		return pVars->pMap->mesh.mesh.pEdges[pRuvmFace->start + ruvmLoop];
	}
	else {
		int32_t baseLoop = pLoopBuf[loopBufIndex].baseLoop;
		return pVars->mesh.mesh.pEdges[pBaseFace->start + baseLoop];
	}
}

static
void addEdge(MappingJobVars *pVars, int32_t loopBufIndex, BufMesh *pBufMesh,
             LoopBuf *pLoopBuf, RuvmAlloc *pAlloc, int32_t refFace,
			 AddClippedFaceVars *pAcfVars, FaceRange *pBaseFace,
			 FaceRange *pRuvmFace) {
	int32_t refEdge =
		getRefEdge(pVars, pRuvmFace, pBaseFace, pLoopBuf, loopBufIndex);
	int32_t isMapEdge = pLoopBuf[loopBufIndex].isRuvm;
	int32_t key = isMapEdge ? refEdge : (refEdge + 1) * -1;
	int32_t hash =
		ruvmFnvHash((uint8_t *)&key, 4, pVars->localTables.edgeTableSize);
	LocalEdge *pEntry = pVars->localTables.pEdgeTable + hash;
	int32_t depth = 0;
	do {
		if (!pEntry->loopCount) {
			initEdgeTableEntry(pVars, pEntry, pAcfVars, pBufMesh, refEdge,
			                   refFace, isMapEdge);
			break;
		}
		int32_t match = pEntry->refEdge == refEdge &&
		                pEntry->refFace == refFace;
		if (match) {
			pAcfVars->edge = pEntry->edge;
			break;
		}
		if (!pEntry->pNext) {
			pEntry = pEntry->pNext = pAlloc->pCalloc(1, sizeof(LocalEdge));
			initEdgeTableEntry(pVars, pEntry,pAcfVars, pBufMesh, refEdge,
			                   refFace, isMapEdge);
			break;
		}
		RUVM_ASSERT("", pEntry->pNext && pEntry->pNext->pNext <= (LocalEdge *)1000000000000000);
		pEntry = pEntry->pNext;
		RUVM_ASSERT("", depth >= 0 && depth < 1000);
		depth++;
	} while(1);
	pEntry->loopCount++;
}

static
void addNewLoopAndOrVert(MappingJobVars *pVars, int32_t loopBufIndex,
                         AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						 LoopBuf *pLoopBuf, FaceRange *pBaseFace, FaceRange *pMapFace) {
		bool realloced = false;
		BufMeshIndex vert = bufMeshAddVert(&pVars->alloc, pBufMesh, true, pVars->pDpVars, &realloced);
		pAcfVars->vert = vert.index;
		asMesh(pBufMesh)->pVerts[vert.realIndex] = pLoopBuf[loopBufIndex].loop;
		//temporarily setting mesh data index to 0, as it's only needed if interpolation is disabled
		blendMapAndInAttribs(
			pBufMesh, &asMesh(pBufMesh)->mesh.vertAttribs,
			&pVars->pMap->mesh.mesh.vertAttribs,
			&pVars->mesh.mesh.vertAttribs,
			pLoopBuf, loopBufIndex, vert.realIndex,
			pLoopBuf[loopBufIndex].ruvmLoop, 0,
			pVars->pCommonAttribList->pVert,
			pVars->pCommonAttribList->vertCount,
			pBaseFace, pMapFace, RUVM_DOMAIN_VERT, NULL
		);
}

static
void initMapVertTableEntry(MappingJobVars *pVars, int32_t loopBufIndex,
                           AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						   LoopBuf *pLoopBuf, LocalVert *pEntry,
						   FaceRange baseFace, int32_t ruvmVert, FaceRange *pMapFace) {
	bool realloced = false;
	BufMeshIndex vert = bufMeshAddVert(&pVars->alloc, pBufMesh, false, pVars->pDpVars, &realloced);
	pAcfVars->vert = vert.index;
	asMesh(pBufMesh)->pVerts[vert.realIndex] = pLoopBuf[loopBufIndex].loop;
	pEntry->vert = vert.index;
	pEntry->mapVert = ruvmVert;
	pEntry->baseFace = baseFace.index;
	blendMapAndInAttribs(pBufMesh, &asMesh(pBufMesh)->mesh.vertAttribs,
						 &pVars->pMap->mesh.mesh.vertAttribs,
						 &pVars->mesh.mesh.vertAttribs,
						 pLoopBuf, loopBufIndex, vert.realIndex,
						 pLoopBuf[loopBufIndex].ruvmLoop, 0,
						 pVars->pCommonAttribList->pVert,
						 pVars->pCommonAttribList->vertCount,
						 &baseFace, pMapFace, RUVM_DOMAIN_VERT, NULL);
}

static
void addRuvmLoopAndOrVert(MappingJobVars *pVars, int32_t loopBufIndex,
                          AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						  LoopBuf *pLoopBufEntry, RuvmAlloc *pAlloc,
						  FaceRange baseFace, FaceRange *pRuvmFace) {
	int32_t ruvmLoop = pRuvmFace->start + pLoopBufEntry[loopBufIndex].ruvmLoop;
	uint32_t uRuvmVert = pVars->pMap->mesh.mesh.pLoops[ruvmLoop];
	int32_t hash =
		ruvmFnvHash((uint8_t *)&uRuvmVert, 4, pVars->localTables.vertTableSize);
	LocalVert *pEntry = pVars->localTables.pVertTable + hash;
	do {
		if (!pEntry->loopSize) {
			initMapVertTableEntry(pVars, loopBufIndex, pAcfVars,
			                      pBufMesh, pLoopBufEntry, pEntry, baseFace,
								  uRuvmVert, pRuvmFace);
			break;
		}
		//TODO should you be checking tile here as well?
		int32_t match = pEntry->mapVert == uRuvmVert &&
		                pEntry->baseFace == baseFace.index;
		if (match) {
			pAcfVars->vert = pEntry->vert;
			break;
		}
		if (!pEntry->pNext) {
			pEntry = pEntry->pNext = pAlloc->pCalloc(1, sizeof(LocalVert));
			initMapVertTableEntry(pVars, loopBufIndex, pAcfVars,
			                      pBufMesh, pLoopBufEntry, pEntry, baseFace,
								  uRuvmVert, pRuvmFace);
			break;
		}
		pEntry = pEntry->pNext;
	} while (1);
	pEntry->loopSize++;
}

static
void setBorderFaceMapAttrib(BorderFace *pEntry, UBitField8 *pArr,
                            int32_t loop, int32_t value) {
	int32_t len = 3 + pEntry->memType;
	setBitArr(pArr, loop, value, len);
}

static
void initBorderTableEntry(MappingJobVars *pVars, AddClippedFaceVars *pAcfVars,
                          BorderFace *pEntry, FaceRange *pMapFace,
                          V2_I32 tile, LoopBufWrap *pLoopBuf, FaceRange baseFace,
                          bool faceWindDir, bool mapFaceWindDir, int32_t memType,
                          Segments *pSegments) {
	pEntry->memType = memType;
	pEntry->face = pAcfVars->face;
	pEntry->faceIndex = pMapFace->index;
	pEntry->tileX = *(uint64_t *)&tile.d[0];
	pEntry->tileY = *(uint64_t *)&tile.d[1];
	pEntry->job = pVars->id;
	pEntry->baseFace = baseFace.index;
	pEntry->inOrient = faceWindDir;
	pEntry->mapOrient = mapFaceWindDir;

	BorderFaceBitArrs bitArrs;
	getBorderFaceBitArrs(pEntry, &bitArrs);

	RUVM_ASSERT("", pLoopBuf->size <= 64);
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		LoopBuf *pLoop = pLoopBuf->buf + i;
		if (pLoop->onLine != 0) {
			setBitArr(bitArrs.pOnLine, i, true, 1);
		}
		if (pLoop->isRuvm) {
			setBitArr(bitArrs.pIsRuvm, i, true, 1);
		}
		if (pLoop->ruvmLoop) {
			setBorderFaceMapAttrib(pEntry, bitArrs.pRuvmLoop, i, pLoop->ruvmLoop);
		}
		if (pLoop->isBaseLoop) {
			setBitArr(bitArrs.pOnInVert, i, true, 1);
		}
		if (!pLoop->isRuvm && pLoop->segment) {
			int32_t index = pLoop->segment - 1;
			int32_t j = 1;
			//if map face orientation is inverted, then the position
			//of each inloop in the segments array is offset by 1.
			//This is because pLoop->baseLoop is decremented at the end of
			//clipping to account for the inverted wind order
			int32_t segIndex = mapFaceWindDir ?
				pLoop->baseLoop : (pLoop->baseLoop + 1) % baseFace.size;
			for (j; j < pSegments[segIndex].count; ++j) {
				if (pSegments[segIndex].pIndices[j] == index) {
					break;
				}
			}
			j--;
			if (j) {
				setBorderFaceMapAttrib(pEntry, bitArrs.pSegment, i, j);
			}
		}
		// Only add baseloop for ruvm if online, otherwise value will
		// will not fit within 2 bits
		if ((!pLoop->isRuvm || pLoop->onLine) && pLoop->baseLoop) {
			setBitArr(bitArrs.pBaseLoop, i, pLoop->baseLoop, 2);
		}
	}
}

static
void addFaceToBorderTable(MappingJobVars *pVars, AddClippedFaceVars *pAcfVars,
                          LoopBufWrap *pLoopBuf, FaceRange *pMapFace,
						  V2_I32 tile, FaceRange baseFace, bool faceWindDir,
                          bool mapFaceWindDir, Segments *pSegments) {
	int32_t memType = getBorderFaceMemType(pMapFace->size, pLoopBuf->size);
	int32_t allocSize = getBorderFaceSize(memType);
	int32_t hash =
		ruvmFnvHash((uint8_t *)&pMapFace->index, 4, pVars->borderTable.size);
	BorderBucket *pBucket = pVars->borderTable.pTable + hash;
	BorderFace *pEntry = pBucket->pEntry;
	if (!pEntry) {
		pEntry = pBucket->pEntry = pVars->alloc.pCalloc(1, allocSize);
		initBorderTableEntry(pVars, pAcfVars, pEntry, pMapFace, tile,
		                     pLoopBuf, baseFace, faceWindDir, mapFaceWindDir,
		                     memType, pSegments);
	}
	else {
		do {
			if (pEntry->faceIndex == pMapFace->index) {
				if (pBucket->pTail) {
					pEntry = pBucket->pTail;
				}
				pEntry = pEntry->pNext = pVars->alloc.pCalloc(1, allocSize);
				pBucket->pTail = pEntry;
				initBorderTableEntry(pVars, pAcfVars, pEntry, pMapFace, tile,
				                     pLoopBuf, baseFace, faceWindDir, mapFaceWindDir,
				                     memType, pSegments);
				break;
			}
			if (!pBucket->pNext) {
				pBucket = pBucket->pNext =
					pVars->alloc.pCalloc(1, sizeof(BorderBucket));
				pEntry =
					pBucket->pEntry = pVars->alloc.pCalloc(1, allocSize);
				initBorderTableEntry(pVars, pAcfVars, pEntry, pMapFace, tile,
				                     pLoopBuf, baseFace, faceWindDir, mapFaceWindDir,
				                     memType, pSegments);
				break;
			}
			pBucket = pBucket->pNext;
			pEntry = pBucket->pEntry;
		} while (1);
	}
}

static
void addInFace(MappingJobVars *pVars, int32_t face, FaceRange *pBaseFace, FaceRange *pMapFace) {
	InFaceArr *pInFaceEntry = pVars->pInFaces + face;
	pInFaceEntry->pArr = pVars->alloc.pMalloc(sizeof(int32_t));
	*pInFaceEntry->pArr = pBaseFace->index + pVars->inFaceOffset;
	pInFaceEntry->count = 1;
	pInFaceEntry->usg = pMapFace->index;
	int32_t faceCount = pVars->bufMesh.mesh.mesh.faceCount;
	RUVM_ASSERT("", faceCount <= pVars->inFaceSize);
	if (pVars->inFaceSize == faceCount) {
		pVars->inFaceSize *= 2;
		pVars->pInFaces =
			pVars->alloc.pRealloc(pVars->pInFaces, sizeof(InFaceArr) * pVars->inFaceSize);
	}
}

static
void addClippedFaceToBufMesh(MappingJobVars *pVars, LoopBufWrap *pLoopBuf,
							 FaceRange ruvmFace, V2_I32 tile, FaceRange baseFace,
                             bool faceWindDir, bool mapFaceWindDir,
                             Segments *pSegments, LoopAncestors *pAncestors) {
	bool realloced = false;
	AddClippedFaceVars acfVars = {0};
	BufMesh *pBufMesh = &pVars->bufMesh;
	bool isBorderFace = pLoopBuf->edgeFace || pLoopBuf->onLine;
	bool invert = !faceWindDir && !isBorderFace;
	int32_t size = pLoopBuf->size;
	for (int32_t i = invert ? size - 1 : 0;
		invert ? i >= 0 : i < size;
		invert ? --i : ++i) {

		int32_t refFace;
		int32_t isRuvm = pLoopBuf->buf[i].isRuvm;
		if (!isRuvm || pLoopBuf->buf[i].onLine) {
			//TODO these only add verts, not loops. Outdated name?
			addNewLoopAndOrVert(pVars, i, &acfVars, &pVars->bufMesh,
			                    pLoopBuf->buf, &baseFace, &ruvmFace);
			refFace = ruvmFace.index;
		}
		else {
			addRuvmLoopAndOrVert(pVars, i, &acfVars, &pVars->bufMesh,
			                     pLoopBuf->buf, &pVars->alloc, baseFace,
								 &ruvmFace);
			refFace = baseFace.index;
		}
		BufMeshIndex loop = bufMeshAddLoop(&pVars->alloc, pBufMesh, isBorderFace, pVars->pDpVars, &realloced);
		acfVars.loop = loop.index;
		if (invert ? i == size - 1 : !i) {
			acfVars.loopStart = loop.index;
		}
		pBufMesh->pW[loop.realIndex] = pLoopBuf->buf[i].uvw.d[2];
		pBufMesh->pInNormal[loop.realIndex] = pLoopBuf->buf[i].projNormal;
		pBufMesh->pInTangent[loop.realIndex] = pLoopBuf->buf[i].inTangent;
		pBufMesh->pAlpha[loop.realIndex] = pLoopBuf->buf[i].alpha;
		pBufMesh->pInTSign[loop.realIndex] = pLoopBuf->buf[i].inTSign;
		asMesh(pBufMesh)->mesh.pLoops[loop.realIndex] = acfVars.vert;
		asMesh(pBufMesh)->pNormals[loop.realIndex] = pLoopBuf->buf[i].normal;
		asMesh(pBufMesh)->pUvs[loop.realIndex] = pLoopBuf->buf[i].uv;
		blendMapAndInAttribs(&pVars->bufMesh, &asMesh(pBufMesh)->mesh.loopAttribs,
							 &pVars->pMap->mesh.mesh.loopAttribs,
							 &pVars->mesh.mesh.loopAttribs,
							 pLoopBuf->buf, i, loop.realIndex,
							 ruvmFace.start + pLoopBuf->buf[i].ruvmLoop,
							 baseFace.start,
							 pVars->pCommonAttribList->pLoop,
							 pVars->pCommonAttribList->loopCount, &baseFace, &ruvmFace,
		                     RUVM_DOMAIN_LOOP, pAncestors);
		addEdge(pVars, i, &pVars->bufMesh, pLoopBuf->buf, &pVars->alloc,
		        refFace, &acfVars, &baseFace, &ruvmFace);
		asMesh(pBufMesh)->mesh.pEdges[loop.realIndex] = acfVars.edge;
	}
	BufMeshIndex face = bufMeshAddFace(&pVars->alloc, pBufMesh, isBorderFace, pVars->pDpVars, &realloced);
	if (pVars->getInFaces && !isBorderFace) {
		addInFace(pVars, face.index, &baseFace, &ruvmFace);
	}
	acfVars.face = face.index;
	asMesh(pBufMesh)->mesh.pFaces[face.realIndex] = acfVars.loopStart;
	blendMapAndInAttribs(&pVars->bufMesh, &asMesh(pBufMesh)->mesh.faceAttribs,
						 &pVars->pMap->mesh.mesh.faceAttribs,
						 &pVars->mesh.mesh.faceAttribs,
						 pLoopBuf->buf, 0, face.realIndex,
						 ruvmFace.index, baseFace.index,
						 pVars->pCommonAttribList->pFace,
						 pVars->pCommonAttribList->faceCount, &baseFace, &ruvmFace,
	                     RUVM_DOMAIN_FACE, NULL);
	if (isBorderFace) {
		addFaceToBorderTable(pVars, &acfVars, pLoopBuf, &ruvmFace,
		                     tile, baseFace, faceWindDir, mapFaceWindDir,
		                     pSegments);
	}
}

static
bool isOnLine(LoopBufWrap *pLoopBuf) {
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		if (pLoopBuf->buf[i].onLine) {
			return true;
		}
	}
	return false;
}

Result ruvmMapToSingleFace(MappingJobVars *pVars, FaceCellsTable *pFaceCellsTable,
                           DebugAndPerfVars *pDpVars,
					       V2_F32 fTileMin, V2_I32 tile, FaceRange baseFace) {
	if (baseFace.size < 3 || baseFace.size > 4) {
		return RUVM_SUCCESS;
	}
	FaceBounds bounds = {0};
	getFaceBounds(&bounds, pVars->mesh.pUvs, baseFace);
	BaseTriVerts baseTri = {0};
	pDpVars->facesNotSkipped++;
	RUVM_ASSERT("", baseFace.size >= 3 && baseFace.size <= 4);
	for (int32_t i = 0; i < baseFace.size; ++i) {
		int32_t loop = baseFace.start + i;
		baseTri.uv[i] = _(pVars->mesh.pUvs[loop] V2SUB fTileMin);
		baseTri.xyz[i] = pVars->mesh.pVerts[pVars->mesh.mesh.pLoops[loop]];
	}
	getTriScale(baseFace.size, &baseTri);
	bool degenerate;
	degenerate = v3DegenerateTri(baseTri.xyz[0], baseTri.xyz[1],
	                             baseTri.xyz[2], .0f);
	if (baseFace.size == 4) {
		degenerate |= v3DegenerateTri(baseTri.xyz[2], baseTri.xyz[3],
		                              baseTri.xyz[0], .0f);
	}
	if (degenerate) {
		return RUVM_ERROR;
	}
	int32_t faceWindingDir =
		calcFaceOrientation(&pVars->mesh, &baseFace, true);
	if (faceWindingDir == 2) {
		//face is degenerate
		return RUVM_ERROR;
	}
	Segments *pSegments = pVars->alloc.pCalloc(baseFace.size, sizeof(Segments));
	for (int32_t i = 0; i < baseFace.size; ++i) {
		pSegments[i].size = 3;
		pSegments[i].pArr = pVars->alloc.pCalloc(pSegments[i].size, sizeof(float));
		pSegments[i].pIndices = pVars->alloc.pCalloc(pSegments[i].size, sizeof(int32_t));
		pSegments[i].pArr[0] = -FLT_MAX;
		pSegments[i].pIndices[0] = -1;
		pSegments[i].count = 1;
	}
	LoopAncestors ancestors = {.size = 2};
	ancestors.pArr = pVars->alloc.pMalloc(sizeof(LoopBuf) * ancestors.size);
	for (int32_t i = 0; i < pFaceCellsTable->pFaceCells[baseFace.index].cellSize; ++i) {
		RUVM_ASSERT("", asMesh(&pVars->bufMesh)->mesh.faceCount >= 0);
		RUVM_ASSERT("", asMesh(&pVars->bufMesh)->mesh.faceCount <
		                asMesh(&pVars->bufMesh)->faceBufSize);
		Cell* pCell = pFaceCellsTable->pFaceCells[baseFace.index].pCells[i];
		RUVM_ASSERT("", pCell->localIndex >= 0 && pCell->localIndex < 4);
		RUVM_ASSERT("", pCell->initialized % 2 == pCell->initialized);
		int32_t* pCellFaces;
		Range range = {0};
		if (pFaceCellsTable->pFaceCells[baseFace.index].pCellType[i]) {
			pCellFaces = pCell->pEdgeFaces;
			range = pFaceCellsTable->pFaceCells[baseFace.index].pRanges[i];
			//range.start = 0;
			//range.end = pCell->edgeFaceSize;
		}
		else if (pFaceCellsTable->pFaceCells[baseFace.index].pCellType[i] != 1) {
			pCellFaces = pCell->pFaces;
			range.start = 0;
			range.end = pCell->faceSize;
		}
		else {
			continue;
		}
		for (int32_t j = range.start; j < range.end; ++j) {
			pDpVars->totalFacesComp++;
			FaceRange ruvmFace =
				getFaceRange(&pVars->pMap->mesh.mesh, pCellFaces[j], false);
			if (!checkFaceIsInBounds(_(bounds.fMin V2SUB fTileMin),
									 _(bounds.fMax V2SUB fTileMin),
									 ruvmFace, &pVars->pMap->mesh)) {
				continue;
			}
			pDpVars->facesNotSkipped++;
			for (int32_t k = 0; k < baseFace.size; ++k) {
				if (pSegments[k].count > 1) {
					memset(pSegments[k].pIndices + 1, 0, pSegments[k].count - 1);
					pSegments[k].count = 1;
				}
			}
			LoopBufWrap loopBuf = {0};
			loopBuf.size = ruvmFace.size;
			for (int32_t k = 0; k < ruvmFace.size; ++k) {
				int32_t vertIndex = pVars->pMap->mesh.mesh.pLoops[ruvmFace.start + k];
				LoopBuf *pLoop = loopBuf.buf + k;
				pLoop->preserve = 0;
				pLoop->isRuvm = 1;
				pLoop->baseLoop = (vertIndex + 1) * -1;
				pLoop->loop = pVars->pMap->mesh.pVerts[vertIndex];
				pLoop->loop.d[0] += fTileMin.d[0];
				pLoop->loop.d[1] += fTileMin.d[1];
				pLoop->ruvmLoop = k;
				pLoop->normal =
					pVars->pMap->mesh.pNormals[ruvmFace.start + k];
			}
			int32_t mapFaceWindDir =
				calcFaceOrientation(&pVars->pMap->mesh, &ruvmFace, false);
			loopBuf.lastInLoop = mapFaceWindDir ? 0 : baseFace.size - 1;
			LoopBufWrap *pLoopBuf = &loopBuf;
			ancestors.count = 0;
			do {
				if (!pLoopBuf->invalid) {
					clipRuvmFaceAgainstBaseFace(pVars, baseFace, pLoopBuf,
												faceWindingDir, mapFaceWindDir,
					                            pSegments, &ancestors);
				}
				pLoopBuf = pLoopBuf->pNext;
			} while (pLoopBuf);
			for (int32_t k = 0; k < baseFace.size; ++k) {
				if (pSegments[k].count > 2) {
					fInsertionSort(pSegments[k].pIndices + 1, pSegments[k].count - 1, pSegments[k].pArr + 1);
				}
			}
			pLoopBuf = &loopBuf;
			int32_t depth = 0;
			do{
				if (!pLoopBuf->invalid) {
					if (pLoopBuf->size >= 3) {
						//TODO move this after addClippedFaceToBufMesh,
						//     that way, you can skip merged ruvm verts,
						//     and you can also remove the use of LoopBuf,
						//     and turn the func into a generic (generic for BufMesh)
						//     one that can be used on deffered loops in MergeBoundsFaces.c
						transformClippedFaceFromUvToXyz(pLoopBuf, ruvmFace, baseFace, &baseTri,
														pVars, fTileMin, pVars->wScale);
						int32_t faceIndex = pVars->bufMesh.mesh.mesh.faceCount;
						addClippedFaceToBufMesh(pVars, pLoopBuf, ruvmFace, tile,
						                        baseFace, faceWindingDir,
						                        mapFaceWindDir, pSegments, &ancestors);
					}
				}
				LoopBufWrap *pNextBuf = pLoopBuf->pNext;
				if (depth) {
					pVars->alloc.pFree(pLoopBuf);
				}
				pLoopBuf = pNextBuf;
				depth++;
			} while(pLoopBuf);
		}
	}
	pVars->alloc.pFree(ancestors.pArr);
	for (int32_t i = 0; i < baseFace.size; ++i) {
		pVars->alloc.pFree(pSegments[i].pArr);
		pVars->alloc.pFree(pSegments[i].pIndices);
	}
	pVars->alloc.pFree(pSegments);
	return RUVM_SUCCESS;
}
