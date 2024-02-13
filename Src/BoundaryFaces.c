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

static void splitIntoPieces(RuvmContext pContext, Piece *pPieces,
                            int32_t *pPieceCount, BoundaryVert *pEntry,
                            SendOffArgs *pJobArgs, FaceInfo *ruvmFaceInfo,
                            EdgeVerts *pEdgeVerts, const int32_t entryCount,
							Piece **ppPieceArray) {
	int32_t tableSize = entryCount;
	SharedEdge *pSharedEdges = pContext->alloc.pCalloc(tableSize, sizeof(SharedEdge));
	Piece *pEntries = pContext->alloc.pCalloc(entryCount, sizeof(Piece));
	*ppPieceArray = pEntries;
	int32_t entryIndex = 0;
	do {
		WorkMesh *localMesh = &pJobArgs[pEntry->job].localMesh;
		int32_t faceStart = localMesh->pFaces[pEntry->face];
		int32_t faceEnd = localMesh->pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		for (int32_t i = 0; i < loopAmount; ++i) {
			int32_t iNext = (i + 1) % loopAmount;
			int32_t vert = localMesh->pLoops[faceStart - i];
			int32_t vertNext = localMesh->pLoops[faceStart - iNext];
			int32_t baseLoopLocal = pEntry->baseLoops[i] >> 4;
			if (vert < localMesh->vertCount) {
				//ruvm loop - skip
				continue;
			}
			int32_t baseLoopLocalAbs = baseLoopLocal < 0 ? baseLoopLocal * -1 : baseLoopLocal;
			int32_t baseFaceStart = pJobArgs[0].mesh.pFaces[pEntry->baseFace];
			int32_t baseFaceEnd = pJobArgs[0].mesh.pFaces[pEntry->baseFace + 1];
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
					pEdgeEntry = pEdgeEntry->pNext = pContext->alloc.pCalloc(1, sizeof(SharedEdge));
					pEdgeEntry->edge = baseEdge + 1;
					pEdgeEntry->entries[0] = entryIndex;
					break;
				}
				pEdgeEntry = pEdgeEntry->pNext;
			} while(1);
		}
		pEntries[entryIndex].pEntry = pEntry;
		pEntries[entryIndex].entryIndex = entryIndex;
		entryIndex++;
		BoundaryVert *pNextEntry = pEntry->pNext;
		pEntry->pNext = NULL;
		pEntry = pNextEntry;
	} while(pEntry);

	//TODO replace search with a hash table
	//each table entry for each edge index.
	//And each entry contains int32 indices of each
	//boundary entry. 
	//Boundary entry indices must be duplicated at each
	//edge entry, so that entries can be strung together below.
	if (entryCount == 1) {
		pPieces[0] = pEntries[0];
		++*pPieceCount;
	}
	else {
		for (int32_t i = 0; i < entryCount; ++i) {
			if (pEntries[i].listed) {
				continue;
			}
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
			pPieces[*pPieceCount] = pEntries[i];
			++*pPieceCount;
		}
	}
	/*
	for (int32_t i = 0) {
		//find if already exists in list
		int32_t connectedPiece = -1;
		for (int32_t j = 0; j < *pPieceCount; ++j) {
			for (int32_t k = 0; k < pSharedEdges[j].edgeCount; ++k) {
				if (baseEdge == pSharedEdges[j].edges[k]) {
					connectedPiece = j;
					goto end;
				}
			}
		}
end:
		if (connectedPiece >= 0) {
			pNextEntry = pEntry->pNext;
			pEntry->pNext = NULL;
			BoundaryVert *pPieceEntry = ppPieces[connectedPiece];
			do {
				if (!pPieceEntry->pNext) {
					pPieceEntry->pNext = pEntry;
					break;
				}
				pPieceEntry = pPieceEntry->pNext;
			} while(pPieceEntry);
			break;
		}
	}
	*/
	pContext->alloc.pFree(pSharedEdges);
	//pContext->alloc.pFree(pEntries);
}

static void initEdgeTableEntry(EdgeTable *pEdgeEntry, BoundaryVert *pEntry, Mesh *pMeshOut,
                        WorkMesh *pLocalMesh, int32_t ruvmVert, int32_t ruvmNextVert,
						int32_t vert, int32_t baseEdge, int32_t baseVert) {
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
}

static void getRuvmVerts(int32_t *pRuvmVerts, SendOffArgs *pJobArgs,
                         BoundaryVert *pEntry, int32_t *pSeamLoopCount,
						 int32_t *pSeamLoops, FaceInfo *ruvmFaceInfo, RuvmMap pMap,
						 RuvmContext pContext, EdgeVerts *pEdgeVerts) {
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
				int32_t sortNext, baseLoopLocal, baseLoopLocalAbs,
						baseLoop, baseEdge, baseVert;
				sortNext = sort;
				sort -= 1;
				if (sort < 0) {
					sort = ruvmFaceInfo->size - 1;
				}
				baseLoopLocal = pEntry->baseLoops[k] >> 4;
				baseLoopLocalAbs = baseLoopLocal < 0 ? baseLoopLocal * -1 : baseLoopLocal;
				int32_t baseFaceStart = pJobArgs[0].mesh.pFaces[pEntry->baseFace];
				int32_t baseFaceEnd = pJobArgs[0].mesh.pFaces[pEntry->baseFace + 1];
				int32_t baseFaceSize = baseFaceEnd - baseFaceStart;
				//-1 cause negative
				baseLoop = baseFaceStart + baseLoopLocalAbs - 1;
				int32_t nextBaseLoop = baseFaceStart + (baseLoopLocalAbs % baseFaceSize);
				int32_t isBaseLoop = baseLoopLocal < 0;
				baseEdge = pJobArgs[pEntry->job].mesh.pEdges[baseLoop];
				baseVert = isBaseLoop ? pJobArgs[pEntry->job].mesh.pLoops[baseLoop] : -1;

				if (isBaseLoop) {
					int32_t *pVerts = pEdgeVerts[baseEdge].verts;
					if (pVerts[0] >= 0 && pVerts[1] >= 0) {
						int32_t whichLoop = pVerts[0] == baseLoop;
						int32_t otherLoop = pVerts[whichLoop];
						Vec2 uv = pJobArgs[0].mesh.pUvs[nextBaseLoop];
						Vec2 uvOther = pJobArgs[0].mesh.pUvs[otherLoop];
						int32_t notSeam = _(uv V2EQL uvOther);
						if (notSeam) {
							//continue;
						}
					}
				}
				
				int32_t hash, ruvmVert, ruvmNextVert;

				if (isBaseLoop) {
					hash = ruvmFnvHash((uint8_t *)&baseVert, 4, 16);
					ruvmVert = ruvmNextVert = -1;
				}
				else {
					ruvmVert = pMap->mesh.pLoops[ruvmFaceInfo->start + sort];
					ruvmNextVert = pMap->mesh.pLoops[ruvmFaceInfo->start + sortNext];
					uint32_t ruvmEdgeId = ruvmVert + ruvmNextVert;
					hash = ruvmFnvHash((uint8_t *)&ruvmEdgeId, 4, 16);
				}
				EdgeTable *pEdgeEntry = localEdgeTable + hash;
				if (!pEdgeEntry->loops) {
					pEdgeEntry->ruvmVert = ruvmVert;
					pEdgeEntry->ruvmVertNext = ruvmNextVert;
					pEdgeEntry->tile = pEntry->tile;
					pEdgeEntry->baseEdge = baseEdge;
					pEdgeEntry->baseVert = baseVert;
					pEdgeEntry->loops = 1;
					pEdgeEntry->vert = faceStart - k;
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
							break;
						}
						if (!pEdgeEntry->pNext) {
							pEdgeEntry = pEdgeEntry->pNext =
								pContext->alloc.pCalloc(1, sizeof(EdgeTable));
							pEdgeEntry->ruvmVert = ruvmVert;
							pEdgeEntry->ruvmVertNext = ruvmNextVert;
							pEdgeEntry->tile = pEntry->tile;
							pEdgeEntry->baseEdge = baseEdge;
							pEdgeEntry->baseVert = baseVert;
							pEdgeEntry->loops = 1;
							pEdgeEntry->vert = faceStart - k;
							break;
						}
						pEdgeEntry = pEdgeEntry->pNext;
					} while(1);
				}
			}
			else {
				vert += pJobArgs[pEntry->job].vertBase;
				pRuvmVerts[sort] = vert;
			}
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
	
	for (int32_t i = 0; i < 16; ++i) {
		EdgeTable *pEdgeEntry = localEdgeTable + i;
		int32_t depth = 0;
		do {
			if (pEdgeEntry->loops == 1) {
				pSeamLoops[*pSeamLoopCount] = pEdgeEntry->vert;
				++*pSeamLoopCount;
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

static void addToPreserveBuf(int32_t *pPreserveBufCount, PreserveBuf *pPreserveBuf,
                             int32_t loopIndex, int32_t baseEdge) {
	for (int32_t l = 0; l < *pPreserveBufCount; ++l) {
		if (pPreserveBuf[l].edge == baseEdge) {
			pPreserveBuf[l].loops[1] = loopIndex;
			return;
		}
	}
	pPreserveBuf[*pPreserveBufCount].edge = baseEdge;
	pPreserveBuf[*pPreserveBufCount].loops[0] = loopIndex;
	++*pPreserveBufCount;
}

static int32_t addFaceLoopsAndVertsToBuffer(RuvmContext pContext, RuvmMap pMap, SendOffArgs *pJobArgs,
                                         Mesh *pMeshOut, BoundaryVert **ppEntry,
                                         int32_t seamFace, int32_t *loopBufferSize, int32_t *pLoopBuffer,
								         Vec2 *pUvBuffer, FaceInfo *ruvmFaceInfo, int32_t edgeTableSize,
								         EdgeTable *pEdgeTable, int32_t *pRuvmIndicesSort,
										 int32_t hasPreservedEdge, int32_t *pInfoBufSize,
										 int32_t *pRuvmVerts, PreserveBuf *pPreserveBuf,
										 int32_t *pPreserveBufCount, int32_t seamLoopCount,
										 int32_t *pSeamLoops) {
	BoundaryVert *pEntry = *ppEntry;
	do {
		WorkMesh *localMesh = &pJobArgs[pEntry->job].localMesh;
		int32_t ruvmLoopsAdded = 0;
		int32_t faceStart = localMesh->pFaces[pEntry->face];
		int32_t faceEnd = localMesh->pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		int8_t movedBuf[8] = {0};
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = localMesh->pLoops[faceStart - k];
			Vec2 uv = localMesh->pUvs[faceStart - k];

			int32_t sort;
			//sort is stored in first 4 bits,
			//and base loop is stored in the last 4
			sort = pEntry->baseLoops[k] & 15; //zero last 4 bits;

			if (vert >= localMesh->vertCount) {
				int32_t baseEdge, baseLoop, baseLoopLocalAbs, baseLoopLocal,
						isBaseLoop, baseVert, preserve, sortNext, baseFaceStart;
				sortNext = sort;
				sort -= 1;
				if (sort < 0) {
					sort = ruvmFaceInfo->size - 1;
				}
				baseLoopLocal = pEntry->baseLoops[k] >> 4;
				isBaseLoop = baseLoopLocal < 0;
				baseLoopLocalAbs = isBaseLoop ? baseLoopLocal * -1 : baseLoopLocal;
				if (baseLoopLocalAbs != 0) {
					baseFaceStart = pJobArgs[0].mesh.pFaces[pEntry->baseFace];
					baseLoop = baseFaceStart + baseLoopLocalAbs - 1;
					baseEdge = pJobArgs[pEntry->job].mesh.pEdges[baseLoop];
					baseVert = isBaseLoop ? pJobArgs[pEntry->job].mesh.pLoops[baseLoop] : -1;
					preserve = isBaseLoop ? pJobArgs[0].pInVertTable[baseVert] : 
						pJobArgs[pEntry->job].mesh.pEdgePreserve[baseEdge];
				}
				else {
					baseLoop = -1;
					baseVert = -1;
					baseEdge = 0;
					preserve = 0;
				}
				if (preserve && !isBaseLoop && 0) {
					addToPreserveBuf(pPreserveBufCount, pPreserveBuf,
					                 faceStart - k, baseEdge);
					continue;
				}
				if (!seamFace) {
					continue;
				}
				int32_t isSeamLoop = 0;
				for (int32_t l = 0; l < seamLoopCount; ++l) {
					if (faceStart - k == pSeamLoops[l]) {
						isSeamLoop = 1;
						break;
					}
				}
				if (!isSeamLoop) {
					continue;
				}
				if ((!preserve && hasPreservedEdge && 0) || (isBaseLoop && seamFace && 0)) {
					int32_t closestRuvm;
					float shortestDistance = FLT_MAX;
					for (int32_t l = 0; l < ruvmFaceInfo->size; ++l) {
						if (pRuvmVerts[l] < 0) {
							continue;
						}
						int32_t ruvmVertIndex = pMap->mesh.pLoops[ruvmFaceInfo->start + l];
						Vec2 ruvmVert = *(Vec2 *)(pMap->mesh.pVerts + ruvmVertIndex);
						Vec2 dir = _(ruvmVert V2SUB uv);
						float distance = dir.x * dir.x + dir.y * dir.y;
						if (distance < shortestDistance) {
							shortestDistance = distance;
							closestRuvm = l;
						}
					}
					int32_t dup = 0;
					for (int32_t l = 0; l < loopAmount; ++l) {
						int32_t otherSort = pEntry->baseLoops[l] & 15;
						int32_t isRuvm = localMesh->pLoops[faceStart - l] < localMesh->vertCount;
						if (otherSort == closestRuvm && (isRuvm || movedBuf[l])) {
							dup = 1;
							break;
						}
					}
					if (dup) {
						continue;
					}
					vert = pRuvmVerts[closestRuvm];
					movedBuf[k] = 1;
					pEntry->baseLoops[k] &= -16;
					pEntry->baseLoops[k] |= closestRuvm;
					pRuvmIndicesSort[*loopBufferSize] = closestRuvm * 10;
				}
				else {
					int32_t nextVertIndex = (k + 1) % loopAmount;
					int32_t lastVertIndex = (k - 1) % loopAmount;
					int32_t nextVert = localMesh->pLoops[faceStart - nextVertIndex];
					int32_t lastVert = localMesh->pLoops[faceStart - lastVertIndex];
					if (lastVert >= localMesh->vertCount && nextVert >= localMesh->vertCount) {
						//continue;
					}
					pRuvmIndicesSort[*loopBufferSize] = sortNext * 10 - 5;
					int32_t hash, ruvmVert, ruvmNextVert;
					if (isBaseLoop) {
						hash = ruvmFnvHash((uint8_t *)&baseVert, 4, edgeTableSize);
						ruvmVert = ruvmNextVert = -1;
					}
					else {
						ruvmVert = pMap->mesh.pLoops[ruvmFaceInfo->start + sort];
						ruvmNextVert = pMap->mesh.pLoops[ruvmFaceInfo->start + sortNext];
						uint32_t ruvmEdgeId = ruvmVert + ruvmNextVert;
						hash = ruvmFnvHash((uint8_t *)&ruvmEdgeId, 4, edgeTableSize);
					}
					EdgeTable *pEdgeEntry = pEdgeTable + hash;
					if (!pEdgeEntry->loops) {
						initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, localMesh,
										   ruvmVert, ruvmNextVert, vert, baseEdge,
										   baseVert);
						vert = pEdgeEntry->vert;
					}
					else {
						int32_t skip = 0;
						do {
							//ideally, we'd just compare the edges, but maps don't have
							//edges currently, so I'll remove ruvmVert/ruvmVertNext,
							//and replace them with a single edge index, one edges are
							//added the ruvm maps.
							int32_t match;
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
								vert = pEdgeEntry->vert;
								break;
							}
							if (!pEdgeEntry->pNext) {
								pEdgeEntry = pEdgeEntry->pNext =
									pContext->alloc.pCalloc(1, sizeof(EdgeTable));
								initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, localMesh,
												   ruvmVert, ruvmNextVert, vert, baseEdge,
												   baseVert);
								vert = pEdgeEntry->vert;
								break;
							}
							pEdgeEntry = pEdgeEntry->pNext;
						} while(1);
						if (skip) {
							continue;
						}
					}
				}
			}
			else {
				vert += pJobArgs[pEntry->job].vertBase;
				pRuvmIndicesSort[*loopBufferSize] = sort * 10;
				ruvmLoopsAdded++;
			}
			pLoopBuffer[*loopBufferSize] = vert;
			pUvBuffer[*loopBufferSize] = localMesh->pUvs[faceStart - k];
			(*loopBufferSize)++;
			++*pInfoBufSize;
		}
		BoundaryVert *pPrevEntry;
		pPrevEntry = pEntry;
		pEntry = pEntry->pNext;
		//pContext->alloc.pFree(pPrevEntry);
		/*
		if (hasPreservedEdge) {
			*ppEntry = pEntry;
			return pEntry != NULL;
		}
		*/
	} while(pEntry);
	return 0;
}

static void sortLoops(int32_t *pVertRuvmIndices, int32_t *pIndexTable, int32_t loopBufferSize) {
	//insertion sort
	int32_t a = pVertRuvmIndices[0];
	int32_t b = pVertRuvmIndices[1];
	int32_t order = a < b;
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufferSize = 2;
	for (int32_t k = bufferSize; k < loopBufferSize; ++k) {
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

static void mergeAndCopyEdgeFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut, SendOffArgs *pJobArgs,
		                   int32_t edgeTableSize, EdgeTable *pEdgeTable,
						   int32_t allBoundaryFacesSize, BoundaryVert **pAllBoundaryFaces,
						   int8_t *pPreservedEdgeFlag, EdgeVerts *pEdgeVerts, int32_t *pEntryCounts) {
	for (int32_t j = 0; j < allBoundaryFacesSize; ++j) {
		int32_t loopBase = pMeshOut->loopCount;
		BoundaryVert *pEntry = pAllBoundaryFaces[j];
		int32_t entryNum;
		int32_t seamFace = determineIfSeamFace(pMap, pEntry, &entryNum);
		FaceInfo ruvmFaceInfo;
		ruvmFaceInfo.start = pMap->mesh.pFaces[pEntry->faceIndex];
		ruvmFaceInfo.end = pMap->mesh.pFaces[pEntry->faceIndex + 1];
		ruvmFaceInfo.size = ruvmFaceInfo.end - ruvmFaceInfo.start;
		int32_t runAgain;
		//store loops of preserve verts
		Piece pieces[64];
		Piece *pPieceArray;
		int32_t pieceCount;
		int32_t hasPreservedEdge = pPreservedEdgeFlag[j];
		if (pEntry->seam || hasPreservedEdge) {
			pieceCount = 0;
			splitIntoPieces(pContext, pieces, &pieceCount, pEntry, pJobArgs,
			                &ruvmFaceInfo, pEdgeVerts, pEntryCounts[j], &pPieceArray);
			if (pieceCount > 1) {
				seamFace = 1;
			}
		}
		else {
			pieceCount = 1;
			pieces[0].pEntry = pEntry;
		}
		for (int32_t l = 0; l < pieceCount; ++l) {
			pEntry = pieces[l].pEntry;
			int32_t ruvmIndicesSort[64];
			ruvmIndicesSort[0] = -10;
			int32_t loopBufferSize;
			int32_t loopBuffer[128];
			Vec2 uvBuffer[128];
			PreserveBuf preserveBuf[8];
			int32_t preserveBufCount = 0;
			int32_t seamLoops[8];
			int32_t seamLoopCount = 0;
			int32_t ruvmVerts[4] = {-1, -1, -1, -1};
			int32_t infoBufSize = 0;
			if (seamFace) {
				getRuvmVerts(ruvmVerts, pJobArgs, pEntry, &seamLoopCount, seamLoops,
							 &ruvmFaceInfo, pMap, pContext, pEdgeVerts);
				//printf("Seam loop count: %d\n", seamLoopCount);
			}
			do {
				loopBufferSize = 0;
				pMeshOut->pFaces[pMeshOut->faceCount] = loopBase;
				runAgain = 
					addFaceLoopsAndVertsToBuffer(pContext, pMap, pJobArgs, pMeshOut, &pEntry,
												 seamFace, &loopBufferSize, loopBuffer,
												 uvBuffer, &ruvmFaceInfo, edgeTableSize,
												 pEdgeTable, ruvmIndicesSort + 1,
												 hasPreservedEdge, &infoBufSize, ruvmVerts,
												 preserveBuf, &preserveBufCount, seamLoopCount,
												 seamLoops);
				if (loopBufferSize <= 2) {
					continue;
				}
				pMeshOut->loopCount += loopBufferSize;
				pMeshOut->faceCount++;
				/*
				if (hasPreservedEdge) {
					for (int32_t k = 0; k < loopBufferSize; ++k) {
						pMeshOut->pLoops[loopBase + k] = loopBuffer[k];
						pMeshOut->pUvs[loopBase + k] = uvBuffer[k];
					}
				}
				*/
				
				for (int32_t k = 0; k < loopBufferSize; ++k) {
					int32_t sameSort = -1;
					for (int32_t m = k + 1; m < loopBufferSize; ++m) {
						if (ruvmIndicesSort[k + 1] == ruvmIndicesSort[m + 1]) {
							sameSort = m;
							break;
						}
					}
					if (sameSort < 0) {
						continue;
					}
					int32_t ruvmLocalIndex = (ruvmIndicesSort[k + 1] + 5) / 10;
					int32_t ruvmVertIndex = pMap->mesh.pLoops[ruvmFaceInfo.start + ruvmLocalIndex];
					Vec2 ruvmVert = *(Vec2 *)(pMap->mesh.pVerts + ruvmVertIndex);
					Vec2 dirA = _(uvBuffer[k] V2SUB ruvmVert);
					float distanceA = dirA.x * dirA.x + dirA.y * dirA.y;
					Vec2 dirB = _(uvBuffer[sameSort] V2SUB ruvmVert);
					float distanceB = dirB.x * dirB.x + dirB.y * dirB.y;
					if (distanceA > distanceB) {
						ruvmIndicesSort[k + 1]--;
					}
					else {
						ruvmIndicesSort[sameSort + 1]--;
					}
				}
				int32_t indexTable[13];
				indexTable[0] = -1;
				sortLoops(ruvmIndicesSort + 1, indexTable + 1, loopBufferSize);
				for (int32_t k = 0; k < loopBufferSize; ++k) {
					pMeshOut->pLoops[loopBase + k] = loopBuffer[indexTable[k + 1]];
					pMeshOut->pUvs[loopBase + k] = uvBuffer[indexTable[k + 1]];
				}
				loopBase += loopBufferSize;
			} while(runAgain);
		}
		if (seamFace) {
			pContext->alloc.pFree(pPieceArray);
		}

		/*
		if (hasPreservedEdge) {
			runAgain = 0;
			do {
				//only need a newLoopBuf, just use pMeshOut loop array
				//for base loopBuf
				for (int32_t k = 0; k < preserveBufCount; ++k) {
					//make loopbuf, then clip against preserve edges,
					//reversing preserve vert order on second run.
					//
					//run again/ second run occurs as long as verts
					//have not yet been added to a face
				}
			} while(runAgain);
		}
		*/
	}
}

void ruvmMergeBoundaryFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                            SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts) {
	int32_t totalBoundaryFaces = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		totalBoundaryFaces += pJobArgs[i].totalBoundaryFaces;
	}
	BoundaryVert **pAllBoundaryFaces = pContext->alloc.pMalloc(sizeof(void *) * totalBoundaryFaces);
	int32_t *pEntryCounts = pContext->alloc.pMalloc(sizeof(int32_t) * totalBoundaryFaces);
	int8_t *pPreservedEdgeFlag = pContext->alloc.pMalloc(totalBoundaryFaces);
	int32_t allBoundaryFacesSize = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		for (int32_t hash = 0; hash < pJobArgs[i].boundaryBufferSize; ++hash) {
			BoundaryDir *pEntryDir = pJobArgs[i].pBoundaryBuffer + hash;
			int32_t depth = 0;
			do {
				if (pEntryDir->pEntry) {
					int32_t seam = pEntryDir->pEntry->seam;
					int8_t hasPreservedEdge = pEntryDir->pEntry->hasPreservedEdge;
					BoundaryVert *pChild = pEntryDir->pEntry->pNext;
					pEntryCounts[allBoundaryFacesSize] = 1;
					while (pChild) {
						pEntryCounts[allBoundaryFacesSize]++;
						hasPreservedEdge += pChild->hasPreservedEdge;
						seam += pChild->seam;
						pChild = pChild->pNext;
					}
					int32_t faceIndex = pEntryDir->pEntry->faceIndex;
					for (int32_t j = i + 1; j < pContext->threadCount; ++j) {
						BoundaryDir *pEntryDirOther = pJobArgs[j].pBoundaryBuffer + hash;
						do {
							if (pEntryDirOther->pEntry) {
								if (faceIndex == pEntryDirOther->pEntry->faceIndex) {
									pEntryCounts[allBoundaryFacesSize]++;
									hasPreservedEdge += pEntryDirOther->pEntry->hasPreservedEdge;
									seam += pEntryDirOther->pEntry->seam;
									BoundaryVert *pEntry = pEntryDir->pEntry;
									while(pEntry->pNext) {
										pEntry = pEntry->pNext;
									}
									pEntry->pNext = pEntryDirOther->pEntry;
									pEntryDirOther->pEntry = NULL;
								}
							}
							pEntryDirOther = pEntryDirOther->pNext;
						} while (pEntryDirOther);
					}
					pPreservedEdgeFlag[allBoundaryFacesSize] = hasPreservedEdge;
					pEntryDir->pEntry->seam = seam;
					pAllBoundaryFaces[allBoundaryFacesSize] = pEntryDir->pEntry;
					allBoundaryFacesSize++;
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
						  pPreservedEdgeFlag, pEdgeVerts, pEntryCounts);
	
	pContext->alloc.pFree(pPreservedEdgeFlag);
	pContext->alloc.pFree(pAllBoundaryFaces);
	pContext->alloc.pFree(pEntryCounts);
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
