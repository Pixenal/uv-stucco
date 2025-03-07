#include <string.h>
#include <stdio.h>

#include <uv_stucco.h>
#include <map_to_job_mesh.h>
#include <map.h>
#include <context.h>
#include <attrib_utils.h>
#include <utils.h>
#include <error.h>
#include <alloc.h>

static
Result allocBufMesh(MappingJobState *pState, I32 cornerBufSize) {
	Result err = STUC_SUCCESS;
	StucMap pMap = pState->pBasic->pMap;
	const StucMesh *pMeshIn = &pState->pBasic->pInMesh->core;
	Mesh *pMesh = &pState->bufMesh.mesh;
	const StucAlloc *pAlloc = &pState->pBasic->pCtx->alloc;
	pMesh->core.type.type = STUC_OBJECT_DATA_MESH_BUF;
	pMesh->faceBufSize = pState->bufSize;
	pMesh->cornerBufSize = pState->cornerBufSize;
	pMesh->edgeBufSize = pState->cornerBufSize;
	pMesh->vertBufSize = pState->bufSize;
	pMesh->core.pFaces = pAlloc->pMalloc(sizeof(I32) * pMesh->faceBufSize);
	pMesh->core.pCorners = pAlloc->pMalloc(sizeof(I32) * pMesh->cornerBufSize);
	pMesh->core.pEdges = pAlloc->pMalloc(sizeof(I32) * pMesh->edgeBufSize);
	//in-mesh is the active src,
	// unmatched active map attribs will not be marked active in the buf mesh
	const Mesh *srcs[2] = {(Mesh *)pMeshIn, pMap->pMesh};
	err = stucAllocAttribsFromMeshArr(
		pState->pBasic->pCtx,
		pMesh,
		2,
		srcs,
		0,
		true, true, false
	);
	STUC_THROW_IFNOT(err, "", 0);
	stucAppendBufOnlySpecialAttribs(&pState->pBasic->pCtx->alloc, &pState->bufMesh);
	err = stucSetSpecialBufAttribs((BufMesh *)pMesh, 0x3e); //set all
	STUC_THROW_IFNOT(err, "", 0);
	err = stucAssignActiveAliases(
		pState->pBasic->pCtx,
		pMesh,
		0x40e, //set vert, normal, uv, and w scale
		STUC_DOMAIN_NONE
	);
	STUC_THROW_IFNOT(err, "", 0);
	stucSetAttribCopyOpt(
		pState->pBasic->pCtx,
		&pMesh->core,
		STUC_ATTRIB_DONT_COPY,
		0x400 //set w scale to DONT_COPY
	);

	pMesh->cornerBufSize = cornerBufSize;
	pMesh->core.type.type = STUC_OBJECT_DATA_MESH_BUF;

	STUC_CATCH(0, err,
		stucMeshDestroy(pState->pBasic->pCtx, &pMesh->core);
	;);
	return err;
}

static
void destroyMappingTables(const StucAlloc *pAlloc, LocalTables *pLocalTables) {
	if (pLocalTables->pVertTableAlloc) {
		stucLinAllocDestroy(pLocalTables->pVertTableAlloc);
	}
	if (pLocalTables->pEdgeTableAlloc) {
		stucLinAllocDestroy(pLocalTables->pEdgeTableAlloc);
	}
	if (pLocalTables->pVertTable) {
		pAlloc->pFree(pLocalTables->pVertTable);
	}
	if (pLocalTables->pEdgeTable) {
		pAlloc->pFree(pLocalTables->pEdgeTable);
	}
}

static
Result allocBufMeshAndTables(MappingJobState *pState, FaceCellsTable *pFaceCellsTable) {
	Result err = STUC_SUCCESS;
	const StucAlloc *pAlloc = &pState->pBasic->pCtx->alloc;
	pState->rawBufSize = pState->bufSize;
	pState->bufSize = pState->bufSize / 20 + 2; //Add 2 incase it truncs to 0
	pState->bufSize += pState->bufSize % 2; //ensure it's even, so realloc is easier
	I32 cornerBufSize = pState->bufSize * 2;
	pState->cornerBufSize = cornerBufSize;
	pState->borderTable.pTable =
		pAlloc->pCalloc(pState->borderTable.size, sizeof(BorderBucket));
	{
		I32 initSize = pState->borderTable.size / 16 + 1;
		err = stucLinAllocInit(
			pAlloc,
			&pState->borderTableAlloc.pSmall,
			sizeof(BorderFaceSmall),
			initSize
		);
		STUC_THROW_IFNOT(err, "", 0);
		err = stucLinAllocInit(
			pAlloc,
			&pState->borderTableAlloc.pMid,
			sizeof(BorderFaceMid),
			initSize
		);
		STUC_THROW_IFNOT(err, "", 0);
		err = stucLinAllocInit(
			pAlloc,
			&pState->borderTableAlloc.pLarge,
			sizeof(BorderFaceLarge),
			initSize
		);
		STUC_THROW_IFNOT(err, "", 0);
	}
	err = allocBufMesh(pState, cornerBufSize);
	STUC_THROW_IFNOT(err, "", 0);

	//TODO: maybe reduce further if unifaces if low,
	//as a larger buf seems more necessary at higher face counts.
	//Doesn't provie much speed up at lower resolutions.
	LocalTables *pLocalTables = &pState->localTables;
	pLocalTables->vertTableSize = pFaceCellsTable->uniqueFaces / 4 + 1;
	pLocalTables->edgeTableSize = pFaceCellsTable->uniqueFaces / 3 + 1;
	pLocalTables->pVertTable =
		pAlloc->pCalloc(pLocalTables->vertTableSize, sizeof(LocalVert));
	pLocalTables->pEdgeTable =
		pAlloc->pCalloc(pLocalTables->edgeTableSize, sizeof(LocalEdge));
	err = stucLinAllocInit(pAlloc,
		&pLocalTables->pVertTableAlloc,
		sizeof(LocalVert),
		pLocalTables->vertTableSize / 8 + 1
	);
	STUC_THROW_IFNOT(err, "", 0);
#ifndef STUC_DISABLE_EDGES_IN_BUF
	err = stucLinAllocInit(pAlloc,
		&pLocalTables->pEdgeTableAlloc,
		sizeof(LocalEdge),
		pLocalTables->edgeTableSize / 8 + 1
	);
	STUC_THROW_IFNOT(err, "", 0);
#endif

	err = stucLinAllocInit(pAlloc, &pState->pCornerBufWrapAlloc, sizeof(CornerBufWrap), 1);
	STUC_THROW_IFNOT(err, "", 0);

	STUC_CATCH(0, err, 
		if (pState->pCornerBufWrapAlloc) {
			stucLinAllocDestroy(pState->pCornerBufWrapAlloc);
		}
		destroyMappingTables(pAlloc, &pState->localTables);
		stucBorderTableDestroyAlloc(&pState->borderTableAlloc);
		if (pState->borderTable.pTable) {
			pAlloc->pFree(pState->borderTable.pTable);
		}
	;);
	return err;
}

static
Result mapPerTile(
	MappingJobState *pState,
	FaceRange *pInFace,
	FaceCellsTable *pFaceCellsTable,
	I32 faceIdx
) {
	Result err = STUC_SUCCESS;
	FaceBounds *pFaceBounds = 
		&stucIdxFaceCells(pFaceCellsTable, faceIdx, pState->inFaceRange.start)->faceBounds;
	for (I32 j = pFaceBounds->min.d[1]; j <= pFaceBounds->max.d[1]; ++j) {
		for (I32 k = pFaceBounds->min.d[0]; k <= pFaceBounds->max.d[0]; ++k) {
			STUC_ASSERT("", k < 2048 && k > -2048 && j < 2048 && j > -2048);
			V2_I32 tile = {k, j};
			err = stucMapToSingleFace(
				pState,
				pFaceCellsTable,
				tile,
				pInFace
			);
			STUC_RETURN_ERR_IFNOT(err, "");
		}
	}
	return err;
}

static
Result mapToFaces(MappingJobState *pState, FaceCellsTable *pFaceCellsTable) {
	Result err = STUC_SUCCESS;
	STUC_ASSERT("record stores tiles with 16 bits earch", STUC_TILE_BIT_LEN <= 16);
	MapToMeshBasic *pBasic = pState->pBasic;
	for (I32 i = pState->inFaceRange.start; i < pState->inFaceRange.end; ++i) {
		if (pBasic->maskIdx != -1 && pBasic->pInMesh->pMatIdx &&
		    pBasic->pInMesh->pMatIdx[i] != pBasic->maskIdx) {

			continue;
		}
		FaceRange inFace = {0};
		inFace.start = pBasic->pInMesh->core.pFaces[i];
		inFace.end = pBasic->pInMesh->core.pFaces[i + 1];
		inFace.size = inFace.end - inFace.start;
		inFace.idx = i;
		pState->tbn = stucBuildFaceTbn(inFace, pBasic->pInMesh, NULL);
		//pState->tbnInv = mat3x3Invert(&pState->tbn);
		FaceTriangulated faceTris = {0};
		bool skipped = false;
		if (inFace.size > 4) {
			//TODO reimplement at some point
			// disabled cause current triangulation method is bad
			//faceTris = triangulateFace(pState->alloc, inFace, pState->mesh.pUvs,
			                           //NULL, 1);
		}
		if (inFace.size <= 4) {
			//face is a quad, or a tri
			err = mapPerTile(pState, &inFace, pFaceCellsTable, i);
		}
		else {
			skipped = true;
			//face is an ngon. ngons are processed per tri
			//TODO re-enable when triangulation method is improved.
			//how will extra in faces be handled (given we're splitting
			// a face into tris). Will probably need to merge the resulting
			//clipped faces into a single one in this func?
			/*
			for (I32 j = 0; j < faceTris.triCount; ++j) {
				I32 triFaceStart = j * 3;
				inFace.start = faceTris.pCorners[triFaceStart];
				inFace.end = faceTris.pCorners[triFaceStart + 2];
				inFace.size = inFace.end - inFace.start;
				result = mapPerTile(pState, &inFace, &faceCellsTable,
				                    &dpState, i);
				if (result != STUC_SUCCESS) {
					break;
				}
			}
			*/
		}
		if (faceTris.pCorners) {
			pBasic->pCtx->alloc.pFree(faceTris.pCorners);
			faceTris.pCorners = NULL;
		}
		if (!skipped) {
			FaceCells *pFaceCellsEntry =
				stucIdxFaceCells(pFaceCellsTable, i, pState->inFaceRange.start);
			stucDestroyFaceCellsEntry(&pBasic->pCtx->alloc, pFaceCellsEntry);
		}
		STUC_RETURN_ERR_IFNOT(err, "");
	}
	return err;
}

StucResult stucMapToJobMesh(void *pArgsVoid) {
	Result err = STUC_SUCCESS;
	MappingJobArgs *pArgs = pArgsVoid;
	STUC_ASSERT("", pArgs);
	StucContext pCtx = pArgs->pBasic->pCtx;

	MappingJobState state = {0};
	state.pBasic = pArgs->pBasic;
	state.id = pArgs->id;
	state.borderTable.size = pArgs->borderTable.size;
	state.inFaceRange = pArgs->inFaceRange;
	I32 inFaceRangeSize = state.inFaceRange.end - state.inFaceRange.start;

	FaceCellsTable faceCellsTable = {0};
	I32 averageMapFacesPerFace = 0;
	printf("A");
	stucGetEncasingCells(
		&pCtx->alloc,
		state.pBasic->pMap,
		state.pBasic->pInMesh,
		state.pBasic->maskIdx,
		state.inFaceRange,
		&faceCellsTable,
		&averageMapFacesPerFace
	);
	printf("B");
	STUC_THROW_IFNOT(err, "", 0);
	state.bufSize = inFaceRangeSize + faceCellsTable.cellFacesTotal;
	err = allocBufMeshAndTables(&state, &faceCellsTable);
	STUC_THROW_IFNOT(err, "", 0);
	if (state.pBasic->ppInFaceTable) {
		state.inFaceSize = 8;
		state.pInFaces = pCtx->alloc.pCalloc(state.inFaceSize, sizeof(InFaceArr));
	}
	printf("C");
	err = mapToFaces(&state, &faceCellsTable);
	STUC_THROW_IFNOT(err, "", 0);
	printf("D");
	bool empty = !(state.bufMesh.mesh.core.faceCount || state.bufMesh.borderFaceCount);
	STUC_ASSERT("", state.bufSize > 0 || empty);
	if (!empty) {
		stucBufMeshSetLastFaces(pCtx, &state.bufMesh);
		pArgs->bufSize = state.bufSize;
		pArgs->rawBufSize = state.rawBufSize;
		pArgs->finalBufSize = state.bufMesh.mesh.faceBufSize;
		STUC_ASSERT("", state.borderTable.pTable);
		pArgs->borderTable.pTable = state.borderTable.pTable;
		pArgs->bufMesh = state.bufMesh;
		pArgs->pInFaces = state.pInFaces;
		pArgs->borderTableAlloc = state.borderTableAlloc;
		pArgs->facesChecked = state.facesChecked;
		pArgs->facesUsed = state.facesUsed;
	}
	STUC_CATCH(0, err, ;);
	if (state.pCornerBufWrapAlloc) {
		stucLinAllocDestroy(state.pCornerBufWrapAlloc);
	}
	destroyMappingTables(&pCtx->alloc, &state.localTables);
	stucDestroyFaceCellsTable(&pCtx->alloc, &faceCellsTable, state.inFaceRange);
	//printf("Average map faces per face: %d\n", averageMapFacesPerFace);
	return err;
}