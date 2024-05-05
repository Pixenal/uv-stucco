#include <stdio.h>
#include <string.h>
#include <math.h>

#include <Io.h>
#include <EnclosingCells.h>
#include <MapToMesh.h>
#include <BoundaryFaces.h>
#include <MapFile.h>
#include <Context.h>
#include <Allocator.h>
#include <ThreadPool.h>
#include <RUVM.h>
#include <Clock.h>
#include <MathUtils.h>
#include <AttribUtils.h>
#include <Utils.h>

// TODO
// - Reduce the bits written to the UVGP file for vert and loop indices, based on the total amount, in order to save space.
//   No point storing them as 32 bit if there's only like 4,000 verts
// - Split compressed data into chunks maybe?
// - Split whole quadtree into chunks?
//
// - Add blending options to interface, that control how MeshIn attributes blend with
//   those from the Map. Also add an option to disable or enable interpolation.
//   Add these to the RuvmAttrib struct.
//
//TODO repalce localMesh with bufMesh.
//The old name is still present in some functions & vars
//TODO a highly distorted meshIn can cause invalid geometry
//(enough to crash blender). When meshIn is quads atleast
//(I've not tested with tris). Find out why
//TODO ruvmPreserve isn't working.
//TODO add option to vary z projection depth with uv stretch (for wires and such)
//Add option to mask ruvmpreserve by map edges. Where masking is defined
//per edge, not per face. An preserve meshin edge must pass through 2 map
//edges which are marked preserve, for the edge to cut that map face.
//This will cause there to be gaps in the loop, in cases of diagional meshin
//preserve edges. Just triangulate these faces, or leave as an ngon?
//TODO Add an option for subdivision like smoothing (for instances where
//the map is higher res than the base mesh). So that the surface can be
//smoothed, without needing to induce the perf cost of actually subdividing
//the base mesh. Is this possible?

static void ruvmSetTypeDefaultConfig(RuvmContext pContext) {
	RuvmTypeDefaultConfig config = {0};
	pContext->typeDefaults = config;
}

void ruvmContextInit(RuvmContext *pContext, RuvmAllocator *pAllocator,
                     RuvmThreadPool *pThreadPool, RuvmIo *pIo,
					 RuvmTypeDefaultConfig *pTypeDefaultConfig) {
	RuvmAllocator alloc;
	if (pAllocator) {
		ruvmAllocatorSetCustom(&alloc, pAllocator);
	}
	else {
		ruvmAllocatorSetDefault(&alloc);
	}
	*pContext = alloc.pCalloc(1, sizeof(RuvmContextInternal));
	(*pContext)->alloc = alloc;
	if (pThreadPool) {
		ruvmThreadPoolSetCustom(*pContext, pThreadPool);
	}
	else {
		ruvmThreadPoolSetDefault(*pContext);
	}
	if (pIo) {
		ruvmIoSetCustom(*pContext, pIo);
	}
	else {
		ruvmIoSetDefault(*pContext);
	}
	(*pContext)->threadPool.pInit(&(*pContext)->pThreadPoolHandle, &(*pContext)->threadCount,
	                              &(*pContext)->alloc);
	if (pTypeDefaultConfig) {
		(*pContext)->typeDefaults = *pTypeDefaultConfig;
	}
	else {
		ruvmSetTypeDefaultConfig(*pContext);
	}
}

void ruvmContextDestroy(RuvmContext pContext) {
	pContext->threadPool.pDestroy(pContext->pThreadPoolHandle);
	pContext->alloc.pFree(pContext);
}

void ruvmMapFileExport(RuvmContext pContext, RuvmMesh *pMesh) {
	printf("%d vertices, and %d faces\n", pMesh->vertCount, pMesh->faceCount);
	ruvmWriteRuvmFile(pContext, pMesh);
}

void ruvmMapFileLoad(RuvmContext pContext, RuvmMap *pMapHandle,
                      char *filePath) {
	RuvmMap pMap = pContext->alloc.pCalloc(1, sizeof(MapFile));
	ruvmLoadRuvmFile(pContext, pMap, filePath);
	ruvmCreateQuadTree(pContext, pMap);
	*pMapHandle = pMap;
	//writeDebugImage(pMap->quadTree.rootCell);
}

void ruvmMapFileUnload(RuvmContext pContext, RuvmMap pMap) {
	ruvmDestroyQuadTree(pContext, pMap->quadTree.pRootCell);
	ruvmMeshDestroy(pContext, &pMap->mesh.mesh);
	pContext->alloc.pFree(pMap);
}

static void initCommonAttrib(RuvmContext pContext, RuvmCommonAttrib *pEntry, RuvmAttrib *pAttrib) {
	memcpy(pEntry->name, pAttrib->name, RUVM_ATTRIB_NAME_MAX_LEN);
	RuvmTypeDefault *pDefault = 
		getTypeDefaultConfig(&pContext->typeDefaults, pAttrib->type);
	pEntry->blendConfig = pDefault->blendConfig;
}

static void getCommonAttribs(RuvmContext pContext, AttribArray *pMapAttribs,
                             AttribArray *pMeshAttribs,
                             int32_t *pCommonAttribCount, RuvmCommonAttrib **ppCommonAttribs) {
	if (!pMeshAttribs || !pMapAttribs) {
		return;
	}
	int32_t count = 0;
	for (int32_t i = 0; i < pMeshAttribs->count; ++i) {
		for (int32_t j = 0; j < pMapAttribs->count; ++j) {
			if (0 == strncmp(pMeshAttribs->pArr[i].name,
			                 pMapAttribs->pArr[j].name,
			                 RUVM_ATTRIB_NAME_MAX_LEN)) {
				count++;
			}
		}
	}
	*ppCommonAttribs = count ?
		pContext->alloc.pMalloc(sizeof(RuvmCommonAttrib) * count) : NULL;
	count = 0;
	for (int32_t i = 0; i < pMeshAttribs->count; ++i) {
		for (int32_t j = 0; j < pMapAttribs->count; ++j) {
			if (0 == strncmp(pMeshAttribs->pArr[i].name,
			                 pMapAttribs->pArr[j].name,
			                 RUVM_ATTRIB_NAME_MAX_LEN)) {
				initCommonAttrib(pContext, *ppCommonAttribs + count,
				                 pMeshAttribs->pArr + i);
				count++;
			}
		}
	}
	*pCommonAttribCount = count;
}

//TODO handle edge case, where attribute share the same name, but have incompatible types.
//Such as a float and a string.
void ruvmQueryCommonAttribs(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMesh,
                            RuvmCommonAttribList *pCommonAttribs) {
	getCommonAttribs(pContext, &pMap->mesh.mesh.meshAttribs, &pMesh->meshAttribs,
					 &pCommonAttribs->meshCount, &pCommonAttribs->pMesh);
	getCommonAttribs(pContext, &pMap->mesh.mesh.faceAttribs, &pMesh->faceAttribs,
					 &pCommonAttribs->faceCount, &pCommonAttribs->pFace);
	getCommonAttribs(pContext, &pMap->mesh.mesh.loopAttribs, &pMesh->loopAttribs,
					 &pCommonAttribs->loopCount, &pCommonAttribs->pLoop);
	getCommonAttribs(pContext, &pMap->mesh.mesh.edgeAttribs, &pMesh->edgeAttribs,
	                 &pCommonAttribs->edgeCount, &pCommonAttribs->pEdge);
	getCommonAttribs(pContext, &pMap->mesh.mesh.vertAttribs, &pMesh->vertAttribs,
					 &pCommonAttribs->vertCount, &pCommonAttribs->pVert);
}

void ruvmDestroyCommonAttribs(RuvmContext pContext,
                              RuvmCommonAttribList *pCommonAttribs) {
	if (pCommonAttribs->pMesh) {
		pContext->alloc.pFree(pCommonAttribs->pMesh);
	}
	if (pCommonAttribs->pFace) {
		pContext->alloc.pFree(pCommonAttribs->pFace);
	}
	if (pCommonAttribs->pLoop) {
		pContext->alloc.pFree(pCommonAttribs->pLoop);
	}
	if (pCommonAttribs->pEdge) {
		pContext->alloc.pFree(pCommonAttribs->pEdge);
	}
	if (pCommonAttribs->pVert) {
		pContext->alloc.pFree(pCommonAttribs->pVert);
	}
}

void allocAttribs(RuvmAllocator alloc, RuvmAttribArray *pDest,
                  RuvmAttribArray *pSrcA, RuvmAttribArray *pSrcB,
				  int32_t dataLen) {
	if (pSrcA && pSrcA->count) {
		pDest->count = pSrcA->count;
	}
	if (pSrcB && pSrcB->count) {
		for (int32_t i = 0; i < pSrcB->count; ++i) {
			if (getAttrib(pSrcB->pArr[i].name, pSrcA)) {
				//skip if attribute already exists in bufmesh
				continue;
			}
			++pDest->count;
		}
	}
	if (!pDest->count) {
		return;
	}
	pDest->pArr = alloc.pMalloc(sizeof(RuvmAttrib) * pDest->count);
	//reset attrib count, for use in the below loops
	pDest->count = 0;
	if (pSrcA && pSrcA->count) {
		for (int32_t i = 0; i < pSrcA->count; ++i) {
			pDest->pArr[i].type = pSrcA->pArr[i].type;
			memcpy(pDest->pArr[i].name, pSrcA->pArr[i].name,
			       RUVM_ATTRIB_NAME_MAX_LEN);
			pDest->pArr[i].origin = RUVM_ATTRIB_ORIGIN_MESH_IN;
			pDest->pArr[i].interpolate = pSrcA->pArr[i].interpolate;
			int32_t attribSize = getAttribSize(pSrcA->pArr[i].type);
			pDest->pArr[i].pData = alloc.pMalloc(attribSize * dataLen);
		}
		pDest->count = pSrcA->count;
	}
	if (!pSrcB || !pSrcB->count) {
		return;
	}
	for (int32_t i = 0; i < pSrcB->count; ++i) {
		RuvmAttrib *pExisting = getAttrib(pSrcB->pArr[i].name, pDest);
		if (pExisting) {
			pExisting->origin = RUVM_ATTRIB_ORIGIN_COMMON;
			continue;
		}
		pDest->pArr[pDest->count].type = pSrcB->pArr[i].type;
		memcpy(pDest->pArr[pDest->count].name, pSrcB->pArr[i].name, RUVM_ATTRIB_NAME_MAX_LEN);
		pDest->pArr[pDest->count].origin = RUVM_ATTRIB_ORIGIN_MAP;
		pDest->pArr[pDest->count].interpolate = pSrcB->pArr[i].interpolate;
		int32_t attribSize = getAttribSize(pSrcB->pArr[i].type);
		pDest->pArr[pDest->count].pData = alloc.pMalloc(attribSize * dataLen);
		++pDest->count;
	}
	return;
}

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

static Mat3x3 buildFaceTbn(FaceInfo face, Mesh *pMesh) {
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

static void mapPerTile(MappingJobVars *pMVars, FaceInfo *pBaseFace,
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

static void destroyMappingTables(RuvmAllocator *pAlloc, MappingTables *pMVars) {
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

static void mapToMeshJob(void *pArgsPtr) {
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

void sendOffJobs(RuvmContext pContext, RuvmMap pMap, SendOffArgs *pJobArgs,
                 int32_t *pJobsCompleted, Mesh *pMesh, void *pMutex,
                 EdgeVerts *pEdgeVerts, int8_t *pInVertTable,
				 RuvmCommonAttribList *pCommonAttribList) {
	//struct timeval start, stop;
	//CLOCK_START;
	int32_t facesPerThread = pMesh->mesh.faceCount / pContext->threadCount;
	int32_t threadAmountMinus1 = pContext->threadCount - 1;
	void *jobArgPtrs[MAX_THREADS];
	int32_t boundsTableSize = pMap->mesh.mesh.faceCount / 5;
	printf("fromjobsendoff: BoundaryBufferSize: %d\n", boundsTableSize);
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		int32_t meshStart = facesPerThread * i;
		int32_t meshEnd = i == threadAmountMinus1 ?
			pMesh->mesh.faceCount : meshStart + facesPerThread;
		Mesh meshPart = *pMesh;
		meshPart.mesh.pFaces += meshStart;
		meshPart.mesh.faceCount = meshEnd - meshStart;
		pJobArgs[i].pInVertTable = pInVertTable;
		pJobArgs[i].pEdgeVerts = pEdgeVerts;
		pJobArgs[i].pMap = pMap;
		pJobArgs[i].boundsTable.size = boundsTableSize;
		pJobArgs[i].mesh = meshPart;
		pJobArgs[i].pJobsCompleted = pJobsCompleted;
		pJobArgs[i].id = i;
		pJobArgs[i].pContext = pContext;
		pJobArgs[i].pMutex = pMutex;
		pJobArgs[i].pCommonAttribList = pCommonAttribList;
		jobArgPtrs[i] = pJobArgs + i;
	}
	pContext->threadPool.pJobStackPushJobs(pContext->pThreadPoolHandle, pContext->threadCount,
	                                       mapToMeshJob, jobArgPtrs);
	//CLOCK_STOP("send off jobs");
}

void allocateMeshOut(RuvmContext pContext, RuvmMesh *pMeshOut, SendOffArgs *pJobArgs) {
	RuvmAllocator *pAlloc = &pContext->alloc;
	typedef struct {
		int32_t faces;
		int32_t loops;
		int32_t edges;
		int32_t verts;
	} MeshCounts;
	MeshCounts totalCount = {0};
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		totalCount.faces += pJobArgs[i].totalFaces;
		totalCount.loops += pJobArgs[i].totalLoops;
		totalCount.edges += pJobArgs[i].totalEdges;
		totalCount.verts += pJobArgs[i].totalVerts;
	}
	//TODO figure out how to handle edges in local meshes,
	//probably just add internal edges to local mesh,
	//and figure out edges in boundary faces after jobs are finished?
	//You'll need to provide functionality for interpolating and blending
	//edge data, so keep that in mind.
	RuvmMesh *pBufMesh = &pJobArgs[0].bufMesh.mesh;
	pMeshOut->pFaces = pAlloc->pMalloc(sizeof(int32_t) * (totalCount.faces + 1));
	allocAttribs(*pAlloc, &pMeshOut->faceAttribs, &pBufMesh->faceAttribs,
				 NULL, totalCount.faces);
	pMeshOut->pLoops = pAlloc->pMalloc(sizeof(int32_t) * totalCount.loops);
	allocAttribs(*pAlloc, &pMeshOut->loopAttribs, &pBufMesh->loopAttribs,
				 NULL, totalCount.loops);
	pMeshOut->pEdges = pAlloc->pMalloc(sizeof(int32_t) * totalCount.loops);
	allocAttribs(*pAlloc, &pMeshOut->edgeAttribs, &pBufMesh->edgeAttribs,
				 NULL, totalCount.edges);
	allocAttribs(*pAlloc, &pMeshOut->vertAttribs, &pBufMesh->vertAttribs,
				 NULL, totalCount.verts);
}

static void bulkCopyAttribs(AttribArray *pSrc, int32_t SrcOffset,
                            AttribArray *pDest, int32_t dataLen) {
	for (int32_t i = 0; i < pSrc->count; ++i) {
		void *attribDestStart = attribAsVoid(pDest->pArr + i, SrcOffset);
		int32_t attribTypeSize = getAttribSize(pSrc->pArr[i].type);
		memcpy(attribDestStart, pSrc->pArr[i].pData, attribTypeSize * dataLen);
	}
}

void copyMesh(int32_t jobIndex, RuvmMesh *pMeshOut, SendOffArgs *pJobArgs) {
	BufMesh *bufMesh = &pJobArgs[jobIndex].bufMesh;
	for (int32_t j = 0; j < bufMesh->mesh.faceCount; ++j) {
		bufMesh->mesh.pFaces[j] += pMeshOut->loopCount;
	}
	for (int32_t j = 0; j < bufMesh->mesh.loopCount; ++j) {
		bufMesh->mesh.pLoops[j] += pMeshOut->vertCount;
		bufMesh->mesh.pEdges[j] += pMeshOut->edgeCount;
	}
	int32_t *facesStart = pMeshOut->pFaces + pMeshOut->faceCount;
	int32_t *loopsStart = pMeshOut->pLoops + pMeshOut->loopCount;
	int32_t *edgesStart = pMeshOut->pEdges + pMeshOut->loopCount;
	memcpy(facesStart, bufMesh->mesh.pFaces, sizeof(int32_t) * bufMesh->mesh.faceCount);
	bulkCopyAttribs(&bufMesh->mesh.faceAttribs, pMeshOut->faceCount,
	                &pMeshOut->faceAttribs, bufMesh->mesh.faceCount);
	pMeshOut->faceCount += bufMesh->mesh.faceCount;
	memcpy(loopsStart, bufMesh->mesh.pLoops, sizeof(int32_t) * bufMesh->mesh.loopCount);
	bulkCopyAttribs(&bufMesh->mesh.loopAttribs, pMeshOut->loopCount,
	                &pMeshOut->loopAttribs, bufMesh->mesh.loopCount);
	pMeshOut->loopCount += bufMesh->mesh.loopCount;
	memcpy(edgesStart, bufMesh->mesh.pEdges, sizeof(int32_t) * bufMesh->mesh.loopCount);
	bulkCopyAttribs(&bufMesh->mesh.edgeAttribs, pMeshOut->edgeCount,
	                &pMeshOut->edgeAttribs, bufMesh->mesh.edgeCount);
	pMeshOut->edgeCount += bufMesh->mesh.edgeCount;
	bulkCopyAttribs(&bufMesh->mesh.vertAttribs, pMeshOut->vertCount,
	                &pMeshOut->vertAttribs, bufMesh->mesh.vertCount);
	pMeshOut->vertCount += bufMesh->mesh.vertCount;
}

void combineJobMeshesIntoSingleMesh(RuvmContext pContext, RuvmMap pMap,  RuvmMesh *pMeshOut,
                                    SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
									int8_t *pVertSeamTable) {
	//struct timeval start, stop;
	//CLOCK_START;
	allocateMeshOut(pContext, pMeshOut, pJobArgs);
	JobBases *pJobBases =
		pContext->alloc.pMalloc(sizeof(JobBases) * pContext->threadCount);
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		pJobBases[i].vertBase = pMeshOut->vertCount;
		pJobBases[i].edgeBase = pMeshOut->edgeCount;
		copyMesh(i, pMeshOut, pJobArgs);
	}
	Mesh meshOutWrap = {.mesh = *pMeshOut};
	meshOutWrap.pVertAttrib = getAttrib("position", &pMeshOut->vertAttribs);
	meshOutWrap.pVerts = meshOutWrap.pVertAttrib->pData;
	meshOutWrap.pUvAttrib = getAttrib("UVMap", &pMeshOut->loopAttribs);
	meshOutWrap.pUvs = meshOutWrap.pUvAttrib->pData;
	ruvmMergeBoundaryFaces(pContext, pMap, &meshOutWrap, pJobArgs,
	                       pEdgeVerts, pJobBases, pVertSeamTable);
	*pMeshOut = meshOutWrap.mesh;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		BufMesh *bufMesh = &pJobArgs[i].bufMesh;
		ruvmMeshDestroy(pContext, &bufMesh->mesh);
		pContext->alloc.pFree(pJobArgs[i].boundsTable.pTable);
	}
	pMeshOut->pFaces[pMeshOut->faceCount] = pMeshOut->loopCount;
	//CLOCK_STOP("moving to work mesh");
}

static void buildEdgeVertsTable(RuvmContext pContext, EdgeVerts **ppEdgeVerts, RuvmMesh *pMesh) {
	*ppEdgeVerts = pContext->alloc.pMalloc(sizeof(EdgeTable) * pMesh->edgeCount);
	memset(*ppEdgeVerts, -1, sizeof(EdgeTable) * pMesh->edgeCount);
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		int32_t edge = pMesh->pEdges[i];
		int32_t whichVert = (*ppEdgeVerts)[edge].verts[0] >= 0;
		(*ppEdgeVerts)[edge].verts[whichVert] = i;
	}
}

static void buildVertTables(RuvmContext pContext, Mesh *pMesh,
                            int8_t **ppInVertTable, int8_t **ppVertSeamTable,
							EdgeVerts *pEdgeVerts) {
	*ppInVertTable = pContext->alloc.pCalloc(pMesh->mesh.vertCount, 1);
	*ppVertSeamTable = pContext->alloc.pCalloc(pMesh->mesh.vertCount, 1);
	for (int32_t i = 0; i < pMesh->mesh.faceCount; ++i) {
		FaceInfo face;
		face.start = pMesh->mesh.pFaces[i];
		face.end = pMesh->mesh.pFaces[i + 1];
		face.size = face.end - face.start;
		face.index = i;
		for (int32_t j = 0; j < face.size; ++j) {
			int32_t loopIndex = face.start + j;
			int32_t vertIndex = pMesh->mesh.pLoops[loopIndex];
			int32_t edgeIndex = pMesh->mesh.pEdges[loopIndex];
			int32_t isSeam = checkIfEdgeIsSeam(edgeIndex, face, j, pMesh, pEdgeVerts);
			if (!(*ppInVertTable)[vertIndex] &&
			    checkIfEdgeIsPreserve(pMesh, edgeIndex) && !isSeam) {
				(*ppInVertTable)[vertIndex] = 1;
			}
			//cap at 3 to avoid integer overflow
			else if (isSeam && (*ppVertSeamTable)[vertIndex] < 3) {
				//isSeam returns 2 if border edge, and 1 if seam
				(*ppVertSeamTable)[vertIndex] += isSeam;
			}
		}
	}
}

int32_t validateMeshIn(RuvmMesh *pMeshIn) {
	for (int32_t i = 0; i < pMeshIn->faceCount; ++i) {
		int32_t loopCount = pMeshIn->pFaces[i + 1] - pMeshIn->pFaces[i];
		if (loopCount > 4) {
			return 1;
		}
	}
	return 0;
}

static void setAttribOrigins(AttribArray *pAttribs, RUVM_ATTRIB_ORIGIN origin) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		pAttribs->pArr[i].origin = origin;
	}
}

static void reallocMeshOut(RuvmContext pContext, RuvmMesh *pMeshOut) {
	pMeshOut->pFaces = 
		pContext->alloc.pRealloc(pMeshOut->pFaces,
		                         sizeof(int32_t) * (pMeshOut->faceCount + 1));
	for (int32_t i = 0; i < pMeshOut->faceAttribs.count; ++i) {
		RuvmAttrib *pAttrib = pMeshOut->faceAttribs.pArr + i;
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData =
			pContext->alloc.pRealloc(pAttrib->pData,
			                         attribSize * (pMeshOut->faceCount + 1));
	}
	pMeshOut->pLoops = 
		pContext->alloc.pRealloc(pMeshOut->pLoops,
		                         sizeof(int32_t) * pMeshOut->loopCount);
	for (int32_t i = 0; i < pMeshOut->loopAttribs.count; ++i) {
		RuvmAttrib *pAttrib = pMeshOut->loopAttribs.pArr + i;
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData = 
			pContext->alloc.pRealloc(pAttrib->pData,
			                         attribSize * pMeshOut->loopCount);
	}
	pMeshOut->pEdges = 
		pContext->alloc.pRealloc(pMeshOut->pEdges, sizeof(int32_t) * pMeshOut->loopCount);
	for (int32_t i = 0; i < pMeshOut->edgeAttribs.count; ++i) {
		RuvmAttrib *pAttrib = pMeshOut->edgeAttribs.pArr + i;
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData = 
			pContext->alloc.pRealloc(pAttrib->pData,
			                         attribSize * pMeshOut->edgeCount);
	}
	for (int32_t i = 0; i < pMeshOut->vertAttribs.count; ++i) {
		RuvmAttrib *pAttrib = pMeshOut->vertAttribs.pArr + i;
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData = 
			pContext->alloc.pRealloc(pAttrib->pData,
			                         attribSize * pMeshOut->vertCount);
	}
}

int32_t ruvmMapToMesh(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMeshIn,
                      RuvmMesh *pMeshOut, RuvmCommonAttribList *pCommonAttribList) {
	CLOCK_INIT;
	if (!pMeshIn) {
		printf("Ruvm map to mesh failed, pMeshIn was null\n");
		return 2;
	}
	if (!pMap) {
		printf("Ruvm map to mesh failed, pMap was null\n");
		return 3;
	}
	if (validateMeshIn(pMeshIn)) {
		//return 1;
	}

	Mesh meshIn = {*pMeshIn};
	//TODO replace hard coded names with function parameters.
	//User can specify which attributes should be treated as vert, uv, and normal.
	meshIn.pVertAttrib = getAttrib("position", &meshIn.mesh.vertAttribs);
	meshIn.pVerts = meshIn.pVertAttrib->pData;
	meshIn.pUvAttrib = getAttrib("UVMap", &meshIn.mesh.loopAttribs);
	meshIn.pUvs = meshIn.pUvAttrib->pData;
	meshIn.pNormalAttrib = getAttrib("normal", &meshIn.mesh.loopAttribs);
	meshIn.pNormals = meshIn.pNormalAttrib->pData;
	meshIn.pEdgePreserveAttrib = getAttrib("RuvmEdgePreserve",
	                                       &meshIn.mesh.edgeAttribs);
	if (meshIn.pEdgePreserveAttrib) {
		meshIn.pEdgePreserve = meshIn.pEdgePreserveAttrib->pData;
	}
	//TODO remove this, I dont think it's necessary. Origin is only used in bufmesh
	//it doesn't matter what it's set to here
	setAttribOrigins(&pMeshIn->meshAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);
	setAttribOrigins(&pMeshIn->faceAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);
	setAttribOrigins(&pMeshIn->loopAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);
	setAttribOrigins(&pMeshIn->edgeAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);
	setAttribOrigins(&pMeshIn->vertAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);

	CLOCK_START;
	EdgeVerts *pEdgeVerts;
	printf("EdgeCount: %d\n", pMeshIn->edgeCount);
	buildEdgeVertsTable(pContext, &pEdgeVerts, pMeshIn);
	int8_t *pInVertTable;
	int8_t *pVertSeamTable;
	buildVertTables(pContext, &meshIn, &pInVertTable, &pVertSeamTable, pEdgeVerts);
	CLOCK_STOP("Edge Table Time");

	CLOCK_START;
	SendOffArgs jobArgs[MAX_THREADS] = {0};
	int32_t jobsCompleted = 0;
	void *pMutex = NULL;
	pContext->threadPool.pMutexGet(pContext->pThreadPoolHandle, &pMutex);
	sendOffJobs(pContext, pMap, jobArgs, &jobsCompleted, &meshIn, pMutex,
	            pEdgeVerts, pInVertTable, pCommonAttribList);
	CLOCK_STOP("Send Off Time");

	CLOCK_START;
	int32_t waiting;
	do  {
		void (*pJob)(void *) = NULL;
		void *pArgs = NULL;
		pContext->threadPool.pJobStackGetJob(pContext->pThreadPoolHandle, &pJob, &pArgs);
		if (pJob) {
			pJob(pArgs);
		}
		pContext->threadPool.pMutexLock(pContext->pThreadPoolHandle, pMutex);
		waiting = jobsCompleted < pContext->threadCount;
		pContext->threadPool.pMutexUnlock(pContext->pThreadPoolHandle, pMutex);
	} while(waiting);
	pContext->threadPool.pMutexDestroy(pContext->pThreadPoolHandle, pMutex);
	CLOCK_STOP("Waiting Time");

	CLOCK_START;
	combineJobMeshesIntoSingleMesh(pContext, pMap, pMeshOut, jobArgs, pEdgeVerts,
	                               pVertSeamTable);
	CLOCK_STOP("Combine time");
	pContext->alloc.pFree(pEdgeVerts);
	pContext->alloc.pFree(pInVertTable);
	pContext->alloc.pFree(pVertSeamTable);
	CLOCK_START;
	reallocMeshOut(pContext, pMeshOut);
	CLOCK_STOP("Realloc time");
	return 0;
}

void ruvmMeshDestroy(RuvmContext pContext, RuvmMesh *pMesh) {
	for (int32_t i = 0; i < pMesh->meshAttribs.count; ++i) {
		if (pMesh->meshAttribs.pArr[i].pData) {
			pContext->alloc.pFree(pMesh->meshAttribs.pArr[i].pData);
		}
	}
	if (pMesh->meshAttribs.count && pMesh->meshAttribs.pArr) {
		pContext->alloc.pFree(pMesh->meshAttribs.pArr);
	}
	if(pMesh->pFaces) {
		pContext->alloc.pFree(pMesh->pFaces);
	}
	for (int32_t i = 0; i < pMesh->faceAttribs.count; ++i) {
		if (pMesh->faceAttribs.pArr[i].pData) {
			pContext->alloc.pFree(pMesh->faceAttribs.pArr[i].pData);
		}
	}
	if (pMesh->faceAttribs.count && pMesh->faceAttribs.pArr) {
		pContext->alloc.pFree(pMesh->faceAttribs.pArr);
	}
	if (pMesh->pLoops) {
		pContext->alloc.pFree(pMesh->pLoops);
	}
	for (int32_t i = 0; i < pMesh->loopAttribs.count; ++i) {
		if (pMesh->loopAttribs.pArr[i].pData) {
			pContext->alloc.pFree(pMesh->loopAttribs.pArr[i].pData);
		}
	}
	if (pMesh->loopAttribs.count && pMesh->loopAttribs.pArr) {
		pContext->alloc.pFree(pMesh->loopAttribs.pArr);
	}
	if (pMesh->pEdges) {
		pContext->alloc.pFree(pMesh->pEdges);
	}
	for (int32_t i = 0; i < pMesh->edgeAttribs.count; ++i) {
		if (pMesh->edgeAttribs.pArr[i].pData) {
			pContext->alloc.pFree(pMesh->edgeAttribs.pArr[i].pData);
		}
	}
	if (pMesh->edgeAttribs.count && pMesh->edgeAttribs.pArr) {
		pContext->alloc.pFree(pMesh->edgeAttribs.pArr);
	}
	for (int32_t i = 0; i < pMesh->vertAttribs.count; ++i) {
		if (pMesh->vertAttribs.pArr[i].pData) {
			pContext->alloc.pFree(pMesh->vertAttribs.pArr[i].pData);
		}
	}
	if (pMesh->vertAttribs.count && pMesh->vertAttribs.pArr) {
		pContext->alloc.pFree(pMesh->vertAttribs.pArr);
	}
}

void ruvmGetAttribSize(RuvmAttrib *pAttrib, int32_t *pSize) {
	*pSize = getAttribSize(pAttrib->type);
}

RuvmAttrib *ruvmGetAttrib(char *pName, AttribArray *pAttribs) {
	return getAttrib(pName, pAttribs);
}
