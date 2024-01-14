#include "QuadTree.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

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

void findFullyEnclosingCell(iVec2 tileMin, int32_t *enclosingCellAmount,
                            Cell **enclosingCellFaces, int32_t *totalCellFaces,
                            Cell *rootCell, int32_t loopStart, int32_t loopEnd,
                            int32_t *loops, Vec2 *verts) {
	typedef struct {
		int32_t a;
		int32_t b;
		int32_t c;
		int32_t d;
	} Children;
	Cell *cellStack[16];
	Children children[16];
	cellStack[0] = rootCell;
	rootCell->initialized = 0;
	int32_t cellStackPointer = 0;
	int32_t loopAmount = loopEnd - loopStart;
	do {
		Cell *cell = cellStack[cellStackPointer];
		if (!cell->children) {
			enclosingCellFaces[*enclosingCellAmount] = cell;
			++*enclosingCellAmount;
			*totalCellFaces += cell->faceAmount;
			cellStackPointer--;
			cell->initialized = 1;
			continue;
		}
		if (cell->initialized) {
			int32_t nextChild = -1;
			for (int32_t i = 0; i < 4; ++i) {
				if (!cell->children[i].initialized &&
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
		midPoint.x += (float)tileMin.x;
		midPoint.y += (float)tileMin.y;
		iVec2 sides[loopAmount];
		iVec2 commonSides = {1, 1};
		for (int32_t i = 0; i < loopAmount; ++i) {
			int32_t loopIndex = loopStart + i;
			sides[i].x = verts[loopIndex].x >= midPoint.x;
			sides[i].y = verts[loopIndex].y < midPoint.y;
			for (int32_t j = 0; j < i; ++j) {
				commonSides.x *= sides[i].x == sides[j].x;
				commonSides.y *= sides[i].y == sides[j].y;
			}
		}
		if (!commonSides.x && !commonSides.y) {
			enclosingCellFaces[*enclosingCellAmount] = cell;
			++*enclosingCellAmount;
			*totalCellFaces += cell->faceAmount;
			cellStackPointer--;
			cell->initialized = 1;
			continue;
		}
		iVec2 signs = {
			.x = sides[0].x,
			.y = sides[0].y
		};
		if (commonSides.x && commonSides.y) {
			children[cellStackPointer].a = !signs.x && !signs.y;
			children[cellStackPointer].b = signs.x && !signs.y;
			children[cellStackPointer].c = !signs.x && signs.y;
			children[cellStackPointer].d = signs.x && signs.y;
		}
		else {
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
		}
		cell->initialized = 1;
		for (int32_t i = 0; i < 4; ++i) {
			cell->children[i].initialized = 0;
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

void allocateChildren(Cell *cell) {
	cell->children = malloc(sizeof(Cell) * 4);
	for (int32_t i = 0; i < 4; ++i) {
		cell->children[i].cellIndex = rand();
		cellIndex++;
		cell->children[i].localIndex = (uint32_t)i;
		cell->children[i].initialized = 0u;
		cell->children[i].children = NULL;
		cell->children[i].faceAmount = -1;
		cell->children[i].faces = NULL;
	}
	leafAmount += 4;
}

void addEnclosedVertsToCell(Cell **cellStack, Cell *cell, int32_t *cellStackPointer, int32_t *cellStackBase, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer) {
	calcCellBounds(cell);
	_(&cell->boundsMin V2DIVSEQL (float)pow(2.0, *cellStackPointer - 1));
	_(&cell->boundsMax V2DIVSEQL (float)pow(2.0, *cellStackPointer - 1));
	Vec2 *ancestorBoundsMin = &cellStack[*cellStackPointer - 1]->boundsMin;
	_(&cell->boundsMin V2ADDEQL *ancestorBoundsMin);
	_(&cell->boundsMax V2ADDEQL *ancestorBoundsMin);
	
	// Get enclosed verts if not already present
	// First, determine which verts are enclosed, and mark them by negating
	Cell* parentCell = cellStack[*cellStackPointer - 1];
	cell->faceAmount = 0;
	for (int32_t i = 0; i < parentCell->faceAmount; ++i) {
		int32_t face = parentCell->faces[i] - 1;
		int32_t faceStart = faceBuffer[face];
		int32_t faceEnd = faceBuffer[face + 1];
		int32_t faceLoopAmount = faceEnd - faceStart;
		int32_t isInside = 0;
		for (int32_t j = 0; j < faceLoopAmount; ++j) {
			int32_t vert = loopBuffer[faceStart + j];
			isInside += (vertBuffer[vert].x >= cell->boundsMin.x) &&
			                (vertBuffer[vert].y >= cell->boundsMin.y) &&
			                (vertBuffer[vert].x < cell->boundsMax.x) &&
			                (vertBuffer[vert].y < cell->boundsMax.y);
		}
		if (isInside) {
			parentCell->faces[i] *= -1;
			cell->faceAmount++;
		}
	}
	// Now that the amount is known, allocate the array for the current cell,
	// and copy over the marked verts from the parent cell
	cell->faces = malloc(sizeof(int32_t) * cell->faceAmount);
	int32_t facesNextIndex = 0;
	for (int32_t i = 0; i < parentCell->faceAmount; ++i) {
		if (parentCell->faces[i] < 0) {
			cell->faces[facesNextIndex] = parentCell->faces[i] *= -1;
			facesNextIndex++;
		}
	}
}

void processCell(Cell **cellStack, int32_t *cellStackPointer, int32_t *cellStackBase, Cell *rootCell, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer) {
	// First, calculate the positions of the cells bounding points
	Cell *cell = cellStack[*cellStackPointer];
	if (cell->faceAmount < 0) {
		addEnclosedVertsToCell(cellStack, cell, cellStackPointer, cellStackBase, vertBuffer, loopBuffer, faceBuffer);
	}

	// If more than CELL_MAX_VERTS in cell, then subdivide cell
	int32_t hasChildren = cell->faceAmount > CELL_MAX_VERTS;
	if (hasChildren) {
		// Get number of children
		int32_t childAmount = 0;
		if (!cell->children) {
			leafAmount--;
			allocateChildren(cell);
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

void createQuadTree(Cell **rootCell, int32_t *maxTreeDepth, int32_t faceAmount, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer) {
	cellIndex = 0;
	leafAmount = 0;
	counter = 0;

	*rootCell = malloc(sizeof(Cell));
	*maxTreeDepth = 32;

	Cell *cellStack[32];
	(*rootCell)->cellIndex = cellIndex;
	cellIndex++;
	cellStack[0] = *rootCell;
	(*rootCell)->boundsMin.x = (*rootCell)->boundsMin.y = .0f;
	(*rootCell)->boundsMax.x = (*rootCell)->boundsMax.y = 1.0f;
	(*rootCell)->localIndex = 0;
	(*rootCell)->initialized = 1;
	allocateChildren(*rootCell);
	(*rootCell)->faceAmount = faceAmount;
	(*rootCell)->faces = malloc(sizeof(int32_t) * faceAmount);
	for (int32_t i = 0; i < faceAmount; ++i) {
		(*rootCell)->faces[i] = i + 1;
	}
	cellStack[1] = (*rootCell)->children;
	int32_t cellStackPointer = 1;
	int32_t cellStackBase = 0;
	do {
		processCell(cellStack, &cellStackPointer, &cellStackBase, *rootCell, vertBuffer, loopBuffer, faceBuffer);
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
		cellStackPointer--;
	} while(cellStackPointer >= 0);
	free(rootCell);
}
