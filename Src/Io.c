#define VERT_ATTRIBUTE_AMOUNT 3
#define LOOP_ATTRIBUTE_AMOUNT 3
#define ENCODE_DECODE_BUFFER_LENGTH 34
#define MAP_FORMAT_NAME_MAX_LEN 14

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

//TODO add info to header to identify file as an ruvm,
//to prevent accidentally trying to load a different format

typedef struct {
	unsigned char *pString;
	int32_t nextBitIndex;
	int32_t byteIndex;
} ByteString;

static int32_t decodeSingleBit(ByteString *byteString) {
	int32_t value = byteString->pString[byteString->byteIndex];
	value >>= byteString->nextBitIndex;
	value &= 1;
	byteString->nextBitIndex++;
	byteString->byteIndex += byteString->nextBitIndex >= 8;
	byteString->nextBitIndex %= 8;
	return value;
}

static void encodeValue(ByteString *byteString, uint8_t *value,
                        int32_t lengthInBits, int64_t *pSize) {
	uint8_t valueBuffer[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	int32_t lengthInBytes = lengthInBits / 8;
	lengthInBytes += (lengthInBits - lengthInBytes * 8) > 0;
	for (int32_t i = 1; i <= lengthInBytes; ++i) {
		valueBuffer[i] = value[i - 1];
	}
	for (int32_t i = lengthInBytes - 1; i >= 1; --i) {
		valueBuffer[i] <<= byteString->nextBitIndex;
		uint8_t nextByteCopy = valueBuffer[i - 1];
		nextByteCopy >>= 8 - byteString->nextBitIndex;
		valueBuffer[i] |= nextByteCopy;
	}
	int32_t writeUpTo = lengthInBytes + (byteString->nextBitIndex > 0);
	for (int32_t i = 0; i < writeUpTo; ++i) {
		byteString->pString[byteString->byteIndex + i] |= valueBuffer[i + 1];
	}
	byteString->nextBitIndex = byteString->nextBitIndex + lengthInBits;
	byteString->byteIndex += byteString->nextBitIndex / 8;
	byteString->nextBitIndex %= 8;
	*pSize -= lengthInBits;
}

static void encodeString(ByteString *byteString,
                         uint8_t *string, int64_t *pSize) {
	int32_t lengthInBits = (strlen((char *)string) + 1) * 8;
	int32_t lengthInBytes = lengthInBits / 8;
	byteString->byteIndex += byteString->nextBitIndex > 0;
	byteString->nextBitIndex = 0;
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		byteString->pString[byteString->byteIndex] = string[i];
		byteString->byteIndex++;
	}
	*pSize -= lengthInBits;
}

static void decodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits) {
	int32_t lengthInBytes = lengthInBits / 8;
	int32_t bitDifference = lengthInBits - lengthInBytes * 8;
	lengthInBytes += bitDifference > 0;
	uint8_t buffer[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		buffer[i] = byteString->pString[byteString->byteIndex + i];
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		buffer[i] >>= byteString->nextBitIndex;
		uint8_t nextByteCopy = buffer[i + 1];
		nextByteCopy <<= 8 - byteString->nextBitIndex;
		buffer[i] |= nextByteCopy;
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		value[i] = buffer[i];
	}
	uint8_t mask = UCHAR_MAX >> ((8 - bitDifference) % 8);
	value[lengthInBytes - 1] &= mask;
	byteString->nextBitIndex = byteString->nextBitIndex + lengthInBits;
	byteString->byteIndex += byteString->nextBitIndex / 8;
	byteString->nextBitIndex %= 8;
}

static void decodeString(ByteString *byteString, char *string, int32_t maxLen) {
	byteString->byteIndex += byteString->nextBitIndex > 0;
	uint8_t *dataPtr = byteString->pString + byteString->byteIndex;
	int32_t i = 0;
	for (; i < maxLen && dataPtr[i]; ++i) {
		string[i] = dataPtr[i];
	}
	string[i] = 0;
	byteString->byteIndex += i + 1;
	byteString->nextBitIndex = 0;
}

static int32_t getTotalAttribSize(AttribArray *pAttribs) {
	int32_t totalSize = 0;
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		totalSize += getAttribSize(pAttribs->pArr[i].type) * 8;
	}
	return totalSize;
}

static int32_t getTotalAttribMetaSize(AttribArray *pAttribs) {
	int32_t totalSize = pAttribs->count * 16;
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		totalSize += (strlen(pAttribs->pArr[i].name) + 1) * 8;
	}
	return totalSize;
}

static
void encodeAttribs(ByteString *pData, AttribArray *pAttribs,
                   int32_t dataLen, int64_t *pSize) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		if (pAttribs->pArr[i].type == RUVM_ATTRIB_STRING) {
			for (int32_t j = 0; j < dataLen; ++j) {
				void *pString = attribAsVoid(pAttribs->pArr + i, j);
				encodeString(pData, pString, pSize);
			}
		}
		else {
			int32_t attribSize = getAttribSize(pAttribs->pArr[i].type) * 8;
			for (int32_t j = 0; j < dataLen; ++j) {
				encodeValue(pData, attribAsVoid(pAttribs->pArr + i, j),
				            attribSize, pSize);
			}
		}
	}
}

static
void encodeAttribMeta(ByteString *pData,
                      AttribArray *pAttribs, int64_t *pSize) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		encodeValue(pData, (uint8_t *)&pAttribs->pArr[i].type, 16, pSize);
		encodeString(pData, (uint8_t *)pAttribs->pArr[i].name, pSize);
	}
}

typedef struct {
	int64_t transform;
	int64_t type;
	int64_t dataNames;
	int64_t attribCounts;
	int64_t meshAttribMeta;
	int64_t faceAttribMeta;
	int64_t loopAttribMeta;
	int64_t edgeAttribMeta;
	int64_t vertAttribMeta;
	int64_t meshAttribs;
	int64_t faceAttribs;
	int64_t loopAttribs;
	int64_t edgeAttribs;
	int64_t vertAttribs;
	int64_t listCounts;
	int64_t faceList;
	int64_t loopList;
	int64_t edgeList;
} MeshSizeInBits;

static getObjDataSize(RuvmObject *pObj,
                      int32_t *pDataSizeInBits, MeshSizeInBits *pSize) {
	pSize->transform = 32 * 16;
	pSize->type = 8;
	pSize->dataNames = 16 * 3;
	if (!checkIfMesh(pObj->pData)) {
		return RUVM_SUCCESS;
	}
	RuvmMesh *pMesh = pObj->pData;
	pSize->dataNames += 16 * 9;

	//calculate total size of attribute header info
	pSize->meshAttribMeta = getTotalAttribMetaSize(&pMesh->meshAttribs);
	pSize->faceAttribMeta = getTotalAttribMetaSize(&pMesh->faceAttribs);
	pSize->loopAttribMeta = getTotalAttribMetaSize(&pMesh->loopAttribs);
	pSize->edgeAttribMeta = getTotalAttribMetaSize(&pMesh->edgeAttribs);
	pSize->vertAttribMeta = getTotalAttribMetaSize(&pMesh->vertAttribs);

	pSize->attribCounts = 32 + //mesh attrib count
	                    32 + //face attrib count
	                    32 + //loop attrib count
	                    32 + //edge attrib count
	                    32; //vert attrib count
	pSize->listCounts = 32 + //face count
	                  32 + //loop count
	                  32 + //edge count
	                  32;  //vert count

	//calculate total size of attribute data
	int64_t meshAttribSize = getTotalAttribSize(&pMesh->meshAttribs);
	int64_t faceAttribSize = getTotalAttribSize(&pMesh->faceAttribs);
	int64_t loopAttribSize = getTotalAttribSize(&pMesh->loopAttribs);
	int64_t edgeAttribSize = getTotalAttribSize(&pMesh->edgeAttribs);
	int64_t vertAttribSize = getTotalAttribSize(&pMesh->vertAttribs);

	pSize->meshAttribs = meshAttribSize;
	pSize->faceAttribs = faceAttribSize * pMesh->faceCount;
	pSize->loopAttribs = loopAttribSize * pMesh->loopCount;
	pSize->edgeAttribs = edgeAttribSize * pMesh->edgeCount;
	pSize->vertAttribs = vertAttribSize * pMesh->vertCount;

	pSize->faceList = 32 * (int64_t)pMesh->faceCount;
	pSize->loopList = 32 * (int64_t)pMesh->loopCount;
	pSize->edgeList = 32 * (int64_t)pMesh->loopCount;
	return RUVM_SUCCESS;
}

static
void encodeDataName(ByteString *pByteString, char *pName, int64_t *pSize) {
	//not using encodeString, as there's not need for a null terminator.
	//Only using 2 characters
	
	//ensure string is aligned with byte (we need to do this manually,
	//as encodeValue is being used instead of encodeString)
	pByteString->byteIndex += pByteString->nextBitIndex > 0;
	pByteString->nextBitIndex = 0;
	encodeValue(pByteString, (uint8_t *)pName, 16, pSize);
}

static
bool isSizeInvalid(int64_t size) {
	return size != 0;
}

static
RuvmResult encodeObj(ByteString *pByteString,
                     RuvmObject *pObj, MeshSizeInBits *pSize) {
	//encode obj header
	encodeDataName(pByteString, "OS", &pSize->dataNames); //object start
	encodeDataName(pByteString, "XF", &pSize->dataNames); //transform/ xform
	for (int32_t i = 0; i < 16; ++i) {
		int32_t x = i % 4;
		int32_t y = i / 4;
		encodeValue(pByteString, (uint8_t *)&pObj->transform.d[y][x], 32, &pSize->transform);
	}
	if (isSizeInvalid(pSize->transform)) {
		return RUVM_ERROR;
	}
	encodeDataName(pByteString, "OT", &pSize->dataNames); //object type
	encodeValue(pByteString, (uint8_t *)&pObj->pData->type, 8, &pSize->type);
	if (!checkIfMesh(pObj->pData)) {
		if (isSizeInvalid(pSize->dataNames)) {
			return RUVM_ERROR;
		}
		return RUVM_SUCCESS;
	}
	RuvmMesh *pMesh = pObj->pData;
	encodeDataName(pByteString, "HD", &pSize->dataNames); //header
	encodeValue(pByteString, (uint8_t *)&pMesh->meshAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->meshAttribs, &pSize->meshAttribMeta);
	if (isSizeInvalid(pSize->meshAttribMeta)) {
		return RUVM_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->faceAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->faceAttribs, &pSize->faceAttribMeta);
	if (isSizeInvalid(pSize->faceAttribMeta)) {
		return RUVM_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->loopAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->loopAttribs, &pSize->loopAttribMeta);
	if (isSizeInvalid(pSize->loopAttribMeta)) {
		return RUVM_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->edgeAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->edgeAttribs, &pSize->edgeAttribMeta);
	if (isSizeInvalid(pSize->edgeAttribMeta)) {
		return RUVM_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->vertAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->vertAttribs, &pSize->vertAttribMeta);
	if (isSizeInvalid(pSize->vertAttribMeta)) {
		return RUVM_ERROR;
	}
	if (isSizeInvalid(pSize->attribCounts)) {
		return RUVM_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->faceCount, 32, &pSize->listCounts);
	encodeValue(pByteString, (uint8_t *)&pMesh->loopCount, 32, &pSize->listCounts);
	encodeValue(pByteString, (uint8_t *)&pMesh->edgeCount, 32, &pSize->listCounts);
	encodeValue(pByteString, (uint8_t *)&pMesh->vertCount, 32, &pSize->listCounts);
	if (isSizeInvalid(pSize->listCounts)) {
		return RUVM_ERROR;
	}
	//encode data
	encodeDataName(pByteString, "MA", &pSize->dataNames); //mesh attribs
	encodeAttribs(pByteString, &pMesh->meshAttribs, 1, &pSize->meshAttribs);
	if (isSizeInvalid(pSize->meshAttribs)) {
		return RUVM_ERROR;
	}
	encodeDataName(pByteString, "FL", &pSize->dataNames); //face list
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		encodeValue(pByteString, (uint8_t *)&pMesh->pFaces[i], 32, &pSize->faceList);
	}
	if (isSizeInvalid(pSize->faceList)) {
		return RUVM_ERROR;
	}
	encodeDataName(pByteString, "FA", &pSize->dataNames); //face attribs
	encodeAttribs(pByteString, &pMesh->faceAttribs, pMesh->faceCount, &pSize->faceAttribs);
	if (isSizeInvalid(pSize->faceAttribs)) {
		return RUVM_ERROR;
	}
	encodeDataName(pByteString, "LL", &pSize->dataNames); //loop and edge lists
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		encodeValue(pByteString, (uint8_t *)&pMesh->pLoops[i], 32, &pSize->loopList);
		encodeValue(pByteString, (uint8_t *)&pMesh->pEdges[i], 32, &pSize->edgeList);
	}
	if (isSizeInvalid(pSize->loopList) || isSizeInvalid(pSize->edgeList)) {
		return RUVM_ERROR;
	}
	encodeDataName(pByteString, "LA", &pSize->dataNames); //loop attribs
	encodeAttribs(pByteString, &pMesh->loopAttribs, pMesh->loopCount, &pSize->loopAttribs);
	if (isSizeInvalid(pSize->loopAttribs)) {
		return RUVM_ERROR;
	}
	encodeDataName(pByteString, "EA", &pSize->dataNames); //edge attribs
	encodeAttribs(pByteString, &pMesh->edgeAttribs, pMesh->edgeCount, &pSize->edgeAttribs);
	if (isSizeInvalid(pSize->edgeAttribs)) {
		return RUVM_ERROR;
	}
	encodeDataName(pByteString, "VA", &pSize->dataNames); //vert attribs
	encodeAttribs(pByteString, &pMesh->vertAttribs, pMesh->vertCount, &pSize->vertAttribs);
	if (isSizeInvalid(pSize->vertAttribs)) {
		return RUVM_ERROR;
	}
	encodeDataName(pByteString, "OE", &pSize->dataNames); //object end
	if (isSizeInvalid(pSize->dataNames)) {
		return RUVM_ERROR;
	}
	return RUVM_SUCCESS;
}

static
int64_t sumOfMeshSize(MeshSizeInBits *pSize) {
	return pSize->transform +
	       pSize->type +
		   pSize->dataNames +
		   pSize->attribCounts +
		   pSize->meshAttribMeta +
		   pSize->faceAttribMeta +
		   pSize->loopAttribMeta +
		   pSize->edgeAttribMeta +
		   pSize->vertAttribMeta +
		   pSize->meshAttribs +
		   pSize->faceAttribs +
		   pSize->loopAttribs +
		   pSize->edgeAttribs +
		   pSize->vertAttribs +
		   pSize->listCounts +
		   pSize->faceList +
		   pSize->loopList +
		   pSize->edgeList;
}

static
void addSpacing(ByteString *pByteString, int32_t lenInBits, int64_t *pSize) {
	int32_t lenInBytes = lenInBits / 8;
	pByteString->byteIndex += lenInBytes;
	pByteString->nextBitIndex = lenInBits - lenInBytes * 8;
	*pSize -= lenInBits;
}

RuvmResult ruvmWriteRuvmFile(RuvmContext pContext, const char *pName,
                             int32_t objCount, RuvmObject *pObjArr,
                             int32_t usgCount, RuvmObject *pUsgArr) {
	RuvmResult err = 0;
	ByteString header = {0};
	ByteString data = {0};

	int64_t dataSizeInBits = 0;
	MeshSizeInBits *pMeshSizes =
		pContext->alloc.pCalloc(objCount + usgCount, sizeof(MeshSizeInBits));
	for (int32_t i = 0; i < objCount; ++i) {
		err = getObjDataSize(pObjArr + i, &dataSizeInBits, pMeshSizes + i);
		if (err != RUVM_SUCCESS) {
			return err;
		}
		dataSizeInBits += sumOfMeshSize(pMeshSizes + i);
	}
	for (int32_t i = 0; i < usgCount; ++i) {
		int32_t sizesIndex = objCount + i;
		err = getObjDataSize(pUsgArr + i, &dataSizeInBits, pMeshSizes + sizesIndex);
		if (err != RUVM_SUCCESS) {
			return err;
		}
		dataSizeInBits += sumOfMeshSize(pMeshSizes + sizesIndex);
	}
	int64_t dataSizeInBytes = dataSizeInBits / 8 + 2;
	data.byteIndex = 0;
	data.nextBitIndex = 0;
	data.pString = pContext->alloc.pCalloc(dataSizeInBytes, 1);
	for (int32_t i = 0; i < objCount; ++i) {
		err = encodeObj(&data, pObjArr + i, pMeshSizes + i);
		if (err != RUVM_SUCCESS) {
			return err;
		}
	}
	for (int32_t i = 0; i < usgCount; ++i) {
		int32_t sizesIndex = objCount + i;
		err = encodeObj(&data, pUsgArr + i, pMeshSizes + sizesIndex);
		if (err != RUVM_SUCCESS) {
			return err;
		}
	}
	pContext->alloc.pFree(pMeshSizes);

	//compress data
	//TODO convert to use proper zlib inflate and deflate calls
	//compress and decompress are not context independent iirc
	int64_t dataSize = data.byteIndex + (data.nextBitIndex > 0);
	int64_t dataSizeExtra = dataSize / 1000;
	dataSizeExtra += ((dataSize * 1000) - dataSize) > 0;
	dataSizeExtra += 12;
	unsigned long uCompressedDataSize = dataSize + dataSizeExtra;
	uint8_t *compressedData = pContext->alloc.pMalloc(uCompressedDataSize);
	int32_t zResult = compress(compressedData, &uCompressedDataSize, data.pString, dataSize);
	switch(zResult) {
		case Z_OK:
			printf("Successfully compressed RUVM data\n");
			break;
		case Z_MEM_ERROR:
			printf("Failed to compress RUVM data, memory error\n");
			break;
		case Z_BUF_ERROR:
			printf("Failed to compress RUVM data, output buffer too small\n");
			break;
	}
	int64_t compressedDataSize = uCompressedDataSize;
	printf("Compressed data is %lu long\n", compressedDataSize);

	//encode header
	const char *format = "RUVM Map File";
	int32_t formatLen = strnlen(format, MAP_FORMAT_NAME_MAX_LEN);
	RUVM_ASSERT("", formatLen < MAP_FORMAT_NAME_MAX_LEN)
	int64_t headerSizeInBits = 8 * (formatLen + 1) +
	                           16 + //version
	                           64 + //compressed data size
	                           64 + //uncompressed data size
	                           32 + //obj count
	                           32; //usg count
	int64_t headerSizeInBytes = headerSizeInBits / 8 + 2;
	header.pString = pContext->alloc.pCalloc(headerSizeInBytes, 1);
	encodeString(&header, (uint8_t *)format, &headerSizeInBits);
	int32_t version = 100;
	encodeValue(&header, (uint8_t *)&version, 16, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&compressedDataSize, 64, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&dataSize, 64, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&objCount, 32, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&usgCount, 32, &headerSizeInBits);
	if (isSizeInvalid(headerSizeInBits)) {
		return RUVM_ERROR;
	}

	//TODO CRC for uncompressed data
	
	void *pFile;
	pContext->io.pOpen(&pFile, pName, 0, &pContext->alloc);
	headerSizeInBytes = header.byteIndex + (header.nextBitIndex > 0);
	pContext->io.pWrite(pFile, (uint8_t *)&headerSizeInBytes, 2);
	pContext->io.pWrite(pFile, header.pString, headerSizeInBytes);
	pContext->io.pWrite(pFile, compressedData, (int32_t)compressedDataSize);
	pContext->io.pClose(pFile);

	pContext->alloc.pFree(header.pString);
	pContext->alloc.pFree(data.pString);

	printf("Finished RUVM export\n");
	return RUVM_SUCCESS;
}

static void decodeAttribMeta(ByteString *pData, AttribArray *pAttribs) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		decodeValue(pData, (uint8_t *)&pAttribs->pArr[i].type, 16);
		int32_t maxNameLen = sizeof(pAttribs->pArr[i].name);
		decodeString(pData, (char *)&pAttribs->pArr[i].name, maxNameLen);
	}
}

static void decodeAttribs(RuvmContext pContext, ByteString *pData,
                          AttribArray *pAttribs, int32_t dataLen) {
	stageBeginWrap(pContext, "", pAttribs->count * dataLen);
	const char stageName[] = "Deconding attrib ";
	char stageBuf[RUVM_STAGE_NAME_LEN] = {0};
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		Attrib* pAttrib = pAttribs->pArr + i;
		memcpy(stageBuf, stageName, sizeof(stageName));
		setStageName(pContext, strncat(stageBuf, pAttrib->name, RUVM_STAGE_NAME_LEN - sizeof(stageName)));
		int32_t attribSize = getAttribSize(pAttrib->type) * 8;
		pAttrib->pData = dataLen ?
			pContext->alloc.pCalloc(dataLen, attribSize) : NULL;
		int32_t progressBase = i * pAttribs->count * dataLen;
		for (int32_t j = 0; j < dataLen; ++j) {
			void *pAttribData = attribAsVoid(pAttrib, j);
			if (pAttribs->pArr[i].type == RUVM_ATTRIB_STRING) {
				decodeString(pData, pAttribData, attribSize);
			}
			else {
				decodeValue(pData, pAttribData, attribSize);
			}
			stageProgressWrap(pContext, j + progressBase);
		}
		memset(stageBuf, 0, RUVM_STAGE_NAME_LEN);
	}
	stageEndWrap(pContext);
}

static RuvmHeader decodeRuvmHeader(RuvmContext pContext, ByteString *headerByteString) {
	RuvmHeader header = {0};
	decodeString(headerByteString, (uint8_t*)&header.format, MAP_FORMAT_NAME_MAX_LEN);
	decodeValue(headerByteString, (uint8_t *)&header.version, 16);
	decodeValue(headerByteString, (uint8_t *)&header.dataSizeCompressed, 64);
	decodeValue(headerByteString, (uint8_t *)&header.dataSize, 64);
	decodeValue(headerByteString, (uint8_t *)&header.objCount, 32);
	decodeValue(headerByteString, (uint8_t *)&header.usgCount, 32);

	return header;
}

static
bool isDataNameInvalid(ByteString *pByteString, char *pName) {
	//ensure string is aligned with byte (we need to do this manually,
	//as decodeValue is being used instead of decodeString, given there's
	//only 2 characters)
	pByteString->byteIndex += pByteString->nextBitIndex > 0;
	pByteString->nextBitIndex = 0;
	char dataName[2] = {0};
	decodeValue(pByteString, (uint8_t *)&dataName, 16);
	return dataName[0] != pName[0] || dataName[1] != pName[1];
}

static
void loadObj(RuvmContext pContext, RuvmObject *pObj, ByteString *pByteString, bool usesUsg) {
	createMesh(pContext, pObj, RUVM_OBJECT_DATA_MESH_INTERN);
	RuvmMesh *pMesh = pObj->pData;

	if (isDataNameInvalid(pByteString, "OS")) { //transform/ xform and type
		return;
	}
	if (isDataNameInvalid(pByteString, "XF")) { //transform/ xform and type
		return;
	}
	for (int32_t i = 0; i < 16; ++i) {
		int32_t x = i % 4;
		int32_t y = i / 4;
		decodeValue(pByteString, (uint8_t *)&pObj->transform.d[y][x], 32);
	}
	if (isDataNameInvalid(pByteString, "OT")) { //object type
		return;
	}
	decodeValue(pByteString, (uint8_t *)&pObj->pData->type, 8);
	if (!checkIfMesh(pObj->pData)) {
		return;
	}

	if (isDataNameInvalid(pByteString, "HD")) { //header
		return;
	}
	decodeValue(pByteString, (uint8_t *)&pMesh->meshAttribs.count, 32);
	pMesh->meshAttribs.pArr = pMesh->meshAttribs.count ?
		pContext->alloc.pCalloc(pMesh->meshAttribs.count, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(pByteString, &pMesh->meshAttribs);

	decodeValue(pByteString, (uint8_t *)&pMesh->faceAttribs.count, 32);
	pMesh->faceAttribs.pArr = pMesh->faceAttribs.count ?
		pContext->alloc.pCalloc(pMesh->faceAttribs.count, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(pByteString, &pMesh->faceAttribs);

	decodeValue(pByteString, (uint8_t *)&pMesh->loopAttribs.count, 32);
	pMesh->loopAttribs.pArr = pMesh->loopAttribs.count ?
		pContext->alloc.pCalloc(pMesh->loopAttribs.count, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(pByteString, &pMesh->loopAttribs);

	decodeValue(pByteString, (uint8_t *)&pMesh->edgeAttribs.count, 32);
	pMesh->edgeAttribs.pArr = pMesh->edgeAttribs.count ?
		pContext->alloc.pCalloc(pMesh->edgeAttribs.count, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(pByteString, &pMesh->edgeAttribs);

	decodeValue(pByteString, (uint8_t *)&pMesh->vertAttribs.count, 32);
	pMesh->vertAttribs.pArr = pMesh->vertAttribs.count ?
		pContext->alloc.pCalloc(pMesh->vertAttribs.count + usesUsg, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(pByteString, &pMesh->vertAttribs);
	//set usg attrib metadata if used
	if (usesUsg) {
		Attrib *usgAttrib = pMesh->vertAttribs.pArr + pMesh->vertAttribs.count;
		strncpy(usgAttrib->name, "RuvmUsg", RUVM_ATTRIB_NAME_MAX_LEN);
		usgAttrib->origin = RUVM_ATTRIB_ORIGIN_MAP;
		usgAttrib->interpolate = true;
		usgAttrib->type = RUVM_ATTRIB_I32;
	}

	decodeValue(pByteString, (uint8_t *)&pMesh->faceCount, 32);
	decodeValue(pByteString, (uint8_t *)&pMesh->loopCount, 32);
	decodeValue(pByteString, (uint8_t *)&pMesh->edgeCount, 32);
	decodeValue(pByteString, (uint8_t *)&pMesh->vertCount, 32);

	//TODO add short headers (like 2 or 4 bytes) to the start of each
	//of these large blocks of data, to better catch corrupt files.
	//So one for faces, loops, edges, etc

	if (isDataNameInvalid(pByteString, "MA")) { //mesh attribs
		return;
	}
	decodeAttribs(pContext, pByteString, &pMesh->meshAttribs, 1);
	stageEndWrap(pContext);
	if (isDataNameInvalid(pByteString, "FL")) { //face list
		return;
	}
	pMesh->pFaces = pContext->alloc.pCalloc(pMesh->faceCount + 1, sizeof(int32_t));
	stageBeginWrap(pContext, "Decoding faces", pMesh->faceCount);
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		decodeValue(pByteString, (uint8_t *)&pMesh->pFaces[i], 32);
		stageProgressWrap(pContext, i);
	}
	stageEndWrap(pContext);
	if (isDataNameInvalid(pByteString, "FA")) { //face attribs
		return;
	}
	pMesh->pFaces[pMesh->faceCount] = pMesh->loopCount;
	decodeAttribs(pContext, pByteString, &pMesh->faceAttribs, pMesh->faceCount);

	if (isDataNameInvalid(pByteString, "LL")) { //loop and edge lists
		return;
	}
	pMesh->pLoops = pContext->alloc.pCalloc(pMesh->loopCount, sizeof(int32_t));
	pMesh->pEdges = pContext->alloc.pCalloc(pMesh->loopCount, sizeof(int32_t));
	stageBeginWrap(pContext, "Decoding loops", pMesh->loopCount);
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		decodeValue(pByteString, (uint8_t *)&pMesh->pLoops[i], 32);
		decodeValue(pByteString, (uint8_t *)&pMesh->pEdges[i], 32);
		stageProgressWrap(pContext, i);
	}
	stageEndWrap(pContext);

	if (isDataNameInvalid(pByteString, "LA")) { //loop attribs
		return;
	}
	decodeAttribs(pContext, pByteString, &pMesh->loopAttribs, pMesh->loopCount);
	if (isDataNameInvalid(pByteString, "EA")) { //edge attribs
		return;
	}
	decodeAttribs(pContext, pByteString, &pMesh->edgeAttribs, pMesh->edgeCount);
	if (isDataNameInvalid(pByteString, "VA")) { //vert attribs
		return;
	}
	decodeAttribs(pContext, pByteString, &pMesh->vertAttribs, pMesh->vertCount);

	if (isDataNameInvalid(pByteString, "OE")) { //obj end
		return;
	}
}

static
void decodeRuvmData(RuvmContext pContext, RuvmHeader *pHeader,
                    ByteString *dataByteString, RuvmObject **ppObjArr,
                    RuvmObject **ppUsgArr, bool forEdit) {
	*ppObjArr = pContext->alloc.pCalloc(pHeader->objCount, sizeof(RuvmObject));
	RUVM_ASSERT("", pHeader->usgCount >= 0);
	bool usesUsg = pHeader->usgCount > 0 && !forEdit;
	for (int32_t i = 0; i < pHeader->objCount; ++i) {
		//usgUsg is passed here to indicate that an extra vert
		//attrib should be created. This would be used later to mark a verts
		//respective usg.
		loadObj(pContext, *ppObjArr + i, dataByteString, usesUsg);
	}

	*ppUsgArr = pContext->alloc.pCalloc(pHeader->usgCount, sizeof(RuvmObject));
	for (int32_t i = 0; i < pHeader->usgCount; ++i) {
		//usgs themselves don't need a usg attrib, so false is passed
		loadObj(pContext, *ppUsgArr + i, dataByteString, false);
	}
}

void ruvmLoadRuvmFile(RuvmContext pContext, char *filePath,
                      int32_t *pObjCount, RuvmObject **ppObjArr,
                      int32_t *pUsgCount, RuvmObject **ppUsgArr, bool forEdit) {
	ByteString headerByteString = {0};
	ByteString dataByteString = {0};
	void *pFile;
	printf("Loading RUVM file: %s\n", filePath);
	pContext->io.pOpen(&pFile, filePath, 1, &pContext->alloc);
	int16_t headerSize = 0;
	pContext->io.pRead(pFile, (uint8_t *)&headerSize, 2);
	printf("Ruvm File Header Size: %d\n", headerSize);
	printf("Header is %d bytes\n", headerSize);
	headerByteString.pString = pContext->alloc.pMalloc(headerSize);
	printf("Reading header\n");
	pContext->io.pRead(pFile, headerByteString.pString, headerSize);
	printf("Decoding header\n");
	RuvmHeader header = decodeRuvmHeader(pContext, &headerByteString);
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
			printf("Successfully decompressed RUVM file data\n");
			break;
		case Z_MEM_ERROR:
			printf("Failed to decompress RUVM file data. Memory error\n");
			break;
		case Z_BUF_ERROR:
			printf("Failed to decompress RUVM file data. Buffer was too small\n");
			break;
	}
	if (dataSizeUncompressed != header.dataSize) {
		printf("Failed to load RUVM file. Decompressed data size doesn't match header description\n");
		return;
	}
	printf("Decoding data\n");
	decodeRuvmData(pContext, &header, &dataByteString, ppObjArr, ppUsgArr, forEdit);
	pContext->alloc.pFree(headerByteString.pString);
	pContext->alloc.pFree(dataByteString.pString);
	*pObjCount = header.objCount;
	*pUsgCount = header.usgCount;
}

void ruvmIoSetCustom(RuvmContext pContext, RuvmIo *pIo) {
	if (!pIo->pOpen || !pIo->pClose || !pIo->pWrite || !pIo->pRead) {
		printf("Failed to set custom IO. One or more functions were NULL");
		abort();
	}
	pContext->io = *pIo;
}

void ruvmIoSetDefault(RuvmContext pContext) {
	pContext->io.pOpen = ruvmPlatformFileOpen;
	pContext->io.pClose = ruvmPlatformFileClose;
	pContext->io.pWrite = ruvmPlatformFileWrite;
    pContext->io.pRead = ruvmPlatformFileRead;
}
