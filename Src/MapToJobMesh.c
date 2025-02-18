#include <string.h>
#include <stdio.h>

#include <UvStucco.h>
#include <MapToJobMesh.h>
#include <MapFile.h>
#include <Context.h>
#include <AttribUtils.h>
#include <Utils.h>
#include <Error.h>
#include <Alloc.h>

static
Result allocBufMesh(MappingJobVars *pVars, I32 cornerBufSize) {
	Result err = STUC_SUCCESS;
	StucMap pMap = pVars->pBasic->pMap;
	const StucMesh *pMeshIn = &pVars->pBasic->pInMesh->core;
	Mesh *pMesh = &pVars->bufMesh.mesh;
	const StucAlloc *pAlloc = &pVars->pBasic->pCtx->alloc;
	pMesh->core.type.type = STUC_OBJECT_DATA_MESH_BUF;
	pMesh->faceBufSize = pVars->bufSize;
	pMesh->cornerBufSize = pVars->cornerBufSize;
	pMesh->edgeBufSize = pVars->cornerBufSize;
	pMesh->vertBufSize = pVars->bufSize;
	pMesh->core.pFaces = pAlloc->pMalloc(sizeof(I32) * pMesh->faceBufSize);
	pMesh->core.pCorners = pAlloc->pMalloc(sizeof(I32) * pMesh->cornerBufSize);
	pMesh->core.pEdges = pAlloc->pMalloc(sizeof(I32) * pMesh->edgeBufSize);
	Mesh *srcs[2] = {(Mesh *)pMeshIn, &pMap->mesh};
	stucAllocAttribsFromMeshArr(&pVars->pBasic->pCtx->alloc, pMesh, 2, srcs, true);
	stucAppendBufOnlySpecialAttribs(&pVars->pBasic->pCtx->alloc, &pVars->bufMesh);
	err = stucSetSpecialBufAttribs((BufMesh *)pMesh, 0x3e); //set all
	STUC_THROW_IFNOT(err, "", 0);
	err = stucSetSpecialAttribs(pVars->pBasic->pCtx, pMesh, 0x40e); //set vert, normal, uv, and w scale
	STUC_THROW_IFNOT(err, "", 0);
	stucSetAttribToDontCopy(pVars->pBasic->pCtx, pMesh, 0x400); //set w scale to DONT_COPY

	pMesh->cornerBufSize = cornerBufSize;
	pMesh->core.type.type = STUC_OBJECT_DATA_MESH_BUF;

	STUC_CATCH(0, err,
		stucMeshDestroy(pVars->pBasic->pCtx, &pMesh->core);
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
Result allocBufMeshAndTables(MappingJobVars *pVars, FaceCellsTable *pFaceCellsTable) {
	Result err = STUC_SUCCESS;
	const StucAlloc *pAlloc = &pVars->pBasic->pCtx->alloc;
	pVars->rawBufSize = pVars->bufSize;
	pVars->bufSize = pVars->bufSize / 20 + 2; //Add 2 incase it truncs to 0
	pVars->bufSize += pVars->bufSize % 2; //ensure it's even, so realloc is easier
	I32 cornerBufSize = pVars->bufSize * 2;
	pVars->cornerBufSize = cornerBufSize;
	pVars->borderTable.pTable =
		pAlloc->pCalloc(pVars->borderTable.size, sizeof(BorderBucket));
	{
		I32 initSize = pVars->borderTable.size / 16 + 1;
		err = stucLinAllocInit(
			pAlloc,
			&pVars->borderTableAlloc.pSmall,
			sizeof(BorderFaceSmall),
			initSize
		);
		STUC_THROW_IFNOT(err, "", 0);
		err = stucLinAllocInit(
			pAlloc,
			&pVars->borderTableAlloc.pMid,
			sizeof(BorderFaceMid),
			initSize
		);
		STUC_THROW_IFNOT(err, "", 0);
		err = stucLinAllocInit(
			pAlloc,
			&pVars->borderTableAlloc.pLarge,
			sizeof(BorderFaceLarge),
			initSize
		);
		STUC_THROW_IFNOT(err, "", 0);
	}
	err = allocBufMesh(pVars, cornerBufSize);
	STUC_THROW_IFNOT(err, "", 0);

	//TODO: maybe reduce further if unifaces if low,
	//as a larger buf seems more necessary at higher face counts.
	//Doesn't provie much speed up at lower resolutions.
	LocalTables *pLocalTables = &pVars->localTables;
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

	err = stucLinAllocInit(pAlloc, &pVars->pCornerBufWrapAlloc, sizeof(CornerBufWrap), 1);
	STUC_THROW_IFNOT(err, "", 0);

	STUC_CATCH(0, err, 
		if (pVars->pCornerBufWrapAlloc) {
			stucLinAllocDestroy(pVars->pCornerBufWrapAlloc);
		}
		destroyMappingTables(pAlloc, &pVars->localTables);
		stucBorderTableDestroyAlloc(&pVars->borderTableAlloc);
		if (pVars->borderTable.pTable) {
			pAlloc->pFree(pVars->borderTable.pTable);
		}
	;);
	return err;
}

static
Result mapPerTile(
	MappingJobVars *pVars,
	FaceRange *pInFace,
	FaceCellsTable *pFaceCellsTable,
	DebugAndPerfVars *pDpVars,
	I32 faceIdx
) {
	Result err = STUC_SUCCESS;
	FaceBounds *pFaceBounds = 
		&stucIdxFaceCells(pFaceCellsTable, faceIdx, pVars->inFaceRange.start)->faceBounds;
	for (I32 j = pFaceBounds->min.d[1]; j <= pFaceBounds->max.d[1]; ++j) {
		for (I32 k = pFaceBounds->min.d[0]; k <= pFaceBounds->max.d[0]; ++k) {
			STUC_ASSERT("", k < 2048 && k > -2048 && j < 2048 && j > -2048);
			V2_F32 fTileMin = {(F32)k, (F32)j};
			V2_I32 tile = {k, j};
			err = stucMapToSingleFace(
				pVars,
				pFaceCellsTable,
				pDpVars,
				fTileMin,
				tile,
				pInFace
			);
			STUC_RETURN_ERR_IFNOT(err, "");
		}
	}
	return err;
}

static
Result mapToFaces(
	MappingJobVars *pVars,
	DebugAndPerfVars *pDpVars,
	FaceCellsTable *pFaceCellsTable
) {
	Result err = STUC_SUCCESS;
	for (I32 i = pVars->inFaceRange.start; i < pVars->inFaceRange.end; ++i) {
		if (pVars->pBasic->maskIdx != -1 && pVars->pBasic->pInMesh->pMatIdx &&
		    pVars->pBasic->pInMesh->pMatIdx[i] != pVars->pBasic->maskIdx) {

			continue;
		}
		FaceRange inFace = {0};
		inFace.start = pVars->pBasic->pInMesh->core.pFaces[i];
		inFace.end = pVars->pBasic->pInMesh->core.pFaces[i + 1];
		inFace.size = inFace.end - inFace.start;
		inFace.idx = i;
		pVars->tbn = stucBuildFaceTbn(inFace, pVars->pBasic->pInMesh, NULL);
		//pVars->tbnInv = mat3x3Invert(&pVars->tbn);
		FaceTriangulated faceTris = {0};
		bool skipped = false;
		if (inFace.size > 4) {
			//TODO reimplement at some point
			// disabled cause current triangulation method is bad
			//faceTris = triangulateFace(pVars->alloc, inFace, pVars->mesh.pUvs,
			                           //NULL, 1);
		}
		if (inFace.size <= 4) {
			//face is a quad, or a tri
			err = mapPerTile(pVars, &inFace, pFaceCellsTable, pDpVars, i);
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
				result = mapPerTile(pVars, &inFace, &faceCellsTable,
				                    &dpVars, i);
				if (result != STUC_SUCCESS) {
					break;
				}
			}
			*/
		}
		if (faceTris.pCorners) {
			pVars->pBasic->pCtx->alloc.pFree(faceTris.pCorners);
			faceTris.pCorners = NULL;
		}
		if (!skipped) {
			FaceCells *pFaceCellsEntry =
				stucIdxFaceCells(pFaceCellsTable, i, pVars->inFaceRange.start);
			stucDestroyFaceCellsEntry(&pVars->pBasic->pCtx->alloc, pFaceCellsEntry);
		}
		STUC_RETURN_ERR_IFNOT(err, "");
	}
	return err;
}

StucResult stucMapToJobMesh(void *pVarsPtr) {
	Result err = STUC_SUCCESS;
	SendOffArgs *pSend = pVarsPtr;
	STUC_ASSERT("", pSend);
	StucContext pCtx = pSend->pBasic->pCtx;

	MappingJobVars vars = {0};
	vars.pBasic = pSend->pBasic;
	vars.id = pSend->id;
	vars.borderTable.size = pSend->borderTable.size;
	vars.pInFaces = pSend->pInFaces;
	vars.inFaceOffset = pSend->inFaceOffset;
	vars.inFaceRange = pSend->inFaceRange;
	I32 inFaceRangeSize = vars.inFaceRange.end - vars.inFaceRange.start;

	FaceCellsTable faceCellsTable = {0};
	I32 averageMapFacesPerFace = 0;
	stucGetEncasingCells(
		&pCtx->alloc,
		vars.pBasic->pMap,
		vars.pBasic->pInMesh,
		vars.pBasic->maskIdx,
		vars.inFaceRange,
		&faceCellsTable,
		&averageMapFacesPerFace
	);
	STUC_THROW_IFNOT(err, "", 0);
	vars.bufSize = inFaceRangeSize + faceCellsTable.cellFacesTotal;
	err = allocBufMeshAndTables(&vars, &faceCellsTable);
	STUC_THROW_IFNOT(err, "", 0);
	DebugAndPerfVars dpVars = {0};
	vars.pDpVars = &dpVars;
	if (vars.pBasic->ppInFaceTable) {
		vars.inFaceSize = 8;
		vars.pInFaces = pCtx->alloc.pCalloc(vars.inFaceSize, sizeof(InFaceArr));
	}
	err = mapToFaces(&vars, &dpVars, &faceCellsTable);
	STUC_THROW_IFNOT(err, "", 0);

	bool empty = !(vars.bufMesh.mesh.core.faceCount || vars.bufMesh.borderFaceCount);
	STUC_ASSERT("", vars.bufSize > 0 || empty);
	if (!empty) {
		stucBufMeshSetLastFaces(&pCtx->alloc, &vars.bufMesh, &dpVars);
		pSend->reallocTime = dpVars.reallocTime;
		pSend->bufSize = vars.bufSize;
		pSend->rawBufSize = vars.rawBufSize;
		pSend->finalBufSize = vars.bufMesh.mesh.faceBufSize;
		STUC_ASSERT("", vars.borderTable.pTable);
		pSend->borderTable.pTable = vars.borderTable.pTable;
		pSend->bufMesh = vars.bufMesh;
		pSend->pInFaces = vars.pInFaces;
		pSend->borderTableAlloc = vars.borderTableAlloc;
	}
	STUC_CATCH(0, err, ;);
	stucLinAllocDestroy(vars.pCornerBufWrapAlloc);
	destroyMappingTables(&pCtx->alloc, &vars.localTables);
	stucDestroyFaceCellsTable(&pCtx->alloc, &faceCellsTable, vars.inFaceRange);
	//printf("Average Faces Not Skipped: %d\n", dpVars.facesNotSkipped / inFaceRangeSize);
	//printf("Average total Faces comped: %d\n", dpVars.totalFacesComp / inFaceRangeSize);
	//printf("Average map faces per face: %d\n", averageMapFacesPerFace);
	return err;
}