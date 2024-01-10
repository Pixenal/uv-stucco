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

typedef struct {
	float x;
	float y;
	float z;
} BlenderVert;

typedef struct {
	float u;
	float v;
} BlenderUv;

typedef struct {
	int32_t vertAmount;
	BlenderVert *vertBuffer;
	int32_t loopAmount;
	int32_t *loopBuffer;
	int32_t faceAmount;
	int32_t *faceBuffer;
	BlenderUv *uvBuffer;
} BlenderMeshData;

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

DECL_SPEC_EXPORT void uvgpProjectOntoMesh(BlenderMeshData *mesh, BlenderMeshData *workMesh) {
	workMesh->vertAmount = mesh->vertAmount;
	workMesh->vertBuffer = malloc(sizeof(BlenderVert) * mesh->vertAmount);
	workMesh->loopAmount = mesh->loopAmount;
	workMesh->loopBuffer = malloc(sizeof(int32_t) * mesh->loopAmount);
	workMesh->faceAmount = mesh->faceAmount;
	workMesh->faceBuffer = malloc(sizeof(int32_t) * mesh->faceAmount);
	workMesh->uvBuffer = malloc(sizeof(BlenderUv) * mesh->loopAmount);

	memcpy(workMesh->vertBuffer, mesh->vertBuffer, sizeof(BlenderVert) * workMesh->vertAmount);
	memcpy(workMesh->loopBuffer, mesh->loopBuffer, sizeof(int32_t) * workMesh->loopAmount);
	memcpy(workMesh->faceBuffer, mesh->faceBuffer, sizeof(int32_t) * workMesh->faceAmount);
	memcpy(workMesh->uvBuffer, mesh->uvBuffer, sizeof(BlenderUv) * workMesh->loopAmount);
}

DECL_SPEC_EXPORT void uvgpUpdateMesh(BlenderMeshData *uvgpMesh, BlenderMeshData *workMesh) {
	memcpy(uvgpMesh->vertBuffer, workMesh->vertBuffer, sizeof(BlenderVert) * uvgpMesh->vertAmount);
	memcpy(uvgpMesh->loopBuffer, workMesh->loopBuffer, sizeof(int32_t) * uvgpMesh->loopAmount);
	memcpy(uvgpMesh->faceBuffer, workMesh->faceBuffer, sizeof(int32_t) * uvgpMesh->faceAmount);
	free(workMesh->vertBuffer);
	free(workMesh->loopBuffer);
	free(workMesh->faceBuffer);
}

DECL_SPEC_EXPORT void uvgpUpdateMeshUv(BlenderMeshData *uvgpMesh, BlenderMeshData *workMesh) {
	memcpy(uvgpMesh->uvBuffer, workMesh->uvBuffer, sizeof(BlenderUv) * uvgpMesh->loopAmount);
	free(workMesh->uvBuffer);
}
