/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <types.h>

struct MapToMeshBasic;

typedef struct JobArgs {
	//padding between this and previous arg entry (core is always the first component)
	char padding[PIXTH_CACHELINE_SIZE / 2];
	const void *pShared;
	PixErr (*fpJob) (void *);
	StucCtx *pCtx;
	StucCark *pCark;
	Range range;
	I32 id;
	I32 threadId;
	I32 logInst;
} JobArgs;

typedef struct JobArgsFoot {
	char padding[PIXTH_CACHELINE_SIZE / 2];
} JobArgsFoot;

void stucMakeJobArgs(
	StucCtx *pCtx,
	StucCark *pCark,
	const void *pShared,
	I32 *pJobCount, void *pArgs, I32 argStructSize,
	void *pInitInfo,
	I32 (* fpGetArrCount)(const StucCtx *, const void *, void *),
	void (* fpInitArgEntry)(const StucCtx *, const void *, void *, void *)
);
