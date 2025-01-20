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
	Mesh *srcs[2] = {(Mesh *)pMeshIn, &pMap->mesh};
	allocAttribsFromMeshArr(&pVars->alloc, pMesh, 2, srcs, true);
	//create attribs for deffering offset transform until combine stage
	//make these bufmesh only attribs a part of the special attribs,
	//you'll need to expand the flags param to a 16 bits
	pMesh->mesh.loopAttribs.size += 3;
	pMesh->mesh.loopAttribs.pArr =
		pVars->alloc.pRealloc(pMesh->mesh.loopAttribs.pArr,
	                          pMesh->mesh.loopAttribs.size * sizeof(Attrib));
	Attrib *pWAttrib = pMesh->mesh.loopAttribs.pArr + pMesh->mesh.loopAttribs.count;
	initAttrib(&pVars->alloc, pWAttrib, "RuvmW", pMesh->loopBufSize, false,
	           RUVM_ATTRIB_ORIGIN_IGNORE, RUVM_ATTRIB_F32);
	Attrib *pInNormalAttrib = pMesh->mesh.loopAttribs.pArr + pMesh->mesh.loopAttribs.count + 1;
	initAttrib(&pVars->alloc, pInNormalAttrib, "RuvmInNormal", pMesh->loopBufSize, false,
		RUVM_ATTRIB_ORIGIN_IGNORE, RUVM_ATTRIB_V3_F32);
	Attrib *pInTangentAttrib = pMesh->mesh.loopAttribs.pArr + pMesh->mesh.loopAttribs.count + 2;
	initAttrib(&pVars->alloc, pInTangentAttrib, "RuvmInTangent", pMesh->loopBufSize, false,
		RUVM_ATTRIB_ORIGIN_IGNORE, RUVM_ATTRIB_V3_F32);
	Attrib *pAlphaAttrib = pMesh->mesh.loopAttribs.pArr + pMesh->mesh.loopAttribs.count + 3;
	initAttrib(&pVars->alloc, pAlphaAttrib, "RuvmAlpha", pMesh->loopBufSize, false,
		RUVM_ATTRIB_ORIGIN_IGNORE, RUVM_ATTRIB_F32);
	Attrib *pInTSignAttrib = pMesh->mesh.loopAttribs.pArr + pMesh->mesh.loopAttribs.count + 4;
	initAttrib(&pVars->alloc, pInTSignAttrib, "RuvmInTSign", pMesh->loopBufSize, false,
		RUVM_ATTRIB_ORIGIN_IGNORE, RUVM_ATTRIB_F32);
	pMesh->mesh.loopAttribs.count += 5;
	pVars->bufMesh.pWAttrib = pWAttrib;
	pVars->bufMesh.pW = pWAttrib->pData;
	pVars->bufMesh.pInNormalAttrib = pInNormalAttrib;
	pVars->bufMesh.pInNormal = pInNormalAttrib->pData;
	pVars->bufMesh.pInTangentAttrib = pInTangentAttrib;
	pVars->bufMesh.pInTangent = pInTangentAttrib->pData;
	pVars->bufMesh.pAlphaAttrib = pAlphaAttrib;
	pVars->bufMesh.pAlpha = pAlphaAttrib->pData;
	pVars->bufMesh.pInTSignAttrib = pInTSignAttrib;
	pVars->bufMesh.pInTSign = pInTSignAttrib->pData;

	//generalise this
	pMesh->pUvAttrib = getAttrib("UVMap", &pMesh->mesh.loopAttribs);
	pMesh->pUvs = pMesh->pUvAttrib->pData;
	pMesh->pNormalAttrib = getAttrib("normal", &pMesh->mesh.loopAttribs);
	pMesh->pNormals = pMesh->pNormalAttrib->pData;
	pMesh->pVertAttrib = getAttrib("position", &pMesh->mesh.vertAttribs);
	pMesh->pVerts = pMesh->pVertAttrib->pData;
	pMesh->loopBufSize = loopBufSize;
	pMesh->mesh.type.type = RUVM_OBJECT_DATA_MESH_BUF;
	pMesh->pWScaleAttrib = getAttrib("RuvmWScale", &pMesh->mesh.vertAttribs);
	if (pMesh->pWScaleAttrib) {
		pMesh->pWScale = pMesh->pWScaleAttrib->pData;
		//temp override to prevent it from being added to out mesh
		//generaliee this in an 'originOverride' func or something
		pMesh->pWScaleAttrib->origin = RUVM_ATTRIB_ORIGIN_IGNORE;
	}
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
Result mapPerTile(MappingJobVars *pMVars, FaceRange *pBaseFace,
                       FaceCellsTable *pFaceCellsTable,
					   DebugAndPerfVars *pDpVars, int32_t rawFace) {
	Result result = RUVM_NOT_SET;
	FaceBounds *pFaceBounds = &pFaceCellsTable->pFaceCells[rawFace].faceBounds;
	for (int32_t j = pFaceBounds->min.d[1]; j <= pFaceBounds->max.d[1]; ++j) {
		for (int32_t k = pFaceBounds->min.d[0]; k <= pFaceBounds->max.d[0]; ++k) {
			RUVM_ASSERT("", k < 2048 && k > -2048 && j < 2048 && j > -2048);
			V2_F32 fTileMin = {k, j};
			V2_I32 tile = {k, j};
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
	SendOffArgs *pSend = pVarsPtr;
	MappingJobVars vars = {0};
	vars.pEdgeVerts = pSend->pEdgeVerts;
	vars.alloc = pSend->pContext->alloc;
	vars.id = pSend->id;
	vars.borderTable.size = pSend->borderTable.size;
	vars.mesh = pSend->mesh;
	vars.pMap = pSend->pMap;
	vars.pCommonAttribList = pSend->pCommonAttribList;
	vars.pInFaces = pSend->pInFaces;
	vars.getInFaces = pSend->getInFaces;
	vars.wScale = pSend->wScale;
	//CLOCK_START;
	FaceCellsTable faceCellsTable = {0};
	int32_t averageMapFacesPerFace = 0;
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
	if (vars.getInFaces) {
		vars.inFaceSize = 8;
		vars.pInFaces = vars.alloc.pCalloc(vars.inFaceSize, sizeof(InFaceArr));
	}
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
		vars.tbn = buildFaceTbn(baseFace, &vars.mesh, NULL);
		//vars.tbnInv = mat3x3Invert(&vars.tbn);
		FaceTriangulated faceTris = {0};
		if (baseFace.size > 4) {
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
				int32_t triFaceStart = j * 3;
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
		RUVM_ASSERT("", result == RUVM_SUCCESS);
		if (faceTris.pLoops) {
			vars.alloc.pFree(faceTris.pLoops);
			faceTris.pLoops = NULL;
		}
		//CLOCK_STOP_NO_PRINT;
		//mappingTime += CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;
		ruvmDestroyFaceCellsEntry(&vars.alloc, i, &faceCellsTable);
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
		pSend->pInFaces = vars.pInFaces;
	}
	destroyMappingTables(&vars.alloc, &vars.localTables);
	ruvmDestroyFaceCellsTable(&vars.alloc, &faceCellsTable);
	pSend->pContext->threadPool.pMutexLock(pSend->pContext->pThreadPoolHandle,
	                                       pSend->pMutex);
	RUVM_ASSERT("", pSend->bufSize > 0);
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
