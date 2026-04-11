/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <pixenals_alloc_utils.h>
#include <pixenals_error_utils.h>

#include <mesh.h>
#include <usg.h>
#include <types.h>
#include <map.h>
#include <pixenals_structs.h>
#include <in_piece.h>

#ifdef WIN32
#define STUC_FORCE_INLINE __forceinline
#else
#define STUC_FORCE_INLINE __attribute__((always_inline)) static inline
#endif

typedef struct MapToMeshBasic {
	Mesh outMesh;
	const StucContext pCtx;
	const Mesh *pInMesh;
	const StucInIslandArr *pInIslands;
	const StucMap pMap;
	InFaceTable *pInFaceTable;
	const StucBlendOptArr *pOptArr;
	I32 inFaceSize;
	const F32 wScale;
	const F32 receiveLen;
	const I8 maskIdx;
} MapToMeshBasic;

typedef struct OutBufIdx {
	FaceCorner corner;
	I32 mergedVert;
} OutBufIdx;

typedef struct OutBufIdxArr {
	OutBufIdx *pArr;
	I32 size;
	I32 count;
} OutBufIdxArr;

typedef struct BufOutRange {
	Range outCorners;
	I32 bufMesh;
	bool clip;
	bool empty;
} BufOutRange;

typedef struct BufOutRangeTable {
	BufOutRange *pArr;
	I32 size;
	I32 count;
} BufOutRangeTable;

typedef struct RangeArr {
	PixtyRange *pArr;
	I32 size;
	I32 count;
} RangeArr;

StucErr stucBuildTangentsForInPieces(
	StucContext pCtx,
	I32 threadId,
	Mesh *pInMesh,
	const BufMeshArr *pBufMeshArr, const BufMeshArr *pBufMeshClipArr,
	PixuctHTable *pMergeTable
);
StucErr stucBuildTangents(void *pArgs);
StucErr stucBuildTangentsForTris(StucContext pCtx, Mesh *pMesh);

StucErr stucInitOutMesh(
	MapToMeshBasic *pBasic,
	PixuctHTable *pMergeTable
	//, I32 snappedVerts
);
void stucAddVertsToOutMesh(
	MapToMeshBasic *pBasic,
	PixuctHTable *pMergeTable,
	I32 vertAllocIdx
);
void stucAddFacesAndCornersToOutMesh(
	MapToMeshBasic *pBasic,
	const BufMeshArr *pBufMeshArr,
	PixuctHTable *pMergeTable,
	OutBufIdxArr *pOutBufIdxArr,
	BufOutRangeTable *pBufOutTable,
	bool clip
);
