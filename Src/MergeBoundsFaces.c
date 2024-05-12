#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include <CombineJobMeshes.h>
#include <RUVM.h>
#include <MapFile.h>
#include <Mesh.h>
#include <Context.h>
#include <Clock.h>
#include <MathUtils.h>
#include <Utils.h>
#include <AttribUtils.h>

typedef struct {
	int32_t edge;
	int32_t loops[0];
} PreserveBuf;

typedef struct SharedEdge {
	struct SharedEdge *pNext;
	void *pLast;
	int32_t entries[2];
	int32_t refIndex[2];
	int32_t edge;
	int8_t receive;
	uint8_t checked : 1;
	uint8_t preserve : 1;
	uint8_t index : 1;
	uint8_t altIndex : 1;
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

typedef struct {
	BorderFace **ppTable;
	int32_t count;
} CompiledBorderTable;

static
void initSharedEdgeEntry(SharedEdge *pEntry, int32_t baseEdge, int32_t entryIndex,
                         int32_t refIndex, int32_t isPreserve, int32_t isReceive,
						 Piece *pPiece, int32_t i) {
	//TODO do you need to add one to baseEdge?
	pEntry->edge = baseEdge + 1;
	pEntry->entries[0] = entryIndex;
	pEntry->refIndex[0] = refIndex;
	pEntry->receive = isReceive;
	pEntry->preserve = isPreserve;
	if (isReceive && refIndex >= 0) {
		pPiece->pKeep |= 1 << i;
	}
}

static
void addEntryToSharedEdgeTable(uint64_t *pTimeSpent, RuvmContext pContext,
                               BorderFace *pEntry, SharedEdgeWrap *pSharedEdges,
							   Piece *pEntries, int32_t tableSize,
							   int32_t entryIndex, SendOffArgs *pJobArgs,
							   EdgeVerts *pEdgeVerts, RuvmMap pMap) {
	//CLOCK_INIT;
	Piece *pPiece = pEntries + entryIndex;
	BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
	int32_t faceStart = pBufMesh->mesh.pFaces[pEntry->face];
	int32_t faceEnd = pBufMesh->mesh.pFaces[pEntry->face - 1];
	int32_t loopAmount = faceStart - faceEnd;
	for (int32_t i = 0; i < loopAmount; ++i) {
		//CLOCK_START;
		//int32_t vert = pBufMesh->mesh.pLoops[faceStart - i];
		int32_t isRuvm = pEntry->isRuvm >> i & 1;
		if (isRuvm) {
			//ruvm loop - skip
			continue;
		}
		//CLOCK_STOP_NO_PRINT;
		//pTimeSpent[1] += //CLOCK_TIME_DIFF(start, stop);
		int32_t baseLoopLocal = pEntry->baseLoop >> i * 2 & 3;
		int32_t baseFaceStart =
			pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace];
		int32_t baseLoop = baseFaceStart + baseLoopLocal;
		int32_t baseEdge = pJobArgs[pEntry->job].mesh.mesh.pEdges[baseLoop];
		int32_t *pVerts = pEdgeVerts[baseEdge].verts;
		if (pVerts[1] < 0) {
			//no adjacent vert
			continue;
		}
		//CLOCK_START;
		int32_t whichLoop = pVerts[0] == baseLoop;
		int32_t otherLoop = pVerts[whichLoop];
		int32_t baseFaceEnd =
			pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace + 1];
		int32_t baseFaceSize = baseFaceEnd - baseFaceStart;
		int32_t nextBaseLoop =
			baseFaceStart + ((baseLoopLocal + 1) % baseFaceSize);
		V2_F32 uv = pJobArgs[0].mesh.pUvs[nextBaseLoop];
		V2_F32 uvOther = pJobArgs[0].mesh.pUvs[otherLoop];
		//need to use approximate equal comparison here,
		//in case there are small gaps in uvs. Small enough
		//to likely be technical error, rather than artist authored.
		//For instance, the subdiv modifier in blender will create small
		//splits in uvs if "Keep Boundaries" is set.
		int32_t seam = !_(uv V2APROXEQL uvOther);
		if (seam) {
			continue;
		}
		//face is connected
		pEntries[entryIndex].edges[pEntries[entryIndex].edgeCount] = baseEdge;
		pEntries[entryIndex].edgeCount++;

		int8_t isPreserve = checkIfEdgeIsPreserve(&pJobArgs[0].mesh, baseEdge);
		int8_t isReceive = 0;
		//If 1, then map edge, otherwise base vert
		int32_t refIndex = 0; 
		if (isPreserve) {
			int32_t isBaseLoop = (pEntry->onLine >> i & 1) && !isRuvm;
			if (!isBaseLoop) {
				int32_t ruvmLoop = pEntry->ruvmLoop >> i * 3 & 7;
				int32_t ruvmFaceStart = pMap->mesh.mesh.pFaces[pEntry->faceIndex];
				int32_t ruvmEdge = pMap->mesh.mesh.pEdges[ruvmFaceStart + ruvmLoop];
				isReceive = checkIfEdgeIsReceive(&pMap->mesh, ruvmEdge);
				refIndex = ruvmEdge;
			}
			else {
				int32_t baseVert = pJobArgs[pEntry->job].mesh.mesh.pLoops[baseLoop];
				//addToPreserveVertTable(pContext, ppPreserveVerts, baseVert,
				//                       baseEdge, tableSize);
				isReceive = pJobArgs[0].pInVertTable[baseVert] == 1;
				//negate if base loop
				refIndex = (baseVert + 1) * -1;
			}
		}

		//CLOCK_STOP_NO_PRINT;
		//pTimeSpent[2] += //CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;
		int32_t hash = ruvmFnvHash((uint8_t *)&baseEdge, 4, tableSize);
		SharedEdgeWrap *pEdgeEntryWrap = pSharedEdges + hash;
		SharedEdge *pEdgeEntry = pEdgeEntryWrap->pEntry;
		if (!pEdgeEntry) {
			pEdgeEntry = pEdgeEntryWrap->pEntry =
				pContext->alloc.pCalloc(1, sizeof(SharedEdge));
			pEdgeEntry->pLast = pEdgeEntryWrap;
			initSharedEdgeEntry(pEdgeEntry, baseEdge, entryIndex, refIndex,
			                    isPreserve, isReceive, pPiece, i);
			continue;
		}
		do {
			if (pEdgeEntry->edge == baseEdge + 1) {
				if (pEdgeEntry->entries[pEdgeEntry->index] != entryIndex) {
					//other side of the edge
					pEdgeEntry->entries[1] = entryIndex;
					pEdgeEntry->index = 1;
				}
				if (!pEdgeEntry->altIndex &&
					isPreserve && pEdgeEntry->refIndex[0] != refIndex) {
					pEdgeEntry->refIndex[1] = refIndex;
					pEdgeEntry->receive += isReceive;
					if (isReceive && refIndex >= 0) {
						//this is done here (as well as in initSharedEdgeEntry),
						//in order to avoid duplicate loops being added later on.
						//Ie, only one loop should be marked keep per vert
						pPiece->pKeep |= 1 << i;
					}
					pEdgeEntry->altIndex = 1;
				}
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(SharedEdge));
				pEdgeEntry->pNext->pLast = pEdgeEntry;
				pEdgeEntry = pEdgeEntry->pNext;
				initSharedEdgeEntry(pEdgeEntry, baseEdge, entryIndex, refIndex,
				                    isPreserve, isReceive, pPiece, i);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
	pEntries[entryIndex].pEntry = pEntry;
	pEntries[entryIndex].entryIndex = entryIndex;
	//CLOCK_STOP_NO_PRINT;
	//pTimeSpent[3] += //CLOCK_TIME_DIFF(start, stop);
}

static
void validatePreserveEdges(RuvmContext pContext, SharedEdgeWrap *pBucket) {
	SharedEdge *pEntry = pBucket->pEntry;
	while (pEntry) {
		if (pEntry->checked || !pEntry->preserve) {
			pEntry = pEntry->pNext;
			continue;
		}
		if (pEntry->receive <= 1) {
			//edge intersects 1 or no map receive edges or base verts
			//on preserve junctions, so keep it in the list
			pEntry->checked = 1;
			pEntry = pEntry->pNext;
			continue;
		}
		//remove edge from list
		SharedEdge *pNext = pEntry->pNext;
		((SharedEdge *)pEntry->pLast)->pNext = pNext;
		if (pNext) {
			pNext->pLast = pEntry->pLast;
		}
		pContext->alloc.pFree(pEntry);
		pEntry = pNext;
	}
}

static
void combineConnectedIntoPiece(Piece *pEntries, SharedEdgeWrap *pSharedEdges,
                               int32_t tableSize, int32_t i) {
	Piece *pPiece = pEntries + i;
	Piece *pPieceTail = pPiece;
	BorderFace *pTail = pPiece->pEntry;
	pPiece->listed = 1;
	int32_t depth = 0;
	do {
		for (int32_t j = 0; j < pPiece->edgeCount; ++j) {
			int32_t edge = pPiece->edges[j];
			int32_t hash = ruvmFnvHash((uint8_t *)&edge, 4, tableSize);
			SharedEdgeWrap *pSharedEdgeWrap = pSharedEdges + hash;
			SharedEdge *pEdgeEntry = pSharedEdgeWrap->pEntry;
			void *pLastEdgeEntry = pSharedEdgeWrap;
			//TODO why is this a do-while loop, not a while-do?
			do {
				if (!pEdgeEntry) {
					//printf("Reached end of edge entry list,
					//       without finding match!\n");
					break;
				}
				if (pEdgeEntry->edge - 1 == pPiece->edges[j]) {
					int32_t whichEntry =
						pEdgeEntry->entries[0] == pPiece->entryIndex;
					int32_t otherEntryIndex = pEdgeEntry->entries[whichEntry];
					if (pEntries[otherEntryIndex].listed) {
						break;
					}
					//add entry to linked list
					//BorderFace *pTail = pPiece->pEntry;
					//while (pTail->pNext) {
					//	pTail = pTail->pNext;
					//}
					pTail->pNext = pEntries[otherEntryIndex].pEntry;
					pTail = pTail->pNext;
					//pPiece->pTail->pNext = pEntries[otherEntryIndex].pEntry;
					//pPiece->pTail = pPiece->pTail->pNext;
					//add piece to piece linked list
					//Piece *pPieceTail = pPiece;
					//while (pPieceTail->pNext) {
					//	pPieceTail = pPieceTail->pNext;
					//}
					pPieceTail->pNext = pEntries + otherEntryIndex;
					pPieceTail = pPieceTail->pNext;
					pEntries[otherEntryIndex].listed = 1;
					//removeEdgeEntryFromList(pContext, pEdgeEntry,
					//                        pLastEdgeEntry);
					break;
				}
				pLastEdgeEntry = pEdgeEntry;
				pEdgeEntry = pEdgeEntry->pNext;
			} while(1);
		}
		depth++;
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void splitIntoPieces(uint64_t *pTimeSpent, RuvmContext pContext, Piece **ppPieces,
                     int32_t *pPieceCount, BorderFace *pEntry,
                     SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
					 const int32_t entryCount, Piece **ppPieceArray, RuvmMap pMap) {
	//CLOCK_INIT;
	//CLOCK_START;
	int32_t tableSize = entryCount;
	SharedEdgeWrap *pSharedEdges =
		pContext->alloc.pCalloc(tableSize, sizeof(SharedEdgeWrap));
	//PreserveVert **ppPreserveVerts =
	//	pContext->alloc.pCalloc(tableSize, sizeof(PreserveVert *));
	Piece *pEntries = pContext->alloc.pCalloc(entryCount, sizeof(Piece));
	//Piece **ppTailCache = pContext->alloc.pMalloc(sizeof(void *) * entryCount);
	//Piece **ppTailCache;
	*ppPieceArray = pEntries;
	int32_t entryIndex = 0;
	//CLOCK_START;
	do {
		addEntryToSharedEdgeTable(pTimeSpent, pContext, pEntry, pSharedEdges,
		                          pEntries, tableSize, entryIndex, pJobArgs,
								  pEdgeVerts, pMap);
		entryIndex++;
		BorderFace *pNextEntry = pEntry->pNext;
		pEntry->pNext = NULL;
		pEntry = pNextEntry;
	} while(pEntry);
	
	//CLOCK_STOP_NO_PRINT;
	////pTimeSpent[2] += CLOCK_TIME_DIFF(start, stop);
	//CLOCK_START;
	if (entryCount == 1) {
		ppPieces[0] = pEntries;
		++*pPieceCount;
	}
	else {
		//check if preserve inMesh edge intersects at least 2
		//map receiver edges. Edge is only preserved if this is so.
		for (int32_t i = 0; i < tableSize; ++i) {
			SharedEdgeWrap *pBucket = pSharedEdges + i;
			validatePreserveEdges(pContext, pBucket);
		}
		//now link together connected entries
		for (int32_t i = 0; i < entryCount; ++i) {
			if (pEntries[i].listed) {
				continue;
			}
			combineConnectedIntoPiece(pEntries, pSharedEdges, tableSize, i);
			if (pEntries[i].pEntry) {
				ppPieces[*pPieceCount] = pEntries + i;
				++*pPieceCount;
			}
		}
	}
	//CLOCK_STOP_NO_PRINT;
	//pTimeSpent[3] += CLOCK_TIME_DIFF(start, stop);
	//CLOCK_START;
	/*
	for (int32_t i = 0; i < tableSize; ++i) {
		PreserveVert *pVertEntry = ppPreserveVerts[i];
		while(pVertEntry) {
			PreserveVert *pNext = pVertEntry->pNext;
			pContext->alloc.pFree(pVertEntry);
			pVertEntry = pNext;
		}
	}
	pContext->alloc.pFree(ppPreserveVerts);
	*/
	//TODO turn this into a general tableFree function or such maybe?
	for (int32_t i = 0; i < tableSize; ++i) {
		SharedEdge *pEdgeEntry = pSharedEdges[i].pEntry;
		while(pEdgeEntry) {
			SharedEdge *pNext = pEdgeEntry->pNext;
			pContext->alloc.pFree(pEdgeEntry);
			pEdgeEntry = pNext;
		}
	}
	pContext->alloc.pFree(pSharedEdges);
	//CLOCK_STOP_NO_PRINT;
	//pTimeSpent[4] += CLOCK_TIME_DIFF(start, stop);
}

static
void compileEntryInfo(BorderFace *pEntry, int32_t *pCount, int32_t *pIsSeam,
                      int32_t *pHasPreservedEdge) {
	*pCount = 0;
	*pIsSeam = 0;
	*pHasPreservedEdge = 0;
	while (pEntry) {
		++*pCount;
		*pIsSeam |= pEntry->seam;
		*pHasPreservedEdge |= pEntry->hasPreservedEdge;
		pEntry = pEntry->pNext;
	}
}

static
void mergeAndCopyEdgeFaces(RuvmContext pContext, CombineTables *pCTables,
                           RuvmMap pMap, Mesh *pMeshOut, SendOffArgs *pJobArgs,
						   CompiledBorderTable *pBorderTable,
						   EdgeVerts *pEdgeVerts, JobBases *pJobBases,
						   int8_t *pVertSeamTable) {
	uint64_t timeSpent[7] = {0};
	for (int32_t i = 0; i < pBorderTable->count; ++i) {
		CLOCK_INIT;
		CLOCK_START;
		BorderFace *pEntry = pBorderTable->ppTable[i];
		int32_t entryCount, isSeam, hasPreservedEdge;
		compileEntryInfo(pEntry, &entryCount, &isSeam, &hasPreservedEdge);
		//int32_t seamFace = ;
		FaceInfo ruvmFace;
		ruvmFace.index = pEntry->faceIndex;
		ruvmFace.start = pMap->mesh.mesh.pFaces[pEntry->faceIndex];
		ruvmFace.end = pMap->mesh.mesh.pFaces[pEntry->faceIndex + 1];
		ruvmFace.size = ruvmFace.end - ruvmFace.start;
		Piece **ppPieces = pContext->alloc.pMalloc(sizeof(Piece) * entryCount);
		Piece *pPieceArray = NULL;
		int32_t pieceCount;
		pieceCount = 0;
		CLOCK_STOP_NO_PRINT;
		timeSpent[0] += CLOCK_TIME_DIFF(start, stop);
		CLOCK_START;
		splitIntoPieces(timeSpent, pContext, ppPieces, &pieceCount, pEntry,
		                pJobArgs, pEdgeVerts, entryCount, &pPieceArray, pMap);
		CLOCK_STOP_NO_PRINT;
		timeSpent[1] += CLOCK_TIME_DIFF(start, stop);
		for (int32_t j = 0; j < pieceCount; ++j) {
			ruvmMergeSingleBorderFace(timeSpent, pContext, pMap, pMeshOut,
			                          pJobArgs, ppPieces[j], pCTables, pJobBases,
									  pVertSeamTable, &ruvmFace);
		}
		CLOCK_START;
		if (ppPieces) {
			pContext->alloc.pFree(ppPieces);
		}
		if (pPieceArray) {
			pContext->alloc.pFree(pPieceArray);
		}
		CLOCK_STOP_NO_PRINT;
		timeSpent[6] += CLOCK_TIME_DIFF(start, stop);
	}
	printf("Combine time breakdown: \n");
	for(int32_t i = 0; i < 7; ++i) {
		printf("	%lu\n", timeSpent[i]);
	}
	printf("\n");
}

static
void linkEntriesFromOtherJobs(RuvmContext pContext, SendOffArgs *pJobArgs,
                              BorderBucket *pBucket, int32_t faceIndex,
							  int32_t hash, int32_t job) {
	for (int32_t j = job + 1; j < pContext->threadCount; ++j) {
		BorderBucket *pBucketOther = pJobArgs[j].borderTable.pTable + hash;
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
			BorderBucket *pBucket = pJobArgs[i].borderTable.pTable + hash;
			int32_t depth = 0;
			do {
				if (pBucket->pEntry) {
					int32_t faceIndex = pBucket->pEntry->faceIndex;
					linkEntriesFromOtherJobs(pContext, pJobArgs, pBucket,
					                         faceIndex, hash, i);
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
		}
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

void ruvmMergeBorderFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
					      JobBases *pJobBases, int8_t *pVertSeamTable) {
	int32_t totalBorderFaces = 0;
	int32_t totalBorderEdges = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		totalBorderFaces += pJobArgs[i].totalBorderFaces;
		totalBorderEdges += pJobArgs[i].totalBorderEdges;
	}
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
		}
		pJobArgs[0].pInVertTable[i] = preserve;
	}
	mergeAndCopyEdgeFaces(pContext, &cTables, pMap, pMeshOut, pJobArgs,
	                      &borderTable, pEdgeVerts, pJobBases, pVertSeamTable);
	
	pContext->alloc.pFree(borderTable.ppTable);
	destroyCombineTables(&pContext->alloc, &cTables);
}
