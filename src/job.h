/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <types.h>

struct MapToMeshBasic;

typedef struct JobArgs {
	const struct MapToMeshBasic *pBasic;
	Range range;
	I32 id;
} JobArgs;

void stucMakeJobArgs(
	struct MapToMeshBasic *pBasic,
	I32 *pJobCount, void *pArgs, I32 argStructSize,
	void *pInitInfo,
	I32 (* fpGetArrCount)(const struct MapToMeshBasic *, void *),
	void (* fpInitArgEntry)(struct MapToMeshBasic *, void *, void *)
);
