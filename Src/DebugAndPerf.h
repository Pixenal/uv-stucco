#pragma once

#include <stdint.h>

typedef struct {
	uint64_t timeSpent[3];
	int32_t maxDepth;
	int32_t facesNotSkipped;
	int32_t totalFacesComp;
	uint64_t reallocTime;
} DebugAndPerfVars;
