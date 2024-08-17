#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <Mesh.h>
#include <DebugAndPerf.h>
#include <Usg.h>
#include <Error.h>

#define RUVM_TILE_MIN_BIT_LEN 11

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

typedef struct BorderFace {
	struct BorderFace *pNext;
	int32_t faceIndex;
	int32_t face;
	int32_t baseFace;
	uint32_t tileX : RUVM_TILE_MIN_BIT_LEN;
	uint32_t tileY : RUVM_TILE_MIN_BIT_LEN;
	UBitField32 job : 6;
	uint32_t inOrient : 1;
	uint32_t mapOrient : 1;
	uint32_t memType : 2;
} BorderFace;

//max map loops: 8
//max possible buf loops: 14
typedef struct {
	BorderFace header;
	//these are indexed as multichar bit arrays
	UBitField8 baseLoop[4];
	UBitField8 ruvmLoop[6];
	UBitField8 segment[6];
	UBitField8 isRuvm[2];
	UBitField8 onLine[2];
	UBitField8 onInVert[2];
} BorderFaceSmall;

//max map loops: 16
//max possible buf loops: 26
typedef struct {
	BorderFace header;

	UBitField8 baseLoop[7];
	UBitField8 ruvmLoop[13];
	UBitField8 segment[13];
	UBitField8 isRuvm[4];
	UBitField8 onLine[4];
	UBitField8 onInVert[4];
} BorderFaceMid;

//max map loops 32
//max possible buf loops: 50
typedef struct {
	BorderFace header;

	UBitField8 baseLoop[13];
	UBitField8 ruvmLoop[32];
	UBitField8 segment[32];
	UBitField8 isRuvm[7];
	UBitField8 onLine[7];
	UBitField8 onInVert[7];
} BorderFaceLarge;

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
	Mesh mesh;
	BufMesh bufMesh;
	RuvmMap pMap;
	InFaceArr *pInFaces;
	bool getInFaces;
	uint64_t reallocTime;
	Result* pResult;
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
	float wScale;
} SendOffArgs;
