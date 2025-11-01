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
	if (byteCount >= pByteString->size) {
		I64 oldSize = pByteString->size;
		pByteString->size *= 2;
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
	pStart[0] = pValue[0] << pByteString->nextBitIdx;
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

void stucEncodeString(const StucAlloc *pAlloc, ByteString *pByteString, U8 *pString) {
	I32 lengthInBits = ((I32)strlen((char *)pString) + 1) * 8;
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

	I32 byteLen = getByteLen(bitLen);
	for (I32 i = 0; i < byteLen; ++i) {
		pValue[i] = pStart[i] >> pByteString->nextBitIdx;
		U8 nextByte = pStart[i + 1];
		nextByte <<= 8 - pByteString->nextBitIdx;
		pValue[i] |= nextByte;
	}
	U8 mask = UCHAR_MAX >> (8 - abs(bitLen - byteLen * 8)) % 8;
	pValue[byteLen - 1] &= mask;
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
		stucEncodeString(pAlloc, pData, (U8 *)pAttribs->pArr[i].core.name);
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
		stucEncodeString(pAlloc, pData, (U8 *)pAttribs->pArr[i].core.name);
	}
}

static
void encodeDataName(const StucAlloc *pAlloc, ByteString *pByteString, char *pName) {
	//not using stucEncodeString, as there's not need for a null terminator.
	//Only using 2 characters
	
	//ensure string is aligned with byte (we need to do this manually,
	//as stucEncodeValue is being used instead of stucEncodeString)
	if (pByteString->nextBitIdx != 0) {
		pByteString->nextBitIdx = 0;
		pByteString->byteIdx++;
	}
	stucEncodeValue(pAlloc, pByteString, (U8 *)pName, 16);
}

static
PixErr encodeActiveAttribs(
	const StucAlloc *pAlloc,
	ByteString *pData,
	StucMesh *pMesh
) {
	PixErr err = PIX_ERR_SUCCESS;
	
	encodeDataName(pAlloc, pData, "AA");
	I32 count = 0;
	for (I32 i = 0; i < STUC_ATTRIB_USE_ENUM_COUNT; ++i) {
		count += pMesh->activeAttribs[i].active;
	}
	stucEncodeValue(pAlloc, pData, (U8 *)&count, 8);
	for (I32 i = 0; i < STUC_ATTRIB_USE_ENUM_COUNT; ++i) {
		if (!pMesh->activeAttribs[i].active) {
			continue;
		}
		PIX_ERR_RETURN_IFNOT_COND(
			err,
			pMesh->activeAttribs[i].idx < 65536,
			"active attrib idx exceeds 2^16"
		);
		stucEncodeValue(pAlloc, pData, (U8 *)&i, 8);
		stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->activeAttribs[i].domain, 4);
		stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->activeAttribs[i].idx, 16);
	}
	return err;
}

typedef struct IdxTableArr {
	PixtyI8Arr *pArr;
	I32 size;
	I32 count;
} IdxTableArr;

static
void destroyIdxTableArr(StucAlloc *pAlloc, IdxTableArr *pArr) {
	if (pArr->pArr) {
		for (I32 i = 0; i < pArr->count; ++i) {
			if (pArr->pArr[i].pArr) {
				pAlloc->fpFree(pArr->pArr[i].pArr);
			}
		}
		pAlloc->fpFree(pArr->pArr);
	}
}

static
void encodeRedirectTable(
	const StucAlloc *pAlloc,
	ByteString *pData,
	const IdxTableArr *pIdxTable
) {
	encodeDataName(pAlloc, pData, "IR"); //Index Redirects
	stucEncodeValue(pAlloc, pData, (U8 *)&pIdxTable->count, 16);
	for (I32 i = 0; i < pIdxTable->count; ++i) {
		PixtyI8Arr *pTable = pIdxTable->pArr + i;
		stucEncodeValue(pAlloc, pData, (U8 *)&pTable->count, 8);
		for (I32 j = 0; j < pTable->count; ++j) {
			if (pTable->pArr[j] >= 0) {
				stucEncodeValue(pAlloc, pData, (U8 *)&j, 8);//local
				stucEncodeValue(pAlloc, pData, (U8 *)&pTable->pArr[j], 8);//global
			}
		}
	}
}

typedef struct MappingOpt {
	F32 wScale;
	F32 receiveLen;
} MappingOpt;

typedef struct MatMapEntry {
	HTableEntryCore core;
	I32 linIdx;
	I32 mat;//global idx
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
	HTableEntryCore *pCore,
	const void *pKeyData,
	void *pInitInfo,
	I32 linIdx
) {
	StucMapExport *pHandle = (StucMapExport *)pUserData;
	MatMapEntryInit *pInit = (MatMapEntryInit *)pInitInfo;
	MatMapEntry *pEntry = (MatMapEntry *)pCore;
	pEntry->linIdx = linIdx;
	pEntry->mat = *(I32 *)pKeyData;
	pEntry->pMap = pInit->pMap;
	pEntry->opt = pInit->opt;
}

bool matMapEntryCmp(
	const HTableEntryCore *pEntry,
	const void *pKeyData,
	const void *pInitInfo
) {
	return ((MatMapEntry *)pEntry)->mat == *(I32 *)pKeyData;
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
void encodeMappingOpt(
	StucMapExport *pHandle,
	const StucMesh *pMesh,
	const StucMapArr *pMapArr,
	const AttribIndexedArr *pIdxAttribArr,
	const IdxTableArr *pIdxTable,
	F32 wScale,
	F32 receiveLen
) {
	StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	ByteString *pData = &pHandle->data;
	bool wroteDataName = false;
	const Attrib *pAttrib =
		stucGetActiveAttribConst(pHandle->pCtx, pMesh, STUC_ATTRIB_USE_IDX);
	const AttribIndexed *pMats =
		stucGetAttribIndexedInternConst(pIdxAttribArr, pAttrib->core.name);
	intptr_t attribIdx = (intptr_t)(pMats) - (intptr_t)(pAttrib->core.pData);
	attribIdx /= (intptr_t)stucGetAttribSizeIntern(pAttrib->core.type);
	for (I32 i = 0; i < pMats->count; ++i) {
		I32 globMatIdx = pIdxTable->pArr[attribIdx].pArr[i];
		if (globMatIdx == -2) {
			continue;//mat is not use in mesh
		}
		MappingOpt mappingOpt = {
			.wScale = wScale,
			.receiveLen = receiveLen
		};
		const char *pMatName = stucAttribAsVoidConst(&pMats->core, i);
		MatMapEntry *pEntry = NULL;
		stucHTableGet(
			&pHandle->matMapTable,
			0,
			&globMatIdx,
			&pEntry,
			true,
			&(MatMapEntryInit) {.pMap = pMapArr->pArr[i].pMap, .opt = mappingOpt},
			stucKeyFromI32, NULL, matMapEntryInit, matMapEntryCmp
		);
		bool mapOverride = pEntry->pMap != pMapArr->pArr[i].pMap;
		bool wScaleOverride = pEntry->opt.wScale = wScale;
		bool receiveOverride = pEntry->opt.receiveLen == receiveLen;
		UBitField8 commonOverride = 0x0;
		I32 domainOverrides[STUC_DOMAIN_MESH] = {0};
		ByteString commonBuf = {0};
		for (StucDomain domain = STUC_DOMAIN_FACE; domain <= STUC_DOMAIN_VERT; ++domain) {
			StucCommonAttribArr *pArr = NULL;
			stucCommonAttribArrGetFromDomain(
				pHandle->pCtx,
				pMapArr->pCommonAttribArr + i,
				domain,
				&pArr
			);
			const AttribArray *pAttribArr = stucGetAttribArrFromDomainConst(pMesh, domain);
			PIX_ERR_ASSERT(
				"common attrib arr len differs from mesh attrib arr",
				pArr->count == pAttribArr->count
			);
			for (I32 j = 0; j < pArr->count; ++j) {
				const StucTypeDefault *pDefault = stucGetTypeDefaultConfig(
					&pHandle->pCtx->typeDefaults,
					pAttribArr->pArr[j].core.type
				);
				BlendConfig *pBlendConfig = &pArr->pArr[j].blendConfig;
				if (memcmp(
					pBlendConfig,
					&pDefault->blendConfig,
					sizeof(BlendConfig))
				) {
					PIX_ERR_ASSERT("num attribs exceeds limit", pEntry->linIdx <= INT16_MAX);
					stucEncodeValue(pAlloc, &commonBuf, (U8 *)&pEntry->linIdx, 16);
					encodeBlendConfigOverride(pAlloc, &commonBuf, pDefault, pBlendConfig);
					++domainOverrides[domain];
				}
			}
		}
		UBitField8 header =
			mapOverride |
			!!commonOverride << 1 |
			wScaleOverride << 2 |
			receiveOverride << 3;
		if (header) {
			if (!wroteDataName) {
				encodeDataName(&pHandle->pCtx->alloc, pData, "MO");
			}
			stucEncodeValue(pAlloc, pData, (U8 *)&header, 8);
			if (mapOverride) {
				stucEncodeValue(pAlloc, pData, (U8 *)&pEntry->linIdx, 1);
			}
			if (commonOverride) {
				for (StucDomain domain = STUC_DOMAIN_FACE; domain <= STUC_DOMAIN_VERT; ++domain) {
					stucEncodeValue(pAlloc, pData, (U8 *)&domainOverrides[domain], 16);
				}
				reallocByteStringIfNeeded(pAlloc, pData, (I64)commonBuf.byteIdx * 8);
				memcpy(pData->pString, commonBuf.pString, commonBuf.byteIdx);
				pData->byteIdx += commonBuf.byteIdx;
			}
			if (commonBuf.pString) {
				pAlloc->fpFree(commonBuf.pString);
			}
			if (wScaleOverride) {
				stucEncodeValue(pAlloc, pData, (U8 *)&pEntry->opt.wScale, 32);
			}
			if (receiveOverride) {
				stucEncodeValue(pAlloc, pData, (U8 *)&pEntry->opt.receiveLen, 32);
			}
		}
	}
}

static
StucErr encodeObj(
	StucMapExport *pHandle,
	const StucObject *pObj,
	const IdxTableArr *pIdxTable,
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
	err = stucValidateMesh(pMesh, false, checkPosOnly);
	PIX_ERR_RETURN_IFNOT(err, "mesh validation failed");
	//encode obj header
	encodeDataName(pAlloc, pData, "OS"); //object start
	if (pIdxTable && pIdxTable->count) {
		encodeRedirectTable(pAlloc, pData, pIdxTable);
	}
	if (mappingOpt) {
		encodeMappingOpt(
			pHandle,
			pMesh,
			pMapArr,
			pIdxAttribArr,
			pIdxTable,
			wScale,
			receiveLen
		);
	}
	err = encodeActiveAttribs(pAlloc, pData, pMesh);
	PIX_ERR_RETURN_IFNOT(err, "");
	encodeDataName(pAlloc, pData, "XF"); //transform
	for (I32 i = 0; i < 16; ++i) {
		I32 x = i % 4;
		I32 y = i / 4;
		stucEncodeValue(pAlloc, pData, (U8 *)&pObj->transform.d[y][x], 32);
	}
	encodeDataName(pAlloc, pData, "OT"); //object type
	stucEncodeValue(pAlloc, pData, (U8 *)&pObj->pData->type, 8);
	if (!stucCheckIfMesh(*pObj->pData)) {
		return err;
	}
	encodeDataName(pAlloc, pData, "HD"); //header
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
	encodeDataName(pAlloc, pData, "MA"); //mesh attribs
	encodeAttribs(pAlloc, pData, &pMesh->meshAttribs, 1);
	encodeDataName(pAlloc, pData, "FL"); //face list
	for (I32 i = 0; i < pMesh->faceCount; ++i) {
		PIX_ERR_ASSERT("",
			pMesh->pFaces[i] >= 0 &&
			pMesh->pFaces[i] < pMesh->cornerCount
		);
		stucEncodeValue(pAlloc, pData, (U8 *)&pMesh->pFaces[i], 32);
	}
	encodeDataName(pAlloc, pData, "FA"); //face attribs
	encodeAttribs(pAlloc, pData, &pMesh->faceAttribs, pMesh->faceCount);
	encodeDataName(pAlloc, pData, "LL"); //corner and edge lists
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
	encodeDataName(pAlloc, pData, "LA"); //corner attribs
	encodeAttribs(pAlloc, pData, &pMesh->cornerAttribs, pMesh->cornerCount);
	encodeDataName(pAlloc, pData, "EA"); //edge attribs
	encodeAttribs(pAlloc, pData, &pMesh->edgeAttribs, pMesh->edgeCount);
	encodeDataName(pAlloc, pData, "VA"); //vert attribs
	encodeAttribs(pAlloc, pData, &pMesh->vertAttribs, pMesh->vertCount);
	encodeDataName(pAlloc, pData, "OE"); //object end
	return err;
}

/*
static
I32 addUniqToPtrArr(void *pPtr, I32 *pCount, void **ppArr) {
	for (I32 i = 0; i < *pCount; ++i) {
		if (pPtr == ppArr[i]) {
			return i;
		}
	}
	ppArr[*pCount] = pPtr;
	++*pCount;
	return *pCount - 1;
}

static
void getUniqueFlatCutoffs(
	StucContext pCtx,
	I32 usgCount,
	StucUsg *pUsgArr,
	I32 *pCutoffCount,
	StucObject ***pppCutoffs,
	I32 **ppIndices
) {
	*ppIndices = pCtx->alloc.fpCalloc(usgCount, sizeof(I32));
	*pppCutoffs = pCtx->alloc.fpCalloc(usgCount, sizeof(void *));
	*pCutoffCount = 0;
	for (I32 i = 0; i < usgCount; ++i) {
		if (!pUsgArr[i].pFlatCutoff) {
			continue;
		}
		(*ppIndices)[i] =
			addUniqToPtrArr(pUsgArr[i].pFlatCutoff, pCutoffCount, (void **)*pppCutoffs);
	}
	*pppCutoffs = pCtx->alloc.fpRealloc(*pppCutoffs, sizeof(void *) * *pCutoffCount);
}

static
I64 estimateObjSize(StucObject *pObj) {
	I64 total = 0;
	StucMesh *pMesh = (StucMesh *)pObj->pData;
	total += 4 * 16; //transform
	total += 1; //type
	total += 2 * 3; //data names/ checks
	if (!stucCheckIfMesh(*pObj->pData)) {
		return total;
	}
	
	total += 2 * 9; //data names/ checks

	total += (I64)pMesh->faceAttribs.count * (I64)pMesh->faceCount;
	total += (I64)pMesh->cornerAttribs.count * (I64)pMesh->cornerCount;
	total += (I64)pMesh->edgeAttribs.count * (I64)pMesh->edgeCount;
	total += (I64)pMesh->vertAttribs.count * (I64)pMesh->vertCount;

	total += 4 * (I64)pMesh->faceCount;
	total += 4 * (I64)pMesh->cornerCount;
	total += 4 * (I64)pMesh->cornerCount; //edge list

	return total;
}

static
I64 estimateUsgArrSize(I32 count, StucUsg *pUsgArr) {
	I64 total = 0;
	for (I32 i = 0; i < count; ++i) {
		total += estimateObjSize(&pUsgArr[i].obj);
		total += estimateObjSize(pUsgArr[i].pFlatCutoff);
	}
	return total;
}

static
I64 estimateObjArrSize(I32 count, StucObject *pObjArr) {
	I64 total = 0;
	for (I32 i = 0; i < count; ++i) {
		total += estimateObjSize(pObjArr + i);
	}
	return total;
}
*/

static
void destroyMapExport(StucMapExport *pHandle) {
	StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	pAlloc->fpFree(pHandle->pPath);
	pAlloc->fpFree(pHandle->data.pString);
	stucHTableDestroy(&pHandle->matMapTable);
	stucAttribIndexedArrDestroy(pHandle->pCtx, &pHandle->idxAttribs);
	*pHandle = (StucMapExport){0};
}

StucErr stucMapExportInit(
	StucContext pCtx,
	StucMapExport **ppHandle,
	const char *pPath
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
			Z_DEFAULT_COMPRESSION,
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
	printf("Compressed data is %ld long\n", zStream.total_out);

	//encode header
	const char *format = "UV Stucco Map File";
	I32 formatLen = (I32)strnlen(format, MAP_FORMAT_NAME_MAX_LEN);
	PIX_ERR_ASSERT("", formatLen < MAP_FORMAT_NAME_MAX_LEN);
	header.size =
		8 * ((I64)formatLen + 1) +
		16 + //version
		64 + //compressed data size
		64 + //uncompressed data size
		32 + //indexed attrib count
		32 + //obj count
		32 + //usg count
		32;  //flatten cutoff count
	header.size = header.size / 8 + (header.size % 8 != 0);
	header.pString = pAlloc->fpCalloc(header.size, 1);
	stucEncodeString(pAlloc, &header, (U8 *)format);
	I32 version = STUC_MAP_VERSION;
	stucEncodeValue(pAlloc, &header, (U8 *)&version, 16);
	stucEncodeValue(pAlloc, &header, (U8 *)&zStream.total_out, 64);
	stucEncodeValue(pAlloc, &header, (U8 *)&dataSize, 64);
	stucEncodeValue(pAlloc, &header, (U8 *)&pHandle->header.inAttribCount, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&pHandle->header.objCount, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&pHandle->header.usgCount, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&pHandle->header.cutoffCount, 32);

	err = pHandle->pCtx->io.fpOpen(&pFile, pHandle->pPath, 0, pAlloc);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	I64 finalHeaderLen = header.byteIdx + (header.nextBitIdx > 0);
	err = pHandle->pCtx->io.fpWrite(pFile, (U8 *)&finalHeaderLen, 2);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pHandle->pCtx->io.fpWrite(pFile, header.pString, (I32)finalHeaderLen);
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
StucErr makeIdxAttribRedirects(
	StucMapExport *pHandle,
	const StucObject *pObj,
	const StucAttribIndexedArr *pIndexedAttribs,
	IdxTableArr *pIdxTable
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
	pIdxTable->pArr = pAlloc->fpCalloc(pIdxTable->size, sizeof(PixtyI8Arr));
	for (I32 i = 0; i < pIndexedAttribs->count; ++i) {
		const AttribIndexed *pRef = pIndexedAttribs->pArr + i;
		memset(pIdxIsUsed, 0, INT8_MAX);
		const Attrib *pCompRef = NULL;
		I32 compCount = 0;
		for (StucDomain domain = STUC_DOMAIN_FACE; domain <= STUC_DOMAIN_VERT; ++domain) {
			pCompRef = stucGetAttribInternConst(
				pRef->core.name,
				stucGetAttribArrFromDomainConst(pMesh, domain),
				false,
				pHandle->pCtx,
				pMesh
			);
			if (pCompRef) {
				compCount = stucDomainCountGetIntern(pMesh, domain);
				break;
			}
		}
		PIX_ERR_THROW_IFNOT_COND(
			err,
			pCompRef->core.use == STUC_ATTRIB_USE_IDX &&
				pCompRef->core.type == STUC_ATTRIB_I8,
			"indexed attrib must be indexed with an attrib \
of use STUC_ATTRIB_USE_IDX & type STUC_ATTRIB_I8",
			0);
		for (I32 j = 0; j < compCount; ++j) {
			I8 idx = *(const I8 *)stucAttribAsVoidConst(&pCompRef->core, j);
			PIX_ERR_THROW_IFNOT_COND(err, idx >= 0, "negative STUC_ATTRIB_USE_IDX idx", 0);
			pIdxIsUsed[idx] = true;
		}

		AttribIndexed *pAttrib = stucGetAttribIndexedInternConst(
			&pHandle->idxAttribs,
			pRef->core.name
		);
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
		pIdxTable->pArr[i].pArr = pAlloc->fpCalloc(pRef->count, 1);
		for (I32 j = 0; j < pRef->count; ++j) {
			if (!pIdxIsUsed[j]) {
				pIdxTable->pArr[i].pArr[j] = -2;
				continue;
			}
			I32 idx = stucGetIdxInIndexedAttrib(pAttrib, pRef, j);
			if (idx == -1) {
				PIX_ERR_THROW_IFNOT_COND(err, pAttrib->count <= pAttrib->size, "", 0);
				if (pAttrib->count == pAttrib->size) {
					pAttrib->size *= 2;
					stucReallocAttrib(pAlloc, NULL, &pAttrib->core, pAttrib->size);
				}
				idx = pAttrib->count;
				++pAttrib->count;
				memcpy(
					stucAttribAsVoid(&pAttrib->core, idx),
					stucAttribAsVoidConst(&pRef->core, j),
					stucGetAttribSizeIntern(pRef->core.type)
				);
			}
			pIdxTable->pArr[i].pArr[j] = j == idx ? -1 : idx;
		}
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
	IdxTableArr idxTable = {0};
	err = makeIdxAttribRedirects(pHandle, pObj, pIndexedAttribs, &idxTable);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	encodeDataName(&pHandle->pCtx->alloc, &pHandle->data, "OB");
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
	return
		mapExportObjAdd(pHandle, pObj, pIndexedAttribs, true, pMapArr, wScale, receiveLen);
}

StucErr stucMapExportObjAdd(
	void *pHandle,
	const StucObject *pObj,
	const StucAttribIndexedArr *pIndexedAttribs
) {
	return mapExportObjAdd(pHandle, pObj, pIndexedAttribs, false, NULL, .0f, .0f);
}

StucErr stucMapExportUsgAdd(
	StucMapExport *pHandle,
	StucUsg *pUsg
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucAlloc *pAlloc = &pHandle->pCtx->alloc;
	encodeDataName(pAlloc, &pHandle->data, "UG");
	err = encodeObj(pHandle, &pUsg->obj, NULL, false, NULL, NULL, .0f, .0f, true);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	stucEncodeValue(pAlloc, &pHandle->data, (U8 *)pUsg->flatCutoff.enabled, 1);
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
	encodeDataName(pAlloc, &pHandle->data, "FC");
	err = encodeObj(pHandle, pFlatCutoff, NULL, false, NULL, NULL, .0f, .0f, true);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	++pHandle->header.cutoffCount;
	PIX_ERR_CATCH(0, err, destroyMapExport(pHandle););
	return err;
}

/*
StucErr stucWriteStucFile(
	StucContext pCtx,
	const char *pPath,
	I32 objCount,
	StucObject *pObjArr,
	I32 usgCount,
	StucUsg *pUsgArr,
	StucAttribIndexedArr *pIndexedAttribs
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pPath[0], "path is empty");
	const StucAlloc *pAlloc = &pCtx->alloc;
	ByteString header = {0};
	ByteString data = {0};
	U8 *pCompressed = NULL;
	void *pFile = NULL;

	I32 *pCutoffIndices = NULL;
	StucObject **ppCutoffs = NULL;
	I32 cutoffCount = 0;
	if (usgCount) {
		getUniqueFlatCutoffs(
			pCtx,
			usgCount,
			pUsgArr,
			&cutoffCount,
			&ppCutoffs,
			&pCutoffIndices
		);
	}
	data.size =
		estimateObjArrSize(objCount, pObjArr) +
		estimateUsgArrSize(usgCount, pUsgArr);
	data.pString = pCtx->alloc.fpCalloc(data.size, 1);
	if (pIndexedAttribs->count) {
		encodeIndexedAttribMeta(pAlloc, &data, pIndexedAttribs);
		encodeIndexedAttribs(pAlloc, &data, pIndexedAttribs);
	}
	for (I32 i = 0; i < objCount; ++i) {
		err = encodeObj(pAlloc, &data, pObjArr + i, NULL);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	if (usgCount) {
		for (I32 i = 0; i < cutoffCount; ++i) {
			err = encodeObj(pAlloc, &data, ppCutoffs[i], NULL);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}
		for (I32 i = 0; i < usgCount; ++i) {
			err = encodeObj(pAlloc, &data, &pUsgArr[i].obj, NULL);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			bool hasFlatCutoff = pUsgArr[i].pFlatCutoff != NULL;
			encodeDataName(pAlloc, &data, "FC"); //flatten cut-off
			stucEncodeValue(pAlloc, &data, (U8 *)&hasFlatCutoff, 8);
			if (hasFlatCutoff) {
				stucEncodeValue(pAlloc, &data, (U8 *)&pCutoffIndices[i], 32);
			}
		}
	}

	//compress data
	I64 dataSize = data.byteIdx + (data.nextBitIdx > 0);
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
			Z_DEFAULT_COMPRESSION,
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
	zStream.next_in = data.pString;
	err = checkZlibErr(Z_STREAM_END, deflate(&zStream, Z_FINISH));
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = checkZlibErr(Z_OK, deflateEnd(&zStream));
	PIX_ERR_THROW_IFNOT(err, "", 0);
	printf("Compressed data is %ld long\n", zStream.total_out);

	//encode header
	const char *format = "UV Stucco Map File";
	I32 formatLen = (I32)strnlen(format, MAP_FORMAT_NAME_MAX_LEN);
	PIX_ERR_ASSERT("", formatLen < MAP_FORMAT_NAME_MAX_LEN);
	header.size =
		8 * ((I64)formatLen + 1) +
		16 + //version
		64 + //compressed data size
		64 + //uncompressed data size
		32 + //indexed attrib count
		32 + //obj count
		32 + //usg count
		32;  //flatten cutoff count
	header.size = header.size / 8 + (header.size % 8 != 0);
	header.pString = pCtx->alloc.fpCalloc(header.size, 1);
	stucEncodeString(pAlloc, &header, (U8 *)format);
	I32 version = STUC_MAP_VERSION;
	stucEncodeValue(pAlloc, &header, (U8 *)&version, 16);
	stucEncodeValue(pAlloc, &header, (U8 *)&zStream.total_out, 64);
	stucEncodeValue(pAlloc, &header, (U8 *)&dataSize, 64);
	stucEncodeValue(pAlloc, &header, (U8 *)&pIndexedAttribs->count, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&objCount, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&usgCount, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&cutoffCount, 32);

	err = pCtx->io.fpOpen(&pFile, pPath, 0, &pCtx->alloc);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	I64 finalHeaderLen = header.byteIdx + (header.nextBitIdx > 0);
	err = pCtx->io.fpWrite(pFile, (U8 *)&finalHeaderLen, 2);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pCtx->io.fpWrite(pFile, header.pString, (I32)finalHeaderLen);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pCtx->io.fpWrite(pFile, pCompressed, (I32)zStream.total_out);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	PIX_ERR_CATCH(0, err, ;);
	if (pFile) {
		err = pCtx->io.fpClose(pFile);
	}
	if (header.pString) {
		pCtx->alloc.fpFree(header.pString);
	}
	if (data.pString) {
		pCtx->alloc.fpFree(data.pString);
	}
	if (pCompressed) {
		pAlloc->fpFree(pCompressed);
	}
	printf("Finished STUC export\n");
	return err;
}
*/

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
	stucStageBeginWrap(pCtx, "", pAttribs->count * dataLen);
	for (I32 i = 0; i < pAttribs->count; ++i) {
		Attrib* pAttrib = pAttribs->pArr + i;
		stucSetStageName(pCtx, "Decoding attrib");
		I32 attribSize = stucGetAttribSizeIntern(pAttrib->core.type);
		pAttrib->core.pData = dataLen ?
			pCtx->alloc.fpCalloc(dataLen, attribSize) : NULL;
		attribSize *= 8;
		I32 progressBase = i * pAttribs->count * dataLen;
		for (I32 j = 0; j < dataLen; ++j) {
			void *pAttribData = stucAttribAsVoid(&pAttrib->core, j);
			if (pAttribs->pArr[i].core.type == STUC_ATTRIB_STRING) {
				stucDecodeString(pData, pAttribData, attribSize);
			}
			else {
				stucDecodeValue(pData, pAttribData, attribSize);
			}
			stucStageProgressWrap(pCtx, j + progressBase);
		}
	}
	stucStageEndWrap(pCtx);
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
StucHeader decodeStucHeader(
	ByteString *headerByteString,
	AttribIndexedArr *pIndexedAttribs
) {
	StucHeader header = {0};
	stucDecodeString(headerByteString, header.format, MAP_FORMAT_NAME_MAX_LEN);
	stucDecodeValue(headerByteString, (U8 *)&header.version, 16);
	stucDecodeValue(headerByteString, (U8 *)&header.dataSizeCompressed, 64);;
	stucDecodeValue(headerByteString, (U8 *)&header.dataSize, 64);
	stucDecodeValue(headerByteString, (U8 *)&pIndexedAttribs->count, 32);
	stucDecodeValue(headerByteString, (U8 *)&header.objCount, 32);
	stucDecodeValue(headerByteString, (U8 *)&header.usgCount, 32);
	stucDecodeValue(headerByteString, (U8 *)&header.cutoffCount, 32);

	return header;
}

static
StucErr isDataNameInvalid(ByteString *pByteString, char *pName) {
	//ensure string is aligned with byte (we need to do this manually,
	//as stucDecodeValue is being used instead of stucDecodeString, given there's
	//only 2 characters)
	pByteString->byteIdx += pByteString->nextBitIdx > 0;
	pByteString->nextBitIdx = 0;
	char dataName[2] = {0};
	stucDecodeValue(pByteString, (U8 *)&dataName, 16);
	if (dataName[0] != pName[0] || dataName[1] != pName[1]) {
		return PIX_ERR_ERROR;
	}
	else {
		return PIX_ERR_SUCCESS;
	}
}

static
PixErr loadActiveAttribs(
	const StucContext pCtx,
	AttribActive *pActiveAttribs,
	ByteString *pData
) {
	PixErr err = PIX_ERR_SUCCESS;
	err = isDataNameInvalid(pData, "AA");
	PIX_ERR_RETURN_IFNOT(err, "");
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
StucErr loadObj(
	StucContext pCtx,
	StucObject *pObj,
	ByteString *pData,
	bool usesUsg
) {
	StucErr err = PIX_ERR_SUCCESS;
	stucCreateMesh(pCtx, pObj, STUC_OBJECT_DATA_MESH_INTERN);
	StucMesh *pMesh = (StucMesh *)pObj->pData;

	err = isDataNameInvalid(pData, "OS"); //transform/ xform and type
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'OS'", 0);
	err = loadActiveAttribs(pCtx, pMesh->activeAttribs, pData);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = isDataNameInvalid(pData, "XF"); //transform/ xform and type
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'XF'", 0);
	for (I32 i = 0; i < 16; ++i) {
		I32 x = i % 4;
		I32 y = i / 4;
		stucDecodeValue(pData, (U8 *)&pObj->transform.d[y][x], 32);
	}
	err = isDataNameInvalid(pData, "OT"); //object type
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'OT'", 0);
	stucDecodeValue(pData, (U8 *)&pObj->pData->type, 8);
	if (!stucCheckIfMesh(*pObj->pData)) {
		PIX_ERR_THROW(err, "Object is not a mesh", 0);
	}
	err = isDataNameInvalid(pData, "HD"); //header
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'HD'", 0);
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

	err = isDataNameInvalid(pData, "MA"); //mesh attribs
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'MA'", 0);
	decodeAttribs(pCtx, pData, &pMesh->meshAttribs, 1);
	stucStageEndWrap(pCtx);
	err = isDataNameInvalid(pData, "FL"); //face list
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'FL'", 0);
	pMesh->pFaces = pCtx->alloc.fpCalloc(pMesh->faceCount + 1, sizeof(I32));
	stucStageBeginWrap(pCtx, "Decoding faces", pMesh->faceCount);
	for (I32 i = 0; i < pMesh->faceCount; ++i) {
		stucDecodeValue(pData, (U8 *)&pMesh->pFaces[i], 32);
		PIX_ERR_ASSERT("",
			pMesh->pFaces[i] >= 0 &&
			pMesh->pFaces[i] < pMesh->cornerCount
		);
		stucStageProgressWrap(pCtx, i);
	}
	stucStageEndWrap(pCtx);
	err = isDataNameInvalid(pData, "FA"); //face attribs
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'FA'", 0);
	pMesh->pFaces[pMesh->faceCount] = pMesh->cornerCount;
	decodeAttribs(pCtx, pData, &pMesh->faceAttribs, pMesh->faceCount);

	err = isDataNameInvalid(pData, "LL"); //corner and edge lists
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'LL'", 0);
	pMesh->pCorners = pCtx->alloc.fpCalloc(pMesh->cornerCount, sizeof(I32));
	pMesh->pEdges = pCtx->alloc.fpCalloc(pMesh->cornerCount, sizeof(I32));
	stucStageBeginWrap(pCtx, "Decoding corners", pMesh->cornerCount);
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
		stucStageProgressWrap(pCtx, i);
	}
	stucStageEndWrap(pCtx);

	err = isDataNameInvalid(pData, "LA"); //corner attribs
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'LA'", 0);
	decodeAttribs(pCtx, pData, &pMesh->cornerAttribs, pMesh->cornerCount);
	err = isDataNameInvalid(pData, "EA"); //edge attribs
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'EA'", 0);
	decodeAttribs(pCtx, pData, &pMesh->edgeAttribs, pMesh->edgeCount);
	err = isDataNameInvalid(pData, "VA"); //vert attribs
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'VA'", 0);
	decodeAttribs(pCtx, pData, &pMesh->vertAttribs, pMesh->vertCount);

	err = isDataNameInvalid(pData, "OE"); //obj end
	PIX_ERR_THROW_IFNOT(err, "Data name did not match 'OE'", 0);
	PIX_ERR_CATCH(0, err,
		stucMeshDestroy(pCtx, pMesh);
		pCtx->alloc.fpFree(pMesh);
	);
	return err;
}

static
StucErr decodeStucData(
	StucContext pCtx,
	StucHeader *pHeader,
	ByteString *pData,
	StucObject **ppObjArr,
	StucUsg **ppUsgArr,
	StucObject **ppFlatCutoffArr,
	bool forEdit,
	AttribIndexedArr *pIndexedAttribs
) {
	StucErr err = PIX_ERR_SUCCESS;
	if (pIndexedAttribs && pIndexedAttribs->count) {
		PIX_ERR_ASSERT("", pIndexedAttribs->count > 0);
		pIndexedAttribs->pArr =
			pCtx->alloc.fpCalloc(pIndexedAttribs->count, sizeof(AttribIndexed));
		pIndexedAttribs->size = pIndexedAttribs->count;
		decodeIndexedAttribMeta(pData, pIndexedAttribs);
		decodeIndexedAttribs(pCtx, pData, pIndexedAttribs);
	}
	PIX_ERR_RETURN_IFNOT_COND(err, pHeader->objCount, "no objects in stuc file");
	*ppObjArr = pCtx->alloc.fpCalloc(pHeader->objCount, sizeof(StucObject));
	PIX_ERR_ASSERT("", pHeader->usgCount >= 0);
	bool usesUsg = pHeader->usgCount > 0 && !forEdit;
	for (I32 i = 0; i < pHeader->objCount; ++i) {
		//usgUsg is passed here to indicate that an extra vert
		//attrib should be created. This would be used later to mark a verts
		//respective usg.
		err = loadObj(pCtx, *ppObjArr + i, pData, usesUsg);
		PIX_ERR_RETURN_IFNOT(err, "");
	}

	if (pHeader->usgCount) {
		*ppUsgArr = pCtx->alloc.fpCalloc(pHeader->usgCount, sizeof(StucUsg));
		*ppFlatCutoffArr =
			pCtx->alloc.fpCalloc(pHeader->cutoffCount, sizeof(StucObject));
		for (I32 i = 0; i < pHeader->cutoffCount; ++i) {
			err = loadObj(pCtx, *ppFlatCutoffArr + i, pData, false);
			PIX_ERR_RETURN_IFNOT(err, "");
		}
		for (I32 i = 0; i < pHeader->usgCount; ++i) {
			//usgs themselves don't need a usg attrib, so false is passed
			err = loadObj(pCtx, &(*ppUsgArr)[i].obj, pData, false);
			PIX_ERR_RETURN_IFNOT(err, "");
			err = isDataNameInvalid(pData, "FC");
			PIX_ERR_RETURN_IFNOT(err, "");
			bool hasFlatCutoff = false;
			stucDecodeValue(pData, (U8 *)&hasFlatCutoff, 8);
			if (hasFlatCutoff) {
				I32 cutoffIdx = 0;
				stucDecodeValue(pData, (U8 *)&cutoffIdx, 32);
				PIX_ERR_ASSERT("",
					cutoffIdx >= 0 &&
					cutoffIdx < pHeader->cutoffCount
				);
				(*ppUsgArr)[i].flatCutoff.idx = cutoffIdx;
			}
		}
	}
	return err;
}

StucErr stucLoadStucFile(
	StucContext pCtx,
	const char *filePath,
	I32 *pObjCount,
	StucObject **ppObjArr,
	I32 *pUsgCount,
	StucUsg **ppUsgArr,
	I32 *pFlatCutoffCount,
	StucObject **ppFlatCutoffArr,
	bool forEdit,
	StucAttribIndexedArr *pIndexedAttribs
) {
	StucErr err = PIX_ERR_SUCCESS;
	ByteString headerByteString = {0};
	ByteString dataByteString = {0};
	void *pFile = NULL;
	U8 *pDataRaw = NULL;
	printf("Loading STUC file: %s\n", filePath);
	err = pCtx->io.fpOpen(&pFile, filePath, 1, &pCtx->alloc);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	I16 headerSize = 0;
	err = pCtx->io.fpRead(pFile, (U8 *)&headerSize, 2);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	printf("Stuc File Header Size: %d\n", headerSize);
	printf("Header is %d bytes\n", headerSize);
	headerByteString.pString = pCtx->alloc.fpMalloc(headerSize);
	printf("Reading header\n");
	err = pCtx->io.fpRead(pFile, headerByteString.pString, headerSize);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	printf("Decoding header\n");
	StucHeader header = decodeStucHeader(&headerByteString, pIndexedAttribs);
	PIX_ERR_THROW_IFNOT_COND(
		err,
		!strncmp(header.format, "UV Stucco Map File", MAP_FORMAT_NAME_MAX_LEN),
		"map file is corrupt",
		0
	);
	PIX_ERR_THROW_IFNOT_COND(
		err, 
		header.version == STUC_MAP_VERSION,
		"map file version not supported",
		0
	);

	//decompress data
	z_stream zStream = {
		.zalloc = mallocZlibWrap,
		.zfree = freeZlibWrap,
		.opaque = (void *)&pCtx->alloc
	};
	printf("Reading data\n");
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
	printf("Decompressing data\n");
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

	printf("Decoding data\n");
	err = decodeStucData(
		pCtx,
		&header,
		&dataByteString,
		ppObjArr,
		ppUsgArr,
		ppFlatCutoffArr,
		forEdit,
		pIndexedAttribs
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	*pObjCount = header.objCount;
	*pUsgCount = header.usgCount;
	*pFlatCutoffCount = header.cutoffCount;

	PIX_ERR_CATCH(0, err, ;);
	if (pFile) {
		pCtx->io.fpClose(pFile);
	}
	if (pDataRaw) {
		pCtx->alloc.fpFree(pDataRaw);
	}
	if (headerByteString.pString) {
		pCtx->alloc.fpFree(headerByteString.pString);
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
