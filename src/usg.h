/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <math_utils.h>
#include <types.h>
#include <alloc.h>

typedef struct Usg {
	//allow user to save name of usg meshes if desired?
	//Obviously wouldn't include it in the map file by default,
	//due to potential size increase. But if one wants to go
	//and edit the stuc file later, it may be helpful as a choice
	Mesh *pMesh;
	Mesh *pFlatCutoff;
	V2_F32 origin;
} Usg;

typedef struct InFaceArr {
	struct InFaceArr *pNext;
	I32 *pArr;
	Mat3x3 tbn;
	V3_F32 normal;
	I32 tri[3];
	I32 count;
	I32 usg;
	F32 offset;
} InFaceArr;

typedef struct InFaceTable {
	InFaceArr *pArr;
	LinAlloc alloc;
} InFaceTable;

typedef struct UsgInFace {
	struct UsgInFace *pNext;
	InFaceArr *pEntry;
	I32 face;
} UsgInFace;

typedef struct UsgArr {
	const Mesh *pSquares;
	Usg *pArr;
	StucUsg *pMemArr;
	UsgInFace *pInFaceTable;
	I32 tableSize;
	I32 count;
} UsgArr;

StucResult stucAllocUsgSquaresMesh(
	StucContext pCtx,
	const StucMap pMap,
	Mesh *pMesh
);
StucResult stucFillUsgSquaresMesh(
	const StucMap pMap,
	const StucUsg *pUsgArr,
	Mesh *pMesh
);
StucResult stucAssignUsgsToVerts(
	const StucAlloc *pAlloc,
	StucMap pMap,
	StucUsg *pUsgArr
);
StucResult stucSampleInAttribsAtUsgOrigins(
	StucContext pCtx,
	const StucMap pMap,
	const Mesh *pInMesh,
	StucMesh *pSquares,
	InFaceArr *pInFaceTable
);
UsgInFace *stucGetUsgForCorner(
	I32 stucCorner,
	const StucMap pMap,
	const FaceRange *pMapFace,
	I32 inFace,
	bool *pAboveCutoff
);
void stucUsgVertTransform(
	UsgInFace *pEntry,
	V2_F32 uv,
	V3_F32 *pPos,
	const Mesh *pInMesh,
	V2_F32 tileMin,
	Mat3x3 *pTbn
);
bool stucIsPointInsideMesh(const StucAlloc *pAlloc, V3_F32 pointV3, Mesh *pMesh);