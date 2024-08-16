#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <RUVM.h>
#include <CombineJobMeshes.h>
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
	int8_t segment;
	bool checked : 1;
	bool preserve : 1;
	bool index : 1;
	bool altIndex : 1;
	bool seam : 1;
	bool removed : 1;
	bool hasSegment : 1;
	bool inOrient : 1;
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
	bool isOnInVert = getIfOnInVert(pEntry, k);
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
                         int32_t refIndex, bool isPreserve, bool isReceive,
						 Piece *pPiece, int32_t i, bool seam, int32_t segment) {
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
	pEntry->segment = segment;
	pEntry->inOrient = pPiece->pEntry->inOrient;
	if (refIndex >= 0 && isReceive) {
		//pPiece->keepPreserve |= 1 << i;
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
	pPiece->pEntry = pEntry;
	pPiece->entryIndex = entryIndex;
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
		RUVM_ASSERT("", pVerts &&
		                (pVerts[0] == inInfo.edgeLoop ||
		                 pVerts[1] == inInfo.edgeLoop));
		if (pVerts[1] < 0) {
			//no other vert on edge
			pPiece->hasSeam = true;
			continue;
		}
		bool baseKeep;
		if (isOnInVert) {
			RUVM_ASSERT("", pArgs->pJobArgs[0].pInVertTable[inInfo.vert] >= 0); //pInVertTable is 0 .. 3
			RUVM_ASSERT("", pArgs->pJobArgs[0].pInVertTable[inInfo.vert] <= 3); 
			baseKeep = pArgs->pJobArgs[0].pInVertTable[inInfo.vert] > 0;
			pPiece->keepPreserve |= baseKeep << i;
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
		pEntries[entryIndex].edges[pEntries[entryIndex].edgeCount].edge = inInfo.edge;
		int32_t segment = getSegment(pEntry, i);
		pEntries[entryIndex].edges[pEntries[entryIndex].edgeCount].segment = segment;
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
				if (isReceive) {
					pPiece->keepPreserve |= true << i;
				}
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
			                    isPreserve, isReceive, pPiece, i, seam, segment);
			continue;
		}
		do {
			RUVM_ASSERT("", pEdgeEntry->edge - 1 >= 0);
			RUVM_ASSERT("", pEdgeEntry->edge - 1 < pArgs->pInMesh->mesh.edgeCount);
			RUVM_ASSERT("", pEdgeEntry->index % 2 == pEdgeEntry->index); // range 0 .. 1
			if (pEdgeEntry->edge == inInfo.edge + 1
				&& pEdgeEntry->segment == segment) {

				if (pEdgeEntry->entries[pEdgeEntry->index] != entryIndex) {
					//other side of the edge
					pEdgeEntry->entries[1] = entryIndex;
					pEdgeEntry->loop[1] = i;
					pEdgeEntry->index = 1;
					pEdgeEntry->refIndex[1] = refIndex;
					if (pEdgeEntry->inOrient != pEntry->inOrient) {
						pEdgeEntry->removed = true;
					}
				}
				if (!pEdgeEntry->seam &&
					!pEdgeEntry->altIndex &&
					isPreserve && pEdgeEntry->refIndex[0] != refIndex) {
					pEdgeEntry->receive += isReceive;
					if (refIndex >= 0 && isReceive) {
						//this is done here (as well as in initSharedEdgeEntry),
						//in order to avoid duplicate loops being added later on.
						//Ie, only one loop should be marked keep per vert
						//pPiece->keepPreserve |= 1 << i;
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
				                    isPreserve, isReceive, pPiece, i, seam, segment);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
		RUVM_ASSERT("", i >= 0 && i < face.size);
	}
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
	int32_t segment = getSegment(pPiece->pEntry, *pLoop);
	BorderInInfo inInfo = getBorderEntryInInfo(pPiece->pEntry, pArgs->pJobArgs, *pLoop);
	int32_t hash = ruvmFnvHash((uint8_t*)&inInfo.edge, 4, edgeTableSize);
	SharedEdgeWrap* pEdgeEntryWrap = pEdgeTable + hash;
	SharedEdge* pEdgeEntry = pEdgeEntryWrap->pEntry;
	while (pEdgeEntry) {
		if (pEdgeEntry->index && !pEdgeEntry->removed) {
			bool loopMatches = *pLoop == pEdgeEntry->loop[0] &&
							   pPiece->entryIndex == pEdgeEntry->entries[0] ||
							   *pLoop == pEdgeEntry->loop[1] &&
							   pPiece->entryIndex == pEdgeEntry->entries[1];
			if (loopMatches &&
				inInfo.edge + 1 == pEdgeEntry->edge &&
			    segment == pEdgeEntry->segment) {
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
		//loop does not share an edge without any other loop,
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
	Piece *pPiecePrev;
	Piece *pStartPiece;
	SharedEdge *pActiveEdge;
	SharedEdge *pQuadEdge;
	int32_t loop;
	int32_t loopPrev;
	int32_t startLoop;
	int32_t validBranches;
	bool quad;
} EdgeStack;

static
bool checkIfIntersectsReceive(MergeSendOffArgs *pArgs, EdgeStack *pItem, Mesh *pBufMesh, RuvmMap pMap,
                              FaceRange *pMapFace, int32_t *pMapLoop, bool side) {
	Mesh *pMapMesh = &pMap->mesh;
	RUVM_ASSERT("", pItem->pPiece->bufFace.size >= 3);
	int32_t loopNext = (pItem->loop + 1) % pItem->pPiece->bufFace.size;
	bool isOnInVert;
	if (side) {
		isOnInVert = getIfOnInVert(pItem->pPiece->pEntry, loopNext);
	}
	else {
		isOnInVert = getIfOnInVert(pItem->pPiece->pEntry, pItem->loop);
	}
	if (!isOnInVert) {
		//exterior intersects with a map edge,
		//so just check if said edge is receive
		int32_t loop;
		if (side) {
			loop = loopNext;
		}
		else {
			loop = pItem->loop == 0 ?
				pItem->pPiece->bufFace.size - 1 : pItem->loop - 1;
		}
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
	int32_t loop = pItem->loop;
	if (side) {
		loop = loopNext;
		loopNext = pItem->loop;
	}
	V2_I32 tileMin = getTileMinFromBoundsEntry(pItem->pPiece->pEntry);
	V2_F32 fTileMin = {(float)tileMin.d[0], (float)tileMin.d[1]};
	V2_F32 c = _(pUvStart[-loop] V2SUB fTileMin);
	V2_F32 d = _(pUvStart[-loopNext] V2SUB fTileMin);
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
		//RUVM_ASSERT("", !_(*(V2_F32 *)&pMapMesh->pVerts[mapVert] V2EQL *(V2_F32 *)&pMapMesh->pVerts[mapVertNext]));
		V3_F32 intersect = {0};
		bool valid = 
			calcIntersection(pMapMesh->pVerts[mapVert],
					         pMapMesh->pVerts[mapVertNext], c, cd,
					         &intersect, &t, NULL);
		//do you need to handle .0 or 1.0 as distinct cases?
		//ie, should you track preserve verts hit?

		V2_F32 ci = _(*(V2_F32 *)&intersect V2SUB c);
		float dot = _(ci V2DOT cd);
		if (!valid || t < .0f || t > 1.0f || dot > .0f) {
			continue;
		}
		//BorderInInfo inInfo =
		//	getBorderEntryInInfo(pItem->pPiece->pEntry,
		//	                     pArgs->pJobArgs, loop);
		//setBitArr(pArgs->pInVertKeep, inInfo.vert, true);
		return true;
	}
	return false;
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
                    SharedEdge *pEdge, bool side) {
	EdgeStack *pItem = pStack + *pStackPtr;
	Mesh *pMapMesh = &pArgs->pMap->mesh;
	Mesh *pBufMesh = &pArgs->pJobArgs[pItem->pPiece->pEntry->job].bufMesh;
	int32_t mapLoop = -1;
	bool isReceive =
		checkIfIntersectsReceive(pArgs, pNeighbour, pBufMesh, pArgs->pMap,
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
		pEdge->validIdx = treeCount;
		pItem->validBranches++;
		return true;
	}
	return false;
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
	}
	else if (pItem->pStartPiece) {
		//branch has returned, and is valid
		pItem->validBranches++;
		if (!*pStackPtr) {
			bool onInVert = getIfOnInVert(pItem->pPiecePrev->pEntry, pItem->loopPrev);
			if (onInVert) {
				BorderInInfo inInfo =
					getBorderEntryInInfo(pItem->pPiecePrev->pEntry,
										 pArgs->pJobArgs, pItem->loopPrev);
				setBitArr(pArgs->pInVertKeep, inInfo.vert, true);
			}
		}
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
			*pUnwind = !pItem->validBranches;
			if (!*pUnwind) {
				bool onInVert = getIfOnInVert(pItem->pPiece->pEntry, pItem->loop);
				if (onInVert) {
					BorderInInfo inInfo =
						getBorderEntryInInfo(pItem->pPiece->pEntry,
						                     pArgs->pJobArgs, pItem->loop);
					setBitArr(pArgs->pInVertKeep, inInfo.vert, true);
				}
			}
			--*pStackPtr;
			return;
		}
		//Set next loop
		if (!*pStackPtr &&
		    getIfRuvm(pItem->pPiece->pEntry, pItem->loop) &&
			!getIfOnLine(pItem->pPiece->pEntry, pItem->loop)) {

			pItem->loop++;
			continue;
		}
		if (!pItem->quad) {
			neighbour.loop = pItem->loop;
			neighbour.pPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
												 pItem->pPiece, pPieceRoot,
												 &neighbour.loop, &pEdge);
			if (!neighbour.pPiece) {
				pItem->loop++;
				continue;
			}
		}
		RUVM_ASSERT("", pEdge);
		//if validIdx isn't -1, this edge has already been checked
		if (pEdge->preserve && pEdge->validIdx == -1) {
			if (!*pStackPtr) {
				handleExterior(pStack, pStackPtr, pItem, pValid, treeCount, pReceive,
				                pArgs, pMapFace, pEdge, false);
			}
			Piece *pPieceNext = pItem->quad ? neighbour.pPiece : pItem->pPiece;
			int32_t loopNext = pItem->quad ? neighbour.loop : pItem->loop;
			loopNext = (loopNext + 1) % pPieceNext->bufFace.size;
			if (*pReceive != -1) {
				bool exterior;
				exterior = isLoopOnExterior(pArgs, pPieceNext, pPieceRoot,
				                            pEdgeTable, edgeTableSize, loopNext);
				if (exterior) {
					EdgeStack *pItemToTest = pItem->quad ? &neighbour : pItem;
					handleExterior(pStack, pStackPtr, pItemToTest, pValid, treeCount,
					               pReceive, pArgs, pMapFace, pEdge, true);
					if (!*pStackPtr) {
						ret = true;
					}
				}
				else {
					pushToEdgeStack(pStack, pStackPtr, treeCount, pEdge, pPieceNext,
									loopNext);
					ret = true;
				}
			}
		}
		else if (!*pStackPtr && pEdge->preserve) {
			bool onInVert = getIfOnInVert(pItem->pPiece->pEntry, pItem->loop);
			if (onInVert) {
				BorderInInfo inInfo =
					getBorderEntryInInfo(pItem->pPiece->pEntry,
										 pArgs->pJobArgs, pItem->loop);
				setBitArr(pArgs->pInVertKeep, inInfo.vert, true);
			}
		}
		if (!pItem->quad) {
			pItem->pPiecePrev = pItem->pPiece;
			pItem->loopPrev = pItem->loop;
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
                           int32_t edgeTableSize, bool **ppValid,
                           int32_t *pValidCount, int32_t *pValidSize) {
	//TODO first , check if the map face even has any preserve edges,
	//     no point doing all this if not
	RuvmAlloc *pAlloc = &pArgs->pContext->alloc;
	Mesh *pInMesh = pArgs->pInMesh;
	Mesh *pMapMesh = &pArgs->pMap->mesh;
	// Get first not exterior loop
	// This is done to ensure we don't start inside the face
	Piece *pPiece = pPieceArr->pArr + piece;
	Piece *pPieceRoot = pPiece;
	FaceRange mapFace = getFaceRange(&pArgs->pMap->mesh.mesh,
	                                 pPieceRoot->pEntry->faceIndex, false);
	if (!*ppValid) {
		*pValidSize = 8;
		*ppValid = pAlloc->pCalloc(*pValidSize, sizeof(bool));
	}
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
		if (*pValidCount == *pValidSize) {
			int32_t oldSize = *pValidSize;
			*pValidSize *= 2;
			*ppValid = pAlloc->pRealloc(*ppValid, sizeof(bool) * *pValidSize);
			memset(*ppValid + oldSize, 0, sizeof(bool) * oldSize);
		}
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
			RUVM_ASSERT("", *pValidCount <= *pValidSize);
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
		//TODO can you just get the edges in the piece by calling getNeighbourEntry?
		//     Rather than storing a list of edges in the piece?
		//     Is there a perf cost?
		for (int32_t j = 0; j < pPiece->edgeCount; ++j) {
			EdgeSegmentPair edge = pPiece->edges[j];
			int32_t hash = ruvmFnvHash((uint8_t *)&edge, 4, tableSize);
			SharedEdgeWrap *pSharedEdgeWrap = pSharedEdges + hash;
			SharedEdge *pEdgeEntry = pSharedEdgeWrap->pEntry;
			while (pEdgeEntry) {
				if (pEdgeEntry->removed) {}
				else if (pEdgeEntry->seam) {
					bool aIsOnInVert = pEdgeEntry->refIndex[0] < 0;
					bool bIsOnInVert = pEdgeEntry->refIndex[1] < 0;
					if (aIsOnInVert ^ bIsOnInVert) {
						bool whichLoop = aIsOnInVert;

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
				else if (pEdgeEntry->edge - 1 == pPiece->edges[j].edge &&
				         pEdgeEntry->segment == pPiece->edges[j].segment) {
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
	int32_t validSize = 0;
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
					validatePreserveEdges(pArgs, pPieceArr, j,
					                      pEdgeTable, edgeTableSize, &pValid,
					                      &validCount, &validSize);
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
void compileEntryInfo(BorderFace *pEntry, int32_t *pCount) {
	*pCount = 0;
	while (pEntry) {
		++*pCount;
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

static
void markPreserveIfKeepInVert(MergeSendOffArgs *pArgs, Piece *pPieceRoot,
                              Piece* pPiece, int32_t k) {
	bool keepInVert = false;
	BorderInInfo inInfo = getBorderEntryInInfo(pPiece->pEntry, pArgs->pJobArgs, k);
	bool onInVert = getIfOnInVert(pPiece->pEntry, k);
	if (onInVert) {
		keepInVert = indexBitArray(pArgs->pInVertKeep, inInfo.vert);
		if (keepInVert) {
			pPiece->keepPreserve |= true << k;
		}
	}
}

static
void markKeepInVertsPreserve(MergeSendOffArgs *pArgs, RuvmMap *pMap, Piece* pPiece) {
	Piece* pPieceRoot = pPiece;
	do {
		for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
			if (getIfRuvm(pPiece->pEntry, i)) {
				continue;
			}
			markPreserveIfKeepInVert(pArgs, pPieceRoot, pPiece, i);
		}
		pPiece = pPiece->pNext;
	} while (pPiece);
}

static
int32_t getPieceCount(Piece* pPiece) {
	int32_t count = 0;
	do {
		count++;
		pPiece = pPiece->pNext;
	} while(pPiece);
	return count;
}

static
void sortLoops(MergeSendOffArgs* pArgs, Piece* pPiece, PieceArr *pPieceArr,
               SharedEdgeWrap* pEdgeTable, int32_t edgeTableSize, int32_t *pCount) {
	bool single = false;
	if (!pPiece->pNext) {
		single = true;
	}
	Mesh* pBufMesh = &pArgs->pJobArgs[0].mesh;
	Piece* pPieceRoot = pPiece;
	int32_t loop = 0;
	if (!single) {
		// get starting loop
		// This is done to ensure we don't start inside the face
		loop = getStartingLoop(&pPiece, pArgs, pPiece, pEdgeTable,
	                           edgeTableSize);
	}
	RUVM_ASSERT("No valid starting loop found", loop >= 0);
	bool adj = false;
	int32_t sort = 1;
	Piece *pOtherPiece = NULL;
	if (!single) {
		pOtherPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
										pPiece, pPieceRoot, &loop, NULL);
	}
	if (!pOtherPiece) {
			loop++;
	}
	else {
		adj = true;
		pPiece = pOtherPiece;
	}
	do {
		loop %= pPiece->bufFace.size;
		if (pPiece->order[loop]) {
			//We've done a full loop around
			break;
		}
		//Set next loop
		if (getIfRuvm(pPiece->pEntry, loop) &&
			!getIfOnLine(pPiece->pEntry, loop)) {
			
			pPiece->add |= true << loop;
			pPiece->order[loop] = sort;
			sort++;
			loop++;
			adj = false;
			continue;
		}
		int32_t otherLoop = loop;
		pOtherPiece = NULL;
		if (!single) {
			pOtherPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
											pPiece, pPieceRoot, &otherLoop, NULL);
		}
		if (!pOtherPiece) {
			if (!adj) {
				pPiece->add |= true << loop;
				pPiece->order[loop] = sort;
				//set keep preserve to false if true
				UBitField16 mask = -0x1 ^ (0x1 << loop);
				pPiece->keepPreserve &= mask;
				sort++;
			}
			else {
				pPiece->order[loop] = 1;
				adj = false;
			}
			loop++;
			continue;
		}
		else if (!adj) {
			if (pPiece->keepPreserve >> loop & 0x01 ||
				(pPiece->keepSeam >> loop & 0x01) ||
				(pPiece->keepVertPreserve >> loop & 0x01)) {

				pPiece->add |= true << loop;
				pPiece->order[loop] = sort;
				sort++;
			}
			else {
				pPiece->order[loop] = 1;
			}
			adj = true;
		}
		else {
			pPiece->order[loop] = 1;
		}
		loop = otherLoop;
		pPiece = pOtherPiece;
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
void initVertTableEntry(MergeSendOffArgs *pArgs, BorderVert *pVertEntry,
                        BorderFace *pEntry, BufMesh *pBufMesh,
                        int32_t ruvmEdge, int32_t *pVert,
                        BorderInInfo *pInInfo, int32_t ruvmFace,
                        int32_t loop, int32_t loopLocal,
                        Piece *pPieceRoot, V2_I32 tileMin) {
	bool realloced = false;
	int32_t outVert = meshAddVert(&pArgs->pContext->alloc, pArgs->pMeshOut, &realloced);
	copyAllAttribs(&pArgs->pMeshOut->mesh.vertAttribs, outVert,
				   &asMesh(pBufMesh)->mesh.vertAttribs, *pVert);
	*pVert = outVert;
	pVertEntry->vert = outVert;
	pVertEntry->tile = tileMin;
	pVertEntry->ruvmEdge = ruvmEdge;
	pVertEntry->loops = 1;
	pVertEntry->baseEdge = pInInfo->edge;
	pVertEntry->baseVert = pInInfo->vert;
	pVertEntry->loopIndex = pInInfo->vertLoop;
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
void addBorderLoopAndVert(MergeSendOffArgs *pArgs, Piece *pPiece, Piece *pPieceRoot,
                          int32_t k, bool addToTables) {
	BufMesh *pBufMesh = &pArgs->pJobArgs[pPiece->pEntry->job].bufMesh;
	int32_t loop = pPiece->bufFace.start - k;
	int32_t vert = bufMeshGetVertIndex(pPiece, pBufMesh, k);
	RUVM_ASSERT("", vert > asMesh(pBufMesh)->vertBufSize - 1 -
		pBufMesh->borderVertCount);
	RUVM_ASSERT("", vert < asMesh(pBufMesh)->vertBufSize);
	int32_t edge = bufMeshGetEdgeIndex(pPiece, pBufMesh, k);
	RUVM_ASSERT("", edge > asMesh(pBufMesh)->edgeBufSize - 1 -
		pBufMesh->borderEdgeCount);
	RUVM_ASSERT("", edge < asMesh(pBufMesh)->edgeBufSize);
	int32_t mapLoop = getMapLoop(pPiece->pEntry, pArgs->pMap, k);
	RUVM_ASSERT("", mapLoop >= 0 && mapLoop < pArgs->pMap->mesh.mesh.loopCount);
	BorderFace *pEntry = pPiece->pEntry;
	V2_I32 tileMin = getTileMinFromBoundsEntry(pEntry);
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, k);
	bool isOnInVert = getIfOnInVert(pEntry, k);
	if (!isOnInVert) {
		inInfo.vert = -1;
	}
	int32_t hash;
	int32_t ruvmEdge;
	if (isOnInVert) {
		hash = ruvmFnvHash((uint8_t *)&inInfo.vert, 4, pArgs->pCTables->vertTableSize);
		ruvmEdge = -1;
	}
	else {
		FaceRange ruvmFace =
			getFaceRange(&pArgs->pMap->mesh.mesh, pEntry->faceIndex, false);
		ruvmEdge = pArgs->pMap->mesh.mesh.pEdges[ruvmFace.start + mapLoop];
		hash = ruvmFnvHash((uint8_t *)&ruvmEdge, 4, pArgs->pCTables->vertTableSize);
	}
	BorderVert *pVertEntry = pArgs->pCTables->pVertTable + hash;
	if (!pVertEntry->loops) {
		if (addToTables) {
			initVertTableEntry(pArgs, pVertEntry, pEntry, pBufMesh, ruvmEdge,
							   &vert, &inInfo, pEntry->faceIndex, loop, k,
							   pPieceRoot, tileMin);
		}
		else {
			pVertEntry = NULL;
		}
	}
	else {
		do {
			//Check vert entry is valid
			RUVM_ASSERT("", pVertEntry->ruvmEdge >= -1);
			RUVM_ASSERT("", pVertEntry->ruvmEdge < pArgs->pMap->mesh.mesh.edgeCount);
			RUVM_ASSERT("", pVertEntry->ruvmFace >= 0);
			RUVM_ASSERT("", pVertEntry->ruvmFace < pArgs->pMap->mesh.mesh.faceCount);
			bool match;
			if (isOnInVert) {
				V2_F32 *pMeshInUvA = pArgs->pInMesh->pUvs + pVertEntry->loopIndex;
				V2_F32 *pMeshInUvB = pArgs->pInMesh->pUvs + inInfo.vertLoop;
				match = pVertEntry->baseVert == inInfo.vert &&
						pVertEntry->ruvmFace == pEntry->faceIndex &&
						pMeshInUvA->d[0] == pMeshInUvB->d[0] &&
						pMeshInUvA->d[1] == pMeshInUvB->d[1];
			}
			else {
				BufMesh *pOtherBufMesh = &pArgs->pJobArgs[pVertEntry->job].bufMesh;
				bool connected = 
					_(asMesh(pBufMesh)->pUvs[loop] V2APROXEQL
					  asMesh(pOtherBufMesh)->pUvs[pVertEntry->loop]);
				match =  pVertEntry->ruvmEdge == ruvmEdge &&
					     *(uint32_t *)&pVertEntry->tile.d[0] == tileMin.d[0] &&
					     *(uint32_t *)&pVertEntry->tile.d[1] == tileMin.d[1] &&
						 pVertEntry->baseEdge == inInfo.edge &&
						 connected;
			}
			if (match) {
				//If loop isOnInVert,
				//then entry must also be an isOnInVert entry.
				//And if not, then entry must also not be
				RUVM_ASSERT("", (isOnInVert && pVertEntry->baseVert != -1) ||
				       (!isOnInVert && pVertEntry->baseVert == -1));
				vert = pVertEntry->vert;
				break;
			}
			if (!pVertEntry->pNext && addToTables) {
				pVertEntry = pVertEntry->pNext =
					pArgs->pContext->alloc.pCalloc(1, sizeof(BorderVert));
				initVertTableEntry(pArgs, pVertEntry, pEntry, pBufMesh, ruvmEdge,
				                   &vert, &inInfo, pEntry->faceIndex, loop, k,
				                   pPieceRoot, tileMin);
				break;
			}
			pVertEntry = pVertEntry->pNext;
		} while(pVertEntry);
	}
	//TODO debug/ verify border edge implementation is working correctly
	//TODO why cant you just determine connected edges with the above loop table?
	//     is a separate table really needed? If you know 2 loops are connected,
	//     can't you then connect their edges?
	uint32_t valueToHash = inInfo.edge + pEntry->faceIndex;
	hash = ruvmFnvHash((uint8_t *)&valueToHash, 4, pArgs->pCTables->edgeTableSize);
	BorderEdge *pEdgeEntry = pArgs->pCTables->pEdgeTable + hash;
	if (!pEdgeEntry->valid) {
		if (addToTables) {
			initEdgeTableEntry(pArgs, pEdgeEntry, pBufMesh, &edge, inInfo.edge,
							   pEntry->faceIndex);
		}
		else {
			pEdgeEntry = NULL;
		}
	}
	else {
		do {
			if (pEdgeEntry->inEdge == inInfo.edge &&
				pEdgeEntry->mapFace == pEntry->faceIndex) {
				edge = pEdgeEntry->edge;
				break;
			}
			if (!pEdgeEntry->pNext && addToTables) {
				pEdgeEntry = pEdgeEntry->pNext =
					pArgs->pContext->alloc.pCalloc(1, sizeof(BorderEdge));
				initEdgeTableEntry(pArgs, pEdgeEntry, pBufMesh, &edge,
				                   inInfo.edge, pEntry->faceIndex);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(pEdgeEntry);
	}
	if (pVertEntry) {
		pBufMesh->mesh.mesh.pLoops[loop] = vert;
		pBufMesh->mesh.mesh.pEdges[loop] = 0;
	}
	else {
		//set add to false
		UBitField16 mask = -0x1 ^ (0x1 << k);
		pPiece->add &= mask;
		//correct sort, as this loop will not be kept
		int32_t sort = pPiece->order[k];
		pPiece = pPieceRoot;
		do {
			for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
				if (pPiece->order[i] > sort) {
					pPiece->order[i]--;
				}
			}
			pPiece = pPiece->pNext;
		} while(pPiece);
	}
}

static
void addToOutMesh(MergeSendOffArgs *pArgs) {
	CLOCK_INIT;
	uint64_t timeSpent[7] = {0};
	RuvmContext pContext = pArgs->pContext;
	RuvmAlloc *pAlloc = &pContext->alloc;
	int32_t count = pArgs->entriesEnd - pArgs->entriesStart;
	MergeBufHandles mergeBufHandles = {0};
	ruvmAllocMergeBufs(pArgs->pContext, &mergeBufHandles, pArgs->totalVerts);
	for (int32_t i = 0; i < count; ++i) {
		int32_t reali = pArgs->entriesStart + i;
		CLOCK_START;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		FaceRange ruvmFace =
			getFaceRange(&pArgs->pMap->mesh.mesh, pPieceArr->pArr[0].pEntry->faceIndex, false);
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
				pAlloc->pFree(pInFaces);
			}
			RUVM_ASSERT("", j >= 0 && j < pPieceRoots->count);
		}
		if (pPieceRoots->pArr) {
			pAlloc->pFree(pPieceRoots->pArr);
		}
		if (pPieceArr->pArr) {
			pAlloc->pFree(pPieceArr->pArr);
			int abc = 0;
		}
		CLOCK_STOP_NO_PRINT;
		timeSpent[6] += CLOCK_TIME_DIFF(start, stop);
		RUVM_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
	ruvmDestroyMergeBufs(pArgs->pContext, &mergeBufHandles);
	pAlloc->pFree(pArgs->pPieceArrTable);
	pAlloc->pFree(pArgs->pPieceRootTable);
	pAlloc->pFree(pArgs->pTotalVertTable);
	printf("Combine time breakdown: \n");
	for(int32_t i = 0; i < 7; ++i) {
		printf("	%lu\n", timeSpent[i]);
	}
	printf("\n");
	++*pArgs->pJobsCompleted;
}

static
void mergeIntersectionLoops(MergeSendOffArgs *pArgs, bool preserve) {
	int32_t count = pArgs->entriesEnd - pArgs->entriesStart;
	for (int32_t i = 0; i < count; ++i) {
		int32_t reali = pArgs->entriesStart + i;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		for (int32_t j = 0; j < pPieceRoots->count; ++j) {
			Piece *pPiece = pPieceArr->pArr + pPieceRoots->pArr[j];
			Piece *pPieceRoot = pPiece;
			do {
				for (int32_t k = 0; k < pPiece->bufFace.size; ++k) {
					if (getIfRuvm(pPiece->pEntry, k)) {
						continue;
					}
					bool add = pPiece->add >> k & 0x1;
					if (preserve) {
						add = add && (pPiece->keepPreserve >> k & 0x1);
					}
					else {
						add = add && !(pPiece->keepPreserve >> k & 0x1);
					}
					if (add) {
						RUVM_ASSERT("loop marked add, but sort didn't touch it?",
						            pPiece->order[k] > 0);
						addBorderLoopAndVert(pArgs, pPiece, pPieceRoot, k, !preserve);
					}
				}
				pPiece = pPiece->pNext;
			} while(pPiece);
		}
		RUVM_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
}

static
void transformDeferredVert(MergeSendOffArgs *pArgs, Piece *pPiece,
                           BufMesh *pBufMesh, FaceRange *pMapFace,
                           int32_t loopLocal, V2_I32 tileMin) {
	BorderFace *pEntry = pPiece->pEntry;
	int32_t loop = pPiece->bufFace.start - loopLocal;
	int32_t vert = bufMeshGetVertIndex(pPiece, pBufMesh, loopLocal);
	V3_F32 posFlat = pBufMesh->mesh.pVerts[vert];
	float w = pBufMesh->pW[loop];
	V3_F32 projNormal = pBufMesh->pInNormal[loop];
	V3_F32 inTangent = pBufMesh->pInTangent[loop];
	float inTSign = pBufMesh->pInTSign[loop];
	Mat3x3 tbn;
	*(V3_F32 *)&tbn.d[0] = inTangent;
	*(V3_F32 *)&tbn.d[1] = _(_(projNormal V3CROSS inTangent) V3MULS inTSign);
	*(V3_F32 *)&tbn.d[2] = projNormal;
	V3_F32 pos = _(posFlat V3ADD _(projNormal V3MULS w * pArgs->wScale));
	V3_F32 normal = {0};
	V2_F32 fTileMin = {(float)tileMin.d[0], (float)tileMin.d[1]};
	bool normalTransformed = false;
	if (!getIfOnInVert(pEntry, loopLocal) && !pArgs->ppInFaceTable) {
		V3_F32 uvw;
		*(V2_F32 *)&uvw = _(pBufMesh->mesh.pUvs[loop] V2SUB fTileMin);
		uvw.d[2] = pBufMesh->pW[loop];
		RuvmMap pMap = pArgs->pMap;
		V3_F32 usgBc = {0};
		bool transformed = false;
		for (int32_t i = 0; i < pMapFace->size; ++i) {
			int32_t mapVert = pMap->mesh.mesh.pLoops[pMapFace->start + i];
			if (!pMap->mesh.pUsg) {
				continue;
			}
			int32_t usgIndex = pMap->mesh.pUsg[mapVert];
			if (!usgIndex) {
				continue;
			}
			usgIndex = abs(usgIndex) - 1;
			Usg *pUsg = pMap->usgArr.pArr + usgIndex;
			if (isPointInsideMesh(&pArgs->pContext->alloc, uvw, pUsg->pMesh)) {
				bool flatCutoff = pUsg->pFlatCutoff &&
					isPointInsideMesh(&pArgs->pContext->alloc, uvw, pUsg->pFlatCutoff);
				bool inside = sampleUsg(i, uvw, &posFlat, &transformed,
				                        &usgBc, *pMapFace, pMap, pEntry->baseFace,
				                        pArgs->pInMesh, &normal, fTileMin, flatCutoff, true,
				                        &tbn);
				if (inside) {
					pos = _(posFlat V3ADD _(normal V3MULS w * pArgs->wScale));
					if (transformed) {
						normalTransformed = true;
						normal = _(pBufMesh->mesh.pNormals[loop] V3MULM3X3 &tbn);
					}
					break;
				}
			}
		}
	}
	if (!normalTransformed) {
		normal = _(pBufMesh->mesh.pNormals[loop] V3MULM3X3 &tbn);
	}
	pBufMesh->mesh.pVerts[vert] = pos;
	pBufMesh->mesh.pNormals[loop] = normal;
}

static
void transformDefferedLoops(MergeSendOffArgs *pArgs,
                            FaceRange *pMapFace, Piece *pPiece) {
	do {
		BufMesh *pBufMesh = &pArgs->pJobArgs[pPiece->pEntry->job].bufMesh;
		V2_I32 tileMin = getTileMinFromBoundsEntry(pPiece->pEntry);
		for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
			if (getIfRuvm(pPiece->pEntry, i) || !(pPiece->add >> i & 0x1)) {
				continue;
			}
			transformDeferredVert(pArgs, pPiece, pBufMesh, pMapFace,
			                      i, tileMin);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void invertWind(Piece *pPiece, int32_t count) {
	count++;
	do {
		for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
			if (!(pPiece->add >> i & 0x1)) {
				continue;
			}
			pPiece->order[i] = count - pPiece->order[i];
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void createAndJoinPieces(MergeSendOffArgs *pArgs) {
	CLOCK_INIT;
	RuvmContext pContext = pArgs->pContext;
	RuvmAlloc *pAlloc = &pContext->alloc;
	uint64_t timeSpent[7] = {0};
	int32_t count = pArgs->entriesEnd - pArgs->entriesStart;
	pArgs->pPieceArrTable = pAlloc->pCalloc(count, sizeof(PieceArr));
	pArgs->pPieceRootTable = pAlloc->pCalloc(count, sizeof(PieceRootsArr));
	pArgs->pTotalVertTable = pAlloc->pCalloc(count, sizeof(int32_t));
	pArgs->pInVertKeep = pAlloc->pCalloc(pArgs->pInMesh->mesh.vertCount, 1);
	for (int32_t i = 0; i < count; ++i) {
		int32_t reali = pArgs->entriesStart + i;
		CLOCK_START;
		BorderFace *pEntry = pArgs->pBorderTable->ppTable[reali];
		int32_t entryCount = 0;
		compileEntryInfo(pEntry, &entryCount);
		RUVM_ASSERT("", entryCount);
		//int32_t seamFace = ;
		FaceRange ruvmFace =
			getFaceRange(&pArgs->pMap->mesh.mesh, pEntry->faceIndex, false);
		RUVM_ASSERT("", ruvmFace.size <= 6);
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		pPieceRoots->count = 0;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		pPieceArr->count = entryCount;
		pPieceArr->pArr = NULL;
		SharedEdgeWrap *pSharedEdges = NULL;
		int32_t edgeTableSize = 0;
		CLOCK_STOP_NO_PRINT;
		timeSpent[0] += CLOCK_TIME_DIFF(start, stop);
		CLOCK_START;
		int32_t totalVerts = 0;
		splitIntoPieces(pArgs, pPieceRoots, pEntry, &pSharedEdges, &edgeTableSize,
			pPieceArr, &totalVerts);
		RUVM_ASSERT("", pPieceRoots->count > 0);
		int32_t aproxVertsPerPiece = totalVerts / pPieceRoots->count;
		RUVM_ASSERT("", aproxVertsPerPiece != 0);
		for (int32_t j = 0; j < pPieceRoots->count; ++j) {
			Piece *pPiece = pPieceArr->pArr + pPieceRoots->pArr[j];
			RUVM_ASSERT("", pPiece->pEntry);
			bool seamFace = determineIfSeamFace(pArgs->pMap, pPiece);
			markKeepInVertsPreserve(pArgs, pArgs->pMap, pPiece);
			sortLoops(pArgs, pPiece, pPieceArr, pSharedEdges, edgeTableSize, &totalVerts);
			if (!pPiece->pEntry->inOrient) {
				invertWind(pPiece, totalVerts);
			}
			transformDefferedLoops(pArgs, &ruvmFace, pPiece);
			if (totalVerts > pArgs->totalVerts) {
				pArgs->totalVerts = totalVerts;
			}
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
	}
	CLOCK_STOP_NO_PRINT;
	timeSpent[1] += CLOCK_TIME_DIFF(start, stop);
}

static
void mergeAndCopyEdgeFaces(void *pArgsVoid) {
	MergeSendOffArgs *pArgs = pArgsVoid;
	RuvmContext pContext = pArgs->pContext;
	void *pThreadPoolHandle = pContext->pThreadPoolHandle;
	RuvmThreadPool *pThreadPool = &pArgs->pContext->threadPool;
	createAndJoinPieces(pArgs);
	bool barrierRet;
	barrierRet = pThreadPool->pBarrierWait(pThreadPoolHandle, pArgs->pBarrier);
	if (barrierRet) {
		for (int32_t i = 0; i < pContext->threadCount; ++i) {
			mergeIntersectionLoops(pArgs->pArgArr + i, false);
			mergeIntersectionLoops(pArgs->pArgArr + i, true);
		}
	}
	barrierRet = pThreadPool->pBarrierWait(pThreadPoolHandle, pArgs->pBarrier);
	if (barrierRet) {
		for (int32_t i = 0; i < pContext->threadCount; ++i) {
			addToOutMesh(pArgs->pArgArr + i);
		}
	}
	pContext->alloc.pFree(pArgs->pInVertKeep);
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
                      InFaceArr **ppInFaceTable, float wScale, Mesh *pInMesh, void *pBarrier) {
	int32_t entriesPerJob = pBorderTable->count / pContext->threadCount;
	void *jobArgPtrs[MAX_THREADS];
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		int32_t entriesStart = entriesPerJob * i;
		int32_t entriesEnd = i == pContext->threadCount - 1 ?
			pBorderTable->count : entriesStart + entriesPerJob;
		//TODO make a struct for these common variables, like pContext,
		//pMap, pEdgeVerts, etc, so you don't need to move them
		//around manually like this.
		pMergeJobArgs[i].pArgArr = pMergeJobArgs;
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
		pMergeJobArgs[i].pBarrier = pBarrier;
		pMergeJobArgs[i].pMutex = pMutex;
		pMergeJobArgs[i].wScale = wScale;
		pMergeJobArgs[i].pInMesh = pInMesh;
		pMergeJobArgs[i].totalVerts = 4;
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
	int32_t fence = 0;
	void *pMutex = NULL;
	void *pBarrier = NULL;
	pContext->threadPool.pMutexGet(pContext->pThreadPoolHandle, &pMutex);
	pContext->threadPool.pBarrierGet(pContext->pThreadPoolHandle, &pBarrier);
	sendOffMergeJobs(pContext, &borderTable, mergeJobArgs, pMap, pMeshOut,
	                 pJobArgs, pEdgeVerts, pVertSeamTable, &cTables, pJobBases,
					 &jobsCompleted, pMutex, pEdgeSeamTable, ppInFaceTable,
	                 wScale, pInMesh, pBarrier);
	waitForJobs(pContext, &jobsCompleted, pMutex);
	pContext->threadPool.pMutexDestroy(pContext->pThreadPoolHandle, pMutex);
	pContext->threadPool.pBarrierDestroy(pContext->pThreadPoolHandle, pBarrier);
	pContext->alloc.pFree(borderTable.ppTable);
	destroyCombineTables(&pContext->alloc, &cTables);
}
