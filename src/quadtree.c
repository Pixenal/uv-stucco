#define STUC_CELL_STACK_SIZE 16

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <quadtree.h>
#include <context.h>
#include <map.h>
#include <math_utils.h>
#include <utils.h>
#include <error.h>

typedef struct {
	I32 d[4];
} Children;

static
void calcCellBounds(Cell *cell) {
	F32 xSide = (F32)(cell->localIdx % 2);
	F32 ySide = (F32)(((cell->localIdx + 2) / 2) % 2);
	STUC_ASSERT("", isfinite(xSide) && isfinite(ySide));
	cell->boundsMin.d[0] = xSide * .5f;
	cell->boundsMin.d[1] = ySide * .5f;
	cell->boundsMax.d[0] = 1.0f - (1.0f - xSide) * .5f;
	cell->boundsMax.d[1] = 1.0f - (1.0f - ySide) * .5f;
	V2_F32 zero = {.0f, .0f};
	V2_F32 one = {1.0f, 1.0f};
	STUC_ASSERT("", _(cell->boundsMin V2GREATEQL zero));
	STUC_ASSERT("", _(cell->boundsMax V2LESSEQL one));
}

static
Result addCellToEncasingCells(
	StucMap pMap,
	Cell *cell,
	EncasingCells *pEncasingCells,
	I32 edge
) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("", edge % 2 == edge);
	STUC_ASSERT("", cell->initialized % 2 == cell->initialized);
	I32 faceSize = edge ? cell->edgeFaceSize : cell->faceSize;
	STUC_ASSERT("", pEncasingCells->faceTotal >= 0);
	pEncasingCells->faceTotal += faceSize;
	I32 dupIdx = -1;
	for (I32 i = 0; i < pEncasingCells->cellSize; ++i) {
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
		return err;
	}
	pEncasingCells->pCells[pEncasingCells->cellSize] = cell->cellIdx;
	pEncasingCells->pCellType[pEncasingCells->cellSize] = (I8)edge;
	pEncasingCells->cellSize++;;
	pEncasingCells->faceTotalNoDup += faceSize;
	return err;
}

static
Result findFaceQuadrantUv(
	I32 vertCount,
	V2_F32 *pVerts,
	V2_F32 midPoint,
	V2_I32 *commonSides,
	V2_I32 *signs,
	I32 *pResult
) {
	Result err = STUC_SUCCESS;
	commonSides->d[0] = commonSides->d[1] = 1;
	STUC_RETURN_ERR_IFNOT_COND(err, vertCount <= 4, "");
	V2_I32 sides[4] = {0};
	for (I32 i = 0; i < vertCount; ++i) {
		STUC_ASSERT("", v2F32IsFinite(midPoint) && v2F32IsFinite(pVerts[i]));
		sides[i].d[0] = pVerts[i].d[0] >= midPoint.d[0];
		sides[i].d[1] = pVerts[i].d[1] < midPoint.d[1];
		for (I32 j = 0; j < i; ++j) {
			commonSides->d[0] *= sides[i].d[0] == sides[j].d[0];
			commonSides->d[1] *= sides[i].d[1] == sides[j].d[1];
		}
	}
	STUC_ASSERT("", commonSides->d[0] % 2 == commonSides->d[0]);
	STUC_ASSERT("", commonSides->d[1] % 2 == commonSides->d[1]);
	if (!commonSides->d[0] && !commonSides->d[1]) {
		*pResult = 0;
		return err;
	}
	STUC_ASSERT("", sides->d[0] % 2 == sides->d[0]);
	STUC_ASSERT("", sides->d[1] % 2 == sides->d[1]);
	signs->d[0] = sides[0].d[0];
	signs->d[1] = sides[0].d[1];
	if (commonSides->d[0] && commonSides->d[1]) {
		*pResult = 1;
		return err;
	}
	*pResult = 2;
	return err;
}

static
I32 findFaceQuadrant(
	const StucAlloc *pAlloc,
	FaceRange *pFace,
	const Mesh *pMesh,
	V2_F32 midPoint,
	V2_I32 *commonSides,
	V2_I32 *signs
) {
	commonSides->d[0] = commonSides->d[1] = 1;
	V2_I32* pSides = pAlloc->pMalloc(sizeof(V2_I32) * pFace->size);
	for (I32 i = 0; i < pFace->size; ++i) {
		I32 vertIdx = pMesh->core.pCorners[pFace->start + i];
		STUC_ASSERT("", v2F32IsFinite(midPoint) && v3F32IsFinite(pMesh->pVerts[vertIdx]));
		pSides[i].d[0] = pMesh->pVerts[vertIdx].d[0] >= midPoint.d[0];
		pSides[i].d[1] = pMesh->pVerts[vertIdx].d[1] < midPoint.d[1];
		for (I32 j = 0; j < i; ++j) {
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
Result getChildrenFromResult(
	I32 result,
	Children *pChildren,
	V2_I32 *pCommonSides,
	V2_I32 *pSigns
) {
	Result err = STUC_SUCCESS;
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
			I32 top, bottom = top = pCommonSides->d[1];
			I32 left, right = left = pCommonSides->d[0];
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
	return err;
}

static
Result findEncasingChildCells(
	Cell *pCell,
	Children *pChildren,
	I32 *pCellStackPtr,
	I32 vertCount,
	V2_F32 *pVerts,
	V2_I32 *pTileMin
) {
	Result err = STUC_SUCCESS;
	V2_F32 zero = {.0f, .0f};
	V2_F32 one = {1.0f, 1.0f};
	STUC_THROW_IFNOT_COND(err, _(pCell->boundsMin V2GREATEQL zero), "", 0);
	STUC_THROW_IFNOT_COND(err, _(pCell->boundsMax V2LESSEQL one), "", 0);
	V2_F32 midPoint = _(_(pCell->boundsMax V2SUB pCell->boundsMin) V2MULS .5);
	_(&midPoint V2ADDEQL pCell->boundsMin);
	STUC_ASSERT("", v2F32IsFinite(midPoint));
	V2_I32 signs;
	V2_I32 commonSides;
	midPoint.d[0] += (F32)pTileMin->d[0];
	midPoint.d[1] += (F32)pTileMin->d[1];
	I32 result = 0;
	err = findFaceQuadrantUv(
		vertCount,
		pVerts,
		midPoint,
		&commonSides,
		&signs,
		&result
	);
	STUC_THROW_IFNOT(err, "", 0);
	err = getChildrenFromResult(
		result,
		pChildren + *pCellStackPtr,
		&commonSides,
		&signs
	);
	STUC_THROW_IFNOT(err, "", 0);
	STUC_CATCH(0, err, ;);
	return err;
}

static
void getNextChild(
	QuadTreeSearch *pState,
	Cell **ppCellStack,
	I32 *pCellStackPtr,
	Cell *pCell,
	Children *pChildren
) {
	I32 nextChild = -1;
	for (I32 i = 0; i < 4; ++i) {
		if (!pState->pCellInits[pCell->pChildren[i].cellIdx] &&
			*((I32 *)(pChildren + *pCellStackPtr) + i)) {
			nextChild = i;
			break;
		}
		STUC_ASSERT("", i >= 0 && i < 4);
	}
	if (nextChild == -1) {
		--*pCellStackPtr;
	}
	else {
		++*pCellStackPtr;
		ppCellStack[*pCellStackPtr] = pCell->pChildren + nextChild;
	}
}

static
Result stucGetAllEncasingCells(
	QuadTreeSearch *pState,
	EncasingCells *pEncasingCells,
	I32 vertCount,
	V2_F32 *pVerts,
	V2_I32 tileMin
) {
	Result err = STUC_SUCCESS;
	QuadTree *pTree = &pState->pMap->quadTree;
	Cell *cellStack[STUC_CELL_STACK_SIZE] = {0};
	Children children[STUC_CELL_STACK_SIZE] = {0};
	Cell *pRootCell = pState->pMap->quadTree.pRootCell;
	STUC_ASSERT("", !pRootCell->cellIdx && !pRootCell->localIdx);
	cellStack[0] = pRootCell;
	pState->pCellInits[0] = 0;
	I32 cellStackPtr = 0;
	for (I32 i = 0; i < 4; ++i) {
		STUC_ASSERT("", pRootCell->pChildren[i].localIdx >= 0);
		STUC_ASSERT("", pRootCell->pChildren[i].localIdx < 4);
		pState->pCellInits[pRootCell->pChildren[i].cellIdx] = 0;
		STUC_ASSERT("", i >= 0 && i < 4);
	}
	do {
		STUC_ASSERT(
			"",
			cellStackPtr >= 0 && cellStackPtr < STUC_CELL_STACK_SIZE
		);
		Cell *pCell = cellStack[cellStackPtr];
		STUC_ASSERT("", pCell->localIdx >= 0 && pCell->localIdx < 4);
		STUC_ASSERT(
			"",
			pCell->cellIdx >= 0 && pCell->cellIdx < pTree->cellCount
		);
		STUC_ASSERT(
			"",
			(pCell->pChildren && pCell->faceSize > 0) || !pCell->pChildren
		);
		STUC_ASSERT("", pCell->initialized % 2 == pCell->initialized);
		if (!pCell->pChildren) {
			STUC_ASSERT("", !pCell->edgeFaceSize);
			err = addCellToEncasingCells(pState->pMap, pCell, pEncasingCells, 0);
			STUC_THROW_IFNOT(err, "", 0);
			cellStackPtr--;
			STUC_ASSERT("", !pState->pCellInits[pCell->cellIdx]);
			pState->pCellInits[pCell->cellIdx] = 1;
			continue;
		}
		if (pState->pCellInits[pCell->cellIdx]) {
			getNextChild(pState, cellStack, &cellStackPtr, pCell, children);
			continue;
		}
		STUC_ASSERT("", vertCount >= 0 && vertCount < 10000);
		err = findEncasingChildCells(
			pCell,
			children,
			&cellStackPtr,
			vertCount,
			pVerts,
			&tileMin
		);
		STUC_THROW_IFNOT(err, "", 0);
		err = addCellToEncasingCells(pState->pMap, pCell, pEncasingCells, 1);
		STUC_THROW_IFNOT(err, "", 0);
		STUC_ASSERT("", !pState->pCellInits[pCell->cellIdx]);
		pState->pCellInits[pCell->cellIdx] = 1;
		for (I32 i = 0; i < 4; ++i) {
			pState->pCellInits[pCell->pChildren[i].cellIdx] = 0;
			STUC_ASSERT("", i >= 0 && i < 4);
		}
		I32 nextChild = 0;
		for (I32 i = 0; i < 4; ++i) {
			if (*((I32 *)(children + cellStackPtr) + i)) {
				nextChild = i;
				break;
			}
			STUC_ASSERT("", i >= 0 && i < 4);
		}
		cellStackPtr++;
		cellStack[cellStackPtr] = pCell->pChildren + nextChild;
	} while (cellStackPtr >= 0);
	STUC_CATCH(0, err, ;);
	return err;
}

I32 stucCheckIfFaceIsInsideTile(
	I32 vertCount,
	V2_F32 *pVerts,
	FaceBounds *pFaceBounds,
	V2_I32 tileMin
) {
	I32 isInsideBuf[4] = {1, 1, 1, 1};
	I32 faceVertInside = 0;
	for (I32 i = 0; i < vertCount; ++i) {
		//check if current edge intersects tile
		I32 nexti = (i + 1) % vertCount;
		V2_F32 cornerDir = _(pVerts[nexti] V2SUB pVerts[i]);
		V2_F32 cornerCross = v2F32Cross(cornerDir);
		for (I32 j = 0; j < 4; ++j) {
			V2_F32 cellPoint = {(F32)(tileMin.d[0] + j % 2), (F32)(tileMin.d[1] + j / 2)};
			V2_F32 cellDir = _(cellPoint V2SUB pVerts[i]);
			F32 dot = _(cornerCross V2DOT cellDir);
			isInsideBuf[j] *= dot < .0f;
		}
		//in addition, test for face verts inside tile
		//edge cases may not be cause by the above,
		//like if a face entered the tile, and then exited the same side,
		//with a single vert in the tile. Checking for verts will catch this:
		faceVertInside +=
			_(pVerts[i] V2GREAT pFaceBounds->fMin) &&
			_(pVerts[i] V2LESSEQL pFaceBounds->fMax);
	}
	I32 isInside =
		isInsideBuf[0] || isInsideBuf[1] ||
		isInsideBuf[2] || isInsideBuf[3];
	I32 isFullyEnclosed =
		isInsideBuf[0] && isInsideBuf[1] &&
		isInsideBuf[2] && isInsideBuf[3];
	if (isFullyEnclosed) {
		return 2;
	}
	if (!faceVertInside && !isInside) {
		//face is not inside current tile
		return 0;
	}
	return 1;
}

static
void checkIfInLinkEdges(Cell *pCell, Cell *pLeaf, Range *pRange, I32 *pLinked) {
	for (I32 k = 0; k < pLeaf->linkEdgeSize; ++k) {
		if (pCell->cellIdx == pLeaf->pLinkEdges[k]) {
			if (!*pLinked) {
				*pLinked = 1;
			}
			if (pLeaf->pLinkEdgeRanges[k].start < pRange->start) {
				pRange->start = pLeaf->pLinkEdgeRanges[k].start;
			}
			if (pLeaf->pLinkEdgeRanges[k].end > pRange->end) {
				pRange->end = pLeaf->pLinkEdgeRanges[k].end;
			}
			break;
		}
	}
}

static
I32 checkBranchCellIsLinked(
	StucMap pMap,
	EncasingCells *pCellsBuf,
	I32 idx,
	Range *pRange
) {
	I32 linked = 0;
	I32 cellIdx = pCellsBuf->pCells[idx];
	Cell *pCell = pMap->quadTree.cellTable.pArr + cellIdx;
	for (I32 j = 0; j < pCellsBuf->cellSize; ++j) {
		if (pCellsBuf->pCellType[j] || idx == j) {
			continue;
		}
		I32 leafIdx = pCellsBuf->pCells[j];
		Cell *pLeaf = pMap->quadTree.cellTable.pArr + leafIdx;
		checkIfInLinkEdges(pCell, pLeaf, pRange, &linked);
	}
	return linked;
}

static
void removeNonLinkedBranchCells(StucMap pMap, EncasingCells *pCellsBuf) {
	for (I32 i = 0; i < pCellsBuf->cellSize;) {
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
		I32 cellIdx = pCellsBuf->pCells[i];
		Cell *pCell = pMap->quadTree.cellTable.pArr + cellIdx;
		pCellsBuf->faceTotal -= pCell->edgeFaceSize;
		pCellsBuf->faceTotalNoDup -= pCell->edgeFaceSize;
		for (I32 j = i; j < pCellsBuf->cellSize - 1; ++j) {
			pCellsBuf->pCells[j] = pCellsBuf->pCells[j + 1];
			pCellsBuf->pCellType[j] = pCellsBuf->pCellType[j + 1];
		}
		pCellsBuf->cellSize--;
	}
}

static
void copyCellsIntoTotalList(
	const StucAlloc *pAlloc,
	FaceCellsTable *pFaceCellsTable,
	EncasingCells *pCellsBuf,
	I32 faceIdx,
	Range faceRange
) {
	FaceCells *pEntry = stucIdxFaceCells(pFaceCellsTable, faceIdx, faceRange.start);
	pFaceCellsTable->cellFacesTotal += pCellsBuf->faceTotalNoDup;
	pEntry->pCells = pAlloc->pMalloc(sizeof(I32) * pCellsBuf->cellSize);
	pEntry->pCellType = pAlloc->pMalloc(pCellsBuf->cellSize);
	memcpy(pEntry->pCells, pCellsBuf->pCells,
	       sizeof(I32) * pCellsBuf->cellSize);
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
void removeChildFacesFromCount(
	QuadTreeSearch *pState,
	FaceCellsTable *pFaceCellsTable,
	Cell *pCell,
	Cell **ppStack,
	I32 *pStackPtr,
	I8 *pChildrenLeft
) {
	Cell *pChild = ppStack[*pStackPtr];
	I32 nextChild = pChildrenLeft[*pStackPtr];
	if (nextChild > 3) {
		--*pStackPtr;
		return;
	}
	I32 childType = pState->pCellFlags[pChild->cellIdx];
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
		--*pStackPtr;
		return;
	}
	pChildrenLeft[*pStackPtr]++;
	++*pStackPtr;
	ppStack[*pStackPtr] = pChild->pChildren + nextChild;
	pChildrenLeft[*pStackPtr] = 0;
}

static
void recordCellsInTable(
	QuadTreeSearch *pState,
	FaceCellsTable *pFaceCellsTable,
	EncasingCells *pCellsBuf
) {
	for (I32 i = 0; i < pCellsBuf->cellSize; ++i) {
		I32 cellIdx = pCellsBuf->pCells[i];
		Cell *pCell = pState->pMap->quadTree.cellTable.pArr + cellIdx;
		//must be != 0, not > 0, so as to catch entries set to -1
		if (pState->pCellFlags[pCell->cellIdx] != 0) {
			continue;
		}
		I32 cellType = pCellsBuf->pCellType[i];
		pState->pCellFlags[pCell->cellIdx] = (I8)(cellType + 1);
		Cell *pStack[32] = {0};
		I8 childrenLeft[32] = {0};
		pStack[0] = pCell;
		I32 stackPtr = 0;
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
			removeChildFacesFromCount(
				pState,
				pFaceCellsTable,
				pCell,
				pStack,
				&stackPtr,
				childrenLeft
			);
		} while (stackPtr >= 0);
	}
}

void stucDestroyFaceCellsTable(
	const StucAlloc *pAlloc,
	FaceCellsTable *pFaceCellsTable,
	Range faceRange
) {
	if (pFaceCellsTable->pFaceCells) {
		I32 size = faceRange.end - faceRange.start;
		STUC_ASSERT("", size >= 0);
		for (I32 i = 0; i < size; ++i) {
			FaceCells *pEntry = pFaceCellsTable->pFaceCells + i;
			stucDestroyFaceCellsEntry(pAlloc, pEntry);
		}
		pAlloc->pFree(pFaceCellsTable->pFaceCells);
	}
}

void stucDestroyFaceCellsEntry(const StucAlloc *pAlloc, FaceCells *pEntry) {
	if (pEntry->pCells) {
		pAlloc->pFree(pEntry->pCells);
		pEntry->pCells = NULL;
	}
	if (pEntry->pCellType) {
		pAlloc->pFree(pEntry->pCellType);
		pEntry->pCellType = NULL;
	}
	//TODO segfault when mapping a single quad larger than the 0-1 uv tile
	//i've not really tested the code thats supposed to handle this case,
	//so no surprise it crashes
	if (pEntry->pRanges) {
		pAlloc->pFree(pEntry->pRanges);
		pEntry->pRanges = NULL;
	}
}

void stucInitQuadTreeSearch(QuadTreeSearch *pState) {
	const StucAlloc *pAlloc = pState->pAlloc;
	pState->pCellInits = pAlloc->pCalloc(pState->pMap->quadTree.cellCount, 1);
	pState->pCellFlags = pAlloc->pCalloc(pState->pMap->quadTree.cellCount, 1);
	pState->pCells = pAlloc->pCalloc(sizeof(I32) * pState->pMap->quadTree.cellCount, 1);
	pState->pCellType = pAlloc->pCalloc(pState->pMap->quadTree.cellCount, 1);
}

void stucDestroyQuadTreeSearch(QuadTreeSearch *pState) {
	pState->pAlloc->pFree(pState->pCellFlags);
	pState->pAlloc->pFree(pState->pCellInits);
	pState->pAlloc->pFree(pState->pCells);
	pState->pAlloc->pFree(pState->pCellType);
}

static
I32 getLinearTileIdx(I32 x, I32 y, V2_I32 minTile, V2_I32 maxTile) {
	return (maxTile.d[0] - minTile.d[0] + 1) * (y - minTile.d[1]) + x - minTile.d[0];
}

static
bool getTilesFaceResidesIn(
	I32 vertCount,
	V2_F32 *pVerts,
	FaceBounds *pFaceBounds,
	V2_I32 minTile,
	V2_I32 maxTile,
	bool *pInTileList
) {
	for (I32 i = minTile.d[1]; i <= maxTile.d[1]; ++i) {
		for (I32 j = minTile.d[0]; j <= maxTile.d[0]; ++j) {
			V2_I32 tile = { j, i };
			//Don't check if in tile, if pFaceBounds is NULL
			I32 in = stucCheckIfFaceIsInsideTile(vertCount, pVerts, pFaceBounds, tile);
			if (in == 2) {
				return true;
			}
			I32 tileIdx = getLinearTileIdx(j, i, minTile, maxTile);
			pInTileList[tileIdx] = in;
		}
	}
	return false;
}

Result stucGetCellsForSingleFace(
	QuadTreeSearch *pState,
	I32 vertCount,
	V2_F32 *pVerts,
	FaceCellsTable *pFaceCellsTable,
	FaceBounds *pFaceBounds,
	I32 faceIdx,
	Range faceRange
) {
	Result err = STUC_SUCCESS;
	EncasingCells cellsBuf = {0};
	cellsBuf.pCells = pState->pCells;
	cellsBuf.pCellType = pState->pCellType;
	stucIdxFaceCells(pFaceCellsTable, faceIdx, faceRange.start);
	FaceCells *pEntry = stucIdxFaceCells(pFaceCellsTable, faceIdx, faceRange.start);
	V2_I32 minTile = {0};
	V2_I32 maxTile = {0};
	if (pFaceBounds) {
		pEntry->faceBounds = *pFaceBounds;
		minTile = pFaceBounds->min;
		maxTile = pFaceBounds->max;
	}
	I32 tileCount = (maxTile.d[0] - minTile.d[0] + 1) * (maxTile.d[1] - minTile.d[1] + 1);
	bool *pInTileList = NULL;
	bool fullyEnclosed = false;
	if (pFaceBounds) {
		pInTileList = pState->pAlloc->pCalloc(tileCount, sizeof(bool));
		fullyEnclosed = getTilesFaceResidesIn(
			vertCount,
			pVerts,
			pFaceBounds,
			minTile,
			maxTile,
			pInTileList
		);
	}
	if (fullyEnclosed) {
		//add only root cell
		//TODO rename cell->faceSize to faceCount
		I32 faceCount = pState->pMap->quadTree.pRootCell->faceSize;
		pEntry->pCells = pState->pAlloc->pMalloc(sizeof(Cell *));
		pEntry->pCellType = pState->pAlloc->pMalloc(sizeof(I8));
		pEntry->pRanges = pState->pAlloc->pMalloc(sizeof(Range));
		pEntry->pCellType[0] = 0;
		pEntry->pCells[0] = 0;
		pEntry->cellSize = 1;
		pEntry->pRanges[0].start = 0;
		pEntry->pRanges[0].end = faceCount;
		pEntry->faceSize = faceCount;
		pFaceCellsTable->cellFacesTotal += faceCount;
		if (pState->pCellFlags[0] == 0) {
			pFaceCellsTable->uniqueFaces += faceCount;
			pState->pCellFlags[0] = -1;
		}
		return err;
	}
	for (I32 i = minTile.d[1]; i <= maxTile.d[1]; ++i) {
		for (I32 j = minTile.d[0]; j <= maxTile.d[0]; ++j) {
			V2_I32 tile = { j, i };
			I32 tileIdx = getLinearTileIdx(j, i, minTile, maxTile);
			if (pFaceBounds && !pInTileList[tileIdx]) {
				continue;
			}
			err = stucGetAllEncasingCells(pState, &cellsBuf, vertCount, pVerts, tile);
			STUC_THROW_IFNOT(err, "", 0);
		}
	}
	cellsBuf.pRangeBuf = pState->pAlloc->pMalloc(sizeof(Range) * cellsBuf.cellSize);
	removeNonLinkedBranchCells(pState->pMap, &cellsBuf);
	recordCellsInTable(pState, pFaceCellsTable, &cellsBuf);
	copyCellsIntoTotalList(
		pState->pAlloc,
		pFaceCellsTable,
		&cellsBuf,
		faceIdx,
		faceRange
	);
	STUC_CATCH(0, err, ;);
	pState->pAlloc->pFree(pInTileList);
	return err;
}

Cell *stucFindEncasingCell(Cell *rootCell, V2_F32 pos) {
	V2_F32 cellBoundsMin = {.d[0] = .0, .d[1] = .0};
	V2_F32 cellBoundsMax = {.d[0] = 1.0, .d[1] = 1.0};
	Cell *cell = rootCell;
	I32 depth = -1;
	while (true) {
		if (!cell->pChildren) {
			return cell;
		}
		V2_F32 midPoint = _(_(cellBoundsMax V2SUB cellBoundsMin) V2MULS .5);
		_(&midPoint V2ADDEQL cellBoundsMin);
		depth++;
		I32 childIdx = (pos.d[0] >= midPoint.d[0]) + (pos.d[1] < midPoint.d[1]) * 2;
		cell = cell->pChildren + childIdx;
		cellBoundsMin = cell->boundsMin;
		cellBoundsMax = cell->boundsMax;
	};
}

static
void setCellBounds(Cell *cell, Cell *parentCell, I32 cellStackPtr) {
	calcCellBounds(cell);
	_(&cell->boundsMin V2DIVSEQL (F32)pow(2.0, cellStackPtr));
	_(&cell->boundsMax V2DIVSEQL (F32)pow(2.0, cellStackPtr));
	V2_F32 *ancestorBoundsMin = &parentCell->boundsMin;
	_(&cell->boundsMin V2ADDEQL *ancestorBoundsMin);
	_(&cell->boundsMax V2ADDEQL *ancestorBoundsMin);
	V2_F32 zero = {.0f, .0f};
	V2_F32 one = {1.0f, 1.0f};
	STUC_ASSERT("", _(cell->boundsMin V2GREATEQL zero));
	STUC_ASSERT("", _(cell->boundsMax V2LESSEQL one));
}

static
I32 checkIfLinkedEdge(Cell *pChild, Cell *pAncestor, const Mesh *pMesh, Range *pRange) {
	I32 linked = 0;
	for (I32 i = 0; i < pAncestor->edgeFaceSize; ++i) {
		I32 faceIdx = pAncestor->pEdgeFaces[i];
		FaceRange face = stucGetFaceRange(&pMesh->core, faceIdx, false);
		//doesn't catch cases where edge intersect with bounds,
		//replace with a better method
		if (stucCheckFaceIsInBounds(pChild->boundsMin, pChild->boundsMax, face, pMesh)) {
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
void addLinkEdgesToCells(
	StucContext pCtx,
	I32 parentCell,
	const Mesh *pMesh,
	CellTable *pTable,
	I32 *pCellStack,
	I32 cellStackPtr
) {
	I32 buf[32] = {0};
	Range rangeBuf[32] = {0};
	I32 bufSize;
	for (I32 i = 0; i < 4; ++i) {
		bufSize = 0;
		Cell *pChild = pTable->pArr[parentCell].pChildren + i;
		STUC_ASSERT("", pChild->initialized == 0);
		STUC_ASSERT("", pChild->localIdx >= 0 && pChild->localIdx < 4);
		for (I32 j = 0; j <= cellStackPtr; ++j) {
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
			pChild->pLinkEdges = pCtx->alloc.pMalloc(sizeof(I32) * bufSize);
			memcpy(pChild->pLinkEdges, buf, sizeof(I32) * bufSize);
			//Add link edge ranges
			pChild->pLinkEdgeRanges = pCtx->alloc.pMalloc(sizeof(Range) * bufSize);
				memcpy(pChild->pLinkEdgeRanges, rangeBuf, sizeof(Range) * bufSize);
			pChild->linkEdgeSize = bufSize;
		}
		STUC_ASSERT("", i >= 0 && i < 4);
	}
}

static
void addEnclosedVertsToCell(
	StucContext pCtx,
	I32 parentCellIdx,
	const Mesh *pMesh,
	CellTable *pTable,
	I8 *pFaceFlag
) {
	// Get enclosed verts if not already present
	// First, determine which verts are enclosed, and mark them by negating
	Cell* pParentCell = pTable->pArr + parentCellIdx;
	V2_F32 midPoint = pParentCell->pChildren[1].boundsMin;
	STUC_ASSERT("", v2F32IsFinite(midPoint));
	STUC_ASSERT("", midPoint.d[0] < 1.0f && midPoint.d[1] < 1.0f);
	STUC_ASSERT("", midPoint.d[0] > .0f && midPoint.d[1] > .0f);
	for (I32 i = 0; i < pParentCell->faceSize; ++i) {
		FaceRange face = stucGetFaceRange(&pMesh->core, pParentCell->pFaces[i], false);
		V2_I32 signs;
		V2_I32 commonSides;
		I32 result =
			findFaceQuadrant(&pCtx->alloc, &face, pMesh, midPoint, &commonSides, &signs);
		if (result == 1) {
			I32 childIdx = signs.d[0] + signs.d[1] * 2;
			Cell *pChild = pParentCell->pChildren + childIdx;
			pFaceFlag[i] = (I8)(childIdx + 1);
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
	for (I32 i = 0; i < 4; ++i) {
		Cell *cell = pParentCell->pChildren + i;
		STUC_ASSERT("", cell->initialized == 0u && (I32)cell->localIdx == i);
		STUC_ASSERT("", cell->faceSize >= 0);
		STUC_ASSERT("", cell->faceSize <= pParentCell->faceSize);
		if (cell->faceSize) {
			cell->pFaces = pCtx->alloc.pMalloc(sizeof(I32) * cell->faceSize);
		}
		STUC_ASSERT("", i >= 0 && i < 4);
	}
	STUC_ASSERT("", pParentCell->edgeFaceSize >= 0);
	STUC_ASSERT("", pParentCell->edgeFaceSize <= pParentCell->faceSize);
	if (pParentCell->edgeFaceSize) {
		pParentCell->pEdgeFaces =
			pCtx->alloc.pMalloc(sizeof(I32) * pParentCell->edgeFaceSize);
	}
	I32 facesSize[4] = {0};
	I32 edgeFacesSize = 0;
	for (I32 i = 0; i < pParentCell->faceSize; ++i) {
		if (!pFaceFlag[i]) {
			continue;
		}
		STUC_ASSERT("", pFaceFlag[i] <= 5);
		if (pFaceFlag[i] > 0) {
			I32 child = pFaceFlag[i] - 1;
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
	for (I32 i = 0; i < pTree->cellCount; ++i) {
		Cell *pCell = pTree->cellTable.pArr + i;
		if (pCell->pChildren) {
			I64 offset = pCell->pChildren - pOldPtr;
			pCell->pChildren = pTree->cellTable.pArr + offset;
		}
	}
}

static
void reallocCellTable(const StucContext pCtx, QuadTree *pTree, const I32 sizeDiff) {
	if (!sizeDiff) {
		return;
	}
	pTree->cellTable.size += sizeDiff;
	STUC_ASSERT("", pTree->cellTable.size > 0);
	Cell *pOldPtr = pTree->cellTable.pArr;
	pTree->cellTable.pArr = pCtx->alloc.pRealloc(
		pTree->cellTable.pArr,
		sizeof(Cell) * pTree->cellTable.size
	);
	if (sizeDiff > 0) {
		memset(pTree->cellTable.pArr + pTree->cellCount, 0, sizeof(Cell) * sizeDiff);
	}
	updateCellPtrs(pTree, pOldPtr);
	pTree->pRootCell = pTree->cellTable.pArr;
}

static
void allocateChildren(
	StucContext pCtx,
	I32 parentCell,
	I32 cellStackPtr,
	QuadTree *pTree
) {
	STUC_ASSERT("", pTree->cellCount > 0);
	STUC_ASSERT("", pTree->cellCount <= pTree->cellTable.size);
	if (pTree->cellCount + 4 > pTree->cellTable.size) {
		I32 sizeIncrease = (pTree->cellTable.size + 4) * 2;
		reallocCellTable(pCtx, pTree, sizeIncrease);
	}
	STUC_ASSERT("", pTree->cellTable.size >= pTree->cellCount + 4);
	STUC_ASSERT("", pTree->cellTable.pArr[0].initialized == 1);
	STUC_ASSERT("", !pTree->cellTable.pArr[0].cellIdx);
	STUC_ASSERT("", !pTree->cellTable.pArr[0].localIdx);
	pTree->cellTable.pArr[parentCell].pChildren = pTree->cellTable.pArr + pTree->cellCount;
	for (I32 i = 0; i < 4; ++i) {
		// v for visualizing quadtree v
		//cell->children[i].cellIdx = rand();
		Cell *cell = pTree->cellTable.pArr[parentCell].pChildren + i;
		STUC_ASSERT("", !cell->initialized);
		cell->cellIdx = pTree->cellCount;
		pTree->cellCount++;
		cell->localIdx = (U32)i;
		setCellBounds(cell, pTree->cellTable.pArr + parentCell, cellStackPtr);
		STUC_ASSERT("", i >= 0 && i < 4);
	}
	pTree->leafCount += 4;
}

static
Result processCell(
	StucContext pCtx,
	I32 *pCellStack,
	I32 *pCellStackPtr,
	const Mesh *pMesh,
	QuadTree *pTree,
	I8 *pFaceFlag,
	I32 *pProgress
) {
	Result err = STUC_SUCCESS;
	CellTable *pCellTable = &pTree->cellTable;
	STUC_ASSERT("", pTree && pCellTable);
	I32 cell = pCellStack[*pCellStackPtr];
	STUC_ASSERT("",
		pCellTable->pArr[cell].initialized % 2 ==
		pCellTable->pArr[cell].initialized
	);
	STUC_ASSERT("",
		pCellTable->pArr[cell].cellIdx >= 0 &&
		pCellTable->pArr[cell].cellIdx < pTree->cellCount
	);
	STUC_ASSERT("",
		pCellTable->pArr[cell].localIdx >= 0 &&
		pCellTable->pArr[cell].localIdx < 4
	);
	STUC_ASSERT("",
		pCellTable->pArr[cell].faceSize >= 0 &&
		pCellTable->pArr[cell].faceSize < 100000000
	);
	// If more than CELL_MAX_VERTS in cell, then subdivide cell
	I32 hasChildren = pCellTable->pArr[cell].faceSize > CELL_MAX_VERTS;
	if (hasChildren) {
		// Get number of children
		I32 childSize = 0;
		if (!pCellTable->pArr[cell].pChildren) {
			pTree->leafCount--;
			allocateChildren(pCtx, cell, *pCellStackPtr, pTree);
			addEnclosedVertsToCell(pCtx, cell, pMesh, &pTree->cellTable, pFaceFlag);
			addLinkEdgesToCells(
				pCtx,
				cell,
				pMesh,
				&pTree->cellTable,
				pCellStack,
				*pCellStackPtr
			);
		}
		STUC_ASSERT("", childSize >= 0);
		for (I32 i = 0; i < 4; ++i) {
			childSize += (I32)pCellTable->pArr[cell].pChildren[i].initialized;
			STUC_ASSERT("", i >= 0 && i < 4);
		}
		// If the cell has children, and they are not yet all initialized,
		// then add the next one to the stack
		if (childSize < 4) {
			(*pCellStackPtr)++;
			pCellStack[*pCellStackPtr] = pCellTable->pArr[cell].pChildren[childSize].cellIdx;
			return err;
		}
	}
	// Otherwise, set the current cell as initialized, and pop it off the stack
	pCellTable->pArr[cell].initialized = 1;
	if (*pCellStackPtr == 2) {
		*pProgress += pCtx->stageReport.outOf / 16;
		stucStageProgressWrap(pCtx, *pProgress);
	}
	(*pCellStackPtr)--;
	STUC_CATCH(0, err, ;);
	return err;
}

static
Result initRootAndChildren(
	StucContext pCtx,
	I32 *pCellStack,
	QuadTree *pTree,
	const Mesh *pMesh,
	I8 *pFaceFlag
) {
	CellTable* pTable = &pTree->cellTable;
	Cell *pRoot = pTable->pArr;
	pRoot->cellIdx = 0;
	pTree->cellCount = 1;
	pCellStack[0] = 0;
	pRoot->boundsMax.d[0] = pRoot->boundsMax.d[1] = 1.0f;
	pRoot->initialized = 1;
	pRoot->pFaces = pCtx->alloc.pMalloc(sizeof(I32) * pMesh->core.faceCount);
	for (I32 i = 0; i < pMesh->core.faceCount; ++i) {
		FaceRange face = stucGetFaceRange(&pMesh->core, i, false);
		bool inside = false;
		for (I32 j = 0; j < face.size; ++j) {
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
	allocateChildren(pCtx, 0, 0, pTree);
	addEnclosedVertsToCell(pCtx, 0, pMesh, pTable, pFaceFlag);
	addLinkEdgesToCells(pCtx, 0, pMesh, pTable, pCellStack, 0);
	pCellStack[1] = 1;
	return STUC_SUCCESS;
}

Result stucCreateQuadTree(StucContext pCtx, QuadTree *pTree, const Mesh *pMesh) {
	Result err = STUC_NOT_SET;
	STUC_ASSERT("", pMesh->core.faceCount > 0);
	stucStageBeginWrap(pCtx, "Creating quad tree", pCtx->stageReport.outOf);
	pTree->cellTable.size = pMesh->core.faceCount / CELL_MAX_VERTS + 1;
	pTree->cellTable.pArr =
		pCtx->alloc.pCalloc(pTree->cellTable.size, sizeof(Cell));
	pTree->cellCount = 0;
	pTree->leafCount = 0;
	I8 *pFaceFlag = pCtx->alloc.pCalloc(pMesh->core.faceCount, sizeof(I8));

	pTree->pRootCell = pTree->cellTable.pArr;
	I32 cellStack[STUC_CELL_STACK_SIZE];
	err =  initRootAndChildren(pCtx, cellStack, pTree, pMesh, pFaceFlag);
	STUC_THROW_IFNOT(err, "All faces were outside 0-1 tile", 0);
	I32 cellStackPtr = 1;
	I32 progress = 0;
	do {
		STUC_ASSERT("", cellStackPtr < STUC_CELL_STACK_SIZE);
		processCell(
			pCtx,
			cellStack,
			&cellStackPtr,
			pMesh,
			pTree,
			pFaceFlag,
			&progress
		);
	} while(cellStackPtr >= 0);
	STUC_ASSERT("", pTree->cellCount <= pTree->cellTable.size);
	STUC_ASSERT("", pTree->pRootCell->initialized == 1);
	I32 sizeDecrease = pTree->cellCount - pTree->cellTable.size;
	STUC_ASSERT("", sizeDecrease < 0);
	printf("Created quadTree -- cells: %d, leaves: %d\n",
	       pTree->cellCount, pTree->leafCount);
	reallocCellTable(pCtx, pTree, sizeDecrease);
	stucStageEndWrap(pCtx);
	STUC_CATCH(0, err, ;)
	pCtx->alloc.pFree(pFaceFlag);
	return err;
}

void stucDestroyQuadTree(StucContext pCtx, QuadTree *pTree) {
	for (I32 i = 0; i < pTree->cellCount; ++i) {
		Cell *cell = pTree->cellTable.pArr + i;
		if (cell->pLinkEdges) {
			pCtx->alloc.pFree(cell->pLinkEdges);
		}
		if (cell->pLinkEdgeRanges) {
			pCtx->alloc.pFree(cell->pLinkEdgeRanges);
		}
		if (cell->pFaces) {
			pCtx->alloc.pFree(cell->pFaces);
		}
		if (cell->pEdgeFaces) {
			pCtx->alloc.pFree(cell->pEdgeFaces);
		}
	}
	pCtx->alloc.pFree(pTree->cellTable.pArr);
}

void stucGetFaceBoundsForTileTest(
	FaceBounds *pFaceBounds,
	const Mesh *pMesh,
	FaceRange *pFace
) {
	stucGetFaceBounds(pFaceBounds, pMesh->pUvs, *pFace);
	pFaceBounds->fMinSmall = pFaceBounds->fMin;
	pFaceBounds->fMaxSmall = pFaceBounds->fMax;
	pFaceBounds->min = v2F32FloorAssign(&pFaceBounds->fMin);
	pFaceBounds->max = v2F32FloorAssign(&pFaceBounds->fMax);
	_(&pFaceBounds->fMax V2ADDEQLS 1.0f);
}

Result stucGetEncasingCells(
	const StucAlloc *pAlloc,
	const StucMap pMap,
	const Mesh *pInMesh,
	I8 maskIdx,
	Range faceRange,
	FaceCellsTable *pFaceCellsTable,
	I32 *pAverageMapFacesPerFace
) {
	Result err = STUC_SUCCESS;
	*pAverageMapFacesPerFace = 0;
	I32 inFaceRangeSize = faceRange.end - faceRange.start;
	pFaceCellsTable->pFaceCells = pAlloc->pCalloc(inFaceRangeSize,sizeof(FaceCells));
	QuadTreeSearch searchState = {.pAlloc = pAlloc, .pMap = pMap};
	stucInitQuadTreeSearch(&searchState);
	for (I32 i = faceRange.start; i < faceRange.end; ++i) {
		if (maskIdx != -1 && pInMesh->pMatIdx &&
		    pInMesh->pMatIdx[i] != maskIdx) {
			continue;
		}
		FaceRange faceInfo = stucGetFaceRange(&pInMesh->core, i, false);
		if (faceInfo.size > 4) {
			continue;
		}
		FaceBounds faceBounds = {0};
		stucGetFaceBoundsForTileTest(&faceBounds, pInMesh, &faceInfo);
		V2_F32 *pVertBuf = pAlloc->pMalloc(sizeof(V2_F32) * faceInfo.size);
		for (I32 j = 0; j < faceInfo.size; ++j) {
			pVertBuf[j] = pInMesh->pUvs[faceInfo.start + j];
		}
		err = stucGetCellsForSingleFace(
			&searchState,
			faceInfo.size,
			pVertBuf,
			pFaceCellsTable,
			&faceBounds,
			i,
			faceRange
		);
		pAlloc->pFree(pVertBuf);
		STUC_THROW_IFNOT(err, "", 0);
		*pAverageMapFacesPerFace +=
			stucIdxFaceCells(pFaceCellsTable, i, faceRange.start)->faceSize;
	}
	*pAverageMapFacesPerFace /= inFaceRangeSize;
	STUC_CATCH(0, err,
		pAlloc->pFree(pFaceCellsTable->pFaceCells);
	;);
	stucDestroyQuadTreeSearch(&searchState);
	return err;
}

FaceCells *stucIdxFaceCells(
	FaceCellsTable *pFaceCellsTable,
	I32 faceIdx,
	I32 faceOffset
) {
	return pFaceCellsTable->pFaceCells + (faceIdx - faceOffset);
}