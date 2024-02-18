#include <math.h>
#include <float.h>
#include <stdio.h>

#include <RUVM.h>
#include <MapFile.h>
#include <Mesh.h>
#include <Context.h>

typedef struct {
	int32_t edge;
	int32_t loops[0];
} PreserveBuf;

typedef struct SharedEdge {
	struct SharedEdge *pNext;
	int32_t edge;
	int32_t entries[2];
} SharedEdge;

typedef struct Piece {
	struct Piece *pNext;
	BoundaryVert *pEntry;
	int32_t listed;
	int32_t edgeCount;
	int32_t edges[8];
	int32_t entryIndex;
} Piece;

typedef struct {
	BoundaryVert *pEntry;
	int32_t seamFace;
	int32_t infoBufSize;
	int32_t loopBufferSize;
	int32_t loopBuffer[8];
	Vec3 normalBuffer[8];
	Vec2 uvBuffer[8];
	int32_t ruvmIndicesSort[8];
	int32_t seamLoops[8];
	int32_t seamLoopCount;
	FaceInfo ruvmFace;
	Vec2 centre;
} AddFaceVars;

static void addEntryToSharedEdgeTable(RuvmContext pContext, BoundaryVert *pEntry,
                                      SharedEdge *pSharedEdges, Piece *pEntries,
									  int32_t tableSize, int32_t entryIndex,
                                      SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts) {
	WorkMesh *localMesh = &pJobArgs[pEntry->job].localMesh;
	int32_t faceStart = localMesh->pFaces[pEntry->face];
	int32_t faceEnd = localMesh->pFaces[pEntry->face - 1];
	int32_t loopAmount = faceStart - faceEnd;
	for (int32_t i = 0; i < loopAmount; ++i) {
		int32_t vert = localMesh->pLoops[faceStart - i];
		int32_t baseLoopLocal = pEntry->baseLoops[i] >> 4;
		if (vert < localMesh->vertCount) {
			//ruvm loop - skip
			continue;
		}
		int32_t baseLoopLocalAbs = baseLoopLocal < 0 ? baseLoopLocal * -1 : baseLoopLocal;
		int32_t baseFaceStart = pJobArgs[pEntry->job].mesh.pFaces[pEntry->baseFace];
		int32_t baseFaceEnd = pJobArgs[pEntry->job].mesh.pFaces[pEntry->baseFace + 1];
		int32_t baseFaceSize = baseFaceEnd - baseFaceStart;
		//-1 cause negative
		int32_t baseLoop = baseFaceStart + baseLoopLocalAbs - 1;
		int32_t nextBaseLoop = baseFaceStart + (baseLoopLocalAbs % baseFaceSize);
		int32_t baseEdge = pJobArgs[pEntry->job].mesh.pEdges[baseLoop];
		if (pJobArgs[0].mesh.pEdgePreserve[baseEdge]) {
			//preserved edge
			continue;
		}
		int32_t *pVerts = pEdgeVerts[baseEdge].verts;
		if (pVerts[1] < 0) {
			//no adjacent vert
			continue;
		}
		int32_t whichLoop = pVerts[0] == baseLoop;
		int32_t otherLoop = pVerts[whichLoop];
		Vec2 uv = pJobArgs[0].mesh.pUvs[nextBaseLoop];
		Vec2 uvOther = pJobArgs[0].mesh.pUvs[otherLoop];
		int32_t seam = _(uv V2NOTEQL uvOther);
		if (seam) {
			continue;
		}
		//face is connected
		
		pEntries[entryIndex].edges[pEntries[entryIndex].edgeCount] = baseEdge;
		pEntries[entryIndex].edgeCount++;

		int32_t hash = ruvmFnvHash((uint8_t *)&baseEdge, 4, tableSize);
		SharedEdge *pEdgeEntry = pSharedEdges + hash;
		if (!pEdgeEntry->edge) {
			pEdgeEntry->edge = baseEdge + 1;
			pEdgeEntry->entries[0] = entryIndex;
			continue;
		}
		do {
			if (pEdgeEntry->edge == baseEdge + 1) {
				if (pEdgeEntry->entries[0] != entryIndex) {
					pEdgeEntry->entries[1] = entryIndex;
				}
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry = pEdgeEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(SharedEdge));
				pEdgeEntry->edge = baseEdge + 1;
				pEdgeEntry->entries[0] = entryIndex;
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
	pEntries[entryIndex].pEntry = pEntry;
	pEntries[entryIndex].entryIndex = entryIndex;
}

static void combineConnectedIntoPiece(Piece *pEntries, SharedEdge *pSharedEdges,
                                      int32_t tableSize, int32_t i) {
	Piece *pPiece = pEntries + i;
	pPiece->listed = 1;
	do {
		for (int32_t j = 0; j < pPiece->edgeCount; ++j) {
			int32_t hash = ruvmFnvHash((uint8_t *)&pPiece->edges[j], 4, tableSize);
			SharedEdge *pEdgeEntry = pSharedEdges + hash;
			do {
				if (pEdgeEntry->edge - 1 == pPiece->edges[j]) {
					int32_t whichEntry = pEdgeEntry->entries[0] == pPiece->entryIndex;
					int32_t otherEntryIndex = pEdgeEntry->entries[whichEntry];
					if (pEntries[otherEntryIndex].listed) {
						break;
					}
					//add entry to linked list
					BoundaryVert *pTail = pPiece->pEntry;
					while (pTail->pNext) {
						pTail = pTail->pNext;
					}
					pTail->pNext = pEntries[otherEntryIndex].pEntry;
					//add piece to piece linked list
					Piece *pPieceTail = pPiece;
					while (pPieceTail->pNext) {
						pPieceTail = pPieceTail->pNext;
					}
					pPieceTail->pNext = pEntries + otherEntryIndex;
					pEntries[otherEntryIndex].listed = 1;
					break;
				}
				if (!pEdgeEntry->pNext) {
					printf("Reached end of edge entry list, without finding match!\n");
					break;
				}
				pEdgeEntry = pEdgeEntry->pNext;
			} while(1);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static void splitIntoPieces(RuvmContext pContext, Piece *pPieces,
                            int32_t *pPieceCount, BoundaryVert *pEntry,
                            SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
							const int32_t entryCount, Piece **ppPieceArray) {
	int32_t tableSize = entryCount;
	SharedEdge *pSharedEdges = pContext->alloc.pCalloc(tableSize, sizeof(SharedEdge));
	Piece *pEntries = pContext->alloc.pCalloc(entryCount, sizeof(Piece));
	*ppPieceArray = pEntries;
	int32_t entryIndex = 0;
	do {
		addEntryToSharedEdgeTable(pContext, pEntry, pSharedEdges, pEntries,
		                          tableSize, entryIndex, pJobArgs, pEdgeVerts);
		entryIndex++;
		BoundaryVert *pNextEntry = pEntry->pNext;
		pEntry->pNext = NULL;
		pEntry = pNextEntry;
	} while(pEntry);
	
	if (entryCount == 1) {
		pPieces[0] = pEntries[0];
		++*pPieceCount;
	}
	else {
		for (int32_t i = 0; i < entryCount; ++i) {
			if (pEntries[i].listed) {
				continue;
			}
			combineConnectedIntoPiece(pEntries, pSharedEdges, tableSize, i);
			if (pEntries[i].pEntry) {
				pPieces[*pPieceCount] = pEntries[i];
				++*pPieceCount;
			}
		}
	}
	for (int32_t i = 0; i < tableSize; ++i) {
		SharedEdge *pEdgeEntry = pSharedEdges[i].pNext;
		while(pEdgeEntry) {
			SharedEdge *pNext = pEdgeEntry->pNext;
			pContext->alloc.pFree(pEdgeEntry);
			pEdgeEntry = pNext;
		}
	}
	pContext->alloc.pFree(pSharedEdges);
}

static void initEdgeTableEntry(EdgeTable *pEdgeEntry, BoundaryVert *pEntry, Mesh *pMeshOut,
                               WorkMesh *pLocalMesh, int32_t ruvmVert, int32_t ruvmNextVert,
                               int32_t vert, int32_t baseEdge, int32_t baseVert,
                               int32_t baseEdgeSign, int32_t loopIndex, int32_t ruvmFace) {
	pMeshOut->pVerts[pMeshOut->vertCount] =
		pLocalMesh->pVerts[vert];
	pEdgeEntry->ruvmVert = ruvmVert;
	pEdgeEntry->ruvmVertNext = ruvmNextVert;
	pEdgeEntry->vert = pMeshOut->vertCount;
	pMeshOut->vertCount++;
	pEdgeEntry->tile = pEntry->tile;
	pEdgeEntry->loops = 1;
	pEdgeEntry->baseEdge = baseEdge;
	pEdgeEntry->baseVert = baseVert;
	pEdgeEntry->baseEdgeSign = baseEdgeSign;
	pEdgeEntry->loopIndex = loopIndex;
	pEdgeEntry->ruvmFace = ruvmFace;
}

static void initLocalEdgeTableEntry(EdgeTable *pEdgeEntry, int32_t ruvmVert, int32_t ruvmNextVert,
                                    BoundaryVert *pEntry, int32_t baseEdge, int32_t baseVert,
									int32_t keepBaseLoop, int32_t loop) {
	pEdgeEntry->ruvmVert = ruvmVert;
	pEdgeEntry->ruvmVertNext = ruvmNextVert;
	pEdgeEntry->tile = pEntry->tile;
	pEdgeEntry->baseEdge = baseEdge;
	pEdgeEntry->baseVert = baseVert;
	pEdgeEntry->loops = 1;
	pEdgeEntry->vert = loop;
	pEdgeEntry->keepBaseLoop = keepBaseLoop;
	pEdgeEntry->job = pEntry->job;
}

static void addLoopToLocalEdgeTable(RuvmContext pContext, EdgeTable *localEdgeTable,
                                    AddFaceVars *pAfVars, SendOffArgs *pJobArgs,
									BoundaryVert *pEntry, RuvmMap pMap,
									int32_t faceStart, int32_t sort, int32_t k) {
	int32_t sortNext, baseLoopLocal, baseLoopLocalAbs,
			baseLoop, baseEdge, baseVert;
	sortNext = sort;
	sort -= 1;
	if (sort < 0) {
		sort = pAfVars->ruvmFace.size - 1;
	}
	baseLoopLocal = pEntry->baseLoops[k] >> 4;
	baseLoopLocalAbs = baseLoopLocal < 0 ? baseLoopLocal * -1 : baseLoopLocal;
	FaceInfo baseFace;
	baseFace.index = pEntry->baseFace;
	baseFace.start = pJobArgs[pEntry->job].mesh.pFaces[pEntry->baseFace];
	baseFace.end = pJobArgs[pEntry->job].mesh.pFaces[pEntry->baseFace + 1];
	baseFace.size = baseFace.end - baseFace.start;
	//-1 cause negative
	baseLoop = baseFace.start + baseLoopLocalAbs - 1;
	int32_t isBaseLoop = baseLoopLocal < 0;
	baseEdge = pJobArgs[pEntry->job].mesh.pEdges[baseLoop];
	baseVert = isBaseLoop ? pJobArgs[pEntry->job].mesh.pLoops[baseLoop] : -1;

	int32_t keepBaseLoop = 0;
	if (isBaseLoop) {
		if (pJobArgs[0].pVertSeamTable[baseVert] > 2 ||
			(pJobArgs[0].pVertSeamTable[baseVert] && pJobArgs[0].pInVertTable[baseVert])) {

			keepBaseLoop = 1;
		}
	}		

	int32_t hash, ruvmVert, ruvmNextVert;
	if (isBaseLoop) {
		hash = ruvmFnvHash((uint8_t *)&baseVert, 4, 16);
		ruvmVert = ruvmNextVert = -1;
	}
	else {
		ruvmVert = pMap->mesh.pLoops[pAfVars->ruvmFace.start + sort];
		ruvmNextVert = pMap->mesh.pLoops[pAfVars->ruvmFace.start + sortNext];
		uint32_t ruvmEdgeId = ruvmVert + ruvmNextVert;
		hash = ruvmFnvHash((uint8_t *)&ruvmEdgeId, 4, 16);
	}
	EdgeTable *pEdgeEntry = localEdgeTable + hash;
	if (!pEdgeEntry->loops) {
		initLocalEdgeTableEntry(pEdgeEntry, ruvmVert, ruvmNextVert,
								pEntry, baseEdge, baseVert, keepBaseLoop,
								faceStart - k);
	}
	else {
		do {
			//ideally, we'd just compare the edges, but maps don't have
			//edges currently, so I'll remove ruvmVert/ruvmVertNext,
			//and replace them with a single edge index, one edges are
			//added the ruvm maps.
			int32_t	match;
			if (isBaseLoop) {
				match = pEdgeEntry->baseVert == baseVert;
			}
			else {
				match = (pEdgeEntry->ruvmVert == ruvmVert ||
						 pEdgeEntry->ruvmVertNext == ruvmVert) &&
						(pEdgeEntry->ruvmVert == ruvmNextVert ||
						 pEdgeEntry->ruvmVertNext == ruvmNextVert) &&
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
					pContext->alloc.pCalloc(1, sizeof(EdgeTable));
					initLocalEdgeTableEntry(pEdgeEntry, ruvmVert, ruvmNextVert,
											pEntry, baseEdge, baseVert, keepBaseLoop,
											faceStart - k);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
}

static void addLoopsWithSingleVert(RuvmContext pContext, AddFaceVars *pAfVars,
                                   SendOffArgs *pJobArgs, EdgeTable *localEdgeTable, 
                                   int32_t *pTotalVerts) {
	for (int32_t i = 0; i < 16; ++i) {
		EdgeTable *pEdgeEntry = localEdgeTable + i;
		int32_t depth = 0;
		do {
			if (pEdgeEntry->loops == 1 || pEdgeEntry->keepBaseLoop) {
				WorkMesh *pLocalMesh = &pJobArgs[pEdgeEntry->job].localMesh;
				_(&pAfVars->centre V2ADDEQL pLocalMesh->pUvs[pEdgeEntry->vert]);
				++*pTotalVerts;
				pAfVars->seamLoops[pAfVars->seamLoopCount] = pEdgeEntry->vert;
				pAfVars->seamLoopCount++;
			}
			EdgeTable *pNextEdgeEntry = pEdgeEntry->pNext;
			if (depth > 0) {
				pContext->alloc.pFree(pEdgeEntry);
			}
			pEdgeEntry = pNextEdgeEntry;
			depth++;
		} while(pEdgeEntry);
	}
}

static void determineLoopsToKeep(RuvmContext pContext, RuvmMap pMap,
                                 AddFaceVars *pAfVars, SendOffArgs *pJobArgs,
                                 int32_t *pTotalVerts) {
	BoundaryVert *pEntry = pAfVars->pEntry;
	EdgeTable localEdgeTable[16] = {0};
	do {
		WorkMesh *localMesh = &pJobArgs[pEntry->job].localMesh;
		int32_t faceStart = localMesh->pFaces[pEntry->face];
		int32_t faceEnd = localMesh->pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = localMesh->pLoops[faceStart - k];
			int32_t sort = pEntry->baseLoops[k] & 15; //zero last 4 bits;
			//sort is stored in first 4 bits,
			//and base loop is stored in the last 4
			if (vert > localMesh->vertCount) {

				addLoopToLocalEdgeTable(pContext, localEdgeTable, pAfVars, pJobArgs,
				                        pEntry, pMap, faceStart, sort, k);
			}
			else {
				_(&pAfVars->centre V2ADDEQL localMesh->pUvs[faceStart - k]);
				++*pTotalVerts;
				vert += pJobArgs[pEntry->job].vertBase;
			}
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
	
	addLoopsWithSingleVert(pContext, pAfVars, pJobArgs, localEdgeTable, pTotalVerts);
}

static void addBoundaryLoopAndVert(RuvmContext pContext, RuvmMap pMap,
                                   AddFaceVars *pAfVars, int32_t *pVert,
                                   EdgeVerts *pEdgeVerts, BoundaryVert *pEntry,
								   SendOffArgs *pJobArgs, EdgeTable *pEdgeTable,
								   int32_t edgeTableSize, RuvmMesh *pMeshOut,
								   int32_t k, int32_t sort) {
	FaceInfo baseFace;
	baseFace.index = pEntry->baseFace;
	baseFace.start = pJobArgs[pEntry->job].mesh.pFaces[pEntry->baseFace];
	baseFace.end = pJobArgs[pEntry->job].mesh.pFaces[pEntry->baseFace + 1];
	baseFace.size = baseFace.end - baseFace.start;
	int32_t sortNext = sort;
	sort -= 1;
	if (sort < 0) {
		sort = pAfVars->ruvmFace.size - 1;
	}
	int32_t baseLoopLocal = pEntry->baseLoops[k] >> 4;
	int32_t isBaseLoop = baseLoopLocal < 0;
	int32_t baseLoopLocalAbs = isBaseLoop ? baseLoopLocal * -1 : baseLoopLocal;
	int32_t baseLoop = baseFace.start + baseLoopLocalAbs - 1;
	int32_t baseEdge = pJobArgs[pEntry->job].mesh.pEdges[baseLoop];
	int32_t baseVert = isBaseLoop ? pJobArgs[pEntry->job].mesh.pLoops[baseLoop] : -1;
	int32_t hash, ruvmVert, ruvmNextVert;
	WorkMesh *localMesh = &pJobArgs[pEntry->job].localMesh;
	if (isBaseLoop) {
		hash = ruvmFnvHash((uint8_t *)&baseVert, 4, edgeTableSize);
		ruvmVert = ruvmNextVert = -1;
	}
	else {
		ruvmVert = pMap->mesh.pLoops[pAfVars->ruvmFace.start + sort];
		ruvmNextVert = pMap->mesh.pLoops[pAfVars->ruvmFace.start + sortNext];
		uint32_t ruvmEdgeId = ruvmVert + ruvmNextVert;
		hash = ruvmFnvHash((uint8_t *)&ruvmEdgeId, 4, edgeTableSize);
	}
	int32_t baseEdgeSign = 0;
	if (!isBaseLoop && 
		checkIfEdgeIsSeam(baseEdge, baseFace, baseLoopLocalAbs - 1,
						  &pJobArgs[pEntry->job].mesh, pEdgeVerts)) {
		int32_t *pVerts = pJobArgs[0].pEdgeVerts[baseEdge].verts;
		baseEdgeSign = vec2WindingCompare(pJobArgs[0].mesh.pUvs[pVerts[0]],
										  pJobArgs[0].mesh.pUvs[pVerts[1]],
										  pAfVars->centre, 1);
	}
	EdgeTable *pEdgeEntry = pEdgeTable + hash;
	if (!pEdgeEntry->loops) {
		initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, localMesh,
						   ruvmVert, ruvmNextVert, *pVert, baseEdge,
						   baseVert, baseEdgeSign, baseLoop,
						   pEntry->faceIndex);
		*pVert = pEdgeEntry->vert;
	}
	else {
		do {
			//TODO ideally, we'd just compare the edges, but maps don't have
			//edges currently, so I'll remove ruvmVert/ruvmVertNext,
			//and replace them with a single edge index, once edges are
			//added the ruvm maps.
			int32_t match;
			if (isBaseLoop) {
				match = pEdgeEntry->baseVert == baseVert &&
						pEdgeEntry->ruvmFace == pEntry->faceIndex &&
						pJobArgs[0].mesh.pUvs[pEdgeEntry->loopIndex].x ==
							pJobArgs[0].mesh.pUvs[baseLoop].x &&
						pJobArgs[0].mesh.pUvs[pEdgeEntry->loopIndex].y ==
							pJobArgs[0].mesh.pUvs[baseLoop].y;
			}
			else {
				match = (pEdgeEntry->ruvmVert == ruvmVert ||
						 pEdgeEntry->ruvmVertNext == ruvmVert) &&
						(pEdgeEntry->ruvmVert == ruvmNextVert ||
						 pEdgeEntry->ruvmVertNext == ruvmNextVert) &&
						 pEdgeEntry->tile == pEntry->tile &&
						 pEdgeEntry->baseEdge == baseEdge &&
						 pEdgeEntry->baseEdgeSign == baseEdgeSign;
			}
			if (match) {
				*pVert = pEdgeEntry->vert;
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry = pEdgeEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(EdgeTable));
				initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, localMesh,
								   ruvmVert, ruvmNextVert, *pVert, baseEdge,
								   baseVert, baseEdgeSign, baseLoop,
								   pEntry->faceIndex);
				*pVert = pEdgeEntry->vert;
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
}

static int32_t checkIfShouldSkip(AddFaceVars *pAfVars, int32_t faceStart,
                                 int32_t k) {
	if (!pAfVars->seamFace) {
		return 1;
	}
	int32_t isSeamLoop = 0;
	for (int32_t l = 0; l < pAfVars->seamLoopCount; ++l) {
		if (faceStart - k == pAfVars->seamLoops[l]) {
			isSeamLoop = 1;
			break;
		}
	}
	if (!isSeamLoop) {
		return 1;
	}
	return 0;
}

static void addLoopsToBufferAndVertsToMesh(RuvmContext pContext, RuvmMap pMap,
                                           AddFaceVars *pAfVars, SendOffArgs *pJobArgs,
                                           Mesh *pMeshOut, int32_t edgeTableSize,
								           EdgeTable *pEdgeTable, EdgeVerts *pEdgeVerts) {
	BoundaryVert *pEntry = pAfVars->pEntry;
	do {
		WorkMesh *localMesh = &pJobArgs[pEntry->job].localMesh;
		int32_t ruvmLoopsAdded = 0;
		int32_t faceStart = localMesh->pFaces[pEntry->face];
		int32_t faceEnd = localMesh->pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = localMesh->pLoops[faceStart - k];

			int32_t sort;
			//sort is stored in first 4 bits,
			//and base loop is stored in the last 4
			sort = pEntry->baseLoops[k] & 15; //zero last 4 bits;

			if (vert >= localMesh->vertCount) {
				if (checkIfShouldSkip(pAfVars, faceStart, k)) {
					continue;
				}
				addBoundaryLoopAndVert(pContext, pMap, pAfVars, &vert, pEdgeVerts,
				                       pEntry, pJobArgs, pEdgeTable, edgeTableSize,
									   pMeshOut, k, sort);
			}
			else {
				vert += pJobArgs[pEntry->job].vertBase;
				pAfVars->ruvmIndicesSort[pAfVars->loopBufferSize + 1] = sort * 10;
				ruvmLoopsAdded++;
			}
			pAfVars->loopBuffer[pAfVars->loopBufferSize] = vert;
			pAfVars->uvBuffer[pAfVars->loopBufferSize] = localMesh->pUvs[faceStart - k];
			pAfVars->normalBuffer[pAfVars->loopBufferSize] = localMesh->pNormals[faceStart - k];
			(pAfVars->loopBufferSize)++;
			pAfVars->infoBufSize++;
		}
		BoundaryVert *pNext = pEntry->pNext;
		pContext->alloc.pFree(pEntry);
		pEntry = pNext;
	} while(pEntry);
}

static void sortLoopsFull(int32_t *pIndexTable, AddFaceVars *pAfVars) {
	//insertion sort
	int32_t order = vec2WindingCompare(pAfVars->uvBuffer[0], pAfVars->uvBuffer[1],
	                                   pAfVars->centre, 1);
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufferSize = 2;
	for (int32_t k = bufferSize; k < pAfVars->loopBufferSize; ++k) {
		int32_t l, insert;
		for (l = bufferSize - 1; l >= 0; --l) {
			if (l != 0) {
				insert =
					vec2WindingCompare(pAfVars->uvBuffer[k],
					                   pAfVars->uvBuffer[pIndexTable[l]],
					                   pAfVars->centre, 1)
					&&
					vec2WindingCompare(pAfVars->uvBuffer[pIndexTable[l - 1]],
					                   pAfVars->uvBuffer[k], pAfVars->centre, 1);
			}
			else {
				insert =
					vec2WindingCompare(pAfVars->uvBuffer[k],
					                   pAfVars->uvBuffer[pIndexTable[l]],
									   pAfVars->centre, 1);
			}
			if (insert) {
				break;
			}
		}
		if (!insert) {
			pIndexTable[bufferSize++] = k;
		}
		else {
			for (int32_t m = bufferSize; m > l; --m) {
				pIndexTable[m] = pIndexTable[m - 1];
			}
			pIndexTable[l] = k;
			bufferSize++;
		}
	}
}

static void sortLoops(int32_t *pIndexTable, AddFaceVars *pAfVars) {
	int32_t *pVertRuvmIndices = pAfVars->ruvmIndicesSort + 1;
	//insertion sort
	int32_t a = pVertRuvmIndices[0];
	int32_t b = pVertRuvmIndices[1];
	int32_t order = a < b;
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufferSize = 2;
	for (int32_t k = bufferSize; k < pAfVars->loopBufferSize; ++k) {
		int32_t l, insert;
		for (l = bufferSize - 1; l >= 0; --l) {
			insert = pVertRuvmIndices[k] < pVertRuvmIndices[pIndexTable[l]] &&
							 pVertRuvmIndices[k] > pVertRuvmIndices[pIndexTable[l - 1]];
			if (insert) {
				break;
			}
		}
		if (!insert) {
			pIndexTable[bufferSize++] = k;
		}
		else {
			for (int32_t m = bufferSize; m > l; --m) {
				pIndexTable[m] = pIndexTable[m - 1];
			}
			pIndexTable[l] = k;
			bufferSize++;
		}
	}
}

static int32_t determineIfSeamFace(RuvmMap pMap, BoundaryVert *pEntry, int32_t *pEntryNum) {
	int32_t faceIndex = pEntry->faceIndex;
	int32_t ruvmLoops = 0;
	*pEntryNum = 0;
	do {
		ruvmLoops += pEntry->type;
		pEntry = pEntry->pNext;
		++*pEntryNum;
	} while(pEntry);
	int32_t faceStart = pMap->mesh.pFaces[faceIndex];
	int32_t faceEnd = pMap->mesh.pFaces[faceIndex + 1];
	int32_t faceSize = faceEnd - faceStart;
	return ruvmLoops < faceSize;
}

static void compileEntryInfo(BoundaryVert *pEntry, int32_t *pCount, int32_t *pIsSeam,
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

static void mergeAndCopyEdgeFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                           SendOffArgs *pJobArgs, int32_t edgeTableSize, EdgeTable *pEdgeTable,
						   int32_t allBoundaryFacesSize, BoundaryVert **pAllBoundaryFaces,
						   EdgeVerts *pEdgeVerts) {
	for (int32_t j = 0; j < allBoundaryFacesSize; ++j) {
		BoundaryVert *pEntry = pAllBoundaryFaces[j];
		int32_t entryCount, isSeam, hasPreservedEdge;
		compileEntryInfo(pEntry, &entryCount, &isSeam, &hasPreservedEdge);
		int32_t entryNum;
		//int32_t seamFace = ;
		FaceInfo ruvmFace;
		ruvmFace.index = pEntry->faceIndex;
		ruvmFace.start = pMap->mesh.pFaces[pEntry->faceIndex];
		ruvmFace.end = pMap->mesh.pFaces[pEntry->faceIndex + 1];
		ruvmFace.size = ruvmFace.end - ruvmFace.start;
		Piece *pPieces = pContext->alloc.pMalloc(sizeof(Piece) * entryCount);
		Piece *pPieceArray = NULL;
		int32_t pieceCount;
		pieceCount = 0;
		splitIntoPieces(pContext, pPieces, &pieceCount, pEntry, pJobArgs,
						pEdgeVerts, entryCount, &pPieceArray);
		for (int32_t l = 0; l < pieceCount; ++l) {
			AddFaceVars afVars = {0};
			afVars.pEntry = pPieces[l].pEntry;
			afVars.ruvmFace = ruvmFace;
			afVars.seamFace = determineIfSeamFace(pMap, pEntry, &entryNum);
			afVars.ruvmIndicesSort[0] = -10;
			if (afVars.seamFace) {
				int32_t totalVerts = 0;
				determineLoopsToKeep(pContext, pMap, &afVars, pJobArgs, &totalVerts);
				_(&afVars.centre V2DIVSEQL (float)totalVerts);
			}
			int32_t loopBase = pMeshOut->loopCount;
			pMeshOut->pFaces[pMeshOut->faceCount] = loopBase;
			addLoopsToBufferAndVertsToMesh(pContext, pMap, &afVars, pJobArgs, pMeshOut,
										   edgeTableSize, pEdgeTable, pEdgeVerts);
			if (afVars.loopBufferSize <= 2) {
				continue;
			}
			pMeshOut->loopCount += afVars.loopBufferSize;
			pMeshOut->faceCount++;
			
			int32_t indexTable[9];
			indexTable[0] = -1;
			if (afVars.seamFace) {
				//full winding sort
				sortLoopsFull(indexTable + 1, &afVars);
			}
			else {
				sortLoops(indexTable + 1, &afVars);
			}
			for (int32_t k = 0; k < afVars.loopBufferSize; ++k) {
				pMeshOut->pLoops[loopBase + k] = afVars.loopBuffer[indexTable[k + 1]];
				pMeshOut->pNormals[loopBase + k] = afVars.normalBuffer[indexTable[k + 1]];
				pMeshOut->pUvs[loopBase + k] = afVars.uvBuffer[indexTable[k + 1]];
			}
		}
		if (pPieceArray) {
			pContext->alloc.pFree(pPieceArray);
		}
	}
}

static void linkEntriesFromOtherJobs(RuvmContext pContext, SendOffArgs *pJobArgs,
                                     BoundaryDir *pEntryDir, int32_t faceIndex,
									 int32_t hash, int32_t job) {
	for (int32_t j = job + 1; j < pContext->threadCount; ++j) {
		BoundaryDir *pEntryDirOther = pJobArgs[j].pBoundaryBuffer + hash;
		do {
			if (pEntryDirOther->pEntry) {
				if (faceIndex == pEntryDirOther->pEntry->faceIndex) {
					BoundaryVert *pEntry = pEntryDir->pEntry;
					pEntry->pNext = pEntryDirOther->pEntry;
					pEntryDirOther->pEntry = NULL;
				}
			}
			pEntryDirOther = pEntryDirOther->pNext;
		} while (pEntryDirOther);
	}
}

static void compileEntriesFromJobs(RuvmContext pContext, SendOffArgs *pJobArgs,
                                   BoundaryVert **pAllBoundaryFaces,
								   int32_t *pAllBoundaryFacesSize) {
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		for (int32_t hash = 0; hash < pJobArgs[i].boundaryBufferSize; ++hash) {
			BoundaryDir *pEntryDir = pJobArgs[i].pBoundaryBuffer + hash;
			int32_t depth = 0;
			do {
				if (pEntryDir->pEntry) {
					int32_t faceIndex = pEntryDir->pEntry->faceIndex;
					linkEntriesFromOtherJobs(pContext, pJobArgs, pEntryDir,
					                         faceIndex, hash, i);
					pAllBoundaryFaces[*pAllBoundaryFacesSize] = pEntryDir->pEntry;
					++*pAllBoundaryFacesSize;
				}
				BoundaryDir *pNextEntryDir = pEntryDir->pNext;
				if (depth != 0) {
					pContext->alloc.pFree(pEntryDir);
				}
				pEntryDir = pNextEntryDir;
				depth++;
			} while (pEntryDir);
		}
	}
}

void ruvmMergeBoundaryFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                            SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts) {
	int32_t totalBoundaryFaces = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		totalBoundaryFaces += pJobArgs[i].totalBoundaryFaces;
	}
	BoundaryVert **pAllBoundaryFaces = pContext->alloc.pMalloc(sizeof(void *) * totalBoundaryFaces);
	int32_t allBoundaryFacesSize = 0;
	compileEntriesFromJobs(pContext, pJobArgs, pAllBoundaryFaces, &allBoundaryFacesSize);
	EdgeTable *pEdgeTable = pContext->alloc.pCalloc(totalBoundaryFaces, sizeof(EdgeTable));
	int32_t edgeTableSize = totalBoundaryFaces;
	for (int32_t i = 0; i < pJobArgs[0].mesh.vertCount; ++i) {
		int32_t preserve = pJobArgs[0].pInVertTable[i];
		for (int32_t j = 1; j < pContext->threadCount; ++j) {
			preserve |= pJobArgs[j].pInVertTable[i];
		}
		pJobArgs[0].pInVertTable[i] = preserve;
	}
	mergeAndCopyEdgeFaces(pContext, pMap, pMeshOut, pJobArgs, edgeTableSize,
	                      pEdgeTable, allBoundaryFacesSize, pAllBoundaryFaces,
						  pEdgeVerts);
	
	pContext->alloc.pFree(pAllBoundaryFaces);
	for (int32_t i = 0; i < edgeTableSize; ++i) {
		EdgeTable *pEntry = pEdgeTable[i].pNext;
		while (pEntry) {
			EdgeTable *pNextEntry = pEntry->pNext;
			pContext->alloc.pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	pContext->alloc.pFree(pEdgeTable);
}
