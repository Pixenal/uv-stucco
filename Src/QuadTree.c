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

void addCellToEnclosingCells(Cell *cell, int32_t *enclosingCellAmount, Cell **enclosingCellFaces,
                             int8_t *enclosingCellType, int32_t *totalCellFaces,
							 int32_t *totalCellFacesNoDup, int32_t edge) {
	int32_t faceAmount = edge ? cell->edgeFaceAmount : cell->faceAmount;
	*totalCellFaces += faceAmount;
	int32_t dupIndex = -1;
	for (int32_t i = 0; i < *enclosingCellAmount; ++i) {
		if (enclosingCellFaces[i] == cell) {
			dupIndex = i;
			break;
		}
	}
	if (dupIndex >= 0) {
		if (!enclosingCellType[dupIndex] && edge) {
			enclosingCellType[dupIndex] = 2;
		}
		return;
	}
	enclosingCellFaces[*enclosingCellAmount] = cell;
	enclosingCellType[*enclosingCellAmount] = edge;
	++*enclosingCellAmount;
	*totalCellFacesNoDup += faceAmount;
}

int32_t findFaceQuadrantUv(int32_t loopAmount, int32_t loopStart,
                         Vec2 *verts, Vec2 midPoint, iVec2 *commonSides, iVec2 *signs) {
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
                         Vec3 *verts, int32_t *loopBuffer, Vec2 midPoint,
						 iVec2 *commonSides, iVec2 *signs) {
	commonSides->x = commonSides->y = 1;
	iVec2 sides[loopAmount];
	for (int32_t i = 0; i < loopAmount; ++i) {
		int32_t vertIndex = loopBuffer[faceStart + i];
		sides[i].x = verts[vertIndex].x >= midPoint.x;
		sides[i].y = verts[vertIndex].y < midPoint.y;
		for (int32_t j = 0; j < i; ++j) {
			commonSides->x *= sides[i].x == sides[j].x;
			commonSides->y *= sides[i].y == sides[j].y;
		}
	}
	signs->x = sides[0].x;
	signs->y = sides[0].y;
	return commonSides->x && commonSides->y;
}

void findFullyEnclosingCell(iVec2 tileMin, int32_t *enclosingCellAmount,
                            Cell **enclosingCellFaces, int8_t *enclosingCellType,
							int32_t *totalCellFaces, int32_t *totalCellFacesNoDup,
							Cell *rootCell, int32_t loopStart, int32_t loopEnd,
							int32_t *loops, Vec2 *verts, int8_t *cellInits) {
	typedef struct {
		int32_t a;
		int32_t b;
		int32_t c;
		int32_t d;
	} Children;
	Cell *cellStack[16];
	Children children[16];
	cellStack[0] = rootCell;
	cellInits[0] = 0;
	int32_t cellStackPointer = 0;
	int32_t loopAmount = loopEnd - loopStart;
	for (int32_t i = 0; i < 4; ++i) {
		cellInits[rootCell->children[i].cellIndex] = 0;
	}
	do {
		Cell *cell = cellStack[cellStackPointer];
		if (!cell->children) {
			addCellToEnclosingCells(cell, enclosingCellAmount, enclosingCellFaces,
			                        enclosingCellType, totalCellFaces, totalCellFacesNoDup, 0);
			cellStackPointer--;
			cellInits[cell->cellIndex] = 1;
			continue;
		}
		if (cellInits[cell->cellIndex]) {
			int32_t nextChild = -1;
			for (int32_t i = 0; i < 4; ++i) {
				if (!cellInits[cell->children[i].cellIndex] &&
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
			cellStack[cellStackPointer] = cell->children + nextChild;
			continue;
		}
		Vec2 midPoint = _(_(_(cell->boundsMax V2SUB cell->boundsMin) V2MULS .5) V2ADD cell->boundsMin);
		iVec2 signs;
		iVec2 commonSides;
		midPoint.x += (float)tileMin.x;
		midPoint.y += (float)tileMin.y;
		int32_t result = findFaceQuadrantUv(loopAmount, loopStart, verts, midPoint,
		                                  &commonSides, &signs);
		switch (result) {
			case 0: {
				addCellToEnclosingCells(cell, enclosingCellAmount, enclosingCellFaces,
										enclosingCellType, totalCellFaces, totalCellFacesNoDup, 0);
				cellStackPointer--;
				cellInits[cell->cellIndex] = 1;
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
		addCellToEnclosingCells(cell, enclosingCellAmount, enclosingCellFaces,
								enclosingCellType, totalCellFaces, totalCellFacesNoDup, 1);
		cellInits[cell->cellIndex] = 1;
		for (int32_t i = 0; i < 4; ++i) {
			cellInits[cell->children[i].cellIndex] = 0;
		}
		int32_t nextChild = 0;
		for (int32_t i = 0; i < 4; ++i) {
			if (*((int32_t *)(children + cellStackPointer) + i)) {
				nextChild = i;
				break;
			}
		}
		cellStackPointer++;
		cellStack[cellStackPointer] = cell->children + nextChild;
	} while (cellStackPointer >= 0);
}

Cell *findFullyEnclosingCellOld(Cell *rootCell, int32_t loopStart, int32_t loopEnd, int32_t *loops, Vec2 *verts) {
	Vec2 cellBoundsMin = {.x = .0, .y = .0};
	Vec2 cellBoundsMax = {.x = 1.0, .y = 1.0};
	Cell *cell = rootCell;
	int32_t depth = -1;
	while (true) {
		if (!cell->children) {
			return cell;
		}
		Vec2 midPoint = _(_(_(cellBoundsMax V2SUB cellBoundsMin) V2MULS .5) V2ADD cellBoundsMin);
		depth++;
		Vec2 *vert0 = verts + loopStart;
		int32_t childIndexVert0 = (vert0->x >= midPoint.x) + (vert0->y < midPoint.y) * 2;
		int32_t fullyEnclosed = true;
		for (int32_t i = loopStart + 1; i < loopEnd; ++i) {
			int32_t childIndexVerti = (verts[i].x >= midPoint.x) +
			                          (verts[i].y < midPoint.y) * 2;
			fullyEnclosed *= childIndexVerti == childIndexVert0;
		}
		if (!fullyEnclosed) {
			return cell;
		}
		cell = cell->children + childIndexVert0;
		cellBoundsMin = cell->boundsMin;
		cellBoundsMax = cell->boundsMax;
	};
}

Cell *findEnclosingCell(Cell *rootCell, Vec2 pos) {
	Vec2 cellBoundsMin = {.x = .0, .y = .0};
	Vec2 cellBoundsMax = {.x = 1.0, .y = 1.0};
	Cell *cell = rootCell;
	int32_t depth = -1;
	while (true) {
		if (!cell->children) {
			return cell;
		}
		Vec2 midPoint = _(_(_(cellBoundsMax V2SUB cellBoundsMin) V2MULS .5) V2ADD cellBoundsMin);
		depth++;
		int32_t childIndex = (pos.x >= midPoint.x) + (pos.y < midPoint.y) * 2;
		cell = cell->children + childIndex;
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

void addEnclosedVertsToCell(Cell *parentCell, Vec3 *vertBuffer,
                            int32_t *loopBuffer, int32_t *faceBuffer, int8_t *faceFlag) {
	// Get enclosed verts if not already present
	// First, determine which verts are enclosed, and mark them by negating
	Vec2 midPoint = parentCell->children[1].boundsMin;
	for (int32_t i = 0; i < parentCell->faceAmount; ++i) {
		int32_t face = parentCell->faces[i];
		int32_t faceStart = faceBuffer[face];
		int32_t faceEnd = faceBuffer[face + 1];
		int32_t faceLoopAmount = faceEnd - faceStart;
		int32_t isInside = 0;
		iVec2 insideBuffer = {0};
		int32_t inside[4];
		iVec2 signs;
		iVec2 commonSides;
		int32_t result = findFaceQuadrant(faceLoopAmount, faceStart,
		                                  vertBuffer, loopBuffer, midPoint,
		                                  &commonSides, &signs);
		if (result) {
			int32_t child = signs.x + signs.y * 2;
			faceFlag[i] = child + 1;
			parentCell->children[child].faceAmount++;
		}
		else {
			faceFlag[i] = -1;
			parentCell->edgeFaceAmount++;
		}
	}
	for (int32_t i = 0; i < 4; ++i) {
		Cell *cell = parentCell->children + i;
		if (cell->faceAmount) {
			cell->faces = malloc(sizeof(int32_t) * cell->faceAmount);
		}
	}
	if (parentCell->edgeFaceAmount) {
		parentCell->edgeFaces = malloc(sizeof(int32_t) * parentCell->edgeFaceAmount);
	}
	int32_t facesTop[4] = {0};
	int32_t edgeFacesTop = 0;
	for (int32_t i = 0; i < parentCell->faceAmount; ++i) {
		if (!faceFlag[i]) {
			continue;
		}
		if (faceFlag[i] > 0) {
			int32_t child = faceFlag[i] - 1;
			Cell *cell = parentCell->children + child;
			cell->faces[facesTop[child]] = parentCell->faces[i];
			facesTop[child]++;
		}
		else {
			parentCell->edgeFaces[edgeFacesTop] = parentCell->faces[i];
			edgeFacesTop++;
		}
		faceFlag[i] = 0;
	}
}

void allocateChildren(Cell *parentCell, Cell **cellStack, int32_t cellStackPointer,
                      Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer,
					  int8_t *faceFlag) {
	parentCell->children = calloc(4, sizeof(Cell));
	for (int32_t i = 0; i < 4; ++i) {
		// v for visualizing quadtree v
		//cell->children[i].cellIndex = rand();
		Cell *cell = parentCell->children + i;
		cell->cellIndex = cellIndex;
		cellIndex++;
		cell->localIndex = (uint32_t)i;
		setCellBounds(cell, parentCell, cellStackPointer);
	}
	leafAmount += 4;
}


void processCell(Cell **cellStack, int32_t *cellStackPointer, int32_t *cellStackBase,
                Cell *rootCell, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer,
                QuadTree *quadTree, int8_t *faceFlag) {
	Cell *cell = cellStack[*cellStackPointer];
	// If more than CELL_MAX_VERTS in cell, then subdivide cell
	int32_t hasChildren = cell->faceAmount > CELL_MAX_VERTS;
	if (hasChildren) {
		// Get number of children
		int32_t childAmount = 0;
		if (!cell->children) {
			leafAmount--;
			allocateChildren(cell, cellStack, *cellStackPointer,
			                 vertBuffer, loopBuffer, faceBuffer,
							 faceFlag);
			addEnclosedVertsToCell(cell, vertBuffer, loopBuffer, faceBuffer, faceFlag);
		}
		for (int32_t i = 0; i < 4; ++i) {
			childAmount += (int32_t)cell->children[i].initialized;
		}
		// If the cell has children, and they are not yet all initialized,
		// then add the next one to the stack
		if (childAmount < 4) {
			(*cellStackPointer)++;
			cellStack[*cellStackPointer] = cell->children + childAmount;
			return;
		}
	}
	// Otherwise, set the current cell as initialized, and pop it off the stack
	cell->initialized = 1;
	(*cellStackPointer)--;
}

int32_t calculateMaxTreeDepth(int32_t vertAmount) {
	return log(CELL_MAX_VERTS * vertAmount) / log(4) + 2;
}

void createQuadTree(QuadTree *quadTree, int32_t faceAmount, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer) {
	cellIndex = 0;
	leafAmount = 0;
	counter = 0;

	quadTree->rootCell = calloc(1, sizeof(Cell));
	Cell *rootCell = quadTree->rootCell;
	quadTree->maxTreeDepth = 32;

	Cell *cellStack[32];
	rootCell->cellIndex = cellIndex;
	cellIndex++;
	cellStack[0] = rootCell;
	rootCell->boundsMax.x = rootCell->boundsMax.y = 1.0f;
	rootCell->initialized = 1;
	int8_t *faceFlag = calloc(faceAmount, sizeof(int8_t));
	rootCell->faceAmount = faceAmount;
	rootCell->faces = malloc(sizeof(int32_t) * faceAmount);
	for (int32_t i = 0; i < faceAmount; ++i) {
		rootCell->faces[i] = i;
	}
	allocateChildren(rootCell, cellStack, 0, vertBuffer, loopBuffer, faceBuffer, faceFlag);
	addEnclosedVertsToCell(rootCell, vertBuffer, loopBuffer, faceBuffer, faceFlag);
	cellStack[1] = rootCell->children;
	int32_t cellStackPointer = 1;
	int32_t cellStackBase = 0;
	do {
		processCell(cellStack, &cellStackPointer, &cellStackBase,
		            rootCell, vertBuffer, loopBuffer, faceBuffer,
		            quadTree, faceFlag);
		counter++;
	} while(cellStackPointer >= 0);
	printf("Created quadTree -- cells: %d, leaves: %d\n", cellIndex, leafAmount);
}

void destroyQuadTree(Cell *rootCell, int32_t maxTreeDepth) {
	Cell *cellStack[32];
	cellStack[0] = rootCell;
	int32_t cellStackPointer = 0;
	do {
		Cell *cell = cellStack[cellStackPointer];
		int32_t nextChild = 0;
		if (cell->children) {
			cell->initialized = 0;
			for (int32_t i = 0; i < 4; ++i) {
				nextChild += cell->children[i].initialized == 0;
			}
			if (nextChild < 4) {
				cellStackPointer++;
				cellStack[cellStackPointer] = cell->children + nextChild;
				continue;
			}
			free(cell->children);
		}
		else {
			cell->initialized = 0;
		}
		if (cell->faces) {
			free(cell->faces);
		}
		if (cell->edgeFaces) {
			free(cell->edgeFaces);
		}
		cellStackPointer--;
	} while(cellStackPointer >= 0);
	free(rootCell);
}
