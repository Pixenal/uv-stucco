#include "QuadTree.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <memory.h>

extern int32_t cellIndex;
extern int32_t leafAmount;

static int32_t counter = 0;

void calcCellBounds(Cell *cell) {
	float xSide = (float)(cell->localIndex % 2);
	float ySide = (float)(((cell->localIndex + 2) / 2) % 2);
	cell->boundsMin.x = xSide * .5;
	cell->boundsMin.y = ySide * .5;
	cell->boundsMax.x = 1.0 - (1.0 - xSide) * .5;
	cell->boundsMax.y = 1.0 - (1.0 - ySide) * .5;
}

void addCellToEnclosingCells(Cell *cell, EnclosingCellsInfo *pEnclosingCellsInfo, int32_t edge) {
	int32_t faceAmount = edge ? cell->edgeFaceAmount : cell->faceAmount;
	pEnclosingCellsInfo->faceTotal += faceAmount;
	int32_t dupIndex = -1;
	for (int32_t i = 0; i < pEnclosingCellsInfo->cellAmount; ++i) {
		if (pEnclosingCellsInfo->cells[i] == cell) {
			dupIndex = i;
			break;
		}
	}
	if (dupIndex >= 0) {
		if (!pEnclosingCellsInfo->cellType[dupIndex] && edge) {
			pEnclosingCellsInfo->cellType[dupIndex] = 2;
		}
		return;
	}
	pEnclosingCellsInfo->cells[pEnclosingCellsInfo->cellAmount] = cell;
	pEnclosingCellsInfo->cellType[pEnclosingCellsInfo->cellAmount] = edge;
	pEnclosingCellsInfo->cellAmount++;;
	pEnclosingCellsInfo->faceTotalNoDup += faceAmount;
}

int32_t findFaceQuadrantUv(int32_t loopAmount, int32_t loopStart,
                           Vec3 *verts, Vec2 midPoint, iVec2 *commonSides, iVec2 *signs) {
	commonSides->x = commonSides->y = 1;
	iVec2 sides[loopAmount];
	for (int32_t i = 0; i < loopAmount; ++i) {
		int32_t loopIndex = loopStart + i;
		sides[i].x = verts[loopIndex].x >= midPoint.x;
		sides[i].y = verts[loopIndex].y < midPoint.y;
		for (int32_t j = 0; j < i; ++j) {
			commonSides->x *= sides[i].x == sides[j].x;
			commonSides->y *= sides[i].y == sides[j].y;
		}
	}
	if (!commonSides->x && !commonSides->y) {
		return 0;
	}
	signs->x = sides[0].x;
	signs->y = sides[0].y;
	if (commonSides->x && commonSides->y) {
		return 1;
	}
	else {
		return 2;
	}
}

int32_t findFaceQuadrant(int32_t loopAmount, int32_t faceStart,
                         MeshData *pMesh, Vec2 midPoint,
						 iVec2 *commonSides, iVec2 *signs) {
	commonSides->x = commonSides->y = 1;
	iVec2 sides[loopAmount];
	for (int32_t i = 0; i < loopAmount; ++i) {
		int32_t vertIndex = pMesh->pLoops[faceStart + i];
		sides[i].x = pMesh->pVerts[vertIndex].x >= midPoint.x;
		sides[i].y = pMesh->pVerts[vertIndex].y < midPoint.y;
		for (int32_t j = 0; j < i; ++j) {
			commonSides->x *= sides[i].x == sides[j].x;
			commonSides->y *= sides[i].y == sides[j].y;
		}
	}
	signs->x = sides[0].x;
	signs->y = sides[0].y;
	return commonSides->x && commonSides->y;
}

void quadTreeGetAllEnclosingCells(iVec2 tileMin, EnclosingCellsInfo *pEnclosingCellsInfo,
									 Cell *pRootCell, int32_t loopStart, int32_t loopEnd,
							         MeshData *pMesh, int8_t *pCellInits) {
	typedef struct {
		int32_t a;
		int32_t b;
		int32_t c;
		int32_t d;
	} Children;
	Cell *cellStack[16];
	Children children[16];
	cellStack[0] = pRootCell;
	pCellInits[0] = 0;
	int32_t cellStackPointer = 0;
	int32_t loopAmount = loopEnd - loopStart;
	for (int32_t i = 0; i < 4; ++i) {
		pCellInits[pRootCell->pChildren[i].cellIndex] = 0;
	}
	do {
		Cell *cell = cellStack[cellStackPointer];
		if (!cell->pChildren) {
			addCellToEnclosingCells(cell, pEnclosingCellsInfo, 0);
			cellStackPointer--;
			pCellInits[cell->cellIndex] = 1;
			continue;
		}
		if (pCellInits[cell->cellIndex]) {
			int32_t nextChild = -1;
			for (int32_t i = 0; i < 4; ++i) {
				if (!pCellInits[cell->pChildren[i].cellIndex] &&
				    *((int32_t *)(children + cellStackPointer) + i)) {
					nextChild = i;
					break;
				}
			}
			if (nextChild == -1) {
				cellStackPointer--;
				continue;
			}
			cellStackPointer++;
			cellStack[cellStackPointer] = cell->pChildren + nextChild;
			continue;
		}
		Vec2 midPoint = _(_(_(cell->boundsMax V2SUB cell->boundsMin) V2MULS .5) V2ADD cell->boundsMin);
		iVec2 signs;
		iVec2 commonSides;
		midPoint.x += (float)tileMin.x;
		midPoint.y += (float)tileMin.y;
		int32_t result = findFaceQuadrantUv(loopAmount, loopStart, pMesh->pVerts, midPoint,
		                                    &commonSides, &signs);
		switch (result) {
			case 0: {
				addCellToEnclosingCells(cell, pEnclosingCellsInfo, 0);
				cellStackPointer--;
				pCellInits[cell->cellIndex] = 1;
				continue;
			}
			case 1: {
				children[cellStackPointer].a = !signs.x && !signs.y;
				children[cellStackPointer].b = signs.x && !signs.y;
				children[cellStackPointer].c = !signs.x && signs.y;
				children[cellStackPointer].d = signs.x && signs.y;
				break;
			}
			case 2: {
				int32_t top, bottom = top = commonSides.y;
				int32_t left, right = left = commonSides.x;
				top *= !signs.y;
				bottom *= signs.y;
				left *= !signs.x;
				right *= signs.x;
				children[cellStackPointer].a = top || left;
				children[cellStackPointer].b = top || right;
				children[cellStackPointer].c = bottom || left;
				children[cellStackPointer].d = bottom || right;
				break;
			}
		}
		addCellToEnclosingCells(cell, pEnclosingCellsInfo, 1);
		pCellInits[cell->cellIndex] = 1;
		for (int32_t i = 0; i < 4; ++i) {
			pCellInits[cell->pChildren[i].cellIndex] = 0;
		}
		int32_t nextChild = 0;
		for (int32_t i = 0; i < 4; ++i) {
			if (*((int32_t *)(children + cellStackPointer) + i)) {
				nextChild = i;
				break;
			}
		}
		cellStackPointer++;
		cellStack[cellStackPointer] = cell->pChildren + nextChild;
	} while (cellStackPointer >= 0);
}

Cell *findEnclosingCell(Cell *rootCell, Vec2 pos) {
	Vec2 cellBoundsMin = {.x = .0, .y = .0};
	Vec2 cellBoundsMax = {.x = 1.0, .y = 1.0};
	Cell *cell = rootCell;
	int32_t depth = -1;
	while (true) {
		if (!cell->pChildren) {
			return cell;
		}
		Vec2 midPoint = _(_(_(cellBoundsMax V2SUB cellBoundsMin) V2MULS .5) V2ADD cellBoundsMin);
		depth++;
		int32_t childIndex = (pos.x >= midPoint.x) + (pos.y < midPoint.y) * 2;
		cell = cell->pChildren + childIndex;
		cellBoundsMin = cell->boundsMin;
		cellBoundsMax = cell->boundsMax;
	};
}

void setCellBounds(Cell *cell, Cell *parentCell, int32_t cellStackPointer) {
	calcCellBounds(cell);
	_(&cell->boundsMin V2DIVSEQL (float)pow(2.0, cellStackPointer));
	_(&cell->boundsMax V2DIVSEQL (float)pow(2.0, cellStackPointer));
	Vec2 *ancestorBoundsMin = &parentCell->boundsMin;
	_(&cell->boundsMin V2ADDEQL *ancestorBoundsMin);
	_(&cell->boundsMax V2ADDEQL *ancestorBoundsMin);
}

void addEnclosedVertsToCell(Cell *pParentCell, MeshData *pMesh, int8_t *pFaceFlag) {
	// Get enclosed verts if not already present
	// First, determine which verts are enclosed, and mark them by negating
	Vec2 midPoint = pParentCell->pChildren[1].boundsMin;
	for (int32_t i = 0; i < pParentCell->faceAmount; ++i) {
		int32_t face = pParentCell->pFaces[i];
		int32_t faceStart = pMesh->pFaces[face];
		int32_t faceEnd = pMesh->pFaces[face + 1];
		int32_t faceLoopAmount = faceEnd - faceStart;
		iVec2 signs;
		iVec2 commonSides;
		int32_t result = findFaceQuadrant(faceLoopAmount, faceStart,
		                                  pMesh, midPoint,
		                                  &commonSides, &signs);
		if (result) {
			int32_t child = signs.x + signs.y * 2;
			pFaceFlag[i] = child + 1;
			pParentCell->pChildren[child].faceAmount++;
		}
		else {
			pFaceFlag[i] = -1;
			pParentCell->edgeFaceAmount++;
		}
	}
	for (int32_t i = 0; i < 4; ++i) {
		Cell *cell = pParentCell->pChildren + i;
		if (cell->faceAmount) {
			cell->pFaces = malloc(sizeof(int32_t) * cell->faceAmount);
		}
	}
	if (pParentCell->edgeFaceAmount) {
		pParentCell->pEdgeFaces = malloc(sizeof(int32_t) * pParentCell->edgeFaceAmount);
	}
	int32_t facesTop[4] = {0};
	int32_t edgeFacesTop = 0;
	for (int32_t i = 0; i < pParentCell->faceAmount; ++i) {
		if (!pFaceFlag[i]) {
			continue;
		}
		if (pFaceFlag[i] > 0) {
			int32_t child = pFaceFlag[i] - 1;
			Cell *cell = pParentCell->pChildren + child;
			cell->pFaces[facesTop[child]] = pParentCell->pFaces[i];
			facesTop[child]++;
		}
		else {
			pParentCell->pEdgeFaces[edgeFacesTop] = pParentCell->pFaces[i];
			edgeFacesTop++;
		}
		pFaceFlag[i] = 0;
	}
}

void allocateChildren(Cell *parentCell, int32_t cellStackPointer) {
	parentCell->pChildren = calloc(4, sizeof(Cell));
	for (int32_t i = 0; i < 4; ++i) {
		// v for visualizing quadtree v
		//cell->children[i].cellIndex = rand();
		Cell *cell = parentCell->pChildren + i;
		cell->cellIndex = cellIndex;
		cellIndex++;
		cell->localIndex = (uint32_t)i;
		setCellBounds(cell, parentCell, cellStackPointer);
	}
	leafAmount += 4;
}


void processCell(Cell **pCellStack, int32_t *pCellStackPointer, MeshData *pMesh,
                 int8_t *pFaceFlag) {
	Cell *cell = pCellStack[*pCellStackPointer];
	// If more than CELL_MAX_VERTS in cell, then subdivide cell
	int32_t hasChildren = cell->faceAmount > CELL_MAX_VERTS;
	if (hasChildren) {
		// Get number of children
		int32_t childAmount = 0;
		if (!cell->pChildren) {
			leafAmount--;
			allocateChildren(cell,*pCellStackPointer);
			addEnclosedVertsToCell(cell, pMesh, pFaceFlag);
		}
		for (int32_t i = 0; i < 4; ++i) {
			childAmount += (int32_t)cell->pChildren[i].initialized;
		}
		// If the cell has children, and they are not yet all initialized,
		// then add the next one to the stack
		if (childAmount < 4) {
			(*pCellStackPointer)++;
			pCellStack[*pCellStackPointer] = cell->pChildren + childAmount;
			return;
		}
	}
	// Otherwise, set the current cell as initialized, and pop it off the stack
	cell->initialized = 1;
	(*pCellStackPointer)--;
}

int32_t calculateMaxTreeDepth(int32_t vertAmount) {
	return log(CELL_MAX_VERTS * vertAmount) / log(4) + 2;
}

void createQuadTree(QuadTree *pQuadTree, MeshData *pMesh) {
	cellIndex = 0;
	leafAmount = 0;
	counter = 0;

	pQuadTree->pRootCell = calloc(1, sizeof(Cell));
	Cell *rootCell = pQuadTree->pRootCell;
	pQuadTree->maxTreeDepth = 32;

	Cell *cellStack[32];
	rootCell->cellIndex = cellIndex;
	cellIndex++;
	cellStack[0] = rootCell;
	rootCell->boundsMax.x = rootCell->boundsMax.y = 1.0f;
	rootCell->initialized = 1;
	int8_t *pFaceFlag = calloc(pMesh->faceAmount, sizeof(int8_t));
	rootCell->faceAmount = pMesh->faceAmount;
	rootCell->pFaces = malloc(sizeof(int32_t) * pMesh->faceAmount);
	for (int32_t i = 0; i < pMesh->faceAmount; ++i) {
		rootCell->pFaces[i] = i;
	}
	allocateChildren(rootCell, 0);
	addEnclosedVertsToCell(rootCell, pMesh, pFaceFlag);
	cellStack[1] = rootCell->pChildren;
	int32_t cellStackPointer = 1;
	do {
		processCell(cellStack, &cellStackPointer, pMesh, pFaceFlag);
		counter++;
	} while(cellStackPointer >= 0);
	printf("Created quadTree -- cells: %d, leaves: %d\n", cellIndex, leafAmount);
}

void destroyQuadTree(Cell *rootCell) {
	Cell *cellStack[32];
	cellStack[0] = rootCell;
	int32_t cellStackPointer = 0;
	do {
		Cell *cell = cellStack[cellStackPointer];
		int32_t nextChild = 0;
		if (cell->pChildren) {
			cell->initialized = 0;
			for (int32_t i = 0; i < 4; ++i) {
				nextChild += cell->pChildren[i].initialized == 0;
			}
			if (nextChild < 4) {
				cellStackPointer++;
				cellStack[cellStackPointer] = cell->pChildren + nextChild;
				continue;
			}
			free(cell->pChildren);
		}
		else {
			cell->initialized = 0;
		}
		if (cell->pFaces) {
			free(cell->pFaces);
		}
		if (cell->pEdgeFaces) {
			free(cell->pEdgeFaces);
		}
		cellStackPointer--;
	} while(cellStackPointer >= 0);
	free(rootCell);
}
