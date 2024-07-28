#pragma once
#include <RUVM.h>
#include <RuvmInternal.h>
#include <Utils.h>
#include <Mesh.h>
#include <QuadTree.h>

typedef struct {
	V3_F32 normal;
	V2_F32 uv;
	V2_F32 vertBuf;
	int32_t bufLoop;
	int32_t bufFace;
	int32_t loop;
	int32_t edge;
	int8_t job;
} BoundsLoopBufEntry;

typedef struct {
	BoundsLoopBufEntry* pBuf;
	int32_t count;
} BoundsLoopBuf;

typedef struct {
	BoundsLoopBufEntry* pLoopBuf;
	int32_t *pMapLoopBuf;
	int32_t *pIndexTable;
	int32_t *pSortedVerts;
	int32_t size;
} MergeBufHandles;


typedef struct {
	int32_t start;
	int32_t loopLocal;
	int32_t loop;
	int32_t edge;
	int32_t vert;
} BorderInInfo;

typedef struct Piece {
	struct Piece *pNext;
	BorderFace *pEntry;
	BorderFace *pTail;
	FaceRange bufFace;
	int32_t edgeCount;
	int32_t edges[11];
	UBitField16 keepSingle;
	UBitField16 keepSeam;
	UBitField16 keepPreserve;
	UBitField16 keepVertPreserve;
	UBitField16 keepOnInVert;
	UBitField16 skip;
	uint8_t order[11];
	int32_t entryIndex;
	V3_F32 realNormal;
	bool listed;
	bool triangulate;
	bool hasSeam;
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

typedef struct {
	int32_t *pArr;
	int32_t count;
} PieceRootsArr;

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

typedef struct {
	BorderFace **ppTable;
	int32_t count;
} CompiledBorderTable;

typedef struct {
	RuvmContext pContext;
	RuvmMap pMap;
	Mesh *pMeshOut;
	InFaceArr **ppInFaceTable;
	SendOffArgs *pJobArgs;
	EdgeVerts *pEdgeVerts;
	int8_t *pVertSeamTable;
	bool* pEdgeSeamTable;
	void *pMutex;
	CompiledBorderTable *pBorderTable;
	JobBases *pJobBases;
	CombineTables *pCTables;
	int32_t *pJobsCompleted;
	PieceArr *pPieceArrTable;
	PieceRootsArr *pPieceRootTable;
	int32_t *pTotalVertTable;
	int32_t entriesStart;
	int32_t entriesEnd;
	int32_t job;
} MergeSendOffArgs;

void ruvmMergeBorderFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
					      JobBases *pJobBases, int8_t *pVertSeamTable,
                          bool *pEdgeSeamTable, InFaceArr **ppInFaceTable);
void ruvmAllocMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle,
                        int32_t totalVerts);
void ruvmMergeSingleBorderFace(MergeSendOffArgs *pArgs, uint64_t *pTimeSpent,
                               int32_t entryIndex, PieceArr *pPieceArr,
							   FaceRange *pRuvmFace,
							   MergeBufHandles *pMergeBufHandles,
                               int32_t *pInFaces, int32_t entryCount);
void ruvmDestroyMergeBufs(RuvmContext pContext, MergeBufHandles *pHandle);
void ruvmCombineJobMeshes(RuvmContext pContext, RuvmMap pMap,  Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  int8_t *pVertSeamTable, bool *pEdgeSeamTable,
                          InFaceArr **ppInFaceTable);
BorderInInfo getBorderEntryInInfo(const BorderFace *pEntry,
                                  const SendOffArgs *pJobArgs, int32_t loopIndex);
_Bool getIfRuvm(const BorderFace *pEntry, int32_t loopIndex);
_Bool getIfOnInVert(const BorderFace *pEntry, int32_t loopIndex);
_Bool getIfOnLine(const BorderFace *pEntry, int32_t loopIndex);
int32_t getMapLoop(const BorderFace *pEntry,
                   const RuvmMap pMap, int32_t loopIndex);
int32_t bufMeshGetVertIndex(const Piece *pPiece,
                            const BufMesh *pBufMesh, int32_t localLoop);
int32_t bufMeshGetEdgeIndex(const Piece *pPiece,
                            const BufMesh *pBufMesh, int32_t localLoop);
