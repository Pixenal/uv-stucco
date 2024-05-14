#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include <RUVM.h>
#include <CombineJobMeshes.h>
#include <MapFile.h>
#include <Mesh.h>
#include <Context.h>
#include <Clock.h>
#include <MathUtils.h>
#include <Utils.h>
#include <AttribUtils.h>

typedef struct {
	V3_F32 normal;
	V2_F32 uv;
	int32_t bufLoop;
	int32_t bufFace;
	int32_t loop;
	int32_t edge;
	int32_t sort;
	int8_t job;
} LoopBufEntry;

typedef struct {
	LoopBufEntry *pBuf;
	int32_t count;
} LoopBuf;

typedef struct {
	int32_t *pBuf;
	int32_t count;
} SeamBuf;

typedef struct {
	int32_t *pBuf;
	int32_t count;
} MapLoopBuf;

typedef struct {
	LoopBuf loopBuf;
	SeamBuf seamBuf;
	MapLoopBuf mapLoopBuf;
	int32_t size;
} MergeBufs;

typedef struct {
	Mat3x3 tbnInv;
	FaceInfo ruvmFace;
	BorderFace *pEntry;
	LoopBuf loopBuf;
	SeamBuf seamBuf;
	MapLoopBuf mapLoopBuf;
	Piece *pPiece;
	V2_F32 centre;
	int32_t seamFace;
	int32_t infoBufSize;
	int8_t fullSort;
	int8_t triangulate;
} Vars;

/*
static int32_t getEdgeLocalLoop(int32_t *pEdgeLoops, FaceInfo *pBaseFace) {
	for (int32_t i = pBaseFace->start; i < pBaseFace->end; ++i) {
		if (i == pEdgeLoops[0] || i == pEdgeLoops[1]) {
			return i;
		}
	}
	printf("Couldn't find loop for edge (winding compare for border face\n)");
	abort();
}

static void removeEdgeEntryFromList(RuvmContext pContext, SharedEdge *pEntry,
                                    void *pLastEntry) {
	//Both SharedEdgeWrap, and SharedEdge, have a pointer to a SharedEdge
	//as their first element. So we just cast and assign pLastEntry.
	*(SharedEdge **)pLastEntry = pEntry->pNext;
	pContext->alloc.pFree(pEntry);
}
*/

void ruvmDestroyMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle) {
	if (pHandle->size) {
		pContext->alloc.pFree(pHandle->pLoopBuf);
		pContext->alloc.pFree(pHandle->pSeamBuf);
		pContext->alloc.pFree(pHandle->pMapLoopBuf);
	}
}

void ruvmAllocMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle,
                        int32_t totalVerts) {
	if (totalVerts > pHandle->size) {
		if (pHandle->size) {
			ruvmDestroyMergeBufs(pContext, pHandle);
		}
		pHandle->pLoopBuf =
			pContext->alloc.pMalloc(sizeof(LoopBuf) * totalVerts);
		pHandle->pSeamBuf =
			pContext->alloc.pMalloc(sizeof(SeamBuf) * totalVerts);
		pHandle->pMapLoopBuf =
			pContext->alloc.pMalloc(sizeof(SeamBuf) * totalVerts);
		pHandle->size = totalVerts;
	}
}

static
int32_t determineIfSeamFace(RuvmMap pMap, BorderFace *pEntry) {
	int32_t faceIndex = pEntry->faceIndex;
	int32_t ruvmLoops = 0;
	do {
		int32_t isRuvm = pEntry->isRuvm;
		for (int32_t i = 0; i < 11; ++i) {
			ruvmLoops += isRuvm >> i & 1;
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
	int32_t faceStart = pMap->mesh.mesh.pFaces[faceIndex];
	int32_t faceEnd = pMap->mesh.mesh.pFaces[faceIndex + 1];
	int32_t faceSize = faceEnd - faceStart;
	return ruvmLoops < faceSize;
}

static
void addLoopsWithSingleVert(RuvmContext pContext, Vars *pVars, SendOffArgs *pJobArgs,
                            BorderVert *localEdgeTable, int32_t *pTotalVerts) {
	for (int32_t i = 0; i < 16; ++i) {
		BorderVert *pEdgeEntry = localEdgeTable + i;
		int32_t depth = 0;
		do {
			if (pEdgeEntry->loops == 1 || pEdgeEntry->keepBaseLoop) {
				BufMesh *pBufMesh = &pJobArgs[pEdgeEntry->job].bufMesh;
				_(&pVars->centre V2ADDEQL pBufMesh->pUvs[pEdgeEntry->vert]);
				++*pTotalVerts;
				pVars->seamBuf.pBuf[pVars->seamBuf.count] = pEdgeEntry->vert;
				pVars->seamBuf.count++;
			}
			BorderVert *pNextEdgeEntry = pEdgeEntry->pNext;
			if (depth > 0) {
				pContext->alloc.pFree(pEdgeEntry);
			}
			pEdgeEntry = pNextEdgeEntry;
			depth++;
		} while(pEdgeEntry);
	}
}

static
void initLocalEdgeTableEntry(BorderVert *pEdgeEntry, int32_t ruvmEdge,
                             BorderFace *pEntry, int32_t baseEdge,
							 int32_t baseVert, int32_t keepBaseLoop,
							 int32_t loop) {
	pEdgeEntry->ruvmEdge = ruvmEdge;
	pEdgeEntry->tile = pEntry->tile;
	pEdgeEntry->baseEdge = baseEdge;
	pEdgeEntry->baseVert = baseVert;
	pEdgeEntry->loops = 1;
	pEdgeEntry->vert = loop;
	pEdgeEntry->keepBaseLoop = keepBaseLoop;
	pEdgeEntry->job = pEntry->job;
}

static
void addLoopToLocalEdgeTable(RuvmContext pContext, BorderVert *localEdgeTable,
                             Vars *pVars, SendOffArgs *pJobArgs,
							 BorderFace *pEntry, RuvmMap pMap,
							 int32_t faceStart, int32_t k, Piece *pPiece) {
	int32_t ruvmLoop = pEntry->ruvmLoop >> k * 3 & 7;
	int32_t baseLoopLocal = pEntry->baseLoop >> k * 2 & 3;
	FaceInfo baseFace;
	baseFace.index = pEntry->baseFace;
	baseFace.start = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace];
	baseFace.end = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace + 1];
	baseFace.size = baseFace.end - baseFace.start;
	int32_t baseLoop = baseFace.start + baseLoopLocal;
	int32_t isBaseLoop = (pEntry->onLine >> k & 1) && !(pEntry->isRuvm >> k & 1);
	int32_t baseEdge = pJobArgs[pEntry->job].mesh.mesh.pEdges[baseLoop];
	int32_t baseVert = isBaseLoop ?
		pJobArgs[pEntry->job].mesh.mesh.pLoops[baseLoop] : -1;

	int32_t keepBaseLoop = 0;
	if (isBaseLoop && pPiece->pBaseKeep >> k & 1) {
		keepBaseLoop = 1;
	}

	int32_t hash, ruvmEdge;
	if (isBaseLoop) {
		hash = ruvmFnvHash((uint8_t *)&baseVert, 4, 16);
		ruvmEdge = -1;
	}
	else {
		ruvmEdge = pMap->mesh.mesh.pEdges[pVars->ruvmFace.start + ruvmLoop];
		hash = ruvmFnvHash((uint8_t *)&ruvmEdge, 4, 16);
	}
	BorderVert *pEdgeEntry = localEdgeTable + hash;
	if (!pEdgeEntry->loops) {
		initLocalEdgeTableEntry(pEdgeEntry, ruvmEdge, pEntry, baseEdge,
		                        baseVert, keepBaseLoop, faceStart - k);
	}
	else {
		do {
			int32_t	match;
			if (isBaseLoop) {
				match = pEdgeEntry->baseVert == baseVert;
			}
			else {
				match =  pEdgeEntry->ruvmEdge == ruvmEdge &&
						 pEdgeEntry->tile == pEntry->tile &&
						 pEdgeEntry->baseEdge == baseEdge;
			}
			if (match) {
				pEdgeEntry->loops++;
				pEdgeEntry->keepBaseLoop |= keepBaseLoop;
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry = pEdgeEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(BorderVert));
					initLocalEdgeTableEntry(pEdgeEntry, ruvmEdge, pEntry, baseEdge,
					                        baseVert, keepBaseLoop, faceStart - k);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
}

static
void determineLoopsToKeep(RuvmContext pContext, RuvmMap pMap,
                          Vars *pVars, SendOffArgs *pJobArgs,
                          int32_t *pTotalVerts, JobBases *pJobBases,
						  Piece *pPiece) {
	BorderVert localEdgeTable[16] = {0};
	do {
		BorderFace *pEntry = pPiece->pEntry;
		BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
		int32_t faceStart = pBufMesh->mesh.pFaces[pEntry->face];
		int32_t faceEnd = pBufMesh->mesh.pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = pBufMesh->mesh.pLoops[faceStart - k];
			if (pEntry->isRuvm >> k & 1) {
				_(&pVars->centre V2ADDEQL pBufMesh->pUvs[faceStart - k]);
				++*pTotalVerts;
				vert += pJobBases[pEntry->job].vertBase;
			}
			else {
				addLoopToLocalEdgeTable(pContext, localEdgeTable, pVars, pJobArgs,
				                        pEntry, pMap, faceStart, k, pPiece);
			}
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	
	addLoopsWithSingleVert(pContext, pVars, pJobArgs, localEdgeTable, pTotalVerts);
}

static void buildApproximateTbnInverse(Vars *pVars,
                                       SendOffArgs *pJobArgs) {
	BorderFace *pEntry = pVars->pEntry;
	V3_F32 normal = {0};
	int32_t entryCount = 0;
	do {
		BufMesh* pBufMesh = &pJobArgs[pEntry->job].bufMesh;
		int32_t faceStart = pBufMesh->mesh.pFaces[pEntry->face];
		int32_t* pLoops = pBufMesh->mesh.pLoops;
		V3_F32* pVertA = pBufMesh->pVerts + pLoops[faceStart];
		V3_F32* pVertB = pBufMesh->pVerts + pLoops[faceStart - 1];
		V3_F32* pVertC = pBufMesh->pVerts + pLoops[faceStart - 2];
		V3_F32 ab = _(*pVertB V3SUB *pVertA);
		V3_F32 ac = _(*pVertC V3SUB *pVertA);
		_(&normal V3ADDEQL v3Cross(ab, ac));
		entryCount++;
		pEntry = pEntry->pNext;
	} while (pEntry);
	_(&normal V3DIVEQLS entryCount);
	float normalLen =
		sqrt(normal.d[0] * normal.d[0] +
		     normal.d[1] * normal.d[1] +
		     normal.d[2] * normal.d[2]);
	_(&normal V3DIVEQLS normalLen);
	V3_F32 axis = {0};
	if (normal.d[2] > .99f || normal.d[2] < -.99f) {
		axis.d[0] = 1.0f;
	}
	else {
		axis.d[2] = 1.0f;
	}
	V3_F32 tangent = v3Cross(normal, axis);
	V3_F32 bitangent = v3Cross(normal, tangent);
	Mat3x3 tbn = mat3x3FromV3_F32(tangent, bitangent, normal);
	pVars->tbnInv = mat3x3Invert(&tbn);
}

static FaceInfo getBaseFace(BorderFace *pEntry, SendOffArgs *pJobArgs) {
	FaceInfo face;
	face.index = pEntry->baseFace;
	face.start = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace];
	face.end = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace + 1];
	face.size = face.end - face.start;
	return face;
}

static void initVertTableEntry(BorderVert *pVertEntry, BorderFace *pEntry,
                               Mesh *pMeshOut, BufMesh *pBufMesh, int32_t ruvmEdge,
							   int32_t *pVert, int32_t baseEdge, int32_t baseVert,
                               int32_t loopIndex, int32_t ruvmFace, int32_t loop) {
	copyAllAttribs(&pMeshOut->mesh.vertAttribs, pMeshOut->mesh.vertCount,
				   &pBufMesh->mesh.vertAttribs, *pVert);
	*pVert = pMeshOut->mesh.vertCount;
	pVertEntry->vert = pMeshOut->mesh.vertCount;
	pMeshOut->mesh.vertCount++;
	pVertEntry->tile = pEntry->tile;
	pVertEntry->ruvmEdge = ruvmEdge;
	pVertEntry->loops = 1;
	pVertEntry->baseEdge = baseEdge;
	pVertEntry->baseVert = baseVert;
	pVertEntry->loopIndex = loopIndex;
	pVertEntry->ruvmFace = ruvmFace;
	pVertEntry->loop = loop;
	pVertEntry->job = pEntry->job;
}

static void initEdgeTableEntry(BorderEdge *pSeamEntry, Mesh *pMeshOut,
                               BufMesh *pBufMesh, int32_t *pEdge,
							   int32_t inEdge, int32_t mapFace) {
	copyAllAttribs(&pMeshOut->mesh.edgeAttribs, pMeshOut->mesh.edgeCount,
				   &pBufMesh->mesh.edgeAttribs, *pEdge);
	*pEdge = pMeshOut->mesh.edgeCount;
	pMeshOut->mesh.edgeCount++;
	pSeamEntry->edge = *pEdge;
	pSeamEntry->inEdge = inEdge;
	pSeamEntry->mapFace = mapFace;
}

static
void addBorderLoopAndVert(RuvmContext pContext, CombineTables *pCTables,
                          RuvmMap pMap, Vars *pVars, int32_t *pVert,
                          BorderFace *pEntry, SendOffArgs *pJobArgs,
						  Mesh *pMeshOut, int32_t k, int32_t ruvmLoop,
						  int32_t *pEdge, int32_t loop) {
	FaceInfo baseFace = getBaseFace(pEntry, pJobArgs);
	int32_t baseLoopLocal = pEntry->baseLoop >> k * 2 & 3;
	int32_t isBaseLoop = (pEntry->onLine >> k & 1) && !(pEntry->isRuvm >> k & 1);
	int32_t baseLoop = baseFace.start + baseLoopLocal;
	int32_t baseEdge = pJobArgs[pEntry->job].mesh.mesh.pEdges[baseLoop];
	int32_t baseVert = isBaseLoop ?
		pJobArgs[pEntry->job].mesh.mesh.pLoops[baseLoop] : -1;
	int32_t hash, ruvmEdge;
	BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
	if (isBaseLoop) {
		hash = ruvmFnvHash((uint8_t *)&baseVert, 4, pCTables->vertTableSize);
		ruvmEdge = -1;
	}
	else {
		ruvmEdge = pMap->mesh.mesh.pEdges[pVars->ruvmFace.start + ruvmLoop];
		hash = ruvmFnvHash((uint8_t *)&ruvmEdge, 4, pCTables->vertTableSize);
	}
	BorderVert *pVertEntry = pCTables->pVertTable + hash;
	if (!pVertEntry->loops) {
		initVertTableEntry(pVertEntry, pEntry, pMeshOut, pBufMesh,
		                   ruvmEdge, pVert, baseEdge, baseVert, baseLoop,
						   pEntry->faceIndex, loop);
	}
	else {
		do {
			//TODO ideally, we'd just compare the edges, but maps don't have
			//edges currently, so I'll remove ruvmVert/ruvmVertNext,
			//and replace them with a single edge index, once edges are
			//added the ruvm maps.
			int32_t match;
			if (isBaseLoop) {
				V2_F32 *pMeshInUvA = pJobArgs[0].mesh.pUvs + pVertEntry->loopIndex;
				V2_F32 *pMeshInUvB = pJobArgs[0].mesh.pUvs + baseLoop;
				match = pVertEntry->baseVert == baseVert &&
						pVertEntry->ruvmFace == pEntry->faceIndex &&
						pMeshInUvA->d[0] == pMeshInUvB->d[0] &&
						pMeshInUvA->d[1] == pMeshInUvB->d[1];
			}
			else {
				int32_t connected = 
					//TODO set pMeshOut->pVerts, when meshout is allocated.
					//It's currently not set
					_(pJobArgs[pEntry->job].bufMesh.pUvs[loop] V2APROXEQL
					  pJobArgs[pVertEntry->job].bufMesh.pUvs[pVertEntry->loop]);
				match =  pVertEntry->ruvmEdge == ruvmEdge &&
						 pVertEntry->tile == pEntry->tile &&
						 pVertEntry->baseEdge == baseEdge &&
						 connected;
			}
			if (match) {
				*pVert = pVertEntry->vert;
				break;
			}
			if (!pVertEntry->pNext) {
				pVertEntry = pVertEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(BorderVert));
				initVertTableEntry(pVertEntry, pEntry, pMeshOut, pBufMesh,
				                   ruvmEdge, pVert, baseEdge, baseVert, baseLoop,
								   pEntry->faceIndex, loop);
				break;
			}
			pVertEntry = pVertEntry->pNext;
		} while(1);
	}
	uint32_t valueToHash = baseEdge + pEntry->faceIndex;
	hash = ruvmFnvHash((uint8_t *)&valueToHash, 4, pCTables->edgeTableSize);
	BorderEdge *pEdgeEntry = pCTables->pEdgeTable + hash;
	if (!pEdgeEntry->valid) {
		initEdgeTableEntry(pEdgeEntry, pMeshOut, pBufMesh, pEdge, baseEdge,
		                   pEntry->faceIndex);
	}
	else {
		do {
			if (pEdgeEntry->inEdge == baseEdge &&
				pEdgeEntry->mapFace == pEntry->faceIndex) {
				*pEdge = pEdgeEntry->edge;
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry = pEdgeEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(BorderEdge));
				initEdgeTableEntry(pEdgeEntry, pMeshOut, pBufMesh, pEdge,
				                   baseEdge, pEntry->faceIndex);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
}

static int32_t checkIfDup(Vars *pVars, int32_t ruvmLoop) {
	for (int32_t i = 0; i < pVars->mapLoopBuf.count; ++i) {
		if (ruvmLoop == pVars->mapLoopBuf.pBuf[i]) {
			return 1;
		}
	}
	return 0;
}

static void initOnLineTableEntry(OnLine *pEntry, Mesh *pMeshOut,
                                 BufMesh *pBufMesh, int32_t base,
								 int32_t isBaseLoop, int32_t ruvmVert,
								 int32_t *pVert) {
	copyAllAttribs(&pMeshOut->mesh.vertAttribs, pMeshOut->mesh.vertCount,
				   &pBufMesh->mesh.vertAttribs, *pVert);
	*pVert = pMeshOut->mesh.vertCount;
	pMeshOut->mesh.vertCount++;
	pEntry->outVert = *pVert;
	pEntry->baseEdgeOrLoop = base;
	pEntry->ruvmVert = ruvmVert;
	pEntry->type = isBaseLoop + 1;
}

static int32_t checkIfShouldSkip(Vars *pVars, Piece *pPiece, int32_t faceStart,
                                 int32_t k) {
	int32_t skip = 0;
	if (!pVars->seamFace) {
		skip = 1;
	}
	int32_t isSeamLoop = 0;
	for (int32_t l = 0; l < pVars->seamBuf.count; ++l) {
		if (faceStart - k == pVars->seamBuf.pBuf[l]) {
			isSeamLoop = 1;
			break;
		}
	}
	if (!isSeamLoop) {
		skip = 1;
	}
	//override if keep is set to 1
	if (skip && pPiece->pKeep >> k & 1) {
		skip = 0;
		pVars->triangulate = 1;
	}
	return skip;
}

static
void addOnLineVert(RuvmContext pContext, Vars *pVars, int32_t ruvmLoop,
                   RuvmMap pMap, SendOffArgs *pJobArgs, BorderFace *pEntry,
				   CombineTables *pCTables, Mesh *pMeshOut, int32_t *pVert,
				   int32_t k) {
	int32_t ruvmVert = pMap->mesh.mesh.pLoops[pVars->ruvmFace.start + ruvmLoop];
	FaceInfo baseFace = getBaseFace(pEntry, pJobArgs);
	int32_t baseLoopLocal = pEntry->baseLoop >> k * 2 & 3;
	int32_t isBaseLoop = (pEntry->onLine >> k & 1) && !(pEntry->isRuvm >> k & 1);
	int32_t baseLoop = baseFace.start + baseLoopLocal;
	int32_t baseEdge = pJobArgs[pEntry->job].mesh.mesh.pEdges[baseLoop];
	int32_t base = isBaseLoop ? baseLoop : baseEdge;
	int32_t hash = ruvmFnvHash((uint8_t *)&base, 4,
							   pCTables->onLineTableSize);
	OnLine *pOnLineEntry = pCTables->pOnLineTable + hash;
	if (!pOnLineEntry->type) {
		initOnLineTableEntry(pOnLineEntry, pMeshOut,
							 &pJobArgs[pEntry->job].bufMesh,
							 base, isBaseLoop, ruvmVert, pVert);
	}
	else {
		do {
			int32_t match = base == pOnLineEntry->baseEdgeOrLoop &&
							ruvmVert == pOnLineEntry->ruvmVert &&
							isBaseLoop + 1 == pOnLineEntry->type;
			if (match) {
				*pVert = pOnLineEntry->outVert;
				break;
			}
			if (!pOnLineEntry->pNext) {
				pOnLineEntry = pOnLineEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(OnLine));
				initOnLineTableEntry(pOnLineEntry, pMeshOut,
									 &pJobArgs[pEntry->job].bufMesh,
									 base, isBaseLoop, ruvmVert, pVert);
				break;
			}
			pOnLineEntry = pOnLineEntry->pNext;
		} while(pOnLineEntry);
	}
}

static
void addLoopsToBufAndVertsToMesh(uint64_t *pTimeSpent, RuvmContext pContext,
                                    CombineTables *pCTables, RuvmMap pMap,
                                    Vars *pVars, SendOffArgs *pJobArgs,
                                    Mesh *pMeshOut, JobBases *pJobBases) {
	//CLOCK_INIT;
	//pieces should be called sub pieces here
	Piece *pPiece = pVars->pPiece;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		BufMesh *bufMesh = &pJobArgs[pEntry->job].bufMesh;
		int32_t faceStart = bufMesh->mesh.pFaces[pEntry->face];
		int32_t faceEnd = bufMesh->mesh.pFaces[pEntry->face - 1];
		//int32_t faceSize = faceStart - faceEnd;
		int32_t loopAmount = faceStart - faceEnd;
		for (int32_t k = 0; k < loopAmount; ++k) {
			//CLOCK_START;
			int32_t vert;
			int32_t edge;
			int32_t isRuvm = pEntry->isRuvm >> k & 1;
			if (!isRuvm) {
				//is not an ruvm loop (is an intersection, or base loop))
				if (checkIfShouldSkip(pVars, pPiece, faceStart, k)) {
					continue;
				}
				//CLOCK_STOP_NO_PRINT;
				//pTimeSpent[3] += CLOCK_TIME_DIFF(start, stop);
				//CLOCK_START;
				vert = bufMesh->mesh.pLoops[faceStart - k];
				edge = bufMesh->mesh.pEdges[faceStart - k];
				int32_t ruvmLoop = pEntry->ruvmLoop >> k * 3 & 7;
				addBorderLoopAndVert(pContext, pCTables, pMap, pVars, &vert,
				                     pEntry, pJobArgs, pMeshOut, k, ruvmLoop,
									 &edge, faceStart - k);
				pVars->loopBuf.pBuf[pVars->loopBuf.count + 1].sort = -1;
				//CLOCK_STOP_NO_PRINT;
				//pTimeSpent[4] += CLOCK_TIME_DIFF(start, stop);
			}
			else {
				//is an ruvm loop (this includes ruvm loops sitting on base edges or verts)

				//add an item to pEntry in mapToMesh, which denotes if an ruvm
				//loop has a dot of 0 (is on a base edge).
				//Then add it to the edgetable if so, without calcing a wind of course.
				//Just use the base edge as the hash, instead of an ruvm edge (cause there isnt one).
				//Or just make a new hash table just for ruvm loops with zero dot.
				//That would probably be cleaner, and more memory concious tbh.
				int32_t onLine = pEntry->onLine >> k & 1;
				int32_t ruvmLoop = pEntry->ruvmLoop >> k * 3 & 7;
				vert = bufMesh->mesh.pLoops[faceStart - k];
				edge = bufMesh->mesh.pEdges[faceStart - k];
				if (onLine) {
					if (checkIfDup(pVars, ruvmLoop)) {
						continue;
					}
					addOnLineVert(pContext, pVars, ruvmLoop, pMap, pJobArgs,
					              pEntry, pCTables, pMeshOut, &vert, k);
				}
				else {
					vert += pJobBases[pEntry->job].vertBase;
				}
				//CLOCK_START;
				//the vert and edge indices are local to the buf mesh,
				//so we need to offset them, so that they point to the
				//correct position in the out mesh. (these vars are set
				//when the non-border mesh data is copied
				edge += pJobBases[pEntry->job].edgeBase;
				
				pVars->loopBuf.pBuf[pVars->loopBuf.count + 1].sort = ruvmLoop * 10;
				pVars->mapLoopBuf.pBuf[pVars->mapLoopBuf.count] = ruvmLoop;
				pVars->mapLoopBuf.count++;
				//CLOCK_STOP_NO_PRINT;
				//pTimeSpent[5] += CLOCK_TIME_DIFF(start, stop);
			}
			//if border loop, or if loop edge has been intersected,
			//add new edge to mesh
			//int32_t kNext = (k + 1) % faceSize;
			//int32_t vertNext = bufMesh->mesh.pLoops[faceStart - kNext];
			//if (borderLoop || vertNext >= bufMesh->mesh.vertCount) {
			//}
			pVars->loopBuf.pBuf[pVars->loopBuf.count].job = pEntry->job;
			pVars->loopBuf.pBuf[pVars->loopBuf.count].bufLoop = faceStart - k;
			pVars->loopBuf.pBuf[pVars->loopBuf.count].bufFace = pEntry->face;
			pVars->loopBuf.pBuf[pVars->loopBuf.count].loop = vert;
			pVars->loopBuf.pBuf[pVars->loopBuf.count].edge = edge;
			pVars->loopBuf.pBuf[pVars->loopBuf.count].uv = bufMesh->pUvs[faceStart - k];
			//CLOCK_START;
			//CLOCK_STOP_NO_PRINT;
			//pTimeSpent[6] += CLOCK_TIME_DIFF(start, stop);
			pVars->loopBuf.count++;
			pVars->infoBufSize++;
		}
		pContext->alloc.pFree(pEntry);
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void sortLoopsFull(int32_t *pIndexTable, Vars *pVars, Mesh *pMeshOut) {
	//insertion sort
	V2_F32 vertBuf[17];
	V2_F32 centre = {0};
	for (int32_t i = 0; i < pVars->loopBuf.count; ++i) {
		V3_F32* pVert = pMeshOut->pVerts + pVars->loopBuf.pBuf[i].loop;
		V3_F32 vertV3 = v3MultiplyMat3x3(*pVert, &pVars->tbnInv);
		vertBuf[i].d[0] = vertV3.d[0];
		vertBuf[i].d[1] = vertV3.d[1];
		_(&centre V2ADDEQL vertBuf[i]);
	}
	_(&centre V2DIVSEQL pVars->loopBuf.count);
	int32_t order = v2WindingCompare(vertBuf[0], vertBuf[1],
	                                   centre, 1);
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufSize = 2;
	for (int32_t k = bufSize; k < pVars->loopBuf.count; ++k) {
		int32_t l, insert;
		for (l = bufSize - 1; l >= 0; --l) {
			if (l != 0) {
				insert =
					v2WindingCompare(vertBuf[k],
						               vertBuf[pIndexTable[l]],
					                   centre, 1)
					&&
					v2WindingCompare(vertBuf[pIndexTable[l - 1]],
						               vertBuf[k], centre, 1);
			}
			else {
				insert =
					v2WindingCompare(vertBuf[k],
					                   vertBuf[pIndexTable[l]],
									   centre, 1);
			}
			if (insert) {
				break;
			}
		}
		if (!insert) {
			pIndexTable[bufSize++] = k;
		}
		else {
			for (int32_t m = bufSize; m > l; --m) {
				pIndexTable[m] = pIndexTable[m - 1];
			}
			pIndexTable[l] = k;
			bufSize++;
		}
	}
}

static void sortLoops(int32_t *pIndexTable, Vars *pVars) {
	LoopBufEntry *pLoopBuf = pVars->loopBuf.pBuf + 1;
	//insertion sort
	int32_t a = pLoopBuf[0].sort;
	int32_t b = pLoopBuf[1].sort;
	int32_t order = a < b;
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufSize = 2;
	for (int32_t k = bufSize; k < pVars->loopBuf.count; ++k) {
		int32_t l, insert;
		for (l = bufSize - 1; l >= 0; --l) {
			insert = pLoopBuf[k].sort < pLoopBuf[pIndexTable[l]].sort &&
			         pLoopBuf[k].sort > pLoopBuf[pIndexTable[l - 1]].sort;
			if (insert) {
				break;
			}
		}
		if (!insert) {
			pIndexTable[bufSize++] = k;
		}
		else {
			for (int32_t m = bufSize; m > l; --m) {
				pIndexTable[m] = pIndexTable[m - 1];
			}
			pIndexTable[l] = k;
			bufSize++;
		}
	}
}

static
void getCentroid(Vars *pVars, SendOffArgs *pJobArgs) {
	BorderFace *pEntry = pVars->pEntry;
	int32_t totalVerts = 0;
	do {
		BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
		int32_t faceStart = pBufMesh->mesh.pFaces[pEntry->face];
		int32_t faceEnd = pBufMesh->mesh.pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		for (int32_t i = 0; i < loopAmount; ++i) {
			_(&pVars->centre V2ADDEQL pBufMesh->pUvs[faceStart - i]);
			totalVerts++;
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
	_(&pVars->centre V2DIVSEQL (float)totalVerts);
}

static
void determineIfFullSort(Vars *pVars) {
	if (pVars->seamFace) {
		pVars->fullSort = 1;
		return;
	}
	Piece *pPiece = pVars->pPiece;
	do {
		if (pPiece->pKeep) {
			pVars->fullSort = 1;
			return;
		}
		pPiece = pPiece->pNext;
	} while (pPiece);
}

static
void addFaceToOutMesh(Vars *pVars, Mesh *pMeshOut, int32_t *pIndices,
                      int32_t count, int32_t *pIndexTable, SendOffArgs *pJobArgs) {
	int32_t loopBase = pMeshOut->mesh.loopCount;
	for (int32_t k = 0; k < count; ++k) {
		int32_t bufIndex = pIndexTable[pIndices[k] + 1];
		pMeshOut->mesh.pLoops[loopBase + k] = pVars->loopBuf.pBuf[bufIndex].loop;
		pMeshOut->mesh.pEdges[loopBase + k] = pVars->loopBuf.pBuf[bufIndex].edge;
		int32_t bufLoop = pVars->loopBuf.pBuf[bufIndex].bufLoop;
		int32_t job = pVars->loopBuf.pBuf[bufIndex].job;
		copyAllAttribs(&pMeshOut->mesh.loopAttribs, loopBase + k,
					   &pJobArgs[job].bufMesh.mesh.loopAttribs, bufLoop);
		//*attribAsV3(pMeshOut->pNormals, loopBase + k) =
		//	pVars->normalBuf[indexTable[k + 1]];
		//*attribAsV2(pMeshOut->pUvs, loopBase + k) =
		//	pVars->uvBuf[indexTable[k + 1]];
	}
	copyAllAttribs(&pMeshOut->mesh.faceAttribs,
				   pMeshOut->mesh.faceCount,
				   &pJobArgs[pVars->loopBuf.pBuf[0].job].bufMesh.mesh.faceAttribs,
				   pVars->loopBuf.pBuf[0].bufFace);
	pMeshOut->mesh.pFaces[pMeshOut->mesh.faceCount] = loopBase;
	pMeshOut->mesh.loopCount += count;
	pMeshOut->mesh.faceCount++;
}

void ruvmMergeSingleBorderFace(uint64_t *pTimeSpent, RuvmContext pContext,
                               RuvmMap pMap, Mesh *pMeshOut,
							   SendOffArgs *pJobArgs, Piece *pPiece,
							   CombineTables *pCTables, JobBases *pJobBases,
							   FaceInfo *pRuvmFace, MergeBufHandles *pMergeBufHandles) {
	CLOCK_INIT
	CLOCK_START;
	Vars vars = {0};
	vars.pPiece = pPiece;
	vars.pEntry = pPiece->pEntry;
	vars.loopBuf.pBuf = pMergeBufHandles->pLoopBuf;
	vars.seamBuf.pBuf = pMergeBufHandles->pSeamBuf;
	vars.mapLoopBuf.pBuf = pMergeBufHandles->pMapLoopBuf;
	if (!vars.pEntry) {
		return;
	}
	vars.ruvmFace = *pRuvmFace;
	vars.seamFace = determineIfSeamFace(pMap, vars.pEntry);
	determineIfFullSort(&vars);
	//determineIfTriangulate(&vars);
	vars.loopBuf.pBuf[0].sort = -10;
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[2] += CLOCK_TIME_DIFF(start, stop);
	CLOCK_START;
	if (vars.seamFace) {
		int32_t totalVerts = 0;
		determineLoopsToKeep(pContext, pMap, &vars, pJobArgs, &totalVerts,
		                     pJobBases, pPiece);
		_(&vars.centre V2DIVSEQL (float)totalVerts);
	}
	else if (vars.fullSort) {
		getCentroid(&vars, pJobArgs);
	}
	if (vars.fullSort) {
		buildApproximateTbnInverse(&vars, pJobArgs);
	}
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[3] += CLOCK_TIME_DIFF(start, stop);
	CLOCK_START;
	pMeshOut->mesh.pFaces[pMeshOut->mesh.faceCount] = pMeshOut->mesh.loopCount;
	addLoopsToBufAndVertsToMesh(pTimeSpent, pContext, pCTables, pMap,
								   &vars, pJobArgs, pMeshOut, pJobBases);
	if (vars.loopBuf.count <= 2) {
		return;
	}
	int32_t indexTable[32];
	indexTable[0] = -1;
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[4] += CLOCK_TIME_DIFF(start, stop);
	CLOCK_START;
	if (vars.fullSort) {
		//full winding sort
		sortLoopsFull(indexTable + 1, &vars, pMeshOut);
	}
	else {
		sortLoops(indexTable + 1, &vars);
	}
	if (vars.triangulate) {
		FaceInfo tempFace = {0};
		tempFace.end = tempFace.size = vars.loopBuf.count;
		V2_F32 uvBuf[32];
		for (int32_t i = 0; i < vars.loopBuf.count; ++i) {
			uvBuf[i] = vars.loopBuf.pBuf[indexTable[i + 1]].uv;
		}
		FaceTriangulated tris;
		tris = triangulateFace(pContext->alloc, tempFace, uvBuf, NULL, 1);
		for (int32_t i = 0; i < tris.triCount; ++i) {
			addFaceToOutMesh(&vars, pMeshOut, tris.pLoops + (i * 3), 3,
			                 indexTable, pJobArgs);
		}
	}
	else {
		int32_t indices[32];
		for (int32_t i = 0; i < vars.loopBuf.count; ++i) {
			indices[i] = i;
		}
		addFaceToOutMesh(&vars, pMeshOut, indices, vars.loopBuf.count,
		                 indexTable, pJobArgs);

		//int32_t loopBase = pMeshOut->mesh.loopCount;
		//for (int32_t k = 0; k < vars.loopBufSize; ++k) {
		//int32_t bufIndex = indexTable[k + 1];
		//pMeshOut->mesh.pLoops[loopBase + k] =
		//	vars.loopBuf[bufIndex];
		//pMeshOut->mesh.pEdges[loopBase + k] = 
		//	vars.edgeBuf[bufIndex];
		//int32_t bufLoop = vars.bufLoopBuf[bufIndex];
		//int32_t job = vars.jobBuf[bufIndex];
		//copyAllAttribs(&pMeshOut->mesh.loopAttribs, loopBase + k,
		//			   &pJobArgs[job].bufMesh.mesh.loopAttribs, bufLoop);
		////*attribAsV3(pMeshOut->pNormals, loopBase + k) =
		////	vars.normalBuf[indexTable[k + 1]];
		////*attribAsV2(pMeshOut->pUvs, loopBase + k) =
		////	vars.uvBuf[indexTable[k + 1]];
		//}
		//copyAllAttribs(&pMeshOut->mesh.faceAttribs,
		//			   pMeshOut->mesh.faceCount,
		//			   &pJobArgs[vars.jobBuf[0]].bufMesh.mesh.faceAttribs,
		//			   vars.bufFaceBuf[0]);
		//pMeshOut->mesh.loopCount += vars.loopBufSize;
		//pMeshOut->mesh.faceCount++;
	}
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[5] += CLOCK_TIME_DIFF(start, stop);
}
