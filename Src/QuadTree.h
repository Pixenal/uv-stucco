#pragma once
#include <MathUtils.h>
#include <Utils.h>
#include <Error.h>
#include <Types.h>

#define CELL_MAX_VERTS 32

typedef struct Cell {
	U32 localIdx;
	U32 initialized;
	struct Cell *pChildren;
	I32 faceSize;
	I32 *pFaces;
	I32 edgeFaceSize;
	I32 *pEdgeFaces;
	I32 cellIdx;
	V2_F32 boundsMin;
	V2_F32 boundsMax;
	I32 linkEdgeSize;
	I32 *pLinkEdges;
	Range* pLinkEdgeRanges;
} Cell;

typedef struct {
	Cell *pArr;
	I32 size;
} CellTable;

typedef struct {
	Cell *pRootCell;
	CellTable cellTable;
	I32 edgeSize;
	I32 *pEdges;
	I32 edgeFaceSize;
	I32 *pEdgeFaces;
	I32 cellCount;
	I32 leafCount;
} QuadTree;

typedef struct {
	Range *pRangeBuf;
	I32 *pCells;
	I32 cellSize;
	I32 faceTotal;
	I32 faceTotalNoDup;
	I8 *pCellType;
} EncasingCells;

typedef struct {
	FaceBounds faceBounds;
	I32 *pCells;
	I8 *pCellType;
	Range* pRanges;
	I32 cellSize;
	I32 faceSize;
} FaceCells;

typedef struct {
	I32 cellFacesTotal;
	I32 cellFacesMax;
	FaceCells *pFaceCells;
	I32 uniqueFaces;
} FaceCellsTable;

typedef struct {
	const StucMap pMap;
	const StucAlloc *pAlloc;
	I32 *pCells;
	I8 *pCellInits;
	I8 *pCellFlags;
	I8 *pCellType;
} QuadTreeSearch;

void stucInitFaceCellsTable(
	const StucAlloc *pAlloc,
	FaceCellsTable *pTable,
	I32 faceCount
);
void stucDestroyFaceCellsTable(
	const StucAlloc *pAlloc,
	FaceCellsTable *pFaceCellsTable
);
void stucDestroyFaceCellsEntry(const StucAlloc *pAlloc, FaceCells *pEntry);
void stucInitQuadTreeSearch(QuadTreeSearch *pState);
void stucGetCellsForSingleFace(
	QuadTreeSearch *pState,
	I32 vertCount,
	V2_F32 *pVerts,
	FaceCellsTable *pFaceCellsTable,
	FaceBounds *pFaceBounds,
	I32 faceIdx,
	Range faceRange
);
void stucDestroyQuadTreeSearch(QuadTreeSearch *pState);
Cell *stucFindEncasingCell(Cell *rootCell, V2_F32 pos);
Result stucCreateQuadTree(StucContext pCtx, StucMap pMap);
void stucDestroyQuadTree(StucContext pCtx, QuadTree *pTree);
void stucGetEncasingCells(
	const StucAlloc *pAlloc,
	const StucMap pMap,
	const Mesh *pInMesh,
	I8 maskIdx,
	Range faceRange,
	FaceCellsTable *pFaceCellsTable,
	I32 *pAverageMapFacesPerFace
);
I32 stucCheckIfFaceIsInsideTile(
	I32 vertCount,
	V2_F32 *pVerts,
	FaceBounds *pFaceBounds,
	V2_I32 tileMin
);
void stucGetFaceBoundsForTileTest(
	FaceBounds *pFaceBounds,
	const Mesh *pMesh,
	FaceRange *pFace
);
FaceCells *stucIdxFaceCells(
	FaceCellsTable *pFaceCellsTable,
	I32 faceIdx,
	I32 faceOffset
);
