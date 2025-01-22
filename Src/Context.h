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
} StucContextInternal;
