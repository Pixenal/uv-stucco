/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <uv_stucco_intern.h>
#include <uv_stucco.h>
#include <quadtree.h>
#include <utils.h>
#include <types.h>

typedef enum IntersectTravelDir {
	INBOUND,
	OUTBOUND
} IntersectTravelDir;

typedef enum IslandType {
	MAP_FACE,
	IN_FACE
} IslandType;

typedef struct Island {
	IslandType type;
} Island;

typedef struct InFaceIsland{
	Island type;
	FaceCorner pCorners;
	I32 size;
	I32 count;
} InFaceIsland;

/*
typedef struct MapIslandCorner {
	Mat3x3 tbn;
	V3_F32 uvw;
	V3_F32 corner;
	V3_F32 cornerFlat;
	V3_F32 normal;
	V3_F32 inTangent;
	V3_F32 bc; //barycentric coords
	V3_F32 projNormal;
	V2_F32 uv;
	InFaceIsland *pInIslandNext;
	InFaceIsland *pInIslandPrev;
	I32 inFace;
	InsideStatus inside;
	I32 ancestor;
	I32 ancestorNext;
	F32 tInEdge;
	F32 tMapEdge;
	F32 inTSign;
	I8 triCorners[3];
	I8 mapCorner;
	I8 segment;
	U8 onLine : 1;
	U8 onInVert : 1;
	U8 isMapCorner : 1;
	U8 intersect : 1;
} MapIslandCorner;

struct MapIsland;
*/

typedef enum CornerType {
	STUC_CORNER_NONE,
	STUC_CORNER_ROOT,
	STUC_CORNER_MAP,
	STUC_CORNER_IN,
	STUC_CORNER_INTERSECT
} CornerType;

typedef struct CornerCore {
	struct CornerCore *pNext;
	struct CornerCore *pPrev;
	I8 type;
	bool checked;
} CornerCore;

typedef struct InCorner {
	CornerCore core;
	I32 inFace;
	I32 corner;
} InCorner;

typedef struct MapCorner {
	CornerCore core;
	I32 corner;
	I32 inFace;
} MapCorner;

typedef struct IntersectCorner {
	CornerCore core;
	V3_F32 pos;
	I32 borderIdx;
	I32 mapCorner;
	F32 tInEdge;
	F32 tMapEdge;
	FaceCorner inCorner;
	I32 borderEdge;
	I8 type;
	I8 travelDir;
	bool merged;
} IntersectCorner;

typedef struct ClippedRoot {
	CornerCore root;
	bool noIntersect;
} ClippedRoot;

typedef struct ClippedArr {
	PixalcLinAlloc inAlloc;
	PixalcLinAlloc mapAlloc;
	PixalcLinAlloc rootAlloc;
} ClippedArr;

/*
typedef struct MapIsland {
	Island type;
	struct MapIsland *pNext;
	I32 *pCorners;
	const IntersectCorner *pInbound;
	const IntersectCorner *pOutbound;
	I32 *pPendingMerge;
	I32 size;
	I32 count;
	I32 mergeCount;
	I32 mergeSize;
	I32 lastInCorner;
	bool invalid;
	bool edgeFace;
	bool onLine;
} MapIsland;
*/

/*
typedef struct IntersectTreeNode {
	struct IntersectTreeNode *pLess;
	struct IntersectTreeNode *pGreat;
	IntersectCorner *pCorner;
} IntersectTreeNode;

typedef struct {
	IntersectCorner *pList;
	IntersectTreeNode *pTree;
	void *pListAlloc;
	void *pNodeAlloc;
	I32 len;
} IntersectList;

typedef struct {
	IntersectList *pArr;
	I32 count;
} IntersectListArr;
*/

typedef struct IntersectArr {
	I32 *pSortedIn;
	I32 *pSortedMap;
	Range *pBorderRanges;
	IntersectCorner *pArr;
	I32 size;
	I32 count;
} IntersectArr;

typedef struct xformAndInterpVertsJobArgs {
	JobArgs core;
	Mesh *pOutMesh;// outmesh in core.pBasic is const
	const InPieceArr *pInPieces;
	const InPieceArr *pInPiecesClip;
	PixalcLinAlloc *pVertAlloc;
	bool intersect;
} xformAndInterpVertsJobArgs;

typedef struct InterpAttribsJobArgs {
	JobArgs core;
	Mesh *pOutMesh;
	const InPieceArr *pInPieces;
	const InPieceArr *pInPiecesClip;
	const HTable *pMergeTable;
} InterpAttribsJobArgs;

StucErr stucFindEncasedFaces(void *pArgsVoid);
StucErr stucGetEncasedFacesPerFace(
	FindEncasedFacesJobArgs *pArgs,
	FaceCellsTable *pFaceCellsTable,
	V2_I16 tile,
	FaceRange *pInFace
);

typedef struct InFaceCacheEntry {
	HTableEntryCore core;
	FaceRange face;
	V2_F32 fMin;
	V2_F32 fMax;
	bool wind;
} InFaceCacheEntry;

typedef struct InFaceCacheEntryIntern {
	InFaceCacheEntry faceEntry;
	HalfPlane *pCorners;
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

typedef struct BufMeshInitJobArgs {
	JobArgs core;
	StucErr (* fpAddPiece)(
		const MapToMeshBasic *,
		I32,
		const InPiece *,
		BufMesh *,
		BorderCache *
	);
	const InPieceArr *pInPiecesSplit;
	BufMesh bufMesh;
} BufMeshInitJobArgs;

StucErr stucClipMapFace(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache
);
StucErr stucAddMapFaceToBufMesh(
	const MapToMeshBasic *pBasic,
	I32 inPieceOffset,
	const InPiece *pInPiece,
	BufMesh *pBufMesh,
	BorderCache *pBorderCache
);
StucErr stucBufMeshInit(void *pArgsVoid);
StucErr stucXformAndInterpVertsInRange(void *pArgsVoid);
StucErr stucInterpCornerAttribs(void *pArgsVoid);
StucErr stucInterpFaceAttribs(void *pArgsVoid);
