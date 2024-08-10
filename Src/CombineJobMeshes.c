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
void combineJobInFaceLists(RuvmContext pContext, InFaceArr *pInFaceTable, SendOffArgs *pJobArgs) {
	int32_t face = 0;
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		int32_t faceCount = pJobArgs[i].bufMesh.mesh.mesh.faceCount;
		for (int32_t j = 0; j < faceCount; ++j) {
			pInFaceTable[face] = pJobArgs[i].pInFaces[j];
			face++;
		}
		pContext->alloc.pFree(pJobArgs[i].pInFaces);
	}
}

void ruvmCombineJobMeshes(RuvmContext pContext, RuvmMap pMap,  Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  int8_t *pVertSeamTable, bool *pEdgeSeamTable,
                          InFaceArr **ppInFaceTable, float wScale, Mesh *pInMesh) {
	//struct timeval start, stop;
	//CLOCK_START;
	//TODO figure out how to handle edges in local meshes,
	//probably just add internal edges to local mesh,
	//and figure out edges in border faces after jobs are finished?
	//You'll need to provide functionality for interpolating and blending
	//edge data, so keep that in mind.
	pMeshOut->mesh.type.type = RUVM_OBJECT_DATA_MESH_INTERN;
	MeshCounts totalCount = {0};
	MeshCounts totalBoundsCount = {0};
	for (int32_t i = 0; i < pContext->threadCount; ++i) {
		addToMeshCounts(pContext, &totalCount, &totalBoundsCount,
		                (Mesh *)&pJobArgs[i].bufMesh);
	}
	//3 for last face index
	pMeshOut->faceBufSize = 3 + totalCount.faces + totalBoundsCount.faces / 10;
	pMeshOut->loopBufSize = 2 + totalCount.loops + totalBoundsCount.loops / 10;
	pMeshOut->edgeBufSize = 2 + totalCount.edges + totalBoundsCount.edges / 10;
	pMeshOut->vertBufSize = 2 + totalCount.verts + totalBoundsCount.verts / 10;
	pMeshOut->mesh.pFaces =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMeshOut->faceBufSize);
	pMeshOut->mesh.pLoops =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMeshOut->loopBufSize);
	pMeshOut->mesh.pEdges =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMeshOut->loopBufSize);
	//only need to use the first buf mesh, as attribs are the same across all jobs
	Mesh *src = &pJobArgs[0].bufMesh;
	allocAttribsFromMeshArr(&pContext->alloc, pMeshOut, 1, &src, false);
	setSpecialAttribs(pMeshOut, 0xe); //1110 - set only verts, uvs, & normals
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
		copyMesh(&pMeshOut->mesh, &pJobArgs[i].bufMesh.mesh);
	}
	if (ppInFaceTable) {
		*ppInFaceTable = pContext->alloc.pCalloc(pMeshOut->faceBufSize, sizeof(InFaceArr));
		combineJobInFaceLists(pContext, *ppInFaceTable, pJobArgs);
	}
	printf("realloc time total %lu\n", reallocTime);
	ruvmMergeBorderFaces(pContext, pMap, pMeshOut, pJobArgs,
	                     pEdgeVerts, pJobBases, pVertSeamTable,
	                     pEdgeSeamTable, ppInFaceTable, wScale, pInMesh);
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

bool getIfRuvm(const BorderFace *pEntry, const int32_t loopIndex) {
	return pEntry->isRuvm >> loopIndex & 0x1;
}

bool getIfOnInVert(const BorderFace *pEntry, const int32_t loopIndex) {
	return pEntry->onInVert >> loopIndex & 0x1;
}

bool getIfOnLine(const BorderFace *pEntry, int32_t loopIndex) {
	return pEntry->onLine >> loopIndex & 0x1;
}

int32_t getSegment(const BorderFace *pEntry, int32_t loopIndex) {
	return pEntry->segment >> (loopIndex * 3) & 0x7;
}

int32_t getMapLoop(const BorderFace *pEntry,
                   const RuvmMap pMap, const int32_t loopIndex) {
	int32_t mapLoop = pEntry->ruvmLoop >> loopIndex * 3 & 7;
	RUVM_ASSERT("", mapLoop >= 0 && mapLoop < pMap->mesh.mesh.loopCount);
	return mapLoop;
}

V2_I32 getTileMinFromBoundsEntry(BorderFace *pEntry) {
	//tileX and Y are signed, but are stored unsigned in pEntry
	V2_I32 tileMin = {0};
	bool sign = pEntry->tileX >> 11 & 0x1;
	tileMin.d[0] |= sign ? pEntry->tileX | ~0xfff : pEntry->tileX;
	sign = pEntry->tileY >> 11 & 0x1;
	tileMin.d[1] |= sign ? pEntry->tileY | ~0xfff : pEntry->tileY;
	return tileMin;
}

int32_t bufMeshGetVertIndex(const Piece *pPiece,
                            const BufMesh *pBufMesh, const int32_t localLoop) {
	bool isRuvm = getIfRuvm(pPiece->pEntry, localLoop);
	bool isOnLine = getIfOnLine(pPiece->pEntry, localLoop);
	int32_t vert = pBufMesh->mesh.mesh.pLoops[pPiece->bufFace.start - localLoop];
	if (!isRuvm || isOnLine) {
		vert = convertBorderVertIndex(pBufMesh, vert).realIndex;
	}
	return vert;
}

int32_t bufMeshGetEdgeIndex(const Piece *pPiece,
                            const BufMesh *pBufMesh, const int32_t localLoop) {
	bool isRuvm = getIfRuvm(pPiece->pEntry, localLoop);
	bool isOnLine = getIfOnLine(pPiece->pEntry, localLoop);
	int32_t edge = pBufMesh->mesh.mesh.pEdges[pPiece->bufFace.start - localLoop];
	if (!isRuvm || isOnLine) {
		edge = convertBorderEdgeIndex(pBufMesh, edge).realIndex;
	}
	return edge;
}
