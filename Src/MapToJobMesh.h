#pragma once
#include <UvStuccoIntern.h>
#include <UvStucco.h>
#include <QuadTree.h>
#include <Utils.h>

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
	int32_t ancestor;
	int32_t ancestorNext;
	float scale;
	float alpha;
	float mapAlpha;
	float inTSign;
	int8_t triCorners[3];
	int8_t baseCorner;
	int8_t stucCorner;
	int8_t segment;
	uint8_t onLine : 1;
	uint8_t isBaseCorner : 1;
	uint8_t preserve : 1;
	uint8_t isStuc : 1;
	bool transformed;
} CornerBuf;

typedef struct CornerBufWrap {
	CornerBuf buf[64];
	struct CornerBufWrap *pNext;
	int32_t *pPendingMerge;
	int32_t mergeCount;
	int32_t mergeSize;
	int32_t size;
	int32_t lastInCorner;
	bool invalid;
	bool edgeFace;
	bool onLine;
} CornerBufWrap;

typedef struct LocalVert {
	struct LocalVert *pNext;
	int32_t vert;
	int32_t cornerSize;
	int32_t inFace;
	int32_t mapVert;
	V2_I32 tile;
} LocalVert;

typedef struct LocalEdge  {
	struct LocalEdge *pNext;
	int32_t edge;
	int32_t refFace;
	int32_t refEdge;
	int32_t cornerCount;
} LocalEdge;

typedef struct {
	LocalVert *pVertTable;
	LocalEdge *pEdgeTable;
	uint32_t vertTableSize;
	int32_t edgeTableSize;
} LocalTables;

typedef struct {
	StucContext pContext;
	BufMesh bufMesh;
	Mesh mesh;
	StucMap pMap;
	InFaceArr *pInFaces;
	int32_t inFaceSize;
	bool getInFaces;
	DebugAndPerfVars *pDpVars;
	StucAlloc alloc;
	LocalTables localTables;
	Mat3x3 tbn;
	Mat3x3 tbnInv;
	StucCommonAttribList *pCommonAttribList;
	BorderTable borderTable;
	EdgeVerts *pEdgeVerts;
	int8_t *pInVertTable;
	Range inFaceRange;
	int32_t id;
	int32_t bufSize;
	int32_t rawBufSize;
	int32_t finalBufSize;
	int32_t cornerBufSize;
	int32_t inFaceOffset;
	float wScale;
	int8_t maskIdx;
} MappingJobVars;

Result stucMapToJobMesh(void *pArgsPtr);
Result stucMapToSingleFace(MappingJobVars *pVars, FaceCellsTable *pFaceCellsTable,
                           DebugAndPerfVars *pDpVars, V2_F32 fTileMin, V2_I32 tile,
                           FaceRange *pInFace);