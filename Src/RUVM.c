#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

#include <Io.h>
#include <MapToJobMesh.h>
#include <CombineJobMeshes.h>
#include <MapFile.h>
#include <Context.h>
#include <Alloc.h>
#include <ThreadPool.h>
#include <RUVM.h>
#include <Clock.h>
#include <AttribUtils.h>
#include <Utils.h>
#include <ImageUtils.h>
#include <Error.h>

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
//TODO add a cache (hash table?) for triangulated in faces, as your going to need to reference
//their edges when merging border faces
//TODO add user define void * args to custom callbacks
//TODO add the ability to open map files in dcc, to make edits to mesh, USGs etc

static
void ruvmSetTypeDefaultConfig(RuvmContext pContext) {
	RuvmTypeDefaultConfig config = {0};
	pContext->typeDefaults = config;
}

static
void setDefaultStageReport(RuvmContext pContext) {
	pContext->stageReport.outOf = 50,
	pContext->stageReport.pBegin = stageBegin;
	pContext->stageReport.pProgress = stageProgress;
	pContext->stageReport.pEnd = stageEnd;
}

RuvmResult ruvmContextInit(RuvmContext *pContext, RuvmAlloc *pAlloc,
                           RuvmThreadPool *pThreadPool, RuvmIo *pIo,
					       RuvmTypeDefaultConfig *pTypeDefaultConfig,
                           RuvmStageReport *pStageReport) {
	RuvmAlloc alloc;
	if (pAlloc) {
		ruvmAllocSetCustom(&alloc, pAlloc);
	}
	else {
		ruvmAllocSetDefault(&alloc);
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
	(*pContext)->threadPool.pInit(&(*pContext)->pThreadPoolHandle,
	                              &(*pContext)->threadCount,
	                              &(*pContext)->alloc);
	if (pTypeDefaultConfig) {
		(*pContext)->typeDefaults = *pTypeDefaultConfig;
	}
	else {
		ruvmSetTypeDefaultConfig(*pContext);
	}
	if (pStageReport) {
		(*pContext)->stageReport = *pStageReport;
	}
	else {
		setDefaultStageReport(*pContext);
	}
	return RUVM_SUCCESS;
}

RuvmResult ruvmContextDestroy(RuvmContext pContext) {
	pContext->threadPool.pDestroy(pContext->pThreadPoolHandle);
	pContext->alloc.pFree(pContext);
	return RUVM_SUCCESS;
}

RuvmResult ruvmMapFileExport(RuvmContext pContext, const char *pName,
                             int32_t objCount, RuvmObject* pObjArr,
                             int32_t usgCount, RuvmUsg* pUsgArr,
                             RuvmAttribIndexedArr indexedAttribs) {
	return ruvmWriteRuvmFile(pContext, pName, objCount, pObjArr,
	                         usgCount, pUsgArr, indexedAttribs);
}

//TODO replace these with RuvmUsg and RuvmObj arr structs, that combine arr and count
RuvmResult ruvmMapFileLoadForEdit(RuvmContext pContext, char *filePath,
                                  int32_t *pObjCount, RuvmObject **ppObjArr,
                                  int32_t *pUsgCount, RuvmUsg **ppUsgArr,
                                  int32_t *pFlatCutoffCount, RuvmObject **ppFlatCutoffArr,
                                  RuvmAttribIndexedArr *pIndexedAttribs) {
	return ruvmLoadRuvmFile(pContext, filePath, pObjCount, ppObjArr, pUsgCount,
	                        ppUsgArr, pFlatCutoffCount, ppFlatCutoffArr, true, pIndexedAttribs);
}

RuvmResult ruvmMapFileLoad(RuvmContext pContext, RuvmMap *pMapHandle,
                           char *filePath) {
	RuvmResult status = RUVM_NOT_SET;
	RuvmMap pMap = pContext->alloc.pCalloc(1, sizeof(MapFile));
	int32_t objCount = 0;
	RuvmObject *pObjArr = NULL;
	RuvmUsg *pUsgArr = NULL;
	int32_t flatCutoffCount = 0;
	RuvmObject *pFlatCutoffArr = NULL;
	status = ruvmLoadRuvmFile(pContext, filePath, &objCount, &pObjArr,
	                          &pMap->usgArr.count, &pUsgArr, &flatCutoffCount,
	                          &pFlatCutoffArr, false, &pMap->indexedAttribs);
	if (status != RUVM_SUCCESS) {
		return status;
	}

	for (int32_t i = 0; i < objCount; ++i) {
		setSpecialAttribs(pObjArr[i].pData, 0xae); //10101110 - all except for preserve
		applyObjTransform(pObjArr + i);
	}
	pMap->mesh.mesh.type.type = RUVM_OBJECT_DATA_MESH_INTERN;
	mergeObjArr(pContext, &pMap->mesh, objCount, pObjArr, false);
	setSpecialAttribs(&pMap->mesh, 0xae);
	//TODO some form of heap corruption when many objects
	//test with address sanitizer on CircuitPieces.ruvm
	destroyObjArr(pContext, objCount, pObjArr);

	if (pMap->mesh.pUvAttrib) {
		//TODO as with all special attributes, allow user to define what should be considered
		//     the primary UV channel. This especially important for integration with other DCCs
		if (!strncmp(pMap->mesh.pUvAttrib->name, "UVMap", RUVM_ATTRIB_NAME_MAX_LEN)) {
			char newName[RUVM_ATTRIB_NAME_MAX_LEN] = "Map_UVMap";
			memcpy(pMap->mesh.pUvAttrib->name, newName, RUVM_ATTRIB_NAME_MAX_LEN);
		}
	}

	//set loop attribs to interpolate by default
	//TODO make this an option in ui, even for non common attribs
	for (int32_t i = 0; i < pMap->mesh.mesh.loopAttribs.count; ++i) {
		pMap->mesh.mesh.loopAttribs.pArr[i].interpolate = true;
	}

	//the quadtree is created before USGs are assigned to verts,
	//as the tree's used to speed up the process
	printf("File loaded. Creating quad tree\n");
	ruvmCreateQuadTree(pContext, pMap);

	if (pMap->usgArr.count) {
		pMap->usgArr.pArr = pContext->alloc.pCalloc(pMap->usgArr.count, sizeof(Usg));
		for (int32_t i = 0; i < pMap->usgArr.count; ++i) {
			setSpecialAttribs(pUsgArr[i].obj.pData, 0x02); //000010 - set only vert pos
			pMap->usgArr.pArr[i].origin = *(V2_F32 *)&pUsgArr[i].obj.transform.d[3];
			pMap->usgArr.pArr[i].pMesh = pUsgArr[i].obj.pData;
			applyObjTransform(&pUsgArr[i].obj);
			if (pUsgArr[i].pFlatCutoff) {
				pMap->usgArr.pArr[i].pFlatCutoff = pUsgArr[i].pFlatCutoff->pData;
				setSpecialAttribs(pUsgArr[i].pFlatCutoff->pData, 0x02); //000010 - set only vert pos
				applyObjTransform(pUsgArr[i].pFlatCutoff);
			}
		}
		allocUsgSquaresMesh(&pContext->alloc, pMap);
		fillUsgSquaresMesh(pMap, pUsgArr);
		assignUsgsToVerts(&pContext->alloc, pMap, pUsgArr);
		pMap->usgArr.pMemArr = pUsgArr;
	}

	*pMapHandle = pMap;
	//TODO add proper checks, and return RUVM_ERROR if fails.
	//Do for all public functions (or internal ones as well)
	return RUVM_SUCCESS;
}

RuvmResult ruvmMapFileUnload(RuvmContext pContext, RuvmMap pMap) {
	ruvmDestroyQuadTree(pContext, &pMap->quadTree);
	ruvmMeshDestroy(pContext, &pMap->mesh.mesh);
	pContext->alloc.pFree(pMap);
	return RUVM_SUCCESS;
}

static
void initCommonAttrib(RuvmContext pContext, RuvmCommonAttrib *pEntry,
                      RuvmAttrib *pAttrib) {
	memcpy(pEntry->name, pAttrib->name, RUVM_ATTRIB_NAME_MAX_LEN);
	RuvmTypeDefault *pDefault = 
		getTypeDefaultConfig(&pContext->typeDefaults, pAttrib->type);
	pEntry->blendConfig = pDefault->blendConfig;
}

static
void getCommonAttribs(RuvmContext pContext, AttribArray *pMapAttribs,
					  AttribArray *pMeshAttribs,
					  int32_t *pCommonAttribCount,
					  RuvmCommonAttrib **ppCommonAttribs) {
	if (!pMeshAttribs || !pMapAttribs) {
		return;
	}
	int32_t count = 0;
	for (int32_t i = 0; i < pMeshAttribs->count; ++i) {
		for (int32_t j = 0; j < pMapAttribs->count; ++j) {
			if (!strncmp(pMeshAttribs->pArr[i].name,
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
			if (!strncmp(pMeshAttribs->pArr[i].name,
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

//TODO handle edge case, where attribute share the same name,
//but have incompatible types. Such as a float and a string.
RuvmResult ruvmQueryCommonAttribs(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMesh,
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
	return RUVM_SUCCESS;
}

RuvmResult ruvmDestroyCommonAttribs(RuvmContext pContext,
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
	return RUVM_SUCCESS;
}

static
void sendOffJobs(RuvmContext pContext, RuvmMap pMap, SendOffArgs *pJobArgs,
                 int32_t *pJobsCompleted, Mesh *pMesh, void *pMutex,
                 EdgeVerts *pEdgeVerts, int8_t *pInVertTable,
				 RuvmCommonAttribList *pCommonAttribList, Result *pJobResult,
	             bool getInFaces, float wScale) {
	//struct timeval start, stop;
	//CLOCK_START;
	int32_t facesPerThread = pMesh->mesh.faceCount / pContext->threadCount;
	void *jobArgPtrs[MAX_THREADS];
	int32_t borderTableSize = pMap->mesh.mesh.faceCount / 5;
	printf("fromjobsendoff: BorderTableSize: %d\n", borderTableSize);
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		int32_t meshStart = facesPerThread * i;
		int32_t meshEnd = i == pContext->threadCount - 1 ?
			pMesh->mesh.faceCount : meshStart + facesPerThread;
		Mesh meshPart = *pMesh;
		meshPart.mesh.pFaces += meshStart;
		meshPart.mesh.faceCount = meshEnd - meshStart;
		pJobArgs[i].pInVertTable = pInVertTable;
		pJobArgs[i].pEdgeVerts = pEdgeVerts;
		pJobArgs[i].pMap = pMap;
		pJobArgs[i].borderTable.size = borderTableSize;
		pJobArgs[i].mesh = meshPart;
		pJobArgs[i].pJobsCompleted = pJobsCompleted;
		pJobArgs[i].id = i;
		pJobArgs[i].pContext = pContext;
		pJobArgs[i].pMutex = pMutex;
		pJobArgs[i].pCommonAttribList = pCommonAttribList;
		pJobArgs[i].pResult = pJobResult;
		pJobArgs[i].getInFaces = getInFaces;
		pJobArgs[i].wScale = wScale;
		jobArgPtrs[i] = pJobArgs + i;
	}
	pContext->threadPool.pJobStackPushJobs(pContext->pThreadPoolHandle,
	                                       pContext->threadCount,
	                                       ruvmMapToJobMesh, jobArgPtrs);
	//CLOCK_STOP("send off jobs");
}

static
void buildEdgeVertsTable(RuvmContext pContext, EdgeVerts **ppEdgeVerts,
                         RuvmMesh *pMesh) {
	*ppEdgeVerts = pContext->alloc.pMalloc(sizeof(EdgeVerts) * pMesh->edgeCount);
	memset(*ppEdgeVerts, -1, sizeof(EdgeVerts) * pMesh->edgeCount);
	for (int32_t i = 0; i < pMesh->loopCount; ++i) {
		int32_t edge = pMesh->pEdges[i];
		int32_t whichVert = (*ppEdgeVerts)[edge].verts[0] >= 0;
		(*ppEdgeVerts)[edge].verts[whichVert] = i;
	}
}

typedef struct {
	int32_t d[2];
	int32_t index;
} EdgeCache;

static
void addVertToTableEntry(Mesh *pMesh, FaceRange face, int32_t localLoop,
                         int32_t vert, int32_t edge, EdgeVerts *pEdgeVerts,
						 int8_t *pVertSeamTable, int8_t *pInVertTable,
						 EdgeCache *pEdgeCache, bool *pEdgeSeamTable) {
	//isSeam returns 2 if mesh border, and 1 if uv seam
	int32_t isSeam = checkIfEdgeIsSeam(edge, face, localLoop, pMesh,
									   pEdgeVerts);
	if (isSeam) {
		pVertSeamTable[vert] = isSeam;
		pEdgeSeamTable[edge] = true;
	}
	if (pInVertTable[vert] < 3 &&
		checkIfEdgeIsPreserve(pMesh, edge) &&
		pEdgeCache[vert].d[0] != edge + 1 &&
		pEdgeCache[vert].d[1] != edge + 1) {
		pInVertTable[vert]++;
		int32_t *pEdgeCacheIndex = &pEdgeCache[vert].index;
		pEdgeCache[vert].d[*pEdgeCacheIndex] = edge + 1;
		++*pEdgeCacheIndex;
	}
}

static
void buildVertTables(RuvmContext pContext, Mesh *pMesh,
					 int8_t **ppInVertTable, int8_t **ppVertSeamTable,
					 EdgeVerts *pEdgeVerts, bool **ppEdgeSeamTable) {
	*ppInVertTable = pContext->alloc.pCalloc(pMesh->mesh.vertCount, 1);
	*ppVertSeamTable = pContext->alloc.pCalloc(pMesh->mesh.vertCount, 1);
	*ppEdgeSeamTable = pContext->alloc.pCalloc(pMesh->mesh.edgeCount, 1);
	//TODO do we need to list number of unique preserve edges per vert?
	//I'm not doing so currently (hence why pEdgeCache is commented out),
	//and it seems to be working. (talking about split to pieces)
	EdgeCache *pEdgeCache =
		pContext->alloc.pCalloc(pMesh->mesh.vertCount, sizeof(EdgeCache));
	for (int32_t i = 0; i < pMesh->mesh.faceCount; ++i) {
		FaceRange face = {0};
		face.start = pMesh->mesh.pFaces[i];
		face.end = pMesh->mesh.pFaces[i + 1];
		face.size = face.end - face.start;
		face.index = i;
		for (int32_t j = 0; j < face.size; ++j) {
			int32_t loop = face.start + j;
			int32_t vert = pMesh->mesh.pLoops[loop];
			int32_t edge = pMesh->mesh.pEdges[loop];
			addVertToTableEntry(pMesh, face, j, vert, edge, pEdgeVerts,
			                    *ppVertSeamTable, *ppInVertTable, pEdgeCache,
			                    *ppEdgeSeamTable);
			int32_t prevj = j == 0 ? face.size - 1 : j - 1;
			int32_t prevEdge = pMesh->mesh.pEdges[face.start + prevj];
			addVertToTableEntry(pMesh, face, prevj, vert, prevEdge, pEdgeVerts,
			                    *ppVertSeamTable, *ppInVertTable, pEdgeCache,
			                    *ppEdgeSeamTable);
		}
	}
	pContext->alloc.pFree(pEdgeCache);
}

static
void setAttribOrigins(AttribArray *pAttribs, RuvmAttribOrigin origin) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		pAttribs->pArr[i].origin = origin;
	}
}

static
Result mapToMeshInternal(RuvmContext pContext, RuvmMap pMap, Mesh *pMeshIn,
                         RuvmMesh *pMeshOut, RuvmCommonAttribList *pCommonAttribList,
                         InFaceArr **ppInFaceTable, float wScale) {
	CLOCK_INIT;
	CLOCK_START;
	EdgeVerts *pEdgeVerts = {0};
	printf("EdgeCount: %d\n", pMeshIn->mesh.edgeCount);
	buildEdgeVertsTable(pContext, &pEdgeVerts, &pMeshIn->mesh);
	int8_t *pInVertTable;
	int8_t *pVertSeamTable;
	bool *pEdgeSeamTable;
	buildVertTables(pContext, pMeshIn, &pInVertTable,
	                &pVertSeamTable, pEdgeVerts, &pEdgeSeamTable);
	CLOCK_STOP("Edge Table Time");

	CLOCK_START;
	SendOffArgs jobArgs[MAX_THREADS] = {0};
	int32_t jobsCompleted = 0;
	Result jobResult = RUVM_NOT_SET;
	void *pMutex = NULL;
	pContext->threadPool.pMutexGet(pContext->pThreadPoolHandle, &pMutex);
	sendOffJobs(pContext, pMap, jobArgs, &jobsCompleted, pMeshIn, pMutex,
	            pEdgeVerts, pInVertTable, pCommonAttribList, &jobResult,
	            ppInFaceTable != NULL, wScale);
	CLOCK_STOP("Send Off Time");
	CLOCK_START;
	waitForJobs(pContext, &jobsCompleted, pMutex);
	pContext->threadPool.pMutexDestroy(pContext->pThreadPoolHandle, pMutex);
	CLOCK_STOP("Waiting Time");
	if (jobResult != RUVM_SUCCESS) {
		return jobResult;
	}
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		RUVM_ASSERT("", jobArgs[i].bufSize > 0);
	}

	CLOCK_START;
	Mesh meshOutWrap = {0};
	ruvmCombineJobMeshes(pContext, pMap, &meshOutWrap, jobArgs, pEdgeVerts,
	                     pVertSeamTable, pEdgeSeamTable, ppInFaceTable, wScale, pMeshIn);
	CLOCK_STOP("Combine time");
	pContext->alloc.pFree(pEdgeVerts);
	pContext->alloc.pFree(pInVertTable);
	pContext->alloc.pFree(pVertSeamTable);
	pContext->alloc.pFree(pEdgeSeamTable);
	CLOCK_START;
	reallocMeshToFit(&pContext->alloc, &meshOutWrap);
	*pMeshOut = meshOutWrap.mesh;
	CLOCK_STOP("Realloc time");
	return RUVM_SUCCESS;
}

static
void InFaceTableToHashTable(RuvmAlloc *pAlloc,
                            RuvmMap pMap, int32_t count, InFaceArr *pInFaceTable) {
	UsgInFace **ppHashTable = &pMap->usgArr.pInFaceTable;
	pMap->usgArr.tableSize = count * 2;
	*ppHashTable = pAlloc->pCalloc(pMap->usgArr.tableSize, sizeof(UsgInFace));
	for (int32_t i = 0; i < count; ++i) {
		for (int32_t j = 0; j < pInFaceTable[i].count; ++j) {
			uint32_t sum = pInFaceTable[i].usg + pInFaceTable[i].pArr[j];
			int32_t hash = ruvmFnvHash((uint8_t *)&sum, 4, pMap->usgArr.tableSize);
			UsgInFace *pEntry = *ppHashTable + hash;
			if (!pEntry->pEntry) {
				pEntry->pEntry = pInFaceTable + i;
				pEntry->face = pInFaceTable[i].pArr[j];
				continue;
			}
			do {
				if (!pEntry->pNext) {
					pEntry = pEntry->pNext = pAlloc->pCalloc(1, sizeof(UsgInFace));
					pEntry->pEntry = pInFaceTable + i;
					pEntry->face = pInFaceTable[i].pArr[j];
					break;
				}
				pEntry = pEntry->pNext;
			} while(true);
		}
	}
}

Result ruvmMapToMesh(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMeshIn,
                     RuvmMesh *pMeshOut, RuvmCommonAttribList *pCommonAttribList,
                     float wScale) {
	if (!pMeshIn) {
		printf("Ruvm map to mesh failed, pMeshIn was null\n");
		return RUVM_ERROR;
	}
	if (!pMap) {
		printf("Ruvm map to mesh failed, pMap was null\n");
		return RUVM_ERROR;
	}
	Mesh meshInWrap = {.mesh = *pMeshIn};
	setSpecialAttribs(&meshInWrap, 0x70e); //don't set preserve yet
	if (isMeshInvalid(&meshInWrap)) {
		return RUVM_ERROR;
	}
	buildTangents(&meshInWrap);
	//TODO remove this, I dont think it's necessary. Origin is only used in bufmesh
	//it doesn't matter what it's set to here
	setAttribOrigins(&meshInWrap.mesh.meshAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);
	setAttribOrigins(&meshInWrap.mesh.faceAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);
	setAttribOrigins(&meshInWrap.mesh.loopAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);
	setAttribOrigins(&meshInWrap.mesh.edgeAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);
	setAttribOrigins(&meshInWrap.mesh.vertAttribs, RUVM_ATTRIB_ORIGIN_MESH_IN);

	if (!meshInWrap.mesh.edgeCount) {
		RUVM_ASSERT("", !meshInWrap.mesh.edgeAttribs.count);
		RUVM_ASSERT("", !meshInWrap.mesh.edgeAttribs.pArr);
		buildEdgeList(pContext, &meshInWrap);
	}

	InFaceArr *pInFaceTable = NULL;
	if (pMap->usgArr.count) {
		MapFile squares = { .mesh = pMap->usgArr.squares };
		ruvmCreateQuadTree(pContext, &squares);
		RuvmMesh squaresOut = {0};
		mapToMeshInternal(pContext, &squares, &meshInWrap, &squaresOut, pCommonAttribList, &pInFaceTable, 1.0f);
		sampleInAttribsAtUsgOrigins(pMap, &meshInWrap, &squaresOut, pInFaceTable);
		InFaceTableToHashTable(&pContext->alloc, pMap, squaresOut.faceCount, pInFaceTable);
		//*pMeshOut = squaresOut;
		//return RUVM_SUCCESS;
		ruvmMeshDestroy(pContext, &squaresOut);
	}
	setSpecialAttribs(&meshInWrap, 0x50); //set perserve if present

	mapToMeshInternal(pContext, pMap, &meshInWrap, pMeshOut, pCommonAttribList, NULL, wScale);
	if (pMap->usgArr.count) {
		pContext->alloc.pFree(pMap->usgArr.pInFaceTable);
		pMap->usgArr.pInFaceTable = NULL;
		for (int32_t i = 0; i < pMap->usgArr.count; ++i) {
			//pContext->alloc.pFree(pInFaceTable[i].pArr);
		}
		pContext->alloc.pFree(pInFaceTable);
	}
}

RuvmResult ruvmObjArrDestroy(RuvmContext pContext,
                             int32_t objCount, RuvmObject *pObjArr) {
	destroyObjArr(pContext, objCount, pObjArr);
}

RuvmResult ruvmUsgArrDestroy(RuvmContext pContext,
                                    int32_t count, RuvmUsg *pUsgArr) {
	for (int32_t i = 0; i < count; ++i) {
		ruvmMeshDestroy(pContext, pUsgArr[i].obj.pData);
	}
	pContext->alloc.pFree(pUsgArr);
	return RUVM_SUCCESS;
}

RuvmResult ruvmMeshDestroy(RuvmContext pContext, RuvmMesh *pMesh) {
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
	return RUVM_SUCCESS;
}

RuvmResult ruvmGetAttribSize(RuvmAttrib *pAttrib, int32_t *pSize) {
	*pSize = getAttribSize(pAttrib->type);
	return RUVM_SUCCESS;
}

RuvmResult ruvmGetAttrib(char *pName, AttribArray *pAttribs, Attrib **ppAttrib) {
	*ppAttrib = getAttrib(pName, pAttribs);
	return RUVM_SUCCESS;
}

typedef struct {
	RuvmImage imageBuf;
	RuvmMap pMap;
	RuvmContext pContext;
	int32_t *pJobsCompleted;
	void *pMutex;
	int32_t bufOffset;
	int32_t pixelCount;
	int8_t id;
	V2_F32 zBounds;
} RenderArgs;

static void testPixelAgainstFace(RenderArgs *pVars, V2_F32 *pPos, FaceRange *pFace, Color *pColor) {
	Mesh* pMesh = &pVars->pMap->mesh;
	V2_F32 verts[4];
	for (int32_t i = 0; i < pFace->size; ++i) {
		verts[i] = *(V2_F32 *)(pMesh->pVerts + pMesh->mesh.pLoops[pFace->start + i]);
	}
	int8_t triLoops[4] = {0};
	V3_F32 bc = getBarycentricInFace(verts, triLoops, pFace->size, *pPos);
	if (bc.d[0] < -.0001f || bc.d[1] < -.0001f || bc.d[2] < -.0001f ||
		!v3IsFinite(bc)) {
		return;
	}
	V3_F32 vertsXyz[3];
	for (int32_t i = 0; i < 3; ++i) {
		int32_t vertIndex = pMesh->mesh.pLoops[pFace->start + triLoops[i]];
		vertsXyz[i] = pMesh->pVerts[vertIndex];
	}
	V3_F32 wsPos = barycentricToCartesian(vertsXyz, &bc);
	//the alpha channel is used as a depth buffer
	if (wsPos.d[2] <= pColor->d[3]) {
		return;
	}
	V3_F32 ab = _(vertsXyz[1] V3SUB vertsXyz[0]);
	V3_F32 ac = _(vertsXyz[2] V3SUB vertsXyz[0]);
	V3_F32 normal = v3Cross(ab, ac);
	float normalLen =
		sqrt(normal.d[0] * normal.d[0] +
		     normal.d[1] * normal.d[1] +
		     normal.d[2] * normal.d[2]);
	_(&normal V3DIVEQLS normalLen);
	V3_F32 up = { .0f, .0f, 1.0f };
	float dotUp = _(normal V3DOT up);
	if (dotUp < .0f) {
		return;
	}
	float depth = (wsPos.d[2] - pVars->zBounds.d[0]) / pVars->zBounds.d[1];
	float value = dotUp;
	value *= 1.0f - (1.0f - depth) * .5f;
	value *= .75;
	pColor->d[0] = value;
	pColor->d[1] = value;
	pColor->d[2] = value;
	pColor->d[3] = wsPos.d[2];
}

static void ruvmRenderJob(void *pArgs) {
	RenderArgs vars = *(RenderArgs *)pArgs;
	int32_t dataLen = vars.pixelCount * getPixelSize(vars.imageBuf.type);
	vars.imageBuf.pData = vars.pContext->alloc.pMalloc(dataLen);
	Mesh *pMesh = &vars.pMap->mesh;
	float pixelScale = 1.0 / (float)vars.imageBuf.res;
	float pixelHalfScale = pixelScale / 2.0f;
	FaceCells faceCells = {0};
	FaceCellsTable faceCellsTable = {.pFaceCells = &faceCells};
	QuadTreeSearch searchState = {0};
	ruvmInitQuadTreeSearch(&vars.pContext->alloc, vars.pMap, &searchState);
	for (int32_t i = 0; i < vars.pixelCount; ++i) {
		int32_t iOffset = vars.bufOffset + i;
		V2_F32 index = {iOffset % vars.imageBuf.res,
		                iOffset / vars.imageBuf.res};
		V2_F32 pos = {pixelScale * index.d[0] + pixelHalfScale,
		              pixelScale * index.d[1] + pixelHalfScale};
		Color color = { 0 };
		color.d[3] = FLT_MAX * -1.0f;
		ruvmGetCellsForSingleFace(&searchState, 1, &pos, &faceCellsTable, NULL, 0);
		Cell *pLeaf = faceCells.pCells[faceCells.cellSize - 1];
		for (int32_t j = 0; j < faceCells.cellSize; ++j) {
			Cell* pCell = faceCells.pCells[j];
			int32_t* pCellFaces;
			Range cellFaceRange = {0};
			if (faceCells.pCellType[j]) {
				pCellFaces = pCell->pEdgeFaces;
				cellFaceRange = pLeaf->pLinkEdgeRanges[j];
			}
			else if (faceCells.pCellType[j] != 1) {
				pCellFaces = pCell->pFaces;
				cellFaceRange.start = 0;
				cellFaceRange.end = pCell->faceSize;
			}
			else {
				continue;
			}
			for (int32_t k = cellFaceRange.start; k < cellFaceRange.end; ++k) {
				FaceRange face = {0};
				face.index = pCellFaces[k];
				face.start = pMesh->mesh.pFaces[face.index];
				face.end = pMesh->mesh.pFaces[face.index + 1];
				face.size = face.end - face.start;
				FaceTriangulated faceTris = {0};
				if (face.size > 4) {
					faceTris = triangulateFace(vars.pContext->alloc, face,
					                           pMesh->pVerts,
					                           pMesh->mesh.pLoops, 0);
					for (int32_t l = 0; l < faceTris.triCount; ++l) {
						FaceRange tri = {0};
						tri.index = face.index;
						tri.start = l * 3;
						tri.end = tri.start + 3;
						tri.size = tri.end - tri.start;
						tri.start += face.start;
						tri.end += face.start;
						testPixelAgainstFace(&vars, &pos, &tri, &color);
					}
				}
				else {
					testPixelAgainstFace(&vars, &pos, &face, &color);
				}
			}
		}
		color.d[3] = color.d[3] != FLT_MAX * -1.0f;
		setPixelColor(&vars.imageBuf, i, &color);
	}
	ruvmDestroyQuadTreeSearch(&searchState);
	*(RenderArgs *)pArgs = vars;
	RuvmThreadPool *pThreadPool = &vars.pContext->threadPool;
	pThreadPool->pMutexLock(vars.pContext->pThreadPoolHandle, vars.pMutex);
	++*vars.pJobsCompleted;
	pThreadPool->pMutexUnlock(vars.pContext->pThreadPoolHandle, vars.pMutex);
}

static
V2_F32 getZBounds(RuvmMap pMap) {
	Mesh* pMesh = &pMap->mesh;
	V2_F32 zBounds = {.d = {FLT_MAX, FLT_MIN}};
	for (int32_t i = 0; i < pMesh->mesh.vertCount; ++i) {
		if (pMesh->pVerts[i].d[2] < zBounds.d[0]) {
			zBounds.d[0] = pMesh->pVerts[i].d[2];
		}
		if (pMesh->pVerts[i].d[2] > zBounds.d[1]) {
			zBounds.d[1] = pMesh->pVerts[i].d[2];
		}
	}
	return zBounds;
}

RuvmResult ruvmMapFileGenPreviewImage(RuvmContext pContext, RuvmMap pMap, RuvmImage *pImage) {
	V2_F32 zBounds = getZBounds(pMap);
	int32_t pixelCount = pImage->res * pImage->res;
	int32_t pixelsPerJob = pixelCount / pContext->threadCount;
	void *pMutex = NULL;
	pContext->threadPool.pMutexGet(pContext->pThreadPoolHandle, &pMutex);
	int32_t jobsCompleted = 0;
	void *jobArgPtrs[MAX_THREADS];
	RenderArgs args[MAX_THREADS];
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		args[i].bufOffset = i * pixelsPerJob;
		args[i].imageBuf.res = pImage->res;
		args[i].imageBuf.type = pImage->type;
		args[i].pContext = pContext;
		args[i].pixelCount = i == pContext->threadCount - 1 ?
			pixelCount - args[i].bufOffset : pixelsPerJob;
		args[i].pMap = pMap;
		args[i].zBounds = zBounds;
		args[i].pMutex = pMutex;
		args[i].pJobsCompleted = &jobsCompleted;
		args[i].id = i;
		jobArgPtrs[i] = args + i;
	}
	pContext->threadPool.pJobStackPushJobs(pContext->pThreadPoolHandle,
	                                       pContext->threadCount,
	                                       ruvmRenderJob, jobArgPtrs);
	CLOCK_INIT;
	CLOCK_START;
	waitForJobs(pContext, &jobsCompleted, pMutex);
	pContext->threadPool.pMutexDestroy(pContext->pThreadPoolHandle, pMutex);
	CLOCK_STOP("CREATE_IMAGE");
	int32_t pixelSize = getPixelSize(pImage->type);
	pImage->pData = pContext->alloc.pMalloc(pixelCount * pixelSize);
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		void *pImageOffset = offsetImagePtr(pImage, i * pixelsPerJob);
		int32_t bytesToCopy = pixelSize * args[i].pixelCount;
		memcpy(pImageOffset, args[i].imageBuf.pData, bytesToCopy);
		pContext->alloc.pFree(args[i].imageBuf.pData);
	}
	return RUVM_SUCCESS;
}

void ruvmMapIndexedAttribsGet(RuvmContext pContext, RuvmMap pMap,
                              RuvmAttribIndexedArr *pIndexedAttribs) {
	*pIndexedAttribs = pMap->indexedAttribs;
}
