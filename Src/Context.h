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
	char spAttribs[12][STUC_ATTRIB_NAME_MAX_LEN];
} StucContextInternal;
