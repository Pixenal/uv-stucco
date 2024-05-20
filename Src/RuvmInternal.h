#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <Mesh.h>
#include <DebugAndPerf.h>

typedef uint64_t UBitField64;
typedef uint32_t UBitField32;
typedef uint16_t UBitField16;
typedef uint8_t UBitField8;

typedef struct {
	int32_t start;
	int32_t end;
} Range;

typedef struct {
	float d[4];
} Color;

typedef struct BorderFace{
	struct BorderFace *pNext;
	UBitField64 baseLoop : 22;
	UBitField64 ruvmLoop : 33;
	UBitField64 job : 8;
	uint64_t seam : 1;

	uint64_t tile : 24;
	UBitField64 isRuvm : 11;
	UBitField64 onLine : 11;
	UBitField64 onInVert : 11;
	uint64_t hasPreservedEdge : 1;

	int32_t faceIndex;
	int32_t face;
	int32_t baseFace;
} BorderFace;

typedef struct BorderBucket {
	struct BorderBucket *pNext;
	BorderFace *pEntry;
} BorderBucket;

typedef struct {
	int32_t verts[2];
} EdgeVerts;

typedef struct {
	BorderBucket *pTable;
	int32_t size;
} BorderTable;

typedef struct {
	RuvmMap pMap;
	Mesh mesh;
	BufMesh bufMesh;
	RuvmContext pContext;
	BorderTable borderTable;
	EdgeVerts *pEdgeVerts;
	RuvmCommonAttribList *pCommonAttribList;
	int32_t *pJobsCompleted;
	void *pMutex;
	int32_t bufSize;
	int32_t rawBufSize;
	int32_t finalBufSize;
	int8_t *pInVertTable;
	int32_t id;
	int32_t totalBorderFaces;
	int32_t totalBorderEdges;
	int32_t totalVerts;
	int32_t totalLoops;
	int32_t totalEdges;
	int32_t totalFaces;
	uint64_t reallocTime;
} SendOffArgs;
