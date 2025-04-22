/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <string.h>
#include <stdio.h>

#include <uv_stucco.h>
#include <map_to_job_mesh.h>
#include <map.h>
#include <context.h>
#include <attrib_utils.h>
#include <utils.h>
#include <pixenals_error_utils.h>
#include <pixenals_alloc_utils.h>

static
StucErr getEncasedFacesPerTile(
	FindEncasedFacesJobArgs *pArgs,
	FaceRange *pInFace,
	FaceCellsTable *pFaceCellsTable,
	I32 faceIdx
) {
	StucErr err = PIX_ERR_SUCCESS;
	FaceBounds *pFaceBounds = 
		&stucIdxFaceCells(pFaceCellsTable, faceIdx, pArgs->core.range.start)->faceBounds;
	for (I32 j = pFaceBounds->min.d[1]; j <= pFaceBounds->max.d[1]; ++j) {
		for (I32 k = pFaceBounds->min.d[0]; k <= pFaceBounds->max.d[0]; ++k) {
			if (j < INT16_MIN || j > INT16_MAX || k < INT16_MIN || k > INT16_MAX) {
				continue;
			}
			V2_I16 tile = {k, j};
			err = stucGetEncasedFacesPerFace(
				pArgs,
				pFaceCellsTable,
				tile,
				pInFace
			);
			PIX_ERR_RETURN_IFNOT(err, "");
		}
	}
	return err;
}

static
StucErr getEncasedFaces(FindEncasedFacesJobArgs *pArgs, FaceCellsTable *pFaceCellsTable) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("record stores tiles with 16 bits earch", STUC_TILE_BIT_LEN <= 16);
	const MapToMeshBasic *pBasic = pArgs->core.pBasic;
	for (I32 i = pArgs->core.range.start; i < pArgs->core.range.end; ++i) {
		if (pBasic->maskIdx != -1 && pBasic->pInMesh->pMatIdx &&
		    pBasic->pInMesh->pMatIdx[i] != pBasic->maskIdx) {

			continue;
		}
		FaceRange inFace = {0};
		inFace.start = pBasic->pInMesh->core.pFaces[i];
		inFace.end = pBasic->pInMesh->core.pFaces[i + 1];
		inFace.size = inFace.end - inFace.start;
		inFace.idx = i;
		bool skipped = false;
		if (inFace.size <= 4) {
			err = getEncasedFacesPerTile(pArgs, &inFace, pFaceCellsTable, i);
		}
		else {
			skipped = true;
		}
		if (!skipped) {
			FaceCells *pFaceCellsEntry =
				stucIdxFaceCells(pFaceCellsTable, i, pArgs->core.range.start);
			stucDestroyFaceCellsEntry(&pBasic->pCtx->alloc, pFaceCellsEntry);
		}
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	return err;
}

StucErr stucFindEncasedFaces(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	FindEncasedFacesJobArgs *pArgs = pArgsVoid;
	PIX_ERR_ASSERT("", pArgs);
	StucContext pCtx = pArgs->core.pBasic->pCtx;

	FaceCellsTable faceCellsTable = {0};
	I32 averageMapFacesPerFace = 0;
	stucGetEncasingCells(
		&pCtx->alloc,
		pArgs->core.pBasic->pMap,
		pArgs->core.pBasic->pInMesh,
		pArgs->core.pBasic->maskIdx,
		pArgs->core.range,
		&faceCellsTable,
		&averageMapFacesPerFace
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	EncasedMapFaceTableState tableState =  {.pBasic = pArgs->core.pBasic};
	stucHTableInit(
		&pArgs->core.pBasic->pCtx->alloc,
		&pArgs->encasedFaces,
		faceCellsTable.uniqueFaces / 4 + 1,
		(I32Arr) {.pArr = (I32[]) {sizeof(EncasedMapFace)}, .count = 1},
		&tableState
	);
	err = getEncasedFaces(pArgs, &faceCellsTable);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err, ;);
	stucDestroyFaceCellsTable(&pCtx->alloc, &faceCellsTable, pArgs->core.range);
	return err;
}
