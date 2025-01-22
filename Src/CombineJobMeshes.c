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
void combineJobInFaceLists(RuvmContext pContext, InFaceArr *pInFaceTable,
                           SendOffArgs *pJobArgs, int32_t mapJobsSent) {
	int32_t face = 0;
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		int32_t faceCount = pJobArgs[i].bufMesh.mesh.mesh.faceCount;
		for (int32_t j = 0; j < faceCount; ++j) {
			pInFaceTable[face] = pJobArgs[i].pInFaces[j];
			face++;
		}
		pContext->alloc.pFree(pJobArgs[i].pInFaces);
	}
}

void uvsCombineJobMeshes(RuvmContext pContext, RuvmMap pMap,  Mesh *pMeshOut,
                          SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
						  int8_t *pVertSeamTable, bool *pEdgeSeamTable,
                          InFaceArr **ppInFaceTable, float wScale, Mesh *pInMesh,
                          int32_t mapJobsSent) {
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
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		addToMeshCounts(pContext, &totalCount, &totalBoundsCount,
		                (Mesh *)&pJobArgs[i].bufMesh);
	}
	//3 for last face index
	pMeshOut->faceBufSize = 3 + totalCount.faces + totalBoundsCount.faces / 10;
	pMeshOut->cornerBufSize = 2 + totalCount.corners + totalBoundsCount.corners / 10;
	pMeshOut->edgeBufSize = 2 + totalCount.edges + totalBoundsCount.edges / 10;
	pMeshOut->vertBufSize = 2 + totalCount.verts + totalBoundsCount.verts / 10;
	pMeshOut->mesh.pFaces =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMeshOut->faceBufSize);
	pMeshOut->mesh.pCorners =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMeshOut->cornerBufSize);
	pMeshOut->mesh.pEdges =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMeshOut->cornerBufSize);
	//only need to use the first buf mesh, as attribs are the same across all jobs
	Mesh *src = NULL;
	//get first bufmesh that isn't empty
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		if (pJobArgs[i].bufSize) {
			src = &pJobArgs[i].bufMesh;
			break;
		}
	}
	allocAttribsFromMeshArr(&pContext->alloc, pMeshOut, 1, &src, false);
	setSpecialAttribs(pMeshOut, 0xe); //1110 - set only verts, uvs, & normals
	JobBases *pJobBases =
		pContext->alloc.pMalloc(sizeof(JobBases) * mapJobsSent);
	uint64_t reallocTime = 0;
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		reallocTime += pJobArgs[i].reallocTime;
		printf("realloc time %lu\n", pJobArgs[i].reallocTime);
		printf("rawbufSize: %d | ", pJobArgs[i].rawBufSize);
		printf("bufSize: %d | ", pJobArgs[i].bufSize);
		printf("finalbufSize: %d | \n\n", pJobArgs[i].finalBufSize);
		if (pJobArgs[i].bufSize) { //don't copy if mesh is empty
			pJobBases[i].vertBase = pMeshOut->mesh.vertCount;
			pJobBases[i].edgeBase = pMeshOut->mesh.edgeCount;
			copyMesh(&pMeshOut->mesh, &pJobArgs[i].bufMesh.mesh);
		}
	}
	if (ppInFaceTable) {
		*ppInFaceTable = pContext->alloc.pCalloc(pMeshOut->faceBufSize, sizeof(InFaceArr));
		combineJobInFaceLists(pContext, *ppInFaceTable, pJobArgs, mapJobsSent);
	}
	printf("realloc time total %lu\n", reallocTime);
	uvsMergeBorderFaces(pContext, pMap, pMeshOut, pJobArgs,
	                     pEdgeVerts, pJobBases, pVertSeamTable,
	                     pEdgeSeamTable, ppInFaceTable, wScale, pInMesh,
	                     mapJobsSent);
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		BufMesh *pBufMesh = &pJobArgs[i].bufMesh;
		uvsMeshDestroy(pContext, &asMesh(pBufMesh)->mesh);
		pContext->alloc.pFree(pJobArgs[i].borderTable.pTable);
	}
	pContext->alloc.pFree(pJobBases);
	meshSetLastFace(&pContext->alloc, pMeshOut);
	//CLOCK_STOP("moving to work mesh");
}

//Returns struct containing inMesh corner, edge, vert, and local corner,
//for the current buf corner in the given border face entry.
//The start corner of the face is also given.
//Though for face end, and size, call getFaceRange
BorderInInfo getBorderEntryInInfo(const BorderFace *pEntry,
                                  const SendOffArgs *pJobArgs,
								  const int32_t cornerIdx) {
	BorderInInfo inInfo = {0};
	RUVM_ASSERT("", pEntry->baseFace >= 0);
	RUVM_ASSERT("", pEntry->baseFace < pJobArgs[pEntry->job].mesh.mesh.faceCount);
	inInfo.cornerLocal = getBaseCorner(pEntry, cornerIdx);
	RUVM_ASSERT("", inInfo.cornerLocal >= 0);
	inInfo.start = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace];
	inInfo.end = pJobArgs[pEntry->job].mesh.mesh.pFaces[pEntry->baseFace + 1];
	inInfo.size = inInfo.end - inInfo.start;
	RUVM_ASSERT("", inInfo.start >= 0);
	RUVM_ASSERT("", inInfo.start < pJobArgs[pEntry->job].mesh.mesh.cornerCount);
	RUVM_ASSERT("", inInfo.cornerLocal < inInfo.size);
	inInfo.edgeCorner = inInfo.start + inInfo.cornerLocal;
	RUVM_ASSERT("", inInfo.edgeCorner < pJobArgs[pEntry->job].mesh.mesh.cornerCount);
	inInfo.edge = pJobArgs[pEntry->job].mesh.mesh.pEdges[inInfo.edgeCorner];
	RUVM_ASSERT("", inInfo.edge < pJobArgs[pEntry->job].mesh.mesh.edgeCount);
	bool useNextVert = pEntry->inOrient ^ pEntry->mapOrient;
	inInfo.vertCorner = (inInfo.cornerLocal + useNextVert) % inInfo.size;
	inInfo.vertCorner += inInfo.start;
	inInfo.vert = pJobArgs[pEntry->job].mesh.mesh.pCorners[inInfo.vertCorner];
	RUVM_ASSERT("", inInfo.vert < pJobArgs[0].mesh.mesh.vertCount);
	return inInfo;
}

bool getIfRuvm(const BorderFace *pEntry, const int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return idxBitArray(&((BorderFaceSmall *)pEntry)->isRuvm, cornerIdx, 1);
		case 1:
			return idxBitArray(&((BorderFaceMid *)pEntry)->isRuvm, cornerIdx, 1);
		case 2:
			return idxBitArray(&((BorderFaceLarge *)pEntry)->isRuvm, cornerIdx, 1);
	}
}

bool getIfOnInVert(const BorderFace *pEntry, const int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return idxBitArray(&((BorderFaceSmall *)pEntry)->onInVert, cornerIdx, 1);
		case 1:
			return idxBitArray(&((BorderFaceMid *)pEntry)->onInVert, cornerIdx, 1);
		case 2:
			return idxBitArray(&((BorderFaceLarge *)pEntry)->onInVert, cornerIdx, 1);
	}
}

bool getIfOnLine(const BorderFace *pEntry, int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return idxBitArray(&((BorderFaceSmall *)pEntry)->onLine, cornerIdx, 1);
		case 1:
			return idxBitArray(&((BorderFaceMid *)pEntry)->onLine, cornerIdx, 1);
		case 2:
			return idxBitArray(&((BorderFaceLarge *)pEntry)->onLine, cornerIdx, 1);
	}
}

int32_t getSegment(const BorderFace *pEntry, int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return idxBitArray(&((BorderFaceSmall *)pEntry)->segment, cornerIdx, 3);
		case 1:
			return idxBitArray(&((BorderFaceMid *)pEntry)->segment, cornerIdx, 4);
		case 2:
			return idxBitArray(&((BorderFaceLarge *)pEntry)->segment, cornerIdx, 5);
	}
}

int32_t getMapCorner(const BorderFace *pEntry, const int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return idxBitArray(&((BorderFaceSmall *)pEntry)->uvsCorner, cornerIdx, 3);
		case 1:
			return idxBitArray(&((BorderFaceMid *)pEntry)->uvsCorner, cornerIdx, 4);
		case 2:
			return idxBitArray(&((BorderFaceLarge *)pEntry)->uvsCorner, cornerIdx, 5);
	}
}

int32_t getBaseCorner(const BorderFace *pEntry, int32_t cornerIdx) {
	switch (pEntry->memType) {
	case 0:
		return idxBitArray(&((BorderFaceSmall *)pEntry)->baseCorner, cornerIdx, 2);
	case 1:
		return idxBitArray(&((BorderFaceMid *)pEntry)->baseCorner, cornerIdx, 2);
	case 2:
		return idxBitArray(&((BorderFaceLarge *)pEntry)->baseCorner, cornerIdx, 2);
	}
}

V2_I16 getTileMinFromBoundsEntry(BorderFace *pEntry) {
	//tileX and Y are signed, but are stored unsigned in pEntry
	V2_I16 tileMin = {0};
	bool sign = pEntry->tileX >> RUVM_TILE_MIN_BIT_LEN & 0x1;
	tileMin.d[0] |= sign ? pEntry->tileX | ~0xfff : pEntry->tileX;
	sign = pEntry->tileY >> RUVM_TILE_MIN_BIT_LEN & 0x1;
	tileMin.d[1] |= sign ? pEntry->tileY | ~0xfff : pEntry->tileY;
	return tileMin;
}

int32_t bufMeshGetVertIdx(const Piece *pPiece,
                            const BufMesh *pBufMesh, const int32_t localCorner) {
	bool isRuvm = getIfRuvm(pPiece->pEntry, localCorner);
	bool isOnLine = getIfOnLine(pPiece->pEntry, localCorner);
	int32_t vert = pBufMesh->mesh.mesh.pCorners[pPiece->bufFace.start - localCorner];
	if (!isRuvm || isOnLine) {
		vert = convertBorderVertIdx(pBufMesh, vert).realIdx;
	}
	return vert;
}

int32_t bufMeshGetEdgeIdx(const Piece *pPiece,
                            const BufMesh *pBufMesh, const int32_t localCorner) {
	bool isRuvm = getIfRuvm(pPiece->pEntry, localCorner);
	bool isOnLine = getIfOnLine(pPiece->pEntry, localCorner);
	int32_t edge = pBufMesh->mesh.mesh.pEdges[pPiece->bufFace.start - localCorner];
	if (!isRuvm || isOnLine) {
		edge = convertBorderEdgeIdx(pBufMesh, edge).realIdx;
	}
	return edge;
}
