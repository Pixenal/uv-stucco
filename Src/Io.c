#define VERT_ATTRIBUTE_AMOUNT 3
#define LOOP_ATTRIBUTE_AMOUNT 3
#define ENCODE_DECODE_BUFFER_LENGTH 34

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

#include <zlib.h>

#include <Types.h>
#include <Io.h>
#include <QuadTree.h>
#include <MapFile.h>
#include <Context.h>
#include <Platform.h>

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

static void encodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits) {
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
}

static void encodeString(ByteString *byteString, uint8_t *string) {
	int32_t lengthInBits = (strlen((char *)string) + 1) * 8;
	int32_t lengthInBytes = lengthInBits / 8;
	byteString->byteIndex += byteString->nextBitIndex > 0;
	byteString->nextBitIndex = 0;
	for (int32_t i = 0; i < lengthInBytes; ++i) {
		byteString->pString[byteString->byteIndex] = string[i];
		byteString->byteIndex++;
	}
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

/*
static void ruvmWriteDebugImage(Cell *rootCell) {
	FILE* file;
	file = fopen("/run/media/calebdawson/Tuna/workshop_folders/RUVM/DebugOutput_LoadedFile.ppm", "w");
	fprintf(file, "P3\n%d %d\n255\n", 512, 512);
	for (int32_t i = 0; i < 512; ++i) {
		for (int32_t j = 0; j < 512; ++j) {
			unsigned char red, green, blue, alpha;
			float pixelSize = 1.0 / (float)512;
			Vec2 pixelPos = {.x = pixelSize * j, .y = pixelSize * i};
			Cell *enclosingCell = findEnclosingCell(rootCell, pixelPos);
			red = (unsigned char)(enclosingCell->cellIndex % 256);
			green = (unsigned char)(enclosingCell->cellIndex << 3 % 256);
			blue = (unsigned char)(enclosingCell->cellIndex << 1 % 256);
			alpha = (unsigned char)1.0;
			fprintf(file, "%d %d %d\n", red, green, blue);
		}
	}
	fclose(file);
}
*/

static int32_t getTotalAttribSize(RuvmAttrib *pAttribs, int32_t attribCount) {
	int32_t totalSize = 0;
	for (int32_t i = 0; i < attribCount; ++i) {
		totalSize += getAttribSize(pAttribs[i].type) * 8;
	}
	return totalSize;
}

static int32_t getTotalAttribMetaSize(RuvmAttrib *pAttribs, int32_t attribCount) {
	int32_t totalSize = attribCount * 16;
	for (int32_t i = 0; i < attribCount; ++i) {
		totalSize += (strlen(pAttribs[i].name) + 1) * 8;
	}
	return totalSize;
}

static void encodeAttribs(ByteString *pData, RuvmAttrib *pAttribs,
                          int32_t attribCount, int32_t dataLen) {
	for (int32_t i = 0; i < attribCount; ++i) {
		if (pAttribs[i].type == RUVM_ATTRIB_STRING) {
			for (int32_t j = 0; j < dataLen; ++j) {
				void *pString = attribAsVoid(pAttribs + i, j);
				encodeString(pData, pString);
			}
		}
		else {
			int32_t attribSize = getAttribSize(pAttribs[i].type) * 8;
			for (int32_t j = 0; j < dataLen; ++j) {
				encodeValue(pData, attribAsVoid(pAttribs + i, j), attribSize);
			}
		}
	}
}

static void encodeAttribMeta(ByteString *pData, RuvmAttrib *pAttribs,
                             int32_t attribCount) {
	for (int32_t i = 0; i < attribCount; ++i) {
		encodeValue(pData, (uint8_t *)&pAttribs[i].type, 16);
		encodeString(pData, (uint8_t *)pAttribs[i].name);
	}
}

void ruvmWriteRuvmFile(RuvmContext pContext, RuvmMesh *pMesh) {
	ByteString header;
	ByteString data;

	//calculate total size of attribute header info
	int64_t meshAttribMetaSize =
		getTotalAttribMetaSize(pMesh->pMeshAttribs, pMesh->meshAttribCount);
	int64_t faceAttribMetaSize =
		getTotalAttribMetaSize(pMesh->pFaceAttribs, pMesh->faceAttribCount);
	int64_t loopAttribMetaSize =
		getTotalAttribMetaSize(pMesh->pLoopAttribs, pMesh->loopAttribCount);
	int64_t edgeAttribMetaSize =
		getTotalAttribMetaSize(pMesh->pEdgeAttribs, pMesh->edgeAttribCount);
	int64_t vertAttribMetaSize =
		getTotalAttribMetaSize(pMesh->pVertAttribs, pMesh->vertAttribCount);

	int64_t headerSizeInBits = 64 + //compressed data size
	                           64 + //uncompressed data size
							   meshAttribMetaSize +
	                           32 + //face attrib count
	                           faceAttribMetaSize +
							   32 + //loop attrib count
							   loopAttribMetaSize +
							   32 + //edge attrib count
							   edgeAttribMetaSize +
							   32 + //vert attrib count
							   vertAttribMetaSize +
	                           32 + //face count
	                           32 + //loop count
							   32 + //edge count
	                           32 + //vert count
							   128; //reserved for future extensions

	//calculate total size of attribute data
	int64_t meshAttribSize =
		getTotalAttribSize(pMesh->pMeshAttribs, pMesh->meshAttribCount);
	int64_t faceAttribSize =
		getTotalAttribSize(pMesh->pFaceAttribs, pMesh->faceAttribCount);
	int64_t loopAttribSize =
		getTotalAttribSize(pMesh->pLoopAttribs, pMesh->loopAttribCount);
	int64_t edgeAttribSize =
		getTotalAttribSize(pMesh->pEdgeAttribs, pMesh->edgeAttribCount);
	int64_t vertAttribSize =
		getTotalAttribSize(pMesh->pVertAttribs, pMesh->vertAttribCount);

	int64_t dataSizeInBits = meshAttribSize +
	                         32 * (int64_t)pMesh->faceCount + //faces
	                         faceAttribSize * pMesh->faceCount + //face attributes
							 32 * (int64_t)pMesh->loopCount + //loops
							 32 * (int64_t)pMesh->loopCount + //edges
	                         loopAttribSize * pMesh->loopCount + //loop attributes
							 edgeAttribSize * pMesh->edgeCount + //edge attributes
							 vertAttribSize * pMesh->vertCount; //vert attributes
	
	int32_t headerSizeInBytes = (int32_t)(headerSizeInBits / 8 + 2);
	int64_t dataSizeInBytes = dataSizeInBits / 8 + 2;
	data.byteIndex = 0;
	data.nextBitIndex = 0;
	header.byteIndex = 0;
	header.nextBitIndex = 0;

	//encode data
	data.pString = pContext->alloc.pCalloc(dataSizeInBytes, 1);
	encodeAttribs(&data, pMesh->pMeshAttribs, pMesh->meshAttribCount, 1);
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pFaces[i], 32);
	}
	encodeAttribs(&data, pMesh->pFaceAttribs, pMesh->faceAttribCount,
	              pMesh->faceCount);
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		encodeValue(&data, (uint8_t *)&pMesh->pLoops[i], 32);
		encodeValue(&data, (uint8_t *)&pMesh->pEdges[i], 32);
	}
	encodeAttribs(&data, pMesh->pLoopAttribs, pMesh->loopAttribCount,
	              pMesh->loopCount);
	encodeAttribs(&data, pMesh->pEdgeAttribs, pMesh->edgeAttribCount,
	              pMesh->edgeCount);
	encodeAttribs(&data, pMesh->pVertAttribs, pMesh->vertAttribCount,
	              pMesh->vertCount);

	//compress data
	//TODO convert to use proper zlib inflate and deflate calls
	//compress and decompress are not context independent iirc
	int64_t dataSize = data.byteIndex + (data.nextBitIndex > 0);
	int64_t dataSizeExtra = dataSize / 1000;
	dataSizeExtra += ((dataSize * 1000) - dataSize) > 0;
	dataSizeExtra += 12;
	unsigned long compressedDataSize = dataSize + dataSizeExtra;
	uint8_t *compressedData = pContext->alloc.pMalloc(compressedDataSize);
	int32_t zResult = compress(compressedData, &compressedDataSize, data.pString, dataSize);
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

	printf("Compressed data is %lu long\n", compressedDataSize);

	//encode header
	header.pString = pContext->alloc.pCalloc(headerSizeInBytes, 1);
	int32_t version = 100;
	encodeValue(&header, (uint8_t *)&version, 16);
	encodeValue(&header, (uint8_t *)&compressedDataSize, 64);
	encodeValue(&header, (uint8_t *)&dataSize, 64);

	encodeValue(&header, (uint8_t *)&pMesh->meshAttribCount, 32);
	encodeAttribMeta(&header, pMesh->pMeshAttribs, pMesh->meshAttribCount);
	encodeValue(&header, (uint8_t *)&pMesh->faceAttribCount, 32);
	encodeAttribMeta(&header, pMesh->pFaceAttribs, pMesh->faceAttribCount);
	encodeValue(&header, (uint8_t *)&pMesh->loopAttribCount, 32);
	encodeAttribMeta(&header, pMesh->pLoopAttribs, pMesh->loopAttribCount);
	encodeValue(&header, (uint8_t *)&pMesh->edgeAttribCount, 32);
	encodeAttribMeta(&header, pMesh->pEdgeAttribs, pMesh->edgeAttribCount);
	encodeValue(&header, (uint8_t *)&pMesh->vertAttribCount, 32);
	encodeAttribMeta(&header, pMesh->pVertAttribs, pMesh->vertAttribCount);

	encodeValue(&header, (uint8_t *)&pMesh->faceCount, 32);
	encodeValue(&header, (uint8_t *)&pMesh->loopCount, 32);
	encodeValue(&header, (uint8_t *)&pMesh->edgeCount, 32);
	encodeValue(&header, (uint8_t *)&pMesh->vertCount, 32);

	//TODO CRC for uncompressed data
	
	void *pFile;
	pContext->io.pOpen(&pFile, "/run/media/calebdawson/Tuna/workshop_folders/RUVM/TestOutputDir/HardSurface.ruvm",
	                   0, &pContext->alloc);
	//pContext->io.pOpen(&pFile, "T:/workshop_folders/RUVM/TestOutputDir/CrownfallWall.ruvm",
	//                   0, &pContext->alloc);
	headerSizeInBytes = header.byteIndex + (header.nextBitIndex > 0);
	pContext->io.pWrite(pFile, (uint8_t *)&headerSizeInBytes, 4);
	pContext->io.pWrite(pFile, header.pString, headerSizeInBytes);
	pContext->io.pWrite(pFile, compressedData, (int32_t)compressedDataSize);
	pContext->io.pClose(pFile);

	pContext->alloc.pFree(header.pString);
	pContext->alloc.pFree(data.pString);

	printf("Finished RUVM export\n");
}

static void decodeAttribMeta(ByteString *pData, RuvmAttrib *pAttribs, int32_t attribCount) {
	for (int32_t i = 0; i < attribCount; ++i) {
		decodeValue(pData, (uint8_t *)&pAttribs[i].type, 16);
		int32_t maxNameLen = sizeof(pAttribs[i].name);
		decodeString(pData, (char *)&pAttribs[i].name, maxNameLen);
	}
}

static void decodeAttribs(RuvmContext pContext, ByteString *pData,
                          RuvmAttrib *pAttribs, int32_t attribCount,
						  int32_t dataLen) {
	for (int32_t i = 0; i < attribCount; ++i) {
		int32_t attribSize = getAttribSize(pAttribs[i].type) * 8;
		pAttribs[i].pData = dataLen ?
			pContext->alloc.pCalloc(dataLen, attribSize) : NULL;
		for (int32_t j = 0; j < dataLen; ++j) {
			void *pAttribData = attribAsVoid(pAttribs + i, j);
			if (pAttribs[i].type == RUVM_ATTRIB_STRING) {
				decodeString(pData, pAttribData, attribSize);
			}
			else {
				decodeValue(pData, pAttribData, attribSize);
			}
		}
	}
}

static RuvmHeader decodeRuvmHeader(RuvmContext pContext, RuvmMap pMapFile, ByteString *headerByteString) {
	RuvmHeader header = {0};
	RuvmMesh *pMesh = &pMapFile->mesh.mesh;
	//printf("0\n");
	decodeValue(headerByteString, (uint8_t *)&header.version, 16);
	decodeValue(headerByteString, (uint8_t *)&header.dataSizeCompressed, 64);
	decodeValue(headerByteString, (uint8_t *)&header.dataSize, 64);

	decodeValue(headerByteString, (uint8_t *)&pMesh->meshAttribCount, 32);
	pMesh->pMeshAttribs = pMesh->meshAttribCount ?
		pContext->alloc.pCalloc(pMesh->meshAttribCount, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, pMesh->pMeshAttribs, pMesh->meshAttribCount);

	decodeValue(headerByteString, (uint8_t *)&pMesh->faceAttribCount, 32);
	pMesh->pFaceAttribs = pMesh->faceAttribCount ?
		pContext->alloc.pCalloc(pMesh->faceAttribCount, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, pMesh->pFaceAttribs, pMesh->faceAttribCount);

	decodeValue(headerByteString, (uint8_t *)&pMesh->loopAttribCount, 32);
	pMesh->pLoopAttribs = pMesh->loopAttribCount ?
		pContext->alloc.pCalloc(pMesh->loopAttribCount, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, pMesh->pLoopAttribs, pMesh->loopAttribCount);

	decodeValue(headerByteString, (uint8_t *)&pMesh->edgeAttribCount, 32);
	pMesh->pEdgeAttribs = pMesh->edgeAttribCount ?
		pContext->alloc.pCalloc(pMesh->edgeAttribCount, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, pMesh->pEdgeAttribs, pMesh->edgeAttribCount);

	decodeValue(headerByteString, (uint8_t *)&pMesh->vertAttribCount, 32);
	pMesh->pVertAttribs = pMesh->vertAttribCount ?
		pContext->alloc.pCalloc(pMesh->vertAttribCount, sizeof(RuvmAttrib)) : NULL;
	decodeAttribMeta(headerByteString, pMesh->pVertAttribs, pMesh->vertAttribCount);

	decodeValue(headerByteString, (uint8_t *)&pMesh->faceCount, 32);
	decodeValue(headerByteString, (uint8_t *)&pMesh->loopCount, 32);
	decodeValue(headerByteString, (uint8_t *)&pMesh->edgeCount, 32);
	decodeValue(headerByteString, (uint8_t *)&pMesh->vertCount, 32);

	return header;
}

static void decodeRuvmData(RuvmContext pContext, RuvmMap pMapFile,
                           ByteString *dataByteString) {
	RuvmMesh *pMesh = &pMapFile->mesh.mesh;

	decodeAttribs(pContext, dataByteString, pMesh->pMeshAttribs, pMesh->meshAttribCount, 1);

	pMesh->pFaces = pContext->alloc.pCalloc(pMesh->faceCount + 1, sizeof(int32_t));
	for (int32_t i = 0; i < pMesh->faceCount; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pFaces[i], 32);
	}
	pMesh->pFaces[pMesh->faceCount] = pMesh->loopCount;
	decodeAttribs(pContext, dataByteString, pMesh->pFaceAttribs, pMesh->faceAttribCount, pMesh->faceCount);

	pMesh->pLoops = pContext->alloc.pCalloc(pMesh->loopCount, sizeof(int32_t));
	pMesh->pEdges = pContext->alloc.pCalloc(pMesh->loopCount, sizeof(int32_t));
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		decodeValue(dataByteString, (uint8_t *)&pMesh->pLoops[i], 32);
		decodeValue(dataByteString, (uint8_t *)&pMesh->pEdges[i], 32);
	}
	decodeAttribs(pContext, dataByteString, pMesh->pLoopAttribs, pMesh->loopAttribCount, pMesh->loopCount);
	decodeAttribs(pContext, dataByteString, pMesh->pEdgeAttribs, pMesh->edgeAttribCount, pMesh->edgeCount);

	decodeAttribs(pContext, dataByteString, pMesh->pVertAttribs, pMesh->vertAttribCount, pMesh->vertCount);
}

void ruvmLoadRuvmFile(RuvmContext pContext, RuvmMap pMapFile, char *filePath) {
	ByteString headerByteString = {0};
	ByteString dataByteString = {0};
	void *pFile;
	printf("Loading RUVM file: %s\n", filePath);
	pContext->io.pOpen(&pFile, filePath, 1, &pContext->alloc);
	uint8_t headerSize[4];
	pContext->io.pRead(pFile, headerSize, 4);
	int32_t headerSizeInt = *((int32_t *)headerSize);
	printf("Ruvm File Header Size: %d\n", headerSizeInt);
	printf("Header is %d bytes\n", headerSizeInt);
	headerByteString.pString = pContext->alloc.pMalloc(headerSizeInt);
	printf("Reading header\n");
	pContext->io.pRead(pFile, headerByteString.pString, headerSizeInt);
	printf("Decoding header\n");
	RuvmHeader header = decodeRuvmHeader(pContext, pMapFile, &headerByteString);
	uint8_t *dataByteStringRaw = pContext->alloc.pMalloc(header.dataSize);
	unsigned long dataSizeUncompressed = header.dataSize;
	printf("Reading data\n");
	pContext->io.pRead(pFile, dataByteStringRaw, header.dataSizeCompressed);
	pContext->io.pClose(pFile);
	dataByteString.pString = pContext->alloc.pMalloc(header.dataSize);
	printf("Decompressing data\n");
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
	decodeRuvmData(pContext, pMapFile, &dataByteString);
	pContext->alloc.pFree(headerByteString.pString);
	pContext->alloc.pFree(dataByteString.pString);
	RuvmMesh *pMesh = &pMapFile->mesh.mesh;
	pMapFile->mesh.pVertAttrib =
		getAttrib("position", pMesh->pVertAttribs, pMesh->vertAttribCount);
	pMapFile->mesh.pVerts = pMapFile->mesh.pVertAttrib->pData;
	pMapFile->mesh.pUvAttrib =
		getAttrib("UVMap", pMesh->pLoopAttribs, pMesh->loopAttribCount);
	pMapFile->mesh.pUvs = pMapFile->mesh.pUvAttrib->pData;
	pMapFile->mesh.pNormalAttrib =
		getAttrib("normal", pMesh->pLoopAttribs, pMesh->loopAttribCount);
	pMapFile->mesh.pNormals = pMapFile->mesh.pNormalAttrib->pData;
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
