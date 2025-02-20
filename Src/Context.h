#pragma once
#include <UvStucco.h>
#include <Types.h>

#define STUC_ATTRIB_SP_COUNT 17

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
	char spAttribNames[STUC_ATTRIB_SP_COUNT][STUC_ATTRIB_NAME_MAX_LEN];
	StucAttribType spAttribTypes[STUC_ATTRIB_SP_COUNT];
	StucDomain spAttribDomains[STUC_ATTRIB_SP_COUNT];
} StucContextInternal;
