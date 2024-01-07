#include "QuadTree.h"
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

extern int32_t cellIndex;
extern int32_t leafAmount;

void calcCellBounds(Cell *cell, Vec2 *boundsMin, Vec2 *boundsMax) {
	float xSide = (float)(cell->localIndex % 2);
	float ySide = (float)(((cell->localIndex + 2) / 2) % 2);
	boundsMin->x = xSide * .5;
	boundsMin->y = ySide * .5;
	boundsMax->x = 1.0 - (1.0 - xSide) * .5;
	boundsMax->y = 1.0 - (1.0 - ySide) * .5;
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
		Vec2 parentBoundsMin = cellBoundsMin;
		calcCellBounds(cell, &cellBoundsMin, &cellBoundsMax);
		_(&cellBoundsMin V2DIVSEQL (float)pow(2.0, depth));
		_(&cellBoundsMax V2DIVSEQL (float)pow(2.0, depth));
		_(&cellBoundsMin V2ADDEQL parentBoundsMin);
		_(&cellBoundsMax V2ADDEQL parentBoundsMin);
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
		cell->children[i].vertAmount = -1;
		cell->children[i].verts = NULL;
	}
	leafAmount += 4;
}

void addEnclosedVertsToCell(Cell **cellStack, Cell *cell, int32_t *cellStackPointer, int32_t *cellStackBase, Vert *vertBuffer) {
	Vec2 cellBoundsMin, cellBoundsMax;
	calcCellBounds(cell, &cellBoundsMin, &cellBoundsMax);
	_(&cellBoundsMin V2DIVSEQL (float)pow(2.0, *cellStackPointer - 1));
	_(&cellBoundsMax V2DIVSEQL (float)pow(2.0, *cellStackPointer - 1));
	for (int32_t i = (*cellStackPointer) - 1; i > *cellStackBase; --i) {
		Vec2 ancestorBoundsMin, ancestorBoundsMax;
		calcCellBounds(cellStack[i], &ancestorBoundsMin, &ancestorBoundsMax);
		_(&ancestorBoundsMin V2DIVSEQL (float)pow(2.0, i - 1));
		_(&cellBoundsMin V2ADDEQL ancestorBoundsMin);
		_(&cellBoundsMax V2ADDEQL ancestorBoundsMin);
	}
	// Get enclosed verts if not already present
	// First, determine which verts are enclosed, and mark them by negating
	cell->vertAmount = 0;
	Cell* parentCell = cellStack[*cellStackPointer - 1];
	for (int32_t i = 0; i < parentCell->vertAmount; ++i) {
		int32_t vert = parentCell->verts[i] - 1;
		int32_t isInside = (vertBuffer[vert].pos.x >= cellBoundsMin.x) &&
			(vertBuffer[vert].pos.y >= cellBoundsMin.y) &&
			(vertBuffer[vert].pos.x < cellBoundsMax.x) &&
			(vertBuffer[vert].pos.y < cellBoundsMax.y);
		if (isInside) {
			parentCell->verts[i] *= -1;
			cell->vertAmount++;
		}
	}
	// Now that the amount is known, allocate the array for the current cell,
	// and copy over the marked verts from the parent cell
	cell->verts = malloc(sizeof(int32_t) * cell->vertAmount);
	int32_t vertsNextIndex = 0;
	for (int32_t i = 0; i < parentCell->vertAmount; ++i) {
		if (parentCell->verts[i] < 0) {
			cell->verts[vertsNextIndex] = parentCell->verts[i] *= -1;
			vertsNextIndex++;
		}
	}
}

void processCell(Cell **cellStack, int32_t *cellStackPointer, int32_t *cellStackBase, Cell *rootCell, Vert *vertBuffer) {
	// First, calculate the positions of the cells bounding points
	Cell *cell = cellStack[*cellStackPointer];
	if (cell->vertAmount < 0) {
		addEnclosedVertsToCell(cellStack, cell, cellStackPointer, cellStackBase, vertBuffer);
	}

	// If more than CELL_MAX_VERTS in cell, then subdivide cell
	int32_t hasChildren = cell->vertAmount > CELL_MAX_VERTS;
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

void createQuadTree(Cell **rootCell, int32_t *maxTreeDepth, int32_t vertAmount, Vert *vertBuffer) {
	*rootCell = malloc(sizeof(Cell));
	*maxTreeDepth = calculateMaxTreeDepth(vertAmount);

	Cell **cellStack = malloc(sizeof(Cell *) * *maxTreeDepth);
	(*rootCell)->cellIndex = cellIndex;
	cellIndex++;
	cellStack[0] = *rootCell;
	(*rootCell)->localIndex = 0;
	(*rootCell)->initialized = 1;
	allocateChildren(*rootCell);
	(*rootCell)->vertAmount = vertAmount;
	(*rootCell)->verts = malloc(sizeof(int32_t) * vertAmount);
	for (int32_t i = 0; i < vertAmount; ++i) {
		(*rootCell)->verts[i] = i + 1;
	}
	cellStack[1] = (*rootCell)->children;
	int32_t cellStackPointer = 1;
	int32_t cellStackBase = 0;
	do {
		processCell(cellStack, &cellStackPointer, &cellStackBase, *rootCell, vertBuffer);
	} while(cellStackPointer >= 0);
	free(cellStack);
}
