/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

//TODO these should be prefixed with STUC_
#define VERT_ATTRIBUTE_AMOUNT 3
#define LOOP_ATTRIBUTE_AMOUNT 3
#define ENCODE_DECODE_BUFFER_LENGTH 34
#define STUC_MAP_VERSION 101
#define STUC_FLAT_CUTOFF_HEADER_SIZE 56
#define STUC_WINDOW_BITS 31 //15 (+16 as using gzip)

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

#include <zlib.h>

#include <pixenals_io_utils.h>
#include <pixenals_math_utils.h>
#include <pixenals_error_utils.h>

#include <io.h>
#include <map.h>
#include <context.h>
#include <attrib_utils.h>

typedef enum DataTag {
	TAG_NONE,
	TAG_HEADER,
	TAG_DATA,
	TAG_OBJECT,
	TAG_ACTIVE_ATTRIBS,
	TAG_IDX_REDIRECTS,
	TAG_MAP_OVERRIDES,
	TAG_XFORM,
	TAG_OBJECT_TYPE,
	TAG_MESH_HEADER,
	TAG_MESH_ATTRIBS,
	TAG_FACE_LIST,
	TAG_FACE_ATTRIBS,
	TAG_CORNER_AND_EDGE_LISTS,
	TAG_CORNER_ATTRIBS,
	TAG_EDGE_ATTRIBS,
	TAG_VERT_ATTRIBS,
	TAG_IDX_ATTRIBS,
	TAG_DEP,
	TAG_DEP_TYPE_MAP,
	TAG_TYPE_OBJECT,
	TAG_TYPE_TARGET,
	TAG_TYPE_USG,
	TAG_TYPE_USG_FLAT_CUTOFF,
	TAG_ENUM_COUNT
} DataTag;

#define DATA_TAG_KEY(charA, charB) ((U64)charA * 16777619 | (U64)charB * 7907 << 32)
#define DATA_TAG_KEY_MAX 251
#define DATA_TAG_KEY_TO_STR(key) \
	(char[2]){(key & 0xffffffff) / 16777619, (key >> 32) / 7907}

#define TAG_STR_HEADER                DATA_TAG_KEY('H', 'E')
#define TAG_STR_DATA                  DATA_TAG_KEY('D', 'A')
#define TAG_STR_OBJECT                DATA_TAG_KEY('O', 'B')
#define TAG_STR_ACTIVE_ATTRIBS        DATA_TAG_KEY('A', 'A')
#define TAG_STR_IDX_REDIRECTS         DATA_TAG_KEY('I', 'R')
#define TAG_STR_MAP_OVERRIDES         DATA_TAG_KEY('M', 'O')
#define TAG_STR_XFORM                 DATA_TAG_KEY('X', 'F')
#define TAG_STR_OBJECT_TYPE           DATA_TAG_KEY('O', 'T')
#define TAG_STR_MESH_HEADER           DATA_TAG_KEY('M', 'H')
#define TAG_STR_MESH_ATTRIBS          DATA_TAG_KEY('M', 'A')
#define TAG_STR_FACE_LIST             DATA_TAG_KEY('F', 'L')
#define TAG_STR_FACE_ATTRIBS          DATA_TAG_KEY('F', 'A')
#define TAG_STR_CORNER_AND_EDGE_LISTS DATA_TAG_KEY('C', 'L')
#define TAG_STR_CORNER_ATTRIBS        DATA_TAG_KEY('C', 'A')
#define TAG_STR_EDGE_ATTRIBS          DATA_TAG_KEY('E', 'A')
#define TAG_STR_VERT_ATTRIBS          DATA_TAG_KEY('V', 'A')
#define TAG_STR_IDX_ATTRIBS           DATA_TAG_KEY('I', 'A')
#define TAG_STR_DEP                   DATA_TAG_KEY('D', 'P')
#define TAG_STR_DEP_TYPE_MAP          DATA_TAG_KEY('T', 'M')
#define TAG_STR_TYPE_OBJECT           DATA_TAG_KEY('T', 'O')
#define TAG_STR_TYPE_TARGET           DATA_TAG_KEY('T', 'T')
#define TAG_STR_TYPE_USG              DATA_TAG_KEY('T', 'U')
#define TAG_STR_TYPE_USG_FLAT_CUTOFF  DATA_TAG_KEY('T', 'F')

#define DATA_TAG_WRAP(key) (key % DATA_TAG_KEY_MAX)
static const I8 dataTagKeyToTag[DATA_TAG_KEY_MAX] = {
	[0] = 0,
	[DATA_TAG_WRAP(TAG_STR_HEADER)] = TAG_HEADER,
	[DATA_TAG_WRAP(TAG_STR_DATA)] = TAG_DATA,
	[DATA_TAG_WRAP(TAG_STR_OBJECT)] = TAG_OBJECT,
	[DATA_TAG_WRAP(TAG_STR_ACTIVE_ATTRIBS)] = TAG_ACTIVE_ATTRIBS,
	[DATA_TAG_WRAP(TAG_STR_IDX_REDIRECTS)] = TAG_IDX_REDIRECTS,
	[DATA_TAG_WRAP(TAG_STR_MAP_OVERRIDES)] = TAG_MAP_OVERRIDES,
	[DATA_TAG_WRAP(TAG_STR_XFORM)] = TAG_XFORM,
	[DATA_TAG_WRAP(TAG_STR_OBJECT_TYPE)] = TAG_OBJECT_TYPE,
	[DATA_TAG_WRAP(TAG_STR_MESH_HEADER)] = TAG_MESH_HEADER,
	[DATA_TAG_WRAP(TAG_STR_MESH_ATTRIBS)] = TAG_MESH_ATTRIBS,
	[DATA_TAG_WRAP(TAG_STR_FACE_LIST)] = TAG_FACE_LIST,
	[DATA_TAG_WRAP(TAG_STR_FACE_ATTRIBS)] = TAG_FACE_ATTRIBS,
	[DATA_TAG_WRAP(TAG_STR_CORNER_AND_EDGE_LISTS)] = TAG_CORNER_AND_EDGE_LISTS,
	[DATA_TAG_WRAP(TAG_STR_CORNER_ATTRIBS)] = TAG_CORNER_ATTRIBS,
	[DATA_TAG_WRAP(TAG_STR_EDGE_ATTRIBS)] = TAG_EDGE_ATTRIBS,
	[DATA_TAG_WRAP(TAG_STR_VERT_ATTRIBS)] = TAG_VERT_ATTRIBS,
	[DATA_TAG_WRAP(TAG_STR_IDX_ATTRIBS)] = TAG_IDX_ATTRIBS,
	[DATA_TAG_WRAP(TAG_STR_DEP)] = TAG_DEP,
	[DATA_TAG_WRAP(TAG_STR_DEP_TYPE_MAP)] = TAG_DEP_TYPE_MAP,
	[DATA_TAG_WRAP(TAG_STR_TYPE_OBJECT)] = TAG_TYPE_OBJECT,
	[DATA_TAG_WRAP(TAG_STR_TYPE_TARGET)] = TAG_TYPE_TARGET,
	[DATA_TAG_WRAP(TAG_STR_TYPE_USG)] = TAG_TYPE_USG,
	[DATA_TAG_WRAP(TAG_STR_TYPE_USG_FLAT_CUTOFF)] = TAG_TYPE_USG_FLAT_CUTOFF
};

static const U64 dataTagToKey[TAG_ENUM_COUNT] = {
	0,
	TAG_STR_HEADER,
	TAG_STR_DATA,
	TAG_STR_OBJECT,
	TAG_STR_ACTIVE_ATTRIBS,
	TAG_STR_IDX_REDIRECTS,
	TAG_STR_MAP_OVERRIDES,
	TAG_STR_XFORM,
	TAG_STR_OBJECT_TYPE,
	TAG_STR_MESH_HEADER,
	TAG_STR_MESH_ATTRIBS,
	TAG_STR_FACE_LIST,
	TAG_STR_FACE_ATTRIBS,
	TAG_STR_CORNER_AND_EDGE_LISTS,
	TAG_STR_CORNER_ATTRIBS,
	TAG_STR_EDGE_ATTRIBS,
	TAG_STR_VERT_ATTRIBS,
	TAG_STR_IDX_ATTRIBS,
	TAG_STR_DEP,
	TAG_STR_DEP_TYPE_MAP,
	TAG_STR_TYPE_OBJECT,
	TAG_STR_TYPE_TARGET,
	TAG_STR_TYPE_USG,
	TAG_STR_TYPE_USG_FLAT_CUTOFF
};

void stucIoDataTagValidate() {
	I8 flags[DATA_TAG_KEY_MAX] = {0};
	for (I32 i = 1; i < TAG_ENUM_COUNT; ++i) {
		PIX_ERR_ASSERT("no key assigned to data tag", dataTagToKey[i]);
		I32 idx = DATA_TAG_WRAP(dataTagToKey[i]);
		PIX_ERR_ASSERT("collision in data tag keys", !flags[idx]);
		flags[idx] = 1;
	}
}

PixErr checkZlibErr(I32 success, I32 zErr) {
	PixErr err = PIX_ERR_SUCCESS;
	if (zErr == success) {
		return err;
	}
	switch (zErr) {
		case Z_OK:
			PIX_ERR_RETURN(err, "zlib did not complete");
		case Z_MEM_ERROR:
			PIX_ERR_RETURN(err, "zlib err, Z_MEM_ERROR");
		case Z_BUF_ERROR:
			PIX_ERR_RETURN(err, "zlib err, Z_BUF_ERROR");
		case Z_DATA_ERROR:
			PIX_ERR_RETURN(err, "zlib err, Z_DATA_ERROR");
		case Z_STREAM_ERROR:
			PIX_ERR_RETURN(err, "zlib err, Z_STREAM_ERROR");
		default:
			PIX_ERR_RETURN(err, "zlib err");
	}
}

static
void *mallocZlibWrap(void *pOpaque, U32 count, U32 typeSize) {
	return ((StucAlloc *)pOpaque)->fpMalloc((I32)(count * typeSize));
}

static
void freeZlibWrap(void *pOpaque, void *pPtr) {
	((StucAlloc *)pOpaque)->fpFree(pPtr);
}

static
void reallocByteStringIfNeeded(
	const StucAlloc *pAlloc,
	ByteString *pByteString,
	I64 bitOffset
) {
	I64 bitCount = ((pByteString->byteIdx) * 8) + pByteString->nextBitIdx;
	PIX_ERR_ASSERT("", bitCount <= pByteString->size * 8);
	bitCount += bitOffset;
	I64 byteCount = bitCount / 8 + (bitCount % 8 != 0);
	if (byteCount && byteCount >= pByteString->size) {
		I64 oldSize = pByteString->size;
		pByteString->size = byteCount * 2;
		pByteString->pString = pAlloc->fpRealloc(pByteString->pString, pByteString->size);
		memset(pByteString->pString + oldSize, 0, pByteString->size - oldSize);
	}
}

static
I32 getByteLen(I32 bitLen) {
	I32 byteLen = bitLen / 8;
	byteLen += bitLen != byteLen * 8;
	return byteLen;
}

//TODO move these funcs to pixio lib and make U8 *pValue void * instead,
//having to cast every call is tedious
void stucEncodeValue(
	const StucAlloc *pAlloc,
	ByteString *pByteString,
	U8 *pValue,
	I32 bitLen
) {
	reallocByteStringIfNeeded(pAlloc, pByteString, bitLen);
	U8 *pStart = pByteString->pString + pByteString->byteIdx;

	I32 byteLen = getByteLen(bitLen);
	I32 strByteLen = getByteLen(bitLen + pByteString->nextBitIdx);
	pStart[0] |= pValue[0] << pByteString->nextBitIdx;
	for (I32 i = 1; i < strByteLen; ++i) {
		pStart[i] = i == byteLen ? 0x0 : pValue[i] << pByteString->nextBitIdx;
		U8 nextByte = pValue[i - 1];
		nextByte >>= 8 - pByteString->nextBitIdx;
		pStart[i] |= nextByte;
	}
	pByteString->nextBitIdx = pByteString->nextBitIdx + bitLen;
	pByteString->byteIdx += pByteString->nextBitIdx / 8;
	pByteString->nextBitIdx %= 8;
}

void stucEncodeString(const StucAlloc *pAlloc, ByteString *pByteString, const char *pString) {
	I32 lengthInBits = ((I32)strlen(pString) + 1) * 8;
	I32 lengthInBytes = lengthInBits / 8;
	//+8 for potential padding
	reallocByteStringIfNeeded(pAlloc, pByteString, lengthInBits + 8);
	if (pByteString->nextBitIdx != 0) {
		//pad to beginning of next byte
		pByteString->nextBitIdx = 0;
		pByteString->byteIdx++;
	}
	for (I32 i = 0; i < lengthInBytes; ++i) {
		pByteString->pString[pByteString->byteIdx] = pString[i];
		pByteString->byteIdx++;
	}
}

void stucDecodeValue(ByteString *pByteString, U8 *pValue, I32 bitLen) {
	U8 *pStart = pByteString->pString + pByteString->byteIdx;

	I32 strByteLen = getByteLen(bitLen + pByteString->nextBitIdx);
	for (I32 i = 0; i < strByteLen; ++i) {
		pValue[i] = pStart[i] >> pByteString->nextBitIdx;
		if (i != strByteLen - 1) {
			U8 nextByte = pStart[i + 1];
			nextByte <<= 8 - pByteString->nextBitIdx;
			pValue[i] |= nextByte;
		}
	}
	U8 mask = UCHAR_MAX >> (8 - abs(bitLen - strByteLen * 8)) % 8;
	pValue[strByteLen - 1] &= mask;
	pByteString->nextBitIdx = pByteString->nextBitIdx + bitLen;
	pByteString->byteIdx += pByteString->nextBitIdx / 8;
	pByteString->nextBitIdx %= 8;
}

void stucDecodeString(ByteString *pByteString, char *pString, I32 maxLen) {
	pByteString->byteIdx += pByteString->nextBitIdx > 0;
	U8 *dataPtr = pByteString->pString + pByteString->byteIdx;
	I32 i = 0;
	for (; i < maxLen && dataPtr[i]; ++i) {
		pString[i] = dataPtr[i];
	}
	pString[i] = 0;
	pByteString->byteIdx += i + 1;
	pByteString->nextBitIdx = 0;
}

static
void encodeAttribs(
	const StucAlloc *pAlloc,
	ByteString *pData,
	AttribArray *pAttribs,
	I32 dataLen
) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		if (pAttribs->pArr[i].core.type == STUC_ATTRIB_STRING) {
			for (I32 j = 0; j < dataLen; ++j) {
				void *pString = stucAttribAsVoid(&pAttribs->pArr[i].core, j);
				stucEncodeString(pAlloc, pData, pString);
			}
		}
		else {
			I32 attribSize = stucGetAttribSizeIntern(pAttribs->pArr[i].core.type) * 8;
			for (I32 j = 0; j < dataLen; ++j) {
				stucEncodeValue(pAlloc, pData, stucAttribAsVoid(&pAttribs->pArr[i].core, j), attribSize);
			}
		}
	}
}

//TODO now that StucAttrib and StucAttribIndexed share the same StucAttribCore
//struct, try and generalize these funcs for both if possible
static
void encodeIndexedAttribs(
	const StucAlloc *pAlloc,
	ByteString *pData,
	AttribIndexedArr *pAttribs
) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		AttribIndexed *pAttrib = pAttribs->pArr + i;
		if (pAttrib->core.type == STUC_ATTRIB_STRING) {
			for (I32 j = 0; j < pAttrib->count; ++j) {
				void *pString = stucAttribAsVoid(&pAttrib->core, j);
				stucEncodeString(pAlloc, pData, pString);
			}
		}
		else {
			I32 attribSize = stucGetAttribSizeIntern(pAttrib->core.type) * 8;
			for (I32 j = 0; j < pAttrib->count; ++j) {
				stucEncodeValue(pAlloc, pData, stucAttribAsVoid(&pAttrib->core, j), attribSize);
			}
		}
	}
}

static
void encodeAttribMeta(
	const StucAlloc *pAlloc,
	ByteString *pData,
	AttribArray *pAttribs
) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		stucEncodeValue(pAlloc, pData, (U8 *)&pAttribs->pArr[i].core.type, 8);
		stucEncodeValue(pAlloc, pData, (U8 *)&pAttribs->pArr[i].core.use, 8);
		stucEncodeValue(pAlloc, pData, (U8 *)&pAttribs->pArr[i].interpolate, 1);
		stucEncodeString(pAlloc, pData, pAttribs->pArr[i].core.name);
	}
}

static
void encodeIndexedAttribMeta(
	const StucAlloc *pAlloc,
	ByteString *pData,
	AttribIndexedArr *pAttribs
) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		stucEncodeValue(pAlloc, pData, (U8 *)&pAttribs->pArr[i].core.type, 8);
		stucEncodeValue(pAlloc, pData, (U8 *)&pAttribs->pArr[i].core.use, 8);
		stucEncodeValue(pAlloc, pData, (U8 *)&pAttribs->pArr[i].count, 32);
		stucEncodeString(pAlloc, pData, pAttribs->pArr[i].core.name);
	}
}

static
U64 dataTagKeyFromStr(const char *pStr) {
	return (U64)DATA_TAG_KEY(pStr[0], pStr[1]);
}

static
DataTag dataTagFromStr(const char *pStr) {
	return dataTagKeyToTag[DATA_TAG_WRAP(dataTagKeyFromStr(pStr))];
}

static
DataTag decodeDataTag(ByteString *pByteString, char *pTagOut) {
	//ensure string is aligned with byte (we need to do this manually,
	//as stucDecodeValue is being used instead of stucDecodeString, given there's
	//only 2 characters)
	pByteString->byteIdx += pByteString->nextBitIdx > 0;
	pByteString->nextBitIdx = 0;
	char tag[2] = {0};
	stucDecodeValue(pByteString, (U8 *)tag, 16);
	if (pTagOut) {
		pTagOut[0] = tag[0];
		pTagOut[1] = tag[1];
	}
	return dataTagFromStr(tag);
}

static
void strFromDataTag(DataTag tag, char *pStr) {
	memcpy(pStr, &DATA_TAG_KEY_TO_STR(dataTagToKey[tag]), 2); 
}

static
StucErr isDataTagInvalid(ByteString *pByteString, DataTag tag) {
	StucErr err = PIX_ERR_SUCCESS;
	char str[2] = {0};
	PIX_ERR_THROW_IFNOT_COND(
		err,
		decodeDataTag(pByteString, str) == tag,
		"tag doesn't match",
		0
	);
	char refStr[2] = {0};
	strFromDataTag(tag, refStr);
	PIX_ERR_CATCH(0, err, ;);
	return err;
}

static
void encodeDataTag(const StucAlloc *pAlloc, ByteString *pByteString, DataTag tag) {
	//not using stucEncodeString, as there's not need for a null terminator.
	//Only using 2 characters
	
	//ensure string is aligned with byte (we need to do this manually,
	//as stucEncodeValue is being used instead of stucEncodeString)
	if (pByteString->nextBitIdx != 0) {
		pByteString->nextBitIdx = 0;
		pByteString->byteIdx++;
	}
	char str[2] = {0};
	strFromDataTag(tag, str);
	stucEncodeValue(pAlloc, pByteString, (U8 *)str, 16);
}

static
PixErr encodeActiveAttribs(
	const StucAlloc *pAlloc,
	ByteString *pData,
	StucMesh *pMesh
) {
	PixErr err = PIX_ERR_SUCCESS;
	
	encodeDataTag(pAlloc, pData, TAG_ACTIVE_ATTRIBS);
	I32 count = 0;
	for (I32 i = 0; i < STUC_ATTRIB_USE_ENUM_COUNT; ++i) {
		count += pMesh->activeAttribs[i].active;
	}
	stucEncodeValue(pAlloc, pData, (U8 *)&count, 8);
	for (I32 i = 0; i < STUC_ATTRIB_USE_ENUM_COUNT; ++i) {
		if (!pMesh->activeAttribs[i].active) {
			continue;
		}
		stucEncodeValue(pAlloc, pData, (U8 *)&i, 8);
		stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->activeAttribs[i].domain, 4);
		stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->activeAttribs[i].idx, 16);
	}
	return err;
}

static
void destroyIdxTableArr(StucAlloc *pAlloc, StucIdxTableArr *pArr) {
	if (pArr->pArr) {
		for (I32 i = 0; i < pArr->count; ++i) {
			if (pArr->pArr[i].table.pArr) {
				pAlloc->fpFree(pArr->pArr[i].table.pArr);
			}
		}
		pAlloc->fpFree(pArr->pArr);
	}
	*pArr = (StucIdxTableArr){0};
}

static
void destroyIdxTableArrs(StucAlloc *pAlloc, StucIdxTableArr **ppArr, I32 count) {
	if (*ppArr) {
		for (I32 i = 0; i < count; ++i) {
			destroyIdxTableArr(pAlloc, (*ppArr) + i);
		}
		pAlloc->fpFree(*ppArr);
		*ppArr = NULL;
	}
}

static
void encodeRedirectTable(
	const StucAlloc *pAlloc,
	ByteString *pData,
	const StucIdxTableArr *pIdxTable
) {
	encodeDataTag(pAlloc, pData, TAG_IDX_REDIRECTS);
	stucEncodeValue(pAlloc, pData, (U8 *)&pIdxTable->count, 16);
	for (I32 i = 0; i < pIdxTable->count; ++i) {
		StucIdxTable *pTable = pIdxTable->pArr + i;
		stucEncodeValue(pAlloc, pData, (U8 *)&pTable->idx, 16);
		stucEncodeValue(pAlloc, pData, (U8 *)&pTable->table.count, 8);
		PIX_ERR_ASSERT("", !pData->nextBitIdx);
		I32 countMem = pData->byteIdx;
		stucEncodeValue(pAlloc, pData, &(U8){0}, 8);
		I32 count = 0;
		for (I32 j = 0; j < pTable->table.count; ++j) {
			if (pTable->table.pArr[j] >= 0) {
				++count;
				stucEncodeValue(pAlloc, pData, (U8 *)&j, 8);//local
				stucEncodeValue(pAlloc, pData, (U8 *)&pTable->table.pArr[j], 8);//global
			}
		}
		PIX_ERR_ASSERT("", count <= UINT8_MAX);
		pData->pString[countMem] = (U8)count;
	}
}

typedef struct MappingOpt {
	F32 wScale;
	F32 receiveLen;
} MappingOpt;

typedef struct MatMapEntry {
	PixuctHTableEntryCore core;
	I32 linIdx;
	StucMap pMap;
	MappingOpt opt;
} MatMapEntry;

typedef struct MatMapEntryInit {
	StucMap pMap;
	MappingOpt opt;
} MatMapEntryInit;

static
void matMapEntryInit(
	void *pUserData,
	PixuctHTableEntryCore *pCore,
	const void *pKeyData,
	void *pInitInfo,
	I32 linIdx
) {
	MatMapEntryInit *pInit = (MatMapEntryInit *)pInitInfo;
	MatMapEntry *pEntry = (MatMapEntry *)pCore;
	pEntry->linIdx = linIdx;
	pEntry->pMap = pInit->pMap;
	pEntry->opt = pInit->opt;
}

static
bool matMapEntryCmp(
	const PixuctHTableEntryCore *pEntry,
	const void *pKeyData,
	const void *pInitInfo
) {
	return !strncmp(((MatMapEntry *)pEntry)->pMap->pName, pKeyData, pixioPathMaxGet());
}

static
void encodeBlendConfigOverride(
	const StucAlloc *pAlloc,
	ByteString *pCommonBuf,
	const StucTypeDefault *pDefault,
	const StucBlendConfig *pBlendConfig
) {
	UBitField8 flags =
		(pBlendConfig->blend != pDefault->blendConfig.blend) |
		(pBlendConfig->fMax != pDefault->blendConfig.fMax) << 1 |
		(pBlendConfig->fMin != pDefault->blendConfig.fMin) << 2 |
		(pBlendConfig->iMax != pDefault->blendConfig.iMax) << 3 |
		(pBlendConfig->iMin != pDefault->blendConfig.iMin) << 4 |
		(pBlendConfig->opacity != pDefault->blendConfig.opacity) << 5 |
		(pBlendConfig->clamp != pDefault->blendConfig.clamp) << 6 |
		(pBlendConfig->order != pDefault->blendConfig.order) << 7;
	stucEncodeValue(pAlloc, pCommonBuf, (U8 *)&flags, 8);
	if (flags & 0x1) {
		stucEncodeValue(pAlloc, pCommonBuf, (U8 *)&pBlendConfig->blend, 8);
	}
	if (flags >> 1 & 0x1) {
		stucEncodeValue(pAlloc, pCommonBuf, (U8 *)&pBlendConfig->fMax, 64);
	}
	if (flags >> 2 & 0x1) {
		stucEncodeValue(pAlloc, pCommonBuf, (U8 *)&pBlendConfig->fMin, 64);
	}
	if (flags >> 3 & 0x1) {
		stucEncodeValue(pAlloc, pCommonBuf, (U8 *)&pBlendConfig->iMax, 64);
	}
	if (flags >> 4 & 0x1) {
		stucEncodeValue(pAlloc, pCommonBuf, (U8 *)&pBlendConfig->iMin, 64);
	}
	if (flags >> 5 & 0x1) {
		stucEncodeValue(pAlloc, pCommonBuf, (U8 *)&pBlendConfig->opacity, 32);
	}
	if (flags >> 6 & 0x1) {
		stucEncodeValue(pAlloc, pCommonBuf, (U8 *)&pBlendConfig->clamp, 1);
	}
	if (flags >> 7 & 0x1) {
		stucEncodeValue(pAlloc, pCommonBuf, (U8 *)&pBlendConfig->order, 1);
	}
}

static
void encodeBlendOpts(
	StucMapExport *pHandle,
	const StucMesh *pMesh,
	StucMapArrEntry *pMapArrEntry,
	const MatMapEntry *pMatMapEntry,
	ByteString *pBlendOptBuf,
	bool blendOptOverride
) {
	const StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	for (StucDomain domain = STUC_DOMAIN_FACE; domain <= STUC_DOMAIN_VERT; ++domain) {
		StucBlendOptArr *pArr = pMapArrEntry->blendOptArr + domain; 
		const AttribArray *pAttribArr = stucGetAttribArrFromDomainConst(pMesh, domain);

		I32 countBytePos = pBlendOptBuf->byteIdx;
		stucEncodeValue(pAlloc, pBlendOptBuf, (U8[]){0, 0}, 16);
		I32 count = 0;
		for (I32 j = 0; j < pArr->count; ++j) {
			const Attrib *pAttrib = pAttribArr->pArr + pArr->pArr[j].attrib;
			const StucTypeDefault *pDefault = stucGetTypeDefaultConfig(
				&pHandle->pCtx->typeDefaults,
				pAttrib->core.type
			);
			BlendConfig *pBlendConfig = &pArr->pArr[j].blendConfig;
			if (memcmp(
				pBlendConfig,
				&pDefault->blendConfig,
				sizeof(BlendConfig))
			) {
				stucEncodeValue(pAlloc, pBlendOptBuf, (U8 *)&pArr->pArr[j].attrib, 16);
				stucEncodeValue(pAlloc, pBlendOptBuf, (U8 *)&pAttrib->core.type, 8);
				encodeBlendConfigOverride(pAlloc, pBlendOptBuf, pDefault, pBlendConfig);
				++count;
			}
		}
		if (count) {
			blendOptOverride = true;
		}
		*(I16 *)&pBlendOptBuf->pString[countBytePos] = count;
	}
}

static
void optsFinalEncode(
	StucMapExport *pHandle,
	MatMapEntry *pMatMapEntry,
	I32 matIdx,
	ByteString *pBlendOpt,
	bool blendOptOverride,
	bool wScaleOverride,
	bool receiveOverride
) {
	const StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	ByteString *pData = &pHandle->data;
	UBitField8 header = !!blendOptOverride | wScaleOverride << 1 | receiveOverride << 2;
	stucEncodeValue(pAlloc, pData, (U8 *)&matIdx, 16);
	stucEncodeValue(pAlloc, pData, (U8 *)&header, 8);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMatMapEntry->linIdx, 16);
	if (blendOptOverride) {
		reallocByteStringIfNeeded(pAlloc, pData, (I64)pBlendOpt->byteIdx * 8);
		memcpy(pData->pString, pBlendOpt->pString, pBlendOpt->byteIdx);
		pData->byteIdx += pBlendOpt->byteIdx;
	}
	if (pBlendOpt->pString) {
		pAlloc->fpFree(pBlendOpt->pString);
	}
	if (wScaleOverride) {
		stucEncodeValue(pAlloc, pData, (U8 *)&pMatMapEntry->opt.wScale, 32);
	}
	if (receiveOverride) {
		stucEncodeValue(pAlloc, pData, (U8 *)&pMatMapEntry->opt.receiveLen, 32);
	}
}

static
StucErr encodeMappingOpt(
	StucMapExport *pHandle,
	const StucMesh *pMesh,
	const StucMapArr *pMapArr,
	const AttribIndexedArr *pIdxAttribArr,
	const StucIdxTableArr *pIdxTable,
	F32 wScale,
	F32 receiveLen
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	ByteString *pData = &pHandle->data;

	StucMesh meshCpy = *pMesh;
	err = stucAttemptToSetMissingActiveDomains(&meshCpy);
	PIX_ERR_RETURN_IFNOT(err, "");
	const Attrib *pAttrib =
		stucGetActiveAttribConst(pHandle->pCtx, &meshCpy, STUC_ATTRIB_USE_IDX);
	encodeDataTag(pAlloc, pData, TAG_MAP_OVERRIDES);
	I32 countDataPos = pData->byteIdx;
	stucEncodeValue(pAlloc, pData, (U8[]){0, 0}, 16);
	I32 count = 0;
	const AttribIndexed *pMats =
		stucGetAttribIndexedInternConst(pIdxAttribArr, pAttrib->core.name);
	intptr_t attribIdx = (intptr_t)pMats - (intptr_t)pIdxAttribArr->pArr;
	for (I32 i = 0; i < pMapArr->count; ++i) {
		I32 globMatIdx = pIdxTable->pArr[attribIdx].table.pArr[i];
		if (globMatIdx == -2) {
			continue;//mat is not use in mesh
		}
		++count;
		MappingOpt mappingOpt = {
			.wScale = wScale,
			.receiveLen = receiveLen
		};
		MatMapEntry *pEntry = NULL;
		pixuctHTableGet(
			&pHandle->mapTable,
			0,
			pMapArr->pArr[i].map.ptr->pName,
			(void **)&pEntry,
			true,
			&(MatMapEntryInit) {.pMap = pMapArr->pArr[i].map.ptr, .opt = mappingOpt},
			stucKeyFromPath, NULL, matMapEntryInit, matMapEntryCmp
		);
		bool wScaleOverride = pEntry->opt.wScale != wScale;
		bool receiveOverride = pEntry->opt.receiveLen != receiveLen;
		bool blendOptOverride = false;
		ByteString blendOptBuf = {0};
		encodeBlendOpts(
			pHandle,
			&meshCpy,
			pMapArr->pArr + i,
			pEntry,
			&blendOptBuf,
			&blendOptOverride
		);
		optsFinalEncode(
			pHandle,
			pEntry,
			pMapArr->pArr[i].matIdx,
			&blendOptBuf,
			blendOptOverride, wScaleOverride, receiveOverride
		);
	}
	*(I16 *)&pData->pString[countDataPos] = count;
	return err;
}

static
StucErr encodeObj(
	StucMapExport *pHandle,
	const StucObject *pObj,
	const StucIdxTableArr *pIdxTable,
	bool mappingOpt,
	const StucMapArr *pMapArr,
	const AttribIndexedArr *pIdxAttribArr,
	F32 wScale,
	F32 receiveLen,
	bool checkPosOnly
) {
	StucErr err = PIX_ERR_SUCCESS;
	const StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	ByteString *pData = &pHandle->data;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pObj->pData && pObj->pData->type == STUC_OBJECT_DATA_MESH,
		"invalid mesh"
	);
	StucMesh *pMesh = (StucMesh *)pObj->pData;
	err = stucValidateMesh(&pHandle->pCtx->alloc, pMesh, false, checkPosOnly);
	PIX_ERR_RETURN_IFNOT(err, "mesh validation failed");
	//encode obj header
	encodeDataTag(pAlloc, pData, TAG_OBJECT);
	if (pIdxTable && pIdxTable->count) {
		encodeRedirectTable(pAlloc, pData, pIdxTable);
	}
	err = encodeActiveAttribs(pAlloc, pData, pMesh);
	PIX_ERR_RETURN_IFNOT(err, "");
	encodeDataTag(pAlloc, pData, TAG_XFORM);
	for (I32 i = 0; i < 16; ++i) {
		I32 x = i % 4;
		I32 y = i / 4;
		stucEncodeValue(pAlloc, pData, (U8 *)&pObj->transform.d[y][x], 32);
	}
	encodeDataTag(pAlloc, pData, TAG_OBJECT_TYPE);
	stucEncodeValue(pAlloc, pData, (U8 *)&pObj->pData->type, 8);
	if (!stucCheckIfMesh(*pObj->pData)) {
		return err;
	}
	encodeDataTag(pAlloc, pData, TAG_MESH_HEADER);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->meshAttribs.count, 32);
	encodeAttribMeta(pAlloc, pData, &pMesh->meshAttribs);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->faceAttribs.count, 32);
	encodeAttribMeta(pAlloc, pData, &pMesh->faceAttribs);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->cornerAttribs.count, 32);
	encodeAttribMeta(pAlloc, pData, &pMesh->cornerAttribs);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->edgeAttribs.count, 32);
	encodeAttribMeta(pAlloc, pData, &pMesh->edgeAttribs);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->vertAttribs.count, 32);
	encodeAttribMeta(pAlloc, pData, &pMesh->vertAttribs);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->faceCount, 32);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->cornerCount, 32);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->edgeCount, 32);
	stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->vertCount, 32);
	//encode data
	encodeDataTag(pAlloc, pData, TAG_MESH_ATTRIBS);
	encodeAttribs(pAlloc, pData, &pMesh->meshAttribs, 1);
	encodeDataTag(pAlloc, pData, TAG_FACE_LIST);
	for (I32 i = 0; i < pMesh->faceCount; ++i) {
		PIX_ERR_ASSERT("",
			pMesh->pFaces[i] >= 0 &&
			pMesh->pFaces[i] < pMesh->cornerCount
		);
		stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->pFaces[i], 32);
	}
	encodeDataTag(pAlloc, pData, TAG_FACE_ATTRIBS);
	encodeAttribs(pAlloc, pData, &pMesh->faceAttribs, pMesh->faceCount);
	encodeDataTag(pAlloc, pData, TAG_CORNER_AND_EDGE_LISTS);
	for (I32 i = 0; i < pMesh->cornerCount; ++i) {
		PIX_ERR_ASSERT("",
			pMesh->pCorners[i] >= 0 &&
			pMesh->pCorners[i] < pMesh->vertCount
		);
		stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->pCorners[i], 32);
		PIX_ERR_ASSERT("",
			pMesh->pEdges[i] >= 0 &&
			pMesh->pEdges[i] < pMesh->edgeCount
		);
		stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->pEdges[i], 32);
	}
	encodeDataTag(pAlloc, pData, TAG_CORNER_ATTRIBS);
	encodeAttribs(pAlloc, pData, &pMesh->cornerAttribs, pMesh->cornerCount);
	encodeDataTag(pAlloc, pData, TAG_EDGE_ATTRIBS);
	encodeAttribs(pAlloc, pData, &pMesh->edgeAttribs, pMesh->edgeCount);
	encodeDataTag(pAlloc, pData, TAG_VERT_ATTRIBS);
	encodeAttribs(pAlloc, pData, &pMesh->vertAttribs, pMesh->vertCount);
	return err;
}

static
void destroyMapExport(StucMapExport *pHandle) {
	StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	pAlloc->fpFree(pHandle->pPath);
	pAlloc->fpFree(pHandle->data.pString);
	pixuctHTableDestroy(&pHandle->mapTable);
	stucAttribIndexedArrDestroy(pHandle->pCtx, &pHandle->idxAttribs);
	*pHandle = (StucMapExport){0};
}

StucErr stucMapExportInit(
	StucContext pCtx,
	StucMapExport **ppHandle,
	const char *pPath,
	bool compress
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucAlloc *pAlloc = &pCtx->alloc;
	PIX_ERR_RETURN_IFNOT_COND(err, pPath[0], "path is empty");
	I32 pathLen = strnlen(pPath, pixioPathMaxGet());
	PIX_ERR_RETURN_IFNOT_COND(err, pathLen != pixioPathMaxGet(), "path is too long");
	++pathLen;
	StucMapExport *pHandle = pAlloc->fpCalloc(1, sizeof(StucMapExport));
	pHandle->pCtx = pCtx;
	pHandle->pPath = pAlloc->fpCalloc(pathLen, 1);
	memcpy(pHandle->pPath, pPath, pathLen);
	ByteString *pData = &pHandle->data;
	pData->size = 1024;
	pData->pString = pAlloc->fpCalloc(pData->size, 1);
	pHandle->cutoffIdxMax = -1;
	pixuctHTableInit(
		pAlloc,
		&pHandle->mapTable,
		16,
		(I32Arr){.pArr = (I32[1]){sizeof(MatMapEntry)}, .count = 1},
		NULL,
		pHandle,
		true
	);
	pHandle->compress = compress;
	*ppHandle = pHandle;
	return err;
}

StucErr stucMapExportEnd(StucMapExport **ppHandle) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		ppHandle && *ppHandle,
		"invalid handle"
	);
	PIX_ERR_ASSERT(
		"invalid handle",
		(*ppHandle)->pPath && (*ppHandle)->data.pString
	);
	StucMapExport *pHandle = *ppHandle;
	StucAlloc *pAlloc = &pHandle->pCtx->alloc;

	ByteString header = {0};
	U8 *pCompressed = NULL;
	void *pFile = NULL;

	PIX_ERR_THROW_IFNOT_COND(
		err,
		pHandle->header.objCount > 0,
		"no objects were supplied",
		0
	);
	PIX_ERR_THROW_IFNOT_COND(
		err,
		pHandle->cutoffIdxMax <= pHandle->header.cutoffCount,
		"one or more USG's reference an invalid flat-cutoff index",
		0
	);
	PIX_ERR_WARN_IFNOT_COND(
		pHandle->header.cutoffCount && pHandle->cutoffIdxMax >= 0 ||
		!pHandle->header.cutoffCount,
		"no supplied flat-cutoffs are referenced by a USG"
	);

	encodeDataTag(&pHandle->pCtx->alloc, &pHandle->data, TAG_IDX_ATTRIBS);
	encodeIndexedAttribMeta(pAlloc, &pHandle->data, &pHandle->idxAttribs);
	encodeIndexedAttribs(pAlloc, &pHandle->data, &pHandle->idxAttribs);

	//compress data
	I64 dataSize = pHandle->data.byteIdx + (pHandle->data.nextBitIdx > 0);
	z_stream zStream = {
		.zalloc = mallocZlibWrap,
		.zfree = freeZlibWrap,
		.opaque = (void *)pAlloc 
	};
	//using gzip with crc32
	err = checkZlibErr(
		Z_OK,
		deflateInit2(
			&zStream,
			pHandle->compress ? Z_DEFAULT_COMPRESSION : Z_NO_COMPRESSION,
			Z_DEFLATED,
			STUC_WINDOW_BITS,
			8,
			Z_DEFAULT_STRATEGY
		)
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	zStream.avail_out = deflateBound(&zStream, dataSize);
	pCompressed = pAlloc->fpMalloc(zStream.avail_out);
	zStream.next_out = pCompressed;
	zStream.avail_in = dataSize;
	zStream.next_in = pHandle->data.pString;
	err = checkZlibErr(Z_STREAM_END, deflate(&zStream, Z_FINISH));
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = checkZlibErr(Z_OK, deflateEnd(&zStream));
	PIX_ERR_THROW_IFNOT(err, "", 0);

	//encode header
	const char *format = MAP_FORMAT_NAME;
	header.size = 64;
	header.pString = pAlloc->fpCalloc(header.size, 1);
	stucEncodeString(pAlloc, &header, format);
	I32 version = STUC_MAP_VERSION;
	stucEncodeValue(pAlloc, &header, (U8 *)&version, 16);
	stucEncodeValue(pAlloc, &header, (U8 *)&zStream.total_out, 64);
	stucEncodeValue(pAlloc, &header, (U8 *)&dataSize, 64);
	stucEncodeValue(pAlloc, &header, (U8 *)&pHandle->idxAttribs.count, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&pHandle->header.objCount, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&pHandle->header.usgCount, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&pHandle->header.cutoffCount, 32);

	encodeDataTag(pAlloc, &header, TAG_DEP);
	PixalcLinAlloc *pTableAlloc = pixuctHTableAllocGet(&pHandle->mapTable, 0);
	stucEncodeValue(pAlloc, &header, (U8 *)&pTableAlloc->linIdx, 32);
	PixalcLinAllocIter iter = {0};
	pixalcLinAllocIterInit(pTableAlloc, (PixtyRange){.start=0, .end=INT32_MAX}, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		encodeDataTag(pAlloc, &header, TAG_DEP_TYPE_MAP);
		MatMapEntry *pEntry = pixalcLinAllocGetItem(&iter);
		stucEncodeString(pAlloc, &header, pEntry->pMap->pName);
	}

	header.size = header.byteIdx + !!header.nextBitIdx;

	err = pHandle->pCtx->io.fpOpen(&pFile, pHandle->pPath, 0, pAlloc);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pHandle->pCtx->io.fpWrite(pFile, (U8 *)&header.size, 4);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pHandle->pCtx->io.fpWrite(pFile, header.pString, header.size);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pHandle->pCtx->io.fpWrite(pFile, pCompressed, (I32)zStream.total_out);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	PIX_ERR_CATCH(0, err, ;);
	if (pFile) {
		err = pHandle->pCtx->io.fpClose(pFile);
	}
	if (header.pString) {
		pAlloc->fpFree(header.pString);
	}
	if (pCompressed) {
		pAlloc->fpFree(pCompressed);
	}
	destroyMapExport(pHandle);
	printf("Finished STUC export\n");
	return err;
}

static
StucErr markUsedIndices(
	StucMapExport *pHandle,
	I8 *pIdxIsUsed,
	const StucMesh *pMesh,
	const AttribIndexed *pRef
) {
	StucErr err = PIX_ERR_SUCCESS;
	memset(pIdxIsUsed, 0, INT8_MAX);
	const Attrib *pCompRef = NULL;
	I32 compCount = 0;
	for (StucDomain domain = STUC_DOMAIN_FACE; domain <= STUC_DOMAIN_VERT; ++domain) {
		pCompRef = stucGetAttribInternConst(
			pRef->core.name,
			stucGetAttribArrFromDomainConst(pMesh, domain),
			false,
			pHandle->pCtx,
			pMesh,
			NULL
		);
		if (pCompRef) {
			compCount = stucDomainCountGetIntern(pMesh, domain);
			break;
		}
	}
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pCompRef->core.use == STUC_ATTRIB_USE_IDX &&
			pCompRef->core.type == STUC_ATTRIB_I8,
		"indexed attrib must be indexed with an attrib \
of use STUC_ATTRIB_USE_IDX & type STUC_ATTRIB_I8"
		);
	for (I32 j = 0; j < compCount; ++j) {
		I8 idx = *(const I8 *)stucAttribAsVoidConst(&pCompRef->core, j);
		PIX_ERR_RETURN_IFNOT_COND(err, idx >= 0, "negative STUC_ATTRIB_USE_IDX idx");
		pIdxIsUsed[idx] = true;
	}
	return err;
}

static
AttribIndexed *getGlobIdxAttrib(StucMapExport *pHandle, const AttribIndexed *pRef) {
	AttribIndexed *pAttrib =
		stucGetAttribIndexedInternConst(&pHandle->idxAttribs, pRef->core.name);
	if (!pAttrib) {
		pAttrib = stucAppendIndexedAttrib(
			pHandle->pCtx,
			&pHandle->idxAttribs,
			pRef->core.name,
			pRef->count,
			pRef->core.type,
			pRef->core.use
		);
	}
	return pAttrib;
}

static
StucErr getGlobIdx(
	const StucAlloc *pAlloc,
	AttribIndexed *pAttrib,
	const AttribIndexed *pRef, I32 refIdx,
	I32 *pIdx
) {
	StucErr err = PIX_ERR_SUCCESS;
	*pIdx = stucGetIdxInIndexedAttrib(pAttrib, pRef, refIdx);
	if (*pIdx == -1) {
		PIX_ERR_RETURN_IFNOT_COND(err, pAttrib->count <= pAttrib->size, "");
		if (pAttrib->count == pAttrib->size) {
			pAttrib->size *= 2;
			stucReallocAttrib(pAlloc, NULL, &pAttrib->core, pAttrib->size);
		}
		*pIdx = pAttrib->count;
		++pAttrib->count;
		memcpy(
			stucAttribAsVoid(&pAttrib->core, *pIdx),
			stucAttribAsVoidConst(&pRef->core, refIdx),
			stucGetAttribSizeIntern(pRef->core.type)
		);
	}
	return err;
}

static
StucErr setRedirects(
	StucMapExport *pHandle,
	StucIdxTable *pIdxTable,
	const I8 *pIdxIsUsed,
	const AttribIndexed *pRef,
	bool *pHasRedirect
) {
	StucErr err = PIX_ERR_SUCCESS;
	AttribIndexed *pAttrib = getGlobIdxAttrib(pHandle, pRef);
	pIdxTable->idx = 
		((intptr_t)pAttrib - (intptr_t)pHandle->idxAttribs.pArr) / sizeof(AttribIndexed);
	PIX_ERR_ASSERT("", pIdxTable->idx >= 0 && pIdxTable->idx < pHandle->idxAttribs.count);
	pIdxTable->table.pArr = pHandle->pCtx->alloc.fpCalloc(pRef->count, 1);
	for (I32 i = 0; i < pRef->count; ++i) {
		if (!pIdxIsUsed[i]) {
			pIdxTable->table.pArr[i] = -2;
			continue;
		}
		I32 idx = 0;
		err = getGlobIdx(&pHandle->pCtx->alloc, pAttrib, pRef, i, &idx);
		PIX_ERR_RETURN_IFNOT(err, "");
		if (i == idx) {
			pIdxTable->table.pArr[i] = -1;
		}
		else {
			pIdxTable->table.pArr[i] = idx;
			*pHasRedirect = true;
		}
		++pIdxTable->table.count;
	}
	return err;
}

static
StucErr makeIdxAttribRedirects(
	StucMapExport *pHandle,
	const StucObject *pObj,
	const StucAttribIndexedArr *pIndexedAttribs,
	StucIdxTableArr *pIdxTable
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pObj->pData && pObj->pData->type == STUC_OBJECT_DATA_MESH,
		"invalid mesh"
	);
	const StucMesh *pMesh = (StucMesh *)pObj->pData;

	StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	I8 *pIdxIsUsed = pAlloc->fpMalloc(INT8_MAX);
	pIdxTable->size = pIdxTable->count = pIndexedAttribs->count;
	pIdxTable->pArr = pAlloc->fpCalloc(pIdxTable->size, sizeof(StucIdxTable));
	for (I32 i = 0; i < pIndexedAttribs->count; ++i) {
		const AttribIndexed *pRef = pIndexedAttribs->pArr + i;
		err = markUsedIndices(pHandle, pIdxIsUsed, pMesh, pRef);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		err = setRedirects(
			pHandle,
			pIdxTable->pArr + i,
			pIdxIsUsed,
			pRef,
			&pIdxTable->hasRedirect
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	PIX_ERR_CATCH(0, err, destroyIdxTableArr(pAlloc, pIdxTable););
	if (pIdxIsUsed) {
		pAlloc->fpFree(pIdxIsUsed);
	}
	return err;
}

static
StucErr mapExportObjAdd(
	StucMapExport *pHandle,
	const StucObject *pObj,
	const StucAttribIndexedArr *pIndexedAttribs,
	bool isTarget,
	const StucMapArr *pMapArr,
	F32 wScale,
	F32 receiveLen
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucIdxTableArr idxTable = {0};
	err = makeIdxAttribRedirects(pHandle, pObj, pIndexedAttribs, &idxTable);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	if (isTarget) {
		err = encodeMappingOpt(
			pHandle,
			(StucMesh *)pObj->pData,
			pMapArr,
			pIndexedAttribs,
			&idxTable,
			wScale,
			receiveLen
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	if (!idxTable.hasRedirect) {
		destroyIdxTableArr(&pHandle->pCtx->alloc, &idxTable);
	}
	err = encodeObj(
		pHandle,
		pObj,
		&idxTable,
		isTarget,
		pMapArr,
		pIndexedAttribs,
		wScale,
		receiveLen,
		false
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	++pHandle->header.objCount;
	PIX_ERR_CATCH(0, err, destroyMapExport(pHandle););
	destroyIdxTableArr(&pHandle->pCtx->alloc, &idxTable);
	return err;
}

StucErr stucMapExportTargetAdd(
	StucMapExport *pHandle,
	const StucMapArr *pMapArr,
	const StucObject *pObj,
	const StucAttribIndexedArr *pIndexedAttribs,
	F32 wScale,
	F32 receiveLen
) {
	encodeDataTag(&pHandle->pCtx->alloc, &pHandle->data, TAG_TYPE_TARGET);
	return
		mapExportObjAdd(pHandle, pObj, pIndexedAttribs, true, pMapArr, wScale, receiveLen);
}

StucErr stucMapExportObjAdd(
	StucMapExport *pHandle,
	const StucObject *pObj,
	const StucAttribIndexedArr *pIndexedAttribs
) {
	encodeDataTag(&pHandle->pCtx->alloc, &pHandle->data, TAG_TYPE_OBJECT);
	return mapExportObjAdd(pHandle, pObj, pIndexedAttribs, false, NULL, .0f, .0f);
}

StucErr stucMapExportUsgAdd(
	StucMapExport *pHandle,
	StucUsg *pUsg
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	encodeDataTag(pAlloc, &pHandle->data, TAG_TYPE_USG);
	err = encodeObj(pHandle, &pUsg->obj, NULL, false, NULL, NULL, .0f, .0f, true);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	stucEncodeValue(pAlloc, &pHandle->data, (U8 *)&pUsg->flatCutoff.enabled, 1);
	if (pUsg->flatCutoff.enabled) {
		PIX_ERR_THROW_IFNOT_COND(
			err,
			pUsg->flatCutoff.idx >= 0,
			"invalid flat-cutoff index",
			0
		);
		if (pUsg->flatCutoff.idx > pHandle->cutoffIdxMax) {
			pHandle->cutoffIdxMax = pUsg->flatCutoff.idx;
		}
		stucEncodeValue(pAlloc, &pHandle->data, (U8 *)&pUsg->flatCutoff.idx, 32);
	}
	++pHandle->header.usgCount;
	PIX_ERR_CATCH(0, err, destroyMapExport(pHandle););
	return err;
}

StucErr stucMapExportUsgCutoffAdd(StucMapExport *pHandle, StucObject *pFlatCutoff) {
	StucErr err = PIX_ERR_SUCCESS;
	StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	encodeDataTag(pAlloc, &pHandle->data, TAG_TYPE_USG_FLAT_CUTOFF);
	err = encodeObj(pHandle, pFlatCutoff, NULL, false, NULL, NULL, .0f, .0f, true);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	++pHandle->header.cutoffCount;
	PIX_ERR_CATCH(0, err, destroyMapExport(pHandle););
	return err;
}

static
StucErr decodeAttribMeta(ByteString *pData, AttribArray *pAttribs) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		stucDecodeValue(pData, (U8 *)&pAttribs->pArr[i].core.type, 8);
		stucDecodeValue(pData, (U8 *)&pAttribs->pArr[i].core.use, 8);
		stucDecodeValue(pData, (U8 *)&pAttribs->pArr[i].interpolate, 1);
		I32 maxNameLen = sizeof(pAttribs->pArr[i].core.name);
		stucDecodeString(pData, (char *)pAttribs->pArr[i].core.name, maxNameLen);
		for (I32 j = 0; j < i; ++j) {
			if (!strncmp(pAttribs->pArr[i].core.name, pAttribs->pArr[j].core.name,
			    STUC_ATTRIB_NAME_MAX_LEN)) {

				//dup
				return PIX_ERR_ERROR;
			}
		}
	}
	return PIX_ERR_SUCCESS;
}

static
StucErr decodeIndexedAttribMeta(ByteString *pData, AttribIndexedArr *pAttribs) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		stucDecodeValue(pData, (U8 *)&pAttribs->pArr[i].core.type, 16);
		stucDecodeValue(pData, (U8 *)&pAttribs->pArr[i].count, 32);
		I32 maxNameLen = sizeof(pAttribs->pArr[i].core.name);
		stucDecodeString(pData, (char *)pAttribs->pArr[i].core.name, maxNameLen);
		for (I32 j = 0; j < i; ++j) {
			if (!strncmp(pAttribs->pArr[i].core.name, pAttribs->pArr[j].core.name,
				STUC_ATTRIB_NAME_MAX_LEN)) {

				//dup
				return PIX_ERR_ERROR;
			}
		}
	}
	return PIX_ERR_SUCCESS;
}

static
void decodeAttribs(
	StucContext pCtx,
	ByteString *pData,
	AttribArray *pAttribs,
	I32 dataLen
) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		Attrib* pAttrib = pAttribs->pArr + i;
		I32 attribSize = stucGetAttribSizeIntern(pAttrib->core.type);
		pAttrib->core.pData = dataLen ?
			pCtx->alloc.fpCalloc(dataLen, attribSize) : NULL;
		attribSize *= 8;
		for (I32 j = 0; j < dataLen; ++j) {
			void *pAttribData = stucAttribAsVoid(&pAttrib->core, j);
			if (pAttribs->pArr[i].core.type == STUC_ATTRIB_STRING) {
				stucDecodeString(pData, pAttribData, attribSize);
			}
			else {
				stucDecodeValue(pData, pAttribData, attribSize);
			}
		}
	}
}

static
void decodeIndexedAttribs(
	StucContext pCtx,
	ByteString *pData,
	AttribIndexedArr *pAttribs
) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		AttribIndexed* pAttrib = pAttribs->pArr + i;
		I32 attribSize = stucGetAttribSizeIntern(pAttrib->core.type);
		pAttrib->core.pData = pAttrib->count ?
			pCtx->alloc.fpCalloc(pAttrib->count, attribSize) : NULL;
		attribSize *= 8;
		for (I32 j = 0; j < pAttrib->count; ++j) {
			void *pAttribData = stucAttribAsVoid(&pAttrib->core, j);
			if (pAttribs->pArr[i].core.type == STUC_ATTRIB_STRING) {
				stucDecodeString(pData, pAttribData, attribSize);
			}
			else {
				stucDecodeValue(pData, pAttribData, attribSize);
			}
		}
	}
}

static
StucErr decodeStucHeader(
	StucContext pCtx,
	ByteString *pByteString,
	StucHeader *pHeader,
	StucMapDeps *pDeps
) {
	StucErr err = PIX_ERR_SUCCESS;
	stucDecodeString(pByteString, pHeader->format, MAP_FORMAT_NAME_MAX_LEN);
	stucDecodeValue(pByteString, (U8 *)&pHeader->version, 16);
	stucDecodeValue(pByteString, (U8 *)&pHeader->dataSizeCompressed, 64);;
	stucDecodeValue(pByteString, (U8 *)&pHeader->dataSize, 64);
	stucDecodeValue(pByteString, (U8 *)&pHeader->idxAttribCount, 32);
	stucDecodeValue(pByteString, (U8 *)&pHeader->objCount, 32);
	stucDecodeValue(pByteString, (U8 *)&pHeader->usgCount, 32);
	stucDecodeValue(pByteString, (U8 *)&pHeader->cutoffCount, 32);

	err = isDataTagInvalid(pByteString, TAG_DEP);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	I32 depCount = 0;
	stucDecodeValue(pByteString, (U8 *)&depCount, 32);
	if (!depCount) {
		return err;
	}
	I32 pathMax = pixioPathMaxGet();
	char *pBuf = pCtx->alloc.fpMalloc(pathMax);
	for (I32 i = 0; i < depCount; ++i) {
		DataTag type = decodeDataTag(pByteString, NULL);
		switch (type) {
			case TAG_DEP_TYPE_MAP: {
				memset(pBuf, 0, pathMax);
				stucDecodeString(pByteString, pBuf, pathMax);
				I32 len = strnlen(pBuf, pathMax);
				PIX_ERR_THROW_IFNOT_COND(err, len != pathMax, "", 0);
				I32 newIdx = 0;
				PIXALC_DYN_ARR_ADD(PixtyStr, &pCtx->alloc, &pDeps->maps, newIdx);
				pDeps->maps.pArr[newIdx].pStr = pCtx->alloc.fpMalloc(len + 1);
				memcpy(pDeps->maps.pArr[newIdx].pStr, pBuf, len + 1);
				break;
			}
		}
	}
	PIX_ERR_CATCH(0, err, stucMapDepsDestroy(&pCtx->alloc, pDeps););
	if (pBuf) {
		pCtx->alloc.fpFree(pBuf);
	}
	return err;
}

static
PixErr loadActiveAttribs(
	const StucContext pCtx,
	AttribActive *pActiveAttribs,
	ByteString *pData
) {
	PixErr err = PIX_ERR_SUCCESS;
	I32 count = 0;
	stucDecodeValue(pData, (U8 *)&count, 8);
	for (I32 i = 0; i < count; ++i) {
		I32 idx = 0;
		stucDecodeValue(pData, (U8 *)&idx, 8);
		stucDecodeValue(pData, (U8 *)&pActiveAttribs[idx].domain, 4);
		stucDecodeValue(pData, (U8 *)&pActiveAttribs[idx].idx, 16);
		pActiveAttribs[idx].active = true;
	}
	return err;
}

static
StucErr loadIdxRedirects(
	const StucAlloc *pAlloc,
	ByteString *pData,
	StucIdxTableArr *pIdxTableArr
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("", pIdxTableArr);
	stucDecodeValue(pData, (U8 *)&pIdxTableArr->count, 16);
	pIdxTableArr->size = pIdxTableArr->count;
	pIdxTableArr->pArr = pAlloc->fpCalloc(pIdxTableArr->size, sizeof(StucIdxTable));
	for (I32 i = 0; i < pIdxTableArr->count; ++i) {
		StucIdxTable *pTable = pIdxTableArr->pArr + i;
		stucDecodeValue(pData, (U8 *)&pTable->idx, 16);
		stucDecodeValue(pData, (U8 *)&pTable->table.count, 8);
		pTable->table.size = pTable->table.count;
		pTable->table.pArr = pAlloc->fpCalloc(pTable->table.size, 1);
		I32 count = 0;
		stucDecodeValue(pData, (U8 *)&count, 8);
		for (I32 j = 0; j < count; ++j) {
			I32 idx = 0;
			stucDecodeValue(pData, (U8 *)&idx, 8);
			PIX_ERR_RETURN_IFNOT_COND(err, idx < pTable->table.count, "");
			stucDecodeValue(pData, (U8 *)&pTable->table.pArr[idx], 8);
		}
	}
	return err;
}

static
StucErr loadObj(
	StucContext pCtx,
	StucObject *pObj,
	ByteString *pData,
	bool checkIdxRedirects,
	StucIdxTableArr *pIdxTableArr
) {
	StucErr err = PIX_ERR_SUCCESS;
	stucCreateMesh(pCtx, pObj, STUC_OBJECT_DATA_MESH_INTERN);
	StucMesh *pMesh = (StucMesh *)pObj->pData;

	err = isDataTagInvalid(pData, TAG_OBJECT);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	DataTag tag = decodeDataTag(pData, NULL);
	if (tag == TAG_IDX_REDIRECTS) {
		PIX_ERR_THROW_IFNOT_COND(err, checkIdxRedirects, "unexpected data tag", 0);
		err = loadIdxRedirects(&pCtx->alloc, pData, pIdxTableArr);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		tag = decodeDataTag(pData, NULL);
	}
	PIX_ERR_THROW_IFNOT_COND(err, tag == TAG_ACTIVE_ATTRIBS, "", 0);
	err = loadActiveAttribs(pCtx, pMesh->activeAttribs, pData);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = isDataTagInvalid(pData, TAG_XFORM);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	for (I32 i = 0; i < 16; ++i) {
		I32 x = i % 4;
		I32 y = i / 4;
		stucDecodeValue(pData, (U8 *)&pObj->transform.d[y][x], 32);
	}
	err = isDataTagInvalid(pData, TAG_OBJECT_TYPE);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	stucDecodeValue(pData, (U8 *)&pObj->pData->type, 8);
	if (!stucCheckIfMesh(*pObj->pData)) {
		PIX_ERR_THROW(err, "Object is not a mesh", 0);
	}
	err = isDataTagInvalid(pData, TAG_MESH_HEADER);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	stucDecodeValue(pData, (U8 *)&pMesh->meshAttribs.count, 32);
	pMesh->meshAttribs.pArr = pMesh->meshAttribs.count ?
		pCtx->alloc.fpCalloc(pMesh->meshAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pData, &pMesh->meshAttribs);
	PIX_ERR_THROW_IFNOT(err, "Failed to decode mesh attrib meta", 0);

	stucDecodeValue(pData, (U8 *)&pMesh->faceAttribs.count, 32);
	pMesh->faceAttribs.pArr = pMesh->faceAttribs.count ?
		pCtx->alloc.fpCalloc(pMesh->faceAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pData, &pMesh->faceAttribs);
	PIX_ERR_THROW_IFNOT(err, "Failed to decode face attrib meta", 0);

	stucDecodeValue(pData, (U8 *)&pMesh->cornerAttribs.count, 32);
	pMesh->cornerAttribs.pArr = pMesh->cornerAttribs.count ?
		pCtx->alloc.fpCalloc(pMesh->cornerAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pData, &pMesh->cornerAttribs);
	PIX_ERR_THROW_IFNOT(err, "Failed to decode corner attrib meta", 0);

	stucDecodeValue(pData, (U8 *)&pMesh->edgeAttribs.count, 32);
	pMesh->edgeAttribs.pArr = pMesh->edgeAttribs.count ?
		pCtx->alloc.fpCalloc(pMesh->edgeAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pData, &pMesh->edgeAttribs);
	PIX_ERR_THROW_IFNOT(err, "Failed to decode edge meta", 0);

	stucDecodeValue(pData, (U8 *)&pMesh->vertAttribs.count, 32);
	pMesh->vertAttribs.pArr = pMesh->vertAttribs.count ?
		pCtx->alloc.fpCalloc(pMesh->vertAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pData, &pMesh->vertAttribs);
	PIX_ERR_THROW_IFNOT(err, "Failed to decode vert attrib meta", 0);

	stucDecodeValue(pData, (U8 *)&pMesh->faceCount, 32);
	stucDecodeValue(pData, (U8 *)&pMesh->cornerCount, 32);
	stucDecodeValue(pData, (U8 *)&pMesh->edgeCount, 32);
	stucDecodeValue(pData, (U8 *)&pMesh->vertCount, 32);

	err = isDataTagInvalid(pData, TAG_MESH_ATTRIBS);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	decodeAttribs(pCtx, pData, &pMesh->meshAttribs, 1);
	err = isDataTagInvalid(pData, TAG_FACE_LIST);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	pMesh->pFaces = pCtx->alloc.fpCalloc(pMesh->faceCount + 1, sizeof(I32));
	for (I32 i = 0; i < pMesh->faceCount; ++i) {
		stucDecodeValue(pData, (U8 *)&pMesh->pFaces[i], 32);
		PIX_ERR_ASSERT("",
			pMesh->pFaces[i] >= 0 &&
			pMesh->pFaces[i] < pMesh->cornerCount
		);
	}
	err = isDataTagInvalid(pData, TAG_FACE_ATTRIBS);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	pMesh->pFaces[pMesh->faceCount] = pMesh->cornerCount;
	decodeAttribs(pCtx, pData, &pMesh->faceAttribs, pMesh->faceCount);

	err = isDataTagInvalid(pData, TAG_CORNER_AND_EDGE_LISTS);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	pMesh->pCorners = pCtx->alloc.fpCalloc(pMesh->cornerCount, sizeof(I32));
	pMesh->pEdges = pCtx->alloc.fpCalloc(pMesh->cornerCount, sizeof(I32));
	for (I32 i = 0; i < pMesh->cornerCount; ++i) {
		stucDecodeValue(pData, (U8 *)&pMesh->pCorners[i], 32);
		PIX_ERR_ASSERT("",
			pMesh->pCorners[i] >= 0 &&
			pMesh->pCorners[i] < pMesh->vertCount
		);
		stucDecodeValue(pData, (U8 *)&pMesh->pEdges[i], 32);
		PIX_ERR_ASSERT("",
			pMesh->pEdges[i] >= 0 &&
			pMesh->pEdges[i] < pMesh->edgeCount
		);
	}

	err = isDataTagInvalid(pData, TAG_CORNER_ATTRIBS);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	decodeAttribs(pCtx, pData, &pMesh->cornerAttribs, pMesh->cornerCount);
	err = isDataTagInvalid(pData, TAG_EDGE_ATTRIBS);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	decodeAttribs(pCtx, pData, &pMesh->edgeAttribs, pMesh->edgeCount);
	err = isDataTagInvalid(pData, TAG_VERT_ATTRIBS);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	decodeAttribs(pCtx, pData, &pMesh->vertAttribs, pMesh->vertCount);

	PIX_ERR_CATCH(0, err,
		stucMeshDestroy(pCtx, pMesh);
		pCtx->alloc.fpFree(pMesh);
	);
	return err;
}

static
StucErr decodeBlendOpts(
	StucContext pCtx,
	const StucAlloc *pAlloc,
	ByteString *pData,
	StucMapArrEntry *pEntry
) {
	StucErr err = PIX_ERR_SUCCESS;
	for (StucDomain domain = STUC_DOMAIN_FACE; domain <= STUC_DOMAIN_VERT; ++domain) {
		StucBlendOptArr *pArr = pEntry->blendOptArr + domain;
		stucDecodeValue(pData, (U8 *)&pArr->count, 16);
		pArr->size = pArr->count;
		pArr->pArr = pAlloc->fpCalloc(pArr->size, sizeof(StucBlendOpt));
		for (I32 i = 0; i < pArr->count; ++i) {
			StucBlendOpt *pOpts = pArr->pArr + i;
			stucDecodeValue(pData, (U8 *)&pOpts->attrib, 16);
			AttribType type = STUC_ATTRIB_NONE;
			stucDecodeValue(pData, (U8 *)&type, 16);
			PIX_ERR_RETURN_IFNOT_COND(err, type >= 0 && type < STUC_ATTRIB_TYPE_ENUM_COUNT, "");
			pOpts->blendConfig = stucGetTypeDefaultConfig(&pCtx->typeDefaults, type)->blendConfig;
			UBitField8 flags = 0;
			stucDecodeValue(pData, (U8 *)&flags, 8);
			if (flags & 0x1) {
				stucDecodeValue(pData, (U8 *)&pOpts->blendConfig.blend, 8);
			}
			if (flags >> 1 & 0x1) {
				stucDecodeValue(pData, (U8 *)&pOpts->blendConfig.fMax, 64);
			}
			if (flags >> 2 & 0x1) {
				stucDecodeValue(pData, (U8 *)&pOpts->blendConfig.fMin, 64);
			}
			if (flags >> 3 & 0x1) {
				stucDecodeValue(pData, (U8 *)&pOpts->blendConfig.iMax, 64);
			}
			if (flags >> 4 & 0x1) {
				stucDecodeValue(pData, (U8 *)&pOpts->blendConfig.iMin, 64);
			}
			if (flags >> 5 & 0x1) {
				stucDecodeValue(pData, (U8 *)&pOpts->blendConfig.opacity, 32);
			}
			if (flags >> 6 & 0x1) {
				stucDecodeValue(pData, (U8 *)&pOpts->blendConfig.clamp, 1);
			}
			if (flags >> 7 & 0x1) {
				stucDecodeValue(pData, (U8 *)&pOpts->blendConfig.order, 1);
			}
		}
	}
	return err;
}

static
StucErr loadMapOverrides(
	StucContext pCtx,
	const StucAlloc *pAlloc,
	ByteString *pData,
	ObjMapOptsArr *pMapOptsArr,
	I32 objIdx
) {
	StucErr err = PIX_ERR_SUCCESS;
	err = isDataTagInvalid(pData, TAG_MAP_OVERRIDES);
	PIX_ERR_RETURN_IFNOT(err, "");
	I32 count = 0;
	stucDecodeValue(pData, (U8 *)&count, 16);
	if (!count) {
		return err;
	}
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(ObjMapOpts, pAlloc, pMapOptsArr, newIdx);
	ObjMapOpts *pOpts = pMapOptsArr->pArr + newIdx;
	pOpts->obj = objIdx;
	pOpts->arr.size = pOpts->arr.count = count;
	pOpts->arr.pArr = pAlloc->fpCalloc(pOpts->arr.size, sizeof(StucMapArrEntry));
	for (I32 i = 0; i < count; ++i) {
		StucMapArrEntry *pEntry = pOpts->arr.pArr + i;
		stucDecodeValue(pData, (U8 *)&pEntry->matIdx, 16);
		UBitField8 header = 0;
		stucDecodeValue(pData, (U8 *)&header, 8);
		stucDecodeValue(pData, (U8 *)&pEntry->map.idx, 16);
		if (header & 0x1) {
			err = decodeBlendOpts(pCtx, pAlloc, pData, pEntry);
			PIX_ERR_RETURN_IFNOT(err, "");
		}
		if (header >> 1 & 0x1) {
			stucDecodeValue(pData, (U8 *)&pEntry->wScale, 32);
		}
		else {
			//TODO replace with default wscale
			pEntry->wScale = 1.0f;
		}
		if (header >> 2 & 0x1) {
			stucDecodeValue(pData, (U8 *)&pEntry->receiveLen, 32);
		}
		else {
			pEntry->receiveLen = -1.0f;
		}
	}
	return err;
}

static
void destroyUsgArrTemp(const StucContext pCtx, StucUsgArr *pArr) {
	for (I32 i = 0; i < pArr->count; ++i) {
		if (pArr->pArr[i].obj.pData) {
			StucMesh *pMesh = (StucMesh *)pArr->pArr[i].obj.pData;
			stucMeshDestroy(pCtx, pMesh);
			pCtx->alloc.fpFree(pMesh);
		}
	}
	pCtx->alloc.fpFree(pArr->pArr);
	*pArr = (StucUsgArr){0};
}

static
StucErr loadDataByTag(
	StucContext pCtx,
	StucHeader *pHeader,
	ByteString *pData,
	StucObjArr *pObjArr,
	ObjMapOptsArr *pMapOptsArr,
	StucUsgArr *pUsgArr,
	StucObjArr *pCutoffArr,
	StucIdxTableArr *pIdxTableArrs,
	AttribIndexedArr *pIndexedAttribs
) {
	StucErr err = PIX_ERR_SUCCESS;
	switch (decodeDataTag(pData, NULL)) {
		case TAG_TYPE_TARGET:
			err = loadMapOverrides(pCtx, &pCtx->alloc, pData, pMapOptsArr, pObjArr->count);
			PIX_ERR_RETURN_IFNOT(err, "");
			//v fallthrough v
		case TAG_TYPE_OBJECT: {
			PIX_ERR_RETURN_IFNOT_COND(err, pObjArr->count < pHeader->objCount, "");
			StucObject *pObj = pObjArr->pArr + pObjArr->count;
			err = loadObj(pCtx, pObj, pData, true, pIdxTableArrs + pObjArr->count);
			++pObjArr->count;
			PIX_ERR_RETURN_IFNOT(err, "");
			break;
		}
		case TAG_TYPE_USG: {
			PIX_ERR_RETURN_IFNOT_COND(err, pUsgArr->count < pHeader->usgCount, "");
			StucUsg *pUsg = pUsgArr->pArr + pUsgArr->count;
			++pUsgArr->count;
			err = loadObj(pCtx, &pUsg->obj, pData, false, NULL);
			PIX_ERR_RETURN_IFNOT(err, "");
			stucDecodeValue(pData, (U8 *)&pUsg->flatCutoff.enabled, 1);
			if (pUsg->flatCutoff.enabled) {
				stucDecodeValue(pData, (U8 *)&pUsg->flatCutoff.idx, 32);
				PIX_ERR_RETURN_IFNOT_COND(
					err,
					pUsg->flatCutoff.idx >= 0 &&
					pUsg->flatCutoff.idx < pHeader->cutoffCount,
					"usg flat-cutoff idx is out of bounds"
				);
			}
			break;
		}
		case TAG_TYPE_USG_FLAT_CUTOFF: {
			PIX_ERR_RETURN_IFNOT_COND(err, pCutoffArr->count < pHeader->cutoffCount, "");
			StucObject *pObj = pCutoffArr->pArr + pCutoffArr->count;
			err = loadObj(pCtx, pObj, pData, false, NULL);
			++pCutoffArr->count;
			PIX_ERR_RETURN_IFNOT(err, "");
			break;
		}
		case TAG_IDX_ATTRIBS:
			if (pHeader->idxAttribCount) {
				PIX_ERR_ASSERT("", pHeader->idxAttribCount > 0);
				pIndexedAttribs->size = pIndexedAttribs->count = pHeader->idxAttribCount;
				pIndexedAttribs->pArr =
					pCtx->alloc.fpCalloc(pIndexedAttribs->size, sizeof(AttribIndexed));
				decodeIndexedAttribMeta(pData, pIndexedAttribs);
				decodeIndexedAttribs(pCtx, pData, pIndexedAttribs);
			}
			break;
		default:
			PIX_ERR_RETURN(err, "unexpected data tag");
	}
	return err;
}

static
StucErr correctIdxAttribsOnLoad(
	StucContext pCtx,
	const AttribIndexedArr *pIdxAttribs,
	const StucIdxTableArr *pIdxTableArr,
	StucMesh *pMesh
) {
	StucErr err = PIX_ERR_SUCCESS;
	for (I32 i = 0; i < pIdxTableArr->count; ++i) {
		if (!pIdxTableArr->pArr[i].table.count) {
			continue;
		}
		Attrib *pAttrib = NULL;
		StucDomain domain = STUC_DOMAIN_NONE;
		err = stucAttribGetAllDomains(
			pCtx,
			pMesh,
			pIdxAttribs->pArr[pIdxTableArr->pArr[i].idx].core.name,
			&pAttrib,
			NULL,
			&domain
		);
		PIX_ERR_RETURN_IFNOT(err, "");
		PIX_ERR_RETURN_IFNOT_COND(
			err,
			pAttrib,
			"indexed attrib has no matching attrib in mesh"
		);
		PIX_ERR_RETURN_IFNOT_COND(
			err,
			pAttrib->core.type == STUC_ATTRIB_I8,
			"indexed attribs must be referenced with an attrib of type I8"
		);
		I32 compCount = stucDomainCountGetIntern(pMesh, domain);
		for (I32 j = 0; j < compCount; ++j) {
			I8 *pIdx = stucAttribAsI8(&pAttrib->core, j);
			PIX_ERR_RETURN_IFNOT_COND(
				err,
				*pIdx >= 0 &&
				*pIdx < pIdxTableArr->pArr[i].table.count,
				"idx out of bounds"
			);
			*pIdx = pIdxTableArr->pArr[i].table.pArr[*pIdx];
		}
	}
	return err;
}

static
StucErr decodeStucData(
	StucContext pCtx,
	StucHeader *pHeader,
	ByteString *pData,
	StucObjArr *pObjArr,
	ObjMapOptsArr *pMapOptsArr,
	StucUsgArr *pUsgArr,
	StucObjArr *pCutoffArr,
	StucIdxTableArr **ppIdxTableArrs,
	AttribIndexedArr *pIndexedAttribs,
	bool correctIdxAttribs
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pHeader->objCount, "no objects in stuc file");
	PIX_ERR_ASSERT("", pHeader->usgCount >= 0);
	if (pHeader->objCount) {
		pObjArr->pArr = pCtx->alloc.fpCalloc(pHeader->objCount, sizeof(StucObject));
	}
	if (pHeader->usgCount) {
		pUsgArr->pArr = pCtx->alloc.fpCalloc(pHeader->usgCount, sizeof(StucUsg));
	}
	if (pHeader->cutoffCount) {
		pCutoffArr->pArr = pCtx->alloc.fpCalloc(pHeader->cutoffCount, sizeof(StucObject));
	}
	StucIdxTableArr *pIdxTableArrs =
		pCtx->alloc.fpCalloc(pHeader->objCount, sizeof(StucIdxTableArr));
	do {
		PIX_ERR_THROW_IFNOT_COND(err, pData->byteIdx < pData->size, "", 0);
		err = loadDataByTag(
			pCtx,
			pHeader,
			pData,
			pObjArr,
			pMapOptsArr,
			pUsgArr,
			pCutoffArr,
			pIdxTableArrs,
			pIndexedAttribs
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	} while(pData->byteIdx != pData->size);
	if (correctIdxAttribs) {
		for (I32 i = 0; i < pObjArr->count; ++i) {
			StucMesh *pMesh = (StucMesh *)pObjArr->pArr[i].pData;
			err = correctIdxAttribsOnLoad(
				pCtx,
				pIndexedAttribs,
				pIdxTableArrs + i,
				pMesh
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}
		destroyIdxTableArrs(&pCtx->alloc, &pIdxTableArrs, pObjArr->count);
	}
	else {
		PIX_ERR_THROW_IFNOT_COND(
			err,
			ppIdxTableArrs,
			"ppIdxTableArrs arg must not be NULL if correctIdxAttribs is false",
			0
		);
		*ppIdxTableArrs = pIdxTableArrs;
	}
	PIX_ERR_CATCH(0, err,
		destroyIdxTableArrs(&pCtx->alloc, &pIdxTableArrs, pObjArr->count);
		stucObjArrDestroy(pCtx, pObjArr);
		destroyUsgArrTemp(pCtx, pUsgArr);
		stucObjArrDestroy(pCtx, pCutoffArr);
	);
	return err;
}

static
StucErr openMapFile(StucContext pCtx, const char *pFilepath, void **ppFile) {
	StucErr err = PIX_ERR_SUCCESS;
	printf("Loading STUC file: %s\n", pFilepath);
	err = pCtx->io.fpOpen(ppFile, pFilepath, 1, &pCtx->alloc);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

static
StucErr importMapHeader(
	StucContext pCtx,
	void *pFile,
	StucHeader *pHeader,
	StucMapDeps *pDeps
) {
	StucErr err = PIX_ERR_SUCCESS;
	ByteString headerByteString = {0};
	I32 headerSize = 0;
	err = pCtx->io.fpRead(pFile, (U8 *)&headerSize, 4);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	headerByteString.pString = pCtx->alloc.fpMalloc(headerSize);
	err = pCtx->io.fpRead(pFile, headerByteString.pString, headerSize);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = decodeStucHeader(pCtx, &headerByteString, pHeader, pDeps);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_THROW_IFNOT_COND(
		err,
		!strncmp(pHeader->format, MAP_FORMAT_NAME, MAP_FORMAT_NAME_MAX_LEN),
		"map file is corrupt",
		0
	);
	PIX_ERR_THROW_IFNOT_COND(
		err, 
		pHeader->version == STUC_MAP_VERSION,
		"map file version not supported",
		0
	);
	PIX_ERR_CATCH(0, err, ;);
	if (headerByteString.pString) {
		pCtx->alloc.fpFree(headerByteString.pString);
	}
	return err;
}

StucErr stucMapImportGetDep(
	StucContext pCtx,
	const char *filePath,
	StucMapDeps *pDeps
) {
	StucErr err = PIX_ERR_SUCCESS;
	void *pFile = NULL;
	StucHeader header = {0};

	err = openMapFile(pCtx, filePath, &pFile);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = importMapHeader(pCtx, pFile, &header, pDeps);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	PIX_ERR_CATCH(0, err, stucMapDepsDestroy(&pCtx->alloc, pDeps););
	if (pFile) {
		pCtx->io.fpClose(pFile);
	}
	return err;
}

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
) {
	StucErr err = PIX_ERR_SUCCESS;
	void *pFile = NULL;
	U8 *pDataRaw = NULL;
	ByteString dataByteString = {0};
	StucHeader header = {0};
	StucMapDeps deps = {0};

	err = openMapFile(pCtx, filePath, &pFile);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = importMapHeader(pCtx, pFile, &header, &deps);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	//decompress data
	z_stream zStream = {
		.zalloc = mallocZlibWrap,
		.zfree = freeZlibWrap,
		.opaque = (void *)&pCtx->alloc
	};
	pDataRaw = pCtx->alloc.fpMalloc((I32)header.dataSizeCompressed);
	err = pCtx->io.fpRead(pFile, pDataRaw, (I32)header.dataSizeCompressed);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	zStream.next_in = pDataRaw;
	err = checkZlibErr(Z_OK, inflateInit2(&zStream, STUC_WINDOW_BITS));
	PIX_ERR_THROW_IFNOT(err, "", 0);
	zStream.avail_in = header.dataSizeCompressed;
	dataByteString.pString = pCtx->alloc.fpMalloc(header.dataSize);
	zStream.next_out = dataByteString.pString;
	zStream.avail_out = header.dataSize;
	err = checkZlibErr(Z_STREAM_END, inflate(&zStream, Z_FINISH));
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = checkZlibErr(Z_OK, inflateEnd(&zStream));
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_THROW_IFNOT_COND(
		err,
		zStream.total_out == header.dataSize,
		"Failed to load STUC file. decompressed data len is wrong\n",
		0
	);
	dataByteString.size = header.dataSize;

	printf("Decoding data\n");
	err = decodeStucData(
		pCtx,
		&header,
		&dataByteString,
		pObjArr,
		pMapOptsArr,
		pUsgArr,
		pCutoffArr,
		ppIdxTableArrs,
		pIndexedAttribs,
		correctIdxAttribs
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err, ;);
	stucMapDepsDestroy(&pCtx->alloc, &deps);
	if (pFile) {
		pCtx->io.fpClose(pFile);
	}
	if (pDataRaw) {
		pCtx->alloc.fpFree(pDataRaw);
	}
	if (dataByteString.pString) {
		pCtx->alloc.fpFree(dataByteString.pString);
	}
	return err;
}

void stucIoSetCustom(StucContext pCtx, StucIo *pIo) {
	if (!pIo->fpOpen || !pIo->fpClose || !pIo->fpWrite || !pIo->fpRead) {
		printf("Failed to set custom IO. One or more functions were NULL");
		abort();
	}
	pCtx->io = *pIo;
}

void stucIoSetDefault(StucContext pCtx) {
	pCtx->io.fpOpen = pixioFileOpen;
	pCtx->io.fpClose = pixioFileClose;
	pCtx->io.fpWrite = pixioFileWrite;
	pCtx->io.fpRead = pixioFileRead;
}

const char *stucGetBasename(const char *pStr, I32 *pNameLen, I32 *pPathLen) {
	I32 pathMax = pixioPathMaxGet();
	I32 len = strnlen(pStr, pathMax);
	if (len > pathMax) {
		if (pNameLen) {
			*pNameLen = 0;
		}
		if (pPathLen) {
			*pPathLen = 0;
		}
		return NULL;
	}
	if (pPathLen) {
		*pPathLen = len;
	}
	for (I32 i = len - 1; i > 0; --i) {
		if (pStr[i - 1] == '/' || pStr[i - 1] == '\\') {
			if (pNameLen) {
				*pNameLen = len - i;
			}
			return pStr + i;
		}
	}
	if (pNameLen) {
		*pNameLen = len;
	}
	return pStr;
}
