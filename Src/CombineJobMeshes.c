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
void combineJobInFaceLists(
	MapToMeshBasic *pBasic,
	MappingJobArgs *pMappingJobArgs,
	I32 mapJobsSent
) {
	I32 face = 0;
	for (I32 i = 0; i < mapJobsSent; ++i) {
		I32 faceCount = pMappingJobArgs[i].bufMesh.mesh.core.faceCount;
		for (I32 j = 0; j < faceCount; ++j) {
			(*pBasic->ppInFaceTable)[face] = pMappingJobArgs[i].pInFaces[j];
			face++;
		}
		pBasic->pCtx->alloc.pFree(pMappingJobArgs[i].pInFaces);
	}
}

Result stucCombineJobMeshes(
	MapToMeshBasic *pBasic,
	MappingJobArgs *pMappingJobArgs,
	I32 mapJobsSent
) {
	Result err = STUC_SUCCESS;
	//TODO fix this naming inconsistancy. fix with in-mesh as well
	Mesh *pMeshOut = &pBasic->outMesh;
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	//TODO figure out how to handle edges in local meshes,
	//probably just add internal edges to local mesh,
	//and figure out edges in border faces after jobs are finished?
	//You'll need to provide functionality for interpolating and blending
	//edge data, so keep that in mind.
	pMeshOut->core.type.type = STUC_OBJECT_DATA_MESH_INTERN;
	MeshCounts totalCount = {0};
	MeshCounts totalBoundsCount = {0};
	for (I32 i = 0; i < mapJobsSent; ++i) {
		stucAddToMeshCounts(
			&totalCount,
			&totalBoundsCount,
			(Mesh *)&pMappingJobArgs[i].bufMesh
		);
	}
	//3 for last face index
	pMeshOut->faceBufSize = 3 + totalCount.faces + totalBoundsCount.faces / 10;
	pMeshOut->cornerBufSize = 2 + totalCount.corners + totalBoundsCount.corners / 10;
	pMeshOut->edgeBufSize = 2 + totalCount.edges + totalBoundsCount.edges / 10;
	pMeshOut->vertBufSize = 2 + totalCount.verts + totalBoundsCount.verts / 10;
	pMeshOut->core.pFaces = pAlloc->pMalloc(sizeof(I32) * pMeshOut->faceBufSize);
	pMeshOut->core.pCorners = pAlloc->pMalloc(sizeof(I32) * pMeshOut->cornerBufSize);
	pMeshOut->core.pEdges = pAlloc->pMalloc(sizeof(I32) * pMeshOut->cornerBufSize);
	//only need to use the first buf mesh, as attribs are the same across all jobs
	Mesh *src = NULL;
	//get first bufmesh that isn't empty
	for (I32 i = 0; i < mapJobsSent; ++i) {
		if (pMappingJobArgs[i].bufSize) {
			src = &pMappingJobArgs[i].bufMesh.mesh;
			break;
		}
	}
	stucAllocAttribsFromMeshArr(pAlloc, pMeshOut, 1, &src, false);
	err = stucSetSpecialAttribs(pBasic->pCtx, pMeshOut, 0xe); //1110 - set only verts, stuc, & normals
	JobBases *pMappingJobBases = NULL;
	STUC_THROW_IFNOT(err, "", 0);
	pMappingJobBases = pAlloc->pMalloc(sizeof(JobBases) * mapJobsSent);
	U64 reallocTime = 0;
	for (I32 i = 0; i < mapJobsSent; ++i) {
		reallocTime += pMappingJobArgs[i].reallocTime;
#ifdef WIN32
		//printf("realloc time %llu\n", pMappingJobArgs[i].reallocTime);
#else
		//printf("realloc time %lu\n", pMappingJobArgs[i].reallocTime);
#endif
		//printf("rawbufSize: %d | ", pMappingJobArgs[i].rawBufSize);
		//printf("bufSize: %d | ", pMappingJobArgs[i].bufSize);
		//printf("finalbufSize: %d | \n\n", pMappingJobArgs[i].finalBufSize);
		if (pMappingJobArgs[i].bufSize) { //don't copy if mesh is empty
			pMappingJobBases[i].vertBase = pMeshOut->core.vertCount;
			pMappingJobBases[i].edgeBase = pMeshOut->core.edgeCount;
			err = stucCopyMesh(&pMeshOut->core, &pMappingJobArgs[i].bufMesh.mesh.core);
			STUC_THROW_IFNOT(err, "", 0);
		}
	}
	if (pBasic->ppInFaceTable) {
		*pBasic->ppInFaceTable = pAlloc->pCalloc(pMeshOut->faceBufSize, sizeof(InFaceArr));
		combineJobInFaceLists(pBasic, pMappingJobArgs, mapJobsSent);
	}
#ifdef WIN32
	//printf("realloc time total %llu\n", reallocTime);
#else
	//printf("realloc time total %lu\n", reallocTime);
#endif
	err = stucMergeBorderFaces(pBasic, pMappingJobArgs, pMappingJobBases, mapJobsSent);
	STUC_THROW_IFNOT(err, "", 0);

	stucMeshSetLastFace(&pBasic->pCtx->alloc, pMeshOut);
	STUC_CATCH(0, err,
		stucMeshDestroy(pBasic->pCtx, &pBasic->outMesh.core);
	;);
	if (pMappingJobBases) {
		pBasic->pCtx->alloc.pFree(pMappingJobBases);
	}
	return err;
}

//Returns struct containing inMesh corner, edge, vert, and local corner,
//for the current buf corner in the given border face entry.
//The start corner of the face is also given.
//Though for face end, and size, call getFaceRange
BorderInInfo stucGetBorderEntryInInfo(
	const MapToMeshBasic *pBasic,
	const BorderFace *pEntry,
	const I32 cornerIdx
) {
	BorderInInfo inInfo = {0};
	STUC_ASSERT("", pEntry->inFace >= 0);
	inInfo.cornerLocal = stucGetBaseCorner(pEntry, cornerIdx);
	STUC_ASSERT("", inInfo.cornerLocal >= 0);
	inInfo.start = pBasic->pInMesh->core.pFaces[pEntry->inFace];
	inInfo.end = pBasic->pInMesh->core.pFaces[pEntry->inFace + 1];
	inInfo.size = inInfo.end - inInfo.start;
	STUC_ASSERT("", inInfo.start >= 0);
	STUC_ASSERT("", inInfo.start < pBasic->pInMesh->core.cornerCount);
	STUC_ASSERT("", inInfo.cornerLocal < inInfo.size);
	inInfo.edgeCorner = inInfo.start + inInfo.cornerLocal;
	STUC_ASSERT("", inInfo.edgeCorner < pBasic->pInMesh->core.cornerCount);
	inInfo.edge = pBasic->pInMesh->core.pEdges[inInfo.edgeCorner];
	STUC_ASSERT("", inInfo.edge < pBasic->pInMesh->core.edgeCount);
	bool useNextVert = pEntry->inOrient ^ pEntry->mapOrient;
	inInfo.vertCorner = (inInfo.cornerLocal + useNextVert) % inInfo.size;
	inInfo.vertCorner += inInfo.start;
	inInfo.vert = pBasic->pInMesh->core.pCorners[inInfo.vertCorner];
	return inInfo;
}

bool stucGetIfStuc(const BorderFace *pEntry, const I32 cornerIdx) {
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

bool stucGetIfOnInVert(const BorderFace *pEntry, const I32 cornerIdx) {
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

bool stucGetIfOnLine(const BorderFace *pEntry, I32 cornerIdx) {
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

I32 stucGetSegment(const BorderFace *pEntry, I32 cornerIdx) {
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

I32 stucGetMapCorner(const BorderFace *pEntry, const I32 cornerIdx) {
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

I32 stucGetBaseCorner(const BorderFace *pEntry, I32 cornerIdx) {
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

I32 stucBufMeshGetVertIdx(
	const Piece *pPiece,
	const BufMesh *pBufMesh,
	const I32 localCorner
) {
	bool isStuc = stucGetIfStuc(pPiece->pEntry, localCorner);
	bool isOnLine = stucGetIfOnLine(pPiece->pEntry, localCorner);
	I32 vert = pBufMesh->mesh.core.pCorners[pPiece->bufFace.start - localCorner];
	if (!isStuc || isOnLine) {
		vert = stucConvertBorderVertIdx(pBufMesh, vert).realIdx;
	}
	return vert;
}

I32 stucBufMeshGetEdgeIdx(
	const Piece *pPiece,
	const BufMesh *pBufMesh,
	const I32 localCorner
) {
	bool isStuc = stucGetIfStuc(pPiece->pEntry, localCorner);
	bool isOnLine = stucGetIfOnLine(pPiece->pEntry, localCorner);
	I32 edge = pBufMesh->mesh.core.pEdges[pPiece->bufFace.start - localCorner];
	if (!isStuc || isOnLine) {
		edge = stucConvertBorderEdgeIdx(pBufMesh, edge).realIdx;
	}
	return edge;
}