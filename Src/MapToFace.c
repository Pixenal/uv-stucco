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
	        !pCornerBuf->buf[i].isStuc && !pCornerBuf->buf[iNext].isStuc;
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
			stucCalcIntersection(pCorner->corner, pCornerNext->corner, pBaseCorner->vert,
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
int32_t appendToAncestors(StucAlloc *pAlloc,
                          CornerAncestors *pAncestors, CornerBuf *pCorner) {
	STUC_ASSERT("", pAncestors->count <= pAncestors->size);
	if (pAncestors->count == pAncestors->size) {
		pAncestors->size *= 2;
		pAncestors->pArr =
			pAlloc->pRealloc(pAncestors->pArr, sizeof(CornerBuf) * pAncestors->size);
	}
	int32_t idx = pAncestors->count;
	pAncestors->pArr[idx] = *pCorner;
	pAncestors->count++;
	return idx;
}

static
void addIntersectionToBuf(StucAlloc *pAlloc, CornerBufWrap *pNewCornerBuf,
                          CornerBufWrap *pCornerBuf, int32_t i, CornerInfo *pBaseCorner,
                          int32_t iNext, bool flippedWind, IslandIdxPair *pIntersectCache,
                          float *ptBuf, int32_t *pCount, int32_t mapFaceWindDir,
                          int32_t faceWindDir, CornerAncestors *pAncestors) {
	pIntersectCache[*pCount].pIsland = pNewCornerBuf;
	pIntersectCache[*pCount].corner = pNewCornerBuf->size;
	CornerBuf *pCorner = pCornerBuf->buf + i;
	CornerBuf *pCornerNext = pCornerBuf->buf + iNext;
	CornerBuf *pNewEntry = pNewCornerBuf->buf + pNewCornerBuf->size;
	stucCalcIntersection(pCorner->corner, pCornerNext->corner, pBaseCorner->vert,
	                     pBaseCorner->dir, &pNewEntry->corner, &pNewEntry->mapAlpha,
	                     &pNewEntry->alpha);
	if (true) {
		pNewEntry->alpha *= -1.0f;
	}
	//this attrib is lerped here instead of later like other attribs,
	//as it's needed to transform from uvw to xyz
	//TODO is this still necessary? or is it obsolete?
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
CornerBufWrap *createNewCornerBuf(StucAlloc *pAlloc) {
	CornerBufWrap *pCornerBuf = pAlloc->pCalloc(1, sizeof(CornerBufWrap));
	return pCornerBuf;
}

static
void initPendingMerge(StucAlloc *pAlloc, CornerBufWrap *pIsland) {
	pIsland->mergeSize = 3;
	pIsland->pPendingMerge = pAlloc->pCalloc(pIsland->mergeSize, sizeof(void *));
	pIsland->pPendingMerge[0] = -1;
	pIsland->mergeCount = 1;
}

static
void addToPendingMerge(StucAlloc *pAlloc,
                       CornerBufWrap *pIsland, int32_t value) {
	STUC_ASSERT("", pIsland->pPendingMerge);
	STUC_ASSERT("", pIsland->mergeSize > 0);
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
void destroyPendingMerge(StucAlloc *pAlloc, CornerBufWrap *pIsland) {
	STUC_ASSERT("", pIsland->mergeSize > 0);
	STUC_ASSERT("", pIsland->mergeCount > 0);
	if (pIsland->pPendingMerge) {
		pAlloc->pFree(pIsland->pPendingMerge);
		pIsland->pPendingMerge = NULL;
		pIsland->mergeSize = 0;
		pIsland->mergeCount = 0;
	}
}

static
void addIslandToPendingMerge(StucAlloc *pAlloc, IslandIdxPair *pCornerPair,
                             IslandIdxPair *pCornerPairNext, int32_t realiNext,
                             IslandIdxPair *pIntersectCache, int32_t cacheCount) {
	STUC_ASSERT("", pCornerPair->pIsland->size > 0);
	STUC_ASSERT("", pCornerPairNext->pIsland->size > 0);
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
		stucInsertionSort(idxTable + 1, pIsland->mergeCount - 1,
		                  pIsland->pPendingMerge + 1);
	}
	for (int32_t i = 1; i < pIsland->mergeCount; ++i) {
		int32_t idxPending = pIsland->pPendingMerge[idxTable[i] + 1];
		CornerBufWrap *pIslandPending = pIntersectCache[idxPending].pIsland;
		STUC_ASSERT("", pIslandPending->invalid);
		STUC_ASSERT("", pIslandPending->size > 0);
		for (int32_t j = 0; j < pIslandPending->size; ++j) {
			pIsland->buf[pIsland->size + j] = pIslandPending->buf[j];
		}
		pIsland->size += pIslandPending->size;
	}
}

static
void setIsland(StucAlloc *pAlloc, CornerBufWrap **ppIsland,
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
void setSegments(StucAlloc *pAlloc, float *ptBuf, Segments *pSegments,
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
void clipMapFaceAgainstCorner(MappingJobVars *pVars, CornerBufWrap *pCornerBuf,
                              CornerBufWrap *pNewCornerBuf, int32_t *pInsideBuf,
                              FaceRange *pInFace, CornerInfo *pBaseCorner,
                              V2_F32 baseCornerCross, bool flippedWind,
                              int32_t mapFaceWindDir, int32_t faceWindDir,
                              Segments *pSegments, CornerAncestors *pAncestors) {
	for (int32_t i = 0; i < pCornerBuf->size; ++i) {
		V2_F32 stucVert = *(V2_F32 *)&pCornerBuf->buf[i].corner;
		V2_F32 uvStucDir = _(stucVert V2SUB pBaseCorner->vert);
		float dot = _(baseCornerCross V2DOT uvStucDir);
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
			                     pBaseCorner, intersectCache, ptBuf, &count, faceWindDir);
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
	STUC_ASSERT("", count >= 2);
	STUC_ASSERT("should be even", !(count % 2));
	int32_t idxTable[65] = {-1}; //first element to point to first tbuf element
	int32_t *pIdxTable = idxTable + 1;
	if (pBaseCorner->flipEdgeDir) {
		for (int32_t i = 0; i < count; ++i) {
			ptBuf[i] = 1.0f - ptBuf[i];
		}
	}
	stucFInsertionSort(pIdxTable, count, ptBuf);
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
void cornerBufDecrementBaseCorners(CornerBufWrap* pCornerBuf, FaceRange* pInFace) {
	for (int i = 0; i < pCornerBuf->size; ++i) {
		int8_t* pBaseCorner = &pCornerBuf->buf[i].baseCorner;
		*pBaseCorner = *pBaseCorner ? *pBaseCorner - 1 : pInFace->size - 1;
	}
}

static
void clipMapFaceAgainstInFace(MappingJobVars *pVars, FaceRange *pInFace,
                                 CornerBufWrap *pCornerBuf, int32_t faceWindingDir,
                                 int32_t mapFaceWindDir, Segments *pSegments,
                                 CornerAncestors *pAncestors) {
	bool flippedWind = !faceWindingDir || !mapFaceWindDir;
	int32_t start = pCornerBuf->lastInCorner;
	for (int32_t i = start; mapFaceWindDir ? i < pInFace->size : i >= 0; mapFaceWindDir ? ++i : --i) {
		STUC_ASSERT("", i >= 0 && i < pInFace->size);
		CornerInfo baseCorner = {0};
		//why is both this and idxLocal local? Shouldn't this be absolute?
		baseCorner.idx = i;
		baseCorner.vert = pVars->mesh.pUvs[i + pInFace->start];
		int32_t uvNextIdxLocal;
		int32_t uvPrevIdxLocal;
		int32_t edgeCorner;
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
		baseCorner.edgeIdx = pVars->mesh.core.pEdges[edgeCorner];
		//TODO rename verts in pEdgeVerts to corners. They're not verts anymore.
		int32_t *pEdgeCorners = pVars->pEdgeVerts[baseCorner.edgeIdx].verts;
		STUC_ASSERT("", pEdgeCorners[0] == edgeCorner || pEdgeCorners[1] == edgeCorner);
		baseCorner.flipEdgeDir = edgeCorner != pEdgeCorners[0];
		int32_t uvNextIdx = uvNextIdxLocal + pInFace->start;
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
		clipMapFaceAgainstCorner(pVars, pCornerBuf, &newCornerBuf, insideBuf,
		                         pInFace, &baseCorner, baseCornerCross,
		                         flippedWind, mapFaceWindDir, faceWindingDir,
		                         pSegments, pAncestors);

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
		cornerBufDecrementBaseCorners(pCornerBuf, pInFace);
	}
}

static
V3_F32 getCornerRealNormal(Mesh *pMesh, FaceRange *pFace, int32_t corner) {
	int32_t a = corner == 0 ? pFace->size - 1 : corner - 1;
	int32_t c = (corner + 1) % pFace->size;
	int32_t aIdx = pMesh->core.pCorners[pFace->start + a];
	int32_t bIdx = pMesh->core.pCorners[pFace->start + corner];
	int32_t cIdx = pMesh->core.pCorners[pFace->start + c];
	V3_F32 ba = _(pMesh->pVerts[aIdx] V3SUB pMesh->pVerts[bIdx]);
	V3_F32 bc = _(pMesh->pVerts[cIdx] V3SUB pMesh->pVerts[bIdx]);
	return v3Normalize(_(ba V3CROSS bc));
}

static
void transformClippedFaceFromUvToXyz(CornerBufWrap *pCornerBuf, FaceRange *pMapFace,
                                     FaceRange *pInFace, BaseTriVerts *pInTri,
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
		V3_F32 vertBc = stucGetBarycentricInFace(pInTri->uv, pCorner->triCorners,
		                                         pInFace->size, *(V2_F32 *)&pCorner->uvw);
		int8_t *pTriCorners = pCorner->triCorners;
		V3_F32 vertsXyz[3] = {0};
		int32_t inVerts[3] = {0};
		for (int32_t i = 0; i < 3; ++i) {
			inVerts[i] = pVars->mesh.core.pCorners[pInFace->start + pTriCorners[i]];
			vertsXyz[i] = pVars->mesh.pVerts[inVerts[i]];
		}
		pCorner->bc = vertBc;
		pCorner->tbn = stucGetInterpolatedTbn(&pVars->mesh, pInFace, pTriCorners, vertBc);
		float inVertsWScaleMul = 1.0;
		if (pVars->mesh.pWScale) {
			Attrib wScaleWrap = {
				.core.pData = &inVertsWScaleMul,
				.interpolate = true,
				.core.type = STUC_ATTRIB_F32
			};
			stucTriInterpolateAttrib(&wScaleWrap, 0, pVars->mesh.pWScaleAttrib,
			                         inVerts[0], inVerts[1], inVerts[2], vertBc);
		}
		pCorner->uvw.d[2] *= inVertsWScaleMul;
		pCorner->inTangent = *(V3_F32 *)&pCorner->tbn.d[0];
		pCorner->projNormal = *(V3_F32 *)&pCorner->tbn.d[2];
		pCorner->inTSign = pVars->mesh.pTSigns[pInFace->start + pTriCorners[0]];
		if (pMapMesh->pUsg && pCorner->isStuc) {
			V3_F32 usgBc = {0};
			stucSampleUsg(pCorner->stucCorner, pCorner->uvw, &pCorner->cornerFlat,
			              &pCorner->transformed, &usgBc, pMapFace, pVars->pMap,
			              pInFace->idx + pVars->inFaceOffset, &pVars->mesh,
			              &pCorner->projNormal, tileMin, false, false, &pCorner->tbn);
		}
		if (!pCorner->transformed) {
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
lerpIntersect(CornerBuf *pCorner, Attrib *pDestAttrib, int32_t destIdx,
              Attrib *pMapAttrib, FaceRange *pMapFace, CornerAncestors *pAncestors) {
	STUC_ASSERT("", pMapAttrib->core.type != STUC_ATTRIB_STRING);
	if (pCorner->isStuc) {
		stucCopyAttrib(pDestAttrib, destIdx, pMapAttrib,
		           pMapFace->start + pCorner->stucCorner);
	}
	else {
		uint8_t dataA[32] = {0}; //enough for a V4 8 byte type
		uint8_t dataB[32] = {0}; //enough for a V4 8 byte type
		Attrib attribA = *pMapAttrib;
		Attrib attribB = *pMapAttrib;
		attribA.core.pData = dataA;
		attribB.core.pData = dataB;
		int32_t idxA = 0;
		int32_t idxB = 0;
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
void blendCommonAttrib(StucContext pContext, BufMesh *pBufMesh,
                       AttribArray *pDestAttribs, StucAttrib *pDestAttrib,
                       AttribArray *pMapAttribs, AttribArray *pMeshAttribs,
                       CornerBuf *pCornerBuf, int32_t cornerBufIdx, int32_t dataIdx,
                       int32_t mapDataIdx, int32_t meshDataIdx,
                       StucCommonAttrib *pCommonAttribs, int32_t commonAttribCount,
                       FaceRange *pInFace, int32_t attribIdx) {
	if (pDestAttrib == pBufMesh->mesh.pVertAttrib ||
	    pDestAttrib == pBufMesh->mesh.pUvAttrib ||
	    pDestAttrib == pBufMesh->mesh.pNormalAttrib) {

		return;
	}
	//TODO if this attrib is not found, nullptr can be accessed,
	//check
	StucAttrib *pMapAttrib =
		stucGetAttribIntern(pDestAttribs->pArr[attribIdx].core.name, pMapAttribs);
	StucAttrib *pMeshAttrib =
		stucGetAttribIntern(pDestAttribs->pArr[attribIdx].core.name, pMeshAttribs);
	StucAttribType type = pDestAttribs->pArr[attribIdx].core.type;
	uint8_t mapDataBuf[STUC_ATTRIB_STRING_MAX_LEN] = {0};
	StucAttrib mapBuf = {.core.pData = mapDataBuf, .core.type = type};
	uint8_t meshDataBuf[STUC_ATTRIB_STRING_MAX_LEN] = {0};
	StucAttrib meshBuf = {.core.pData = meshDataBuf, .core.type = type};
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
		memcpy(mapBuf.core.pData, stucAttribAsVoid(&pMapAttrib->core, mapDataIdx),
		       stucGetAttribSizeIntern(pMapAttrib->core.type));
	}
	if (pMeshAttrib->interpolate) {
		//TODO skip interlopation if in corner? is it worth it? profile.
		stucTriInterpolateAttrib(
			&meshBuf,
			0,
			pMeshAttrib,
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[0],
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[1],
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[2],
			pCornerBuf[cornerBufIdx].bc
		);
	}
	else {
		memcpy(meshBuf.core.pData, stucAttribAsVoid(&pMeshAttrib->core, meshDataIdx),
		       stucGetAttribSizeIntern(pMeshAttrib->core.type));
	}
	StucCommonAttrib *pCommon = stucGetCommonAttrib(pCommonAttribs, commonAttribCount,
	                                                pDestAttribs->pArr[attribIdx].core.name);
	StucBlendConfig blendConfig = {0};
	if (pCommon) {
			blendConfig = pCommon->blendConfig;
	}
	else {
		StucTypeDefault *pDefault =
			stucGetTypeDefaultConfig(&pContext->typeDefaults, pMeshAttrib->core.type);
		blendConfig = pDefault->blendConfig;
	}
	StucAttrib *orderTable[2] = {0};
	int8_t order = blendConfig.order;
	orderTable[0] = order ? &mapBuf : &meshBuf;
	orderTable[1] = !order ? &mapBuf : &meshBuf;
	stucBlendAttribs(pDestAttrib, dataIdx, orderTable[0], 0, orderTable[1], 0,
	                 blendConfig);
}

static
void interpolateMapAttrib(AttribArray *pDestAttribs, int32_t attribIdx,
                          AttribArray *pMapAttribs, StucDomain domain,
                          CornerBuf *pCorner, StucAttrib *pDestAttrib,
                          int32_t dataIdx, FaceRange *pMapFace,
                          CornerAncestors *pAncestors, int32_t mapDataIdx) {
	StucAttrib *pMapAttrib = stucGetAttribIntern(pDestAttribs->pArr[attribIdx].core.name, pMapAttribs);
	if (pMapAttrib->interpolate && domain == STUC_DOMAIN_CORNER) {
		StucAttrib *pMapAttrib = stucGetAttribIntern(pDestAttribs->pArr[attribIdx].core.name, pMapAttribs);
		lerpIntersect(pCorner, pDestAttrib, dataIdx, pMapAttrib, pMapFace, pAncestors);
	}
	else {
		memcpy(stucAttribAsVoid(&pDestAttrib->core, dataIdx),
		       stucAttribAsVoid(&pMapAttrib->core, mapDataIdx),
		       stucGetAttribSizeIntern(pMapAttrib->core.type));
	}
}

static
void interpolateInMeshAttrib(AttribArray *pDestAttribs, int32_t attribIdx,
                             AttribArray *pMeshAttribs, StucAttrib *pDestAttrib,
                             int32_t dataIdx, FaceRange *pInFace,
                             CornerBuf *pCornerBuf, int32_t cornerBufIdx,
                             int32_t meshDataIdx) {
	StucAttrib *pMeshAttrib =
		stucGetAttribIntern(pDestAttribs->pArr[attribIdx].core.name, pMeshAttribs);
	if (pMeshAttrib->interpolate) {
		//TODO skip interlopation is in corner? is it worth it? profile.
		stucTriInterpolateAttrib(
			pDestAttrib,
			dataIdx,
			pMeshAttrib,
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[0],
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[1],
			pInFace->start + pCornerBuf[cornerBufIdx].triCorners[2],
			pCornerBuf[cornerBufIdx].bc
		);
	}
	else {
		memcpy(stucAttribAsVoid(&pDestAttrib->core, dataIdx),
		       stucAttribAsVoid(&pMeshAttrib->core, meshDataIdx),
		       stucGetAttribSizeIntern(pMeshAttrib->core.type));
	}
}

//NOTE map and mesh date idx params are only used if interpolation is not enabled
//for the attrib. This is always the case on faces.
//Except for right now, because I havn't implemented map triangulation and interpolation,
//so the map data idx is used temporarily until that's done.
static
void blendMapAndInAttribs(StucContext pContext, BufMesh *pBufMesh,
                          AttribArray *pDestAttribs, AttribArray *pMapAttribs,
                          AttribArray *pMeshAttribs, CornerBuf *pCornerBuf,
                          int32_t cornerBufIdx, int32_t dataIdx, int32_t mapDataIdx,
                          int32_t meshDataIdx, StucCommonAttrib *pCommonAttribs,
                          int32_t commonAttribCount, FaceRange *pInFace,
                          FaceRange *pMapFace, StucDomain domain,
                          CornerAncestors *pAncestors) {
	CornerBuf *pCorner = pCornerBuf + cornerBufIdx;
	//TODO make naming for MeshIn consistent
	//TODO rename meshBuf in this func to inBuf,
	//it's ambiguous whether it's refering to in-mesh or bufmesh
	for (int32_t i = 0; i < pDestAttribs->count; ++i) {
		StucAttrib *pDestAttrib = pDestAttribs->pArr + i;
		if (pDestAttribs->pArr[i].origin == STUC_ATTRIB_ORIGIN_COMMON) {
			blendCommonAttrib(pContext, pBufMesh, pDestAttribs, pDestAttrib, pMapAttribs,
			                  pMeshAttribs, pCornerBuf, cornerBufIdx, dataIdx, mapDataIdx,
			                  meshDataIdx, pCommonAttribs, commonAttribCount, pInFace, i);
		}
		else if (pDestAttribs->pArr[i].origin == STUC_ATTRIB_ORIGIN_MAP) {
			interpolateMapAttrib(pDestAttribs, i, pMapAttribs, domain, pCorner,
			                     pDestAttrib, dataIdx, pMapFace, pAncestors, mapDataIdx);
		}
		else if (pDestAttribs->pArr[i].origin == STUC_ATTRIB_ORIGIN_MESH_IN) {
			interpolateInMeshAttrib(pDestAttribs, i, pMeshAttribs, pDestAttrib, dataIdx,
			                        pInFace, pCornerBuf, cornerBufIdx, meshDataIdx);
		}
	}
}

static
void simpleCopyAttribs(AttribArray *pDestAttribs, AttribArray *pMapAttribs,
                       AttribArray *pMeshAttribs, int32_t destDataIdx,
                       int32_t srcDataIdx, int32_t idxOrigin) {
	for (int32_t i = 0; i < pDestAttribs->count; ++i) {
		switch (pDestAttribs->pArr[i].origin) {
			case (STUC_ATTRIB_ORIGIN_COMMON): {
				StucAttrib *pSrcAttrib;
				if (idxOrigin) {
					pSrcAttrib = stucGetAttribIntern(pDestAttribs->pArr[i].core.name, pMapAttribs);
				}
				else {
					pSrcAttrib = stucGetAttribIntern(pDestAttribs->pArr[i].core.name, pMeshAttribs);
				}
				break;
			}
			case (STUC_ATTRIB_ORIGIN_MAP): {
				if (!idxOrigin) {
					//idx is a meshIn idx
					continue;
				}
				StucAttrib *pMapAttrib =
					stucGetAttribIntern(pDestAttribs->pArr[i].core.name, pMapAttribs);
				memcpy(stucAttribAsVoid(&pDestAttribs->pArr[i].core, destDataIdx),
				       stucAttribAsVoid(&pMapAttrib->core, srcDataIdx),
				       stucGetAttribSizeIntern(pMapAttrib->core.type));
				break;
			}
			case (STUC_ATTRIB_ORIGIN_MESH_IN): {
				if (idxOrigin) {
					//idx is a map idx
					continue;
				}
				StucAttrib *pMeshAttrib =
					stucGetAttribIntern(pDestAttribs->pArr[i].core.name, pMeshAttribs);
				memcpy(stucAttribAsVoid(&pDestAttribs->pArr[i].core, destDataIdx),
				       stucAttribAsVoid(&pMeshAttrib->core, srcDataIdx),
				       stucGetAttribSizeIntern(pMeshAttrib->core.type));
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
	BufMeshIdx edge =
		stucBufMeshAddEdge(&pVars->alloc, pBufMesh, !isMapEdge, pVars->pDpVars, &realloced);
	pAcfVars->edge = edge.idx;
	pEntry->edge = edge.idx;
	simpleCopyAttribs(&pBufMesh->mesh.core.edgeAttribs,
	                  &pVars->pMap->mesh.core.edgeAttribs,
	                  &pVars->mesh.core.edgeAttribs,
	                  edge.realIdx, refEdge, isMapEdge);
	pEntry->refEdge = refEdge;
	pEntry->refFace = refFace;
}

static
int32_t getRefEdge(MappingJobVars *pVars, FaceRange *pMapFace,
                   FaceRange *pInFace, CornerBuf *pCornerBuf,
                   int32_t cornerBufIdx) {
	if (pCornerBuf[cornerBufIdx].isStuc) {
		int32_t stucCorner = pCornerBuf[cornerBufIdx].stucCorner;
		return pVars->pMap->mesh.core.pEdges[pMapFace->start + stucCorner];
	}
	else {
		int32_t baseCorner = pCornerBuf[cornerBufIdx].baseCorner;
		return pVars->mesh.core.pEdges[pInFace->start + baseCorner];
	}
}

static
void addEdge(MappingJobVars *pVars, int32_t cornerBufIdx, BufMesh *pBufMesh,
             CornerBuf *pCornerBuf, StucAlloc *pAlloc, int32_t refFace,
             AddClippedFaceVars *pAcfVars, FaceRange *pInFace, FaceRange *pMapFace) {
	int32_t refEdge = getRefEdge(pVars, pMapFace, pInFace, pCornerBuf, cornerBufIdx);
	int32_t isMapEdge = pCornerBuf[cornerBufIdx].isStuc;
	int32_t key = isMapEdge ? refEdge : (refEdge + 1) * -1;
	int32_t hash = stucFnvHash((uint8_t *)&key, 4, pVars->localTables.edgeTableSize);
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
		STUC_ASSERT("", pEntry->pNext && pEntry->pNext->pNext <= (LocalEdge *)1000000000000000);
		pEntry = pEntry->pNext;
		STUC_ASSERT("", depth >= 0 && depth < 1000);
		depth++;
	} while(1);
	pEntry->cornerCount++;
}

static
void addNewCornerAndOrVert(MappingJobVars *pVars, int32_t cornerBufIdx,
                         AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
                         CornerBuf *pCornerBuf, FaceRange *pInFace,
                         FaceRange *pMapFace) {
		bool realloced = false;
		BufMeshIdx vert =
			stucBufMeshAddVert(&pVars->alloc, pBufMesh, true, pVars->pDpVars, &realloced);
		pAcfVars->vert = vert.idx;
		pBufMesh->mesh.pVerts[vert.realIdx] = pCornerBuf[cornerBufIdx].corner;
		//TODO temporarily setting mesh data idx to 0, as it's only needed if interpolation is disabled
		blendMapAndInAttribs(
			pVars->pContext,
			pBufMesh, &pBufMesh->mesh.core.vertAttribs,
			&pVars->pMap->mesh.core.vertAttribs,
			&pVars->mesh.core.vertAttribs,
			pCornerBuf, cornerBufIdx, vert.realIdx,
			pCornerBuf[cornerBufIdx].stucCorner, 0,
			pVars->pCommonAttribList->pVert,
			pVars->pCommonAttribList->vertCount,
			pInFace, pMapFace, STUC_DOMAIN_VERT, NULL
		);
}

static
void initMapVertTableEntry(MappingJobVars *pVars, int32_t cornerBufIdx,
                           AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
                           CornerBuf *pCornerBuf, LocalVert *pEntry, FaceRange *pInFace,
                           int32_t stucVert, FaceRange *pMapFace, V2_I32 tile) {
	bool realloced = false;
	BufMeshIdx vert = stucBufMeshAddVert(&pVars->alloc, pBufMesh, false,
	                                     pVars->pDpVars, &realloced);
	pAcfVars->vert = vert.idx;
	pBufMesh->mesh.pVerts[vert.realIdx] = pCornerBuf[cornerBufIdx].corner;
	pEntry->vert = vert.idx;
	pEntry->mapVert = stucVert;
	pEntry->inFace = pInFace->idx;
	pEntry->tile = tile;
	blendMapAndInAttribs(pVars->pContext,
	                     pBufMesh, &pBufMesh->mesh.core.vertAttribs,
	                     &pVars->pMap->mesh.core.vertAttribs,
	                     &pVars->mesh.core.vertAttribs,
	                     pCornerBuf, cornerBufIdx, vert.realIdx,
	                     pCornerBuf[cornerBufIdx].stucCorner, 0,
	                     pVars->pCommonAttribList->pVert,
	                     pVars->pCommonAttribList->vertCount,
	                     pInFace, pMapFace, STUC_DOMAIN_VERT, NULL);
}

static
void addStucCornerAndOrVert(MappingJobVars *pVars, int32_t cornerBufIdx,
                            AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
                            CornerBuf *pCornerBufEntry, StucAlloc *pAlloc,
                            FaceRange *pInFace, FaceRange *pMapFace, V2_I32 tile) {
	int32_t stucCorner = pMapFace->start + pCornerBufEntry[cornerBufIdx].stucCorner;
	uint32_t uStucVert = pVars->pMap->mesh.core.pCorners[stucCorner];
	int32_t hash =
		stucFnvHash((uint8_t *)&uStucVert, 4, pVars->localTables.vertTableSize);
	LocalVert *pEntry = pVars->localTables.pVertTable + hash;
	do {
		if (!pEntry->cornerSize) {
			initMapVertTableEntry(pVars, cornerBufIdx, pAcfVars, pBufMesh,
			                      pCornerBufEntry, pEntry, pInFace, uStucVert,
			                      pMapFace, tile);
			break;
		}
		int32_t match = pEntry->mapVert == uStucVert &&
		                pEntry->inFace == pInFace->idx &&
		                pEntry->tile.d[0] == tile.d[0] &&
		                pEntry->tile.d[1] == tile.d[1]; //TODO int vector ops don't current have macros
		if (match) {
			pAcfVars->vert = pEntry->vert;
			break;
		}
		if (!pEntry->pNext) {
			pEntry = pEntry->pNext = pAlloc->pCalloc(1, sizeof(LocalVert));
			initMapVertTableEntry(pVars, cornerBufIdx, pAcfVars, pBufMesh,
			                      pCornerBufEntry, pEntry, pInFace, uStucVert,
			                      pMapFace, tile);
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
	stucSetBitArr(pArr, corner, value, len);
}

static
void initBorderTableEntry(MappingJobVars *pVars, AddClippedFaceVars *pAcfVars,
                          BorderFace *pEntry, FaceRange *pMapFace,
                          V2_I32 tile, CornerBufWrap *pCornerBuf, FaceRange *pInFace,
                          bool faceWindDir, bool mapFaceWindDir, int32_t memType,
                          Segments *pSegments) {
	pEntry->memType = memType;
	pEntry->bufFace = pAcfVars->face;
	pEntry->mapFace = pMapFace->idx;
	pEntry->tileX = *(uint64_t *)&tile.d[0];
	pEntry->tileY = *(uint64_t *)&tile.d[1];
	pEntry->job = pVars->id;
	pEntry->inFace = pInFace->idx;
	pEntry->inOrient = faceWindDir;
	pEntry->mapOrient = mapFaceWindDir;

	BorderFaceBitArrs bitArrs = {0};
	stucGetBorderFaceBitArrs(pEntry, &bitArrs);

	STUC_ASSERT("", pCornerBuf->size <= 64);
	for (int32_t i = 0; i < pCornerBuf->size; ++i) {
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
			int32_t idx = pCorner->segment - 1;
			int32_t j = 1;
			//if map face orientation is inverted, then the position
			//of each incorner in the segments array is offset by 1.
			//This is because pCorner->baseCorner is decremented at the end of
			//clipping to account for the inverted wind order
			int32_t segIdx = mapFaceWindDir ?
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
void walkBorderTable(MappingJobVars *pVars, AddClippedFaceVars *pAcfVars,
                     CornerBufWrap *pCornerBuf, FaceRange *pMapFace,
                     V2_I32 tile, FaceRange *pInFace, bool faceWindDir,
                     bool mapFaceWindDir, Segments *pSegments, BorderBucket *pBucket,
                     BorderFace *pEntry, int32_t memType, int32_t allocSize ) {
	do {
		if (pEntry->mapFace == pMapFace->idx) {
			if (pBucket->pTail) {
				pEntry = pBucket->pTail;
			}
			pEntry = pEntry->pNext = pVars->alloc.pCalloc(1, allocSize);
			pBucket->pTail = pEntry;
			initBorderTableEntry(pVars, pAcfVars, pEntry, pMapFace, tile,
			                     pCornerBuf, pInFace, faceWindDir, mapFaceWindDir,
			                     memType, pSegments);
			break;
		}
		if (!pBucket->pNext) {
			pBucket = pBucket->pNext = pVars->alloc.pCalloc(1, sizeof(BorderBucket));
			pEntry = pBucket->pEntry = pVars->alloc.pCalloc(1, allocSize);
			initBorderTableEntry(pVars, pAcfVars, pEntry, pMapFace, tile,
			                     pCornerBuf, pInFace, faceWindDir, mapFaceWindDir,
			                     memType, pSegments);
			break;
		}
		pBucket = pBucket->pNext;
		pEntry = pBucket->pEntry;
	} while (1);
}

static
void addFaceToBorderTable(MappingJobVars *pVars, AddClippedFaceVars *pAcfVars,
                          CornerBufWrap *pCornerBuf, FaceRange *pMapFace,
                          V2_I32 tile, FaceRange *pInFace, bool faceWindDir,
                          bool mapFaceWindDir, Segments *pSegments) {
	int32_t memType = stucGetBorderFaceMemType(pMapFace->size, pCornerBuf->size);
	int32_t allocSize = stucGetBorderFaceSize(memType);
	int32_t hash =
		stucFnvHash((uint8_t *)&pMapFace->idx, 4, pVars->borderTable.size);
	BorderBucket *pBucket = pVars->borderTable.pTable + hash;
	BorderFace *pEntry = pBucket->pEntry;
	if (!pEntry) {
		pEntry = pBucket->pEntry = pVars->alloc.pCalloc(1, allocSize);
		initBorderTableEntry(pVars, pAcfVars, pEntry, pMapFace, tile,
		                     pCornerBuf, pInFace, faceWindDir, mapFaceWindDir,
		                     memType, pSegments);
	}
	else {
		walkBorderTable(pVars, pAcfVars, pCornerBuf, pMapFace, tile, pInFace,
		                faceWindDir, mapFaceWindDir, pSegments, pBucket, pEntry,
		                memType, allocSize);
	}
}

static
void addInFace(MappingJobVars *pVars, int32_t face, FaceRange *pInFace, FaceRange *pMapFace) {
	InFaceArr *pInFaceEntry = pVars->pInFaces + face;
	pInFaceEntry->pArr = pVars->alloc.pMalloc(sizeof(int32_t));
	*pInFaceEntry->pArr = pInFace->idx + pVars->inFaceOffset;
	pInFaceEntry->count = 1;
	pInFaceEntry->usg = pMapFace->idx;
	int32_t faceCount = pVars->bufMesh.mesh.core.faceCount;
	STUC_ASSERT("", faceCount <= pVars->inFaceSize);
	if (pVars->inFaceSize == faceCount) {
		pVars->inFaceSize *= 2;
		pVars->pInFaces =
			pVars->alloc.pRealloc(pVars->pInFaces, sizeof(InFaceArr) * pVars->inFaceSize);
	}
}

static
void addClippedFaceToBufMesh(MappingJobVars *pVars, CornerBufWrap *pCornerBuf,
                             FaceRange *pMapFace, V2_I32 tile, FaceRange *pInFace,
                             bool faceWindDir, bool mapFaceWindDir,
                             Segments *pSegments, CornerAncestors *pAncestors) {;
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
		int32_t isStuc = pCornerBuf->buf[i].isStuc;
		if (!isStuc || pCornerBuf->buf[i].onLine) {
			//TODO these only add verts, not corners. Outdated name?
			addNewCornerAndOrVert(pVars, i, &acfVars, &pVars->bufMesh,
			                      pCornerBuf->buf, pInFace, pMapFace);
			refFace = pMapFace->idx;
		}
		else {
			addStucCornerAndOrVert(pVars, i, &acfVars, &pVars->bufMesh,
			                       pCornerBuf->buf, &pVars->alloc, pInFace,
			                       pMapFace, tile);
			refFace = pInFace->idx;
		}
		BufMeshIdx corner = stucBufMeshAddCorner(&pVars->alloc, pBufMesh, isBorderFace,
		                                         pVars->pDpVars, &realloced);
		acfVars.corner = corner.idx;
		if (invert ? i == size - 1 : !i) {
			acfVars.cornerStart = corner.idx;
		}
		pBufMesh->pW[corner.realIdx] = pCornerBuf->buf[i].uvw.d[2];
		pBufMesh->pInNormal[corner.realIdx] = pCornerBuf->buf[i].projNormal;
		pBufMesh->pInTangent[corner.realIdx] = pCornerBuf->buf[i].inTangent;
		pBufMesh->pAlpha[corner.realIdx] = pCornerBuf->buf[i].alpha;
		pBufMesh->pInTSign[corner.realIdx] = pCornerBuf->buf[i].inTSign;
		pBufMesh->mesh.core.pCorners[corner.realIdx] = acfVars.vert;
		pBufMesh->mesh.pNormals[corner.realIdx] = pCornerBuf->buf[i].normal;
		pBufMesh->mesh.pUvs[corner.realIdx] = pCornerBuf->buf[i].uv;
		//TODO add an intermediate function to shorten the arg lists in blendattrib functions
		blendMapAndInAttribs(pVars->pContext,
		                     &pVars->bufMesh,
		                     &pBufMesh->mesh.core.cornerAttribs,
		                     &pVars->pMap->mesh.core.cornerAttribs,
		                     &pVars->mesh.core.cornerAttribs,
		                     pCornerBuf->buf, i, corner.realIdx,
		                     pMapFace->start + pCornerBuf->buf[i].stucCorner,
		                     pInFace->start,
		                     pVars->pCommonAttribList->pCorner,
		                     pVars->pCommonAttribList->cornerCount, pInFace, pMapFace,
		                     STUC_DOMAIN_CORNER, pAncestors);
		addEdge(pVars, i, &pVars->bufMesh, pCornerBuf->buf, &pVars->alloc,
		        refFace, &acfVars, pInFace, pMapFace);
		pBufMesh->mesh.core.pEdges[corner.realIdx] = acfVars.edge;
	}
	BufMeshIdx face = stucBufMeshAddFace(&pVars->alloc, pBufMesh, isBorderFace,
	                                     pVars->pDpVars, &realloced);
	if (pVars->getInFaces && !isBorderFace) {
		addInFace(pVars, face.idx, pInFace, pMapFace);
	}
	acfVars.face = face.idx;
	pBufMesh->mesh.core.pFaces[face.realIdx] = acfVars.cornerStart;
	blendMapAndInAttribs(pVars->pContext,
	                     &pVars->bufMesh, &pBufMesh->mesh.core.faceAttribs,
	                     &pVars->pMap->mesh.core.faceAttribs,
	                     &pVars->mesh.core.faceAttribs,
	                     pCornerBuf->buf, 0, face.realIdx,
	                     pMapFace->idx, pInFace->idx,
	                     pVars->pCommonAttribList->pFace,
	                     pVars->pCommonAttribList->faceCount, pInFace, pMapFace,
	                     STUC_DOMAIN_FACE, NULL);
	if (isBorderFace) {
		addFaceToBorderTable(pVars, &acfVars, pCornerBuf, pMapFace,
		                     tile, pInFace, faceWindDir, mapFaceWindDir,
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

static
bool isTriDegenerate(BaseTriVerts *pTri, FaceRange *pFace) {
	if (v2DegenerateTri(pTri->uv[0], pTri->uv[1], pTri->uv[2], .0f) ||
		v3DegenerateTri(pTri->xyz[0], pTri->xyz[1], pTri->xyz[2], .0f)) {
		return true;
	}
	if (pFace->size == 4) {
		if (v2DegenerateTri(pTri->uv[2], pTri->uv[3], pTri->uv[0], .0f) ||
			v3DegenerateTri(pTri->xyz[2], pTri->xyz[3], pTri->xyz[0], .0f)) {
			return true;
		}
	}
	return false;
}

static
void getCellMapFaces(MappingJobVars *pVars, FaceCells *pFaceCellsEntry,
                     int32_t faceCellsIdx, int32_t **ppCellFaces, Range *pRange) {
	STUC_ASSERT("", &pVars->bufMesh.mesh.core.faceCount >= 0);
	STUC_ASSERT("", &pVars->bufMesh.mesh.core.faceCount <
	                &pVars->bufMesh.mesh.faceBufSize);
	int32_t cellIdx = pFaceCellsEntry->pCells[faceCellsIdx];
	Cell *pCell = pVars->pMap->quadTree.cellTable.pArr + cellIdx;
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
	for (int32_t i = 0; i < pInFace->size; ++i) {
		if (pSegments[i].count > 1) {
			memset(pSegments[i].pIndices + 1, 0, pSegments[i].count - 1);
			pSegments[i].count = 1;
		}
	}
}

static
void initCornerBuf(StucMap pMap, V2_F32 fTileMin, CornerBufWrap *pCornerBuf,
                   FaceRange *pInFace, FaceRange *pMapFace, int32_t mapFaceWindDir) {
	pCornerBuf->size = pMapFace->size;
	for (int32_t k = 0; k < pMapFace->size; ++k) {
		int32_t vertIdx = pMap->mesh.core.pCorners[pMapFace->start + k];
		CornerBuf *pCorner = pCornerBuf->buf + k;
		pCorner->preserve = 0;
		pCorner->isStuc = 1;
		pCorner->baseCorner = (vertIdx + 1) * -1;
		pCorner->corner = pMap->mesh.pVerts[vertIdx];
		pCorner->corner.d[0] += fTileMin.d[0];
		pCorner->corner.d[1] += fTileMin.d[1];
		pCorner->stucCorner = k;
		pCorner->normal = pMap->mesh.pNormals[pMapFace->start + k];
	}
	pCornerBuf->lastInCorner = mapFaceWindDir ? 0 : pInFace->size - 1;
}

static
void clipMapFaceIntoFaces(MappingJobVars *pVars, CornerBufWrap *pCornerBuf,
                          FaceRange *pInFace, int32_t inFaceWind, int32_t mapFaceWind,
                          Segments *pSegments, CornerAncestors *pAncestors) {
	CornerBufWrap *pCornerBufPtr = pCornerBuf;
	do {
		if (!pCornerBufPtr->invalid) {
			clipMapFaceAgainstInFace(pVars, pInFace, pCornerBufPtr, inFaceWind,
			                            mapFaceWind, pSegments, pAncestors);
		}
		pCornerBufPtr = pCornerBufPtr->pNext;
	} while (pCornerBufPtr);
}

static
void sortSegments(Segments *pSegments, FaceRange *pInFace) {
	for (int32_t i = 0; i < pInFace->size; ++i) {
		if (pSegments[i].count > 2) {
			stucFInsertionSort(pSegments[i].pIndices + 1, pSegments[i].count - 1,
				pSegments[i].pArr + 1);
		}
	}
}

static
void addOrDiscardClippedFaces(MappingJobVars *pVars, V2_F32 fTileMin,
                              CornerBufWrap *pCornerBuf, V2_I32 tile,
                              Segments *pSegments, CornerAncestors *pAncestors,
                              FaceRange *pInFace, FaceRange *pMapFace,
                              BaseTriVerts *pInTri, int32_t inFaceWind,
                              int32_t mapFaceWind) {
	CornerBufWrap *pCornerBufPtr = pCornerBuf;
	int32_t depth = 0;
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
				transformClippedFaceFromUvToXyz(pCornerBufPtr, pMapFace, pInFace, pInTri,
				                                pVars, fTileMin, pVars->wScale);
				addClippedFaceToBufMesh(pVars, pCornerBufPtr, pMapFace, tile, pInFace,
				                        inFaceWind, mapFaceWind, pSegments, pAncestors);
			}
		}
		CornerBufWrap *pNextBuf = pCornerBufPtr->pNext;
		if (depth) {
			pVars->alloc.pFree(pCornerBufPtr);
		}
		pCornerBufPtr = pNextBuf;
		depth++;
	} while(pCornerBufPtr);
}

Result stucMapToSingleFace(MappingJobVars *pVars, FaceCellsTable *pFaceCellsTable,
                           DebugAndPerfVars *pDpVars, V2_F32 fTileMin, V2_I32 tile,
                           FaceRange *pInFace) {
	STUC_ASSERT("", pInFace->size == 3 || pInFace->size == 4);
	FaceBounds bounds = {0};
	stucGetFaceBounds(&bounds, pVars->mesh.pUvs, *pInFace);
	BaseTriVerts inTri = {0};
	pDpVars->facesNotSkipped++;
	STUC_ASSERT("", pInFace->size >= 3 && pInFace->size <= 4);
	for (int32_t i = 0; i < pInFace->size; ++i) {
		int32_t corner = pInFace->start + i;
		inTri.uv[i] = _(pVars->mesh.pUvs[corner] V2SUB fTileMin);
		inTri.xyz[i] = pVars->mesh.pVerts[pVars->mesh.core.pCorners[corner]];
	}
	stucGetTriScale(pInFace->size, &inTri);
	if (isTriDegenerate(&inTri, pInFace)) {
		return STUC_SUCCESS;
	}
	int32_t inFaceWind = stucCalcFaceOrientation(&pVars->mesh, pInFace, true);
	if (inFaceWind == 2) {
		//face is degenerate
		return STUC_SUCCESS;
	}
	Segments *pSegments = pVars->alloc.pCalloc(pInFace->size, sizeof(Segments));
	for (int32_t i = 0; i < pInFace->size; ++i) {
		pSegments[i].size = 3;
		pSegments[i].pArr = pVars->alloc.pCalloc(pSegments[i].size, sizeof(float));
		pSegments[i].pIndices = pVars->alloc.pCalloc(pSegments[i].size, sizeof(int32_t));
		pSegments[i].pArr[0] = -FLT_MAX;
		pSegments[i].pIndices[0] = -1;
		pSegments[i].count = 1;
	}
	CornerAncestors ancestors = {.size = 2};
	ancestors.pArr = pVars->alloc.pMalloc(sizeof(CornerBuf) * ancestors.size);
	FaceCells *pFaceCellsEntry =
		stucIdxFaceCells(pFaceCellsTable, pInFace->idx, pVars->inFaceRange.start);
	for (int32_t i = 0; i < pFaceCellsEntry->cellSize; ++i) {
		int32_t *pCellFaces = NULL;
		Range range = {0};
		getCellMapFaces(pVars, pFaceCellsEntry, i, &pCellFaces, &range);
		for (int32_t j = range.start; j < range.end; ++j) {
			pDpVars->totalFacesComp++;
			FaceRange mapFace =
				stucGetFaceRange(&pVars->pMap->mesh.core, pCellFaces[j], false);
			if (!stucCheckFaceIsInBounds(_(bounds.fMin V2SUB fTileMin),
			                             _(bounds.fMax V2SUB fTileMin),
			                             mapFace, &pVars->pMap->mesh)) {
				continue;
			}
			pDpVars->facesNotSkipped++;
			resetSegments(pSegments, pInFace);
			int32_t mapFaceWind =
				stucCalcFaceOrientation(&pVars->pMap->mesh, &mapFace, false);
			CornerBufWrap cornerBuf = {0};
			initCornerBuf(pVars->pMap, fTileMin, &cornerBuf, pInFace, &mapFace,
			              mapFaceWind);
			ancestors.count = 0;
			clipMapFaceIntoFaces(pVars, &cornerBuf, pInFace, inFaceWind, mapFaceWind,
			                     pSegments, &ancestors);
			sortSegments(pSegments, pInFace);
			addOrDiscardClippedFaces(pVars, fTileMin, &cornerBuf, tile, pSegments,
			                         &ancestors, pInFace, &mapFace, &inTri, inFaceWind,
			                         mapFaceWind);
		}
	}
	pVars->alloc.pFree(ancestors.pArr);
	for (int32_t i = 0; i < pInFace->size; ++i) {
		pVars->alloc.pFree(pSegments[i].pArr);
		pVars->alloc.pFree(pSegments[i].pIndices);
	}
	pVars->alloc.pFree(pSegments);
	return STUC_SUCCESS;
}
