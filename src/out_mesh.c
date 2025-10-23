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

typedef struct OutCornerBufCorner {
	I32 mergedVert;
	FaceCorner bufCorner;
	bool intersect;
} OutCornerBufCorner;

typedef struct OutCornerBufArr {
	OutCornerBufCorner *pArr;
	I32 size;
	I32 count;
} OutCornerBufArr;

typedef struct OutCornerBuf {
	OutCornerBufArr buf;
	OutCornerBufArr final;
} OutCornerBuf;

static
void addBufFaceToOutMesh(
	MapToMeshBasic *pBasic,
	OutCornerBuf *pOutBuf,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	I32 bufMeshIdx,
	bool clip,
	HTable *pMergeTable,
	OutBufIdxArr *pOutBufIdxArr,
	I32 faceIdx
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	pOutBuf->buf.count = 0;
	BufFace bufFace = pBufMesh->faces.pArr[faceIdx];
	for (I32 i = 0; i < bufFace.size; ++i) {
		FaceCorner bufCorner = {.face = faceIdx, .corner = i};
		MergeTableKey key = {0};
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
			pOutBuf->buf.pArr = pAlloc->fpRealloc(
				pOutBuf->buf.pArr,
				pOutBuf->buf.size * sizeof(OutCornerBufCorner)
			);
		}
		OutCornerBufCorner *pBufEntry = pOutBuf->buf.pArr + pOutBuf->buf.count;
		pBufEntry->mergedVert = pEntry->linIdx;
		pBufEntry->intersect = pEntry->key.type == STUC_BUF_VERT_INTERSECT;
		pBufEntry->bufCorner = bufCorner;
		pOutBuf->buf.count++;
	}
	pOutBuf->final.count = 0;
	for (I32 i = 0; i < pOutBuf->buf.count; ++i) {
		OutCornerBufCorner vert = pOutBuf->buf.pArr[i];
		I32 iPrev = i ? i - 1 : pOutBuf->buf.count - 1;
		if (vert.intersect == pOutBuf->buf.pArr[iPrev].intersect &&
			vert.mergedVert == pOutBuf->buf.pArr[iPrev].mergedVert
		) {
			continue;
		}
		//not a dup, add
		PIX_ERR_ASSERT("", pOutBuf->final.count <= pOutBuf->final.size);
		if (pOutBuf->final.count == pOutBuf->final.size) {
			pOutBuf->final.size *= 2;
			pOutBuf->final.pArr = pAlloc->fpRealloc(
				pOutBuf->final.pArr,
				pOutBuf->final.size * sizeof(OutCornerBufCorner)
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
		I32 idx = bufFace.flipWind ? pOutBuf->final.count - i - 1 : i;
		I32 outCorner = stucMeshAddCorner(pBasic->pCtx, &pBasic->outMesh, NULL);
		I32 newIdx = -1;
		PIXALC_DYN_ARR_ADD(OutBufIdx, pAlloc, pOutBufIdxArr, newIdx);
		PIX_ERR_ASSERT("", newIdx != -1);
		pOutBufIdxArr->pArr[newIdx] = (OutBufIdx){
			.corner = pOutBuf->final.pArr[idx].bufCorner,
			.mergedVert = pOutBuf->final.pArr[idx].mergedVert,
		};
		pBasic->outMesh.core.pCorners[outCorner] = newIdx;
	}
}

void stucAddFacesAndCornersToOutMesh(
	MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	HTable *pMergeTable,
	OutBufIdxArr *pOutBufIdxArr,
	BufOutRangeTable *pBufOutTable,
	bool clip
) {
	OutCornerBuf outBuf = {.buf.size = 16, .final.size = 16};
	outBuf.buf.pArr =
		pBasic->pCtx->alloc.fpMalloc(outBuf.buf.size * sizeof(OutCornerBufCorner));
	outBuf.final.pArr =
		pBasic->pCtx->alloc.fpMalloc(outBuf.final.size * sizeof(OutCornerBufCorner));
	for (I32 i = 0; i < pInPieces->pBufMeshes->count; ++i) {
		BufOutRange *pRange = pBufOutTable->pArr + pBufOutTable->count;
		pRange->bufMesh = i;
		I32 cornerStart = pBasic->outMesh.core.cornerCount;
		const BufMesh *pBufMesh = pInPieces->pBufMeshes->arr + i;
		for (I32 j = 0; j < pBufMesh->faces.count; ++j) {
			addBufFaceToOutMesh(
				pBasic,
				&outBuf,
				pInPieces->pArr + pBufMesh->faces.pArr[j].inPiece,
				pBufMesh,
				i,
				clip,
				pMergeTable,
				pOutBufIdxArr,
				j
			);
		}
		if (cornerStart == pBasic->outMesh.core.cornerCount) {
			pRange->empty = true;
			continue;
		}
		pRange->outCorners.start = cornerStart;
		pRange->outCorners.end = pBasic->outMesh.core.cornerCount;
		pRange->clip = clip;
		++pBufOutTable->count;
	}
	if (outBuf.buf.pArr) {
		pBasic->pCtx->alloc.fpFree(outBuf.buf.pArr);
	}
	if (outBuf.final.pArr) {
		pBasic->pCtx->alloc.fpFree(outBuf.final.pArr);
	}
}

