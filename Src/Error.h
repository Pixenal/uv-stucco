#pragma once
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <UvStucco.h>

typedef StucResult Result;

//TODO implement STUC_ERROR() if not debug build
#define STUC_ASSERT(message, condition) \
	if (!(condition)) \
	printf("STUC ASSERT in %s, MESSAGE: %s\n", __func__, message); \
	assert(condition);

#define STUC_ERROR(message, err) \
	if (err != STUC_SUCCESS) { \
		printf("STUC ERROR CODE %d in %s, MESSAGE: %s\n", err, __func__, message); \
		goto handle_error; \
	}

#define STUC_CATCH(err, cleanup) \
	if (err != STUC_SUCCESS) { \
	handle_error: \
		##cleanup \
	}