#pragma once
#include <RUVM.h>

typedef struct RuvmContextInternal {
	RuvmThreadPool threadPool;
	RuvmAllocator alloc;
	RuvmIo io;
	void *pThreadPoolHandle;
	int32_t threadCount;
	RuvmTypeDefaultConfig typeDefaults;
} RuvmContextInternal;
