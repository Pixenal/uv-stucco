/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#define MAP_FORMAT_NAME_MAX_LEN 19

#include <uv_stucco.h>
#include <types.h>
#include <hash_table.h>

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
	I32 inAttribCount;
	I32 objCount;
	I32 usgCount;
	I32 cutoffCount;
} StucHeader;

typedef struct StucMapExportIntern {
	StucContext pCtx;
	char *pPath;
	StucHeader header;
	ByteString data;
	I32 cutoffIdxMax;
	HTable matMapTable;
	StucAttribIndexedArr idxAttribs;
} StucMapExportIntern;

typedef struct StucIdxTable {
	PixtyI8Arr table;
	I32 idx;
} StucIdxTable;

typedef struct StucIdxTableArr {
	StucIdxTable *pArr;
	I32 size;
	I32 count;
} StucIdxTableArr;

//void stucWriteDebugImage(Cell *pRootCell);
/*StucErr stucWriteStucFile(
	StucContext pCtx,
	const char *pPath,
	I32 objCount,
	StucObject *pObjArr,
	I32 usgCount,
	StucUsg *pUsgArr,
	StucAttribIndexedArr *pIndexedAttribs
);*/

StucErr stucLoadStucFile(
	StucContext pCtx,
	const char *filePath,
	StucObjArr *pObjArr,
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
void stucEncodeString(const StucAlloc *pAlloc, ByteString *byteString, U8 *string);
void stucDecodeValue(ByteString *byteString, U8 *value, I32 lengthInBits);
void stucDecodeString(ByteString *byteString, char *string, I32 maxLen);
