/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <pixenals_thread_utils.h>

#include <job.h>
#include <uv_stucco_intern.h>

static
void setJobArgsCore(
	const MapToMeshBasic *pBasic,
	JobArgs *pCore,
	Range *pRanges,
	I32 jobIdx
) {
	pCore->pBasic = pBasic;
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
	MapToMeshBasic *pBasic,
	I32 *pJobCount, void *pArgs, I32 argStructSize,
	void *pInitInfo,
	I32 (* fpGetArrCount)(const MapToMeshBasic *, void *),
	void (* fpInitArgEntry)(MapToMeshBasic *, void *, void *)
) {
	Range ranges[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = {0};
	divideArrAmongstJobs(fpGetArrCount(pBasic, pInitInfo), pJobCount, ranges);
	for (I32 i = 0; i < *pJobCount; ++i) {
		void *pArgEntry = (U8 *)pArgs + i * argStructSize;
		setJobArgsCore(pBasic, (JobArgs *)pArgEntry, ranges, i);
		if (fpInitArgEntry) {
			fpInitArgEntry(pBasic, pInitInfo, pArgEntry);
		}
	}
}
