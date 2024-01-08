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

// TODO
// - Reduce the bits written to the UVGP file for vert and loop indices, based on the total amount, in order to save space.
//   No point storing them as 32 bit if there's only like 4,000 verts
// - Split compressed data into chunks maybe?
// - Split whole quadtree into chunks?

int32_t cellIndex;
int32_t leafAmount;

DECL_SPEC_EXPORT void UvgpExportUvgpFile(int32_t vertAmount, Vert *vertBuffer,
                                         int32_t faceAmount, Face *faceBuffer) {
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

DECL_SPEC_EXPORT void uvgpProjectOntoMesh(const char *objName, int32_t vertAmount,
                                          Vert *vertBuffer, int32_t faceAmount, Face *faceBuffer) {
	printf("Obj name: %s\n", objName);
}
