#pragma once
#include <RUVM.h>

typedef struct RuvmContextInternal {
	void *pCustom;
	RuvmThreadPool threadPool;
	RuvmAlloc alloc;
	RuvmIo io;
	void *pThreadPoolHandle;
	int32_t threadCount;
	RuvmTypeDefaultConfig typeDefaults;
	RuvmStageReport stageReport;
	int32_t stageInterval;
} RuvmContextInternal;
