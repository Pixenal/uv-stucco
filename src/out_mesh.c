/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <uv_stucco_intern.h>
#include <utils.h>
#include <attrib_utils.h>
#include <merge_and_snap.h>

StucErr stucInitOutMesh(MapToMeshBasic *pBasic, HTable *pMergeTable, I32 snappedVerts) {
	StucErr err = PIX_ERR_SUCCESS;
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	Mesh *pMesh = &pBasic->outMesh;
	pMesh->core.type.type = STUC_OBJECT_DATA_MESH_BUF;
	PixalcLinAlloc *pVertAlloc = stucHTableAllocGet(pMergeTable, 0);
	PixalcLinAlloc *pIntersectVertAlloc = stucHTableAllocGet(pMergeTable, 1);
	I32 bufVertTotal = pixalcLinAllocGetCount(pVertAlloc);
	bufVertTotal += pixalcLinAllocGetCount(pIntersectVertAlloc);
	bufVertTotal -= snappedVerts;
	pMesh->faceBufSize = bufVertTotal;
	pMesh->cornerBufSize = pMesh->faceBufSize * 2;
	pMesh->edgeBufSize = pMesh->cornerBufSize;
	pMesh->vertBufSize = pMesh->faceBufSize;
	pMesh->core.pFaces = pAlloc->fpMalloc(sizeof(I32) * pMesh->faceBufSize);
	pMesh->core.pCorners = pAlloc->fpMalloc(sizeof(I32) * pMesh->cornerBufSize);
	//pMesh->core.pEdges = pAlloc->fpMalloc(sizeof(I32) * pMesh->edgeBufSize);

	//in-mesh is the active src,
	// unmatched active map attribs will not be marked active
	const Mesh *srcs[2] = {pBasic->pInMesh, pBasic->pMap->pMesh};
	err = stucAllocAttribsFromMeshArr(
		pBasic->pCtx,
		pMesh,
		2,
		srcs,
		0,
		true, true, false
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = stucAssignActiveAliases(
		pBasic->pCtx,
		pMesh,
		STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) {
			STUC_ATTRIB_USE_POS,
			STUC_ATTRIB_USE_UV,
			STUC_ATTRIB_USE_NORMAL
		})),
		STUC_DOMAIN_NONE
	);
	PIX_ERR_CATCH(0, err,
		stucMeshDestroy(pBasic->pCtx, &pMesh->core);
	;);
	return err;
}

void stucAddVertsToOutMesh(
	MapToMeshBasic *pBasic,
	HTable *pMergeTable,
	I32 vertAllocIdx
) {
	PixalcLinAlloc *pVertAlloc = stucHTableAllocGet(pMergeTable, vertAllocIdx);
	PixalcLinAllocIter iter = {0};
	pixalcLinAllocIterInit(pVertAlloc, (Range) {0, INT32_MAX}, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		VertMerge *pEntry = pixalcLinAllocGetItem(&iter);
		if (vertAllocIdx == 1) { //TODO make an enum for vert alloc types
			VertMergeIntersect *pIntersect = (VertMergeIntersect *)pEntry;
			if (pIntersect->pSnapTo) {
				continue; //vert has been snapped to another - skip
			}
		}
		I32 vert = stucMeshAddVert(pBasic->pCtx, &pBasic->outMesh, NULL);
		pEntry->outVert = vert;
	}
}

typedef struct OutCornerBuf {
	I32Arr buf;
	I32Arr final;
} OutCornerBuf;

static
void addBufFaceToOutMesh(
	MapToMeshBasic *pBasic,
	OutCornerBuf *pOutBuf,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	HTable *pMergeTable,
	I32 faceIdx
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	pOutBuf->buf.count = 0;
	BufFace bufFace = pBufMesh->faces.pArr[faceIdx];
	for (I32 i = 0; i < bufFace.size; ++i) {
		FaceCorner bufCorner = {.face = faceIdx, .corner = i};
		MergeTableKey key = { 0 };
		stucMergeTableGetVertKey(pBasic, pInPiece, pBufMesh, bufCorner, &key);
		VertMerge *pEntry = NULL;
		SearchResult result = stucHTableGet(
			pMergeTable,
			0,
			&key,
			(void **)&pEntry,
			false, NULL,
			mergeTableMakeKey, NULL, NULL, mergeTableEntryCmp
		);
		PIX_ERR_ASSERT("", pEntry && result == STUC_SEARCH_FOUND);
		BufVertType type = bufMeshGetType(pBufMesh, bufCorner);
		if (pEntry->removed) {
			continue;
		}
		if (type == STUC_BUF_VERT_INTERSECT) {
			while (
				pEntry->key.type == STUC_BUF_VERT_INTERSECT &&
				((VertMergeIntersect *)pEntry)->pSnapTo
			) {
				pEntry = ((VertMergeIntersect *)pEntry)->pSnapTo;
			}
		}
		PIX_ERR_ASSERT("", pOutBuf->buf.count <= pOutBuf->buf.size);
		if (pOutBuf->buf.count == pOutBuf->buf.size) {
			pOutBuf->buf.size *= 2;
			pOutBuf->buf.pArr =
				pAlloc->fpRealloc(pOutBuf->buf.pArr, pOutBuf->buf.size * sizeof(I32));
		}
		//using lin-idx (vert merge table) for now,
		//this is to allow easy access during fac eand corner attrib interp.
		//Will be set to out-vert later on.
		I32 linIdx = pEntry->linIdx;
		if (pEntry->key.type == STUC_BUF_VERT_INTERSECT) {
			linIdx |= -0x80000000;
		}
		pOutBuf->buf.pArr[pOutBuf->buf.count] = linIdx;
		pOutBuf->buf.count++;
	}
	pOutBuf->final.count = 0;
	for (I32 i = 0; i < pOutBuf->buf.count; ++i) {
		I32 vert = pOutBuf->buf.pArr[i];
		I32 iPrev = i ? i - 1 : pOutBuf->buf.count - 1;
		if (vert == pOutBuf->buf.pArr[iPrev]) {
			continue;
		}
		//not a dup, add
		PIX_ERR_ASSERT("", pOutBuf->final.count <= pOutBuf->final.size);
		if (pOutBuf->final.count == pOutBuf->final.size) {
			pOutBuf->final.size *= 2;
			pOutBuf->final.pArr = pAlloc->fpRealloc(
				pOutBuf->final.pArr,
				pOutBuf->final.size * sizeof(I32)
			);
		}
		pOutBuf->final.pArr[pOutBuf->final.count] = vert;
		pOutBuf->final.count++;
	}
	if (pOutBuf->final.count < 3) {
		return; //skip face
	}
	I32 outFace = stucMeshAddFace(pBasic->pCtx, &pBasic->outMesh, NULL);
	pBasic->outMesh.core.pFaces[outFace] = pBasic->outMesh.core.cornerCount;
	for (I32 i = 0; i < pOutBuf->final.count; ++i) {
		I32 outCorner = stucMeshAddCorner(pBasic->pCtx, &pBasic->outMesh, NULL);
		pBasic->outMesh.core.pCorners[outCorner] = pOutBuf->final.pArr[i];
	}
}

void stucAddFacesAndCornersToOutMesh(
	MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	HTable *pMergeTable
) {
	OutCornerBuf outBuf = {.buf.size = 16, .final.size = 16};
	outBuf.buf.pArr = pBasic->pCtx->alloc.fpMalloc(outBuf.buf.size * sizeof(I32));
	outBuf.final.pArr = pBasic->pCtx->alloc.fpMalloc(outBuf.final.size * sizeof(I32));
	for (I32 i = 0; i < pInPieces->pBufMeshes->count; ++i) {
		const BufMesh *pBufMesh = pInPieces->pBufMeshes->arr + i;
		for (I32 j = 0; j < pBufMesh->faces.count; ++j) {
			addBufFaceToOutMesh(
				pBasic,
				&outBuf,
				pInPieces->pArr + pBufMesh->faces.pArr[j].inPiece,
				pBufMesh,
				pMergeTable,
				j
			);
		}
	}
	if (outBuf.buf.pArr) {
		pBasic->pCtx->alloc.fpFree(outBuf.buf.pArr);
	}
	if (outBuf.final.pArr) {
		pBasic->pCtx->alloc.fpFree(outBuf.final.pArr);
	}
}

