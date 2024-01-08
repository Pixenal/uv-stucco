#pragma once
#include "Types.h"

#define CELL_MAX_VERTS 16


typedef struct Cell {
	uint32_t localIndex;
	uint32_t initialized;
	struct Cell *children;
	int32_t vertAmount;
	int32_t *verts;
	int32_t cellIndex;
} Cell;

typedef struct {
	Cell *rootCell;
	int32_t maxTreeDepth;
} QuadTree;

void calcCellBounds(Cell *cell, Vec2 *boundsMin, Vec2 *boundsMax);
Cell *findEnclosingCell(Cell *rootCell, Vec2 pos);
void allocateChildren(Cell *cell);
void addEnclosedVertsToCell(Cell **cellStack, Cell *cell, int32_t *cellStackPointer, int32_t *cellStackBase, Vert *vertBuffer);
void processCell(Cell **cellStack, int32_t *cellStackPointer, int32_t *cellStackBase, Cell *rootCell, Vert *vertBuffer);
int32_t calculateMaxTreeDepth(int32_t vertAmount);
void createQuadTree(Cell **rootCell, int32_t *maxTreeDepth, int32_t vertAmount, Vert *vertBuffer);
void destroyQuadTree(Cell *rootCell, int32_t maxtreedepth);
