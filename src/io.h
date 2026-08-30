/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <pixenals_io_utils.h>
#include <pixenals_structs.h>

#include <uv_stucco.h>
#include <types.h>
#include <map.h>

typedef struct StucMapDeps {
	PixtyStrArr maps;
} StucMapDeps;

typedef struct StucIdxTable {
	PixtyI8Arr table;
	I32 idx;
} StucIdxTable;

typedef struct StucIdxTableArr {
	StucIdxTable *pArr;
	I32 size;
	I32 count;
	bool hasRedirect;
} StucIdxTableArr;

typedef struct ObjMapOpts {
	StucMapArr arr;
	I32 obj;
} ObjMapOpts;

typedef struct ObjMapOptsArr {
	ObjMapOpts *pArr;
	I32 size;
	I32 count;
} ObjMapOptsArr;

StucErr stucMapImportGetDep(
	StucCtx *pCtx,
	const char *filePath,
	StucMapDeps *pDeps
);

StucErr stucMapImport(
	StucCtx *pCtx,
	const char *filePath,
	StucObjArr *pObjArr,
	ObjMapOptsArr *pMapOptsArr,
	StucUsgArr *pUsgArr,
	StucObjArr *pCutoffArr,
	StucIdxTableArr **ppIdxTableArrs,
	StucAttribIndexedArr *pIndexedAttribs,
	bool correctIdxAttribs
);

void stucIoInit();
void stucIoSetCustom(StucCtx *pCtx, PixioFPtrs *pIo);
void stucIoSetDefault(StucCtx *pCtx);
const char *stucGetBasename(const char *pStr, I32 *pNameLen, I32 *pPathLen);
void stucIoDataTagValidate();
StucErr stucWalkMapDeps(StucMapLoad *pLoadCtx);
static inline void stucMapDepsClear(StucMapDeps *pDeps) {
	pDeps->maps.count = 0;
}
static inline void stucMapDepsDestroy(const StucAlloc *pAlloc, StucMapDeps *pDeps) {
	if (pDeps->maps.pArr) {
		for (I32 i = 0; i < pDeps->maps.count; ++i) {
			if (pDeps->maps.pArr[i].pStr) {
				pAlloc->fpFree(pDeps->maps.pArr[i].pStr);
			}
		}
		pAlloc->fpFree(pDeps->maps.pArr);
	}
	*pDeps = (StucMapDeps){0};
}
