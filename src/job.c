/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <pixenals_thread_utils.h>

#include <job.h>
#include <uv_stucco_intern.h>

static
void setJobArgsCore(
	StucContext pCtx,
	const void *pShared,
	JobArgs *pCore,
	Range *pRanges,
	I32 jobIdx
) {
	pCore->pCtx = pCtx;
	pCore->pShared = pShared;
	pCore->range = pRanges[jobIdx];
	pCore->id = jobIdx;
}

static
void divideArrAmongstJobs(I32 arrSize, I32 *pJobCount, Range *pRanges) {
	if (!arrSize) {
		*pJobCount = 0;
		return;
	}
	PIX_ERR_ASSERT("", *pJobCount >= 0);
	I32 jobCount =  *pJobCount && *pJobCount < PIX_THREAD_MAX_SUB_MAPPING_JOBS ?
		*pJobCount : PIX_THREAD_MAX_SUB_MAPPING_JOBS;
	I32 piecesPerJob = arrSize / jobCount;
	jobCount = !piecesPerJob ? 1 : jobCount;
	for (I32 i = 0; i < jobCount; ++i) {
		pRanges[i].start = piecesPerJob * i;
		pRanges[i].end = i == jobCount - 1 ? arrSize : pRanges[i].start + piecesPerJob;
	}
	*pJobCount = jobCount;
}

void stucMakeJobArgs(
	StucContext pCtx,
	void *pShared,
	I32 *pJobCount, void *pArgs, I32 argStructSize,
	void *pInitInfo,
	I32 (* fpGetArrCount)(StucContext, const void *, void *),
	void (* fpInitArgEntry)(StucContext, void *, void *, void *)
) {
	Range ranges[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = {0};
	divideArrAmongstJobs(fpGetArrCount(pCtx, pShared, pInitInfo), pJobCount, ranges);
	for (I32 i = 0; i < *pJobCount; ++i) {
		void *pArgEntry = (U8 *)pArgs + i * argStructSize;
		setJobArgsCore(pCtx, pShared, (JobArgs *)pArgEntry, ranges, i);
		if (fpInitArgEntry) {
			fpInitArgEntry(pCtx, pShared, pInitInfo, pArgEntry);
		}
	}
}
