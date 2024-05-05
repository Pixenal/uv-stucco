#include <string.h>

#include <RUVM.h>
#include <MapToJobMesh.h>
#include <MapFile.h>
#include <Context.h>
#include <Clock.h>
#include <AttribUtils.h>
#include <Utils.h>

static
void allocateLocalMesh(MappingJobVars *pArgs, BufMesh *pLocalMesh, RuvmMesh *pMeshIn,
                       int32_t bufferSize, int32_t loopBufferSize) {
	RuvmAllocator alloc = pArgs->alloc;
	pLocalMesh->mesh.pFaces = alloc.pMalloc(sizeof(int32_t) * bufferSize);
	allocAttribs(alloc, &pLocalMesh->mesh.faceAttribs, &pMeshIn->faceAttribs,
	             &pArgs->pMap->mesh.mesh.faceAttribs, bufferSize);
	pLocalMesh->mesh.pLoops = alloc.pMalloc(sizeof(int32_t) * loopBufferSize);
	allocAttribs(alloc, &pLocalMesh->mesh.loopAttribs, &pMeshIn->loopAttribs,
	             &pArgs->pMap->mesh.mesh.loopAttribs, loopBufferSize);
	pLocalMesh->mesh.pEdges = alloc.pMalloc(sizeof(int32_t) * loopBufferSize);
	allocAttribs(alloc, &pLocalMesh->mesh.edgeAttribs, &pMeshIn->edgeAttribs,
	             &pArgs->pMap->mesh.mesh.edgeAttribs, loopBufferSize);
	allocAttribs(alloc, &pLocalMesh->mesh.vertAttribs, &pMeshIn->vertAttribs,
	             &pArgs->pMap->mesh.mesh.vertAttribs, bufferSize);
	pArgs->bufMesh.pUvAttrib = getAttrib("UVMap", &pLocalMesh->mesh.loopAttribs);
	pArgs->bufMesh.pUvs = pArgs->bufMesh.pUvAttrib->pData;
	pArgs->bufMesh.pNormalAttrib = getAttrib("normal", &pLocalMesh->mesh.loopAttribs);
	pArgs->bufMesh.pNormals = pArgs->bufMesh.pNormalAttrib->pData;
	pArgs->bufMesh.pVertAttrib = getAttrib("position", &pLocalMesh->mesh.vertAttribs);
	pArgs->bufMesh.pVerts = pArgs->bufMesh.pVertAttrib->pData;

	pLocalMesh->boundaryVertSize = bufferSize - 1;
	pLocalMesh->boundaryLoopSize = loopBufferSize - 1;
	pLocalMesh->boundaryEdgeSize = loopBufferSize - 1;
	pLocalMesh->boundaryFaceSize = bufferSize - 1;
}

static
void allocStructuresForMapping(MappingJobVars *pArgs, EnclosingCells *pEcVars) {
	//struct timeval start, stop;
	RuvmAllocator *pAlloc = &pArgs->alloc;
	//pArgs->boundaryFace = pContext->alloc.pMalloc(sizeof(int32_t) * pArgs->mesh.faceCount + 1);
	int32_t loopBufferSize = pArgs->bufferSize * 2;
	pArgs->loopBufferSize = loopBufferSize;
	pArgs->boundsTable.pTable = pAlloc->pCalloc(pArgs->boundsTable.size, sizeof(BoundaryDir));
	allocateLocalMesh(pArgs, &pArgs->bufMesh, &pArgs->mesh.mesh,
	                  pArgs->bufferSize, loopBufferSize);
	pEcVars->pCellFaces = pAlloc->pMalloc(sizeof(int32_t) * pEcVars->cellFacesMax);
	//pArgs->pInVertTable = pAlloc->pCalloc(pArgs->mesh.vertCount, 1);
	//TODO: maybe reduce further if unifaces if low,
	//as a larger buffer seems more necessary at higher face counts.
	//Doesn't provie much speed up at lower resolutions.
	pArgs->mTables.vertAdjSize = pEcVars->uniqueFaces / 10;
	pArgs->mTables.edgeTableSize = pEcVars->uniqueFaces / 7;
	pArgs->mTables.pRuvmVertAdj =
		pAlloc->pCalloc(pArgs->mTables.vertAdjSize, sizeof(VertAdj));
	pArgs->mTables.pEdgeTable =
		pAlloc->pCalloc(pArgs->mTables.edgeTableSize, sizeof(MeshBufEdgeTable));
}

static
void linearizeCellFaces(FaceCellsInfo *pFaceCellsInfo, int32_t *pCellFaces,
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

static
Mat3x3 buildFaceTbn(FaceInfo face, Mesh *pMesh) {
	int32_t loop = face.start;
	int32_t vertIndex = pMesh->mesh.pLoops[loop];
	V2_F32 uv = pMesh->pUvs[loop];
	V3_F32 vert = pMesh->pVerts[vertIndex];
	int32_t vertIndexNext = pMesh->mesh.pLoops[face.start + 1];
	V2_F32 uvNext = pMesh->pUvs[face.start + 1];
	V3_F32 vertNext = pMesh->pVerts[vertIndexNext];
	int32_t vertIndexPrev = pMesh->mesh.pLoops[face.end - 1];
	V2_F32 uvPrev = pMesh->pUvs[face.end - 1];
	V3_F32 vertPrev = pMesh->pVerts[vertIndexPrev];
	//uv space direction vectors,
	//forming the coefficient matrix
	Mat2x2 coeffMat;
	*(V2_F32 *)&coeffMat.d[0] = _(uvNext V2SUB uv);
	*(V2_F32 *)&coeffMat.d[1] = _(uvPrev V2SUB uv);
	//object space direction vectors,
	//forming the variable matrix
	Mat2x3 varMat;
	V3_F32 osDirA = _(vertNext V3SUB vert);
    V3_F32 osDirB = _(vertPrev V3SUB vert);
	*(V3_F32 *)&varMat.d[0] = osDirA;
	*(V3_F32 *)&varMat.d[1] = osDirB;
	Mat2x2 coeffMatInv = mat2x2Invert(coeffMat);
	Mat2x3 tb = mat2x2MultiplyMat2x3(coeffMatInv, varMat);
	Mat3x3 tbn;
	*(V3_F32 *)&tbn.d[0] = v3Normalize(*(V3_F32 *)&tb.d[0]);
	*(V3_F32 *)&tbn.d[1] = v3Normalize(*(V3_F32 *)&tb.d[1]);
	V3_F32 normal = _(osDirA V3CROSS osDirB);
	*(V3_F32 *)&tbn.d[2] = v3Normalize(normal);
	return tbn;
}

static
void mapPerTile(MappingJobVars *pMVars, FaceInfo *pBaseFace,
                       EnclosingCells *pEcVars,
					   DebugAndPerfVars *pDpVars, int32_t rawFace) {
	FaceBounds *pFaceBounds = &pEcVars->pFaceCellsInfo[rawFace].faceBounds;
	for (int32_t j = pFaceBounds->min.d[1]; j <= pFaceBounds->max.d[1]; ++j) {
		for (int32_t k = pFaceBounds->min.d[0]; k <= pFaceBounds->max.d[0]; ++k) {
			V2_F32 fTileMin = {k, j};
			int32_t tile = k + (j * pFaceBounds->max.d[0]);
			ruvmMapToSingleFace(pMVars, pEcVars, pDpVars, fTileMin, tile,
								*pBaseFace);
		}
	}
}

static
void destroyMappingTables(RuvmAllocator *pAlloc, MappingTables *pMVars) {
	for (int32_t i = 0; i < pMVars->vertAdjSize; ++i) {
		VertAdj *pEntry = pMVars->pRuvmVertAdj + i;
		if (!pEntry->loopSize) {
			continue;
		}
		pEntry = pEntry->pNext;
		while (pEntry) {
			VertAdj *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		};
	}
	for (int32_t i = 0; i < pMVars->edgeTableSize; ++i) {
		MeshBufEdgeTable *pEntry = pMVars->pEdgeTable + i;
		if (!pEntry->loopCount) {
			continue;
		}
		pEntry = pEntry->pNext;
		while (pEntry) {
			MeshBufEdgeTable *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		};
	}
	pAlloc->pFree(pMVars->pRuvmVertAdj);
	pAlloc->pFree(pMVars->pEdgeTable);
}

void ruvmMapToJobMesh(void *pArgsPtr) {
	//CLOCK_INIT;
	//CLOCK_START;
	EnclosingCells ec = {0};
	SendOffArgs *pSend = pArgsPtr;
	MappingJobVars vars = {0};
	vars.pEdgeVerts = pSend->pEdgeVerts;
	vars.alloc = pSend->pContext->alloc;
	vars.id = pSend->id;
	vars.boundsTable.size = pSend->boundsTable.size;
	vars.mesh = pSend->mesh;
	vars.pMap = pSend->pMap;
	vars.pCommonAttribList = pSend->pCommonAttribList;
	ruvmGetEnclosingCells(&vars.alloc, vars.pMap, &vars.mesh, &ec);
	//CLOCK_STOP("getting enclosing cells");
	//CLOCK_START;
	vars.bufferSize = vars.mesh.mesh.faceCount + ec.cellFacesTotal;
	allocStructuresForMapping(&vars, &ec);
	DebugAndPerfVars dpVars = {0};
	//CLOCK_STOP("allocate structures for mapping");
	uint64_t mappingTime, copySingleTime;
	copySingleTime = 0;
	mappingTime = 0;
	for (int32_t i = 0; i < vars.mesh.mesh.faceCount; ++i) {
		//CLOCK_START;
		// copy faces over to a new contiguous array
		linearizeCellFaces(ec.pFaceCellsInfo, ec.pCellFaces, i);
		//iterate through tiles
		//CLOCK_STOP_NO_PRINT;
		//copySingleTime += CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;
		FaceInfo baseFace;
		baseFace.start = vars.mesh.mesh.pFaces[i];
		baseFace.end = vars.mesh.mesh.pFaces[i + 1];
		baseFace.size = baseFace.end - baseFace.start;
		baseFace.index = i;
		vars.tbn = buildFaceTbn(baseFace, &vars.mesh);
		FaceTriangulated faceTris = {0};
		if (baseFace.size > 3) {
			faceTris = triangulateFace(vars.alloc, baseFace, &vars.mesh);
		}
		if (baseFace.size <= 4) {
			//face is a quad, or a tri
			mapPerTile(&vars, &baseFace, &ec, &dpVars, i);
		}
		else {
			//face is an ngon. ngons are processed per tri
			for (int32_t j = 0; j < faceTris.triCount; ++j) {
				int32_t triFaceStart = faceTris.pTris[j];
				int32_t triFaceEnd = faceTris.pTris[j + 1];
				baseFace.start = faceTris.pLoops[triFaceStart];
				baseFace.end = faceTris.pLoops[triFaceEnd];
				baseFace.size = baseFace.end - baseFace.start;
				mapPerTile(&vars, &baseFace, &ec, &dpVars, i);
			}
		}
		//CLOCK_STOP_NO_PRINT;
		//mappingTime += CLOCK_TIME_DIFF(start, stop);
		if (faceTris.pTris) {
			vars.alloc.pFree(faceTris.pTris);
			faceTris.pTris = NULL;
		}
		if (faceTris.pLoops) {
			vars.alloc.pFree(faceTris.pLoops);
			faceTris.pLoops = NULL;
		}
		vars.alloc.pFree(ec.pFaceCellsInfo[i].pCells);
		vars.alloc.pFree(ec.pFaceCellsInfo[i].pCellType);
	}
	//printf("copy faces into single array %lu\n", copySingleTime);
	//printf("maping %lu\n", mappingTime);
	//CLOCK_START;
	//printf("#######Boundary Buffer Size: %d\n", pArgs->bufMesh.boundaryFaceSize);
	vars.bufMesh.mesh.pFaces[vars.bufMesh.boundaryFaceSize] = 
		vars.bufMesh.boundaryLoopSize;
	//args.totalFaces += args.bufMesh.faceCount;
	//args.totalLoops += args.bufMesh.loopCount;
	pSend->totalBoundaryFaces =
		vars.bufferSize - vars.bufMesh.boundaryFaceSize;
	int32_t totalBoundaryLoops = 
		vars.loopBufferSize - vars.bufMesh.boundaryLoopSize;
	pSend->totalBoundaryEdges =
		vars.loopBufferSize - vars.bufMesh.boundaryEdgeSize;
	int32_t totalBoundaryVerts =
		vars.bufferSize - vars.bufMesh.boundaryVertSize;
	pSend->totalFaces = vars.bufMesh.mesh.faceCount + pSend->totalBoundaryFaces;
	pSend->totalLoops = vars.bufMesh.mesh.loopCount + totalBoundaryLoops;
	pSend->totalEdges = vars.bufMesh.mesh.edgeCount + pSend->totalBoundaryEdges;
	pSend->totalVerts = vars.bufMesh.mesh.vertCount + totalBoundaryVerts;
	//printf("MaxDepth: %d\n", dpVars.maxDepth);
	////CLOCK_STOP("projecting");
	//printf("  ^  project: %lu, move & transform: %lu, memset vert adj: %lu\n",
	//		dpVars.timeSpent[0], dpVars.timeSpent[1], dpVars.timeSpent[2]);
	//processBoundaryBuffer(pArgs, bufferSize, boundaryBufferSize);
	destroyMappingTables(&vars.alloc, &vars.mTables);
	ruvmDestroyEnclosingCells(&vars.alloc, &ec);
	//CLOCK_STOP("post mapping stuff");
	//CLOCK_START;
	pSend->boundsTable.pTable = vars.boundsTable.pTable;
	pSend->bufMesh = vars.bufMesh;
	pSend->pContext->threadPool.pMutexLock(pSend->pContext->pThreadPoolHandle, pSend->pMutex);
	++*pSend->pJobsCompleted;
	pSend->pContext->threadPool.pMutexUnlock(pSend->pContext->pThreadPoolHandle, pSend->pMutex);
	//CLOCK_STOP("setting jobs completed");
}
