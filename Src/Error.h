#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define RUVM_ASSERT(message, condition) \
	if (!(condition)) printf("RUVM ASSERT MESSAGE: %s\n", message); \
	assert(condition);