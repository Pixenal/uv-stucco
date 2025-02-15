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
#include <DebugDump.h>

typedef struct SharedEdge {
	struct SharedEdge *pNext;
	void *pLast;
	I32 entries[2];
	I32 refIdx[2];
	I32 edge;
	I32 validIdx;
	I16 corner[2];
	V2_I16 tile;
	I8 receive;
	I8 segment;
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
void initSharedEdgeEntry(
	SharedEdge *pEntry,
	I32 baseEdge,
	I32 entryIdx,
	I32 refIdx,
	bool isPreserve,
	bool isReceive,
	Piece *pPiece,
	I32 i,
	bool seam,
	I32 segment
) {
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
void checkIfSeamOrPreserve(
	MergeSendOffArgs *pArgs,
	Piece *pEntries,
	I32 entryIdx,
	BorderInInfo *pInInfo,
	bool isOnInVert,
	I32 corner,
	bool *pHasPreserve,
	bool *pSeam,
	bool *pIsPreserve,
	bool *pIsReceive,
	I32 *pRefIdx
) {
	Piece *pPiece = pEntries + entryIdx;
	*pSeam = pArgs->pBasic->pEdgeSeamTable[pInInfo->edge];

	*pIsPreserve = stucCheckIfEdgeIsPreserve(pArgs->pBasic->pInMesh, pInInfo->edge);
	if (*pIsPreserve && !*pHasPreserve) {
		*pHasPreserve = true;
	}
	*pIsReceive = false;
	if (*pSeam) {
		*pRefIdx = isOnInVert ? -1 : 1;
	}
	else if (*pIsPreserve) {
		if (isOnInVert) {
			*pIsReceive = true;
			//negate if base corner
			*pRefIdx = (pInInfo->vert + 1) * -1;
		}
		else {
			I32 mapCorner = stucGetMapCorner(pPiece->pEntry, corner);
			STUC_ASSERT("", pPiece->pEntry->mapFace < pArgs->pBasic->pMap->mesh.core.faceCount);
			I32 mapFaceStart = pArgs->pBasic->pMap->mesh.core.pFaces[pPiece->pEntry->mapFace];
			STUC_ASSERT("", mapFaceStart < pArgs->pBasic->pMap->mesh.core.cornerCount);
			I32 mapEdge = pArgs->pBasic->pMap->mesh.core.pEdges[mapFaceStart + mapCorner];
			STUC_ASSERT("", mapEdge < pArgs->pBasic->pMap->mesh.core.edgeCount);
			*pIsReceive = stucCheckIfEdgeIsReceive(&pArgs->pBasic->pMap->mesh, mapEdge);
			*pRefIdx = mapEdge;
			if (*pIsReceive) {
				pPiece->keepPreserve |= true << corner;
			}
		}
	}
}

static
void addToTable(
	MergeSendOffArgs *pArgs,
	Piece *pPiece,
	SharedEdgeWrap *pSharedEdges,
	I32 tableSize,
	BorderInInfo *pInInfo,
	I32 corner,
	bool seam,
	bool isPreserve,
	bool isReceive,
	I32 refIdx,
	I32 segment
) {
	I32 hash = stucFnvHash((U8 *)&pInInfo->edge, 4, tableSize);
	SharedEdgeWrap *pEdgeEntryWrap = pSharedEdges + hash;
	SharedEdge *pEdgeEntry = pEdgeEntryWrap->pEntry;
	if (!pEdgeEntry) {
		pEdgeEntry = pEdgeEntryWrap->pEntry =
			pArgs->pBasic->pCtx->alloc.pCalloc(1, sizeof(SharedEdge));
		pEdgeEntry->pLast = pEdgeEntryWrap;
		initSharedEdgeEntry(
			pEdgeEntry,
			pInInfo->edge,
			pPiece->entryIdx,
			refIdx,
			isPreserve,
			isReceive,
			pPiece,
			corner,
			seam,
			segment
		);
		return;
	}
	do {
		STUC_ASSERT("", pEdgeEntry->edge - 1 >= 0);
		STUC_ASSERT("", pEdgeEntry->edge - 1 < pArgs->pBasic->pInMesh->core.edgeCount);
		STUC_ASSERT("", pEdgeEntry->idx % 2 == pEdgeEntry->idx); // range 0 .. 1
		if (pEdgeEntry->edge == pInInfo->edge + 1 &&
			pEdgeEntry->segment == segment &&
			pEdgeEntry->tile.d[0] == pPiece->tile.d[0] &&
			pEdgeEntry->tile.d[1] == pPiece->tile.d[1]) {

			if (pEdgeEntry->entries[pEdgeEntry->idx] != pPiece->entryIdx) {
				//other side of the edge
				pEdgeEntry->entries[1] = pPiece->entryIdx;
				pEdgeEntry->corner[1] = corner;
				pEdgeEntry->idx = 1;
				pEdgeEntry->refIdx[1] = refIdx;
				if (pEdgeEntry->inOrient != pPiece->pEntry->inOrient) {
					pEdgeEntry->removed = true;
				}
			}
			if (!pEdgeEntry->seam &&
				!pEdgeEntry->altIdx &&
				isPreserve && pEdgeEntry->refIdx[0] != refIdx) {
				pEdgeEntry->receive += isReceive;
				pEdgeEntry->altIdx = 1;
			}
			break;
		}
		if (!pEdgeEntry->pNext) {
			pEdgeEntry->pNext =
				pArgs->pBasic->pCtx->alloc.pCalloc(1, sizeof(SharedEdge));
			pEdgeEntry->pNext->pLast = pEdgeEntry;
			pEdgeEntry = pEdgeEntry->pNext;
			initSharedEdgeEntry(
				pEdgeEntry,
				pInInfo->edge,
				pPiece->entryIdx,
				refIdx,
				isPreserve,
				isReceive,
				pPiece,
				corner,
				seam,
				segment
			);
			break;
		}
		pEdgeEntry = pEdgeEntry->pNext;
	} while(true);
}

static
void addEntryToSharedEdgeTable(
	MergeSendOffArgs *pArgs,
	BorderFace *pEntry,
	SharedEdgeWrap *pSharedEdges,
	Piece *pEntries,
	I32 tableSize,
	I32 entryIdx,
	I32 *pTotalVerts,
	bool *pHasPreserve
) {
	const StucAlloc *pAlloc = &pArgs->pBasic->pCtx->alloc;
	STUC_ASSERT("", (tableSize > 0 && entryIdx >= 0) || entryIdx == 0);
	Piece *pPiece = pEntries + entryIdx;
	BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
	FaceRange face = stucGetFaceRange(&pBufMesh->mesh.core, pEntry->bufFace, true);
	pPiece->bufFace = face;
	pPiece->pEntry = pEntry;
	pPiece->entryIdx = entryIdx;
	pPiece->pOrder = pAlloc->pCalloc(face.size, 1);
	pPiece->pEdges = pAlloc->pCalloc(face.size, sizeof(EdgeSegmentPair));
	pPiece->tile = stucGetTileMinFromBoundsEntry(pEntry);
	for (I32 i = 0; i < face.size; ++i) {
		STUC_ASSERT("", pTotalVerts && *pTotalVerts >= 0 && *pTotalVerts < 10000);
		++*pTotalVerts;
		bool isStuc = stucGetIfStuc(pEntry, i);
		bool isOnLine = stucGetIfOnLine(pEntry, i);
		if (isStuc && !isOnLine) {
			//stuc corner - skip
			continue;
		}
		bool isOnInVert = stucGetIfOnInVert(pEntry, i);
		//Get in mesh details for current buf corner
		BorderInInfo inInfo = stucGetBorderEntryInInfo(pArgs->pBasic, pEntry, i);
		I32 lasti = i ? i - 1 : face.size - 1;
		if (stucGetBaseCorner(pEntry, i) == stucGetBaseCorner(pEntry, lasti) &&
			!(stucGetIfStuc(pEntry, lasti) && !stucGetIfOnLine(pEntry, lasti))) {
			//Edge belongs to last corner, not this one
			continue;
		}
		if (isOnInVert &&
			stucCheckIfVertIsPreserve(pArgs->pBasic->pInMesh, inInfo.vert)) {
			//This does not necessarily mean this vert will be kept,
			//only corners encountered in sortCorners func will be kept.
			//ie, only corners on the exterior. Interior corners are skipped.
			pPiece->keepVertPreserve |= true << i;
		}
		I32* pVerts = pArgs->pBasic->pEdgeVerts[inInfo.edge].verts;
		STUC_ASSERT("",
			pVerts &&
			(pVerts[0] == inInfo.edgeCorner ||
			pVerts[1] == inInfo.edgeCorner)
		);
		if (pVerts[1] < 0) {
			//no other vert on edge
			pPiece->hasSeam = true;
			continue;
		}
		bool baseKeep;
		if (isOnInVert) {
			STUC_ASSERT("", pArgs->pBasic->pInVertTable[inInfo.vert] >= 0); //pInVertTable is 0 .. 3
			STUC_ASSERT("", pArgs->pBasic->pInVertTable[inInfo.vert] <= 3); 
			baseKeep = pArgs->pBasic->pInVertTable[inInfo.vert] > 0;
			pPiece->keepPreserve |= baseKeep << i;
		}
		if (!pSharedEdges) {
			//If shared edges if NULL, then there's only 1 border face entry.
			//So no need for a shared edge table
			STUC_ASSERT("", entryIdx == 0);
			continue;
		}
		pEntries[entryIdx].pEdges[pEntries[entryIdx].edgeCount].edge = inInfo.edge;
		I32 segment = stucGetSegment(pPiece->pEntry, i);
		pEntries[entryIdx].pEdges[pEntries[entryIdx].edgeCount].segment = segment;
		pEntries[entryIdx].edgeCount++;

		bool seam = false;
		bool isPreserve = false;
		bool isReceive = false;
		I32 refIdx = 0;
		checkIfSeamOrPreserve(
			pArgs,
			pEntries,
			entryIdx,
			&inInfo,
			isOnInVert,
			i,
			pHasPreserve,
			&seam,
			&isPreserve,
			&isReceive,
			&refIdx
		);

		addToTable(
			pArgs,
			pPiece,
			pSharedEdges,
			tableSize,
			&inInfo,
			i,
			seam,
			isPreserve,
			isReceive,
			refIdx,
			segment
		);
		STUC_ASSERT("", i >= 0 && i < face.size);
	}
}

static
Piece *getEntryInPiece(Piece *pPieceRoot, I32 otherPiece) {
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
Piece *getNeighbourEntry(
	MergeSendOffArgs *pArgs,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	Piece *pPiece,
	Piece *pPieceRoot,
	I32 *pCorner,
	SharedEdge **ppEdge
) {
	I32 segment = stucGetSegment(pPiece->pEntry, *pCorner);
	BorderInInfo inInfo =
		stucGetBorderEntryInInfo(pArgs->pBasic, pPiece->pEntry, *pCorner);
	I32 hash = stucFnvHash((U8*)&inInfo.edge, 4, edgeTableSize);
	SharedEdgeWrap* pEdgeEntryWrap = pEdgeTable + hash;
	SharedEdge* pEdgeEntry = pEdgeEntryWrap->pEntry;
	while (pEdgeEntry) {
		if (pEdgeEntry->idx && !pEdgeEntry->removed) {
			bool cornerMatches =
				*pCorner == pEdgeEntry->corner[0] &&
				pPiece->entryIdx == pEdgeEntry->entries[0] ||
				*pCorner == pEdgeEntry->corner[1] &&
				pPiece->entryIdx == pEdgeEntry->entries[1];
			if (cornerMatches &&
			    inInfo.edge + 1 == pEdgeEntry->edge &&
			    segment == pEdgeEntry->segment &&
			    pPiece->tile.d[0] == pEdgeEntry->tile.d[0] &&
			    pPiece->tile.d[1] == pEdgeEntry->tile.d[1]) {

				bool which = pEdgeEntry->entries[1] == pPiece->entryIdx;
				I32 otherPiece = pEdgeEntry->entries[!which];
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
bool isCornerOnExterior(
	MergeSendOffArgs *pArgs,
	Piece *pPiece,
	Piece *pPieceRoot,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	I32 corner
) {
	if (!getNeighbourEntry(
		pArgs,
		pEdgeTable,
		edgeTableSize,
		pPiece,
		pPieceRoot,
		&corner,
		NULL
	)) {
		//corner does not share an edge without any other corner,
		//must be on outside
		return true;
	}
	return false;
}

static
I32 getStartingCorner(
	Piece **ppPiece,
	MergeSendOffArgs *pArgs,
	Piece *pPieceRoot,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize
) {
	do {
		for (I32 i = 0; i < (*ppPiece)->bufFace.size; ++i) {
			if (isCornerOnExterior(
				pArgs,
				*ppPiece,
				pPieceRoot,
				pEdgeTable,
				edgeTableSize,
				i
			)) {
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
	I32 corner;
	I32 cornerPrev;
	I32 startCorner;
	I32 validBranches;
	bool quad;
} EdgeStack;

static
bool checkIfIntersectsReceive(
	EdgeStack *pItem,
	Mesh *pBufMesh,
	StucMap pMap,
	FaceRange *pMapFace,
	I32 *pMapCorner,
	bool side
) {
	Mesh *pMapMesh = &pMap->mesh;
	STUC_ASSERT("", pItem->pPiece->bufFace.size >= 3);
	I32 cornerNext = (pItem->corner + 1) % pItem->pPiece->bufFace.size;
	bool isOnInVert;
	if (side) {
		isOnInVert = stucGetIfOnInVert(pItem->pPiece->pEntry, cornerNext);
	}
	else {
		isOnInVert = stucGetIfOnInVert(pItem->pPiece->pEntry, pItem->corner);
	}
	if (!isOnInVert) {
		//exterior intersects with a map edge,
		//so just check if said edge is receive
		I32 corner;
		if (side) {
			corner = cornerNext;
		}
		else {
			corner = pItem->corner == 0 ?
				pItem->pPiece->bufFace.size - 1 : pItem->corner - 1;
		}
		*pMapCorner = stucGetMapCorner(pItem->pPiece->pEntry, corner);
		STUC_ASSERT("", *pMapCorner >= 0 && *pMapCorner < pMapFace->size);
		*pMapCorner += pMapFace->start;
		I32 mapEdge = pMapMesh->core.pEdges[*pMapCorner];
		return pMapMesh->pEdgeReceive[mapEdge];
	}
	//exterior does not intersect with a map edge.
	//In this case, we perform an intersect test,
	//and use that to see if the base edge would intersect
	//with a preserve edge, were it to extend out infinitely
	V2_F32 *pUvStart = pBufMesh->pUvs + pItem->pPiece->bufFace.start;
	I32 corner = pItem->corner;
	if (side) {
		corner = cornerNext;
		cornerNext = pItem->corner;
	}
	V2_F32 fTileMin = {
		(F32)pItem->pPiece->tile.d[0],
		(F32)pItem->pPiece->tile.d[1]
	};
	V2_F32 c = _(pUvStart[-corner] V2SUB fTileMin);
	V2_F32 d = _(pUvStart[-cornerNext] V2SUB fTileMin);
	V2_F32 cd = _(d V2SUB c);
	for (I32 i = 0; i < pMapFace->size; ++i) {
		*pMapCorner = pMapFace->start + i;
		I32 mapEdge = pMapMesh->core.pEdges[*pMapCorner];
		if (!pMapMesh->pEdgeReceive[mapEdge]) {
			continue;
		}
		F32 t = .0f;
		I32 iNext = (i + 1) % pMapFace->size;
		I32 mapVert = pMapMesh->core.pCorners[*pMapCorner];
		I32 mapCornerNext = pMapFace->start + iNext;
		I32 mapVertNext = pMapMesh->core.pCorners[mapCornerNext];
		//STUC_ASSERT("", !_(*(V2_F32 *)&pMapMesh->pVerts[mapVert] V2EQL *(V2_F32 *)&pMapMesh->pVerts[mapVertNext]));
		V3_F32 intersect = {0};
		bool valid = stucCalcIntersection(
			pMapMesh->pVerts[mapVert],
			pMapMesh->pVerts[mapVertNext],
			c,
			cd,
			&intersect,
			&t,
			NULL
		);
		//do you need to handle .0 or 1.0 as distinct cases?
		//ie, should you track preserve verts hit?

		V2_F32 ci = _(*(V2_F32 *)&intersect V2SUB c);
		F32 dot = _(ci V2DOT cd);
		if (!valid || t < .0f || t > 1.0f || dot > .0f) {
			continue;
		}
		return true;
	}
	return false;
}

static
void pushToEdgeStack(
	EdgeStack *pStack,
	I32 *pStackPtr,
	I32 treeCount,
	SharedEdge *pEdge,
	Piece *pPiece,
	I32 corner
) {
	pStack[*pStackPtr].pActiveEdge = pEdge;
	pEdge->validIdx = treeCount;
	++*pStackPtr;
	EdgeStack next = {.pPiece = pPiece, .corner = corner};
	pStack[*pStackPtr] = next;
}

static
bool handleExterior(
	EdgeStack *pStack,
	I32 *pStackPtr,
	EdgeStack *pNeighbour,
	bool *pValid,
	I32 treeCount,
	I32 *pReceive,
	MergeSendOffArgs *pArgs,
	FaceRange *pMapFace,
	SharedEdge *pEdge,
	bool side
) {
	EdgeStack *pItem = pStack + *pStackPtr;
	Mesh *pMapMesh = &pArgs->pBasic->pMap->mesh;
	Mesh *pBufMesh = (Mesh *)&pArgs->pJobArgs[pItem->pPiece->pEntry->job].bufMesh;
	I32 mapCorner = -1;
	bool isReceive = checkIfIntersectsReceive(
		pNeighbour,
		pBufMesh,
		pArgs->pBasic->pMap,
		pMapFace,
		&mapCorner,
		side
	);
	if (isReceive) {
		//preserve edge intersects receive edge. Add to count
		STUC_ASSERT("", mapCorner >= 0 && mapCorner < pMapMesh->core.cornerCount);
		STUC_ASSERT("", *pReceive >= -1 && *pReceive < pMapMesh->core.cornerCount);
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
SharedEdge *getIfQuadJunc(
	MergeSendOffArgs *pArgs,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	Piece *pPieceRoot,
	EdgeStack *pItem,
	EdgeStack *pNeighbour
) {
	EdgeStack copy = *pItem;
	I32 idx = -1;
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
		//Set next corner
		EdgeStack neighbour = {.corner = copy.corner};
		neighbour.pPiece = getNeighbourEntry(
			pArgs,
			pEdgeTable,
			edgeTableSize,
			copy.pPiece,
			pPieceRoot,
			&neighbour.corner,
			cache + idx
		);
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
void handleIfFullyUnwound(
	MergeSendOffArgs *pArgs,
	I32 *pStackPtr,
	EdgeStack *pItem,
	bool *pUnwind
) {
	*pUnwind = !pItem->validBranches;
	if (!*pUnwind) {
		bool onInVert = stucGetIfOnInVert(pItem->pPiece->pEntry, pItem->corner);
		if (onInVert) {
			BorderInInfo inInfo =
				stucGetBorderEntryInInfo(
					pArgs->pBasic,
					pItem->pPiece->pEntry,
					pItem->corner
				);
			stucSetBitArr(pArgs->pInVertKeep, inInfo.vert, true, 1);
		}
	}
	--*pStackPtr;
	return;
}

static
void decideNextEdge(
	MergeSendOffArgs *pArgs,
	EdgeStack *pStack,
	I32 *pStackPtr,
	bool *pValid,
	I32 treeCount,
	I32 *pReceive,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	Piece *pPieceRoot,
	FaceRange *pMapFace,
	EdgeStack *pItem,
	EdgeStack *pNeighbour,
	SharedEdge *pEdge,
	bool *pRet
) {
	Piece *pPieceNext = pItem->quad ? pNeighbour->pPiece : pItem->pPiece;
	I32 cornerNext = pItem->quad ? pNeighbour->corner : pItem->corner;
	cornerNext = (cornerNext + 1) % pPieceNext->bufFace.size;
	if (*pReceive != -1) {
		bool exterior;
		exterior = isCornerOnExterior(
			pArgs,
			pPieceNext,
			pPieceRoot,
			pEdgeTable,
			edgeTableSize,
			cornerNext
		);
		if (exterior) {
			EdgeStack *pItemToTest = pItem->quad ? pNeighbour : pItem;
			handleExterior(
				pStack,
				pStackPtr,
				pItemToTest,
				pValid,
				treeCount,
				pReceive,
				pArgs,
				pMapFace,
				pEdge,
				true
			);
			if (!*pStackPtr) {
				*pRet = true;
			}
		}
		else {
			pushToEdgeStack(pStack, pStackPtr, treeCount, pEdge, pPieceNext ,cornerNext);
			*pRet = true;
		}
	}
}

static
void setKeepIfOnInVert(MergeSendOffArgs *pArgs, Piece *pPiece, I32 corner) {
	bool onInVert = stucGetIfOnInVert(pPiece->pEntry, corner);
	if (onInVert) {
		BorderInInfo inInfo =
			stucGetBorderEntryInInfo(pArgs->pBasic, pPiece->pEntry, corner);
		stucSetBitArr(pArgs->pInVertKeep, inInfo.vert, true, 1);
	}
}

static
void walkEdgesForPreserve(
	EdgeStack *pStack,
	I32 *pStackPtr,
	bool *pValid,
	I32 treeCount,
	I32 *pReceive,
	MergeSendOffArgs *pArgs,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	Piece *pPieceRoot,
	FaceRange *pMapFace,
	bool *pUnwind
) {
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
			setKeepIfOnInVert(pArgs, pItem->pPiecePrev, pItem->cornerPrev);
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
				pEdge = getIfQuadJunc(
					pArgs,
					pEdgeTable,
					edgeTableSize,
					pPieceRoot,
					pItem,
					&neighbour
				);
				if (pEdge) {
					pItem->quad = true;
				}
			}
		}
		else if (pItem->quad ||
		         (pItem->pStartPiece == pItem->pPiece &&
		          pItem->startCorner == pItem->corner)) {
			handleIfFullyUnwound(pArgs, pStackPtr, pItem, pUnwind);
			return;
		}
		//Set next corner
		if (!*pStackPtr &&
		    stucGetIfStuc(pItem->pPiece->pEntry, pItem->corner) &&
		    !stucGetIfOnLine(pItem->pPiece->pEntry, pItem->corner)) {

			pItem->corner++;
			continue;
		}
		if (!pItem->quad) {
			neighbour.corner = pItem->corner;
			neighbour.pPiece = getNeighbourEntry(
				pArgs,
				pEdgeTable,
				edgeTableSize,
				pItem->pPiece,
				pPieceRoot,
				&neighbour.corner,
				&pEdge
			);
			if (!neighbour.pPiece) {
				pItem->corner++;
				continue;
			}
		}
		STUC_ASSERT("", pEdge);
		//if validIdx isn't -1, this edge has already been checked
		if (pEdge->preserve && pEdge->validIdx == -1) {
			if (!*pStackPtr) {
				handleExterior(
					pStack,
					pStackPtr,
					pItem,
					pValid,
					treeCount,
					pReceive,
					pArgs,
					pMapFace,
					pEdge,
					false
				);
			}
			decideNextEdge(
				pArgs,
				pStack,
				pStackPtr,
				pValid,
				treeCount,
				pReceive,
				pEdgeTable,
				edgeTableSize,
				pPieceRoot,
				pMapFace,
				pItem,
				&neighbour,
				pEdge,
				&ret
			);
		}
		else if (!*pStackPtr && pEdge->preserve) {
			setKeepIfOnInVert(pArgs, pItem->pPiece, pItem->corner);
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
void validatePreserveEdges(
	MergeSendOffArgs *pArgs,
	PieceArr *pPieceArr,
	I32 piece,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	bool **ppValid,
	I32 *pValidCount,
	I32 *pValidSize
) {
	//TODO first , check if the map face even has any preserve edges,
	//     no point doing all this if not
	const StucAlloc *pAlloc = &pArgs->pBasic->pCtx->alloc;
	// Get first not exterior corner
	// This is done to ensure we don't start inside the face
	Piece *pPiece = pPieceArr->pArr + piece;
	Piece *pPieceRoot = pPiece;
	FaceRange mapFace = stucGetFaceRange(
		&pArgs->pBasic->pMap->mesh.core,
		pPieceRoot->pEntry->mapFace,
		false
	);
	if (!*ppValid) {
		*pValidSize = 8;
		*ppValid = pAlloc->pCalloc(*pValidSize, sizeof(bool));
	}
	I32 stackSize = 8;
	EdgeStack *pStack = pAlloc->pCalloc(stackSize, sizeof(EdgeStack));
	pStack[0].pPiece = pPiece;
	I32 stackPtr = 0;
	pStack[0].corner = getStartingCorner(
		&pStack[0].pPiece,
		pArgs,
		pPiece,
		pEdgeTable,
		edgeTableSize
	);
	bool unwind = false;
	//note that order is used here to determine if a corner has already been checked.
	//Sorting is not done until later, and order is cleared at the end of this func.
	I32 receive = -1;
	do {
		if (*pValidCount == *pValidSize) {
			I32 oldSize = *pValidSize;
			*pValidSize *= 2;
			*ppValid = pAlloc->pRealloc(*ppValid, sizeof(bool) * *pValidSize);
			memset(*ppValid + oldSize, 0, sizeof(bool) * oldSize);
		}
		walkEdgesForPreserve(
			pStack,
			&stackPtr,
			*ppValid,
			*pValidCount,
			&receive,
			pArgs,
			pEdgeTable,
			edgeTableSize,
			pPieceRoot,
			&mapFace,
			&unwind
		);
		STUC_ASSERT("", stackPtr < stackSize);
		if (stackPtr == stackSize - 1) {
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
void setValidPreserveAsSeam(
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	bool *pValid,
	I32 validCount
) {
	//mark valid preserve edges as seams
	for (I32 i = 0; i < edgeTableSize; ++i) {
		SharedEdge *pEntry = pEdgeTable[i].pEntry;
		while (pEntry) {
			if (pEntry->checked || !pEntry->preserve) {
				pEntry = pEntry->pNext;
				continue;
			}
			I32 validIdx = pEntry->validIdx;
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
	for (I32 i = 0; i < pPieceArr->count; ++i) {
		Piece *pPiece = pPieceArr->pArr + i;
		if (!pPiece->listed && (pPiece->pNext || pPiece->pEntry->pNext)) {
			return true;
		}
	}
	return false;
}

static
void markKeepSeamInAdjPiece(SharedEdge *pEdgeEntry, PieceArr *pPieceArr) {
	bool aIsOnInVert = pEdgeEntry->refIdx[0] < 0;
	bool bIsOnInVert = pEdgeEntry->refIdx[1] < 0;
	if (aIsOnInVert ^ bIsOnInVert)
	{
		bool whichCorner = aIsOnInVert;

		I32 cornerA = pEdgeEntry->corner[whichCorner];
		Piece *pPieceA = pPieceArr->pArr + pEdgeEntry->entries[whichCorner];
		pPieceA->keepSeam |= 1 << cornerA;

		I32 cornerB = pEdgeEntry->corner[!whichCorner];
		Piece *pPieceB = pPieceArr->pArr + pEdgeEntry->entries[!whichCorner];
		// Also set adjacent corner
		I32 adjCorner = (cornerB + 1) % pPieceB->bufFace.size;
		pPieceB->keepSeam |= 1 << adjCorner;
	}
}

static
void addAdjPiece(
	SharedEdge *pEdgeEntry,
	Piece *pPiece,
	PieceArr *pPieceArr,
	Piece *pPieceRoot,
	BorderFace **ppTail,
	Piece **ppPieceTail
) {
	STUC_ASSERT("",
		pEdgeEntry->entries[0] == pPiece->entryIdx ||
		pEdgeEntry->entries[1] == pPiece->entryIdx
	);
	I32 whichEntry = pEdgeEntry->entries[0] == pPiece->entryIdx;
	I32 otherEntryIdx = pEdgeEntry->entries[whichEntry];
	if (pPieceArr->pArr[otherEntryIdx].listed) {
		return;
	}
	if (!pPieceRoot->hasSeam && pPieceArr->pArr[otherEntryIdx].hasSeam) {
		pPieceRoot->hasSeam = true;
	}
	// add entry to linked list
	(*ppTail)->pNext = pPieceArr->pArr[otherEntryIdx].pEntry;
	*ppTail = (*ppTail)->pNext;
	// add piece to piece linked list
	(*ppPieceTail)->pNext = pPieceArr->pArr + otherEntryIdx;
	*ppPieceTail = (*ppPieceTail)->pNext;
	pPieceArr->pArr[otherEntryIdx].listed = 1;
}

static
void getAndAddAdjPiece(
	PieceArr *pPieceArr,
	SharedEdgeWrap *pSharedEdges,
	I32 tableSize,
	Piece *pPiece,
	Piece *pPieceRoot,
	Piece **ppPieceTail,
	BorderFace **ppTail,
	I32 edgeIdx
) {
	EdgeSegmentPair edge = pPiece->pEdges[edgeIdx];
	I32 hash = stucFnvHash((U8 *)&edge, 4, tableSize);
	SharedEdgeWrap *pSharedEdgeWrap = pSharedEdges + hash;
	SharedEdge *pEdgeEntry = pSharedEdgeWrap->pEntry;
	while (pEdgeEntry) {
		if (pEdgeEntry->removed) {}
		else if (pEdgeEntry->seam) {
			markKeepSeamInAdjPiece(pEdgeEntry, pPieceArr);
		}
		else if (pEdgeEntry->edge - 1 == pPiece->pEdges[edgeIdx].edge &&
		         pEdgeEntry->segment == pPiece->pEdges[edgeIdx].segment &&
		         pEdgeEntry->tile.d[0] == pPiece->pEntry->tileX &&
		         pEdgeEntry->tile.d[1] == pPiece->pEntry->tileY) {

			addAdjPiece(pEdgeEntry, pPiece, pPieceArr, pPieceRoot, ppTail, ppPieceTail);
			break;
		}
		pEdgeEntry = pEdgeEntry->pNext;
	};
}

static
void combineConnectedIntoPiece(
	PieceArr *pPieceArr,
	SharedEdgeWrap *pSharedEdges,
	I32 tableSize,
	I32 pieceIdx
) {
	Piece *pPiece = pPieceArr->pArr + pieceIdx;
	Piece* pPieceRoot = pPiece;
	Piece *pPieceTail = pPiece;
	BorderFace *pTail = pPiece->pEntry;
	pPiece->listed = true;
	I32 depth = 0;
	do {
		STUC_ASSERT("", pPiece->edgeCount <= 64);
		//TODO can you just get the edges in the piece by calling getNeighbourEntry?
		//     Rather than storing a list of edges in the piece?
		//     Is there a perf cost?
		for (I32 j = 0; j < pPiece->edgeCount; ++j) {
			getAndAddAdjPiece(
				pPieceArr,
				pSharedEdges,
				tableSize,
				pPiece,
				pPieceRoot,
				&pPieceTail,
				&pTail,
				j
			);
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
bool isEntryInPiece(Piece *pPiece, I32 entryIdx) {
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
void joinAdjIntoPieceArr(
	MergeSendOffArgs *pArgs,
	bool hasPreserve,
	PieceRootsArr *pPieceRoots,
	PieceArr *pPieceArr,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	bool **ppValid,
	I32 *pValidCount,
	I32 *pValidSize,
	I32 i
) {
	for (I32 j = 0; j < pPieceArr->count; ++j) {
		if (pPieceArr->pArr[j].listed) {
			continue;
		}
		combineConnectedIntoPiece(pPieceArr, pEdgeTable, edgeTableSize, j);
		if (pPieceArr->pArr[j].pEntry) {
			pPieceRoots->pArr[pPieceRoots->count] = j;
			if (hasPreserve && !i) {
				//check if preserve inMesh edge intersects at least 2
				//map receiver edges. Edge is only preserved if this is so.
				validatePreserveEdges(
					pArgs,
					pPieceArr,
					j,
					pEdgeTable,
					edgeTableSize,
					ppValid,
					pValidCount,
					pValidSize
				);
			}
			++pPieceRoots->count;
		}
		STUC_ASSERT("",
			!pPieceRoots->count ||
			pPieceArr->pArr[pPieceRoots->count - 1].pEntry
		);
		STUC_ASSERT("", j >= 0 && j < pPieceArr->count);
	}
}

static
void flipSharedEdgeEntry(SharedEdge *pEntry) {
	I32 entryBuf = pEntry->entries[0];
	I32 cornerBuf = pEntry->corner[0];
	I32 refIdxBuf = pEntry->refIdx[0];
	pEntry->entries[0] = pEntry->entries[1];
	pEntry->entries[1] = entryBuf;
	pEntry->corner[0] = pEntry->corner[1];
	pEntry->corner[1] = cornerBuf;
	pEntry->refIdx[0] = pEntry->refIdx[1];
	pEntry->refIdx[1] = refIdxBuf;
}

static
void breakPieceLinks(PieceArr *pPieceArr) {
	for (I32 i = 0; i < pPieceArr->count; ++i) {
		pPieceArr->pArr[i].pNext = NULL;
		pPieceArr->pArr[i].pEntry->pNext = NULL;
	}
}

static
void linkConnectedPieces(
	MergeSendOffArgs *pArgs,
	bool hasPreserve,
	PieceRootsArr *pPieceRoots,
	PieceArr *pPieceArr,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize
) {
	//A first pass separates pieces by connectivity only, then validates preserve edges.
	//Once that's done, a second pass is done with preserve edges
	bool *pValid = NULL;
	I32 validCount = 1; //first is reserved and is always false
	I32 validSize = 0;
	I32 i = 0;
	do {
		if (areNonListedPiecesLinked(pPieceArr)) {
			STUC_ASSERT("Linked pieces here will cause an infinite corner", false);
			return;
		}
		joinAdjIntoPieceArr(
			pArgs,
			hasPreserve,
			pPieceRoots,
			pPieceArr,
			pEdgeTable,
			edgeTableSize,
			&pValid,
			&validCount,
			&validSize,
			i
		);
		STUC_ASSERT("", i >= 0 && i < 2);
		if (hasPreserve && !i) {
			setValidPreserveAsSeam(pEdgeTable, edgeTableSize, pValid, validCount);
			pArgs->pBasic->pCtx->alloc.pFree(pValid);
			for (I32 j = 0; j < pPieceArr->count; ++j) {
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
void splitIntoPieces(
	MergeSendOffArgs *pArgs,
	PieceRootsArr *pPieceRoots,
	BorderFace *pEntry,
	SharedEdgeWrap **ppSharedEdges,
	I32 *pEdgeTableSize,
	PieceArr *pPieceArr,
	I32 *pTotalVerts
) {
	//CLOCK_INIT;
	//CLOCK_START;
	*pEdgeTableSize = 0;
	I32 entryCount = pPieceArr->count;
	if (entryCount > 1) {
		*pEdgeTableSize = entryCount;
		*ppSharedEdges =
			pArgs->pBasic->pCtx->alloc.pCalloc(*pEdgeTableSize, sizeof(SharedEdgeWrap));
	}
	STUC_ASSERT("", entryCount > 0);
	Piece *pEntries = pArgs->pBasic->pCtx->alloc.pCalloc(entryCount, sizeof(Piece));
	pPieceRoots->pArr = pArgs->pBasic->pCtx->alloc.pMalloc(sizeof(I32) * entryCount);
	pPieceArr->pArr = pEntries;
	pPieceArr->count = entryCount;
	I32 entryIdx = 0;
	bool hasPreserve = false;
	//CLOCK_START;
	do {
		//If there's only 1 border face entry, then this function will just
		//initialize the Piece.
		addEntryToSharedEdgeTable(
			pArgs,
			pEntry,
			*ppSharedEdges,
			pEntries,
			*pEdgeTableSize,
			entryIdx,
			pTotalVerts,
			&hasPreserve
		);
		STUC_ASSERT("", entryIdx < entryCount);
		entryIdx++;
		BorderFace *pNextEntry = pEntry->pNext;
		pEntry->pNext = NULL;
		pEntry = pNextEntry;
	} while(pEntry);
	STUC_ASSERT("", entryIdx == entryCount);
	if (false) {
		dumpBoundsFaceToFile(pArgs, pPieceArr);
	}
	//CLOCK_STOP_NO_PRINT;
	////pTimeSpent[2] += CLOCK_TIME_DIFF(start, stop);
	//CLOCK_START;
	if (entryCount == 1) {
		pPieceRoots->pArr[0] = 0;
		pPieceRoots->count = 1;
	}
	else {
		//now link together connected entries.
		linkConnectedPieces(
			pArgs,
			hasPreserve,
			pPieceRoots,
			pPieceArr,
			*ppSharedEdges,
			*pEdgeTableSize
		);
	}
	for (I32 i = 0; i < entryCount; ++i) {
		SharedEdgeWrap* pBucket = *ppSharedEdges + i;
		//correctEdgeDir(pArgs, pPieceArr, pBucket);
		STUC_ASSERT("", i < entryCount);
	}
	STUC_ASSERT("", pPieceRoots->count >= 0 && pPieceRoots->count < 10000);
}

static
void compileEntryInfo(BorderFace *pEntry, I32 *pCount) {
	*pCount = 0;
	while (pEntry) {
		++*pCount;
		pEntry = pEntry->pNext;
	}
}

static
void destroySharedEdgeTable(
	const StucAlloc *pAlloc,
	SharedEdgeWrap *pSharedEdges,
	I32 tableSize
) {
	for (I32 i = 0; i < tableSize; ++i) {
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
void markPreserveIfKeepInVert(MergeSendOffArgs *pArgs, Piece* pPiece, I32 k) {
	bool keepInVert = false;
	BorderInInfo inInfo = stucGetBorderEntryInInfo(pArgs->pBasic, pPiece->pEntry, k);
	bool onInVert = stucGetIfOnInVert(pPiece->pEntry, k);
	if (onInVert) {
		keepInVert = stucIdxBitArray(pArgs->pInVertKeep, inInfo.vert, 1);
		if (keepInVert) {
			pPiece->keepPreserve |= true << k;
		}
	}
}

static
void markKeepInVertsPreserve(MergeSendOffArgs *pArgs, Piece* pPiece) {
	do {
		for (I32 i = 0; i < pPiece->bufFace.size; ++i) {
			if (stucGetIfStuc(pPiece->pEntry, i)) {
				continue;
			}
			markPreserveIfKeepInVert(pArgs, pPiece, i);
		}
		pPiece = pPiece->pNext;
	} while (pPiece);
}

static
I32 getPieceCount(Piece* pPiece) {
	I32 count = 0;
	do {
		count++;
		pPiece = pPiece->pNext;
	} while(pPiece);
	return count;
}

static
void sortCorners(
	MergeSendOffArgs *pArgs,
	Piece *pPiece,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	I32 *pCount
) {
	bool single = false;
	if (!pPiece->pNext) {
		single = true;
	}
	Piece* pPieceRoot = pPiece;
	I32 corner = 0;
	if (!single) {
		// get starting corner
		// This is done to ensure we don't start inside the face
		corner = getStartingCorner(
			&pPiece,
			pArgs,
			pPiece,
			pEdgeTable,
			edgeTableSize
		);
	}
	STUC_ASSERT("No valid starting corner found", corner >= 0);
	bool adj = false;
	I32 sort = 1;
	Piece *pOtherPiece = NULL;
	if (!single) {
		pOtherPiece = getNeighbourEntry(
			pArgs,
			pEdgeTable,
			edgeTableSize,
			pPiece,
			pPieceRoot,
			&corner,
			NULL
		);
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
		if (stucGetIfStuc(pPiece->pEntry, corner) &&
		    !stucGetIfOnLine(pPiece->pEntry, corner)) {
			pPiece->add |= true << corner;
			pPiece->pOrder[corner] = sort;
			sort++;
			corner++;
			adj = false;
			continue;
		}
		I32 otherCorner = corner;
		pOtherPiece = NULL;
		if (!single) {
			pOtherPiece = getNeighbourEntry(
				pArgs,
				pEdgeTable,
				edgeTableSize,
				pPiece,
				pPieceRoot,
				&otherCorner,
				NULL
			);
		}
		if (!pOtherPiece) {
			if (!adj ||
				stucGetIfOnLine(pPiece->pEntry, corner)) {
				pPiece->add |= true << corner;
				pPiece->pOrder[corner] = sort;
				//set keep preserve to false if true
				UBitField16 mask = -0x1 ^ (0x1 << corner);
				pPiece->keepPreserve &= mask;
				sort++;
			}
			else {
				pPiece->pOrder[corner] = 1;
			}
			adj = false;
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
void getPieceInFaces(
	const StucAlloc *pAlloc,
	I32 **ppInFaces,
	Piece *pPiece,
	I32 pieceCount,
	SendOffArgs *pJobArgs
) {
	*ppInFaces = pAlloc->pCalloc(pieceCount, sizeof(I32));
	I32 i = 0;
	do {
		I32 offset = pJobArgs[pPiece->pEntry->job].inFaceOffset;
		(*ppInFaces)[i] = pPiece->pEntry->inFace + offset;
		pPiece = pPiece->pNext;
		i++;
	} while(pPiece);
}

static
void initVertTableEntry(
	MergeSendOffArgs *pArgs,
	BorderVert *pVertEntry,
	BorderFace *pEntry,
	BufMesh *pBufMesh,
	I32 mapEdge,
	I32 *pVert,
	BorderInInfo *pInInfo,
	I32 mapFace,
	I32 corner,
	V2_I16 tile
) {
	bool realloced = false;
	I32 outVert = stucMeshAddVert(&pArgs->pBasic->pCtx->alloc, &pArgs->pBasic->outMesh, &realloced);
	stucCopyAllAttribs(
		&pArgs->pBasic->outMesh.core.vertAttribs,
		outVert,
		&pBufMesh->mesh.core.vertAttribs,
		*pVert
	);
	*pVert = outVert;
	pVertEntry->vert = outVert;
	pVertEntry->tile = tile;
	pVertEntry->mapEdge = mapEdge;
	pVertEntry->corners = 1;
	pVertEntry->inEdge = pInInfo->edge;
	pVertEntry->inVert = pInInfo->vert;
	pVertEntry->cornerIdx = pInInfo->vertCorner;
	pVertEntry->mapFace = mapFace;
	pVertEntry->corner = corner;
	pVertEntry->job = (I8)pEntry->job;
}

static
void initEdgeTableEntry(
	MergeSendOffArgs *pArgs,
	BorderEdge *pSeamEntry,
	BufMesh *pBufMesh,
	I32 *pEdge,
	I32 inEdge,
	I32 mapFace
) {
	StucContext pCtx = pArgs->pBasic->pCtx;
	bool realloced = false;
	I32 edgeOut = stucMeshAddEdge(&pCtx->alloc, &pArgs->pBasic->outMesh, &realloced);
	stucCopyAllAttribs(
		&pArgs->pBasic->outMesh.core.edgeAttribs,
		edgeOut,
		&pBufMesh->mesh.core.edgeAttribs,
		*pEdge
	);
	*pEdge = edgeOut;
	pSeamEntry->edge = *pEdge;
	pSeamEntry->inEdge = inEdge;
	pSeamEntry->mapFace = mapFace;
}

static
void blendMergedCornerAttribs(
	BlendConfig config,
	AttribArray *pDestArr,
	I32 iDest,
	AttribArray *pSrcArr,
	I32 iSrc,
	Attrib *pDestNormalAttrib
) {
	for (I32 i = 0; i < pDestArr->count; ++i) {
		Attrib *pDest = pDestArr->pArr + i;
		Attrib *pSrc = pSrcArr->pArr + i;
		STUC_ASSERT("", !strncmp(pDest->core.name, pSrc->core.name, STUC_ATTRIB_NAME_MAX_LEN));
		if (pDest != pDestNormalAttrib &&
			(!pDest->origin == STUC_ATTRIB_ORIGIN_MAP ||
			!pDest->interpolate)) {
			continue;
		}
		stucBlendAttribs(pDest, iDest, pDest, iDest, pSrc, iSrc, config);
	}
}

static
void divideCornerAttribsByScalar(
	AttribArray *pCornerAttribs,
	I32 corner,
	I32 scalar,
	Attrib *pNormalAttrib
) {
	for (I32 i = 0; i < pCornerAttribs->count; ++i) {
		Attrib *pAttrib = pCornerAttribs->pArr + i;
		if (pAttrib != pNormalAttrib &&
		    (!pAttrib->origin == STUC_ATTRIB_ORIGIN_MAP ||
		    !pAttrib->interpolate)) {

			continue;
		}
		stucDivideAttribByScalarInt(pAttrib, corner, scalar);
	}
}

static
void addBorderCornerAndVert(
	MergeSendOffArgs *pArgs,
	Piece *pPiece,
	Piece *pPieceRoot,
	I32 k,
	bool addToTables
) {
	StucMap pMap = pArgs->pBasic->pMap;
	BorderFace *pEntry = pPiece->pEntry;
	STUC_ASSERT("This should not be called on a map corner", !stucGetIfStuc(pEntry, k));
	BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
	I32 corner = pPiece->bufFace.start - k;
	I32 vert = stucBufMeshGetVertIdx(pPiece, pBufMesh, k);
	STUC_ASSERT("", vert > pBufMesh->mesh.vertBufSize - 1 - pBufMesh->borderVertCount);
	STUC_ASSERT("", vert < pBufMesh->mesh.vertBufSize);
#ifndef STUC_DISABLE_EDGES_IN_BUF
	I32 edge = stucBufMeshGetEdgeIdx(pPiece, pBufMesh, k);
	STUC_ASSERT("", edge > pBufMesh->mesh.edgeBufSize - 1 - pBufMesh->borderEdgeCount);
	STUC_ASSERT("", edge < pBufMesh->mesh.edgeBufSize);
#endif
	I32 mapCorner = stucGetMapCorner(pEntry, k);
	STUC_ASSERT("",
		mapCorner >= 0 && mapCorner < pMap->mesh.core.cornerCount);
	BorderInInfo inInfo = stucGetBorderEntryInInfo(pArgs->pBasic, pEntry, k);
	bool isOnInVert = stucGetIfOnInVert(pEntry, k);
	if (!isOnInVert) {
		inInfo.vert = -1;
	}
	I32 hash;
	I32 mapEdge;
	if (isOnInVert) {
		hash = stucFnvHash((U8 *)&inInfo.vert, 4, pArgs->pCTables->vertTableSize);
		mapEdge = -1;
	}
	else {
		FaceRange mapFace =
			stucGetFaceRange(&pMap->mesh.core, pEntry->mapFace, false);
		mapEdge = pMap->mesh.core.pEdges[mapFace.start + mapCorner];
		hash = stucFnvHash((U8 *)&mapEdge, 4, pArgs->pCTables->vertTableSize);
	}
	BlendConfig blendConfigAdd = {.blend = STUC_BLEND_ADD};
	BorderVert *pVertEntry = pArgs->pCTables->pVertTable + hash;
	if (!pVertEntry->corners) {
		if (addToTables) {
			initVertTableEntry(
				pArgs,
				pVertEntry,
				pEntry,
				pBufMesh,
				mapEdge,
				&vert,
				&inInfo,
				pEntry->mapFace,
				corner,
				pPiece->tile
			);
		}
		else {
			pVertEntry = NULL;
		}
	}
	else {
		do {
			//Check vert entry is valid
			STUC_ASSERT("", pVertEntry->mapEdge >= -1);
			STUC_ASSERT("", pVertEntry->mapEdge < pMap->mesh.core.edgeCount);
			STUC_ASSERT("", pVertEntry->mapFace >= 0);
			STUC_ASSERT("", pVertEntry->mapFace < pMap->mesh.core.faceCount);
			bool match;
			if (isOnInVert) {
				V2_F32 *pMeshInUvA = pArgs->pBasic->pInMesh->pUvs + pVertEntry->cornerIdx;
				V2_F32 *pMeshInUvB = pArgs->pBasic->pInMesh->pUvs + inInfo.vertCorner;
				match = pVertEntry->inVert == inInfo.vert &&
				        pVertEntry->mapFace == pEntry->mapFace &&
				        pMeshInUvA->d[0] == pMeshInUvB->d[0] &&
				        pMeshInUvA->d[1] == pMeshInUvB->d[1];
			}
			else {
				BufMesh *pOtherBufMesh = &pArgs->pJobArgs[pVertEntry->job].bufMesh;
				bool connected = 
					_(pBufMesh->mesh.pUvs[corner] V2APROXEQL
					pOtherBufMesh->mesh.pUvs[pVertEntry->corner]
					);
				match =  pVertEntry->mapEdge == mapEdge &&
				         pVertEntry->tile.d[0] == pPiece->tile.d[0] &&
				         pVertEntry->tile.d[1] == pPiece->tile.d[1] &&
				         pVertEntry->inEdge == inInfo.edge &&
				         connected;
			}
			if (match) {
				//If corner isOnInVert,
				//then entry must also be an isOnInVert entry.
				//And if not, then entry must also not be
				STUC_ASSERT("",
					(isOnInVert && pVertEntry->inVert != -1) ||
					(!isOnInVert && pVertEntry->inVert == -1));
				vert = pVertEntry->vert;
				pVertEntry->corners++;
				if (isOnInVert) {
					BufMesh *pOtherBufMesh = &pArgs->pJobArgs[pVertEntry->job].bufMesh;
					blendMergedCornerAttribs(
						blendConfigAdd,
						&pOtherBufMesh->mesh.core.cornerAttribs,
						pVertEntry->corner,
						&pBufMesh->mesh.core.cornerAttribs,
						corner,
						pOtherBufMesh->mesh.pNormalAttrib
					);
				}
				break;
			}
			if (!pVertEntry->pNext && addToTables) {
				pVertEntry = pVertEntry->pNext =
					pArgs->pBasic->pCtx->alloc.pCalloc(1, sizeof(BorderVert));
				initVertTableEntry(
					pArgs,
					pVertEntry,
					pEntry,
					pBufMesh,
					mapEdge,
					&vert,
					&inInfo,
					pEntry->mapFace,
					corner,
					pPiece->tile
				);
				break;
			}
			pVertEntry = pVertEntry->pNext;
		} while(pVertEntry);
	}
	//TODO debug/ verify border edge implementation is working correctly
	//TODO why cant you just determine connected edges with the above corner table?
	//     is a separate table really needed? If you know 2 corners are connected,
	//     can't you then connect their edges?
#ifndef STUC_DISABLE_EDGES_IN_BUF
	U32 valueToHash = inInfo.edge + pEntry->mapFace;
	hash = stucFnvHash((U8 *)&valueToHash, 4, pArgs->pCTables->edgeTableSize);
	BorderEdge *pEdgeEntry = pArgs->pCTables->pEdgeTable + hash;
	if (!pEdgeEntry->valid) {
		if (addToTables) {
			initEdgeTableEntry(
				pArgs,
				pEdgeEntry,
				pBufMesh,
				&edge,
				inInfo.edge,
				pEntry->mapFace
			);
		}
		else {
			pEdgeEntry = NULL;
		}
	}
	else {
		do {
			if (pEdgeEntry->inEdge == inInfo.edge &&
				pEdgeEntry->mapFace == pEntry->mapFace) {
				edge = pEdgeEntry->edge;
				break;
			}
			if (!pEdgeEntry->pNext && addToTables) {
				pEdgeEntry = pEdgeEntry->pNext =
					pArgs->pBasic->pCtx->alloc.pCalloc(1, sizeof(BorderEdge));
				initEdgeTableEntry(
					pArgs,
					pEdgeEntry,
					pBufMesh,
					&edge,
					inInfo.edge,
					pEntry->mapFace
				);
				break;
			}
			pEdgeEntry = pEdgeEntry->pNext;
		} while(pEdgeEntry);
	}
#endif
	if (pVertEntry) {
		pBufMesh->mesh.core.pCorners[corner] = vert;
		pBufMesh->mesh.core.pEdges[corner] = 0;
	}
	else {
		//set add to false
		UBitField16 mask = -0x1 ^ (0x1 << k);
		pPiece->add &= mask;
		//correct sort, as this corner will not be kept
		I32 sort = pPiece->pOrder[k];
		pPiece = pPieceRoot;
		do {
			for (I32 i = 0; i < pPiece->bufFace.size; ++i) {
				if (pPiece->pOrder[i] > sort) {
					pPiece->pOrder[i]--;
				}
			}
			pPiece = pPiece->pNext;
		} while(pPiece);
	}
}

static
void mergeAttribsForSingleCorner(MergeSendOffArgs *pArgs, Piece *pPiece, I32 k) {
	StucMap pMap = pArgs->pBasic->pMap;
	BorderFace *pEntry = pPiece->pEntry;
	STUC_ASSERT("This should only be called on onInVert corners",
		stucGetIfOnInVert(pEntry, k)
	);
	I32 mapCorner = stucGetMapCorner(pEntry, k);
	STUC_ASSERT("", mapCorner >= 0 && mapCorner < pMap->mesh.core.cornerCount);
	BorderInInfo inInfo = stucGetBorderEntryInInfo(pArgs->pBasic, pEntry, k);
	I32 hash;
	hash = stucFnvHash((U8 *)&inInfo.vert, 4, pArgs->pCTables->vertTableSize);
	BorderVert *pVertEntry = pArgs->pCTables->pVertTable + hash;
	STUC_ASSERT("No entry was initialized for this corner", pVertEntry->corners);

	BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
	I32 corner = pPiece->bufFace.start - k;

	BlendConfig blendConfigReplace = {.blend = STUC_BLEND_REPLACE};
	do {
		STUC_ASSERT("", pVertEntry->mapFace >= 0);
		STUC_ASSERT("", pVertEntry->mapFace < pMap->mesh.core.faceCount);
		bool match;
		V2_F32 *pMeshInUvA = pArgs->pBasic->pInMesh->pUvs + pVertEntry->cornerIdx;
		V2_F32 *pMeshInUvB = pArgs->pBasic->pInMesh->pUvs + inInfo.vertCorner;
		match = pVertEntry->inVert == inInfo.vert &&
		        pVertEntry->mapFace == pEntry->mapFace &&
		        pMeshInUvA->d[0] == pMeshInUvB->d[0] &&
		        pMeshInUvA->d[1] == pMeshInUvB->d[1];
		if (match) {
			STUC_ASSERT("Entry is not onInVert", pVertEntry->inVert != -1);
			BufMesh *pOtherBufMesh = &pArgs->pJobArgs[pVertEntry->job].bufMesh;
			if (!pVertEntry->divided) {

				divideCornerAttribsByScalar(
					&pOtherBufMesh->mesh.core.cornerAttribs,
					pVertEntry->corner,
					pVertEntry->corners,
					pOtherBufMesh->mesh.pNormalAttrib
				);
				pVertEntry->divided = true;
			}
			blendMergedCornerAttribs(
				blendConfigReplace,
				&pBufMesh->mesh.core.cornerAttribs,
				corner,
				&pOtherBufMesh->mesh.core.cornerAttribs,
				pVertEntry->corner,
				pBufMesh->mesh.pNormalAttrib
			);
			return;
		}
		pVertEntry = pVertEntry->pNext;
	} while(pVertEntry);
	STUC_ASSERT("No entry was initialized for this corner", pVertEntry);
}

static
void addToOutMesh(MergeSendOffArgs *pArgs) {
	CLOCK_INIT;
	U64 timeSpent[7] = {0};
	StucContext pCtx = pArgs->pBasic->pCtx;
	const StucAlloc *pAlloc = &pCtx->alloc;
	StucMap pMap = pArgs->pBasic->pMap;
	I32 count = pArgs->entriesEnd - pArgs->entriesStart;
	MergeBufHandles mergeBufHandles = {0};
	stucAllocMergeBufs(pArgs->pBasic->pCtx, &mergeBufHandles, pArgs->totalVerts);
	for (I32 i = 0; i < count; ++i) {
		I32 reali = pArgs->entriesStart + i;
		CLOCK_START;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		FaceRange mapFace =
			stucGetFaceRange(&pMap->mesh.core, pPieceArr->pArr[0].pEntry->mapFace, false);
		for (I32 j = 0; j < pPieceRoots->count; ++j) {
			Piece *pPieceRoot = pPieceArr->pArr + pPieceRoots->pArr[j];
			I32 *pInFaces = NULL;
			I32 pieceCount = 0; //this is only need if getting in faces
			if (pArgs->pBasic->ppInFaceTable) {
				pieceCount = getPieceCount(pPieceRoot); 
				getPieceInFaces(
					&pArgs->pBasic->pCtx->alloc,
					&pInFaces,
					pPieceRoot,
					pieceCount,
					pArgs->pJobArgs
				);
			}
			I32 job = pPieceArr->pArr[pPieceRoots->pArr[j]].pEntry->job;
			STUC_ASSERT("", job >= 0 && job < pCtx->threadCount);
			stucMergeSingleBorderFace(
				pArgs,
				timeSpent,
				pPieceRoots->pArr[j],
				pPieceArr,
				&mapFace,
				&mergeBufHandles,
				pInFaces,
				pieceCount
			);
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
		}
		CLOCK_STOP_NO_PRINT;
		timeSpent[6] += CLOCK_TIME_DIFF(start, stop);
		STUC_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
	stucDestroyMergeBufs(pArgs->pBasic->pCtx, &mergeBufHandles);
	pAlloc->pFree(pArgs->pPieceArrTable);
	pAlloc->pFree(pArgs->pPieceRootTable);
	pAlloc->pFree(pArgs->pTotalVertTable);
	printf("Combine time breakdown: \n");
	for(I32 i = 0; i < 7; ++i) {
#ifdef WIN32
		printf("	%llu\n", timeSpent[i]);
#else
		printf("	%lu\n", timeSpent[i]);
#endif
	}
	printf("\n");
}

static
void addPieceBorderCornersAndVerts(
	MergeSendOffArgs *pArgs,
	PieceArr *pPieceArr,
	PieceRootsArr *pPieceRoots,
	bool preserve,
	I32 pieceIdx
) {
	Piece *pPiece = pPieceArr->pArr + pPieceRoots->pArr[pieceIdx];
	Piece *pPieceRoot = pPiece;
	do {
		for (I32 k = 0; k < pPiece->bufFace.size; ++k) {
			if (stucGetIfStuc(pPiece->pEntry, k)) {
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
					pPiece->pOrder[k] > 0
				);
				addBorderCornerAndVert(pArgs, pPiece, pPieceRoot, k, !preserve);
			}
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void mergeIntersectionCorners(MergeSendOffArgs *pArgs, bool preserve) {
	I32 count = pArgs->entriesEnd - pArgs->entriesStart;
	for (I32 i = 0; i < count; ++i) {
		I32 reali = pArgs->entriesStart + i;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		for (I32 j = 0; j < pPieceRoots->count; ++j) {
			addPieceBorderCornersAndVerts(pArgs, pPieceArr, pPieceRoots, preserve, j);
		}
		STUC_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
}

static
void mergePieceCornerAttribs(
	MergeSendOffArgs *pArgs,
	PieceArr *pPieceArr,
	PieceRootsArr *pPieceRoots,
	I32 pieceIdx
) {
	Piece *pPiece = pPieceArr->pArr + pPieceRoots->pArr[pieceIdx];
	do {
		for (I32 k = 0; k < pPiece->bufFace.size; ++k) {
			if ((pPiece->add >> k & 0x1) &&
			    !stucGetIfStuc(pPiece->pEntry, k) &&
			    stucGetIfOnInVert(pPiece->pEntry, k)) {

				mergeAttribsForSingleCorner(pArgs, pPiece, k);
			}
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void mergeCornerAttribs(MergeSendOffArgs *pArgs) {
	I32 count = pArgs->entriesEnd - pArgs->entriesStart;
	for (I32 i = 0; i < count; ++i) {
		I32 reali = pArgs->entriesStart + i;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		for (I32 j = 0; j < pPieceRoots->count; ++j) {
			mergePieceCornerAttribs(pArgs, pPieceArr, pPieceRoots, j);
		}
		STUC_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
}

static
void transformVertsInUsg(
	MergeSendOffArgs *pArgs,
	BufMesh *pBufMesh,
	I32 corner,
	FaceRange *pMapFace,
	BorderFace *pEntry,
	V3_F32 *pPos,
	V3_F32 *pPosFlat,
	V3_F32 *pNormal,
	bool *pNormalTransformed,
	Mat3x3 *pTbn,
	F32 w,
	V2_F32 fTileMin
) {
	V3_F32 uvw = {0};
	*(V2_F32 *)&uvw = _(pBufMesh->mesh.pUvs[corner] V2SUB fTileMin);
	uvw.d[2] = pBufMesh->pW[corner];
	StucMap pMap = pArgs->pBasic->pMap;
	V3_F32 usgBc = {0};
	bool transformed = false;
	for (I32 i = 0; i < pMapFace->size; ++i) {
		I32 mapVert = pMap->mesh.core.pCorners[pMapFace->start + i];
		if (!pMap->mesh.pUsg) {
			continue;
		}
		I32 usgIdx = pMap->mesh.pUsg[mapVert];
		if (!usgIdx) {
			continue;
		}
		usgIdx = abs(usgIdx) - 1;
		Usg *pUsg = pMap->usgArr.pArr + usgIdx;
		if (stucIsPointInsideMesh(&pArgs->pBasic->pCtx->alloc, uvw, pUsg->pMesh)) {
			bool flatCutoff = pUsg->pFlatCutoff &&
				stucIsPointInsideMesh(&pArgs->pBasic->pCtx->alloc, uvw, pUsg->pFlatCutoff);
			I32 inFaceOffset = pArgs->pJobArgs[pEntry->job].inFaceOffset;
			bool inside = stucSampleUsg(
				i,
				uvw,
				pPosFlat,
				&transformed,
				&usgBc,
				pMapFace,
				pMap,
				pEntry->inFace + inFaceOffset,
				pArgs->pBasic->pInMesh,
				pNormal,
				fTileMin,
				flatCutoff,
				true,
				pTbn
			);
			if (inside) {
				*pPos = _(*pPosFlat V3ADD _(*pNormal V3MULS w * pArgs->pBasic->wScale));
				if (transformed) {
					*pNormalTransformed = true;
					*pNormal = _(pBufMesh->mesh.pNormals[corner] V3MULM3X3 pTbn);
				}
				break;
			}
		}
	}
}

static
void transformDeferredVert(
	MergeSendOffArgs *pArgs,
	Piece *pPiece,
	BufMesh *pBufMesh,
	FaceRange *pMapFace,
	I32 cornerLocal,
	V2_I16 tile
) {
	BorderFace *pEntry = pPiece->pEntry;
	I32 corner = pPiece->bufFace.start - cornerLocal;
	I32 vert = stucBufMeshGetVertIdx(pPiece, pBufMesh, cornerLocal);
	V3_F32 posFlat = pBufMesh->mesh.pVerts[vert];
	F32 w = pBufMesh->pW[corner];
	V3_F32 projNormal = pBufMesh->pInNormal[corner];
	V3_F32 inTangent = pBufMesh->pInTangent[corner];
	F32 inTSign = pBufMesh->pInTSign[corner];
	Mat3x3 tbn = {0};
	*(V3_F32 *)&tbn.d[0] = inTangent;
	*(V3_F32 *)&tbn.d[1] = _(_(projNormal V3CROSS inTangent) V3MULS inTSign);
	*(V3_F32 *)&tbn.d[2] = projNormal;
	V3_F32 pos = _(posFlat V3ADD _(projNormal V3MULS w * pArgs->pBasic->wScale));
	V3_F32 normal = {0};
	V2_F32 fTileMin = {(F32)tile.d[0], (F32)tile.d[1]};
	bool normalTransformed = false;
	if (!stucGetIfOnInVert(pEntry, cornerLocal) && !pArgs->pBasic->ppInFaceTable) {
		transformVertsInUsg(
			pArgs,
			pBufMesh,
			corner,
			pMapFace,
			pEntry,
			&pos,
			&posFlat,
			&normal,
			&normalTransformed,
			&tbn,
			w,
			fTileMin
		);
	}
	if (!normalTransformed) {
		normal = _(pBufMesh->mesh.pNormals[corner] V3MULM3X3 &tbn);
	}
	pBufMesh->mesh.pVerts[vert] = pos;
	pBufMesh->mesh.pNormals[corner] = normal;
}

static
void transformDefferedCorners(
	MergeSendOffArgs *pArgs,
	FaceRange *pMapFace,
	Piece *pPiece
) {
	do {
		BufMesh *pBufMesh = &pArgs->pJobArgs[pPiece->pEntry->job].bufMesh;
		for (I32 i = 0; i < pPiece->bufFace.size; ++i) {
			if (stucGetIfStuc(pPiece->pEntry, i) || !(pPiece->add >> i & 0x1)) {
				continue;
			}
			transformDeferredVert(
				pArgs,
				pPiece,
				pBufMesh,
				pMapFace,
				i,
				pPiece->tile
			);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void invertWind(Piece *pPiece, I32 count) {
	count++;
	do {
		for (I32 i = 0; i < pPiece->bufFace.size; ++i) {
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
	StucContext pCtx = pArgs->pBasic->pCtx;
	const StucAlloc *pAlloc = &pCtx->alloc;
	U64 timeSpent[7] = {0};
	I32 count = pArgs->entriesEnd - pArgs->entriesStart;
	pArgs->pPieceArrTable = pAlloc->pCalloc(count, sizeof(PieceArr));
	pArgs->pPieceRootTable = pAlloc->pCalloc(count, sizeof(PieceRootsArr));
	pArgs->pTotalVertTable = pAlloc->pCalloc(count, sizeof(I32));
	pArgs->pInVertKeep = pAlloc->pCalloc(pArgs->pBasic->pInMesh->core.vertCount, 1);
	for (I32 i = 0; i < count; ++i) {
		I32 reali = pArgs->entriesStart + i;
		CLOCK_START;
		BorderFace *pEntry = pArgs->pBorderTable->ppTable[reali];
		I32 entryCount = 0;
		compileEntryInfo(pEntry, &entryCount);
		STUC_ASSERT("", entryCount);
		//I32 seamFace = ;
		FaceRange mapFace =
			stucGetFaceRange(&pArgs->pBasic->pMap->mesh.core, pEntry->mapFace, false);
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		pPieceRoots->count = 0;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		pPieceArr->count = entryCount;
		pPieceArr->pArr = NULL;
		SharedEdgeWrap *pSharedEdges = NULL;
		I32 edgeTableSize = 0;
		CLOCK_STOP_NO_PRINT;
		timeSpent[0] += CLOCK_TIME_DIFF(start, stop);
		CLOCK_START;
		I32 totalVerts = 0;
		splitIntoPieces(
			pArgs,
			pPieceRoots,
			pEntry,
			&pSharedEdges,
			&edgeTableSize,
			pPieceArr,
			&totalVerts
		);
		STUC_ASSERT("", pPieceRoots->count > 0);
		I32 aproxVertsPerPiece = totalVerts / pPieceRoots->count;
		STUC_ASSERT("", aproxVertsPerPiece != 0);
		for (I32 j = 0; j < pPieceRoots->count; ++j) {
			Piece *pPiece = pPieceArr->pArr + pPieceRoots->pArr[j];
			STUC_ASSERT("", pPiece->pEntry);
			markKeepInVertsPreserve(pArgs, pPiece);
			sortCorners(
				pArgs,
				pPiece,
				pSharedEdges,
				edgeTableSize,
				&totalVerts
			);
			if (!pPiece->pEntry->inOrient) {
				invertWind(pPiece, totalVerts);
			}
			transformDefferedCorners(pArgs, &mapFace, pPiece);
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
			if (false) {
				dumpBoundsFaceToFile(pArgs, pPieceArr);
			}
		}
		if (pSharedEdges) {
			destroySharedEdgeTable(
				&pArgs->pBasic->pCtx->alloc,
				pSharedEdges,
				edgeTableSize
			);
		}
	}
	CLOCK_STOP_NO_PRINT;
	timeSpent[1] += CLOCK_TIME_DIFF(start, stop);
}

static
StucResult makePiecesJob(void *pArgsVoid) {
	Result err = STUC_SUCCESS;
	MergeSendOffArgs *pArgs = pArgsVoid;
	createAndJoinPieces(pArgs);
	return err;
}

static
StucResult mergeAndAddToOutMesh(
	StucContext pCtx,
	int32_t jobCount,
	MergeSendOffArgs *pArgArr
) {
	Result err = STUC_SUCCESS;
	for (I32 i = 0; i < jobCount; ++i) {
		mergeIntersectionCorners(pArgArr + i, false);
		mergeIntersectionCorners(pArgArr + i, true);
		mergeCornerAttribs(pArgArr + i);
	}
	for (I32 i = 0; i < jobCount; ++i) {
		addToOutMesh(pArgArr + i);
		pCtx->alloc.pFree(pArgArr[i].pInVertKeep);
	}
	return err;
}

static
void linkOtherEntry(BorderBucket *pBucketOther, BorderBucket *pBucket, I32 mapFace) {
	if (mapFace == pBucketOther->pEntry->mapFace) {
		BorderFace *pEntry = pBucket->pEntry;
		while (pEntry->pNext) {
			pEntry = pEntry->pNext;
		}
		pEntry->pNext = pBucketOther->pEntry;
		pBucketOther->pEntry = NULL;
	}
}

static
void linkEntriesFromOtherJobs(
	SendOffArgs *pJobArgs,
	BorderBucket *pBucket,
	I32 mapFace,
	I32 hash,
	I32 job,
	I32 mapJobsSent
) {
	for (I32 j = job + 1; j < mapJobsSent; ++j) {
		if (!pJobArgs[j].bufSize) {
			continue;
		}
		//STUC_ASSERT("", pJobArgs[j].borderTable.size > 0);
		//STUC_ASSERT("", pJobArgs[j].borderTable.pTable != NULL);
		BorderBucket *pBucketOther = pJobArgs[j].borderTable.pTable + hash;
		//STUC_ASSERT("", pBucketOther != NULL);
		do {
			if (pBucketOther->pEntry) {
				linkOtherEntry(pBucketOther, pBucket, mapFace);
			}
			pBucketOther = pBucketOther->pNext;
		} while (pBucketOther);
	}
}

static
void walkBucketsAndLink(
	StucContext pCtx,
	SendOffArgs *pJobArgs,
	CompiledBorderTable *pBorderTable,
	I32 totalBorderFaces,
	I32 mapJobsSent,
	I32 jobIdx,
	I32 hash
) {
	STUC_ASSERT("", pJobArgs[jobIdx].borderTable.size > 0);
	STUC_ASSERT("", pJobArgs[jobIdx].borderTable.pTable);
	BorderBucket *pBucket = pJobArgs[jobIdx].borderTable.pTable + hash;
	I32 depth = 0;
	do {
		if (pBucket->pEntry) {
			I32 mapFace = pBucket->pEntry->mapFace;
			STUC_ASSERT("", mapFace >= 0);
			linkEntriesFromOtherJobs(
				pJobArgs,
				pBucket,
				mapFace,
				hash,
				jobIdx,
				mapJobsSent
			);
			STUC_ASSERT("", pBorderTable->count >= 0);
			STUC_ASSERT("", pBorderTable->count < totalBorderFaces);
			pBorderTable->ppTable[pBorderTable->count] = pBucket->pEntry;
			pBorderTable->count++;
		}
		BorderBucket *pNextBucket = pBucket->pNext;
		if (depth != 0) {
			pCtx->alloc.pFree(pBucket);
		}
		pBucket = pNextBucket;
		depth++;
	} while (pBucket);
}

static
void compileBorderTables(
	StucContext pCtx,
	SendOffArgs *pJobArgs,
	CompiledBorderTable *pBorderTable,
	I32 totalBorderFaces,
	I32 mapJobsSent
) {
	pBorderTable->ppTable = pCtx->alloc.pMalloc(sizeof(void *) * totalBorderFaces);
	for (I32 i = 0; i < mapJobsSent; ++i) {
		if (!pJobArgs[i].bufSize) {
			//TODO why is bufsize zero? how? find out
			continue; //skip if buf mesh is empty
		}
		for (I32 hash = 0; hash < pJobArgs[i].borderTable.size; ++hash) {
			walkBucketsAndLink(
				pCtx,
				pJobArgs,
				pBorderTable,
				totalBorderFaces,
				mapJobsSent,
				i,
				hash
			);
			STUC_ASSERT("", hash >= 0 && hash < pJobArgs[i].borderTable.size);
		}
		STUC_ASSERT("", i >= 0 && i < mapJobsSent);
	}
}

static
void allocCombineTables(
	const StucAlloc *pAlloc,
	CombineTables *pCTables,
	I32 totalBorderFaces,
	I32 totalBorderEdges
) {
	pCTables->pVertTable = pAlloc->pCalloc(totalBorderFaces, sizeof(BorderVert));
	pCTables->pOnLineTable = pAlloc->pCalloc(totalBorderFaces, sizeof(OnLine));
	pCTables->pEdgeTable = pAlloc->pCalloc(totalBorderEdges, sizeof(BorderEdge));
	pCTables->vertTableSize = totalBorderFaces;
	pCTables->onLineTableSize = totalBorderFaces;
	pCTables->edgeTableSize = totalBorderEdges;
}

static
void destroyCombineTables(const StucAlloc *pAlloc, CombineTables *pCTables) {
	for (I32 i = 0; i < pCTables->vertTableSize; ++i) {
		BorderVert *pEntry = pCTables->pVertTable[i].pNext;
		while (pEntry) {
			BorderVert *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	pAlloc->pFree(pCTables->pVertTable);
	for (I32 i = 0; i < pCTables->onLineTableSize; ++i) {
		OnLine *pEntry = pCTables->pOnLineTable[i].pNext;
		while (pEntry) {
			OnLine *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	pAlloc->pFree(pCTables->pOnLineTable);
	for (I32 i = 0; i < pCTables->edgeTableSize; ++i) {
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
void sendOffMergeJobs(
	MapToMeshBasic *pBasic,
	CompiledBorderTable *pBorderTable,
	I32 *pJobCount,
	void ***pppJobHandles,
	MergeSendOffArgs **ppMergeJobArgs,
	SendOffArgs *pMapJobArgs,
	CombineTables *pCTables,
	JobBases *pJobBases
) {
	*pJobCount = MAX_SUB_MAPPING_JOBS;
	*pJobCount += *pJobCount == 0;
	I32 entriesPerJob = pBorderTable->count / *pJobCount;
	bool singleThread = !entriesPerJob;
	void *jobArgPtrs[MAX_THREADS] = {0};
	*pJobCount = singleThread ? 1 : *pJobCount;
	*ppMergeJobArgs = pBasic->pCtx->alloc.pCalloc(*pJobCount, sizeof(MergeSendOffArgs));
	for (I32 i = 0; i < *pJobCount; ++i) {
		I32 entriesStart = entriesPerJob * i;
		I32 entriesEnd = i == *pJobCount - 1 ?
			pBorderTable->count : entriesStart + entriesPerJob;
		//TODO make a struct for these common variables, like pCtx,
		//pMap, pEdgeVerts, etc, so you don't need to move them
		//around manually like this.
		(*ppMergeJobArgs)[i].pBasic = pBasic;
		(*ppMergeJobArgs)[i].pBorderTable = pBorderTable;
		(*ppMergeJobArgs)[i].entriesStart = entriesStart;
		(*ppMergeJobArgs)[i].entriesEnd = entriesEnd;
		(*ppMergeJobArgs)[i].pJobArgs = pMapJobArgs;
		(*ppMergeJobArgs)[i].pJobBases = pJobBases;
		(*ppMergeJobArgs)[i].pCTables = pCTables;
		(*ppMergeJobArgs)[i].job = i;
		(*ppMergeJobArgs)[i].totalVerts = 4;
		(*ppMergeJobArgs)[i].jobCount = *pJobCount;
		jobArgPtrs[i] = *ppMergeJobArgs + i;
	}
	*pppJobHandles = pBasic->pCtx->alloc.pCalloc(*pJobCount, sizeof(void *));
	pBasic->pCtx->threadPool.pJobStackPushJobs(
		pBasic->pCtx->pThreadPoolHandle,
		*pJobCount,
		*pppJobHandles,
		makePiecesJob,
		jobArgPtrs
	);
}

Result stucMergeBorderFaces(
	MapToMeshBasic *pBasic,
	SendOffArgs *pJobArgs,
	JobBases *pJobBases,
	I32 mapJobsSent
) {
	StucResult err = STUC_SUCCESS;
	StucContext pCtx = pBasic->pCtx;
	I32 totalBorderFaces = 0;
	I32 totalBorderEdges = 0;
	for (I32 i = 0; i < mapJobsSent; ++i) {
		totalBorderFaces += pJobArgs[i].bufMesh.borderFaceCount;
		totalBorderEdges += pJobArgs[i].bufMesh.borderEdgeCount;
		STUC_ASSERT("", i < mapJobsSent);
	}
	STUC_ASSERT("", totalBorderFaces >= 0 && totalBorderFaces < 100000000);
	STUC_ASSERT("", totalBorderEdges >= 0 && totalBorderEdges < 100000000);
	CompiledBorderTable borderTable = {0};
	//compile border table entries from all jobs, into a single table
	compileBorderTables(
		pBasic->pCtx,
		pJobArgs,
		&borderTable,
		totalBorderFaces,
		mapJobsSent
	);
	//tables used for merging mesh mesh data correctly
	CombineTables cTables = {0};
	allocCombineTables(
		&pBasic->pCtx->alloc,
		&cTables,
		totalBorderFaces,
		totalBorderEdges
	);
	MergeSendOffArgs *pMergeJobArgs = NULL;
	I32 jobCount = 0;
	void **ppJobHandles = NULL;
	sendOffMergeJobs(
		pBasic,
		&borderTable,
		&jobCount,
		&ppJobHandles,
		&pMergeJobArgs,
		pJobArgs,
		&cTables,
		pJobBases
	);
	pCtx->threadPool.pWaitForJobs(
		pCtx->pThreadPoolHandle,
		jobCount,
		ppJobHandles,
		true,
		NULL
	);
	err = stucJobGetErrs(pCtx, jobCount, &ppJobHandles);
	mergeAndAddToOutMesh(pCtx, jobCount, pMergeJobArgs);
	err = stucJobDestroyHandles(pCtx, jobCount, &ppJobHandles);
	pCtx->alloc.pFree(ppJobHandles);
	STUC_THROW_IF(err, true, "", 0);
	STUC_CATCH(0, err, ;);
	pCtx->alloc.pFree(pMergeJobArgs);
	pCtx->alloc.pFree(borderTable.ppTable);
	destroyCombineTables(&pCtx->alloc, &cTables);
	return err;
}