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
	Vec2 fMinSmall, fMaxSmall;
} FaceBounds;

typedef struct {
	int32_t index;
	Vec2 vert;
	Vec2 vertNext;
	Vec2 dir;
	Vec2 dirBack;
} LoopInfo;

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
	uint32_t vertAdjSize;
	VertAdj *pRuvmVertAdj;
} MapToMeshVars;

typedef struct {
	int32_t loopStart;
	int32_t boundaryLoopStart;
	int32_t firstRuvmVert, lastRuvmVert;
	int32_t ruvmLoops;
	int32_t vertIndex;
	int32_t loopIndex;
} AddClippedFaceVars;

typedef struct {
	uint64_t timeSpent[3];
	int32_t maxDepth;
} DebugAndPerfVars;

typedef struct {
	Vec2 uv[3];
	Vec3 xyz[3];
	Vec3 *pNormals;
} BaseTriVerts;

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

void clipRuvmFaceAgainstSingleLoop(LoopBufferWrap *pLoopBuf, LoopBufferWrap *pNewLoopBuf,
                                   int32_t *pInsideBuf, LoopInfo *pBaseLoop,
								   Vec2 baseLoopCross, int32_t *pEdgeFace) {
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		Vec2 ruvmVert = *(Vec2 *)&pLoopBuf->buf[i].loop;
		Vec2 uvRuvmDir = _(ruvmVert V2SUB pBaseLoop->vert);
		float dot = _(baseLoopCross V2DOT uvRuvmDir);
		pInsideBuf[i] = dot < .0f;
	}
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		int32_t vertNextIndex = (i + 1) % pLoopBuf->size;
		if (pInsideBuf[i]) {
			pNewLoopBuf->buf[pNewLoopBuf->size] = pLoopBuf->buf[i];
			(pNewLoopBuf->size)++;
		}
		if (pInsideBuf[i] != pInsideBuf[vertNextIndex]) {
			*pEdgeFace += 1;
			Vec3 *pRuvmVert = &pLoopBuf->buf[i].loop;
			Vec3 *pRuvmVertNext = &pLoopBuf->buf[vertNextIndex].loop;
			Vec3 ruvmDir = _(*pRuvmVert V3SUB *pRuvmVertNext);
			Vec3 ruvmDirBack = _(*pRuvmVertNext V3SUB *pRuvmVert);
			float t = (pRuvmVert->x - pBaseLoop->vert.x) * pBaseLoop->dirBack.y;
			t -= (pRuvmVert->y - pBaseLoop->vert.y) * pBaseLoop->dirBack.x;
			t /= ruvmDir.x * pBaseLoop->dirBack.y - ruvmDir.y * pBaseLoop->dirBack.x;
			Vec3 intersection = _(*pRuvmVert V3ADD _(ruvmDirBack V3MULS t));
			LoopBuffer *pNewEntry = pNewLoopBuf->buf + pNewLoopBuf->size;
			pNewLoopBuf->size++;
			pNewEntry->loop = intersection;
			pNewEntry->index = -1;
			pNewEntry->sort = pLoopBuf->buf[vertNextIndex].sort;
			pNewEntry->baseLoop = pBaseLoop->index;
		}
	}
}

void clipRuvmFaceAgainstBaseFace(ThreadArg *pArgs, FaceInfo baseFace,
                                 LoopBufferWrap *pLoopBuf, int32_t *pEdgeFace) {
	for (int32_t i = 0; i < baseFace.size; ++i) {
		LoopInfo baseLoop;
		baseLoop.index = i;
		baseLoop.vert = pArgs->mesh.pUvs[i + baseFace.start];
		int32_t uvNextIndex = ((i + 1) % baseFace.size) + baseFace.start;
		baseLoop.vertNext = pArgs->mesh.pUvs[uvNextIndex];
		baseLoop.dir = _(baseLoop.vertNext V2SUB baseLoop.vert);
		baseLoop.dirBack = _(baseLoop.vert V2SUB baseLoop.vertNext);
		LoopBufferWrap newLoopBuf = {0};
		int32_t insideBuf[12] = {0};
		Vec2 baseLoopCross = vec2Cross(baseLoop.dir);

		clipRuvmFaceAgainstSingleLoop(pLoopBuf, &newLoopBuf, insideBuf,
		         				      &baseLoop, baseLoopCross, pEdgeFace);

		if (newLoopBuf.size <= 2) {
			pLoopBuf->size = newLoopBuf.size;
			return;
		}
		memcpy(pLoopBuf->buf, newLoopBuf.buf, sizeof(LoopBuffer) * newLoopBuf.size);
		pLoopBuf->size = newLoopBuf.size;
	}
}

void transformClippedFaceFromUvToXyz(LoopBufferWrap *pLoopBuf, BaseTriVerts baseTri,
									 Vec2 tileMin) {
	for (int32_t j = 0; j < pLoopBuf->size; ++j) {
		Vec3 vert = pLoopBuf->buf[j].loop;
		pLoopBuf->buf[j].uv = *(Vec2 *)&vert;
		_((Vec2 *)&vert V2SUBEQL tileMin);
		Vec3 vertBc = cartesianToBarycentric(baseTri.uv, &vert);
		pLoopBuf->buf[j].loop = barycentricToCartesian(baseTri.xyz, &vertBc);
		Vec3 normal = _(baseTri.pNormals[0] V3MULS vertBc.x);
		_(&normal V3ADDEQL _(baseTri.pNormals[1] V3MULS vertBc.y));
		_(&normal V3ADDEQL _(baseTri.pNormals[2] V3MULS vertBc.z));
		_(&normal V3DIVEQLS vertBc.x + vertBc.y + vertBc.z);
		_(&pLoopBuf->buf[j].loop V3ADDEQL _(normal V3MULS vert.z));
	}
}


void addNewLoopAndOrVert(int32_t loopBufIndex, int32_t *pVertIndex,
                         MeshData *pLocalMesh, LoopBuffer *pLoopBuffer) {
		*pVertIndex = pLocalMesh->boundaryVertSize;
		pLocalMesh->pVerts[*pVertIndex] = pLoopBuffer[loopBufIndex].loop;
		pLocalMesh->boundaryVertSize--;
		pLocalMesh->pUvs[pLocalMesh->boundaryLoopSize] = pLoopBuffer[loopBufIndex].uv;
}

void initVertAdjEntry(int32_t loopBufferIndex, int32_t *pVertIndex, MeshData *pLocalMesh,
                      LoopBuffer *pLoopBuffer, VertAdj *pVertAdj) {
	pVertAdj->ruvmVert = *pVertIndex;
	*pVertIndex = pLocalMesh->vertSize++;
	pVertAdj->vert = *pVertIndex;
	pLocalMesh->pVerts[*pVertIndex] = pLoopBuffer[loopBufferIndex].loop;
}

void addRuvmLoopAndOrVert(int32_t loopBufIndex, AddClippedFaceVars *pAcfVars,
                          MeshData *pLocalMesh, LoopBuffer *pLoopBufEntry,
						  MapToMeshVars *pMmVars) {
	if (pAcfVars->firstRuvmVert < 0) {
		pAcfVars->firstRuvmVert = pLoopBufEntry[loopBufIndex].sort;
	}
	pAcfVars->lastRuvmVert = pLoopBufEntry[loopBufIndex].sort;
	uint32_t uVertIndex = pAcfVars->vertIndex;
	int32_t hash = fnvHash((uint8_t *)&uVertIndex, 4, pMmVars->vertAdjSize);
	VertAdj *pVertAdj = pMmVars->pRuvmVertAdj + hash;
	do {
		if (!pVertAdj->loopSize) {
			initVertAdjEntry(loopBufIndex, &pAcfVars->vertIndex, pLocalMesh,
			                 pLoopBufEntry, pVertAdj);
			break;
		}
		int32_t match = pVertAdj->ruvmVert == pAcfVars->vertIndex;
		if (match) {
			pAcfVars->vertIndex = pVertAdj->vert;
			break;
		}
		if (!pVertAdj->pNext) {
			pVertAdj = pVertAdj->pNext = calloc(1, sizeof(VertAdj));
			initVertAdjEntry(loopBufIndex, &pAcfVars->vertIndex, pLocalMesh,
			                 pLoopBufEntry, pVertAdj);
			break;
		}
		pVertAdj = pVertAdj->pNext;
	} while (1);
	pVertAdj->loopSize++;
	pLocalMesh->pUvs[pAcfVars->loopIndex] = pLoopBufEntry[loopBufIndex].uv;
}

void initBoundaryBufferEntry(ThreadArg *pArgs, AddClippedFaceVars *pAcfVars,
                             BoundaryVert *pEntry, int32_t ruvmFaceIndex,
                             int32_t tile, LoopBufferWrap *pLoopBuf) {
	pEntry->face = pArgs->localMesh.boundaryFaceSize;
	pEntry->firstVert = pAcfVars->firstRuvmVert;
	pEntry->lastVert = pAcfVars->lastRuvmVert;
	pEntry->faceIndex = ruvmFaceIndex;
	pEntry->tile = tile;
	pEntry->job = pArgs->id;
	pEntry->type = pAcfVars->ruvmLoops;
	if (pAcfVars->firstRuvmVert < 0) {
		int32_t *pNonRuvmSort = (int32_t *)(pEntry + 1);
		for (int32_t i = 0; i < pLoopBuf->size; ++i) {
			pNonRuvmSort[i] = pLoopBuf->buf[i].sort;
		}
	}
}


void addEdgeFaceToBoundaryBuffer(ThreadArg *pArgs, AddClippedFaceVars *pAcfVars,
                                 LoopBufferWrap *pLoopBuf, int32_t ruvmFaceIndex,
								 int32_t tile) {
	pArgs->localMesh.pFaces[pArgs->localMesh.boundaryFaceSize] = pAcfVars->boundaryLoopStart;
	int32_t hash = fnvHash((uint8_t *)&ruvmFaceIndex, 4, pArgs->boundaryBufferSize);
	BoundaryDir *pEntryDir = pArgs->pBoundaryBuffer + hash;
	BoundaryVert *pEntry = pEntryDir->pEntry;
	int32_t sizeToAllocate = sizeof(BoundaryVert);
	if (pAcfVars->firstRuvmVert < 0) {
		sizeToAllocate += sizeof(int32_t) * pLoopBuf->size;
	}
	if (!pEntry) {
		pEntry = pEntryDir->pEntry = calloc(1, sizeToAllocate);
		initBoundaryBufferEntry(pArgs, pAcfVars, pEntry, ruvmFaceIndex,
		                        tile, pLoopBuf);
		pArgs->totalFaces++;
	}
	else {
		do {
			if (pEntry->faceIndex == ruvmFaceIndex) {
				while (pEntry->pNext) {
					pEntry = pEntry->pNext;
				}
				pEntry = pEntry->pNext = calloc(1, sizeToAllocate);
				initBoundaryBufferEntry(pArgs, pAcfVars, pEntry, ruvmFaceIndex,
				                        tile, pLoopBuf);
				break;
			}
			if (!pEntryDir->pNext) {
				pEntryDir = pEntryDir->pNext = calloc(1, sizeof(BoundaryDir));
				pEntry = pEntryDir->pEntry = calloc(1, sizeToAllocate);
				initBoundaryBufferEntry(pArgs, pAcfVars, pEntry, ruvmFaceIndex,
				                        tile, pLoopBuf);
				pArgs->totalFaces++;
				break;
			}
			pEntryDir = pEntryDir->pNext;
			pEntry = pEntryDir->pEntry;
		} while (1);
	}
	pArgs->localMesh.boundaryFaceSize--;
}

void addClippedFaceToLocalMesh(ThreadArg *pArgs, MapToMeshVars *pMmVars,
                               LoopBufferWrap *pLoopBuf, int32_t edgeFace,
							   FaceInfo ruvmFace, int32_t tile) {
	if (pLoopBuf->size <= 2) {
		return;
	}
	AddClippedFaceVars acfVars;
	acfVars.loopStart = pArgs->localMesh.loopSize;
	acfVars.boundaryLoopStart = pArgs->localMesh.boundaryLoopSize;
	acfVars.firstRuvmVert = -1;
	acfVars.lastRuvmVert = -1;
	acfVars.ruvmLoops = 0;
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		acfVars.vertIndex = pLoopBuf->buf[i].index;
		pArgs->totalLoops++;
		acfVars.loopIndex = edgeFace ?
			pArgs->localMesh.boundaryLoopSize-- : pArgs->localMesh.loopSize++;
		if (acfVars.vertIndex < 0) {
			addNewLoopAndOrVert(i, &acfVars.vertIndex, &pArgs->localMesh,
			                    pLoopBuf->buf);
		}
		else {
			acfVars.ruvmLoops++;
			addRuvmLoopAndOrVert(i, &acfVars, &pArgs->localMesh, pLoopBuf->buf,
			                     pMmVars);
		}
		pArgs->localMesh.pLoops[acfVars.loopIndex] = acfVars.vertIndex;
	}
	if (edgeFace) {
		addEdgeFaceToBoundaryBuffer(pArgs, &acfVars, pLoopBuf,
		                            ruvmFace.index, tile);
	}
	else {
		pArgs->localMesh.pFaces[pArgs->localMesh.faceSize] = acfVars.loopStart;
		pArgs->localMesh.faceSize++;
	}
}

void mapToSingleFace(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                     MapToMeshVars *pMmVars, DebugAndPerfVars *pDpVars,
					 Vec2 fTileMin, int32_t tile, FaceInfo baseFace) {
	struct timeval start, stop;
	FaceBounds bounds;
	getFaceBounds(&bounds, pArgs->mesh.pUvs, baseFace);
	BaseTriVerts baseTri;
	baseTri.uv[0] = _(pArgs->mesh.pUvs[baseFace.start] V2SUB fTileMin);
	baseTri.uv[1] = _(pArgs->mesh.pUvs[baseFace.start + 1] V2SUB fTileMin);
	baseTri.uv[2] = _(pArgs->mesh.pUvs[baseFace.start + 2] V2SUB fTileMin);
	baseTri.xyz[0] = pArgs->mesh.pVerts[pArgs->mesh.pLoops[baseFace.start]];
	baseTri.xyz[1] = pArgs->mesh.pVerts[pArgs->mesh.pLoops[baseFace.start + 1]];
	baseTri.xyz[2] = pArgs->mesh.pVerts[pArgs->mesh.pLoops[baseFace.start + 2]];
	baseTri.pNormals = pArgs->mesh.pNormals + baseFace.start;
	for (int32_t i = 0; i < pEcVars->pFaceCellsInfo[baseFace.index].faceSize; ++i) {
		////CLOCK_START;
		FaceInfo ruvmFace;
		ruvmFace.index = pEcVars->pCellFaces[i];
		ruvmFace.start = pFileLoaded->mesh.pFaces[ruvmFace.index];
		ruvmFace.end = pFileLoaded->mesh.pFaces[ruvmFace.index + 1];
		ruvmFace.size = ruvmFace.end - ruvmFace.start;
		////CLOCK_START;
		pArgs->averageRuvmFacesPerFace++;
		if (!checkFaceIsInBounds(bounds.fMin, bounds.fMax, ruvmFace, &pFileLoaded->mesh)) {
			continue;
		}
		////CLOCK_STOP_NO_PRINT;
		//pDpVars->timeSpent[1] += getTimeDiff(&start, &stop);
		LoopBufferWrap loopBuf = {0};
		loopBuf.size = ruvmFace.size;
		for (int32_t j = 0; j < ruvmFace.size; ++j) {
			int32_t vertIndex = pFileLoaded->mesh.pLoops[ruvmFace.start + j];
			loopBuf.buf[j].index = vertIndex;
			loopBuf.buf[j].loop = pFileLoaded->mesh.pVerts[vertIndex];
			loopBuf.buf[j].loop.x += fTileMin.x;
			loopBuf.buf[j].loop.y += fTileMin.y;
			loopBuf.buf[j].sort = j;
		}
		////CLOCK_STOP_NO_PRINT;
		//pDpVars->timeSpent[0] += getTimeDiff(&start, &stop);
		int32_t edgeFace = 0;
		clipRuvmFaceAgainstBaseFace(pArgs, baseFace, &loopBuf, &edgeFace);
		transformClippedFaceFromUvToXyz(&loopBuf, baseTri, fTileMin);
		////CLOCK_START;
		addClippedFaceToLocalMesh(pArgs, pMmVars, &loopBuf, edgeFace,
		                          ruvmFace, tile);
		////CLOCK_STOP_NO_PRINT;
		//pDpVars->timeSpent[2] += getTimeDiff(&start, &stop);
	}
	debugFaceIndex++;
	//printf("Total vert adj: %d %d %d - depth: %d %d\n", totalEmpty, totalComputed, vertAdjSize, maxDepth, *averageDepth);
	////CLOCK_START;
	//memset(ruvmVertAdj, 0, sizeof(VertAdj) * pFileLoaded->header.vertSize);
	////CLOCK_STOP_NO_PRINT;
	//timeSpent[2] += getTimeDiff(&start, &stop);
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

int32_t checkBranchCellIsLinked(EnclosingCellsInfo *pCellsBuffer, int32_t index) {
	Cell *cell = pCellsBuffer->cells[index];
	for (int32_t j = 0; j < pCellsBuffer->cellSize; ++j) {
		if (pCellsBuffer->cellType[j] || index == j) {
			continue;
		}
		Cell *leaf = pCellsBuffer->cells[j];
		for (int32_t k = 0; k < leaf->linkEdgeSize; ++k) {
			if (cell->cellIndex == leaf->pLinkEdges[k]) {
				return 1;
			}
		}
	}
	return 0;
}

void removeNonLinkedBranchCells(EnclosingCellsInfo *pCellsBuffer) {
	for (int32_t i = 0; i < pCellsBuffer->cellSize;) {
		if (!pCellsBuffer->cellType[i]) {
			i++;
			continue;
		}
		if (checkBranchCellIsLinked(pCellsBuffer, i)) {
			i++;
			continue;
		}
		Cell *pCell = pCellsBuffer->cells[i];
		pCellsBuffer->faceTotal -= pCell->edgeFaceSize;
		pCellsBuffer->faceTotalNoDup -= pCell->edgeFaceSize;
		for (int32_t j = i; j < pCellsBuffer->cellSize - 1; ++j) {
			pCellsBuffer->cells[j] = pCellsBuffer->cells[j + 1];
			pCellsBuffer->cellType[j] = pCellsBuffer->cellType[j + 1];
		}
		pCellsBuffer->cellSize--;
	}
}

void copyCellsIntoTotalList(EnclosingCellsVars *pEcVars, EnclosingCellsInfo *pCellsBuffer,
                            int32_t faceIndex) {
	FaceCellsInfo *pEntry = pEcVars->pFaceCellsInfo + faceIndex;
	pEcVars->cellFacesTotal += pCellsBuffer->faceTotalNoDup;
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

int32_t getCellsForSingleFace(ThreadArg *pArgs, EnclosingCellsVars *pEcVars, int32_t faceIndex) {
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
	removeNonLinkedBranchCells(&cellsBuffer);
	copyCellsIntoTotalList(pEcVars, &cellsBuffer, faceIndex);
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
		pFaceBounds->fMinSmall = pFaceBounds->fMin;
		pFaceBounds->fMaxSmall = pFaceBounds->fMax;
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

void allocateStructuresForMapping(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                                  MapToMeshVars *pMmVars) {
	struct timeval start, stop;
	//pArgs->boundaryFace = malloc(sizeof(int32_t) * pArgs->mesh.faceSize + 1);
	int32_t loopBufferSize = pArgs->bufferSize * 2;
	pArgs->loopBufferSize = loopBufferSize;
	pArgs->pBoundaryBuffer = calloc(pArgs->boundaryBufferSize, sizeof(BoundaryDir));
	pArgs->localMesh.boundaryVertSize = pArgs->bufferSize - 1;
	pArgs->localMesh.boundaryLoopSize = loopBufferSize - 1;
	pArgs->localMesh.boundaryFaceSize = pArgs->bufferSize - 1;
	pArgs->localMesh.pFaces = malloc(sizeof(int32_t) * pArgs->bufferSize);
	pArgs->localMesh.pLoops = malloc(sizeof(int32_t) * loopBufferSize);
	pArgs->localMesh.pVerts = malloc(sizeof(Vec3) * pArgs->bufferSize);
	pArgs->localMesh.pUvs = malloc(sizeof(Vec2) * loopBufferSize);
	pEcVars->pCellFaces = malloc(sizeof(int32_t) * pEcVars->cellFacesMax);
	pMmVars->vertAdjSize = pEcVars->cellFacesTotal / 150;
	printf("VertAdjBufSize: %d\n", pMmVars->vertAdjSize);
	//pMmVars->vertAdjSize = 4000;
	pMmVars->pRuvmVertAdj = calloc(pMmVars->vertAdjSize, sizeof(VertAdj));
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
	//CLOCK_START;
	EnclosingCellsVars ecVars = {0};
	SendOffArgs *pSend = pArgsPtr;
	ThreadArg args = {0};
	args.id = pSend->id;
	args.boundaryBufferSize = pSend->boundaryBufferSize;
	args.mesh = pSend->mesh;
	ecVars.pFaceCellsInfo = malloc(sizeof(FaceCellsInfo) * args.mesh.faceSize);
	getEnclosingCellsForAllFaces(&args, &ecVars);
	//CLOCK_STOP("getting enclosing cells");
	//CLOCK_START;
	MapToMeshVars mmVars = {0};
	args.bufferSize = args.mesh.faceSize + ecVars.cellFacesTotal;
	allocateStructuresForMapping(&args, &ecVars, &mmVars);
	DebugAndPerfVars dpVars = {0};
	//CLOCK_STOP("allocate structures for mapping");
	uint64_t mappingTime, copySingleTime;
	copySingleTime = 0;
	mappingTime = 0;
	for (int32_t i = 0; i < args.mesh.faceSize; ++i) {
		//CLOCK_START;
		// copy faces over to a new contiguous array
		copyCellFacesIntoSingleArray(ecVars.pFaceCellsInfo, ecVars.pCellFaces, i);
		//iterate through tiles
		//CLOCK_STOP_NO_PRINT;
		copySingleTime += getTimeDiff(&start, &stop);
		//CLOCK_START;
		FaceBounds *pFaceBounds = &ecVars.faceBounds;
		for (int32_t j = pFaceBounds->min.y; j <= pFaceBounds->max.y; ++j) {
			for (int32_t k = pFaceBounds->min.x; k <= pFaceBounds->max.x; ++k) {
				Vec2 fTileMin = {k, j};
				int32_t tile = k + (j * ecVars.faceBounds.max.x);
				FaceInfo baseFace;
				baseFace.start = args.mesh.pFaces[i];
				baseFace.end = args.mesh.pFaces[i + 1];
				baseFace.size = baseFace.end - baseFace.start;
				baseFace.index = i;
				mapToSingleFace(&args, &ecVars, &mmVars, &dpVars, fTileMin, tile,
				                baseFace);
			}
		}
		//CLOCK_STOP_NO_PRINT;
		mappingTime += getTimeDiff(&start, &stop);
		free(ecVars.pFaceCellsInfo[i].pCells);
	}
	//printf("copy faces into single array %lu\n", copySingleTime);
	//printf("maping %lu\n", mappingTime);
	//CLOCK_START;
	args.averageRuvmFacesPerFace /= args.mesh.faceSize;
	//printf("#######Boundary Buffer Size: %d\n", pArgs->localMesh.boundaryFaceSize);
	args.localMesh.pFaces[args.localMesh.boundaryFaceSize] = 
		args.localMesh.boundaryLoopSize;
	args.totalBoundaryFaces = args.totalFaces;
	args.totalFaces += args.localMesh.faceSize;
	args.totalLoops += args.localMesh.loopSize;
	//pArgs->totalFaces = pArgs->localMesh.faceSize +
	//	(bufferSize - pArgs->localMesh.boundaryFaceSize);
	//pArgs->totalLoops = pArgs->localMesh.loopSize +
	//	(loopBufferSize - pArgs->localMesh.boundaryLoopSize);
	args.totalVerts = args.localMesh.vertSize +
		(args.bufferSize - args.localMesh.boundaryVertSize);
	//printf("MaxDepth: %d\n", dpVars.maxDepth);
	////CLOCK_STOP("projecting");
	//printf("  ^  project: %lu, move & transform: %lu, memset vert adj: %lu\n",
	//		dpVars.timeSpent[0], dpVars.timeSpent[1], dpVars.timeSpent[2]);
	//processBoundaryBuffer(pArgs, bufferSize, boundaryBufferSize);
	free(mmVars.pRuvmVertAdj);
	free(ecVars.pCellFaces);
	free(ecVars.pFaceCellsInfo);
	//CLOCK_STOP("post mapping stuff");
	//CLOCK_START;
	pSend->pBoundaryBuffer = args.pBoundaryBuffer;
	pSend->averageVertAdjDepth = args.averageVertAdjDepth;
	pSend->averageRuvmFacesPerFace = args.averageRuvmFacesPerFace;
	pSend->localMesh = args.localMesh;
	pSend->vertBase = args.vertBase;
	pSend->totalBoundaryFaces = args.totalBoundaryFaces;
	pSend->totalVerts = args.totalVerts;
	pSend->totalLoops = args.totalLoops;
	pSend->totalFaces = args.totalFaces;
	mutexLock();
	++*pSend->pJobsCompleted;
	mutexUnlock();
	//CLOCK_STOP("setting jobs completed");
}

void sendOffJobs(SendOffArgs *pJobArgs, int32_t *pJobsCompleted, MeshData *pMesh) {
	struct timeval start, stop;
	//CLOCK_START;
	int32_t facesPerThread = pMesh->faceSize / threadAmount;
	int32_t threadAmountMinus1 = threadAmount - 1;
	void *jobArgPtrs[MAX_THREADS];
	int32_t boundaryBufferSize = pFileLoaded->mesh.faceSize / 5;
	printf("fromjobsendoff: BoundaryBufferSize: %d\n", boundaryBufferSize);
	for (int32_t i = 0; i < threadAmount; ++i) {
		int32_t meshStart = facesPerThread * i;
		int32_t meshEnd = i == threadAmountMinus1 ?
			pMesh->faceSize : meshStart + facesPerThread;
		MeshData meshPart = *pMesh;
		meshPart.pFaces += meshStart;
		meshPart.faceSize = meshEnd - meshStart;
		pJobArgs[i].boundaryBufferSize = boundaryBufferSize;
		pJobArgs[i].averageVertAdjDepth = 0;
		pJobArgs[i].mesh = meshPart;
		pJobArgs[i].pJobsCompleted = pJobsCompleted;
		pJobArgs[i].id = i;
		jobArgPtrs[i] = pJobArgs + i;
	}
	pushJobs(threadAmount, mapToMeshJob, jobArgPtrs);
	//CLOCK_STOP("send off jobs");
}

void allocateWorkMesh(MeshData *pWorkMesh, SendOffArgs *pJobArgs) {
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

void copyMesh(int32_t jobIndex, MeshData *pWorkMesh, SendOffArgs *pJobArgs) {
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

void addFaceLoopsAndVertsToBuffer(SendOffArgs *pJobArgs, MeshData *pWorkMesh, BoundaryVert *pEntry,
                                  int32_t seamFace, int32_t *loopBufferSize, int32_t *pLoopBuffer,
								  Vec2 *pUvBuffer, FaceInfo *ruvmFaceInfo, int32_t edgeTableSize,
								  EdgeTable *pEdgeTable, int32_t *pRuvmIndicesSort) {
	int32_t ruvmLastLoop = ruvmFaceInfo->size - 1;
	int32_t hashBuffer[32];
	int32_t hashBufferSize = 0;
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

void mergeAndCopyEdgeFaces(MeshData *pWorkMesh, SendOffArgs *pJobArgs,
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
									 uvBuffer, &ruvmFaceInfo, edgeTableSize,
									 pEdgeTable, ruvmIndicesSort + 1);
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

void combineJobMeshesIntoSingleMesh(MeshData *pWorkMesh, SendOffArgs *pJobArgs) {
	struct timeval start, stop;
	//CLOCK_START;
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
	//CLOCK_STOP("moving to work mesh");
}

DECL_SPEC_EXPORT void ruvmMapToMesh(MeshData *pMesh, MeshData *pWorkMesh, float *pNormals) {
	struct timeval start, stop;
	CLOCK_START;
	pMesh->pNormals = (Vec3 *)pNormals;
	
	SendOffArgs jobArgs[MAX_THREADS] = {0};
	int32_t jobsCompleted = 0;
	sendOffJobs(jobArgs, &jobsCompleted, pMesh);

	executeJobIfPresent();
	//mapToMeshJob((void *)jobArgs);
	int32_t waiting;
	struct timespec remaining, request = {0, 25};
	do  {
		nanosleep(&request, &remaining);
		mutexLock();
		waiting = jobsCompleted < threadAmount;
		mutexUnlock();
	} while(waiting);

	int64_t averageRuvmFacesPerFace = 0;
	for(int32_t i = 0; i < threadAmount; ++i) {
		averageRuvmFacesPerFace += jobArgs[i].averageRuvmFacesPerFace;
	}
	averageRuvmFacesPerFace /= threadAmount;
	printf("---- averageRuvmFacesPerFace: %lu ----\n", averageRuvmFacesPerFace);

	combineJobMeshesIntoSingleMesh(pWorkMesh, jobArgs);
	CLOCK_STOP("Whole Total");
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
