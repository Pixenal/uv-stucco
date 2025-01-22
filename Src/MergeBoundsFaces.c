#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <UvStucco.h>
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
	int32_t refIdx[2];
	int32_t edge;
	int32_t validIdx;
	int16_t corner[2];
	V2_I16 tile;
	int8_t receive;
	int8_t segment;
	bool checked : 1;
	bool preserve : 1;
	bool idx : 1;
	bool altIdx : 1;
	bool seam : 1;
	bool removed : 1;
	bool hasSegment : 1;
	bool inOrient : 1;
} SharedEdge;

typedef struct {
	SharedEdge *pEntry;
} SharedEdgeWrap;

static
void initSharedEdgeEntry(SharedEdge *pEntry, int32_t baseEdge, int32_t entryIdx,
                         int32_t refIdx, bool isPreserve, bool isReceive,
						 Piece *pPiece, int32_t i, bool seam, int32_t segment) {
	STUC_ASSERT("", baseEdge >= 0 && entryIdx >= 0 && i >= 0);
	STUC_ASSERT("", isPreserve % 2 == isPreserve); //range 0 .. 1
	STUC_ASSERT("", isReceive % 2 == isReceive);
	//TODO do you need to add one to baseEdge?
	pEntry->edge = baseEdge + 1;
	pEntry->entries[0] = entryIdx;
	pEntry->refIdx[0] = refIdx;
	pEntry->receive = isReceive;
	pEntry->preserve = isPreserve;
	pEntry->seam = seam;
	pEntry->corner[0] = i;
	pEntry->validIdx = -1;
	pEntry->segment = segment;
	pEntry->inOrient = pPiece->pEntry->inOrient;
	pEntry->tile = pPiece->tile;
	if (refIdx >= 0 && isReceive) {
		//pPiece->keepPreserve |= 1 << i;
	}
}

static
void addEntryToSharedEdgeTable(MergeSendOffArgs *pArgs, BorderFace *pEntry,
                               SharedEdgeWrap *pSharedEdges, Piece *pEntries,
							   int32_t tableSize, int32_t entryIdx,
							   int32_t *pTotalVerts, bool *pHasPreserve) {
	//CLOCK_INIT;
	StucAlloc *pAlloc = &pArgs->pContext->alloc;
	STUC_ASSERT("", (tableSize > 0 && entryIdx >= 0) || entryIdx == 0);
	Piece *pPiece = pEntries + entryIdx;
	BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
	FaceRange face = getFaceRange(&asMesh(pBufMesh)->mesh, pEntry->face, true);
	pPiece->bufFace = face;
	pPiece->pEntry = pEntry;
	pPiece->entryIdx = entryIdx;
	pPiece->pOrder = pAlloc->pCalloc(face.size, 1);
	pPiece->pEdges = pAlloc->pCalloc(face.size, sizeof(EdgeSegmentPair));
	pPiece->tile = getTileMinFromBoundsEntry(pEntry);
	for (int32_t i = 0; i < face.size; ++i) {
		STUC_ASSERT("", pTotalVerts && *pTotalVerts >= 0 && *pTotalVerts < 10000);
		++*pTotalVerts;
		//CLOCK_START;
		//int32_t vert = pBufMesh->mesh.pCorners[face.start - i];
		bool isStuc = getIfStuc(pEntry, i);
		bool isOnLine = getIfOnLine(pEntry, i);
		if (isStuc && !isOnLine) {
			//stuc corner - skip
			continue;
		}
		//CLOCK_STOP_NO_PRINT;
		//pTimeSpent[1] += //CLOCK_TIME_DIFF(start, stop);
		bool isOnInVert = getIfOnInVert(pEntry, i);
		//Get in mesh details for current buf corner
		BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, i);
		int32_t lasti = i ? i - 1 : face.size - 1;
		if (getBaseCorner(pEntry, i) == getBaseCorner(pEntry, lasti) &&
			!(getIfStuc(pEntry, lasti) && !getIfOnLine(pEntry, lasti))) {
			//Edge belongs to last corner, not this one
			continue;
		}
		if (isOnInVert &&
			checkIfVertIsPreserve(&pArgs->pJobArgs[0].mesh, inInfo.vert)) {
			//This does not necessarily mean this vert will be kept,
			//only corners encountered in sortCorners func will be kept.
			//ie, only corners on the exterior. Interior corners are skipped.
			pPiece->keepVertPreserve |= true << i;
		}
		int32_t* pVerts = pArgs->pEdgeVerts[inInfo.edge].verts;
		STUC_ASSERT("", pVerts &&
		                (pVerts[0] == inInfo.edgeCorner ||
		                 pVerts[1] == inInfo.edgeCorner));
		if (pVerts[1] < 0) {
			//no other vert on edge
			pPiece->hasSeam = true;
			continue;
		}
		bool baseKeep;
		if (isOnInVert) {
			STUC_ASSERT("", pArgs->pJobArgs[0].pInVertTable[inInfo.vert] >= 0); //pInVertTable is 0 .. 3
			STUC_ASSERT("", pArgs->pJobArgs[0].pInVertTable[inInfo.vert] <= 3); 
			baseKeep = pArgs->pJobArgs[0].pInVertTable[inInfo.vert] > 0;
			pPiece->keepPreserve |= baseKeep << i;
		}
		if (!pSharedEdges) {
			//If shared edges if NULL, then there's only 1 border face entry.
			//So no need for a shared edge table
			STUC_ASSERT("", entryIdx == 0);
			continue;
		}
		//CLOCK_START;
		bool seam = pArgs->pEdgeSeamTable[inInfo.edge];
		//face is connected
		pEntries[entryIdx].pEdges[pEntries[entryIdx].edgeCount].edge = inInfo.edge;
		int32_t segment = getSegment(pEntry, i);
		pEntries[entryIdx].pEdges[pEntries[entryIdx].edgeCount].segment = segment;
		pEntries[entryIdx].edgeCount++;

		bool isPreserve =
			checkIfEdgeIsPreserve(&pArgs->pJobArgs[0].mesh, inInfo.edge);
		if (isPreserve && !*pHasPreserve) {
			*pHasPreserve = true;
		}
		bool isReceive = false;
		int32_t refIdx = 0; 
		if (seam) {
			refIdx = isOnInVert ? -1 : 1;
		}
		else if (isPreserve) {
			if (isOnInVert) {
				isReceive = true;
				//negate if base corner
				refIdx = (inInfo.vert + 1) * -1;
			}
			else {
				int32_t mapCorner = getMapCorner(pEntry, i);
				STUC_ASSERT("", pEntry->faceIdx < pArgs->pMap->mesh.mesh.faceCount);
				int32_t stucFaceStart = pArgs->pMap->mesh.mesh.pFaces[pEntry->faceIdx];
				STUC_ASSERT("", stucFaceStart < pArgs->pMap->mesh.mesh.cornerCount);
				int32_t stucEdge = pArgs->pMap->mesh.mesh.pEdges[stucFaceStart + mapCorner];
				STUC_ASSERT("", stucEdge < pArgs->pMap->mesh.mesh.edgeCount);
				isReceive = checkIfEdgeIsReceive(&pArgs->pMap->mesh, stucEdge);
				refIdx = stucEdge;
				if (isReceive) {
					pPiece->keepPreserve |= true << i;
				}
			}
		}

		//CLOCK_STOP_NO_PRINT;
		//pTimeSpent[2] += //CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;

		int32_t hash = stucFnvHash((uint8_t *)&inInfo.edge, 4, tableSize);
		SharedEdgeWrap *pEdgeEntryWrap = pSharedEdges + hash;
		SharedEdge *pEdgeEntry = pEdgeEntryWrap->pEntry;
		if (!pEdgeEntry) {
			pEdgeEntry = pEdgeEntryWrap->pEntry =
				pArgs->pContext->alloc.pCalloc(1, sizeof(SharedEdge));
			pEdgeEntry->pLast = pEdgeEntryWrap;
			initSharedEdgeEntry(pEdgeEntry, inInfo.edge, entryIdx, refIdx,
			                    isPreserve, isReceive, pPiece, i, seam, segment);
			continue;
		}
		do {
			STUC_ASSERT("", pEdgeEntry->edge - 1 >= 0);
			STUC_ASSERT("", pEdgeEntry->edge - 1 < pArgs->pInMesh->mesh.edgeCount);
			STUC_ASSERT("", pEdgeEntry->idx % 2 == pEdgeEntry->idx); // range 0 .. 1
			if (pEdgeEntry->edge == inInfo.edge + 1 &&
			    pEdgeEntry->segment == segment &&
				pEdgeEntry->tile.d[0] == pPiece->tile.d[0] &&
				pEdgeEntry->tile.d[1] == pPiece->tile.d[1]) {

				if (pEdgeEntry->entries[pEdgeEntry->idx] != entryIdx) {
					//other side of the edge
					pEdgeEntry->entries[1] = entryIdx;
					pEdgeEntry->corner[1] = i;
					pEdgeEntry->idx = 1;
					pEdgeEntry->refIdx[1] = refIdx;
					if (pEdgeEntry->inOrient != pEntry->inOrient) {
						pEdgeEntry->removed = true;
					}
				}
				if (!pEdgeEntry->seam &&
					!pEdgeEntry->altIdx &&
					isPreserve && pEdgeEntry->refIdx[0] != refIdx) {
					pEdgeEntry->receive += isReceive;
					if (refIdx >= 0 && isReceive) {
						//this is done here (as well as in initSharedEdgeEntry),
						//in order to avoid duplicate corners being added later on.
						//Ie, only one corner should be marked keep per vert
						//pPiece->keepPreserve |= 1 << i;
					}
					pEdgeEntry->altIdx = 1;
				}
				break;
			}
			if (!pEdgeEntry->pNext) {
				pEdgeEntry->pNext =
					pArgs->pContext->alloc.pCalloc(1, sizeof(SharedEdge));
				pEdgeEntry->pNext->pLast = pEdgeEntry;
				pEdgeEntry = pEdgeEntry->pNext;
				initSharedEdgeEntry(pEdgeEntry, inInfo.edge, entryIdx, refIdx,
				                    isPreserve, isReceive, pPiece, i, seam, segment);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(1);
		STUC_ASSERT("", i >= 0 && i < face.size);
	}
	//CLOCK_STOP_NO_PRINT;
	//pTimeSpent[3] += //CLOCK_TIME_DIFF(start, stop);
}

static
Piece *getEntryInPiece(Piece *pPieceRoot, int32_t otherPiece) {
	STUC_ASSERT("", pPieceRoot);
	Piece* pPiece = pPieceRoot;
	do {
		if (pPiece->entryIdx == otherPiece) {
			return pPiece;
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	return NULL;
}

static
Piece *getNeighbourEntry(MergeSendOffArgs *pArgs, SharedEdgeWrap *pEdgeTable,
                         int32_t edgeTableSize, Piece *pPiece, Piece *pPieceRoot,
                         int32_t *pCorner, SharedEdge **ppEdge) {
	int32_t segment = getSegment(pPiece->pEntry, *pCorner);
	BorderInInfo inInfo = getBorderEntryInInfo(pPiece->pEntry, pArgs->pJobArgs, *pCorner);
	int32_t hash = stucFnvHash((uint8_t*)&inInfo.edge, 4, edgeTableSize);
	SharedEdgeWrap* pEdgeEntryWrap = pEdgeTable + hash;
	SharedEdge* pEdgeEntry = pEdgeEntryWrap->pEntry;
	while (pEdgeEntry) {
		if (pEdgeEntry->idx && !pEdgeEntry->removed) {
			bool cornerMatches = *pCorner == pEdgeEntry->corner[0] &&
							   pPiece->entryIdx == pEdgeEntry->entries[0] ||
							   *pCorner == pEdgeEntry->corner[1] &&
							   pPiece->entryIdx == pEdgeEntry->entries[1];
			if (cornerMatches &&
				inInfo.edge + 1 == pEdgeEntry->edge &&
			    segment == pEdgeEntry->segment &&
				pPiece->tile.d[0] == pEdgeEntry->tile.d[0] &&
				pPiece->tile.d[1] == pEdgeEntry->tile.d[1]) {
				bool which = pEdgeEntry->entries[1] == pPiece->entryIdx;
				int32_t otherPiece = pEdgeEntry->entries[!which];
				Piece *pNeighbour = getEntryInPiece(pPieceRoot, otherPiece);
				if (pNeighbour) {
					*pCorner = (pEdgeEntry->corner[!which] + 1) % pNeighbour->bufFace.size;
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
bool isCornerOnExterior(MergeSendOffArgs *pArgs, Piece *pPiece,
                      Piece *pPieceRoot, SharedEdgeWrap *pEdgeTable,
                      int32_t edgeTableSize, int32_t corner) {
	if (!getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
			               pPiece, pPieceRoot, &corner, NULL)) {
		//corner does not share an edge without any other corner,
		//must be on outside
		return true;
	}
	return false;
}

static
int32_t getStartingCorner(Piece **ppPiece, MergeSendOffArgs *pArgs,
                        Piece *pPieceRoot, SharedEdgeWrap *pEdgeTable,
                        int32_t edgeTableSize) {
	do {
		for (int32_t i = 0; i < (*ppPiece)->bufFace.size; ++i) {
			if (isCornerOnExterior(pArgs, *ppPiece, pPieceRoot, pEdgeTable,
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
	int32_t corner;
	int32_t cornerPrev;
	int32_t startCorner;
	int32_t validBranches;
	bool quad;
} EdgeStack;

static
bool checkIfIntersectsReceive(MergeSendOffArgs *pArgs, EdgeStack *pItem, Mesh *pBufMesh, StucMap pMap,
                              FaceRange *pMapFace, int32_t *pMapCorner, bool side) {
	Mesh *pMapMesh = &pMap->mesh;
	STUC_ASSERT("", pItem->pPiece->bufFace.size >= 3);
	int32_t cornerNext = (pItem->corner + 1) % pItem->pPiece->bufFace.size;
	bool isOnInVert;
	if (side) {
		isOnInVert = getIfOnInVert(pItem->pPiece->pEntry, cornerNext);
	}
	else {
		isOnInVert = getIfOnInVert(pItem->pPiece->pEntry, pItem->corner);
	}
	if (!isOnInVert) {
		//exterior intersects with a map edge,
		//so just check if said edge is receive
		int32_t corner;
		if (side) {
			corner = cornerNext;
		}
		else {
			corner = pItem->corner == 0 ?
				pItem->pPiece->bufFace.size - 1 : pItem->corner - 1;
		}
		*pMapCorner = getMapCorner(pItem->pPiece->pEntry, corner);
		STUC_ASSERT("", *pMapCorner >= 0 && *pMapCorner < pMapFace->size);
		*pMapCorner += pMapFace->start;
		int32_t mapEdge = pMapMesh->mesh.pEdges[*pMapCorner];
		return pMapMesh->pEdgeReceive[mapEdge];
	}
	//exterior does not intersect with a map edge.
	//In this case, we perform an intersect test,
	//and use that to see if the base edge would intersect
	//with a preserve edge, were it to extend out infinitely
	V2_F32 *pUvStart = pBufMesh->pStuc + pItem->pPiece->bufFace.start;
	int32_t corner = pItem->corner;
	if (side) {
		corner = cornerNext;
		cornerNext = pItem->corner;
	}
	V2_F32 fTileMin = {(float)pItem->pPiece->tile.d[0],
	                   (float)pItem->pPiece->tile.d[1]};
	V2_F32 c = _(pUvStart[-corner] V2SUB fTileMin);
	V2_F32 d = _(pUvStart[-cornerNext] V2SUB fTileMin);
	V2_F32 cd = _(d V2SUB c);
	for (int32_t i = 0; i < pMapFace->size; ++i) {
		*pMapCorner = pMapFace->start + i;
		int32_t mapEdge = pMapMesh->mesh.pEdges[*pMapCorner];
		if (!pMapMesh->pEdgeReceive[mapEdge]) {
			continue;
		}
		float t = .0f;
		int32_t iNext = (i + 1) % pMapFace->size;
		int32_t mapVert = pMapMesh->mesh.pCorners[*pMapCorner];
		int32_t mapCornerNext = pMapFace->start + iNext;
		int32_t mapVertNext = pMapMesh->mesh.pCorners[mapCornerNext];
		//STUC_ASSERT("", !_(*(V2_F32 *)&pMapMesh->pVerts[mapVert] V2EQL *(V2_F32 *)&pMapMesh->pVerts[mapVertNext]));
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
		//	                     pArgs->pJobArgs, corner);
		//setBitArr(pArgs->pInVertKeep, inInfo.vert, true);
		return true;
	}
	return false;
}

static
void pushToEdgeStack(EdgeStack *pStack, int32_t *pStackPtr, int32_t treeCount,
                     SharedEdge *pEdge, Piece *pPiece, int32_t corner) {
	pStack[*pStackPtr].pActiveEdge = pEdge;
	pEdge->validIdx = treeCount;
	++*pStackPtr;
	EdgeStack next = {.pPiece = pPiece, .corner = corner};
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
	int32_t mapCorner = -1;
	bool isReceive =
		checkIfIntersectsReceive(pArgs, pNeighbour, pBufMesh, pArgs->pMap,
		                         pMapFace, &mapCorner, side);
	if (isReceive) {
		//preserve edge intersects receive edge. Add to count
		STUC_ASSERT("", mapCorner >= 0 && mapCorner < pMapMesh->mesh.cornerCount);
		STUC_ASSERT("", *pReceive >= -1 &&
					    *pReceive < pMapMesh->mesh.cornerCount);
		if (*pReceive == -1) {
			//start of new preserve tree
			*pReceive = mapCorner;
		}
		else if (!pValid[treeCount] && mapCorner != *pReceive) {
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
	int32_t idx = -1;
	SharedEdge *cache[4] = {0};
	EdgeStack retNeighbour = {0};
	do {
		copy.corner %= copy.pPiece->bufFace.size;
		if (copy.pStartPiece == copy.pPiece &&
			copy.startCorner == copy.corner) {
			if (idx == 3) {
				*pNeighbour = retNeighbour;
				return cache[1];
			}
		}
		else if (idx >= 3) {
			break;
		}
		idx++;
		SharedEdge *pEdge = NULL;
		//Set next corner
		EdgeStack neighbour = {.corner = copy.corner};
		neighbour.pPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
		                                     copy.pPiece, pPieceRoot,
		                                     &neighbour.corner, cache + idx);
		if (!neighbour.pPiece) {
			break;
		}
		if (!idx) {
			retNeighbour = neighbour;
		}
		copy.pPiece = neighbour.pPiece;
		copy.corner = neighbour.corner;
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
			bool onInVert = getIfOnInVert(pItem->pPiecePrev->pEntry, pItem->cornerPrev);
			if (onInVert) {
				BorderInInfo inInfo =
					getBorderEntryInInfo(pItem->pPiecePrev->pEntry,
										 pArgs->pJobArgs, pItem->cornerPrev);
				setBitArr(pArgs->pInVertKeep, inInfo.vert, true, 1);
			}
		}
	}

	bool ret = false;
	do {
		SharedEdge *pEdge = NULL;
		EdgeStack neighbour = {0};
		pItem->corner %= pItem->pPiece->bufFace.size;
		if (!pItem->pStartPiece) {
			pItem->pStartPiece = pItem->pPiece;
			pItem->startCorner = pItem->corner;
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
		          pItem->startCorner == pItem->corner)) {
			*pUnwind = !pItem->validBranches;
			if (!*pUnwind) {
				bool onInVert = getIfOnInVert(pItem->pPiece->pEntry, pItem->corner);
				if (onInVert) {
					BorderInInfo inInfo =
						getBorderEntryInInfo(pItem->pPiece->pEntry,
						                     pArgs->pJobArgs, pItem->corner);
					setBitArr(pArgs->pInVertKeep, inInfo.vert, true, 1);
				}
			}
			--*pStackPtr;
			return;
		}
		//Set next corner
		if (!*pStackPtr &&
		    getIfStuc(pItem->pPiece->pEntry, pItem->corner) &&
			!getIfOnLine(pItem->pPiece->pEntry, pItem->corner)) {

			pItem->corner++;
			continue;
		}
		if (!pItem->quad) {
			neighbour.corner = pItem->corner;
			neighbour.pPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
												 pItem->pPiece, pPieceRoot,
												 &neighbour.corner, &pEdge);
			if (!neighbour.pPiece) {
				pItem->corner++;
				continue;
			}
		}
		STUC_ASSERT("", pEdge);
		//if validIdx isn't -1, this edge has already been checked
		if (pEdge->preserve && pEdge->validIdx == -1) {
			if (!*pStackPtr) {
				handleExterior(pStack, pStackPtr, pItem, pValid, treeCount, pReceive,
				                pArgs, pMapFace, pEdge, false);
			}
			Piece *pPieceNext = pItem->quad ? neighbour.pPiece : pItem->pPiece;
			int32_t cornerNext = pItem->quad ? neighbour.corner : pItem->corner;
			cornerNext = (cornerNext + 1) % pPieceNext->bufFace.size;
			if (*pReceive != -1) {
				bool exterior;
				exterior = isCornerOnExterior(pArgs, pPieceNext, pPieceRoot,
				                            pEdgeTable, edgeTableSize, cornerNext);
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
									cornerNext);
					ret = true;
				}
			}
		}
		else if (!*pStackPtr && pEdge->preserve) {
			bool onInVert = getIfOnInVert(pItem->pPiece->pEntry, pItem->corner);
			if (onInVert) {
				BorderInInfo inInfo =
					getBorderEntryInInfo(pItem->pPiece->pEntry,
										 pArgs->pJobArgs, pItem->corner);
				setBitArr(pArgs->pInVertKeep, inInfo.vert, true, 1);
			}
		}
		if (!pItem->quad) {
			pItem->pPiecePrev = pItem->pPiece;
			pItem->cornerPrev = pItem->corner;
			pItem->pPiece = neighbour.pPiece;
			pItem->corner = neighbour.corner;
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
//     set preserve in the later validate corner
static
void validatePreserveEdges(MergeSendOffArgs* pArgs,PieceArr *pPieceArr,
                           int32_t piece, SharedEdgeWrap* pEdgeTable,
                           int32_t edgeTableSize, bool **ppValid,
                           int32_t *pValidCount, int32_t *pValidSize) {
	//TODO first , check if the map face even has any preserve edges,
	//     no point doing all this if not
	StucAlloc *pAlloc = &pArgs->pContext->alloc;
	Mesh *pInMesh = pArgs->pInMesh;
	Mesh *pMapMesh = &pArgs->pMap->mesh;
	// Get first not exterior corner
	// This is done to ensure we don't start inside the face
	Piece *pPiece = pPieceArr->pArr + piece;
	Piece *pPieceRoot = pPiece;
	FaceRange mapFace = getFaceRange(&pArgs->pMap->mesh.mesh,
	                                 pPieceRoot->pEntry->faceIdx, false);
	if (!*ppValid) {
		*pValidSize = 8;
		*ppValid = pAlloc->pCalloc(*pValidSize, sizeof(bool));
	}
	int32_t stackSize = 8;
	EdgeStack *pStack = pAlloc->pCalloc(stackSize, sizeof(EdgeStack));
	pStack[0].pPiece = pPiece;
	int32_t stackPtr = 0;
	pStack[0].corner = getStartingCorner(&pStack[0].pPiece, pArgs, pPiece,
	                                 pEdgeTable, edgeTableSize);
	bool unwind = false;
	//note that order is used here to determine if a corner has already been checked.
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
		STUC_ASSERT("", stackPtr < stackSize);
		if (stackPtr == stackSize - 1) {
			int32_t oldSize = stackSize;
			stackSize *= 2;
			pStack = pAlloc->pRealloc(pStack, sizeof(EdgeStack) * stackSize);
		}
		else if (!stackPtr) {
			//reset for next preserve tree
			receive = -1;
			++*pValidCount;
			STUC_ASSERT("", *pValidCount <= *pValidSize);
		}
	} while(stackPtr >= 0);
	++*pValidCount;
	pAlloc->pFree(pStack);

	//set order back to zero
	do {
		memset(pPiece->pOrder, 0, pPiece->bufFace.size);
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
			STUC_ASSERT("", validIdx >= -1 && validIdx < validCount);
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
		STUC_ASSERT("", pPiece->edgeCount <= 64);
		//TODO can you just get the edges in the piece by calling getNeighbourEntry?
		//     Rather than storing a list of edges in the piece?
		//     Is there a perf cost?
		for (int32_t j = 0; j < pPiece->edgeCount; ++j) {
			EdgeSegmentPair edge = pPiece->pEdges[j];
			int32_t hash = stucFnvHash((uint8_t *)&edge, 4, tableSize);
			SharedEdgeWrap *pSharedEdgeWrap = pSharedEdges + hash;
			SharedEdge *pEdgeEntry = pSharedEdgeWrap->pEntry;
			while (pEdgeEntry) {
				if (pEdgeEntry->removed) {}
				else if (pEdgeEntry->seam) {
					bool aIsOnInVert = pEdgeEntry->refIdx[0] < 0;
					bool bIsOnInVert = pEdgeEntry->refIdx[1] < 0;
					if (aIsOnInVert ^ bIsOnInVert) {
						bool whichCorner = aIsOnInVert;

						int32_t cornerA = pEdgeEntry->corner[whichCorner];
						Piece *pPieceA = pPieceArr->pArr + pEdgeEntry->entries[whichCorner];
						pPieceA->keepSeam |= 1 << cornerA;

						int32_t cornerB = pEdgeEntry->corner[!whichCorner];
						Piece *pPieceB = pPieceArr->pArr + pEdgeEntry->entries[!whichCorner];
						//Also set adjacent corner
						int32_t adjCorner = (cornerB + 1) % pPieceB->bufFace.size;
						pPieceB->keepSeam |= 1 << adjCorner;
					}
				}
				else if (pEdgeEntry->edge - 1 == pPiece->pEdges[j].edge &&
				         pEdgeEntry->segment == pPiece->pEdges[j].segment &&
				         pEdgeEntry->tile.d[0] == pPiece->pEntry->tileX &&
					     pEdgeEntry->tile.d[1] == pPiece->pEntry->tileY) {
					STUC_ASSERT("", pEdgeEntry->entries[0] == pPiece->entryIdx ||
					       pEdgeEntry->entries[1] == pPiece->entryIdx);
					int32_t whichEntry =
						pEdgeEntry->entries[0] == pPiece->entryIdx;
					int32_t otherEntryIdx = pEdgeEntry->entries[whichEntry];
					if (pPieceArr->pArr[otherEntryIdx].listed) {
						break;
					}
					if (!pPieceRoot->hasSeam &&
						pPieceArr->pArr[otherEntryIdx].hasSeam) {

						pPieceRoot->hasSeam = true;
					}
					//add entry to linked list
					pTail->pNext = pPieceArr->pArr[otherEntryIdx].pEntry;
					pTail = pTail->pNext;
					//add piece to piece linked list
					pPieceTail->pNext = pPieceArr->pArr + otherEntryIdx;
					pPieceTail = pPieceTail->pNext;
					pPieceArr->pArr[otherEntryIdx].listed = 1;
					break;
				}
				pEdgeEntry = pEdgeEntry->pNext;
			};
			STUC_ASSERT("", j < pPiece->edgeCount);
		}
		depth++;
		if (depth > pPieceArr->count) {
			//an infinite corner can occur if pNext are not NULL prior to this func
			//this is checked for, so it shouldn't occur
			STUC_ASSERT("Piece list has likely linked in a corner", false);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static 
bool isEntryInPiece(Piece *pPiece, int32_t entryIdx) {
	STUC_ASSERT("", pPiece);
	do {
		if (pPiece->entryIdx == entryIdx) {
			return true;
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	return false;
}

static
void flipSharedEdgeEntry(SharedEdge *pEntry) {
	int32_t entryBuf = pEntry->entries[0];
	int32_t cornerBuf = pEntry->corner[0];
	int32_t refIdxBuf = pEntry->refIdx[0];
	pEntry->entries[0] = pEntry->entries[1];
	pEntry->entries[1] = entryBuf;
	pEntry->corner[0] = pEntry->corner[1];
	pEntry->corner[1] = cornerBuf;
	pEntry->refIdx[0] = pEntry->refIdx[1];
	pEntry->refIdx[1] = refIdxBuf;
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
			STUC_ASSERT("Linked pieces here will cause an infinite corner", false);
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
			STUC_ASSERT("", !pPieceRoots->count ||
							pPieceArr->pArr[pPieceRoots->count - 1].pEntry);
			STUC_ASSERT("", j >= 0 && j < pPieceArr->count);
		}
		STUC_ASSERT("", i >= 0 && i < 2);
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
	STUC_ASSERT("", entryCount > 0);
	Piece *pEntries = pArgs->pContext->alloc.pCalloc(entryCount, sizeof(Piece));
	pPieceRoots->pArr = pArgs->pContext->alloc.pMalloc(sizeof(int32_t) * entryCount);
	pPieceArr->pArr = pEntries;
	pPieceArr->count = entryCount;
	int32_t entryIdx = 0;
	bool hasPreserve = false;
	//CLOCK_START;
	do {
		//If there's only 1 border face entry, then this function will just
		//initialize the Piece.
		addEntryToSharedEdgeTable(pArgs, pEntry, *ppSharedEdges, pEntries,
		                          *pEdgeTableSize, entryIdx, pTotalVerts,
		                          &hasPreserve);
		STUC_ASSERT("", entryIdx < entryCount);
		entryIdx++;
		BorderFace *pNextEntry = pEntry->pNext;
		pEntry->pNext = NULL;
		pEntry = pNextEntry;
	} while(pEntry);
	STUC_ASSERT("", entryIdx == entryCount);
	
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
		STUC_ASSERT("", i < entryCount);
	}
	STUC_ASSERT("", pPieceRoots->count >= 0 && pPieceRoots->count < 10000);
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
void destroySharedEdgeTable(StucAlloc *pAlloc,
                            SharedEdgeWrap *pSharedEdges, int32_t tableSize) {
	for (int32_t i = 0; i < tableSize; ++i) {
		SharedEdge* pEdgeEntry = pSharedEdges[i].pEntry;
		while (pEdgeEntry) {
			SharedEdge* pNext = pEdgeEntry->pNext;
			pAlloc->pFree(pEdgeEntry);
			pEdgeEntry = pNext;
		}
		STUC_ASSERT("", i < tableSize);
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
		keepInVert = idxBitArray(pArgs->pInVertKeep, inInfo.vert, 1);
		if (keepInVert) {
			pPiece->keepPreserve |= true << k;
		}
	}
}

static
void markKeepInVertsPreserve(MergeSendOffArgs *pArgs, StucMap *pMap, Piece* pPiece) {
	Piece* pPieceRoot = pPiece;
	do {
		for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
			if (getIfStuc(pPiece->pEntry, i)) {
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
void sortCorners(MergeSendOffArgs* pArgs, Piece* pPiece, PieceArr *pPieceArr,
               SharedEdgeWrap* pEdgeTable, int32_t edgeTableSize, int32_t *pCount) {
	bool single = false;
	if (!pPiece->pNext) {
		single = true;
	}
	Mesh* pBufMesh = &pArgs->pJobArgs[0].mesh;
	Piece* pPieceRoot = pPiece;
	int32_t corner = 0;
	if (!single) {
		// get starting corner
		// This is done to ensure we don't start inside the face
		corner = getStartingCorner(&pPiece, pArgs, pPiece, pEdgeTable,
	                           edgeTableSize);
	}
	STUC_ASSERT("No valid starting corner found", corner >= 0);
	bool adj = false;
	int32_t sort = 1;
	Piece *pOtherPiece = NULL;
	if (!single) {
		pOtherPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
										pPiece, pPieceRoot, &corner, NULL);
	}
	if (!pOtherPiece) {
			corner++;
	}
	else {
		adj = true;
		pPiece = pOtherPiece;
	}
	do {
		corner %= pPiece->bufFace.size;
		if (pPiece->pOrder[corner]) {
			//We've done a full corner around
			break;
		}
		//Set next corner
		if (getIfStuc(pPiece->pEntry, corner) &&
			!getIfOnLine(pPiece->pEntry, corner)) {
			
			pPiece->add |= true << corner;
			pPiece->pOrder[corner] = sort;
			sort++;
			corner++;
			adj = false;
			continue;
		}
		int32_t otherCorner = corner;
		pOtherPiece = NULL;
		if (!single) {
			pOtherPiece = getNeighbourEntry(pArgs, pEdgeTable, edgeTableSize,
											pPiece, pPieceRoot, &otherCorner, NULL);
		}
		if (!pOtherPiece) {
			if (!adj) {
				pPiece->add |= true << corner;
				pPiece->pOrder[corner] = sort;
				//set keep preserve to false if true
				UBitField16 mask = -0x1 ^ (0x1 << corner);
				pPiece->keepPreserve &= mask;
				sort++;
			}
			else {
				pPiece->pOrder[corner] = 1;
				adj = false;
			}
			corner++;
			continue;
		}
		else if (!adj) {
			if (pPiece->keepPreserve >> corner & 0x01 ||
				(pPiece->keepSeam >> corner & 0x01) ||
				(pPiece->keepVertPreserve >> corner & 0x01)) {

				pPiece->add |= true << corner;
				pPiece->pOrder[corner] = sort;
				sort++;
			}
			else {
				pPiece->pOrder[corner] = 1;
			}
			adj = true;
		}
		else {
			pPiece->pOrder[corner] = 1;
		}
		corner = otherCorner;
		pPiece = pOtherPiece;
	} while(1);
	*pCount = sort - 1;
}

static
void getPieceInFaces(StucAlloc *pAlloc, int32_t **ppInFaces,
                     Piece *pPiece, int32_t pieceCount,
                     SendOffArgs *pJobArgs) {
	*ppInFaces = pAlloc->pCalloc(pieceCount, sizeof(int32_t));
	int32_t i = 0;
	do {
		int32_t offset = pJobArgs[pPiece->pEntry->job].inFaceOffset;
		(*ppInFaces)[i] = pPiece->pEntry->baseFace + offset;
		pPiece = pPiece->pNext;
		i++;
	} while(pPiece);
}

static
void initVertTableEntry(MergeSendOffArgs *pArgs, BorderVert *pVertEntry,
                        BorderFace *pEntry, BufMesh *pBufMesh,
                        int32_t stucEdge, int32_t *pVert,
                        BorderInInfo *pInInfo, int32_t stucFace,
                        int32_t corner, int32_t cornerLocal,
                        Piece *pPieceRoot, V2_I16 tile) {
	bool realloced = false;
	int32_t outVert = meshAddVert(&pArgs->pContext->alloc, pArgs->pMeshOut, &realloced);
	copyAllAttribs(&pArgs->pMeshOut->mesh.vertAttribs, outVert,
				   &asMesh(pBufMesh)->mesh.vertAttribs, *pVert);
	*pVert = outVert;
	pVertEntry->vert = outVert;
	pVertEntry->tile = tile;
	pVertEntry->stucEdge = stucEdge;
	pVertEntry->corners = 1;
	pVertEntry->baseEdge = pInInfo->edge;
	pVertEntry->baseVert = pInInfo->vert;
	pVertEntry->cornerIdx = pInInfo->vertCorner;
	pVertEntry->stucFace = stucFace;
	pVertEntry->corner = corner;
	pVertEntry->job = pEntry->job;
}

static void initEdgeTableEntry(MergeSendOffArgs *pArgs, BorderEdge *pSeamEntry,
                               BufMesh *pBufMesh, int32_t *pEdge,
							   int32_t inEdge, int32_t mapFace) {
	StucContext pContext = pArgs->pContext;
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
void blendMergedCornerAttribs(BlendConfig config,
                            AttribArray *pDestArr, int32_t iDest,
                            AttribArray *pSrcArr, int32_t iSrc,
                            Attrib *pDestNormalAttrib) {
	for (int32_t i = 0; i < pDestArr->count; ++i) {
		Attrib *pDest = pDestArr->pArr + i;
		Attrib *pSrc = pSrcArr->pArr + i;
		STUC_ASSERT("", !strncmp(pDest->name, pSrc->name, STUC_ATTRIB_NAME_MAX_LEN));
		if (pDest != pDestNormalAttrib &&
			(!pDest->origin == STUC_ATTRIB_ORIGIN_MAP ||
			!pDest->interpolate)) {
			continue;
		}
		blendAttribs(pDest, iDest, pDest, iDest, pSrc, iSrc, config);
	}
}

static
void divideCornerAttribsByScalar(AttribArray *pCornerAttribs, int32_t corner,
                               int32_t scalar, Attrib *pNormalAttrib) {
	for (int32_t i = 0; i < pCornerAttribs->count; ++i) {
		Attrib *pAttrib = pCornerAttribs->pArr + i;
		if (pAttrib != pNormalAttrib &&
			(!pAttrib->origin == STUC_ATTRIB_ORIGIN_MAP ||
			!pAttrib->interpolate)) {
			continue;
		}
		divideAttribByScalarInt(pAttrib, corner, scalar);
	}
}

static
void addBorderCornerAndVert(MergeSendOffArgs *pArgs, Piece *pPiece,
                          Piece *pPieceRoot, int32_t k, bool addToTables) {
	BorderFace *pEntry = pPiece->pEntry;
	STUC_ASSERT("This should not be called on a map corner", !getIfStuc(pEntry, k));
	BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
	int32_t corner = pPiece->bufFace.start - k;
	int32_t vert = bufMeshGetVertIdx(pPiece, pBufMesh, k);
	STUC_ASSERT("", vert > asMesh(pBufMesh)->vertBufSize - 1 -
		pBufMesh->borderVertCount);
	STUC_ASSERT("", vert < asMesh(pBufMesh)->vertBufSize);
	int32_t edge = bufMeshGetEdgeIdx(pPiece, pBufMesh, k);
	STUC_ASSERT("", edge > asMesh(pBufMesh)->edgeBufSize - 1 -
		pBufMesh->borderEdgeCount);
	STUC_ASSERT("", edge < asMesh(pBufMesh)->edgeBufSize);
	int32_t mapCorner = getMapCorner(pEntry, k);
	STUC_ASSERT("", mapCorner >= 0 && mapCorner < pArgs->pMap->mesh.mesh.cornerCount);
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, k);
	bool isOnInVert = getIfOnInVert(pEntry, k);
	if (!isOnInVert) {
		inInfo.vert = -1;
	}
	int32_t hash;
	int32_t stucEdge;
	if (isOnInVert) {
		hash = stucFnvHash((uint8_t *)&inInfo.vert, 4, pArgs->pCTables->vertTableSize);
		stucEdge = -1;
	}
	else {
		FaceRange stucFace =
			getFaceRange(&pArgs->pMap->mesh.mesh, pEntry->faceIdx, false);
		stucEdge = pArgs->pMap->mesh.mesh.pEdges[stucFace.start + mapCorner];
		hash = stucFnvHash((uint8_t *)&stucEdge, 4, pArgs->pCTables->vertTableSize);
	}
	BlendConfig blendConfigAdd = {.blend = STUC_BLEND_ADD};
	BorderVert *pVertEntry = pArgs->pCTables->pVertTable + hash;
	if (!pVertEntry->corners) {
		if (addToTables) {
			initVertTableEntry(pArgs, pVertEntry, pEntry, pBufMesh, stucEdge,
							   &vert, &inInfo, pEntry->faceIdx, corner, k,
							   pPieceRoot, pPiece->tile);
		}
		else {
			pVertEntry = NULL;
		}
	}
	else {
		do {
			//Check vert entry is valid
			STUC_ASSERT("", pVertEntry->stucEdge >= -1);
			STUC_ASSERT("", pVertEntry->stucEdge < pArgs->pMap->mesh.mesh.edgeCount);
			STUC_ASSERT("", pVertEntry->stucFace >= 0);
			STUC_ASSERT("", pVertEntry->stucFace < pArgs->pMap->mesh.mesh.faceCount);
			bool match;
			if (isOnInVert) {
				V2_F32 *pMeshInUvA = pArgs->pInMesh->pStuc + pVertEntry->cornerIdx;
				V2_F32 *pMeshInUvB = pArgs->pInMesh->pStuc + inInfo.vertCorner;
				match = pVertEntry->baseVert == inInfo.vert &&
						pVertEntry->stucFace == pEntry->faceIdx &&
						pMeshInUvA->d[0] == pMeshInUvB->d[0] &&
						pMeshInUvA->d[1] == pMeshInUvB->d[1];
			}
			else {
				BufMesh *pOtherBufMesh = &pArgs->pJobArgs[pVertEntry->job].bufMesh;
				bool connected = 
					_(asMesh(pBufMesh)->pStuc[corner] V2APROXEQL
					  asMesh(pOtherBufMesh)->pStuc[pVertEntry->corner]);
				match =  pVertEntry->stucEdge == stucEdge &&
					     pVertEntry->tile.d[0] == pPiece->tile.d[0] &&
					     pVertEntry->tile.d[1] == pPiece->tile.d[1] &&
						 pVertEntry->baseEdge == inInfo.edge &&
						 connected;
			}
			if (match) {
				//If corner isOnInVert,
				//then entry must also be an isOnInVert entry.
				//And if not, then entry must also not be
				STUC_ASSERT("", (isOnInVert && pVertEntry->baseVert != -1) ||
				       (!isOnInVert && pVertEntry->baseVert == -1));
				vert = pVertEntry->vert;
				pVertEntry->corners++;
				if (isOnInVert) {
					BufMesh *pOtherBufMesh = &pArgs->pJobArgs[pVertEntry->job].bufMesh;
					blendMergedCornerAttribs(blendConfigAdd,
					                       &pOtherBufMesh->mesh.mesh.cornerAttribs,
					                       pVertEntry->corner,
					                       &pBufMesh->mesh.mesh.cornerAttribs,
					                       corner,
					                       pOtherBufMesh->mesh.pNormalAttrib);
				}
				break;
			}
			if (!pVertEntry->pNext && addToTables) {
				pVertEntry = pVertEntry->pNext =
					pArgs->pContext->alloc.pCalloc(1, sizeof(BorderVert));
				initVertTableEntry(pArgs, pVertEntry, pEntry, pBufMesh, stucEdge,
				                   &vert, &inInfo, pEntry->faceIdx, corner, k,
				                   pPieceRoot, pPiece->tile);
				break;
			}
			pVertEntry = pVertEntry->pNext;
		} while(pVertEntry);
	}
	//TODO debug/ verify border edge implementation is working correctly
	//TODO why cant you just determine connected edges with the above corner table?
	//     is a separate table really needed? If you know 2 corners are connected,
	//     can't you then connect their edges?
	uint32_t valueToHash = inInfo.edge + pEntry->faceIdx;
	hash = stucFnvHash((uint8_t *)&valueToHash, 4, pArgs->pCTables->edgeTableSize);
	BorderEdge *pEdgeEntry = pArgs->pCTables->pEdgeTable + hash;
	if (!pEdgeEntry->valid) {
		if (addToTables) {
			initEdgeTableEntry(pArgs, pEdgeEntry, pBufMesh, &edge, inInfo.edge,
							   pEntry->faceIdx);
		}
		else {
			pEdgeEntry = NULL;
		}
	}
	else {
		do {
			if (pEdgeEntry->inEdge == inInfo.edge &&
				pEdgeEntry->mapFace == pEntry->faceIdx) {
				edge = pEdgeEntry->edge;
				break;
			}
			if (!pEdgeEntry->pNext && addToTables) {
				pEdgeEntry = pEdgeEntry->pNext =
					pArgs->pContext->alloc.pCalloc(1, sizeof(BorderEdge));
				initEdgeTableEntry(pArgs, pEdgeEntry, pBufMesh, &edge,
				                   inInfo.edge, pEntry->faceIdx);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(pEdgeEntry);
	}
	if (pVertEntry) {
		pBufMesh->mesh.mesh.pCorners[corner] = vert;
		pBufMesh->mesh.mesh.pEdges[corner] = 0;
	}
	else {
		//set add to false
		UBitField16 mask = -0x1 ^ (0x1 << k);
		pPiece->add &= mask;
		//correct sort, as this corner will not be kept
		int32_t sort = pPiece->pOrder[k];
		pPiece = pPieceRoot;
		do {
			for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
				if (pPiece->pOrder[i] > sort) {
					pPiece->pOrder[i]--;
				}
			}
			pPiece = pPiece->pNext;
		} while(pPiece);
	}
}

static
void mergeAttribsForSingleCorner(MergeSendOffArgs *pArgs,
                               Piece *pPiece, int32_t k) {
	BorderFace *pEntry = pPiece->pEntry;
	STUC_ASSERT("This should only be called on onInVert corners",
	            getIfOnInVert(pEntry, k));
	int32_t mapCorner = getMapCorner(pEntry, k);
	STUC_ASSERT("", mapCorner >= 0 && mapCorner < pArgs->pMap->mesh.mesh.cornerCount);
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, k);
	int32_t hash;
	hash = stucFnvHash((uint8_t *)&inInfo.vert, 4, pArgs->pCTables->vertTableSize);
	BorderVert *pVertEntry = pArgs->pCTables->pVertTable + hash;
	STUC_ASSERT("No entry was initialized for this corner", pVertEntry->corners);

	BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
	int32_t corner = pPiece->bufFace.start - k;

	BlendConfig blendConfigReplace = {.blend = STUC_BLEND_REPLACE};
	do {
		STUC_ASSERT("", pVertEntry->stucFace >= 0);
		STUC_ASSERT("", pVertEntry->stucFace < pArgs->pMap->mesh.mesh.faceCount);
		bool match;
		V2_F32 *pMeshInUvA = pArgs->pInMesh->pStuc + pVertEntry->cornerIdx;
		V2_F32 *pMeshInUvB = pArgs->pInMesh->pStuc + inInfo.vertCorner;
		match = pVertEntry->baseVert == inInfo.vert &&
		        pVertEntry->stucFace == pEntry->faceIdx &&
		        pMeshInUvA->d[0] == pMeshInUvB->d[0] &&
		        pMeshInUvA->d[1] == pMeshInUvB->d[1];
		if (match) {
			STUC_ASSERT("Entry is not onInVert", pVertEntry->baseVert != -1);
			BufMesh *pOtherBufMesh = &pArgs->pJobArgs[pVertEntry->job].bufMesh;
			if (!pVertEntry->divided) {

				divideCornerAttribsByScalar(&pOtherBufMesh->mesh.mesh.cornerAttribs,
					                      pVertEntry->corner, pVertEntry->corners,
				                          pOtherBufMesh->mesh.pNormalAttrib);
				pVertEntry->divided = true;
			}
			blendMergedCornerAttribs(blendConfigReplace,
					               &pBufMesh->mesh.mesh.cornerAttribs,
					               corner,
			                       &pOtherBufMesh->mesh.mesh.cornerAttribs,
					               pVertEntry->corner,
			                       pBufMesh->mesh.pNormalAttrib);
			return;
		}
		pVertEntry = pVertEntry->pNext;
	} while(pVertEntry);
	STUC_ASSERT("No entry was initialized for this corner", pVertEntry);
}

static
void addToOutMesh(MergeSendOffArgs *pArgs) {
	CLOCK_INIT;
	uint64_t timeSpent[7] = {0};
	StucContext pContext = pArgs->pContext;
	StucAlloc *pAlloc = &pContext->alloc;
	int32_t count = pArgs->entriesEnd - pArgs->entriesStart;
	MergeBufHandles mergeBufHandles = {0};
	stucAllocMergeBufs(pArgs->pContext, &mergeBufHandles, pArgs->totalVerts);
	for (int32_t i = 0; i < count; ++i) {
		int32_t reali = pArgs->entriesStart + i;
		CLOCK_START;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		FaceRange stucFace =
			getFaceRange(&pArgs->pMap->mesh.mesh, pPieceArr->pArr[0].pEntry->faceIdx, false);
		for (int32_t j = 0; j < pPieceRoots->count; ++j) {
			Piece *pPieceRoot = pPieceArr->pArr + pPieceRoots->pArr[j];
			int32_t *pInFaces = NULL;
			int32_t pieceCount = 0; //this is only need if getting in faces
			if (pArgs->ppInFaceTable) {
				pieceCount = getPieceCount(pPieceRoot); 
				getPieceInFaces(&pArgs->pContext->alloc, &pInFaces,
				                pPieceRoot, pieceCount, pArgs->pJobArgs);
			}
			int32_t job = pPieceArr->pArr[pPieceRoots->pArr[j]].pEntry->job;
			STUC_ASSERT("", job >= 0 && job < pContext->threadCount);
			stucMergeSingleBorderFace(pArgs, timeSpent, pPieceRoots->pArr[j], pPieceArr,
										&stucFace, &mergeBufHandles, pInFaces, pieceCount);
			if (pInFaces) {
				pAlloc->pFree(pInFaces);
			}
			STUC_ASSERT("", j >= 0 && j < pPieceRoots->count);
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
		STUC_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
	stucDestroyMergeBufs(pArgs->pContext, &mergeBufHandles);
	pAlloc->pFree(pArgs->pPieceArrTable);
	pAlloc->pFree(pArgs->pPieceRootTable);
	pAlloc->pFree(pArgs->pTotalVertTable);
	printf("Combine time breakdown: \n");
	for(int32_t i = 0; i < 7; ++i) {
		printf("	%lu\n", timeSpent[i]);
	}
	printf("\n");
}

static
void mergeIntersectionCorners(MergeSendOffArgs *pArgs, bool preserve) {
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
					if (getIfStuc(pPiece->pEntry, k)) {
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
						STUC_ASSERT("corner marked add, but sort didn't touch it?",
						            pPiece->pOrder[k] > 0);
						addBorderCornerAndVert(pArgs, pPiece, pPieceRoot, k, !preserve);
					}
				}
				pPiece = pPiece->pNext;
			} while(pPiece);
		}
		STUC_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
}

static
void mergeCornerAttribs(MergeSendOffArgs *pArgs) {
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
					if ((pPiece->add >> k & 0x1) &&
						!getIfStuc(pPiece->pEntry, k) &&
						getIfOnInVert(pPiece->pEntry, k)) {

						mergeAttribsForSingleCorner(pArgs, pPiece, k);
					}
				}
				pPiece = pPiece->pNext;
			} while(pPiece);
		}
		STUC_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
}

static
void transformDeferredVert(MergeSendOffArgs *pArgs, Piece *pPiece,
                           BufMesh *pBufMesh, FaceRange *pMapFace,
                           int32_t cornerLocal, V2_I16 tile) {
	BorderFace *pEntry = pPiece->pEntry;
	int32_t corner = pPiece->bufFace.start - cornerLocal;
	int32_t vert = bufMeshGetVertIdx(pPiece, pBufMesh, cornerLocal);
	V3_F32 posFlat = pBufMesh->mesh.pVerts[vert];
	float w = pBufMesh->pW[corner];
	V3_F32 projNormal = pBufMesh->pInNormal[corner];
	V3_F32 inTangent = pBufMesh->pInTangent[corner];
	float inTSign = pBufMesh->pInTSign[corner];
	Mat3x3 tbn;
	*(V3_F32 *)&tbn.d[0] = inTangent;
	*(V3_F32 *)&tbn.d[1] = _(_(projNormal V3CROSS inTangent) V3MULS inTSign);
	*(V3_F32 *)&tbn.d[2] = projNormal;
	V3_F32 pos = _(posFlat V3ADD _(projNormal V3MULS w * pArgs->wScale));
	V3_F32 normal = {0};
	V2_F32 fTileMin = {(float)tile.d[0], (float)tile.d[1]};
	bool normalTransformed = false;
	if (!getIfOnInVert(pEntry, cornerLocal) && !pArgs->ppInFaceTable) {
		V3_F32 uvw;
		*(V2_F32 *)&uvw = _(pBufMesh->mesh.pStuc[corner] V2SUB fTileMin);
		uvw.d[2] = pBufMesh->pW[corner];
		StucMap pMap = pArgs->pMap;
		V3_F32 usgBc = {0};
		bool transformed = false;
		for (int32_t i = 0; i < pMapFace->size; ++i) {
			int32_t mapVert = pMap->mesh.mesh.pCorners[pMapFace->start + i];
			if (!pMap->mesh.pUsg) {
				continue;
			}
			int32_t usgIdx = pMap->mesh.pUsg[mapVert];
			if (!usgIdx) {
				continue;
			}
			usgIdx = abs(usgIdx) - 1;
			Usg *pUsg = pMap->usgArr.pArr + usgIdx;
			if (isPointInsideMesh(&pArgs->pContext->alloc, uvw, pUsg->pMesh)) {
				bool flatCutoff = pUsg->pFlatCutoff &&
					isPointInsideMesh(&pArgs->pContext->alloc, uvw, pUsg->pFlatCutoff);
				int32_t inFaceOffset = pArgs->pJobArgs[pEntry->job].inFaceOffset;
				bool inside = sampleUsg(i, uvw, &posFlat, &transformed,
				                        &usgBc, *pMapFace, pMap, pEntry->baseFace + inFaceOffset,
				                        pArgs->pInMesh, &normal, fTileMin, flatCutoff, true,
				                        &tbn);
				if (inside) {
					pos = _(posFlat V3ADD _(normal V3MULS w * pArgs->wScale));
					if (transformed) {
						normalTransformed = true;
						normal = _(pBufMesh->mesh.pNormals[corner] V3MULM3X3 &tbn);
					}
					break;
				}
			}
		}
	}
	if (!normalTransformed) {
		normal = _(pBufMesh->mesh.pNormals[corner] V3MULM3X3 &tbn);
	}
	pBufMesh->mesh.pVerts[vert] = pos;
	pBufMesh->mesh.pNormals[corner] = normal;
}

static
void transformDefferedCorners(MergeSendOffArgs *pArgs,
                            FaceRange *pMapFace, Piece *pPiece) {
	do {
		BufMesh *pBufMesh = &pArgs->pJobArgs[pPiece->pEntry->job].bufMesh;
		for (int32_t i = 0; i < pPiece->bufFace.size; ++i) {
			if (getIfStuc(pPiece->pEntry, i) || !(pPiece->add >> i & 0x1)) {
				continue;
			}
			transformDeferredVert(pArgs, pPiece, pBufMesh, pMapFace,
			                      i, pPiece->tile);
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
			pPiece->pOrder[i] = count - pPiece->pOrder[i];
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void createAndJoinPieces(MergeSendOffArgs *pArgs) {
	CLOCK_INIT;
	StucContext pContext = pArgs->pContext;
	StucAlloc *pAlloc = &pContext->alloc;
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
		STUC_ASSERT("", entryCount);
		//int32_t seamFace = ;
		FaceRange stucFace =
			getFaceRange(&pArgs->pMap->mesh.mesh, pEntry->faceIdx, false);
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
		STUC_ASSERT("", pPieceRoots->count > 0);
		int32_t aproxVertsPerPiece = totalVerts / pPieceRoots->count;
		STUC_ASSERT("", aproxVertsPerPiece != 0);
		for (int32_t j = 0; j < pPieceRoots->count; ++j) {
			Piece *pPiece = pPieceArr->pArr + pPieceRoots->pArr[j];
			STUC_ASSERT("", pPiece->pEntry);
			markKeepInVertsPreserve(pArgs, pArgs->pMap, pPiece);
			sortCorners(pArgs, pPiece, pPieceArr, pSharedEdges, edgeTableSize, &totalVerts);
			if (!pPiece->pEntry->inOrient) {
				invertWind(pPiece, totalVerts);
			}
			transformDefferedCorners(pArgs, &stucFace, pPiece);
			if (totalVerts > pArgs->totalVerts) {
				pArgs->totalVerts = totalVerts;
			}
#ifndef STUC_DISABLE_TRIANGULATION
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
	StucContext pContext = pArgs->pContext;
	void *pThreadPoolHandle = pContext->pThreadPoolHandle;
	StucThreadPool *pThreadPool = &pArgs->pContext->threadPool;
	createAndJoinPieces(pArgs);
	bool barrierRet;
	barrierRet = pThreadPool->pBarrierWait(pThreadPoolHandle, pArgs->pBarrier);
	if (barrierRet) {
		for (int32_t i = 0; i < *pArgs->pActiveJobs; ++i) {
			mergeIntersectionCorners(pArgs->pArgArr + i, false);
			mergeIntersectionCorners(pArgs->pArgArr + i, true);
			mergeCornerAttribs(pArgs->pArgArr + i);
		}
	}
	barrierRet = pThreadPool->pBarrierWait(pThreadPoolHandle, pArgs->pBarrier);
	if (barrierRet) {
		for (int32_t i = 0; i < *pArgs->pActiveJobs; ++i) {
			addToOutMesh(pArgs->pArgArr + i);
		}
		*pArgs->pActiveJobs = 0;
	}
	pContext->alloc.pFree(pArgs->pInVertKeep);
}

static
void linkEntriesFromOtherJobs(StucContext pContext, SendOffArgs *pJobArgs,
                              BorderBucket *pBucket, int32_t faceIdx,
							  int32_t hash, int32_t job, int32_t mapJobsSent) {
	for (int32_t j = job + 1; j < mapJobsSent; ++j) {
		if (!pJobArgs[j].bufSize) {
			continue;
		}
		//STUC_ASSERT("", pJobArgs[j].borderTable.size > 0);
		//STUC_ASSERT("", pJobArgs[j].borderTable.pTable != NULL);
		BorderBucket *pBucketOther = pJobArgs[j].borderTable.pTable + hash;
		//STUC_ASSERT("", pBucketOther != NULL);
		do {
			if (pBucketOther->pEntry) {
				if (faceIdx == pBucketOther->pEntry->faceIdx) {
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
void compileBorderTables(StucContext pContext, SendOffArgs *pJobArgs,
                         CompiledBorderTable *pBorderTable,
						 int32_t totalBorderFaces, int32_t mapJobsSent) {
	pBorderTable->ppTable =
		pContext->alloc.pMalloc(sizeof(void *) * totalBorderFaces);
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		if (!pJobArgs[i].bufSize) {
			//TODO why is bufsize zero? how? find out
			continue; //skip if buf mesh is empty
		}
		for (int32_t hash = 0; hash < pJobArgs[i].borderTable.size; ++hash) {
			STUC_ASSERT("", pJobArgs[i].borderTable.size > 0);
			STUC_ASSERT("", pJobArgs[i].borderTable.pTable);
			BorderBucket *pBucket = pJobArgs[i].borderTable.pTable + hash;
			int32_t depth = 0;
			do {
				if (pBucket->pEntry) {
					int32_t faceIdx = pBucket->pEntry->faceIdx;
					STUC_ASSERT("", faceIdx >= 0);
					linkEntriesFromOtherJobs(pContext, pJobArgs, pBucket,
					                         faceIdx, hash, i, mapJobsSent);
					STUC_ASSERT("", pBorderTable->count >= 0);
					STUC_ASSERT("", pBorderTable->count < totalBorderFaces);
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
			STUC_ASSERT("", hash >= 0 && hash < pJobArgs[i].borderTable.size);
		}
		STUC_ASSERT("", i >= 0 && i < mapJobsSent);
	}
}

static
void allocCombineTables(StucAlloc *pAlloc, CombineTables *pCTables,
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
void destroyCombineTables(StucAlloc *pAlloc, CombineTables *pCTables) {
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
void sendOffMergeJobs(StucContext pContext, CompiledBorderTable *pBorderTable,
                      MergeSendOffArgs *pMergeJobArgs, StucMap pMap,
					  Mesh *pMeshOut, SendOffArgs *pMapJobArgs,
					  EdgeVerts *pEdgeVerts, int8_t *pVertSeamTable,
					  CombineTables *pCTables, JobBases *pJobBases,
					  int32_t *pActiveJobs, void *pMutex, bool *pEdgeSeamTable,
                      InFaceArr **ppInFaceTable, float wScale, Mesh *pInMesh, void *pBarrier) {
	int32_t entriesPerJob = pBorderTable->count / pContext->threadCount;
	bool singleThread = !entriesPerJob;
	void *jobArgPtrs[MAX_THREADS];
	*pActiveJobs = singleThread ? 1 : pContext->threadCount;
	pContext->threadPool.pBarrierGet(pContext->pThreadPoolHandle, &pBarrier, *pActiveJobs);
	for (int32_t i = 0; i < *pActiveJobs; ++i) {
		int32_t entriesStart = entriesPerJob * i;
		int32_t entriesEnd = i == *pActiveJobs - 1 ?
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
		pMergeJobArgs[i].pActiveJobs = pActiveJobs;
		pMergeJobArgs[i].pBarrier = pBarrier;
		pMergeJobArgs[i].pMutex = pMutex;
		pMergeJobArgs[i].wScale = wScale;
		pMergeJobArgs[i].pInMesh = pInMesh;
		pMergeJobArgs[i].totalVerts = 4;
		jobArgPtrs[i] = pMergeJobArgs + i;
	}
	pContext->threadPool.pJobStackPushJobs(pContext->pThreadPoolHandle,
	                                       *pActiveJobs,
										   mergeAndCopyEdgeFaces, jobArgPtrs);
}

void stucMergeBorderFaces(StucContext pContext, StucMap pMap, Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
					      JobBases *pJobBases, int8_t *pVertSeamTable,
                          bool *pEdgeSeamTable, InFaceArr **ppInFaceTable,
                          float wScale, Mesh *pInMesh, int32_t mapJobsSent) {
	int32_t totalBorderFaces = 0;
	int32_t totalBorderEdges = 0;
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		totalBorderFaces += pJobArgs[i].bufMesh.borderFaceCount;
		totalBorderEdges += pJobArgs[i].bufMesh.borderEdgeCount;
		STUC_ASSERT("", i < mapJobsSent);
	}
	STUC_ASSERT("", totalBorderFaces >= 0 && totalBorderFaces < 100000000);
	STUC_ASSERT("", totalBorderEdges >= 0 && totalBorderEdges < 100000000);
	CompiledBorderTable borderTable = {0};
	//compile border table entries from all jobs, into a single table
	compileBorderTables(pContext, pJobArgs, &borderTable,
	                    totalBorderFaces, mapJobsSent);
	//tables used for merging mesh mesh data correctly
	CombineTables cTables = {0};
	allocCombineTables(&pContext->alloc, &cTables, totalBorderFaces,
	                   totalBorderEdges);
	for (int32_t i = 0; i < pJobArgs[0].mesh.mesh.vertCount; ++i) {
		int32_t preserve = pJobArgs[0].pInVertTable[i];
		for (int32_t j = 1; j < mapJobsSent; ++j) {
			preserve |= pJobArgs[j].pInVertTable[i];
			STUC_ASSERT("", j >= 0 && j < mapJobsSent);
		}
		pJobArgs[0].pInVertTable[i] = preserve;
		STUC_ASSERT("", i >= 0 && i < pJobArgs[0].mesh.mesh.vertCount);
	}
	MergeSendOffArgs mergeJobArgs[MAX_THREADS];
	int32_t activeJobs = 0;
	int32_t fence = 0;
	void *pMutex = NULL;
	void *pBarrier = NULL;
	pContext->threadPool.pMutexGet(pContext->pThreadPoolHandle, &pMutex);
	sendOffMergeJobs(pContext, &borderTable, mergeJobArgs, pMap, pMeshOut,
	                 pJobArgs, pEdgeVerts, pVertSeamTable, &cTables, pJobBases,
					 &activeJobs, pMutex, pEdgeSeamTable, ppInFaceTable,
	                 wScale, pInMesh, pBarrier);
	waitForJobs(pContext, &activeJobs, pMutex);
	pContext->threadPool.pMutexDestroy(pContext->pThreadPoolHandle, pMutex);
	pContext->threadPool.pBarrierDestroy(pContext->pThreadPoolHandle, pBarrier);
	pContext->alloc.pFree(borderTable.ppTable);
	destroyCombineTables(&pContext->alloc, &cTables);
}
