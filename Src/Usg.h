#pragma once
#include <MathUtils.h>

typedef struct {
	//allow user to save name of usg meshes if desired?
	//Obviously wouldn't include it in the map file by default,
	//due to potential size increase. But if one wants to go
	//and edit the ruvm file later, it may be helpful as a choice
	V2_F32 origin;
} Usg;

typedef struct {
	Usg *pArr;
	int32_t count;
} UsgArr;

RuvmResult assignUsgsToVerts(RuvmContext pContext,
                             RuvmMap pMap, RuvmObject *pUsgArr);