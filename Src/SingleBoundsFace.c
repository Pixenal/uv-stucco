#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <UvStucco.h>
#include <CombineJobMeshes.h>
#include <MapFile.h>
#include <Mesh.h>
#include <Context.h>
#include <MathUtils.h>
#include <Utils.h>
#include <AttribUtils.h>
#include <MapToJobMesh.h>
#include <Usg.h>
#include <Error.h>

typedef struct {
	I32 *pBuf;
	I32 count;
} MapCornerBuf;

typedef struct {
	BoundsCornerBuf cornerBuf;
	MapCornerBuf mapCornerBuf;
	I32 *pIdxTable;
	I32 *pIndices;
	I32 *pSortedVertBuf;
	I32 size;
} MergeBufs;

typedef struct {
	MakePiecesJobArgs *pArgs;
	FaceRange mapFace;
	BoundsCornerBuf cornerBuf;
	MapCornerBuf mapCornerBuf;
	I32 *pIdxTable;
	Piece *pPieceRoot;
	I32 bufSize;
	I32 bufFace;
} AddFaceState;

void stucDestroyMergeBufs(StucContext pCtx, MergeBufHandles *pHandle) {
	if (pHandle->size) {
		pCtx->alloc.pFree(pHandle->pCornerBuf);
		pCtx->alloc.pFree(pHandle->pMapCornerBuf);
		pCtx->alloc.pFree(pHandle->pIdxTable);
		pCtx->alloc.pFree(pHandle->pSortedVerts);
	}
}

void stucAllocMergeBufs(
	StucContext pCtx,
	MergeBufHandles *pHandle,
	I32 totalVerts
) {
	STUC_ASSERT("", totalVerts >= 0 && totalVerts < 100000);
	pHandle->size = totalVerts;
	pHandle->pCornerBuf =
		pCtx->alloc.pMalloc(sizeof(BoundsCornerBufEntry) * (pHandle->size + 1));
	pHandle->pMapCornerBuf = pCtx->alloc.pMalloc(sizeof(I32) * pHandle->size);
	pHandle->pIdxTable = pCtx->alloc.pMalloc(sizeof(I32) * pHandle->size);
	pHandle->pSortedVerts = pCtx->alloc.pMalloc(sizeof(I32) * pHandle->size);
	pHandle->pIdxTable = pCtx->alloc.pMalloc(sizeof(I32) * pHandle->size);
}

static
bool checkIfDup(AddFaceState *pState, I32 stucCorner) {
	for (I32 i = 0; i < pState->mapCornerBuf.count; ++i) {
		if (stucCorner == pState->mapCornerBuf.pBuf[i]) {
			return true;
		}
		STUC_ASSERT("", i >= 0 && i < pState->mapCornerBuf.count);
	}
	return false;
}

static
void initOnLineTableEntry(
	MakePiecesJobArgs *pArgs,
	OnLine *pEntry,
	BufMesh *pBufMesh,
	I32 base,
	bool isBaseCorner,
	I32 stucVert,
	I32 *pVert
) {
	StucContext pCtx = pArgs->pBasic->pCtx;
	bool realloced = false;
	I32 outVert = stucMeshAddVert(&pCtx->alloc, &pArgs->pBasic->outMesh, &realloced);
	stucCopyAllAttribs(
		&pArgs->pBasic->outMesh.core.vertAttribs,
		outVert,
		&pBufMesh->mesh.core.vertAttribs,
		*pVert
	);
	*pVert = outVert;
	pEntry->outVert = *pVert;
	pEntry->baseEdgeOrCorner = base;
	pEntry->stucVert = stucVert;
	pEntry->type = isBaseCorner + 1;
}

static
void addOnLineVert(
	AddFaceState *pState,
	I32 stucCorner,
	BorderFace *pEntry,
	I32 *pVert,
	I32 k
) {
	MakePiecesJobArgs *pArgs = pState->pArgs;
	BorderInInfo inInfo = stucGetBorderEntryInInfo(pArgs->pBasic, pEntry, k);
	bool isOnInVert = stucGetIfOnInVert(pEntry, k);
	I32 base = isOnInVert ? inInfo.vert : inInfo.edge;
	I32 stucVert = pArgs->pBasic->pMap->pMesh->core.pCorners[pState->mapFace.start + stucCorner];
	I32 hash = stucFnvHash((U8 *)&base, 4, pArgs->pCTables->onLineTableSize);
	OnLine *pOnLineEntry = pArgs->pCTables->pOnLineTable + hash;
	if (!pOnLineEntry->type) {
		initOnLineTableEntry(
			pArgs,
			pOnLineEntry,
			&pArgs->pMappingJobArgs[pEntry->job].bufMesh,
			base,
			isOnInVert,
			stucVert,
			pVert
		);
	}
	else {
		do {
			bool match = base ==
				pOnLineEntry->baseEdgeOrCorner &&
				stucVert == pOnLineEntry->stucVert &&
				isOnInVert + 1 == pOnLineEntry->type;
			if (match) {
				*pVert = pOnLineEntry->outVert;
				break;
			}
			if (!pOnLineEntry->pNext) {
				pOnLineEntry = pOnLineEntry->pNext =
					pArgs->pBasic->pCtx->alloc.pCalloc(1, sizeof(OnLine));
				initOnLineTableEntry(
					pArgs,
					pOnLineEntry,
					&pArgs->pMappingJobArgs[pEntry->job].bufMesh,
					base,
					isOnInVert,
					stucVert,
					pVert
				);
				break;
			}
			pOnLineEntry = pOnLineEntry->pNext;
		} while(pOnLineEntry);
	}
}

static
void addMapCorner(AddFaceState *pState,
	BufMesh *pBufMesh,
	Piece *pPiece,
	BorderFace *pEntry,
	I32 *pVert,
	I32 *pEdge,
	I32 idx
) {
	MakePiecesJobArgs *pArgs = pState->pArgs;
	FaceRange face = pPiece->bufFace;
	//is an stuc corner (this includes stuc corners sitting on base edges or verts)

	//add an item to pEntry in mapToMesh, which denotes if an stuc
	//corner has a dot of 0 (is on a base edge).
	//Then add it to the edgetable if so, without calcing a wind of course.
	//Just use the base edge as the hash, instead of an stuc edge (cause there isnt one).
	//Or just make a new hash table just for stuc corners with zero dot.
	//That would probably be cleaner, and more memory concious tbh.
	bool onLine = stucGetIfOnLine(pEntry, idx);
	I32 mapCorner = stucGetMapCorner(pEntry, idx);
	*pEdge = pBufMesh->mesh.core.pEdges[face.start - idx];
	if (onLine) {
		*pVert = stucBufMeshGetVertIdx(pPiece, pBufMesh, idx);
		STUC_ASSERT("",
			*pVert > pBufMesh->mesh.vertBufSize - 1 -
			pBufMesh->borderVertCount
		);
		STUC_ASSERT("", *pVert < pBufMesh->mesh.vertBufSize);
		addOnLineVert(pState, mapCorner, pEntry, pVert, idx);
	}
	//the vert and edge indices are local to the buf mesh,
	//so we need to offset them, so that they point to the
	//correct position in the out mesh. (these state are set
	//when the non-border mesh data is copied
	else {
		*pVert = pBufMesh->mesh.core.pCorners[face.start - idx];
		*pVert += pArgs->pMappingJobBases[pEntry->job].vertBase;
	}
	*pEdge += pArgs->pMappingJobBases[pEntry->job].edgeBase;
				
	pState->mapCornerBuf.pBuf[pState->mapCornerBuf.count] = mapCorner;
	pState->mapCornerBuf.count++;
}

static
void addCornersToBufAndVertsToMesh(AddFaceState *pState) {
	MakePiecesJobArgs *pArgs = pState->pArgs;
	//pieces should be called sub pieces here
	Piece *pPiece = pState->pPieceRoot;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		//Check entry is valid
		BufMesh *pBufMesh = &pArgs->pMappingJobArgs[pEntry->job].bufMesh;
		FaceRange face = pPiece->bufFace;
		for (I32 k = 0; k < face.size; ++k) {
			if (!(pPiece->add >> k & 0x01)) {
				continue;
			}
			I32 vert;
			I32 edge;
			bool isStuc = stucGetIfStuc(pEntry, k);
			if (!isStuc) {
				//is not an stuc corner (is an intersection, or base corner))
				
				STUC_ASSERT("marked add but not sort", pPiece->pOrder[k] > 0);
				vert = pBufMesh->mesh.core.pCorners[face.start - k];
				edge = pBufMesh->mesh.core.pEdges[face.start - k];
			}
			else {
				STUC_ASSERT("stuc corner has no sort", pPiece->pOrder[k] > 0);
				addMapCorner(pState, pBufMesh, pPiece, pEntry, &vert, &edge, k);
			}
			//if border corner, or if corner edge has been intersected,
			//add new edge to mesh
			BoundsCornerBuf *pCornerBuf = &pState->cornerBuf;
			STUC_ASSERT("", pPiece->pOrder[k] > 0);
			STUC_ASSERT("", pPiece->pOrder[k] <= pState->bufSize);
			pState->pIdxTable[pPiece->pOrder[k] - 1] = pCornerBuf->count;
			pCornerBuf->pBuf[pCornerBuf->count].job = (I8)pEntry->job;
			pCornerBuf->pBuf[pCornerBuf->count].bufCorner = face.start - k;
			pCornerBuf->pBuf[pCornerBuf->count].bufFace = pEntry->bufFace;
			pCornerBuf->pBuf[pCornerBuf->count].corner = vert;
			pCornerBuf->pBuf[pCornerBuf->count].edge = edge;
			pCornerBuf->pBuf[pCornerBuf->count].uv = pBufMesh->mesh.pUvs[face.start - k];
			pState->cornerBuf.count++;
			STUC_ASSERT("", pCornerBuf->count <= pState->bufSize);
			STUC_ASSERT("", k >= 0 && k < face.size);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

static
void addFaceToOutMesh(
	AddFaceState *pState,
	I32 *pIndices,
	I32 count,
	I32 *pInFaces,
	I32 entryCount
) {
	MakePiecesJobArgs *pArgs = pState->pArgs;
	Mesh *pMeshOut = &pState->pArgs->pBasic->outMesh;
	I32 cornerBase = pMeshOut->core.cornerCount;
	bool realloced = false;
	for (I32 i = 0; i < count; ++i) {
		I32 bufIdx = pState->pIdxTable[pIndices[i]];
		STUC_ASSERT("", pState->cornerBuf.pBuf[bufIdx].corner >= 0);
		STUC_ASSERT("", pState->cornerBuf.pBuf[bufIdx].corner < pMeshOut->core.vertCount);
		I32 outCorner =
			stucMeshAddCorner(&pArgs->pBasic->pCtx->alloc, pMeshOut, &realloced);
		STUC_ASSERT("", outCorner == cornerBase + i);
		pMeshOut->core.pCorners[outCorner] = pState->cornerBuf.pBuf[bufIdx].corner;
#ifndef STUC_DISABLE_EDGES_IN_BUF
		STUC_ASSERT("", pState->cornerBuf.pBuf[bufIdx].edge >= 0);
		STUC_ASSERT("", pState->cornerBuf.pBuf[bufIdx].edge < pMeshOut->core.edgeCount);
#endif
		pMeshOut->core.pEdges[outCorner] = pState->cornerBuf.pBuf[bufIdx].edge;
		I32 bufCorner = pState->cornerBuf.pBuf[bufIdx].bufCorner;
		I32 job = pState->cornerBuf.pBuf[bufIdx].job;
		BufMesh *pBufMesh = &pArgs->pMappingJobArgs[job].bufMesh;
		stucCopyAllAttribs(
			&pMeshOut->core.cornerAttribs,
			outCorner,
			&pBufMesh->mesh.core.cornerAttribs,
			bufCorner
		);
		STUC_ASSERT("", i >= 0 && i < count);
	}
	realloced = false;
	I32 outFace = stucMeshAddFace(&pArgs->pBasic->pCtx->alloc, pMeshOut, &realloced);
	if (pArgs->pBasic->ppInFaceTable) {
		if (realloced) {
			//realloc to match meshOut face buf
			I32 byteCount = sizeof(InFaceArr) * pMeshOut->faceBufSize;
			*pArgs->pBasic->ppInFaceTable =
				pArgs->pBasic->pCtx->alloc.pRealloc(*pArgs->pBasic->ppInFaceTable, byteCount);
		}
		STUC_ASSERT("", outFace < pMeshOut->faceBufSize);
		//add face to inFace table
		InFaceArr *pInFaceEntry = *pArgs->pBasic->ppInFaceTable + outFace;
		STUC_ASSERT("", entryCount > 0);
		pInFaceEntry->pArr =
			pArgs->pBasic->pCtx->alloc.pCalloc(entryCount, sizeof(I32));
		memcpy(pInFaceEntry->pArr, pInFaces, sizeof(I32) * entryCount);
		pInFaceEntry->count = entryCount;
		pInFaceEntry->usg = pState->mapFace.idx;
	}
	BufMesh *pBufMesh = &pArgs->pMappingJobArgs[pState->pPieceRoot->pEntry->job].bufMesh;
	stucCopyAllAttribs(
		&pMeshOut->core.faceAttribs,
		outFace,
		&pBufMesh->mesh.core.faceAttribs,
		pState->bufFace
	);
	pMeshOut->core.pFaces[outFace] = cornerBase;
}

static
void nullEntries(Piece *pPiece) {
	do {
		if (pPiece->pEntry) {
			pPiece->pEntry = NULL;
		}
		if (pPiece->pOrder) {
			pPiece->pOrder = NULL;
		}
		if (pPiece->pEdges) {
			pPiece->pEdges = NULL;
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

void stucMergeSingleBorderFace(
	MakePiecesJobArgs *pArgs,
	I32 entryIdx,
	PieceArr *pPieceArr,
	FaceRange *pMapFace,
	MergeBufHandles *pMergeBufHandles,
	I32 *pInFaces,
	I32 entryCount
) {
	StucContext pCtx = pArgs->pBasic->pCtx;
	AddFaceState state = {0};
	state.pArgs = pArgs;
	state.pPieceRoot = pPieceArr->pArr + entryIdx;
	state.bufSize = pMergeBufHandles->size;
	state.cornerBuf.pBuf = pMergeBufHandles->pCornerBuf;
	state.mapCornerBuf.pBuf = pMergeBufHandles->pMapCornerBuf;
	state.pIdxTable = pMergeBufHandles->pIdxTable;
	BufMesh *pBufMesh = &pArgs->pMappingJobArgs[state.pPieceRoot->pEntry->job].bufMesh;
	I32 bufFaceVirtual = state.pPieceRoot->pEntry->bufFace;
	state.bufFace = stucConvertBorderFaceIdx(pBufMesh, bufFaceVirtual).realIdx;
	if (!state.pPieceRoot->pEntry) {
		return;
	}
	state.mapFace = *pMapFace;
	addCornersToBufAndVertsToMesh(&state);

	if (state.cornerBuf.count <= 2) {
		return;
	}
	if (state.pPieceRoot->triangulate) {
		FaceRange tempFace = {0};
		tempFace.end = tempFace.size = state.cornerBuf.count;
		for (I32 i = 0; i < state.cornerBuf.count; ++i) {
			I32 vertIdx = state.cornerBuf.pBuf[state.pIdxTable[i]].corner;
			pMergeBufHandles->pSortedVerts[i] = vertIdx;
			STUC_ASSERT("", i >= 0 && i < state.cornerBuf.count);
		}
		FaceTriangulated tris = {0};
		tris = stucTriangulateFace(
			pCtx->alloc,
			&tempFace,
			pArgs->pBasic->outMesh.pVerts,
			pMergeBufHandles->pSortedVerts,
			false
		);
		for (I32 i = 0; i < tris.triCount; ++i) {
			addFaceToOutMesh(&state, tris.pCorners + (i * 3), 3, pInFaces, entryCount);
			STUC_ASSERT("", i >= 0 && i < tris.triCount);
		}
		pCtx->alloc.pFree(tris.pCorners);
	}
	else {
		//STUC_ASSERT("", state.cornerBuf.count <= 16);
		I32 *pIndices =
			pCtx->alloc.pMalloc(sizeof(I32) * state.cornerBuf.count);
		for (I32 i = 0; i < state.cornerBuf.count; ++i) {
			pIndices[i] = i;
		}
		addFaceToOutMesh(&state, pIndices, state.cornerBuf.count, pInFaces, entryCount);
		pCtx->alloc.pFree(pIndices);
	}
	nullEntries(state.pPieceRoot);
}