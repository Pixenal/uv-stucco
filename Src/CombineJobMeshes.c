#include <string.h>

#include <RUVM.h>
#include <Context.h>
#include <CombineJobMeshes.h>
#include <AttribUtils.h>

static
void allocateMeshOut(RuvmContext pContext, RuvmMesh *pMeshOut,
                     SendOffArgs *pJobArgs) {
	RuvmAlloc *pAlloc = &pContext->alloc;
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
	//and figure out edges in border faces after jobs are finished?
	//You'll need to provide functionality for interpolating and blending
	//edge data, so keep that in mind.
	RuvmMesh *pBufMesh = &pJobArgs[0].bufMesh.mesh;
	pMeshOut->pFaces = pAlloc->pMalloc(sizeof(int32_t) * (totalCount.faces + 1));
	allocAttribs(pAlloc, &pMeshOut->faceAttribs, &pBufMesh->faceAttribs,
				 NULL, totalCount.faces);
	pMeshOut->pLoops = pAlloc->pMalloc(sizeof(int32_t) * totalCount.loops);
	allocAttribs(pAlloc, &pMeshOut->loopAttribs, &pBufMesh->loopAttribs,
				 NULL, totalCount.loops);
	pMeshOut->pEdges = pAlloc->pMalloc(sizeof(int32_t) * totalCount.loops);
	allocAttribs(pAlloc, &pMeshOut->edgeAttribs, &pBufMesh->edgeAttribs,
				 NULL, totalCount.edges);
	allocAttribs(pAlloc, &pMeshOut->vertAttribs, &pBufMesh->vertAttribs,
				 NULL, totalCount.verts);
}

static
void bulkCopyAttribs(AttribArray *pSrc, int32_t SrcOffset,
                     AttribArray *pDest, int32_t dataLen) {
	for (int32_t i = 0; i < pSrc->count; ++i) {
		void *attribDestStart = attribAsVoid(pDest->pArr + i, SrcOffset);
		int32_t attribTypeSize = getAttribSize(pSrc->pArr[i].type);
		memcpy(attribDestStart, pSrc->pArr[i].pData, attribTypeSize * dataLen);
	}
}

static
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
	memcpy(facesStart, bufMesh->mesh.pFaces,
	       sizeof(int32_t) * bufMesh->mesh.faceCount);
	bulkCopyAttribs(&bufMesh->mesh.faceAttribs, pMeshOut->faceCount,
	                &pMeshOut->faceAttribs, bufMesh->mesh.faceCount);
	pMeshOut->faceCount += bufMesh->mesh.faceCount;
	memcpy(loopsStart, bufMesh->mesh.pLoops,
	       sizeof(int32_t) * bufMesh->mesh.loopCount);
	bulkCopyAttribs(&bufMesh->mesh.loopAttribs, pMeshOut->loopCount,
	                &pMeshOut->loopAttribs, bufMesh->mesh.loopCount);
	pMeshOut->loopCount += bufMesh->mesh.loopCount;
	memcpy(edgesStart, bufMesh->mesh.pEdges,
	       sizeof(int32_t) * bufMesh->mesh.loopCount);
	bulkCopyAttribs(&bufMesh->mesh.edgeAttribs, pMeshOut->edgeCount,
	                &pMeshOut->edgeAttribs, bufMesh->mesh.edgeCount);
	pMeshOut->edgeCount += bufMesh->mesh.edgeCount;
	bulkCopyAttribs(&bufMesh->mesh.vertAttribs, pMeshOut->vertCount,
	                &pMeshOut->vertAttribs, bufMesh->mesh.vertCount);
	pMeshOut->vertCount += bufMesh->mesh.vertCount;
}

void ruvmCombineJobMeshes(RuvmContext pContext, RuvmMap pMap,  RuvmMesh *pMeshOut,
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
	ruvmMergeBorderFaces(pContext, pMap, &meshOutWrap, pJobArgs,
	                       pEdgeVerts, pJobBases, pVertSeamTable);
	*pMeshOut = meshOutWrap.mesh;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		BufMesh *bufMesh = &pJobArgs[i].bufMesh;
		ruvmMeshDestroy(pContext, &bufMesh->mesh);
		pContext->alloc.pFree(pJobArgs[i].borderTable.pTable);
	}
	pContext->alloc.pFree(pJobBases);
	pMeshOut->pFaces[pMeshOut->faceCount] = pMeshOut->loopCount;
	//CLOCK_STOP("moving to work mesh");
}
