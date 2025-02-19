#pragma once
#include <UvStuccoIntern.h>
#include <UvStucco.h>
#include <QuadTree.h>
#include <Utils.h>
#include <Types.h>

typedef struct CornerBuf {
	Mat3x3 tbn;
	V3_F32 uvw;
	V3_F32 corner;
	V3_F32 cornerFlat;
	V3_F32 normal;
	V3_F32 inTangent;
	V3_F32 bc; //barycentric coords
	V2_F32 uv;
	V3_F32 projNormal;
	V3_F32 projNormalMasked;
	I32 ancestor;
	I32 ancestorNext;
	F32 scale;
	F32 alpha;
	F32 mapAlpha;
	F32 inTSign;
	I8 triCorners[3];
	I8 baseCorner;
	I8 stucCorner;
	I8 segment;
	U8 onLine : 1;
	U8 isBaseCorner : 1;
	U8 preserve : 1;
	U8 isStuc : 1;
} CornerBuf;

typedef struct CornerBufWrap {
	CornerBuf buf[64];
	struct CornerBufWrap *pNext;
	I32 *pPendingMerge;
	I32 mergeCount;
	I32 mergeSize;
	I32 size;
	I32 lastInCorner;
	bool invalid;
	bool edgeFace;
	bool onLine;
} CornerBufWrap;

typedef struct LocalVert {
	struct LocalVert *pNext;
	I32 vert;
	I32 cornerSize;
	I32 inFace;
	I32 mapVert;
	V2_I32 tile;
} LocalVert;

typedef struct LocalEdge  {
	struct LocalEdge *pNext;
	I32 edge;
	I32 refFace;
	I32 refEdge;
	I32 cornerCount;
} LocalEdge;

typedef struct {
	LocalVert *pVertTable;
	LocalEdge *pEdgeTable;
	void *pVertTableAlloc;
	void *pEdgeTableAlloc;
	U32 vertTableSize;
	I32 edgeTableSize;
} LocalTables;

typedef struct {
	MapToMeshBasic *pBasic;
	BufMesh bufMesh;
	InFaceArr *pInFaces;
	I32 inFaceSize;
	LocalTables localTables;
	Mat3x3 tbn;
	Mat3x3 tbnInv;
	BorderTable borderTable;
	BorderTableAlloc borderTableAlloc;
	void *pCornerBufWrapAlloc;
	Range inFaceRange;
	I32 id;
	I32 bufSize;
	I32 rawBufSize;
	I32 finalBufSize;
	I32 cornerBufSize;
	I32 inFaceOffset;
} MappingJobVars;

Result stucMapToJobMesh(void *pArgsPtr);
Result stucMapToSingleFace(
	MappingJobVars *pVars,
	FaceCellsTable *pFaceCellsTable,
	V2_F32 fTileMin,
	V2_I32 tile,
	FaceRange *pInFace
);