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

void clipPass(Vec2 tileMin, int32_t loopTop, Vec3 *loopBuffer, int32_t *indexBuffer, int32_t *newLoopTop,
              Vec3 *newLoopBuffer, int32_t *newIndexBuffer, int32_t *clipLoop,
              Vec3 *clipLoopVerts, Vec2 *uv, Vec2 *uvNext, Vec2 *uvCross, int32_t *insideBuffer){
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
			newIndexBuffer[*newLoopTop] = -1;
			(*newLoopTop)++;
		}
	}
}

void checkIfVertIsDup(int32_t *vertIndex, Vec3 *loopVert, BlenderMeshData *workMesh) {
	for (int32_t i = 0; i < workMesh->vertAmount; ++i) {
		int32_t dup = _(*(Vec2 *)loopVert V2GREAT _(*(Vec2 *)&workMesh->vertBuffer[i] V2SUBS .0001f)) &&
		              _(*(Vec2 *)loopVert V2LESS _(*(Vec2 *)&workMesh->vertBuffer[i] V2ADDS .0001f));
		if (dup) {
			*vertIndex = i;
		}
	}
}

void addCellFaceToWorkMesh(Vec2 tileMin,int32_t loopTop, Vec3 *loopBuffer, int32_t *indexBuffer,
                           BlenderMeshData *workMesh, BlenderMeshData *mesh, int32_t faceStart,
                           int32_t faceEnd, VertAdj *uvgpVertAdj) {
	if (loopTop <= 2) {
		return;
	}
	int32_t vertsAdded = 0;
	for (int32_t i = 0; i < loopTop; ++i) {
		int32_t vertIndex = indexBuffer[i];
		if (vertIndex < 0) {
			vertIndex = workMesh->vertAmount;
			workMesh->vertBuffer[vertIndex] = loopBuffer[i];
			workMesh->vertAmount++;
		}
		else {
			VertAdj *vertAdj = uvgpVertAdj + vertIndex;
			vertIndex = (vertIndex + 1) * -1;
			vertAdj->used = 1;
			vertAdj->loops[vertAdj->loopAmount] = workMesh->loopAmount + i;
			vertAdj->loopAmount++;
		}
		workMesh->loopBuffer[workMesh->loopAmount + i] = vertIndex;
		workMesh->uvBuffer[workMesh->loopAmount + i] = *(Vec2 *)&loopBuffer[i];
	}
	workMesh->faceBuffer[workMesh->faceAmount] = workMesh->loopAmount;
	workMesh->loopAmount += loopTop;
	workMesh->faceAmount++;
}

void clipAgainstFace(Vec2 tileMin, int32_t *loopTop, Vec3 *loopBuffer, int32_t *indexBuffer,
                     BlenderMeshData *mesh, int32_t loopAmount, int32_t faceStart) {
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
		int32_t insideBuffer[12] = {0};
		int32_t newLoopTop = 0;
		Vec2 uvCross = vec2Cross(uvDir);

		clipPass(tileMin, *loopTop, loopBuffer, indexBuffer, &newLoopTop, newLoopBuffer, newIndexBuffer,
		         clipLoop, clipLoopVerts, &uv, &uvNext, &uvCross, insideBuffer);

		if (newLoopTop <= 2) {
			*loopTop = newLoopTop;
			return;
		}
		memcpy(loopBuffer, newLoopBuffer, sizeof(Vec3) * newLoopTop);
		memcpy(indexBuffer, newIndexBuffer, sizeof(int32_t) * newLoopTop);
		*loopTop = newLoopTop;
	}
}

void processCellFaces(Vec2 tileMin, int32_t cellFacesTotal, int32_t *cellFaces, UvgpFileLoaded *fileLoaded,
                      BlenderMeshData *mesh, BlenderMeshData *workMesh, int32_t faceStart,
                      int32_t faceEnd, VertAdj *uvgpVertAdj, uint64_t *timeSpent) {
	struct timeval start, stop;
	CLOCK_START;
	int32_t workMeshVertBase = workMesh->vertAmount;
	for (int32_t i = 0; i < cellFacesTotal; ++i) {
		//need to subtract one, as that face indices are offset by one,
		//in order to be able to negate them when contstructing quadtree (can't negate zero)
		int32_t uvgpFaceIndex = cellFaces[i] - 1;
		int32_t loopAmount = faceEnd - faceStart;
		Vec3 loopBuffer[12];
		int32_t indexBuffer[12];
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
		}
		clipAgainstFace(tileMin, &loopTop, loopBuffer, indexBuffer, mesh, loopAmount, faceStart);
		addCellFaceToWorkMesh(tileMin, loopTop, loopBuffer, indexBuffer, workMesh,
		                      mesh, faceStart, faceEnd, uvgpVertAdj);
	}
	CLOCK_STOP_NO_PRINT;
	timeSpent[0] += getTimeDiff(&start, &stop);
	CLOCK_START;
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
	int32_t vertsAdded = workMesh->vertAmount - workMeshVertBase;
	for (int32_t i = 0; i < vertsAdded; ++i) {
		int32_t vertIndex = workMeshVertBase + i;
		Vec3 vert = workMesh->vertBuffer[vertIndex];
		_((Vec2 *)&vert V2SUBEQL tileMin);
		Vec3 vertBc = cartesianToBarycentric(&triUv0, &triUv1, &triUv2, &vert);
		workMesh->vertBuffer[vertIndex] =
			barycentricToCartesian(triVert0, triVert1, triVert2, &vertBc);
	}
	struct timeval start2, stop2;
	for (int32_t i = 0; i < workMesh->loopAmount; ++i) {
		if (workMesh->loopBuffer[i] >= 0) {
			continue;
		}
		gettimeofday(&start2, NULL);
		int32_t *vertIndex = workMesh->loopBuffer + i;
		*vertIndex = (*vertIndex * -1) - 1;
		Vec3 vert = fileLoaded->data.vertBuffer[*vertIndex];
		_((Vec2 *)&vert V2SUBEQL tileMin);
		Vec3 vertBc = cartesianToBarycentric(&triUv0, &triUv1, &triUv2, &vert);
		workMesh->vertBuffer[workMesh->vertAmount] =
			barycentricToCartesian(triVert0, triVert1, triVert2, &vertBc);
		VertAdj *vertAdj = uvgpVertAdj + *vertIndex;
		for (int32_t j = 0; j < vertAdj->loopAmount; ++j) {
			int32_t loopIndex = vertAdj->loops[j];
			workMesh->loopBuffer[loopIndex] = workMesh->vertAmount;
		}
		vertAdj->used = 0;
		vertAdj->loopAmount = 0;
		workMesh->vertAmount++;
		gettimeofday(&stop2, NULL);
		timeSpent[2] += getTimeDiff(&start2, &stop2);
	}
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

int32_t clipCellsToFace (int32_t *enclosingCellAmount, Cell **enclosingCells, int32_t *cellFacesTotal,
                         int32_t *totalCellFacesNoDup, iVec2 tileMin, int32_t faceStart,
                         int32_t faceEnd, BlenderMeshData *mesh) {
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
	findFullyEnclosingCell(tileMin, enclosingCellAmount, enclosingCells, cellFacesTotal,
	                       totalCellFacesNoDup, fileLoaded->quadTree.rootCell, faceStart,
	                       faceEnd, mesh->loopBuffer, mesh->uvBuffer);
	return 0;
}

int32_t rasterizeFaceInCells(int32_t *cellFacesMax, int32_t faceIndex, FaceCellsInfo *faceCellsInfo, int32_t *totalCellFaces,
                             iVec2 faceBoundsMin, iVec2 faceBoundsMax,
                             int32_t faceStart, int32_t faceEnd, BlenderMeshData *mesh) {
	Cell *enclosingCells[256];
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
			result = clipCellsToFace(&enclosingCellAmount, enclosingCells, totalCellFaces,
			                         &totalCellFacesNoDup, tileMin, faceStart, faceEnd, mesh);
			if (result == 1) {
				//fully enclosed
				return 1;
			}
		}
	}
	faceCellsInfo->cells = malloc(sizeof(Cell *) * enclosingCellAmount);
	memcpy(faceCellsInfo->cells, enclosingCells, sizeof(Cell *) * enclosingCellAmount);
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
								  FaceCellsInfo *faceCellsInfo, BlenderMeshData *mesh) {
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
		                         *faceBoundsMax, faceStart, faceEnd, mesh)) {
			Cell *rootCell = fileLoaded->quadTree.rootCell;
			faceCellsInfo[i].cells = malloc(sizeof(Cell *));
			*faceCellsInfo[i].cells = rootCell;
			*totalCellFaces = rootCell->faceAmount;
			return;
		}
	}
}

void projectOntoMeshJob(void *argsPtr) {
	struct timeval start, stop;
	CLOCK_START;
	ThreadArg *args = argsPtr;
	int32_t cellFacesTotal = 0;;
	iVec2 faceBoundsMin, faceBoundsMax;
	int32_t cellFacesMax = 0;
	FaceCellsInfo *faceCellsInfo = malloc(sizeof(FaceCellsInfo) * args->mesh.faceAmount);
	getAllEnclosingCellsForFaces(&cellFacesMax, &faceBoundsMin, &faceBoundsMax, &cellFacesTotal,
	                             faceCellsInfo, &args->mesh);
	CLOCK_STOP("getting enclosing cells");
	CLOCK_START;
	int32_t bufferSize = args->mesh.faceAmount + cellFacesTotal;
	args->localMesh.faceBuffer = malloc(sizeof(int32_t) * bufferSize);
	args->localMesh.loopBuffer = malloc(sizeof(int32_t) * bufferSize * 2);
	args->localMesh.vertBuffer = malloc(sizeof(Vec3) * bufferSize);
	args->localMesh.uvBuffer = malloc(sizeof(Vec2) * bufferSize * 2);
	int32_t workMeshFaceTop = 0;
	int32_t workMeshLoopTop = 0;
	int32_t workMeshVertTop = 0;
	int32_t workMeshUvTop = 0;
	CLOCK_STOP("allocating local mesh");
	CLOCK_START;
	int32_t *cellFaces = malloc(sizeof(int32_t) * cellFacesMax);
	CLOCK_STOP("allocating cell faces");
	CLOCK_START;
	VertAdj *uvgpVertAdj = calloc(fileLoaded->header.vertAmount, sizeof(VertAdj));
	CLOCK_STOP("allocating vert adj");
	uint64_t timeSpent[3] = {0};
	CLOCK_START;
	for (int32_t i = 0; i < args->mesh.faceAmount; ++i) {
		// copy faces over to a new contiguous array to avoid cache thrashing
		int32_t facesNextIndex = 0;
		for (int32_t j = 0; j < faceCellsInfo[i].cellAmount; ++j) {
			Cell *cell = faceCellsInfo[i].cells[j];
			memcpy(cellFaces + facesNextIndex, cell->faces, sizeof(int32_t) * cell->faceAmount);
			facesNextIndex += cell->faceAmount;
		}
		//iterate through tiles
		for (int32_t j = faceBoundsMin.y; j <= faceBoundsMax.y; ++j) {
			for (int32_t k = faceBoundsMin.x; k <= faceBoundsMax.x; ++k) {
				Vec2 tileMin = {k, j};
				int32_t faceStart = args->mesh.faceBuffer[i];
				int32_t faceEnd = args->mesh.faceBuffer[i + 1];
				processCellFaces(tileMin, faceCellsInfo[i].faceAmount, cellFaces,
				                 fileLoaded, &args->mesh, &args->localMesh, faceStart,
				                 faceEnd, uvgpVertAdj, timeSpent);
			}
		}
		free(faceCellsInfo[i].cells);
	}
	CLOCK_STOP("projecting");
	printf("  ^  project: %lu, move & transform: %lu, memset vert adj: %lu\n",
			timeSpent[0], timeSpent[1], timeSpent[2]);
	CLOCK_START;
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
	int32_t workMeshFaces, workMeshLoops, workMeshVerts;
	workMeshFaces = workMeshLoops = workMeshVerts = 0;
	for (int32_t i = 0; i < threadAmount; ++i) {
		workMeshFaces += jobArgs[i].localMesh.faceAmount;
		workMeshLoops += jobArgs[i].localMesh.loopAmount;
		workMeshVerts += jobArgs[i].localMesh.vertAmount;
	}
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
		int32_t *facesStart = workMesh->faceBuffer + workMesh->faceAmount;
		int32_t *loopsStart = workMesh->loopBuffer + workMesh->loopAmount;
		Vec3 *vertsStart = workMesh->vertBuffer + workMesh->vertAmount;
		Vec2 *uvsStart = workMesh->uvBuffer + workMesh->loopAmount;
		memcpy(facesStart, localMesh->faceBuffer, sizeof(int32_t) * localMesh->faceAmount);
		free(localMesh->faceBuffer);
		workMesh->faceAmount += localMesh->faceAmount;
		memcpy(loopsStart, localMesh->loopBuffer, sizeof(int32_t) * localMesh->loopAmount);
		free(localMesh->loopBuffer);
		memcpy(uvsStart, localMesh->uvBuffer, sizeof(Vec2) * localMesh->loopAmount);
		free(localMesh->uvBuffer);
		workMesh->loopAmount += localMesh->loopAmount;
		memcpy(vertsStart, localMesh->vertBuffer, sizeof(Vec3) * localMesh->vertAmount);
		free(localMesh->vertBuffer);
		workMesh->vertAmount += localMesh->vertAmount;
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
