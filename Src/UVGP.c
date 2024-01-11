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

DECL_SPEC_EXPORT void UvgpExportUvgpFile(int32_t vertAmount, float *vertBuffer,
                                         int32_t loopAmount, int32_t *loopBuffer,
                                         int32_t faceAmount, int32_t *faceBuffer) {
	printf("%d vertices, and %d faces\n", vertAmount, faceAmount);
	writeUvgpFile(vertAmount, vertBuffer, loopAmount, loopBuffer, faceAmount, faceBuffer);
}

DECL_SPEC_EXPORT void uvgpLoadUvgpFile(char *filePath) {
	fileLoaded = calloc(1, sizeof(UvgpFileLoaded));
	loadUvgpFile(fileLoaded, filePath);
	createQuadTree(&fileLoaded->quadTree.rootCell, &fileLoaded->quadTree.maxTreeDepth,
	               fileLoaded->header.faceAmount, fileLoaded->data.vertBuffer, fileLoaded->data.faceBuffer);
	//writeDebugImage(fileLoaded->quadTree.rootCell);
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

void clipPass(int32_t loopTop, Vec3 *loopBuffer, int32_t *newLoopTop, Vec3 *newLoopBuffer, int32_t *clipLoop, Vec3 *clipLoopVerts, Vec2 *uv, Vec2 *uvNext){
	for (int32_t i = 0; i < loopTop; ++i) {
		Vec2 uvgpVert = *(Vec2 *)&loopBuffer[i];
		Vec2 uvgpVertNext = *(Vec2 *)&loopBuffer[(i + 1) % loopTop];
		float t = (uvgpVert.x - uv->x) * (uv->y - uvNext->y) -
				  (uvgpVert.y - uv->y) * (uv->x - uvNext->x);
		t /= (uvgpVert.x - uvgpVertNext.x) * (uv->y - uvNext->y) -
			 (uvgpVert.y - uvgpVertNext.y) * (uv->x - uvNext->x);
		Vec2 intersection = _(uvgpVert V2SUB _(_(uvgpVertNext V2SUB uvgpVert) V2MULS t));
		Vec2 ab = _(uvgpVertNext V2SUB uvgpVert);
		Vec2 ba = _(uvgpVert V2SUB uvgpVertNext);
		Vec2 ai = _(intersection V2SUB uvgpVert);
		Vec2 bi = _(intersection V2SUB uvgpVertNext);
		float dotai = _(ai V2DOT uvgpVert);
		float dotbi = _(bi V2DOT uvgpVertNext);
		if ((dotai < 0) || (dotbi < 0)) {
			if ((clipLoop[0] < 0) && (clipLoop[1] < 0)) {
				//if both cliploop [0] and [1] are empty, then add current loop to Workmesh.
				newLoopBuffer[*newLoopTop] = loopBuffer[i];
			}
			else if (clipLoop[1] >= 0) {
				//elif neither are empty, then add a loop to workmesh from [0] to [1],
				//as well as one from [1] to the current loop's vert. Then add the current loop
				//to workMesh.
				newLoopBuffer[*newLoopTop] = clipLoopVerts[0];
				newLoopBuffer[*newLoopTop + 1] = clipLoopVerts[1];
				newLoopBuffer[*newLoopTop + 2] = *(Vec3 *)&uvgpVert;
				*newLoopTop += 3;
			}
			//elif cliploop [0] isnt empty, but [1] is, then don't add current loop to workMesh. 
			continue;
		}
		//if cliploop[0] == -1, then add intersection point to [0].
		if (clipLoop[1] < 0) {
			clipLoop[0] = 1;
			clipLoopVerts[0].x = intersection.x;
			clipLoopVerts[0].y = intersection.y;
			clipLoopVerts[0].z = ((Vec3 *)&uvgpVert)->z;
			continue;
		}
		//else, add intersection point to [1] (replacing contents if present),
		//and don't add current loop to workmesh.
		clipLoop[1] = 1;
		clipLoopVerts[1].x = intersection.x;
		clipLoopVerts[1].y = intersection.y;
		clipLoopVerts[1].z = ((Vec3 *)&uvgpVert)->z;
	}
}

void checkIfVertIsDup(int32_t *vertIndex, Vec3 *loopVert, BlenderMeshData *workMesh) {
	for (int32_t i = 0; i < workMesh->vertAmount; ++i) {
		int32_t dup = (loopVert->x > workMesh->vertBuffer[i].x - .01f) &&
					  (loopVert->x < workMesh->vertBuffer[i].x + .01f);
		if (dup) {
			*vertIndex = i;
		}
	}
}

void addCellFaceToWorkMesh(int32_t loopTop, Vec3 *loopBuffer, BlenderMeshData *workMesh){
	if (loopTop <= 2) {
		return;
	}
	for (int32_t i = 0; i < loopTop; ++i) {
		int32_t vertIndex = -1;
		checkIfVertIsDup(&vertIndex, loopBuffer + i, workMesh);
		if (vertIndex < 0) {
			workMesh->vertBuffer[workMesh->vertAmount] = loopBuffer[i];
			vertIndex = workMesh->vertAmount;
			workMesh->vertAmount++;
		}
		workMesh->loopBuffer[workMesh->loopAmount] = vertIndex;
		workMesh->loopAmount++;
	}
}

void clipAgainstFace(int32_t *loopTop, Vec3 *loopBuffer, BlenderMeshData *mesh, int32_t loopAmount, int32_t faceStart) {
	for (int32_t i = 0; i < loopAmount; ++i) {
		Vec2 uv = mesh->uvBuffer[i + faceStart];
		int32_t uvNextIndex = ((i + 1) % loopAmount) + faceStart;
		Vec2 uvNext = mesh->uvBuffer[uvNextIndex];
		int32_t clipLoop[2] = {-1, -1};
		Vec3 clipLoopVerts[2];
		int32_t loopsAdded;
		Vec3 newLoopBuffer[12];
		int32_t newLoopTop = 0;

		clipPass(*loopTop, loopBuffer, &newLoopTop, newLoopBuffer, clipLoop, clipLoopVerts, &uv, &uvNext);

		if (newLoopTop <= 2) {
			break;
		}
		memcpy(loopBuffer, newLoopBuffer, sizeof(Vec3) * newLoopTop);
		*loopTop = newLoopTop;
	}
}

void processCellFaces(Cell *cell, UvgpFileLoaded *fileLoaded, BlenderMeshData *mesh, BlenderMeshData *workMesh, int32_t faceStart, int32_t faceEnd){
	for (int32_t i = 0; i < cell->faceAmount; ++i) {
		int32_t uvgpFaceIndex = cell->faces[i] - 1;
		Face *uvgpFace = fileLoaded->data.faceBuffer + uvgpFaceIndex;
		int32_t loopAmount = faceEnd - faceStart;
		Vec3 loopBuffer[12];
		int32_t loopTop = uvgpFace->loopAmount;
		for (int32_t j = 0; j < uvgpFace->loopAmount; ++j) {
			int32_t vertIndex = uvgpFace->loops[j].vert;
			loopBuffer[j] = fileLoaded->data.vertBuffer[vertIndex].pos;
		}
		clipAgainstFace(&loopTop, loopBuffer, mesh, loopAmount, faceStart);
		addCellFaceToWorkMesh(loopTop, loopBuffer, workMesh);
	}
}

DECL_SPEC_EXPORT void uvgpProjectOntoMesh(BlenderMeshData *mesh, BlenderMeshData *workMesh) {
	workMesh->vertAmount = mesh->vertAmount;
	workMesh->vertBuffer = malloc(sizeof(Vec3) * mesh->vertAmount);
	workMesh->loopAmount = mesh->loopAmount;
	workMesh->loopBuffer = malloc(sizeof(int32_t) * mesh->loopAmount);
	workMesh->faceAmount = mesh->faceAmount;
	workMesh->faceBuffer = malloc(sizeof(int32_t) * mesh->faceAmount);
	workMesh->uvBuffer = malloc(sizeof(Vec2) * mesh->loopAmount);
	Cell **enclosingCells = malloc(sizeof(Cell *) * mesh->faceAmount);
	int32_t bufferSize = mesh->faceAmount;
	for (int32_t i = 0; i < mesh->faceAmount; ++i) {
		int32_t faceStart = mesh->faceBuffer[i];
		int32_t faceEnd = mesh->faceBuffer[i + 1];
		enclosingCells[i] = findFullyEnclosingCell(fileLoaded->quadTree.rootCell, faceStart,
		                                           faceEnd, mesh->loopBuffer, mesh->uvBuffer);
		bufferSize += enclosingCells[i]->faceAmount;
	}
	workMesh->faceBuffer = malloc(sizeof(int32_t) * (bufferSize + 2));
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

	memcpy(workMesh->vertBuffer, mesh->vertBuffer, sizeof(Vec3) * workMesh->vertAmount);
	memcpy(workMesh->loopBuffer, mesh->loopBuffer, sizeof(int32_t) * workMesh->loopAmount);
	memcpy(workMesh->faceBuffer, mesh->faceBuffer, sizeof(int32_t) * workMesh->faceAmount);
	memcpy(workMesh->uvBuffer, mesh->uvBuffer, sizeof(Vec2) * workMesh->loopAmount);
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
