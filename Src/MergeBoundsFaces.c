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
#include <ThreadPool.h>
#include <Error.h>

typedef struct SharedEdge {
	struct SharedEdge *pNext;
	void *pLast;
	int32_t entries[2];
	int32_t refIndex[2];
	int32_t edge;
	int16_t loop[2];
	int8_t receive;
	_Bool checked : 1;
	_Bool preserve : 1;
	_Bool index : 1;
	_Bool altIndex : 1;
	_Bool seam : 1;
	_Bool removed : 1;
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

int32_t determineIfSeamFace(RuvmMap pMap, BorderFace *pEntry) {
	int32_t faceIndex = pEntry->faceIndex;
	int32_t ruvmLoops = 0;
	do {
		for (int32_t i = 0; i < 11; ++i) {
			ruvmLoops += getIfRuvm(pEntry, i);
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
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
void addLoopToLocalEdgeTable(MergeSendOffArgs *pArgs, FaceRange *pRuvmFace, int32_t tableSize,
                             BorderVert *localEdgeTable, BorderFace *pEntry,
							 int32_t faceStart, int32_t k, Piece *pPiece) {
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
		                        k, faceStart);
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
						 pEdgeEntry->tile == pEntry->tile &&
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
					                        k, faceStart);
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
	if (refIndex >= 0 && isReceive) {
		pPiece->keepPreserve |= 1 << i;
	}
}

static
void addEntryToSharedEdgeTable(MergeSendOffArgs *pArgs, BorderFace *pEntry,
                               SharedEdgeWrap *pSharedEdges, Piece *pEntries,
							   int32_t tableSize, int32_t entryIndex,
							   int32_t *pTotalVerts) {
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
		_Bool isRuvm = getIfRuvm(pEntry, i);
		if (isRuvm) {
			//ruvm loop - skip
			continue;
		}
		//CLOCK_STOP_NO_PRINT;
		//pTimeSpent[1] += //CLOCK_TIME_DIFF(start, stop);
		//Get in mesh details for current buf loop
		BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, i);
		int32_t *pVerts = pArgs->pEdgeVerts[inInfo.edge].verts;
		RUVM_ASSERT("", pVerts && (pVerts[0] == inInfo.loop || pVerts[1] == inInfo.loop));
		if (pVerts[1] < 0) {
			//no other vert on edge
			continue;
		}
		int32_t lasti = i ? i - 1 : face.size - 1;
		if ((pEntry->baseLoop >> i * 2 & 0x03) ==
			(pEntry->baseLoop >> lasti * 2 & 0x03) &&
			!getIfRuvm(pEntry, lasti)) {
			//Edge belongs to last loop, not this one
			continue;
		}
		_Bool isOnInVert = getIfOnInVert(pEntry, i);
		_Bool baseKeep;
		if (isOnInVert) {
			RUVM_ASSERT("", pArgs->pJobArgs[0].pInVertTable[inInfo.vert] >= 0); //pInVertTable is 0 .. 3
			RUVM_ASSERT("", pArgs->pJobArgs[0].pInVertTable[inInfo.vert] <= 3); 
			baseKeep = pArgs->pJobArgs[0].pInVertTable[inInfo.vert] > 2;
			pPiece->keepOnInVert |= baseKeep << i;
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
		/*
		int32_t whichLoop = pVerts[0] == inInfo.loop;
		int32_t otherLoop = pVerts[whichLoop];
		int32_t baseFaceEnd =
			pArgs->pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace + 1];
		int32_t baseFaceSize = baseFaceEnd - inInfo.start;
		RUVM_ASSERT("", baseFaceSize > 1 && baseFaceSize < 10000);
		RUVM_ASSERT("", inInfo.start +
		       baseFaceSize <= pArgs->pJobArgs[0].mesh.mesh.loopCount);
		int32_t nextBaseLoop =
			inInfo.start + ((inInfo.loopLocal + 1) % baseFaceSize);
		V2_F32 uv = pArgs->pJobArgs[0].mesh.pUvs[nextBaseLoop];
		V2_F32 uvOther = pArgs->pJobArgs[0].mesh.pUvs[otherLoop];
		RUVM_ASSERT("", v2IsFinite(uv) && v2IsFinite(uvOther));
		//need to use approximate equal comparison here,
		//in case there are small gaps in uvs. Small enough
		//to likely be technical error, rather than artist authored.
		//For instance, the subdiv modifier in blender will create small
		//splits in uvs if "Keep Boundaries" is set.
		
		_Bool seam = !_(uv V2APROXEQL uvOther);
		if (seam) {
			//continue;
		}
		*/
		bool seam = pArgs->pEdgeSeamTable[inInfo.edge];
		//face is connected
		pEntries[entryIndex].edges[pEntries[entryIndex].edgeCount] = inInfo.edge;
		pEntries[entryIndex].edgeCount++;

		_Bool isPreserve =
			checkIfEdgeIsPreserve(&pArgs->pJobArgs[0].mesh, inInfo.edge);
		_Bool isReceive = false;
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
			pEntry->checked = true;
			pEntry = pEntry->pNext;
			continue;
		}
		//intersects 2 or more receive, so mark as a seam
		pEntry->seam = true;
		pEntry = pEntry->pNext;
	}
}

static
void combineConnectedIntoPiece(Piece *pEntries, SharedEdgeWrap *pSharedEdges,
                               int32_t tableSize, int32_t i) {
	Piece *pPiece = pEntries + i;
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
						Piece *pPieceA = pEntries + pEdgeEntry->entries[whichLoop];
						pPieceA->keepSeam |= 1 << loopA;

						int32_t loopB = pEdgeEntry->loop[!whichLoop];
						Piece *pPieceB = pEntries + pEdgeEntry->entries[!whichLoop];
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
					if (pEntries[otherEntryIndex].listed) {
						break;
					}
					//add entry to linked list
					pTail->pNext = pEntries[otherEntryIndex].pEntry;
					pTail = pTail->pNext;
					//add piece to piece linked list
					pPieceTail->pNext = pEntries + otherEntryIndex;
					pPieceTail = pPieceTail->pNext;
					pEntries[otherEntryIndex].listed = 1;
					break;
				}
				pEdgeEntry = pEdgeEntry->pNext;
			};
			RUVM_ASSERT("", j < pPiece->edgeCount);
		}
		depth++;
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
                         int32_t *pLoop) {
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
				return pNeighbour;
			}
		}
		pEdgeEntry = pEdgeEntry->pNext;
	}
	return NULL;
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
void splitIntoPieces(MergeSendOffArgs *pArgs, PieceRootsArr *pPieceRoots,
                     BorderFace *pEntry, SharedEdgeWrap **ppSharedEdges,
					 int32_t *pEdgeTableSize, PieceArr *pPieceArr, int32_t *pTotalVerts) {
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
	int32_t entryIndex = 0;
	//CLOCK_START;
	do {
		//If there's only 1 border face entry, then this function will just
		//initialize the Piece.
		addEntryToSharedEdgeTable(pArgs, pEntry, *ppSharedEdges, pEntries, *pEdgeTableSize,
		                          entryIndex, pTotalVerts);
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
		//check if preserve inMesh edge intersects at least 2
		//map receiver edges. Edge is only preserved if this is so.
		for (int32_t i = 0; i < entryCount; ++i) {
			SharedEdgeWrap *pBucket = *ppSharedEdges + i;
			validatePreserveEdges(pArgs->pContext, pBucket);
			RUVM_ASSERT("", i < entryCount);
		}
		//now link together connected entries
		for (int32_t i = 0; i < entryCount; ++i) {
			if (pEntries[i].listed) {
				continue;
			}
			combineConnectedIntoPiece(pEntries, *ppSharedEdges, *pEdgeTableSize, i);
			if (pEntries[i].pEntry) {
				pPieceRoots->pArr[pPieceRoots->count] = i;
				++pPieceRoots->count;
			}
			RUVM_ASSERT("", !pPieceRoots->count ||
			                pPieceArr->pArr[pPieceRoots->count - 1].pEntry);
			RUVM_ASSERT("", i < entryCount);
		}
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
		pPieceRoot->triangulate = true;
	}
	return skip;
}

static
void determineLoopsToSkip(Piece* pPiece) {
	Piece* pPieceRoot = pPiece;
	do {
		for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
			if (getIfRuvm(pPiece->pEntry, i)) {
				continue;
			}
			bool skip = checkIfShouldSkip(pPieceRoot, pPiece, i);
			pPiece->skip |= skip << i;
		}
		pPiece = pPiece->pNext;
	} while (pPiece);
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
               SharedEdgeWrap* pEdgeTable, int32_t edgeTableSize) {
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
		if (!(pPiece->skip >> loop & 0x01)) {
			pPiece->order[loop] = sort;
			sort++;
		}
		else {
			pPiece->order[loop] = 1;
		}
		//Set next loop
		if (getIfRuvm(pPiece->pEntry, loop)) {
			loop++;
			continue;
		}
		Piece *pOtherPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
		                                       pPiece, pPieceRoot, &loop);
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
			_Bool seamFace = determineIfSeamFace(pArgs->pMap, pPiece->pEntry);
			if (seamFace) {
				determineLoopsToKeep(pArgs, pPieceArr, &ruvmFace, pPiece, aproxVertsPerPiece);
			}
			determineLoopsToSkip(pPiece);
			sortLoops(pArgs, pPiece, pPieceArr, pSharedEdges, edgeTableSize);
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
			int32_t job = pPieceArr->pArr[pPieceRoots->pArr[j]].pEntry->job;
			RUVM_ASSERT("", job >= 0 && job < pContext->threadCount);
			ruvmMergeSingleBorderFace(pArgs, timeSpent, pPieceRoots->pArr[j], pPieceArr,
									  &ruvmFace, &mergeBufHandles);
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
					  int32_t *pJobsCompleted, void *pMutex, bool *pEdgeSeamTable) {
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
		pMergeJobArgs[i].pJobArgs = pMapJobArgs;
		pMergeJobArgs[i].pEdgeVerts = pEdgeVerts;
		pMergeJobArgs[i].pVertSeamTable = pVertSeamTable;
		pMergeJobArgs[i].pEdgeSeamTable = pEdgeSeamTable;
		pMergeJobArgs[i].pJobBases = pJobBases;
		pMergeJobArgs[i].pCTables = pCTables;
		pMergeJobArgs[i].job = i;
		pMergeJobArgs[i].pJobsCompleted = pJobsCompleted;
		pMergeJobArgs[i].pMutex = pMutex;
		jobArgPtrs[i] = pMergeJobArgs + i;
	}
	pContext->threadPool.pJobStackPushJobs(pContext->pThreadPoolHandle,
	                                       pContext->threadCount,
										   mergeAndCopyEdgeFaces, jobArgPtrs);
}

void ruvmMergeBorderFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
					      JobBases *pJobBases, int8_t *pVertSeamTable,
                          bool *pEdgeSeamTable) {
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
					 &jobsCompleted, pMutex, pEdgeSeamTable);
	waitForJobs(pContext, &jobsCompleted, pMutex);
	pContext->threadPool.pMutexDestroy(pContext->pThreadPoolHandle, pMutex);
	pContext->alloc.pFree(borderTable.ppTable);
	destroyCombineTables(&pContext->alloc, &cTables);
}
