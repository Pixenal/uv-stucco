#pragma once
#include "Types.h"

#define CELL_MAX_VERTS 256

typedef struct {
	Cell *rootCell;
	int32_t edgeAmount;
	int32_t *edges;
	int32_t edgeFaceAmount;
	int32_t *edgeFaces;
	int32_t maxTreeDepth;
} QuadTree;

void calcCellBounds(Cell *cell);
Cell *findEnclosingCell(Cell *rootCell, Vec2 pos);
void allocateChildren(Cell *cell, Cell **cellStack, int32_t cellStackPointer,
                      Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer,
					  int8_t *faceFlag);
void findFullyEnclosingCell(iVec2 tileMin, int32_t *enclosingCellAmount,
                            Cell **enclosingCellFaces, int8_t *enclosingCellType,
							int32_t *totalCellFaces, int32_t *totalCellFacesNoDup,
							Cell *rootCell, int32_t loopStart, int32_t loopEnd,
							int32_t *loops, Vec2 *verts, int8_t *cellInits);
void addEnclosedVertsToCell(Cell *parentCell, Vec3 *vertBuffer,
                            int32_t *loopBuffer, int32_t *faceBuffer, int8_t *faceFlag);
void processCell(Cell **cellStack, int32_t *cellStackPointer, int32_t *cellStackBase,
                Cell *rootCell, Vec3 *vertBuffer, int32_t *loopBuffer, int32_t *faceBuffer,
                QuadTree *quadTree, int8_t *faceFlag);
int32_t calculateMaxTreeDepth(int32_t vertAmount);
void createQuadTree(QuadTree *quadTree, int32_t faceAmount, Vec3 *vertBuffer,
                    int32_t *loopBuffer, int32_t *faceBuffer);
void destroyQuadTree(Cell *rootCell, int32_t maxtreedepth);
