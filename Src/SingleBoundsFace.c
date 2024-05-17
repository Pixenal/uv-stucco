#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

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
	V2_F32 vertBuf;
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
} MapLoopBuf;

typedef struct {
	LoopBuf loopBuf;
	MapLoopBuf mapLoopBuf;
	int32_t *pIndexTable;
	int32_t *pIndices;
	V2_F32 *pSortedUvBuf;
	int32_t size;
} MergeBufs;

typedef struct {
	Mat3x3 tbnInv;
	FaceRange ruvmFace;
	int32_t bufSize;
	LoopBuf loopBuf;
	MapLoopBuf mapLoopBuf;
	int32_t *pIndexTable;
	V2_F32 *pSortedUvBuf;
	PieceArr *pPieceArr;
	Piece *pPieceRoot;
	int32_t infoBufSize;
	_Bool seamFace;
	_Bool fullSort;
	_Bool triangulate;
} Vars;

/*
static int32_t getEdgeLocalLoop(int32_t *pEdgeLoops, FaceRange *pBaseFace) {
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
		pContext->alloc.pFree(pHandle->pMapLoopBuf);
		pContext->alloc.pFree(pHandle->pIndexTable);
		pContext->alloc.pFree(pHandle->pSortedUvs);
	}
}

void ruvmAllocMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle,
                        int32_t totalVerts) {
	assert(totalVerts >= 0 && totalVerts < 100000);
	if (totalVerts > pHandle->size) {
		if (pHandle->size) {
			ruvmDestroyMergeBufs(pContext, pHandle);
		}
		pHandle->pLoopBuf =
			pContext->alloc.pMalloc(sizeof(LoopBufEntry) * totalVerts);
		pHandle->pMapLoopBuf =
			pContext->alloc.pMalloc(sizeof(int32_t) * totalVerts);
		pHandle->pIndexTable =
			pContext->alloc.pMalloc(sizeof(int32_t) * totalVerts);
		pHandle->pSortedUvs =
			pContext->alloc.pMalloc(sizeof(V2_F32) * totalVerts);
		pHandle->size = totalVerts;
	}
}

static
int32_t determineIfSeamFace(RuvmMap pMap, BorderFace *pEntry) {
	int32_t faceIndex = pEntry->faceIndex;
	int32_t ruvmLoops = 0;
	do {
		for (int32_t i = 0; i < 11; ++i) {
			ruvmLoops += getIfRuvm(pEntry, i);
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
	FaceRange face = getFaceRange(&pMap->mesh.mesh, faceIndex, 1);
	return ruvmLoops < face.size;
}

static
void addLoopsWithSingleVert(RuvmContext pContext, Vars *pVars,
                            int32_t tableSize, BorderVert *localEdgeTable) {
	assert(tableSize >= 0 && tableSize < 10000);
	for (int32_t i = 0; i < tableSize; ++i) {
		BorderVert *pEdgeEntry = localEdgeTable + i;
		int32_t depth = 0;
		do {
			if (pEdgeEntry->loops == 1) {
				Piece *pPiece = pVars->pPieceArr->pArr + pEdgeEntry->entryIndex;
				//TODO replace with better asserts
				assert(pEdgeEntry->loop >= 0);
				assert(pEdgeEntry->entryIndex >= 0);
				pPiece->keepSingle |= 1 << pEdgeEntry->loop;
			}
			BorderVert *pNextEdgeEntry = pEdgeEntry->pNext;
			if (depth > 0) {
				pContext->alloc.pFree(pEdgeEntry);
			}
			depth++;
			pEdgeEntry = pNextEdgeEntry;
		} while(pEdgeEntry);
		assert(i >= 0 && i < tableSize);
	}
}

static
void initLocalEdgeTableEntry(BorderVert *pEdgeEntry, Piece *pPiece,
                             int32_t ruvmEdge, BorderInInfo *pInInfo,
							 int32_t loop, int32_t faceStart) {
	pEdgeEntry->ruvmEdge = ruvmEdge;
	pEdgeEntry->tile = pPiece->pEntry->tile;
	pEdgeEntry->baseEdge = pInInfo->edge;
	pEdgeEntry->baseVert = pInInfo->vert;
	pEdgeEntry->loops = 1;
	pEdgeEntry->loop = loop;
	pEdgeEntry->loopIndex = faceStart - loop;
	pEdgeEntry->job = pPiece->pEntry->job;
	pEdgeEntry->entryIndex = pPiece->entryIndex;
}

static
void addLoopToLocalEdgeTable(RuvmContext pContext, int32_t tableSize,
                             BorderVert *localEdgeTable, Vars *pVars,
							 SendOffArgs *pJobArgs, BorderFace *pEntry,
							 RuvmMap pMap, int32_t faceStart,
							 int32_t k, Piece *pPiece) {
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pJobArgs, k);
	Mesh *pInMesh = &pJobArgs[pEntry->job].mesh;
	_Bool isOnInVert = getIfOnInVert(pEntry, k);
	int32_t mapEdge = -1;
	if (!isOnInVert) {
		inInfo.vert = -1;
		int32_t mapLoop = getMapLoop(pEntry, pMap, k);
		mapEdge = pMap->mesh.mesh.pEdges[pVars->ruvmFace.start + mapLoop];
		assert(mapEdge >= 0 && mapEdge < pMap->mesh.mesh.edgeCount);
	}
	int32_t indexToHash = isOnInVert ? inInfo.vert : mapEdge;
	int32_t hash = ruvmFnvHash((uint8_t *)&indexToHash, 4, tableSize);
	BorderVert *pEdgeEntry = localEdgeTable + hash;
	if (!pEdgeEntry->loops) {
		initLocalEdgeTableEntry(pEdgeEntry, pPiece, mapEdge, &inInfo,
		                        k, faceStart);
	}
	else {
		do {
			//Check entry is valid
			assert(pEdgeEntry->baseVert >= -1);
			assert(pEdgeEntry->baseVert < pInMesh->mesh.vertCount);
			int32_t	match;
			if (isOnInVert) {
				match = pEdgeEntry->baseVert == inInfo.vert;
			}
			else {
				match =  pEdgeEntry->ruvmEdge == mapEdge &&
						 pEdgeEntry->tile == pEntry->tile &&
						 pEdgeEntry->baseEdge == inInfo.edge;
			}
			if (match) {
				assert(pEdgeEntry->loops > 0); //Check entry is valid
				pEdgeEntry->loops++;
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry = pEdgeEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(BorderVert));
					initLocalEdgeTableEntry(pEdgeEntry, pPiece, mapEdge, &inInfo,
					                        k, faceStart);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
}

static
void determineLoopsToKeep(RuvmContext pContext, RuvmMap pMap,
                          Vars *pVars, SendOffArgs *pJobArgs,
                          int32_t aproxVertsPerPiece,
						  JobBases *pJobBases) {
	int32_t tableSize = aproxVertsPerPiece;
	assert(tableSize >= 0 && tableSize < 10000);
	BorderVert *pLocalEdgeTable =
		pContext->alloc.pCalloc(tableSize, sizeof(BorderVert));
	Piece *pPiece = pVars->pPieceRoot;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
		FaceRange face = pPiece->bufFace;
		for (int32_t k = 0; k < face.size; ++k) {
			int32_t vert = pBufMesh->mesh.pLoops[face.start - k];
			if (getIfRuvm(pEntry, k)) {
				vert += pJobBases[pEntry->job].vertBase;
			}
			else {
				addLoopToLocalEdgeTable(pContext, tableSize, pLocalEdgeTable,
				                        pVars, pJobArgs, pEntry, pMap, face.start,
										k, pPiece);
			}
			assert(k >= 0 && k < face.size);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	addLoopsWithSingleVert(pContext, pVars, tableSize, pLocalEdgeTable);
	pContext->alloc.pFree(pLocalEdgeTable);
}

static
void buildApproximateTbnInverse(Vars *pVars, SendOffArgs *pJobArgs) {
	BorderFace *pEntry = pVars->pPieceRoot->pEntry;
	V3_F32 normal = {0};
	int32_t entryCount = 0;
	do {
		BufMesh* pBufMesh = &pJobArgs[pEntry->job].bufMesh;
		int32_t faceStart = pBufMesh->mesh.pFaces[pEntry->face];
		assert(faceStart > pBufMesh->borderLoopCount);
		assert(faceStart < 100000000); //Probably invalid if greater
		//Check face size is at least 3
		assert(faceStart - pBufMesh->mesh.pFaces[pEntry->face - 1] >= 3);
		int32_t* pLoops = pBufMesh->mesh.pLoops;
		V3_F32* pVertA = pBufMesh->pVerts + pLoops[faceStart];
		V3_F32* pVertB = pBufMesh->pVerts + pLoops[faceStart - 1];
		V3_F32* pVertC = pBufMesh->pVerts + pLoops[faceStart - 2];
		assert(v3IsFinite(*pVertA) && v3IsFinite(*pVertB) && v3IsFinite(*pVertC));
		V3_F32 ab = _(*pVertB V3SUB *pVertA);
		V3_F32 ac = _(*pVertC V3SUB *pVertA);
		_(&normal V3ADDEQL v3Cross(ab, ac));
		assert(v3IsFinite(normal));
		assert(entryCount >= 0 && entryCount < 100000);
		entryCount++;
		pEntry = pEntry->pNext;
	} while (pEntry);
	assert(entryCount > 0 && entryCount < 100000);
	_(&normal V3DIVEQLS entryCount);
	float normalLen =
		sqrt(normal.d[0] * normal.d[0] +
		     normal.d[1] * normal.d[1] +
		     normal.d[2] * normal.d[2]);
	_(&normal V3DIVEQLS normalLen);
	assert(v3IsFinite(normal));
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
	assert(mat3x3IsFinite(&pVars->tbnInv));
}

static void initVertTableEntry(BorderVert *pVertEntry, BorderFace *pEntry,
                               Mesh *pMeshOut, BufMesh *pBufMesh, int32_t ruvmEdge,
							   int32_t *pVert, BorderInInfo *pInInfo,
							   int32_t ruvmFace, int32_t loop) {
	copyAllAttribs(&pMeshOut->mesh.vertAttribs, pMeshOut->mesh.vertCount,
				   &pBufMesh->mesh.vertAttribs, *pVert);
	*pVert = pMeshOut->mesh.vertCount;
	pVertEntry->vert = pMeshOut->mesh.vertCount;
	pMeshOut->mesh.vertCount++;
	pVertEntry->tile = pEntry->tile;
	pVertEntry->ruvmEdge = ruvmEdge;
	pVertEntry->loops = 1;
	pVertEntry->baseEdge = pInInfo->edge;
	pVertEntry->baseVert = pInInfo->vert;
	pVertEntry->loopIndex = pInInfo->loop;
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
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pJobArgs, k);
	_Bool isOnInVert = getIfOnInVert(pEntry, k);
	if (!isOnInVert) {
		inInfo.vert = -1;
	}
	int32_t hash;
	int32_t ruvmEdge;
	BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
	if (isOnInVert) {
		hash = ruvmFnvHash((uint8_t *)&inInfo.vert, 4, pCTables->vertTableSize);
		ruvmEdge = -1;
	}
	else {
		ruvmEdge = pMap->mesh.mesh.pEdges[pVars->ruvmFace.start + ruvmLoop];
		hash = ruvmFnvHash((uint8_t *)&ruvmEdge, 4, pCTables->vertTableSize);
	}
	BorderVert *pVertEntry = pCTables->pVertTable + hash;
	if (!pVertEntry->loops) {
		initVertTableEntry(pVertEntry, pEntry, pMeshOut, pBufMesh,
		                   ruvmEdge, pVert, &inInfo, pEntry->faceIndex,
						   loop);
	}
	else {
		do {
			//Check vert entry is valid
			assert(pVertEntry->ruvmEdge >= -1);
			assert(pVertEntry->ruvmEdge < pMap->mesh.mesh.edgeCount);
			assert(pVertEntry->ruvmFace >= 0);
			assert(pVertEntry->ruvmFace < pMap->mesh.mesh.faceCount);
			_Bool match;
			if (isOnInVert) {
				V2_F32 *pMeshInUvA = pJobArgs[0].mesh.pUvs + pVertEntry->loopIndex;
				V2_F32 *pMeshInUvB = pJobArgs[0].mesh.pUvs + inInfo.loop;
				match = pVertEntry->baseVert == inInfo.vert &&
						pVertEntry->ruvmFace == pEntry->faceIndex &&
						pMeshInUvA->d[0] == pMeshInUvB->d[0] &&
						pMeshInUvA->d[1] == pMeshInUvB->d[1];
			}
			else {
				_Bool connected = 
					_(pJobArgs[pEntry->job].bufMesh.pUvs[loop] V2APROXEQL
					  pJobArgs[pVertEntry->job].bufMesh.pUvs[pVertEntry->loop]);
				match =  pVertEntry->ruvmEdge == ruvmEdge &&
						 pVertEntry->tile == pEntry->tile &&
						 pVertEntry->baseEdge == inInfo.edge &&
						 connected;
			}
			if (match) {
				//If loop isOnInVert,
				//then entry must also be an isOnInVert entry.
				//And if not, then entry must also not be
				assert((isOnInVert && pVertEntry->baseVert != -1) ||
				       (!isOnInVert && pVertEntry->baseVert == -1));
				*pVert = pVertEntry->vert;
				break;
			}
			if (!pVertEntry->pNext) {
				pVertEntry = pVertEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(BorderVert));
				initVertTableEntry(pVertEntry, pEntry, pMeshOut, pBufMesh,
				                   ruvmEdge, pVert, &inInfo, pEntry->faceIndex,
								   loop);
				break;
			}
			pVertEntry = pVertEntry->pNext;
		} while(1);
	}
	//TODO debug/ verify border edge implementation is working correctly
	uint32_t valueToHash = inInfo.edge + pEntry->faceIndex;
	hash = ruvmFnvHash((uint8_t *)&valueToHash, 4, pCTables->edgeTableSize);
	BorderEdge *pEdgeEntry = pCTables->pEdgeTable + hash;
	if (!pEdgeEntry->valid) {
		initEdgeTableEntry(pEdgeEntry, pMeshOut, pBufMesh, pEdge, inInfo.edge,
		                   pEntry->faceIndex);
	}
	else {
		do {
			if (pEdgeEntry->inEdge == inInfo.edge &&
				pEdgeEntry->mapFace == pEntry->faceIndex) {
				*pEdge = pEdgeEntry->edge;
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry = pEdgeEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(BorderEdge));
				initEdgeTableEntry(pEdgeEntry, pMeshOut, pBufMesh, pEdge,
				                   inInfo.edge, pEntry->faceIndex);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
}

static
_Bool checkIfDup(Vars *pVars, int32_t ruvmLoop) {
	for (int32_t i = 0; i < pVars->mapLoopBuf.count; ++i) {
		if (ruvmLoop == pVars->mapLoopBuf.pBuf[i]) {
			return true;
		}
		assert(i >= 0 && i < pVars->mapLoopBuf.count);
	}
	return false;
}

static void initOnLineTableEntry(OnLine *pEntry, Mesh *pMeshOut,
                                 BufMesh *pBufMesh, int32_t base,
								 _Bool isBaseLoop, int32_t ruvmVert,
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
	_Bool skip = true;
	if (pPiece->keepSingle >> k & 1) {
		skip = false;
	}
	//override if keep is set to 1
	if (skip && (pPiece->keepPreserve >> k & 1 ||
		         pPiece->keepOnInVert >> k & 1 ||
		         pPiece->keepSeam >> k & 1)) {
		skip = false;
		pVars->triangulate = true;
	}
	return skip;
}

static
void addOnLineVert(RuvmContext pContext, Vars *pVars, int32_t ruvmLoop,
                   RuvmMap pMap, SendOffArgs *pJobArgs, BorderFace *pEntry,
				   CombineTables *pCTables, Mesh *pMeshOut, int32_t *pVert,
				   int32_t k) {
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pJobArgs, k);
	_Bool isOnInVert = getIfOnInVert(pEntry, k);
	int32_t ruvmVert = pMap->mesh.mesh.pLoops[pVars->ruvmFace.start + ruvmLoop];
	int32_t base = isOnInVert ? inInfo.loop : inInfo.edge;
	int32_t hash = ruvmFnvHash((uint8_t *)&base, 4, pCTables->onLineTableSize);
	OnLine *pOnLineEntry = pCTables->pOnLineTable + hash;
	if (!pOnLineEntry->type) {
		initOnLineTableEntry(pOnLineEntry, pMeshOut,
							 &pJobArgs[pEntry->job].bufMesh,
							 base, isOnInVert, ruvmVert, pVert);
	}
	else {
		do {
			_Bool match = base == pOnLineEntry->baseEdgeOrLoop &&
			              ruvmVert == pOnLineEntry->ruvmVert &&
			              isOnInVert + 1 == pOnLineEntry->type;
			if (match) {
				*pVert = pOnLineEntry->outVert;
				break;
			}
			if (!pOnLineEntry->pNext) {
				pOnLineEntry = pOnLineEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(OnLine));
				initOnLineTableEntry(pOnLineEntry, pMeshOut,
									 &pJobArgs[pEntry->job].bufMesh,
									 base, isOnInVert, ruvmVert, pVert);
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
	Piece *pPiece = pVars->pPieceRoot;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		//Check entry is valid
		assert(pEntry->baseLoop || pEntry->ruvmLoop || pEntry->onInVert);
		BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
		FaceRange face = pPiece->bufFace;
		for (int32_t k = 0; k < face.size; ++k) {
			//CLOCK_START;
			int32_t vert;
			int32_t edge;
			_Bool isRuvm = getIfRuvm(pEntry, k);
			if (!isRuvm) {
				//is not an ruvm loop (is an intersection, or base loop))
				if (checkIfShouldSkip(pVars, pPiece, face.start, k)) {
					continue;
				}
				//CLOCK_STOP_NO_PRINT;
				//pTimeSpent[3] += CLOCK_TIME_DIFF(start, stop);
				//CLOCK_START;
				vert = pBufMesh->mesh.pLoops[face.start - k];
				assert(vert > pBufMesh->borderVertCount && vert < 100000000);
				edge = pBufMesh->mesh.pEdges[face.start - k];
				assert(edge > pBufMesh->borderEdgeCount && edge < 100000000);
				int32_t mapLoop = getMapLoop(pEntry, pMap, k);
				assert(mapLoop >= 0 && mapLoop < pMap->mesh.mesh.loopCount);
				addBorderLoopAndVert(pContext, pCTables, pMap, pVars, &vert,
				                     pEntry, pJobArgs, pMeshOut, k, mapLoop,
									 &edge, face.start - k);
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
				_Bool onLine = getIfOnLine(pEntry, k);
				int32_t mapLoop = getMapLoop(pEntry, pMap, k);
				vert = pBufMesh->mesh.pLoops[face.start - k];
				edge = pBufMesh->mesh.pEdges[face.start - k];
				if (onLine) {
					if (checkIfDup(pVars, mapLoop)) {
						continue;
					}
					addOnLineVert(pContext, pVars, mapLoop, pMap, pJobArgs,
					              pEntry, pCTables, pMeshOut, &vert, k);
				}
				//the vert and edge indices are local to the buf mesh,
				//so we need to offset them, so that they point to the
				//correct position in the out mesh. (these vars are set
				//when the non-border mesh data is copied
				else {
					vert += pJobBases[pEntry->job].vertBase;
				}
				edge += pJobBases[pEntry->job].edgeBase;
				
				//CLOCK_START;
				pVars->loopBuf.pBuf[pVars->loopBuf.count + 1].sort = mapLoop * 10;
				pVars->mapLoopBuf.pBuf[pVars->mapLoopBuf.count] = mapLoop;
				pVars->mapLoopBuf.count++;
				//CLOCK_STOP_NO_PRINT;
				//pTimeSpent[5] += CLOCK_TIME_DIFF(start, stop);
			}
			//if border loop, or if loop edge has been intersected,
			//add new edge to mesh
			//int32_t kNext = (k + 1) % faceSize;
			//int32_t vertNext = bufMesh->mesh.pLoops[face.start - kNext];
			//if (borderLoop || vertNext >= bufMesh->mesh.vertCount) {
			//}
			LoopBuf *pLoopBuf = &pVars->loopBuf;
			pLoopBuf->pBuf[pLoopBuf->count].job = pEntry->job;
			pLoopBuf->pBuf[pLoopBuf->count].bufLoop = face.start - k;
			pLoopBuf->pBuf[pLoopBuf->count].bufFace = pEntry->face;
			pLoopBuf->pBuf[pLoopBuf->count].loop = vert;
			pLoopBuf->pBuf[pLoopBuf->count].edge = edge;
			pLoopBuf->pBuf[pLoopBuf->count].uv = pBufMesh->pUvs[face.start - k];
			//CLOCK_START;
			//CLOCK_STOP_NO_PRINT;
			//pTimeSpent[6] += CLOCK_TIME_DIFF(start, stop);
			pVars->loopBuf.count++;
			pVars->infoBufSize++;
			assert(k >= 0 && k < face.size);
		}
		pContext->alloc.pFree(pEntry);
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void sortLoopsFull(int32_t *pIndexTable, Vars *pVars, Mesh *pMeshOut) {
	//insertion sort
	LoopBuf *pLoopBuf = &pVars->loopBuf;
	V2_F32 centre = {0};
	for (int32_t i = 0; i < pVars->loopBuf.count; ++i) {
		V3_F32* pVert = pMeshOut->pVerts + pVars->loopBuf.pBuf[i].loop;
		V3_F32 vertV3 = v3MultiplyMat3x3(*pVert, &pVars->tbnInv);
		pLoopBuf->pBuf[i].vertBuf.d[0] = vertV3.d[0];
		pLoopBuf->pBuf[i].vertBuf.d[1] = vertV3.d[1];
		_(&centre V2ADDEQL pLoopBuf->pBuf[i].vertBuf);
		assert(i >= 0 && i < pVars->loopBuf.count);
	}
	_(&centre V2DIVSEQL pVars->loopBuf.count);
	int32_t order = v2WindingCompare(pLoopBuf->pBuf[0].vertBuf,
	                                 pLoopBuf->pBuf[1].vertBuf,
	                                 centre, 1);
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufSize = 2;
	for (int32_t i = bufSize; i < pVars->loopBuf.count; ++i) {
		_Bool insert;
		int32_t j;
		for (j = bufSize - 1; j >= 0; --j) {
			if (j != 0) {
				insert = v2WindingCompare(pLoopBuf->pBuf[i].vertBuf,
					                      pLoopBuf->pBuf[pIndexTable[j]].vertBuf,
					                      centre, 1)
					&&
					v2WindingCompare(pLoopBuf->pBuf[pIndexTable[j - 1]].vertBuf,
					                 pLoopBuf->pBuf[i].vertBuf, centre, 1);
			}
			else {
				insert = v2WindingCompare(pLoopBuf->pBuf[i].vertBuf,
					                      pLoopBuf->pBuf[pIndexTable[j]].vertBuf,
					                      centre, 1);
			}
			if (insert) {
				break;
			}
			assert(j < bufSize && j >= 0);
		}
		if (!insert) {
			pIndexTable[bufSize] = i;
		}
		else {
			for (int32_t k = bufSize; k > j; --k) {
				pIndexTable[k] = pIndexTable[k - 1];
				assert(k <= bufSize && k > j);
			}
			pIndexTable[j] = i;
		}
		assert(i >= 0 && i < pVars->loopBuf.count);
		bufSize++;
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
	for (int32_t i = bufSize; i < pVars->loopBuf.count; ++i) {
		_Bool insert;
		int32_t j;
		for (j = bufSize - 1; j >= 0; --j) {
			insert = pLoopBuf[i].sort < pLoopBuf[pIndexTable[j]].sort &&
			         pLoopBuf[i].sort > pLoopBuf[pIndexTable[j - 1]].sort;
			if (insert) {
				break;
			}
			assert(j < bufSize && j >= 0);
		}
		if (!insert) {
			pIndexTable[bufSize] = i;
		}
		else {
			for (int32_t m = bufSize; m > j; --m) {
				pIndexTable[m] = pIndexTable[m - 1];
				assert(m <= bufSize && m > j);
			}
			pIndexTable[j] = i;
		}
		assert(i >= bufSize && i < pVars->loopBuf.count);
		bufSize++;
	}
}

static
void determineIfFullSort(Vars *pVars) {
	if (pVars->seamFace) {
		pVars->fullSort = 1;
		return;
	}
	Piece *pPiece = pVars->pPieceRoot;
	do {
		if (pPiece->keepPreserve ||
			pPiece->keepOnInVert || pPiece->keepSeam) {
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
	for (int32_t i = 0; i < count; ++i) {
		int32_t bufIndex = pIndexTable[pIndices[i] + 1];
		pMeshOut->mesh.pLoops[loopBase + i] = pVars->loopBuf.pBuf[bufIndex].loop;
		pMeshOut->mesh.pEdges[loopBase + i] = pVars->loopBuf.pBuf[bufIndex].edge;
		int32_t bufLoop = pVars->loopBuf.pBuf[bufIndex].bufLoop;
		int32_t job = pVars->loopBuf.pBuf[bufIndex].job;
		copyAllAttribs(&pMeshOut->mesh.loopAttribs, loopBase + i,
					   &pJobArgs[job].bufMesh.mesh.loopAttribs, bufLoop);
		assert(i >= 0 && i < count);
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
							   SendOffArgs *pJobArgs, int32_t entryIndex,
							   PieceArr *pPieceArr, CombineTables *pCTables,
							   JobBases *pJobBases, FaceRange *pRuvmFace,
							   MergeBufHandles *pMergeBufHandles,
							   int32_t approxVertsPerPiece) {
	CLOCK_INIT
	CLOCK_START;
	Vars vars = {0};
	vars.pPieceArr = pPieceArr;
	vars.pPieceRoot = pPieceArr->pArr + entryIndex;
	vars.bufSize = pMergeBufHandles->size;
	vars.loopBuf.pBuf = pMergeBufHandles->pLoopBuf;
	vars.mapLoopBuf.pBuf = pMergeBufHandles->pMapLoopBuf;
	vars.pIndexTable = pMergeBufHandles->pIndexTable;
	vars.pSortedUvBuf = pMergeBufHandles->pSortedUvs;
	if (!vars.pPieceRoot->pEntry) {
		return;
	}
	vars.ruvmFace = *pRuvmFace;
	vars.seamFace = determineIfSeamFace(pMap, vars.pPieceRoot->pEntry);
	determineIfFullSort(&vars);
	//determineIfTriangulate(&vars);
	vars.loopBuf.pBuf[0].sort = -10;
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[2] += CLOCK_TIME_DIFF(start, stop);
	CLOCK_START;
	if (vars.seamFace) {
		determineLoopsToKeep(pContext, pMap, &vars, pJobArgs,
		                     approxVertsPerPiece, pJobBases);
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
	vars.pIndexTable[0] = -1;
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[4] += CLOCK_TIME_DIFF(start, stop);
	CLOCK_START;
	if (vars.fullSort) {
		//full winding sort
		sortLoopsFull(vars.pIndexTable + 1, &vars, pMeshOut);
	}
	else {
		sortLoops(vars.pIndexTable + 1, &vars);
	}
	if (vars.triangulate) {
		FaceRange tempFace = {0};
		tempFace.end = tempFace.size = vars.loopBuf.count;
		for (int32_t i = 0; i < vars.loopBuf.count; ++i) {
			vars.pSortedUvBuf[i] = vars.loopBuf.pBuf[vars.pIndexTable[i + 1]].uv;
			assert(i >= 0 && i < vars.loopBuf.count);
		}
		FaceTriangulated tris;
		tris = triangulateFace(pContext->alloc, tempFace, vars.pSortedUvBuf,
		                       NULL, 1);
		for (int32_t i = 0; i < tris.triCount; ++i) {
			addFaceToOutMesh(&vars, pMeshOut, tris.pLoops + (i * 3), 3,
			                 vars.pIndexTable, pJobArgs);
			assert(i >= 0 && i < tris.triCount);
		}
	}
	else {
		assert(vars.loopBuf.count <= 11);
		int32_t indices[11] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
		addFaceToOutMesh(&vars, pMeshOut, indices, vars.loopBuf.count,
		                 vars.pIndexTable, pJobArgs);
	}
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[5] += CLOCK_TIME_DIFF(start, stop);
}
