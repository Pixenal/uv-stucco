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
#include <Error.h>

typedef struct {
	int32_t *pBuf;
	int32_t count;
} MapLoopBuf;

typedef struct {
	BoundsLoopBuf loopBuf;
	MapLoopBuf mapLoopBuf;
	int32_t *pIndexTable;
	int32_t *pIndices;
	int32_t *pSortedVertBuf;
	int32_t size;
} MergeBufs;

typedef struct {
	Mat3x3 tbnInv;
	FaceRange ruvmFace;
	int32_t bufSize;
	BoundsLoopBuf loopBuf;
	MapLoopBuf mapLoopBuf;
	MergeSendOffArgs *pArgs;
	int32_t *pIndexTable;
	int32_t *pSortedVertBuf;
	PieceArr *pPieceArr;
	Piece *pPieceRoot;
	int32_t *pInFaces;
	int32_t infoBufSize;
	int32_t entryCount;
	_Bool seamFace;
	_Bool fullSort;
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
		pContext->alloc.pFree(pHandle->pSortedVerts);
	}
}

void ruvmAllocMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle,
                        int32_t totalVerts) {
	RUVM_ASSERT("", totalVerts >= 0 && totalVerts < 100000);
	if (totalVerts > pHandle->size) {
		if (pHandle->size) {
			ruvmDestroyMergeBufs(pContext, pHandle);
		}
		pHandle->pLoopBuf =
			pContext->alloc.pMalloc(sizeof(BoundsLoopBufEntry) * (totalVerts + 1));
		pHandle->pMapLoopBuf =
			pContext->alloc.pMalloc(sizeof(int32_t) * totalVerts);
		pHandle->pIndexTable =
			pContext->alloc.pMalloc(sizeof(int32_t) * totalVerts);
		pHandle->pSortedVerts =
			pContext->alloc.pMalloc(sizeof(int32_t) * totalVerts);
		pHandle->size = totalVerts;
	}
}

static
void buildApproximateTbnInverse(Vars *pVars) {
	V3_F32 normal = {0};
	int32_t entryCount = 0;
	Piece *pPiece = pVars->pPieceRoot;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		BufMesh* pBufMesh = &pVars->pArgs->pJobArgs[pEntry->job].bufMesh;
		FaceRange face = pPiece->bufFace;
		Mesh *pMesh = asMesh(pBufMesh);
		RUVM_ASSERT("", face.size >= 3);
		V3_F32 vertA = pMesh->pVerts[bufMeshGetVertIndex(pPiece, pBufMesh, 0)];
		V3_F32 vertB = pMesh->pVerts[bufMeshGetVertIndex(pPiece, pBufMesh, 1)];
		V3_F32 vertC = pMesh->pVerts[bufMeshGetVertIndex(pPiece, pBufMesh, 2)];
		RUVM_ASSERT("", v3IsFinite(vertA) && v3IsFinite(vertB) && v3IsFinite(vertC));
		V3_F32 ab = _(vertB V3SUB vertA);
		V3_F32 ac = _(vertC V3SUB vertA);
		_(&normal V3ADDEQL v3Cross(ab, ac));
		RUVM_ASSERT("", v3IsFinite(normal));
		RUVM_ASSERT("", entryCount >= 0 && entryCount < 100000);
		entryCount++;
		pPiece = pPiece->pNext;
	} while (pPiece);
	RUVM_ASSERT("", entryCount > 0 && entryCount < 100000);
	_(&normal V3DIVEQLS entryCount);
	float normalLen =
		sqrt(normal.d[0] * normal.d[0] +
		     normal.d[1] * normal.d[1] +
		     normal.d[2] * normal.d[2]);
	_(&normal V3DIVEQLS normalLen);
	RUVM_ASSERT("", v3IsFinite(normal));
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
	RUVM_ASSERT("", mat3x3IsFinite(&pVars->tbnInv));
}

static void initVertTableEntry(MergeSendOffArgs *pArgs, BorderVert *pVertEntry,
                               BorderFace *pEntry, BufMesh *pBufMesh,
							   int32_t ruvmEdge, int32_t *pVert,
							   BorderInInfo *pInInfo, int32_t ruvmFace,
							   int32_t loop) {
	bool realloced = false;
	int32_t outVert = meshAddVert(&pArgs->pContext->alloc, pArgs->pMeshOut, &realloced);
	copyAllAttribs(&pArgs->pMeshOut->mesh.vertAttribs, outVert,
				   &asMesh(pBufMesh)->mesh.vertAttribs, *pVert);
	*pVert = outVert;
	pVertEntry->vert = outVert;
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

static void initEdgeTableEntry(MergeSendOffArgs *pArgs, BorderEdge *pSeamEntry,
                               BufMesh *pBufMesh, int32_t *pEdge,
							   int32_t inEdge, int32_t mapFace) {
	RuvmContext pContext = pArgs->pContext;
	bool realloced = false;
	int32_t edgeOut = meshAddEdge(&pContext->alloc, pArgs->pMeshOut, &realloced);
	copyAllAttribs(&pArgs->pMeshOut->mesh.edgeAttribs, edgeOut,
				   &asMesh(pBufMesh)->mesh.edgeAttribs, *pEdge);
	*pEdge = edgeOut;
	pSeamEntry->edge = *pEdge;
	pSeamEntry->inEdge = inEdge;
	pSeamEntry->mapFace = mapFace;
}

static
void addBorderLoopAndVert(Vars *pVars, int32_t *pVert,
                          BorderFace *pEntry, int32_t k,
						  int32_t ruvmLoop, int32_t *pEdge, int32_t loop) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, k);
	_Bool isOnInVert = getIfOnInVert(pEntry, k);
	if (!isOnInVert) {
		inInfo.vert = -1;
	}
	int32_t hash;
	int32_t ruvmEdge;
	BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
	if (isOnInVert) {
		hash = ruvmFnvHash((uint8_t *)&inInfo.vert, 4, pArgs->pCTables->vertTableSize);
		ruvmEdge = -1;
	}
	else {
		ruvmEdge = pArgs->pMap->mesh.mesh.pEdges[pVars->ruvmFace.start + ruvmLoop];
		hash = ruvmFnvHash((uint8_t *)&ruvmEdge, 4, pArgs->pCTables->vertTableSize);
	}
	BorderVert *pVertEntry = pArgs->pCTables->pVertTable + hash;
	if (!pVertEntry->loops) {
		initVertTableEntry(pArgs, pVertEntry, pEntry, pBufMesh, ruvmEdge,
		                   pVert, &inInfo, pEntry->faceIndex, loop);
	}
	else {
		do {
			//Check vert entry is valid
			RUVM_ASSERT("", pVertEntry->ruvmEdge >= -1);
			RUVM_ASSERT("", pVertEntry->ruvmEdge < pArgs->pMap->mesh.mesh.edgeCount);
			RUVM_ASSERT("", pVertEntry->ruvmFace >= 0);
			RUVM_ASSERT("", pVertEntry->ruvmFace < pArgs->pMap->mesh.mesh.faceCount);
			_Bool match;
			if (isOnInVert) {
				V2_F32 *pMeshInUvA = pArgs->pJobArgs[0].mesh.pUvs + pVertEntry->loopIndex;
				V2_F32 *pMeshInUvB = pArgs->pJobArgs[0].mesh.pUvs + inInfo.loop;
				match = pVertEntry->baseVert == inInfo.vert &&
						pVertEntry->ruvmFace == pEntry->faceIndex &&
						pMeshInUvA->d[0] == pMeshInUvB->d[0] &&
						pMeshInUvA->d[1] == pMeshInUvB->d[1];
			}
			else {
				BufMesh *pOtherBufMesh = &pArgs->pJobArgs[pVertEntry->job].bufMesh;
				_Bool connected = 
					_(asMesh(pBufMesh)->pUvs[loop] V2APROXEQL
					  asMesh(pOtherBufMesh)->pUvs[pVertEntry->loop]);
				match =  pVertEntry->ruvmEdge == ruvmEdge &&
						 pVertEntry->tile == pEntry->tile &&
						 pVertEntry->baseEdge == inInfo.edge &&
						 connected;
			}
			if (match) {
				//If loop isOnInVert,
				//then entry must also be an isOnInVert entry.
				//And if not, then entry must also not be
				RUVM_ASSERT("", (isOnInVert && pVertEntry->baseVert != -1) ||
				       (!isOnInVert && pVertEntry->baseVert == -1));
				*pVert = pVertEntry->vert;
				break;
			}
			if (!pVertEntry->pNext) {
				pVertEntry = pVertEntry->pNext =
					pArgs->pContext->alloc.pCalloc(1, sizeof(BorderVert));
				initVertTableEntry(pArgs, pVertEntry, pEntry, pBufMesh, ruvmEdge,
				                   pVert, &inInfo, pEntry->faceIndex, loop);
				break;
			}
			pVertEntry = pVertEntry->pNext;
		} while(1);
	}
	//TODO debug/ verify border edge implementation is working correctly
	uint32_t valueToHash = inInfo.edge + pEntry->faceIndex;
	hash = ruvmFnvHash((uint8_t *)&valueToHash, 4, pArgs->pCTables->edgeTableSize);
	BorderEdge *pEdgeEntry = pArgs->pCTables->pEdgeTable + hash;
	if (!pEdgeEntry->valid) {
		initEdgeTableEntry(pArgs, pEdgeEntry, pBufMesh, pEdge, inInfo.edge,
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
					pArgs->pContext->alloc.pCalloc(1, sizeof(BorderEdge));
				initEdgeTableEntry(pArgs, pEdgeEntry, pBufMesh, pEdge,
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
		RUVM_ASSERT("", i >= 0 && i < pVars->mapLoopBuf.count);
	}
	return false;
}

static void initOnLineTableEntry(MergeSendOffArgs *pArgs, OnLine *pEntry,
                                 BufMesh *pBufMesh, int32_t base,
								 _Bool isBaseLoop, int32_t ruvmVert,
								 int32_t *pVert) {
	RuvmContext pContext = pArgs->pContext;
	bool realloced = false;
	int32_t outVert = meshAddVert(&pContext->alloc, pArgs->pMeshOut, &realloced);
	copyAllAttribs(&pArgs->pMeshOut->mesh.vertAttribs, outVert,
				   &asMesh(pBufMesh)->mesh.vertAttribs, *pVert);
	*pVert = outVert;
	pEntry->outVert = *pVert;
	pEntry->baseEdgeOrLoop = base;
	pEntry->ruvmVert = ruvmVert;
	pEntry->type = isBaseLoop + 1;
}

static
void addOnLineVert(Vars *pVars, int32_t ruvmLoop,
                   BorderFace *pEntry, int32_t *pVert, int32_t k) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, k);
	_Bool isOnInVert = getIfOnInVert(pEntry, k);
	int32_t base = isOnInVert ? inInfo.vert : inInfo.edge;
	int32_t ruvmVert = pArgs->pMap->mesh.mesh.pLoops[pVars->ruvmFace.start + ruvmLoop];
	int32_t hash = ruvmFnvHash((uint8_t *)&base, 4, pArgs->pCTables->onLineTableSize);
	OnLine *pOnLineEntry = pArgs->pCTables->pOnLineTable + hash;
	if (!pOnLineEntry->type) {
		initOnLineTableEntry(pArgs, pOnLineEntry,
							 &pArgs->pJobArgs[pEntry->job].bufMesh,
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
					pArgs->pContext->alloc.pCalloc(1, sizeof(OnLine));
				initOnLineTableEntry(pArgs, pOnLineEntry,
									 &pArgs->pJobArgs[pEntry->job].bufMesh,
									 base, isOnInVert, ruvmVert, pVert);
				break;
			}
			pOnLineEntry = pOnLineEntry->pNext;
		} while(pOnLineEntry);
	}
}

static
bool addLoopsToBufAndVertsToMesh(Vars *pVars) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
	//CLOCK_INIT;
	//pieces should be called sub pieces here
	Piece *pPiece = pVars->pPieceRoot;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		//Check entry is valid
		RUVM_ASSERT("", pEntry->baseLoop || pEntry->ruvmLoop || pEntry->onInVert);
		BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
		FaceRange face = pPiece->bufFace;
		for (int32_t k = 0; k < face.size; ++k) {
			//CLOCK_START;
			if (pPiece->skip >> k & 0x01) {
				continue;
			}
			int32_t vert;
			int32_t edge;
			_Bool isRuvm = getIfRuvm(pEntry, k);
			if (!isRuvm) {
				//is not an ruvm loop (is an intersection, or base loop))
				
				if (!pPiece->order[k]) {
					//was not set skip, and was not given an order, must be interior
					continue;
				}
				//CLOCK_STOP_NO_PRINT;
				//pTimeSpent[3] += CLOCK_TIME_DIFF(start, stop);
				//CLOCK_START;
				vert = bufMeshGetVertIndex(pPiece, pBufMesh, k);
				RUVM_ASSERT("", vert > asMesh(pBufMesh)->vertBufSize - 1 -
				       pBufMesh->borderVertCount);
				RUVM_ASSERT("", vert < asMesh(pBufMesh)->vertBufSize);
				edge = bufMeshGetEdgeIndex(pPiece, pBufMesh, k);
				RUVM_ASSERT("", edge > asMesh(pBufMesh)->edgeBufSize - 1 -
				       pBufMesh->borderEdgeCount);
				RUVM_ASSERT("", edge < asMesh(pBufMesh)->edgeBufSize);
				int32_t mapLoop = getMapLoop(pEntry, pArgs->pMap, k);
				RUVM_ASSERT("", mapLoop >= 0 && mapLoop < pArgs->pMap->mesh.mesh.loopCount);
				addBorderLoopAndVert(pVars, &vert, pEntry, k, mapLoop,
									 &edge, face.start - k);
				//CLOCK_STOP_NO_PRINT;
				//pTimeSpent[4] += CLOCK_TIME_DIFF(start, stop);
			}
			else {
				if (!pPiece->order[k]) {
					return true;
				}
				//is an ruvm loop (this includes ruvm loops sitting on base edges or verts)

				//add an item to pEntry in mapToMesh, which denotes if an ruvm
				//loop has a dot of 0 (is on a base edge).
				//Then add it to the edgetable if so, without calcing a wind of course.
				//Just use the base edge as the hash, instead of an ruvm edge (cause there isnt one).
				//Or just make a new hash table just for ruvm loops with zero dot.
				//That would probably be cleaner, and more memory concious tbh.
				_Bool onLine = getIfOnLine(pEntry, k);
				int32_t mapLoop = getMapLoop(pEntry, pArgs->pMap, k);
				edge = asMesh(pBufMesh)->mesh.pEdges[face.start - k];
				if (onLine) {
					vert = bufMeshGetVertIndex(pPiece, pBufMesh, k);
					RUVM_ASSERT("", vert > asMesh(pBufMesh)->vertBufSize - 1 -
						   pBufMesh->borderVertCount);
					RUVM_ASSERT("", vert < asMesh(pBufMesh)->vertBufSize);
					addOnLineVert(pVars, mapLoop, pEntry, &vert, k);
				}
				//the vert and edge indices are local to the buf mesh,
				//so we need to offset them, so that they point to the
				//correct position in the out mesh. (these vars are set
				//when the non-border mesh data is copied
				else {
					vert = asMesh(pBufMesh)->mesh.pLoops[face.start - k];
					vert += pArgs->pJobBases[pEntry->job].vertBase;
				}
				edge += pArgs->pJobBases[pEntry->job].edgeBase;
				
				//CLOCK_START;
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
			BoundsLoopBuf *pLoopBuf = &pVars->loopBuf;
			RUVM_ASSERT("", pPiece->order[k] > 0);
			pVars->pIndexTable[pPiece->order[k] - 1] = pLoopBuf->count;
			pLoopBuf->pBuf[pLoopBuf->count].job = pEntry->job;
			pLoopBuf->pBuf[pLoopBuf->count].bufLoop = face.start - k;
			pLoopBuf->pBuf[pLoopBuf->count].bufFace = pEntry->face;
			pLoopBuf->pBuf[pLoopBuf->count].loop = vert;
			pLoopBuf->pBuf[pLoopBuf->count].edge = edge;
			pLoopBuf->pBuf[pLoopBuf->count].uv =
				asMesh(pBufMesh)->pUvs[face.start - k];
			//CLOCK_START;
			//CLOCK_STOP_NO_PRINT;
			//pTimeSpent[6] += CLOCK_TIME_DIFF(start, stop);
			pVars->loopBuf.count++;
			pVars->infoBufSize++;
			RUVM_ASSERT("", k >= 0 && k < face.size);
		}
		pArgs->pContext->alloc.pFree(pEntry);
		pPiece = pPiece->pNext;
	} while(pPiece);
	return false;
}

static
void addFaceToOutMesh(Vars *pVars, int32_t *pIndices,
                      int32_t count, int32_t *pIndexTable) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
	Mesh *pMeshOut = pVars->pArgs->pMeshOut;
	int32_t loopBase = pMeshOut->mesh.loopCount;
	bool realloced = false;
	for (int32_t i = 0; i < count; ++i) {
		int32_t bufIndex = pIndexTable[pIndices[i]];
		RUVM_ASSERT("", pVars->loopBuf.pBuf[bufIndex].loop >= 0);
		RUVM_ASSERT("", pVars->loopBuf.pBuf[bufIndex].loop < pMeshOut->mesh.vertCount);
		int32_t outLoop = meshAddLoop(&pArgs->pContext->alloc, pMeshOut, &realloced);
		RUVM_ASSERT("", outLoop == loopBase + i);
		pMeshOut->mesh.pLoops[outLoop] = pVars->loopBuf.pBuf[bufIndex].loop;
		RUVM_ASSERT("", pVars->loopBuf.pBuf[bufIndex].edge >= 0);
		RUVM_ASSERT("", pVars->loopBuf.pBuf[bufIndex].edge < pMeshOut->mesh.edgeCount);
		pMeshOut->mesh.pEdges[outLoop] = pVars->loopBuf.pBuf[bufIndex].edge;
		int32_t bufLoop = pVars->loopBuf.pBuf[bufIndex].bufLoop;
		int32_t job = pVars->loopBuf.pBuf[bufIndex].job;
		BufMesh *pBufMesh = &pArgs->pJobArgs[job].bufMesh;
		copyAllAttribs(&pMeshOut->mesh.loopAttribs, outLoop,
					   &asMesh(pBufMesh)->mesh.loopAttribs, bufLoop);
		RUVM_ASSERT("", i >= 0 && i < count);
	}
	realloced = false;
	int32_t outFace = meshAddFace(&pArgs->pContext->alloc, pMeshOut, &realloced);
	if (pArgs->ppInFaceTable) {
		if (realloced) {
			//realloc to match meshOut face buf
			*pArgs->ppInFaceTable =
				pArgs->pContext->alloc.pRealloc(*pArgs->ppInFaceTable,
					sizeof(InFaceArr) * pMeshOut->faceBufSize);
		}
		RUVM_ASSERT("", outFace < pMeshOut->faceBufSize);
		//add face to inFace table
		InFaceArr *pInFaceEntry = *pArgs->ppInFaceTable + outFace;
		RUVM_ASSERT("", pVars->entryCount > 0);
		pInFaceEntry->pArr =
			pArgs->pContext->alloc.pCalloc(pVars->entryCount, sizeof(int32_t));
		memcpy(pInFaceEntry->pArr, pVars->pInFaces,
				sizeof(int32_t) * pVars->entryCount);
		pInFaceEntry->count = pVars->entryCount;
		pInFaceEntry->usg = pVars->ruvmFace.index;
	}
	BufMesh *pBufMesh = &pArgs->pJobArgs[pVars->loopBuf.pBuf[0].job].bufMesh;
	copyAllAttribs(&pMeshOut->mesh.faceAttribs,
				   outFace,
				   &asMesh(pBufMesh)->mesh.faceAttribs,
				   pVars->loopBuf.pBuf[0].bufFace);
	pMeshOut->mesh.pFaces[outFace] = loopBase;
}

void ruvmMergeSingleBorderFace(MergeSendOffArgs *pArgs, uint64_t *pTimeSpent,
                               int32_t entryIndex, PieceArr *pPieceArr,
							   FaceRange *pRuvmFace,
							   MergeBufHandles *pMergeBufHandles,
                               int32_t *pInFaces, int32_t entryCount) {
	CLOCK_INIT
	CLOCK_START;
	Vars vars = {0};
	vars.pArgs = pArgs;
	vars.pPieceArr = pPieceArr;
	vars.pPieceRoot = pPieceArr->pArr + entryIndex;
	vars.bufSize = pMergeBufHandles->size;
	vars.loopBuf.pBuf = pMergeBufHandles->pLoopBuf;
	vars.mapLoopBuf.pBuf = pMergeBufHandles->pMapLoopBuf;
	vars.pIndexTable = pMergeBufHandles->pIndexTable;
	vars.pSortedVertBuf = pMergeBufHandles->pSortedVerts;
	vars.pInFaces = pInFaces;
	vars.entryCount = entryCount;
	if (!vars.pPieceRoot->pEntry) {
		return;
	}
	vars.ruvmFace = *pRuvmFace;
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[2] += CLOCK_TIME_DIFF(start, stop);
	CLOCK_START;
	if (addLoopsToBufAndVertsToMesh(&vars)) {
		return;
	}
	if (vars.loopBuf.count <= 2) {
		return;
	}
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[4] += CLOCK_TIME_DIFF(start, stop);
	CLOCK_START;
	if (vars.pPieceRoot->triangulate) {
		FaceRange tempFace = {0};
		tempFace.end = tempFace.size = vars.loopBuf.count;
		for (int32_t i = 0; i < vars.loopBuf.count; ++i) {
			int32_t vertIndex = vars.loopBuf.pBuf[vars.pIndexTable[i]].loop;
			vars.pSortedVertBuf[i] = vertIndex;
			RUVM_ASSERT("", i >= 0 && i < vars.loopBuf.count);
		}
		FaceTriangulated tris = {0};
		tris = triangulateFace(pArgs->pContext->alloc, tempFace, pArgs->pMeshOut->pVerts,
		                       vars.pSortedVertBuf, false);
		for (int32_t i = 0; i < tris.triCount; ++i) {
			addFaceToOutMesh(&vars, tris.pLoops + (i * 3), 3,
			                 vars.pIndexTable, pInFaces);
			RUVM_ASSERT("", i >= 0 && i < tris.triCount);
		}
		pArgs->pContext->alloc.pFree(tris.pLoops);
	}
	else {
		//RUVM_ASSERT("", vars.loopBuf.count <= 16);
		int32_t *pIndices =
			pArgs->pContext->alloc.pMalloc(sizeof(int32_t) * vars.loopBuf.count);
		for (int32_t i = 0; i < vars.loopBuf.count; ++i) {
			pIndices[i] = i;
		}
		addFaceToOutMesh(&vars, pIndices, vars.loopBuf.count,
		                 vars.pIndexTable, pInFaces);
		pArgs->pContext->alloc.pFree(pIndices);
	}
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[5] += CLOCK_TIME_DIFF(start, stop);
}
