#pragma once
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <UvStucco.h>

typedef StucResult Result;

#define STUC_ASSERT(message, condition) \
	if (!(condition)) \
	printf("STUC ASSERT in %s, MESSAGE: %s\n", __func__, message); \
	assert(condition);

//TODO add ifdef for putting assert inside STUC_THROW macros for debug builds,
// or if specified with a define
#define STUC_THROW_IF(err, condition, message, idx) \
	{\
		bool isError = err != STUC_SUCCESS;\
		bool isNotCondition = !(condition);\
		if (isError || isNotCondition) { \
			char *isNotConditionStr = isNotCondition ? "false" : "true";\
			printf("STUC ERROR THROWN IN %s, IDX: %d, CODE: %d, CONDITION (%s) WAS %s, MESSAGE: %s\n",\
			       __func__, idx, err, #condition, isNotConditionStr, #message); \
			err = STUC_ERROR;\
			goto handle_error_##idx; \
		}\
	}

#define STUC_THROW(err, message, idx) \
	printf("STUC ERROR THROWN IN %s, IDX: %d, MESSAGE: %s\n",\
	       __func__, idx, #message); \
	err = STUC_ERROR;\
	goto handle_error_##idx;

#define STUC_CATCH(idx, err, cleanup) \
	if (err != STUC_SUCCESS) { \
	handle_error_##idx: \
		##cleanup \
	}