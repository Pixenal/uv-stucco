#pragma once
#include <stdint.h>
#include <Mesh.h>

typedef struct BoundaryVert{
	struct BoundaryVert *pNext;
	uint64_t baseLoop : 22;
	uint64_t ruvmLoop : 33;
	uint64_t job : 8;
	uint64_t seam : 1;

	uint64_t tile : 24;
	uint64_t isRuvm : 11;
	uint64_t onLine : 11;
	uint64_t hasPreservedEdge : 1;

	int32_t faceIndex;
	int32_t face;
	int32_t baseFace;
} BoundaryVert;

typedef struct {
	int32_t verts[2];
} EdgeVerts;

typedef struct BoundaryDir {
	struct BoundaryDir *pNext;
	BoundaryVert *pEntry;
} BoundaryDir;

typedef struct {
	BoundaryDir *pTable;
	int32_t size;
} BoundaryTable;

typedef struct {
	RuvmMap pMap;
	Mesh mesh;
	BufMesh bufMesh;
	RuvmContext pContext;
	BoundaryTable boundsTable;
	EdgeVerts *pEdgeVerts;
	RuvmCommonAttribList *pCommonAttribList;
	int32_t *pJobsCompleted;
	void *pMutex;
	int8_t *pInVertTable;
	int32_t id;
	int32_t totalBoundaryFaces;
	int32_t totalBoundaryEdges;
	int32_t totalVerts;
	int32_t totalLoops;
	int32_t totalEdges;
	int32_t totalFaces;
} SendOffArgs;

typedef struct {
	uint64_t timeSpent[3];
	int32_t maxDepth;
} DebugAndPerfVars;
