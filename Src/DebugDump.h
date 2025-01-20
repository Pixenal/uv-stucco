#pragma once

#include <float.h>

#include <RuvmInternal.h>
#include <Io.h>

static inline
void dumpBoundsFaceToFile(MergeSendOffArgs *pArgs, PieceArr *pPieceArr) {
	RuvmContext pContext = pArgs->pContext;
	int32_t totalLoops = 0;
	V2_F32 viewportMin = {FLT_MAX, FLT_MAX};
	V2_F32 viewportMax = {-FLT_MAX, -FLT_MAX};
	for (int32_t i = 0; i < pPieceArr->count; ++i) {
		Piece *pPiece = pPieceArr->pArr + i;
		totalLoops += pPiece->bufFace.size;
		BufMesh *pBufMesh = &pArgs->pJobArgs[pPiece->pEntry->job].bufMesh;
		for (int32_t j = 0; j < pPiece->bufFace.size; ++j) {
			V2_F32 uv = pBufMesh->mesh.pUvs[pPiece->bufFace.start - j];
			if (uv.d[0] < viewportMin.d[0]) {
				viewportMin.d[0] = uv.d[0];
			}
			if (uv.d[1] < viewportMin.d[1]) {
				viewportMin.d[1] = uv.d[1];
			}
			if (uv.d[0] > viewportMax.d[0]) {
				viewportMax.d[0] = uv.d[0];
			}
			if (uv.d[1] > viewportMax.d[1]) {
				viewportMax.d[1] = uv.d[1];
			}
		}
	}
	int64_t bitSize = 0;
	char openBrkt = '{';
	char closeBrkt = '}';
	char colon = ':';
	char comma = ',';
	char quote = '"';
	int32_t headerLen = 200;
	int32_t loopObjLen = 150;
	int32_t bufSize = headerLen + loopObjLen * totalLoops;
	ByteString string = {0};
	string.pString = pContext->alloc.pCalloc(bufSize, 1);
#define DUMP_STR_BUF_SIZE 400
	char stringBuf[DUMP_STR_BUF_SIZE] = {0};
	snprintf(&stringBuf, DUMP_STR_BUF_SIZE, "{\"version\":5,\"graph\":{\"viewport\":{\"xmin\":%f,\"ymin\":%f,\"xmax\":%f,\"ymax\":%f}},\"expressions\":{\"list\":[",
	         viewportMin.d[0], viewportMin.d[1], viewportMax.d[0], viewportMax.d[1]);
	encodeString(&string, stringBuf, &bitSize);
	string.byteIndex--; //remove null terminator
	memset(stringBuf, 0, DUMP_STR_BUF_SIZE);
	int32_t id = 0;
	char colors[6][10] = {"\"#fa7e19\"", "\"#2d70b3\"", "\"#388c46\"", "\"#6042a6\"", "\"#000000\"", "\"#c74440\""};
	for (int32_t i = 0; i < pPieceArr->count; ++i) {
		if (i) {
			encodeValue(&string, (uint8_t *)&comma, 8, &bitSize);
		}
		Piece *pPiece = pPieceArr->pArr + i;
		BufMesh *pBufMesh = &pArgs->pJobArgs[pPiece->pEntry->job].bufMesh;
		for (int32_t j = 0; j < pPiece->bufFace.size; ++j) {
			BorderInInfo inInfo = getBorderEntryInInfo(pPiece->pEntry, pArgs->pJobArgs, j);
			V2_F32 uv = pBufMesh->mesh.pUvs[pPiece->bufFace.start - j];
			snprintf(stringBuf, DUMP_STR_BUF_SIZE, "{\"type\":\"expression\",\"id\":%d,\"color\":%s,\"latex\":\"a=\\\\left(%f,%f\\\\right)\",\"\label\":\"inLoop %d inEdge %d\",\"style\":\"SOLID\"},",
			         id, colors[i % 6], uv.d[0], uv.d[1], inInfo.vertLoop, inInfo.edge);
			encodeString(&string, stringBuf, &bitSize);
			string.byteIndex--; //remove null terminator
			memset(stringBuf, 0, DUMP_STR_BUF_SIZE);
			id++;
		}
		snprintf(stringBuf, DUMP_STR_BUF_SIZE, "{\"type\":\"expression\",\"id\":%d,\"color\":%s,\"style\":\"SOLID\"}",
		         id, colors[i % 6]);
		encodeString(&string, stringBuf, &bitSize);
		string.byteIndex--; //remove null terminator
		memset(stringBuf, 0, DUMP_STR_BUF_SIZE);
		id++;
	}
	memset(stringBuf, 0, DUMP_STR_BUF_SIZE);
	snprintf(&stringBuf, DUMP_STR_BUF_SIZE, "]}}");
	encodeString(&string, stringBuf, &bitSize);
	string.byteIndex--; //remove null terminator

	void *pFile;
	pContext->io.pOpen(&pFile, "C:/Users/scout/Desktop/RuvmBoundsFaceDump.des", 0, &pContext->alloc);
	pContext->io.pWrite(pFile, (uint8_t *)string.pString, string.byteIndex);
	pContext->io.pClose(pFile);

	pContext->alloc.pFree(string.pString);
}