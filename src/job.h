/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <types.h>

struct MapToMeshBasic;

typedef struct JobArgs {
	const void *pShared;
	StucContext pCtx;
	Range range;
	I32 id;
} JobArgs;

void stucMakeJobArgs(
	StucContext pCtx,
	void *pShared,
	I32 *pJobCount, void *pArgs, I32 argStructSize,
	void *pInitInfo,
	I32 (* fpGetArrCount)(StucContext, const void *, void *),
	void (* fpInitArgEntry)(StucContext, void *, void *, void *)
);
