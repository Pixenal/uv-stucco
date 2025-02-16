#pragma once
#include <UvStucco.h>
#include <UvStuccoIntern.h>
#include <Utils.h>
#include <Mesh.h>
#include <QuadTree.h>
#include <Types.h>

typedef struct {
	V3_F32 normal;
	V2_F32 uv;
	V2_F32 vertBuf;
	I32 bufCorner;
	I32 bufFace;
	I32 corner;
	I32 edge;
	I8 job;
} BoundsCornerBufEntry;

typedef struct {
	BoundsCornerBufEntry* pBuf;
	I32 count;
} BoundsCornerBuf;

typedef struct {
	BoundsCornerBufEntry* pCornerBuf;
	I32 *pMapCornerBuf;
	I32 *pIdxTable;
	I32 *pSortedVerts;
	I32 size;
} MergeBufHandles;


typedef struct {
	I32 start;
	I32 end;
	I32 size;
	I32 cornerLocal;
	I32 edgeCorner;
	I32 vertCorner;
	I32 edge;
	I32 vert;
} BorderInInfo;

typedef struct {
	I32 edge;
	I32 segment;
} EdgeSegmentPair;

// // indicates only used in createAndJoinPieces
typedef struct Piece {
	struct Piece *pNext;
	BorderFace *pEntry;
	FaceRange bufFace;
	I32 edgeCount;//
	EdgeSegmentPair *pEdges;
	UBitField64 keepSeam;//
	UBitField64 keepPreserve;
	UBitField64 keepVertPreserve;//
	UBitField64 add;
	U8 *pOrder;
	I32 entryIdx;//
	V2_I16 tile;
	V3_F32 realNormal;
	bool listed;
	bool triangulate;
	bool hasSeam;
} Piece;

typedef struct BorderEdge {
	struct BorderEdge *pNext;
	I32 edge;
	I32 inEdge;
	I32 mapFace;
	bool valid;
} BorderEdge;

typedef struct {
	Piece *pArr;
	I32 count;
} PieceArr;

typedef struct {
	I32 *pArr;
	I32 count;
} PieceRootsArr;

typedef struct OnLine {
	struct OnLine *pNext;
	I32 baseEdgeOrCorner;
	I32 stucVert;
	I32 outVert;
	I32 type;
} OnLine;

typedef struct BorderVert {
	struct BorderVert *pNext;
	I32 entryIdx;
	I32 mapFace;
	I32 mapEdge;
	I32 vert;
	V2_I16 tile;
	I32 corners;
	I32 inEdge;
	I32 inVert;
	I32 cornerIdx;
	I32 corner;
	I8 job;
	bool keepBaseCorner;
	bool divided;
} BorderVert;

typedef struct {
	BorderVert *pVertTable;
	OnLine *pOnLineTable;
	BorderEdge *pEdgeTable;
	I32 vertTableSize;
	I32 onLineTableSize;
	I32 edgeTableSize;
} CombineTables;

typedef struct {
	I32 vertBase;
	I32 edgeBase;
} JobBases;

typedef struct {
	BorderFace **ppTable;
	I32 count;
} CompiledBorderTable;

typedef struct MergeSendOffArgs {
	MapToMeshBasic *pBasic;
	SendOffArgs *pJobArgs;
	UBitField8 *pInVertKeep;
	CompiledBorderTable *pBorderTable;
	JobBases *pJobBases;
	CombineTables *pCTables;
	PieceArr *pPieceArrTable;
	PieceRootsArr *pPieceRootTable;
	void *pOrderAlloc;
	void *pEdgeSegPairAlloc;
	void *pSharedEdgeAlloc;
	I32 *pTotalVertTable;
	I32 *pCornerMergeTable;
	I32 jobCount;
	I32 totalVerts;
	I32 entriesStart;
	I32 entriesEnd;
	I32 job;
} MergeSendOffArgs;

Result stucMergeBorderFaces(
	MapToMeshBasic *pBasic,
	SendOffArgs *pJobArgs,
	JobBases *pJobBases,
	I32 mapJobsSent
);
void stucAllocMergeBufs(
	StucContext pCtx,
	MergeBufHandles *pHandle,
	I32 totalVerts
);
void stucMergeSingleBorderFace(
	MergeSendOffArgs *pArgs,
	U64 *pTimeSpent,
	I32 entryIdx,
	PieceArr *pPieceArr,
	FaceRange *pStucFace,
	MergeBufHandles *pMergeBufHandles,
	I32 *pInFaces,
	I32 entryCount
);
void stucDestroyMergeBufs(StucContext pCtx, MergeBufHandles *pHandle);
Result stucCombineJobMeshes(
	MapToMeshBasic *pBasic,
	SendOffArgs *pJobArgs,
	I32 mapJobsSent
);
BorderInInfo stucGetBorderEntryInInfo(
	const MapToMeshBasic *pBasic,
	const BorderFace *pEntry,
	const I32 cornerIdx
);
bool stucGetIfStuc(const BorderFace *pEntry, I32 cornerIdx);
bool stucGetIfOnInVert(const BorderFace *pEntry, I32 cornerIdx);
bool stucGetIfOnLine(const BorderFace *pEntry, I32 cornerIdx);
I32 stucGetSegment(const BorderFace *pEntry, I32 cornerIdx);
I32 stucGetMapCorner(const BorderFace *pEntry, I32 cornerIdx);
I32 stucGetBaseCorner(const BorderFace *pEntry, I32 cornerIdx);
V2_I16 stucGetTileMinFromBoundsEntry(BorderFace *pEntry);
I32 stucBufMeshGetVertIdx(const Piece *pPiece, const BufMesh *pBufMesh, I32 localCorner);
I32 stucBufMeshGetEdgeIdx(const Piece *pPiece, const BufMesh *pBufMesh, I32 localCorner);
