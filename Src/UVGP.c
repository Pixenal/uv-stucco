#define DECL_SPEC_EXPORT
#ifdef PLATFORM_WIN
	#define DECL_SPEC_EXPORT __declspec(dllexport)
#endif

#define CLOCK_START gettimeofday(&start, NULL)
#define CLOCK_STOP(a) gettimeofday(&stop, NULL); printf("%s - %s: %lu\n", __func__, (a), getTimeDiff(&start, &stop))
#define CLOCK_STOP_NO_PRINT gettimeofday(&stop, NULL)

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
#include <float.h>
#include "ThreadPool.h"

// TODO
// - Reduce the bits written to the UVGP file for vert and loop indices, based on the total amount, in order to save space.
//   No point storing them as 32 bit if there's only like 4,000 verts
// - Split compressed data into chunks maybe?
// - Split whole quadtree into chunks?

extern int32_t threadAmount;

int32_t cellIndex;
int32_t leafAmount;

float tempTime = 0;

static UvgpFileLoaded *fileLoaded;

uint64_t getTimeDiff(struct timeval *start, struct timeval *stop) {
	return (stop->tv_sec - start->tv_sec) * 1000000 + (stop->tv_usec - start->tv_usec);
}

DECL_SPEC_EXPORT void UvgpExportUvgpFile(int32_t vertAmount, Vec3 *vertBuffer,
                                         int32_t loopAmount, int32_t *loopBuffer, Vec3 *normalBuffer,
                                         int32_t faceAmount, int32_t *faceBuffer) {
	printf("%d vertices, and %d faces\n", vertAmount, faceAmount);
	writeUvgpFile(vertAmount, vertBuffer, loopAmount, loopBuffer, normalBuffer, faceAmount, faceBuffer);
}

DECL_SPEC_EXPORT void uvgpLoadUvgpFile(char *filePath) {
	createThreadPool();
	fileLoaded = calloc(1, sizeof(UvgpFileLoaded));
	loadUvgpFile(fileLoaded, filePath);
	createQuadTree(&fileLoaded->quadTree,
	               fileLoaded->header.faceAmount, fileLoaded->data.vertBuffer,
				   fileLoaded->data.loopBuffer, fileLoaded->data.faceBuffer);
	//writeDebugImage(fileLoaded->quadTree.rootCell);
}

DECL_SPEC_EXPORT void uvgpUnloadUvgpFile(void *uvgpFile) {
	fileLoaded = uvgpFile;
	destroyQuadTree(fileLoaded->quadTree.rootCell, fileLoaded->quadTree.maxTreeDepth);
	destroyUvgpFile(fileLoaded);
}

uint32_t fnvHash(uint8_t *value, int32_t valueSize, uint32_t size) {
	uint32_t hash = 2166136261;
	for (int32_t i = 0; i < valueSize; ++i) {
		hash ^= value[i];
		hash *= 16777619;
	}
	hash %= size;
	return hash;
}

int32_t isPointInPolygon(Vec2 point, int32_t faceIndex, Vec3 *verts, int32_t *loops, int32_t *faces) {
	int32_t loopTop = faces[faceIndex + 1];
	for (int32_t i = faces[faceIndex]; i < loopTop; ++i) {
	}
	return 0;
}

void clipPass(Vec2 tileMin, int32_t loopTop, Vec3 *loopBuffer, int32_t *indexBuffer, int32_t *newLoopTop,
              Vec3 *newLoopBuffer, int32_t *newIndexBuffer, int32_t *clipLoop,
              Vec3 *clipLoopVerts, Vec2 *uv, Vec2 *uvNext, Vec2 *uvCross, int32_t *insideBuffer,
			  int32_t *baseLoopBuffer, int32_t *newBaseLoopBuffer, int32_t loopIndex, int32_t *edgeFace,
			  int32_t *sortBuffer, int32_t *newSortBuffer) {
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
			newIndexBuffer[*newLoopTop] = indexBuffer[i];
			newSortBuffer[*newLoopTop] = sortBuffer[i];
			newBaseLoopBuffer[*newLoopTop] = baseLoopBuffer[i];
			(*newLoopTop)++;
		}
		if (insideBuffer[i] != insideBuffer[vertNextIndex]) {
			*edgeFace += 1;
			Vec2 uvgpVert = *(Vec2 *)&loopBuffer[i];
			Vec2 uvgpVertNext = *(Vec2 *)&loopBuffer[vertNextIndex];
			float t = (uvgpVert.x - uv->x) * (uv->y - uvNext->y) -
					  (uvgpVert.y - uv->y) * (uv->x - uvNext->x);
			t /= (uvgpVert.x - uvgpVertNext.x) * (uv->y - uvNext->y) -
				 (uvgpVert.y - uvgpVertNext.y) * (uv->x - uvNext->x);
			Vec2 intersection = _(uvgpVert V2ADD _(_(uvgpVertNext V2SUB uvgpVert) V2MULS t));
			*(Vec2 *)&newLoopBuffer[*newLoopTop] = intersection;
			newLoopBuffer[*newLoopTop].z = .0f;
			newIndexBuffer[*newLoopTop] = -1;
			newSortBuffer[*newLoopTop] = -1;
			newBaseLoopBuffer[*newLoopTop] = loopIndex;
			(*newLoopTop)++;
		}
	}
}

void addCellFaceToWorkMesh(Vec2 tileMin,int32_t loopTop, Vec3 *loopBuffer, int32_t *indexBuffer, Vec2 *uvBuffer,
                           BlenderMeshData *workMesh, BlenderMeshData *mesh, int32_t faceStart,
                           int32_t faceEnd, VertAdj *uvgpVertAdj, uint32_t vertAdjSize,
						   int32_t *baseLoopBuffer, BoundaryVert *boundaryBuffer,
	                       int32_t boundaryBufferSize, int32_t *maxDepth, int32_t edgeFace,
						   int32_t faceIndex, int32_t *sortBuffer) {
	if (loopTop <= 2) {
		return;
	}
	if (faceIndex == 205066) {
		int32_t a = 0;
	}
	int32_t loopStart = workMesh->loopAmount;
	int32_t boundaryLoopStart = workMesh->boundaryLoopAmount;
	int32_t vertsAdded = 0;
	int32_t firstUvgpVert = -1;
	for (int32_t i = 0; i < loopTop; ++i) {
		int32_t vertIndex = indexBuffer[i];
		if (vertIndex < 0) {
			/*
			int32_t loopIndex = baseLoopBuffer[i];
			int32_t loopAmount = faceEnd - faceStart;
			int32_t baseVert = mesh->loopBuffer[faceStart + loopIndex];
			int32_t loopNextIndex = (loopIndex + 1) % loopAmount;
			int32_t baseVertNext = mesh->loopBuffer[faceStart + loopNextIndex];
			int32_t edgeId = baseVert + baseVertNext;
			int32_t hash = fnvHash((uint8_t *)&edgeId, 4, boundaryBufferSize);
			BoundaryVert *entry = boundaryBuffer + hash;
			int32_t depth = 0;
			do {
				if (!entry->loopAmount) {
					workMesh->vertBuffer[workMesh->vertAmount] = loopBuffer[i];
					entry->vert = workMesh->vertAmount;
					workMesh->vertAmount++;
					break;
				}
				Vec3 *entryVert = workMesh->vertBuffer + entry->vert;
				int32_t match = loopBuffer[i].x == entryVert->x &&
								loopBuffer[i].y == entryVert->y;
				if (match) {
					break;
				}
				if (!entry->next) {
					depth++;
					entry = entry->next = calloc(1, sizeof(BoundaryVert));
					workMesh->vertBuffer[workMesh->vertAmount] = loopBuffer[i];
					entry->vert = workMesh->vertAmount;
					workMesh->vertAmount++;
					break;
				}
				depth++;
				entry = entry->next;
			} while (1);
			entry->loopAmount++;
			vertIndex = entry->vert;
			if (depth > *maxDepth) {
				*maxDepth = depth;
			}
			*/

			vertIndex = workMesh->boundaryVertAmount;
			workMesh->vertBuffer[vertIndex] = loopBuffer[i];
			workMesh->boundaryVertAmount--;
			workMesh->uvBuffer[workMesh->boundaryLoopAmount] = uvBuffer[i];
		}
		else {
			if (firstUvgpVert < 0) {
				firstUvgpVert = sortBuffer[i];
			}
			uint32_t uVertIndex = vertIndex;
			int32_t hash = fnvHash((uint8_t *)&uVertIndex, 4, vertAdjSize);
			VertAdj *vertAdj = uvgpVertAdj + hash;
			do {
				if (!vertAdj->loopAmount) {
					vertAdj->uvgpVert = vertIndex;
					vertIndex = workMesh->vertAmount;
					vertAdj->vert = vertIndex;
					workMesh->vertBuffer[vertIndex] = loopBuffer[i];
					workMesh->vertAmount++;
					break;
				}
				int32_t match = vertAdj->uvgpVert == vertIndex;
				if (match) {
					vertIndex = vertAdj->vert;
					break;
				}
				if (!vertAdj->next) {
					vertAdj = vertAdj->next = calloc(1, sizeof(VertAdj));
					vertAdj->uvgpVert = vertIndex;
					vertIndex = workMesh->vertAmount;
					vertAdj->vert = vertIndex;
					workMesh->vertBuffer[vertIndex] = loopBuffer[i];
					workMesh->vertAmount++;
					break;
				}
				vertAdj = vertAdj->next;
			} while (1);
			vertAdj->loopAmount++;
			if (edgeFace) {
				workMesh->uvBuffer[workMesh->boundaryLoopAmount] = uvBuffer[i];
			}
			else {
				workMesh->uvBuffer[workMesh->loopAmount] = uvBuffer[i];
			}
		}
		if (edgeFace) {
			workMesh->loopBuffer[workMesh->boundaryLoopAmount] = vertIndex;
			workMesh->boundaryLoopAmount--;
		}
		else {
			workMesh->loopBuffer[workMesh->loopAmount] = vertIndex;
			workMesh->loopAmount++;
		}
	}
	if (edgeFace) {
		workMesh->faceBuffer[workMesh->boundaryFaceAmount] = boundaryLoopStart;
		int32_t hash = fnvHash((uint8_t *)&faceIndex, 4, boundaryBufferSize);
		BoundaryVert *entry = boundaryBuffer + hash;
		do {
			if (!entry->valid) {
				entry->face = workMesh->boundaryFaceAmount;
				entry->firstVert = firstUvgpVert;
				entry->faceIndex = faceIndex;
				entry->valid = 1;
				break;
			}
			if (!entry->next) {
				entry = entry->next = calloc(1, sizeof(BoundaryVert));
				entry->face = workMesh->boundaryFaceAmount;
				entry->firstVert = firstUvgpVert;
				entry->faceIndex = faceIndex;
				entry->valid = 1;
				break;
			}
			entry = entry->next;
		} while (1);
		workMesh->boundaryFaceAmount--;
	}
	else {
		workMesh->faceBuffer[workMesh->faceAmount] = loopStart;
		workMesh->faceAmount++;
	}
}

void clipAgainstFace(Vec2 tileMin, int32_t *loopTop, Vec3 *loopBuffer, int32_t *indexBuffer,
                     BlenderMeshData *mesh, int32_t loopAmount, int32_t faceStart,
					 int32_t *baseLoopBuffer, int32_t *edgeFace, int32_t *sortBuffer) {
	for (int32_t i = 0; i < loopAmount; ++i) {
		Vec2 uv = mesh->uvBuffer[i + faceStart];
		int32_t uvNextIndex = ((i + 1) % loopAmount) + faceStart;
		Vec2 uvNext = mesh->uvBuffer[uvNextIndex];
		Vec2 uvDir = _(uvNext V2SUB uv);
		int32_t clipLoop[2] = {-1, -1};
		Vec3 clipLoopVerts[2];
		int32_t loopsAdded;
		Vec3 newLoopBuffer[12];
		int32_t newIndexBuffer[12];
		int32_t newSortBuffer[12];
		int32_t newBaseLoopBuffer[12] = {0};
		int32_t insideBuffer[12] = {0};
		int32_t newLoopTop = 0;
		Vec2 uvCross = vec2Cross(uvDir);

		clipPass(tileMin, *loopTop, loopBuffer, indexBuffer, &newLoopTop, newLoopBuffer, newIndexBuffer,
		         clipLoop, clipLoopVerts, &uv, &uvNext, &uvCross, insideBuffer, baseLoopBuffer,
				 newBaseLoopBuffer, i, edgeFace, sortBuffer, newSortBuffer);

		if (newLoopTop <= 2) {
			*loopTop = newLoopTop;
			return;
		}
		memcpy(loopBuffer, newLoopBuffer, sizeof(Vec3) * newLoopTop);
		memcpy(indexBuffer, newIndexBuffer, sizeof(int32_t) * newLoopTop);
		memcpy(sortBuffer, newSortBuffer, sizeof(int32_t) * newLoopTop);
		memcpy(baseLoopBuffer, newBaseLoopBuffer, sizeof(int32_t) * newLoopTop);
		*loopTop = newLoopTop;
	}
}

void CellsDebugCustom() {
	int a = 0;
	a += 1;
	return;
}

void processCellFaces(Vec2 tileMin, int32_t cellFacesTotal, int32_t *cellFaces,
                      BlenderMeshData *mesh, BlenderMeshData *workMesh, int32_t faceStart,
                      int32_t faceEnd, VertAdj *uvgpVertAdj, uint64_t *timeSpent, uint32_t vertAdjSize,
                      int32_t *averageDepth, BoundaryVert *boundaryBuffer, int32_t *boundaryFaceStart,
					  int32_t faceIndex, int32_t boundaryBufferSize, int32_t *maxDepth) {
	struct timeval start, stop;
	CLOCK_START;
	//*boundaryFaceStart = workMesh->boundaryLoopAmount;
	//int32_t boundaryVertStart = workMesh->boundaryVertAmount;
	int32_t vertAmountBase = workMesh->vertAmount;
	Vec3 triUv0;
	Vec3 triUv1;
	Vec3 triUv2;
	*(Vec2 *)&triUv0 = _(mesh->uvBuffer[faceStart] V2SUB tileMin);
	triUv0.z = .0f;
	*(Vec2 *)&triUv1 = _(mesh->uvBuffer[faceStart + 1] V2SUB tileMin);
	triUv1.z = .0f;
	*(Vec2 *)&triUv2 = _(mesh->uvBuffer[faceStart + 2] V2SUB tileMin);
	triUv2.z = .0f;
	Vec3 *triVert0 = mesh->vertBuffer + mesh->loopBuffer[faceStart];
	Vec3 *triVert1 = mesh->vertBuffer + mesh->loopBuffer[faceStart + 1];
	Vec3 *triVert2 = mesh->vertBuffer + mesh->loopBuffer[faceStart + 2];
	for (int32_t i = 0; i < cellFacesTotal; ++i) {
		int32_t uvgpFaceIndex = cellFaces[i];
		int32_t loopAmount = faceEnd - faceStart;
		Vec3 loopBuffer[12];
		int32_t indexBuffer[12];
		int32_t sortBuffer[12];
		int32_t baseLoopBuffer[12] = {0};
		int32_t uvgpFaceStart = fileLoaded->data.faceBuffer[uvgpFaceIndex];
		int32_t uvgpFaceEnd = fileLoaded->data.faceBuffer[uvgpFaceIndex + 1];
		int32_t uvgpFaceAmount = uvgpFaceEnd - uvgpFaceStart;
		int32_t loopTop = uvgpFaceAmount;
		for (int32_t j = 0; j < uvgpFaceAmount; ++j) {
			int32_t vertIndex = fileLoaded->data.loopBuffer[uvgpFaceStart + j];
			indexBuffer[j] = vertIndex;
			loopBuffer[j] = fileLoaded->data.vertBuffer[vertIndex];
			loopBuffer[j].x += tileMin.x;
			loopBuffer[j].y += tileMin.y;
			sortBuffer[j] = j;
		}
		int32_t edgeFace = 0;
		clipAgainstFace(tileMin, &loopTop, loopBuffer, indexBuffer, mesh, loopAmount,
		                faceStart, baseLoopBuffer, &edgeFace, sortBuffer);
		Vec2 uvBuffer[12];
		for (int32_t j = 0; j < loopTop; ++j) {
			Vec3 vert = loopBuffer[j];
			uvBuffer[j] = *(Vec2 *)&vert;
			vert.z = .0f;
			_((Vec2 *)&vert V2SUBEQL tileMin);
			Vec3 vertBc = cartesianToBarycentric(&triUv0, &triUv1, &triUv2, &vert);
			loopBuffer[j] =
				barycentricToCartesian(triVert0, triVert1, triVert2, &vertBc);
		}
		addCellFaceToWorkMesh(tileMin, loopTop, loopBuffer, indexBuffer, uvBuffer, workMesh,
		                      mesh, faceStart, faceEnd, uvgpVertAdj, vertAdjSize,
							  baseLoopBuffer, boundaryBuffer, boundaryBufferSize, maxDepth, edgeFace,
							  uvgpFaceIndex, sortBuffer);
	}
	CLOCK_STOP_NO_PRINT;
	timeSpent[0] += getTimeDiff(&start, &stop);
	CLOCK_START;
	/*
	*/
	//printf("Total vert adj: %d %d %d - depth: %d %d\n", totalEmpty, totalComputed, vertAdjSize, maxDepth, *averageDepth);
	CLOCK_STOP_NO_PRINT;
	timeSpent[1] += getTimeDiff(&start, &stop);
	//CLOCK_START;
	//memset(uvgpVertAdj, 0, sizeof(VertAdj) * fileLoaded->header.vertAmount);
	//CLOCK_STOP_NO_PRINT;
	//timeSpent[2] += getTimeDiff(&start, &stop);
}

void getFaceBounds(Vec2 *boundsMin, Vec2 *boundsMax, Vec2 *uvBuffer, int32_t faceStart, int32_t faceEnd) {
	boundsMin->x = boundsMin->y = FLT_MAX;
	boundsMax->x = boundsMax->y = .0f;
	int32_t faceLoopAmount = faceEnd - faceStart;
	for (int32_t i = 0; i < faceLoopAmount; ++i) {
		Vec2 *uv = uvBuffer + faceStart + i;
		boundsMin->x = uv->x < boundsMin->x ? uv->x : boundsMin->x;
		boundsMin->y = uv->y < boundsMin->y ? uv->y : boundsMin->y;
		boundsMax->x = uv->x > boundsMax->x ? uv->x : boundsMax->x;
		boundsMax->y = uv->y > boundsMax->y ? uv->y : boundsMax->y;
	}
}

int32_t clipCellsToFace (int32_t *enclosingCellAmount, Cell **enclosingCells, int8_t *enclosingCellType,
						 int32_t *cellFacesTotal, int32_t *totalCellFacesNoDup, iVec2 tileMin,
						 int32_t faceStart, int32_t faceEnd, BlenderMeshData *mesh, int8_t *cellInits) {
	int32_t loopAmount = faceEnd - faceStart;
	int32_t isInsideBuffer[4] = {1, 1, 1, 1};
	int32_t faceVertInside = 0;
	for (int32_t i = 0; i < loopAmount; ++i) {
		Vec2 *loop = mesh->uvBuffer + faceStart + i;
		int32_t nextLoopIndex = faceStart + (i + 1) % loopAmount;
		Vec2 *loopNext = mesh->uvBuffer + nextLoopIndex;
		Vec2 loopDir = _(*loopNext V2SUB *loop);
		Vec2 loopCross = vec2Cross(loopDir);
		for (int32_t j = 0; j < 4; ++j) {
			Vec2 cellPoint = {tileMin.x + j % 2, tileMin.y + j / 2};
			Vec2 cellDir = _(cellPoint V2SUB *loop);
			float dot = _(loopCross V2DOT cellDir);
			isInsideBuffer[j] *= dot < .0f;
		}
		//in addition, test for face verts inside tile
		//edge cases may not be cause by the above, like if a face entered the tile,
		//and then exited the same side, with a single vert in the tile.
		//Checking for verts will catch this:
		Vec2 tileMinf = {tileMin.x, tileMin.y};
		Vec2 tileMaxf = {tileMinf.x + 1.0f, tileMinf.y + 1.0f};
		faceVertInside += _(*loop V2GREAT tileMinf) && _(*loop V2LESSEQL tileMaxf);
	}
	int32_t isInside = isInsideBuffer[0] || isInsideBuffer[1] ||
	                   isInsideBuffer[2] || isInsideBuffer[3];
	if (isInsideBuffer[0] && isInsideBuffer[1] &&
	    isInsideBuffer[2] && isInsideBuffer[3]) {
		//fully enclosed
		return 1;
	}
	if (!faceVertInside && !isInside) {
		//face is not inside current tile
		return 0;
	}
	//find fully enclosing cell using clipped face
	findFullyEnclosingCell(tileMin, enclosingCellAmount, enclosingCells, enclosingCellType, cellFacesTotal,
	                       totalCellFacesNoDup, fileLoaded->quadTree.rootCell, faceStart,
	                       faceEnd, mesh->loopBuffer, mesh->uvBuffer, cellInits);
	return 0;
}

int32_t rasterizeFaceInCells(int32_t *cellFacesMax, int32_t faceIndex, FaceCellsInfo *faceCellsInfo,
                             int32_t *totalCellFaces, iVec2 faceBoundsMin, iVec2 faceBoundsMax,
                             int32_t faceStart, int32_t faceEnd, BlenderMeshData *mesh,
                             int8_t *cellInits) {
	Cell *enclosingCells[256];
	int8_t enclosingCellType[256];
	int32_t totalCellFacesNoDup = 0;
	int32_t enclosingCellAmount = 0;
	for (int32_t i = faceBoundsMin.y; i <= faceBoundsMax.y; ++i) {
		for (int32_t j = faceBoundsMin.x; j <= faceBoundsMax.x; ++j) {
			int32_t result;
			iVec2 tileMin = {j, i};
			int32_t cellTop = enclosingCellAmount;
			int32_t totalFaces = *totalCellFaces;
			//continue until the smallest cell that fully encloses the face is found (result == 0).
			//if face fully encloses the while uv tile (result == 1), then return (root cell will be used).
			//if the face is not within the current tile, then skip tile (result == 2).
			result = clipCellsToFace(&enclosingCellAmount, enclosingCells, enclosingCellType,
			                         totalCellFaces, &totalCellFacesNoDup, tileMin, faceStart,
			                         faceEnd, mesh, cellInits);
			if (result == 1) {
				//fully enclosed
				return 1;
			}
		}
	}
	faceCellsInfo->cells = malloc(sizeof(Cell *) * enclosingCellAmount);
	faceCellsInfo->cellType = malloc(sizeof(int8_t) * enclosingCellAmount);
	memcpy(faceCellsInfo->cells, enclosingCells, sizeof(Cell *) * enclosingCellAmount);
	memcpy(faceCellsInfo->cellType, enclosingCellType, sizeof(int8_t) * enclosingCellAmount);
	faceCellsInfo->cellAmount = enclosingCellAmount;
	faceCellsInfo->faceAmount = totalCellFacesNoDup;
	if (totalCellFacesNoDup > *cellFacesMax) {
		*cellFacesMax = totalCellFacesNoDup;
	}
	return 0;
}

int32_t uvgpFloor(float a) {
	int32_t aTrunc = a;
	aTrunc -= ((float)aTrunc != a) && (a < .0f);
	return aTrunc;
}

void getAllEnclosingCellsForFaces(int32_t *cellFacesMax, iVec2 *faceBoundsMin, iVec2 *faceBoundsMax,
                                  int32_t *totalCellFaces,
								  FaceCellsInfo *faceCellsInfo, BlenderMeshData *mesh,
                                  int32_t *averageUvgpFacesPerFace) {
	int8_t *cellInits = malloc(cellIndex);
	for (int32_t i = 0; i < mesh->faceAmount; ++i) {
		int32_t faceStart = mesh->faceBuffer[i];
		int32_t faceEnd = mesh->faceBuffer[i + 1];
		Vec2 faceBoundsMinf, faceBoundsMaxf;
		getFaceBounds(&faceBoundsMinf, &faceBoundsMaxf, mesh->uvBuffer, faceStart, faceEnd);
		faceBoundsMin->x = uvgpFloor(faceBoundsMinf.x);
		faceBoundsMin->y = uvgpFloor(faceBoundsMinf.y);
		faceBoundsMax->x = uvgpFloor(faceBoundsMaxf.x);
		faceBoundsMax->y = uvgpFloor(faceBoundsMaxf.y);
		if (rasterizeFaceInCells(cellFacesMax, i, faceCellsInfo + i, totalCellFaces, *faceBoundsMin,
		                         *faceBoundsMax, faceStart, faceEnd, mesh, cellInits)) {
			Cell *rootCell = fileLoaded->quadTree.rootCell;
			faceCellsInfo[i].cells = malloc(sizeof(Cell *));
			*faceCellsInfo[i].cells = rootCell;
			*totalCellFaces = rootCell->faceAmount;
			return;
		}
		if (faceCellsInfo[i].faceAmount < 1000) {
			CellsDebugCustom();
		}
		*averageUvgpFacesPerFace += faceCellsInfo[i].faceAmount;
		//printf("Total cell amount: %d\n", faceCellsInfo[i].cellAmount);
	}
	free(cellInits);
	*averageUvgpFacesPerFace /= mesh->faceAmount;
}

void projectOntoMeshJob(void *argsPtr) {
	struct timeval start, stop;
	CLOCK_START;
	ThreadArg *args = argsPtr;
	int32_t cellFacesTotal = 0;;
	iVec2 faceBoundsMin, faceBoundsMax;
	int32_t cellFacesMax = 0;
	FaceCellsInfo *faceCellsInfo = malloc(sizeof(FaceCellsInfo) * args->mesh.faceAmount);
	int32_t averageUvgpFacesPerFace = 0;
	getAllEnclosingCellsForFaces(&cellFacesMax, &faceBoundsMin, &faceBoundsMax, &cellFacesTotal,
	                             faceCellsInfo, &args->mesh, &averageUvgpFacesPerFace);
	CLOCK_STOP("getting enclosing cells");
	CLOCK_START;
	//args->boundaryFace = malloc(sizeof(int32_t) * args->mesh.faceAmount + 1);
	int32_t bufferSize = args->mesh.faceAmount + cellFacesTotal;
	args->bufferSize = bufferSize;
	int32_t loopBufferSize = bufferSize * 2;
	args->loopBufferSize = loopBufferSize;
	int32_t boundaryBufferSize = bufferSize;
	args->boundaryBuffer = calloc(boundaryBufferSize, sizeof(BoundaryVert));
	args->localMesh.boundaryVertAmount = bufferSize - 1;
	args->localMesh.boundaryLoopAmount = loopBufferSize - 1;
	args->localMesh.boundaryFaceAmount = bufferSize - 1;
	args->localMesh.faceBuffer = malloc(sizeof(int32_t) * bufferSize);
	args->localMesh.loopBuffer = malloc(sizeof(int32_t) * loopBufferSize);
	args->localMesh.vertBuffer = malloc(sizeof(Vec3) * bufferSize);
	args->localMesh.uvBuffer = malloc(sizeof(Vec2) * loopBufferSize);
	int32_t workMeshFaceTop = 0;
	int32_t workMeshLoopTop = 0;
	int32_t workMeshVertTop = 0;
	int32_t workMeshUvTop = 0;
	CLOCK_STOP("allocating local mesh");
	CLOCK_START;
	int32_t *cellFaces = malloc(sizeof(int32_t) * cellFacesMax);
	CLOCK_STOP("allocating cell faces");
	CLOCK_START;
	uint32_t vertAdjSize = averageUvgpFacesPerFace / 10;
	VertAdj *uvgpVertAdj = calloc(vertAdjSize, sizeof(VertAdj));
	CLOCK_STOP("allocating vert adj");
	uint64_t timeSpent[3] = {0};
	int32_t maxDepth;
	CLOCK_START;
	for (int32_t i = 0; i < args->mesh.faceAmount; ++i) {
		// copy faces over to a new contiguous array
		int32_t facesNextIndex = 0;
		for (int32_t j = 0; j < faceCellsInfo[i].cellAmount; ++j) {
			Cell *cell = faceCellsInfo[i].cells[j];
			int32_t facesAdded = 0;
			if (faceCellsInfo[i].cellType[j]) {
				memcpy(cellFaces + facesNextIndex, cell->edgeFaces, sizeof(int32_t) * cell->edgeFaceAmount);
				facesNextIndex += cell->edgeFaceAmount;
			}
			if (faceCellsInfo[i].cellType[j] != 1) {
				memcpy(cellFaces + facesNextIndex, cell->faces, sizeof(int32_t) * cell->faceAmount);
				facesNextIndex += cell->faceAmount;
			}
		}
		//iterate through tiles
		for (int32_t j = faceBoundsMin.y; j <= faceBoundsMax.y; ++j) {
			for (int32_t k = faceBoundsMin.x; k <= faceBoundsMax.x; ++k) {
				Vec2 tileMin = {k, j};
				int32_t faceStart = args->mesh.faceBuffer[i];
				int32_t faceEnd = args->mesh.faceBuffer[i + 1];
				processCellFaces(tileMin, faceCellsInfo[i].faceAmount, cellFaces,
				                 &args->mesh, &args->localMesh, faceStart,
				                 faceEnd, uvgpVertAdj, timeSpent, vertAdjSize,
				                 &args->averageVertAdjDepth, args->boundaryBuffer,
								 args->boundaryFaceStart + i, i, boundaryBufferSize,
								 &maxDepth);
			}
		}
		free(faceCellsInfo[i].cells);
	}
	args->localMesh.faceBuffer[args->localMesh.boundaryFaceAmount] = 
		args->localMesh.boundaryLoopAmount;
	args->totalFaces = args->localMesh.faceAmount;
	args->totalLoops = args->localMesh.loopAmount;
	//args->totalFaces = args->localMesh.faceAmount + (bufferSize - args->localMesh.boundaryFaceAmount);
	//args->totalLoops = args->localMesh.loopAmount + (loopBufferSize - args->localMesh.boundaryLoopAmount);
	args->totalVerts = args->localMesh.vertAmount;
	printf("MaxDepth: %d\n", maxDepth);
	CLOCK_STOP("projecting");
	printf("  ^  project: %lu, move & transform: %lu, memset vert adj: %lu\n",
			timeSpent[0], timeSpent[1], timeSpent[2]);
	CLOCK_START;
	if (1) {
		args->finalBoundary = malloc(sizeof(BoundaryVert * ) *
				(bufferSize - args->localMesh.boundaryFaceAmount));
		args->totalBoundaryFaces = 0;
		BoundaryVert *entryBuffer[256];
		int32_t entryBufferTop = 0;
		for (int32_t i = 0; i < boundaryBufferSize; ++i) {
			BoundaryVert *entry = args->boundaryBuffer + i;
			do {
				if (!entry->valid) {
					break;
				}
				entryBuffer[entryBufferTop++] = entry;
				BoundaryVert *nextEntry = entry->next;
				entry->next = NULL;
				entry = nextEntry;
			} while (entry);
			for (int32_t j = 0; j < entryBufferTop; ++j) {
				if (!entryBuffer[j]) {
					continue;
				}
				entry = entryBuffer[j];
				int32_t faceStart = args->localMesh.faceBuffer[entry->face];
				int32_t faceEnd = args->localMesh.faceBuffer[entry->face - 1];
				int32_t loopAmount = faceStart - faceEnd;
				int32_t totalLoops = loopAmount;
				int32_t uvgpLoops = 0;
				for (int32_t k = 0; k < loopAmount; ++k) {
					int32_t vert = args->localMesh.loopBuffer[faceStart - k];
					if (vert < args->localMesh.vertAmount) {
						uvgpLoops++;
					}
				}
				for (int32_t k = j + 1; k < entryBufferTop; ++k) {
					if (!entryBuffer[k]) {
						continue;
					}
					BoundaryVert *otherEntry = entryBuffer[k];
					if (entry->faceIndex != otherEntry->faceIndex) {
						continue;
					}
					int32_t otherFaceStart = args->localMesh.faceBuffer[otherEntry->face];
					int32_t otherFaceEnd = args->localMesh.faceBuffer[otherEntry->face - 1];
					int32_t otherLoopAmount = otherFaceStart - otherFaceEnd;
					for (int32_t l = 0; l < otherLoopAmount; ++l) {
						int32_t vert = args->localMesh.loopBuffer[otherFaceStart - l];
						if (vert < args->localMesh.vertAmount) {
							uvgpLoops++;
						}
					}
					totalLoops += otherLoopAmount;
					entryBuffer[k] = NULL;
					BoundaryVert *nextEntry = entry;
					do {
						if (!nextEntry->next) {
							nextEntry->next = otherEntry;
							break;
						}
						nextEntry = nextEntry->next;
					} while(1);
				}
				if (totalLoops <= 2) {
					goto skip;
				}
				int32_t seamFace = uvgpLoops <= 2;
				entry->valid = seamFace + 1;
				args->totalLoops += seamFace ? totalLoops : totalLoops;
				args->totalFaces++;
				args->finalBoundary[args->totalBoundaryFaces] = entry;
				args->totalBoundaryFaces++;

			}
skip:
			entryBufferTop = 0;
		}
	}
	free(uvgpVertAdj);
	free(cellFaces);
	free(faceCellsInfo);
	CLOCK_STOP("freeing memory");
	CLOCK_START;
	mutexLock();
	++*args->jobsCompleted;
	mutexUnlock();
	CLOCK_STOP("setting jobs completed");
}

DECL_SPEC_EXPORT void uvgpProjectOntoMesh(BlenderMeshData *mesh, BlenderMeshData *workMesh) {
	struct timeval start, stop;
	CLOCK_START;
	int32_t facesPerThread = mesh->faceAmount / threadAmount;
	int32_t threadAmountMinus1 = threadAmount - 1;
	ThreadArg jobArgs[MAX_THREADS] = {0};
	void *jobArgPtrs[MAX_THREADS];
	int32_t jobsCompleted = 0;
	for (int32_t i = 0; i < threadAmount; ++i) {
		int32_t meshStart = facesPerThread * i;
		int32_t meshEnd = i == threadAmountMinus1 ?
			mesh->faceAmount : meshStart + facesPerThread;
		BlenderMeshData meshPart = *mesh;
		meshPart.faceBuffer += meshStart;
		meshPart.faceAmount = meshEnd - meshStart;
		jobArgs[i].averageVertAdjDepth = 0;
		jobArgs[i].mesh = meshPart;
		jobArgs[i].jobsCompleted = &jobsCompleted;
		jobArgs[i].id = i;
		jobArgPtrs[i] = jobArgs + i;
	}
	pushJobs(threadAmount, projectOntoMeshJob, jobArgPtrs);
	CLOCK_STOP("send off jobs");
	CLOCK_START;
	struct timespec remaining, request = {0, 1000};
	int32_t waiting;
	do  {
		nanosleep(&request, &remaining);
		mutexLock();
		waiting = jobsCompleted < threadAmount;
		mutexUnlock();
	} while(waiting);
	CLOCK_STOP("waiting");
	CLOCK_START;
	int32_t averageVertAdjDepth = 0;
	int32_t workMeshFaces, workMeshLoops, workMeshVerts;
	workMeshFaces = workMeshLoops = workMeshVerts = 0;
	for (int32_t i = 0; i < threadAmount; ++i) {
		averageVertAdjDepth += jobArgs[i].averageVertAdjDepth;
		workMeshFaces += jobArgs[i].totalFaces;
		workMeshLoops += jobArgs[i].totalLoops;
		workMeshVerts += jobArgs[i].totalVerts;
	}
	averageVertAdjDepth /= threadAmount;
	printf("Average Vert Adj Depth: %d\n", averageVertAdjDepth);


	workMesh->faceBuffer = malloc(sizeof(int32_t) * (workMeshFaces + 1));
	workMesh->loopBuffer = malloc(sizeof(int32_t) * workMeshLoops);
	workMesh->vertBuffer = malloc(sizeof(Vec3) * workMeshVerts);
	workMesh->uvBuffer = malloc(sizeof(Vec2) * workMeshLoops);
	for (int32_t i = 0; i < threadAmount; ++i) {
		BlenderMeshData *localMesh = &jobArgs[i].localMesh;
		for (int32_t j = 0; j < localMesh->faceAmount; ++j) {
			localMesh->faceBuffer[j] += workMesh->loopAmount;
		}
		for (int32_t j = 0; j < localMesh->loopAmount; ++j) {
			localMesh->loopBuffer[j] += workMesh->vertAmount;
		}
		int32_t vertBase = workMesh->vertAmount;
		int32_t *facesStart = workMesh->faceBuffer + workMesh->faceAmount;
		int32_t *loopsStart = workMesh->loopBuffer + workMesh->loopAmount;
		Vec3 *vertsStart = workMesh->vertBuffer + workMesh->vertAmount;
		Vec2 *uvsStart = workMesh->uvBuffer + workMesh->loopAmount;
		memcpy(facesStart, localMesh->faceBuffer, sizeof(int32_t) * localMesh->faceAmount);
		workMesh->faceAmount += localMesh->faceAmount;
		memcpy(loopsStart, localMesh->loopBuffer, sizeof(int32_t) * localMesh->loopAmount);
		memcpy(uvsStart, localMesh->uvBuffer, sizeof(Vec2) * localMesh->loopAmount);
		workMesh->loopAmount += localMesh->loopAmount;
		memcpy(vertsStart, localMesh->vertBuffer, sizeof(Vec3) * localMesh->vertAmount);
		workMesh->vertAmount += localMesh->vertAmount;

		for (int32_t j = 0; j < jobArgs[i].totalBoundaryFaces; ++j) {
			int32_t loopBase = workMesh->loopAmount;
			workMesh->faceBuffer[workMesh->faceAmount] = loopBase;
			BoundaryVert *entry = jobArgs[i].finalBoundary[j];
			int32_t uvgpFaceStart = fileLoaded->data.faceBuffer[entry->faceIndex];
			int32_t uvgpFaceEnd = fileLoaded->data.faceBuffer[entry->faceIndex + 1];
			int32_t uvgpLastLoop = uvgpFaceEnd - uvgpFaceStart - 1;
			int32_t seamFace = entry->valid - 1;
			int32_t groupTable[128] = {0};
			int32_t groups[32] = {0};
			int32_t groupTop = 0;
			int32_t vertUvgpIndices[64];
			int32_t loopTop = 0;
			int32_t loopBuffer[128];
			Vec2 uvBuffer[128];
			//int32_t uvgpFaceStart = fileLoaded->data.faceBuffer[entry->faceIndex];
			//int32_t uvgpFaceEnd = fileLoaded->data.faceBuffer[entry->faceIndex + 1];
			//int32_t uvgpLoopAmount = uvgpFaceEnd - uvgpFaceStart;
			do {
				int32_t uvgpIndicesLocal[64];
				int32_t loopTopLocal = 0;
				int32_t uvgpLoopsAdded = 0;
				int32_t faceStart = localMesh->faceBuffer[entry->face];
				int32_t faceEnd = localMesh->faceBuffer[entry->face - 1];
				int32_t loopAmount = faceStart - faceEnd;
				for (int32_t k = 0; k < loopAmount; ++k) {
					int32_t vert = localMesh->loopBuffer[faceStart - k];
					int32_t firstLoop = loopTop;
					if (vert >= localMesh->vertAmount) {
						uvgpIndicesLocal[loopTopLocal++] = -1;
						if (!seamFace) {
							int32_t nextVertIndex = (k + 1) % loopAmount;
							int32_t lastVertIndex = (k - 1) % loopAmount;
							int32_t nextVert = localMesh->loopBuffer[faceStart - nextVertIndex];
							int32_t lastVert = localMesh->loopBuffer[faceStart - lastVertIndex];
							if (lastVert >= localMesh->vertAmount && nextVert >= localMesh->vertAmount) {
								continue;
							}
							continue;
						}
						workMesh->vertBuffer[workMesh->vertAmount] =
							localMesh->vertBuffer[vert];
						vert = workMesh->vertAmount++;
					}
					else {
						vert += vertBase;
						int32_t sortPos;
						switch (uvgpLoopsAdded) {
							case 0: {
								sortPos = entry->firstVert;
								break;
							}
							case 1: {
								if (uvgpIndicesLocal[k - 1] >= 0) {
									sortPos = entry->firstVert + 1;
								}
								else {
									sortPos = uvgpLastLoop;
								}
								break;
							}
							case 2: {
								sortPos = uvgpLastLoop;
								vertUvgpIndices[k - 1]--;
								break;
							}
							default: {
								printf("Boundary face merging hit default switch case, aborting\n");
								abort();
							}
						}
						uvgpIndicesLocal[loopTopLocal++] = sortPos;
						vertUvgpIndices[loopTop] = sortPos;
						uvgpLoopsAdded++;
					}
					loopBuffer[loopTop] = vert;
					uvBuffer[loopTop] = localMesh->uvBuffer[faceStart - k];
					groupTable[loopTop++] = groupTop;
				}
				groups[++groupTop] = loopTop;
				entry = entry->next;
			} while(entry);
			workMesh->loopAmount += loopTop;
			workMesh->faceAmount++;
			if (seamFace) {
				for (int32_t k = 0; k < loopTop; ++k) {
					workMesh->loopBuffer[loopBase + k] = loopBuffer[k];
					workMesh->uvBuffer[loopBase + k] = uvBuffer[k];
				}
				continue;
			}
			//insertion sort
			int32_t indexBuffer[12];
			int32_t a = vertUvgpIndices[0];
			int32_t b = vertUvgpIndices[1];
			int32_t order = a < b;
			indexBuffer[0] = !order;
			indexBuffer[1] = order;
			int32_t bufferTop = 2;
			for (int32_t k = bufferTop; k < loopTop; ++k) {
				//k > 0 is intentional, k should stop at 1
				for (int32_t l = bufferTop - 1; l > 0; --l) {
					int32_t insert = vertUvgpIndices[k] < vertUvgpIndices[indexBuffer[l]] &&
									 vertUvgpIndices[k] > vertUvgpIndices[indexBuffer[l - 1]];
					if (!insert) {
						indexBuffer[bufferTop++] = k;
					}
					else {
						for (int32_t m = bufferTop; m > l; --m) {
							indexBuffer[m] = indexBuffer[m - 1];
						}
						indexBuffer[l] = k;
						bufferTop++;
						//vertPairAdd(k, buffer, &bufferTop, groups, groupTable, vertUvgpIndices);
						break;
					}
				}
			}
			for (int32_t k = 0; k < loopTop; ++k) {
				workMesh->loopBuffer[loopBase + k] = loopBuffer[indexBuffer[k]];
				workMesh->uvBuffer[loopBase + k] = uvBuffer[indexBuffer[k]];
			}
		}
		free(localMesh->faceBuffer);
		free(localMesh->loopBuffer);
		free(localMesh->uvBuffer);
		free(localMesh->vertBuffer);
		free(jobArgs[i].boundaryBuffer);
		free(jobArgs[i].finalBoundary);
	}
	workMesh->faceBuffer[workMesh->faceAmount] = workMesh->loopAmount;
	CLOCK_STOP("moving to work mesh");
}

DECL_SPEC_EXPORT void uvgpUpdateMesh(BlenderMeshData *uvgpMesh, BlenderMeshData *workMesh) {
	memcpy(uvgpMesh->vertBuffer, workMesh->vertBuffer, sizeof(Vec3) * uvgpMesh->vertAmount);
	memcpy(uvgpMesh->loopBuffer, workMesh->loopBuffer, sizeof(int32_t) * uvgpMesh->loopAmount);
	memcpy(uvgpMesh->faceBuffer, workMesh->faceBuffer, sizeof(int32_t) * (uvgpMesh->faceAmount + 1));
	free(workMesh->vertBuffer);
	free(workMesh->loopBuffer);
	free(workMesh->faceBuffer);
}

DECL_SPEC_EXPORT void uvgpUpdateMeshUv(BlenderMeshData *uvgpMesh, BlenderMeshData *workMesh) {
	memcpy(uvgpMesh->uvBuffer, workMesh->uvBuffer, sizeof(Vec2) * uvgpMesh->loopAmount);
	free(workMesh->uvBuffer);
}
