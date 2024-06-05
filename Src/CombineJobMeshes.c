#include <string.h>
#include <assert.h>
#include <stdio.h>

#include <RUVM.h>
#include <Context.h>
#include <CombineJobMeshes.h>
#include <AttribUtils.h>
#include <MapFile.h>
#include <Error.h>

static
void allocateMeshOut(RuvmContext pContext, Mesh *pMeshOut,
                     SendOffArgs *pJobArgs) {
	RuvmAlloc *pAlloc = &pContext->alloc;
	typedef struct {
		int32_t faces;
		int32_t loops;
		int32_t edges;
		int32_t verts;
	} MeshCounts;
	MeshCounts totalCount = {0};
	MeshCounts totalBorderCount = {0};
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		//TODO maybe replace *Counts vars in Mesh to use MeshCounts,
		//     so we can just do:
		//     meshCountsAdd(totalCount, pJobArgs[i].bufMesh.mesh.meshCounts);
		//     or something.
		totalCount.faces += pJobArgs[i].bufMesh.mesh.mesh.faceCount;
		totalCount.loops += pJobArgs[i].bufMesh.mesh.mesh.loopCount;
		totalCount.edges += pJobArgs[i].bufMesh.mesh.mesh.edgeCount;
		totalCount.verts += pJobArgs[i].bufMesh.mesh.mesh.vertCount;
		totalBorderCount.faces += pJobArgs[i].bufMesh.borderFaceCount;
		totalBorderCount.loops += pJobArgs[i].bufMesh.borderLoopCount;
		totalBorderCount.edges += pJobArgs[i].bufMesh.borderEdgeCount;
		totalBorderCount.verts += pJobArgs[i].bufMesh.borderVertCount;
	}
	//TODO figure out how to handle edges in local meshes,
	//probably just add internal edges to local mesh,
	//and figure out edges in border faces after jobs are finished?
	//You'll need to provide functionality for interpolating and blending
	//edge data, so keep that in mind.
	RuvmMesh *pBufCore = &asMesh(&pJobArgs[0].bufMesh)->mesh;
	pMeshOut->faceBufSize = 2 + totalCount.faces + totalBorderCount.faces / 10;
	pMeshOut->loopBufSize = 2 + totalCount.loops + totalBorderCount.loops / 10;
	pMeshOut->edgeBufSize = 2 + totalCount.edges + totalBorderCount.edges / 10;
	pMeshOut->vertBufSize = 2 + totalCount.verts + totalBorderCount.verts / 10;
	pMeshOut->mesh.pFaces =
		pAlloc->pMalloc(sizeof(int32_t) * pMeshOut->faceBufSize);
	pMeshOut->mesh.pLoops =
		pAlloc->pMalloc(sizeof(int32_t) * pMeshOut->loopBufSize);
	pMeshOut->mesh.pEdges =
		pAlloc->pMalloc(sizeof(int32_t) * pMeshOut->loopBufSize);
	allocAttribs(pAlloc, &pMeshOut->mesh.faceAttribs, &pBufCore->faceAttribs,
				 NULL, pMeshOut->faceBufSize);
	allocAttribs(pAlloc, &pMeshOut->mesh.loopAttribs, &pBufCore->loopAttribs,
				 NULL, pMeshOut->loopBufSize);
	allocAttribs(pAlloc, &pMeshOut->mesh.edgeAttribs, &pBufCore->edgeAttribs,
				 NULL, pMeshOut->edgeBufSize);
	allocAttribs(pAlloc, &pMeshOut->mesh.vertAttribs, &pBufCore->vertAttribs,
				 NULL, pMeshOut->vertBufSize);
	setSpecialAttribs(pMeshOut, 0xe); //1110 - set only verts, uvs, & normals
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
void copyMesh(int32_t jobIndex, Mesh *pMeshOut, SendOffArgs *pJobArgs) {
	BufMesh *pBufMesh = &pJobArgs[jobIndex].bufMesh;
	RuvmMesh *pOutCore = &pMeshOut->mesh;
	for (int32_t j = 0; j < asMesh(pBufMesh)->mesh.faceCount; ++j) {
		asMesh(pBufMesh)->mesh.pFaces[j] += pOutCore->loopCount;
	}
	for (int32_t j = 0; j < asMesh(pBufMesh)->mesh.loopCount; ++j) {
		asMesh(pBufMesh)->mesh.pLoops[j] += pOutCore->vertCount;
		asMesh(pBufMesh)->mesh.pEdges[j] += pOutCore->edgeCount;
	}
	int32_t *facesStart = pOutCore->pFaces + pOutCore->faceCount;
	int32_t *loopsStart = pOutCore->pLoops + pOutCore->loopCount;
	int32_t *edgesStart = pOutCore->pEdges + pOutCore->loopCount;
	memcpy(facesStart, asMesh(pBufMesh)->mesh.pFaces,
	       sizeof(int32_t) * asMesh(pBufMesh)->mesh.faceCount);
	bulkCopyAttribs(&asMesh(pBufMesh)->mesh.faceAttribs, pOutCore->faceCount,
	                &pOutCore->faceAttribs, asMesh(pBufMesh)->mesh.faceCount);
	pOutCore->faceCount += asMesh(pBufMesh)->mesh.faceCount;
	memcpy(loopsStart, asMesh(pBufMesh)->mesh.pLoops,
	       sizeof(int32_t) * asMesh(pBufMesh)->mesh.loopCount);
	bulkCopyAttribs(&asMesh(pBufMesh)->mesh.loopAttribs, pOutCore->loopCount,
	                &pOutCore->loopAttribs, asMesh(pBufMesh)->mesh.loopCount);
	pOutCore->loopCount += asMesh(pBufMesh)->mesh.loopCount;
	memcpy(edgesStart, asMesh(pBufMesh)->mesh.pEdges,
	       sizeof(int32_t) * asMesh(pBufMesh)->mesh.loopCount);
	bulkCopyAttribs(&asMesh(pBufMesh)->mesh.edgeAttribs, pOutCore->edgeCount,
	                &pOutCore->edgeAttribs, asMesh(pBufMesh)->mesh.edgeCount);
	pOutCore->edgeCount += asMesh(pBufMesh)->mesh.edgeCount;
	bulkCopyAttribs(&asMesh(pBufMesh)->mesh.vertAttribs, pOutCore->vertCount,
	                &pOutCore->vertAttribs, asMesh(pBufMesh)->mesh.vertCount);
	pOutCore->vertCount += asMesh(pBufMesh)->mesh.vertCount;
}

void ruvmCombineJobMeshes(RuvmContext pContext, RuvmMap pMap,  Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  int8_t *pVertSeamTable, bool *pEdgeSeamTable) {
	//struct timeval start, stop;
	//CLOCK_START;
	allocateMeshOut(pContext, pMeshOut, pJobArgs);
	JobBases *pJobBases =
		pContext->alloc.pMalloc(sizeof(JobBases) * pContext->threadCount);
	uint64_t reallocTime = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		reallocTime += pJobArgs[i].reallocTime;
		printf("realloc time %lu\n", pJobArgs[i].reallocTime);
		printf("rawbufSize: %d | ", pJobArgs[i].rawBufSize);
		printf("bufSize: %d | ", pJobArgs[i].bufSize);
		printf("finalbufSize: %d | \n\n", pJobArgs[i].finalBufSize);
		pJobBases[i].vertBase = pMeshOut->mesh.vertCount;
		pJobBases[i].edgeBase = pMeshOut->mesh.edgeCount;
		copyMesh(i, pMeshOut, pJobArgs);
	}
	printf("realloc time total %lu\n", reallocTime);
	ruvmMergeBorderFaces(pContext, pMap, pMeshOut, pJobArgs,
	                     pEdgeVerts, pJobBases, pVertSeamTable,
	                     pEdgeSeamTable);
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		BufMesh *pBufMesh = &pJobArgs[i].bufMesh;
		ruvmMeshDestroy(pContext, &asMesh(pBufMesh)->mesh);
		pContext->alloc.pFree(pJobArgs[i].borderTable.pTable);
	}
	pContext->alloc.pFree(pJobBases);
	meshSetLastFace(&pContext->alloc, pMeshOut);
	//CLOCK_STOP("moving to work mesh");
}

//Returns struct containing inMesh loop, edge, vert, and local loop,
//for the current buf loop in the given border face entry.
//The start loop of the face is also given.
//Though for face end, and size, call getFaceRange
BorderInInfo getBorderEntryInInfo(const BorderFace *pEntry,
                                  const SendOffArgs *pJobArgs,
								  const int32_t loopIndex) {
	BorderInInfo inInfo = {0};
	RUVM_ASSERT("", pEntry->baseFace >= 0);
	RUVM_ASSERT("", pEntry->baseFace < pJobArgs[pEntry->job].mesh.mesh.faceCount);
	inInfo.loopLocal = pEntry->baseLoop >> loopIndex * 2 & 3;
	RUVM_ASSERT("", inInfo.loopLocal >= 0);
	inInfo.start =
		pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace];
	RUVM_ASSERT("", inInfo.start >= 0);
	RUVM_ASSERT("", inInfo.start < pJobArgs[pEntry->job].mesh.mesh.loopCount);
	//Check local loop is less than face size
	RUVM_ASSERT("", inInfo.loopLocal <
	       pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace + 1] -
		   inInfo.start);
	inInfo.loop = inInfo.start + inInfo.loopLocal;
	RUVM_ASSERT("", inInfo.loop < pJobArgs[pEntry->job].mesh.mesh.loopCount);
	inInfo.edge = pJobArgs[pEntry->job].mesh.mesh.pEdges[inInfo.loop];
	RUVM_ASSERT("", inInfo.edge < pJobArgs[pEntry->job].mesh.mesh.edgeCount);
	inInfo.vert = pJobArgs[pEntry->job].mesh.mesh.pLoops[inInfo.loop];
	RUVM_ASSERT("", inInfo.vert < pJobArgs[0].mesh.mesh.vertCount);
	return inInfo;
}

_Bool getIfRuvm(const BorderFace *pEntry, const int32_t loopIndex) {
	return pEntry->isRuvm >> loopIndex & 1;
}

_Bool getIfOnInVert(const BorderFace *pEntry, const int32_t loopIndex) {
	return pEntry->onInVert >> loopIndex & 1;
}

_Bool getIfOnLine(const BorderFace *pEntry, int32_t loopIndex) {
	return pEntry->onLine >> loopIndex & 1;
}

int32_t getMapLoop(const BorderFace *pEntry,
                   const RuvmMap pMap, const int32_t loopIndex) {
	int32_t mapLoop = pEntry->ruvmLoop >> loopIndex * 3 & 7;
	RUVM_ASSERT("", mapLoop >= 0 && mapLoop < pMap->mesh.mesh.loopCount);
	return mapLoop;
}

int32_t bufMeshGetVertIndex(const Piece *pPiece,
                            const BufMesh *pBufMesh, const int32_t localLoop) {
	_Bool isRuvm = getIfRuvm(pPiece->pEntry, localLoop);
	_Bool isOnLine = getIfOnLine(pPiece->pEntry, localLoop);
	int32_t vert = pBufMesh->mesh.mesh.pLoops[pPiece->bufFace.start - localLoop];
	if (!isRuvm || isOnLine) {
		vert = convertBorderVertIndex(pBufMesh, vert).realIndex;
	}
	return vert;
}

int32_t bufMeshGetEdgeIndex(const Piece *pPiece,
                            const BufMesh *pBufMesh, const int32_t localLoop) {
	_Bool isRuvm = getIfRuvm(pPiece->pEntry, localLoop);
	_Bool isOnLine = getIfOnLine(pPiece->pEntry, localLoop);
	int32_t edge = pBufMesh->mesh.mesh.pEdges[pPiece->bufFace.start - localLoop];
	if (!isRuvm || isOnLine) {
		edge = convertBorderEdgeIndex(pBufMesh, edge).realIndex;
	}
	return edge;
}
