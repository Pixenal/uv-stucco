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
	int32_t end;
	int32_t size;
	int32_t loopLocal;
	int32_t edgeLoop;
	int32_t vertLoop;
	int32_t edge;
	int32_t vert;
} BorderInInfo;

typedef struct {
	int32_t edge;
	int32_t segment;
} EdgeSegmentPair;

// // indicates only used in createAndJoinPieces
typedef struct Piece {
	struct Piece *pNext;
	BorderFace *pEntry;
	FaceRange bufFace;
	int32_t edgeCount;//
	EdgeSegmentPair *pEdges;
	UBitField64 keepSeam;//
	UBitField64 keepPreserve;
	UBitField64 keepVertPreserve;//
	UBitField64 add;
	uint8_t *pOrder;
	int32_t entryIndex;//
	V2_I16 tile;
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
	bool valid;
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
	V2_I16 tile;
	int32_t loops;
	int32_t baseEdge;
	int32_t baseVert;
	int32_t loopIndex;
	int32_t loop;
	int8_t job;
	bool keepBaseLoop;
	bool divided;
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

typedef struct MergeSendOffArgs {
	RuvmContext pContext;
	RuvmMap pMap;
	struct MergeSendOffArgs *pArgArr;
	Mesh *pMeshOut;
	InFaceArr **ppInFaceTable;
	SendOffArgs *pJobArgs;
	EdgeVerts *pEdgeVerts;
	int8_t *pVertSeamTable;
	bool* pEdgeSeamTable;
	void *pMutex;
	UBitField8 *pInVertKeep;
	CompiledBorderTable *pBorderTable;
	JobBases *pJobBases;
	CombineTables *pCTables;
	int32_t *pActiveJobs;
	void *pBarrier;
	PieceArr *pPieceArrTable;
	PieceRootsArr *pPieceRootTable;
	int32_t *pTotalVertTable;
	int32_t totalVerts;
	Mesh *pInMesh;
	int32_t *pLoopMergeTable;
	int32_t entriesStart;
	int32_t entriesEnd;
	int32_t job;
	float wScale;
} MergeSendOffArgs;

void ruvmMergeBorderFaces(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
					      JobBases *pJobBases, int8_t *pVertSeamTable,
                          bool *pEdgeSeamTable, InFaceArr **ppInFaceTable,
                          float wScale, Mesh *pInMesh, int32_t mapJobsSent);
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
                          InFaceArr **ppInFaceTable, float wScale, Mesh *pInMesh,
                          int32_t mapJobsSent);
BorderInInfo getBorderEntryInInfo(const BorderFace *pEntry,
                                  const SendOffArgs *pJobArgs, int32_t loopIndex);
bool getIfRuvm(const BorderFace *pEntry, int32_t loopIndex);
bool getIfOnInVert(const BorderFace *pEntry, int32_t loopIndex);
bool getIfOnLine(const BorderFace *pEntry, int32_t loopIndex);
int32_t getSegment(const BorderFace *pEntry, int32_t loopIndex);
int32_t getMapLoop(const BorderFace *pEntry, int32_t loopIndex);
int32_t getBaseLoop(const BorderFace *pEntry, int32_t loopIndex);
V2_I16 getTileMinFromBoundsEntry(BorderFace *pEntry);
int32_t bufMeshGetVertIndex(const Piece *pPiece,
                            const BufMesh *pBufMesh, int32_t localLoop);
int32_t bufMeshGetEdgeIndex(const Piece *pPiece,
                            const BufMesh *pBufMesh, int32_t localLoop);
