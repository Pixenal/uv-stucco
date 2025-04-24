/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <pixenals_thread_utils.h>

#include <job.h>
#include <context.h>
#include <hash_table.h>
#include <Mesh.h>

struct MapToMeshBasic;

typedef struct EncasingInFace {
	U32 idx : 31;
	U32 wind : 1;
} EncasingInFace;

typedef struct EncasingInFaceArr {
	EncasingInFace *pArr;
	I32 size;
	I32 count;
} EncasingInFaceArr;

typedef struct EncasedMapFace {
	HTableEntryCore core;
	EncasingInFaceArr inFaces;
	I32 mapFace;
	V2_I16 tile;
} EncasedMapFace;

typedef struct EncasedEntryIdx {
	struct EncasedEntryIdx *pNext;
	I32 mapFace;
	V2_I16 tile;
	I32 entryIdx;
} EncasedEntryIdx;

typedef struct Border {
	FaceCorner start;
	I32 len;
} Border;

typedef struct BorderArr {
	Border *pArr;
	I32 size;
	I32 count;
} BorderArr;

typedef struct BufFace {
	I32 start;
	I32 size;
	I32 inPiece;
} BufFace;

typedef struct InPiece {
	EncasedMapFace *pList;
	BorderArr borderArr;
	I32 faceCount;
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
	V2_F32 pos;
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
	BufMesh arr[PIX_THREAD_MAX_SUB_MAPPING_JOBS];
	I32 count;
} BufMeshArr;

typedef struct InPieceArr {
	BufMeshArr *pBufMeshes;
	InPiece *pArr;
	I32 size;
	I32 count;
} InPieceArr;

typedef struct FindEncasedFacesJobArgs {
	JobArgs core;
	HTable encasedFaces;
	InPieceArr inPiecesMono;
} FindEncasedFacesJobArgs;

typedef struct SplitInPiecesAlloc {
	PixalcLinAlloc encased;
	PixalcLinAlloc inFace;
	PixalcLinAlloc border;
} SplitInPiecesAlloc;

typedef struct InFaceCacheEntry {
	HTableEntryCore core;
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

typedef struct BorderCache {
	InFaceCornerArr *pBorders;
	I32 size;
	I32 count;
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
	const struct MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner corner
);
StucErr stucClipMapFace(
	const struct MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache
);
StucErr stucAddMapFaceToBufMesh(
	const struct MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache
);
StucErr stucBufMeshInit(void *pArgsVoid);
StucErr stucInPieceArrInit(
	struct MapToMeshBasic *pBasic,
	InPieceArr *pInPieces,
	I32 *pJobCount, FindEncasedFacesJobArgs *pJobArgs,
	bool *pEmpty
);
StucErr stucInPieceArrInitBufMeshes(
	struct MapToMeshBasic *pBasic,
	InPieceArr *pInPieces,
	StucErr (* fpAddPiece)(
		const struct MapToMeshBasic *,
		I32,
		const InPiece *,
		BufMesh *,
		BorderCache *
	)
);
void stucBufMeshArrDestroy(StucContext pCtx, BufMeshArr *pArr);
bool stucCouldInEdgeIntersectMapFace(const Mesh *pInMesh, I32 edge);

static inline
void inPieceArrDestroy(const StucContext pCtx, InPieceArr *pArr) {
	if (pArr->pArr) {
		pCtx->alloc.fpFree(pArr->pArr);
	}
	*pArr = (InPieceArr) {0};
	//bufmeshes are stored on stack, so we don't free that
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
