#pragma once

#define MAP_FORMAT_NAME_MAX_LEN 19

#include <QuadTree.h>
#include <UvStucco.h>
#include <Types.h>

typedef struct {
    unsigned char *pString;
    I64 size;
    I64 nextBitIdx;
    I64 byteIdx;
} ByteString;

//void stucWriteDebugImage(Cell *pRootCell);
StucResult stucWriteStucFile(
	StucContext pCtx,
	char *pName,
	I32 objCount,
	StucObject *pObjArr,
	I32 usgCount,
	StucUsg *pUsgArr,
	StucAttribIndexedArr *pIndexedAttribs
);
StucResult stucLoadStucFile(
	StucContext pCtx,
	char *filePath,
	I32 *pObjCount,
	StucObject **ppObjArr,
	I32 *pUsgCount,
	StucUsg **ppUsgArr,
	I32 *pFlatCutoffCount,
	StucObject **ppFlatCutoffArr,
	bool forEdit,
	StucAttribIndexedArr *pIndexedAttribs
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
