#pragma once
#include <UvStucco.h>
#include <UvStuccoIntern.h>
#include <Utils.h>
#include <Mesh.h>
#include <QuadTree.h>

typedef struct {
	V3_F32 normal;
	V2_F32 uv;
	V2_F32 vertBuf;
	int32_t bufCorner;
	int32_t bufFace;
	int32_t corner;
	int32_t edge;
	int8_t job;
} BoundsCornerBufEntry;

typedef struct {
	BoundsCornerBufEntry* pBuf;
	int32_t count;
} BoundsCornerBuf;

typedef struct {
	BoundsCornerBufEntry* pCornerBuf;
	int32_t *pMapCornerBuf;
	int32_t *pIdxTable;
	int32_t *pSortedVerts;
	int32_t size;
} MergeBufHandles;


typedef struct {
	int32_t start;
	int32_t end;
	int32_t size;
	int32_t cornerLocal;
	int32_t edgeCorner;
	int32_t vertCorner;
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
	int32_t entryIdx;//
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
	int32_t baseEdgeOrCorner;
	int32_t stucVert;
	int32_t outVert;
	int32_t type;
} OnLine;

typedef struct BorderVert {
	struct BorderVert *pNext;
	int32_t entryIdx;
	int32_t mapFace;
	int32_t mapEdge;
	int32_t vert;
	V2_I16 tile;
	int32_t corners;
	int32_t inEdge;
	int32_t inVert;
	int32_t cornerIdx;
	int32_t corner;
	int8_t job;
	bool keepBaseCorner;
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
	StucContext pContext;
	StucMap pMap;
	struct MergeSendOffArgs *pArgArr;
	Mesh *pMeshOut;
	InFaceArr **ppInFaceTable;
	SendOffArgs *pJobArgs;
	EdgeVerts *pEdgeVerts;
	int8_t *pVertSeamTable;
	bool* pEdgeSeamTable;
	UBitField8 *pInVertKeep;
	CompiledBorderTable *pBorderTable;
	JobBases *pJobBases;
	CombineTables *pCTables;
	int32_t jobCount;
	void *pBarrier;
	PieceArr *pPieceArrTable;
	PieceRootsArr *pPieceRootTable;
	int32_t *pTotalVertTable;
	int32_t totalVerts;
	Mesh *pInMesh;
	int32_t *pCornerMergeTable;
	int32_t entriesStart;
	int32_t entriesEnd;
	int32_t job;
	float wScale;
} MergeSendOffArgs;

Result stucMergeBorderFaces(StucContext pContext, StucMap pMap, Mesh *pMeshOut,
                            SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
                            JobBases *pJobBases, int8_t *pVertSeamTable,
                            bool *pEdgeSeamTable, InFaceArr **ppInFaceTable,
                            float wScale, Mesh *pInMesh, int32_t mapJobsSent);
void stucAllocMergeBufs(StucContext pContext, MergeBufHandles *pHandle,
                        int32_t totalVerts);
void stucMergeSingleBorderFace(MergeSendOffArgs *pArgs, uint64_t *pTimeSpent,
                               int32_t entryIdx, PieceArr *pPieceArr,
                               FaceRange *pStucFace,
                               MergeBufHandles *pMergeBufHandles,
                               int32_t *pInFaces, int32_t entryCount);
void stucDestroyMergeBufs(StucContext pContext, MergeBufHandles *pHandle);
Result stucCombineJobMeshes(StucContext pContext, StucMap pMap,  Mesh *pMeshOut,
                            SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
                            int8_t *pVertSeamTable, bool *pEdgeSeamTable,
                            InFaceArr **ppInFaceTable, float wScale, Mesh *pInMesh,
                            int32_t mapJobsSent);
BorderInInfo getBorderEntryInInfo(const BorderFace *pEntry,
                                  const SendOffArgs *pJobArgs, int32_t cornerIdx);
bool getIfStuc(const BorderFace *pEntry, int32_t cornerIdx);
bool getIfOnInVert(const BorderFace *pEntry, int32_t cornerIdx);
bool getIfOnLine(const BorderFace *pEntry, int32_t cornerIdx);
int32_t getSegment(const BorderFace *pEntry, int32_t cornerIdx);
int32_t getMapCorner(const BorderFace *pEntry, int32_t cornerIdx);
int32_t getBaseCorner(const BorderFace *pEntry, int32_t cornerIdx);
V2_I16 getTileMinFromBoundsEntry(BorderFace *pEntry);
int32_t bufMeshGetVertIdx(const Piece *pPiece,
                            const BufMesh *pBufMesh, int32_t localCorner);
int32_t bufMeshGetEdgeIdx(const Piece *pPiece,
                            const BufMesh *pBufMesh, int32_t localCorner);
