/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef enum PixErr {
	PIX_ERR_NOT_SET,
	PIX_ERR_SUCCESS,
	PIX_ERR_ERROR,
	PIX_ERR_QUIET
} PixErr;

#ifdef _DEBUG
#define PIX_ERR_ASSERT(message, condition) \
	if (!(condition)) \
	printf("PX ERR ASSERT in %s, MESSAGE: %s\n", __func__, message); \
	assert(condition);
#else
#define PX_ERR_ASSERT(message, condition)\
	if (!(condition)) \
	printf("PX ERR ASSERT in %s, MESSAGE: %s\n", __func__, message); \
	assert(condition);
#endif

static inline
void printError(
	PixErr err,
	int32_t idx,
	bool isNotCondition,
	const char *pCondition,
	const char *pMessage,
	const char *pFunc
) {
	char *isNotConditionStr = isNotCondition ? "false" : "true";
	printf("PX ERR ERROR THROWN IN %s, IDX: %d, CODE: %d, CONDITION (%s) WAS %s, MESSAGE: %s\n",
		pFunc,
		idx,
		err,
		pCondition,
		isNotConditionStr,
		pMessage
	);
}

#define PIX_ERR_REPORT(message) printf("PX ERR REPORT IN %s, MESSAGE: %s\n", __func__, message)

//TODO add ifdef for putting assert inside PIX_ERR_THROW macros for debug builds,
// or if specified with a define
#define PIX_ERR_THROW_IFNOT_COND(err, condition, message, idx) \
{\
	bool isNotCondition = !(condition);\
	if (err != PIX_ERR_SUCCESS || isNotCondition) { \
		printError(err, idx, isNotCondition, #condition, #message, __func__);\
		err = PIX_ERR_ERROR;\
		goto handle_error_##idx; \
	}\
}

#define PIX_ERR_THROW_IFNOT(err, message, idx)\
	if (err != PIX_ERR_SUCCESS) { \
		printError(err, idx, true, "'N/A'", #message, __func__);\
		err = PIX_ERR_ERROR;\
		goto handle_error_##idx; \
	}

#define PIX_ERR_RETURN_IFNOT_COND(err, condition, message)\
{\
	bool isNotCondition = !(condition);\
	if (err != PIX_ERR_SUCCESS || isNotCondition) { \
		printError(err, -1, isNotCondition, #condition, #message, __func__);\
		err = PIX_ERR_ERROR;\
		return err;\
	}\
}

#define PIX_ERR_RETURN_QUIET_IFNOT_COND(err, condition, message)\
{\
	bool isNotCondition = !(condition);\
	if (err != PIX_ERR_SUCCESS || isNotCondition) { \
		err = PX_ERR_ERROR_QUIET;\
		return err;\
	}\
}

#define PIX_ERR_RETURN_IFNOT(err, message)\
	if (err != PIX_ERR_SUCCESS) { \
		printError(err, -1, true, "'N/A'", #message, __func__);\
		err = PIX_ERR_ERROR;\
		return err;\
	}

#define PIX_ERR_THROW(err, message, idx) \
	printf("PX ERR ERROR THROWN IN %s, IDX: %d, MESSAGE: %s\n",\
		__func__,\
		idx,\
		message); \
	err = PIX_ERR_ERROR;\
	goto handle_error_##idx;

#define PIX_ERR_RETURN(err, message) \
	printf("PX ERR ERROR THROWN IN %s, MESSAGE: %s\n",\
		__func__,\
		 message); \
	err = PIX_ERR_ERROR;\
	return err;

#define PIX_ERR_CATCH(idx, err, cleanup) \
	if (err != PIX_ERR_SUCCESS) { \
	handle_error_##idx: {\
		cleanup \
	}\
}
