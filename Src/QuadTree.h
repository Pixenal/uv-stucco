#pragma once
#include <MathUtils.h>
#include <Utils.h>
#include <Error.h>
#include <Types.h>

#define CELL_MAX_VERTS 32

typedef struct Cell {
	struct Cell *pChildren;
	I32 *pFaces;
	I32 *pEdgeFaces;
	I32 *pLinkEdges;
	Range* pLinkEdgeRanges;
	V2_F32 boundsMin;
	V2_F32 boundsMax;
	U32 localIdx;
	U32 initialized;
	I32 faceSize;
	I32 edgeFaceSize;
	I32 cellIdx;
	I32 linkEdgeSize;
} Cell;

typedef struct {
	Cell *pArr;
	I32 size;
} CellTable;

typedef struct {
	CellTable cellTable;
	Cell *pRootCell;
	I32 *pEdges;
	I32 *pEdgeFaces;
	I32 edgeSize;
	I32 edgeFaceSize;
	I32 cellCount;
	I32 leafCount;
} QuadTree;

typedef struct {
	Range *pRangeBuf;
	I32 *pCells;
	I8 *pCellType;
	I32 cellSize;
	I32 faceTotal;
	I32 faceTotalNoDup;
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
	FaceCells *pFaceCells;
	I32 cellFacesTotal;
	I32 cellFacesMax;
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

void stucDestroyFaceCellsTable(
	const StucAlloc *pAlloc,
	FaceCellsTable *pFaceCellsTable,
	Range faceRange
);
void stucDestroyFaceCellsEntry(const StucAlloc *pAlloc, FaceCells *pEntry);
void stucInitQuadTreeSearch(QuadTreeSearch *pState);
Result stucGetCellsForSingleFace(
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
Result stucCreateQuadTree(StucContext pCtx, QuadTree *pTree, const Mesh *pMesh);
void stucDestroyQuadTree(StucContext pCtx, QuadTree *pTree);
Result stucGetEncasingCells(
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
