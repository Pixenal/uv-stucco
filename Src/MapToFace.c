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
	int32_t idx;
	int32_t edgeIdx;
	int32_t idxNext;
	int8_t localIdx;
	int8_t localIdxPrev;
	int8_t localIdxNext;
	V2_F32 vert;
	V2_F32 vertNext;
	V2_F32 dir;
	V2_F32 dirBack;
	bool flipEdgeDir;
} CornerInfo;

typedef struct {
	CornerBuf *pArr;
	int32_t count;
	int32_t size;
} CornerAncestors;

typedef struct {
	int32_t cornerStart;
	int32_t face;
	int32_t corner;
	int32_t  edge;
	int32_t  vert;
} AddClippedFaceVars;

typedef struct {
	CornerBufWrap *pIsland;
	CornerBufWrap *pPending;
	int32_t corner;
} IslandIdxPair;

static
bool checkIfOnVert(CornerBufWrap *pCornerBuf, int32_t i, int32_t iNext) {
	return (pCornerBuf->buf[i].baseCorner == pCornerBuf->buf[iNext].baseCorner ||
	        pCornerBuf->buf[i].isBaseCorner || pCornerBuf->buf[iNext].isBaseCorner) &&
		    !pCornerBuf->buf[i].isRuvm && !pCornerBuf->buf[iNext].isRuvm;
}

static
void addInsideCornerToBuf(CornerBufWrap *pNewCornerBuf, CornerBufWrap *pCornerBuf,
                        int32_t *pInsideBuf, int32_t i, int32_t iNext,
                        int32_t iPrev, CornerInfo *pBaseCorner,
						IslandIdxPair *pIntersectCache, float *ptBuf,
                        int32_t *pCount, bool faceWindDir) {
	CornerBuf *pNewEntry = pNewCornerBuf->buf + pNewCornerBuf->size;
	pNewCornerBuf->buf[pNewCornerBuf->size] = pCornerBuf->buf[i];
	if (pInsideBuf[i] == 2) {
		//pNewCornerBuf->buf[pNewCornerBuf->size].onLine = true;
	}
	//using += so that base corners can be determined. ie, if an uvs
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
			calcIntersection(pCorner->corner, pCornerNext->corner, pBaseCorner->vert,
							 pBaseCorner->dir, NULL, NULL, &pNewEntry->alpha);
			pNewEntry->alpha *= -1.0f;
			ptBuf[*pCount] = pNewEntry->alpha;
			++*pCount;
		}
		if (pCornerBuf->buf[i].onLine) {
			//this corner already resided on a previous base edge,
			//it must then reside on a base vert, rather than an edge.
			//determine which vert in the edge it sits on:
			int32_t onLineBase;
			if (pCornerBuf->buf[i].corner.d[0] == pBaseCorner->vert.d[0] &&
				pCornerBuf->buf[i].corner.d[1] == pBaseCorner->vert.d[1]) {
				//on base vert
				onLineBase = pBaseCorner->localIdx;
			}
			else {
				//on next base vert
				onLineBase = pBaseCorner->localIdxNext;
			}
			pNewEntry->baseCorner = onLineBase;
			pNewEntry->isBaseCorner = true;
		}
		else if (pCornerBuf->buf[i].isRuvm) {
			//resides on base edge
			pNewEntry->baseCorner =
				pBaseCorner->localIdx;
		}
		pNewCornerBuf->onLine = true;
		pNewEntry->onLine = 1;
	}
	pNewCornerBuf->size++;
}

static
int32_t appendToAncestors(RuvmAlloc *pAlloc, CornerAncestors *pAncestors, CornerBuf *pCorner) {
	RUVM_ASSERT("", pAncestors->count <= pAncestors->size);
	if (pAncestors->count == pAncestors->size) {
		pAncestors->size *= 2;
		pAncestors->pArr = pAlloc->pRealloc(pAncestors->pArr, sizeof(CornerBuf) *
		                                    pAncestors->size);
	}
	int32_t idx = pAncestors->count;
	pAncestors->pArr[idx] = *pCorner;
	pAncestors->count++;
	return idx;
}

static
void addIntersectionToBuf(RuvmAlloc *pAlloc, CornerBufWrap *pNewCornerBuf, CornerBufWrap *pCornerBuf,
                          int32_t i, CornerInfo *pBaseCorner,
						  int32_t iNext, bool flippedWind,
                          IslandIdxPair *pIntersectCache, float *ptBuf,
                          int32_t *pCount, int32_t mapFaceWindDir, int32_t faceWindDir,
                          CornerAncestors *pAncestors) {
	pIntersectCache[*pCount].pIsland = pNewCornerBuf;
	pIntersectCache[*pCount].corner = pNewCornerBuf->size;
	CornerBuf *pCorner = pCornerBuf->buf + i;
	CornerBuf *pCornerNext = pCornerBuf->buf + iNext;
	CornerBuf *pNewEntry = pNewCornerBuf->buf + pNewCornerBuf->size;
	calcIntersection(pCorner->corner, pCornerNext->corner, pBaseCorner->vert,
	                 pBaseCorner->dir, &pNewEntry->corner, &pNewEntry->mapAlpha, &pNewEntry->alpha);
	if (true) {
		pNewEntry->alpha *= -1.0f;
	}
	//this attrib is lerped here instead of later like other attribs,
	//as it's needed to transform from uvw to xyz
	pNewEntry->normal = v3Lerp(pCorner->normal, pCornerNext->normal, pNewEntry->mapAlpha);
	//pNewEntry->normal = pCornerBuf->buf[i].normal;
	//V3_F32 up = {.0f, .0f, 1.0f};
	//pNewEntry->normal = up;
	if (checkIfOnVert(pCornerBuf, i, iNext)) {
		int32_t lastBaseCorner = mapFaceWindDir ?
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
		pNewEntry->baseCorner = pBaseCorner->idx;
		pNewEntry->isBaseCorner = false;
	}
	pNewEntry->ancestor =
		appendToAncestors(pAlloc, pAncestors, pCornerBuf->buf + i);
	pNewEntry->ancestorNext =
		appendToAncestors(pAlloc, pAncestors, pCornerBuf->buf + iNext);
	pNewEntry->isRuvm = false;
	pNewEntry->uvsCorner = pCornerBuf->buf[i].uvsCorner;
	ptBuf[*pCount] = pNewEntry->alpha;
	++*pCount;
	pNewCornerBuf->size++;
}

static
CornerBufWrap *createNewCornerBuf(RuvmAlloc *pAlloc) {
	CornerBufWrap *pCornerBuf = pAlloc->pCalloc(1, sizeof(CornerBufWrap));
	return pCornerBuf;
}

static
void initPendingMerge(RuvmAlloc *pAlloc, CornerBufWrap *pIsland) {
	pIsland->mergeSize = 3;
	pIsland->pPendingMerge =
		pAlloc->pCalloc(pIsland->mergeSize, sizeof(void *));
	pIsland->pPendingMerge[0] = -1;
	pIsland->mergeCount = 1;
}

static
void addToPendingMerge(RuvmAlloc *pAlloc,
                       CornerBufWrap *pIsland, int32_t value) {
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
void destroyPendingMerge(RuvmAlloc *pAlloc, CornerBufWrap *pIsland) {
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
void addIslandToPendingMerge(RuvmAlloc *pAlloc, IslandIdxPair *pCornerPair,
                             IslandIdxPair *pCornerPairNext, int32_t realiNext,
                             IslandIdxPair *pIntersectCache, int32_t cacheCount) {
	RUVM_ASSERT("", pCornerPair->pIsland->size > 0);
	RUVM_ASSERT("", pCornerPairNext->pIsland->size > 0);
	CornerBufWrap *pIsland = pCornerPair->pPending ?
		pCornerPair->pPending : pCornerPair->pIsland;
	if (pCornerPairNext->pPending == pIsland) {
		//already listed
		return;
	}
	CornerBufWrap *pIslandNext = pCornerPairNext->pIsland;
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
	pCornerPairNext->pIsland->invalid = true;
	//update references to invalid entry in list
	for (int32_t i = 0; i < cacheCount; ++i) {
		if (pIntersectCache[i].pIsland == pIslandNext) {
			pIntersectCache[i].pPending = pIsland;
		}
	}
}

static
void mergeIslands(CornerBufWrap *pIsland, IslandIdxPair *pIntersectCache) {
	//17 is the max islands possible with a 32 vert map face
	int32_t idxTable[18] = {-1};
	if (pIsland->mergeCount > 2) {
		insertionSort(idxTable + 1, pIsland->mergeCount - 1,
		              pIsland->pPendingMerge + 1);
	}
	for (int32_t i = 1; i < pIsland->mergeCount; ++i) {
		int32_t idxPending = pIsland->pPendingMerge[idxTable[i] + 1];
		CornerBufWrap *pIslandPending = pIntersectCache[idxPending].pIsland;
		RUVM_ASSERT("", pIslandPending->invalid);
		RUVM_ASSERT("", pIslandPending->size > 0);
		for (int32_t j = 0; j < pIslandPending->size; ++j) {
			pIsland->buf[pIsland->size + j] = pIslandPending->buf[j];
		}
		pIsland->size += pIslandPending->size;
	}
}

static
void setIsland(RuvmAlloc *pAlloc, CornerBufWrap **ppIsland,
               CornerBufWrap *pRoot, bool *pIn, int32_t inCorner,
               bool mapFaceWindDir) {
	if (!*pIn) {
		if (!*ppIsland) {
			*ppIsland = pRoot;
		}
		else {
			while ((*ppIsland)->pNext) {
				*ppIsland = (*ppIsland)->pNext;
			}
			*ppIsland = (*ppIsland)->pNext = createNewCornerBuf(pAlloc);
			(*ppIsland)->lastInCorner = inCorner;
			(*ppIsland)->lastInCorner += mapFaceWindDir ? 1 : -1;
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
void setSegment(IslandIdxPair *pCornerPair, Segments *pSegments, int32_t inCorner) {
	CornerBuf *pCorner = pCornerPair->pIsland->buf + pCornerPair->corner;
	if (pCorner->baseCorner == inCorner) {
		pCorner->segment = pSegments[inCorner].count;
	}
}

static
void setSegments(RuvmAlloc *pAlloc, float *ptBuf, Segments *pSegments,
                 IslandIdxPair *pIntersectCache, int32_t inCorner,
                 int32_t reali, int32_t realiNext) {
	Segments *pSegEntry = pSegments + inCorner;
	IslandIdxPair *pCorner = pIntersectCache + reali;
	IslandIdxPair *pCornerNext = pIntersectCache + realiNext;
	if (ptBuf[reali] > .0f || ptBuf[realiNext] > .0f) {
		setSegment(pCorner, pSegments, inCorner);
		setSegment(pCornerNext, pSegments, inCorner);
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
void clipRuvmFaceAgainstSingleCorner(MappingJobVars *pVars, CornerBufWrap *pCornerBuf,
                                   CornerBufWrap *pNewCornerBuf, int32_t *pInsideBuf,
                                   FaceRange *pInFace, CornerInfo *pBaseCorner,
								   V2_F32 baseCornerCross,
								   bool flippedWind,
								   int32_t mapFaceWindDir,
                                   int32_t faceWindDir, Segments *pSegments,
                                   CornerAncestors *pAncestors) {
	for (int32_t i = 0; i < pCornerBuf->size; ++i) {
		V2_F32 uvsVert = *(V2_F32 *)&pCornerBuf->buf[i].corner;
		V2_F32 uvRuvmDir = _(uvsVert V2SUB pBaseCorner->vert);
		float dot = _(baseCornerCross V2DOT uvRuvmDir);
		bool onLine = dot == .0f;
		pInsideBuf[i] = onLine ? -1 : (dot < .0f) ^ ((bool)mapFaceWindDir ^ (bool)faceWindDir);
	}
	bool in = false;
	CornerBufWrap *pIsland = NULL;
	//32 is the max intersections possible with a 32 vert map face
	IslandIdxPair intersectCache[32] = {0};
	float tBuf[33] = {-FLT_MAX}; //first element must be low for later sorting
	float *ptBuf = tBuf + 1;
	int32_t count = 0;
	for (int32_t i = 0; i < pCornerBuf->size; ++i) {
		int32_t iNext = (i + 1) % pCornerBuf->size;
		int32_t iPrev = i ? i - 1 : pCornerBuf->size - 1;
		if (pInsideBuf[i]) {
			//point is inside, or on the line
			setIsland(&pVars->alloc, &pIsland, pNewCornerBuf, &in,
			          pBaseCorner->localIdx, mapFaceWindDir);
			addInsideCornerToBuf(pIsland, pCornerBuf, pInsideBuf, i, iNext, iPrev,
			                   pBaseCorner, intersectCache, ptBuf, &count,
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
			setIsland(&pVars->alloc, &pIsland, pNewCornerBuf, &in,
			          pBaseCorner->localIdx, mapFaceWindDir);
			addIntersectionToBuf(&pVars->alloc, pIsland, pCornerBuf, i, pBaseCorner,
			                     iNext, flippedWind,
			                     intersectCache, ptBuf, &count,
			                     mapFaceWindDir, faceWindDir,
			                     pAncestors);
			pIsland->edgeFace = true;
		}
	}
	pIsland = pNewCornerBuf; //reset to root
	if (!pIsland || count == 0) {
		return;
	}
	RUVM_ASSERT("", count >= 2);
	RUVM_ASSERT("should be even", !(count % 2));
	int32_t idxTable[65] = {-1}; //first element to point to first tbuf element
	int32_t *pIdxTable = idxTable + 1;
	if (pBaseCorner->flipEdgeDir) {
		for (int32_t i = 0; i < count; ++i) {
			ptBuf[i] = 1.0f - ptBuf[i];
		}
	}
	fInsertionSort(pIdxTable, count, ptBuf);
	int32_t inCorner = pBaseCorner->localIdx;
	if (!pIsland->pNext) {
		setSegments(&pVars->alloc, ptBuf, pSegments, intersectCache, inCorner,
		            pIdxTable[0], pIdxTable[1]);
		return;
	}
	for (int32_t i = 0; i < count; i += 2) {
		int32_t reali = pIdxTable[i];
		int32_t realiNext = pIdxTable[i + 1];
		IslandIdxPair *pCorner = intersectCache + reali;
		IslandIdxPair *pCornerNext = intersectCache + realiNext;
		setSegments(&pVars->alloc, ptBuf, pSegments, intersectCache, inCorner,
		            reali, realiNext);
		//RUVM_ASSERT("", !pCorner->pIsland->invalid && !pCornerNext->pIsland->invalid);
		if (pCorner->pIsland != pCornerNext->pIsland) {
			bool flip = reali > realiNext;
			if (flip) {
				IslandIdxPair *pBuf = pCornerNext;
				pCornerNext = pCorner;
				pCorner = pBuf;
				realiNext = reali;
			}
			addIslandToPendingMerge(&pVars->alloc, pCorner, pCornerNext,
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
void cornerBufDecrementBaseCorners(CornerBufWrap* pCornerBuf, FaceRange* pBaseFace) {
	for (int i = 0; i < pCornerBuf->size; ++i) {
		int8_t* pBaseCorner = &pCornerBuf->buf[i].baseCorner;
		*pBaseCorner = *pBaseCorner ? *pBaseCorner - 1 : pBaseFace->size - 1;
	}
}

static
void clipRuvmFaceAgainstBaseFace(MappingJobVars *pVars, FaceRange baseFace,
                                 CornerBufWrap *pCornerBuf,
								 int32_t faceWindingDir, int32_t mapFaceWindDir,
                                 Segments *pSegments, CornerAncestors *pAncestors) {
	bool flippedWind = !faceWindingDir || !mapFaceWindDir;
	int32_t start = pCornerBuf->lastInCorner;
	for (int32_t i = start; mapFaceWindDir ? i < baseFace.size : i >= 0; mapFaceWindDir ? ++i : --i) {
		RUVM_ASSERT("", i >= 0 && i < baseFace.size);
		CornerInfo baseCorner = {0};
		//why is both this and idxLocal local? Shouldn't this be absolute?
		baseCorner.idx = i;
		baseCorner.vert = pVars->mesh.pUvs[i + baseFace.start];
		int32_t uvNextIdxLocal;
		int32_t uvPrevIdxLocal;
		int32_t edgeCorner;
		if (mapFaceWindDir) {
			uvNextIdxLocal = ((i + 1) % baseFace.size);
			uvPrevIdxLocal = i ? i - 1 : baseFace.size - 1;
			edgeCorner = baseFace.start + i;
		}
		else {
			uvNextIdxLocal = i ? i - 1 : baseFace.size - 1;
			uvPrevIdxLocal = ((i + 1) % baseFace.size);
			edgeCorner = baseFace.start + uvNextIdxLocal;
		}
		baseCorner.edgeIdx = pVars->mesh.mesh.pEdges[edgeCorner];
		//TODO rename verts in pEdgeVerts to corners. They're not verts anymore.
		int32_t *pEdgeCorners = pVars->pEdgeVerts[baseCorner.edgeIdx].verts;
		RUVM_ASSERT("", pEdgeCorners[0] == edgeCorner || pEdgeCorners[1] == edgeCorner);
		baseCorner.flipEdgeDir = edgeCorner != pEdgeCorners[0];
		int32_t uvNextIdx = uvNextIdxLocal + baseFace.start;
		baseCorner.vertNext = pVars->mesh.pUvs[uvNextIdx];
		baseCorner.idxNext = uvNextIdxLocal;
		baseCorner.localIdx = i;
		baseCorner.localIdxPrev = uvPrevIdxLocal;
		baseCorner.localIdxNext = uvNextIdxLocal;
		baseCorner.dir = _(baseCorner.vertNext V2SUB baseCorner.vert);
		baseCorner.dirBack = _(baseCorner.vert V2SUB baseCorner.vertNext);
		CornerBufWrap newCornerBuf = {
			.edgeFace = pCornerBuf->edgeFace,
			.onLine = pCornerBuf->onLine
		};
		int32_t insideBuf[65] = {0};
		V2_F32 baseCornerCross = v2Cross(baseCorner.dir);
		clipRuvmFaceAgainstSingleCorner(pVars, pCornerBuf, &newCornerBuf, insideBuf,
		         						&baseFace, &baseCorner, baseCornerCross,
										flippedWind,
		                                mapFaceWindDir, faceWindingDir, pSegments,
		                                pAncestors);

		if (newCornerBuf.size <= 2) {
			pCornerBuf->size = newCornerBuf.size;
			return;
		}
		CornerBufWrap *pTail = &newCornerBuf;
		while (pTail->pNext) {
			pTail = pTail->pNext;
		}
		pTail->pNext = pCornerBuf->pNext;
		*pCornerBuf = newCornerBuf;
	}
	if (!mapFaceWindDir) {
		cornerBufDecrementBaseCorners(pCornerBuf, &baseFace);
	}
}

static
V3_F32 getCornerRealNormal(Mesh *pMesh, FaceRange *pFace, int32_t corner) {
	int32_t a = corner == 0 ? pFace->size - 1 : corner - 1;
	int32_t c = (corner + 1) % pFace->size;
	int32_t aIdx = pMesh->mesh.pCorners[pFace->start + a];
	int32_t bIdx = pMesh->mesh.pCorners[pFace->start + corner];
	int32_t cIdx = pMesh->mesh.pCorners[pFace->start + c];
	V3_F32 ba = _(pMesh->pVerts[aIdx] V3SUB pMesh->pVerts[bIdx]);
	V3_F32 bc = _(pMesh->pVerts[cIdx] V3SUB pMesh->pVerts[bIdx]);
	return v3Normalize(_(ba V3CROSS bc));
}

static
void transformClippedFaceFromUvToXyz(CornerBufWrap *pCornerBuf, FaceRange uvsFace,
									 FaceRange inFace, BaseTriVerts *pBaseTri,
									 MappingJobVars *pVars, V2_F32 tileMin, float wScale) {
	Mesh *pMapMesh = &pVars->pMap->mesh;
	//replace j, k, l, etc, in code that was moved to a func, but not updated,
	//eg, the below corner should use i, not j
	for (int32_t j = 0; j < pCornerBuf->size; ++j) {
		CornerBuf *pCorner = pCornerBuf->buf + j;
		pCorner->uvw = pCorner->corner;
		//uv is just the vert position before transform, so set that here
		pCorner->uv = *(V2_F32 *)&pCorner->uvw;
		//find enclosing triangle
		_((V2_F32 *)&pCorner->uvw V2SUBEQL tileMin);
		V3_F32 vertBc = getBarycentricInFace(pBaseTri->uv, pCorner->triCorners,
		                                     inFace.size, *(V2_F32 *)&pCorner->uvw);
		int8_t *pTriCorners = pCorner->triCorners;
		V3_F32 vertsXyz[3];
		int32_t inVerts[3];
		for (int32_t i = 0; i < 3; ++i) {
			inVerts[i] =
				pVars->mesh.mesh.pCorners[inFace.start + pTriCorners[i]];
			vertsXyz[i] = pVars->mesh.pVerts[inVerts[i]];
		}
		pCorner->bc = vertBc;
		pCorner->tbn = getInterpolatedTbn(&pVars->mesh, &inFace, pTriCorners, vertBc);
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
		pCorner->uvw.d[2] *= inVertsWScaleMul;
		pCorner->inTangent = *(V3_F32 *)&pCorner->tbn.d[0];
		pCorner->projNormal = *(V3_F32 *)&pCorner->tbn.d[2];
		pCorner->inTSign = pVars->mesh.pTSigns[inFace.start + pTriCorners[0]];
		if (pMapMesh->pUsg && pCorner->isRuvm) {
			V3_F32 usgBc = {0};
			sampleUsg(pCorner->uvsCorner, pCorner->uvw, &pCorner->cornerFlat,
			          &pCorner->transformed, &usgBc, uvsFace, pVars->pMap,
			          inFace.idx + pVars->inFaceOffset, &pVars->mesh,
			          &pCorner->projNormal, tileMin, false, false, &pCorner->tbn);
		}
		if (!pCorner->transformed) {
			pCorner->cornerFlat = barycentricToCartesian(vertsXyz, &vertBc);
		}
		if (pCorner->isRuvm) {
			pCorner->corner =
				_(pCorner->cornerFlat V3ADD _(pCorner->projNormal V3MULS pCorner->uvw.d[2] * wScale));
			pCornerBuf->buf[j].normal = _(pCornerBuf->buf[j].normal V3MULM3X3 &pCorner->tbn);
		}
		else {
			//offset and normal transform will be deferred to combine stage,
			//to allow for interpolation of usg normals.
			//W will be add to the corner in the add to face function after this func
			pCorner->corner = pCorner->cornerFlat;
		}
	}
}

static
lerpIntersect(CornerBuf *pCorner, Attrib *pDestAttrib, int32_t destIdx,
              Attrib *pMapAttrib, FaceRange *pMapFace, CornerAncestors *pAncestors) {
	RUVM_ASSERT("", pMapAttrib->type != RUVM_ATTRIB_STRING);
	if (pCorner->isRuvm) {
		copyAttrib(pDestAttrib, destIdx, pMapAttrib, pMapFace->start + pCorner->uvsCorner);
	}
	else {
		uint8_t dataA[32] = {0}; //enough for a V4 8 byte type
		uint8_t dataB[32] = {0}; //enough for a V4 8 byte type
		Attrib attribA = *pMapAttrib;
		Attrib attribB = *pMapAttrib;
		attribA.pData = dataA;
		attribB.pData = dataB;
		int32_t idxA = 0;
		int32_t idxB = 0;
		CornerBuf *pAncestor = pAncestors->pArr + pCorner->ancestor;
		CornerBuf *pAncestorNext = pAncestors->pArr + pCorner->ancestorNext;
		lerpIntersect(pAncestor, &attribA, 0, pMapAttrib, pMapFace, pAncestors);
		lerpIntersect(pAncestorNext, &attribB, 0, pMapAttrib, pMapFace, pAncestors);
		//TODO probably will need to invert this depending on map wind order
		lerpAttrib(
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

//NOTE map and mesh date idx params are only used if interpolation is not enabled
//for the attrib. This is always the case on faces.
//Except for right now, because I havn't implemented map triangulation and interpolation,
//so the map data idx is used temporarily until that's done.
static
void blendMapAndInAttribs(BufMesh *pBufMesh, AttribArray *pDestAttribs,
                          AttribArray *pMapAttribs, AttribArray *pMeshAttribs,
						  CornerBuf *pCornerBuf, int32_t cornerBufIdx,
						  int32_t dataIdx, int32_t mapDataIdx,
						  int32_t meshDataIdx, RuvmCommonAttrib *pCommonAttribs,
						  int32_t commonAttribCount, FaceRange *pBaseFace,
                          FaceRange *pMapFace, RuvmDomain domain,
                          CornerAncestors *pAncestors) {
	CornerBuf *pCorner = pCornerBuf + cornerBufIdx;
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
				//need to get baerycentry coords for baseCorners (not necessary
				//for intersection points, can just lerp in the clipping function).
				//so to summarise, only base corners will be interpolated here,
				//intersection corners will be lerped at clipping stage,
				//and map corners obviously don't need interpolation
				
				//temp memcpy until the above todo is handled
				memcpy(mapBuf.pData, attribAsVoid(pMapAttrib, mapDataIdx),
				       getAttribSize(pMapAttrib->type));
			}
			if (pMeshAttrib->interpolate) {
				//TODO skip interlopation if base corner? is it worth it? profile.
				triInterpolateAttrib(
					&meshBuf,
					0,
					pMeshAttrib,
				    pBaseFace->start + pCornerBuf[cornerBufIdx].triCorners[0],
					pBaseFace->start + pCornerBuf[cornerBufIdx].triCorners[1],
					pBaseFace->start + pCornerBuf[cornerBufIdx].triCorners[2],
					pCornerBuf[cornerBufIdx].bc
				);
			}
			RuvmCommonAttrib *pCommon =
				getCommonAttrib(pCommonAttribs, commonAttribCount,
			                    pDestAttribs->pArr[i].name);
			RuvmAttrib *orderTable[2];
			int8_t order = pCommon->blendConfig.order;
			orderTable[0] = order ? &mapBuf : &meshBuf;
			orderTable[1] = !order ? &mapBuf : &meshBuf;
			blendAttribs(pDestAttrib, dataIdx, orderTable[0], 0,
			             orderTable[1], 0, pCommon->blendConfig);
		}
		else if (pDestAttribs->pArr[i].origin == RUVM_ATTRIB_ORIGIN_MAP) {
			RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs->pArr[i].name,
											   pMapAttribs);
			if (pMapAttrib->interpolate && domain == RUVM_DOMAIN_LOOP) {
				RuvmAttrib *pMapAttrib =
					getAttrib(pDestAttribs->pArr[i].name, pMapAttribs);
				//memcpy(attribAsVoid(pDestAttrib, dataIdx),
					//attribAsVoid(pMapAttrib, mapDataIdx),
					//getAttribSize(pMapAttrib->type));
				lerpIntersect(pCorner, pDestAttrib, dataIdx, pMapAttrib,
					            pMapFace, pAncestors);
			}
			else {
				memcpy(attribAsVoid(pDestAttrib, dataIdx),
				       attribAsVoid(pMapAttrib, mapDataIdx),
				       getAttribSize(pMapAttrib->type));
			}
		}
		else if (pDestAttribs->pArr[i].origin == RUVM_ATTRIB_ORIGIN_MESH_IN) {
			RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs->pArr[i].name,
											      pMeshAttribs);
			if (pMeshAttrib->interpolate) {
				//TODO skip interlopation is base corner? is it worth it? profile.
				triInterpolateAttrib(
					pDestAttrib,
					dataIdx,
					pMeshAttrib,
				    pBaseFace->start + pCornerBuf[cornerBufIdx].triCorners[0],
					pBaseFace->start + pCornerBuf[cornerBufIdx].triCorners[1],
					pBaseFace->start + pCornerBuf[cornerBufIdx].triCorners[2],
					pCornerBuf[cornerBufIdx].bc
				);
			}
			else {
				memcpy(attribAsVoid(pDestAttrib, dataIdx),
				       attribAsVoid(pMeshAttrib, meshDataIdx),
				       getAttribSize(pMeshAttrib->type));
			}
		}
	}
}

static
void simpleCopyAttribs(AttribArray *pDestAttribs, AttribArray *pMapAttribs,
					   AttribArray *pMeshAttribs, int32_t destDataIdx,
					   int32_t srcDataIdx, int32_t idxOrigin) {
	for (int32_t i = 0; i < pDestAttribs->count; ++i) {
		switch (pDestAttribs->pArr[i].origin) {
			case (RUVM_ATTRIB_ORIGIN_COMMON): {
				RuvmAttrib *pSrcAttrib;
				if (idxOrigin) {
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
				if (!idxOrigin) {
					//idx is a meshIn idx
					continue;
				}
				RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs->pArr[i].name,
												   pMapAttribs);
				memcpy(attribAsVoid(pDestAttribs->pArr + i, destDataIdx),
					   attribAsVoid(pMapAttrib, srcDataIdx),
					   getAttribSize(pMapAttrib->type));
				break;
			}
			case (RUVM_ATTRIB_ORIGIN_MESH_IN): {
				if (idxOrigin) {
					//idx is a map idx
					continue;
				}
				RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs->pArr[i].name,
													  pMeshAttribs);
				memcpy(attribAsVoid(pDestAttribs->pArr + i, destDataIdx),
					   attribAsVoid(pMeshAttrib, srcDataIdx),
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
	BufMeshIdx edge = bufMeshAddEdge(&pVars->alloc, pBufMesh, !isMapEdge, pVars->pDpVars, &realloced);
	pAcfVars->edge = edge.idx;
	pEntry->edge = edge.idx;
	simpleCopyAttribs(&asMesh(pBufMesh)->mesh.edgeAttribs,
	                  &pVars->pMap->mesh.mesh.edgeAttribs,
					  &pVars->mesh.mesh.edgeAttribs,
					  edge.realIdx, refEdge, isMapEdge);
	pEntry->refEdge = refEdge;
	pEntry->refFace = refFace;
}

static
int32_t getRefEdge(MappingJobVars *pVars, FaceRange *pRuvmFace,
                   FaceRange *pBaseFace, CornerBuf *pCornerBuf,
				   int32_t cornerBufIdx) {
	if (pCornerBuf[cornerBufIdx].isRuvm) {
		int32_t uvsCorner = pCornerBuf[cornerBufIdx].uvsCorner;
		return pVars->pMap->mesh.mesh.pEdges[pRuvmFace->start + uvsCorner];
	}
	else {
		int32_t baseCorner = pCornerBuf[cornerBufIdx].baseCorner;
		return pVars->mesh.mesh.pEdges[pBaseFace->start + baseCorner];
	}
}

static
void addEdge(MappingJobVars *pVars, int32_t cornerBufIdx, BufMesh *pBufMesh,
             CornerBuf *pCornerBuf, RuvmAlloc *pAlloc, int32_t refFace,
			 AddClippedFaceVars *pAcfVars, FaceRange *pBaseFace,
			 FaceRange *pRuvmFace) {
	int32_t refEdge =
		getRefEdge(pVars, pRuvmFace, pBaseFace, pCornerBuf, cornerBufIdx);
	int32_t isMapEdge = pCornerBuf[cornerBufIdx].isRuvm;
	int32_t key = isMapEdge ? refEdge : (refEdge + 1) * -1;
	int32_t hash =
		uvsFnvHash((uint8_t *)&key, 4, pVars->localTables.edgeTableSize);
	LocalEdge *pEntry = pVars->localTables.pEdgeTable + hash;
	int32_t depth = 0;
	do {
		if (!pEntry->cornerCount) {
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
	pEntry->cornerCount++;
}

static
void addNewCornerAndOrVert(MappingJobVars *pVars, int32_t cornerBufIdx,
                         AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						 CornerBuf *pCornerBuf, FaceRange *pBaseFace, FaceRange *pMapFace) {
		bool realloced = false;
		BufMeshIdx vert = bufMeshAddVert(&pVars->alloc, pBufMesh, true, pVars->pDpVars, &realloced);
		pAcfVars->vert = vert.idx;
		asMesh(pBufMesh)->pVerts[vert.realIdx] = pCornerBuf[cornerBufIdx].corner;
		//temporarily setting mesh data idx to 0, as it's only needed if interpolation is disabled
		blendMapAndInAttribs(
			pBufMesh, &asMesh(pBufMesh)->mesh.vertAttribs,
			&pVars->pMap->mesh.mesh.vertAttribs,
			&pVars->mesh.mesh.vertAttribs,
			pCornerBuf, cornerBufIdx, vert.realIdx,
			pCornerBuf[cornerBufIdx].uvsCorner, 0,
			pVars->pCommonAttribList->pVert,
			pVars->pCommonAttribList->vertCount,
			pBaseFace, pMapFace, RUVM_DOMAIN_VERT, NULL
		);
}

static
void initMapVertTableEntry(MappingJobVars *pVars, int32_t cornerBufIdx,
                           AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						   CornerBuf *pCornerBuf, LocalVert *pEntry,
						   FaceRange baseFace, int32_t uvsVert, FaceRange *pMapFace) {
	bool realloced = false;
	BufMeshIdx vert = bufMeshAddVert(&pVars->alloc, pBufMesh, false, pVars->pDpVars, &realloced);
	pAcfVars->vert = vert.idx;
	asMesh(pBufMesh)->pVerts[vert.realIdx] = pCornerBuf[cornerBufIdx].corner;
	pEntry->vert = vert.idx;
	pEntry->mapVert = uvsVert;
	pEntry->baseFace = baseFace.idx;
	blendMapAndInAttribs(pBufMesh, &asMesh(pBufMesh)->mesh.vertAttribs,
						 &pVars->pMap->mesh.mesh.vertAttribs,
						 &pVars->mesh.mesh.vertAttribs,
						 pCornerBuf, cornerBufIdx, vert.realIdx,
						 pCornerBuf[cornerBufIdx].uvsCorner, 0,
						 pVars->pCommonAttribList->pVert,
						 pVars->pCommonAttribList->vertCount,
						 &baseFace, pMapFace, RUVM_DOMAIN_VERT, NULL);
}

static
void addRuvmCornerAndOrVert(MappingJobVars *pVars, int32_t cornerBufIdx,
                          AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						  CornerBuf *pCornerBufEntry, RuvmAlloc *pAlloc,
						  FaceRange baseFace, FaceRange *pRuvmFace) {
	int32_t uvsCorner = pRuvmFace->start + pCornerBufEntry[cornerBufIdx].uvsCorner;
	uint32_t uRuvmVert = pVars->pMap->mesh.mesh.pCorners[uvsCorner];
	int32_t hash =
		uvsFnvHash((uint8_t *)&uRuvmVert, 4, pVars->localTables.vertTableSize);
	LocalVert *pEntry = pVars->localTables.pVertTable + hash;
	do {
		if (!pEntry->cornerSize) {
			initMapVertTableEntry(pVars, cornerBufIdx, pAcfVars,
			                      pBufMesh, pCornerBufEntry, pEntry, baseFace,
								  uRuvmVert, pRuvmFace);
			break;
		}
		//TODO should you be checking tile here as well?
		int32_t match = pEntry->mapVert == uRuvmVert &&
		                pEntry->baseFace == baseFace.idx;
		if (match) {
			pAcfVars->vert = pEntry->vert;
			break;
		}
		if (!pEntry->pNext) {
			pEntry = pEntry->pNext = pAlloc->pCalloc(1, sizeof(LocalVert));
			initMapVertTableEntry(pVars, cornerBufIdx, pAcfVars,
			                      pBufMesh, pCornerBufEntry, pEntry, baseFace,
								  uRuvmVert, pRuvmFace);
			break;
		}
		pEntry = pEntry->pNext;
	} while (1);
	pEntry->cornerSize++;
}

static
void setBorderFaceMapAttrib(BorderFace *pEntry, UBitField8 *pArr,
                            int32_t corner, int32_t value) {
	int32_t len = 3 + pEntry->memType;
	setBitArr(pArr, corner, value, len);
}

static
void initBorderTableEntry(MappingJobVars *pVars, AddClippedFaceVars *pAcfVars,
                          BorderFace *pEntry, FaceRange *pMapFace,
                          V2_I32 tile, CornerBufWrap *pCornerBuf, FaceRange baseFace,
                          bool faceWindDir, bool mapFaceWindDir, int32_t memType,
                          Segments *pSegments) {
	pEntry->memType = memType;
	pEntry->face = pAcfVars->face;
	pEntry->faceIdx = pMapFace->idx;
	pEntry->tileX = *(uint64_t *)&tile.d[0];
	pEntry->tileY = *(uint64_t *)&tile.d[1];
	pEntry->job = pVars->id;
	pEntry->baseFace = baseFace.idx;
	pEntry->inOrient = faceWindDir;
	pEntry->mapOrient = mapFaceWindDir;

	BorderFaceBitArrs bitArrs;
	getBorderFaceBitArrs(pEntry, &bitArrs);

	RUVM_ASSERT("", pCornerBuf->size <= 64);
	for (int32_t i = 0; i < pCornerBuf->size; ++i) {
		CornerBuf *pCorner = pCornerBuf->buf + i;
		if (pCorner->onLine != 0) {
			setBitArr(bitArrs.pOnLine, i, true, 1);
		}
		if (pCorner->isRuvm) {
			setBitArr(bitArrs.pIsRuvm, i, true, 1);
		}
		if (pCorner->uvsCorner) {
			setBorderFaceMapAttrib(pEntry, bitArrs.pRuvmCorner, i, pCorner->uvsCorner);
		}
		if (pCorner->isBaseCorner) {
			setBitArr(bitArrs.pOnInVert, i, true, 1);
		}
		if (!pCorner->isRuvm && pCorner->segment) {
			int32_t idx = pCorner->segment - 1;
			int32_t j = 1;
			//if map face orientation is inverted, then the position
			//of each incorner in the segments array is offset by 1.
			//This is because pCorner->baseCorner is decremented at the end of
			//clipping to account for the inverted wind order
			int32_t segIdx = mapFaceWindDir ?
				pCorner->baseCorner : (pCorner->baseCorner + 1) % baseFace.size;
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
		// Only add basecorner for uvs if online, otherwise value will
		// will not fit within 2 bits
		if ((!pCorner->isRuvm || pCorner->onLine) && pCorner->baseCorner) {
			setBitArr(bitArrs.pBaseCorner, i, pCorner->baseCorner, 2);
		}
	}
}

static
void addFaceToBorderTable(MappingJobVars *pVars, AddClippedFaceVars *pAcfVars,
                          CornerBufWrap *pCornerBuf, FaceRange *pMapFace,
						  V2_I32 tile, FaceRange baseFace, bool faceWindDir,
                          bool mapFaceWindDir, Segments *pSegments) {
	int32_t memType = getBorderFaceMemType(pMapFace->size, pCornerBuf->size);
	int32_t allocSize = getBorderFaceSize(memType);
	int32_t hash =
		uvsFnvHash((uint8_t *)&pMapFace->idx, 4, pVars->borderTable.size);
	BorderBucket *pBucket = pVars->borderTable.pTable + hash;
	BorderFace *pEntry = pBucket->pEntry;
	if (!pEntry) {
		pEntry = pBucket->pEntry = pVars->alloc.pCalloc(1, allocSize);
		initBorderTableEntry(pVars, pAcfVars, pEntry, pMapFace, tile,
		                     pCornerBuf, baseFace, faceWindDir, mapFaceWindDir,
		                     memType, pSegments);
	}
	else {
		do {
			if (pEntry->faceIdx == pMapFace->idx) {
				if (pBucket->pTail) {
					pEntry = pBucket->pTail;
				}
				pEntry = pEntry->pNext = pVars->alloc.pCalloc(1, allocSize);
				pBucket->pTail = pEntry;
				initBorderTableEntry(pVars, pAcfVars, pEntry, pMapFace, tile,
				                     pCornerBuf, baseFace, faceWindDir, mapFaceWindDir,
				                     memType, pSegments);
				break;
			}
			if (!pBucket->pNext) {
				pBucket = pBucket->pNext =
					pVars->alloc.pCalloc(1, sizeof(BorderBucket));
				pEntry =
					pBucket->pEntry = pVars->alloc.pCalloc(1, allocSize);
				initBorderTableEntry(pVars, pAcfVars, pEntry, pMapFace, tile,
				                     pCornerBuf, baseFace, faceWindDir, mapFaceWindDir,
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
	*pInFaceEntry->pArr = pBaseFace->idx + pVars->inFaceOffset;
	pInFaceEntry->count = 1;
	pInFaceEntry->usg = pMapFace->idx;
	int32_t faceCount = pVars->bufMesh.mesh.mesh.faceCount;
	RUVM_ASSERT("", faceCount <= pVars->inFaceSize);
	if (pVars->inFaceSize == faceCount) {
		pVars->inFaceSize *= 2;
		pVars->pInFaces =
			pVars->alloc.pRealloc(pVars->pInFaces, sizeof(InFaceArr) * pVars->inFaceSize);
	}
}

static
void addClippedFaceToBufMesh(MappingJobVars *pVars, CornerBufWrap *pCornerBuf,
							 FaceRange uvsFace, V2_I32 tile, FaceRange baseFace,
                             bool faceWindDir, bool mapFaceWindDir,
                             Segments *pSegments, CornerAncestors *pAncestors) {
	bool realloced = false;
	AddClippedFaceVars acfVars = {0};
	BufMesh *pBufMesh = &pVars->bufMesh;
	bool isBorderFace = pCornerBuf->edgeFace || pCornerBuf->onLine;
	bool invert = !faceWindDir && !isBorderFace;
	int32_t size = pCornerBuf->size;
	for (int32_t i = invert ? size - 1 : 0;
		invert ? i >= 0 : i < size;
		invert ? --i : ++i) {

		int32_t refFace;
		int32_t isRuvm = pCornerBuf->buf[i].isRuvm;
		if (!isRuvm || pCornerBuf->buf[i].onLine) {
			//TODO these only add verts, not corners. Outdated name?
			addNewCornerAndOrVert(pVars, i, &acfVars, &pVars->bufMesh,
			                    pCornerBuf->buf, &baseFace, &uvsFace);
			refFace = uvsFace.idx;
		}
		else {
			addRuvmCornerAndOrVert(pVars, i, &acfVars, &pVars->bufMesh,
			                     pCornerBuf->buf, &pVars->alloc, baseFace,
								 &uvsFace);
			refFace = baseFace.idx;
		}
		BufMeshIdx corner = bufMeshAddCorner(&pVars->alloc, pBufMesh, isBorderFace, pVars->pDpVars, &realloced);
		acfVars.corner = corner.idx;
		if (invert ? i == size - 1 : !i) {
			acfVars.cornerStart = corner.idx;
		}
		pBufMesh->pW[corner.realIdx] = pCornerBuf->buf[i].uvw.d[2];
		pBufMesh->pInNormal[corner.realIdx] = pCornerBuf->buf[i].projNormal;
		pBufMesh->pInTangent[corner.realIdx] = pCornerBuf->buf[i].inTangent;
		pBufMesh->pAlpha[corner.realIdx] = pCornerBuf->buf[i].alpha;
		pBufMesh->pInTSign[corner.realIdx] = pCornerBuf->buf[i].inTSign;
		asMesh(pBufMesh)->mesh.pCorners[corner.realIdx] = acfVars.vert;
		asMesh(pBufMesh)->pNormals[corner.realIdx] = pCornerBuf->buf[i].normal;
		asMesh(pBufMesh)->pUvs[corner.realIdx] = pCornerBuf->buf[i].uv;
		blendMapAndInAttribs(&pVars->bufMesh, &asMesh(pBufMesh)->mesh.cornerAttribs,
							 &pVars->pMap->mesh.mesh.cornerAttribs,
							 &pVars->mesh.mesh.cornerAttribs,
							 pCornerBuf->buf, i, corner.realIdx,
							 uvsFace.start + pCornerBuf->buf[i].uvsCorner,
							 baseFace.start,
							 pVars->pCommonAttribList->pCorner,
							 pVars->pCommonAttribList->cornerCount, &baseFace, &uvsFace,
		                     RUVM_DOMAIN_LOOP, pAncestors);
		addEdge(pVars, i, &pVars->bufMesh, pCornerBuf->buf, &pVars->alloc,
		        refFace, &acfVars, &baseFace, &uvsFace);
		asMesh(pBufMesh)->mesh.pEdges[corner.realIdx] = acfVars.edge;
	}
	BufMeshIdx face = bufMeshAddFace(&pVars->alloc, pBufMesh, isBorderFace, pVars->pDpVars, &realloced);
	if (pVars->getInFaces && !isBorderFace) {
		addInFace(pVars, face.idx, &baseFace, &uvsFace);
	}
	acfVars.face = face.idx;
	asMesh(pBufMesh)->mesh.pFaces[face.realIdx] = acfVars.cornerStart;
	blendMapAndInAttribs(&pVars->bufMesh, &asMesh(pBufMesh)->mesh.faceAttribs,
						 &pVars->pMap->mesh.mesh.faceAttribs,
						 &pVars->mesh.mesh.faceAttribs,
						 pCornerBuf->buf, 0, face.realIdx,
						 uvsFace.idx, baseFace.idx,
						 pVars->pCommonAttribList->pFace,
						 pVars->pCommonAttribList->faceCount, &baseFace, &uvsFace,
	                     RUVM_DOMAIN_FACE, NULL);
	if (isBorderFace) {
		addFaceToBorderTable(pVars, &acfVars, pCornerBuf, &uvsFace,
		                     tile, baseFace, faceWindDir, mapFaceWindDir,
		                     pSegments);
	}
}

static
bool isOnLine(CornerBufWrap *pCornerBuf) {
	for (int32_t i = 0; i < pCornerBuf->size; ++i) {
		if (pCornerBuf->buf[i].onLine) {
			return true;
		}
	}
	return false;
}

Result uvsMapToSingleFace(MappingJobVars *pVars, FaceCellsTable *pFaceCellsTable,
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
		int32_t corner = baseFace.start + i;
		baseTri.uv[i] = _(pVars->mesh.pUvs[corner] V2SUB fTileMin);
		baseTri.xyz[i] = pVars->mesh.pVerts[pVars->mesh.mesh.pCorners[corner]];
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
	CornerAncestors ancestors = {.size = 2};
	ancestors.pArr = pVars->alloc.pMalloc(sizeof(CornerBuf) * ancestors.size);
	for (int32_t i = 0; i < pFaceCellsTable->pFaceCells[baseFace.idx].cellSize; ++i) {
		RUVM_ASSERT("", asMesh(&pVars->bufMesh)->mesh.faceCount >= 0);
		RUVM_ASSERT("", asMesh(&pVars->bufMesh)->mesh.faceCount <
		                asMesh(&pVars->bufMesh)->faceBufSize);
		int32_t cellIdx = pFaceCellsTable->pFaceCells[baseFace.idx].pCells[i];
		Cell *pCell = pVars->pMap->quadTree.cellTable.pArr + cellIdx;
		RUVM_ASSERT("", pCell->localIdx >= 0 && pCell->localIdx < 4);
		RUVM_ASSERT("", pCell->initialized % 2 == pCell->initialized);
		int32_t* pCellFaces;
		Range range = {0};
		if (pFaceCellsTable->pFaceCells[baseFace.idx].pCellType[i]) {
			pCellFaces = pCell->pEdgeFaces;
			range = pFaceCellsTable->pFaceCells[baseFace.idx].pRanges[i];
			//range.start = 0;
			//range.end = pCell->edgeFaceSize;
		}
		else if (pFaceCellsTable->pFaceCells[baseFace.idx].pCellType[i] != 1) {
			pCellFaces = pCell->pFaces;
			range.start = 0;
			range.end = pCell->faceSize;
		}
		else {
			continue;
		}
		for (int32_t j = range.start; j < range.end; ++j) {
			pDpVars->totalFacesComp++;
			FaceRange uvsFace =
				getFaceRange(&pVars->pMap->mesh.mesh, pCellFaces[j], false);
			if (!checkFaceIsInBounds(_(bounds.fMin V2SUB fTileMin),
									 _(bounds.fMax V2SUB fTileMin),
									 uvsFace, &pVars->pMap->mesh)) {
				continue;
			}
			pDpVars->facesNotSkipped++;
			for (int32_t k = 0; k < baseFace.size; ++k) {
				if (pSegments[k].count > 1) {
					memset(pSegments[k].pIndices + 1, 0, pSegments[k].count - 1);
					pSegments[k].count = 1;
				}
			}
			CornerBufWrap cornerBuf = {0};
			cornerBuf.size = uvsFace.size;
			for (int32_t k = 0; k < uvsFace.size; ++k) {
				int32_t vertIdx = pVars->pMap->mesh.mesh.pCorners[uvsFace.start + k];
				CornerBuf *pCorner = cornerBuf.buf + k;
				pCorner->preserve = 0;
				pCorner->isRuvm = 1;
				pCorner->baseCorner = (vertIdx + 1) * -1;
				pCorner->corner = pVars->pMap->mesh.pVerts[vertIdx];
				pCorner->corner.d[0] += fTileMin.d[0];
				pCorner->corner.d[1] += fTileMin.d[1];
				pCorner->uvsCorner = k;
				pCorner->normal =
					pVars->pMap->mesh.pNormals[uvsFace.start + k];
			}
			int32_t mapFaceWindDir =
				calcFaceOrientation(&pVars->pMap->mesh, &uvsFace, false);
			cornerBuf.lastInCorner = mapFaceWindDir ? 0 : baseFace.size - 1;
			CornerBufWrap *pCornerBuf = &cornerBuf;
			ancestors.count = 0;
			do {
				if (!pCornerBuf->invalid) {
					clipRuvmFaceAgainstBaseFace(pVars, baseFace, pCornerBuf,
												faceWindingDir, mapFaceWindDir,
					                            pSegments, &ancestors);
				}
				pCornerBuf = pCornerBuf->pNext;
			} while (pCornerBuf);
			for (int32_t k = 0; k < baseFace.size; ++k) {
				if (pSegments[k].count > 2) {
					fInsertionSort(pSegments[k].pIndices + 1, pSegments[k].count - 1, pSegments[k].pArr + 1);
				}
			}
			pCornerBuf = &cornerBuf;
			int32_t depth = 0;
			do{
				if (!pCornerBuf->invalid) {
					if (pCornerBuf->size >= 3) {
						//TODO move this after addClippedFaceToBufMesh,
						//     that way, you can skip merged uvs verts,
						//     and you can also remove the use of CornerBuf,
						//     and turn the func into a generic (generic for BufMesh)
						//     one that can be used on deffered corners in MergeBoundsFaces.c
						transformClippedFaceFromUvToXyz(pCornerBuf, uvsFace, baseFace, &baseTri,
														pVars, fTileMin, pVars->wScale);
						int32_t faceIdx = pVars->bufMesh.mesh.mesh.faceCount;
						addClippedFaceToBufMesh(pVars, pCornerBuf, uvsFace, tile,
						                        baseFace, faceWindingDir,
						                        mapFaceWindDir, pSegments, &ancestors);
					}
				}
				CornerBufWrap *pNextBuf = pCornerBuf->pNext;
				if (depth) {
					pVars->alloc.pFree(pCornerBuf);
				}
				pCornerBuf = pNextBuf;
				depth++;
			} while(pCornerBuf);
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
