#include <string.h>
#include <stdio.h>

#include <RUVM.h>
#include <MapToJobMesh.h>
#include <MapFile.h>
#include <Context.h>
#include <Clock.h>
#include <AttribUtils.h>
#include <Utils.h>
#include <Error.h>

static
void getEncasingCells(RuvmAlloc *pAlloc, RuvmMap pMap,
                      Mesh *pMeshIn, FaceCellsTable *pFaceCellsTable,
					  int32_t *pAverageMapFacesPerFace) {
	*pAverageMapFacesPerFace = 0;
	ruvmInitFaceCellsTable(pAlloc, pFaceCellsTable, pMeshIn->mesh.faceCount);
	QuadTreeSearch searchState = {0};
	ruvmInitQuadTreeSearch(pAlloc, pMap, &searchState);
	for (int32_t i = 0; i < pMeshIn->mesh.faceCount; ++i) {
		FaceRange faceInfo = {0};
		faceInfo.index = i;
		faceInfo.start = pMeshIn->mesh.pFaces[i];
		faceInfo.end = pMeshIn->mesh.pFaces[i + 1];
		faceInfo.size = faceInfo.end - faceInfo.start;
		FaceBounds faceBounds = {0};
		getFaceBounds(&faceBounds, pMeshIn->pUvs, faceInfo);
		faceBounds.fMinSmall = faceBounds.fMin;
		faceBounds.fMaxSmall = faceBounds.fMax;
		faceBounds.min = v2FloorAssign(&faceBounds.fMin);
		faceBounds.max = v2FloorAssign(&faceBounds.fMax);
		_(&faceBounds.fMax V2ADDEQLS 1.0f);
		V2_F32 *pVertBuf = pAlloc->pMalloc(sizeof(V2_F32) * faceInfo.size);
		for (int32_t j = 0; j < faceInfo.size; ++j) {
			pVertBuf[j] = pMeshIn->pUvs[faceInfo.start + j];
		}
		ruvmGetCellsForSingleFace(&searchState, faceInfo.size, pVertBuf,
			                      pFaceCellsTable, &faceBounds, i);
		pAlloc->pFree(pVertBuf);
		*pAverageMapFacesPerFace += pFaceCellsTable->pFaceCells[i].faceSize;
		//printf("Total cell amount: %d\n", faceCellsInfo[i].cellSize);
	}
	*pAverageMapFacesPerFace /= pMeshIn->mesh.faceCount;
	ruvmDestroyQuadTreeSearch(&searchState);
}

static
void allocBufMesh(MappingJobVars *pVars, int32_t loopBufSize) {
	RuvmMap pMap = pVars->pMap;
	RuvmMesh *pMeshIn = &pVars->mesh.mesh;
	Mesh *pMesh = asMesh(&pVars->bufMesh);
	RuvmAlloc *pAlloc = &pVars->alloc;
	pMesh->faceBufSize = pVars->bufSize;
	pMesh->loopBufSize = pVars->loopBufSize;
	pMesh->edgeBufSize = pVars->loopBufSize;
	pMesh->vertBufSize = pVars->bufSize;
	pMesh->mesh.pFaces = pAlloc->pMalloc(sizeof(int32_t) * pMesh->faceBufSize);
	pMesh->mesh.pLoops = pAlloc->pMalloc(sizeof(int32_t) * pMesh->loopBufSize);
	pMesh->mesh.pEdges = pAlloc->pMalloc(sizeof(int32_t) * pMesh->edgeBufSize);
	allocAttribs(pAlloc, &pMesh->mesh.faceAttribs, &pMeshIn->faceAttribs,
	             &pMap->mesh.mesh.faceAttribs, pMesh->faceBufSize);
	allocAttribs(pAlloc, &pMesh->mesh.loopAttribs, &pMeshIn->loopAttribs,
	             &pMap->mesh.mesh.loopAttribs, pMesh->loopBufSize);
	allocAttribs(pAlloc, &pMesh->mesh.edgeAttribs, &pMeshIn->edgeAttribs,
	             &pMap->mesh.mesh.edgeAttribs, pMesh->edgeBufSize);
	allocAttribs(pAlloc, &pMesh->mesh.vertAttribs, &pMeshIn->vertAttribs,
	             &pMap->mesh.mesh.vertAttribs, pMesh->vertBufSize);
	pMesh->pUvAttrib = getAttrib("UVMap", &pMesh->mesh.loopAttribs);
	pMesh->pUvs = pMesh->pUvAttrib->pData;
	pMesh->pNormalAttrib = getAttrib("normal", &pMesh->mesh.loopAttribs);
	pMesh->pNormals = pMesh->pNormalAttrib->pData;
	pMesh->pVertAttrib = getAttrib("position", &pMesh->mesh.vertAttribs);
	pMesh->pVerts = pMesh->pVertAttrib->pData;
	pMesh->loopBufSize = loopBufSize;
}

static
void allocBufMeshAndTables(MappingJobVars *pVars,
                           FaceCellsTable *pFaceCellsTable) {
	//struct timeval start, stop;
	RuvmAlloc *pAlloc = &pVars->alloc;
	pVars->rawBufSize = pVars->bufSize;
	pVars->bufSize = pVars->bufSize / 20 + 2; //Add 2 incase it truncs to 0
	int32_t loopBufSize = pVars->bufSize * 2;
	pVars->loopBufSize = loopBufSize;
	pVars->borderTable.pTable =
		pAlloc->pCalloc(pVars->borderTable.size, sizeof(BorderBucket));
	allocBufMesh(pVars, loopBufSize);
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
Mat3x3 buildFaceTbn(FaceRange face, Mesh *pMesh) {
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
Result mapPerTile(MappingJobVars *pMVars, FaceRange *pBaseFace,
                       FaceCellsTable *pFaceCellsTable,
					   DebugAndPerfVars *pDpVars, int32_t rawFace) {
	Result result = RUVM_NOT_SET;
	FaceBounds *pFaceBounds = &pFaceCellsTable->pFaceCells[rawFace].faceBounds;
	for (int32_t j = pFaceBounds->min.d[1]; j <= pFaceBounds->max.d[1]; ++j) {
		for (int32_t k = pFaceBounds->min.d[0]; k <= pFaceBounds->max.d[0]; ++k) {
			V2_F32 fTileMin = {k, j};
			int32_t tile = k + (j * pFaceBounds->max.d[0]);
			result = ruvmMapToSingleFace(pMVars, pFaceCellsTable, pDpVars,
			                             fTileMin, tile, *pBaseFace);
			if (result != RUVM_SUCCESS) {
				return result;
			}
		}
	}
	return result;
}

static
void destroyMappingTables(RuvmAlloc *pAlloc, LocalTables *pLocalTables) {
	for (int32_t i = 0; i < pLocalTables->vertTableSize; ++i) {
		LocalVert *pEntry = pLocalTables->pVertTable + i;
		if (!pEntry->loopSize) {
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
		if (!pEntry->loopCount) {
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

void ruvmMapToJobMesh(void *pVarsPtr) {
	//CLOCK_INIT;
	Result result = RUVM_NOT_SET;
	FaceCellsTable faceCellsTable = {0};
	SendOffArgs *pSend = pVarsPtr;
	MappingJobVars vars = {0};
	vars.pEdgeVerts = pSend->pEdgeVerts;
	vars.alloc = pSend->pContext->alloc;
	vars.id = pSend->id;
	vars.borderTable.size = pSend->borderTable.size;
	vars.mesh = pSend->mesh;
	vars.pMap = pSend->pMap;
	vars.pCommonAttribList = pSend->pCommonAttribList;
	int32_t averageMapFacesPerFace = 0;
	//CLOCK_START;
	getEncasingCells(&vars.alloc, vars.pMap, &vars.mesh, &faceCellsTable,
	                 &averageMapFacesPerFace);
	//CLOCK_STOP("Get Encasing Cells Time");
	//CLOCK_START;
	vars.bufSize = vars.mesh.mesh.faceCount + faceCellsTable.cellFacesTotal;
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
	for (int32_t i = 0; i < vars.mesh.mesh.faceCount; ++i) {
		// copy faces over to a new contiguous array
		//CLOCK_START;
		//ruvmLinearizeCellFaces(faceCellsTable.pFaceCells, pCellFaces, i);
		//CLOCK_STOP_NO_PRINT;
		//linearizeTime += CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;
		FaceRange baseFace = {0};
		baseFace.start = vars.mesh.mesh.pFaces[i];
		baseFace.end = vars.mesh.mesh.pFaces[i + 1];
		baseFace.size = baseFace.end - baseFace.start;
		baseFace.index = i;
		vars.tbn = buildFaceTbn(baseFace, &vars.mesh);
		FaceTriangulated faceTris = {0};
		if (baseFace.size > 3) {
			faceTris = triangulateFace(vars.alloc, baseFace, vars.mesh.pUvs,
			                           NULL, 1);
		}
		if (baseFace.size <= 4) {
			//face is a quad, or a tri
			result = mapPerTile(&vars, &baseFace, &faceCellsTable,
			                    &dpVars, i);
		}
		else {
			//face is an ngon. ngons are processed per tri
			for (int32_t j = 0; j < faceTris.triCount; ++j) {
				int32_t triFaceStart = faceTris.pTris[j];
				baseFace.start = faceTris.pLoops[triFaceStart];
				baseFace.end = faceTris.pLoops[triFaceStart + 2];
				baseFace.size = baseFace.end - baseFace.start;
				result = mapPerTile(&vars, &baseFace, &faceCellsTable,
				                    &dpVars, i);
				if (result != RUVM_SUCCESS) {
					break;
				}
			}
		}
		if (faceTris.pTris) {
			vars.alloc.pFree(faceTris.pTris);
			faceTris.pTris = NULL;
		}
		if (faceTris.pLoops) {
			vars.alloc.pFree(faceTris.pLoops);
			faceTris.pLoops = NULL;
		}
		//CLOCK_STOP_NO_PRINT;
		//mappingTime += CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;
		vars.alloc.pFree(faceCellsTable.pFaceCells[i].pCells);
		vars.alloc.pFree(faceCellsTable.pFaceCells[i].pCellType);
		vars.alloc.pFree(faceCellsTable.pFaceCells[i].pRanges);
		//CLOCK_STOP_NO_PRINT;
		//linearizeTime += CLOCK_TIME_DIFF(start, stop);
		if (result != RUVM_SUCCESS) {
			break;
		}
	}
	//printf("Linearize time: %lu\nMappingTime: %lu\n", linearizeTime, mappingTime);
	//vars.alloc.pFree(pCellFaces);
	if (result == RUVM_SUCCESS) {
		bufMeshSetLastFaces(&vars.alloc, &vars.bufMesh, &dpVars);
		pSend->reallocTime = dpVars.reallocTime;
		pSend->bufSize = vars.bufSize;
		pSend->rawBufSize = vars.rawBufSize;
		pSend->finalBufSize = asMesh(&vars.bufMesh)->faceBufSize;
		RUVM_ASSERT("", !(!vars.borderTable.pTable ^ !vars.bufMesh.borderFaceCount));
		RUVM_ASSERT("", vars.borderTable.pTable != NULL);
		printf("borderTable %d\n", vars.borderTable.pTable != NULL);
		pSend->borderTable.pTable = vars.borderTable.pTable;
		pSend->bufMesh = vars.bufMesh;
	}
	destroyMappingTables(&vars.alloc, &vars.localTables);
	ruvmDestroyFaceCellsTable(&vars.alloc, &faceCellsTable);
	pSend->pContext->threadPool.pMutexLock(pSend->pContext->pThreadPoolHandle,
	                                       pSend->pMutex);
	printf("Average Faces Not Skipped: %d\n", dpVars.facesNotSkipped / vars.mesh.mesh.faceCount);
	printf("Average total Faces comped: %d\n", dpVars.totalFacesComp / vars.mesh.mesh.faceCount);
	printf("Average map faces per face: %d\n", averageMapFacesPerFace);
	++*pSend->pJobsCompleted;
	if (result != RUVM_SUCCESS || *pSend->pResult == RUVM_NOT_SET) {
		*pSend->pResult = result;
	}
	pSend->pContext->threadPool.pMutexUnlock(pSend->pContext->pThreadPoolHandle,
	                                         pSend->pMutex);
}
