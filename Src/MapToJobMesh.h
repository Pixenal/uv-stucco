#pragma once
#include <RuvmInternal.h>
#include <RUVM.h>
#include <QuadTree.h>
#include <Utils.h>

typedef struct {
	Cell **pCells;
	int8_t *pCellType;
	int32_t cellSize;
	int32_t faceSize;
	FaceBounds faceBounds;
} FaceCellsInfo;

typedef struct {
	int32_t cellFacesTotal;
	int32_t cellFacesMax;
	FaceCellsInfo *pFaceCellsInfo;
	int32_t*pCellFaces;
	int32_t uniqueFaces;
} EnclosingCells;

typedef struct {
	V3_F32 loop;
	V3_F32 normal;
	V3_F32 bc; //barycentric coords
	V2_F32 uv;
	int8_t baseLoop;
	uint8_t ruvmLoop : 4;
	uint8_t onLine : 1;
	uint8_t isBaseLoop : 1;
	uint8_t preserve : 1;
	uint8_t isRuvm : 1;
	int8_t triLoops[3];
} LoopBuffer;

typedef struct {
	LoopBuffer buf[11];
	int32_t size;
} LoopBufferWrap;

typedef struct VertAdj{
	struct VertAdj *pNext;
	int32_t vert;
	int32_t loopSize;
	int32_t baseFace;
	int32_t mapVert;
} VertAdj;

typedef struct MeshBufEdgeTable  {
	struct MeshBufEdgeTable *pNext;
	int32_t edge;
	int32_t refFace;
	int32_t refEdge;
	int32_t loopCount;
} MeshBufEdgeTable;

typedef struct {
	VertAdj *pRuvmVertAdj;
	MeshBufEdgeTable *pEdgeTable;
	uint32_t vertAdjSize;
	int32_t edgeTableSize;
} MappingTables;

typedef struct {
	RuvmMap pMap;
	BufMesh bufMesh;
	Mesh mesh;
	RuvmAllocator alloc;
	MappingTables mTables;
	Mat3x3 tbn;
	RuvmCommonAttribList *pCommonAttribList;
	BoundaryTable boundsTable;
	EdgeVerts *pEdgeVerts;
	int8_t *pInVertTable;
	int32_t id;
	int32_t bufferSize;
	int32_t loopBufferSize;
} MappingJobVars;

void ruvmMapToJobMesh(void *pArgsPtr);
void ruvmMapToSingleFace(MappingJobVars *pArgs, EnclosingCells *pEcVars,
                         DebugAndPerfVars *pDpVars, V2_F32 fTileMin, int32_t tile,
						 FaceInfo baseFace);
void ruvmGetEnclosingCells(RuvmAllocator *pAlloc, RuvmMap pMap,
                           Mesh *pMeshIn, EnclosingCells *pEc);
void ruvmDestroyEnclosingCells(RuvmAllocator *pAlloc, EnclosingCells *pEc);
