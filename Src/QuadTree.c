#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <QuadTree.h>
#include <Context.h>
#include <MapFile.h>
#include <MathUtils.h>
#include <Utils.h>

typedef struct {
	int32_t d[4];
} Children;

static
void calcCellBounds(Cell *cell) {
	float xSide = (float)(cell->localIndex % 2);
	float ySide = (float)(((cell->localIndex + 2) / 2) % 2);
	cell->boundsMin.d[0] = xSide * .5;
	cell->boundsMin.d[1] = ySide * .5;
	cell->boundsMax.d[0] = 1.0 - (1.0 - xSide) * .5;
	cell->boundsMax.d[1] = 1.0 - (1.0 - ySide) * .5;
}

static
void addCellToEncasingCells(Cell *cell, EncasingCells *pEncasingCells,
                            int32_t edge) {
	int32_t faceSize = edge ? cell->edgeFaceSize : cell->faceSize;
	pEncasingCells->faceTotal += faceSize;
	int32_t dupIndex = -1;
	for (int32_t i = 0; i < pEncasingCells->cellSize; ++i) {
		if (pEncasingCells->ppCells[i] == cell) {
			dupIndex = i;
			break;
		}
	}
	if (dupIndex >= 0) {
		if (!pEncasingCells->pCellType[dupIndex] && edge) {
			pEncasingCells->pCellType[dupIndex] = 2;
		}
		return;
	}
	pEncasingCells->ppCells[pEncasingCells->cellSize] = cell;
	pEncasingCells->pCellType[pEncasingCells->cellSize] = edge;
	pEncasingCells->cellSize++;;
	pEncasingCells->faceTotalNoDup += faceSize;
}

static
int32_t findFaceQuadrantUv(RuvmAlloc* pAlloc, int32_t vertCount, V2_F32 *pVerts,
                           V2_F32 midPoint, V2_I32 *commonSides, V2_I32 *signs) {
	commonSides->d[0] = commonSides->d[1] = 1;
	V2_I32* pSides = pAlloc->pMalloc(sizeof(V2_I32) * vertCount);
	for (int32_t i = 0; i < vertCount; ++i) {
		pSides[i].d[0] = pVerts[i].d[0] >= midPoint.d[0];
		pSides[i].d[1] = pVerts[i].d[1] < midPoint.d[1];
		for (int32_t j = 0; j < i; ++j) {
			commonSides->d[0] *= pSides[i].d[0] == pSides[j].d[0];
			commonSides->d[1] *= pSides[i].d[1] == pSides[j].d[1];
		}
	}
	if (!commonSides->d[0] && !commonSides->d[1]) {
		pAlloc->pFree(pSides);
		return 0;
	}
	signs->d[0] = pSides[0].d[0];
	signs->d[1] = pSides[0].d[1];
	pAlloc->pFree(pSides);
	if (commonSides->d[0] && commonSides->d[1]) {
		return 1;
	}
	else {
		return 2;
	}
}

static
int32_t findFaceQuadrant(RuvmAlloc* pAlloc, int32_t loopSize,
	int32_t faceStart, Mesh* pMesh, V2_F32 midPoint,
	V2_I32* commonSides, V2_I32* signs) {
	commonSides->d[0] = commonSides->d[1] = 1;
	V2_I32* pSides = pAlloc->pMalloc(sizeof(V2_I32) * loopSize);
	for (int32_t i = 0; i < loopSize; ++i) {
		int32_t vertIndex = pMesh->mesh.pLoops[faceStart + i];
		pSides[i].d[0] = pMesh->pVerts[vertIndex].d[0] >= midPoint.d[0];
		pSides[i].d[1] = pMesh->pVerts[vertIndex].d[1] < midPoint.d[1];
		for (int32_t j = 0; j < i; ++j) {
			commonSides->d[0] *= pSides[i].d[0] == pSides[j].d[0];
			commonSides->d[1] *= pSides[i].d[1] == pSides[j].d[1];
		}
	}
	if (!commonSides->d[0] && !commonSides->d[1]) {
		pAlloc->pFree(pSides);
		return 0;
	}
	signs->d[0] = pSides[0].d[0];
	signs->d[1] = pSides[0].d[1];
	pAlloc->pFree(pSides);
	if (commonSides->d[0] && commonSides->d[1]) {
		return 1;
	}
	else {
		return 2;
	}
}

static
void getChildrenFromResult(int32_t result, Children* pChildren,
	V2_I32* pCommonSides, V2_I32* pSigns) {
	switch (result) {
		case 0: {
			//addCellToEncasingCells(cell, pEncasingCells, 0);
			//*pCellStackPtr--;
			//pCellInits[cell->cellIndex] = 1;
			//continue;
			pChildren->d[0] = 1;
			pChildren->d[1] = 1;
			pChildren->d[2] = 1;
			pChildren->d[3] = 1;
			break;
		}
		case 1: {
			pChildren->d[0] = !pSigns->d[0] && !pSigns->d[1];
			pChildren->d[1] = pSigns->d[0] && !pSigns->d[1];
			pChildren->d[2] = !pSigns->d[0] && pSigns->d[1];
			pChildren->d[3] = pSigns->d[0] && pSigns->d[1];
			break;
		}
		case 2: {
			int32_t top, bottom = top = pCommonSides->d[1];
			int32_t left, right = left = pCommonSides->d[0];
			top *= !pSigns->d[1];
			bottom *= pSigns->d[1];
			left *= !pSigns->d[0];
			right *= pSigns->d[0];
			pChildren->d[0] = top || left;
			pChildren->d[1] = top || right;
			pChildren->d[2] = bottom || left;
			pChildren->d[3] = bottom || right;
			break;
		}
	}
}

static
void findEncasingChildCells(RuvmAlloc *pAlloc, Cell *pCell, Children *pChildren,
                            int32_t *pCellStackPtr, int32_t vertCount,
							V2_F32 *pVerts, V2_I32 *pTileMin) {
	V2_F32 midPoint = _(_(pCell->boundsMax V2SUB pCell->boundsMin) V2MULS .5);
	_(&midPoint V2ADDEQL pCell->boundsMin);
	V2_I32 signs;
	V2_I32 commonSides;
	midPoint.d[0] += (float)pTileMin->d[0];
	midPoint.d[1] += (float)pTileMin->d[1];
	int32_t result =
		findFaceQuadrantUv(pAlloc, vertCount, pVerts, midPoint,
						   &commonSides, &signs);
	getChildrenFromResult(result, pChildren + *pCellStackPtr, &commonSides, &signs);
}

void ruvmGetAllEncasingCells(QuadTreeSearch *pState, EncasingCells *pEncasingCells,
                             int32_t vertCount, V2_F32 *pVerts, V2_I32 tileMin) {
	Cell *cellStack[16];
	Children children[16];
	Cell *pRootCell = pState->pMap->quadTree.pRootCell;
	cellStack[0] = pRootCell;
	pState->pCellInits[0] = 0;
	int32_t cellStackPtr = 0;
	for (int32_t i = 0; i < 4; ++i) {
		pState->pCellInits[pRootCell->pChildren[i].cellIndex] = 0;
	}
	do {
		Cell *cell = cellStack[cellStackPtr];
		if (!cell->pChildren) {
			addCellToEncasingCells(cell, pEncasingCells, 0);
			cellStackPtr--;
			pState->pCellInits[cell->cellIndex] = 1;
			continue;
		}
		if (pState->pCellInits[cell->cellIndex]) {
			int32_t nextChild = -1;
			for (int32_t i = 0; i < 4; ++i) {
				if (!pState->pCellInits[cell->pChildren[i].cellIndex] &&
				    *((int32_t *)(children + cellStackPtr) + i)) {
					nextChild = i;
					break;
				}
			}
			if (nextChild == -1) {
				cellStackPtr--;
				continue;
			}
			cellStackPtr++;
			cellStack[cellStackPtr] = cell->pChildren + nextChild;
			continue;
		}
		findEncasingChildCells(pState->pAlloc, cell, children, &cellStackPtr,
		                       vertCount, pVerts, &tileMin);
		addCellToEncasingCells(cell, pEncasingCells, 1);
		pState->pCellInits[cell->cellIndex] = 1;
		for (int32_t i = 0; i < 4; ++i) {
			pState->pCellInits[cell->pChildren[i].cellIndex] = 0;
		}
		int32_t nextChild = 0;
		for (int32_t i = 0; i < 4; ++i) {
			if (*((int32_t *)(children + cellStackPtr) + i)) {
				nextChild = i;
				break;
			}
		}
		cellStackPtr++;
		cellStack[cellStackPtr] = cell->pChildren + nextChild;
	} while (cellStackPtr >= 0);
}

static
void checkIfFaceIsInsideTile(int32_t vertCount,
                             V2_F32 *pVerts, FaceBounds *pFaceBounds,
							 int32_t *pIsInsideBuffer, int32_t *pFaceVertInside,
							 V2_I32 tileMin) {
	for (int32_t i = 0; i < vertCount; ++i) {
		//check if current edge intersects tile
		int32_t nexti = (i + 1) % vertCount;
		V2_F32 loopDir = _(pVerts[nexti] V2SUB pVerts[i]);
		V2_F32 loopCross = v2Cross(loopDir);
		for (int32_t j = 0; j < 4; ++j) {
			V2_F32 cellPoint = {tileMin.d[0] + j % 2, tileMin.d[1] + j / 2};
			V2_F32 cellDir = _(cellPoint V2SUB pVerts[i]);
			float dot = _(loopCross V2DOT cellDir);
			pIsInsideBuffer[j] *= dot < .0f;
		}
		//in addition, test for face verts inside tile
		//edge cases may not be cause by the above,
		//like if a face entered the tile, and then exited the same side,
		//with a single vert in the tile. Checking for verts will catch this:
		*pFaceVertInside += _(pVerts[i] V2GREAT pFaceBounds->fMin) &&
		                    _(pVerts[i] V2LESSEQL pFaceBounds->fMax);
	}
}

static
int32_t getCellsForFaceWithinTile(QuadTreeSearch *pState, int32_t vertCount,
                                  V2_F32 *pVerts, FaceBounds *pFaceBounds,
								  EncasingCells *pCellsBuffer, V2_I32 tileMin) {
	//Don't check if in tile, if pFaceBounds is NULL
	if (pFaceBounds) {
		int32_t isInsideBuffer[4] = {1, 1, 1, 1};
		int32_t faceVertInside = 0;
		checkIfFaceIsInsideTile(vertCount, pVerts, pFaceBounds, isInsideBuffer,
								&faceVertInside, tileMin);
		int32_t isInside = isInsideBuffer[0] || isInsideBuffer[1] ||
						   isInsideBuffer[2] || isInsideBuffer[3];
		int32_t isFullyEnclosed = isInsideBuffer[0] && isInsideBuffer[1] &&
								  isInsideBuffer[2] && isInsideBuffer[3];
		if (isFullyEnclosed) {
			return 1;
		}
		if (!faceVertInside && !isInside) {
			//face is not inside current tile
			return 0;
		}
	}
	//find fully encasing cell using clipped face
	ruvmGetAllEncasingCells(pState, pCellsBuffer, vertCount, pVerts, tileMin);
	return 0;
}

static
int32_t checkBranchCellIsLinked(EncasingCells *pCellsBuffer, int32_t index,
                                Range *pRange) {
	int32_t linked = 0;
	Cell *cell = pCellsBuffer->ppCells[index];
	for (int32_t j = 0; j < pCellsBuffer->cellSize; ++j) {
		if (pCellsBuffer->pCellType[j] || index == j) {
			continue;
		}
		Cell *leaf = pCellsBuffer->ppCells[j];
		for (int32_t k = 0; k < leaf->linkEdgeSize; ++k) {
			if (cell->cellIndex == leaf->pLinkEdges[k]) {
				if (!linked) {
					linked = 1;
				}
				if (leaf->pLinkEdgeRanges[k].start < pRange->start) {
					pRange->start = leaf->pLinkEdgeRanges[k].start;
				}
				if (leaf->pLinkEdgeRanges[k].end > pRange->end) {
					pRange->end = leaf->pLinkEdgeRanges[k].end;
				}
				break;
			}
		}
	}
	return linked;
}

static
void removeNonLinkedBranchCells(EncasingCells *pCellsBuffer) {
	for (int32_t i = 0; i < pCellsBuffer->cellSize;) {
		if (!pCellsBuffer->pCellType[i]) {
			i++;
			continue;
		}
		Range range = {.start = INT32_MAX, .end = INT32_MIN};
		if (checkBranchCellIsLinked(pCellsBuffer, i, &range)) {
			pCellsBuffer->pRangeBuf[i] = range;
			i++;
			continue;
		}
		Cell *pCell = pCellsBuffer->ppCells[i];
		pCellsBuffer->faceTotal -= pCell->edgeFaceSize;
		pCellsBuffer->faceTotalNoDup -= pCell->edgeFaceSize;
		for (int32_t j = i; j < pCellsBuffer->cellSize - 1; ++j) {
			pCellsBuffer->ppCells[j] = pCellsBuffer->ppCells[j + 1];
			pCellsBuffer->pCellType[j] = pCellsBuffer->pCellType[j + 1];
		}
		pCellsBuffer->cellSize--;
	}
}

static
void copyCellsIntoTotalList(RuvmAlloc *pAlloc, FaceCellsTable *pFaceCellsTable,
                            EncasingCells *pCellsBuffer, int32_t faceIndex) {
	FaceCells *pEntry = pFaceCellsTable->pFaceCells + faceIndex;
	pFaceCellsTable->cellFacesTotal += pCellsBuffer->faceTotalNoDup;
	pEntry->pCells = pAlloc->pMalloc(sizeof(Cell *) * pCellsBuffer->cellSize);
	pEntry->pCellType = pAlloc->pMalloc(pCellsBuffer->cellSize);
	memcpy(pEntry->pCells, pCellsBuffer->ppCells,
	       sizeof(Cell *) * pCellsBuffer->cellSize);
	memcpy(pEntry->pCellType, pCellsBuffer->pCellType, pCellsBuffer->cellSize);
	pEntry->cellSize = pCellsBuffer->cellSize;
	pEntry->faceSize = pCellsBuffer->faceTotalNoDup;
	pEntry->pRanges = pCellsBuffer->pRangeBuf;
	pCellsBuffer->pRangeBuf = NULL;
	if (pCellsBuffer->faceTotalNoDup > pFaceCellsTable->cellFacesMax) {
		pFaceCellsTable->cellFacesMax = pCellsBuffer->faceTotalNoDup;
	}
}

static
void recordCellsInTable(QuadTreeSearch *pState, FaceCellsTable *pFaceCellsTable,
                        EncasingCells *pCellsBuffer) {
	for (int32_t i = 0; i < pCellsBuffer->cellSize; ++i) {
		Cell *pCell = pCellsBuffer->ppCells[i];
		//must be != 0, not > 0, so as to catch entries set to -1
		if (pState->pCellTable[pCell->cellIndex] != 0) {
			continue;
		}
		int32_t cellType = pCellsBuffer->pCellType[i];
		pState->pCellTable[pCell->cellIndex] = cellType + 1;
		Cell *pStack[32] = {0};
		int8_t childrenLeft[32] = {0};
		pStack[0] = pCell;
		int32_t stackPointer = 0;
		pFaceCellsTable->uniqueFaces += cellType == 1 ?
			pCell->edgeFaceSize : pCell->faceSize;
		if (cellType != 0 || pCell->pChildren == NULL) {
			continue;
		}
		//if cell is not a leaf, and if it isn't an edgefaces cell,
		//then subtract faces from any child cells that have been counted,
		//and/ or set their table entry to -1 so they're not added
		//to the count in future
		do {
			Cell *pChild = pStack[stackPointer];
			int32_t nextChild = childrenLeft[stackPointer];
			if (nextChild > 3) {
				stackPointer--;
				continue;
			}
			int32_t childType = pState->pCellTable[pChild->cellIndex];
			if (pChild != pCell) {
				//must be > 0, so that cells with an entry of -1 arn't touched,
				// as they haven't been added to uniqueFaces
				if (childType > 0) {
					pFaceCellsTable->uniqueFaces -= childType == 2 ?
						pChild->edgeFaceSize : pChild->faceSize;
				}
				//set to -1 so this cell isn't added to the count in future
				pState->pCellTable[pChild->cellIndex] = -1;
			}
			if (pChild->pChildren == NULL) {
				stackPointer--;
				continue;
			}
			childrenLeft[stackPointer]++;
			stackPointer++;
			pStack[stackPointer] = pChild->pChildren + nextChild;
			childrenLeft[stackPointer] = 0;
		} while (stackPointer >= 0);
	}
}

void ruvmInitFaceCellsTable(RuvmAlloc *pAlloc, FaceCellsTable *pTable,
                            int32_t faceCount) {
	pTable->pFaceCells = pAlloc->pMalloc(sizeof(FaceCells) * faceCount);
}

void ruvmDestroyFaceCellsTable(RuvmAlloc *pAlloc,
                               FaceCellsTable *pFaceCellsTable) {
	pAlloc->pFree(pFaceCellsTable->pFaceCells);
}

void ruvmInitQuadTreeSearch(RuvmAlloc *pAlloc, RuvmMap pMap, QuadTreeSearch *pState) {
	pState->pAlloc = pAlloc;
	pState->pMap = pMap;
	pState->pCellInits = pAlloc->pMalloc(pMap->quadTree.cellCount);
	pState->pCellTable = pAlloc->pCalloc(pMap->quadTree.cellCount, sizeof(int8_t));
	pState->ppCells = pAlloc->pMalloc(sizeof(void *) * pMap->quadTree.cellCount);
	pState->pCellType = pAlloc->pMalloc(pMap->quadTree.cellCount);
}

void ruvmDestroyQuadTreeSearch(QuadTreeSearch *pState) {
	pState->pAlloc->pFree(pState->pCellTable);
	pState->pAlloc->pFree(pState->pCellInits);
	pState->pAlloc->pFree(pState->ppCells);
	pState->pAlloc->pFree(pState->pCellType);
}

void ruvmGetCellsForSingleFace(QuadTreeSearch *pState, int32_t vertCount,
                               V2_F32 *pVerts, FaceCellsTable *pFaceCellsTable,
							   FaceBounds *pFaceBounds, int32_t faceIndex) {
	EncasingCells cellsBuffer = {0};
	cellsBuffer.ppCells = pState->ppCells;
	cellsBuffer.pCellType = pState->pCellType;
	FaceCells *pEntry = pFaceCellsTable->pFaceCells + faceIndex;
	V2_I32 minTile = {0};
	V2_I32 maxTile = {0};
	if (pFaceBounds) {
		pEntry->faceBounds = *pFaceBounds;
		minTile = pFaceBounds->min;
		maxTile = pFaceBounds->max;
	}
	for (int32_t i = minTile.d[1]; i <= maxTile.d[1]; ++i) {
		for (int32_t j = minTile.d[0]; j <= maxTile.d[0]; ++j) {
			V2_I32 tileMin = {j, i};
			//continue until the smallest cell that fully
			//encloses the face is found (result == 0).
			//if face fully encloses the while uv tile (result == 1),
			//then return (root cell will be used).
			//if the face is not within the current tile,
			//then skip tile (result == 2).
			if (getCellsForFaceWithinTile(pState, vertCount, pVerts, pFaceBounds,
				                          &cellsBuffer, tileMin)) {
				//fully enclosed
				Cell *rootCell = pState->pMap->quadTree.pRootCell;
				pEntry->pCells = pState->pAlloc->pMalloc(sizeof(Cell *));
				*pEntry->pCells = rootCell;
				pEntry->faceSize = rootCell->faceSize;
				pFaceCellsTable->cellFacesTotal += rootCell->faceSize;
				return;
			}
		}
	}
	cellsBuffer.pRangeBuf =
		pState->pAlloc->pMalloc(sizeof(Range) * cellsBuffer.cellSize);
	removeNonLinkedBranchCells(&cellsBuffer);
	recordCellsInTable(pState, pFaceCellsTable, &cellsBuffer);
	copyCellsIntoTotalList(pState->pAlloc, pFaceCellsTable, &cellsBuffer,
	                       faceIndex);
}

void ruvmLinearizeCellFaces(FaceCells *pFaceCells, int32_t *pCellFaces,
                            int32_t faceIndex) {
	int32_t facesNextIndex = 0;
	for (int32_t j = 0; j < pFaceCells[faceIndex].cellSize; ++j) {
		Cell *cell = pFaceCells[faceIndex].pCells[j];
		if (pFaceCells[faceIndex].pCellType[j]) {
			memcpy(pCellFaces + facesNextIndex, cell->pEdgeFaces,
					sizeof(int32_t) * cell->edgeFaceSize);
			facesNextIndex += cell->edgeFaceSize;
		}
		if (pFaceCells[faceIndex].pCellType[j] != 1) {
			memcpy(pCellFaces + facesNextIndex, cell->pFaces,
					sizeof(int32_t) * cell->faceSize);
			facesNextIndex += cell->faceSize;
		}
	}
}

Cell *ruvmFindEncasingCell(Cell *rootCell, V2_F32 pos) {
	V2_F32 cellBoundsMin = {.d[0] = .0, .d[1] = .0};
	V2_F32 cellBoundsMax = {.d[0] = 1.0, .d[1] = 1.0};
	Cell *cell = rootCell;
	int32_t depth = -1;
	while (true) {
		if (!cell->pChildren) {
			return cell;
		}
		V2_F32 midPoint = _(_(cellBoundsMax V2SUB cellBoundsMin) V2MULS .5);
		_(&midPoint V2ADDEQL cellBoundsMin);
		depth++;
		int32_t childIndex = (pos.d[0] >=
				midPoint.d[0]) + (pos.d[1] < midPoint.d[1]) * 2;
		cell = cell->pChildren + childIndex;
		cellBoundsMin = cell->boundsMin;
		cellBoundsMax = cell->boundsMax;
	};
}

static
void setCellBounds(Cell *cell, Cell *parentCell, int32_t cellStackPtr) {
	calcCellBounds(cell);
	_(&cell->boundsMin V2DIVSEQL (float)pow(2.0, cellStackPtr));
	_(&cell->boundsMax V2DIVSEQL (float)pow(2.0, cellStackPtr));
	V2_F32 *ancestorBoundsMin = &parentCell->boundsMin;
	_(&cell->boundsMin V2ADDEQL *ancestorBoundsMin);
	_(&cell->boundsMax V2ADDEQL *ancestorBoundsMin);
}

static
int32_t checkIfLinkedEdge(Cell *pChild, Cell *pAncestor, Mesh *pMesh, Range *pRange) {
	int32_t linked = 0;
	for (int32_t k = 0; k < pAncestor->edgeFaceSize; ++k) {
		int32_t faceIndex = pAncestor->pEdgeFaces[k];
		FaceRange face;
		face.start = pMesh->mesh.pFaces[faceIndex];
		face.end = pMesh->mesh.pFaces[faceIndex + 1];
		face.size = face.end - face.start;
		//doesn't catch cases where edge intersect with bounds,
		//replace with a better method
		if (checkFaceIsInBounds(pChild->boundsMin, pChild->boundsMax,
			                    face, pMesh)) {
			if (!linked) {
				linked = 1;
				pRange->start = k;
			}
			pRange->end = k;
		}
	}
	pRange->end++;
	return linked;
}

static
void addLinkEdgesToCells(RuvmContext pContext, Cell* pParentCell, Mesh *pMesh,
                         Cell **pCellStack, int32_t cellStackPtr) {
	int32_t buf[32];
	Range rangeBuf[32];
	int32_t bufSize;
	for (int32_t i = 0; i < 4; ++i) {
		bufSize = 0;
		Cell *pChild = pParentCell->pChildren + i;
		for (int32_t j = 0; j <= cellStackPtr; ++j) {
			Cell *pAncestor = pCellStack[j];
			Range range;
			if (checkIfLinkedEdge(pChild, pAncestor, pMesh, &range)) {
				buf[bufSize] = pAncestor->cellIndex;
				rangeBuf[bufSize] = range;
				bufSize++;
			}
		}
		if (bufSize) {
			pChild->pLinkEdges =
				pContext->alloc.pMalloc(sizeof(int32_t) * bufSize);
			memcpy(pChild->pLinkEdges, buf, sizeof(int32_t) * bufSize);
			if (pChild->faceSize <= CELL_MAX_VERTS) {
				//If cell is a leaf, then add link edge ranges
				pChild->pLinkEdgeRanges =
					pContext->alloc.pMalloc(sizeof(Range) * bufSize);
					memcpy(pChild->pLinkEdgeRanges, rangeBuf, sizeof(Range) * bufSize);
			}
			pChild->linkEdgeSize = bufSize;
		}
	}
}

/*
static
void getDupEdgeFaces(RuvmContext pContext, Cell **pCellStack,
                     int32_t cellStackPtr, Mesh *pMesh, int8_t *pFaceFlag) {
	Cell* pCell = pCellStack[cellStackPtr];
	int32_t ancestors[256];
	int32_t ancestorCount = 0;
	for (int32_t i = 0; i < cellStackPtr; ++i) {
		Cell* pAncestor = pCellStack[i];
		int32_t linked = 0;
		for (int32_t j = 0; j < pCell->linkEdgeSize; ++j) {
			if (pAncestor->cellIndex == pCell->pLinkEdges[j]) {
				linked = 1;
				break;
			}
		}
		if (!linked) {
			continue;
		}
		ancestors[ancestorCount] = i;
		ancestorCount++;
		for (int32_t j = 0; j < pAncestor->edgeFaceSize; ++j) {
			int32_t face = pAncestor->pEdgeFaces[j];
			int32_t faceStart = pMesh->mesh.pFaces[face];
			int32_t faceEnd = pMesh->mesh.pFaces[face + 1];
			int32_t faceLoopSize = faceEnd - faceStart;
			for (int32_t k = 0; k < faceLoopSize; ++k) {
				V2_F32* pVert = pMesh->pVerts + pMesh->mesh.pLoops[faceStart + k];
				if (_(*pVert V2GREAT pCell->boundsMin) &&
					_(*pVert V2LESS pCell->boundsMax)) {
					//offset index per cell
					int32_t index = pAncestor->edgeFaceSize + j;
					pFaceFlag[index] = 1;
					pCell->dupEdgeFaceCount++;
					break;
				}
			}
		}
	}
	if (pCell->dupEdgeFaceCount) {
		pCell->pDupEdgeFaces =
			pContext->alloc.pMalloc(sizeof(int32_t) * pCell->dupEdgeFaceCount);
	}
	else {
		return;
	}
	pCell->dupEdgeFaceCount = 0;
	for (int32_t i = 0; i < ancestorCount; ++i) {
		Cell* pAncestor = pCellStack[ancestors[i]];
		for (int32_t j = 0; j < pAncestor->edgeFaceSize; ++j) {
			int32_t index = pAncestor->edgeFaceSize + j;
			if (pFaceFlag[index]) {
				pCell->pDupEdgeFaces[pCell->dupEdgeFaceCount] =
					pAncestor->pEdgeFaces[j];
				pCell->dupEdgeFaceCount++;
				pFaceFlag[index] = 0;
			}
		}
	}
}
*/

static
void addEnclosedVertsToCell(RuvmContext pContext, Cell *pParentCell,
                            Mesh *pMesh, int8_t *pFaceFlag) {
	// Get enclosed verts if not already present
	// First, determine which verts are enclosed, and mark them by negating
	V2_F32 midPoint = pParentCell->pChildren[1].boundsMin;
	for (int32_t i = 0; i < pParentCell->faceSize; ++i) {
		int32_t face = pParentCell->pFaces[i];
		int32_t faceStart = pMesh->mesh.pFaces[face];
		int32_t faceEnd = pMesh->mesh.pFaces[face + 1];
		int32_t faceLoopSize = faceEnd - faceStart;
		V2_I32 signs;
		V2_I32 commonSides;
		int32_t result =
			findFaceQuadrant(&pContext->alloc, faceLoopSize, faceStart, pMesh,
			                 midPoint, &commonSides, &signs);
		if (result == 1) {
			int32_t child = signs.d[0] + signs.d[1] * 2;
			pFaceFlag[i] = child + 1;
			pParentCell->pChildren[child].faceSize++;
		}
		else {
			pFaceFlag[i] = -1;
			pParentCell->edgeFaceSize++;
		}
	}
	for (int32_t i = 0; i < 4; ++i) {
		Cell *cell = pParentCell->pChildren + i;
		if (cell->faceSize) {
			cell->pFaces =
				pContext->alloc.pMalloc(sizeof(int32_t) * cell->faceSize);
		}
	}
	if (pParentCell->edgeFaceSize) {
		pParentCell->pEdgeFaces =
			pContext->alloc.pMalloc(sizeof(int32_t) * pParentCell->edgeFaceSize);
	}
	int32_t facesSize[4] = {0};
	int32_t edgeFacesSize = 0;
	for (int32_t i = 0; i < pParentCell->faceSize; ++i) {
		if (!pFaceFlag[i]) {
			continue;
		}
		if (pFaceFlag[i] > 0) {
			int32_t child = pFaceFlag[i] - 1;
			Cell *cell = pParentCell->pChildren + child;
			cell->pFaces[facesSize[child]] = pParentCell->pFaces[i];
			facesSize[child]++;
		}
		else {
			pParentCell->pEdgeFaces[edgeFacesSize] = pParentCell->pFaces[i];
			edgeFacesSize++;
		}
		pFaceFlag[i] = 0;
	}
}

static
void allocateChildren(RuvmContext pContext, Cell *parentCell,
                      int32_t cellStackPtr, RuvmMap pMap) {
	parentCell->pChildren = pContext->alloc.pCalloc(4, sizeof(Cell));
	for (int32_t i = 0; i < 4; ++i) {
		// v for visualizing quadtree v
		//cell->children[i].cellIndex = rand();
		Cell *cell = parentCell->pChildren + i;
		cell->cellIndex = pMap->quadTree.cellCount;
		pMap->quadTree.cellCount++;
		cell->localIndex = (uint32_t)i;
		setCellBounds(cell, parentCell, cellStackPtr);
	}
	pMap->quadTree.leafCount += 4;
}


static
void processCell(RuvmContext pContext, Cell **pCellStack,
                 int32_t *pCellStackPointer, Mesh *pMesh, int8_t *pFaceFlag,
				 RuvmMap pMap) {
	Cell *cell = pCellStack[*pCellStackPointer];
	// If more than CELL_MAX_VERTS in cell, then subdivide cell
	int32_t hasChildren = cell->faceSize > CELL_MAX_VERTS;
	if (hasChildren) {
		// Get number of children
		int32_t childSize = 0;
		if (!cell->pChildren) {
			pMap->quadTree.leafCount--;
			allocateChildren(pContext, cell,*pCellStackPointer, pMap);
			addEnclosedVertsToCell(pContext, cell, pMesh, pFaceFlag);
			addLinkEdgesToCells(pContext, cell, pMesh, pCellStack,
			                    *pCellStackPointer);
		}
		for (int32_t i = 0; i < 4; ++i) {
			childSize += (int32_t)cell->pChildren[i].initialized;
		}
		// If the cell has children, and they are not yet all initialized,
		// then add the next one to the stack
		if (childSize < 4) {
			(*pCellStackPointer)++;
			pCellStack[*pCellStackPointer] = cell->pChildren + childSize;
			return;
		}
	}
	// Otherwise, set the current cell as initialized, and pop it off the stack
	cell->initialized = 1;
	(*pCellStackPointer)--;
}

static
int32_t calculateMaxTreeDepth(int32_t vertSize) {
	return log(CELL_MAX_VERTS * vertSize) / log(4) + 2;
}

void ruvmCreateQuadTree(RuvmContext pContext, RuvmMap pMap) {
	QuadTree *pQuadTree = &pMap->quadTree;
	Mesh *pMesh = &pMap->mesh;
	pQuadTree->cellCount = 0;
	pQuadTree->leafCount = 0;

	pQuadTree->pRootCell = pContext->alloc.pCalloc(1, sizeof(Cell));
	Cell *rootCell = pQuadTree->pRootCell;
	pQuadTree->maxTreeDepth = 32;

	Cell *cellStack[256];
	rootCell->cellIndex = 0;
	pQuadTree->cellCount = 1;
	cellStack[0] = rootCell;
	rootCell->boundsMax.d[0] = rootCell->boundsMax.d[1] = 1.0f;
	rootCell->initialized = 1;
	int8_t *pFaceFlag =
		pContext->alloc.pCalloc(pMesh->mesh.faceCount, sizeof(int8_t));
	rootCell->faceSize = pMesh->mesh.faceCount;
	rootCell->pFaces =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMesh->mesh.faceCount);
	for (int32_t i = 0; i < pMesh->mesh.faceCount; ++i) {
		rootCell->pFaces[i] = i;
	}
	allocateChildren(pContext, rootCell, 0, pMap);
	addEnclosedVertsToCell(pContext, rootCell, pMesh, pFaceFlag);
	addLinkEdgesToCells(pContext, rootCell, pMesh, cellStack, 0);
	cellStack[1] = rootCell->pChildren;
	int32_t cellStackPtr = 1;
	do {
		processCell(pContext, cellStack, &cellStackPtr, pMesh,
		            pFaceFlag, pMap);
	} while(cellStackPtr >= 0);
	pContext->alloc.pFree(pFaceFlag);
	printf("Created quadTree -- cells: %d, leaves: %d\n",
	       pQuadTree->cellCount, pQuadTree->leafCount);
}

void ruvmDestroyQuadTree(RuvmContext pContext, Cell *rootCell) {
	Cell *cellStack[32];
	cellStack[0] = rootCell;
	int32_t cellStackPtr = 0;
	do {
		Cell *cell = cellStack[cellStackPtr];
		int32_t nextChild = 0;
		if (cell->pChildren) {
			cell->initialized = 0;
			for (int32_t i = 0; i < 4; ++i) {
				nextChild += cell->pChildren[i].initialized == 0;
			}
			if (nextChild < 4) {
				cellStackPtr++;
				cellStack[cellStackPtr] = cell->pChildren + nextChild;
				continue;
			}
			pContext->alloc.pFree(cell->pChildren);
		}
		else {
			cell->initialized = 0;
		}
		if (cell->pLinkEdges) {
			pContext->alloc.pFree(cell->pLinkEdges);
		}
		if (cell->pLinkEdgeRanges) {
			pContext->alloc.pFree(cell->pLinkEdgeRanges);
		}
		if (cell->pFaces) {
			pContext->alloc.pFree(cell->pFaces);
		}
		if (cell->pEdgeFaces) {
			pContext->alloc.pFree(cell->pEdgeFaces);
		}
		cellStackPtr--;
	} while(cellStackPtr >= 0);
	pContext->alloc.pFree(rootCell);
}
