#pragma once
#include "Types.h"

#define CELL_MAX_VERTS 256

typedef struct {
	Cell *pRootCell;
	int32_t edgeAmount;
	int32_t *pEdges;
	int32_t edgeFaceAmount;
	int32_t *pEdgeFaces;
	int32_t maxTreeDepth;
} QuadTree;

typedef struct {
	int32_t cellAmount;
	Cell *cells[256];
	int32_t cellType[256];
	int32_t faceTotal;
	int32_t faceTotalNoDup;
} EnclosingCellsInfo;

void calcCellBounds(Cell *pCell);
Cell *findEnclosingCell(Cell *pRootCell, Vec2 pos);
void allocateChildren(Cell *parentCell, int32_t cellStackPointer);
void quadTreeGetAllEnclosingCells(iVec2 tileMin, EnclosingCellsInfo *pEnclosingCellsInfo,
									 Cell *pRootCell, int32_t loopStart, int32_t loopEnd,
							         MeshData *pMesh, int8_t *pCellInits);
void addEnclosedVertsToCell(Cell *pParentCell, MeshData *pMesh, int8_t *pFaceFlag);
void processCell(Cell **pCellStack, int32_t *pCellStackPointer, MeshData *pMesh,
                 int8_t *pFaceFlag);
int32_t calculateMaxTreeDepth(int32_t vertAmount);
void createQuadTree(QuadTree *pQuadTree, MeshData *pMesh);
void destroyQuadTree(Cell *pRootCell);
