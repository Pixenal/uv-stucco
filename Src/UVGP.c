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
	int32_t vertAmount;
	Vec3 *vertBuffer;
	int32_t loopAmount;
	int32_t *loopBuffer;
	int32_t faceAmount;
	int32_t *faceBuffer;
	Vec2 *uvBuffer;
} BlenderMeshData;

static UvgpFileLoaded *fileLoaded;

DECL_SPEC_EXPORT void UvgpExportUvgpFile(int32_t vertAmount, Vec3 *vertBuffer,
                                         int32_t loopAmount, int32_t *loopBuffer, Vec3 *normalBuffer,
                                         int32_t faceAmount, int32_t *faceBuffer) {
	printf("%d vertices, and %d faces\n", vertAmount, faceAmount);
	writeUvgpFile(vertAmount, vertBuffer, loopAmount, loopBuffer, normalBuffer, faceAmount, faceBuffer);
}

DECL_SPEC_EXPORT void uvgpLoadUvgpFile(char *filePath) {
	fileLoaded = calloc(1, sizeof(UvgpFileLoaded));
	loadUvgpFile(fileLoaded, filePath);
	createQuadTree(&fileLoaded->quadTree.rootCell, &fileLoaded->quadTree.maxTreeDepth,
	               fileLoaded->header.faceAmount, fileLoaded->data.vertBuffer, fileLoaded->data.loopBuffer, fileLoaded->data.faceBuffer);
	writeDebugImage(fileLoaded->quadTree.rootCell);
}

DECL_SPEC_EXPORT void uvgpUnloadUvgpFile(void *uvgpFile) {
	UvgpFileLoaded *fileLoaded = uvgpFile;
	destroyQuadTree(fileLoaded->quadTree.rootCell, fileLoaded->quadTree.maxTreeDepth);
	destroyUvgpFile(fileLoaded);
}

int32_t isPointInPolygon(Vec2 point, int32_t faceIndex, Vec3 *verts, int32_t *loops, int32_t *faces) {
	int32_t loopTop = faces[faceIndex + 1];
	for (int32_t i = faces[faceIndex]; i < loopTop; ++i) {
	}
	return 0;
}

void clipPass(int32_t loopTop, Vec3 *loopBuffer, int32_t *newLoopTop, Vec3 *newLoopBuffer, int32_t *clipLoop, Vec3 *clipLoopVerts, Vec2 *uv, Vec2 *uvNext, Vec2 *uvCross, int32_t *insideBuffer){
	for (int32_t i = 0; i < loopTop; ++i) {
		Vec2 uvgpVert = *(Vec2 *)&loopBuffer[i];
		int32_t vertNextIndex = (i + 1) % loopTop;
		Vec2 uvgpVertNext = *(Vec2 *)&loopBuffer[vertNextIndex];
		Vec2 uvUvgpDir = _(uvgpVert V2SUB *uv);
		float dot = _(*uvCross V2DOT uvUvgpDir);
		insideBuffer[i] = dot < .0f;
	}
	for (int32_t i = 0; i < loopTop; ++i) {
		int32_t vertNextIndex = (i + 1) % loopTop;
		if (insideBuffer[i]) {
			newLoopBuffer[*newLoopTop] = loopBuffer[i];
			(*newLoopTop)++;
		}
		if (insideBuffer[i] != insideBuffer[vertNextIndex]) {
			Vec2 uvgpVert = *(Vec2 *)&loopBuffer[i];
			Vec2 uvgpVertNext = *(Vec2 *)&loopBuffer[vertNextIndex];
			float t = (uvgpVert.x - uv->x) * (uv->y - uvNext->y) -
					  (uvgpVert.y - uv->y) * (uv->x - uvNext->x);
			t /= (uvgpVert.x - uvgpVertNext.x) * (uv->y - uvNext->y) -
				 (uvgpVert.y - uvgpVertNext.y) * (uv->x - uvNext->x);
			Vec2 intersection = _(uvgpVert V2ADD _(_(uvgpVertNext V2SUB uvgpVert) V2MULS t));
			*(Vec2 *)&newLoopBuffer[*newLoopTop] = intersection;
			(*newLoopTop)++;
		}
	}
}

void checkIfVertIsDup(int32_t *vertIndex, Vec3 *loopVert, BlenderMeshData *workMesh) {
	for (int32_t i = 0; i < workMesh->vertAmount; ++i) {
		int32_t dup = _(*(Vec2 *)loopVert V2GREAT _(*(Vec2 *)&workMesh->vertBuffer[i] V2SUBS .01f)) &&
		              _(*(Vec2 *)loopVert V2LESS _(*(Vec2 *)&workMesh->vertBuffer[i] V2ADDS .01f));
		if (dup) {
			*vertIndex = i;
		}
	}
}

void addCellFaceToWorkMesh(int32_t loopTop, Vec3 *loopBuffer, BlenderMeshData *workMesh,
                           BlenderMeshData *mesh, int32_t faceStart, int32_t faceEnd){
	if (loopTop <= 2) {
		return;
	}
	int32_t vertsAdded = 0;
	for (int32_t i = 0; i < loopTop; ++i) {
		Vec3 triUv0;
		Vec3 triUv1;
		Vec3 triUv2;
		*(Vec2 *)&triUv0 = mesh->uvBuffer[faceStart];
		triUv0.z = .0f;
		*(Vec2 *)&triUv1 = mesh->uvBuffer[faceStart + 1];
		triUv1.z = .0f;
		*(Vec2 *)&triUv2 = mesh->uvBuffer[faceStart + 2];
		triUv2.z = .0f;
		Vec3 *triVert0 = mesh->vertBuffer + mesh->loopBuffer[faceStart];
		Vec3 *triVert1 = mesh->vertBuffer + mesh->loopBuffer[faceStart + 1];
		Vec3 *triVert2 = mesh->vertBuffer + mesh->loopBuffer[faceStart + 2];
		Vec3 vertBc = cartesianToBarycentric(&triUv0, &triUv1, &triUv2, loopBuffer + i);
		Vec3 vertWs = barycentricToCartesian(triVert0, triVert1, triVert2, &vertBc);
		int32_t vertIndex = -1;
		//checkIfVertIsDup(&vertIndex, &vertWs, workMesh);
		if (vertIndex < 0) {
			vertIndex = workMesh->vertAmount + i;
			workMesh->vertBuffer[vertIndex] = vertWs;
			vertsAdded++;
		}
		workMesh->loopBuffer[workMesh->loopAmount + i] = vertIndex;
	}
	workMesh->vertAmount += vertsAdded;
	workMesh->faceBuffer[workMesh->faceAmount] = workMesh->loopAmount;
	workMesh->loopAmount += loopTop;
	workMesh->faceAmount++;
}

void clipAgainstFace(int32_t *loopTop, Vec3 *loopBuffer, BlenderMeshData *mesh, int32_t loopAmount, int32_t faceStart) {
	for (int32_t i = 0; i < loopAmount; ++i) {
		Vec2 uv = mesh->uvBuffer[i + faceStart];
		int32_t uvNextIndex = ((i + 1) % loopAmount) + faceStart;
		Vec2 uvNext = mesh->uvBuffer[uvNextIndex];
		Vec2 uvDir = _(uvNext V2SUB uv);
		int32_t clipLoop[2] = {-1, -1};
		Vec3 clipLoopVerts[2];
		int32_t loopsAdded;
		Vec3 newLoopBuffer[12];
		int32_t insideBuffer[12] = {0};
		int32_t newLoopTop = 0;
		Vec2 uvCross = vec2Cross(uvDir);

		clipPass(*loopTop, loopBuffer, &newLoopTop, newLoopBuffer, clipLoop, clipLoopVerts, &uv, &uvNext, &uvCross, insideBuffer);

		if (newLoopTop <= 2) {
			*loopTop = newLoopTop;
			return;
		}
		memcpy(loopBuffer, newLoopBuffer, sizeof(Vec3) * newLoopTop);
		*loopTop = newLoopTop;
	}
}

void processCellFaces(Cell *cell, UvgpFileLoaded *fileLoaded, BlenderMeshData *mesh, BlenderMeshData *workMesh, int32_t faceStart, int32_t faceEnd){
	for (int32_t i = 0; i < cell->faceAmount; ++i) {
		int32_t uvgpFaceIndex = cell->faces[i] - 1;
		int32_t loopAmount = faceEnd - faceStart;
		Vec3 loopBuffer[12];
		int32_t uvgpFaceStart = fileLoaded->data.faceBuffer[uvgpFaceIndex];
		int32_t uvgpFaceEnd = fileLoaded->data.faceBuffer[uvgpFaceIndex + 1];
		int32_t uvgpFaceAmount = uvgpFaceEnd - uvgpFaceStart;
		int32_t loopTop = uvgpFaceAmount;
		for (int32_t j = 0; j < uvgpFaceAmount; ++j) {
			int32_t vertIndex = fileLoaded->data.loopBuffer[uvgpFaceStart + j];
			loopBuffer[j] = fileLoaded->data.vertBuffer[vertIndex];
		}
		clipAgainstFace(&loopTop, loopBuffer, mesh, loopAmount, faceStart);
		addCellFaceToWorkMesh(loopTop, loopBuffer, workMesh, mesh, faceStart, faceEnd);
	}
}

DECL_SPEC_EXPORT void uvgpProjectOntoMesh(BlenderMeshData *mesh, BlenderMeshData *workMesh) {
	Cell **enclosingCells = malloc(sizeof(Cell *) * mesh->faceAmount);
	int32_t bufferSize = mesh->faceAmount;
	for (int32_t i = 0; i < mesh->faceAmount; ++i) {
		int32_t faceStart = mesh->faceBuffer[i];
		int32_t faceEnd = mesh->faceBuffer[i + 1];
		enclosingCells[i] = findFullyEnclosingCell(fileLoaded->quadTree.rootCell, faceStart,
		                                           faceEnd, mesh->loopBuffer, mesh->uvBuffer);
		bufferSize += enclosingCells[i]->faceAmount;
	}
	workMesh->faceBuffer = malloc(sizeof(int32_t) * (bufferSize + 1));
	workMesh->loopBuffer = malloc(sizeof(int32_t) * bufferSize * 2);
	workMesh->vertBuffer = malloc(sizeof(Vec3) * bufferSize);
	workMesh->uvBuffer = malloc(sizeof(Vec2) * bufferSize * 2);
	int32_t workMeshFaceTop = 0;
	int32_t workMeshLoopTop = 0;
	int32_t workMeshVertTop = 0;
	int32_t workMeshUvTop = 0;
	for (int32_t i = 0; i < mesh->faceAmount; ++i) {
		int32_t faceStart = mesh->faceBuffer[i];
		int32_t faceEnd = mesh->faceBuffer[i + 1];
		Cell *cell = enclosingCells[i];
		processCellFaces(cell, fileLoaded, mesh, workMesh, faceStart, faceEnd);
	}
	workMesh->faceBuffer[workMesh->faceAmount] = workMesh->loopAmount;
}

DECL_SPEC_EXPORT void uvgpUpdateMesh(BlenderMeshData *uvgpMesh, BlenderMeshData *workMesh) {
	memcpy(uvgpMesh->vertBuffer, workMesh->vertBuffer, sizeof(Vec3) * uvgpMesh->vertAmount);
	memcpy(uvgpMesh->loopBuffer, workMesh->loopBuffer, sizeof(int32_t) * uvgpMesh->loopAmount);
	memcpy(uvgpMesh->faceBuffer, workMesh->faceBuffer, sizeof(int32_t) * uvgpMesh->faceAmount);
	free(workMesh->vertBuffer);
	free(workMesh->loopBuffer);
	free(workMesh->faceBuffer);
}

DECL_SPEC_EXPORT void uvgpUpdateMeshUv(BlenderMeshData *uvgpMesh, BlenderMeshData *workMesh) {
	memcpy(uvgpMesh->uvBuffer, workMesh->uvBuffer, sizeof(Vec2) * uvgpMesh->loopAmount);
	free(workMesh->uvBuffer);
}
