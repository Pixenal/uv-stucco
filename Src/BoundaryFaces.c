#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

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
	int32_t edges[11];
	int32_t entryIndex;
} Piece;

typedef struct {
	BoundaryVert *pEntry;
	int32_t seamFace;
	int32_t infoBufSize;
	int32_t loopBufferSize;
	int32_t bufLoopBuffer[11];
	int32_t bufFaceBuffer[11];
	int32_t loopBuffer[11];
	int32_t edgeBuffer[11];
	Vec3 normalBuffer[11];
	Vec2 uvBuffer[11];
	int32_t ruvmIndicesSort[11];
	int32_t ruvmOnlySort[11];
	int32_t seamLoops[32];
	int32_t seamLoopCount;
	FaceInfo ruvmFace;
	Vec2 centre;
	Mat3x3 tbnInv;
} AddFaceVars;

static void addEntryToSharedEdgeTable(RuvmContext pContext, BoundaryVert *pEntry,
                                      SharedEdge *pSharedEdges, Piece *pEntries,
									  int32_t tableSize, int32_t entryIndex,
                                      SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts) {
	BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
	int32_t faceStart = pBufMesh->mesh.pFaces[pEntry->face];
	int32_t faceEnd = pBufMesh->mesh.pFaces[pEntry->face - 1];
	int32_t loopAmount = faceStart - faceEnd;
	for (int32_t i = 0; i < loopAmount; ++i) {
		int32_t vert = pBufMesh->mesh.pLoops[faceStart - i];
		int32_t baseLoopLocal = pEntry->baseLoop >> i * 2 & 3;
		if (vert < pBufMesh->mesh.vertCount) {
			//ruvm loop - skip
			continue;
		}
		int32_t baseFaceStart = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace];
		int32_t baseFaceEnd = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace + 1];
		int32_t baseFaceSize = baseFaceEnd - baseFaceStart;
		int32_t baseLoop = baseFaceStart + baseLoopLocal;
		int32_t nextBaseLoop = baseFaceStart + ((baseLoopLocal + 1) % baseFaceSize);
		int32_t baseEdge = pJobArgs[pEntry->job].mesh.mesh.pEdges[baseLoop];
		if (checkIfEdgeIsPreserve(&pJobArgs[0].mesh, baseEdge)) {
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
		Vec2 uv = *attribAsV2(pJobArgs[0].mesh.pUvs, nextBaseLoop);
		Vec2 uvOther = *attribAsV2(pJobArgs[0].mesh.pUvs, otherLoop);
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

static void initLocalEdgeTableEntry(EdgeTable *pEdgeEntry, int32_t ruvmEdge,
                                    BoundaryVert *pEntry, int32_t baseEdge, int32_t baseVert,
									int32_t keepBaseLoop, int32_t loop) {
	pEdgeEntry->ruvmEdge = ruvmEdge;
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
									int32_t faceStart, int32_t ruvmLoop, int32_t k) {
	int32_t baseLoopLocal = pEntry->baseLoop >> k * 2 & 3;
	FaceInfo baseFace;
	baseFace.index = pEntry->baseFace;
	baseFace.start = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace];
	baseFace.end = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace + 1];
	baseFace.size = baseFace.end - baseFace.start;
	int32_t baseLoop = baseFace.start + baseLoopLocal;
	int32_t isBaseLoop = (pEntry->onLine >> k & 1) && !(pEntry->isRuvm >> k & 1);
	int32_t baseEdge = pJobArgs[pEntry->job].mesh.mesh.pEdges[baseLoop];
	int32_t baseVert = isBaseLoop ? pJobArgs[pEntry->job].mesh.mesh.pLoops[baseLoop] : -1;

	int32_t keepBaseLoop = 0;
	if (isBaseLoop) {
		if (pJobArgs[0].pVertSeamTable[baseVert] > 2 ||
			(pJobArgs[0].pVertSeamTable[baseVert] && pJobArgs[0].pInVertTable[baseVert])) {

			keepBaseLoop = 1;
		}
	}		

	int32_t hash, ruvmEdge;
	if (isBaseLoop) {
		hash = ruvmFnvHash((uint8_t *)&baseVert, 4, 16);
		ruvmEdge = -1;
	}
	else {
		ruvmEdge = pMap->mesh.mesh.pEdges[pAfVars->ruvmFace.start + ruvmLoop];
		hash = ruvmFnvHash((uint8_t *)&ruvmEdge, 4, 16);
	}
	EdgeTable *pEdgeEntry = localEdgeTable + hash;
	if (!pEdgeEntry->loops) {
		initLocalEdgeTableEntry(pEdgeEntry, ruvmEdge, pEntry, baseEdge,
		                        baseVert, keepBaseLoop, faceStart - k);
	}
	else {
		do {
			int32_t	match;
			if (isBaseLoop) {
				match = pEdgeEntry->baseVert == baseVert;
			}
			else {
				match =  pEdgeEntry->ruvmEdge == ruvmEdge &&
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
					initLocalEdgeTableEntry(pEdgeEntry, ruvmEdge, pEntry, baseEdge,
					                        baseVert, keepBaseLoop, faceStart - k);
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
				BufMesh *pBufMesh = &pJobArgs[pEdgeEntry->job].bufMesh;
				_(&pAfVars->centre V2ADDEQL *attribAsV2(pBufMesh->pUvs, pEdgeEntry->vert));
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
		BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
		int32_t faceStart = pBufMesh->mesh.pFaces[pEntry->face];
		int32_t faceEnd = pBufMesh->mesh.pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = pBufMesh->mesh.pLoops[faceStart - k];
			int32_t ruvmLoop = pEntry->ruvmLoop >> k * 3 & 7;
			if (vert > pBufMesh->mesh.vertCount) {

				addLoopToLocalEdgeTable(pContext, localEdgeTable, pAfVars, pJobArgs,
				                        pEntry, pMap, faceStart, ruvmLoop, k);
			}
			else {
				_(&pAfVars->centre V2ADDEQL *attribAsV2(pBufMesh->pUvs, faceStart - k));
				++*pTotalVerts;
				vert += pJobArgs[pEntry->job].vertBase;
			}
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
	
	addLoopsWithSingleVert(pContext, pAfVars, pJobArgs, localEdgeTable, pTotalVerts);
}

static int32_t getEdgeLocalLoop(int32_t *pEdgeLoops, FaceInfo *pBaseFace) {
	for (int32_t i = pBaseFace->start; i < pBaseFace->end; ++i) {
		if (i == pEdgeLoops[0] || i == pEdgeLoops[1]) {
			return i;
		}
	}
	printf("Couldn't find loop for edge (winding compare for bounding face\n)");
	abort();
}

static void initEdgeTableEntry(EdgeTable *pEdgeEntry, BoundaryVert *pEntry,
                               Mesh *pMeshOut, BufMesh *pBufMesh, int32_t ruvmEdge,
							   int32_t *pVert, int32_t baseEdge, int32_t baseVert,
                               int32_t loopIndex, int32_t ruvmFace, Vec2 *pCentre,
							   FaceInfo baseFace, int32_t baseLoopLocalAbs,
							   SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
							   int32_t isBaseLoop, int32_t loop) {
	copyAllAttribs(pMeshOut->mesh.pVertAttribs,
				   pMeshOut->mesh.vertCount,
				   pBufMesh->mesh.pVertAttribs,
				   *pVert,
				   pMeshOut->mesh.vertAttribCount);
	*pVert = pMeshOut->mesh.vertCount;
	pEdgeEntry->vert = pMeshOut->mesh.vertCount;
	pMeshOut->mesh.vertCount++;
	pEdgeEntry->tile = pEntry->tile;
	pEdgeEntry->ruvmEdge = ruvmEdge;
	pEdgeEntry->loops = 1;
	pEdgeEntry->baseEdge = baseEdge;
	pEdgeEntry->baseVert = baseVert;
	pEdgeEntry->loopIndex = loopIndex;
	pEdgeEntry->ruvmFace = ruvmFace;
	pEdgeEntry->loop = loop;
}

static void initSeamEdgeTableEntry(SeamEdgeTable *pSeamEntry, Mesh *pMeshOut,
                                   BufMesh *pBufMesh, int32_t *pEdge,
								   int32_t inEdge, int32_t mapFace) {
	copyAllAttribs(pMeshOut->mesh.pEdgeAttribs,
				   pMeshOut->mesh.edgeCount,
				   pBufMesh->mesh.pEdgeAttribs,
				   *pEdge,
				   pMeshOut->mesh.edgeAttribCount);
	*pEdge = pMeshOut->mesh.edgeCount;
	pMeshOut->mesh.edgeCount++;
	pSeamEntry->edge = *pEdge;
	pSeamEntry->inEdge = inEdge;
	pSeamEntry->mapFace = mapFace;
}

static FaceInfo getBaseFace(BoundaryVert *pEntry, SendOffArgs *pJobArgs) {
	FaceInfo face;
	face.index = pEntry->baseFace;
	face.start = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace];
	face.end = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace + 1];
	face.size = face.end - face.start;
	return face;
}

static void addBoundaryLoopAndVert(RuvmContext pContext, RuvmMap pMap,
                                   AddFaceVars *pAfVars, int32_t *pVert,
                                   EdgeVerts *pEdgeVerts, BoundaryVert *pEntry,
								   SendOffArgs *pJobArgs, EdgeTable *pEdgeTable,
									SeamEdgeTable *pSeamEdgeTable,
								   int32_t edgeTableSize, int32_t seamTableSize, Mesh *pMeshOut,
								   int32_t k, int32_t ruvmLoop, int32_t *pEdge, int32_t loop) {
	FaceInfo baseFace = getBaseFace(pEntry, pJobArgs);
	int32_t baseLoopLocal = pEntry->baseLoop >> k * 2 & 3;
	int32_t isBaseLoop = (pEntry->onLine >> k & 1) && !(pEntry->isRuvm >> k & 1);
	int32_t baseLoop = baseFace.start + baseLoopLocal;
	int32_t baseEdge = pJobArgs[pEntry->job].mesh.mesh.pEdges[baseLoop];
	int32_t baseVert = isBaseLoop ? pJobArgs[pEntry->job].mesh.mesh.pLoops[baseLoop] : -1;
	int32_t hash, ruvmEdge;
	BufMesh *pBufMesh = &pJobArgs[pEntry->job].bufMesh;
	if (isBaseLoop) {
		hash = ruvmFnvHash((uint8_t *)&baseVert, 4, edgeTableSize);
		ruvmEdge = -1;
	}
	else {
		ruvmEdge = pMap->mesh.mesh.pEdges[pAfVars->ruvmFace.start + ruvmLoop];
		hash = ruvmFnvHash((uint8_t *)&ruvmEdge, 4, edgeTableSize);
	}
	EdgeTable *pEdgeEntry = pEdgeTable + hash;
	if (!pEdgeEntry->loops) {
		initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, pBufMesh, ruvmEdge,
		                   pVert, baseEdge, baseVert, baseLoop,
						   pEntry->faceIndex, &pAfVars->centre, baseFace,
						   baseLoopLocal, pJobArgs, pEdgeVerts, isBaseLoop, loop);
	}
	else {
		do {
			//TODO ideally, we'd just compare the edges, but maps don't have
			//edges currently, so I'll remove ruvmVert/ruvmVertNext,
			//and replace them with a single edge index, once edges are
			//added the ruvm maps.
			int32_t match;
			if (isBaseLoop) {
				Vec2 *pMeshInUvA = attribAsV2(pJobArgs[0].mesh.pUvs, pEdgeEntry->loopIndex);
				Vec2 *pMeshInUvB = attribAsV2(pJobArgs[0].mesh.pUvs, baseLoop);
				match = pEdgeEntry->baseVert == baseVert &&
						pEdgeEntry->ruvmFace == pEntry->faceIndex &&
						pMeshInUvA->x == pMeshInUvB->x &&
						pMeshInUvA->y == pMeshInUvB->y;
			}
			else {
				int32_t connected = 
					//TODO set pMeshOut->pVerts, when meshout is allocated.
					//It's currently not set
					_(*attribAsV2(pJobArgs[pEntry->job].bufMesh.pUvs, loop) V2APROXEQL
					  *attribAsV2(pJobArgs[pEntry->job].bufMesh.pUvs, pEdgeEntry->loop));
				match =  pEdgeEntry->ruvmEdge == ruvmEdge &&
						 pEdgeEntry->tile == pEntry->tile &&
						 pEdgeEntry->baseEdge == baseEdge &&
						 connected;
			}
			if (match) {
				*pVert = pEdgeEntry->vert;
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry = pEdgeEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(EdgeTable));
				initEdgeTableEntry(pEdgeEntry, pEntry, pMeshOut, pBufMesh, ruvmEdge,
								   pVert, baseEdge, baseVert, baseLoop,
								   pEntry->faceIndex, &pAfVars->centre, baseFace,
								   baseLoopLocal, pJobArgs, pEdgeVerts, isBaseLoop, loop);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
	}
	uint32_t valueToHash = baseEdge + pEntry->faceIndex;
	hash = ruvmFnvHash((uint8_t *)&valueToHash, 4, seamTableSize);
	SeamEdgeTable *pSeamEntry = pSeamEdgeTable + hash;
	if (!pSeamEntry->valid) {
		initSeamEdgeTableEntry(pSeamEntry, pMeshOut, pBufMesh, pEdge, baseEdge,
		                       pEntry->faceIndex);
	}
	else {
		do {
			if (pSeamEntry->inEdge == baseEdge &&
				pSeamEntry->mapFace == pEntry->faceIndex) {
				*pEdge = pSeamEntry->edge;
				break;
			}
			if (!pSeamEntry->pNext) {
				pSeamEntry = pSeamEntry->pNext =
					pContext->alloc.pCalloc(1, sizeof(SeamEdgeTable));
				initSeamEdgeTableEntry(pSeamEntry, pMeshOut, pBufMesh, pEdge,
				                       baseEdge, pEntry->faceIndex);
				break;
			}
			pSeamEntry = pSeamEntry->pNext;
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

static int32_t checkIfDup(AddFaceVars *pAfVars, int32_t ruvmLoopsAdded,
                          int32_t ruvmLoop) {
	for (int32_t i = 0; i < ruvmLoopsAdded; ++i) {
		if (ruvmLoop == pAfVars->ruvmOnlySort[i]) {
			return 1;
		}
	}
	return 0;
}

static void initOnLineTableEntry(OnLineTable *pEntry, Mesh *pMeshOut,
                                 BufMesh *pBufMesh, int32_t base,
								 int32_t isBaseLoop, int32_t ruvmVert,
								 int32_t *pVert) {
	copyAllAttribs(pMeshOut->mesh.pVertAttribs,
				   pMeshOut->mesh.vertCount,
				   pBufMesh->mesh.pVertAttribs,
				   *pVert,
				   pMeshOut->mesh.vertAttribCount);
	*pVert = pMeshOut->mesh.vertCount;
	pMeshOut->mesh.vertCount++;
	pEntry->outVert = *pVert;
	pEntry->baseEdgeOrLoop = base;
	pEntry->ruvmVert = ruvmVert;
	pEntry->type = isBaseLoop + 1;
}

static void addLoopsToBufferAndVertsToMesh(RuvmContext pContext, RuvmMap pMap,
                                           AddFaceVars *pAfVars, SendOffArgs *pJobArgs,
                                           Mesh *pMeshOut, int32_t edgeTableSize,
								           int32_t seamTableSize, EdgeTable *pEdgeTable,
										   SeamEdgeTable *pSeamEdgeTable, EdgeVerts *pEdgeVerts,
										   OnLineTable *pOnLineTable, int32_t onLineTableSize) {
	BoundaryVert *pEntry = pAfVars->pEntry;
	int32_t totalRuvmLoopsAdded = 0;
	do {
		BufMesh *bufMesh = &pJobArgs[pEntry->job].bufMesh;
		int32_t ruvmLoopsAdded = 0;
		int32_t faceStart = bufMesh->mesh.pFaces[pEntry->face];
		int32_t faceEnd = bufMesh->mesh.pFaces[pEntry->face - 1];
		//int32_t faceSize = faceStart - faceEnd;
		int32_t loopAmount = faceStart - faceEnd;
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = bufMesh->mesh.pLoops[faceStart - k];
			int32_t edge = bufMesh->mesh.pEdges[faceStart - k];
			int32_t ruvmLoop = pEntry->ruvmLoop >> k * 3 & 7;

			int32_t vertNoOffset;
			int32_t isRuvm = pEntry->isRuvm >> k & 1;
			if (!isRuvm) {
				//is not an ruvm loop (is an intersection, or base loop))
				if (checkIfShouldSkip(pAfVars, faceStart, k)) {
					continue;
				}
				addBoundaryLoopAndVert(pContext, pMap, pAfVars, &vert, pEdgeVerts,
				                       pEntry, pJobArgs, pEdgeTable, pSeamEdgeTable,
									   edgeTableSize, seamTableSize, pMeshOut, k, ruvmLoop,
									   &edge, faceStart - k);
				vertNoOffset = vert;
				pAfVars->ruvmIndicesSort[pAfVars->loopBufferSize + 1] = -1;
			}
			else {
				//is an ruvm loop (this includes ruvm loops sitting on base edges or verts)

				//add an item to pEntry in mapToMesh, which denotes if an ruvm
				//loop has a dot of 0 (is on a base edge).
				//Then add it to the edgetable if so, without calcing a wind of course.
				//Just use the base edge as the hash, instead of an ruvm edge (cause there isnt one).
				//Or just make a new hash table just for ruvm loops with zero dot.
				//That would probably be cleaner, and more memory concious tbh.
				int32_t onLine = pEntry->onLine >> k & 1;
				if (onLine) {
					if (checkIfDup(pAfVars, totalRuvmLoopsAdded, ruvmLoop)) {
						continue;
					}
					int32_t ruvmVert = pMap->mesh.mesh.pLoops[pAfVars->ruvmFace.start + ruvmLoop];
					FaceInfo baseFace = getBaseFace(pEntry, pJobArgs);
					int32_t baseLoopLocal = pEntry->baseLoop >> k * 2 & 3;
					int32_t isBaseLoop = (pEntry->onLine >> k & 1) && !(pEntry->isRuvm >> k & 1);
					int32_t baseLoop = baseFace.start + baseLoopLocal;
					int32_t baseEdge = pJobArgs[pEntry->job].mesh.mesh.pEdges[baseLoop];
					int32_t base = isBaseLoop ? baseLoop : baseEdge;
					int32_t hash = ruvmFnvHash((uint8_t *)&base, 4, onLineTableSize);
					OnLineTable *pOnLineEntry = pOnLineTable + hash;
					if (!pOnLineEntry->type) {
						initOnLineTableEntry(pOnLineEntry, pMeshOut,
						                     &pJobArgs[pEntry->job].bufMesh,
											 base, isBaseLoop, ruvmVert, &vert);
					}
					else {
						do {
							int32_t match = base == pOnLineEntry->baseEdgeOrLoop &&
											ruvmVert == pOnLineEntry->ruvmVert &&
											isBaseLoop + 1 == pOnLineEntry->type;
							if (match) {
								vert = pOnLineEntry->outVert;
								break;
							}
							if (!pOnLineEntry->pNext) {
								pOnLineEntry = pOnLineEntry->pNext =
									pContext->alloc.pCalloc(1, sizeof(OnLineTable));
								initOnLineTableEntry(pOnLineEntry, pMeshOut,
													 &pJobArgs[pEntry->job].bufMesh,
													 base, isBaseLoop, ruvmVert, &vert);
								break;
							}
							pOnLineEntry = pOnLineEntry->pNext;
						} while(pOnLineEntry);
					}
				}
				//the vert and edge indices are local to the buffer mesh,
				//so we need to offset them, so that they point to the
				//correct position in the out mesh. (these vars are set
				//when the non-boundary mesh data is copied
				vertNoOffset = vert;
				vert += pJobArgs[pEntry->job].vertBase;
				edge += pJobArgs[pEntry->job].edgeBase;
				
				pAfVars->ruvmIndicesSort[pAfVars->loopBufferSize + 1] = ruvmLoop * 10;
				pAfVars->ruvmOnlySort[totalRuvmLoopsAdded] = ruvmLoop;
				ruvmLoopsAdded++;
				totalRuvmLoopsAdded++;
			}
			//if boundary loop, or if loop edge has been intersected,
			//add new edge to mesh
			//int32_t kNext = (k + 1) % faceSize;
			//int32_t vertNext = bufMesh->mesh.pLoops[faceStart - kNext];
			//if (boundaryLoop || vertNext >= bufMesh->mesh.vertCount) {
			//}
			pAfVars->bufLoopBuffer[pAfVars->loopBufferSize] = faceStart - k;
			pAfVars->bufFaceBuffer[pAfVars->loopBufferSize] = pEntry->face;
			pAfVars->loopBuffer[pAfVars->loopBufferSize] = vert;
			pAfVars->edgeBuffer[pAfVars->loopBufferSize] = edge;
			pAfVars->uvBuffer[pAfVars->loopBufferSize] =
				*attribAsV2(bufMesh->pUvs, faceStart - k);
			pAfVars->normalBuffer[pAfVars->loopBufferSize] =
				*attribAsV3(bufMesh->pNormals, faceStart - k);
			pAfVars->loopBufferSize++;
			pAfVars->infoBufSize++;
		}
		BoundaryVert *pNext = pEntry->pNext;
		pContext->alloc.pFree(pEntry);
		pEntry = pNext;
	} while(pEntry);
}

static void sortLoopsFull(int32_t *pIndexTable, AddFaceVars *pAfVars,
                          Mesh *pMeshOut) {
	//insertion sort
	Vec2 vertBuf[17];
	Vec2 centre = {0};
	for (int32_t i = 0; i < pAfVars->loopBufferSize; ++i) {
		Vec3* pVert = attribAsV3(pMeshOut->pVerts, pAfVars->loopBuffer[i]);
		Vec3 vertV3 = vec3MultiplyMat3x3(*pVert, &pAfVars->tbnInv);
		vertBuf[i].x = vertV3.x;
		vertBuf[i].y = vertV3.y;
		_(&centre V2ADDEQL vertBuf[i]);
	}
	_(&centre V2DIVSEQL pAfVars->loopBufferSize);
	int32_t order = vec2WindingCompare(vertBuf[0], vertBuf[1],
	                                   centre, 1);
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufferSize = 2;
	for (int32_t k = bufferSize; k < pAfVars->loopBufferSize; ++k) {
		int32_t l, insert;
		for (l = bufferSize - 1; l >= 0; --l) {
			if (l != 0) {
				insert =
					vec2WindingCompare(vertBuf[k],
						               vertBuf[pIndexTable[l]],
					                   centre, 1)
					&&
					vec2WindingCompare(vertBuf[pIndexTable[l - 1]],
						               vertBuf[k], centre, 1);
			}
			else {
				insert =
					vec2WindingCompare(vertBuf[k],
					                   vertBuf[pIndexTable[l]],
									   centre, 1);
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
	int32_t *pLoopSort = pAfVars->ruvmIndicesSort + 1;
	//insertion sort
	int32_t a = pLoopSort[0];
	int32_t b = pLoopSort[1];
	int32_t order = a < b;
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufferSize = 2;
	for (int32_t k = bufferSize; k < pAfVars->loopBufferSize; ++k) {
		int32_t l, insert;
		for (l = bufferSize - 1; l >= 0; --l) {
			insert = pLoopSort[k] < pLoopSort[pIndexTable[l]] &&
							 pLoopSort[k] > pLoopSort[pIndexTable[l - 1]];
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
		int32_t isRuvm = pEntry->isRuvm;
		for (int32_t i = 0; i < 11; ++i) {
			ruvmLoops += isRuvm >> i & 1;
		}
		pEntry = pEntry->pNext;
		++*pEntryNum;
	} while(pEntry);
	int32_t faceStart = pMap->mesh.mesh.pFaces[faceIndex];
	int32_t faceEnd = pMap->mesh.mesh.pFaces[faceIndex + 1];
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

static void buildApproximateTbnInverse(AddFaceVars *pAfVars,
                                       SendOffArgs *pJobArgs) {
	BoundaryVert* pEntry = pAfVars->pEntry;
	Vec3 normal = {0};
	int32_t entryCount = 0;
	do {
		BufMesh* pBufMesh = &pJobArgs[pEntry->job].bufMesh;
		int32_t faceStart = pBufMesh->mesh.pFaces[pEntry->face];
		int32_t* pLoops = pBufMesh->mesh.pLoops;
		Vec3* pVertA = attribAsV3(pBufMesh->pVerts, pLoops[faceStart]);
		Vec3* pVertB = attribAsV3(pBufMesh->pVerts, pLoops[faceStart - 1]);
		Vec3* pVertC = attribAsV3(pBufMesh->pVerts, pLoops[faceStart - 2]);
		Vec3 ab = _(*pVertB V3SUB *pVertA);
		Vec3 ac = _(*pVertC V3SUB *pVertA);
		_(&normal V3ADDEQL vec3Cross(ab, ac));
		entryCount++;
		pEntry = pEntry->pNext;
	} while (pEntry);
	_(&normal V3DIVEQLS entryCount);
	float normalLen =
		sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
	_(&normal V3DIVEQLS normalLen);
	Vec3 axis = {0};
	if (normal.z > .99f || normal.z < -.99f) {
		axis.x = 1.0f;
	}
	else {
		axis.z = 1.0f;
	}
	Vec3 tangent = vec3Cross(normal, axis);
	Vec3 bitangent = vec3Cross(normal, tangent);
	Mat3x3 tbn = mat3x3FromVec3(tangent, bitangent, normal);
	pAfVars->tbnInv = mat3x3Invert(&tbn);
}

static void mergeAndCopyEdgeFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                           SendOffArgs *pJobArgs, int32_t edgeTableSize, int32_t seamTableSize,
						   EdgeTable *pEdgeTable,
						   SeamEdgeTable *pSeamEdgeTable, int32_t allBoundaryFacesSize,
						   BoundaryVert **pAllBoundaryFaces, EdgeVerts *pEdgeVerts,
						   OnLineTable *pOnLineTable, int32_t onLineTableSize) {
	int32_t vertBase = pMeshOut->mesh.vertCount;
	for (int32_t j = 0; j < allBoundaryFacesSize; ++j) {
		BoundaryVert *pEntry = pAllBoundaryFaces[j];
		int32_t entryCount, isSeam, hasPreservedEdge;
		compileEntryInfo(pEntry, &entryCount, &isSeam, &hasPreservedEdge);
		int32_t entryNum;
		//int32_t seamFace = ;
		FaceInfo ruvmFace;
		ruvmFace.index = pEntry->faceIndex;
		ruvmFace.start = pMap->mesh.mesh.pFaces[pEntry->faceIndex];
		ruvmFace.end = pMap->mesh.mesh.pFaces[pEntry->faceIndex + 1];
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
			if (!afVars.pEntry) {
				continue;
			}
			afVars.ruvmFace = ruvmFace;
			afVars.seamFace = determineIfSeamFace(pMap, afVars.pEntry, &entryNum);
			afVars.ruvmIndicesSort[0] = -10;
			if (afVars.seamFace) {
				int32_t totalVerts = 0;
				determineLoopsToKeep(pContext, pMap, &afVars, pJobArgs, &totalVerts);
				_(&afVars.centre V2DIVSEQL (float)totalVerts);
				buildApproximateTbnInverse(&afVars, pJobArgs);
			}
			int32_t loopBase = pMeshOut->mesh.loopCount;
			pMeshOut->mesh.pFaces[pMeshOut->mesh.faceCount] = loopBase;
			addLoopsToBufferAndVertsToMesh(pContext, pMap, &afVars, pJobArgs, pMeshOut,
										   edgeTableSize, seamTableSize, pEdgeTable, pSeamEdgeTable,
										   pEdgeVerts, pOnLineTable, onLineTableSize);
			if (afVars.loopBufferSize <= 2) {
				continue;
			}
			pMeshOut->mesh.loopCount += afVars.loopBufferSize;
			
			int32_t indexTable[17];
			indexTable[0] = -1;
			if (afVars.seamFace) {
				//full winding sort
				sortLoopsFull(indexTable + 1, &afVars, pMeshOut);
			}
			else {
				sortLoops(indexTable + 1, &afVars);
			}
			for (int32_t k = 0; k < afVars.loopBufferSize; ++k) {
				pMeshOut->mesh.pLoops[loopBase + k] =
					afVars.loopBuffer[indexTable[k + 1]];
				pMeshOut->mesh.pEdges[loopBase + k] = 
					afVars.edgeBuffer[indexTable[k + 1]];
				int32_t bufLoop = afVars.bufLoopBuffer[indexTable[k + 1]];
				copyAllAttribs(pMeshOut->mesh.pLoopAttribs,
				               loopBase + k,
							   pJobArgs[0].bufMesh.mesh.pLoopAttribs,
							   bufLoop,
				               pMeshOut->mesh.loopAttribCount);
				//*attribAsV3(pMeshOut->pNormals, loopBase + k) =
				//	afVars.normalBuffer[indexTable[k + 1]];
				//*attribAsV2(pMeshOut->pUvs, loopBase + k) =
				//	afVars.uvBuffer[indexTable[k + 1]];
			}
			copyAllAttribs(pMeshOut->mesh.pFaceAttribs,
						   pMeshOut->mesh.faceCount,
						   pJobArgs[0].bufMesh.mesh.pFaceAttribs,
						   afVars.bufFaceBuffer[0],
						   pMeshOut->mesh.faceAttribCount);
			pMeshOut->mesh.faceCount++;
		}
		if (pPieces) {
			pContext->alloc.pFree(pPieces);
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
	int32_t totalBoundaryEdges = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		totalBoundaryFaces += pJobArgs[i].totalBoundaryFaces;
		totalBoundaryEdges += pJobArgs[i].totalBoundaryEdges;
	}
	BoundaryVert **pAllBoundaryFaces = pContext->alloc.pMalloc(sizeof(void *) * totalBoundaryFaces);
	int32_t allBoundaryFacesSize = 0;
	compileEntriesFromJobs(pContext, pJobArgs, pAllBoundaryFaces, &allBoundaryFacesSize);
	EdgeTable *pEdgeTable =
		pContext->alloc.pCalloc(totalBoundaryFaces, sizeof(EdgeTable));
	OnLineTable *pOnLineTable =
		pContext->alloc.pCalloc(totalBoundaryFaces / 2, sizeof(OnLineTable));
	SeamEdgeTable *pSeamEdgeTable =
		pContext->alloc.pCalloc(totalBoundaryEdges, sizeof(SeamEdgeTable));
	int32_t edgeTableSize = totalBoundaryFaces;
	int32_t onLineTableSize = totalBoundaryFaces / 2;
	int32_t seamTableSize = totalBoundaryEdges;
	for (int32_t i = 0; i < pJobArgs[0].mesh.mesh.vertCount; ++i) {
		int32_t preserve = pJobArgs[0].pInVertTable[i];
		for (int32_t j = 1; j < pContext->threadCount; ++j) {
			preserve |= pJobArgs[j].pInVertTable[i];
		}
		pJobArgs[0].pInVertTable[i] = preserve;
	}
	mergeAndCopyEdgeFaces(pContext, pMap, pMeshOut, pJobArgs, edgeTableSize, seamTableSize,
	                      pEdgeTable, pSeamEdgeTable, allBoundaryFacesSize, pAllBoundaryFaces,
						  pEdgeVerts, pOnLineTable, onLineTableSize);
	
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
	for (int32_t i = 0; i < onLineTableSize; ++i) {
		OnLineTable *pEntry = pOnLineTable[i].pNext;
		while (pEntry) {
			OnLineTable *pNextEntry = pEntry->pNext;
			pContext->alloc.pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	pContext->alloc.pFree(pOnLineTable);
	for (int32_t i = 0; i < seamTableSize; ++i) {
		SeamEdgeTable *pEntry = pSeamEdgeTable[i].pNext;
		while (pEntry) {
			SeamEdgeTable *pNextEntry = pEntry->pNext;
			pContext->alloc.pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	pContext->alloc.pFree(pSeamEdgeTable);
}
