#pragma once
#include <MathUtils.h>
#include <Types.h>

typedef struct {
	//allow user to save name of usg meshes if desired?
	//Obviously wouldn't include it in the map file by default,
	//due to potential size increase. But if one wants to go
	//and edit the stuc file later, it may be helpful as a choice
	Mesh *pMesh;
	Mesh *pFlatCutoff;
	V2_F32 origin;
} Usg;

typedef struct InFaceArr {
	Mat3x3 tbn;
	V3_F32 normal;
	I32 tri[3];
	struct InFaceArr *pNext;
	I32 *pArr;
	I32 count;
	I32 usg;
	F32 offset;
} InFaceArr;

typedef struct UsgInFace {
	struct UsgInFace *pNext;
	InFaceArr *pEntry;
	I32 face;
} UsgInFace;

typedef struct {
	Usg *pArr;
	StucUsg *pMemArr;
	Mesh squares;
	UsgInFace *pInFaceTable;
	I32 tableSize;
	I32 count;
} UsgArr;

StucResult stucAllocUsgSquaresMesh(
	StucContext pCtx,
	const StucAlloc *pAlloc,
	StucMap pMap
);
StucResult stucFillUsgSquaresMesh(StucMap pMap, StucUsg *pUsgArr);
StucResult stucAssignUsgsToVerts(
	const StucAlloc *pAlloc,
	StucMap pMap,
	StucUsg *pUsgArr
);
StucResult stucSampleInAttribsAtUsgOrigins(
	StucContext pCtx,
	StucMap pMap,
	Mesh *pInMesh,
	StucMesh *pSquares,
	InFaceArr *pInFaceTable
);
bool stucSampleUsg(
	I32 stucCorner,
	V3_F32 uvw,
	V3_F32 *pPos,
	bool *pTransformed,
	V3_F32 *pUsgBc,
	FaceRange *pMapFace,
	StucMap pMap,
	I32 inFace,
	const Mesh *pInMesh,
	V3_F32 *pNormal,
	V2_F32 tileMin,
	bool useFlatCutoff,
	bool flatCutoffOveride,
	Mat3x3 *pTbn
);
bool stucIsPointInsideMesh(const StucAlloc *pAlloc, V3_F32 pointV3, Mesh *pMesh);