/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <uv_stucco.h>

typedef StucResult Result;

#ifdef _DEBUG
#define STUC_ASSERT(message, condition) \
	if (!(condition)) \
	printf("STUC ASSERT in %s, MESSAGE: %s\n", __func__, message); \
	assert(condition);
#else
#define STUC_ASSERT(message, condition)\
	if (!(condition)) \
	printf("STUC ASSERT in %s, MESSAGE: %s\n", __func__, message); \
	assert(condition);
#endif

static inline
void printError(
	Result err,
	int32_t idx,
	bool isNotCondition,
	const char *pCondition,
	const char *pMessage,
	const char *pFunc
) {
	char *isNotConditionStr = isNotCondition ? "false" : "true";
	printf("STUC ERROR THROWN IN %s, IDX: %d, CODE: %d, CONDITION (%s) WAS %s, MESSAGE: %s\n",
		pFunc,
		idx,
		err,
		pCondition,
		isNotConditionStr,
		pMessage
	);
}

#define STUC_REPORT(message) printf("STUC REPORT IN %s, MESSAGE: %s\n", __func__, message)

//TODO add ifdef for putting assert inside STUC_THROW macros for debug builds,
// or if specified with a define
#define STUC_THROW_IFNOT_COND(err, condition, message, idx) \
{\
	bool isNotCondition = !(condition);\
	if (err != STUC_SUCCESS || isNotCondition) { \
		printError(err, idx, isNotCondition, #condition, #message, __func__);\
		err = STUC_ERROR;\
		goto handle_error_##idx; \
	}\
}

#define STUC_THROW_IFNOT(err, message, idx)\
	if (err != STUC_SUCCESS) { \
		printError(err, idx, true, "'N/A'", #message, __func__);\
		err = STUC_ERROR;\
		goto handle_error_##idx; \
	}

#define STUC_RETURN_ERR_IFNOT_COND(err, condition, message)\
{\
	bool isNotCondition = !(condition);\
	if (err != STUC_SUCCESS || isNotCondition) { \
		printError(err, -1, isNotCondition, #condition, #message, __func__);\
		err = STUC_ERROR;\
		return err;\
	}\
}

#define STUC_RETURN_ERR_IFNOT(err, message)\
	if (err != STUC_SUCCESS) { \
		printError(err, -1, true, "'N/A'", #message, __func__);\
		err = STUC_ERROR;\
		return err;\
	}

#define STUC_THROW(err, message, idx) \
	printf("STUC ERROR THROWN IN %s, IDX: %d, MESSAGE: %s\n",\
		__func__,\
		idx,\
		message); \
	err = STUC_ERROR;\
	goto handle_error_##idx;

#define STUC_RETURN_ERR(err, message) \
	printf("STUC ERROR THROWN IN %s, MESSAGE: %s\n",\
		__func__,\
		 message); \
	err = STUC_ERROR;\
	return err;

#define STUC_CATCH(idx, err, cleanup) \
	if (err != STUC_SUCCESS) { \
	handle_error_##idx: {\
		cleanup \
	}\
}
