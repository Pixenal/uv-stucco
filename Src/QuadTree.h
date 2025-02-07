#pragma once
#include <MathUtils.h>
#include <Utils.h>
#include <Error.h>

#define CELL_MAX_VERTS 32

typedef struct Cell {
	uint32_t localIdx;
	uint32_t initialized;
	struct Cell *pChildren;
	int32_t faceSize;
	int32_t *pFaces;
	int32_t edgeFaceSize;
	int32_t *pEdgeFaces;
	int32_t cellIdx;
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
	int32_t cellCount;
	int32_t leafCount;
} QuadTree;

typedef struct {
	Range *pRangeBuf;
	int32_t *pCells;
	int32_t cellSize;
	int32_t faceTotal;
	int32_t faceTotalNoDup;
	int8_t *pCellType;
} EncasingCells;

typedef struct {
	FaceBounds faceBounds;
	int32_t *pCells;
	int8_t *pCellType;
	Range* pRanges;
	int32_t cellSize;
	int32_t faceSize;
} FaceCells;

typedef struct {
	int32_t cellFacesTotal;
	int32_t cellFacesMax;
	FaceCells *pFaceCells;
	int32_t uniqueFaces;
} FaceCellsTable;

typedef struct {
	StucMap pMap;
	StucAlloc *pAlloc;
	int32_t *pCells;
	int8_t *pCellInits;
	int8_t *pCellFlags;
	int8_t *pCellType;
} QuadTreeSearch;

void stucInitFaceCellsTable(StucAlloc *pAlloc, FaceCellsTable *pTable,
                            int32_t faceCount);
void stucDestroyFaceCellsTable(StucAlloc *pAlloc,
                               FaceCellsTable *pFaceCellsTable);
void stucDestroyFaceCellsEntry(StucAlloc *pAlloc, FaceCells *pEntry);
void stucInitQuadTreeSearch(StucAlloc *pAlloc, StucMap pMap, QuadTreeSearch *pState);
void stucGetCellsForSingleFace(QuadTreeSearch *pState, int32_t vertCount,
                               V2_F32 *pVerts, FaceCellsTable *pFaceCellsTable,
							   FaceBounds *pFaceBounds, int32_t faceIdx, Range faceRange);
void stucDestroyQuadTreeSearch(QuadTreeSearch *pState);
Cell *stucFindEncasingCell(Cell *rootCell, V2_F32 pos);
Result stucCreateQuadTree(StucContext pContext, StucMap pMap);
void stucDestroyQuadTree(StucContext pContext, QuadTree *pTree);
void getEncasingCells(StucAlloc *pAlloc, StucMap pMap, Range inFaceRange,
                      Mesh *pMeshIn, FaceCellsTable *pFaceCellsTable,
					  int8_t maskIdx, int32_t *pAverageMapFacesPerFace);
int32_t checkIfFaceIsInsideTile(int32_t vertCount, V2_F32 *pVerts,
                                FaceBounds *pFaceBounds, V2_I32 tileMin);
void getFaceBoundsForTileTest(FaceBounds *pFaceBounds,
                              Mesh *pMesh, FaceRange *pFace);
FaceCells *idxFaceCells(FaceCellsTable *pFaceCellsTable,
                        int32_t faceIdx, int32_t faceOffset);
