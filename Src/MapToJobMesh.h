#pragma once
#include <RuvmInternal.h>
#include <RUVM.h>
#include <QuadTree.h>
#include <Utils.h>

typedef struct {
	V3_F32 loop;
	V3_F32 normal;
	V3_F32 bc; //barycentric coords
	V2_F32 uv;
	V3_F32 projNormal;
	V3_F32 projNormalMasked;
	float z;
	int8_t baseLoop;
	uint8_t ruvmLoop : 4;
	uint8_t onLine : 1;
	uint8_t isBaseLoop : 1;
	uint8_t preserve : 1;
	uint8_t isRuvm : 1;
	int8_t triLoops[3];
} LoopBuf;

typedef struct {
	LoopBuf buf[11];
	int32_t size;
} LoopBufWrap;

typedef struct LocalVert {
	struct LocalVert *pNext;
	int32_t vert;
	int32_t loopSize;
	int32_t baseFace;
	int32_t mapVert;
} LocalVert;

typedef struct LocalEdge  {
	struct LocalEdge *pNext;
	int32_t edge;
	int32_t refFace;
	int32_t refEdge;
	int32_t loopCount;
} LocalEdge;

typedef struct {
	LocalVert *pVertTable;
	LocalEdge *pEdgeTable;
	uint32_t vertTableSize;
	int32_t edgeTableSize;
} LocalTables;

typedef struct {
	BufMesh bufMesh;
	Mesh mesh;
	RuvmMap pMap;
	InFaceArr *pInFaces;
	int32_t inFaceSize;
	bool getInFaces;
	DebugAndPerfVars *pDpVars;
	RuvmAlloc alloc;
	LocalTables localTables;
	Mat3x3 tbn;
	Mat3x3 tbnInv;
	RuvmCommonAttribList *pCommonAttribList;
	BorderTable borderTable;
	EdgeVerts *pEdgeVerts;
	int8_t *pInVertTable;
	int32_t id;
	int32_t bufSize;
	int32_t rawBufSize;
	int32_t finalBufSize;
	int32_t loopBufSize;
} MappingJobVars;

void ruvmMapToJobMesh(void *pArgsPtr);
Result ruvmMapToSingleFace(MappingJobVars *pArgs, FaceCellsTable *pFaceCellsTable,
                         DebugAndPerfVars *pDpVars,
					     V2_F32 fTileMin, int32_t tile, FaceRange baseFace);
