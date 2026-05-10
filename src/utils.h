/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <float.h>

#include <pixenals_thread_utils.h>
#include <pixenals_math_utils.h>
#include <pixenals_structs.h>
#include <pixenals_mesh_utils.h>

#include <uv_stucco_intern.h>

typedef enum InsideStatus {
	STUC_INSIDE_STATUS_NONE,
	STUC_INSIDE_STATUS_OUTSIDE,
	STUC_INSIDE_STATUS_INSIDE,
	STUC_INSIDE_STATUS_ON_LINE,
	STUC_INSIDE_STATUS_ON_VERT
} InsideStatus;

typedef enum StucCompare {
	STUC_COMPARE_LESS,
	STUC_COMPARE_EQUAL,
	STUC_COMPARE_GREAT
} StucCompare;

typedef struct HalfPlane {
	V2_F32 uv;
	V2_F32 dirUnit;
	F32 len;
	I32 edge;
} HalfPlane;

static inline
void initHalfPlaneLookup(
	const Mesh *pMesh,
	const FaceRange *pInFace,
	V2_I16 tile,
	HalfPlane *pCache
) {
	V2_F32 fTile = {.d = {(F32)tile.d[0], (F32)tile.d[1]}};
	for (I32 i = 0; i < pInFace->range.size; ++i) {
		pCache[i] = (HalfPlane){
			.edge = stucGetMeshEdge(
				&pMesh->core,
				(FaceCorner) {.face = pInFace->idx, .corner = i}
			),
			.uv = _(pMesh->pUvs[pInFace->range.start + i] V2SUB fTile)
		};
	}
	for (I32 i = 0; i < pInFace->range.size; ++i) {
		I32 iNext = stucGetCornerNext(i, pInFace);
		V2_F32 dir = _(pCache[iNext].uv V2SUB pCache[i].uv);
		pCache[i].len = pixmV2F32Len(dir);
	}
}

void stucGetInFaceBounds(PixmshV2Bb *pBb, const V2_F32 *pUvs, FaceRange face);
bool stucCheckIfVertIsPreserve(const Mesh *pMesh, I32 vert);
bool stucCheckIfEdgeIsReceive(const Mesh *pMesh, I32 edge, F32 receiveLen);

InsideStatus stucIsPointInHalfPlane(
	V2_F32 point,
	V2_F32 lineA,
	V2_F32 halfPlane,
	bool wind
);

static inline
I32 stucCalcFaceWindFromVerts(const PixmshFaceRange face, const Mesh *pMesh) {
	return pixmshCalcFaceWind(face, pMesh, stucGetVertPosAsV2);
}
static inline
I32 stucCalcFaceWindFromUvs(const PixmshFaceRange face, const Mesh *pMesh) {
	return pixmshCalcFaceWind(face, pMesh, stucGetUvPos);
}

static inline
V3_F32 stucGetBarycentricInTriFromVerts(
	const Mesh *pMesh,
	const FaceRange *pFace,
	const I8 *pTriCorners,
	V2_F32 vert
) {
	return
		pixmshGetBarycentricInTri(pMesh, pFace->range, stucGetVertPos, pTriCorners, vert);
}

static inline
V3_F32 stucGetBarycentricInFaceFromVerts(
	const Mesh *pMesh,
	const FaceRange *pFace,
	I8 *pTriCorners,
	V2_F32 vert
) {
	return pixmshGetBarycentricInFace(
		pMesh,
		pFace->range,
		(V2_I16) {0},
		stucGetVertPos,
		pTriCorners,
		vert
	);
}

static inline
V3_F32 stucGetBarycentricInFaceFromUvs(
	const Mesh *pMesh,
	const FaceRange *pFace,
	V2_I16 tile,
	I8 *pTriCorners,
	V2_F32 vert
) {
	return pixmshGetBarycentricInFace(
		pMesh,
		pFace->range,
		tile,
		stucGetUvPosAsV3,
		pTriCorners,
		vert
	);
}

StucErr stucBuildEdgeList(StucContext pCtx, StucMesh *pMesh);
void stucProgressBarClear();
void stucProgressBarPrint(StucContext pCtx, I32 progress);
void stucStageBegin(void *pCtx, StucStageReport *pReport, const char *pName);
void stucStageProgress(void *pCtx, StucStageReport *pReport, I32 progress);
void stucStageEnd(void *pCtx, StucStageReport *pReport);
void stucStageBeginWrap(StucContext pCtx, const char *pName, I32 max);
void stucStageProgressWrap(StucContext pCtx, I32 progress);
void stucStageEndWrap(StucContext pCtx);
void stucSetStageName(StucContext pCtx, const char *pName);
F32 stucGetT(V2_F32 point, V2_F32 lineA, V2_F32 lineUnit, F32 lineLen);
I32 stucIdxBitArray(UBitField8 *pArr, I32 idx, I32 len);
void stucSetBitArr(UBitField8 *pArr, I32 idx, I32 value, I32 len);
STUC_FORCE_INLINE
void stucInsertionSort(
	I32 *pIdxArr,
	I32 count,
	const void *pData,
	StucCompare (*fpCompare)(const void *, I32, I32)
) {
	bool order = fpCompare(pData, 0, 1) == STUC_COMPARE_LESS;
	pIdxArr[0] = !order;
	pIdxArr[1] = order;
	I32 bufSize = 2;
	for (I32 i = bufSize; i < count; ++i) {
		bool insert = false;
		I32 j;
		for (j = bufSize - 1; j >= 0; --j) {
			insert =
				fpCompare(pData, i, pIdxArr[j]) == STUC_COMPARE_LESS &&
				fpCompare(pData, i, pIdxArr[j - 1]) == STUC_COMPARE_GREAT;
			if (insert) {
				break;
			}
		}
		if (!insert) {
			pIdxArr[bufSize] = i;
		}
		else {
			for (I32 m = bufSize; m > j; --m) {
				pIdxArr[m] = pIdxArr[m - 1];
				PIX_ERR_ASSERT("", m <= bufSize && m > j);
			}
			pIdxArr[j] = i;
		}
		bufSize++;
	}
}
M3x3 stucGetInterpolatedTbn(
	const Mesh *pMesh,
	const FaceRange *pFace,
	const I8 *pTriCorners,
	V3_F32 bc
);

StucErr stucDoJobInParallel(
	StucContext pCtx,
	I32 threadId,
	I32 jobCount, void *pJobArgs, I32 argStructSize,
	StucErr (* func)(void *)
);

/*
typedef struct InPieceKey {
	I32 cluster;
	V2_I16 tile;
	bool clip;
} InPieceKey;
*/

typedef struct InPieceKey {
	U8 key[9];
} InPieceKey;

static inline
I32 inPieceKeyGetClust(InPieceKey key) {
	return *(I32 *)key.key;
}

static inline
V2_I16 inPieceKeyGetTile(InPieceKey key) {
	I32 offset = 4;
	return (V2_I16){{*(I16 *)(key.key + offset), *(I16 *)(key.key + offset + 2)}};
}

static inline
I32 inPieceKeyGetClip(InPieceKey key) {
	return *(U8 *)(key.key + 8);
}

static inline
void inPieceKeySetClust(InPieceKey *pKey, I32 clust) {
	*(I32 *)pKey->key = clust;
}

static inline
void inPieceKeySetTile(InPieceKey *pKey, V2_I16 tile) {
	I32 offset = 4;
	*(I16 *)(pKey->key + offset) = tile.d[0];
	*(I16 *)(pKey->key + offset + 2) = tile.d[1];
}

static inline
void inPieceKeySetClip(InPieceKey *pKey, bool clip) {
	*(U8 *)(pKey->key + 8) = clip;
}

static inline
PixuctKey stucInPieceMakeKey(const void *pKeyRaw) {
	const InPieceKey *pKey = pKeyRaw;
	return (PixuctKey){.pKey = pKey->key, .size = sizeof(pKey->key)};
}

void stucThreadPoolSetDefault(StucContext context);
void stucAllocSetCustom(PixalcFPtrs *pAlloc, PixalcFPtrs *pCustomAlloc);
void stucAllocSetDefault(PixalcFPtrs *pAlloc);
void stucBuildEdgeLenList(StucContext pCtx, Mesh *pMesh);
bool stucCheckIfNoFacesHaveMaskIdx(const Mesh *pMesh, I8 maskIdx);
void stucBuildEdgeAdj(Mesh *pMesh);
void stucBuildSeamAndPreserveTables(Mesh *pMesh);
StucErr StucSplitMeshToIslands(
	StucContext pCtx,
	const Mesh *pMesh,
	StucInIslandArr *pIslands
);
void stucInIslandsBorderArrDestroy(StucContext pCtx, BorderArr *pArr);
void stucInIslandsDestroy(StucContext pCtx, StucInIslandArr *pArr);

static inline
PixtyV2_F32 stucClustUv(const void *pMeshRaw, I32 corner) {
	const Mesh *pMesh = pMeshRaw;
	PIX_ERR_ASSERT("", pMesh->pUvs && corner >= 0 && corner < pMesh->core.cornerCount);
	return pMesh->pUvs[corner];
}

static inline
PixtyV2_F32 stucClustPos(const void *pMeshRaw, I32 vert) {
	const Mesh *pMesh = pMeshRaw;
	PIX_ERR_ASSERT("", pMesh->pPos && vert >= 0 && vert < pMesh->core.vertCount);
	return *(PixtyV2_F32 *)&pMesh->pPos[vert];
}

static inline
I32 stucClustVert(const void *pMeshRaw, I32 corner) {
	const Mesh *pMesh = pMeshRaw;
	PIX_ERR_ASSERT("", corner >= 0 && corner < pMesh->core.cornerCount);
	return pMesh->core.pCorners[corner];
}

static inline
I32 stucTriangulateFaceFromVerts(
	const StucAlloc *pAlloc,
	const FaceRange *pFace,
	const Mesh *pMesh,
	U8 *pTris
) {
	return pixmshTriangulateFace(pAlloc, pFace->range, pMesh, stucGetVertPos, pTris);
}

static inline
bool stucGetIfPreserveEdge(const Mesh *pMesh, I32 edge) {
	PIX_ERR_ASSERT("", pMesh && edge >= 0);
	if (pMesh->pEdgePreserve) {
		PIX_ERR_ASSERT("", pMesh->pEdgePreserve[edge] % 2 == pMesh->pEdgePreserve[edge]);
	}
	return pMesh->pEdgePreserve ? pMesh->pEdgePreserve[edge] : false;
}

static inline
I32 stucCouldInEdgeIntersectMapFace(const Mesh *pMesh, I32 edge) {
	bool preserve = stucGetIfPreserveEdge(pMesh, edge);
	bool ret = stucGetIfSeamEdge(pMesh, edge) || stucGetIfMatBorderEdge(pMesh, edge);
	return preserve && !ret ? 2 : preserve || ret;
}
