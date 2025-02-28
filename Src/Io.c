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
void reallocByteStringIfNeeded(
	const StucAlloc *pAlloc,
	ByteString *pByteString,
	I64 bitOffset
) {
	I64 bitCount = ((pByteString->byteIdx) * 8) + pByteString->nextBitIdx;
	STUC_ASSERT("", bitCount <= pByteString->size * 8);
	bitCount += bitOffset;
	I64 byteCount = bitCount / 8 + (bitCount % 8 != 0);
	if (byteCount >= pByteString->size) {
		I64 oldSize = pByteString->size;
		pByteString->size *= 2;
		pByteString->pString = pAlloc->pRealloc(pByteString->pString, pByteString->size);
		memset(pByteString->pString + oldSize, 0, pByteString->size - oldSize);
	}
}

void stucEncodeValue(
	const StucAlloc *pAlloc,
	ByteString *pByteString,
	U8 *pValue,
	I32 lengthInBits
) {
	reallocByteStringIfNeeded(pAlloc, pByteString, lengthInBits);
	U8 valueBuf[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	I32 lengthInBytes = lengthInBits / 8;
	lengthInBytes += (lengthInBits - lengthInBytes * 8) > 0;
	for (I32 i = 1; i <= lengthInBytes; ++i) {
		valueBuf[i] = pValue[i - 1];
	}
	for (I32 i = lengthInBytes - 1; i >= 1; --i) {
		valueBuf[i] <<= pByteString->nextBitIdx;
		U8 nextByteCopy = valueBuf[i - 1];
		nextByteCopy >>= 8 - pByteString->nextBitIdx;
		valueBuf[i] |= nextByteCopy;
	}
	I32 writeUpTo = lengthInBytes + (pByteString->nextBitIdx > 0);
	for (I32 i = 0; i < writeUpTo; ++i) {
		pByteString->pString[pByteString->byteIdx + i] |= valueBuf[i + 1];
	}
	pByteString->nextBitIdx = pByteString->nextBitIdx + lengthInBits;
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

void stucDecodeValue(ByteString *pByteString, U8 *pValue, I32 lengthInBits) {
	I32 lengthInBytes = lengthInBits / 8;
	I32 bitDifference = lengthInBits - lengthInBytes * 8;
	lengthInBytes += bitDifference > 0;
	U8 buf[ENCODE_DECODE_BUFFER_LENGTH] = {0};
	for (I32 i = 0; i < lengthInBytes; ++i) {
		buf[i] = pByteString->pString[pByteString->byteIdx + i];
	}
	for (I32 i = 0; i < lengthInBytes; ++i) {
		buf[i] >>= pByteString->nextBitIdx;
		U8 nextByteCopy = buf[i + 1];
		nextByteCopy <<= 8 - pByteString->nextBitIdx;
		buf[i] |= nextByteCopy;
	}
	for (I32 i = 0; i < lengthInBytes; ++i) {
		pValue[i] = buf[i];
	}
	U8 mask = UCHAR_MAX >> ((8 - bitDifference) % 8);
	pValue[lengthInBytes - 1] &= mask;
	pByteString->nextBitIdx = pByteString->nextBitIdx + lengthInBits;
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
StucResult encodeObj(
	const StucAlloc *pAlloc,
	ByteString *pByteString,
	StucObject *pObj
) {
	//encode obj header
	StucMesh *pMesh = (StucMesh *)pObj->pData;
	encodeDataName(pAlloc, pByteString, "OS"); //object start
	encodeDataName(pAlloc, pByteString, "XF"); //transform/ xform
	for (I32 i = 0; i < 16; ++i) {
		I32 x = i % 4;
		I32 y = i / 4;
		stucEncodeValue(pAlloc, pByteString, (U8 *)&pObj->transform.d[y][x], 32);
	}
	encodeDataName(pAlloc, pByteString, "OT"); //object type
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pObj->pData->type, 8);
	if (!stucCheckIfMesh(*pObj->pData)) {
		return STUC_SUCCESS;
	}
	encodeDataName(pAlloc, pByteString, "HD"); //header
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->meshAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->meshAttribs);
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->faceAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->faceAttribs);
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->cornerAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->cornerAttribs);
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->edgeAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->edgeAttribs);
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->vertAttribs.count, 32);
	encodeAttribMeta(pAlloc, pByteString, &pMesh->vertAttribs);
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->faceCount, 32);
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->cornerCount, 32);
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->edgeCount, 32);
	stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->vertCount, 32);
	//encode data
	encodeDataName(pAlloc, pByteString, "MA"); //mesh attribs
	encodeAttribs(pAlloc, pByteString, &pMesh->meshAttribs, 1);
	encodeDataName(pAlloc, pByteString, "FL"); //face list
	for (I32 i = 0; i < pMesh->faceCount; ++i) {
		STUC_ASSERT("",
			pMesh->pFaces[i] >= 0 &&
			pMesh->pFaces[i] < pMesh->cornerCount
		);
		stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->pFaces[i], 32);
	}
	encodeDataName(pAlloc, pByteString, "FA"); //face attribs
	encodeAttribs(pAlloc, pByteString, &pMesh->faceAttribs, pMesh->faceCount);
	encodeDataName(pAlloc, pByteString, "LL"); //corner and edge lists
	for (I32 i = 0; i < pMesh->cornerCount; ++i) {
		STUC_ASSERT("",
			pMesh->pCorners[i] >= 0 &&
			pMesh->pCorners[i] < pMesh->vertCount
		);
		stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->pCorners[i], 32);
		STUC_ASSERT("",
			pMesh->pEdges[i] >= 0 &&
			pMesh->pEdges[i] < pMesh->edgeCount
		);
		stucEncodeValue(pAlloc, pByteString, (U8 *)&pMesh->pEdges[i], 32);
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
void addSpacing(ByteString *pByteString, I32 lenInBits, I64 *pSize) {
	I32 lenInBytes = lenInBits / 8;
	pByteString->byteIdx += lenInBytes;
	pByteString->nextBitIdx = lenInBits - lenInBytes * 8;
	*pSize -= lenInBits;
}

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
	*ppIndices = pCtx->alloc.pCalloc(usgCount, sizeof(I32));
	*pppCutoffs = pCtx->alloc.pCalloc(usgCount, sizeof(void *));
	*pCutoffCount = 0;
	for (I32 i = 0; i < usgCount; ++i) {
		if (!pUsgArr[i].pFlatCutoff) {
			continue;
		}
		(*ppIndices)[i] =
			addUniqToPtrArr(pUsgArr[i].pFlatCutoff, pCutoffCount, *pppCutoffs);
	}
	*pppCutoffs = pCtx->alloc.pRealloc(*pppCutoffs, sizeof(void *) * *pCutoffCount);
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

StucResult stucWriteStucFile(
	StucContext pCtx,
	const char *pName,
	I32 objCount,
	StucObject *pObjArr,
	I32 usgCount,
	StucUsg *pUsgArr,
	StucAttribIndexedArr *pIndexedAttribs
) {
	StucResult err = STUC_SUCCESS;
	const StucAlloc *pAlloc = &pCtx->alloc;
	ByteString header = {0};
	ByteString data = {0};
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
	data.pString = pCtx->alloc.pCalloc(data.size, 1);
	if (pIndexedAttribs->count) {
		encodeIndexedAttribMeta(pAlloc, &data, pIndexedAttribs);
		encodeIndexedAttribs(pAlloc, &data, pIndexedAttribs);
	}
	for (I32 i = 0; i < objCount; ++i) {
		err = encodeObj(pAlloc, &data, pObjArr + i);
		STUC_THROW_IFNOT(err, "", 0);
	}
	for (I32 i = 0; i < cutoffCount; ++i) {
		err = encodeObj(pAlloc, &data, ppCutoffs[i]);
		STUC_THROW_IFNOT(err, "", 0);
	}
	for (I32 i = 0; i < usgCount; ++i) {
		err = encodeObj(pAlloc, &data, &pUsgArr[i].obj);
		STUC_THROW_IFNOT(err, "", 0);
		bool hasFlatCutoff = pUsgArr[i].pFlatCutoff != NULL;
		encodeDataName(pAlloc, &data, "FC"); //flatten cut-off
		stucEncodeValue(pAlloc, &data, (U8 *)&hasFlatCutoff, 8);
		if (hasFlatCutoff) {
			stucEncodeValue(pAlloc, &data, (U8 *)&pCutoffIndices[i], 32);
		}
	}
	//compress data
	//TODO convert to use proper zlib inflate and deflate calls
	//compress and decompress are not context independent iirc
	I64 dataSize = data.byteIdx + (data.nextBitIdx > 0);
	//zlib needs some padding
	uLongf uCompressedDataSize = (uLong)((I32)((F32)dataSize * 1.01f) + 12);
	U8 *compressedData = pCtx->alloc.pMalloc(uCompressedDataSize);
	I32 zResult = compress(
		compressedData,
		&uCompressedDataSize,
		data.pString,
		(uLong)dataSize
	);
	switch(zResult) {
		case Z_OK:
			printf("Successfully compressed STUC data\n");
			break;
		case Z_MEM_ERROR:
			STUC_THROW(err, "Failed to compress STUC data, memory error\n", 0);
		case Z_BUF_ERROR:
			STUC_THROW(err, "Failed to compress STUC data, output buffer too small\n", 0);
	}
	I64 compressedDataSize = (I64)uCompressedDataSize;
#ifdef WIN32
	printf("Compressed data is %llu long\n", compressedDataSize);
#else
	printf("Compressed data is %lu long\n", compressedDataSize);
#endif

	//encode header
	const char *format = "UV Stucco Map File";
	I32 formatLen = (I32)strnlen(format, MAP_FORMAT_NAME_MAX_LEN);
	STUC_ASSERT("", formatLen < MAP_FORMAT_NAME_MAX_LEN)
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
	header.pString = pCtx->alloc.pCalloc(header.size, 1);
	stucEncodeString(pAlloc, &header, (U8 *)format);
	I32 version = STUC_MAP_VERSION;
	stucEncodeValue(pAlloc, &header, (U8 *)&version, 16);
	stucEncodeValue(pAlloc, &header, (U8 *)&compressedDataSize, 64);
	stucEncodeValue(pAlloc, &header, (U8 *)&dataSize, 64);
	stucEncodeValue(pAlloc, &header, (U8 *)&pIndexedAttribs->count, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&objCount, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&usgCount, 32);
	stucEncodeValue(pAlloc, &header, (U8 *)&cutoffCount, 32);

	//TODO CRC for uncompressed data
	
	err = pCtx->io.pOpen(&pFile, pName, 0, &pCtx->alloc);
	STUC_THROW_IFNOT(err, "", 0);
	I64 finalHeaderLen = header.byteIdx + (header.nextBitIdx > 0);
	err = pCtx->io.pWrite(pFile, (U8 *)&finalHeaderLen, 2);
	STUC_THROW_IFNOT(err, "", 0);
	err = pCtx->io.pWrite(pFile, header.pString, (I32)finalHeaderLen);
	STUC_THROW_IFNOT(err, "", 0);
	err = pCtx->io.pWrite(pFile, compressedData, (I32)compressedDataSize);
	STUC_THROW_IFNOT(err, "", 0);

	STUC_CATCH(0, err, ;);
	if (pFile) {
		err = pCtx->io.pClose(pFile);
	}
	if (header.pString) {
		pCtx->alloc.pFree(header.pString);
	}
	if (data.pString) {
		pCtx->alloc.pFree(data.pString);
	}
	printf("Finished STUC export\n");
	return err;
}

static
StucResult decodeAttribMeta(ByteString *pData, AttribArray *pAttribs) {
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
				return STUC_ERROR;
			}
		}
	}
	return STUC_SUCCESS;
}

static
StucResult decodeIndexedAttribMeta(ByteString *pData, AttribIndexedArr *pAttribs) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		stucDecodeValue(pData, (U8 *)&pAttribs->pArr[i].core.type, 16);
		stucDecodeValue(pData, (U8 *)&pAttribs->pArr[i].count, 32);
		I32 maxNameLen = sizeof(pAttribs->pArr[i].core.name);
		stucDecodeString(pData, (char *)pAttribs->pArr[i].core.name, maxNameLen);
		for (I32 j = 0; j < i; ++j) {
			if (!strncmp(pAttribs->pArr[i].core.name, pAttribs->pArr[j].core.name,
				STUC_ATTRIB_NAME_MAX_LEN)) {

				//dup
				return STUC_ERROR;
			}
		}
	}
	return STUC_SUCCESS;
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
			pCtx->alloc.pCalloc(dataLen, attribSize) : NULL;
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
			pCtx->alloc.pCalloc(pAttrib->count, attribSize) : NULL;
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
	stucDecodeValue(headerByteString, (U8 *)&header.flatCutoffCount, 32);

	return header;
}

static
StucResult isDataNameInvalid(ByteString *pByteString, char *pName) {
	//ensure string is aligned with byte (we need to do this manually,
	//as stucDecodeValue is being used instead of stucDecodeString, given there's
	//only 2 characters)
	pByteString->byteIdx += pByteString->nextBitIdx > 0;
	pByteString->nextBitIdx = 0;
	char dataName[2] = {0};
	stucDecodeValue(pByteString, (U8 *)&dataName, 16);
	if (dataName[0] != pName[0] || dataName[1] != pName[1]) {
		return STUC_ERROR;
	}
	else {
		return STUC_SUCCESS;
	}
}

static
StucResult loadObj(
	StucContext pCtx,
	StucObject *pObj,
	ByteString *pByteString,
	bool usesUsg
) {
	stucCreateMesh(pCtx, pObj, STUC_OBJECT_DATA_MESH_INTERN);
	StucMesh *pMesh = (StucMesh *)pObj->pData;

	StucResult err = STUC_NOT_SET;

	err = isDataNameInvalid(pByteString, "OS"); //transform/ xform and type
	STUC_THROW_IFNOT(err, "Data name did not match 'OS'", 0);
	err = isDataNameInvalid(pByteString, "XF"); //transform/ xform and type
	STUC_THROW_IFNOT(err, "Data name did not match 'XF'", 0);
	for (I32 i = 0; i < 16; ++i) {
		I32 x = i % 4;
		I32 y = i / 4;
		stucDecodeValue(pByteString, (U8 *)&pObj->transform.d[y][x], 32);
	}
	err = isDataNameInvalid(pByteString, "OT"); //object type
	STUC_THROW_IFNOT(err, "Data name did not match 'OT'", 0);
	stucDecodeValue(pByteString, (U8 *)&pObj->pData->type, 8);
	if (!stucCheckIfMesh(*pObj->pData)) {
		err = STUC_ERROR;
		STUC_THROW_IFNOT(err, "Object is not a mesh", 0);
	}
	err = isDataNameInvalid(pByteString, "HD"); //header
	STUC_THROW_IFNOT(err, "Data name did not match 'HD'", 0);
	stucDecodeValue(pByteString, (U8 *)&pMesh->meshAttribs.count, 32);
	pMesh->meshAttribs.pArr = pMesh->meshAttribs.count ?
		pCtx->alloc.pCalloc(pMesh->meshAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->meshAttribs);
	STUC_THROW_IFNOT(err, "Failed to decode mesh attrib meta", 0);

	stucDecodeValue(pByteString, (U8 *)&pMesh->faceAttribs.count, 32);
	pMesh->faceAttribs.pArr = pMesh->faceAttribs.count ?
		pCtx->alloc.pCalloc(pMesh->faceAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->faceAttribs);
	STUC_THROW_IFNOT(err, "Failed to decode face attrib meta", 0);

	stucDecodeValue(pByteString, (U8 *)&pMesh->cornerAttribs.count, 32);
	pMesh->cornerAttribs.pArr = pMesh->cornerAttribs.count ?
		pCtx->alloc.pCalloc(pMesh->cornerAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->cornerAttribs);
	STUC_THROW_IFNOT(err, "Failed to decode corner attrib meta", 0);

	stucDecodeValue(pByteString, (U8 *)&pMesh->edgeAttribs.count, 32);
	pMesh->edgeAttribs.pArr = pMesh->edgeAttribs.count ?
		pCtx->alloc.pCalloc(pMesh->edgeAttribs.count, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->edgeAttribs);
	STUC_THROW_IFNOT(err, "Failed to decode edge meta", 0);

	stucDecodeValue(pByteString, (U8 *)&pMesh->vertAttribs.count, 32);
	pMesh->vertAttribs.pArr = pMesh->vertAttribs.count ?
		pCtx->alloc.pCalloc(pMesh->vertAttribs.count + usesUsg, sizeof(StucAttrib)) : NULL;
	err = decodeAttribMeta(pByteString, &pMesh->vertAttribs);
	STUC_THROW_IFNOT(err, "Failed to decode vert attrib meta", 0);

	stucDecodeValue(pByteString, (U8 *)&pMesh->faceCount, 32);
	stucDecodeValue(pByteString, (U8 *)&pMesh->cornerCount, 32);
	stucDecodeValue(pByteString, (U8 *)&pMesh->edgeCount, 32);
	stucDecodeValue(pByteString, (U8 *)&pMesh->vertCount, 32);

	//set usg attrib metadata if used
	if (usesUsg) {
		Attrib *usgAttrib = pMesh->vertAttribs.pArr + pMesh->vertAttribs.count;
		usgAttrib->core.pData = pCtx->alloc.pCalloc(pMesh->vertCount, sizeof(I32));
		strncpy(
			usgAttrib->core.name,
			pCtx->spAttribNames[STUC_ATTRIB_USE_USG],
			STUC_ATTRIB_NAME_MAX_LEN
		);
		usgAttrib->origin = STUC_ATTRIB_ORIGIN_MAP;
		usgAttrib->interpolate = true;
		usgAttrib->core.type = STUC_ATTRIB_I32;
	}
	err = isDataNameInvalid(pByteString, "MA"); //mesh attribs
	STUC_THROW_IFNOT(err, "Data name did not match 'MA'", 0);
	decodeAttribs(pCtx, pByteString, &pMesh->meshAttribs, 1);
	stucStageEndWrap(pCtx);
	err = isDataNameInvalid(pByteString, "FL"); //face list
	STUC_THROW_IFNOT(err, "Data name did not match 'FL'", 0);
	pMesh->pFaces = pCtx->alloc.pCalloc(pMesh->faceCount + 1, sizeof(I32));
	stucStageBeginWrap(pCtx, "Decoding faces", pMesh->faceCount);
	for (I32 i = 0; i < pMesh->faceCount; ++i) {
		stucDecodeValue(pByteString, (U8 *)&pMesh->pFaces[i], 32);
		STUC_ASSERT("",
			pMesh->pFaces[i] >= 0 &&
			pMesh->pFaces[i] < pMesh->cornerCount
		);
		stucStageProgressWrap(pCtx, i);
	}
	stucStageEndWrap(pCtx);
	err = isDataNameInvalid(pByteString, "FA"); //face attribs
	STUC_THROW_IFNOT(err, "Data name did not match 'FA'", 0);
	pMesh->pFaces[pMesh->faceCount] = pMesh->cornerCount;
	decodeAttribs(pCtx, pByteString, &pMesh->faceAttribs, pMesh->faceCount);

	err = isDataNameInvalid(pByteString, "LL"); //corner and edge lists
	STUC_THROW_IFNOT(err, "Data name did not match 'LL'", 0);
	pMesh->pCorners = pCtx->alloc.pCalloc(pMesh->cornerCount, sizeof(I32));
	pMesh->pEdges = pCtx->alloc.pCalloc(pMesh->cornerCount, sizeof(I32));
	stucStageBeginWrap(pCtx, "Decoding corners", pMesh->cornerCount);
	for (I32 i = 0; i < pMesh->cornerCount; ++i) {
		stucDecodeValue(pByteString, (U8 *)&pMesh->pCorners[i], 32);
		STUC_ASSERT("",
			pMesh->pCorners[i] >= 0 &&
			pMesh->pCorners[i] < pMesh->vertCount
		);
		stucDecodeValue(pByteString, (U8 *)&pMesh->pEdges[i], 32);
		STUC_ASSERT("",
			pMesh->pEdges[i] >= 0 &&
			pMesh->pEdges[i] < pMesh->edgeCount
		);
		stucStageProgressWrap(pCtx, i);
	}
	stucStageEndWrap(pCtx);

	err = isDataNameInvalid(pByteString, "LA"); //corner attribs
	STUC_THROW_IFNOT(err, "Data name did not match 'LA'", 0);
	decodeAttribs(pCtx, pByteString, &pMesh->cornerAttribs, pMesh->cornerCount);
	err = isDataNameInvalid(pByteString, "EA"); //edge attribs
	STUC_THROW_IFNOT(err, "Data name did not match 'EA'", 0);
	decodeAttribs(pCtx, pByteString, &pMesh->edgeAttribs, pMesh->edgeCount);
	err = isDataNameInvalid(pByteString, "VA"); //vert attribs
	STUC_THROW_IFNOT(err, "Data name did not match 'VA'", 0);
	decodeAttribs(pCtx, pByteString, &pMesh->vertAttribs, pMesh->vertCount);

	err = isDataNameInvalid(pByteString, "OE"); //obj end
	STUC_THROW_IFNOT(err, "Data name did not match 'OE'", 0);
	if (usesUsg) {
		pMesh->vertAttribs.count++;
	}
	//TODO add STUC_ERROR and STUC_CATCH to all functions that return StucResult
	STUC_CATCH(0, err,
		//if error:
		stucMeshDestroy(pCtx, pMesh);
		pCtx->alloc.pFree(pMesh);
	);
	return err;
}

static
StucResult decodeStucData(
	StucContext pCtx,
	StucHeader *pHeader,
	ByteString *dataByteString,
	StucObject **ppObjArr,
	StucUsg **ppUsgArr,
	StucObject **ppFlatCutoffArr,
	bool forEdit,
	AttribIndexedArr *pIndexedAttribs
) {
	StucResult err = STUC_NOT_SET;
	if (pIndexedAttribs && pIndexedAttribs->count) {
		STUC_ASSERT("", pIndexedAttribs->count > 0);
		pIndexedAttribs->pArr =
			pCtx->alloc.pCalloc(pIndexedAttribs->count, sizeof(AttribIndexed));
		pIndexedAttribs->size = pIndexedAttribs->count;
		decodeIndexedAttribMeta(dataByteString, pIndexedAttribs);
		decodeIndexedAttribs(pCtx, dataByteString, pIndexedAttribs);
	}
	if (pHeader->objCount) {
		*ppObjArr = pCtx->alloc.pCalloc(pHeader->objCount, sizeof(StucObject));
		STUC_ASSERT("", pHeader->usgCount >= 0);
		bool usesUsg = pHeader->usgCount > 0 && !forEdit;
		for (I32 i = 0; i < pHeader->objCount; ++i) {
			//usgUsg is passed here to indicate that an extra vert
			//attrib should be created. This would be used later to mark a verts
			//respective usg.
			err = loadObj(pCtx, *ppObjArr + i, dataByteString, usesUsg);
			if (err != STUC_SUCCESS) {
				return err;
			}
		}
	}
	else {
		return STUC_ERROR;
	}

	if (pHeader->usgCount) {
		*ppUsgArr = pCtx->alloc.pCalloc(pHeader->usgCount, sizeof(StucUsg));
		*ppFlatCutoffArr = pCtx->alloc.pCalloc(pHeader->flatCutoffCount, sizeof(StucObject));
		for (I32 i = 0; i < pHeader->flatCutoffCount; ++i) {
			err = loadObj(pCtx, *ppFlatCutoffArr + i, dataByteString, false);
			if (err != STUC_SUCCESS) {
				return err;
			}
		}
		for (I32 i = 0; i < pHeader->usgCount; ++i) {
			//usgs themselves don't need a usg attrib, so false is passed
			err = loadObj(pCtx, &(*ppUsgArr)[i].obj, dataByteString, false);
			if (err != STUC_SUCCESS) {
				return err;
			}
			err = isDataNameInvalid(dataByteString, "FC");
			if (err != STUC_SUCCESS) {
				return err;
			}
			bool hasFlatCutoff = false;
			stucDecodeValue(dataByteString, (U8 *)&hasFlatCutoff, 8);
			if (hasFlatCutoff) {
				I32 cutoffIdx = 0;
				stucDecodeValue(dataByteString, (U8 *)&cutoffIdx, 32);
				STUC_ASSERT("",
					cutoffIdx >= 0 &&
					cutoffIdx < pHeader->flatCutoffCount
				);
				(*ppUsgArr)[i].pFlatCutoff = *ppFlatCutoffArr + cutoffIdx;
			}
		}
	}
	return STUC_SUCCESS;
}

StucResult stucLoadStucFile(
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
	StucResult err = STUC_SUCCESS;
	ByteString headerByteString = {0};
	ByteString dataByteString = {0};
	void *pFile = NULL;
	U8 *dataByteStringRaw = NULL;
	printf("Loading STUC file: %s\n", filePath);
	err = pCtx->io.pOpen(&pFile, filePath, 1, &pCtx->alloc);
	STUC_THROW_IFNOT(err, "", 0);
	I16 headerSize = 0;
	err = pCtx->io.pRead(pFile, (U8 *)&headerSize, 2);
	STUC_THROW_IFNOT(err, "", 0);
	printf("Stuc File Header Size: %d\n", headerSize);
	printf("Header is %d bytes\n", headerSize);
	headerByteString.pString = pCtx->alloc.pMalloc(headerSize);
	printf("Reading header\n");
	err = pCtx->io.pRead(pFile, headerByteString.pString, headerSize);
	STUC_THROW_IFNOT(err, "", 0);
	printf("Decoding header\n");
	StucHeader header = decodeStucHeader(&headerByteString, pIndexedAttribs);
	STUC_THROW_IFNOT_COND(
		err,
		!strncmp(header.format, "UV Stucco Map File", MAP_FORMAT_NAME_MAX_LEN),
		"map file is corrupt",
		0
	);
	STUC_THROW_IFNOT_COND(
		err, 
		header.version == STUC_MAP_VERSION,
		"map file version not supported",
		0
	);
	dataByteStringRaw = pCtx->alloc.pMalloc(header.dataSize);
	uLong dataSizeUncompressed = (uLong)header.dataSize;
	printf("Reading data\n");
	err = pCtx->io.pRead(pFile, dataByteStringRaw, (I32)header.dataSizeCompressed);
	STUC_THROW_IFNOT(err, "", 0);
	dataByteString.pString = pCtx->alloc.pMalloc(header.dataSize);
	printf("Decompressing data\n");
	I32 zResult = uncompress(
		dataByteString.pString,
		&dataSizeUncompressed,
		dataByteStringRaw,
		(uLong)header.dataSizeCompressed
	);
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
	STUC_THROW_IFNOT(err, "", 0);
	*pObjCount = header.objCount;
	*pUsgCount = header.usgCount;
	*pFlatCutoffCount = header.flatCutoffCount;

	STUC_CATCH(0, err, ;);
	if (pFile) {
		err = pCtx->io.pClose(pFile);
	}
	if (dataByteStringRaw) {
		pCtx->alloc.pFree(dataByteStringRaw);
	}
	if (headerByteString.pString) {
		pCtx->alloc.pFree(headerByteString.pString);
	}
	if (dataByteString.pString) {
		pCtx->alloc.pFree(dataByteString.pString);
	}
	return err;
}

void stucIoSetCustom(StucContext pCtx, StucIo *pIo) {
	if (!pIo->pOpen || !pIo->pClose || !pIo->pWrite || !pIo->pRead) {
		printf("Failed to set custom IO. One or more functions were NULL");
		abort();
	}
	pCtx->io = *pIo;
}

void stucIoSetDefault(StucContext pCtx) {
	pCtx->io.pOpen = stucPlatformFileOpen;
	pCtx->io.pClose = stucPlatformFileClose;
	pCtx->io.pWrite = stucPlatformFileWrite;
	pCtx->io.pRead = stucPlatformFileRead;
}
