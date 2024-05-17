#pragma once
#include <RUVM.h>
#include <RuvmInternal.h>
#include <Utils.h>
#include <QuadTree.h>

typedef struct {
	int32_t start;
	int32_t loopLocal;
	int32_t loop;
	int32_t edge;
	int32_t vert;
} BorderInInfo;

typedef struct {
	void *pLoopBuf;
	void *pMapLoopBuf;
	void *pIndexTable;
	void *pSortedUvs;
	int32_t size;
} MergeBufHandles;

typedef struct Piece {
	struct Piece *pNext;
	BorderFace *pEntry;
	BorderFace *pTail;
	FaceRange bufFace;
	int32_t edgeCount;
	int32_t edges[11];
	int32_t entryIndex;
	UBitField16 keepSingle;
	UBitField16 keepSeam;
	UBitField16 keepPreserve;
	UBitField16 keepOnInVert;
	_Bool listed;
} Piece;

typedef struct BorderEdge {
	struct BorderEdge *pNext;
	int32_t edge;
	int32_t inEdge;
	int32_t mapFace;
	_Bool valid;
} BorderEdge;

typedef struct {
	Piece *pArr;
	int32_t count;
} PieceArr;

typedef struct OnLine {
	struct OnLine *pNext;
	int32_t baseEdgeOrLoop;
	int32_t ruvmVert;
	int32_t outVert;
	int32_t type;
} OnLine;

typedef struct BorderVert {
	struct BorderVert *pNext;
	int32_t entryIndex;
	int32_t ruvmFace;
	int32_t ruvmEdge;
	int32_t vert;
	int32_t tile;
	int32_t loops;
	int32_t baseEdge;
	int32_t baseVert;
	int32_t loopIndex;
	int32_t loop;
	int8_t job;
	_Bool keepBaseLoop;
} BorderVert;

typedef struct {
	BorderVert *pVertTable;
	OnLine *pOnLineTable;
	BorderEdge *pEdgeTable;
	int32_t vertTableSize;
	int32_t onLineTableSize;
	int32_t edgeTableSize;
} CombineTables;

typedef struct {
	int32_t vertBase;
	int32_t edgeBase;
} JobBases;

void ruvmMergeBorderFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  JobBases *pJobBases, int8_t *pVertSeamTable);

void ruvmAllocMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle,
                        int32_t totalVerts);
void ruvmMergeSingleBorderFace(uint64_t *pTimeSpent, RuvmContext pContext,
                               RuvmMap pMap, Mesh *pMeshOut,
							   SendOffArgs *pJobArgs, int32_t entryCount,
							   PieceArr *pPieceArr,
							   CombineTables *pCTables, JobBases *pJobBases,
							   FaceRange *pRuvmFace, MergeBufHandles *pMergeBufHandles,
							   int32_t approxVertsPerPiece);
void ruvmDestroyMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle);
void ruvmCombineJobMeshes(RuvmContext pContext, RuvmMap pMap,  RuvmMesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  int8_t *pVertSeamTable);
BorderInInfo getBorderEntryInInfo(const BorderFace *pEntry,
                                  const SendOffArgs *pJobArgs, int32_t loopIndex);
_Bool getIfRuvm(const BorderFace *pEntry, int32_t loopIndex);
_Bool getIfOnInVert(const BorderFace *pEntry, int32_t loopIndex);
_Bool getIfOnLine(const BorderFace *pEntry, int32_t loopIndex);
int32_t getMapLoop(const BorderFace *pEntry,
                   const RuvmMap pMap, int32_t loopIndex);
