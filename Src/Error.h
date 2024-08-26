#pragma once
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <RUVM.h>

typedef RuvmResult Result;

//TODO implement RUVM_ERROR() if not debug build
#define RUVM_ASSERT(message, condition) \
	if (!(condition)) \
	printf("RUVM ASSERT in %s, MESSAGE: %s\n", __func__, message); \
	assert(condition);

#define RUVM_ERROR(message, err) \
	if (err != RUVM_SUCCESS) { \
		printf("RUVM ERROR CODE %d in %s, MESSAGE: %s\n", err, __func__, message); \
		goto handle_error; \
	}

#define RUVM_RETURN(err, cleanup) \
	if (err != RUVM_SUCCESS) { \
	handle_error: \
		##cleanup \
	} \
	return err;