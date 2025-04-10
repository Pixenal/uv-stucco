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

#define TEMP_DISABLE

#ifdef WIN32
#define STUC_FORCE_INLINE __forceinline
#else
#define STUC_FORCE_INLINE __attribute__((always_inline)) static inline
#endif

typedef U64 UBitField64;
typedef U32 UBitField32;
typedef U16 UBitField16;
typedef U8 UBitField8;

typedef struct Color {
	F32 d[4];
} Color;

typedef struct EdgeVerts {
	I32 verts[2];
} EdgeVerts;

typedef struct MapToMeshBasic {
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

typedef struct BorderTableAlloc {
	void *pSmall;
	void *pMid;
	void *pLarge;
} BorderTableAlloc;

typedef struct JobArgs {
	const MapToMeshBasic *pBasic;
	Range range;
	I32 id;
} JobArgs;