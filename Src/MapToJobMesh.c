#include <string.h>
#include <stdio.h>

#include <UvStucco.h>
#include <MapToJobMesh.h>
#include <MapFile.h>
#include <Context.h>
#include <Clock.h>
#include <AttribUtils.h>
#include <Utils.h>
#include <Error.h>
#include <Alloc.h>

static
void allocBufMesh(MappingJobVars *pVars, I32 cornerBufSize) {
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
	stucSetSpecialBufAttribs((BufMesh *)pMesh, 0x3e); //set all
	stucSetSpecialAttribs(pVars->pBasic->pCtx, pMesh, 0x40e); //set vert, normal, uv, and w scale
	stucSetAttribToDontCopy(pVars->pBasic->pCtx, pMesh, 0x400); //set w scale to DONT_COPY

	pMesh->cornerBufSize = cornerBufSize;
	pMesh->core.type.type = STUC_OBJECT_DATA_MESH_BUF;
}

static
void allocBufMeshAndTables(MappingJobVars *pVars, FaceCellsTable *pFaceCellsTable) {
	//struct timeval start, stop;
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
		stucLinAllocInit(
			pAlloc,
			&pVars->borderTableAlloc.pSmall,
			sizeof(BorderFaceSmall),
			initSize
		);
		stucLinAllocInit(
			pAlloc,
			&pVars->borderTableAlloc.pMid,
			sizeof(BorderFaceMid),
			initSize
		);
		stucLinAllocInit(
			pAlloc,
			&pVars->borderTableAlloc.pLarge,
			sizeof(BorderFaceLarge),
			initSize
		);
	}
	allocBufMesh(pVars, cornerBufSize);
	//pVars->pInVertTable = pAlloc->pCalloc(pVars->mesh.vertCount, 1);
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
	stucLinAllocInit(pAlloc,
		&pLocalTables->pVertTableAlloc,
		sizeof(LocalVert),
		pLocalTables->vertTableSize / 8 + 1
	);
#ifndef STUC_DISABLE_EDGES_IN_BUF
	stucLinAllocInit(pAlloc,
		&pLocalTables->pEdgeTableAlloc,
		sizeof(LocalEdge),
		pLocalTables->edgeTableSize / 8 + 1
	);
#endif

	stucLinAllocInit(pAlloc, &pVars->pCornerBufWrapAlloc, sizeof(CornerBufWrap), 1);
}

static
Result mapPerTile(
	MappingJobVars *pVars,
	FaceRange *pInFace,
	FaceCellsTable *pFaceCellsTable,
	DebugAndPerfVars *pDpVars,
	I32 faceIdx
) {
	Result result = STUC_NOT_SET;
	FaceBounds *pFaceBounds = 
		&stucIdxFaceCells(pFaceCellsTable, faceIdx, pVars->inFaceRange.start)->faceBounds;
	for (I32 j = pFaceBounds->min.d[1]; j <= pFaceBounds->max.d[1]; ++j) {
		for (I32 k = pFaceBounds->min.d[0]; k <= pFaceBounds->max.d[0]; ++k) {
			STUC_ASSERT("", k < 2048 && k > -2048 && j < 2048 && j > -2048);
			V2_F32 fTileMin = {(F32)k, (F32)j};
			V2_I32 tile = {k, j};
			result = stucMapToSingleFace(
				pVars,
				pFaceCellsTable,
				pDpVars,
				fTileMin,
				tile,
				pInFace
			);
			if (result != STUC_SUCCESS) {
				return result;
			}
		}
	}
	return result;
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

StucResult stucMapToJobMesh(void *pVarsPtr) {
	//CLOCK_INIT;
	Result result = STUC_NOT_SET;
	SendOffArgs *pSend = pVarsPtr;
	MappingJobVars vars = {0};
	vars.pBasic = pSend->pBasic;
	vars.id = pSend->id;
	vars.borderTable.size = pSend->borderTable.size;
	vars.pInFaces = pSend->pInFaces;
	vars.inFaceOffset = pSend->inFaceOffset;
	vars.inFaceRange = pSend->inFaceRange;
	I32 inFaceRangeSize = vars.inFaceRange.end - vars.inFaceRange.start;
	//CLOCK_START;
	FaceCellsTable faceCellsTable = {0};
	I32 averageMapFacesPerFace = 0;
	stucGetEncasingCells(
		&vars.pBasic->pCtx->alloc,
		vars.pBasic->pMap,
		vars.pBasic->pInMesh,
		vars.pBasic->maskIdx,
		vars.inFaceRange,
		&faceCellsTable,
		&averageMapFacesPerFace
	);
	//CLOCK_STOP("Get Encasing Cells Time");
	//CLOCK_START;
	vars.bufSize = inFaceRangeSize + faceCellsTable.cellFacesTotal;
	allocBufMeshAndTables(&vars, &faceCellsTable);
	//CLOCK_STOP("Alloc buffers and tables time");
	DebugAndPerfVars dpVars = {0};
	vars.pDpVars = &dpVars;
	//U64 mappingTime;
	//mappingTime = 0;
	if (vars.pBasic->ppInFaceTable) {
		vars.inFaceSize = 8;
		vars.pInFaces = vars.pBasic->pCtx->alloc.pCalloc(vars.inFaceSize, sizeof(InFaceArr));
	}
	for (I32 i = vars.inFaceRange.start; i < vars.inFaceRange.end; ++i) {
		if (vars.pBasic->maskIdx != -1 && vars.pBasic->pInMesh->pMatIdx &&
		    vars.pBasic->pInMesh->pMatIdx[i] != vars.pBasic->maskIdx) {

			continue;
		}
		//CLOCK_START;
		FaceRange inFace = {0};
		inFace.start = vars.pBasic->pInMesh->core.pFaces[i];
		inFace.end = vars.pBasic->pInMesh->core.pFaces[i + 1];
		inFace.size = inFace.end - inFace.start;
		inFace.idx = i;
		vars.tbn = stucBuildFaceTbn(inFace, vars.pBasic->pInMesh, NULL);
		//vars.tbnInv = mat3x3Invert(&vars.tbn);
		FaceTriangulated faceTris = {0};
		bool skipped = false;
		if (inFace.size > 4) {
			//TODO reimplement at some point
			// disabled cause current triangulation method is bad
			//faceTris = triangulateFace(vars.alloc, inFace, vars.mesh.pUvs,
			                           //NULL, 1);
		}
		if (inFace.size <= 4) {
			//face is a quad, or a tri
			result = mapPerTile(&vars, &inFace, &faceCellsTable, &dpVars, i);
		}
		else {
			skipped = true;
			result = STUC_SUCCESS;
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
				result = mapPerTile(&vars, &inFace, &faceCellsTable,
				                    &dpVars, i);
				if (result != STUC_SUCCESS) {
					break;
				}
			}
			*/
		}
		if (faceTris.pCorners) {
			vars.pBasic->pCtx->alloc.pFree(faceTris.pCorners);
			faceTris.pCorners = NULL;
		}
		//CLOCK_STOP_NO_PRINT;
		//mappingTime += CLOCK_TIME_DIFF(start, stop);
		//CLOCK_START;
		if (!skipped) {
			FaceCells *pFaceCellsEntry = stucIdxFaceCells(&faceCellsTable, i, vars.inFaceRange.start);
			stucDestroyFaceCellsEntry(&vars.pBasic->pCtx->alloc, pFaceCellsEntry);
		}
		//CLOCK_STOP_NO_PRINT;
		if (result != STUC_SUCCESS) {
			break;
		}
	}
	bool empty = !(vars.bufMesh.mesh.core.faceCount || vars.bufMesh.borderFaceCount);
	if (result == STUC_SUCCESS && !empty) {
		stucBufMeshSetLastFaces(&vars.pBasic->pCtx->alloc, &vars.bufMesh, &dpVars);
		pSend->reallocTime = dpVars.reallocTime;
		pSend->bufSize = vars.bufSize;
		pSend->rawBufSize = vars.rawBufSize;
		pSend->finalBufSize = vars.bufMesh.mesh.faceBufSize;
		//STUC_ASSERT("", !(!vars.borderTable.pTable ^ !vars.bufMesh.borderFaceCount));
		STUC_ASSERT("", vars.borderTable.pTable != NULL);
		pSend->borderTable.pTable = vars.borderTable.pTable;
		pSend->bufMesh = vars.bufMesh;
		pSend->pInFaces = vars.pInFaces;
		pSend->borderTableAlloc = vars.borderTableAlloc;
	}
	else if (empty) {
		result = STUC_SUCCESS;
	}
	stucLinAllocDestroy(vars.pCornerBufWrapAlloc);
	destroyMappingTables(&vars.pBasic->pCtx->alloc, &vars.localTables);
	stucDestroyFaceCellsTable(&vars.pBasic->pCtx->alloc, &faceCellsTable);
	STUC_ASSERT("", pSend->bufSize > 0 || empty);
	//printf("Average Faces Not Skipped: %d\n", dpVars.facesNotSkipped / inFaceRangeSize);
	//printf("Average total Faces comped: %d\n", dpVars.totalFacesComp / inFaceRangeSize);
	//printf("Average map faces per face: %d\n", averageMapFacesPerFace);
	return result;
}