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
int32_t leafSize;

int32_t debugFaceIndex = 0;

float tempTime = 0;

typedef struct {
	int32_t start;
	int32_t end;
	int32_t size;
} FaceInfo;

static RuvmFileLoaded *pFileLoaded;

uint64_t getTimeDiff(struct timeval *start, struct timeval *stop) {
	return (stop->tv_sec - start->tv_sec) * 1000000 + (stop->tv_usec - start->tv_usec);
}

DECL_SPEC_EXPORT void RuvmExportRuvmFile(MeshData *pMesh) {
	printf("%d vertices, and %d faces\n", pMesh->vertSize, pMesh->faceSize);
	writeRuvmFile(pMesh);
}

DECL_SPEC_EXPORT void ruvmLoadRuvmFile(char *filePath) {
	createThreadPool();
	pFileLoaded = calloc(1, sizeof(RuvmFileLoaded));
	loadRuvmFile(pFileLoaded, filePath);
	createQuadTree(&pFileLoaded->quadTree, &pFileLoaded->mesh);
	//writeDebugImage(pFileLoaded->quadTree.rootCell);
}

DECL_SPEC_EXPORT void ruvmUnloadRuvmFile(void *ruvmFile) {
	pFileLoaded = ruvmFile;
	destroyQuadTree(pFileLoaded->quadTree.pRootCell);
	destroyRuvmFile(pFileLoaded);
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

void clipRuvmFaceAgainstSingleLoop(int32_t loopBufferSize, LoopBuffer *pLoopBuffer, int32_t *newLoopSize, 
		                           LoopBuffer *pNewLoopBuffer, Vec2 *uv, Vec2 *uvNext, Vec2 *uvCross,
			                       int32_t *insideBuffer, int32_t loopIndex, int32_t *edgeFace) {
	for (int32_t i = 0; i < loopBufferSize; ++i) {
		Vec2 ruvmVert = *(Vec2 *)&pLoopBuffer[i].loop;
		Vec2 uvRuvmDir = _(ruvmVert V2SUB *uv);
		float dot = _(*uvCross V2DOT uvRuvmDir);
		insideBuffer[i] = dot < .0f;
	}
	for (int32_t i = 0; i < loopBufferSize; ++i) {
		int32_t vertNextIndex = (i + 1) % loopBufferSize;
		if (insideBuffer[i]) {
			pNewLoopBuffer[*newLoopSize] = pLoopBuffer[i];
			(*newLoopSize)++;
		}
		if (insideBuffer[i] != insideBuffer[vertNextIndex]) {
			*edgeFace += 1;
			Vec2 ruvmVert = *(Vec2 *)&pLoopBuffer[i].loop;
			Vec2 ruvmVertNext = *(Vec2 *)&pLoopBuffer[vertNextIndex].loop;
			float t = (ruvmVert.x - uv->x) * (uv->y - uvNext->y) -
					  (ruvmVert.y - uv->y) * (uv->x - uvNext->x);
			t /= (ruvmVert.x - ruvmVertNext.x) * (uv->y - uvNext->y) -
				 (ruvmVert.y - ruvmVertNext.y) * (uv->x - uvNext->x);
			Vec2 intersection = _(ruvmVert V2ADD _(_(ruvmVertNext V2SUB ruvmVert) V2MULS t));
			*(Vec2 *)&pNewLoopBuffer[*newLoopSize] = intersection;
			pNewLoopBuffer[*newLoopSize].loop.z = .0f;
			pNewLoopBuffer[*newLoopSize].index = -1;
			pNewLoopBuffer[*newLoopSize].sort = -1;
			pNewLoopBuffer[*newLoopSize].baseLoop = loopIndex;
			(*newLoopSize)++;
		}
	}
}

void addNewLoopAndOrVert(int32_t loopBufferIndex, int32_t *pVertIndex,
                         MeshData *pLocalMesh, LoopBuffer *pLoopBuffer) {
		*pVertIndex = pLocalMesh->boundaryVertSize;
		pLocalMesh->pVerts[*pVertIndex] = pLoopBuffer[loopBufferIndex].loop;
		pLocalMesh->boundaryVertSize--;
		pLocalMesh->pUvs[pLocalMesh->boundaryLoopSize] = pLoopBuffer[loopBufferIndex].uv;
}

void initVertAdjEntry(int32_t loopBufferIndex, int32_t *pVertIndex, MeshData *pLocalMesh,
                     	  LoopBuffer *pLoopBuffer, VertAdj *pVertAdj) {
	pVertAdj->ruvmVert = *pVertIndex;
	*pVertIndex = pLocalMesh->vertSize++;
	pVertAdj->vert = *pVertIndex;
	pLocalMesh->pVerts[*pVertIndex] = pLoopBuffer[loopBufferIndex].loop;
}

void addRuvmLoopAndOrVert(int32_t loopBufferIndex, int32_t *pVertIndex, MeshData *pLocalMesh,
		                  LoopBuffer *pLoopBuffer, int32_t *pFirstRuvmVert, VertAdj *pRuvmVertAdj,
						  uint32_t *pVertAdjSize, int32_t loopIndex) {
	if (*pFirstRuvmVert < 0) {
		*pFirstRuvmVert = pLoopBuffer[loopBufferIndex].sort;
	}
	uint32_t uVertIndex = *pVertIndex;
	int32_t hash = fnvHash((uint8_t *)&uVertIndex, 4, *pVertAdjSize);
	VertAdj *pVertAdj = pRuvmVertAdj + hash;
	do {
		if (!pVertAdj->loopSize) {
			initVertAdjEntry(loopBufferIndex, pVertIndex, pLocalMesh, pLoopBuffer, pVertAdj);
			break;
		}
		int32_t match = pVertAdj->ruvmVert == *pVertIndex;
		if (match) {
			*pVertIndex = pVertAdj->vert;
			break;
		}
		if (!pVertAdj->pNext) {
			pVertAdj = pVertAdj->pNext = calloc(1, sizeof(VertAdj));
			initVertAdjEntry(loopBufferIndex, pVertIndex, pLocalMesh, pLoopBuffer, pVertAdj);
			break;
		}
		pVertAdj = pVertAdj->pNext;
	} while (1);
	pVertAdj->loopSize++;
	pLocalMesh->pUvs[loopIndex] = pLoopBuffer[loopBufferIndex].uv;
}

void initBoundaryBufferEntry(BoundaryVert *pEntry, MeshData *pLocalMesh, int32_t firstRuvmVert,
		                int32_t faceIndex, int32_t tile) {
	pEntry->face = pLocalMesh->boundaryFaceSize;
	pEntry->firstVert = firstRuvmVert;
	pEntry->faceIndex = faceIndex;
	pEntry->tile = tile;
	pEntry->valid = 1;
}

void addEdgeFaceToBoundaryBuffer(MeshData *pLocalMesh, BoundaryVert *pBoundaryBuffer,
		                         int32_t boundaryBufferSize, int32_t boundaryLoopStart,
								 int32_t faceIndex, int32_t firstRuvmVert, int32_t tile) {
	pLocalMesh->pFaces[pLocalMesh->boundaryFaceSize] = boundaryLoopStart;
	int32_t hash = fnvHash((uint8_t *)&faceIndex, 4, boundaryBufferSize);
	BoundaryVert *entry = pBoundaryBuffer + hash;
	do {
		if (!entry->valid) {
			initBoundaryBufferEntry(entry, pLocalMesh, firstRuvmVert, faceIndex, tile);
			break;
		}
		if (!entry->pNext) {
			entry = entry->pNext = calloc(1, sizeof(BoundaryVert));
			initBoundaryBufferEntry(entry, pLocalMesh, firstRuvmVert, faceIndex, tile);
			break;
		}
		entry = entry->pNext;
	} while (1);
	pLocalMesh->boundaryFaceSize--;
}

void addClippedFaceToLocalMesh(int32_t loopBufferSize, LoopBuffer *pLoopBuffer, MeshData *pLocalMesh,
                            MeshData *pMesh, VertAdj *pRuvmVertAdj, uint32_t vertAdjSize,
						    BoundaryVert *pBoundaryBuffer, int32_t boundaryBufferSize,
						    int32_t edgeFace, int32_t faceIndex, int32_t tile) {
	if (loopBufferSize <= 2) {
		return;
	}
	int32_t loopStart = pLocalMesh->loopSize;
	int32_t boundaryLoopStart = pLocalMesh->boundaryLoopSize;
	int32_t firstRuvmVert = -1;
	for (int32_t i = 0; i < loopBufferSize; ++i) {
		int32_t vertIndex = pLoopBuffer[i].index;
		int32_t loopIndex = edgeFace ?
			pLocalMesh->boundaryLoopSize-- : pLocalMesh->loopSize++;
		if (vertIndex < 0) {
			addNewLoopAndOrVert(i, &vertIndex, pLocalMesh, pLoopBuffer);
		}
		else {
			addRuvmLoopAndOrVert(i, &vertIndex, pLocalMesh, pLoopBuffer, &firstRuvmVert,
			                     pRuvmVertAdj, &vertAdjSize, loopIndex);
		}
		pLocalMesh->pLoops[loopIndex] = vertIndex;
	}
	if (edgeFace) {
		addEdgeFaceToBoundaryBuffer(pLocalMesh, pBoundaryBuffer, boundaryBufferSize,
		                            boundaryLoopStart, faceIndex, firstRuvmVert, tile);
	}
	else {
		pLocalMesh->pFaces[pLocalMesh->faceSize] = loopStart;
		pLocalMesh->faceSize++;
	}
}

void clipRuvmFaceAgainstBaseFace(int32_t *loopBufferSize, LoopBuffer *pLoopBuffer, MeshData *pMesh,
                                 int32_t loopSize, int32_t faceStart, int32_t *edgeFace) {
	for (int32_t i = 0; i < loopSize; ++i) {
		Vec2 uv = pMesh->pUvs[i + faceStart];
		int32_t uvNextIndex = ((i + 1) % loopSize) + faceStart;
		Vec2 uvNext = pMesh->pUvs[uvNextIndex];
		Vec2 uvDir = _(uvNext V2SUB uv);
		LoopBuffer newLoopBuffer[12] = {0};
		int32_t insideBuffer[12] = {0};
		int32_t newLoopSize = 0;
		Vec2 uvCross = vec2Cross(uvDir);

		clipRuvmFaceAgainstSingleLoop(*loopBufferSize, pLoopBuffer, &newLoopSize, newLoopBuffer,
		         				      &uv, &uvNext, &uvCross, insideBuffer, i, edgeFace);

		if (newLoopSize <= 2) {
			*loopBufferSize = newLoopSize;
			return;
		}
		memcpy(pLoopBuffer, newLoopBuffer, sizeof(LoopBuffer) * newLoopSize);
		*loopBufferSize = newLoopSize;
	}
}

void CellsDebugCustom() {
	int a = 0;
	a += 1;
	return;
}

void transformClippedFaceFromUvToXyz(int32_t loopBufferSize, LoopBuffer *loopBuffer, Vec2 *pTriUv,
                                  Vec3 *pTriXyz, Vec2 tileMin) {
	for (int32_t j = 0; j < loopBufferSize; ++j) {
		Vec3 vert = loopBuffer[j].loop;
		loopBuffer[j].uv = *(Vec2 *)&vert;
		_((Vec2 *)&vert V2SUBEQL tileMin);
		Vec3 vertBc = cartesianToBarycentric(pTriUv, &vert);
		loopBuffer[j].loop = barycentricToCartesian(pTriXyz, &vertBc);
	}
}

void mapToSingleFace(Vec2 tileMin, int32_t tile, int32_t cellFacesTotal, int32_t *cellFaces,
                      MeshData *pMesh, MeshData *localMesh, int32_t faceStart,
                      int32_t faceEnd, VertAdj *ruvmVertAdj, uint64_t *timeSpent,
					  uint32_t vertAdjSize, BoundaryVert *boundaryBuffer,
					  int32_t boundaryBufferSize, int32_t *maxDepth) {
	struct timeval start, stop;
	if (debugFaceIndex == 161) {
		int a = 0;
	}
	CLOCK_START;
	//*boundaryFaceStart = workMesh->boundaryLoopSize;
	//int32_t boundaryVertStart = workMesh->boundaryVertSize;
	Vec2 triUv[3];
	triUv[0] = _(pMesh->pUvs[faceStart] V2SUB tileMin);
	triUv[1] = _(pMesh->pUvs[faceStart + 1] V2SUB tileMin);
	triUv[2] = _(pMesh->pUvs[faceStart + 2] V2SUB tileMin);
	Vec3 pTriXyz[3];
	pTriXyz[0] = pMesh->pVerts[pMesh->pLoops[faceStart]];
	pTriXyz[1] = pMesh->pVerts[pMesh->pLoops[faceStart + 1]];
	pTriXyz[2] = pMesh->pVerts[pMesh->pLoops[faceStart + 2]];
	for (int32_t i = 0; i < cellFacesTotal; ++i) {
		int32_t ruvmFaceIndex = cellFaces[i];
		int32_t loopSize = faceEnd - faceStart;
		LoopBuffer loopBuffer[12] = {0};
		int32_t ruvmFaceStart = pFileLoaded->mesh.pFaces[ruvmFaceIndex];
		int32_t ruvmFaceEnd = pFileLoaded->mesh.pFaces[ruvmFaceIndex + 1];
		int32_t ruvmFaceSize = ruvmFaceEnd - ruvmFaceStart;
		int32_t loopBufferSize = ruvmFaceSize;
		for (int32_t j = 0; j < ruvmFaceSize; ++j) {
			int32_t vertIndex = pFileLoaded->mesh.pLoops[ruvmFaceStart + j];
			loopBuffer[j].index = vertIndex;
			loopBuffer[j].loop = pFileLoaded->mesh.pVerts[vertIndex];
			loopBuffer[j].loop.x += tileMin.x;
			loopBuffer[j].loop.y += tileMin.y;
			loopBuffer[j].sort = j;
		}
		int32_t edgeFace = 0;
		clipRuvmFaceAgainstBaseFace(&loopBufferSize, loopBuffer, pMesh, loopSize,
		                            faceStart, &edgeFace);
		transformClippedFaceFromUvToXyz(loopBufferSize, loopBuffer, triUv, pTriXyz, tileMin);
		addClippedFaceToLocalMesh(loopBufferSize, loopBuffer, localMesh, pMesh, ruvmVertAdj,
		                      vertAdjSize, boundaryBuffer, boundaryBufferSize,
		                      edgeFace, ruvmFaceIndex, tile);
	}
	debugFaceIndex++;
	CLOCK_STOP_NO_PRINT;
	timeSpent[0] += getTimeDiff(&start, &stop);
	CLOCK_START;
	/*
	*/
	//printf("Total vert adj: %d %d %d - depth: %d %d\n", totalEmpty, totalComputed, vertAdjSize, maxDepth, *averageDepth);
	CLOCK_STOP_NO_PRINT;
	timeSpent[1] += getTimeDiff(&start, &stop);
	//CLOCK_START;
	//memset(ruvmVertAdj, 0, sizeof(VertAdj) * pFileLoaded->header.vertSize);
	//CLOCK_STOP_NO_PRINT;
	//timeSpent[2] += getTimeDiff(&start, &stop);
}

void getFaceBounds(Vec2 *boundsMin, Vec2 *boundsMax, Vec2 *pUvs, int32_t faceStart, int32_t faceEnd) {
	boundsMin->x = boundsMin->y = FLT_MAX;
	boundsMax->x = boundsMax->y = .0f;
	int32_t faceLoopSize = faceEnd - faceStart;
	for (int32_t i = 0; i < faceLoopSize; ++i) {
		Vec2 *uv = pUvs + faceStart + i;
		boundsMin->x = uv->x < boundsMin->x ? uv->x : boundsMin->x;
		boundsMin->y = uv->y < boundsMin->y ? uv->y : boundsMin->y;
		boundsMax->x = uv->x > boundsMax->x ? uv->x : boundsMax->x;
		boundsMax->y = uv->y > boundsMax->y ? uv->y : boundsMax->y;
	}
}

void checkIfFaceIsInsideTile(int32_t *pIsInsideBuffer, int32_t *pFaceVertInside,
		                     MeshData *pMesh, int32_t faceStart,
							 int32_t loopSize, iVec2 tileMin) {
	for (int32_t i = 0; i < loopSize; ++i) {
		//check if current edge intersects tile
		Vec2 *loop = pMesh->pUvs + faceStart + i;
		int32_t nextLoopIndex = faceStart + (i + 1) % loopSize;
		Vec2 *loopNext = pMesh->pUvs + nextLoopIndex;
		Vec2 loopDir = _(*loopNext V2SUB *loop);
		Vec2 loopCross = vec2Cross(loopDir);
		for (int32_t j = 0; j < 4; ++j) {
			Vec2 cellPoint = {tileMin.x + j % 2, tileMin.y + j / 2};
			Vec2 cellDir = _(cellPoint V2SUB *loop);
			float dot = _(loopCross V2DOT cellDir);
			pIsInsideBuffer[j] *= dot < .0f;
		}
		//in addition, test for face verts inside tile
		//edge cases may not be cause by the above, like if a face entered the tile,
		//and then exited the same side, with a single vert in the tile.
		//Checking for verts will catch this:
		Vec2 tileMinf = {tileMin.x, tileMin.y};
		Vec2 tileMaxf = {tileMinf.x + 1.0f, tileMinf.y + 1.0f};
		*pFaceVertInside += _(*loop V2GREAT tileMinf) && _(*loop V2LESSEQL tileMaxf);
	}
}

int32_t getEnclosingCellsForFaceWithinTile(EnclosingCellsInfo *pEnclosingCellsInfo, iVec2 tileMin,
						 int32_t faceStart, int32_t faceEnd, MeshData *pMesh, int8_t *cellInits) {
	int32_t loopSize = faceEnd - faceStart;
	int32_t isInsideBuffer[4] = {1, 1, 1, 1};
	int32_t faceVertInside = 0;
	checkIfFaceIsInsideTile(isInsideBuffer, &faceVertInside, pMesh, faceStart,
							loopSize, tileMin);
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
	quadTreeGetAllEnclosingCells(tileMin, pEnclosingCellsInfo, pFileLoaded->quadTree.pRootCell,
	                             faceStart, faceEnd, pMesh, cellInits);
	return 0;
}

void copyCellsIntoTotalList(int32_t *pTotalCellFaces, EnclosingCellsInfo *pEnclosingCellsBuffer,
		                   FaceCellsInfo *pFaceCellsInfo, int32_t *pCellFacesMax) {
	*pTotalCellFaces += pEnclosingCellsBuffer->faceTotal;
	pFaceCellsInfo->pCells = malloc(sizeof(Cell *) * pEnclosingCellsBuffer->cellSize);
	pFaceCellsInfo->pCellType = malloc(sizeof(int32_t) * pEnclosingCellsBuffer->cellSize);
	memcpy(pFaceCellsInfo->pCells, pEnclosingCellsBuffer->cells, sizeof(Cell *) *
	       pEnclosingCellsBuffer->cellSize);
	memcpy(pFaceCellsInfo->pCellType, pEnclosingCellsBuffer->cellType, sizeof(int32_t) *
	       pEnclosingCellsBuffer->cellSize);
	pFaceCellsInfo->cellSize = pEnclosingCellsBuffer->cellSize;
	pFaceCellsInfo->faceSize = pEnclosingCellsBuffer->faceTotalNoDup;
	if (pEnclosingCellsBuffer->faceTotalNoDup > *pCellFacesMax) {
		*pCellFacesMax = pEnclosingCellsBuffer->faceTotalNoDup;
	}
}

int32_t getEnclosingCellsForFace(int32_t *cellFacesMax, FaceCellsInfo *faceCellsInfo,
								   int32_t *totalCellFaces, iVec2 faceBoundsMin,
								   iVec2 faceBoundsMax, int32_t faceStart,
								   int32_t faceEnd, MeshData *mesh, int8_t *cellInits) {
	EnclosingCellsInfo enclosingCellsBuffer = {0};
	for (int32_t i = faceBoundsMin.y; i <= faceBoundsMax.y; ++i) {
		for (int32_t j = faceBoundsMin.x; j <= faceBoundsMax.x; ++j) {
			int32_t result;
			iVec2 tileMin = {j, i};
			//continue until the smallest cell that fully encloses the face is found (result == 0).
			//if face fully encloses the while uv tile (result == 1), then return (root cell will be used).
			//if the face is not within the current tile, then skip tile (result == 2).
			result = getEnclosingCellsForFaceWithinTile(&enclosingCellsBuffer, tileMin,
					                                    faceStart, faceEnd, mesh, cellInits);
			if (result == 1) {
				//fully enclosed
				return 1;
			}
		}
	}
	copyCellsIntoTotalList(totalCellFaces, &enclosingCellsBuffer, faceCellsInfo, cellFacesMax);
	return 0;
}

int32_t ruvmFloor(float a) {
	int32_t aTrunc = a;
	aTrunc -= ((float)aTrunc != a) && (a < .0f);
	return aTrunc;
}

void getEnclosingCellsForAllFaces(int32_t *cellFacesMax, iVec2 *faceBoundsMin, iVec2 *faceBoundsMax,
                                  int32_t *totalCellFaces, FaceCellsInfo *faceCellsInfo, MeshData *pMesh,
                                  int32_t *averageRuvmFacesPerFace) {
	int8_t *cellInits = malloc(cellIndex);
	for (int32_t i = 0; i < pMesh->faceSize; ++i) {
		int32_t faceStart = pMesh->pFaces[i];
		int32_t faceEnd = pMesh->pFaces[i + 1];
		Vec2 faceBoundsMinf, faceBoundsMaxf;
		getFaceBounds(&faceBoundsMinf, &faceBoundsMaxf, pMesh->pUvs, faceStart, faceEnd);
		faceBoundsMin->x = ruvmFloor(faceBoundsMinf.x);
		faceBoundsMin->y = ruvmFloor(faceBoundsMinf.y);
		faceBoundsMax->x = ruvmFloor(faceBoundsMaxf.x);
		faceBoundsMax->y = ruvmFloor(faceBoundsMaxf.y);
		if (getEnclosingCellsForFace(cellFacesMax, faceCellsInfo + i, totalCellFaces, *faceBoundsMin,
		                             *faceBoundsMax, faceStart, faceEnd, pMesh, cellInits)) {
			Cell *rootCell = pFileLoaded->quadTree.pRootCell;
			faceCellsInfo[i].pCells = malloc(sizeof(Cell *));
			*faceCellsInfo[i].pCells = rootCell;
			*totalCellFaces += rootCell->faceSize;
		}
		if (faceCellsInfo[i].faceSize < 1000) {
			CellsDebugCustom();
		}
		*averageRuvmFacesPerFace += faceCellsInfo[i].faceSize;
		//printf("Total cell amount: %d\n", faceCellsInfo[i].cellSize);
	}
	free(cellInits);
	*averageRuvmFacesPerFace /= pMesh->faceSize;
}

void allocateStructuresForMapping(ThreadArg *pArgs, int32_t cellFacesMax, int32_t bufferSize,
		                          int32_t boundaryBufferSize, int32_t averageRuvmFacesPerFace,
								  int32_t **pCellFaces, VertAdj **pRuvmVertAdj,
								  int32_t *pVertAdjSize) {
	struct timeval start, stop;
	CLOCK_START;
	//pArgs->boundaryFace = malloc(sizeof(int32_t) * pArgs->mesh.faceSize + 1);
	pArgs->bufferSize = bufferSize;
	int32_t loopBufferSize = bufferSize * 2;
	pArgs->loopBufferSize = loopBufferSize;
	pArgs->pBoundaryBuffer = calloc(boundaryBufferSize, sizeof(BoundaryVert));
	pArgs->localMesh.boundaryVertSize = bufferSize - 1;
	pArgs->localMesh.boundaryLoopSize = loopBufferSize - 1;
	pArgs->localMesh.boundaryFaceSize = bufferSize - 1;
	pArgs->localMesh.pFaces = malloc(sizeof(int32_t) * bufferSize);
	pArgs->localMesh.pLoops = malloc(sizeof(int32_t) * loopBufferSize);
	pArgs->localMesh.pVerts = malloc(sizeof(Vec3) * bufferSize);
	pArgs->localMesh.pUvs = malloc(sizeof(Vec2) * loopBufferSize);
	CLOCK_STOP("allocating local mesh");
	CLOCK_START;
	*pCellFaces = malloc(sizeof(int32_t) * cellFacesMax);
	CLOCK_STOP("allocating cell faces");
	CLOCK_START;
	*pVertAdjSize = averageRuvmFacesPerFace / 10;
	*pRuvmVertAdj = calloc(*pVertAdjSize, sizeof(VertAdj));
	CLOCK_STOP("allocating vert adj");
}

void copyCellFacesIntoSingleArray(FaceCellsInfo *pFaceCellsInfo, int32_t *pCellFaces,
                                  int32_t faceIndex) {
	int32_t facesNextIndex = 0;
	for (int32_t j = 0; j < pFaceCellsInfo[faceIndex].cellSize; ++j) {
		Cell *cell = pFaceCellsInfo[faceIndex].pCells[j];
		if (pFaceCellsInfo[faceIndex].pCellType[j]) {
			memcpy(pCellFaces + facesNextIndex, cell->pEdgeFaces,
					sizeof(int32_t) * cell->edgeFaceSize);
			facesNextIndex += cell->edgeFaceSize;
		}
		if (pFaceCellsInfo[faceIndex].pCellType[j] != 1) {
			memcpy(pCellFaces + facesNextIndex, cell->pFaces,
					sizeof(int32_t) * cell->faceSize);
			facesNextIndex += cell->faceSize;
		}
	}
}

void fillBoundaryEntryBuffer(BoundaryVert *pEntry, BoundaryVert **pEntryBuffer,
                             int32_t *pEntryBufferSize) {
	do {
		if (!pEntry->valid) {
			break;
		}
		pEntryBuffer[*pEntryBufferSize] = pEntry;
		++*pEntryBufferSize;
		BoundaryVert *pNextEntry = pEntry->pNext;
		pEntry->pNext = NULL;
		pEntry = pNextEntry;
	} while (pEntry);
}

void findAndAppendRelatedEntries(ThreadArg *pArgs, BoundaryVert *pEntry, BoundaryVert **pEntryBuffer,
                                 int32_t entryBufferSize, int32_t *pRuvmLoops, int32_t *pTotalLoops,
								 int32_t entryIndex) {
	for (int32_t k = entryIndex + 1; k < entryBufferSize; ++k) {
		if (!pEntryBuffer[k]) {
			continue;
		}
		BoundaryVert *otherEntry = pEntryBuffer[k];
		if (pEntry->faceIndex != otherEntry->faceIndex) {
			continue;
		}
		int32_t otherFaceStart = pArgs->localMesh.pFaces[otherEntry->face];
		int32_t otherFaceEnd = pArgs->localMesh.pFaces[otherEntry->face - 1];
		int32_t otherLoopAmount = otherFaceStart - otherFaceEnd;
		for (int32_t l = 0; l < otherLoopAmount; ++l) {
			int32_t vert = pArgs->localMesh.pLoops[otherFaceStart - l];
			if (vert < pArgs->localMesh.vertSize) {
				++*pRuvmLoops;
			}
		}
		*pTotalLoops += otherLoopAmount;
		pEntryBuffer[k] = NULL;
		BoundaryVert *pNextEntry = pEntry;
		do {
			if (!pNextEntry->pNext) {
				pNextEntry->pNext = otherEntry;
				break;
			}
			pNextEntry = pNextEntry->pNext;
		} while(1);
	}
}

void stringRelatedEntriesTogether(ThreadArg *pArgs, BoundaryVert *pEntry, BoundaryVert **pEntryBuffer,
                                  int32_t entryBufferSize) {
	for (int32_t j = 0; j < entryBufferSize; ++j) {
		if (!pEntryBuffer[j]) {
			continue;
		}
		pEntry = pEntryBuffer[j];
		int32_t faceStart = pArgs->localMesh.pFaces[pEntry->face];
		int32_t faceEnd = pArgs->localMesh.pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		int32_t totalLoops = loopAmount;
		int32_t ruvmLoops = 0;
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = pArgs->localMesh.pLoops[faceStart - k];
			if (vert < pArgs->localMesh.vertSize) {
				ruvmLoops++;
			}
		}
		findAndAppendRelatedEntries(pArgs, pEntry, pEntryBuffer, entryBufferSize,
		                            &ruvmLoops, &totalLoops, j);
		if (totalLoops <= 2) {
			//skip
			return;
		}
		int32_t seamFace = ruvmLoops <= 2;
		pEntry->valid = seamFace + 1;
		pArgs->totalLoops += seamFace ? totalLoops : totalLoops;
		pArgs->totalFaces++;
		pArgs->pFinalBoundary[pArgs->totalBoundaryFaces] = pEntry;
		pArgs->totalBoundaryFaces++;
	}
}

void processBoundaryBuffer(ThreadArg *pArgs, int32_t bufferSize, int32_t boundaryBufferSize) {
	pArgs->pFinalBoundary = malloc(sizeof(BoundaryVert * ) *
			(bufferSize - pArgs->localMesh.boundaryFaceSize));
	pArgs->totalBoundaryFaces = 0;
	BoundaryVert *entryBuffer[256];
	int32_t entryBufferSize = 0;
	for (int32_t i = 0; i < boundaryBufferSize; ++i) {
		BoundaryVert *pEntry = pArgs->pBoundaryBuffer + i;
		fillBoundaryEntryBuffer(pEntry, entryBuffer, &entryBufferSize);
		stringRelatedEntriesTogether(pArgs, pEntry, entryBuffer, entryBufferSize);
		entryBufferSize = 0;
	}

}

void mapToMeshJob(void *pArgsPtr) {
	struct timeval start, stop;
	CLOCK_START;
	ThreadArg *pArgs = pArgsPtr;
	int32_t cellFacesTotal = 0;;
	iVec2 faceBoundsMin, faceBoundsMax;
	int32_t cellFacesMax = 0;
	FaceCellsInfo *pFaceCellsInfo = malloc(sizeof(FaceCellsInfo) * pArgs->mesh.faceSize);
	int32_t averageRuvmFacesPerFace = 0;
	getEnclosingCellsForAllFaces(&cellFacesMax, &faceBoundsMin, &faceBoundsMax, &cellFacesTotal,
	                             pFaceCellsInfo, &pArgs->mesh, &averageRuvmFacesPerFace);
	CLOCK_STOP("getting enclosing cells");
	int32_t vertAdjSize;
	int32_t *pCellFaces;
	VertAdj *pRuvmVertAdj;
	int32_t bufferSize = pArgs->mesh.faceSize + cellFacesTotal;
	int32_t boundaryBufferSize = bufferSize;
	allocateStructuresForMapping(pArgs, cellFacesMax, bufferSize, boundaryBufferSize,
			                     averageRuvmFacesPerFace, &pCellFaces, &pRuvmVertAdj,
								 &vertAdjSize);
	uint64_t timeSpent[3] = {0};
	int32_t maxDepth;
	CLOCK_START;
	for (int32_t i = 0; i < pArgs->mesh.faceSize; ++i) {
		// copy faces over to a new contiguous array
		copyCellFacesIntoSingleArray(pFaceCellsInfo, pCellFaces, i);
		//iterate through tiles
		for (int32_t j = faceBoundsMin.y; j <= faceBoundsMax.y; ++j) {
			for (int32_t k = faceBoundsMin.x; k <= faceBoundsMax.x; ++k) {
				Vec2 tileMin = {k, j};
				int32_t tile = k + (j * faceBoundsMax.x);
				int32_t faceStart = pArgs->mesh.pFaces[i];
				int32_t faceEnd = pArgs->mesh.pFaces[i + 1];
				mapToSingleFace(tileMin, tile, pFaceCellsInfo[i].faceSize, pCellFaces,
				                &pArgs->mesh, &pArgs->localMesh, faceStart, faceEnd,
								pRuvmVertAdj, timeSpent, vertAdjSize, pArgs->pBoundaryBuffer,
								boundaryBufferSize, &maxDepth);
			}
		}
		free(pFaceCellsInfo[i].pCells);
	}
	pArgs->localMesh.pFaces[pArgs->localMesh.boundaryFaceSize] = 
		pArgs->localMesh.boundaryLoopSize;
	pArgs->totalFaces = pArgs->localMesh.faceSize;
	pArgs->totalLoops = pArgs->localMesh.loopSize;
	//pArgs->totalFaces = pArgs->localMesh.faceSize +
	//	(bufferSize - pArgs->localMesh.boundaryFaceSize);
	//pArgs->totalLoops = pArgs->localMesh.loopSize +
	//	(loopBufferSize - pArgs->localMesh.boundaryLoopSize);
	pArgs->totalVerts = pArgs->localMesh.vertSize;
	printf("MaxDepth: %d\n", maxDepth);
	CLOCK_STOP("projecting");
	printf("  ^  project: %lu, move & transform: %lu, memset vert adj: %lu\n",
			timeSpent[0], timeSpent[1], timeSpent[2]);
	CLOCK_START;
	if (0) {
	}
	processBoundaryBuffer(pArgs, bufferSize, boundaryBufferSize);
	free(pRuvmVertAdj);
	free(pCellFaces);
	free(pFaceCellsInfo);
	CLOCK_STOP("freeing memory");
	CLOCK_START;
	mutexLock();
	++*pArgs->pJobsCompleted;
	mutexUnlock();
	CLOCK_STOP("setting jobs completed");
}

void sendOffJobs(ThreadArg *pJobArgs, int32_t *pJobsCompleted, MeshData *pMesh) {
	struct timeval start, stop;
	CLOCK_START;
	int32_t facesPerThread = pMesh->faceSize / threadAmount;
	int32_t threadAmountMinus1 = threadAmount - 1;
	void *jobArgPtrs[MAX_THREADS];
	for (int32_t i = 0; i < threadAmount; ++i) {
		int32_t meshStart = facesPerThread * i;
		int32_t meshEnd = i == threadAmountMinus1 ?
			pMesh->faceSize : meshStart + facesPerThread;
		MeshData meshPart = *pMesh;
		meshPart.pFaces += meshStart;
		meshPart.faceSize = meshEnd - meshStart;
		pJobArgs[i].averageVertAdjDepth = 0;
		pJobArgs[i].mesh = meshPart;
		pJobArgs[i].pJobsCompleted = pJobsCompleted;
		pJobArgs[i].id = i;
		jobArgPtrs[i] = pJobArgs + i;
	}
	pushJobs(threadAmount, mapToMeshJob, jobArgPtrs);
	CLOCK_STOP("send off jobs");
}

void allocateWorkMesh(MeshData *pWorkMesh, ThreadArg *pJobArgs) {
	int32_t averageVertAdjDepth = 0;
	int32_t workMeshFaces, workMeshLoops, workMeshVerts;
	workMeshFaces = workMeshLoops = workMeshVerts = 0;
	for (int32_t i = 0; i < threadAmount; ++i) {
		averageVertAdjDepth += pJobArgs[i].averageVertAdjDepth;
		workMeshFaces += pJobArgs[i].totalFaces;
		workMeshLoops += pJobArgs[i].totalLoops;
		workMeshVerts += pJobArgs[i].totalVerts;
	}
	averageVertAdjDepth /= threadAmount;
	printf("Average Vert Adj Depth: %d\n", averageVertAdjDepth);

	pWorkMesh->pFaces = malloc(sizeof(int32_t) * (workMeshFaces + 1));
	pWorkMesh->pLoops = malloc(sizeof(int32_t) * workMeshLoops);
	pWorkMesh->pVerts = malloc(sizeof(Vec3) * workMeshVerts);
	pWorkMesh->pUvs = malloc(sizeof(Vec2) * workMeshLoops);
}

void copyMesh(int32_t jobIndex, MeshData *pWorkMesh, ThreadArg *pJobArgs) {
	MeshData *localMesh = &pJobArgs[jobIndex].localMesh;
	for (int32_t j = 0; j < localMesh->faceSize; ++j) {
		localMesh->pFaces[j] += pWorkMesh->loopSize;
	}
	for (int32_t j = 0; j < localMesh->loopSize; ++j) {
		localMesh->pLoops[j] += pWorkMesh->vertSize;
	}
	int32_t *facesStart = pWorkMesh->pFaces + pWorkMesh->faceSize;
	int32_t *loopsStart = pWorkMesh->pLoops + pWorkMesh->loopSize;
	Vec3 *vertsStart = pWorkMesh->pVerts + pWorkMesh->vertSize;
	Vec2 *uvsStart = pWorkMesh->pUvs + pWorkMesh->loopSize;
	memcpy(facesStart, localMesh->pFaces, sizeof(int32_t) * localMesh->faceSize);
	pWorkMesh->faceSize += localMesh->faceSize;
	memcpy(loopsStart, localMesh->pLoops, sizeof(int32_t) * localMesh->loopSize);
	memcpy(uvsStart, localMesh->pUvs, sizeof(Vec2) * localMesh->loopSize);
	pWorkMesh->loopSize += localMesh->loopSize;
	memcpy(vertsStart, localMesh->pVerts, sizeof(Vec3) * localMesh->vertSize);
	pWorkMesh->vertSize += localMesh->vertSize;
}

void initEdgeTableEntry(EdgeTable *pEdgeEntry, BoundaryVert *pEntry, MeshData *pWorkMesh,
                        MeshData *pLocalMesh, int32_t ruvmVert, int32_t vert) {
	pWorkMesh->pVerts[pWorkMesh->vertSize] =
		pLocalMesh->pVerts[vert];
	pEdgeEntry->ruvmVert = ruvmVert;
	pEdgeEntry->vert = pWorkMesh->vertSize;
	pWorkMesh->vertSize++;
	pEdgeEntry->tile = pEntry->tile;
	pEdgeEntry->loops = 1;
}

void addFaceLoopsAndVertsToBuffer(ThreadArg *pJobArgs, MeshData *pWorkMesh, BoundaryVert *pEntry,
                                  int32_t jobIndex, int32_t vertBase, int32_t seamFace,
								  int32_t *loopBufferSize, int32_t *pLoopBuffer, Vec2 *pUvBuffer,
								  int32_t *pVertRuvmIndices, FaceInfo *ruvmFaceInfo,
								  int32_t edgeTableSize, EdgeTable *pEdgeTable) {
	MeshData *localMesh = &pJobArgs[jobIndex].localMesh;
	int32_t ruvmLastLoop = ruvmFaceInfo->size - 1;
	do {
		int32_t ruvmIndicesLocal[64];
		int32_t loopBufferSizeLocal = 0;
		int32_t ruvmLoopsAdded = 0;
		int32_t faceStart = localMesh->pFaces[pEntry->face];
		int32_t faceEnd = localMesh->pFaces[pEntry->face - 1];
		int32_t loopAmount = faceStart - faceEnd;
		int32_t mostRecentRuvmLoop;
		int32_t priorRuvmLoop = pEntry->firstVert - 1;
		if (priorRuvmLoop < 0) {
			priorRuvmLoop = ruvmLastLoop;
		}
		for (int32_t k = 0; k < loopAmount; ++k) {
			int32_t vert = localMesh->pLoops[faceStart - k];
			if (vert >= localMesh->vertSize) {
				ruvmIndicesLocal[loopBufferSizeLocal++] = -1;
				if (!seamFace) {
					/*
					int32_t nextVertIndex = (k + 1) % loopAmount;
					int32_t lastVertIndex = (k - 1) % loopAmount;
					int32_t nextVert = localMesh->pLoops[faceStart - nextVertIndex];
					int32_t lastVert = localMesh->pLoops[faceStart - lastVertIndex];
					if (lastVert >= localMesh->vertSize && nextVert >= localMesh->vertSize) {
						continue;
					}
					*/
					continue;
				}
				int32_t notDouble = k ? ruvmIndicesLocal[k - 1] >= 0 : 1;
				int32_t ruvmLocal = ruvmLoopsAdded && notDouble ?
					mostRecentRuvmLoop : priorRuvmLoop;
				int32_t ruvmNextLocal = (ruvmLocal + 1) % ruvmFaceInfo->size;
				int32_t ruvmVert = pFileLoaded->mesh.pLoops[ruvmFaceInfo->start + ruvmLocal];
				int32_t ruvmNextVert = pFileLoaded->mesh.pLoops[ruvmFaceInfo->start + ruvmNextLocal];
				uint32_t ruvmEdgeId = ruvmVert + ruvmNextVert;
				int32_t hash = fnvHash((uint8_t *)&ruvmEdgeId, 4, edgeTableSize);
				EdgeTable *pEdgeEntry = pEdgeTable + hash;
				if (!pEdgeEntry->loops) {
					initEdgeTableEntry(pEdgeEntry, pEntry, pWorkMesh, localMesh,
									   ruvmVert, vert);
					vert = pEdgeEntry->vert;
				}
				else {
					do {
						int32_t match = (pEdgeEntry->ruvmVert == ruvmVert ||
						                 pEdgeEntry->ruvmVert == ruvmNextVert) &&
										 pEdgeEntry->tile == pEntry->tile;
						if (match) {
							vert = pEdgeEntry->vert;
							break;
						}
						if (!pEdgeEntry->pNext) {
							pEdgeEntry = pEdgeEntry->pNext = calloc(1, sizeof(EdgeTable));
							initEdgeTableEntry(pEdgeEntry, pEntry, pWorkMesh, localMesh,
							                   ruvmVert, vert);
							vert = pEdgeEntry->vert;
							break;
						}
						pEdgeEntry = pEdgeEntry->pNext;
					} while(1);
				}
			}
			else {
				vert += vertBase;
				int32_t sortPos;
				switch (ruvmLoopsAdded) {
					case 0: {
						sortPos = pEntry->firstVert;
						break;
					}
					case 1: {
						if (ruvmIndicesLocal[k - 1] >= 0) {
							sortPos = pEntry->firstVert + 1;
						}
						else {
							sortPos = ruvmLastLoop;
						}
						break;
					}
					case 2: {
						sortPos = ruvmLastLoop;
						pVertRuvmIndices[k - 1]--;
						break;
					}
					default: {
						printf("Boundary face merging hit default switch case, aborting\n");
						abort();
					}
				}
				mostRecentRuvmLoop = sortPos;
				ruvmIndicesLocal[loopBufferSizeLocal++] = sortPos;
				pVertRuvmIndices[*loopBufferSize] = sortPos;
				ruvmLoopsAdded++;
			}
			pLoopBuffer[*loopBufferSize] = vert;
			pUvBuffer[*loopBufferSize] = localMesh->pUvs[faceStart - k];
			(*loopBufferSize)++;
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
}

void sortLoops(int32_t *pVertRuvmIndices, int32_t *pIndexTable, int32_t loopBufferSize) {
	//insertion sort
	int32_t a = pVertRuvmIndices[0];
	int32_t b = pVertRuvmIndices[1];
	int32_t order = a < b;
	pIndexTable[0] = !order;
	pIndexTable[1] = order;
	int32_t bufferSize = 2;
	for (int32_t k = bufferSize; k < loopBufferSize; ++k) {
		//k > 0 is intentional, k should stop at 1
		for (int32_t l = bufferSize - 1; l > 0; --l) {
			int32_t insert = pVertRuvmIndices[k] < pVertRuvmIndices[pIndexTable[l]] &&
							 pVertRuvmIndices[k] > pVertRuvmIndices[pIndexTable[l - 1]];
			if (!insert) {
				pIndexTable[bufferSize++] = k;
			}
			else {
				for (int32_t m = bufferSize; m > l; --m) {
					pIndexTable[m] = pIndexTable[m - 1];
				}
				pIndexTable[l] = k;
				bufferSize++;
				//vertPairAdd(k, buffer, &bufferSize, groups, groupTable, vertRuvmIndices);
				break;
			}
		}
	}
}

void mergeAndCopyEdgeFaces(int32_t jobIndex, MeshData *pWorkMesh, ThreadArg *pJobArgs,
		                   int32_t vertBase, int32_t edgeTableSize, EdgeTable *pEdgeTable) {
	for (int32_t j = 0; j < pJobArgs[jobIndex].totalBoundaryFaces; ++j) {
		int32_t loopBase = pWorkMesh->loopSize;
		pWorkMesh->pFaces[pWorkMesh->faceSize] = loopBase;
		BoundaryVert *pEntry = pJobArgs[jobIndex].pFinalBoundary[j];
		int32_t seamFace = pEntry->valid - 1;
		int32_t vertRuvmIndices[64];
		int32_t loopBufferSize = 0;
		int32_t loopBuffer[128];
		Vec2 uvBuffer[128];
		FaceInfo ruvmFaceInfo;
		ruvmFaceInfo.start = pFileLoaded->mesh.pFaces[pEntry->faceIndex];
		ruvmFaceInfo.end = pFileLoaded->mesh.pFaces[pEntry->faceIndex + 1];
		ruvmFaceInfo.size = ruvmFaceInfo.end - ruvmFaceInfo.start;
		addFaceLoopsAndVertsToBuffer(pJobArgs, pWorkMesh, pEntry, jobIndex, vertBase,
									 seamFace, &loopBufferSize, loopBuffer,
									 uvBuffer, vertRuvmIndices, &ruvmFaceInfo,
									 edgeTableSize, pEdgeTable);
		pWorkMesh->loopSize += loopBufferSize;
		pWorkMesh->faceSize++;
		if (seamFace) {
			for (int32_t k = 0; k < loopBufferSize; ++k) {
				pWorkMesh->pLoops[loopBase + k] = loopBuffer[k];
				pWorkMesh->pUvs[loopBase + k] = uvBuffer[k];
			}
			continue;
		}
		int32_t indexTable[12];
		sortLoops(vertRuvmIndices, indexTable, loopBufferSize);
		for (int32_t k = 0; k < loopBufferSize; ++k) {
			pWorkMesh->pLoops[loopBase + k] = loopBuffer[indexTable[k]];
			pWorkMesh->pUvs[loopBase + k] = uvBuffer[indexTable[k]];
		}
	}

}

void combineJobMeshesIntoSingleMesh(MeshData *pWorkMesh, ThreadArg *pJobArgs) {
	struct timeval start, stop;
	CLOCK_START;
	allocateWorkMesh(pWorkMesh, pJobArgs);
	int32_t totalBoundaryFaces = 0;
	for (int32_t i = 0; i < threadAmount; ++i) {
		totalBoundaryFaces += pJobArgs[i].totalBoundaryFaces;
	}
	EdgeTable *pEdgeTable = calloc(totalBoundaryFaces, sizeof(EdgeTable *));
	int32_t edgeTableSize = totalBoundaryFaces;
	for (int32_t i = 0; i < threadAmount; ++i) {
		int32_t vertBase = pWorkMesh->vertSize;
		copyMesh(i, pWorkMesh, pJobArgs);
		mergeAndCopyEdgeFaces(i, pWorkMesh, pJobArgs, vertBase, edgeTableSize, pEdgeTable);
		
		MeshData *localMesh = &pJobArgs[i].localMesh;
		free(localMesh->pFaces);
		free(localMesh->pLoops);
		free(localMesh->pUvs);
		free(localMesh->pVerts);
		free(pJobArgs[i].pBoundaryBuffer);
		free(pJobArgs[i].pFinalBoundary);
	}
	free(pEdgeTable);
	pWorkMesh->pFaces[pWorkMesh->faceSize] = pWorkMesh->loopSize;
	CLOCK_STOP("moving to work mesh");
}

DECL_SPEC_EXPORT void ruvmMapToMesh(MeshData *pMesh, MeshData *pWorkMesh) {
	ThreadArg jobArgs[MAX_THREADS] = {0};
	int32_t jobsCompleted = 0;
	sendOffJobs(jobArgs, &jobsCompleted, pMesh);

	struct timeval start, stop;
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

	combineJobMeshesIntoSingleMesh(pWorkMesh, jobArgs);
}

DECL_SPEC_EXPORT void ruvmUpdateMesh(MeshData *ruvmMesh, MeshData *workMesh) {
	memcpy(ruvmMesh->pVerts, workMesh->pVerts, sizeof(Vec3) * ruvmMesh->vertSize);
	memcpy(ruvmMesh->pLoops, workMesh->pLoops, sizeof(int32_t) * ruvmMesh->loopSize);
	memcpy(ruvmMesh->pFaces, workMesh->pFaces, sizeof(int32_t) * (ruvmMesh->faceSize + 1));
	free(workMesh->pVerts);
	free(workMesh->pLoops);
	free(workMesh->pFaces);
}

DECL_SPEC_EXPORT void ruvmUpdateMeshUv(MeshData *ruvmMesh, MeshData *workMesh) {
	memcpy(ruvmMesh->pUvs, workMesh->pUvs, sizeof(Vec2) * ruvmMesh->loopSize);
	free(workMesh->pUvs);
}
