#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <CombineJobMeshes.h>
#include <RUVM.h>
#include <MapFile.h>
#include <Mesh.h>
#include <Context.h>
#include <Clock.h>
#include <MathUtils.h>
#include <Utils.h>
#include <AttribUtils.h>
#include <ThreadPool.h>
#include <Error.h>

typedef struct SharedEdge {
	struct SharedEdge *pNext;
	void *pLast;
	int32_t entries[2];
	int32_t refIndex[2];
	int32_t edge;
	int32_t validIdx;
	int16_t loop[2];
	int8_t receive;
	bool checked : 1;
	bool preserve : 1;
	bool index : 1;
	bool altIndex : 1;
	bool seam : 1;
	bool removed : 1;
} SharedEdge;

typedef struct {
	SharedEdge *pEntry;
} SharedEdgeWrap;

typedef struct PreserveVert {
	struct PreserveVert *pNext;
	int32_t edge;
	int32_t vert;
	int8_t preserve;
} PreserveVert;

static
bool determineIfSeamFace(RuvmMap pMap, Piece *pPiece) {
	if (pPiece->hasSeam) {
		if (!pPiece->triangulate) {
			pPiece->triangulate = true;
		}
		return true;
	}
	int32_t faceIndex = pPiece->pEntry->faceIndex;
	int32_t ruvmLoops = 0;
	do {
		for (int32_t i = 0; i < 11; ++i) {
			ruvmLoops += getIfRuvm(pPiece->pEntry, i);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	FaceRange face = getFaceRange(&pMap->mesh.mesh, faceIndex, false);
	return ruvmLoops < face.size;
}

static
void addLoopsWithSingleVert(MergeSendOffArgs *pArgs, PieceArr *pPieceArr,
                            int32_t tableSize, BorderVert *localEdgeTable) {
	RUVM_ASSERT("", tableSize >= 0 && tableSize < 10000);
	for (int32_t i = 0; i < tableSize; ++i) {
		BorderVert *pEdgeEntry = localEdgeTable + i;
		int32_t depth = 0;
		do {
			if (pEdgeEntry->loops == 1) {
				Piece *pPiece = pPieceArr->pArr + pEdgeEntry->entryIndex;
				//TODO replace with better asserts
				RUVM_ASSERT("", pEdgeEntry->loop >= 0);
				RUVM_ASSERT("", pEdgeEntry->entryIndex >= 0);
				pPiece->keepSingle |= 1 << pEdgeEntry->loop;
			}
			BorderVert *pNextEdgeEntry = pEdgeEntry->pNext;
			if (depth > 0) {
				pArgs->pContext->alloc.pFree(pEdgeEntry);
			}
			depth++;
			pEdgeEntry = pNextEdgeEntry;
		} while(pEdgeEntry);
		RUVM_ASSERT("", i >= 0 && i < tableSize);
	}
}

static
void initLocalEdgeTableEntry(BorderVert *pEdgeEntry, Piece *pPiece,
                             int32_t ruvmEdge, BorderInInfo *pInInfo,
							 int32_t loop, int32_t faceStart, V2_I32 tileMin) {
	pEdgeEntry->ruvmEdge = ruvmEdge;
	pEdgeEntry->tile = tileMin;
	pEdgeEntry->baseEdge = pInInfo->edge;
	pEdgeEntry->baseVert = pInInfo->vert;
	pEdgeEntry->loops = 1;
	pEdgeEntry->loop = loop;
	pEdgeEntry->loopIndex = faceStart - loop;
	pEdgeEntry->job = pPiece->pEntry->job;
	pEdgeEntry->entryIndex = pPiece->entryIndex;
}

static
void addLoopToLocalEdgeTable(MergeSendOffArgs *pArgs, FaceRange *pRuvmFace, int32_t tableSize,
                             BorderVert *localEdgeTable, BorderFace *pEntry,
							 int32_t faceStart, int32_t k, Piece *pPiece) {
	V2_I32 tileMin = getTileMinFromBoundsEntry(pEntry);
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, k);
	Mesh *pInMesh = &pArgs->pJobArgs[pEntry->job].mesh;
	_Bool isOnInVert = getIfOnInVert(pEntry, k);
	int32_t mapEdge = -1;
	if (!isOnInVert) {
		inInfo.vert = -1;
		int32_t mapLoop = getMapLoop(pEntry, pArgs->pMap, k);
		mapEdge = pArgs->pMap->mesh.mesh.pEdges[pRuvmFace->start + mapLoop];
		RUVM_ASSERT("", mapEdge >= 0 && mapEdge < pArgs->pMap->mesh.mesh.edgeCount);
	}
	int32_t indexToHash = isOnInVert ? inInfo.vert : mapEdge;
	int32_t hash = ruvmFnvHash((uint8_t *)&indexToHash, 4, tableSize);
	BorderVert *pEdgeEntry = localEdgeTable + hash;
	if (!pEdgeEntry->loops) {
		initLocalEdgeTableEntry(pEdgeEntry, pPiece, mapEdge, &inInfo,
		                        k, faceStart, tileMin);
	}
	else {
		do {
			//Check entry is valid
			RUVM_ASSERT("", pEdgeEntry->baseVert >= -1);
			RUVM_ASSERT("", pEdgeEntry->baseVert < pInMesh->mesh.vertCount);
			int32_t	match;
			if (isOnInVert) {
				match = pEdgeEntry->baseVert == inInfo.vert;
			}
			else {
				match =  pEdgeEntry->ruvmEdge == mapEdge &&
						 pEdgeEntry->tile.d[0] == tileMin.d[0] &&
						 pEdgeEntry->tile.d[1] == tileMin.d[1] &&
						 pEdgeEntry->baseEdge == inInfo.edge;
			}
			if (match) {
				RUVM_ASSERT("", pEdgeEntry->loops > 0); //Check entry is valid
				pEdgeEntry->loops++;
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry = pEdgeEntry->pNext =
					pArgs->pContext->alloc.pCalloc(1, sizeof(BorderVert));
					initLocalEdgeTableEntry(pEdgeEntry, pPiece, mapEdge, &inInfo,
					                        k, faceStart, tileMin);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
}

static
void determineLoopsToKeep(MergeSendOffArgs *pArgs, PieceArr *pPieceArr,
                          FaceRange *pRuvmFace, Piece *pPieceRoot,
                          int32_t aproxVertsPerPiece) {
	int32_t tableSize = aproxVertsPerPiece;
	RUVM_ASSERT("", tableSize >= 0 && tableSize < 10000);
	BorderVert *pLocalEdgeTable =
		pArgs->pContext->alloc.pCalloc(tableSize, sizeof(BorderVert));
	Piece *pPiece = pPieceRoot;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
		FaceRange face = pPiece->bufFace;
		for (int32_t k = 0; k < face.size; ++k) {
			int32_t vert = asMesh(pBufMesh)->mesh.pLoops[face.start - k];
			if (getIfRuvm(pEntry, k)) {
				vert += pArgs->pJobBases[pEntry->job].vertBase;
			}
			else {
				addLoopToLocalEdgeTable(pArgs, pRuvmFace, tableSize, pLocalEdgeTable,
				                        pEntry, face.start,
										k, pPiece);
			}
			RUVM_ASSERT("", k >= 0 && k < face.size);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	addLoopsWithSingleVert(pArgs, pPieceArr, tableSize, pLocalEdgeTable);
	pArgs->pContext->alloc.pFree(pLocalEdgeTable);
}

static
void initSharedEdgeEntry(SharedEdge *pEntry, int32_t baseEdge, int32_t entryIndex,
                         int32_t refIndex, _Bool isPreserve, _Bool isReceive,
						 Piece *pPiece, int32_t i, _Bool seam) {
	RUVM_ASSERT("", baseEdge >= 0 && entryIndex >= 0 && i >= 0);
	RUVM_ASSERT("", isPreserve % 2 == isPreserve); //range 0 .. 1
	RUVM_ASSERT("", isReceive % 2 == isReceive);
	//TODO do you need to add one to baseEdge?
	pEntry->edge = baseEdge + 1;
	pEntry->entries[0] = entryIndex;
	pEntry->refIndex[0] = refIndex;
	pEntry->receive = isReceive;
	pEntry->preserve = isPreserve;
	pEntry->seam = seam;
	pEntry->loop[0] = i;
	pEntry->validIdx = -1;
	if (refIndex >= 0 && isReceive) {
		pPiece->keepPreserve |= 1 << i;
	}
}

static
void addEntryToSharedEdgeTable(MergeSendOffArgs *pArgs, BorderFace *pEntry,
                               SharedEdgeWrap *pSharedEdges, Piece *pEntries,
							   int32_t tableSize, int32_t entryIndex,
							   int32_t *pTotalVerts, bool *pHasPreserve) {
	//CLOCK_INIT;
	RUVM_ASSERT("", (tableSize > 0 && entryIndex >= 0) || entryIndex == 0);
	Piece *pPiece = pEntries + entryIndex;
	BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
	FaceRange face = getFaceRange(&asMesh(pBufMesh)->mesh, pEntry->face, true);
	pPiece->bufFace = face;
	for (int32_t i = 0; i < face.size; ++i) {
		RUVM_ASSERT("", pTotalVerts && *pTotalVerts >= 0 && *pTotalVerts < 10000);
		++*pTotalVerts;
		//CLOCK_START;
		//int32_t vert = pBufMesh->mesh.pLoops[face.start - i];
		bool isRuvm = getIfRuvm(pEntry, i);
		bool isOnLine = getIfOnLine(pEntry, i);
		if (isRuvm && !isOnLine) {
			//ruvm loop - skip
			continue;
		}
		//CLOCK_STOP_NO_PRINT;
		//pTimeSpent[1] += //CLOCK_TIME_DIFF(start, stop);
		bool isOnInVert = getIfOnInVert(pEntry, i);
		//Get in mesh details for current buf loop
		BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, i);
		int32_t lasti = i ? i - 1 : face.size - 1;
		if ((pEntry->baseLoop >> i * 2 & 0x03) ==
			(pEntry->baseLoop >> lasti * 2 & 0x03) &&
			!(getIfRuvm(pEntry, lasti) && !getIfOnLine(pEntry, lasti))) {
			//Edge belongs to last loop, not this one
			continue;
		}
		if (isOnInVert &&
			checkIfVertIsPreserve(&pArgs->pJobArgs[0].mesh, inInfo.vert)) {
			//This does not necessarily mean this vert will be kept,
			//only loops encountered in sortLoops func will be kept.
			//ie, only loops on the exterior. Interior loops are skipped.
			pPiece->keepVertPreserve |= true << i;
		}
		int32_t* pVerts = pArgs->pEdgeVerts[inInfo.edge].verts;
		RUVM_ASSERT("", pVerts && (pVerts[0] == inInfo.loop || pVerts[1] == inInfo.loop));
		if (pVerts[1] < 0) {
			//no other vert on edge
			pPiece->hasSeam = true;
			continue;
		}
		bool baseKeep;
		if (isOnInVert) {
			RUVM_ASSERT("", pArgs->pJobArgs[0].pInVertTable[inInfo.vert] >= 0); //pInVertTable is 0 .. 3
			RUVM_ASSERT("", pArgs->pJobArgs[0].pInVertTable[inInfo.vert] <= 3); 
			baseKeep = pArgs->pJobArgs[0].pInVertTable[inInfo.vert] > 2;
			//pPiece->keepOnInVert |= baseKeep << i;
		}
		else {
			//seamVert = pVertSeamTable[inInfo.vert] == 1;
		}
		if (!pSharedEdges) {
			//If shared edges if NULL, then there's only 1 border face entry.
			//So no need for a shared edge table
			RUVM_ASSERT("", entryIndex == 0);
			continue;
		}
		//CLOCK_START;
		bool seam = pArgs->pEdgeSeamTable[inInfo.edge];
		//face is connected
		pEntries[entryIndex].edges[pEntries[entryIndex].edgeCount] = inInfo.edge;
		pEntries[entryIndex].edgeCount++;

		bool isPreserve =
			checkIfEdgeIsPreserve(&pArgs->pJobArgs[0].mesh, inInfo.edge);
		if (isPreserve && !*pHasPreserve) {
			*pHasPreserve = true;
		}
		bool isReceive = false;
		int32_t refIndex = 0; 
		if (seam) {
			refIndex = isOnInVert ? -1 : 1;
		}
		else if (isPreserve) {
			if (isOnInVert) {
				isReceive = true;
				//negate if base loop
				refIndex = (inInfo.vert + 1) * -1;
			}
			else {
				int32_t mapLoop = getMapLoop(pEntry, pArgs->pMap, i);
				RUVM_ASSERT("", pEntry->faceIndex < pArgs->pMap->mesh.mesh.faceCount);
				int32_t ruvmFaceStart = pArgs->pMap->mesh.mesh.pFaces[pEntry->faceIndex];
				RUVM_ASSERT("", ruvmFaceStart < pArgs->pMap->mesh.mesh.loopCount);
				int32_t ruvmEdge = pArgs->pMap->mesh.mesh.pEdges[ruvmFaceStart + mapLoop];
				RUVM_ASSERT("", ruvmEdge < pArgs->pMap->mesh.mesh.edgeCount);
				isReceive = checkIfEdgeIsReceive(&pArgs->pMap->mesh, ruvmEdge);
				refIndex = ruvmEdge;
			}
		}

		//CLOCK_STOP_NO_PRINT;
		//pTimeSpent[2] += //CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;
		int32_t hash = ruvmFnvHash((uint8_t *)&inInfo.edge, 4, tableSize);
		SharedEdgeWrap *pEdgeEntryWrap = pSharedEdges + hash;
		SharedEdge *pEdgeEntry = pEdgeEntryWrap->pEntry;
		if (!pEdgeEntry) {
			pEdgeEntry = pEdgeEntryWrap->pEntry =
				pArgs->pContext->alloc.pCalloc(1, sizeof(SharedEdge));
			pEdgeEntry->pLast = pEdgeEntryWrap;
			initSharedEdgeEntry(pEdgeEntry, inInfo.edge, entryIndex, refIndex,
			                    isPreserve, isReceive, pPiece, i, seam);
			continue;
		}
		do {
			RUVM_ASSERT("", pEdgeEntry->edge - 1 >= 0);
			RUVM_ASSERT("", pEdgeEntry->edge - 1 < pArgs->pJobArgs[0].mesh.mesh.edgeCount);
			RUVM_ASSERT("", pEdgeEntry->index % 2 == pEdgeEntry->index); // range 0 .. 1
			if (pEdgeEntry->edge == inInfo.edge + 1) {
				if (pEdgeEntry->entries[pEdgeEntry->index] != entryIndex) {
					//other side of the edge
					pEdgeEntry->entries[1] = entryIndex;
					pEdgeEntry->loop[1] = i;
					pEdgeEntry->index = 1;
					pEdgeEntry->refIndex[1] = refIndex;
				}
				if (!pEdgeEntry->seam &&
					!pEdgeEntry->altIndex &&
					isPreserve && pEdgeEntry->refIndex[0] != refIndex) {
					pEdgeEntry->receive += isReceive;
					if (refIndex >= 0 && isReceive) {
						//this is done here (as well as in initSharedEdgeEntry),
						//in order to avoid duplicate loops being added later on.
						//Ie, only one loop should be marked keep per vert
						pPiece->keepPreserve |= 1 << i;
					}
					pEdgeEntry->altIndex = 1;
				}
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry->pNext =
					pArgs->pContext->alloc.pCalloc(1, sizeof(SharedEdge));
				pEdgeEntry->pNext->pLast = pEdgeEntry;
				pEdgeEntry = pEdgeEntry->pNext;
				initSharedEdgeEntry(pEdgeEntry, inInfo.edge, entryIndex, refIndex,
				                    isPreserve, isReceive, pPiece, i, seam);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
		RUVM_ASSERT("", i >= 0 && i < face.size);
	}
	pEntries[entryIndex].pEntry = pEntry;
	pEntries[entryIndex].entryIndex = entryIndex;
	//CLOCK_STOP_NO_PRINT;
	//pTimeSpent[3] += //CLOCK_TIME_DIFF(start, stop);
}

static
Piece *getEntryInPiece(Piece *pPieceRoot, int32_t otherPiece) {
	RUVM_ASSERT("", pPieceRoot);
	Piece* pPiece = pPieceRoot;
	do {
		if (pPiece->entryIndex == otherPiece) {
			return pPiece;
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	return NULL;
}

static
Piece *getNeighbourEntry(MergeSendOffArgs *pArgs, SharedEdgeWrap *pEdgeTable,
                         int32_t edgeTableSize, Piece *pPiece, Piece *pPieceRoot,
                         int32_t *pLoop, SharedEdge **ppEdge) {
	BorderInInfo inInfo = getBorderEntryInInfo(pPiece->pEntry, pArgs->pJobArgs, *pLoop);
	int32_t hash = ruvmFnvHash((uint8_t*)&inInfo.edge, 4, edgeTableSize);
	SharedEdgeWrap* pEdgeEntryWrap = pEdgeTable + hash;
	SharedEdge* pEdgeEntry = pEdgeEntryWrap->pEntry;
	while (pEdgeEntry) {
		if (pEdgeEntry->index) {
			bool loopMatches = *pLoop == pEdgeEntry->loop[0] &&
							   pPiece->entryIndex == pEdgeEntry->entries[0] ||
							   *pLoop == pEdgeEntry->loop[1] &&
							   pPiece->entryIndex == pEdgeEntry->entries[1];
			if (loopMatches && inInfo.edge + 1 == pEdgeEntry->edge) {
				bool which = pEdgeEntry->entries[1] == pPiece->entryIndex;
				int32_t otherPiece = pEdgeEntry->entries[!which];
				Piece *pNeighbour = getEntryInPiece(pPieceRoot, otherPiece);
				if (pNeighbour) {
					*pLoop = (pEdgeEntry->loop[!which] + 1) % pNeighbour->bufFace.size;
				}
				if (ppEdge) {
					*ppEdge = pEdgeEntry;
				}
				return pNeighbour;
			}
		}
		pEdgeEntry = pEdgeEntry->pNext;
	}
	return NULL;
}

static
bool isLoopOnExterior(MergeSendOffArgs *pArgs, Piece *pPiece,
                      Piece *pPieceRoot, SharedEdgeWrap *pEdgeTable,
                      int32_t edgeTableSize, int32_t loop) {
	if (!getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
			               pPiece, pPieceRoot, &loop, NULL)) {
		//loop does not shared edge without any other loop,
		//must be on outside
		return true;
	}
	return false;
}

static
int32_t getStartingLoop(Piece **ppPiece, MergeSendOffArgs *pArgs,
                        Piece *pPieceRoot, SharedEdgeWrap *pEdgeTable,
                        int32_t edgeTableSize) {
	do {
		for (int32_t i = 0; i < (*ppPiece)->bufFace.size; ++i) {
			if (isLoopOnExterior(pArgs, *ppPiece, pPieceRoot, pEdgeTable,
			                     edgeTableSize, i)) {
				return i;
			}
		}
		*ppPiece = (*ppPiece)->pNext;
	} while (*ppPiece);
	return -1;
}

typedef struct {
	Piece *pPiece;
	Piece *pStartPiece;
	SharedEdge *pActiveEdge;
	SharedEdge *pQuadEdge;
	int32_t loop;
	int32_t startLoop;
	int32_t validBranches;
	bool quad;
} EdgeStack;

static
bool checkIfIntersectsReceive(EdgeStack *pItem, Mesh *pBufMesh, RuvmMap pMap,
                              FaceRange *pMapFace, int32_t *pMapLoop, bool side) {
	Mesh *pMapMesh = &pMap->mesh;
	int32_t loop;
	if (side) {
		loop = (pItem->loop + 1) % pItem->pPiece->bufFace.size;
	}
	else {
		loop = pItem->loop == 0 ?
			pItem->pPiece->bufFace.size - 1 : pItem->loop - 1;
	}
	bool isOnInVert = getIfOnInVert(pItem->pPiece->pEntry, loop);
	if (!isOnInVert) {
		//exterior intersects with a map edge,
		//so just check if said edge is receive
		*pMapLoop = getMapLoop(pItem->pPiece->pEntry, pMap, loop);
		RUVM_ASSERT("", *pMapLoop >= 0 && *pMapLoop < pMapFace->size);
		*pMapLoop += pMapFace->start;
		int32_t mapEdge = pMapMesh->mesh.pEdges[*pMapLoop];
		return pMapMesh->pEdgeReceive[mapEdge];
	}
	//exterior does not intersect with a map edge.
	//In this case, we perform an intersect test,
	//and use that to see if the base edge would intersect
	//with a preserve edge, were it to extend out infinitely
	V2_F32 *pUvStart = pBufMesh->pUvs + pItem->pPiece->bufFace.start;
	V2_F32 c = pUvStart[-pItem->loop];
	int32_t loopNext = (pItem->loop + 1) % pItem->pPiece->bufFace.size;
	V2_F32 d = pUvStart[-loopNext];
	V2_F32 cd = _(d V2SUB c);
	for (int32_t i = 0; i < pMapFace->size; ++i) {
		*pMapLoop = pMapFace->start + i;
		int32_t mapEdge = pMapMesh->mesh.pEdges[*pMapLoop];
		if (!pMapMesh->pEdgeReceive[mapEdge]) {
			continue;
		}
		float t = .0f;
		int32_t iNext = (i + 1) % pMapFace->size;
		int32_t mapVert = pMapMesh->mesh.pLoops[*pMapLoop];
		int32_t mapLoopNext = pMapFace->start + iNext;
		int32_t mapVertNext = pMapMesh->mesh.pLoops[mapLoopNext];
		calcIntersection(pMapMesh->pVerts[mapVert],
					        pMapMesh->pVerts[mapVertNext], c, cd,
					        NULL, &t);
		//do you need to handle .0 or 1.0 as distinct cases?
		//ie, should you track preserve verts hit?
		if (t < .0f || t > 1.0f) {
			continue;
		}
		return true;
	}
}

static
void pushToEdgeStack(EdgeStack *pStack, int32_t *pStackPtr, int32_t treeCount,
                     SharedEdge *pEdge, Piece *pPiece, int32_t loop) {
	pStack[*pStackPtr].pActiveEdge = pEdge;
	pEdge->validIdx = treeCount;
	++*pStackPtr;
	EdgeStack next = {.pPiece = pPiece, .loop = loop};
	pStack[*pStackPtr] = next;
}

static
bool handleExterior(EdgeStack *pStack, int32_t *pStackPtr, 
                    EdgeStack *pNeighbour, bool *pValid,
                    int32_t treeCount, int32_t *pReceive,
                    MergeSendOffArgs *pArgs, FaceRange *pMapFace,
                    SharedEdge *pEdge, bool *pUnwind, bool side) {
	EdgeStack *pItem = pStack + *pStackPtr;
	Mesh *pMapMesh = &pArgs->pMap->mesh;
	Mesh *pBufMesh = &pArgs->pJobArgs[pItem->pPiece->pEntry->job].bufMesh;
	int32_t mapLoop = -1;
	bool isReceive =
		checkIfIntersectsReceive(pItem, pBufMesh, pArgs->pMap,
		                         pMapFace, &mapLoop, side);
	if (isReceive) {
		//preserve edge intersects receive edge. Add to count
		RUVM_ASSERT("", mapLoop >= 0 && mapLoop < pMapMesh->mesh.loopCount);
		RUVM_ASSERT("", *pReceive >= -1 &&
					    *pReceive < pMapMesh->mesh.loopCount);
		if (*pReceive == -1) {
			//start of new preserve tree
			*pReceive = mapLoop;
		}
		else if (!pValid[treeCount] && mapLoop != *pReceive) {
			pValid[treeCount] = true;
		}
		if (*pUnwind) {
			*pUnwind = false;
		}
		pEdge->validIdx = treeCount;
		pItem->validBranches++;
	}
	else if (!pItem->validBranches) {
		*pUnwind = true;
	}
}

static
SharedEdge *getIfQuadJunc(MergeSendOffArgs *pArgs, SharedEdgeWrap *pEdgeTable,
                          int32_t edgeTableSize, Piece *pPieceRoot,
                          EdgeStack *pItem, EdgeStack *pNeighbour) {
	EdgeStack copy = *pItem;
	int32_t index = -1;
	SharedEdge *cache[4] = {0};
	EdgeStack retNeighbour = {0};
	do {
		copy.loop %= copy.pPiece->bufFace.size;
		if (copy.pStartPiece == copy.pPiece &&
			copy.startLoop == copy.loop) {
			if (index == 3) {
				*pNeighbour = retNeighbour;
				return cache[1];
			}
		}
		else if (index >= 3) {
			break;
		}
		index++;
		SharedEdge *pEdge = NULL;
		//Set next loop
		EdgeStack neighbour = {.loop = copy.loop};
		neighbour.pPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
		                                     copy.pPiece, pPieceRoot,
		                                     &neighbour.loop, cache + index);
		if (!neighbour.pPiece) {
			break;
		}
		if (!index) {
			retNeighbour = neighbour;
		}
		copy.pPiece = neighbour.pPiece;
		copy.loop = neighbour.loop;
	} while(true);
	return NULL;
}

static
void walkEdgesForPreserve(EdgeStack *pStack, int32_t *pStackPtr, bool *pValid,
                          int32_t treeCount, int32_t *pReceive, 
                          MergeSendOffArgs *pArgs, SharedEdge *pEdgeTable,
                          int32_t edgeTableSize, Piece *pPieceRoot,
                          FaceRange *pMapFace, bool *pUnwind) {
	EdgeStack *pItem = pStack + *pStackPtr;
	//if pItem->pStartPiece is NULL, then this is the first time
	//this func is being called on this item
	if (*pUnwind) {
		if (pItem->pActiveEdge) {
			pItem->pActiveEdge->validIdx = 0;
		}
		if (pItem->validBranches || !*pStackPtr) {
			*pUnwind = false;
		}
	}
	else if (pItem->pStartPiece) {
		//branch has returned, and is valid
		pItem->validBranches++;
	}

	bool ret = false;
	do {
		SharedEdge *pEdge = NULL;
		EdgeStack neighbour = {0};
		pItem->loop %= pItem->pPiece->bufFace.size;
		if (!pItem->pStartPiece) {
			pItem->pStartPiece = pItem->pPiece;
			pItem->startLoop = pItem->loop;
			if (*pStackPtr) {
				pEdge = 
					getIfQuadJunc(pArgs, pEdgeTable, edgeTableSize,
								  pPieceRoot, pItem, &neighbour);
				if (pEdge) {
					pItem->quad = true;
				}
			}
		}
		else if (pItem->quad ||
		         (pItem->pStartPiece == pItem->pPiece &&
		          pItem->startLoop == pItem->loop)) {
			--*pStackPtr;
			return;
		}
		//Set next loop
		if (!*pStackPtr &&
		    getIfRuvm(pItem->pPiece->pEntry, pItem->loop) &&
			!getIfOnLine(pItem->pPiece->pEntry, pItem->loop)) {

			RUVM_ASSERT("", !*pStackPtr);
			pItem->loop++;
			continue;
		}
		if (!pItem->quad) {
			neighbour.loop = pItem->loop;
			neighbour.pPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
												 pItem->pPiece, pPieceRoot,
												 &neighbour.loop, &pEdge);
			if (!*pStackPtr && !neighbour.pPiece) {
				pItem->loop++;
				continue;
			}
		}
		//if validIdx isn't -1, this edge has already been checked
		if (pEdge->preserve && pEdge->validIdx == -1) {
			if (!*pStackPtr) {
				bool ret = 
					handleExterior(pStack, pStackPtr, &neighbour, pValid, treeCount, pReceive,
				                   pArgs, pMapFace, pEdge, pUnwind, false);
			}
			Piece *pPieceNext = pItem->quad ? neighbour.pPiece : pItem->pPiece;
			int32_t loopNext = pItem->quad ? neighbour.loop : pItem->loop;
			loopNext = (loopNext + 1) % pPieceNext->bufFace.size;
			bool exterior = true;
			if (*pReceive != -1) {
				exterior = isLoopOnExterior(pArgs, pPieceNext, pPieceRoot,
				                            pEdgeTable, edgeTableSize, loopNext);
			}
			if (exterior) {
				handleExterior(pStack, pStackPtr, &neighbour, pValid, treeCount, pReceive,
				               pArgs, pMapFace, pEdge, pUnwind, true);
				if (!*pStackPtr || *pUnwind || pItem->quad) {
					ret = true;
				}
			}
			else {
				pushToEdgeStack(pStack, pStackPtr, treeCount, pEdge, pPieceNext,
				                loopNext);
				if (*pUnwind) {
					*pUnwind = false;
				}
				ret = true;
			}
		}
		if (!pItem->quad) {
			pItem->pPiece = neighbour.pPiece;
			pItem->loop = neighbour.loop;
		}
		if (ret) {
			return;
		}
	} while(1);
}

//TODO Make a in this function for in verts,
//     list in each entry whether to keep
//     (due to bordering a preserve edge)
//     create the table in the stack walk, then
//     set preserve in the later validate loop
static
void validatePreserveEdges(MergeSendOffArgs* pArgs,PieceArr *pPieceArr,
                           int32_t piece, SharedEdgeWrap* pEdgeTable,
                           int32_t edgeTableSize, bool **ppValid, int32_t *pValidCount) {
	//TODO first , check if the map face even has any preserve edges,
	//     no point doing all this if not
	RuvmAlloc *pAlloc = &pArgs->pContext->alloc;
	Mesh *pInMesh = pArgs->pInMesh;
	Mesh *pMapMesh = &pArgs->pMap->mesh;
	// Get first not exterior loop
	// This is done to ensure we don't start inside the face
	Piece *pPieceRoot = pPieceArr->pArr;
	Piece *pPiece = pPieceArr->pArr + piece;
	FaceRange mapFace = getFaceRange(&pArgs->pMap->mesh.mesh,
	                                 pPieceRoot->pEntry->faceIndex, false);
	int32_t validSize = 8;
	*ppValid = pAlloc->pCalloc(validSize, sizeof(bool));
	int32_t stackSize = 8;
	EdgeStack *pStack = pAlloc->pCalloc(stackSize, sizeof(EdgeStack));
	pStack[0].pPiece = pPiece;
	int32_t stackPtr = 0;
	pStack[0].loop = getStartingLoop(&pStack[0].pPiece, pArgs, pPiece,
	                                 pEdgeTable, edgeTableSize);
	bool unwind = false;
	//note that order is used here to determine if a loop has already been checked.
	//Sorting is not done until later, and order is cleared at the end of this func.
	int32_t receive = -1;
	do {
		walkEdgesForPreserve(pStack, &stackPtr, *ppValid, *pValidCount, &receive,
		                     pArgs, pEdgeTable, edgeTableSize, pPieceRoot,
		                     &mapFace, &unwind);
		RUVM_ASSERT("", stackPtr < stackSize);
		if (stackPtr == stackSize - 1) {
			int32_t oldSize = stackSize;
			stackSize *= 2;
			pStack = pAlloc->pRealloc(pStack, sizeof(EdgeStack) * stackSize);
		}
		else if (!stackPtr) {
			//reset for next preserve tree
			receive = -1;
			++*pValidCount;
			RUVM_ASSERT("", *pValidCount <= validSize);
			if (*pValidCount == validSize) {
				int32_t oldSize = validSize;
				validSize *= 2;
				*ppValid = pAlloc->pRealloc(*ppValid, sizeof(bool) * validSize);
				memset(*ppValid + oldSize, 0, sizeof(bool) * oldSize);
			}
		}
	} while(stackPtr >= 0);
	++*pValidCount;
	pAlloc->pFree(pStack);

	//set order back to zero
	do {
		memset(pPiece->order, 0, 11);
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void setValidPreserveAsSeam(SharedEdgeWrap* pEdgeTable, int32_t edgeTableSize,
                            bool *pValid, int32_t validCount) {
	//mark valid preserve edges as seams
	for (int32_t i = 0; i < edgeTableSize; ++i) {
		SharedEdge *pEntry = pEdgeTable[i].pEntry;
		while (pEntry) {
			if (pEntry->checked || !pEntry->preserve) {
				pEntry = pEntry->pNext;
				continue;
			}
			int32_t validIdx = pEntry->validIdx;
			RUVM_ASSERT("", validIdx >= -1 && validIdx < validCount);
			if (validIdx == -1 || !pValid[validIdx]) {
				//edge intersects 1 or no map receive edges or base verts
				//on preserve junctions, so keep it in the list
				pEntry->checked = true;
				pEntry = pEntry->pNext;
				continue;
			}
			//intersects 2 or more receive, so mark as a seam
			pEntry->seam = true;
			pEntry = pEntry->pNext;
		}
	}
}

static
bool areNonListedPiecesLinked(PieceArr *pPieceArr) {
	for (int32_t i = 0; i < pPieceArr->count; ++i) {
		Piece *pPiece = pPieceArr->pArr + i;
		if (!pPiece->listed && (pPiece->pNext || pPiece->pEntry->pNext)) {
			return true;
		}
	}
	return false;
}

static
void combineConnectedIntoPiece(PieceArr *pPieceArr, SharedEdgeWrap *pSharedEdges,
                               int32_t tableSize, int32_t i) {
	Piece *pPiece = pPieceArr->pArr + i;
	Piece* pPieceRoot = pPiece;
	Piece *pPieceTail = pPiece;
	BorderFace *pTail = pPiece->pEntry;
	pPiece->listed = true;
	int32_t depth = 0;
	do {
		RUVM_ASSERT("", pPiece->edgeCount <= 11);
		for (int32_t j = 0; j < pPiece->edgeCount; ++j) {
			int32_t edge = pPiece->edges[j];
			int32_t hash = ruvmFnvHash((uint8_t *)&edge, 4, tableSize);
			SharedEdgeWrap *pSharedEdgeWrap = pSharedEdges + hash;
			SharedEdge *pEdgeEntry = pSharedEdgeWrap->pEntry;
			while (pEdgeEntry) {
				if (pEdgeEntry->seam) {
					_Bool aIsOnInVert = pEdgeEntry->refIndex[0] < 0;
					_Bool bIsOnInVert = pEdgeEntry->refIndex[1] < 0;
					if (aIsOnInVert ^ bIsOnInVert) {
						_Bool whichLoop = aIsOnInVert;

						int32_t loopA = pEdgeEntry->loop[whichLoop];
						Piece *pPieceA = pPieceArr->pArr + pEdgeEntry->entries[whichLoop];
						pPieceA->keepSeam |= 1 << loopA;

						int32_t loopB = pEdgeEntry->loop[!whichLoop];
						Piece *pPieceB = pPieceArr->pArr + pEdgeEntry->entries[!whichLoop];
						//Also set adjacent loop
						int32_t adjLoop = (loopB + 1) % pPieceB->bufFace.size;
						pPieceB->keepSeam |= 1 << adjLoop;
					}
				}
				else if (pEdgeEntry->edge - 1 == pPiece->edges[j]) {
					RUVM_ASSERT("", pEdgeEntry->entries[0] == pPiece->entryIndex ||
					       pEdgeEntry->entries[1] == pPiece->entryIndex);
					int32_t whichEntry =
						pEdgeEntry->entries[0] == pPiece->entryIndex;
					int32_t otherEntryIndex = pEdgeEntry->entries[whichEntry];
					if (pPieceArr->pArr[otherEntryIndex].listed) {
						break;
					}
					if (!pPieceRoot->hasSeam &&
						pPieceArr->pArr[otherEntryIndex].hasSeam) {

						pPieceRoot->hasSeam = true;
					}
					//add entry to linked list
					pTail->pNext = pPieceArr->pArr[otherEntryIndex].pEntry;
					pTail = pTail->pNext;
					//add piece to piece linked list
					pPieceTail->pNext = pPieceArr->pArr + otherEntryIndex;
					pPieceTail = pPieceTail->pNext;
					pPieceArr->pArr[otherEntryIndex].listed = 1;
					break;
				}
				pEdgeEntry = pEdgeEntry->pNext;
			};
			RUVM_ASSERT("", j < pPiece->edgeCount);
		}
		depth++;
		if (depth > pPieceArr->count) {
			//an infinite loop can occur if pNext are not NULL prior to this func
			//this is checked for, so it shouldn't occur
			RUVM_ASSERT("Piece list has likely linked in a loop", false);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static 
bool isEntryInPiece(Piece *pPiece, int32_t entryIndex) {
	RUVM_ASSERT("", pPiece);
	do {
		if (pPiece->entryIndex == entryIndex) {
			return true;
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	return false;
}

static
void flipSharedEdgeEntry(SharedEdge *pEntry) {
	int32_t entryBuf = pEntry->entries[0];
	int32_t loopBuf = pEntry->loop[0];
	int32_t refIndexBuf = pEntry->refIndex[0];
	pEntry->entries[0] = pEntry->entries[1];
	pEntry->entries[1] = entryBuf;
	pEntry->loop[0] = pEntry->loop[1];
	pEntry->loop[1] = loopBuf;
	pEntry->refIndex[0] = pEntry->refIndex[1];
	pEntry->refIndex[1] = refIndexBuf;
}

static
void breakPieceLinks(PieceArr *pPieceArr) {
	for (int32_t i = 0; i < pPieceArr->count; ++i) {
		pPieceArr->pArr[i].pNext = NULL;
		pPieceArr->pArr[i].pEntry->pNext = NULL;
	}
}

static
void linkConnectedPieces(MergeSendOffArgs *pArgs, bool hasPreserve,
                         PieceRootsArr *pPieceRoots, PieceArr *pPieceArr,
                         SharedEdgeWrap *pEdgeTable, int32_t edgeTableSize) {
	//A first pass separates pieces by connectivity only, then validates preserve edges.
	//Once that's done, a second pass is done with preserve edges
	bool *pValid = NULL;
	int32_t validCount = 1; //first is reserved and is always false
	int32_t i = 0;
	do {
		if (areNonListedPiecesLinked(pPieceArr)) {
			RUVM_ASSERT("Linked pieces here will cause an infinite loop", false);
			return;
		}
		for (int32_t j = 0; j < pPieceArr->count; ++j) {
			if (pPieceArr->pArr[j].listed) {
				continue;
			}
			combineConnectedIntoPiece(pPieceArr, pEdgeTable, edgeTableSize, j);
			if (pPieceArr->pArr[j].pEntry) {
				pPieceRoots->pArr[pPieceRoots->count] = j;
				if (hasPreserve && !i) {
					//check if preserve inMesh edge intersects at least 2
					//map receiver edges. Edge is only preserved if this is so.
					validatePreserveEdges(pArgs, pPieceArr, pPieceRoots->count,
					                      pEdgeTable, edgeTableSize, &pValid, &validCount);
				}
				++pPieceRoots->count;
			}
			RUVM_ASSERT("", !pPieceRoots->count ||
							pPieceArr->pArr[pPieceRoots->count - 1].pEntry);
			RUVM_ASSERT("", j >= 0 && j < pPieceArr->count);
		}
		RUVM_ASSERT("", i >= 0 && i < 2);
		if (hasPreserve && !i) {
			setValidPreserveAsSeam(pEdgeTable, edgeTableSize, pValid, validCount);
			pArgs->pContext->alloc.pFree(pValid);
			for (int32_t j = 0; j < pPieceArr->count; ++j) {
				pPieceArr->pArr[j].listed = false;
			}
			pPieceRoots->count = 0;
			breakPieceLinks(pPieceArr);
			i++;
		}
		else {
			break;
		}
	} while(true);
}

static
void splitIntoPieces(MergeSendOffArgs *pArgs, PieceRootsArr *pPieceRoots,
                     BorderFace *pEntry, SharedEdgeWrap **ppSharedEdges,
					 int32_t *pEdgeTableSize, PieceArr *pPieceArr,
                     int32_t *pTotalVerts) {
	//CLOCK_INIT;
	//CLOCK_START;
	*pEdgeTableSize = 0;
	int32_t entryCount = pPieceArr->count;
	if (entryCount > 1) {
		*pEdgeTableSize = entryCount;
		*ppSharedEdges =
			pArgs->pContext->alloc.pCalloc(*pEdgeTableSize, sizeof(SharedEdgeWrap));
	}
	RUVM_ASSERT("", entryCount > 0);
	Piece *pEntries = pArgs->pContext->alloc.pCalloc(entryCount, sizeof(Piece));
	pPieceRoots->pArr = pArgs->pContext->alloc.pMalloc(sizeof(int32_t) * entryCount);
	pPieceArr->pArr = pEntries;
	pPieceArr->count = entryCount;
	int32_t entryIndex = 0;
	bool hasPreserve = false;
	//CLOCK_START;
	do {
		//If there's only 1 border face entry, then this function will just
		//initialize the Piece.
		addEntryToSharedEdgeTable(pArgs, pEntry, *ppSharedEdges, pEntries,
		                          *pEdgeTableSize, entryIndex, pTotalVerts,
		                          &hasPreserve);
		RUVM_ASSERT("", entryIndex < entryCount);
		entryIndex++;
		BorderFace *pNextEntry = pEntry->pNext;
		pEntry->pNext = NULL;
		pEntry = pNextEntry;
	} while(pEntry);
	RUVM_ASSERT("", entryIndex == entryCount);
	
	//CLOCK_STOP_NO_PRINT;
	////pTimeSpent[2] += CLOCK_TIME_DIFF(start, stop);
	//CLOCK_START;
	if (entryCount == 1) {
		pPieceRoots->pArr[0] = 0;
		pPieceRoots->count = 1;
	}
	else {
		//now link together connected entries.
		linkConnectedPieces(pArgs, hasPreserve, pPieceRoots, pPieceArr,
		                    *ppSharedEdges, *pEdgeTableSize);
	}
	for (int32_t i = 0; i < entryCount; ++i) {
		SharedEdgeWrap* pBucket = *ppSharedEdges + i;
		//correctEdgeDir(pArgs, pPieceArr, pBucket);
		RUVM_ASSERT("", i < entryCount);
	}
	RUVM_ASSERT("", pPieceRoots->count >= 0 && pPieceRoots->count < 10000);
}

static
void compileEntryInfo(BorderFace *pEntry, int32_t *pCount, _Bool *pIsSeam,
                      _Bool *pHasPreservedEdge) {
	*pCount = 0;
	*pIsSeam = false;
	*pHasPreservedEdge = false;
	while (pEntry) {
		++*pCount;
		*pIsSeam |= pEntry->seam;
		*pHasPreservedEdge |= pEntry->hasPreservedEdge;
		pEntry = pEntry->pNext;
	}
}

static
void destroySharedEdgeTable(RuvmAlloc *pAlloc,
                            SharedEdgeWrap *pSharedEdges, int32_t tableSize) {
	for (int32_t i = 0; i < tableSize; ++i) {
		SharedEdge* pEdgeEntry = pSharedEdges[i].pEntry;
		while (pEdgeEntry) {
			SharedEdge* pNext = pEdgeEntry->pNext;
			pAlloc->pFree(pEdgeEntry);
			pEdgeEntry = pNext;
		}
		RUVM_ASSERT("", i < tableSize);
	}
	pAlloc->pFree(pSharedEdges);
}

static int32_t checkIfShouldSkip(Piece *pPieceRoot, Piece* pPiece, int32_t k) {
	_Bool skip = true;
	if (pPiece->keepSingle >> k & 1) {
		skip = false;
	}
	//override if keep is set to 1
	if (skip && (pPiece->keepPreserve >> k & 1 ||
	             pPiece->keepOnInVert >> k & 1 ||
	             pPiece->keepSeam >> k & 1)) {
		skip = false;
		if (!pPieceRoot->triangulate) {
			pPieceRoot->triangulate = true;
		}
	}
	return skip;
}

static
bool setDupsToSkip(RuvmMap* pMap, Piece* pPieceRoot, Piece* pPiece, int32_t loop) {
	int32_t mapLoop = getMapLoop(pPiece->pEntry, pMap, loop);
	Piece* pOtherPiece = pPieceRoot;
	do {
		if (pOtherPiece != pPiece) {
			for (int32_t i = 0; i < pOtherPiece->bufFace.size; ++i) {
				if (!getIfRuvm(pOtherPiece->pEntry, i) ||
				    !getIfOnLine(pOtherPiece->pEntry, i)) {
					continue;
				}
				int32_t otherMapLoop = getMapLoop(pOtherPiece->pEntry, pMap, i);
				if (otherMapLoop == mapLoop) {
					pOtherPiece->skip |= 0x01 << i;
					break;
				}
			}
		}
		pOtherPiece = pOtherPiece->pNext;
	} while (pOtherPiece);
}

static
void determineLoopsToSkip(RuvmMap *pMap, Piece* pPiece) {
	Piece* pPieceRoot = pPiece;
	do {
		for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
			if (getIfRuvm(pPiece->pEntry, i)) {
				if (getIfOnLine(pPiece->pEntry, i) &&
					!(pPiece->skip >> i & 0x01)) {
					setDupsToSkip(pMap, pPieceRoot, pPiece, i);
				}
				continue;
			}
			bool skip = checkIfShouldSkip(pPieceRoot, pPiece, i);
			pPiece->skip |= skip << i;
		}
		pPiece = pPiece->pNext;
	} while (pPiece);
}

static
int32_t getPieceCount(Piece* pPiece) {
	int32_t count = 1;
	Piece* pPieceRoot = pPiece->pNext;
	while (pPiece) {
		count++;
		pPiece = pPiece->pNext;
	};
	return count;
}

static
int32_t getFirstLoopNotSkipped(Piece **ppPiece) {
	do {
		for (int32_t i = 0; i < (*ppPiece)->bufFace.size; ++i) {
			if (!((*ppPiece)->skip >> i & 0x01)) {
				return i;
			}
		}
		*ppPiece = (*ppPiece)->pNext;
	} while (*ppPiece);
	return -1;
}

static
void sortLoops(MergeSendOffArgs* pArgs, Piece* pPiece, PieceArr *pPieceArr,
               SharedEdgeWrap* pEdgeTable, int32_t edgeTableSize, int32_t *pCount) {
	if (!pPiece->pNext) {
		//Only one entry, so just use existing order
		for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
			pPiece->order[i] = i + 1;
		}
		return;
	}
	Mesh* pBufMesh = &pArgs->pJobArgs[0].mesh;
	// Get first not skipped loop.
	// This is done to ensure we don't start inside the face
	Piece* pPieceRoot = pPiece;
	int32_t loop = getFirstLoopNotSkipped(&pPiece);
	RUVM_ASSERT("No valid starting loop found", loop >= 0);
	int32_t sort = 1;
	do {
		loop %= pPiece->bufFace.size;
		if (pPiece->order[loop]) {
			//We've done a full loop around
			break;
		}
		bool skip = pPiece->skip >> loop & 0x01;
		if (!skip || (pPiece->keepVertPreserve >> loop & 0x01)) {
			if (skip) {
				//set skip to false
				pPiece->skip ^= 0x01 << loop;
			}
			pPiece->order[loop] = sort;
			sort++;
		}
		else {
			pPiece->order[loop] = 1;
		}
		//Set next loop
		if (getIfRuvm(pPiece->pEntry, loop) &&
			!getIfOnLine(pPiece->pEntry, loop)) {

			loop++;
			continue;
		}
		Piece *pOtherPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
		                                       pPiece, pPieceRoot, &loop, NULL);
		if (!pOtherPiece) {
			loop++;
			continue;
		}
		pPiece = pOtherPiece;
		/*
		if (!(pPiece->skip >> loop & 0x01)) {
			pPiece->order[loop] = sort;
			sort++;
		}
		else {
			pPiece->order[loop] = 1;
		}*/
	} while(1);
	*pCount = sort - 1;
}

static
void getPieceInFaces(RuvmAlloc *pAlloc, int32_t **ppInFaces,
                     Piece *pPiece, int32_t pieceCount) {
	*ppInFaces = pAlloc->pCalloc(pieceCount, sizeof(int32_t));
	int32_t i = 0;
	do {
		(*ppInFaces)[i] = pPiece->pEntry->baseFace;
		pPiece = pPiece->pNext;
		i++;
	} while(pPiece);
}

static
//void mergeAndCopyEdgeFaces(RuvmContext pContext, CombineTables *pCTables,
//                           RuvmMap pMap, Mesh *pMeshOut, SendOffArgs *pJobArgs,
//						   CompiledBorderTable *pBorderTable,
//						   EdgeVerts *pEdgeVerts, JobBases *pJobBases,
//						   int8_t *pVertSeamTable) {
void mergeAndCopyEdgeFaces(void *pArgsVoid) {
	CLOCK_INIT;
	MergeSendOffArgs *pArgs = pArgsVoid;
	RuvmContext pContext = pArgs->pContext;
	uint64_t timeSpent[7] = {0};
	MergeBufHandles mergeBufHandles = {0};
	int32_t count = pArgs->entriesEnd - pArgs->entriesStart;
	pArgs->pPieceArrTable = pContext->alloc.pCalloc(count, sizeof(PieceArr));
	pArgs->pPieceRootTable = pContext->alloc.pCalloc(count, sizeof(PieceRootsArr));
	pArgs->pTotalVertTable = pContext->alloc.pCalloc(count, sizeof(int32_t));
	for (int32_t i = 0; i < count; ++i) {
		int32_t reali = pArgs->entriesStart + i;
		CLOCK_START;
		BorderFace *pEntry = pArgs->pBorderTable->ppTable[reali];
		int32_t entryCount = 0;
		_Bool isSeam;
		_Bool hasPreservedEdge;
		compileEntryInfo(pEntry, &entryCount, &isSeam, &hasPreservedEdge);
		RUVM_ASSERT("", entryCount);
		//int32_t seamFace = ;
		FaceRange ruvmFace =
			getFaceRange(&pArgs->pMap->mesh.mesh, pEntry->faceIndex, false);
		RUVM_ASSERT("", ruvmFace.size <= 6);
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		pPieceRoots->count = 0;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		int32_t *pTotalVerts = pArgs->pTotalVertTable + i;
		*pTotalVerts = 0;
		pPieceArr->count = entryCount;
		pPieceArr->pArr = NULL;
		SharedEdgeWrap *pSharedEdges = NULL;
		int32_t edgeTableSize = 0;
		CLOCK_STOP_NO_PRINT;
		timeSpent[0] += CLOCK_TIME_DIFF(start, stop);
		CLOCK_START;
		splitIntoPieces(pArgs, pPieceRoots, pEntry, &pSharedEdges, &edgeTableSize,
		                pPieceArr, pTotalVerts);
		RUVM_ASSERT("", pPieceRoots->count > 0);
		int32_t aproxVertsPerPiece = *pTotalVerts / pPieceRoots->count;
		RUVM_ASSERT("", aproxVertsPerPiece != 0);
		for (int32_t j = 0; j < pPieceRoots->count; ++j) {
			Piece *pPiece = pPieceArr->pArr + pPieceRoots->pArr[j];
			RUVM_ASSERT("", pPiece->pEntry);
			bool seamFace = determineIfSeamFace(pArgs->pMap, pPiece);
			if (seamFace) {
				determineLoopsToKeep(pArgs, pPieceArr, &ruvmFace, pPiece, aproxVertsPerPiece);
			}
			determineLoopsToSkip(pArgs->pMap, pPiece);
			sortLoops(pArgs, pPiece, pPieceArr, pSharedEdges, edgeTableSize, pTotalVerts);
#ifndef RUVM_DISABLE_TRIANGULATION
			if (pPiece->triangulate && *pTotalVerts <= 4) {
				pPiece->triangulate = false;
			}
#else
			pPiece->triangulate = false;
#endif
		}
		if (pSharedEdges) {
			destroySharedEdgeTable(&pArgs->pContext->alloc, pSharedEdges, edgeTableSize);
		}
		CLOCK_STOP_NO_PRINT;
		timeSpent[1] += CLOCK_TIME_DIFF(start, stop);
		RUVM_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
	void *pThreadPoolHandle = pContext->pThreadPoolHandle;
	RuvmThreadPool *pThreadPool = &pArgs->pContext->threadPool;
	pThreadPool->pMutexLock(pThreadPoolHandle, pArgs->pMutex);
	for (int32_t i = 0; i < count; ++i) {
		int32_t reali = pArgs->entriesStart + i;
		CLOCK_START;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		int32_t totalVerts = pArgs->pTotalVertTable[i];
		FaceRange ruvmFace =
			getFaceRange(&pArgs->pMap->mesh.mesh, pPieceArr->pArr[0].pEntry->faceIndex, false);
		ruvmAllocMergeBufs(pArgs->pContext, &mergeBufHandles, totalVerts);
		for (int32_t j = 0; j < pPieceRoots->count; ++j) {
			Piece *pPieceRoot = pPieceArr->pArr + pPieceRoots->pArr[j];
			int32_t *pInFaces = NULL;
			int32_t pieceCount = 0; //this is only need if getting in faces
			if (pArgs->ppInFaceTable) {
				pieceCount = getPieceCount(pPieceRoot); 
				getPieceInFaces(&pArgs->pContext->alloc, &pInFaces, pPieceRoot, pieceCount);
			}
			int32_t job = pPieceArr->pArr[pPieceRoots->pArr[j]].pEntry->job;
			RUVM_ASSERT("", job >= 0 && job < pContext->threadCount);
			ruvmMergeSingleBorderFace(pArgs, timeSpent, pPieceRoots->pArr[j], pPieceArr,
									  &ruvmFace, &mergeBufHandles, pInFaces, pieceCount);
			if (pInFaces) {
				pArgs->pContext->alloc.pFree(pInFaces);
			}
			RUVM_ASSERT("", j >= 0 && j < pPieceRoots->count);
		}
		if (pPieceRoots->pArr) {
			pArgs->pContext->alloc.pFree(pPieceRoots->pArr);
		}
		if (pPieceArr->pArr) {
			pArgs->pContext->alloc.pFree(pPieceArr->pArr);
			int abc = 0;
		}
		CLOCK_STOP_NO_PRINT;
		timeSpent[6] += CLOCK_TIME_DIFF(start, stop);
		RUVM_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
	ruvmDestroyMergeBufs(pArgs->pContext, &mergeBufHandles);
	pContext->alloc.pFree(pArgs->pPieceArrTable);
	pContext->alloc.pFree(pArgs->pPieceRootTable);
	pContext->alloc.pFree(pArgs->pTotalVertTable);
	printf("Combine time breakdown: \n");
	for(int32_t i = 0; i < 7; ++i) {
		printf("	%lu\n", timeSpent[i]);
	}
	printf("\n");
	++*pArgs->pJobsCompleted;
	pThreadPool->pMutexUnlock(pThreadPoolHandle, pArgs->pMutex);
}

static
void linkEntriesFromOtherJobs(RuvmContext pContext, SendOffArgs *pJobArgs,
                              BorderBucket *pBucket, int32_t faceIndex,
							  int32_t hash, int32_t job) {
	for (int32_t j = job + 1; j < pContext->threadCount; ++j) {
		//RUVM_ASSERT("", pJobArgs[j].borderTable.size > 0);
		//RUVM_ASSERT("", pJobArgs[j].borderTable.pTable != NULL);
		BorderBucket *pBucketOther = pJobArgs[j].borderTable.pTable + hash;
		//RUVM_ASSERT("", pBucketOther != NULL);
		do {
			if (pBucketOther->pEntry) {
				if (faceIndex == pBucketOther->pEntry->faceIndex) {
					BorderFace *pEntry = pBucket->pEntry;
					while (pEntry->pNext) {
						pEntry = pEntry->pNext;
					}
					pEntry->pNext = pBucketOther->pEntry;
					pBucketOther->pEntry = NULL;
				}
			}
			pBucketOther = pBucketOther->pNext;
		} while (pBucketOther);
	}
}

static
void compileBorderTables(RuvmContext pContext, SendOffArgs *pJobArgs,
                         CompiledBorderTable *pBorderTable,
						 int32_t totalBorderFaces) {
	pBorderTable->ppTable =
		pContext->alloc.pMalloc(sizeof(void *) * totalBorderFaces);
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		for (int32_t hash = 0; hash < pJobArgs[i].borderTable.size; ++hash) {
			RUVM_ASSERT("", pJobArgs[i].borderTable.size > 0);
			RUVM_ASSERT("", pJobArgs[i].borderTable.pTable);
			BorderBucket *pBucket = pJobArgs[i].borderTable.pTable + hash;
			int32_t depth = 0;
			do {
				if (pBucket->pEntry) {
					int32_t faceIndex = pBucket->pEntry->faceIndex;
					RUVM_ASSERT("", faceIndex >= 0);
					linkEntriesFromOtherJobs(pContext, pJobArgs, pBucket,
					                         faceIndex, hash, i);
					RUVM_ASSERT("", pBorderTable->count >= 0);
					RUVM_ASSERT("", pBorderTable->count < totalBorderFaces);
					pBorderTable->ppTable[pBorderTable->count] = pBucket->pEntry;
					pBorderTable->count++;
				}
				BorderBucket *pNextBucket = pBucket->pNext;
				if (depth != 0) {
					pContext->alloc.pFree(pBucket);
				}
				pBucket = pNextBucket;
				depth++;
			} while (pBucket);
			RUVM_ASSERT("", hash >= 0 && hash < pJobArgs[i].borderTable.size);
		}
		RUVM_ASSERT("", i >= 0 && i < pContext->threadCount);
	}
}

static
void allocCombineTables(RuvmAlloc *pAlloc, CombineTables *pCTables,
                        int32_t totalBorderFaces, int32_t totalBorderEdges) {
	pCTables->pVertTable =
		pAlloc->pCalloc(totalBorderFaces, sizeof(BorderVert));
	pCTables->pOnLineTable =
		pAlloc->pCalloc(totalBorderFaces, sizeof(OnLine));
	pCTables->pEdgeTable =
		pAlloc->pCalloc(totalBorderEdges, sizeof(BorderEdge));
	pCTables->vertTableSize = totalBorderFaces;
	pCTables->onLineTableSize = totalBorderFaces;
	pCTables->edgeTableSize = totalBorderEdges;
}

static
void destroyCombineTables(RuvmAlloc *pAlloc, CombineTables *pCTables) {
	for (int32_t i = 0; i < pCTables->vertTableSize; ++i) {
		BorderVert *pEntry = pCTables->pVertTable[i].pNext;
		while (pEntry) {
			BorderVert *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	pAlloc->pFree(pCTables->pVertTable);
	for (int32_t i = 0; i < pCTables->onLineTableSize; ++i) {
		OnLine *pEntry = pCTables->pOnLineTable[i].pNext;
		while (pEntry) {
			OnLine *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	pAlloc->pFree(pCTables->pOnLineTable);
	for (int32_t i = 0; i < pCTables->edgeTableSize; ++i) {
		BorderEdge *pEntry = pCTables->pEdgeTable[i].pNext;
		while (pEntry) {
			BorderEdge *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	pAlloc->pFree(pCTables->pEdgeTable);
}

static
void sendOffMergeJobs(RuvmContext pContext, CompiledBorderTable *pBorderTable,
                      MergeSendOffArgs *pMergeJobArgs, RuvmMap pMap,
					  Mesh *pMeshOut, SendOffArgs *pMapJobArgs,
					  EdgeVerts *pEdgeVerts, int8_t *pVertSeamTable,
					  CombineTables *pCTables, JobBases *pJobBases,
					  int32_t *pJobsCompleted, void *pMutex, bool *pEdgeSeamTable,
                      InFaceArr **ppInFaceTable, float wScale, Mesh *pInMesh) {
	int32_t entriesPerJob = pBorderTable->count / pContext->threadCount;
	void *jobArgPtrs[MAX_THREADS];
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		int32_t entriesStart = entriesPerJob * i;
		int32_t entriesEnd = i == pContext->threadCount - 1 ?
			pBorderTable->count : entriesStart + entriesPerJob;
		//TODO make a struct for these common variables, like pContext,
		//pMap, pEdgeVerts, etc, so you don't need to move them
		//around manually like this.
		pMergeJobArgs[i].pBorderTable = pBorderTable;
		pMergeJobArgs[i].entriesStart = entriesStart;
		pMergeJobArgs[i].entriesEnd = entriesEnd;
		pMergeJobArgs[i].pContext = pContext;
		pMergeJobArgs[i].pMap = pMap;
		pMergeJobArgs[i].pMeshOut = pMeshOut;
		pMergeJobArgs[i].ppInFaceTable = ppInFaceTable;
		pMergeJobArgs[i].pJobArgs = pMapJobArgs;
		pMergeJobArgs[i].pEdgeVerts = pEdgeVerts;
		pMergeJobArgs[i].pVertSeamTable = pVertSeamTable;
		pMergeJobArgs[i].pEdgeSeamTable = pEdgeSeamTable;
		pMergeJobArgs[i].pJobBases = pJobBases;
		pMergeJobArgs[i].pCTables = pCTables;
		pMergeJobArgs[i].job = i;
		pMergeJobArgs[i].pJobsCompleted = pJobsCompleted;
		pMergeJobArgs[i].pMutex = pMutex;
		pMergeJobArgs[i].wScale = wScale;
		pMergeJobArgs[i].pInMesh = pInMesh;
		jobArgPtrs[i] = pMergeJobArgs + i;
	}
	pContext->threadPool.pJobStackPushJobs(pContext->pThreadPoolHandle,
	                                       pContext->threadCount,
										   mergeAndCopyEdgeFaces, jobArgPtrs);
}

void ruvmMergeBorderFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
					      JobBases *pJobBases, int8_t *pVertSeamTable,
                          bool *pEdgeSeamTable, InFaceArr **ppInFaceTable,
                          float wScale, Mesh *pInMesh) {
	int32_t totalBorderFaces = 0;
	int32_t totalBorderEdges = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		totalBorderFaces += pJobArgs[i].bufMesh.borderFaceCount;
		totalBorderEdges += pJobArgs[i].bufMesh.borderEdgeCount;
		RUVM_ASSERT("", i < pContext->threadCount);
	}
	RUVM_ASSERT("", totalBorderFaces >= 0 && totalBorderFaces < 100000000);
	RUVM_ASSERT("", totalBorderEdges >= 0 && totalBorderEdges < 100000000);
	CompiledBorderTable borderTable = {0};
	//compile border table entries from all jobs, into a single table
	compileBorderTables(pContext, pJobArgs, &borderTable, totalBorderFaces);
	//tables used for merging mesh mesh data correctly
	CombineTables cTables = {0};
	allocCombineTables(&pContext->alloc, &cTables, totalBorderFaces,
	                   totalBorderEdges);
	for (int32_t i = 0; i < pJobArgs[0].mesh.mesh.vertCount; ++i) {
		int32_t preserve = pJobArgs[0].pInVertTable[i];
		for (int32_t j = 1; j < pContext->threadCount; ++j) {
			preserve |= pJobArgs[j].pInVertTable[i];
			RUVM_ASSERT("", j >= 0 && j < pContext->threadCount);
		}
		pJobArgs[0].pInVertTable[i] = preserve;
		RUVM_ASSERT("", i >= 0 && i < pJobArgs[0].mesh.mesh.vertCount);
	}
	MergeSendOffArgs mergeJobArgs[MAX_THREADS];
	int32_t jobsCompleted = 0;
	void *pMutex;
	pContext->threadPool.pMutexGet(pContext->pThreadPoolHandle, &pMutex);
	sendOffMergeJobs(pContext, &borderTable, mergeJobArgs, pMap, pMeshOut,
	                 pJobArgs, pEdgeVerts, pVertSeamTable, &cTables, pJobBases,
					 &jobsCompleted, pMutex, pEdgeSeamTable, ppInFaceTable,
	                 wScale, pInMesh);
	waitForJobs(pContext, &jobsCompleted, pMutex);
	pContext->threadPool.pMutexDestroy(pContext->pThreadPoolHandle, pMutex);
	pContext->alloc.pFree(borderTable.ppTable);
	destroyCombineTables(&pContext->alloc, &cTables);
}
