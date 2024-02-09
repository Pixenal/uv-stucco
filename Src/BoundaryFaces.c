#include <RUVM.h>
#include <MapFile.h>
#include <Mesh.h>
#include <Context.h>
#include <math.h>
#include <float.h>

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
                         BoundaryVert *pEntry) {
	do {
		WorkMesh *localMesh = &pJobArgs[pEntry->job].localMesh;
		int32_t faceStart = localMesh->pFaces[pEntry->face];
		int32_t faceEnd = localMesh->pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = localMesh->pLoops[faceStart - k];
			int32_t sort;
			//sort is stored in first 4 bits,
			//and base loop is stored in the last 4
			sort = pEntry->baseLoops[k] & 15; //zero last 4 bits;
			if (vert < localMesh->vertCount) {
				vert += pJobArgs[pEntry->job].vertBase;
				pRuvmVerts[sort] = vert;
			}
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
}


static int32_t addFaceLoopsAndVertsToBuffer(RuvmContext pContext, RuvmMap pMap, SendOffArgs *pJobArgs,
                                         Mesh *pMeshOut, BoundaryVert **ppEntry,
                                         int32_t seamFace, int32_t *loopBufferSize, int32_t *pLoopBuffer,
								         Vec2 *pUvBuffer, FaceInfo *ruvmFaceInfo, int32_t edgeTableSize,
								         EdgeTable *pEdgeTable, int32_t *pRuvmIndicesSort,
										 int32_t hasPreservedEdge, int32_t *pInfoBufSize,
										 int32_t *pRuvmVerts) {
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
				if (!seamFace && !hasPreservedEdge) {
					continue;
				}
				int32_t baseEdge, baseLoop, baseLoopLocalAbs, baseLoopLocal,
						isBaseLoop, baseVert, preserve, sortNext;
				sortNext = sort;
				sort -= 1;
				if (sort < 0) {
					sort = ruvmFaceInfo->size - 1;
				}
				baseLoopLocal = pEntry->baseLoops[k] >> 4;
				isBaseLoop = baseLoopLocal < 0;
				baseLoopLocalAbs = isBaseLoop ? baseLoopLocal * -1 : baseLoopLocal;
				if (baseLoopLocalAbs != 0) {
					baseLoop = pEntry->baseLoop + baseLoopLocalAbs - 1;
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
				if ((!preserve && hasPreservedEdge) || (isBaseLoop && seamFace && 0)) {
					int32_t closestRuvm;
					float shortestDistance = FLT_MAX;
					for (int32_t l = 0; l < ruvmFaceInfo->size; ++l) {
						if (pRuvmVerts[l] < 0) {
							continue;
						}
						int32_t ruvmVertIndex = pMap->mesh.pLoops[ruvmFaceInfo->start + l];
						Vec2 ruvmVert = *(Vec2 *)(pMap->mesh.pVerts + ruvmVertIndex);
						Vec2 dir = _(ruvmVert V2SUB uv);
						float distance = pow(dir.x * dir.x + dir.y * dir.y, 2);
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
		pContext->alloc.pFree(pPrevEntry);
		if (hasPreservedEdge) {
			*ppEntry = pEntry;
			return pEntry != NULL;
		}
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
						   int8_t *pPreservedEdgeFlag) {
	for (int32_t j = 0; j < allBoundaryFacesSize; ++j) {
		int32_t loopBase = pMeshOut->loopCount;
		BoundaryVert *pEntry = pAllBoundaryFaces[j];
		int32_t entryNum;
		int32_t seamFace = determineIfSeamFace(pMap, pEntry, &entryNum);
		int32_t ruvmIndicesSort[64];
		ruvmIndicesSort[0] = -1;
		int32_t loopBufferSize;
		int32_t loopBuffer[128];
		Vec2 uvBuffer[128];
		FaceInfo ruvmFaceInfo;
		ruvmFaceInfo.start = pMap->mesh.pFaces[pEntry->faceIndex];
		ruvmFaceInfo.end = pMap->mesh.pFaces[pEntry->faceIndex + 1];
		ruvmFaceInfo.size = ruvmFaceInfo.end - ruvmFaceInfo.start;
		int32_t runAgain;
		int32_t hasPreservedEdge = pPreservedEdgeFlag[j];
		int32_t ruvmVerts[4] = {-1, -1, -1, -1};
		int32_t infoBufSize = 0;
		if (hasPreservedEdge || seamFace) {
			getRuvmVerts(ruvmVerts, pJobArgs, pEntry);
		}
		do {
			loopBufferSize = 0;
			pMeshOut->pFaces[pMeshOut->faceCount] = loopBase;
			runAgain = 
				addFaceLoopsAndVertsToBuffer(pContext, pMap, pJobArgs, pMeshOut, &pEntry,
										     seamFace, &loopBufferSize, loopBuffer,
										     uvBuffer, &ruvmFaceInfo, edgeTableSize,
										     pEdgeTable, ruvmIndicesSort + 1,
										     hasPreservedEdge, &infoBufSize, ruvmVerts);
			if (loopBufferSize <= 2) {
				continue;
			}
			pMeshOut->loopCount += loopBufferSize;
			pMeshOut->faceCount++;
			if (hasPreservedEdge) {
				for (int32_t k = 0; k < loopBufferSize; ++k) {
					pMeshOut->pLoops[loopBase + k] = loopBuffer[k];
					pMeshOut->pUvs[loopBase + k] = uvBuffer[k];
				}
			}
			else {
				int32_t indexTable[13];
				indexTable[0] = -1;
				sortLoops(ruvmIndicesSort + 1, indexTable + 1, loopBufferSize);
				for (int32_t k = 0; k < loopBufferSize; ++k) {
					pMeshOut->pLoops[loopBase + k] = loopBuffer[indexTable[k + 1]];
					pMeshOut->pUvs[loopBase + k] = uvBuffer[indexTable[k + 1]];
				}
			}
			loopBase += loopBufferSize;
		} while(runAgain);
	}
}

void ruvmMergeBoundaryFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut, SendOffArgs *pJobArgs) {
	int32_t totalBoundaryFaces = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		totalBoundaryFaces += pJobArgs[i].totalBoundaryFaces;
	}
	BoundaryVert **pAllBoundaryFaces = pContext->alloc.pMalloc(sizeof(void *) * totalBoundaryFaces);
	int8_t *pPreservedEdgeFlag = pContext->alloc.pMalloc(totalBoundaryFaces);
	int32_t allBoundaryFacesSize = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		for (int32_t hash = 0; hash < pJobArgs[i].boundaryBufferSize; ++hash) {
			BoundaryDir *pEntryDir = pJobArgs[i].pBoundaryBuffer + hash;
			int32_t depth = 0;
			do {
				if (pEntryDir->pEntry) {
					int8_t hasPreservedEdge = pEntryDir->pEntry->hasPreservedEdge;
					int32_t faceIndex = pEntryDir->pEntry->faceIndex;
					for (int32_t j = i + 1; j < pContext->threadCount; ++j) {
						BoundaryDir *pEntryDirOther = pJobArgs[j].pBoundaryBuffer + hash;
						do {
							if (pEntryDirOther->pEntry) {
								if (faceIndex == pEntryDirOther->pEntry->faceIndex) {
									hasPreservedEdge += pEntryDirOther->pEntry->hasPreservedEdge;
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
						  pPreservedEdgeFlag);
	
	pContext->alloc.pFree(pPreservedEdgeFlag);
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
