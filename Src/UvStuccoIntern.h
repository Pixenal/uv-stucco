#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <Mesh.h>
#include <DebugAndPerf.h>
#include <Usg.h>
#include <Error.h>

#define STUC_TILE_MIN_BIT_LEN 11

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
	int32_t faceIdx;
	int32_t face;
	int32_t baseFace;
	uint32_t tileX : STUC_TILE_MIN_BIT_LEN;
	uint32_t tileY : STUC_TILE_MIN_BIT_LEN;
	UBitField32 job : 6;
	uint32_t inOrient : 1;
	uint32_t mapOrient : 1;
	uint32_t memType : 2;
} BorderFace;

//max map corners: 8
//max possible buf corners: 14
typedef struct {
	BorderFace header;
	//these are indexed as multichar bit arrays
	UBitField8 baseCorner[4];
	UBitField8 stucCorner[6];
	UBitField8 segment[6];
	UBitField8 isStuc[2];
	UBitField8 onLine[2];
	UBitField8 onInVert[2];
} BorderFaceSmall;

//max map corners: 16
//max possible buf corners: 26
typedef struct {
	BorderFace header;

	UBitField8 baseCorner[7];
	UBitField8 stucCorner[13];
	UBitField8 segment[13];
	UBitField8 isStuc[4];
	UBitField8 onLine[4];
	UBitField8 onInVert[4];
} BorderFaceMid;

//max map corners 32
//max possible buf corners: 50
typedef struct {
	BorderFace header;

	UBitField8 baseCorner[13];
	UBitField8 stucCorner[32];
	UBitField8 segment[32];
	UBitField8 isStuc[7];
	UBitField8 onLine[7];
	UBitField8 onInVert[7];
} BorderFaceLarge;

typedef struct BorderBucket {
	struct BorderBucket *pNext;
	BorderFace *pEntry;
	BorderFace *pTail;
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
	StucMap pMap;
	InFaceArr *pInFaces;
	bool getInFaces;
	uint64_t reallocTime;
	Result result;
	StucContext pContext;
	BorderTable borderTable;
	EdgeVerts *pEdgeVerts;
	StucCommonAttribList *pCommonAttribList;
	int32_t *pActiveJobs;
	void *pMutex;
	Range inFaceRange;
	int32_t bufSize;
	int32_t rawBufSize;
	int32_t finalBufSize;
	int32_t inFaceOffset;
	int8_t *pInVertTable;
	int32_t id;
	float wScale;
	int8_t maskIdx;
} SendOffArgs;
