/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <uv_stucco_intern.h>
#include <in_piece.h>

typedef struct InVertKey {
	I32 inVert;
	I32 mapFace;
} InVertKey;

typedef struct MapVertKey {
	I32 mapVert;
	I32 inFace;
} MapVertKey;

typedef struct EdgeInVertKey {
	I32 inVert;
	I32 mapEdge;
} EdgeInVertKey;

typedef struct EdgeMapVertKey {
	I32 mapVert;
	I32 inEdge;
} EdgeMapVertKey;

typedef struct OverlapVertKey {
	I32 inVert;
	I32 mapVert;
} OverlapVertKey;

typedef struct IntersectVertKey {
	I32 inEdge;
	I32 mapEdge;
} IntersectVertKey;


typedef union MergeTableInOrMapKey {
	InVertKey in;
	MapVertKey map;
} MergeTableInOrMapKey;

typedef union MergeTableOnEdgeKey {
	EdgeInVertKey in;
	EdgeMapVertKey map;
} MergeTableOnEdgeKey;

typedef union MergeTableVertKey {
	MergeTableInOrMapKey inOrMap;
	MergeTableOnEdgeKey onEdge;
	OverlapVertKey overlap;
	IntersectVertKey intersect;
} MergeTableVertKey;

typedef struct MergeTableKey {
	BufVertType type;
	V2_I16 tile;
	MergeTableVertKey key;
} MergeTableKey;

typedef union MergeTableInitInfoVert {
	InOrMapVert inOrMap;
	BufVertOnEdge onEdge;
	OverlapVert overlap;
	IntersectVert intersect;
} MergeTableInitInfoVert;

typedef struct VertMergeCorner {
	BufCorner *pBufCorner;
	FaceCorner corner;
	I8 bufMesh;
	bool clipped;
} VertMergeCorner;

typedef struct VertMergeTransform {
	M3x3 tbn;
} VertMergeTransform;

typedef struct VertMerge {
	HTableEntryCore core;
	MergeTableKey key;
	VertMergeCorner bufCorner;
	VertMergeTransform transform;
	U32 cornerCount : 31;
	U32 preserve : 1;
	U32 removed : 1;
	I32 outVert;
	I32 linIdx;
} VertMerge;

typedef struct VertMergeIntersect {
	VertMerge core;
	VertMerge *pSnapTo;
} VertMergeIntersect;

void stucVertMergeTableInit(
	const MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	HTable *pTable
);
void stucMergeVerts(
	const MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	bool clipped,
	HTable *pTable
);
void stucMergeTableGetVertKey(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	MergeTableKey *pKey
);

StucErr stucSnapIntersectVerts(
	MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	HTable *pMergeTable,
	I32 *pSnappedVerts
);

static inline
void getBufMeshForVertMergeEntry(
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	const VertMerge *pVert,
	const InPiece **ppInPiece,
	const BufMesh **ppBufMesh
) {
	const InPieceArr *pArr = pVert->bufCorner.clipped ? pInPiecesClip : pInPieces;
	*ppBufMesh = pArr->pBufMeshes->arr + pVert->bufCorner.bufMesh;
	I32 inPieceIdx = (*ppBufMesh)->faces.pArr[pVert->bufCorner.corner.face].inPiece;
	*ppInPiece = pArr->pArr + inPieceIdx;
}

static inline
StucKey mergeTableMakeKey(const void *pKeyData) {
	return (StucKey){.pKey = pKeyData, .size = sizeof(MergeTableKey)};
}

static inline
bool mergeTableEntryCmp(
	const HTableEntryCore *pEntryCore,
	const void *pKeyData,
	const void *pInitInfo
) {
	const VertMerge *pEntry = (VertMerge *)pEntryCore;
	const MergeTableKey *pKey = pKeyData;
	if (pKey->type != pEntry->key.type ||
		!_(pKey->tile V2I16EQL pEntry->key.tile)
	) {
		return false;
	}
	switch (pKey->type) {
		case STUC_BUF_VERT_SUB_TYPE_IN:
			return
				pKey->key.inOrMap.in.inVert == pEntry->key.key.inOrMap.in.inVert &&
				pKey->key.inOrMap.in.mapFace == pEntry->key.key.inOrMap.in.mapFace;
		case STUC_BUF_VERT_SUB_TYPE_MAP:
			return
				pKey->key.inOrMap.map.mapVert == pEntry->key.key.inOrMap.map.mapVert &&
				pKey->key.inOrMap.map.inFace == pEntry->key.key.inOrMap.map.inFace;
		case STUC_BUF_VERT_SUB_TYPE_EDGE_IN:
			return
				pKey->key.onEdge.in.inVert == pEntry->key.key.onEdge.in.inVert &&
				pKey->key.onEdge.in.mapEdge == pEntry->key.key.onEdge.in.mapEdge;
		case STUC_BUF_VERT_SUB_TYPE_EDGE_MAP:
			return
				pKey->key.onEdge.map.mapVert == pEntry->key.key.onEdge.map.mapVert &&
				pKey->key.onEdge.map.inEdge == pEntry->key.key.onEdge.map.inEdge;
		case STUC_BUF_VERT_OVERLAP:
			return
				pKey->key.overlap.inVert == pEntry->key.key.overlap.inVert &&
				pKey->key.overlap.mapVert == pEntry->key.key.overlap.mapVert;
		case STUC_BUF_VERT_INTERSECT:
			return
				pKey->key.intersect.inEdge == pEntry->key.key.intersect.inEdge &&
				pKey->key.intersect.mapEdge == pEntry->key.key.intersect.mapEdge;
		default:
			PIX_ERR_ASSERT("invalid vert type", false);
			return false;
	}
}

