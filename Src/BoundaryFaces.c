#include <RUVM.h>
#include <MapFile.h>
#include <Mesh.h>
#include <Context.h>

static void initEdgeTableEntry(EdgeTable *pEdgeEntry, BoundaryVert *pEntry, Mesh *pMeshOut,
                        WorkMesh *pLocalMesh, int32_t ruvmVert, int32_t vert) {
	pMeshOut->pVerts[pMeshOut->vertCount] =
		pLocalMesh->pVerts[vert];
	pEdgeEntry->ruvmVert = ruvmVert;
	pEdgeEntry->vert = pMeshOut->vertCount;
	pMeshOut->vertCount++;
	pEdgeEntry->tile = pEntry->tile;
	pEdgeEntry->loops = 1;
}

static int32_t addFaceLoopsAndVertsToBuffer(RuvmContext pContext, RuvmMap pMap, SendOffArgs *pJobArgs,
                                         Mesh *pMeshOut, BoundaryVert **ppEntry,
                                         int32_t seamFace, int32_t *loopBufferSize, int32_t *pLoopBuffer,
								         Vec2 *pUvBuffer, FaceInfo *ruvmFaceInfo, int32_t edgeTableSize,
								         EdgeTable *pEdgeTable, int32_t *pRuvmIndicesSort,
										 int32_t hasPreservedEdge) {
	int32_t ruvmLastLoop = ruvmFaceInfo->size - 1;
	int32_t hashBuffer[32];
	int32_t hashBufferSize = 0;
	BoundaryVert *pEntry = *ppEntry;
	do {
		WorkMesh *localMesh = &pJobArgs[pEntry->job].localMesh;
		int32_t *nonRuvmSort = pEntry->firstVert < 0 ?
			(int32_t *)(pEntry + 1) : NULL;
		int32_t ruvmIndicesLocal[64];
		int32_t loopBufferSizeLocal = 0;
		int32_t ruvmLoopsAdded = 0;
		int32_t faceStart = localMesh->pFaces[pEntry->face];
		int32_t faceEnd = localMesh->pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		int32_t mostRecentRuvmLoop;
		int32_t priorRuvmLoop = pEntry->firstVert - 1;
		if (priorRuvmLoop < 0) {
			priorRuvmLoop = ruvmLastLoop;
		}
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = localMesh->pLoops[faceStart - k];
			if (vert >= localMesh->vertCount) {
				ruvmIndicesLocal[loopBufferSizeLocal++] = -1;
				if (!seamFace && !hasPreservedEdge) {
					continue;
				}
				if (hasPreservedEdge) {
					pMeshOut->pVerts[pMeshOut->vertCount] =
						localMesh->pVerts[vert];
					vert = pMeshOut->vertCount;
					pMeshOut->vertCount++;
				}
				else {
					int32_t nextVertIndex = (k + 1) % loopAmount;
					int32_t lastVertIndex = (k - 1) % loopAmount;
					int32_t nextVert = localMesh->pLoops[faceStart - nextVertIndex];
					int32_t lastVert = localMesh->pLoops[faceStart - lastVertIndex];
					if (lastVert >= localMesh->vertCount && nextVert >= localMesh->vertCount) {
						//continue;
					}
					int32_t ruvmLocal;
					int32_t ruvmNextLocal;
					if (nonRuvmSort) {
						ruvmLocal = nonRuvmSort[k];
						ruvmNextLocal = ruvmLocal - 1;
						if (ruvmNextLocal < 0) {
							ruvmNextLocal = ruvmLastLoop;
						}
					}
					else {
						int32_t notDouble = k ? ruvmIndicesLocal[k - 1] >= 0 : 1;
						ruvmLocal = ruvmLoopsAdded && notDouble ?
							mostRecentRuvmLoop : priorRuvmLoop;
						ruvmNextLocal = (ruvmLocal + 1) % ruvmFaceInfo->size;
					}
					pRuvmIndicesSort[*loopBufferSize] = ruvmNextLocal * 10 - 5;
					int32_t ruvmVert = pMap->mesh.pLoops[ruvmFaceInfo->start + ruvmLocal];
					int32_t ruvmNextVert = pMap->mesh.pLoops[ruvmFaceInfo->start + ruvmNextLocal];
					uint32_t ruvmEdgeId = ruvmVert + ruvmNextVert;
					int32_t hash = ruvmFnvHash((uint8_t *)&ruvmEdgeId, 4, edgeTableSize);
					int32_t dup = 0;
					for (int32_t l = 0; l < hashBufferSize; ++l) {
						if (hash == hashBuffer[l]) {
							dup = 1;
							break;
						}
					}
					if (dup) {
						continue;
					}
					hashBuffer[hashBufferSize] = hash;
					hashBufferSize++;
					EdgeTable *pEdgeEntry = pEdgeTable + hash;
					if (!pEdgeEntry->loops) {
						initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, localMesh,
										   ruvmVert, vert);
						vert = pEdgeEntry->vert;
					}
					else {
						do {
							int32_t match = (pEdgeEntry->ruvmVert == ruvmVert ||
											 pEdgeEntry->ruvmVert == ruvmNextVert) &&
											 pEdgeEntry->tile == pEntry->tile;
							if (match) {
								vert = pEdgeEntry->vert;
								break;
							}
							if (!pEdgeEntry->pNext) {
								pEdgeEntry = pEdgeEntry->pNext = pContext->alloc.pCalloc(1, sizeof(EdgeTable));
								initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, localMesh,
												   ruvmVert, vert);
								vert = pEdgeEntry->vert;
								break;
							}
							pEdgeEntry = pEdgeEntry->pNext;
						} while(1);
					}
				}
			}
			else {
				vert += pJobArgs[pEntry->job].vertBase;
				int32_t sortPos;
				int32_t offset = ruvmIndicesLocal[k - 1] < 0 && ruvmLoopsAdded ?
					1 : 0;
				sortPos = !pEntry->firstVert && offset && pEntry->type == 2 ?
					pEntry->lastVert : pEntry->firstVert + ruvmLoopsAdded + offset;
				mostRecentRuvmLoop = sortPos;
				ruvmIndicesLocal[loopBufferSizeLocal++] = sortPos;
				pRuvmIndicesSort[*loopBufferSize] = sortPos * 10;
				ruvmLoopsAdded++;
			}
			pLoopBuffer[*loopBufferSize] = vert;
			pUvBuffer[*loopBufferSize] = localMesh->pUvs[faceStart - k];
			(*loopBufferSize)++;
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
		int32_t vertRuvmIndices[64];
		ruvmIndicesSort[0] = -1;
		vertRuvmIndices[0] = -1;
		int32_t loopBufferSize;
		int32_t loopBuffer[128];
		Vec2 uvBuffer[128];
		FaceInfo ruvmFaceInfo;
		ruvmFaceInfo.start = pMap->mesh.pFaces[pEntry->faceIndex];
		ruvmFaceInfo.end = pMap->mesh.pFaces[pEntry->faceIndex + 1];
		ruvmFaceInfo.size = ruvmFaceInfo.end - ruvmFaceInfo.start;
		int32_t runAgain;
		int32_t hasPreservedEdge = pPreservedEdgeFlag[j];
		if (pMeshOut->faceCount != 90969) {
			//hasPreservedEdge = 0;
		}
		else {
			int a = 0;
		}
		do {
			loopBufferSize = 0;
			pMeshOut->pFaces[pMeshOut->faceCount] = loopBase;
			runAgain = 
				addFaceLoopsAndVertsToBuffer(pContext, pMap, pJobArgs, pMeshOut, &pEntry,
										     seamFace, &loopBufferSize, loopBuffer,
										     uvBuffer, &ruvmFaceInfo, edgeTableSize,
										     pEdgeTable, ruvmIndicesSort + 1,
										     hasPreservedEdge);
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
