#include <string.h>
#include <assert.h>
#include <stdio.h>

#include <UvStucco.h>
#include <Context.h>
#include <CombineJobMeshes.h>
#include <AttribUtils.h>
#include <MapFile.h>
#include <Error.h>

static
void combineJobInFaceLists(StucContext pContext, InFaceArr *pInFaceTable,
                           SendOffArgs *pJobArgs, int32_t mapJobsSent) {
	int32_t face = 0;
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		int32_t faceCount = pJobArgs[i].bufMesh.mesh.core.faceCount;
		for (int32_t j = 0; j < faceCount; ++j) {
			pInFaceTable[face] = pJobArgs[i].pInFaces[j];
			face++;
		}
		pContext->alloc.pFree(pJobArgs[i].pInFaces);
	}
}

Result stucCombineJobMeshes(StucContext pContext, StucMap pMap,  Mesh *pMeshOut,
                            SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts,
                            int8_t *pVertSeamTable, bool *pEdgeSeamTable,
                            InFaceArr **ppInFaceTable, float wScale, Mesh *pInMesh,
                            int32_t mapJobsSent) {
	Result err = STUC_SUCCESS;
	//struct timeval start, stop;
	//CLOCK_START;
	//TODO figure out how to handle edges in local meshes,
	//probably just add internal edges to local mesh,
	//and figure out edges in border faces after jobs are finished?
	//You'll need to provide functionality for interpolating and blending
	//edge data, so keep that in mind.
	pMeshOut->core.type.type = STUC_OBJECT_DATA_MESH_INTERN;
	MeshCounts totalCount = {0};
	MeshCounts totalBoundsCount = {0};
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		stucAddToMeshCounts(pContext, &totalCount, &totalBoundsCount,
		                (Mesh *)&pJobArgs[i].bufMesh);
	}
	//3 for last face index
	pMeshOut->faceBufSize = 3 + totalCount.faces + totalBoundsCount.faces / 10;
	pMeshOut->cornerBufSize = 2 + totalCount.corners + totalBoundsCount.corners / 10;
	pMeshOut->edgeBufSize = 2 + totalCount.edges + totalBoundsCount.edges / 10;
	pMeshOut->vertBufSize = 2 + totalCount.verts + totalBoundsCount.verts / 10;
	pMeshOut->core.pFaces =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMeshOut->faceBufSize);
	pMeshOut->core.pCorners =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMeshOut->cornerBufSize);
	pMeshOut->core.pEdges =
		pContext->alloc.pMalloc(sizeof(int32_t) * pMeshOut->cornerBufSize);
	//only need to use the first buf mesh, as attribs are the same across all jobs
	Mesh *src = NULL;
	//get first bufmesh that isn't empty
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		if (pJobArgs[i].bufSize) {
			src = &pJobArgs[i].bufMesh.mesh;
			break;
		}
	}
	stucAllocAttribsFromMeshArr(&pContext->alloc, pMeshOut, 1, &src, false);
	stucSetSpecialAttribs(pContext, pMeshOut, 0xe); //1110 - set only verts, stuc, & normals
	JobBases *pJobBases = pContext->alloc.pMalloc(sizeof(JobBases) * mapJobsSent);
	uint64_t reallocTime = 0;
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		reallocTime += pJobArgs[i].reallocTime;
#ifdef WIN32
		printf("realloc time %llu\n", pJobArgs[i].reallocTime);
#else
		printf("realloc time %lu\n", pJobArgs[i].reallocTime);
#endif
		printf("rawbufSize: %d | ", pJobArgs[i].rawBufSize);
		printf("bufSize: %d | ", pJobArgs[i].bufSize);
		printf("finalbufSize: %d | \n\n", pJobArgs[i].finalBufSize);
		if (pJobArgs[i].bufSize) { //don't copy if mesh is empty
			pJobBases[i].vertBase = pMeshOut->core.vertCount;
			pJobBases[i].edgeBase = pMeshOut->core.edgeCount;
			stucCopyMesh(&pMeshOut->core, &pJobArgs[i].bufMesh.mesh.core);
		}
	}
	if (ppInFaceTable) {
		*ppInFaceTable = pContext->alloc.pCalloc(pMeshOut->faceBufSize, sizeof(InFaceArr));
		combineJobInFaceLists(pContext, *ppInFaceTable, pJobArgs, mapJobsSent);
	}
#ifdef WIN32
	printf("realloc time total %llu\n", reallocTime);
#else
	printf("realloc time total %lu\n", reallocTime);
#endif
	err = stucMergeBorderFaces(pContext, pMap, pMeshOut, pJobArgs, pEdgeVerts, pJobBases,
	                           pVertSeamTable, pEdgeSeamTable, ppInFaceTable, wScale,
	                           pInMesh, mapJobsSent);
	for (int32_t i = 0; i < mapJobsSent; ++i) {
		BufMesh *pBufMesh = &pJobArgs[i].bufMesh;
		stucMeshDestroy(pContext, &pBufMesh->mesh.core);
		pContext->alloc.pFree(pJobArgs[i].borderTable.pTable);
	}
	pContext->alloc.pFree(pJobBases);
	stucMeshSetLastFace(&pContext->alloc, pMeshOut);
	//CLOCK_STOP("moving to work mesh");
	return err;
}

//Returns struct containing inMesh corner, edge, vert, and local corner,
//for the current buf corner in the given border face entry.
//The start corner of the face is also given.
//Though for face end, and size, call getFaceRange
BorderInInfo stucGetBorderEntryInInfo(const BorderFace *pEntry,
                                      const SendOffArgs *pJobArgs,
                                      const int32_t cornerIdx) {
	BorderInInfo inInfo = {0};
	STUC_ASSERT("", pEntry->inFace >= 0);
	STUC_ASSERT("", pEntry->inFace < pJobArgs[pEntry->job].mesh.core.faceCount);
	inInfo.cornerLocal = stucGetBaseCorner(pEntry, cornerIdx);
	STUC_ASSERT("", inInfo.cornerLocal >= 0);
	inInfo.start = pJobArgs[pEntry->job].mesh.core.pFaces[pEntry->inFace];
	inInfo.end = pJobArgs[pEntry->job].mesh.core.pFaces[pEntry->inFace + 1];
	inInfo.size = inInfo.end - inInfo.start;
	STUC_ASSERT("", inInfo.start >= 0);
	STUC_ASSERT("", inInfo.start < pJobArgs[pEntry->job].mesh.core.cornerCount);
	STUC_ASSERT("", inInfo.cornerLocal < inInfo.size);
	inInfo.edgeCorner = inInfo.start + inInfo.cornerLocal;
	STUC_ASSERT("", inInfo.edgeCorner < pJobArgs[pEntry->job].mesh.core.cornerCount);
	inInfo.edge = pJobArgs[pEntry->job].mesh.core.pEdges[inInfo.edgeCorner];
	STUC_ASSERT("", inInfo.edge < pJobArgs[pEntry->job].mesh.core.edgeCount);
	bool useNextVert = pEntry->inOrient ^ pEntry->mapOrient;
	inInfo.vertCorner = (inInfo.cornerLocal + useNextVert) % inInfo.size;
	inInfo.vertCorner += inInfo.start;
	inInfo.vert = pJobArgs[pEntry->job].mesh.core.pCorners[inInfo.vertCorner];
	STUC_ASSERT("", inInfo.vert < pJobArgs[0].mesh.core.vertCount);
	return inInfo;
}

bool stucGetIfStuc(const BorderFace *pEntry, const int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return stucIdxBitArray(((BorderFaceSmall *)pEntry)->isStuc, cornerIdx, 1);
		case 1:
			return stucIdxBitArray(((BorderFaceMid *)pEntry)->isStuc, cornerIdx, 1);
		case 2:
			return stucIdxBitArray(((BorderFaceLarge *)pEntry)->isStuc, cornerIdx, 1);
	}
	STUC_ASSERT("", false);
	//TODO return STUC_ERROR instead of bool, pass bool as out param
	return false;
}

bool stucGetIfOnInVert(const BorderFace *pEntry, const int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return stucIdxBitArray(((BorderFaceSmall *)pEntry)->onInVert, cornerIdx, 1);
		case 1:
			return stucIdxBitArray(((BorderFaceMid *)pEntry)->onInVert, cornerIdx, 1);
		case 2:
			return stucIdxBitArray(((BorderFaceLarge *)pEntry)->onInVert, cornerIdx, 1);
	}
	STUC_ASSERT("", false);
	return false;
}

bool stucGetIfOnLine(const BorderFace *pEntry, int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return stucIdxBitArray(((BorderFaceSmall *)pEntry)->onLine, cornerIdx, 1);
		case 1:
			return stucIdxBitArray(((BorderFaceMid *)pEntry)->onLine, cornerIdx, 1);
		case 2:
			return stucIdxBitArray(((BorderFaceLarge *)pEntry)->onLine, cornerIdx, 1);
	}
	STUC_ASSERT("", false);
	return false;
}

int32_t stucGetSegment(const BorderFace *pEntry, int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return stucIdxBitArray(((BorderFaceSmall *)pEntry)->segment, cornerIdx, 3);
		case 1:
			return stucIdxBitArray(((BorderFaceMid *)pEntry)->segment, cornerIdx, 4);
		case 2:
			return stucIdxBitArray(((BorderFaceLarge *)pEntry)->segment, cornerIdx, 5);
	}
	STUC_ASSERT("", false);
	return false;
}

int32_t stucGetMapCorner(const BorderFace *pEntry, const int32_t cornerIdx) {
	switch (pEntry->memType) {
		case 0:
			return stucIdxBitArray(((BorderFaceSmall *)pEntry)->stucCorner, cornerIdx, 3);
		case 1:
			return stucIdxBitArray(((BorderFaceMid *)pEntry)->stucCorner, cornerIdx, 4);
		case 2:
			return stucIdxBitArray(((BorderFaceLarge *)pEntry)->stucCorner, cornerIdx, 5);
	}
	STUC_ASSERT("", false);
	return false;
}

int32_t stucGetBaseCorner(const BorderFace *pEntry, int32_t cornerIdx) {
	switch (pEntry->memType) {
	case 0:
		return stucIdxBitArray(((BorderFaceSmall *)pEntry)->baseCorner, cornerIdx, 2);
	case 1:
		return stucIdxBitArray(((BorderFaceMid *)pEntry)->baseCorner, cornerIdx, 2);
	case 2:
		return stucIdxBitArray(((BorderFaceLarge *)pEntry)->baseCorner, cornerIdx, 2);
	}
	STUC_ASSERT("", false);
	return false;
}

V2_I16 stucGetTileMinFromBoundsEntry(BorderFace *pEntry) {
	//tileX and Y are signed, but are stored unsigned in pEntry
	V2_I16 tileMin = {0};
	bool sign = pEntry->tileX >> STUC_TILE_MIN_BIT_LEN & 0x1;
	tileMin.d[0] |= sign ? pEntry->tileX | ~0xfff : pEntry->tileX;
	sign = pEntry->tileY >> STUC_TILE_MIN_BIT_LEN & 0x1;
	tileMin.d[1] |= sign ? pEntry->tileY | ~0xfff : pEntry->tileY;
	return tileMin;
}

int32_t stucBufMeshGetVertIdx(const Piece *pPiece,
                          const BufMesh *pBufMesh, const int32_t localCorner) {
	bool isStuc = stucGetIfStuc(pPiece->pEntry, localCorner);
	bool isOnLine = stucGetIfOnLine(pPiece->pEntry, localCorner);
	int32_t vert = pBufMesh->mesh.core.pCorners[pPiece->bufFace.start - localCorner];
	if (!isStuc || isOnLine) {
		vert = stucConvertBorderVertIdx(pBufMesh, vert).realIdx;
	}
	return vert;
}

int32_t stucBufMeshGetEdgeIdx(const Piece *pPiece,
                              const BufMesh *pBufMesh, const int32_t localCorner) {
	bool isStuc = stucGetIfStuc(pPiece->pEntry, localCorner);
	bool isOnLine = stucGetIfOnLine(pPiece->pEntry, localCorner);
	int32_t edge = pBufMesh->mesh.core.pEdges[pPiece->bufFace.start - localCorner];
	if (!isStuc || isOnLine) {
		edge = stucConvertBorderEdgeIdx(pBufMesh, edge).realIdx;
	}
	return edge;
}