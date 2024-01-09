#define DECL_SPEC_EXPORT
#ifdef PLATFORM_WIN
	#define DECL_SPEC_EXPORT __declspec(dllexport)
#endif

#include <string.h>
#include <stdio.h>
#include "Types.h"
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include "Platform.h"
#include <miniz.h>
#include "QuadTree.h"
#include "Io.h"
#include <sys/time.h>

// TODO
// - Reduce the bits written to the UVGP file for vert and loop indices, based on the total amount, in order to save space.
//   No point storing them as 32 bit if there's only like 4,000 verts
// - Split compressed data into chunks maybe?
// - Split whole quadtree into chunks?

int32_t cellIndex;
int32_t leafAmount;

float tempTime = 0;

DECL_SPEC_EXPORT void UvgpExportUvgpFile(int32_t vertAmount, float *vertBuffer,
                                         int32_t loopAmount, int32_t *loopBuffer,
                                         int32_t faceAmount, int32_t *faceBuffer) {
	printf("%d vertices, and %d faces\n", vertAmount, faceAmount);
	writeUvgpFile(vertAmount, vertBuffer, faceAmount, faceBuffer);
}

DECL_SPEC_EXPORT void uvgpLoadUvgpFile(char *filePath, void *uvgpFile) {
	uvgpFile = calloc(1, sizeof(UvgpFileLoaded));
	UvgpFileLoaded *fileLoaded = uvgpFile;
	loadUvgpFile(fileLoaded, filePath);
	createQuadTree(&fileLoaded->quadTree.rootCell, &fileLoaded->quadTree.maxTreeDepth,
	               fileLoaded->header.vertAmount, fileLoaded->data.vertBuffer);
	writeDebugImage(fileLoaded->quadTree.rootCell);
}

DECL_SPEC_EXPORT void uvgpUnloadUvgpFile(void *uvgpFile) {
	UvgpFileLoaded *fileLoaded = uvgpFile;
	destroyQuadTree(fileLoaded->quadTree.rootCell, fileLoaded->quadTree.maxTreeDepth);
	destroyUvgpFile(fileLoaded);
}

DECL_SPEC_EXPORT void uvgpProjectOntoMesh(int32_t vertAmount, float *vertBuffer,
                                          int32_t loopAmount, int32_t *loopBuffer,
                                          int32_t faceAmount, int32_t *faceBuffer,
										  int32_t edgeAmount, int32_t *edgeBuffer,
                                          int32_t vertUvgpAmount, float *vertUvgpBuffer,
                                          int32_t loopUvgpAmount, int32_t *loopUvgpBuffer,
                                          int32_t faceUvgpAmount, int32_t *faceUvgpBuffer,
										  int32_t edgeUvgpAmount, int32_t *edgeUvgpBuffer) {
	printf("vertBuffer address: %p\n", vertBuffer);
	int32_t vertBufferLength = vertAmount * 3;
	int32_t vertUvgpBufferLength = vertUvgpAmount * 3;
	for (int32_t i = 0; i < vertUvgpBufferLength; ++i) {
		int32_t iMod = i % vertBufferLength;
		vertUvgpBuffer[i] = vertBuffer[iMod];
	}
	for (int32_t i = 0; i < loopUvgpAmount; ++i) {
		int32_t iMod = i % loopAmount;
		int32_t offset = vertAmount * (i >= loopAmount);
		loopUvgpBuffer[i] = loopBuffer[iMod] + offset;
	}
	for (int32_t i = 0; i < faceUvgpAmount; ++i) {
		int32_t iMod = i % faceAmount;
		int32_t offset = loopAmount * (i >= faceAmount);
		faceUvgpBuffer[i] = faceBuffer[iMod] + offset;
	}
	/*
	int32_t edgeBufferLength = edgeAmount * 2;
	int32_t edgeUvgpBufferLength = edgeUvgpAmount * 2;
	for (int32_t i = 0; i < edgeUvgpBufferLength; ++i) {
		int32_t iMod = i % edgeBufferLength;
		int32_t offset = vertAmount * (i >= edgeBufferLength);
		edgeUvgpBuffer[i] = edgeBuffer[iMod] + offset;
	}
	*/
	struct timespec start, stop;
	clock_gettime(CLOCK_REALTIME, &start);
	clock_gettime(CLOCK_REALTIME, &stop);
	float timeDiff = (float)(stop.tv_sec - start.tv_sec) + (float)(stop.tv_nsec - start.tv_nsec) / 100.0;
	tempTime += .05;
	float theta = tempTime;
	printf("Theta: %f\n", theta);
	float sinTheta = sin(theta);
	float cosTheta = cos(theta);
	for (int32_t i = 0; i < vertUvgpBufferLength; i += 3) {
		//vertUvgpBuffer[i] += tempTime;
		//vertUvgpBuffer[i + 1] += tempTime;
		float x = vertUvgpBuffer[i];
		float y = vertUvgpBuffer[i + 1];
		vertUvgpBuffer[i] = x * cosTheta - y * sinTheta;
		vertUvgpBuffer[i + 1] = x * sinTheta + y * cosTheta;
		vertUvgpBuffer[i + 2] += .5 * (i >= vertBufferLength);
	}
}
