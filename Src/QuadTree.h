#pragma once
#include "Types.h"

#define CELL_MAX_VERTS 256


typedef struct Cell {
	uint32_t localIndex;
	uint32_t initialized;
	struct Cell *children;
	int32_t faceAmount;
	int32_t *faces;
	int32_t cellIndex;
	Vec2 boundsMin;
	Vec2 boundsMax;
} Cell;

typedef struct {
	Cell *rootCell;
	int32_t maxTreeDepth;
} QuadTree;

void calcCellBounds(Cell *cell);
Cell *findEnclosingCell(Cell *rootCell, Vec2 pos);
void allocateChildren(Cell *cell);
Cell *findFullyEnclosingCell(Cell *rootCell, int32_t loopStart, int32_t loopEnd, int32_t *loops, Vec2 *verts);
void addEnclosedVertsToCell(Cell **cellStack, Cell *cell, int32_t *cellStackPointer, int32_t *cellStackBase, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer);
void processCell(Cell **cellStack, int32_t *cellStackPointer, int32_t *cellStackBase, Cell *rootCell, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer);
int32_t calculateMaxTreeDepth(int32_t vertAmount);
void createQuadTree(Cell **rootCell, int32_t *maxTreeDepth, int32_t faceAmount, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer);
void destroyQuadTree(Cell *rootCell, int32_t maxtreedepth);
