#pragma once
#include <uv_stucco_intern.h>
#include <uv_stucco.h>
#include <quadTree.h>
#include <utils.h>
#include <types.h>

typedef struct CornerBuf {
	Mat3x3 tbn;
	V3_F32 uvw;
	V3_F32 corner;
	V3_F32 cornerFlat;
	V3_F32 normal;
	V3_F32 inTangent;
	V3_F32 bc; //barycentric coords
	V3_F32 projNormal;
	V2_F32 uv;
	I32 ancestor;
	I32 ancestorNext;
	F32 alpha;
	F32 mapAlpha;
	F32 inTSign;
	I8 triCorners[3];
	I8 inCorner;
	I8 mapCorner;
	I8 segment;
	U8 onLine : 1;
	U8 onInVert : 1;
	U8 isMapCorner : 1;
} CornerBuf;

typedef struct CornerBufWrap {
	CornerBuf buf[64];
	struct CornerBufWrap *pNext;
	I32 *pPendingMerge;
	I32 mergeCount;
	I32 mergeSize;
	I32 count;
	I32 lastInCorner;
	bool invalid;
	bool edgeFace;
	bool onLine;
} CornerBufWrap;

typedef struct LocalVert {
	struct LocalVert *pNext;
	V2_I32 tile;
	I32 vert;
	I32 cornerSize;
	I32 inFace;
	I32 mapVert;
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
	BufMesh bufMesh;
	LocalTables localTables;
	Mat3x3 tbn;
	Mat3x3 tbnInv;
	MapToMeshBasic *pBasic;
	void *pCornerBufWrapAlloc;
	BorderTable borderTable;
	Range inFaceRange;
	BorderTableAlloc borderTableAlloc;
	I32 id;
	I32 bufSize;
	I32 rawBufSize;
	I32 finalBufSize;
	I32 cornerBufSize;
	I32 facesChecked;
	I32 facesUsed;
} MappingJobState;

Result stucMapToJobMesh(void *pArgsVoid);
Result stucMapToSingleFace(
	MappingJobState *pState,
	FaceCellsTable *pFaceCellsTable,
	V2_I32 tile,
	FaceRange *pInFace
);