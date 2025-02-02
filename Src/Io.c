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

static int32_t decodeSingleBit(ByteString *byteString) {
	int32_t value = byteString->pString[byteString->byteIdx];
	value >>= byteString->nextBitIdx;
	value &= 1;
	byteString->nextBitIdx++;
	byteString->byteIdx += byteString->nextBitIdx >= 8;
	byteString->nextBitIdx %= 8;
	return value;
}

void encodeValue(ByteString *byteString, uint8_t *value,
                        int32_t lengthInBits, int64_t *pSize) {
	uint8_t valueBuf[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	int32_t lengthInBytes = lengthInBits / 8;
	lengthInBytes += (lengthInBits - lengthInBytes * 8) > 0;
	for (int32_t i = 1; i <= lengthInBytes; ++i) {
		valueBuf[i] = value[i - 1];
	}
	for (int32_t i = lengthInBytes - 1; i >= 1; --i) {
		valueBuf[i] <<= byteString->nextBitIdx;
		uint8_t nextByteCopy = valueBuf[i - 1];
		nextByteCopy >>= 8 - byteString->nextBitIdx;
		valueBuf[i] |= nextByteCopy;
	}
	int32_t writeUpTo = lengthInBytes + (byteString->nextBitIdx > 0);
	for (int32_t i = 0; i < writeUpTo; ++i) {
		byteString->pString[byteString->byteIdx + i] |= valueBuf[i + 1];
	}
	byteString->nextBitIdx = byteString->nextBitIdx + lengthInBits;
	byteString->byteIdx += byteString->nextBitIdx / 8;
	byteString->nextBitIdx %= 8;
	*pSize -= lengthInBits;
}

void encodeString(ByteString *byteString,
                         uint8_t *string, int64_t *pSize) {
	int32_t lengthInBits = (strlen((char *)string) + 1) * 8;
	int32_t lengthInBytes = lengthInBits / 8;
	byteString->byteIdx += byteString->nextBitIdx > 0;
	byteString->nextBitIdx = 0;
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		byteString->pString[byteString->byteIdx] = string[i];
		byteString->byteIdx++;
	}
	*pSize -= lengthInBits;
}

void decodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits) {
	int32_t lengthInBytes = lengthInBits / 8;
	int32_t bitDifference = lengthInBits - lengthInBytes * 8;
	lengthInBytes += bitDifference > 0;
	uint8_t buf[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		buf[i] = byteString->pString[byteString->byteIdx + i];
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		buf[i] >>= byteString->nextBitIdx;
		uint8_t nextByteCopy = buf[i + 1];
		nextByteCopy <<= 8 - byteString->nextBitIdx;
		buf[i] |= nextByteCopy;
	}
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		value[i] = buf[i];
	}
	uint8_t mask = UCHAR_MAX >> ((8 - bitDifference) % 8);
	value[lengthInBytes - 1] &= mask;
	byteString->nextBitIdx = byteString->nextBitIdx + lengthInBits;
	byteString->byteIdx += byteString->nextBitIdx / 8;
	byteString->nextBitIdx %= 8;
}

void decodeString(ByteString *byteString, char *string, int32_t maxLen) {
	byteString->byteIdx += byteString->nextBitIdx > 0;
	uint8_t *dataPtr = byteString->pString + byteString->byteIdx;
	int32_t i = 0;
	for (; i < maxLen && dataPtr[i]; ++i) {
		string[i] = dataPtr[i];
	}
	string[i] = 0;
	byteString->byteIdx += i + 1;
	byteString->nextBitIdx = 0;
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
		if (pAttribs->pArr[i].type == STUC_ATTRIB_STRING) {
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
void encodeIndexedAttribs(ByteString *pData, AttribIndexedArr attribs,
                          int64_t *pSize) {
	for (int32_t i = 0; i < attribs.count; ++i) {
		AttribIndexed *pAttrib = attribs.pArr + i;
		if (pAttrib->type == STUC_ATTRIB_STRING) {
			for (int32_t j = 0; j < pAttrib->count; ++j) {
				void *pString = attribAsVoid(pAttrib, j);
				encodeString(pData, pString, pSize);
			}
		}
		else {
			int32_t attribSize = getAttribSize(pAttrib->type) * 8;
			for (int32_t j = 0; j < pAttrib->count; ++j) {
				encodeValue(pData, attribAsVoid(pAttrib, j),
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

static
void encodeIndexedAttribMeta(ByteString *pData,
                             AttribIndexedArr attribs) {
	int64_t size = 0;
	for (int32_t i = 0; i < attribs.count; ++i) {
		encodeValue(pData, (uint8_t *)&attribs.pArr[i].type, 16, &size);
		encodeValue(pData, (uint8_t *)&attribs.pArr[i].count, 32, &size);
		encodeString(pData, (uint8_t *)attribs.pArr[i].name, &size);
	}
}

typedef struct {
	int64_t transform;
	int64_t type;
	int64_t dataNames;
	int64_t attribCounts;
	int64_t meshAttribMeta;
	int64_t faceAttribMeta;
	int64_t cornerAttribMeta;
	int64_t edgeAttribMeta;
	int64_t vertAttribMeta;
	int64_t meshAttribs;
	int64_t faceAttribs;
	int64_t cornerAttribs;
	int64_t edgeAttribs;
	int64_t vertAttribs;
	int64_t listCounts;
	int64_t faceList;
	int64_t cornerList;
	int64_t edgeList;
} MeshSizeInBits;

static getObjDataSize(StucObject *pObj,
                      int64_t *pDataSizeInBits, MeshSizeInBits *pSize) {
	StucMesh *pMesh = pObj->pData;
	pSize->transform = 32 * 16;
	pSize->type = 8;
	pSize->dataNames = 16 * 3;
	if (!checkIfMesh(pObj->pData)) {
		return STUC_SUCCESS;
	}
	pSize->dataNames += 16 * 9;

	//calculate total size of attribute header info
	pSize->meshAttribMeta = getTotalAttribMetaSize(&pMesh->meshAttribs);
	pSize->faceAttribMeta = getTotalAttribMetaSize(&pMesh->faceAttribs);
	pSize->cornerAttribMeta = getTotalAttribMetaSize(&pMesh->cornerAttribs);
	pSize->edgeAttribMeta = getTotalAttribMetaSize(&pMesh->edgeAttribs);
	pSize->vertAttribMeta = getTotalAttribMetaSize(&pMesh->vertAttribs);

	pSize->attribCounts = 32 + //mesh attrib count
	                    32 + //face attrib count
	                    32 + //corner attrib count
	                    32 + //edge attrib count
	                    32; //vert attrib count
	pSize->listCounts = 32 + //face count
	                  32 + //corner count
	                  32 + //edge count
	                  32;  //vert count

	//calculate total size of attribute data
	int64_t meshAttribSize = getTotalAttribSize(&pMesh->meshAttribs);
	int64_t faceAttribSize = getTotalAttribSize(&pMesh->faceAttribs);
	int64_t cornerAttribSize = getTotalAttribSize(&pMesh->cornerAttribs);
	int64_t edgeAttribSize = getTotalAttribSize(&pMesh->edgeAttribs);
	int64_t vertAttribSize = getTotalAttribSize(&pMesh->vertAttribs);

	pSize->meshAttribs = meshAttribSize;
	pSize->faceAttribs = faceAttribSize * pMesh->faceCount;
	pSize->cornerAttribs = cornerAttribSize * pMesh->cornerCount;
	pSize->edgeAttribs = edgeAttribSize * pMesh->edgeCount;
	pSize->vertAttribs = vertAttribSize * pMesh->vertCount;

	pSize->faceList = 32 * (int64_t)pMesh->faceCount;
	pSize->cornerList = 32 * (int64_t)pMesh->cornerCount;
	pSize->edgeList = 32 * (int64_t)pMesh->cornerCount;
	return STUC_SUCCESS;
}

static
void encodeDataName(ByteString *pByteString, char *pName, int64_t *pSize) {
	//not using encodeString, as there's not need for a null terminator.
	//Only using 2 characters
	
	//ensure string is aligned with byte (we need to do this manually,
	//as encodeValue is being used instead of encodeString)
	pByteString->byteIdx += pByteString->nextBitIdx > 0;
	pByteString->nextBitIdx = 0;
	encodeValue(pByteString, (uint8_t *)pName, 16, pSize);
}

static
bool isSizeInvalid(int64_t size) {
	return size != 0;
}

static
StucResult encodeObj(ByteString *pByteString,
                     StucObject *pObj, MeshSizeInBits *pSize) {
	//encode obj header
	StucMesh *pMesh = pObj->pData;
	encodeDataName(pByteString, "OS", &pSize->dataNames); //object start
	encodeDataName(pByteString, "XF", &pSize->dataNames); //transform/ xform
	for (int32_t i = 0; i < 16; ++i) {
		int32_t x = i % 4;
		int32_t y = i / 4;
		encodeValue(pByteString, (uint8_t *)&pObj->transform.d[y][x], 32, &pSize->transform);
	}
	if (isSizeInvalid(pSize->transform)) {
		return STUC_ERROR;
	}
	encodeDataName(pByteString, "OT", &pSize->dataNames); //object type
	encodeValue(pByteString, (uint8_t *)&pObj->pData->type, 8, &pSize->type);
	if (!checkIfMesh(pObj->pData)) {
		if (isSizeInvalid(pSize->dataNames)) {
			return STUC_ERROR;
		}
		return STUC_SUCCESS;
	}
	encodeDataName(pByteString, "HD", &pSize->dataNames); //header
	encodeValue(pByteString, (uint8_t *)&pMesh->meshAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->meshAttribs, &pSize->meshAttribMeta);
	if (isSizeInvalid(pSize->meshAttribMeta)) {
		return STUC_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->faceAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->faceAttribs, &pSize->faceAttribMeta);
	if (isSizeInvalid(pSize->faceAttribMeta)) {
		return STUC_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->cornerAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->cornerAttribs, &pSize->cornerAttribMeta);
	if (isSizeInvalid(pSize->cornerAttribMeta)) {
		return STUC_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->edgeAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->edgeAttribs, &pSize->edgeAttribMeta);
	if (isSizeInvalid(pSize->edgeAttribMeta)) {
		return STUC_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->vertAttribs.count,
	            32, &pSize->attribCounts);
	encodeAttribMeta(pByteString, &pMesh->vertAttribs, &pSize->vertAttribMeta);
	if (isSizeInvalid(pSize->vertAttribMeta)) {
		return STUC_ERROR;
	}
	if (isSizeInvalid(pSize->attribCounts)) {
		return STUC_ERROR;
	}
	encodeValue(pByteString, (uint8_t *)&pMesh->faceCount, 32, &pSize->listCounts);
	encodeValue(pByteString, (uint8_t *)&pMesh->cornerCount, 32, &pSize->listCounts);
	encodeValue(pByteString, (uint8_t *)&pMesh->edgeCount, 32, &pSize->listCounts);
	encodeValue(pByteString, (uint8_t *)&pMesh->vertCount, 32, &pSize->listCounts);
	if (isSizeInvalid(pSize->listCounts)) {
		return STUC_ERROR;
	}
	//encode data
	encodeDataName(pByteString, "MA", &pSize->dataNames); //mesh attribs
	encodeAttribs(pByteString, &pMesh->meshAttribs, 1, &pSize->meshAttribs);
	if (isSizeInvalid(pSize->meshAttribs)) {
		return STUC_ERROR;
	}
	encodeDataName(pByteString, "FL", &pSize->dataNames); //face list
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		STUC_ASSERT("", pMesh->pFaces[i] >= 0 &&
		                pMesh->pFaces[i] < pMesh->cornerCount);
		encodeValue(pByteString, (uint8_t *)&pMesh->pFaces[i], 32, &pSize->faceList);
	}
	if (isSizeInvalid(pSize->faceList)) {
		return STUC_ERROR;
	}
	encodeDataName(pByteString, "FA", &pSize->dataNames); //face attribs
	encodeAttribs(pByteString, &pMesh->faceAttribs, pMesh->faceCount, &pSize->faceAttribs);
	if (isSizeInvalid(pSize->faceAttribs)) {
		return STUC_ERROR;
	}
	encodeDataName(pByteString, "LL", &pSize->dataNames); //corner and edge lists
	for (int32_t i = 0; i < pMesh->cornerCount; ++i) {
		STUC_ASSERT("", pMesh->pCorners[i] >= 0 &&
		                pMesh->pCorners[i] < pMesh->vertCount);
		encodeValue(pByteString, (uint8_t *)&pMesh->pCorners[i], 32, &pSize->cornerList);
		STUC_ASSERT("", pMesh->pEdges[i] >= 0 &&
		                pMesh->pEdges[i] < pMesh->edgeCount);
		encodeValue(pByteString, (uint8_t *)&pMesh->pEdges[i], 32, &pSize->edgeList);
	}
	if (isSizeInvalid(pSize->cornerList) || isSizeInvalid(pSize->edgeList)) {
		return STUC_ERROR;
	}
	encodeDataName(pByteString, "LA", &pSize->dataNames); //corner attribs
	encodeAttribs(pByteString, &pMesh->cornerAttribs, pMesh->cornerCount, &pSize->cornerAttribs);
	if (isSizeInvalid(pSize->cornerAttribs)) {
		return STUC_ERROR;
	}
	encodeDataName(pByteString, "EA", &pSize->dataNames); //edge attribs
	encodeAttribs(pByteString, &pMesh->edgeAttribs, pMesh->edgeCount, &pSize->edgeAttribs);
	if (isSizeInvalid(pSize->edgeAttribs)) {
		return STUC_ERROR;
	}
	encodeDataName(pByteString, "VA", &pSize->dataNames); //vert attribs
	encodeAttribs(pByteString, &pMesh->vertAttribs, pMesh->vertCount, &pSize->vertAttribs);
	if (isSizeInvalid(pSize->vertAttribs)) {
		return STUC_ERROR;
	}
	encodeDataName(pByteString, "OE", &pSize->dataNames); //object end
	if (isSizeInvalid(pSize->dataNames)) {
		return STUC_ERROR;
	}
	return STUC_SUCCESS;
}

static
int64_t sumOfMeshSize(MeshSizeInBits *pSize) {
	return pSize->transform +
	       pSize->type +
		   pSize->dataNames +
		   pSize->attribCounts +
		   pSize->meshAttribMeta +
		   pSize->faceAttribMeta +
		   pSize->cornerAttribMeta +
		   pSize->edgeAttribMeta +
		   pSize->vertAttribMeta +
		   pSize->meshAttribs +
		   pSize->faceAttribs +
		   pSize->cornerAttribs +
		   pSize->edgeAttribs +
		   pSize->vertAttribs +
		   pSize->listCounts +
		   pSize->faceList +
		   pSize->cornerList +
		   pSize->edgeList;
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

StucResult stucWriteStucFile(StucContext pContext, const char *pName,
                             int32_t objCount, StucObject *pObjArr,
                             int32_t usgCount, StucUsg *pUsgArr,
                             StucAttribIndexedArr indexedAttribs) {
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

	int64_t dataSizeInBits = 0;
	int64_t indexedAttribsSize = 0;
	for (int32_t i = 0; i < indexedAttribs.count; ++i) {
		AttribIndexed *pAttrib = indexedAttribs.pArr + i;
		int64_t size = getAttribSize(pAttrib->type) * 8 * pAttrib->count;
		dataSizeInBits += size;
		indexedAttribsSize += size;
	}
	MeshSizeInBits *pMeshSizes =
		pContext->alloc.pCalloc(objCount + usgCount * 2, sizeof(MeshSizeInBits));
	for (int32_t i = 0; i < objCount; ++i) {
		err = getObjDataSize(pObjArr + i, &dataSizeInBits, pMeshSizes + i);
		if (err != STUC_SUCCESS) {
			return err;
		}
		dataSizeInBits += sumOfMeshSize(pMeshSizes + i);
	}
	int32_t sizesIdx = objCount;
	for (int32_t i = 0; i < cutoffCount; ++i) {
		err = getObjDataSize(ppCutoffs[i], &dataSizeInBits, pMeshSizes + sizesIdx);
		if (err != STUC_SUCCESS) {
			return err;
		}
		dataSizeInBits += sumOfMeshSize(pMeshSizes + sizesIdx);
		sizesIdx++;
	}
	for (int32_t i = 0; i < usgCount; ++i) {
		err = getObjDataSize(&pUsgArr[i].obj, &dataSizeInBits, pMeshSizes + sizesIdx);
		if (err != STUC_SUCCESS) {
			return err;
		}
		dataSizeInBits += sumOfMeshSize(pMeshSizes + sizesIdx);
		dataSizeInBits += STUC_FLAT_CUTOFF_HEADER_SIZE;
		if (!pUsgArr[i].pFlatCutoff) {
			dataSizeInBits -= sizeof(int32_t); //no index
		}
		sizesIdx++;
	}
	int64_t dataSizeInBytes = dataSizeInBits / 8 + 2;
	data.byteIdx = 0;
	data.nextBitIdx = 0;
	data.pString = pContext->alloc.pCalloc(dataSizeInBytes, 1);
	if (indexedAttribs.count) {
		encodeIndexedAttribMeta(&data, indexedAttribs);
		encodeIndexedAttribs(&data, indexedAttribs, &indexedAttribsSize);
	}
	for (int32_t i = 0; i < objCount; ++i) {
		err = encodeObj(&data, pObjArr + i, pMeshSizes + i);
		if (err != STUC_SUCCESS) {
			return err;
		}
	}
	sizesIdx = objCount;
	for (int32_t i = 0; i < cutoffCount; ++i) {
		err = encodeObj(&data, ppCutoffs[i], pMeshSizes + sizesIdx);
		if (err != STUC_SUCCESS) {
			return err;
		}
		sizesIdx++;
	}
	for (int32_t i = 0; i < usgCount; ++i) {
		err = encodeObj(&data, &pUsgArr[i].obj, pMeshSizes + sizesIdx);
		if (err != STUC_SUCCESS) {
			return err;
		}
		bool hasFlatCutoff = pUsgArr[i].pFlatCutoff != NULL;
		int64_t fcHeaderSize = STUC_FLAT_CUTOFF_HEADER_SIZE;
		encodeDataName(&data, "FC", &fcHeaderSize); //flatten cut-off
		encodeValue(&data, (uint8_t *)&hasFlatCutoff, 8, &fcHeaderSize);
		if (hasFlatCutoff) {
			encodeValue(&data, (uint8_t *)&pCutoffIndices[i], 32, &fcHeaderSize);
		}
		sizesIdx++;
	}
	pContext->alloc.pFree(pMeshSizes);

	//compress data
	//TODO convert to use proper zlib inflate and deflate calls
	//compress and decompress are not context independent iirc
	int64_t dataSize = data.byteIdx + (data.nextBitIdx > 0);
	int64_t dataSizeExtra = dataSize / 1000;
	dataSizeExtra += ((dataSize * 1000) - dataSize) > 0;
	dataSizeExtra += 12;
	unsigned long uCompressedDataSize = dataSize + dataSizeExtra;
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
	int64_t compressedDataSize = uCompressedDataSize;
	printf("Compressed data is %lu long\n", compressedDataSize);

	//encode header
	const char *format = "UV Stucco Map File";
	int32_t formatLen = strnlen(format, MAP_FORMAT_NAME_MAX_LEN);
	STUC_ASSERT("", formatLen < MAP_FORMAT_NAME_MAX_LEN)
	int64_t headerSizeInBits = 8 * (formatLen + 1) +
	                           16 + //version
	                           64 + //compressed data size
	                           64 + //uncompressed data size
                               32 + //indexed attrib count
	                           32 + //obj count
	                           32 + //usg count
	                           32;  //flatten cutoff count
	int64_t headerSizeInBytes = headerSizeInBits / 8 + 2;
	header.pString = pContext->alloc.pCalloc(headerSizeInBytes, 1);
	encodeString(&header, (uint8_t *)format, &headerSizeInBits);
	int32_t version = STUC_MAP_VERSION;
	encodeValue(&header, (uint8_t *)&version, 16, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&compressedDataSize, 64, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&dataSize, 64, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&indexedAttribs.count, 32, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&objCount, 32, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&usgCount, 32, &headerSizeInBits);
	encodeValue(&header, (uint8_t *)&cutoffCount, 32, &headerSizeInBits);
	if (isSizeInvalid(headerSizeInBits)) {
		return STUC_ERROR;
	}

	//TODO CRC for uncompressed data
	
	void *pFile;
	pContext->io.pOpen(&pFile, pName, 0, &pContext->alloc);
	headerSizeInBytes = header.byteIdx + (header.nextBitIdx > 0);
	pContext->io.pWrite(pFile, (uint8_t *)&headerSizeInBytes, 2);
	pContext->io.pWrite(pFile, header.pString, headerSizeInBytes);
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
		decodeValue(pData, (uint8_t *)&pAttribs->pArr[i].type, 16);
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

	//TODO add short headers (like 2 or 4 bytes) to the start of each
	//of these large blocks of data, to better catch corrupt files.
	//So one for faces, corners, edges, etc

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
