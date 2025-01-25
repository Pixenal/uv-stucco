#pragma once
#include <UvStucco.h>

typedef struct StucContextInternal {
	void *pCustom;
	StucThreadPool threadPool;
	StucAlloc alloc;
	StucIo io;
	void *pThreadPoolHandle;
	int32_t threadCount;
	StucTypeDefaultConfig typeDefaults;
	StucStageReport stageReport;
	int32_t stageInterval;
	char spAttribs[11][STUC_ATTRIB_NAME_MAX_LEN];
} StucContextInternal;
