/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <uv_stucco_intern.h>
#include <utils.h>
#include <in_piece.h>

typedef enum InterpCacheActive {
	STUC_INTERP_CACHE_NONE,
	STUC_INTERP_CACHE_COPY_IN,
	STUC_INTERP_CACHE_COPY_MAP,
	STUC_INTERP_CACHE_LERP_IN,
	STUC_INTERP_CACHE_LERP_MAP,
	STUC_INTERP_CACHE_TRI_IN,
	STUC_INTERP_CACHE_TRI_MAP
} InterpCacheActive;

typedef struct InterpCacheCopy {
	InterpCacheActive active;
	FaceRange mapFace;
	I32 inFace;
	I32 a;//corner or vert
} InterpCacheCopy;

typedef struct InterpCacheLerp {
	InterpCacheActive active;
	I32 a;
	I32 b;
	F32 t;
} InterpCacheLerp;

typedef struct InterpCacheTri {
	InterpCacheActive active;
	I32 triReal[3];
	V3_F32 bc;
} InterpCacheTri;

typedef union InterpCache {
	InterpCacheActive active;
	InterpCacheCopy copyIn;
	InterpCacheCopy copyMap;
	InterpCacheLerp lerpIn;
	InterpCacheLerp lerpMap;
	InterpCacheTri triIn;
	InterpCacheTri triMap;
} InterpCache;

typedef struct InterpCacheLimited {
	const StucDomain domain;
	const AttribOrigin origin;
	InterpCache cache;
} InterpCacheLimited;

typedef struct InterpCaches {
	InterpCacheLimited in;
	InterpCacheLimited map;
} InterpCaches;

StucErr stucInterpCornerAttribs(void *pArgsVoid);
StucErr stucInterpFaceAttribs(void *pArgsVoid);
void stucInterpBufAttrib(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner corner,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
);
StucErr stucInterpAttribs(
	MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	PixuctHTable *pMergeTable,
	const BufOutRangeTable *pBufOutTable,
	const OutBufIdxArr *pOutBufIdxArr,
	StucDomain domain,
	StucErr (* job)(void *)
);
StucErr stucXFormAndInterpVerts(
	MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	PixuctHTable *pMergeTable,
	I32 vertAllocIdx
);
