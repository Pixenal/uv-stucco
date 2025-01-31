#define STUC_CELL_STACK_SIZE 256

//TODO 

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <QuadTree.h>
#include <Context.h>
#include <MapFile.h>
#include <MathUtils.h>
#include <Utils.h>
#include <Error.h>

typedef struct {
	int32_t d[4];
} Children;

static
void calcCellBounds(Cell *cell) {
	float xSide = (float)(cell->localIdx % 2);
	float ySide = (float)(((cell->localIdx + 2) / 2) % 2);
	STUC_ASSERT("", isfinite(xSide) && isfinite(ySide));
	cell->boundsMin.d[0] = xSide * .5;
	cell->boundsMin.d[1] = ySide * .5;
	cell->boundsMax.d[0] = 1.0 - (1.0 - xSide) * .5;
	cell->boundsMax.d[1] = 1.0 - (1.0 - ySide) * .5;
	V2_F32 zero = {.0f, .0f};
	V2_F32 one = {1.0f, 1.0f};
	STUC_ASSERT("", _(cell->boundsMin V2GREATEQL zero));
	STUC_ASSERT("", _(cell->boundsMax V2LESSEQL one));
}

static
void addCellToEncasingCells(StucMap pMap, Cell *cell,
                            EncasingCells *pEncasingCells, int32_t edge) {
	STUC_ASSERT("", edge % 2 == edge);
	STUC_ASSERT("", cell->initialized % 2 == cell->initialized);
	int32_t faceSize = edge ? cell->edgeFaceSize : cell->faceSize;
	STUC_ASSERT("", faceSize >= 0 && faceSize < 100000000);
	STUC_ASSERT("", pEncasingCells->faceTotal >= 0);
	pEncasingCells->faceTotal += faceSize;
	int32_t dupIdx = -1;
	for (int32_t i = 0; i < pEncasingCells->cellSize; ++i) {
		STUC_ASSERT("", pMap->quadTree.cellTable.pArr[pEncasingCells->pCells[i]].localIdx >= 0);
		STUC_ASSERT("", pMap->quadTree.cellTable.pArr[pEncasingCells->pCells[i]].localIdx < 4);
		if (pEncasingCells->pCells[i] == cell->cellIdx) {
			dupIdx = i;
			break;
		}
		STUC_ASSERT("", i >= 0 && i < pEncasingCells->cellSize);
	}
	if (dupIdx >= 0) {
		if (!pEncasingCells->pCellType[dupIdx] && edge) {
			pEncasingCells->pCellType[dupIdx] = 2;
		}
		return;
	}
	pEncasingCells->pCells[pEncasingCells->cellSize] = cell->cellIdx;
	pEncasingCells->pCellType[pEncasingCells->cellSize] = edge;
	pEncasingCells->cellSize++;;
	pEncasingCells->faceTotalNoDup += faceSize;
}

static
int32_t findFaceQuadrantUv(StucAlloc* pAlloc, int32_t vertCount, V2_F32 *pVerts,
                           V2_F32 midPoint, V2_I32 *commonSides, V2_I32 *signs) {
	commonSides->d[0] = commonSides->d[1] = 1;
	V2_I32* pSides = pAlloc->pMalloc(sizeof(V2_I32) * vertCount);
	for (int32_t i = 0; i < vertCount; ++i) {
		STUC_ASSERT("", v2IsFinite(midPoint) && v2IsFinite(pVerts[i]));
		pSides[i].d[0] = pVerts[i].d[0] >= midPoint.d[0];
		pSides[i].d[1] = pVerts[i].d[1] < midPoint.d[1];
		for (int32_t j = 0; j < i; ++j) {
			commonSides->d[0] *= pSides[i].d[0] == pSides[j].d[0];
			commonSides->d[1] *= pSides[i].d[1] == pSides[j].d[1];
		}
	}
	STUC_ASSERT("", commonSides->d[0] % 2 == commonSides->d[0]);
	STUC_ASSERT("", commonSides->d[1] % 2 == commonSides->d[1]);
	if (!commonSides->d[0] && !commonSides->d[1]) {
		pAlloc->pFree(pSides);
		return 0;
	}
	STUC_ASSERT("", pSides->d[0] % 2 == pSides->d[0]);
	STUC_ASSERT("", pSides->d[1] % 2 == pSides->d[1]);
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
int32_t findFaceQuadrant(StucAlloc* pAlloc, FaceRange *pFace,
                         Mesh* pMesh, V2_F32 midPoint,
                         V2_I32* commonSides, V2_I32* signs) {
	commonSides->d[0] = commonSides->d[1] = 1;
	V2_I32* pSides = pAlloc->pMalloc(sizeof(V2_I32) * pFace->size);
	for (int32_t i = 0; i < pFace->size; ++i) {
		int32_t vertIdx = pMesh->core.pCorners[pFace->start + i];
		STUC_ASSERT("", v2IsFinite(midPoint) && v3IsFinite(pMesh->pVerts[vertIdx]));
		pSides[i].d[0] = pMesh->pVerts[vertIdx].d[0] >= midPoint.d[0];
		pSides[i].d[1] = pMesh->pVerts[vertIdx].d[1] < midPoint.d[1];
		for (int32_t j = 0; j < i; ++j) {
			commonSides->d[0] *= pSides[i].d[0] == pSides[j].d[0];
			commonSides->d[1] *= pSides[i].d[1] == pSides[j].d[1];
		}
	}
	STUC_ASSERT("", commonSides->d[0] % 2 == commonSides->d[0]);
	STUC_ASSERT("", commonSides->d[1] % 2 == commonSides->d[1]);
	if (!commonSides->d[0] && !commonSides->d[1]) {
		pAlloc->pFree(pSides);
		return 0;
	}
	STUC_ASSERT("", pSides->d[0] % 2 == pSides->d[0]);
	STUC_ASSERT("", pSides->d[1] % 2 == pSides->d[1]);
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
	STUC_ASSERT("", result >= 0 && result <= 2);
	switch (result) {
		case 0: {
			//addCellToEncasingCells(cell, pEncasingCells, 0);
			//*pCellStackPtr--;
			//pCellInits[cell->cellIdx] = 1;
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
void findEncasingChildCells(StucAlloc *pAlloc, Cell *pCell, Children *pChildren,
                            int32_t *pCellStackPtr, int32_t vertCount,
							V2_F32 *pVerts, V2_I32 *pTileMin) {
	V2_F32 zero = {.0f, .0f};
	V2_F32 one = {1.0f, 1.0f};
	STUC_ASSERT("", _(pCell->boundsMin V2GREATEQL zero));
	STUC_ASSERT("", _(pCell->boundsMax V2LESSEQL one));
	V2_F32 midPoint = _(_(pCell->boundsMax V2SUB pCell->boundsMin) V2MULS .5);
	_(&midPoint V2ADDEQL pCell->boundsMin);
	STUC_ASSERT("", v2IsFinite(midPoint));
	V2_I32 signs;
	V2_I32 commonSides;
	midPoint.d[0] += (float)pTileMin->d[0];
	midPoint.d[1] += (float)pTileMin->d[1];
	int32_t result =
		findFaceQuadrantUv(pAlloc, vertCount, pVerts, midPoint,
						   &commonSides, &signs);
	getChildrenFromResult(result, pChildren + *pCellStackPtr, &commonSides, &signs);
}

void stucGetAllEncasingCells(QuadTreeSearch *pState, EncasingCells *pEncasingCells,
                             int32_t vertCount, V2_F32 *pVerts, V2_I32 tileMin) {
	QuadTree *pTree = &pState->pMap->quadTree;
	Cell *cellStack[16];
	Children children[16];
	Cell *pRootCell = pState->pMap->quadTree.pRootCell;
	STUC_ASSERT("", pRootCell->cellIdx == 0 && pRootCell->localIdx == 0);
	cellStack[0] = pRootCell;
	pState->pCellInits[0] = 0;
	int32_t cellStackPtr = 0;
	for (int32_t i = 0; i < 4; ++i) {
		STUC_ASSERT("", pRootCell->pChildren[i].localIdx >= 0);
		STUC_ASSERT("", pRootCell->pChildren[i].localIdx < 4);
		pState->pCellInits[pRootCell->pChildren[i].cellIdx] = 0;
		STUC_ASSERT("", i >= 0 && i < 4);
	}
	do {
		STUC_ASSERT("", cellStackPtr >= 0 && cellStackPtr < 16);
		Cell *cell = cellStack[cellStackPtr];
		STUC_ASSERT("", cell->localIdx >= 0 && cell->localIdx < 4);
		STUC_ASSERT("", cell->cellIdx >= 0 && cell->cellIdx < pTree->cellCount);
		STUC_ASSERT("", (cell->pChildren && cell->faceSize > 0) || !cell->pChildren);
		STUC_ASSERT("", cell->initialized % 2 == cell->initialized);
		if (!cell->pChildren) {
			STUC_ASSERT("", !cell->edgeFaceSize);
			addCellToEncasingCells(pState->pMap, cell, pEncasingCells, 0);
			cellStackPtr--;
			STUC_ASSERT("", !pState->pCellInits[cell->cellIdx]);
			pState->pCellInits[cell->cellIdx] = 1;
			continue;
		}
		if (pState->pCellInits[cell->cellIdx]) {
			int32_t nextChild = -1;
			for (int32_t i = 0; i < 4; ++i) {
				if (!pState->pCellInits[cell->pChildren[i].cellIdx] &&
				    *((int32_t *)(children + cellStackPtr) + i)) {
					nextChild = i;
					break;
				}
				STUC_ASSERT("", i >= 0 && i < 4);
			}
			if (nextChild == -1) {
				cellStackPtr--;
				continue;
			}
			cellStackPtr++;
			cellStack[cellStackPtr] = cell->pChildren + nextChild;
			continue;
		}
		STUC_ASSERT("", vertCount >= 0 && vertCount < 10000);
		findEncasingChildCells(pState->pAlloc, cell, children, &cellStackPtr,
		                       vertCount, pVerts, &tileMin);
		addCellToEncasingCells(pState->pMap, cell, pEncasingCells, 1);
		STUC_ASSERT("", !pState->pCellInits[cell->cellIdx]);
		pState->pCellInits[cell->cellIdx] = 1;
		for (int32_t i = 0; i < 4; ++i) {
			pState->pCellInits[cell->pChildren[i].cellIdx] = 0;
			STUC_ASSERT("", i >= 0 && i < 4);
		}
		int32_t nextChild = 0;
		for (int32_t i = 0; i < 4; ++i) {
			if (*((int32_t *)(children + cellStackPtr) + i)) {
				nextChild = i;
				break;
			}
			STUC_ASSERT("", i >= 0 && i < 4);
		}
		cellStackPtr++;
		cellStack[cellStackPtr] = cell->pChildren + nextChild;
	} while (cellStackPtr >= 0);
}

int32_t checkIfFaceIsInsideTile(int32_t vertCount, V2_F32 *pVerts,
                                FaceBounds *pFaceBounds, V2_I32 tileMin) {
	int32_t isInsideBuf[4] = {1, 1, 1, 1};
	int32_t faceVertInside = 0;
	for (int32_t i = 0; i < vertCount; ++i) {
		//check if current edge intersects tile
		int32_t nexti = (i + 1) % vertCount;
		V2_F32 cornerDir = _(pVerts[nexti] V2SUB pVerts[i]);
		V2_F32 cornerCross = v2Cross(cornerDir);
		for (int32_t j = 0; j < 4; ++j) {
			V2_F32 cellPoint = {tileMin.d[0] + j % 2, tileMin.d[1] + j / 2};
			V2_F32 cellDir = _(cellPoint V2SUB pVerts[i]);
			float dot = _(cornerCross V2DOT cellDir);
			isInsideBuf[j] *= dot < .0f;
		}
		//in addition, test for face verts inside tile
		//edge cases may not be cause by the above,
		//like if a face entered the tile, and then exited the same side,
		//with a single vert in the tile. Checking for verts will catch this:
		faceVertInside += _(pVerts[i] V2GREAT pFaceBounds->fMin) &&
		                    _(pVerts[i] V2LESSEQL pFaceBounds->fMax);
	}
	int32_t isInside = isInsideBuf[0] || isInsideBuf[1] ||
						isInsideBuf[2] || isInsideBuf[3];
	int32_t isFullyEnclosed = isInsideBuf[0] && isInsideBuf[1] &&
								isInsideBuf[2] && isInsideBuf[3];
	if (isFullyEnclosed) {
		return 1;
	}
	if (!faceVertInside && !isInside) {
		//face is not inside current tile
		return 0;
	}
	return 2;
}

static
int32_t getCellsForFaceWithinTile(QuadTreeSearch *pState, int32_t vertCount,
                                  V2_F32 *pVerts, FaceBounds *pFaceBounds,
								  EncasingCells *pCellsBuf, V2_I32 tileMin) {
	//Don't check if in tile, if pFaceBounds is NULL
	if (pFaceBounds) {
		int32_t result =
			checkIfFaceIsInsideTile(vertCount, pVerts, pFaceBounds, tileMin);
		if (result != 2) {
			return result;
		}
	}
	//find fully encasing cell using clipped face
	stucGetAllEncasingCells(pState, pCellsBuf, vertCount, pVerts, tileMin);
	return 0;
}

static
int32_t checkBranchCellIsLinked(StucMap pMap, EncasingCells *pCellsBuf,
                                int32_t idx, Range *pRange) {
	int32_t linked = 0;
	int32_t cellIdx = pCellsBuf->pCells[idx];
	Cell *cell = pMap->quadTree.cellTable.pArr + cellIdx;
	for (int32_t j = 0; j < pCellsBuf->cellSize; ++j) {
		if (pCellsBuf->pCellType[j] || idx == j) {
			continue;
		}
		int32_t leafIdx = pCellsBuf->pCells[j];
		Cell *leaf = pMap->quadTree.cellTable.pArr + leafIdx;
		for (int32_t k = 0; k < leaf->linkEdgeSize; ++k) {
			if (cell->cellIdx == leaf->pLinkEdges[k]) {
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
void removeNonLinkedBranchCells(StucMap pMap, EncasingCells *pCellsBuf) {
	for (int32_t i = 0; i < pCellsBuf->cellSize;) {
		if (!pCellsBuf->pCellType[i]) {
			i++;
			continue;
		}
		Range range = {.start = INT32_MAX, .end = INT32_MIN};
		if (checkBranchCellIsLinked(pMap, pCellsBuf, i, &range)) {
			pCellsBuf->pRangeBuf[i] = range;
			i++;
			continue;
		}
		int32_t cellIdx = pCellsBuf->pCells[i];
		Cell *pCell = pMap->quadTree.cellTable.pArr + cellIdx;
		pCellsBuf->faceTotal -= pCell->edgeFaceSize;
		pCellsBuf->faceTotalNoDup -= pCell->edgeFaceSize;
		for (int32_t j = i; j < pCellsBuf->cellSize - 1; ++j) {
			pCellsBuf->pCells[j] = pCellsBuf->pCells[j + 1];
			pCellsBuf->pCellType[j] = pCellsBuf->pCellType[j + 1];
		}
		pCellsBuf->cellSize--;
	}
}

static
void copyCellsIntoTotalList(StucAlloc *pAlloc, FaceCellsTable *pFaceCellsTable,
                            EncasingCells *pCellsBuf, int32_t faceIdx) {
	FaceCells *pEntry = pFaceCellsTable->pFaceCells + faceIdx;
	pFaceCellsTable->cellFacesTotal += pCellsBuf->faceTotalNoDup;
	pEntry->pCells = pAlloc->pMalloc(sizeof(int32_t) * pCellsBuf->cellSize);
	pEntry->pCellType = pAlloc->pMalloc(pCellsBuf->cellSize);
	memcpy(pEntry->pCells, pCellsBuf->pCells,
	       sizeof(int32_t) * pCellsBuf->cellSize);
	memcpy(pEntry->pCellType, pCellsBuf->pCellType, pCellsBuf->cellSize);
	pEntry->cellSize = pCellsBuf->cellSize;
	pEntry->faceSize = pCellsBuf->faceTotalNoDup;
	pEntry->pRanges = pCellsBuf->pRangeBuf;
	pCellsBuf->pRangeBuf = NULL;
	if (pCellsBuf->faceTotalNoDup > pFaceCellsTable->cellFacesMax) {
		pFaceCellsTable->cellFacesMax = pCellsBuf->faceTotalNoDup;
	}
}

static
void recordCellsInTable(QuadTreeSearch *pState, FaceCellsTable *pFaceCellsTable,
                        EncasingCells *pCellsBuf) {
	for (int32_t i = 0; i < pCellsBuf->cellSize; ++i) {
		int32_t cellIdx = pCellsBuf->pCells[i];
		Cell *pCell = pState->pMap->quadTree.cellTable.pArr + cellIdx;
		//must be != 0, not > 0, so as to catch entries set to -1
		if (pState->pCellFlags[pCell->cellIdx] != 0) {
			continue;
		}
		int32_t cellType = pCellsBuf->pCellType[i];
		pState->pCellFlags[pCell->cellIdx] = cellType + 1;
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
			int32_t childType = pState->pCellFlags[pChild->cellIdx];
			if (pChild != pCell) {
				//must be > 0, so that cells with an entry of -1 arn't touched,
				// as they haven't been added to uniqueFaces
				if (childType > 0) {
					pFaceCellsTable->uniqueFaces -= childType == 2 ?
						pChild->edgeFaceSize : pChild->faceSize;
				}
				//set to -1 so this cell isn't added to the count in future
				pState->pCellFlags[pChild->cellIdx] = -1;
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

void stucInitFaceCellsTable(StucAlloc *pAlloc, FaceCellsTable *pTable,
                            int32_t faceCount) {
	pTable->pFaceCells = pAlloc->pMalloc(sizeof(FaceCells) * faceCount);
}

void stucDestroyFaceCellsTable(StucAlloc *pAlloc,
                               FaceCellsTable *pFaceCellsTable) {
	pAlloc->pFree(pFaceCellsTable->pFaceCells);
}

void stucDestroyFaceCellsEntry(StucAlloc *pAlloc, int32_t i,
                               FaceCellsTable *pFaceCellsTable) {
	pAlloc->pFree(pFaceCellsTable->pFaceCells[i].pCells);
	pAlloc->pFree(pFaceCellsTable->pFaceCells[i].pCellType);
	//TODO segfault when mapping a single quad larger than the 0-1 uv tile
	//i've not really tested the code thats supposed to handle this case,
	//so no surprise it crashes
	pAlloc->pFree(pFaceCellsTable->pFaceCells[i].pRanges);
}

void stucInitQuadTreeSearch(StucAlloc *pAlloc, StucMap pMap, QuadTreeSearch *pState) {
	pState->pAlloc = pAlloc;
	pState->pMap = pMap;
	pState->pCellInits = pAlloc->pMalloc(pMap->quadTree.cellCount);
	pState->pCellFlags = pAlloc->pCalloc(pMap->quadTree.cellCount, sizeof(int8_t));
	pState->pCells = pAlloc->pMalloc(sizeof(int32_t) * pMap->quadTree.cellCount);
	pState->pCellType = pAlloc->pMalloc(pMap->quadTree.cellCount);
}

void stucDestroyQuadTreeSearch(QuadTreeSearch *pState) {
	pState->pAlloc->pFree(pState->pCellFlags);
	pState->pAlloc->pFree(pState->pCellInits);
	pState->pAlloc->pFree(pState->pCells);
	pState->pAlloc->pFree(pState->pCellType);
}

void stucGetCellsForSingleFace(QuadTreeSearch *pState, int32_t vertCount,
                               V2_F32 *pVerts, FaceCellsTable *pFaceCellsTable,
							   FaceBounds *pFaceBounds, int32_t faceIdx) {
	EncasingCells cellsBuf = {0};
	cellsBuf.pCells = pState->pCells;
	cellsBuf.pCellType = pState->pCellType;
	FaceCells *pEntry = pFaceCellsTable->pFaceCells + faceIdx;
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
				                          &cellsBuf, tileMin)) {
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
	cellsBuf.pRangeBuf =
		pState->pAlloc->pMalloc(sizeof(Range) * cellsBuf.cellSize);
	removeNonLinkedBranchCells(pState->pMap, &cellsBuf);
	recordCellsInTable(pState, pFaceCellsTable, &cellsBuf);
	copyCellsIntoTotalList(pState->pAlloc, pFaceCellsTable, &cellsBuf,
	                       faceIdx);
}

void stucLinearizeCellFaces(FaceCells *pFaceCells, int32_t *pCellFaces,
                            int32_t faceIdx) {
	int32_t facesNextIdx = 0;
	for (int32_t j = 0; j < pFaceCells[faceIdx].cellSize; ++j) {
		Cell *cell = pFaceCells[faceIdx].pCells[j];
		if (pFaceCells[faceIdx].pCellType[j]) {
			memcpy(pCellFaces + facesNextIdx, cell->pEdgeFaces,
					sizeof(int32_t) * cell->edgeFaceSize);
			facesNextIdx += cell->edgeFaceSize;
		}
		if (pFaceCells[faceIdx].pCellType[j] != 1) {
			memcpy(pCellFaces + facesNextIdx, cell->pFaces,
					sizeof(int32_t) * cell->faceSize);
			facesNextIdx += cell->faceSize;
		}
	}
}

Cell *stucFindEncasingCell(Cell *rootCell, V2_F32 pos) {
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
		int32_t childIdx = (pos.d[0] >=
				midPoint.d[0]) + (pos.d[1] < midPoint.d[1]) * 2;
		cell = cell->pChildren + childIdx;
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
	V2_F32 zero = {.0f, .0f};
	V2_F32 one = {1.0f, 1.0f};
	STUC_ASSERT("", _(cell->boundsMin V2GREATEQL zero));
	STUC_ASSERT("", _(cell->boundsMax V2LESSEQL one));
}

static
int32_t checkIfLinkedEdge(Cell *pChild, Cell *pAncestor, Mesh *pMesh, Range *pRange) {
	int32_t linked = 0;
	for (int32_t i = 0; i < pAncestor->edgeFaceSize; ++i) {
		int32_t faceIdx = pAncestor->pEdgeFaces[i];
		FaceRange face = getFaceRange(&pMesh->core, faceIdx, false);
		//doesn't catch cases where edge intersect with bounds,
		//replace with a better method
		if (checkFaceIsInBounds(pChild->boundsMin, pChild->boundsMax,
			                    face, pMesh)) {
			if (!linked) {
				linked = 1;
				pRange->start = i;
			}
			pRange->end = i;
		}
		STUC_ASSERT("", i >= 0 && i < pAncestor->edgeFaceSize);
	}
	STUC_ASSERT("", pRange->start >= 0 && pRange->end <= pAncestor->edgeFaceSize);
	pRange->end++;
	return linked;
}

static
void addLinkEdgesToCells(StucContext pContext, int32_t parentCell,
                         Mesh *pMesh, CellTable *pTable, int32_t *pCellStack,
						 int32_t cellStackPtr) {
	int32_t buf[32];
	Range rangeBuf[32];
	int32_t bufSize;
	for (int32_t i = 0; i < 4; ++i) {
		bufSize = 0;
		Cell *pChild = pTable->pArr[parentCell].pChildren + i;
		STUC_ASSERT("", pChild->initialized == 0);
		STUC_ASSERT("", pChild->localIdx >= 0 && pChild->localIdx < 4);
		for (int32_t j = 0; j <= cellStackPtr; ++j) {
			Cell *pAncestor = pTable->pArr + pCellStack[j];
			STUC_ASSERT("", pAncestor->initialized % 2 == pAncestor->initialized);
			STUC_ASSERT("", pAncestor->localIdx >= 0);
			STUC_ASSERT("", pAncestor->localIdx < 4);
			Range range = {0};
			if (checkIfLinkedEdge(pChild, pAncestor, pMesh, &range)) {
				buf[bufSize] = pAncestor->cellIdx;
				rangeBuf[bufSize] = range;
				bufSize++;
			}
			STUC_ASSERT("", j >= 0 && j <= cellStackPtr);
		}
		if (bufSize) {
			pChild->pLinkEdges =
				pContext->alloc.pMalloc(sizeof(int32_t) * bufSize);
			memcpy(pChild->pLinkEdges, buf, sizeof(int32_t) * bufSize);
			//Add link edge ranges
			pChild->pLinkEdgeRanges =
				pContext->alloc.pMalloc(sizeof(Range) * bufSize);
				memcpy(pChild->pLinkEdgeRanges, rangeBuf, sizeof(Range) * bufSize);
			pChild->linkEdgeSize = bufSize;
		}
		STUC_ASSERT("", i >= 0 && i < 4);
	}
}

/*
static
void getDupEdgeFaces(StucContext pContext, Cell **pCellStack,
                     int32_t cellStackPtr, Mesh *pMesh, int8_t *pFaceFlag) {
	Cell* pCell = pCellStack[cellStackPtr];
	int32_t ancestors[256];
	int32_t ancestorCount = 0;
	for (int32_t i = 0; i < cellStackPtr; ++i) {
		Cell* pAncestor = pCellStack[i];
		int32_t linked = 0;
		for (int32_t j = 0; j < pCell->linkEdgeSize; ++j) {
			if (pAncestor->cellIdx == pCell->pLinkEdges[j]) {
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
			int32_t faceStart = pMesh->core.pFaces[face];
			int32_t faceEnd = pMesh->core.pFaces[face + 1];
			int32_t faceCornerSize = faceEnd - faceStart;
			for (int32_t k = 0; k < faceCornerSize; ++k) {
				V2_F32* pVert = pMesh->pVerts + pMesh->core.pCorners[faceStart + k];
				if (_(*pVert V2GREAT pCell->boundsMin) &&
					_(*pVert V2LESS pCell->boundsMax)) {
					//offset index per cell
					int32_t idx = pAncestor->edgeFaceSize + j;
					pFaceFlag[idx] = 1;
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
			int32_t idx = pAncestor->edgeFaceSize + j;
			if (pFaceFlag[idx]) {
				pCell->pDupEdgeFaces[pCell->dupEdgeFaceCount] =
					pAncestor->pEdgeFaces[j];
				pCell->dupEdgeFaceCount++;
				pFaceFlag[idx] = 0;
			}
		}
	}
}
*/

static
void addEnclosedVertsToCell(StucContext pContext, int32_t parentCellIdx,
                            Mesh *pMesh, CellTable* pTable, int8_t *pFaceFlag) {
	// Get enclosed verts if not already present
	// First, determine which verts are enclosed, and mark them by negating
	Cell* pParentCell = pTable->pArr + parentCellIdx;
	V2_F32 midPoint = pParentCell->pChildren[1].boundsMin;
	STUC_ASSERT("", v2IsFinite(midPoint));
	STUC_ASSERT("", midPoint.d[0] < 1.0f && midPoint.d[1] < 1.0f);
	STUC_ASSERT("", midPoint.d[0] > .0f && midPoint.d[1] > .0f);
	for (int32_t i = 0; i < pParentCell->faceSize; ++i) {
		FaceRange face = getFaceRange(&pMesh->core, pParentCell->pFaces[i], false);
		V2_I32 signs;
		V2_I32 commonSides;
		int32_t result =
			findFaceQuadrant(&pContext->alloc, &face, pMesh,
			                 midPoint, &commonSides, &signs);
		if (result == 1) {
			int32_t childIdx = signs.d[0] + signs.d[1] * 2;
			Cell *pChild = pParentCell->pChildren + childIdx;
			pFaceFlag[i] = childIdx + 1;
			STUC_ASSERT("", pChild->faceSize >= 0);
			STUC_ASSERT("", pChild->faceSize <= pParentCell->faceSize);
			pChild->faceSize++;
		}
		else {
			pFaceFlag[i] = -1;
			STUC_ASSERT("", pParentCell->edgeFaceSize >= 0);
			STUC_ASSERT("", pParentCell->edgeFaceSize <= pParentCell->faceSize);
			pParentCell->edgeFaceSize++;
		}
		STUC_ASSERT("", i >= 0 && i < pParentCell->faceSize);
	}
	for (int32_t i = 0; i < 4; ++i) {
		Cell *cell = pParentCell->pChildren + i;
		STUC_ASSERT("", cell->initialized == 0 && cell->localIdx == i);
		STUC_ASSERT("", cell->faceSize >= 0);
		STUC_ASSERT("", cell->faceSize <= pParentCell->faceSize);
		if (cell->faceSize) {
			cell->pFaces =
				pContext->alloc.pMalloc(sizeof(int32_t) * cell->faceSize);
		}
		STUC_ASSERT("", i >= 0 && i < 4);
	}
	STUC_ASSERT("", pParentCell->edgeFaceSize >= 0);
	STUC_ASSERT("", pParentCell->edgeFaceSize <= pParentCell->faceSize);
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
		STUC_ASSERT("", pFaceFlag[i] <= 5);
		if (pFaceFlag[i] > 0) {
			int32_t child = pFaceFlag[i] - 1;
			Cell *cell = pParentCell->pChildren + child;
			STUC_ASSERT("", cell->initialized == 0 && cell->localIdx < 4);
			STUC_ASSERT("", cell->faceSize >= 0);
			STUC_ASSERT("", cell->faceSize <= pParentCell->faceSize);
			cell->pFaces[facesSize[child]] = pParentCell->pFaces[i];
			facesSize[child]++;
		}
		else {
			STUC_ASSERT("", pFaceFlag[i] == -1);
			STUC_ASSERT("", pParentCell->edgeFaceSize >= 0);
			STUC_ASSERT("", pParentCell->edgeFaceSize <= pParentCell->faceSize);
			pParentCell->pEdgeFaces[edgeFacesSize] = pParentCell->pFaces[i];
			edgeFacesSize++;
		}
		pFaceFlag[i] = 0;
		STUC_ASSERT("", i >= 0 && i < pParentCell->faceSize);
	}
	STUC_ASSERT("", pParentCell->initialized % 2 == pParentCell->initialized);
}


static
void updateCellPtrs(QuadTree *pTree, Cell *pOldPtr) {
	for (int32_t i = 0; i < pTree->cellCount; ++i) {
		Cell *pCell = pTree->cellTable.pArr + i;
		if (pCell->pChildren) {
			int64_t offset = pCell->pChildren - pOldPtr;
			pCell->pChildren = pTree->cellTable.pArr + offset;
		}
	}
}

static
void reallocCellTable(const StucContext pContext, QuadTree *pTree,
                      const int32_t sizeDiff) {
	if (!sizeDiff) {
		return;
	}
	pTree->cellTable.size += sizeDiff;
	STUC_ASSERT("", pTree->cellTable.size > 0);
	Cell *pOldPtr = pTree->cellTable.pArr;
	pTree->cellTable.pArr =
		pContext->alloc.pRealloc(pTree->cellTable.pArr,
								 sizeof(Cell) * pTree->cellTable.size);
	if (sizeDiff > 0) {
		memset(pTree->cellTable.pArr + pTree->cellCount, 0,
			   sizeof(Cell) * sizeDiff);
	}
	updateCellPtrs(pTree, pOldPtr);
	pTree->pRootCell = pTree->cellTable.pArr;
}

static
void allocateChildren(StucContext pContext, int32_t parentCell,
                      int32_t cellStackPtr, StucMap pMap) {
	QuadTree *pTree = &pMap->quadTree;
	STUC_ASSERT("", pTree->cellCount > 0);
	STUC_ASSERT("", pTree->cellCount <= pTree->cellTable.size);
	if (pTree->cellCount + 4 > pTree->cellTable.size) {
		int32_t sizeIncrease = (pTree->cellTable.size + 4) * 2;
		reallocCellTable(pContext, pTree, sizeIncrease);
	}
	STUC_ASSERT("", pTree->cellTable.size >= pTree->cellCount + 4);
	STUC_ASSERT("", pTree->cellTable.pArr[0].initialized == 1);
	STUC_ASSERT("", !pTree->cellTable.pArr[0].cellIdx);
	STUC_ASSERT("", !pTree->cellTable.pArr[0].localIdx);
	pTree->cellTable.pArr[parentCell].pChildren = pTree->cellTable.pArr + pTree->cellCount;
	for (int32_t i = 0; i < 4; ++i) {
		// v for visualizing quadtree v
		//cell->children[i].cellIdx = rand();
		Cell *cell = pTree->cellTable.pArr[parentCell].pChildren + i;
		STUC_ASSERT("", !cell->initialized);
		cell->cellIdx = pMap->quadTree.cellCount;
		pTree->cellCount++;
		cell->localIdx = (uint32_t)i;
		setCellBounds(cell, pTree->cellTable.pArr + parentCell, cellStackPtr);
		STUC_ASSERT("", i >= 0 && i < 4);
	}
	pMap->quadTree.leafCount += 4;
}

static
void processCell(StucContext pContext, int32_t *pCellStack,
                 int32_t *pCellStackPtr, Mesh *pMesh, int8_t *pFaceFlag,
				 StucMap pMap, int32_t *pProgress) {
	QuadTree *pTree = &pMap->quadTree;
	CellTable *pCellTable = &pTree->cellTable;
	int32_t cell = pCellStack[*pCellStackPtr];
	STUC_ASSERT("", pCellTable->pArr[cell].initialized % 2 ==
	                pCellTable->pArr[cell].initialized);
	STUC_ASSERT("", pCellTable->pArr[cell].cellIdx >= 0 &&
	                pCellTable->pArr[cell].cellIdx < pTree->cellCount);
	STUC_ASSERT("", pCellTable->pArr[cell].localIdx >= 0 &&
	                pCellTable->pArr[cell].localIdx < 4);
	STUC_ASSERT("", pCellTable->pArr[cell].faceSize >= 0 &&
	                pCellTable->pArr[cell].faceSize < 100000000);
	// If more than CELL_MAX_VERTS in cell, then subdivide cell
	int32_t hasChildren = pCellTable->pArr[cell].faceSize > CELL_MAX_VERTS;
	if (hasChildren) {
		// Get number of children
		int32_t childSize = 0;
		if (!pCellTable->pArr[cell].pChildren) {
			pMap->quadTree.leafCount--;
			allocateChildren(pContext, cell, *pCellStackPtr, pMap);
			addEnclosedVertsToCell(pContext, cell, pMesh, &pTree->cellTable, pFaceFlag);
			addLinkEdgesToCells(pContext, cell, pMesh, &pTree->cellTable,
			                    pCellStack, *pCellStackPtr);
		}
		STUC_ASSERT("", childSize >= 0);
		for (int32_t i = 0; i < 4; ++i) {
			childSize += (int32_t)pCellTable->pArr[cell].pChildren[i].initialized;
			STUC_ASSERT("", i >= 0 && i < 4);
		}
		// If the cell has children, and they are not yet all initialized,
		// then add the next one to the stack
		if (childSize < 4) {
			(*pCellStackPtr)++;
			pCellStack[*pCellStackPtr] = pCellTable->pArr[cell].pChildren[childSize].cellIdx;
			return;
		}
	}
	// Otherwise, set the current cell as initialized, and pop it off the stack
	pCellTable->pArr[cell].initialized = 1;
	if (*pCellStackPtr == 2) {
		*pProgress += pContext->stageReport.outOf / 16;
		stageProgressWrap(pContext, *pProgress);
	}
	(*pCellStackPtr)--;
}

static
int32_t calculateMaxTreeDepth(int32_t vertSize) {
	return log(CELL_MAX_VERTS * vertSize) / log(4) + 2;
}

static
Result initRootAndChildren(StucContext pContext, int32_t *pCellStack, 
                         QuadTree *pTree, StucMap pMap, Mesh *pMesh,
						 int8_t *pFaceFlag) {
	pTree->maxTreeDepth = 32; //TODO what is this for again? isn't this obsolete?

	CellTable* pTable = &pTree->cellTable;
	Cell *pRoot = pTable->pArr;
	pRoot->cellIdx = 0;
	pTree->cellCount = 1;
	pCellStack[0] = 0;
	pRoot->boundsMax.d[0] = pRoot->boundsMax.d[1] = 1.0f;
	pRoot->initialized = 1;
	pRoot->pFaces =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMesh->core.faceCount);
	for (int32_t i = 0; i < pMesh->core.faceCount; ++i) {
		FaceRange face = getFaceRange(&pMesh->core, i, false);
		bool inside = false;
		for (int32_t j = 0; j < face.size; ++j) {
			V3_F32 vert = pMesh->pVerts[pMesh->core.pCorners[face.start + j]];
			if (vert.d[0] > .0f && vert.d[0] < 1.0f &&
				vert.d[1] > .0f && vert.d[1] < 1.0f) {
				inside = true;
				break;
			}
		}
		if (inside) {
			pRoot->pFaces[pRoot->faceSize] = i;
			pRoot->faceSize++;
		}
	}
	STUC_ASSERT("", pRoot->faceSize >= 0);
	if (pRoot->faceSize == 0) {
		return STUC_ERROR; //all faces are outside of 0-1 tile
	}
	allocateChildren(pContext, 0, 0, pMap);
	addEnclosedVertsToCell(pContext, 0, pMesh, pTable, pFaceFlag);
	addLinkEdgesToCells(pContext, 0, pMesh, pTable,
	                    pCellStack, 0);
	pCellStack[1] = 1;
	return STUC_SUCCESS;
}

Result stucCreateQuadTree(StucContext pContext, StucMap pMap) {
	Result err = STUC_NOT_SET;
	QuadTree *pTree = &pMap->quadTree;
	STUC_ASSERT("", pMap->mesh.core.faceCount > 0);
	stageBeginWrap(pContext, "Creating quad tree", pContext->stageReport.outOf);
	pTree->cellTable.size = pMap->mesh.core.faceCount / CELL_MAX_VERTS + 1;
	pTree->cellTable.pArr =
		pContext->alloc.pCalloc(pTree->cellTable.size, sizeof(Cell));
	Mesh *pMesh = &pMap->mesh;
	pTree->cellCount = 0;
	pTree->leafCount = 0;
	int8_t *pFaceFlag =
		pContext->alloc.pCalloc(pMesh->core.faceCount, sizeof(int8_t));

	pTree->pRootCell = pTree->cellTable.pArr;
	int32_t cellStack[STUC_CELL_STACK_SIZE];
	err =  initRootAndChildren(pContext, cellStack, pTree,
	                                         pMap, pMesh, pFaceFlag);
	STUC_ERROR("All faces were outside 0-1 tile", err);
	int32_t cellStackPtr = 1;
	int32_t progress = 0;
	do {
		STUC_ASSERT("", cellStackPtr < STUC_CELL_STACK_SIZE);
		processCell(pContext, cellStack, &cellStackPtr, pMesh,
		            pFaceFlag, pMap, &progress);
	} while(cellStackPtr >= 0);
	STUC_ASSERT("", pTree->cellCount <= pTree->cellTable.size);
	STUC_ASSERT("", pTree->pRootCell->initialized == 1);
	int32_t sizeDecrease = pTree->cellCount - pTree->cellTable.size;
	STUC_ASSERT("", sizeDecrease < 0);
	printf("Created quadTree -- cells: %d, leaves: %d\n",
		pTree->cellCount, pTree->leafCount);
	reallocCellTable(pContext, pTree, sizeDecrease);
	stageEndWrap(pContext);
	STUC_CATCH(err, ;)
	pContext->alloc.pFree(pFaceFlag);
	return err;
}

void stucDestroyQuadTree(StucContext pContext, QuadTree *pTree) {
	for (int32_t i = 0; i < pTree->cellCount; ++i) {
		Cell *cell = pTree->cellTable.pArr + i;
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
	}
	pContext->alloc.pFree(pTree->cellTable.pArr);
}

void getFaceBoundsForTileTest(FaceBounds *pFaceBounds,
                              Mesh *pMesh, FaceRange *pFace) {
	getFaceBounds(pFaceBounds, pMesh->pUvs, *pFace);
	pFaceBounds->fMinSmall = pFaceBounds->fMin;
	pFaceBounds->fMaxSmall = pFaceBounds->fMax;
	pFaceBounds->min = v2FloorAssign(&pFaceBounds->fMin);
	pFaceBounds->max = v2FloorAssign(&pFaceBounds->fMax);
	_(&pFaceBounds->fMax V2ADDEQLS 1.0f);
}

void getEncasingCells(StucAlloc *pAlloc, StucMap pMap,
                      Mesh *pMesh, FaceCellsTable *pFaceCellsTable,
					  int32_t *pAverageMapFacesPerFace) {
	*pAverageMapFacesPerFace = 0;
	stucInitFaceCellsTable(pAlloc, pFaceCellsTable, pMesh->core.faceCount);
	QuadTreeSearch searchState = {0};
	stucInitQuadTreeSearch(pAlloc, pMap, &searchState);
	for (int32_t i = 0; i < pMesh->core.faceCount; ++i) {
		FaceRange faceInfo = getFaceRange(pMesh, i, false);
		FaceBounds faceBounds = {0};
		getFaceBoundsForTileTest(&faceBounds, pMesh, &faceInfo);
		V2_F32 *pVertBuf = pAlloc->pMalloc(sizeof(V2_F32) * faceInfo.size);
		for (int32_t j = 0; j < faceInfo.size; ++j) {
			pVertBuf[j] = pMesh->pUvs[faceInfo.start + j];
		}
		stucGetCellsForSingleFace(&searchState, faceInfo.size, pVertBuf,
			                      pFaceCellsTable, &faceBounds, i);
		pAlloc->pFree(pVertBuf);
		*pAverageMapFacesPerFace += pFaceCellsTable->pFaceCells[i].faceSize;
		//printf("Total cell amount: %d\n", faceCellsInfo[i].cellSize);
	}
	*pAverageMapFacesPerFace /= pMesh->core.faceCount;
	stucDestroyQuadTreeSearch(&searchState);
}
