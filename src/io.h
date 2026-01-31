/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#define MAP_FORMAT_NAME_MAX_LEN 19
#define MAP_FORMAT_NAME "UV Stucco Map"

#include <uv_stucco.h>
#include <types.h>
#include <pixenals_structs.h>

typedef struct ByteString {
	unsigned char *pString;
	I64 size;
	I64 nextBitIdx;
	I64 byteIdx;
} ByteString;

typedef struct StucHeader {
	char format[MAP_FORMAT_NAME_MAX_LEN];
	I64 dataSize;
	I64 dataSizeCompressed;
	I32 version;
	I32 idxAttribCount;
	I32 objCount;
	I32 usgCount;
	I32 cutoffCount;
} StucHeader;

typedef struct StucMapDeps {
	PixtyStrArr maps;
} StucMapDeps;

typedef struct StucMapExportIntern {
	StucContext pCtx;
	char *pPath;
	StucHeader header;
	ByteString data;
	I32 cutoffIdxMax;
	PixuctHTable mapTable;
	StucAttribIndexedArr idxAttribs;
	I8Arr matMapTable;
	bool compress;
} StucMapExportIntern;

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
	StucContext pCtx,
	const char *filePath,
	StucMapDeps *pDeps
);

StucErr stucMapImport(
	StucContext pCtx,
	const char *filePath,
	StucObjArr *pObjArr,
	ObjMapOptsArr *pMapOptsArr,
	StucUsgArr *pUsgArr,
	StucObjArr *pCutoffArr,
	StucIdxTableArr **ppIdxTableArrs,
	StucAttribIndexedArr *pIndexedAttribs,
	bool correctIdxAttribs
);

void stucIoSetCustom(StucContext pCtx, StucIo *pIo);
void stucIoSetDefault(StucContext pCtx);
void stucEncodeValue(
	const StucAlloc *pAlloc,
	ByteString *byteString,
	U8 *value,
	I32 lengthInBits
);
void stucEncodeString(const StucAlloc *pAlloc, ByteString *byteString, const char *string);
void stucDecodeValue(ByteString *byteString, U8 *value, I32 lengthInBits);
void stucDecodeString(ByteString *byteString, char *string, I32 maxLen);
const char *stucGetBasename(const char *pStr, I32 *pNameLen, I32 *pPathLen);
void stucIoDataTagValidate();
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
