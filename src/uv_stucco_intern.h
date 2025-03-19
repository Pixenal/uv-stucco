/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <mesh.h>
#include <debug_and_perf.h>
#include <usg.h>
#include <error.h>
#include <types.h>
#include <alloc.h>

#define STUC_TILE_BIT_LEN 11

typedef U64 UBitField64;
typedef U32 UBitField32;
typedef U16 UBitField16;
typedef U8 UBitField8;

typedef struct {
	I32 start;
	I32 end;
} Range;

typedef struct {
	F32 d[4];
} Color;

typedef struct BorderFace {
	struct BorderFace *pNext;
	I32 mapFace;
	I32 bufFace;
	I32 inFace;
	U32 tileX : STUC_TILE_BIT_LEN;
	U32 tileY : STUC_TILE_BIT_LEN;
	UBitField32 job : 6;
	U32 inOrient : 1;
	U32 mapOrient : 1;
	U32 memType : 2;
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
	I32 verts[2];
} EdgeVerts;

typedef struct {
	BorderBucket *pTable;
	I32 size;
} BorderTable;

typedef struct {
	Mesh outMesh;
	const StucContext pCtx;
	const Mesh *pInMesh;
	const StucMap pMap;
	InFaceTable *pInFaceTable;
	const StucCommonAttribList *pCommonAttribList;
	I32 inFaceSize;
	const F32 wScale;
	const F32 receiveLen;
	const I8 maskIdx;
} MapToMeshBasic;

typedef struct {
	void *pSmall;
	void *pMid;
	void *pLarge;
} BorderTableAlloc;

typedef struct {
	BufMesh bufMesh;
	MapToMeshBasic *pBasic;
	I32 *pActiveJobs;
	U64 reallocTime;
	BorderTable borderTable;
	BorderTableAlloc borderTableAlloc;
	Range inFaceRange;
	I32 id;
	I32 bufSize;
	I32 rawBufSize;
	I32 finalBufSize;
	I32 facesChecked;
	I32 facesUsed;
} MappingJobArgs;