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
#include <MathUtils.h>
#include <Utils.h>
#include <AttribUtils.h>
#include <ThreadPool.h>
#include <Error.h>
#include <Alloc.h>

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
	pEntry->corner[0] = (I16)i;
	pEntry->validIdx = -1;
	pEntry->segment = (I8)segment;
	pEntry->inOrient = (bool)pPiece->pEntry->inOrient;
	pEntry->tile = pPiece->tile;
}

static
Result checkIfSeamOrPreserve(
	MakePiecesJobArgs *pArgs,
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
	Result err = STUC_SUCCESS;
	Piece *pPiece = pEntries + entryIdx;
	StucMap pMap = pArgs->pBasic->pMap;
	*pSeam = pArgs->pBasic->pInMesh->pSeamEdge[pInInfo->edge];

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
			STUC_ASSERT(
				"",
				pPiece->pEntry->mapFace < pMap->pMesh->core.faceCount
			);
			I32 mapFaceStart = pMap->pMesh->core.pFaces[pPiece->pEntry->mapFace];
			STUC_ASSERT(
				"",
				mapFaceStart < pMap->pMesh->core.cornerCount
			);
			I32 mapEdge = pMap->pMesh->core.pEdges[mapFaceStart + mapCorner];
			STUC_ASSERT(
				"",
				mapEdge < pMap->pMesh->core.edgeCount
			);
			*pIsReceive = stucCheckIfEdgeIsReceive(
				pMap->pMesh,
				mapEdge,
				pArgs->pBasic->receiveLen
			);
			*pRefIdx = mapEdge;
			if (*pIsReceive) {
				pPiece->keepPreserve |= (I64)true << corner;
			}
		}
	}
	return err;
}

static
Result addToTable(
	MakePiecesJobArgs *pArgs,
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
	Result err = STUC_SUCCESS;
	I32 hash = stucFnvHash((U8 *)&pInInfo->edge, 4, tableSize);
	SharedEdgeWrap *pEdgeEntryWrap = pSharedEdges + hash;
	SharedEdge *pEdgeEntry = pEdgeEntryWrap->pEntry;
	if (!pEdgeEntry) {
		err = stucLinAlloc(pArgs->pSharedEdgeAlloc, &pEdgeEntryWrap->pEntry, 1);
		STUC_RETURN_ERR_IFNOT(err, "");
		pEdgeEntry = pEdgeEntryWrap->pEntry;
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
		return err;
	}
	do {
		STUC_ASSERT(
			"",
			//pEdgeEntry->edge is offset by 1, hence > 0 and <= end
			pEdgeEntry->edge > 0 &&
			pEdgeEntry->edge <= pArgs->pBasic->pInMesh->core.edgeCount
		);
		STUC_ASSERT("", pEdgeEntry->idx % 2 == pEdgeEntry->idx); // range 0 .. 1
		if (pEdgeEntry->edge == pInInfo->edge + 1 &&
			pEdgeEntry->segment == segment &&
			pEdgeEntry->tile.d[0] == pPiece->tile.d[0] &&
			pEdgeEntry->tile.d[1] == pPiece->tile.d[1]) {

			if (pEdgeEntry->entries[pEdgeEntry->idx] != pPiece->entryIdx) {
				//other side of the edge
				pEdgeEntry->entries[1] = pPiece->entryIdx;
				pEdgeEntry->corner[1] = (I16)corner;
				pEdgeEntry->idx = 1;
				pEdgeEntry->refIdx[1] = refIdx;
				if (pEdgeEntry->inOrient != (bool)pPiece->pEntry->inOrient) {
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
			err = stucLinAlloc(pArgs->pSharedEdgeAlloc, &pEdgeEntry->pNext, 1);
			STUC_RETURN_ERR_IFNOT(err, "");
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
	return err;
}

static
Result addEntryToSharedEdgeTable(
	MakePiecesJobArgs *pArgs,
	BorderFace *pEntry,
	SharedEdgeWrap *pSharedEdges,
	Piece *pEntries,
	I32 tableSize,
	I32 entryIdx,
	I32 *pTotalVerts,
	bool *pHasPreserve
) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", (tableSize > 0 && entryIdx >= 0) || !entryIdx);
	Piece *pPiece = pEntries + entryIdx;
	BufMesh *pBufMesh = &pArgs->pMappingJobArgs[pEntry->job].bufMesh;
	FaceRange face = stucGetFaceRange(&pBufMesh->mesh.core, pEntry->bufFace, true);
	pPiece->bufFace = face;
	pPiece->pEntry = pEntry;
	pPiece->entryIdx = entryIdx;
	err = stucLinAlloc(pArgs->pOrderAlloc, &pPiece->pOrder, face.size);
	STUC_RETURN_ERR_IFNOT(err, "");
	err = stucLinAlloc(pArgs->pEdgeSegPairAlloc, &pPiece->pEdges, face.size);
	STUC_RETURN_ERR_IFNOT(err, "");
	pPiece->tile = stucGetTileMinFromBoundsEntry(pEntry);
	for (I32 i = 0; i < face.size; ++i) {
		STUC_ASSERT("", pTotalVerts && *pTotalVerts >= 0);
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
			pPiece->keepVertPreserve |= (I64)true << i;
		}
		V2_I32 corners = pArgs->pBasic->pInMesh->pEdgeCorners[inInfo.edge];
		STUC_ASSERT(
			"",
			(corners.d[0] == inInfo.edgeCorner || corners.d[1] == inInfo.edgeCorner)
		);
		if (corners.d[1] < 0) {
			//no other vert on edge
			pPiece->hasSeam = true;
			continue;
		}
		bool baseKeep;
		if (isOnInVert) {
			STUC_ASSERT(
				"pInVertTable is 0 .. 3",
				pArgs->pBasic->pInMesh->pNumAdjPreserve[inInfo.vert] >= 0 &&
				pArgs->pBasic->pInMesh->pNumAdjPreserve[inInfo.vert] <= 3
			);
			baseKeep = pArgs->pBasic->pInMesh->pNumAdjPreserve[inInfo.vert] > 0;
			pPiece->keepPreserve |= (I64)baseKeep << i;
		}
		if (!pSharedEdges) {
			//If shared edges is NULL, then there's only 1 border face entry.
			//So no need for a shared edge table
			STUC_ASSERT("", !entryIdx);
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
		err = checkIfSeamOrPreserve(
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
		STUC_RETURN_ERR_IFNOT(err, "");
		err = addToTable(
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
		STUC_RETURN_ERR_IFNOT(err, "");
		STUC_ASSERT("", i >= 0 && i < face.size);
	}
	return err;
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
	MakePiecesJobArgs *pArgs,
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
	MakePiecesJobArgs *pArgs,
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
	MakePiecesJobArgs *pArgs,
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
	MakePiecesJobArgs *pArgs,
	EdgeStack *pItem,
	Mesh *pBufMesh,
	StucMap pMap,
	FaceRange *pMapFace,
	I32 *pMapCorner,
	bool side
) {
	if (!pMap->pMesh->pEdgeReceive && pArgs->pBasic->receiveLen < .0f) {
		return false;
	}
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
		I32 mapEdge = pMap->pMesh->core.pEdges[*pMapCorner];
		return stucCheckIfEdgeIsReceive(pMap->pMesh, mapEdge, pArgs->pBasic->receiveLen);
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
		I32 mapEdge = pMap->pMesh->core.pEdges[*pMapCorner];
		if (!stucCheckIfEdgeIsReceive(pMap->pMesh, mapEdge, pArgs->pBasic->receiveLen)) {
			continue;
		}
		F32 t = .0f;
		I32 iNext = (i + 1) % pMapFace->size;
		I32 mapVert = pMap->pMesh->core.pCorners[*pMapCorner];
		I32 mapCornerNext = pMapFace->start + iNext;
		I32 mapVertNext = pMap->pMesh->core.pCorners[mapCornerNext];
		//STUC_ASSERT("", !_(*(V2_F32 *)&pMapMesh->pVerts[mapVert] V2EQL *(V2_F32 *)&pMapMesh->pVerts[mapVertNext]));
		V3_F32 intersect = {0};
		bool valid = stucCalcIntersection(
			pMap->pMesh->pVerts[mapVert],
			pMap->pMesh->pVerts[mapVertNext],
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
	MakePiecesJobArgs *pArgs,
	FaceRange *pMapFace,
	SharedEdge *pEdge,
	bool side
) {
	EdgeStack *pItem = pStack + *pStackPtr;
	const Mesh *pMapMesh = pArgs->pBasic->pMap->pMesh;
	Mesh *pBufMesh = (Mesh *)&pArgs->pMappingJobArgs[pItem->pPiece->pEntry->job].bufMesh;
	I32 mapCorner = -1;
	bool isReceive = checkIfIntersectsReceive(
		pArgs,
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
	MakePiecesJobArgs *pArgs,
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
	MakePiecesJobArgs *pArgs,
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
	MakePiecesJobArgs *pArgs,
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
void setKeepIfOnInVert(MakePiecesJobArgs *pArgs, Piece *pPiece, I32 corner) {
	bool onInVert = stucGetIfOnInVert(pPiece->pEntry, corner);
	if (onInVert) {
		BorderInInfo inInfo =
			stucGetBorderEntryInInfo(pArgs->pBasic, pPiece->pEntry, corner);
		stucSetBitArr(pArgs->pInVertKeep, inInfo.vert, true, 1);
	}
}

static
Result walkEdgesForPreserve(
	EdgeStack *pStack,
	I32 *pStackPtr,
	bool *pValid,
	I32 treeCount,
	I32 *pReceive,
	MakePiecesJobArgs *pArgs,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	Piece *pPieceRoot,
	FaceRange *pMapFace,
	bool *pUnwind
) {
	Result err = STUC_SUCCESS;
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
			return err;
		}
		STUC_ASSERT("", pItem->pPiece);
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
			return err;
		}
	} while(true);
	return err;
}

//TODO Make a in this function for in verts,
//     list in each entry whether to keep
//     (due to bordering a preserve edge)
//     create the table in the stack walk, then
//     set preserve in the later validate corner
static
Result validatePreserveEdges(
	MakePiecesJobArgs *pArgs,
	PieceArr *pPieceArr,
	I32 piece,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize,
	bool **ppValid,
	I32 *pValidCount,
	I32 *pValidSize
) {
	Result err = STUC_SUCCESS;
	//TODO first , check if the map face even has any preserve edges,
	//     no point doing all this if not
	const StucAlloc *pAlloc = &pArgs->pBasic->pCtx->alloc;
	// Get first not exterior corner
	// This is done to ensure we don't start inside the face
	Piece *pPiece = pPieceArr->pArr + piece;
	Piece *pPieceRoot = pPiece;
	FaceRange mapFace = stucGetFaceRange(
		&pArgs->pBasic->pMap->pMesh->core,
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
		err = walkEdgesForPreserve(
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
		STUC_RETURN_ERR_IFNOT(err, "");
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
	return err;
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
		pPieceA->keepSeam |= (I64)true << cornerA;

		I32 cornerB = pEdgeEntry->corner[!whichCorner];
		Piece *pPieceB = pPieceArr->pArr + pEdgeEntry->entries[!whichCorner];
		// Also set adjacent corner
		I32 adjCorner = (cornerB + 1) % pPieceB->bufFace.size;
		pPieceB->keepSeam |= (I64)true << adjCorner;
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
		         pEdgeEntry->tile.d[0] == (I16)pPiece->pEntry->tileX &&
		         pEdgeEntry->tile.d[1] == (I16)pPiece->pEntry->tileY) {

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
Result joinAdjIntoPieceArr(
	MakePiecesJobArgs *pArgs,
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
	Result err = STUC_SUCCESS;
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
				err = validatePreserveEdges(
					pArgs,
					pPieceArr,
					j,
					pEdgeTable,
					edgeTableSize,
					ppValid,
					pValidCount,
					pValidSize
				);
				STUC_RETURN_ERR_IFNOT(err, "");
			}
			++pPieceRoots->count;
		}
		STUC_ASSERT(
			"",
			!pPieceRoots->count || pPieceArr->pArr[pPieceRoots->count - 1].pEntry
		);
		STUC_ASSERT("", j >= 0 && j < pPieceArr->count);
	}
	return err;
}

static
void flipSharedEdgeEntry(SharedEdge *pEntry) {
	I32 entryBuf = pEntry->entries[0];
	I32 cornerBuf = pEntry->corner[0];
	I32 refIdxBuf = pEntry->refIdx[0];
	pEntry->entries[0] = pEntry->entries[1];
	pEntry->entries[1] = entryBuf;
	pEntry->corner[0] = pEntry->corner[1];
	pEntry->corner[1] = (I16)cornerBuf;
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
Result linkConnectedPieces(
	MakePiecesJobArgs *pArgs,
	bool hasPreserve,
	PieceRootsArr *pPieceRoots,
	PieceArr *pPieceArr,
	SharedEdgeWrap *pEdgeTable,
	I32 edgeTableSize
) {
	Result err = STUC_SUCCESS;
	//A first pass separates pieces by connectivity only, then validates preserve edges.
	//Once that's done, a second pass is done with preserve edges
	bool *pValid = NULL;
	I32 validCount = 1; //first is reserved and is always false
	I32 validSize = 0;
	I32 i = 0;
	do {
		if (areNonListedPiecesLinked(pPieceArr)) {
			STUC_ASSERT("Linked pieces here will cause an infinite corner", false);
		}
		err = joinAdjIntoPieceArr(
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
		STUC_RETURN_ERR_IFNOT(err, "");
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
	return err;
}

static
Result splitIntoPieces(
	MakePiecesJobArgs *pArgs,
	PieceRootsArr *pPieceRoots,
	BorderFace *pEntry,
	SharedEdgeWrap **ppSharedEdges,
	I32 *pEdgeTableSize,
	PieceArr *pPieceArr,
	I32 *pTotalVerts
) {
	Result err = STUC_SUCCESS;
	const StucAlloc *pAlloc = &pArgs->pBasic->pCtx->alloc;
	*pEdgeTableSize = 0;
	I32 entryCount = pPieceArr->count;
	if (entryCount > 1) {
		*pEdgeTableSize = entryCount;
		*ppSharedEdges =
			pAlloc->pCalloc(*pEdgeTableSize, sizeof(SharedEdgeWrap));
	}
	Piece *pEntries = pAlloc->pCalloc(entryCount, sizeof(Piece));
	pPieceRoots->pArr = pAlloc->pMalloc(sizeof(I32) * entryCount);
	pPieceArr->pArr = pEntries;
	pPieceArr->count = entryCount;
	I32 entryIdx = 0;
	bool hasPreserve = false;
	do {
		//If there's only 1 border face entry, then this function will just
		//initialize the Piece.
		err = addEntryToSharedEdgeTable(
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
		//dumpBoundsFaceToFile(pArgs, pPieceArr);
	}
	if (entryCount == 1) {
		pPieceRoots->pArr[0] = 0;
		pPieceRoots->count = 1;
	}
	else {
		//now link together connected entries.
		err = linkConnectedPieces(
			pArgs,
			hasPreserve,
			pPieceRoots,
			pPieceArr,
			*ppSharedEdges,
			*pEdgeTableSize
		);
		STUC_RETURN_ERR_IFNOT(err, "");
	}
	for (I32 i = 0; i < entryCount; ++i) {
		SharedEdgeWrap* pBucket = *ppSharedEdges + i;
		//correctEdgeDir(pArgs, pPieceArr, pBucket);
		STUC_ASSERT("", i < entryCount);
	}
	return err;
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
void markPreserveIfKeepInVert(MakePiecesJobArgs *pArgs, Piece* pPiece, I32 k) {
	bool keepInVert = false;
	BorderInInfo inInfo = stucGetBorderEntryInInfo(pArgs->pBasic, pPiece->pEntry, k);
	bool onInVert = stucGetIfOnInVert(pPiece->pEntry, k);
	if (onInVert) {
		keepInVert = stucIdxBitArray(pArgs->pInVertKeep, inInfo.vert, 1);
		if (keepInVert) {
			pPiece->keepPreserve |= (I64)true << k;
		}
	}
}

static
void markKeepInVertsPreserve(MakePiecesJobArgs *pArgs, Piece* pPiece) {
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
	MakePiecesJobArgs *pArgs,
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
			pPiece->add |= (I64)true << corner;
			pPiece->pOrder[corner] = (U8)sort;
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
				pPiece->add |= (I64)true << corner;
				pPiece->pOrder[corner] = (U8)sort;
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

				pPiece->add |= (I64)true << corner;
				pPiece->pOrder[corner] = (U8)sort;
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
	MappingJobArgs *pMappingJobArgs
) {
	*ppInFaces = pAlloc->pCalloc(pieceCount, sizeof(I32));
	I32 i = 0;
	do {
		I32 offset = pMappingJobArgs[pPiece->pEntry->job].inFaceRange.start;
		(*ppInFaces)[i] = pPiece->pEntry->inFace + offset;
		pPiece = pPiece->pNext;
		i++;
	} while(pPiece);
}

static
void initVertTableEntry(
	MakePiecesJobArgs *pArgs,
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
	I32 outVert =
		stucMeshAddVert(&pArgs->pBasic->pCtx->alloc, &pArgs->pBasic->outMesh, &realloced);
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
	MakePiecesJobArgs *pArgs,
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
Result blendMergedCornerAttribs(
	BlendConfig config,
	AttribArray *pDestArr,
	I32 iDest,
	AttribArray *pSrcArr,
	I32 iSrc,
	Attrib *pDestNormalAttrib
) {
	Result err = STUC_SUCCESS;
	for (I32 i = 0; i < pDestArr->count; ++i) {
		Attrib *pDest = pDestArr->pArr + i;
		Attrib *pSrc = pSrcArr->pArr + i;
		STUC_ASSERT(
			"",
			!strncmp(pDest->core.name, pSrc->core.name, STUC_ATTRIB_NAME_MAX_LEN)
		);
		if (pDest != pDestNormalAttrib &&
			(!pDest->origin == STUC_ATTRIB_ORIGIN_MAP ||
			!pDest->interpolate)) {
			continue;
		}
		stucBlendAttribs(pDest, iDest, pDest, iDest, pSrc, iSrc, config);
	}
	return err;
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
Result addBorderCornerAndVert(
	MakePiecesJobArgs *pArgs,
	Piece *pPiece,
	Piece *pPieceRoot,
	I32 k,
	bool addToTables
) {
	Result err = STUC_SUCCESS;
	StucMap pMap = pArgs->pBasic->pMap;
	BorderFace *pEntry = pPiece->pEntry;
	STUC_ASSERT(
		"This should not be called on a map corner",
		!stucGetIfStuc(pEntry, k)
	);
	BufMesh *pBufMesh = &pArgs->pMappingJobArgs[pEntry->job].bufMesh;
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
		mapCorner >= 0 && mapCorner < pMap->pMesh->core.cornerCount);
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
			stucGetFaceRange(&pMap->pMesh->core, pEntry->mapFace, false);
		mapEdge = pMap->pMesh->core.pEdges[mapFace.start + mapCorner];
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
			STUC_ASSERT("", pVertEntry->mapEdge < pMap->pMesh->core.edgeCount);
			STUC_ASSERT("", pVertEntry->mapFace >= 0);
			STUC_ASSERT("", pVertEntry->mapFace < pMap->pMesh->core.faceCount);
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
				BufMesh *pOtherBufMesh = &pArgs->pMappingJobArgs[pVertEntry->job].bufMesh;
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
				STUC_ASSERT(
					"'isOnInVert' mismatch between corner and entry",
					(isOnInVert && pVertEntry->inVert != -1) ||
					(!isOnInVert && pVertEntry->inVert == -1)
				);
				vert = pVertEntry->vert;
				pVertEntry->corners++;
				if (isOnInVert) {
					BufMesh *pOtherBufMesh = &pArgs->pMappingJobArgs[pVertEntry->job].bufMesh;
					err = blendMergedCornerAttribs(
						blendConfigAdd,
						&pOtherBufMesh->mesh.core.cornerAttribs,
						pVertEntry->corner,
						&pBufMesh->mesh.core.cornerAttribs,
						corner,
						pOtherBufMesh->mesh.pNormalAttrib
					);
					STUC_RETURN_ERR_IFNOT(err, "");
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
	return err;
}

static
Result mergeAttribsForSingleCorner(MakePiecesJobArgs *pArgs, Piece *pPiece, I32 k) {
	Result err = STUC_SUCCESS;
	StucMap pMap = pArgs->pBasic->pMap;
	BorderFace *pEntry = pPiece->pEntry;
	STUC_ASSERT(
		"This should only be called on onInVert corners",
		stucGetIfOnInVert(pEntry, k)
	);
	I32 mapCorner = stucGetMapCorner(pEntry, k);
	STUC_ASSERT("", mapCorner >= 0 && mapCorner < pMap->pMesh->core.cornerCount);
	BorderInInfo inInfo = stucGetBorderEntryInInfo(pArgs->pBasic, pEntry, k);
	I32 hash;
	hash = stucFnvHash((U8 *)&inInfo.vert, 4, pArgs->pCTables->vertTableSize);
	BorderVert *pVertEntry = pArgs->pCTables->pVertTable + hash;
	STUC_ASSERT("No entry was initialized for this corner", pVertEntry->corners);

	BufMesh *pBufMesh = &pArgs->pMappingJobArgs[pEntry->job].bufMesh;
	I32 corner = pPiece->bufFace.start - k;

	BlendConfig blendConfigReplace = {.blend = STUC_BLEND_REPLACE};
	do {
		STUC_ASSERT("", pVertEntry->mapFace >= 0);
		STUC_ASSERT("", pVertEntry->mapFace < pMap->pMesh->core.faceCount);
		bool match;
		V2_F32 *pMeshInUvA = pArgs->pBasic->pInMesh->pUvs + pVertEntry->cornerIdx;
		V2_F32 *pMeshInUvB = pArgs->pBasic->pInMesh->pUvs + inInfo.vertCorner;
		match = pVertEntry->inVert == inInfo.vert &&
		        pVertEntry->mapFace == pEntry->mapFace &&
		        pMeshInUvA->d[0] == pMeshInUvB->d[0] &&
		        pMeshInUvA->d[1] == pMeshInUvB->d[1];
		if (match) {
			STUC_RETURN_ERR_IFNOT_COND(err, pVertEntry->inVert != -1, "Entry is not onInVert");
			BufMesh *pOtherBufMesh = &pArgs->pMappingJobArgs[pVertEntry->job].bufMesh;
			if (!pVertEntry->divided) {

				divideCornerAttribsByScalar(
					&pOtherBufMesh->mesh.core.cornerAttribs,
					pVertEntry->corner,
					pVertEntry->corners,
					pOtherBufMesh->mesh.pNormalAttrib
				);
				pVertEntry->divided = true;
			}
			err = blendMergedCornerAttribs(
				blendConfigReplace,
				&pBufMesh->mesh.core.cornerAttribs,
				corner,
				&pOtherBufMesh->mesh.core.cornerAttribs,
				pVertEntry->corner,
				pBufMesh->mesh.pNormalAttrib
			);
			STUC_RETURN_ERR_IFNOT(err, "");
			return err;
		}
		pVertEntry = pVertEntry->pNext;
	} while(pVertEntry);
	STUC_RETURN_ERR_IFNOT_COND(err, pVertEntry, "No entry was initialized for this corner");
	return err;
}

static
void addToOutMesh(MakePiecesJobArgs *pArgs) {
	StucContext pCtx = pArgs->pBasic->pCtx;
	const StucAlloc *pAlloc = &pCtx->alloc;
	StucMap pMap = pArgs->pBasic->pMap;
	I32 count = pArgs->entriesEnd - pArgs->entriesStart;
	MergeBufHandles mergeBufHandles = {0};
	stucAllocMergeBufs(pArgs->pBasic->pCtx, &mergeBufHandles, pArgs->totalVerts);
	for (I32 i = 0; i < count; ++i) {
		I32 reali = pArgs->entriesStart + i;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		FaceRange mapFace =
			stucGetFaceRange(&pMap->pMesh->core, pPieceArr->pArr[0].pEntry->mapFace, false);
		for (I32 j = 0; j < pPieceRoots->count; ++j) {
			Piece *pPieceRoot = pPieceArr->pArr + pPieceRoots->pArr[j];
			I32 *pInFaces = NULL;
			I32 pieceCount = 0; //this is only need if getting in faces
			if (pArgs->pBasic->ppInFaceTable) {
				pieceCount = getPieceCount(pPieceRoot); 
				getPieceInFaces(
					pAlloc,
					&pInFaces,
					pPieceRoot,
					pieceCount,
					pArgs->pMappingJobArgs
				);
			}
			I32 job = pPieceArr->pArr[pPieceRoots->pArr[j]].pEntry->job;
			STUC_ASSERT("", job >= 0 && job < pCtx->threadCount);
			stucMergeSingleBorderFace(
				pArgs,
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
		STUC_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
	stucDestroyMergeBufs(pArgs->pBasic->pCtx, &mergeBufHandles);
}

static
Result addPieceBorderCornersAndVerts(
	MakePiecesJobArgs *pArgs,
	PieceArr *pPieceArr,
	PieceRootsArr *pPieceRoots,
	bool preserve,
	I32 pieceIdx
) {
	Result err = STUC_SUCCESS;
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
				STUC_ASSERT(
					"corner marked add, but sort didn't touch it?",
					pPiece->pOrder[k] > 0
				);
				err = addBorderCornerAndVert(pArgs, pPiece, pPieceRoot, k, !preserve);
				STUC_RETURN_ERR_IFNOT(err, "");
			}
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	return err;
}

static
Result mergeIntersectionCorners(MakePiecesJobArgs *pArgs, bool preserve) {
	Result err = STUC_SUCCESS;
	I32 count = pArgs->entriesEnd - pArgs->entriesStart;
	for (I32 i = 0; i < count; ++i) {
		I32 reali = pArgs->entriesStart + i;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		for (I32 j = 0; j < pPieceRoots->count; ++j) {
			err = addPieceBorderCornersAndVerts(
				pArgs,
				pPieceArr,
				pPieceRoots,
				preserve,
				j
			);
			STUC_RETURN_ERR_IFNOT(err, "");
		}
		STUC_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
	return err;
}

static
Result mergePieceCornerAttribs(
	MakePiecesJobArgs *pArgs,
	PieceArr *pPieceArr,
	PieceRootsArr *pPieceRoots,
	I32 pieceIdx
) {
	Result err = STUC_SUCCESS;
	Piece *pPiece = pPieceArr->pArr + pPieceRoots->pArr[pieceIdx];
	do {
		for (I32 k = 0; k < pPiece->bufFace.size; ++k) {
			if ((pPiece->add >> k & 0x1) &&
			    !stucGetIfStuc(pPiece->pEntry, k) &&
			    stucGetIfOnInVert(pPiece->pEntry, k)) {

				err = mergeAttribsForSingleCorner(pArgs, pPiece, k);
				STUC_RETURN_ERR_IFNOT(err, "");
			}
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	return err;
}

static
Result mergeCornerAttribs(MakePiecesJobArgs *pArgs) {
	Result err = STUC_SUCCESS;
	I32 count = pArgs->entriesEnd - pArgs->entriesStart;
	for (I32 i = 0; i < count; ++i) {
		I32 reali = pArgs->entriesStart + i;
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		for (I32 j = 0; j < pPieceRoots->count; ++j) {
			err = mergePieceCornerAttribs(pArgs, pPieceArr, pPieceRoots, j);
			STUC_RETURN_ERR_IFNOT(err, "");
		}
		STUC_ASSERT("", reali >= pArgs->entriesStart && reali < pArgs->entriesEnd);
	}
	return err;
}

static
UsgInFace *findUsgForMapCorners(
	MakePiecesJobArgs *pArgs,
	FaceRange *pMapFace,
	BorderFace *pEntry,
	V3_F32 uvw,
	Usg **ppUsg
) {
	StucMap pMap = pArgs->pBasic->pMap;
	for (I32 i = 0; i < pMapFace->size; ++i) {
		I32 mapVert = pMap->pMesh->core.pCorners[pMapFace->start + i];
		if (!pMap->pMesh->pUsg) {
			continue;
		}
		I32 usgIdx = pMap->pMesh->pUsg[mapVert];
		if (!usgIdx) {
			continue;
		}
		usgIdx = abs(usgIdx) - 1;
		*ppUsg = pMap->usgArr.pArr + usgIdx;
		if (stucIsPointInsideMesh(&pArgs->pBasic->pCtx->alloc, uvw, (*ppUsg)->pMesh)) {
			I32 inFaceOffset = pArgs->pMappingJobArgs[pEntry->job].inFaceRange.start;
			//passing NULL for above cutoff,
			// we don't need to know cause using flatcutoff eitherway here
			UsgInFace *pUsgEntry = stucGetUsgForCorner(
				i,
				pMap,
				pMapFace,
				pEntry->inFace + inFaceOffset,
				NULL
			);
			if (pUsgEntry) {
				return pUsgEntry;
			}
		}
	}
	return NULL;
}

static
void transformDeferredVert(
	MakePiecesJobArgs *pArgs,
	Piece *pPiece,
	BufMesh *pBufMesh,
	FaceRange *pMapFace,
	I32 cornerLocal,
	V2_I16 tile
) {
	MapToMeshBasic *pBasic = pArgs->pBasic;
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
	V3_F32 pos = _(posFlat V3ADD _(projNormal V3MULS w * pBasic->wScale));
	V3_F32 normal = {0};
	V2_F32 fTileMin = {(F32)tile.d[0], (F32)tile.d[1]};
	bool normalTransformed = false;
	if (!stucGetIfOnInVert(pEntry, cornerLocal) && !pBasic->ppInFaceTable) {
		V3_F32 uvw = {0};
		*(V2_F32 *)&uvw = _(pBufMesh->mesh.pUvs[corner] V2SUB fTileMin);
		uvw.d[2] = pBufMesh->pW[corner];
		Usg *pUsg = NULL;
		UsgInFace *pUsgEntry = findUsgForMapCorners(pArgs, pMapFace, pEntry, uvw, &pUsg);
		if (pUsgEntry) {
			STUC_ASSERT("", pUsg);
			bool flatCutoff = pUsg->pFlatCutoff &&
				stucIsPointInsideMesh(&pBasic->pCtx->alloc, uvw, pUsg->pFlatCutoff);
			if (flatCutoff) {
				stucUsgVertTransform(
					pUsgEntry,
					uvw,
					&pos,
					pBasic->pInMesh,
					fTileMin,
					&tbn
				);
			}
			stucUsgVertSetNormal(pUsgEntry, &normal);
			pos = _(posFlat V3ADD _(normal V3MULS w * pBasic->wScale));
			if (flatCutoff) {
				normalTransformed = true;
				normal = _(pBufMesh->mesh.pNormals[corner] V3MULM3X3 &tbn);
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
void transformDefferedCorners(
	MakePiecesJobArgs *pArgs,
	FaceRange *pMapFace,
	Piece *pPiece
) {
	do {
		BufMesh *pBufMesh = &pArgs->pMappingJobArgs[pPiece->pEntry->job].bufMesh;
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
			pPiece->pOrder[i] = (U8)count - pPiece->pOrder[i];
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void allocArrsForPieceStage(MakePiecesJobArgs *pArgs, I32 count) {
	StucAlloc *pAlloc = &pArgs->pBasic->pCtx->alloc;
	pArgs->pPieceArrTable = pAlloc->pCalloc(count, sizeof(PieceArr));
	pArgs->pPieceRootTable = pAlloc->pCalloc(count, sizeof(PieceRootsArr));
	pArgs->pTotalVertTable = pAlloc->pCalloc(count, sizeof(I32));
	pArgs->pInVertKeep = pAlloc->pCalloc(pArgs->pBasic->pInMesh->core.vertCount, 1);
	{
		I32 sizeEstimate = count * 4;
		stucLinAllocInit(
			pAlloc,
			&pArgs->pOrderAlloc,
			1,
			sizeEstimate
		);
		stucLinAllocInit(
			pAlloc,
			&pArgs->pEdgeSegPairAlloc,
			sizeof(EdgeSegmentPair),
			sizeEstimate
		);
	}
	{
		I32 sizeEstimate = count / 8 + 1;
		stucLinAllocInit(
			pAlloc,
			&pArgs->pSharedEdgeAlloc,
			sizeof(SharedEdge),
			sizeEstimate
		);
	}
}

static
Result createAndJoinPieces(void *pArgsVoid) {
	Result err = STUC_SUCCESS;
	MakePiecesJobArgs *pArgs = pArgsVoid;
	StucContext pCtx = pArgs->pBasic->pCtx;
	I32 count = pArgs->entriesEnd - pArgs->entriesStart;
	allocArrsForPieceStage(pArgs, count);
	for (I32 i = 0; i < count; ++i) {
		I32 reali = pArgs->entriesStart + i;
		BorderFace *pEntry = pArgs->pBorderTable->ppTable[reali];
		I32 entryCount = 0;
		compileEntryInfo(pEntry, &entryCount);
		STUC_ASSERT("", entryCount);
		FaceRange mapFace =
			stucGetFaceRange(&pArgs->pBasic->pMap->pMesh->core, pEntry->mapFace, false);
		PieceRootsArr *pPieceRoots = pArgs->pPieceRootTable + i;
		pPieceRoots->count = 0;
		PieceArr *pPieceArr = pArgs->pPieceArrTable + i;
		pPieceArr->count = entryCount;
		pPieceArr->pArr = NULL;
		SharedEdgeWrap *pSharedEdges = NULL;
		I32 edgeTableSize = 0;
		I32 totalVerts = 0;
		err = splitIntoPieces(
			pArgs,
			pPieceRoots,
			pEntry,
			&pSharedEdges,
			&edgeTableSize,
			pPieceArr,
			&totalVerts
		);
		STUC_ASSERT("", pPieceRoots->count > 0);
		STUC_THROW_IFNOT(err, "", 1);
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
		}
		STUC_CATCH(1, err, ;);
		if (pSharedEdges) {
			pArgs->pBasic->pCtx->alloc.pFree(pSharedEdges);
		}
		STUC_THROW_IFNOT(err, "", 0);
	}
	STUC_CATCH(0, err, ;);
	if (pArgs->pSharedEdgeAlloc) {
		stucLinAllocDestroy(pArgs->pSharedEdgeAlloc);
		pArgs->pSharedEdgeAlloc = NULL;
	}
	return err;
}

static
Result mergeAndAddToOutMesh(
	int32_t jobCount,
	MakePiecesJobArgs *pArgArr
) {
	Result err = STUC_SUCCESS;
	for (I32 i = 0; i < jobCount; ++i) {
		err = mergeIntersectionCorners(pArgArr + i, false);
		STUC_RETURN_ERR_IFNOT(err, "");
		err = mergeIntersectionCorners(pArgArr + i, true);
		STUC_RETURN_ERR_IFNOT(err, "");
		err = mergeCornerAttribs(pArgArr + i);
		STUC_RETURN_ERR_IFNOT(err, "");
	}
	for (I32 i = 0; i < jobCount; ++i) {
		addToOutMesh(pArgArr + i);
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
	MappingJobArgs *pMappingJobArgs,
	BorderBucket *pBucket,
	I32 mapFace,
	I32 hash,
	I32 job,
	I32 mapJobsSent
) {
	for (I32 j = job + 1; j < mapJobsSent; ++j) {
		if (!pMappingJobArgs[j].bufSize) {
			continue;
		}
		BorderBucket *pBucketOther = pMappingJobArgs[j].borderTable.pTable + hash;
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
	MappingJobArgs *pMappingJobArgs,
	CompiledBorderTable *pBorderTable,
	I32 totalBorderFaces,
	I32 mapJobsSent,
	I32 jobIdx,
	I32 hash
) {
	STUC_ASSERT("", pMappingJobArgs[jobIdx].borderTable.size > 0);
	STUC_ASSERT("", pMappingJobArgs[jobIdx].borderTable.pTable);
	BorderBucket *pBucket = pMappingJobArgs[jobIdx].borderTable.pTable + hash;
	I32 depth = 0;
	do {
		if (pBucket->pEntry) {
			I32 mapFace = pBucket->pEntry->mapFace;
			STUC_ASSERT("", mapFace >= 0);
			linkEntriesFromOtherJobs(
				pMappingJobArgs,
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
	MappingJobArgs *pMappingJobArgs,
	CompiledBorderTable *pBorderTable,
	I32 totalBorderFaces,
	I32 mapJobsSent
) {
	pBorderTable->ppTable = pCtx->alloc.pMalloc(sizeof(void *) * totalBorderFaces);
	for (I32 i = 0; i < mapJobsSent; ++i) {
		if (!pMappingJobArgs[i].bufSize) {
			//TODO why is bufsize zero? how? find out
			continue; //skip if buf mesh is empty
		}
		for (I32 hash = 0; hash < pMappingJobArgs[i].borderTable.size; ++hash) {
			walkBucketsAndLink(
				pCtx,
				pMappingJobArgs,
				pBorderTable,
				totalBorderFaces,
				mapJobsSent,
				i,
				hash
			);
			STUC_ASSERT("", hash >= 0 && hash < pMappingJobArgs[i].borderTable.size);
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
	if (pCTables->pVertTable) {
		pAlloc->pFree(pCTables->pVertTable);
	}
	for (I32 i = 0; i < pCTables->onLineTableSize; ++i) {
		OnLine *pEntry = pCTables->pOnLineTable[i].pNext;
		while (pEntry) {
			OnLine *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	if (pCTables->pOnLineTable) {
		pAlloc->pFree(pCTables->pOnLineTable);
	}
	for (I32 i = 0; i < pCTables->edgeTableSize; ++i) {
		BorderEdge *pEntry = pCTables->pEdgeTable[i].pNext;
		while (pEntry) {
			BorderEdge *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		}
	}
	if (pCTables->pEdgeTable) {
		pAlloc->pFree(pCTables->pEdgeTable);
	}
}

static
Result sendOffMakePiecesJobs(
	MapToMeshBasic *pBasic,
	CompiledBorderTable *pBorderTable,
	I32 *pJobCount,
	void ***pppJobHandles,
	MakePiecesJobArgs **ppJobArgs,
	MappingJobArgs *pMappingJobArgs,
	CombineTables *pCTables,
	JobBases *pMappingJobBases
) {
	Result err = STUC_SUCCESS;
	*pJobCount = MAX_SUB_MAPPING_JOBS;
	*pJobCount += *pJobCount == 0;
	I32 entriesPerJob = pBorderTable->count / *pJobCount;
	bool singleThread = !entriesPerJob;
	void *jobArgPtrs[MAX_THREADS] = {0};
	*pJobCount = singleThread ? 1 : *pJobCount;
	*ppJobArgs = pBasic->pCtx->alloc.pCalloc(*pJobCount, sizeof(MakePiecesJobArgs));
	for (I32 i = 0; i < *pJobCount; ++i) {
		I32 entriesStart = entriesPerJob * i;
		I32 entriesEnd = i == *pJobCount - 1 ?
			pBorderTable->count : entriesStart + entriesPerJob;
		//TODO make a struct for these common variables, like pCtx,
		//pMap, pEdgeVerts, etc, so you don't need to move them
		//around manually like this.
		(*ppJobArgs)[i].pBasic = pBasic;
		(*ppJobArgs)[i].pBorderTable = pBorderTable;
		(*ppJobArgs)[i].entriesStart = entriesStart;
		(*ppJobArgs)[i].entriesEnd = entriesEnd;
		(*ppJobArgs)[i].pMappingJobArgs = pMappingJobArgs;
		(*ppJobArgs)[i].pMappingJobBases = pMappingJobBases;
		(*ppJobArgs)[i].pCTables = pCTables;
		(*ppJobArgs)[i].job = i;
		(*ppJobArgs)[i].totalVerts = 4;
		(*ppJobArgs)[i].jobCount = *pJobCount;
		jobArgPtrs[i] = *ppJobArgs + i;
	}
	*pppJobHandles = pBasic->pCtx->alloc.pCalloc(*pJobCount, sizeof(void *));
	err =  pBasic->pCtx->threadPool.pJobStackPushJobs(
		pBasic->pCtx->pThreadPoolHandle,
		*pJobCount,
		*pppJobHandles,
		createAndJoinPieces,
		jobArgPtrs
	);
	STUC_RETURN_ERR_IFNOT(err, "");
	return err;
}

static
void destroyMakePiecesJobArgs(StucContext pCtx, I32 jobCount, MakePiecesJobArgs *pArgArr) {
	if (!pArgArr) {
		return;
	}
	for (I32 i = 0; i < jobCount; ++i) {
		MakePiecesJobArgs *pArgs = pArgArr + i;
		I32 count = pArgs->entriesEnd - pArgs->entriesStart;
		for (I32 j = 0; j < count; ++j) {
			if (pArgs->pPieceRootTable) {
				if (pArgs->pPieceRootTable[j].pArr) {
					pCtx->alloc.pFree(pArgs->pPieceRootTable[j].pArr);
				}
			}
			if (pArgs->pPieceArrTable) {
				if (pArgs->pPieceArrTable[j].pArr) {
					pCtx->alloc.pFree(pArgs->pPieceArrTable[j].pArr);
				}
			}
		}
		if (pArgs->pPieceRootTable) {
			pCtx->alloc.pFree(pArgs->pPieceRootTable);
		}
		if (pArgs->pPieceArrTable) {
			pCtx->alloc.pFree(pArgs->pPieceArrTable);
		}
		if (pArgs->pTotalVertTable) {
			pCtx->alloc.pFree(pArgs->pTotalVertTable);
		}
		if (pArgs->pInVertKeep) {
			pCtx->alloc.pFree(pArgs->pInVertKeep);
		}
		if (pArgs->pOrderAlloc) {
			stucLinAllocDestroy(pArgs->pOrderAlloc);
		}
		if (pArgs->pEdgeSegPairAlloc) {
			stucLinAllocDestroy(pArgs->pEdgeSegPairAlloc);
		}
	}
	pCtx->alloc.pFree(pArgArr);
}

Result stucMergeBorderFaces(
	MapToMeshBasic *pBasic,
	MappingJobArgs *pMappingJobArgs,
	JobBases *pMappingJobBases,
	I32 mapJobsSent
) {
	StucResult err = STUC_SUCCESS;
	StucContext pCtx = pBasic->pCtx;
	I32 totalBorderFaces = 0;
	I32 totalBorderEdges = 0;
	for (I32 i = 0; i < mapJobsSent; ++i) {
		totalBorderFaces += pMappingJobArgs[i].bufMesh.borderFaceCount;
		totalBorderEdges += pMappingJobArgs[i].bufMesh.borderEdgeCount;
		STUC_ASSERT("", i < mapJobsSent);
	}
	CompiledBorderTable borderTable = { 0 };
	//compile border table entries from all jobs, into a single table
	compileBorderTables(
		pBasic->pCtx,
		pMappingJobArgs,
		&borderTable,
		totalBorderFaces,
		mapJobsSent
	);
	//tables used for merging mesh mesh data correctly
	CombineTables cTables = { 0 };
	allocCombineTables(
		&pBasic->pCtx->alloc,
		&cTables,
		totalBorderFaces,
		totalBorderEdges
	);
	MakePiecesJobArgs *pJobArgs = NULL;
	I32 jobCount = 0;
	void **ppJobHandles = NULL;
	err = sendOffMakePiecesJobs(
		pBasic,
		&borderTable,
		&jobCount,
		&ppJobHandles,
		&pJobArgs,
		pMappingJobArgs,
		&cTables,
		pMappingJobBases
	);
	STUC_THROW_IFNOT(err, "", 0);
	err = pCtx->threadPool.pWaitForJobs(
		pCtx->pThreadPoolHandle,
		jobCount,
		ppJobHandles,
		true,
		NULL
	);
	STUC_THROW_IFNOT(err, "", 0);
	err = stucJobGetErrs(pCtx, jobCount, &ppJobHandles);
	STUC_THROW_IFNOT(err, "", 0);
	err = mergeAndAddToOutMesh(jobCount, pJobArgs);
	STUC_THROW_IFNOT(err, "", 0);

	STUC_CATCH(0, err, ;);
	destroyMakePiecesJobArgs(pCtx, jobCount, pJobArgs);
	if (ppJobHandles) {
		stucJobDestroyHandles(pCtx, jobCount, ppJobHandles);
		pCtx->alloc.pFree(ppJobHandles);
	}
	//this is the compiled table,
	// the ones in mapping job args are destroyed in mapToMeshIntern
	if (borderTable.ppTable) {
		pCtx->alloc.pFree(borderTable.ppTable);
	}
	destroyCombineTables(&pCtx->alloc, &cTables);
	return err;
}