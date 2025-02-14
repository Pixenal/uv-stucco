#pragma once
#include <MathUtils.h>

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
	int32_t tri[3];
	struct InFaceArr *pNext;
	int32_t *pArr;
	int32_t count;
	int32_t usg;
	float offset;
} InFaceArr;

typedef struct UsgInFace {
	struct UsgInFace *pNext;
	InFaceArr *pEntry;
	int32_t face;
} UsgInFace;

typedef struct {
	Usg *pArr;
	StucUsg *pMemArr;
	Mesh squares;
	UsgInFace *pInFaceTable;
	int32_t tableSize;
	int32_t count;
} UsgArr;

StucResult stucAllocUsgSquaresMesh(StucContext pContext, StucAlloc *pAlloc, StucMap pMap);
StucResult stucFillUsgSquaresMesh(StucMap pMap, StucUsg *pUsgArr);
StucResult stucAssignUsgsToVerts(StucAlloc *pAlloc, StucMap pMap, StucUsg *pUsgArr);
StucResult stucSampleInAttribsAtUsgOrigins(StucContext pContext, StucMap pMap, Mesh *pInMesh,
                                           StucMesh *pSquares, InFaceArr *pInFaceTable);
bool stucSampleUsg(int32_t stucCorner, V3_F32 uvw, V3_F32 *pPos, bool *pTransformed, 
                   V3_F32 *pUsgBc, FaceRange *pMapFace, StucMap pMap, int32_t inFace,
                   Mesh *pInMesh, V3_F32 *pNormal, V2_F32 tileMin,
                   bool useFlatCutoff, bool flatCutoffOveride, Mat3x3 *pTbn);
bool stucIsPointInsideMesh(StucAlloc *pAlloc, V3_F32 pointV3, Mesh *pMesh);