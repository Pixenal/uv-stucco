#pragma once
#include "Types.h"

#define CELL_MAX_VERTS 256

typedef struct {
	Cell *rootCell;
	int32_t maxTreeDepth;
} QuadTree;

void calcCellBounds(Cell *cell);
Cell *findEnclosingCell(Cell *rootCell, Vec2 pos);
void allocateChildren(Cell *cell);
void findFullyEnclosingCell(iVec2 tileMin, int32_t *enclosingCellAmount,
                            Cell **enclosingCellFaces, int32_t *totalCellFaces,
                            int32_t *totalCellFacesNoDup, Cell *rootCell,
                            int32_t loopStart, int32_t loopEnd, int32_t *loops,
                            Vec2 *verts);
void addEnclosedVertsToCell(Cell **cellStack, Cell *cell, int32_t *cellStackPointer, int32_t *cellStackBase, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer);
void processCell(Cell **cellStack, int32_t *cellStackPointer, int32_t *cellStackBase, Cell *rootCell, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer);
int32_t calculateMaxTreeDepth(int32_t vertAmount);
void createQuadTree(Cell **rootCell, int32_t *maxTreeDepth, int32_t faceAmount, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer);
void destroyQuadTree(Cell *rootCell, int32_t maxtreedepth);
