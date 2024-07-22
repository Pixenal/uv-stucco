#pragma once
#include <MathUtils.h>
#include <Utils.h>
#include <Error.h>

#define CELL_MAX_VERTS 32

typedef struct Cell {
	uint32_t localIndex;
	uint32_t initialized;
	struct Cell *pChildren;
	int32_t faceSize;
	int32_t *pFaces;
	int32_t edgeFaceSize;
	int32_t *pEdgeFaces;
	int32_t cellIndex;
	V2_F32 boundsMin;
	V2_F32 boundsMax;
	int32_t linkEdgeSize;
	int32_t *pLinkEdges;
	Range* pLinkEdgeRanges;
} Cell;

typedef struct {
	Cell *pArr;
	int32_t size;
} CellTable;

typedef struct {
	Cell *pRootCell;
	CellTable cellTable;
	int32_t edgeSize;
	int32_t *pEdges;
	int32_t edgeFaceSize;
	int32_t *pEdgeFaces;
	int32_t maxTreeDepth;
	int32_t cellCount;
	int32_t leafCount;
} QuadTree;

typedef struct {
	Cell **ppCells;
	int8_t *pCellType;
	Range *pRangeBuf;
	int32_t cellSize;
	int32_t faceTotal;
	int32_t faceTotalNoDup;
} EncasingCells;

typedef struct {
	Cell **pCells;
	int8_t *pCellType;
	Range* pRanges;
	int32_t cellSize;
	int32_t faceSize;
	FaceBounds faceBounds;
} FaceCells;

typedef struct {
	int32_t cellFacesTotal;
	int32_t cellFacesMax;
	FaceCells *pFaceCells;
	int32_t uniqueFaces;
} FaceCellsTable;

typedef struct {
	Cell **ppCells;
	int8_t *pCellInits;
	int8_t *pCellTable;
	int8_t *pCellType;
	RuvmAlloc *pAlloc;
	RuvmMap pMap;
} QuadTreeSearch;

void ruvmInitFaceCellsTable(RuvmAlloc *pAlloc, FaceCellsTable *pTable,
                            int32_t faceCount);
void ruvmDestroyFaceCellsTable(RuvmAlloc *pAlloc,
                               FaceCellsTable *pFaceCellsTable);
void ruvmInitQuadTreeSearch(RuvmAlloc *pAlloc, RuvmMap pMap, QuadTreeSearch *pState);
void ruvmGetCellsForSingleFace(QuadTreeSearch *pState, int32_t vertCount,
                               V2_F32 *pVerts, FaceCellsTable *pFaceCellsTable,
							   FaceBounds *pFaceBounds, int32_t tableOffset);
void ruvmLinearizeCellFaces(FaceCells *pFaceCells, int32_t *pCellFaces,
                            int32_t faceIndex);
void ruvmDestroyQuadTreeSearch(QuadTreeSearch *pState);
Cell *ruvmFindEncasingCell(Cell *rootCell, V2_F32 pos);
void ruvmCreateQuadTree(RuvmContext pContext, RuvmMap pMap);
void ruvmDestroyQuadTree(RuvmContext pContext, QuadTree *pTree);
void getEncasingCells(RuvmAlloc *pAlloc, RuvmMap pMap,
                      Mesh *pMeshIn, FaceCellsTable *pFaceCellsTable,
					  int32_t *pAverageMapFacesPerFace) ;
