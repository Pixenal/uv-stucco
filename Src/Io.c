//TODO these should be prefixed with STUC_
#define VERT_ATTRIBUTE_AMOUNT 3
#define LOOP_ATTRIBUTE_AMOUNT 3
#define ENCODE_DECODE_BUFFER_LENGTH 34
#define STUC_MAP_VERSION 100
#define STUC_FLAT_CUTOFF_HEADER_SIZE 56

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

#include <zlib.h>

#include <Io.h>
#include <QuadTree.h>
#include <MapFile.h>
#include <Context.h>
#include <PlatformIo.h>
#include <MathUtils.h>
#include <AttribUtils.h>
#include <Error.h>

static
void reallocByteStringIfNeeded(StucAlloc *pAlloc,
                               ByteString *pByteString, int64_t bitOffset) {
	int64_t bitCount = ((pByteString->byteIdx) * 8l) + pByteString->nextBitIdx;
	STUC_ASSERT("", bitCount <= pByteString->size * 8l);
	bitCount += bitOffset;
	int64_t byteCount = bitCount / 8l + (bitCount % 8l != 0l);
	if (byteCount >= pByteString->size) {
		int64_t oldSize = pByteString->size;
		pByteString->size *= 2l;
		pByteString->pString = pAlloc->pRealloc(pByteString->pString, pByteString->size);
		memset(pByteString->pString + oldSize, 0, pByteString->size - oldSize);
	}
}

void encodeValue(StucAlloc *pAlloc, ByteString *pByteString,
                 uint8_t *pValue, int32_t lengthInBits) {
	reallocByteStringIfNeeded(pAlloc, pByteString, lengthInBits);
	uint8_t valueBuf[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	int32_t lengthInBytes = lengthInBits / 8;
	lengthInBytes += (lengthInBits - lengthInBytes * 8) > 0;
	for (int32_t i = 1; i <= lengthInBytes; ++i) {
		valueBuf[i] = pValue[i - 1];
	}
	for (int32_t i = lengthInBytes - 1; i >= 1; --i) {
		valueBuf[i] <<= pByteString->nextBitIdx;
		uint8_t nextByteCopy = valueBuf[i - 1];
		nextByteCopy >>= 8 - pByteString->nextBitIdx;
		valueBuf[i] |= nextByteCopy;
	}
	int32_t writeUpTo = lengthInBytes + (pByteString->nextBitIdx > 0);
	for (int32_t i = 0; i < writeUpTo; ++i) {
		pByteString->pString[pByteString->byteIdx + i] |= valueBuf[i + 1];
	}
	pByteString->nextBitIdx = pByteString->nextBitIdx + lengthInBits;
	pByteString->byteIdx += pByteString->nextBitIdx / 8;
	pByteString->nextBitIdx %= 8;
}

void encodeString(StucAlloc *pAlloc, ByteString *pByteString, uint8_t *pString) {
	int32_t lengthInBits = (strlen((char *)pString) + 1) * 8;
	int32_t lengthInBytes = lengthInBits / 8;
	//+8 for potential padding
	reallocByteStringIfNeeded(pAlloc, pByteString, lengthInBits + 8l);
	if (pByteString->nextBitIdx != 0) {
		//pad to beginning of next byte
		pByteString->nextBitIdx = 0;
		pByteString->byteIdx++;
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		pByteString->pString[pByteString->byteIdx] = pString[i];
		pByteString->byteIdx++;
	}
}

void decodeValue(ByteString *pByteString, uint8_t *pValue, int32_t lengthInBits) {
	int32_t lengthInBytes = lengthInBits / 8;
	int32_t bitDifference = lengthInBits - lengthInBytes * 8;
	lengthInBytes += bitDifference > 0;
	uint8_t buf[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		buf[i] = pByteString->pString[pByteString->byteIdx + i];
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		buf[i] >>= pByteString->nextBitIdx;
		uint8_t nextByteCopy = buf[i + 1];
		nextByteCopy <<= 8 - pByteString->nextBitIdx;
		buf[i] |= nextByteCopy;
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		pValue[i] = buf[i];
	}
	uint8_t mask = UCHAR_MAX >> ((8 - bitDifference) % 8);
	pValue[lengthInBytes - 1] &= mask;
	pByteString->nextBitIdx = pByteString->nextBitIdx + lengthInBits;
	pByteString->byteIdx += pByteString->nextBitIdx / 8;
	pByteString->nextBitIdx %= 8;
}

void decodeString(ByteString *pByteString, char *pString, int32_t maxLen) {
	pByteString->byteIdx += pByteString->nextBitIdx > 0;
	uint8_t *dataPtr = pByteString->pString + pByteString->byteIdx;
	int32_t i = 0;
	for (; i < maxLen && dataPtr[i]; ++i) {
		pString[i] = dataPtr[i];
	}
	pString[i] = 0;
	pByteString->byteIdx += i + 1;
	pByteString->nextBitIdx = 0;
}

static
void encodeAttribs(StucAlloc *pAlloc, ByteString *pData,
                   AttribArray *pAttribs, int32_t dataLen) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		if (pAttribs->pArr[i].type == STUC_ATTRIB_STRING) {
			for (int32_t j = 0; j < dataLen; ++j) {
				void *pString = attribAsVoid(pAttribs->pArr + i, j);
				encodeString(pAlloc, pData, pString);
			}
		}
		else {
			int32_t attribSize = getAttribSize(pAttribs->pArr[i].type) * 8;
			for (int32_t j = 0; j < dataLen; ++j) {
				encodeValue(pAlloc, pData, attribAsVoid(pAttribs->pArr + i, j), attribSize);
			}
		}
	}
}

static
void encodeIndexedAttribs(StucAlloc *pAlloc, ByteString *pData,
                          AttribIndexedArr attribs) {
	for (int32_t i = 0; i < attribs.count; ++i) {
		AttribIndexed *pAttrib = attribs.pArr + i;
		if (pAttrib->type == STUC_ATTRIB_STRING) {
			for (int32_t j = 0; j < pAttrib->count; ++j) {
				void *pString = attribAsVoid(pAttrib, j);
				encodeString(pAlloc, pData, pString);
			}
		}
		else {
			int32_t attribSize = getAttribSize(pAttrib->type) * 8;
			for (int32_t j = 0; j < pAttrib->count; ++j) {
				encodeValue(pAlloc, pData, attribAsVoid(pAttrib, j), attribSize);
			}
		}
	}
}

static
void encodeAttribMeta(StucAlloc *pAlloc, ByteString *pData, AttribArray *pAttribs) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		encodeValue(pAlloc, pData, (uint8_t *)&pAttribs->pArr[i].type, 8);
		encodeValue(pAlloc, pData, (uint8_t *)&pAttribs->pArr[i].use, 8);
		encodeValue(pAlloc, pData, (uint8_t *)&pAttribs->pArr[i].interpolate, 1);
		encodeString(pAlloc, pData, (uint8_t *)pAttribs->pArr[i].name);
	}
}

static
void encodeIndexedAttribMeta(StucAlloc *pAlloc, ByteString *pData,
                             AttribIndexedArr attribs) {
	int64_t size = 0;
	for (int32_t i = 0; i < attribs.count; ++i) {
		encodeValue(pAlloc, pData, (uint8_t *)&attribs.pArr[i].type, 8, &size);
		encodeValue(pAlloc, pData, (uint8_t *)&attribs.pArr[i].use, 8, &size);
		encodeValue(pAlloc, pData, (uint8_t *)&attribs.pArr[i].count, 32, &size);
		encodeString(pAlloc, pData, (uint8_t *)attribs.pArr[i].name, &size);
	}
}

static
void encodeDataName(StucAlloc *pAlloc, ByteString *pByteString, char *pName) {
	//not using encodeString, as there's not need for a null terminator.
	//Only using 2 characters
	
	//ensure string is aligned with byte (we need to do this manually,
	//as encodeValue is being used instead of encodeString)
	if (pByteString->nextBitIdx != 0) {
		pByteString->nextBitIdx = 0;
		pByteString->byteIdx++;
	}
	encodeValue(pAlloc, pByteString, (uint8_t *)pName, 16);
}

static
StucResult encodeObj(StucAlloc *pAlloc, ByteString *pByteString, StucObject *pObj) {
	int64_t byteBase = pByteString->byteIdx;
	//encode obj header
	StucMesh *pMesh = pObj->pData;
	encodeDataName(pAlloc, pByteString, "OS"); //object start
	encodeDataName(pAlloc, pByteString, "XF"); //transform/ xform
	for (int32_t i = 0; i < 16; ++i) {
		int32_t x = i % 4;
		int32_t y = i / 4;
		encodeValue(pAlloc, pByteString, (uint8_t *)&pObj->transform.d[y][x], 32);
	}
	encodeDataName(pAlloc, pByteString, "OT"); //object type
	encodeValue(pAlloc, pByteString, (uint8_t *)&pObj->pData->type, 8);
	if (!checkIfMesh(pObj->pData)) {
		return STUC_SUCCESS;
	}
	encodeDataName(pAlloc, pByteString, "HD"); //header
	encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->meshAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->meshAttribs);
	encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->faceAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->faceAttribs);
	encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->cornerAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->cornerAttribs);
	encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->edgeAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->edgeAttribs);
	encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->vertAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->vertAttribs);
	encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->faceCount, 32);
	encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->cornerCount, 32);
	encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->edgeCount, 32);
	encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->vertCount, 32);
	//encode data
	encodeDataName(pAlloc, pByteString, "MA"); //mesh attribs
	encodeAttribs(pAlloc, pByteString, &pMesh->meshAttribs, 1);
	encodeDataName(pAlloc, pByteString, "FL"); //face list
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		STUC_ASSERT("", pMesh->pFaces[i] >= 0 &&
		                pMesh->pFaces[i] < pMesh->cornerCount);
		encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->pFaces[i], 32);
	}
	encodeDataName(pAlloc, pByteString, "FA"); //face attribs
	encodeAttribs(pAlloc, pByteString, &pMesh->faceAttribs, pMesh->faceCount);
	encodeDataName(pAlloc, pByteString, "LL"); //corner and edge lists
	for (int32_t i = 0; i < pMesh->cornerCount; ++i) {
		STUC_ASSERT("", pMesh->pCorners[i] >= 0 &&
		                pMesh->pCorners[i] < pMesh->vertCount);
		encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->pCorners[i], 32);
		STUC_ASSERT("", pMesh->pEdges[i] >= 0 &&
		                pMesh->pEdges[i] < pMesh->edgeCount);
		encodeValue(pAlloc, pByteString, (uint8_t *)&pMesh->pEdges[i], 32);
	}
	encodeDataName(pAlloc, pByteString, "LA"); //corner attribs
	encodeAttribs(pAlloc, pByteString, &pMesh->cornerAttribs, pMesh->cornerCount);
	encodeDataName(pAlloc, pByteString, "EA"); //edge attribs
	encodeAttribs(pAlloc, pByteString, &pMesh->edgeAttribs, pMesh->edgeCount);
	encodeDataName(pAlloc, pByteString, "VA"); //vert attribs
	encodeAttribs(pAlloc, pByteString, &pMesh->vertAttribs, pMesh->vertCount);
	encodeDataName(pAlloc, pByteString, "OE"); //object end
	return STUC_SUCCESS;
}

static
void addSpacing(ByteString *pByteString, int32_t lenInBits, int64_t *pSize) {
	int32_t lenInBytes = lenInBits / 8;
	pByteString->byteIdx += lenInBytes;
	pByteString->nextBitIdx = lenInBits - lenInBytes * 8;
	*pSize -= lenInBits;
}

static
int32_t addUniqToPtrArr(void *pPtr, int32_t *pCount, void **ppArr) {
	for (int32_t i = 0; i < *pCount; ++i) {
		if (pPtr == ppArr[i]) {
			return i;
		}
	}
	ppArr[*pCount] = pPtr;
	++*pCount;
	return *pCount - 1;
}

static
void getUniqueFlatCutoffs(StucContext pContext, int32_t usgCount,
                          StucUsg *pUsgArr, int32_t *pCutoffCount,
                          StucObject ***pppCutoffs, int32_t **ppIndices) {
	*ppIndices = pContext->alloc.pCalloc(usgCount, sizeof(int32_t));
	*pppCutoffs = pContext->alloc.pCalloc(usgCount, sizeof(void *));
	*pCutoffCount = 0;
	for (int32_t i = 0; i < usgCount; ++i) {
		if (!pUsgArr[i].pFlatCutoff) {
			continue;
		}
		(*ppIndices)[i] =
			addUniqToPtrArr(pUsgArr[i].pFlatCutoff, pCutoffCount, *pppCutoffs);
	}
	*pppCutoffs =
		pContext->alloc.pRealloc(*pppCutoffs, sizeof(void *) * *pCutoffCount);
}

static
int64_t estimateObjSize(StucObject *pObj) {
	//marking literal constants l here is pointless on windows,
	//i'm only doing it to avoid gcc/clang errors with Wall & Werror
	int64_t total = 0l;
	StucMesh *pMesh = pObj->pData;
	total += 4l * 16l; //transform
	total += 1l; //type
	total += 2l * 3l; //data names/ checks
	if (!checkIfMesh(pObj->pData)) {
		return total;
	}
	total += 2l * 9l; //data names/ checks

	total += (int64_t)pMesh->faceAttribs.count * (int64_t)pMesh->faceCount;
	total += (int64_t)pMesh->cornerAttribs.count * (int64_t)pMesh->cornerCount;
	total += (int64_t)pMesh->edgeAttribs.count * (int64_t)pMesh->edgeCount;
	total += (int64_t)pMesh->vertAttribs.count * (int64_t)pMesh->vertCount;

	total += 4l * (int64_t)pMesh->faceCount;
	total += 4l * (int64_t)pMesh->cornerCount;
	total += 4l * (int64_t)pMesh->cornerCount; //edge list

	return total;
}

static
int64_t estimateObjArrSize(int32_t count, StucObject *pObjArr) {
	int64_t total = 0l;
	for (int32_t i = 0; i < count; ++i) {
		total += estimateObjSize(pObjArr + i);
	}
	return total;
}

StucResult stucWriteStucFile(StucContext pContext, const char *pName,
                             int32_t objCount, StucObject *pObjArr,
                             int32_t usgCount, StucUsg *pUsgArr,
                             StucAttribIndexedArr indexedAttribs) {
	StucAlloc *pAlloc = &pContext->alloc;
	StucResult err = 0;
	ByteString header = {0};
	ByteString data = {0};

	int32_t *pCutoffIndices = NULL;
	StucObject **ppCutoffs = NULL;
	int32_t cutoffCount = 0;
	if (usgCount) {
		getUniqueFlatCutoffs(pContext, usgCount, pUsgArr, &cutoffCount, &ppCutoffs,
							 &pCutoffIndices);
	}
	data.size = estimateObjArrSize(objCount, pObjArr) +
	            estimateObjArrSize(usgCount, pUsgArr);
	data.pString = pContext->alloc.pCalloc(data.size, 1);
	if (indexedAttribs.count) {
		encodeIndexedAttribMeta(pAlloc, &data, indexedAttribs);
		encodeIndexedAttribs(pAlloc, &data, indexedAttribs);
	}
	for (int32_t i = 0; i < objCount; ++i) {
		err = encodeObj(pAlloc, &data, pObjArr + i);
		if (err != STUC_SUCCESS) {
			return err;
		}
	}
	for (int32_t i = 0; i < cutoffCount; ++i) {
		err = encodeObj(pAlloc, &data, ppCutoffs[i]);
		if (err != STUC_SUCCESS) {
			return err;
		}
	}
	for (int32_t i = 0; i < usgCount; ++i) {
		err = encodeObj(pAlloc, &data, &pUsgArr[i].obj);
		if (err != STUC_SUCCESS) {
			return err;
		}
		bool hasFlatCutoff = pUsgArr[i].pFlatCutoff != NULL;
		int64_t fcHeaderSize = STUC_FLAT_CUTOFF_HEADER_SIZE;
		encodeDataName(pAlloc, &data, "FC", &fcHeaderSize); //flatten cut-off
		encodeValue(pAlloc, &data, (uint8_t *)&hasFlatCutoff, 8, &fcHeaderSize);
		if (hasFlatCutoff) {
			encodeValue(pAlloc, &data, (uint8_t *)&pCutoffIndices[i], 32, &fcHeaderSize);
		}
	}
	//compress data
	//TODO convert to use proper zlib inflate and deflate calls
	//compress and decompress are not context independent iirc
	int64_t dataSize = data.byteIdx + (data.nextBitIdx > 0l);
	uint64_t uCompressedDataSize = (uint64_t)(dataSize * 1.01l + 12l); //zlib needs some padding
	uint8_t *compressedData = pContext->alloc.pMalloc(uCompressedDataSize);
	int32_t zResult = compress(compressedData, &uCompressedDataSize, data.pString, dataSize);
	switch(zResult) {
		case Z_OK:
			printf("Successfully compressed STUC data\n");
			break;
		case Z_MEM_ERROR:
			printf("Failed to compress STUC data, memory error\n");
			break;
		case Z_BUF_ERROR:
			printf("Failed to compress STUC data, output buffer too small\n");
			break;
	}
	int64_t compressedDataSize = (int64_t)uCompressedDataSize;
	printf("Compressed data is %lu long\n", compressedDataSize);

	//encode header
	const char *format = "UV Stucco Map File";
	int32_t formatLen = strnlen(format, MAP_FORMAT_NAME_MAX_LEN);
	STUC_ASSERT("", formatLen < MAP_FORMAT_NAME_MAX_LEN)
	header.size = 8l * ((int64_t)formatLen + 1l) +
	              16l + //version
	              64l + //compressed data size
	              64l + //uncompressed data size
                  32l + //indexed attrib count
	              32l + //obj count
	              32l + //usg count
	              32l;  //flatten cutoff count
	header.size = header.size / 8l + (header.size % 8l != 0l);
	header.pString = pContext->alloc.pCalloc(header.size, 1);
	encodeString(pAlloc, &header, (uint8_t *)format);
	int32_t version = STUC_MAP_VERSION;
	encodeValue(pAlloc, &header, (uint8_t *)&version, 16);
	encodeValue(pAlloc, &header, (uint8_t *)&compressedDataSize, 64);
	encodeValue(pAlloc, &header, (uint8_t *)&dataSize, 64);
	encodeValue(pAlloc, &header, (uint8_t *)&indexedAttribs.count, 32);
	encodeValue(pAlloc, &header, (uint8_t *)&objCount, 32);
	encodeValue(pAlloc, &header, (uint8_t *)&usgCount, 32);
	encodeValue(pAlloc, &header, (uint8_t *)&cutoffCount, 32);

	//TODO CRC for uncompressed data
	
	void *pFile;
	pContext->io.pOpen(&pFile, pName, 0, &pContext->alloc);
	int64_t finalHeaderLen = header.byteIdx + (header.nextBitIdx > 0);
	pContext->io.pWrite(pFile, (uint8_t *)&finalHeaderLen, 2);
	pContext->io.pWrite(pFile, header.pString, finalHeaderLen);
	pContext->io.pWrite(pFile, compressedData, (int32_t)compressedDataSize);
	pContext->io.pClose(pFile);

	pContext->alloc.pFree(header.pString);
	pContext->alloc.pFree(data.pString);

	printf("Finished STUC export\n");
	return STUC_SUCCESS;
}

static
StucResult decodeAttribMeta(ByteString *pData, AttribArray *pAttribs) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		decodeValue(pData, (uint8_t *)&pAttribs->pArr[i].type, 8);
		decodeValue(pData, (uint8_t *)&pAttribs->pArr[i].use, 8);
		decodeValue(pData, (uint8_t *)&pAttribs->pArr[i].interpolate, 1);
		int32_t maxNameLen = sizeof(pAttribs->pArr[i].name);
		decodeString(pData, (char *)pAttribs->pArr[i].name, maxNameLen);
		for (int32_t j = 0; j < i; ++j) {
			if (!strncmp(pAttribs->pArr[i].name, pAttribs->pArr[j].name,
			    STUC_ATTRIB_NAME_MAX_LEN)) {

				//dup
				return STUC_ERROR;
			}
		}
	}
	return STUC_SUCCESS;
}

static
StucResult decodeIndexedAttribMeta(ByteString *pData, AttribIndexedArr *pAttribs) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		decodeValue(pData, (uint8_t *)&pAttribs->pArr[i].type, 16);
		decodeValue(pData, (uint8_t *)&pAttribs->pArr[i].count, 32);
		int32_t maxNameLen = sizeof(pAttribs->pArr[i].name);
		decodeString(pData, (char *)pAttribs->pArr[i].name, maxNameLen);
		for (int32_t j = 0; j < i; ++j) {
			if (!strncmp(pAttribs->pArr[i].name, pAttribs->pArr[j].name,
				STUC_ATTRIB_NAME_MAX_LEN)) {

				//dup
				return STUC_ERROR;
			}
		}
	}
	return STUC_SUCCESS;
}

static void decodeAttribs(StucContext pContext, ByteString *pData,
                          AttribArray *pAttribs, int32_t dataLen) {
	stageBeginWrap(pContext, "", pAttribs->count * dataLen);
	const char stageName[] = "Deconding attrib ";
	char stageBuf[STUC_STAGE_NAME_LEN] = {0};
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		Attrib* pAttrib = pAttribs->pArr + i;
		memcpy(stageBuf, stageName, sizeof(stageName));
		setStageName(pContext, strncat(stageBuf, pAttrib->name, STUC_STAGE_NAME_LEN - sizeof(stageName)));
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData = dataLen ?
			pContext->alloc.pCalloc(dataLen, attribSize) : NULL;
		attribSize *= 8;
		int32_t progressBase = i * pAttribs->count * dataLen;
		for (int32_t j = 0; j < dataLen; ++j) {
			void *pAttribData = attribAsVoid(pAttrib, j);
			if (pAttribs->pArr[i].type == STUC_ATTRIB_STRING) {
				decodeString(pData, pAttribData, attribSize);
			}
			else {
				decodeValue(pData, pAttribData, attribSize);
			}
			stageProgressWrap(pContext, j + progressBase);
		}
		memset(stageBuf, 0, STUC_STAGE_NAME_LEN);
	}
	stageEndWrap(pContext);
}

static
void decodeIndexedAttribs(StucContext pContext, ByteString *pData,
                          AttribIndexedArr *pAttribs) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		AttribIndexed* pAttrib = pAttribs->pArr + i;
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData = pAttrib->count ?
			pContext->alloc.pCalloc(pAttrib->count, attribSize) : NULL;
		attribSize *= 8;
		for (int32_t j = 0; j < pAttrib->count; ++j) {
			void *pAttribData = attribAsVoid(pAttrib, j);
			if (pAttribs->pArr[i].type == STUC_ATTRIB_STRING) {
				decodeString(pData, pAttribData, attribSize);
			}
			else {
				decodeValue(pData, pAttribData, attribSize);
			}
		}
	}
}

static StucHeader decodeStucHeader(StucContext pContext, ByteString *headerByteString,
                                   AttribIndexedArr *pIndexedAttribs) {
	StucHeader header = {0};
	decodeString(headerByteString, (uint8_t*)&header.format, MAP_FORMAT_NAME_MAX_LEN);
	decodeValue(headerByteString, (uint8_t *)&header.version, 16);
	decodeValue(headerByteString, (uint8_t *)&header.dataSizeCompressed, 64);;
	decodeValue(headerByteString, (uint8_t *)&header.dataSize, 64);
	decodeValue(headerByteString, (uint8_t *)&pIndexedAttribs->count, 32);
	decodeValue(headerByteString, (uint8_t *)&header.objCount, 32);
	decodeValue(headerByteString, (uint8_t *)&header.usgCount, 32);
	decodeValue(headerByteString, (uint8_t *)&header.flatCutoffCount, 32);

	return header;
}

static
StucResult isDataNameInvalid(ByteString *pByteString, char *pName) {
	//ensure string is aligned with byte (we need to do this manually,
	//as decodeValue is being used instead of decodeString, given there's
	//only 2 characters)
	pByteString->byteIdx += pByteString->nextBitIdx > 0;
	pByteString->nextBitIdx = 0;
	char dataName[2] = {0};
	decodeValue(pByteString, (uint8_t *)&dataName, 16);
	if (dataName[0] != pName[0] || dataName[1] != pName[1]) {
		return STUC_ERROR;
	}
	else {
		return STUC_SUCCESS;
	}
}

static
StucResult loadObj(StucContext pContext, StucObject *pObj, ByteString *pByteString, bool usesUsg) {
	createMesh(pContext, pObj, STUC_OBJECT_DATA_MESH_INTERN);
	StucMesh *pMesh = pObj->pData;

	StucResult err = STUC_NOT_SET;

	err = isDataNameInvalid(pByteString, "OS"); //transform/ xform and type
	STUC_ERROR("Data name did not match 'OS'", err);
	err = isDataNameInvalid(pByteString, "XF"); //transform/ xform and type
	STUC_ERROR("Data name did not match 'XF'", err);
	for (int32_t i = 0; i < 16; ++i) {
		int32_t x = i % 4;
		int32_t y = i / 4;
		decodeValue(pByteString, (uint8_t *)&pObj->transform.d[y][x], 32);
	}
	err = isDataNameInvalid(pByteString, "OT"); //object type
	STUC_ERROR("Data name did not match 'OT'", err);
	decodeValue(pByteString, (uint8_t *)&pObj->pData->type, 8);
	if (!checkIfMesh(pObj->pData)) {
		err = STUC_ERROR;
		STUC_ERROR("Object is not a mesh", err);
	}
	err = isDataNameInvalid(pByteString, "HD"); //header
	STUC_ERROR("Data name did not match 'HD'", err);
	decodeValue(pByteString, (uint8_t *)&pMesh->meshAttribs.count, 32);
	pMesh->meshAttribs.pArr = pMesh->meshAttribs.count ?
		pContext->alloc.pCalloc(pMesh->meshAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->meshAttribs);
	STUC_ERROR("Failed to decode mesh attrib meta", err);

	decodeValue(pByteString, (uint8_t *)&pMesh->faceAttribs.count, 32);
	pMesh->faceAttribs.pArr = pMesh->faceAttribs.count ?
		pContext->alloc.pCalloc(pMesh->faceAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->faceAttribs);
	STUC_ERROR("Failed to decode face attrib meta", err);

	decodeValue(pByteString, (uint8_t *)&pMesh->cornerAttribs.count, 32);
	pMesh->cornerAttribs.pArr = pMesh->cornerAttribs.count ?
		pContext->alloc.pCalloc(pMesh->cornerAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->cornerAttribs);
	STUC_ERROR("Failed to decode corner attrib meta", err);

	decodeValue(pByteString, (uint8_t *)&pMesh->edgeAttribs.count, 32);
	pMesh->edgeAttribs.pArr = pMesh->edgeAttribs.count ?
		pContext->alloc.pCalloc(pMesh->edgeAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->edgeAttribs);
	STUC_ERROR("Failed to decode edge meta", err);

	decodeValue(pByteString, (uint8_t *)&pMesh->vertAttribs.count, 32);
	pMesh->vertAttribs.pArr = pMesh->vertAttribs.count ?
		pContext->alloc.pCalloc(pMesh->vertAttribs.count + usesUsg, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->vertAttribs);
	STUC_ERROR("Failed to decode vert attrib meta", err);

	decodeValue(pByteString, (uint8_t *)&pMesh->faceCount, 32);
	decodeValue(pByteString, (uint8_t *)&pMesh->cornerCount, 32);
	decodeValue(pByteString, (uint8_t *)&pMesh->edgeCount, 32);
	decodeValue(pByteString, (uint8_t *)&pMesh->vertCount, 32);

	//set usg attrib metadata if used
	if (usesUsg) {
		Attrib *usgAttrib = pMesh->vertAttribs.pArr + pMesh->vertAttribs.count;
		usgAttrib->pData = pContext->alloc.pCalloc(pMesh->vertCount, sizeof(int32_t));
		strncpy(usgAttrib->name, "StucUsg", STUC_ATTRIB_NAME_MAX_LEN);
		usgAttrib->origin = STUC_ATTRIB_ORIGIN_MAP;
		usgAttrib->interpolate = true;
		usgAttrib->type = STUC_ATTRIB_I32;
	}
	err = isDataNameInvalid(pByteString, "MA"); //mesh attribs
	STUC_ERROR("Data name did not match 'MA'", err);
	decodeAttribs(pContext, pByteString, &pMesh->meshAttribs, 1);
	stageEndWrap(pContext);
	err = isDataNameInvalid(pByteString, "FL"); //face list
	STUC_ERROR("Data name did not match 'FL'", err);
	pMesh->pFaces = pContext->alloc.pCalloc(pMesh->faceCount + 1, sizeof(int32_t));
	stageBeginWrap(pContext, "Decoding faces", pMesh->faceCount);
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		decodeValue(pByteString, (uint8_t *)&pMesh->pFaces[i], 32);
		STUC_ASSERT("", pMesh->pFaces[i] >= 0 &&
		                pMesh->pFaces[i] < pMesh->cornerCount);
		stageProgressWrap(pContext, i);
	}
	stageEndWrap(pContext);
	err = isDataNameInvalid(pByteString, "FA"); //face attribs
	STUC_ERROR("Data name did not match 'FA'", err);
	pMesh->pFaces[pMesh->faceCount] = pMesh->cornerCount;
	decodeAttribs(pContext, pByteString, &pMesh->faceAttribs, pMesh->faceCount);

	err = isDataNameInvalid(pByteString, "LL"); //corner and edge lists
	STUC_ERROR("Data name did not match 'LL'", err);
	pMesh->pCorners = pContext->alloc.pCalloc(pMesh->cornerCount, sizeof(int32_t));
	pMesh->pEdges = pContext->alloc.pCalloc(pMesh->cornerCount, sizeof(int32_t));
	stageBeginWrap(pContext, "Decoding corners", pMesh->cornerCount);
	for (int32_t i = 0; i < pMesh->cornerCount; ++i) {
		decodeValue(pByteString, (uint8_t *)&pMesh->pCorners[i], 32);
		STUC_ASSERT("", pMesh->pCorners[i] >= 0 &&
		                pMesh->pCorners[i] < pMesh->vertCount);
		decodeValue(pByteString, (uint8_t *)&pMesh->pEdges[i], 32);
		STUC_ASSERT("", pMesh->pEdges[i] >= 0 &&
		                pMesh->pEdges[i] < pMesh->edgeCount);
		stageProgressWrap(pContext, i);
	}
	stageEndWrap(pContext);

	err = isDataNameInvalid(pByteString, "LA"); //corner attribs
	STUC_ERROR("Data name did not match 'LA'", err);
	decodeAttribs(pContext, pByteString, &pMesh->cornerAttribs, pMesh->cornerCount);
	err = isDataNameInvalid(pByteString, "EA"); //edge attribs
	STUC_ERROR("Data name did not match 'EA'", err);
	decodeAttribs(pContext, pByteString, &pMesh->edgeAttribs, pMesh->edgeCount);
	err = isDataNameInvalid(pByteString, "VA"); //vert attribs
	STUC_ERROR("Data name did not match 'VA'", err);
	decodeAttribs(pContext, pByteString, &pMesh->vertAttribs, pMesh->vertCount);

	err = isDataNameInvalid(pByteString, "OE"); //obj end
	STUC_ERROR("Data name did not match 'OE'", err);
	if (usesUsg) {
		pMesh->vertAttribs.count++;
	}
	//TODO add STUC_ERROR and STUC_CATCH to all functions that return StucResult
	STUC_CATCH(err,
		//if error:
		stucMeshDestroy(pContext, pMesh);
		pContext->alloc.pFree(pMesh);
	);
	return err;
}

static
StucResult decodeStucData(StucContext pContext, StucHeader *pHeader,
                          ByteString *dataByteString, StucObject **ppObjArr,
                          StucUsg **ppUsgArr, StucObject **ppFlatCutoffArr,
                          bool forEdit, AttribIndexedArr *pIndexedAttribs) {
	StucResult status = STUC_NOT_SET;
	if (pIndexedAttribs && pIndexedAttribs->count) {
		STUC_ASSERT("", pIndexedAttribs->count > 0);
		pIndexedAttribs->pArr =
			pContext->alloc.pCalloc(pIndexedAttribs->count, sizeof(AttribIndexed));
		pIndexedAttribs->size = pIndexedAttribs->count;
		decodeIndexedAttribMeta(dataByteString, pIndexedAttribs);
		decodeIndexedAttribs(pContext, dataByteString, pIndexedAttribs);
	}
	if (pHeader->objCount) {
		*ppObjArr = pContext->alloc.pCalloc(pHeader->objCount, sizeof(StucObject));
		STUC_ASSERT("", pHeader->usgCount >= 0);
		bool usesUsg = pHeader->usgCount > 0 && !forEdit;
		for (int32_t i = 0; i < pHeader->objCount; ++i) {
			//usgUsg is passed here to indicate that an extra vert
			//attrib should be created. This would be used later to mark a verts
			//respective usg.
			status = loadObj(pContext, *ppObjArr + i, dataByteString, usesUsg);
			if (status != STUC_SUCCESS) {
				return status;
			}
		}
	}
	else {
		return STUC_ERROR;
	}

	if (pHeader->usgCount) {
		*ppUsgArr = pContext->alloc.pCalloc(pHeader->usgCount, sizeof(StucUsg));
		*ppFlatCutoffArr = pContext->alloc.pCalloc(pHeader->flatCutoffCount, sizeof(StucObject));
		for (int32_t i = 0; i < pHeader->flatCutoffCount; ++i) {
			status = loadObj(pContext, *ppFlatCutoffArr + i, dataByteString, false);
			if (status != STUC_SUCCESS) {
				return status;
			}
		}
		for (int32_t i = 0; i < pHeader->usgCount; ++i) {
			//usgs themselves don't need a usg attrib, so false is passed
			status = loadObj(pContext, &(*ppUsgArr)[i].obj, dataByteString, false);
			if (status != STUC_SUCCESS) {
				return status;
			}
			status = isDataNameInvalid(dataByteString, "FC");
			if (status != STUC_SUCCESS) {
				return status;
			}
			bool hasFlatCutoff = false;
			decodeValue(dataByteString, (uint8_t *)&hasFlatCutoff, 8);
			if (hasFlatCutoff) {
				int32_t cutoffIdx = 0;
				decodeValue(dataByteString, (uint8_t *)&cutoffIdx, 32);
				STUC_ASSERT("", cutoffIdx >= 0 &&
								cutoffIdx < pHeader->flatCutoffCount);
				(*ppUsgArr)[i].pFlatCutoff = *ppFlatCutoffArr + cutoffIdx;
			}
		}
	}
	return STUC_SUCCESS;
}

StucResult stucLoadStucFile(StucContext pContext, char *filePath,
                            int32_t *pObjCount, StucObject **ppObjArr,
                            int32_t *pUsgCount, StucUsg **ppUsgArr,
	                        int32_t *pFlatCutoffCount, StucObject **ppFlatCutoffArr,
                            bool forEdit, StucAttribIndexedArr *pIndexedAttribs) {
	StucResult status = STUC_NOT_SET;
	ByteString headerByteString = {0};
	ByteString dataByteString = {0};
	void *pFile;
	printf("Loading STUC file: %s\n", filePath);
	pContext->io.pOpen(&pFile, filePath, 1, &pContext->alloc);
	int16_t headerSize = 0;
	pContext->io.pRead(pFile, (uint8_t *)&headerSize, 2);
	printf("Stuc File Header Size: %d\n", headerSize);
	printf("Header is %d bytes\n", headerSize);
	headerByteString.pString = pContext->alloc.pMalloc(headerSize);
	printf("Reading header\n");
	pContext->io.pRead(pFile, headerByteString.pString, headerSize);
	printf("Decoding header\n");
	StucHeader header = decodeStucHeader(pContext, &headerByteString,
	                                     pIndexedAttribs);
	if (strncmp(header.format,  "UV Stucco Map File", MAP_FORMAT_NAME_MAX_LEN) ||
		header.version != STUC_MAP_VERSION) {
		return STUC_ERROR;
	}
	uint8_t *dataByteStringRaw = pContext->alloc.pMalloc(header.dataSize);
	unsigned long dataSizeUncompressed = header.dataSize;
	printf("Reading data\n");
	pContext->io.pRead(pFile, dataByteStringRaw, header.dataSizeCompressed);
	pContext->io.pClose(pFile);
	dataByteString.pString = pContext->alloc.pMalloc(header.dataSize);
	printf("Decompressing data\n");
	z_streamp stream;
	uint64_t size;
	int32_t zResult = uncompress(dataByteString.pString, &dataSizeUncompressed,
			                     dataByteStringRaw, header.dataSizeCompressed);
	pContext->alloc.pFree(dataByteStringRaw);
	switch(zResult) {
		case Z_OK:
			printf("Successfully decompressed STUC file data\n");
			break;
		case Z_MEM_ERROR:
			printf("Failed to decompress STUC file data. Memory error\n");
			break;
		case Z_BUF_ERROR:
			printf("Failed to decompress STUC file data. Buffer was too small\n");
			break;
	}
	if (dataSizeUncompressed != header.dataSize) {
		printf("Failed to load STUC file. Decompressed data size doesn't match header description\n");
		return STUC_ERROR;
	}
	printf("Decoding data\n");
	status = decodeStucData(pContext, &header, &dataByteString, ppObjArr, ppUsgArr,
	                        ppFlatCutoffArr, forEdit, pIndexedAttribs);
	if (status != STUC_SUCCESS) {
		return status;
	}
	pContext->alloc.pFree(headerByteString.pString);
	pContext->alloc.pFree(dataByteString.pString);
	*pObjCount = header.objCount;
	*pUsgCount = header.usgCount;
	*pFlatCutoffCount = header.flatCutoffCount;
	return STUC_SUCCESS;
}

void stucIoSetCustom(StucContext pContext, StucIo *pIo) {
	if (!pIo->pOpen || !pIo->pClose || !pIo->pWrite || !pIo->pRead) {
		printf("Failed to set custom IO. One or more functions were NULL");
		abort();
	}
	pContext->io = *pIo;
}

void stucIoSetDefault(StucContext pContext) {
	pContext->io.pOpen = stucPlatformFileOpen;
	pContext->io.pClose = stucPlatformFileClose;
	pContext->io.pWrite = stucPlatformFileWrite;
    pContext->io.pRead = stucPlatformFileRead;
}
