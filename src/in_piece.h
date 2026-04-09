/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <pixenals_thread_utils.h>

#include <cluster_tree_2d.h>

#include <job.h>
#include <pixenals_structs.h>
#include <mesh.h>

struct MapToMeshBasic;

typedef struct InFaceIdx {
	U32 idx : 30;
	U32 wind : 1;
	U32 border : 1;
} InFaceIdx;

typedef struct InFaceIdxArr {
	InFaceIdx *pArr;
	I32 size;
	I32 count;
} InFaceIdxArr;

typedef struct EncasedMapFace {
	PixuctHTableEntryCore core;
	I32 inFaces;
	I32 job;
	U32 cluster : 30;
	U32 clip : 1;
	U32 border : 1;
	V2_I16 tile;
} EncasedMapFace;

typedef struct EncasedEntryIdx {
	struct EncasedEntryIdx *pNext;
	U32 cluster : 31;
	U32 clip : 1;
	V2_I16 tile;
	I32 entryIdx;
} EncasedEntryIdx;

typedef struct BufFace {
	I32 start;
	I32 size;
	I32 inPiece;
	I32 mapFace;
} BufFace;

typedef struct InPiece {
	EncasedMapFace *pList;
	//BorderArr borderArr;
	I32 faceCount;
	PixtyV2_I16 tile;
} InPiece;

typedef enum BufVertType {
	STUC_BUF_VERT_IN_OR_MAP,
	STUC_BUF_VERT_ON_EDGE,
	STUC_BUF_VERT_OVERLAP,
	STUC_BUF_VERT_INTERSECT,
	STUC_BUF_VERT_SUB_TYPE_IN,
	STUC_BUF_VERT_SUB_TYPE_MAP,
	STUC_BUF_VERT_SUB_TYPE_EDGE_IN,
	STUC_BUF_VERT_SUB_TYPE_EDGE_MAP
} BufVertType;

typedef struct MapVert {
	I8 type;// BufVertType
	I8 mapCorner;
	I32 inFace;
} MapVert;

typedef struct InVert {
	I8 type;
	I8 inCorner;
	I8 tri;
	I32 inFace;
} InVert;

typedef union InOrMapVert {
	InVert in;
	MapVert map;
} InOrMapVert;

typedef struct EdgeMapVert {
	I8 type;// BufVertType
	I8 inCorner;
	I8 mapCorner;
	U32 inFace;
	F32 tInEdge;
} EdgeMapVert;

typedef struct EdgeInVert {
	I8 type;
	I8 mapCorner;
	I8 inCorner;
	U32 inFace;
	F32 tMapEdge;
} EdgeInVert;

typedef union BufVertOnEdge {
	EdgeInVert in;
	EdgeMapVert map;
} BufVertOnEdge;

typedef struct OverlapVert {
	I32 inFace;
	I8 inCorner;
	I8 mapCorner;
} OverlapVert;

typedef struct BufVertOverlapArr {
	OverlapVert *pArr;
	I32 size;
	I32 count;
} BufVertOverlapArr;

typedef struct IntersectVert {
	F32 tInEdge;
	F32 tMapEdge;
	I32 inFace;
	I32 mapCorner;
	I8 inCorner;
	I8 tri;
} IntersectVert;

typedef struct BufVertOnEdgeArr {
	BufVertOnEdge *pArr;
	I32 size;
	I32 count;
} BufVertOnEdgeArr;

typedef struct BufVertInOrMapArr {
	InOrMapVert *pArr;
	I32 size;
	I32 count;
} BufVertInOrMapArr;

typedef struct BufVertIntersectArr {
	IntersectVert *pArr;
	I32 size;
	I32 count;
} BufVertIntersectArr;

typedef struct BufCorner {
	U32 vert : 30;
	U32 type : 2;// BufVertType 
} BufCorner;

typedef struct BufCornerArr {
	BufCorner *pArr;
	I32 size;
	I32 count;
} BufCornerArr;

typedef struct BufFaceArr {
	BufFace *pArr;
	I32 size;
	I32 count;
} BufFaceArr;

typedef struct BufMesh {
	BufFaceArr faces;
	BufCornerArr corners;
	BufVertInOrMapArr inOrMapVerts;
	BufVertOnEdgeArr onEdgeVerts;
	BufVertOverlapArr overlapVerts;
	BufVertIntersectArr intersectVerts;
} BufMesh;

typedef struct BufMeshArr {
	BufMesh *pArr;
	I32 size;
	I32 count;
} BufMeshArr;

typedef struct InPieceArr {
	struct InPieceArr *pNext;
	BufMeshArr bufMeshes;
	InPiece *pArr;
	I32 size;
	I32 count;
} InPieceArr;

typedef struct ClustIdx {
	U32 idx : 30;
	U32 type : 2;//ClutreIntersect
	//PixtyV2_I16 tile;
} ClustIdx;

typedef struct TileRange {
	PixtyRange range;
	PixtyV2_I16 tile;
} TileRange;

typedef struct TileRangeArr {
	TileRange *pArr;
	I32 size;
	I32 count;
} TileRangeArr;

typedef struct StucBorderTable {
	PixuctHTableEntryCore core;
	I32 border;
	I32 idx;
	I32 edge;
} StucBorderTable;

typedef struct BorderEdge {
	FaceCorner corner;
	I32 adjIsland;
} BorderEdge;

typedef struct BorderEdgeArr {
	BorderEdge *pArr;
	I32 size;
	I32 count;
} BorderEdgeArr;

typedef struct Border {
	BorderEdgeArr arr;
	//I32 len;
} Border;

typedef struct BorderArr {
	Border *pArr;
	I32 size;
	I32 count;
	I32 outer;
} BorderArr;

typedef struct StucIsland {
	BorderArr borders;
	PixtyRange faces;
} StucIsland;

typedef struct StucSubIsland {
	StucIsland core;
} StucSubIsland;

typedef struct StucSubIslandArr {
	StucSubIsland *pArr;
	I32 *pFaces;
	I32 size;
	I32 count;
	I32 faceCount;
} StucSubIslandArr;

typedef struct StucInIsland {
	StucIsland core;
	StucSubIslandArr sub;
	PixuctHTable borderTable;
	ClutreBb bb;
} StucInIsland;

typedef struct StucInIslandArr {
	StucInIsland *pArr;
	I32 *pFaces;
	I32 *pFaceTable;
	I32 size;
	I32 count;
	I32 faceCount;
} StucInIslandArr;

typedef struct IslandClustArr {
	const StucInIsland *pIsland;
	ClutreStart start;
	ClustIdx *pArr;
	TileRangeArr tiles;
	I32 size;
	I32 count;
} IslandClustArr;

typedef struct InFaceMem {
	InFaceIdxArr *pArr;
	I32 size;
	I32 count;
	I32 initCount;
} InFaceMem;

typedef struct InFaceMemArr {
	InFaceMem arr[PIXTH_MAX_SUB_MAPPING_JOBS];
	I32 count;
} InFaceMemArr;

typedef struct FindEncasedFacesJobArgs {
	JobArgs core;
	const IslandClustArr *pClustArr;
	InFaceMem inFaces;
	PixuctHTable encasedFaces;
	InPieceArr inPiecesMono;
	JobArgsFoot foot;
} FindEncasedFacesJobArgs;

typedef struct SplitInPiecesAlloc {
	PixalcLinAlloc encased;
	PixalcLinAlloc inFace;
	PixalcLinAlloc border;
} SplitInPiecesAlloc;

typedef struct InFaceCacheEntry {
	PixuctHTableEntryCore core;
	FaceRange face;
	V2_F32 fMin;
	V2_F32 fMax;
	bool wind;
} InFaceCacheEntry;

struct HalfPlane;

typedef struct InFaceCacheEntryIntern {
	InFaceCacheEntry faceEntry;
	struct HalfPlane *pCorners;
} InFaceCacheEntryIntern;

typedef struct InFaceCorner {
	InFaceCacheEntry *pFace;
	I32 corner;
} InFaceCorner;

typedef struct InFaceCornerArr {
	InFaceCorner *pArr;
	I32 size;
	I32 count;
} InFaceCornerArr;

typedef struct PieceBorderEdge {
	ClutreValidIdx next;
	ClutreValidIdx prev;
	I32 idx;
} PieceBorderEdge;

typedef struct PieceBorderList {
	PieceBorderEdge *pArr;
	I32 size;
	I32 count;
	ClutreValidIdx start;
	I32 border;
} PieceBorderList;

typedef struct PieceBorders {
	PixuctAvl *pArr;
	I32 size;
} PieceBorders;

//TODO probably rename, this isn't a cache anymore
typedef struct BorderCache {
	const InPiece *pInPiece;
	const IslandClustArr *pClustArr;
	PixuctAvlIter iter;
	PieceBorders arr;
	PixalcLinAlloc alloc;
	I32 borderCount;
	I32 activeBorder;
} BorderCache;

typedef struct SrcFaces {
	I32 in;
	I32 map;
} SrcFaces;

typedef struct SplitInPiecesAllocArr {
	SplitInPiecesAlloc *pArr;
	I32 count;
} SplitInPiecesAllocArr;

StucErr stucFindEncasedFaces(void *pArgsVoid);
StucErr inPieceArrSplit(
	struct MapToMeshBasic *pBasic,
	InPieceArr *pInPieces,
	InPieceArr *pInPiecesSplit,
	InPieceArr *pInPiecesSplitClip,
	SplitInPiecesAllocArr *pSplitAlloc
);
SrcFaces stucGetSrcFacesForBufCorner(
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner corner
);
StucErr stucClipMapFace(
	const struct MapToMeshBasic *pBasic,
	const InFaceMemArr *pInFaceArr,
	const IslandClustArr *pClustArr,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache,
	void *pHTableAlc,
	void *pPlycutAlc,
	PixtyI32Arr *pOrderCache
);
StucErr stucAddMapFaceToBufMesh(
	const struct MapToMeshBasic *pBasic,
	const InFaceMemArr *pInFaceArr,
	const IslandClustArr *pClustArr,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache,
	void *pHTableAlc,
	void *pPlycutAlc,
	PixtyI32Arr *pOrderCache
);
StucErr stucBufMeshInit(void *pArgsVoid);
StucErr stucInPieceArrInit(
	const struct MapToMeshBasic *pBasic,
	I32 threadId,
	const IslandClustArr *pClustArr,
	InPieceArr *pInPieces,
	InPieceArr *pInPiecesClip,
	I32 *pJobCount, FindEncasedFacesJobArgs *pJobArgs,
	bool *pEmpty
);
StucErr stucInPieceArrInitBufMeshes(
	const struct MapToMeshBasic *pBasic,
	I32 threadId,
	const IslandClustArr *pClustArr,
	const InFaceMemArr *pInFaceArr,
	InPieceArr *pInPieces,
	StucErr (* fpAddPiece)(//TODO large func ptrs like this should be typedef'd
		const struct MapToMeshBasic *,
		const InFaceMemArr *,
		const IslandClustArr *,
		I32,
		const InPiece *,
		BufMesh *,
		BorderCache *,
		void *,
		void *,
		PixtyI32Arr *
	)
);
void stucBufMeshArrDestroy(StucContext pCtx, BufMeshArr *pArr);
//returns 1 if yes, and 2 if only due to preserve
I32 stucCouldInEdgeIntersectMapFace(const Mesh *pInMesh, I32 edge);
//destroys in-piece arr after splitting
StucErr stucInPieceArrSplit(
	struct MapToMeshBasic *pBasic,
	I32 threadId,
	InPieceArr *pInPieces,
	InPieceArr *pInPiecesSplit,
	InPieceArr *pInPiecesSplitClip,
	SplitInPiecesAllocArr *pSplitAlloc
);

static inline
void inPieceArrDestroy(const StucContext pCtx, InPieceArr *pArr) {
	if (pArr->pArr) {
		pCtx->alloc.fpFree(pArr->pArr);
	}
	if (pArr->bufMeshes.pArr) {
		stucBufMeshArrDestroy(pCtx, &pArr->bufMeshes);
	}
	*pArr = (InPieceArr) {0};
}

static inline
const InPiece *bufFaceGetInPiece(
	const BufMesh *pBufMesh,
	I32 face,
	const InPieceArr *pInPieces
) {
	I32 inPieceIdx = pBufMesh->faces.pArr[face].inPiece;
	return pInPieces->pArr + inPieceIdx;
}

static inline
BufVertType bufMeshGetType(const BufMesh *pBufMesh, FaceCorner corner) {
	BufFace bufFace = pBufMesh->faces.pArr[corner.face];
	BufCorner bufCorner = pBufMesh->corners.pArr[bufFace.start + corner.corner];
	return bufCorner.type;
}

static inline
bool stucBorderTableCmp(
	const PixuctHTableEntryCore *pEntry,
	const void *pKeyData,
	const void *pInitInfo
) {
	return ((StucBorderTable *)pEntry)->edge == *((I32 *)pKeyData);
}

static inline
bool stucIsInCornerOnBorder(
	const Mesh *pInMesh,
	const IslandClustArr *pClustArr,
	const FaceRange *pInFace,
	I32 corner,
	const StucBorderTable **ppEntry
) {
	const StucBorderTable *pEntry = NULL;
	I32 edge = stucGetMeshEdge(
		&pInMesh->core,
		(FaceCorner){.face = pInFace->idx, .corner = corner}
	);
	SearchResult result = pixuctHTableGetConst(
		&pClustArr->pIsland->borderTable,
		0,
		&edge,
		&pEntry,
		pixuctKeyFromI32, stucBorderTableCmp
	);
	if (ppEntry) {
		*ppEntry = pEntry;
	}
	return result == PIX_SEARCH_FOUND;
}

static inline
bool stucIsInFaceOnBorder(
	const Mesh *pInMesh,
	const IslandClustArr *pClustArr,
	const FaceRange *pInFace,
	const StucBorderTable **ppEntry
) {
	const StucBorderTable *pEntry = NULL;
	SearchResult result = 0;
	PIX_ERR_ASSERT("", pInFace->size > 0);
	for (I32 i = 0; i < pInFace->size; ++i) {
		I32 edge = stucGetMeshEdge(
			&pInMesh->core,
			(FaceCorner){.face = pInFace->idx, .corner = i}
		);
		result = pixuctHTableGetConst(
			&pClustArr->pIsland->borderTable,
			0,
			&edge,
			&pEntry,
			pixuctKeyFromI32, stucBorderTableCmp
		);
		if (result == PIX_SEARCH_FOUND) {
			break;
		}
	}
	if (ppEntry) {
		*ppEntry = pEntry;
	}
	return result == PIX_SEARCH_FOUND;
}
