#pragma once
#include <MathUtils.h>

typedef struct {
	//allow user to save name of usg meshes if desired?
	//Obviously wouldn't include it in the map file by default,
	//due to potential size increase. But if one wants to go
	//and edit the ruvm file later, it may be helpful as a choice
	V2_F32 origin;
} Usg;

typedef struct InFaceArr {
	Mat3x3 tbn;
	V3_F32 normal;
	struct InFaceArr *pNext;
	int32_t *pArr;
	int32_t count;
	int32_t usg;
} InFaceArr;

typedef struct UsgInFace {
	struct UsgInFace *pNext;
	InFaceArr *pEntry;
	int32_t face;
} UsgInFace;

typedef struct {
	Usg *pArr;
	Mesh squares;
	UsgInFace *pInFaceTable;
	int32_t tableSize;
	int32_t count;
} UsgArr;

RuvmResult allocUsgSquaresMesh(RuvmAlloc *pAlloc, RuvmMap pMap);
RuvmResult fillUsgSquaresMesh(RuvmMap pMap, RuvmObject *pUsgArr);
RuvmResult assignUsgsToVerts(RuvmAlloc *pAlloc,
                             RuvmMap pMap, RuvmObject *pUsgArr);
RuvmResult sampleInAttribsAtUsgOrigins(RuvmMap pMap, RuvmMesh *pInMesh,
                                       InFaceArr *pInFaceTable);