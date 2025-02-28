#pragma once
#include <UvStucco.h>
#include <Types.h>

typedef struct StucContextInternal {
	void *pCustom;
	StucThreadPool threadPool;
	StucAlloc alloc;
	StucIo io;
	void *pThreadPoolHandle;
	I32 threadCount;
	StucTypeDefaultConfig typeDefaults;
	StucStageReport stageReport;
	I32 stageInterval;
	char spAttribNames[STUC_ATTRIB_USE_SP_ENUM_COUNT][STUC_ATTRIB_NAME_MAX_LEN];
	StucAttribType spAttribTypes[STUC_ATTRIB_USE_SP_ENUM_COUNT];
	StucDomain spAttribDomains[STUC_ATTRIB_USE_SP_ENUM_COUNT];
} StucContextInternal;
