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
						int32_t vert, int32_t baseEdge, int32_t baseVert, int32_t baseEdgeSign,
						int32_t loopIndex, int32_t ruvmFace) {
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

static void getRuvmVerts(int32_t *pRuvmVerts, SendOffArgs *pJobArgs,
                         BoundaryVert *pEntry, int32_t *pSeamLoopCount,
						 int32_t *pSeamLoops, FaceInfo *ruvmFaceInfo, RuvmMap pMap,
						 RuvmContext pContext, EdgeVerts *pEdgeVerts, int32_t *pUseFullSort,
						 Vec2 *pCentre, int32_t *pTotalVerts) {
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

				int32_t isSeam = 0;
				int32_t isPreserve = 0;
				
				if (isBaseLoop) {
					if (pJobArgs[0].pVertSeamTable[baseVert] > 2 ||
					    (pJobArgs[0].pVertSeamTable[baseVert] && pJobArgs[0].pInVertTable[baseVert])) {
						isPreserve = 1;
						isSeam = 1;
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
					pEdgeEntry->seam = isSeam;
					pEdgeEntry->preserve = isPreserve;
					pEdgeEntry->isBase = isBaseLoop;
					pEdgeEntry->job = pEntry->job;
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
							pEdgeEntry->seam |= isSeam;
							pEdgeEntry->preserve |= isPreserve;
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
							pEdgeEntry->seam = isSeam;
							pEdgeEntry->preserve = isPreserve;
							pEdgeEntry->isBase = isBaseLoop;
							pEdgeEntry->job = pEntry->job;
							break;
						}
						pEdgeEntry = pEdgeEntry->pNext;
					} while(1);
				}
			}
			else {
				_(pCentre V2ADDEQL localMesh->pUvs[faceStart - k]);
				++*pTotalVerts;
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
			if (pEdgeEntry->loops == 1 || (pEdgeEntry->seam && pEdgeEntry->preserve)) {
				*pUseFullSort |= pEdgeEntry->isBase;
				_(pCentre V2ADDEQL pJobArgs[pEdgeEntry->job].localMesh.pUvs[pEdgeEntry->vert]);
				++*pTotalVerts;
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

static int32_t windingCompare(Vec2 a, Vec2 b, Vec2 centre, int32_t fallBack) {
	Vec2 aDiff = _(a V2SUB centre);
	Vec2 bDiff = _(b V2SUB centre);
	float cross = aDiff.x * bDiff.y - bDiff.x * aDiff.y;
	if (cross != .0f) {
		return cross > .0f;
	}
	if (fallBack) {
	float aDist = aDiff.x * aDiff.x + aDiff.y * aDiff.y;
	float bDist = bDiff.x * bDiff.x + bDiff.y * bDiff.y;
	return bDist > aDist;
	}
	else {
		return 2;
	}
}

static int32_t addFaceLoopsAndVertsToBuffer(RuvmContext pContext, RuvmMap pMap, SendOffArgs *pJobArgs,
                                         Mesh *pMeshOut, BoundaryVert **ppEntry,
                                         int32_t seamFace, int32_t *loopBufferSize, int32_t *pLoopBuffer,
								         Vec2 *pUvBuffer, FaceInfo *ruvmFaceInfo, int32_t edgeTableSize,
								         EdgeTable *pEdgeTable, int32_t *pRuvmIndicesSort,
										 int32_t hasPreservedEdge, int32_t *pInfoBufSize,
										 int32_t *pRuvmVerts, PreserveBuf *pPreserveBuf,
										 int32_t *pPreserveBufCount, int32_t seamLoopCount,
										 int32_t *pSeamLoops, Vec2 centre, EdgeVerts *pEdgeVerts) {
	BoundaryVert *pEntry = *ppEntry;
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
				int32_t baseEdge, baseLoop, baseLoopLocalAbs, baseLoopLocal,
						isBaseLoop, baseVert, preserve, sortNext, baseLoopNext;
				sortNext = sort;
				sort -= 1;
				if (sort < 0) {
					sort = ruvmFaceInfo->size - 1;
				}
				baseLoopLocal = pEntry->baseLoops[k] >> 4;
				isBaseLoop = baseLoopLocal < 0;
				baseLoopLocalAbs = isBaseLoop ? baseLoopLocal * -1 : baseLoopLocal;
				FaceInfo baseFace;
				if (baseLoopLocalAbs != 0) {
					baseFace.index = pEntry->baseFace;
					baseFace.start = pJobArgs[pEntry->job].mesh.pFaces[pEntry->baseFace];
					baseFace.end = pJobArgs[pEntry->job].mesh.pFaces[pEntry->baseFace + 1];
					baseFace.size = baseFace.end - baseFace.start;
					baseLoop = baseFace.start + baseLoopLocalAbs - 1;
					baseLoopNext = ((baseLoopLocalAbs) % baseFace.size) + baseFace.start;
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
				int32_t baseEdgeSign = 0;
				if (!isBaseLoop && 
				    checkIfEdgeIsSeam(baseEdge, baseFace, baseLoopLocalAbs - 1,
				                      &pJobArgs[pEntry->job].mesh, pEdgeVerts)) {
					int32_t *pVerts = pJobArgs[0].pEdgeVerts[baseEdge].verts;
					baseEdgeSign = windingCompare(pJobArgs[0].mesh.pUvs[pVerts[0]],
												  pJobArgs[0].mesh.pUvs[pVerts[1]],
												  centre, 1);
				}
				EdgeTable *pEdgeEntry = pEdgeTable + hash;
				if (!pEdgeEntry->loops) {
					initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, localMesh,
									   ruvmVert, ruvmNextVert, vert, baseEdge,
									   baseVert, baseEdgeSign, baseLoop,
									   pEntry->faceIndex);
					vert = pEdgeEntry->vert;
				}
				else {
					int32_t skip = 0;
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
							vert = pEdgeEntry->vert;
							break;
						}
						if (!pEdgeEntry->pNext) {
							pEdgeEntry = pEdgeEntry->pNext =
								pContext->alloc.pCalloc(1, sizeof(EdgeTable));
							initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, localMesh,
											   ruvmVert, ruvmNextVert, vert, baseEdge,
											   baseVert, baseEdgeSign, baseLoop,
											   pEntry->faceIndex);
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
		BoundaryVert *pNext = pEntry->pNext;
		pContext->alloc.pFree(pEntry);
		pEntry = pNext;
	} while(pEntry);
	return 0;
}

static void sortLoopsFull(int32_t *pIndexTable, int32_t loopBufferSize, Vec2 *pUvBuffer, Vec2 centre) {
	//insertion sort
	int32_t order = windingCompare(pUvBuffer[0], pUvBuffer[1], centre, 1);
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufferSize = 2;
	for (int32_t k = bufferSize; k < loopBufferSize; ++k) {
		int32_t l, insert;
		for (l = bufferSize - 1; l >= 0; --l) {
			if (l != 0) {
				insert = windingCompare(pUvBuffer[k], pUvBuffer[pIndexTable[l]], centre, 1) &&
						 windingCompare(pUvBuffer[pIndexTable[l - 1]], pUvBuffer[k], centre, 1);
			}
			else {
				insert = windingCompare(pUvBuffer[k], pUvBuffer[pIndexTable[l]], centre, 1);
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

static void mergeAndCopyEdgeFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut, SendOffArgs *pJobArgs,
		                   int32_t edgeTableSize, EdgeTable *pEdgeTable,
						   int32_t allBoundaryFacesSize, BoundaryVert **pAllBoundaryFaces,
						   EdgeVerts *pEdgeVerts) {
	for (int32_t j = 0; j < allBoundaryFacesSize; ++j) {
		int32_t loopBase = pMeshOut->loopCount;
		BoundaryVert *pEntry = pAllBoundaryFaces[j];
		int32_t entryCount, isSeam, hasPreservedEdge;
		compileEntryInfo(pEntry, &entryCount, &isSeam, &hasPreservedEdge);
		int32_t entryNum;
		int32_t seamFace = determineIfSeamFace(pMap, pEntry, &entryNum);
		FaceInfo ruvmFaceInfo;
		ruvmFaceInfo.start = pMap->mesh.pFaces[pEntry->faceIndex];
		ruvmFaceInfo.end = pMap->mesh.pFaces[pEntry->faceIndex + 1];
		ruvmFaceInfo.size = ruvmFaceInfo.end - ruvmFaceInfo.start;
		int32_t runAgain;
		//store loops of preserve verts
		Piece pieces[64];
		Piece *pPieceArray = NULL;
		int32_t pieceCount;
		if (isSeam || hasPreservedEdge) {
			pieceCount = 0;
			splitIntoPieces(pContext, pieces, &pieceCount, pEntry, pJobArgs,
			                &ruvmFaceInfo, pEdgeVerts, entryCount, &pPieceArray);
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
			if (!pEntry) {
				continue;
			}
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
			int32_t useFullSort = 0;
			Vec2 centre = {0};
			if (seamFace) {
				int32_t totalVerts = 0;
				getRuvmVerts(ruvmVerts, pJobArgs, pEntry, &seamLoopCount, seamLoops,
							 &ruvmFaceInfo, pMap, pContext, pEdgeVerts, &useFullSort,
							 &centre, &totalVerts);
				_(&centre V2DIVSEQL (float)totalVerts);
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
												 seamLoops, centre, pEdgeVerts);
				if (loopBufferSize <= 2) {
					continue;
				}
				pMeshOut->loopCount += loopBufferSize;
				pMeshOut->faceCount++;
				
				int32_t indexTable[13];
				indexTable[0] = -1;
				if (seamFace) {
					//full winding sort
					sortLoopsFull(indexTable + 1, loopBufferSize, uvBuffer, centre);
				}
				else {
					sortLoops(ruvmIndicesSort + 1, indexTable + 1, loopBufferSize);
				}
				for (int32_t k = 0; k < loopBufferSize; ++k) {
					pMeshOut->pLoops[loopBase + k] = loopBuffer[indexTable[k + 1]];
					pMeshOut->pUvs[loopBase + k] = uvBuffer[indexTable[k + 1]];
				}
				loopBase += loopBufferSize;
			} while(runAgain);
		}
		if (pPieceArray) {
			pContext->alloc.pFree(pPieceArray);
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
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		for (int32_t hash = 0; hash < pJobArgs[i].boundaryBufferSize; ++hash) {
			BoundaryDir *pEntryDir = pJobArgs[i].pBoundaryBuffer + hash;
			int32_t depth = 0;
			do {
				if (pEntryDir->pEntry) {
					BoundaryVert *pChild = pEntryDir->pEntry->pNext;
					while (pChild) {
						pChild = pChild->pNext;
					}
					int32_t faceIndex = pEntryDir->pEntry->faceIndex;
					for (int32_t j = i + 1; j < pContext->threadCount; ++j) {
						BoundaryDir *pEntryDirOther = pJobArgs[j].pBoundaryBuffer + hash;
						do {
							if (pEntryDirOther->pEntry) {
								if (faceIndex == pEntryDirOther->pEntry->faceIndex) {
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
