#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <Context.h>
#include <MapToMesh.h>
#include <MapFile.h>

static void clipRuvmFaceAgainstSingleLoop(LoopBufferWrap *pLoopBuf, LoopBufferWrap *pNewLoopBuf,
                                          int32_t *pInsideBuf, LoopInfo *pBaseLoop, Vec2 baseLoopCross,
								          int32_t *pEdgeFace) {
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
			LoopBuffer *pNewEntry = pNewLoopBuf->buf + pNewLoopBuf->size;
			if (pLoopBuf->buf[i].index < 0 && pLoopBuf->buf[vertNextIndex].index < 0) {
				int32_t whichVert = pLoopBuf->buf[i].baseLoop == pBaseLoop->index - 1;
				*(Vec2 *)&pNewEntry->loop = whichVert ?
					pBaseLoop->vert : pBaseLoop->vertNext;
				pNewEntry->isBaseLoop = whichVert ?
					pBaseLoop->localIndex : pBaseLoop->localIndexNext;
				pNewEntry->isBaseLoop += 1;
				pNewEntry->isBaseLoop *= -1;
			}
			else {
				Vec3 *pRuvmVert = &pLoopBuf->buf[i].loop;
				Vec3 *pRuvmVertNext = &pLoopBuf->buf[vertNextIndex].loop;
				Vec3 ruvmDir = _(*pRuvmVert V3SUB *pRuvmVertNext);
				Vec3 ruvmDirBack = _(*pRuvmVertNext V3SUB *pRuvmVert);
				float t = (pRuvmVert->x - pBaseLoop->vert.x) * pBaseLoop->dirBack.y;
				t -= (pRuvmVert->y - pBaseLoop->vert.y) * pBaseLoop->dirBack.x;
				t /= ruvmDir.x * pBaseLoop->dirBack.y - ruvmDir.y * pBaseLoop->dirBack.x;
				Vec3 intersection = _(*pRuvmVert V3ADD _(ruvmDirBack V3MULS t));
				pNewEntry->loop = intersection;
				pNewEntry->isBaseLoop = pBaseLoop->localIndex + 1;
			}
			pNewEntry->index = -1;
			pNewEntry->sort = pLoopBuf->buf[vertNextIndex].sort;
			pNewEntry->baseLoop = pBaseLoop->index;
			pNewLoopBuf->size++;
		}
	}
}

static void clipRuvmFaceAgainstBaseFace(ThreadArg *pArgs, FaceInfo baseFace,
                                 LoopBufferWrap *pLoopBuf, int32_t *pEdgeFace,
								 int32_t *pHasPreservedEdge) {
	for (int32_t i = 0; i < baseFace.size; ++i) {
		LoopInfo baseLoop;
		baseLoop.index = i;
		int32_t vertIndex = pArgs->mesh.pLoops[baseFace.start + i];
		int32_t edgeIndex = pArgs->mesh.pEdges[baseFace.start + i];
		int8_t preserveEdge[2];
		preserveEdge[0] = pArgs->mesh.pEdgePreserve[edgeIndex];
		baseLoop.vert = pArgs->mesh.pUvs[i + baseFace.start];
		baseLoop.vert = pArgs->mesh.pUvs[i + baseFace.start];
		int32_t uvNextIndexLocal = ((i + 1) % baseFace.size);
		int32_t uvNextIndex = uvNextIndexLocal + baseFace.start;
		edgeIndex = pArgs->mesh.pEdges[uvNextIndex];
		preserveEdge[1] = pArgs->mesh.pEdgePreserve[edgeIndex];
		if (preserveEdge[0]) {
			pArgs->pInVertTable[vertIndex] = 1;
		}
		if (preserveEdge[1]) {
			int32_t nextVertIndex = pArgs->mesh.pLoops[uvNextIndex];
			pArgs->pInVertTable[nextVertIndex] = 1;
		}
		baseLoop.vertNext = pArgs->mesh.pUvs[uvNextIndex];
		baseLoop.indexNext = uvNextIndex;
		baseLoop.localIndex = i;
		baseLoop.localIndexNext = uvNextIndexLocal;
		baseLoop.dir = _(baseLoop.vertNext V2SUB baseLoop.vert);
		baseLoop.dirBack = _(baseLoop.vert V2SUB baseLoop.vertNext);
		LoopBufferWrap newLoopBuf = {0};
		int32_t insideBuf[12] = {0};
		Vec2 baseLoopCross = vec2Cross(baseLoop.dir);
		int32_t edgeFacePre = *pEdgeFace;
		clipRuvmFaceAgainstSingleLoop(pLoopBuf, &newLoopBuf, insideBuf,
		         				      &baseLoop, baseLoopCross, pEdgeFace);
		int32_t intersects = edgeFacePre != *pEdgeFace;
		if (intersects && preserveEdge[0]) {
			*pHasPreservedEdge = 1;
		}

		if (newLoopBuf.size <= 2) {
			pLoopBuf->size = newLoopBuf.size;
			return;
		}
		memcpy(pLoopBuf->buf, newLoopBuf.buf, sizeof(LoopBuffer) * newLoopBuf.size);
		pLoopBuf->size = newLoopBuf.size;
	}
}

static void transformClippedFaceFromUvToXyz(LoopBufferWrap *pLoopBuf, BaseTriVerts baseTri,
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


static void addNewLoopAndOrVert(int32_t loopBufIndex, int32_t *pVertIndex,
                         WorkMesh *pLocalMesh, LoopBuffer *pLoopBuffer, int32_t loopIndex) {
		*pVertIndex = pLocalMesh->boundaryVertSize;
		pLocalMesh->pVerts[*pVertIndex] = pLoopBuffer[loopBufIndex].loop;
		pLocalMesh->boundaryVertSize--;
		pLocalMesh->pUvs[loopIndex] = pLoopBuffer[loopBufIndex].uv;
}

static void initVertAdjEntry(int32_t loopBufferIndex, int32_t *pVertIndex, WorkMesh *pLocalMesh,
                      LoopBuffer *pLoopBuffer, VertAdj *pVertAdj) {
	pVertAdj->ruvmVert = *pVertIndex;
	*pVertIndex = pLocalMesh->vertCount++;
	pVertAdj->vert = *pVertIndex;
	pLocalMesh->pVerts[*pVertIndex] = pLoopBuffer[loopBufferIndex].loop;
}

static void addRuvmLoopAndOrVert(int32_t loopBufIndex, AddClippedFaceVars *pAcfVars,
                          WorkMesh *pLocalMesh, LoopBuffer *pLoopBufEntry,
						  MapToMeshVars *pMmVars, RuvmAllocator *pAlloc) {
	if (pAcfVars->firstRuvmVert < 0) {
		pAcfVars->firstRuvmVert = pLoopBufEntry[loopBufIndex].sort;
	}
	pAcfVars->lastRuvmVert = pLoopBufEntry[loopBufIndex].sort;
	uint32_t uVertIndex = pAcfVars->vertIndex;
	int32_t hash = ruvmFnvHash((uint8_t *)&uVertIndex, 4, pMmVars->vertAdjSize);
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
			pVertAdj = pVertAdj->pNext = pAlloc->pCalloc(1, sizeof(VertAdj));
			initVertAdjEntry(loopBufIndex, &pAcfVars->vertIndex, pLocalMesh,
			                 pLoopBufEntry, pVertAdj);
			break;
		}
		pVertAdj = pVertAdj->pNext;
	} while (1);
	pVertAdj->loopSize++;
	pLocalMesh->pUvs[pAcfVars->loopIndex] = pLoopBufEntry[loopBufIndex].uv;
}

static void initBoundaryBufferEntry(ThreadArg *pArgs, AddClippedFaceVars *pAcfVars,
                             BoundaryVert *pEntry, int32_t ruvmFaceIndex,
                             int32_t tile, LoopBufferWrap *pLoopBuf, FaceInfo baseFace,
							 int32_t hasPreservedEdge) {
	pEntry->face = pArgs->localMesh.boundaryFaceSize;
	pEntry->firstVert = pAcfVars->firstRuvmVert;
	pEntry->lastVert = pAcfVars->lastRuvmVert;
	pEntry->faceIndex = ruvmFaceIndex;
	pEntry->tile = tile;
	pEntry->job = pArgs->id;
	pEntry->type = pAcfVars->ruvmLoops;
	pEntry->baseLoop = baseFace.start;
	pEntry->hasPreservedEdge = hasPreservedEdge;
	if (pLoopBuf->size > 8) {
		printf("----------------------   Loopbuf size exceeded 8\n");
		abort();
	}
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		pEntry->baseLoops[i] = pLoopBuf->buf[i].sort;
		pEntry->baseLoops[i] |= pLoopBuf->buf[i].isBaseLoop << 4;
	}
	if (pLoopBuf->size > pArgs->maxLoopSize) {
		pArgs->maxLoopSize = pLoopBuf->size;
	}
	if (pAcfVars->firstRuvmVert < 0) {
		int32_t *pNonRuvmSort = (int32_t *)(pEntry + 1);
		for (int32_t i = 0; i < pLoopBuf->size; ++i) {
			pNonRuvmSort[i] = pLoopBuf->buf[i].sort;
		}
	}
}


static void addEdgeFaceToBoundaryBuffer(ThreadArg *pArgs, AddClippedFaceVars *pAcfVars,
                                 LoopBufferWrap *pLoopBuf, int32_t ruvmFaceIndex,
								 int32_t tile, FaceInfo baseFace, int32_t hasPreservedEdge) {
	pArgs->localMesh.pFaces[pArgs->localMesh.boundaryFaceSize] = pAcfVars->boundaryLoopStart;
	int32_t hash = ruvmFnvHash((uint8_t *)&ruvmFaceIndex, 4, pArgs->boundaryBufferSize);
	BoundaryDir *pEntryDir = pArgs->pBoundaryBuffer + hash;
	BoundaryVert *pEntry = pEntryDir->pEntry;
	int32_t sizeToAllocate = sizeof(BoundaryVert);
	if (pAcfVars->firstRuvmVert < 0) {
		sizeToAllocate += sizeof(int32_t) * pLoopBuf->size;
	}
	if (!pEntry) {
		pEntry = pEntryDir->pEntry = pArgs->alloc.pCalloc(1, sizeToAllocate);
		initBoundaryBufferEntry(pArgs, pAcfVars, pEntry, ruvmFaceIndex,
		                        tile, pLoopBuf, baseFace, hasPreservedEdge);
		pArgs->totalFaces++;
	}
	else {
		do {
			if (pEntry->faceIndex == ruvmFaceIndex) {
				while (pEntry->pNext) {
					pEntry = pEntry->pNext;
				}
				pEntry = pEntry->pNext = pArgs->alloc.pCalloc(1, sizeToAllocate);
				initBoundaryBufferEntry(pArgs, pAcfVars, pEntry, ruvmFaceIndex,
				                        tile, pLoopBuf, baseFace, hasPreservedEdge);
				break;
			}
			if (!pEntryDir->pNext) {
				pEntryDir = pEntryDir->pNext = pArgs->alloc.pCalloc(1, sizeof(BoundaryDir));
				pEntry = pEntryDir->pEntry = pArgs->alloc.pCalloc(1, sizeToAllocate);
				initBoundaryBufferEntry(pArgs, pAcfVars, pEntry, ruvmFaceIndex,
				                        tile, pLoopBuf, baseFace, hasPreservedEdge);
				pArgs->totalFaces++;
				break;
			}
			pEntryDir = pEntryDir->pNext;
			pEntry = pEntryDir->pEntry;
		} while (1);
	}
	pArgs->localMesh.boundaryFaceSize--;
}

static void addClippedFaceToLocalMesh(ThreadArg *pArgs, MapToMeshVars *pMmVars,
                               LoopBufferWrap *pLoopBuf, int32_t edgeFace,
							   FaceInfo ruvmFace, int32_t tile, FaceInfo baseFace,
                               int32_t hasPreservedEdge) {
	if (pLoopBuf->size <= 2) {
		return;
	}
	AddClippedFaceVars acfVars;
	acfVars.loopStart = pArgs->localMesh.loopCount;
	acfVars.boundaryLoopStart = pArgs->localMesh.boundaryLoopSize;
	acfVars.firstRuvmVert = -1;
	acfVars.lastRuvmVert = -1;
	acfVars.ruvmLoops = 0;
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		acfVars.vertIndex = pLoopBuf->buf[i].index;
		pArgs->totalLoops++;
		acfVars.loopIndex = edgeFace ?
			pArgs->localMesh.boundaryLoopSize-- : pArgs->localMesh.loopCount++;
		if (acfVars.vertIndex < 0) {
			addNewLoopAndOrVert(i, &acfVars.vertIndex, &pArgs->localMesh,
			                    pLoopBuf->buf, acfVars.loopIndex);
		}
		else {
			acfVars.ruvmLoops++;
			addRuvmLoopAndOrVert(i, &acfVars, &pArgs->localMesh, pLoopBuf->buf,
			                     pMmVars, &pArgs->alloc);
		}
		pArgs->localMesh.pLoops[acfVars.loopIndex] = acfVars.vertIndex;
	}
	if (edgeFace) {
		addEdgeFaceToBoundaryBuffer(pArgs, &acfVars, pLoopBuf,
		                            ruvmFace.index, tile, baseFace,
									hasPreservedEdge);
	}
	else {
		pArgs->localMesh.pFaces[pArgs->localMesh.faceCount] = acfVars.loopStart;
		pArgs->localMesh.faceCount++;
	}
}

void ruvmMapToSingleFace(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                     MapToMeshVars *pMmVars, DebugAndPerfVars *pDpVars,
					 Vec2 fTileMin, int32_t tile, FaceInfo baseFace) {
	//struct timeval start, stop;
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
		ruvmFace.start = pArgs->pMap->mesh.pFaces[ruvmFace.index];
		ruvmFace.end = pArgs->pMap->mesh.pFaces[ruvmFace.index + 1];
		ruvmFace.size = ruvmFace.end - ruvmFace.start;
		////CLOCK_START;
		pArgs->averageRuvmFacesPerFace++;
		if (!checkFaceIsInBounds(bounds.fMin, bounds.fMax, ruvmFace, &pArgs->pMap->mesh)) {
			continue;
		}
		////CLOCK_STOP_NO_PRINT;
		//pDpVars->timeSpent[1] += getTimeDiff(&start, &stop);
		LoopBufferWrap loopBuf = {0};
		loopBuf.size = ruvmFace.size;
		for (int32_t j = 0; j < ruvmFace.size; ++j) {
			int32_t vertIndex = pArgs->pMap->mesh.pLoops[ruvmFace.start + j];
			loopBuf.buf[j].index = vertIndex;
			loopBuf.buf[j].loop = pArgs->pMap->mesh.pVerts[vertIndex];
			loopBuf.buf[j].loop.x += fTileMin.x;
			loopBuf.buf[j].loop.y += fTileMin.y;
			loopBuf.buf[j].sort = j;
		}
		////CLOCK_STOP_NO_PRINT;
		//pDpVars->timeSpent[0] += getTimeDiff(&start, &stop);
		int32_t edgeFace = 0;
		int32_t hasPreservedEdge = 0;
		clipRuvmFaceAgainstBaseFace(pArgs, baseFace, &loopBuf, &edgeFace,
		                            &hasPreservedEdge);
		transformClippedFaceFromUvToXyz(&loopBuf, baseTri, fTileMin);
		////CLOCK_START;
		addClippedFaceToLocalMesh(pArgs, pMmVars, &loopBuf, edgeFace,
		                          ruvmFace, tile, baseFace, hasPreservedEdge);
		////CLOCK_STOP_NO_PRINT;
		//pDpVars->timeSpent[2] += getTimeDiff(&start, &stop);
	}
	//debugFaceIndex++;
	//printf("Total vert adj: %d %d %d - depth: %d %d\n", totalEmpty, totalComputed, vertAdjSize, maxDepth, *averageDepth);
	////CLOCK_START;
	//memset(ruvmVertAdj, 0, sizeof(VertAdj) * pMap->header.vertSize);
	////CLOCK_STOP_NO_PRINT;
	//timeSpent[2] += getTimeDiff(&start, &stop);
}
