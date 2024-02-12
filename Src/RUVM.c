#define CLOCK_START gettimeofday(&start, NULL)
#define CLOCK_STOP(a) gettimeofday(&stop, NULL); printf("%s - %s: %lu\n", __func__, (a), getTimeDiff(&start, &stop))
#define CLOCK_STOP_NO_PRINT gettimeofday(&stop, NULL)

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include <Io.h>
#include <EnclosingCells.h>
#include <MapToMesh.h>
#include <BoundaryFaces.h>
#include <MapFile.h>
#include <Context.h>
#include <Allocator.h>
#include <ThreadPool.h>
#include <RUVM.h>

// TODO
// - Reduce the bits written to the UVGP file for vert and loop indices, based on the total amount, in order to save space.
//   No point storing them as 32 bit if there's only like 4,000 verts
// - Split compressed data into chunks maybe?
// - Split whole quadtree into chunks?

uint64_t getTimeDiff(struct timeval *start, struct timeval *stop) {
	return (stop->tv_sec - start->tv_sec) * 1000000 + (stop->tv_usec - start->tv_usec);
}

void ruvmContextInit(RuvmContext *pContext, RuvmAllocator *pAllocator,
                     RuvmThreadPool *pThreadPool, RuvmIo *pIo) {
	RuvmAllocator alloc;
	if (pAllocator) {
		ruvmAllocatorSetCustom(&alloc, pAllocator);
	}
	else {
		ruvmAllocatorSetDefault(&alloc);
	}
	*pContext = alloc.pMalloc(sizeof(RuvmContextInternal));
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
	ruvmLoadRuvmFile(pContext, pMap, filePath, 0);
	ruvmCreateQuadTree(pContext, pMap);
	*pMapHandle = pMap;
	//writeDebugImage(pMap->quadTree.rootCell);
}

void ruvmMapFileUnload(RuvmContext pContext, RuvmMap pMap) {
	ruvmDestroyQuadTree(pContext, pMap->quadTree.pRootCell);
	ruvmMeshDestroy(pContext, &pMap->mesh);
	pContext->alloc.pFree(pMap->header.pLoopAttributeDesc);
	pContext->alloc.pFree(pMap->header.pVertAttributeDesc);
	pContext->alloc.pFree(pMap);
}

void allocateStructuresForMapping(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                                  MapToMeshVars *pMmVars) {
	//struct timeval start, stop;
	RuvmAllocator *pAlloc = &pArgs->alloc;
	//pArgs->boundaryFace = pContext->alloc.pMalloc(sizeof(int32_t) * pArgs->mesh.faceCount + 1);
	int32_t loopBufferSize = pArgs->bufferSize * 2;
	pArgs->loopBufferSize = loopBufferSize;
	pArgs->pBoundaryBuffer = pAlloc->pCalloc(pArgs->boundaryBufferSize, sizeof(BoundaryDir));
	pArgs->localMesh.boundaryVertSize = pArgs->bufferSize - 1;
	pArgs->localMesh.boundaryLoopSize = loopBufferSize - 1;
	pArgs->localMesh.boundaryFaceSize = pArgs->bufferSize - 1;
	pArgs->localMesh.pFaces = pAlloc->pMalloc(sizeof(int32_t) * pArgs->bufferSize);
	pArgs->localMesh.pLoops = pAlloc->pMalloc(sizeof(int32_t) * loopBufferSize);
	pArgs->localMesh.pVerts = pAlloc->pMalloc(sizeof(Vec3) * pArgs->bufferSize);
	pArgs->localMesh.pUvs = pAlloc->pMalloc(sizeof(Vec2) * loopBufferSize);
	pEcVars->pCellFaces = pAlloc->pMalloc(sizeof(int32_t) * pEcVars->cellFacesMax);
	pArgs->pInVertTable = pAlloc->pCalloc(pArgs->mesh.vertCount, 1);
	//TODO: maybe reduce further if unifaces if low,
	//as a larger buffer seems more necessary at higher face counts.
	//Doesn't provie much speed up at lower resolutions.
	pMmVars->vertAdjSize = pEcVars->uniqueFaces / 10;
	printf("Unique ruvm: %d\n", pEcVars->uniqueFaces);
	printf("VertAdjBufSize: %d\n", pMmVars->vertAdjSize);
	//pMmVars->vertAdjSize = 4000;
	pMmVars->pRuvmVertAdj = pAlloc->pCalloc(pMmVars->vertAdjSize, sizeof(VertAdj));
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

static void mapToMeshJob(void *pArgsPtr) {
	struct timeval start, stop;
	//CLOCK_START;
	EnclosingCellsVars ecVars = {0};
	SendOffArgs *pSend = pArgsPtr;
	ThreadArg args = {0};
	args.pEdgeVerts = pSend->pEdgeVerts;
	args.alloc = pSend->pContext->alloc;
	args.id = pSend->id;
	args.boundaryBufferSize = pSend->boundaryBufferSize;
	args.mesh = pSend->mesh;
	args.pMap = pSend->pMap;
	args.maxLoopSize = 0;
	ecVars.pFaceCellsInfo = args.alloc.pMalloc(sizeof(FaceCellsInfo) *
	                        args.mesh.faceCount);
	ruvmGetEnclosingCellsForAllFaces(&args, &ecVars);
	//CLOCK_STOP("getting enclosing cells");
	//CLOCK_START;
	MapToMeshVars mmVars = {0};
	args.bufferSize = args.mesh.faceCount + ecVars.cellFacesTotal;
	allocateStructuresForMapping(&args, &ecVars, &mmVars);
	DebugAndPerfVars dpVars = {0};
	//CLOCK_STOP("allocate structures for mapping");
	uint64_t mappingTime, copySingleTime;
	copySingleTime = 0;
	mappingTime = 0;
	for (int32_t i = 0; i < args.mesh.faceCount; ++i) {
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
				ruvmMapToSingleFace(&args, &ecVars, &mmVars, &dpVars, fTileMin, tile,
				                    baseFace);
			}
		}
		//CLOCK_STOP_NO_PRINT;
		mappingTime += getTimeDiff(&start, &stop);
		args.alloc.pFree(ecVars.pFaceCellsInfo[i].pCells);
		args.alloc.pFree(ecVars.pFaceCellsInfo[i].pCellType);
	}
	printf("Max loop size: %d\n", args.maxLoopSize);
	//printf("copy faces into single array %lu\n", copySingleTime);
	//printf("maping %lu\n", mappingTime);
	//CLOCK_START;
	pSend->pInVertTable = args.pInVertTable;
	args.averageRuvmFacesPerFace /= args.mesh.faceCount;
	//printf("#######Boundary Buffer Size: %d\n", pArgs->localMesh.boundaryFaceSize);
	args.localMesh.pFaces[args.localMesh.boundaryFaceSize] = 
		args.localMesh.boundaryLoopSize;
	args.totalBoundaryFaces = args.totalFaces;
	args.totalFaces += args.localMesh.faceCount;
	args.totalLoops += args.localMesh.loopCount;
	//pArgs->totalFaces = pArgs->localMesh.faceCount +
	//	(bufferSize - pArgs->localMesh.boundaryFaceSize);
	//pArgs->totalLoops = pArgs->localMesh.loopCount +
	//	(loopBufferSize - pArgs->localMesh.boundaryLoopSize);
	args.totalVerts = args.localMesh.vertCount +
		(args.bufferSize - args.localMesh.boundaryVertSize);
	//printf("MaxDepth: %d\n", dpVars.maxDepth);
	////CLOCK_STOP("projecting");
	//printf("  ^  project: %lu, move & transform: %lu, memset vert adj: %lu\n",
	//		dpVars.timeSpent[0], dpVars.timeSpent[1], dpVars.timeSpent[2]);
	//processBoundaryBuffer(pArgs, bufferSize, boundaryBufferSize);
	for (int32_t i = 0; i < mmVars.vertAdjSize; ++i) {
		VertAdj *pEntry = mmVars.pRuvmVertAdj + i;
		if (!pEntry->loopSize) {
			continue;
		}
		pEntry = pEntry->pNext;
		while (pEntry) {
			VertAdj *pNextEntry = pEntry->pNext;
			args.alloc.pFree(pEntry);
			pEntry = pNextEntry;
		};
	}
	args.alloc.pFree(mmVars.pRuvmVertAdj);
	args.alloc.pFree(ecVars.pCellFaces);
	args.alloc.pFree(ecVars.pFaceCellsInfo);
	//CLOCK_STOP("post mapping stuff");
	//CLOCK_START;
	pSend->bufferSize = args.bufferSize;
	pSend->pBoundaryBuffer = args.pBoundaryBuffer;
	pSend->averageVertAdjDepth = args.averageVertAdjDepth;
	pSend->averageRuvmFacesPerFace = args.averageRuvmFacesPerFace;
	pSend->localMesh = args.localMesh;
	pSend->vertBase = args.vertBase;
	pSend->totalBoundaryFaces = args.totalBoundaryFaces;
	pSend->totalVerts = args.totalVerts;
	pSend->totalLoops = args.totalLoops;
	pSend->totalFaces = args.totalFaces;
	pSend->pContext->threadPool.pMutexLock(pSend->pContext->pThreadPoolHandle, pSend->pMutex);
	++*pSend->pJobsCompleted;
	pSend->pContext->threadPool.pMutexUnlock(pSend->pContext->pThreadPoolHandle, pSend->pMutex);
	//CLOCK_STOP("setting jobs completed");
}

void sendOffJobs(RuvmContext pContext, RuvmMap pMap, SendOffArgs *pJobArgs, int32_t *pJobsCompleted,
                 Mesh *pMesh, void *pMutex, EdgeVerts *pEdgeVerts) {
	//struct timeval start, stop;
	//CLOCK_START;
	int32_t facesPerThread = pMesh->faceCount / pContext->threadCount;
	int32_t threadAmountMinus1 = pContext->threadCount - 1;
	void *jobArgPtrs[MAX_THREADS];
	int32_t boundaryBufferSize = pMap->mesh.faceCount / 5;
	printf("fromjobsendoff: BoundaryBufferSize: %d\n", boundaryBufferSize);
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		int32_t meshStart = facesPerThread * i;
		int32_t meshEnd = i == threadAmountMinus1 ?
			pMesh->faceCount : meshStart + facesPerThread;
		Mesh meshPart = *pMesh;
		meshPart.pFaces += meshStart;
		meshPart.faceCount = meshEnd - meshStart;
		pJobArgs[i].pEdgeVerts = pEdgeVerts;
		pJobArgs[i].pMap = pMap;
		pJobArgs[i].boundaryBufferSize = boundaryBufferSize;
		pJobArgs[i].averageVertAdjDepth = 0;
		pJobArgs[i].mesh = meshPart;
		pJobArgs[i].pJobsCompleted = pJobsCompleted;
		pJobArgs[i].id = i;
		pJobArgs[i].pContext = pContext;
		pJobArgs[i].pMutex = pMutex;
		jobArgPtrs[i] = pJobArgs + i;
	}
	pContext->threadPool.pJobStackPushJobs(pContext->pThreadPoolHandle, pContext->threadCount,
	                                       mapToMeshJob, jobArgPtrs);
	//CLOCK_STOP("send off jobs");
}

void allocateMeshOut(RuvmContext pContext, RuvmMesh *pMeshOut, SendOffArgs *pJobArgs) {
	RuvmAllocator *pAlloc = &pContext->alloc;
	int32_t averageVertAdjDepth = 0;
	int32_t workMeshFaces, workMeshLoops, workMeshVerts;
	workMeshFaces = workMeshLoops = workMeshVerts = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		averageVertAdjDepth += pJobArgs[i].averageVertAdjDepth;
		workMeshFaces += pJobArgs[i].totalFaces;
		workMeshLoops += pJobArgs[i].totalLoops;
		workMeshVerts += pJobArgs[i].totalVerts;
	}
	averageVertAdjDepth /= pContext->threadCount;
	printf("Average Vert Adj Depth: %d\n", averageVertAdjDepth);

	pMeshOut->pFaces = pAlloc->pMalloc(sizeof(int32_t) * (workMeshFaces + 1));
	pMeshOut->pLoops = pAlloc->pMalloc(sizeof(int32_t) * workMeshLoops);
	pMeshOut->pVerts = pAlloc->pMalloc(sizeof(Vec3) * workMeshVerts);
	pMeshOut->pUvs = pAlloc->pMalloc(sizeof(Vec2) * workMeshLoops);
}

void copyMesh(int32_t jobIndex, RuvmMesh *pMeshOut, SendOffArgs *pJobArgs) {
	WorkMesh *localMesh = &pJobArgs[jobIndex].localMesh;
	for (int32_t j = 0; j < localMesh->faceCount; ++j) {
		localMesh->pFaces[j] += pMeshOut->loopCount;
	}
	for (int32_t j = 0; j < localMesh->loopCount; ++j) {
		localMesh->pLoops[j] += pMeshOut->vertCount;
	}
	int32_t *facesStart = pMeshOut->pFaces + pMeshOut->faceCount;
	int32_t *loopsStart = pMeshOut->pLoops + pMeshOut->loopCount;
	Vec3 *vertsStart = pMeshOut->pVerts + pMeshOut->vertCount;
	Vec2 *uvsStart = pMeshOut->pUvs + pMeshOut->loopCount;
	memcpy(facesStart, localMesh->pFaces, sizeof(int32_t) * localMesh->faceCount);
	pMeshOut->faceCount += localMesh->faceCount;
	memcpy(loopsStart, localMesh->pLoops, sizeof(int32_t) * localMesh->loopCount);
	memcpy(uvsStart, localMesh->pUvs, sizeof(Vec2) * localMesh->loopCount);
	pMeshOut->loopCount += localMesh->loopCount;
	memcpy(vertsStart, localMesh->pVerts, sizeof(Vec3) * localMesh->vertCount);
	pMeshOut->vertCount += localMesh->vertCount;
}

void combineJobMeshesIntoSingleMesh(RuvmContext pContext, RuvmMap pMap,  RuvmMesh *pMeshOut,
                                    SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts) {
	//struct timeval start, stop;
	//CLOCK_START;
	allocateMeshOut(pContext, pMeshOut, pJobArgs);
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		pJobArgs[i].vertBase = pMeshOut->vertCount;
		copyMesh(i, pMeshOut, pJobArgs);
	}
		ruvmMergeBoundaryFaces(pContext, pMap, pMeshOut, pJobArgs, pEdgeVerts);
		for (int32_t i = 0; i < pContext->threadCount; ++i) {
		WorkMesh *localMesh = &pJobArgs[i].localMesh;
		pContext->alloc.pFree(localMesh->pFaces);
		pContext->alloc.pFree(localMesh->pLoops);
		pContext->alloc.pFree(localMesh->pUvs);
		pContext->alloc.pFree(localMesh->pVerts);
		pContext->alloc.pFree(pJobArgs[i].pBoundaryBuffer);
		pContext->alloc.pFree(pJobArgs[i].pInVertTable);
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

void ruvmMapToMesh(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMeshIn,
                   RuvmMesh *pMeshOut) {
	struct timeval start, stop;
	CLOCK_START;
	EdgeVerts *pEdgeVerts;
	printf("EdgeCount: %d\n", pMeshIn->edgeCount);
	buildEdgeVertsTable(pContext, &pEdgeVerts, pMeshIn);
	CLOCK_STOP("Edge Table Time");

	CLOCK_START;
	SendOffArgs jobArgs[MAX_THREADS] = {0};
	int32_t jobsCompleted = 0;
	void *pMutex = NULL;
	pContext->threadPool.pMutexGet(pContext->pThreadPoolHandle, &pMutex);
	sendOffJobs(pContext, pMap, jobArgs, &jobsCompleted, pMeshIn, pMutex,
	            pEdgeVerts);
	CLOCK_STOP("Send Off Time");

	//might as well do this here while the other threads are busy

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

	int64_t averageRuvmFacesPerFace = 0;
	for(int32_t i = 0; i < pContext->threadCount; ++i) {
		averageRuvmFacesPerFace += jobArgs[i].averageRuvmFacesPerFace;
	}
	averageRuvmFacesPerFace /= pContext->threadCount;
	printf("---- averageRuvmFacesPerFace: %lu ----\n", averageRuvmFacesPerFace);

	CLOCK_START;
	combineJobMeshesIntoSingleMesh(pContext, pMap, pMeshOut, jobArgs, pEdgeVerts);
	pContext->alloc.pFree(pEdgeVerts);
	CLOCK_STOP("Combine time");
}

void ruvmMeshDestroy(RuvmContext pContext, RuvmMesh *pMesh) {
	if(pMesh->pFaces) {
		pContext->alloc.pFree(pMesh->pFaces);
	}
	if (pMesh->pLoops) {
		pContext->alloc.pFree(pMesh->pLoops);
	}
	if (pMesh->pNormals) {
		pContext->alloc.pFree(pMesh->pNormals);
	}
	if (pMesh->pUvs) {
		pContext->alloc.pFree(pMesh->pUvs);
	}
	if (pMesh->pVerts) {
		pContext->alloc.pFree(pMesh->pVerts);
	}
}
