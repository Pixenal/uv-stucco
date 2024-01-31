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
	iVec2 min, max;
	Vec2 fMin, fMax;
} FaceBounds;

typedef struct {
	int32_t cellFacesTotal;
	int32_t cellFacesMax;
	FaceCellsInfo *pFaceCellsInfo;
	int32_t*pCellFaces;
	int32_t averageRuvmFacesPerFace;
	int8_t *pCellInits;
	FaceInfo faceInfo;
	FaceBounds faceBounds;
} EnclosingCellsVars;

typedef struct {
	int32_t vertAdjSize;
	VertAdj *pRuvmVertAdj;
	int32_t bufferSize;
	int32_t boundaryBufferSize;
} MapToMeshVars;

typedef struct {
	int32_t timeSpent;
	int32_t maxDepth;
} DebugAndPerfVars;

static RuvmFileLoaded *pFileLoaded;

uint64_t getTimeDiff(struct timeval *start, struct timeval *stop) {
	return (stop->tv_sec - start->tv_sec) * 1000000 + (stop->tv_usec - start->tv_usec);
}

DECL_SPEC_EXPORT void RuvmExportRuvmFile(MeshData *pMesh, float *pNormals) {
	printf("%d vertices, and %d faces\n", pMesh->vertSize, pMesh->faceSize);
	pMesh->pNormals = (Vec3 *)pNormals;
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
			Vec3 *pRuvmVert = &pLoopBuffer[i].loop;
			Vec3 *pRuvmVertNext = &pLoopBuffer[vertNextIndex].loop;
			Vec3 ruvmDir = _(*pRuvmVert V3SUB *pRuvmVertNext);
			Vec3 ruvmDirBack = _(*pRuvmVertNext V3SUB *pRuvmVert);
			float t = (pRuvmVert->x - uv->x) * (uv->y - uvNext->y) -
					  (pRuvmVert->y - uv->y) * (uv->x - uvNext->x);
			t /= ruvmDir.x * (uv->y - uvNext->y) -
				 ruvmDir.y * (uv->x - uvNext->x);
			Vec3 intersection = _(*pRuvmVert V3ADD _(ruvmDirBack V3MULS t));
			pNewLoopBuffer[*newLoopSize].loop = intersection;
			//float tNormalized = ruvmDir.x * ruvmDir.x + ruvmDir.y * ruvmDir.y + ruvmDir.z * ruvmDir.z;
			//tNormalized = sqrt(tNormalized);
			//pNewLoopBuffer[*newLoopSize].loop.z = ( * t);
			pNewLoopBuffer[*newLoopSize].index = -1;
			pNewLoopBuffer[*newLoopSize].sort = pLoopBuffer[vertNextIndex].sort;
			//pNewLoopBuffer[*newLoopSize].sort = -1;
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
						  uint32_t *pVertAdjSize, int32_t loopIndex, int32_t *pLastRuvmVert) {
	if (*pFirstRuvmVert < 0) {
		*pFirstRuvmVert = pLoopBuffer[loopBufferIndex].sort;
	}
	*pLastRuvmVert = pLoopBuffer[loopBufferIndex].sort;
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
		                int32_t faceIndex, int32_t tile, int32_t job, int32_t ruvmLoops,
						int32_t loopBufferSize, LoopBuffer *pLoopBuffer, int32_t lastRuvmVert) {
	pEntry->face = pLocalMesh->boundaryFaceSize;
	pEntry->firstVert = firstRuvmVert;
	pEntry->lastVert = lastRuvmVert;
	pEntry->faceIndex = faceIndex;
	pEntry->tile = tile;
	pEntry->job = job;
	pEntry->type = ruvmLoops;
	if (firstRuvmVert < 0) {
		int32_t *pNonRuvmSort = (int32_t *)(pEntry + 1);
		for (int32_t i = 0; i < loopBufferSize; ++i) {
			pNonRuvmSort[i] = pLoopBuffer[i].sort;
		}
	}
}

void addEdgeFaceToBoundaryBuffer(MeshData *pLocalMesh, BoundaryDir *pBoundaryBuffer,
		                         int32_t boundaryBufferSize, int32_t boundaryLoopStart,
								 int32_t faceIndex, int32_t firstRuvmVert, int32_t tile,
								 int32_t *pTotalFaces, int32_t job, int32_t ruvmLoops,
								 int32_t loopBufferSize, LoopBuffer *pLoopBuffer,
								 int32_t lastRuvmVert) {
	pLocalMesh->pFaces[pLocalMesh->boundaryFaceSize] = boundaryLoopStart;
	int32_t hash = fnvHash((uint8_t *)&faceIndex, 4, boundaryBufferSize);
	BoundaryDir *pEntryDir = pBoundaryBuffer + hash;
	BoundaryVert *pEntry = pEntryDir->pEntry;
	int32_t sizeToAllocate = sizeof(BoundaryVert);
	if (firstRuvmVert < 0) {
		sizeToAllocate += sizeof(int32_t) * loopBufferSize;
	}
	if (!pEntry) {
		pEntry = pEntryDir->pEntry = calloc(1, sizeToAllocate);
		initBoundaryBufferEntry(pEntry, pLocalMesh, firstRuvmVert, faceIndex,
				                tile, job, ruvmLoops, loopBufferSize, pLoopBuffer,
								lastRuvmVert);
		++*pTotalFaces;
	}
	else {
		do {
			if (pEntry->faceIndex == faceIndex) {
				while (pEntry->pNext) {
					pEntry = pEntry->pNext;
				}
				pEntry = pEntry->pNext = calloc(1, sizeToAllocate);
				initBoundaryBufferEntry(pEntry, pLocalMesh, firstRuvmVert, faceIndex,
						                tile, job, ruvmLoops, loopBufferSize, pLoopBuffer,
										lastRuvmVert);
				break;
			}
			if (!pEntryDir->pNext) {
				pEntryDir = pEntryDir->pNext = calloc(1, sizeof(BoundaryDir));
				pEntry = pEntryDir->pEntry = calloc(1, sizeToAllocate);
				initBoundaryBufferEntry(pEntry, pLocalMesh, firstRuvmVert, faceIndex,
						                tile, job, ruvmLoops, loopBufferSize, pLoopBuffer,
										lastRuvmVert);
				++*pTotalFaces;
				break;
			}
			pEntryDir = pEntryDir->pNext;
			pEntry = pEntryDir->pEntry;
		} while (1);
	}
	pLocalMesh->boundaryFaceSize--;
}

void addClippedFaceToLocalMesh(int32_t loopBufferSize, LoopBuffer *pLoopBuffer, MeshData *pLocalMesh,
                            MeshData *pMesh, VertAdj *pRuvmVertAdj, uint32_t vertAdjSize,
						    BoundaryDir *pBoundaryBuffer, int32_t boundaryBufferSize,
						    int32_t edgeFace, int32_t faceIndex, int32_t tile, int32_t *pTotalFaces,
							int32_t *pTotalLoops, int32_t job) {
	if (loopBufferSize <= 2) {
		return;
	}
	int32_t loopStart = pLocalMesh->loopSize;
	int32_t boundaryLoopStart = pLocalMesh->boundaryLoopSize;
	int32_t firstRuvmVert = -1;
	int32_t lastRuvmVert = -1;
	int32_t ruvmLoops = 0;
	for (int32_t i = 0; i < loopBufferSize; ++i) {
		int32_t vertIndex = pLoopBuffer[i].index;
		++*pTotalLoops;
		int32_t loopIndex = edgeFace ?
			pLocalMesh->boundaryLoopSize-- : pLocalMesh->loopSize++;
		if (vertIndex < 0) {
			addNewLoopAndOrVert(i, &vertIndex, pLocalMesh, pLoopBuffer);
		}
		else {
			ruvmLoops++;
			addRuvmLoopAndOrVert(i, &vertIndex, pLocalMesh, pLoopBuffer, &firstRuvmVert,
			                     pRuvmVertAdj, &vertAdjSize, loopIndex, &lastRuvmVert);
		}
		pLocalMesh->pLoops[loopIndex] = vertIndex;
	}
	if (edgeFace) {
		addEdgeFaceToBoundaryBuffer(pLocalMesh, pBoundaryBuffer, boundaryBufferSize,
		                            boundaryLoopStart, faceIndex, firstRuvmVert, tile,
									pTotalFaces, job, ruvmLoops, loopBufferSize, pLoopBuffer,
									lastRuvmVert);
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

void transformClippedFaceFromUvToXyz(int32_t loopBufferSize, LoopBuffer *loopBuffer, Vec2 *pTriUv,
                                  Vec3 *pTriXyz, Vec3 *pTriNormals, Vec2 tileMin) {
	for (int32_t j = 0; j < loopBufferSize; ++j) {
		Vec3 vert = loopBuffer[j].loop;
		loopBuffer[j].uv = *(Vec2 *)&vert;
		_((Vec2 *)&vert V2SUBEQL tileMin);
		Vec3 vertBc = cartesianToBarycentric(pTriUv, &vert);
		loopBuffer[j].loop = barycentricToCartesian(pTriXyz, &vertBc);
		Vec3 normal = _(pTriNormals[0] V3MULS vertBc.x);
		_(&normal V3ADDEQL _(pTriNormals[1] V3MULS vertBc.y));
		_(&normal V3ADDEQL _(pTriNormals[2] V3MULS vertBc.z));
		_(&normal V3DIVEQLS vertBc.x + vertBc.y + vertBc.z);
		_(&loopBuffer[j].loop V3ADDEQL _(normal V3MULS vert.z));
	}
}

void mapToSingleFace(Vec2 tileMin, int32_t tile, int32_t cellFacesTotal, int32_t *cellFaces,
                      MeshData *pMesh, MeshData *localMesh, int32_t faceStart,
                      int32_t faceEnd, VertAdj *ruvmVertAdj, uint64_t *timeSpent,
					  uint32_t vertAdjSize, BoundaryDir *boundaryBuffer,
					  int32_t boundaryBufferSize, int32_t *maxDepth, int32_t *pTotalFaces,
					  int32_t *pTotalLoops, int32_t job) {
	struct timeval start, stop;
	if (debugFaceIndex == 161) {
		int a = 0;
	}
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
	Vec3 *pTriNormals = pMesh->pNormals + faceStart;
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
		CLOCK_START;
		clipRuvmFaceAgainstBaseFace(&loopBufferSize, loopBuffer, pMesh, loopSize,
		                            faceStart, &edgeFace);
		CLOCK_STOP_NO_PRINT;
		timeSpent[0] += getTimeDiff(&start, &stop);
		CLOCK_START;
		transformClippedFaceFromUvToXyz(loopBufferSize, loopBuffer, triUv, pTriXyz, pTriNormals, tileMin);
		CLOCK_STOP_NO_PRINT;
		timeSpent[1] += getTimeDiff(&start, &stop);
		CLOCK_START;
		addClippedFaceToLocalMesh(loopBufferSize, loopBuffer, localMesh, pMesh, ruvmVertAdj,
		                      vertAdjSize, boundaryBuffer, boundaryBufferSize,
		                      edgeFace, ruvmFaceIndex, tile, pTotalFaces, pTotalLoops, job);
		CLOCK_STOP_NO_PRINT;
		timeSpent[2] += getTimeDiff(&start, &stop);
	}
	debugFaceIndex++;
	/*
	*/
	//printf("Total vert adj: %d %d %d - depth: %d %d\n", totalEmpty, totalComputed, vertAdjSize, maxDepth, *averageDepth);
	//CLOCK_START;
	//memset(ruvmVertAdj, 0, sizeof(VertAdj) * pFileLoaded->header.vertSize);
	//CLOCK_STOP_NO_PRINT;
	//timeSpent[2] += getTimeDiff(&start, &stop);
}

void getFaceBounds(FaceBounds *pBounds, Vec2 *pUvs, FaceInfo faceInfo) {
	pBounds->fMin.x = pBounds->fMin.y = FLT_MAX;
	pBounds->fMax.x = pBounds->fMax.y = .0f;
	for (int32_t i = 0; i < faceInfo.size; ++i) {
		Vec2 *uv = pUvs + faceInfo.start + i;
		pBounds->fMin.x = uv->x < pBounds->fMin.x ? uv->x : pBounds->fMin.x;
		pBounds->fMin.y = uv->y < pBounds->fMin.y ? uv->y : pBounds->fMin.y;
		pBounds->fMax.x = uv->x > pBounds->fMax.x ? uv->x : pBounds->fMax.x;
		pBounds->fMax.y = uv->y > pBounds->fMax.y ? uv->y : pBounds->fMax.y;
	}
}

void checkIfFaceIsInsideTile(EnclosingCellsVars *pEcVars, int32_t *pIsInsideBuffer,
		                     int32_t *pFaceVertInside, MeshData *pMesh, iVec2 tileMin) {
	FaceInfo *pFaceInfo = &pEcVars->faceInfo;
	FaceBounds *pFaceBounds = &pEcVars->faceBounds;
	for (int32_t i = 0; i < pEcVars->faceInfo.size; ++i) {
		//check if current edge intersects tile
		Vec2 *loop = pMesh->pUvs + pFaceInfo->start + i;
		int32_t nextLoopIndex = pFaceInfo->start + (i + 1) % pFaceInfo->size;
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
		*pFaceVertInside += _(*loop V2GREAT pFaceBounds->fMin) &&
		                    _(*loop V2LESSEQL pFaceBounds->fMax);
	}
}

int32_t getCellsForFaceWithinTile(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                                  EnclosingCellsInfo *pCellsBuffer, iVec2 tileMin) {
	int32_t isInsideBuffer[4] = {1, 1, 1, 1};
	int32_t faceVertInside = 0;
	checkIfFaceIsInsideTile(pEcVars, isInsideBuffer, &faceVertInside, &pArgs->mesh, tileMin);
	int32_t isInside = isInsideBuffer[0] || isInsideBuffer[1] ||
	                   isInsideBuffer[2] || isInsideBuffer[3];
	int32_t isFullyEnclosed = isInsideBuffer[0] && isInsideBuffer[1] &&
	                          isInsideBuffer[2] && isInsideBuffer[3];
	if (isFullyEnclosed) {
		return 1;
	}
	if (!faceVertInside && !isInside) {
		//face is not inside current tile
		return 0;
	}
	//find fully enclosing cell using clipped face
	quadTreeGetAllEnclosingCells(pFileLoaded->quadTree.pRootCell, pCellsBuffer,
	                             pEcVars->pCellInits, &pArgs->mesh, pEcVars->faceInfo, tileMin);
	return 0;
}

void copyCellsIntoTotalList(EnclosingCellsVars *pEcVars, EnclosingCellsInfo *pCellsBuffer,
                            int32_t faceIndex) {
	FaceCellsInfo *pEntry = pEcVars->pFaceCellsInfo + faceIndex;
	pEcVars->cellFacesTotal += pCellsBuffer->faceTotal;
	pEntry->pCells = malloc(sizeof(Cell *) * pCellsBuffer->cellSize);
	pEntry->pCellType = malloc(sizeof(int32_t) * pCellsBuffer->cellSize);
	memcpy(pEntry->pCells, pCellsBuffer->cells, sizeof(Cell *) *
	       pCellsBuffer->cellSize);
	memcpy(pEntry->pCellType, pCellsBuffer->cellType, sizeof(int32_t) *
	       pCellsBuffer->cellSize);
	pEntry->cellSize = pCellsBuffer->cellSize;
	pEntry->faceSize = pCellsBuffer->faceTotalNoDup;
	if (pCellsBuffer->faceTotalNoDup > pEcVars->cellFacesMax) {
		pEcVars->cellFacesMax = pCellsBuffer->faceTotalNoDup;
	}
}

int32_t getCellsForSingleFace(ThreadArg *pArgs, EnclosingCellsVars *pEcVars, int32_t i) {
	EnclosingCellsInfo cellsBuffer = {0};
	FaceBounds *pFaceBounds = &pEcVars->faceBounds;
	for (int32_t i = pFaceBounds->min.y; i <= pFaceBounds->max.y; ++i) {
		for (int32_t j = pFaceBounds->min.x; j <= pFaceBounds->max.x; ++j) {
			iVec2 tileMin = {j, i};
			//continue until the smallest cell that fully encloses the face is found (result == 0).
			//if face fully encloses the while uv tile (result == 1), then return (root cell will be used).
			//if the face is not within the current tile, then skip tile (result == 2).
			if (getCellsForFaceWithinTile(pArgs, pEcVars, &cellsBuffer, tileMin)) {
				//fully enclosed
				return 1;
			}
		}
	}
	copyCellsIntoTotalList(pEcVars, &cellsBuffer, i);
	return 0;
}

void getEnclosingCellsForAllFaces(ThreadArg *pArgs, EnclosingCellsVars *pEcVars) {
	FaceBounds *pFaceBounds = &pEcVars->faceBounds;
	pEcVars->pCellInits = malloc(cellIndex);
	for (int32_t i = 0; i < pArgs->mesh.faceSize; ++i) {
		int32_t start, end;
		start = pEcVars->faceInfo.start = pArgs->mesh.pFaces[i];
		end = pEcVars->faceInfo.end = pArgs->mesh.pFaces[i + 1];
		pEcVars->faceInfo.size = end - start;
		getFaceBounds(pFaceBounds, pArgs->mesh.pUvs, pEcVars->faceInfo);
		pFaceBounds->min = vec2FloorAssign(&pFaceBounds->fMin);
		pFaceBounds->max = vec2FloorAssign(&pFaceBounds->fMax);
		_(&pFaceBounds->fMax V2ADDEQLS 1.0f);
		if (getCellsForSingleFace(pArgs, pEcVars, i)) {
			Cell *rootCell = pFileLoaded->quadTree.pRootCell;
			pEcVars->pFaceCellsInfo[i].pCells = malloc(sizeof(Cell *));
			*pEcVars->pFaceCellsInfo[i].pCells = rootCell;
			pEcVars->cellFacesTotal += rootCell->faceSize;
		}
		pEcVars->averageRuvmFacesPerFace += pEcVars->pFaceCellsInfo[i].faceSize;
		//printf("Total cell amount: %d\n", faceCellsInfo[i].cellSize);
	}
	free(pEcVars->pCellInits);
	pEcVars->averageRuvmFacesPerFace /= pArgs->mesh.faceSize;
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
	pArgs->pBoundaryBuffer = calloc(boundaryBufferSize, sizeof(BoundaryDir));
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

void mapToMeshJob(void *pArgsPtr) {
	struct timeval start, stop;
	CLOCK_START;
	EnclosingCellsVars ecVars = {0};
	ThreadArg *pArgs = pArgsPtr;
	ecVars.pFaceCellsInfo = malloc(sizeof(FaceCellsInfo) * pArgs->mesh.faceSize);
	getEnclosingCellsForAllFaces(pArgs, &ecVars);
	CLOCK_STOP("getting enclosing cells");
	int32_t vertAdjSize;
	int32_t *pCellFaces;
	VertAdj *pRuvmVertAdj;
	int32_t bufferSize = pArgs->mesh.faceSize + ecVars.cellFacesTotal;
	int32_t boundaryBufferSize = 50000;
	pArgs->boundaryBufferSize = boundaryBufferSize;
	allocateStructuresForMapping(pArgs, ecVars.cellFacesMax, bufferSize, boundaryBufferSize,
			                     ecVars.averageRuvmFacesPerFace, &pCellFaces, &pRuvmVertAdj,
								 &vertAdjSize);
	uint64_t timeSpent[3] = {0};
	int32_t maxDepth;
	CLOCK_START;
	for (int32_t i = 0; i < pArgs->mesh.faceSize; ++i) {
		// copy faces over to a new contiguous array
		copyCellFacesIntoSingleArray(ecVars.pFaceCellsInfo, pCellFaces, i);
		//iterate through tiles
		for (int32_t j = ecVars.faceBounds.min.y; j <= ecVars.faceBounds.max.y; ++j) {
			for (int32_t k = ecVars.faceBounds.min.x; k <= ecVars.faceBounds.max.x; ++k) {
				Vec2 tileMin = {k, j};
				int32_t tile = k + (j * ecVars.faceBounds.max.x);
				int32_t faceStart = pArgs->mesh.pFaces[i];
				int32_t faceEnd = pArgs->mesh.pFaces[i + 1];
				mapToSingleFace(tileMin, tile, ecVars.pFaceCellsInfo[i].faceSize, pCellFaces,
				                &pArgs->mesh, &pArgs->localMesh, faceStart, faceEnd,
								pRuvmVertAdj, timeSpent, vertAdjSize, pArgs->pBoundaryBuffer,
								boundaryBufferSize, &maxDepth, &pArgs->totalFaces, &pArgs->totalLoops,
								pArgs->id);
			}
		}
		free(ecVars.pFaceCellsInfo[i].pCells);
	}
	pArgs->localMesh.pFaces[pArgs->localMesh.boundaryFaceSize] = 
		pArgs->localMesh.boundaryLoopSize;
	pArgs->totalBoundaryFaces = pArgs->totalFaces;
	pArgs->totalFaces += pArgs->localMesh.faceSize;
	pArgs->totalLoops += pArgs->localMesh.loopSize;
	//pArgs->totalFaces = pArgs->localMesh.faceSize +
	//	(bufferSize - pArgs->localMesh.boundaryFaceSize);
	//pArgs->totalLoops = pArgs->localMesh.loopSize +
	//	(loopBufferSize - pArgs->localMesh.boundaryLoopSize);
	pArgs->totalVerts = pArgs->localMesh.vertSize + (bufferSize - pArgs->localMesh.boundaryVertSize);
	printf("MaxDepth: %d\n", maxDepth);
	CLOCK_STOP("projecting");
	printf("  ^  project: %lu, move & transform: %lu, memset vert adj: %lu\n",
			timeSpent[0], timeSpent[1], timeSpent[2]);
	CLOCK_START;
	//processBoundaryBuffer(pArgs, bufferSize, boundaryBufferSize);
	CLOCK_STOP("Stringing Boundary Buffer");
	free(pRuvmVertAdj);
	free(pCellFaces);
	free(ecVars.pFaceCellsInfo);
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
                                  int32_t seamFace,
								  int32_t *loopBufferSize, int32_t *pLoopBuffer, Vec2 *pUvBuffer,
								  int32_t *pVertRuvmIndices, FaceInfo *ruvmFaceInfo,
								  int32_t edgeTableSize, EdgeTable *pEdgeTable,
								  int32_t *pRuvmIndicesSort, int32_t entryNum) {
	int32_t ruvmLastLoop = ruvmFaceInfo->size - 1;
	int32_t hashBuffer[32];
	int32_t hashBufferSize = 0;
	int32_t pending = -1;
	do {
		MeshData *localMesh = &pJobArgs[pEntry->job].localMesh;
		int32_t *nonRuvmSort = pEntry->firstVert < 0 ?
			(int32_t *)(pEntry + 1) : NULL;
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
					continue;
				}
				int32_t nextVertIndex = (k + 1) % loopAmount;
				int32_t lastVertIndex = (k - 1) % loopAmount;
				int32_t nextVert = localMesh->pLoops[faceStart - nextVertIndex];
				int32_t lastVert = localMesh->pLoops[faceStart - lastVertIndex];
				if (lastVert >= localMesh->vertSize && nextVert >= localMesh->vertSize) {
					//continue;
				}
				int32_t ruvmLocal;
				int32_t ruvmNextLocal;
				if (nonRuvmSort) {
					ruvmLocal = nonRuvmSort[k];
					ruvmNextLocal = ruvmLocal - 1;
					if (ruvmNextLocal < 0) {
						ruvmNextLocal = ruvmLastLoop;
					}
				}
				else {
					int32_t notDouble = k ? ruvmIndicesLocal[k - 1] >= 0 : 1;
					ruvmLocal = ruvmLoopsAdded && notDouble ?
						mostRecentRuvmLoop : priorRuvmLoop;
					ruvmNextLocal = (ruvmLocal + 1) % ruvmFaceInfo->size;
				}
				pRuvmIndicesSort[*loopBufferSize] = ruvmNextLocal * 10 - 5;
				int32_t ruvmVert = pFileLoaded->mesh.pLoops[ruvmFaceInfo->start + ruvmLocal];
				int32_t ruvmNextVert = pFileLoaded->mesh.pLoops[ruvmFaceInfo->start + ruvmNextLocal];
				uint32_t ruvmEdgeId = ruvmVert + ruvmNextVert;
				int32_t hash = fnvHash((uint8_t *)&ruvmEdgeId, 4, edgeTableSize);
				int32_t dup = 0;
				for (int32_t l = 0; l < hashBufferSize; ++l) {
					if (hash == hashBuffer[l]) {
						dup = 1;
						break;
					}
				}
				if (dup) {
					continue;
				}
				hashBuffer[hashBufferSize] = hash;
				hashBufferSize++;
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
				vert += pJobArgs[pEntry->job].vertBase;
				int32_t sortPos;
				int32_t offset = ruvmIndicesLocal[k - 1] < 0 && ruvmLoopsAdded ?
					1 : 0;
				sortPos = !pEntry->firstVert && offset && pEntry->type == 2 ?
					pEntry->lastVert : pEntry->firstVert + ruvmLoopsAdded + offset;
				mostRecentRuvmLoop = sortPos;
				ruvmIndicesLocal[loopBufferSizeLocal++] = sortPos;
				pRuvmIndicesSort[*loopBufferSize] = sortPos * 10;
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
		int32_t l, insert;
		for (l = bufferSize - 1; l >= 0; --l) {
			insert = pVertRuvmIndices[k] < pVertRuvmIndices[pIndexTable[l]] &&
							 pVertRuvmIndices[k] > pVertRuvmIndices[pIndexTable[l - 1]];
			if (insert) {
				break;
			}
		}
		if (!insert) {
			pIndexTable[bufferSize++] = k;
		}
		else {
			for (int32_t m = bufferSize; m > l; --m) {
				pIndexTable[m] = pIndexTable[m - 1];
			}
			pIndexTable[l] = k;
			bufferSize++;
		}
	}
}

int32_t determineIfSeamFace(BoundaryVert *pEntry, int32_t *pEntryNum) {
	int32_t faceIndex = pEntry->faceIndex;
	int32_t ruvmLoops = 0;
	*pEntryNum = 0;
	do {
		ruvmLoops += pEntry->type;
		pEntry = pEntry->pNext;
		++*pEntryNum;
	} while(pEntry);
	int32_t faceStart = pFileLoaded->mesh.pFaces[faceIndex];
	int32_t faceEnd = pFileLoaded->mesh.pFaces[faceIndex + 1];
	int32_t faceSize = faceEnd - faceStart;
	return ruvmLoops < faceSize;
}

void mergeAndCopyEdgeFaces(MeshData *pWorkMesh, ThreadArg *pJobArgs,
		                   int32_t edgeTableSize, EdgeTable *pEdgeTable,
						   int32_t allBoundaryFacesSize, BoundaryVert **pAllBoundaryFaces) {
	for (int32_t j = 0; j < allBoundaryFacesSize; ++j) {
		int32_t loopBase = pWorkMesh->loopSize;
		pWorkMesh->pFaces[pWorkMesh->faceSize] = loopBase;
		BoundaryVert *pEntry = pAllBoundaryFaces[j];
		int32_t entryNum;
		int32_t seamFace = determineIfSeamFace(pEntry, &entryNum);
		int32_t ruvmIndicesSort[64];
		int32_t vertRuvmIndices[64];
		ruvmIndicesSort[0] = -1;
		vertRuvmIndices[0] = -1;
		int32_t loopBufferSize = 0;
		int32_t loopBuffer[128];
		Vec2 uvBuffer[128];
		FaceInfo ruvmFaceInfo;
		ruvmFaceInfo.start = pFileLoaded->mesh.pFaces[pEntry->faceIndex];
		ruvmFaceInfo.end = pFileLoaded->mesh.pFaces[pEntry->faceIndex + 1];
		ruvmFaceInfo.size = ruvmFaceInfo.end - ruvmFaceInfo.start;
		addFaceLoopsAndVertsToBuffer(pJobArgs, pWorkMesh, pEntry,
									 seamFace, &loopBufferSize, loopBuffer,
									 uvBuffer, vertRuvmIndices + 1, &ruvmFaceInfo,
									 edgeTableSize, pEdgeTable, ruvmIndicesSort + 1,
									 entryNum);
		if (loopBufferSize <= 2) {
			continue;
		}
		pWorkMesh->loopSize += loopBufferSize;
		pWorkMesh->faceSize++;
		if (seamFace && 0) {
			for (int32_t k = 0; k < loopBufferSize; ++k) {
				pWorkMesh->pLoops[loopBase + k] = loopBuffer[k];
				pWorkMesh->pUvs[loopBase + k] = uvBuffer[k];
			}
			continue;
		}
		int32_t indexTable[13];
		indexTable[0] = -1;
		sortLoops(ruvmIndicesSort + 1, indexTable + 1, loopBufferSize);
		for (int32_t k = 0; k < loopBufferSize; ++k) {
			pWorkMesh->pLoops[loopBase + k] = loopBuffer[indexTable[k + 1]];
			pWorkMesh->pUvs[loopBase + k] = uvBuffer[indexTable[k + 1]];
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
	BoundaryVert **pAllBoundaryFaces = malloc(sizeof(void *) * totalBoundaryFaces);
	int32_t allBoundaryFacesSize = 0;
	for (int32_t i = 0; i < threadAmount; ++i) {
		for (int32_t hash = 0; hash < pJobArgs[i].boundaryBufferSize; ++hash) {
			BoundaryDir *pEntryDir = pJobArgs[i].pBoundaryBuffer + hash;
			do {
				if (pEntryDir->pEntry) {
					int32_t faceIndex = pEntryDir->pEntry->faceIndex;
					for (int32_t j = i + 1; j < threadAmount; ++j) {
						BoundaryDir *pEntryDirOther = pJobArgs[j].pBoundaryBuffer + hash;
						do {
							if (pEntryDirOther->pEntry) {
								if (faceIndex == pEntryDirOther->pEntry->faceIndex) {
									BoundaryVert *pEntry = pEntryDir->pEntry;
									while(pEntry->pNext) {
										pEntry = pEntry->pNext;
									}
									pEntry->pNext = pEntryDirOther->pEntry;
									pEntryDirOther->pEntry = NULL;
								}
							}
							pEntryDirOther = pEntryDirOther->pNext;
						} while (pEntryDirOther);
					}
					pAllBoundaryFaces[allBoundaryFacesSize] = pEntryDir->pEntry;
					allBoundaryFacesSize++;
				}
				pEntryDir = pEntryDir->pNext;
			} while (pEntryDir);
		}
	}
	EdgeTable *pEdgeTable = calloc(totalBoundaryFaces, sizeof(EdgeTable));
	int32_t edgeTableSize = totalBoundaryFaces;
	for (int32_t i = 0; i < threadAmount; ++i) {
		pJobArgs[i].vertBase = pWorkMesh->vertSize;
		copyMesh(i, pWorkMesh, pJobArgs);
	}
	mergeAndCopyEdgeFaces(pWorkMesh, pJobArgs, edgeTableSize, pEdgeTable, allBoundaryFacesSize,
	                      pAllBoundaryFaces);
	
	for (int32_t i = 0; i < threadAmount; ++i) {
		MeshData *localMesh = &pJobArgs[i].localMesh;
		free(localMesh->pFaces);
		free(localMesh->pLoops);
		free(localMesh->pUvs);
		free(localMesh->pVerts);
		free(pJobArgs[i].pBoundaryBuffer);
	}
	free(pAllBoundaryFaces);
	free(pEdgeTable);
	pWorkMesh->pFaces[pWorkMesh->faceSize] = pWorkMesh->loopSize;
	CLOCK_STOP("moving to work mesh");
}

DECL_SPEC_EXPORT void ruvmMapToMesh(MeshData *pMesh, MeshData *pWorkMesh, float *pNormals) {
	pMesh->pNormals = (Vec3 *)pNormals;
	
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
