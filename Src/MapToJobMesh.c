#include <string.h>
#include <stdio.h>

#include <UvStucco.h>
#include <MapToJobMesh.h>
#include <MapFile.h>
#include <Context.h>
#include <Clock.h>
#include <AttribUtils.h>
#include <Utils.h>
#include <Error.h>

static
void allocBufMesh(MappingJobVars *pVars, int32_t cornerBufSize) {
	StucMap pMap = pVars->pMap;
	StucMesh *pMeshIn = &pVars->mesh.core;
	Mesh *pMesh = &pVars->bufMesh.mesh;
	StucAlloc *pAlloc = &pVars->alloc;
	pMesh->faceBufSize = pVars->bufSize;
	pMesh->cornerBufSize = pVars->cornerBufSize;
	pMesh->edgeBufSize = pVars->cornerBufSize;
	pMesh->vertBufSize = pVars->bufSize;
	pMesh->core.pFaces = pAlloc->pMalloc(sizeof(int32_t) * pMesh->faceBufSize);
	pMesh->core.pCorners = pAlloc->pMalloc(sizeof(int32_t) * pMesh->cornerBufSize);
	pMesh->core.pEdges = pAlloc->pMalloc(sizeof(int32_t) * pMesh->edgeBufSize);
	Mesh *srcs[2] = {(Mesh *)pMeshIn, &pMap->mesh};
	allocAttribsFromMeshArr(&pVars->alloc, pMesh, 2, srcs, true);
	appendBufOnlySpecialAttribs(&pVars->alloc, &pVars->bufMesh);
	setSpecialBufAttribs((BufMesh *)pMesh, 0x3e); //set all
	setSpecialAttribs(pVars->pContext, pMesh, 0x40e); //set vert, normal, uv, and w scale
	setAttribToDontCopy(pVars->pContext, pMesh, 0x400); //set w scale to DONT_COPY

	pMesh->cornerBufSize = cornerBufSize;
	pMesh->core.type.type = STUC_OBJECT_DATA_MESH_BUF;
}

static
void allocBufMeshAndTables(MappingJobVars *pVars,
                           FaceCellsTable *pFaceCellsTable) {
	//struct timeval start, stop;
	StucAlloc *pAlloc = &pVars->alloc;
	pVars->rawBufSize = pVars->bufSize;
	pVars->bufSize = pVars->bufSize / 20 + 2; //Add 2 incase it truncs to 0
	pVars->bufSize += pVars->bufSize % 2; //ensure it's even, so realloc is easier
	int32_t cornerBufSize = pVars->bufSize * 2;
	pVars->cornerBufSize = cornerBufSize;
	pVars->borderTable.pTable =
		pAlloc->pCalloc(pVars->borderTable.size, sizeof(BorderBucket));
	allocBufMesh(pVars, cornerBufSize);
	//pVars->pInVertTable = pAlloc->pCalloc(pVars->mesh.vertCount, 1);
	//TODO: maybe reduce further if unifaces if low,
	//as a larger buf seems more necessary at higher face counts.
	//Doesn't provie much speed up at lower resolutions.
	pVars->localTables.vertTableSize = pFaceCellsTable->uniqueFaces / 10;
	pVars->localTables.edgeTableSize = pFaceCellsTable->uniqueFaces / 7;
	pVars->localTables.pVertTable =
		pAlloc->pCalloc(pVars->localTables.vertTableSize, sizeof(LocalVert));
	pVars->localTables.pEdgeTable =
		pAlloc->pCalloc(pVars->localTables.edgeTableSize, sizeof(LocalEdge));
}

static
Result mapPerTile(MappingJobVars *pMVars, FaceRange *pBaseFace,
                       FaceCellsTable *pFaceCellsTable,
					   DebugAndPerfVars *pDpVars, int32_t faceIdx) {
	Result result = STUC_NOT_SET;
	FaceBounds *pFaceBounds = 
		&idxFaceCells(pFaceCellsTable, faceIdx, pMVars->inFaceRange.start)->faceBounds;
	for (int32_t j = pFaceBounds->min.d[1]; j <= pFaceBounds->max.d[1]; ++j) {
		for (int32_t k = pFaceBounds->min.d[0]; k <= pFaceBounds->max.d[0]; ++k) {
			STUC_ASSERT("", k < 2048 && k > -2048 && j < 2048 && j > -2048);
			V2_F32 fTileMin = {k, j};
			V2_I32 tile = {k, j};
			result = stucMapToSingleFace(pMVars, pFaceCellsTable, pDpVars,
			                             fTileMin, tile, *pBaseFace);
			if (result != STUC_SUCCESS) {
				return result;
			}
		}
	}
	return result;
}

static
void destroyMappingTables(StucAlloc *pAlloc, LocalTables *pLocalTables) {
	for (int32_t i = 0; i < pLocalTables->vertTableSize; ++i) {
		LocalVert *pEntry = pLocalTables->pVertTable + i;
		if (!pEntry->cornerSize) {
			continue;
		}
		pEntry = pEntry->pNext;
		while (pEntry) {
			LocalVert *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		};
	}
	for (int32_t i = 0; i < pLocalTables->edgeTableSize; ++i) {
		LocalEdge *pEntry = pLocalTables->pEdgeTable + i;
		if (!pEntry->cornerCount) {
			continue;
		}
		pEntry = pEntry->pNext;
		while (pEntry) {
			LocalEdge *pNextEntry = pEntry->pNext;
			pAlloc->pFree(pEntry);
			pEntry = pNextEntry;
		};
	}
	pAlloc->pFree(pLocalTables->pVertTable);
	pAlloc->pFree(pLocalTables->pEdgeTable);
}

void stucMapToJobMesh(void *pVarsPtr) {
	//CLOCK_INIT;
	Result result = STUC_NOT_SET;
	SendOffArgs *pSend = pVarsPtr;
	MappingJobVars vars = {0};
	vars.pContext = pSend->pContext;
	vars.alloc = pSend->pContext->alloc;
	vars.pEdgeVerts = pSend->pEdgeVerts;
	vars.id = pSend->id;
	vars.borderTable.size = pSend->borderTable.size;
	vars.mesh = pSend->mesh;
	vars.pMap = pSend->pMap;
	vars.pCommonAttribList = pSend->pCommonAttribList;
	vars.pInFaces = pSend->pInFaces;
	vars.getInFaces = pSend->getInFaces;
	vars.wScale = pSend->wScale;
	vars.inFaceOffset = pSend->inFaceOffset;
	vars.maskIdx = pSend->maskIdx;
	vars.inFaceRange = pSend->inFaceRange;
	int32_t inFaceRangeSize = vars.inFaceRange.end - vars.inFaceRange.start;
	//CLOCK_START;
	FaceCellsTable faceCellsTable = {0};
	int32_t averageMapFacesPerFace = 0;
	getEncasingCells(&vars.alloc, vars.pMap, vars.inFaceRange, &vars.mesh,
	                 &faceCellsTable, vars.maskIdx, &averageMapFacesPerFace);
	//CLOCK_STOP("Get Encasing Cells Time");
	//CLOCK_START;
	vars.bufSize = inFaceRangeSize + faceCellsTable.cellFacesTotal;
	allocBufMeshAndTables(&vars, &faceCellsTable);
	//CLOCK_STOP("Alloc buffers and tables time");
	DebugAndPerfVars dpVars = {0};
	vars.pDpVars = &dpVars;
	//uint64_t mappingTime, copySingleTime;
	//copySingleTime = 0;
	//mappingTime = 0;
	//CLOCK_START;
	//int32_t *pCellFaces = 
	//	vars.alloc.pMalloc(sizeof(int32_t) * faceCellsTable.cellFacesMax);
	//CLOCK_STOP("Alloc cell faces");
	//int64_t linearizeTime = 0;
	if (vars.getInFaces) {
		vars.inFaceSize = 8;
		vars.pInFaces = vars.alloc.pCalloc(vars.inFaceSize, sizeof(InFaceArr));
	}
	printf("Starting face loop\n");
	for (int32_t i = vars.inFaceRange.start; i < vars.inFaceRange.end; ++i) {
		printf("face is %d, face mat is %d, maskIdx is %d", i, vars.mesh.pMatIdx[i], vars.maskIdx);
		if (vars.maskIdx != -1 && vars.mesh.pMatIdx &&
		    vars.mesh.pMatIdx[i] != vars.maskIdx) {
			printf("   -   skipping face\n");
			continue;
		}
		printf("\n");
		// copy faces over to a new contiguous array
		//CLOCK_START;
		//stucLinearizeCellFaces(faceCellsTable.pFaceCells, pCellFaces, i);
		//CLOCK_STOP_NO_PRINT;
		//linearizeTime += CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;
		FaceRange baseFace = {0};
		baseFace.start = vars.mesh.core.pFaces[i];
		baseFace.end = vars.mesh.core.pFaces[i + 1];
		baseFace.size = baseFace.end - baseFace.start;
		baseFace.idx = i;
		vars.tbn = buildFaceTbn(baseFace, &vars.mesh, NULL);
		//vars.tbnInv = mat3x3Invert(&vars.tbn);
		FaceTriangulated faceTris = {0};
		if (baseFace.size > 4) {
			//TODO reimplement at some point
			// disabled cause current triangulation method is bad
			//faceTris = triangulateFace(vars.alloc, baseFace, vars.mesh.pUvs,
			                           //NULL, 1);
		}
		if (baseFace.size <= 4) {
			//face is a quad, or a tri
			result = mapPerTile(&vars, &baseFace, &faceCellsTable,
			                    &dpVars, i);
		}
		else {
			result = STUC_SUCCESS;
			//face is an ngon. ngons are processed per tri
			//TODO re-enable when triangulation method is improved.
			//how will extra in faces be handled (given we're splitting
			// a face into tris). Will probably need to merge the resulting
			//clipped faces into a single one in this func?
			/*
			for (int32_t j = 0; j < faceTris.triCount; ++j) {
				int32_t triFaceStart = j * 3;
				baseFace.start = faceTris.pCorners[triFaceStart];
				baseFace.end = faceTris.pCorners[triFaceStart + 2];
				baseFace.size = baseFace.end - baseFace.start;
				result = mapPerTile(&vars, &baseFace, &faceCellsTable,
				                    &dpVars, i);
				if (result != STUC_SUCCESS) {
					break;
				}
			}
			*/
		}
		if (faceTris.pCorners) {
			vars.alloc.pFree(faceTris.pCorners);
			faceTris.pCorners = NULL;
		}
		//CLOCK_STOP_NO_PRINT;
		//mappingTime += CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;
		FaceCells *pFaceCellsEntry = idxFaceCells(&faceCellsTable, i, vars.inFaceRange.start);
		stucDestroyFaceCellsEntry(&vars.alloc, pFaceCellsEntry);
		//CLOCK_STOP_NO_PRINT;
		//linearizeTime += CLOCK_TIME_DIFF(start, stop);
		if (result != STUC_SUCCESS) {
			break;
		}
	}
	printf("Finished face loop\n");
	//printf("Linearize time: %lu\nMappingTime: %lu\n", linearizeTime, mappingTime);
	//vars.alloc.pFree(pCellFaces);
	bool empty = !(vars.bufMesh.mesh.core.faceCount || vars.bufMesh.borderFaceCount);
	if (result == STUC_SUCCESS && !empty) {
		bufMeshSetLastFaces(&vars.alloc, &vars.bufMesh, &dpVars);
		pSend->reallocTime = dpVars.reallocTime;
		pSend->bufSize = vars.bufSize;
		pSend->rawBufSize = vars.rawBufSize;
		pSend->finalBufSize = vars.bufMesh.mesh.faceBufSize;
		//STUC_ASSERT("", !(!vars.borderTable.pTable ^ !vars.bufMesh.borderFaceCount));
		STUC_ASSERT("", vars.borderTable.pTable != NULL);
		printf("borderTable %d\n", vars.borderTable.pTable != NULL);
		pSend->borderTable.pTable = vars.borderTable.pTable;
		pSend->bufMesh = vars.bufMesh;
		pSend->pInFaces = vars.pInFaces;
	}
	else if (empty) {
		result = STUC_SUCCESS;
	}
	destroyMappingTables(&vars.alloc, &vars.localTables);
	stucDestroyFaceCellsTable(&vars.alloc, &faceCellsTable);
	pSend->result = result;
	pSend->pContext->threadPool.pMutexLock(pSend->pContext->pThreadPoolHandle,
	                                       pSend->pMutex);
	STUC_ASSERT("", pSend->bufSize > 0 || empty);
	printf("Average Faces Not Skipped: %d\n", dpVars.facesNotSkipped / inFaceRangeSize);
	printf("Average total Faces comped: %d\n", dpVars.totalFacesComp / inFaceRangeSize);
	printf("Average map faces per face: %d\n", averageMapFacesPerFace);
	--*pSend->pActiveJobs;
	pSend->pContext->threadPool.pMutexUnlock(pSend->pContext->pThreadPoolHandle,
	                                         pSend->pMutex);
}
