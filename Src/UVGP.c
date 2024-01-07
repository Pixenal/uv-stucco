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

DECL_SPEC_EXPORT void UvgpExportUvgpFile(int32_t vertAmount, Vert *vertBuffer, int32_t faceAmount, Face *faceBuffer) {
	printf("%d vertices, and %d faces\n", vertAmount, faceAmount);

	cellIndex = 0;
	leafAmount = 0;

	Cell *rootCell;
	int32_t maxTreeDepth;
	createQuadTree(&rootCell, &maxTreeDepth, vertAmount, vertBuffer);
	//writeDebugImage(rootCell);
	writeUvgpFileAndFreeMemory(rootCell, maxTreeDepth, vertAmount, vertBuffer, faceAmount, faceBuffer);
}

DECL_SPEC_EXPORT void uvgpLoadUvgpFile(char *filePath) {
	UvgpByteString header = {0};
	UvgpByteString data = {0};
	UvgpFileLoaded uvgpFileLoaded = {0};
	readUvgpFile(&uvgpFileLoaded, &header, &data, filePath);
}

DECL_SPEC_EXPORT void uvgpProjectOntoMesh(const char *objName, int32_t vertAmount, Vert *vertBuffer, int32_t faceAmount, Face *faceBuffer) {
	printf("Obj name: %s\n", objName);
}
