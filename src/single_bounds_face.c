#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <uv_stucco.h>
#include <combine_job_meshes.h>
#include <map.h>
#include <mesh.h>
#include <context.h>
#include <math_utils.h>
#include <utils.h>
#include <attrib_utils.h>
#include <map_to_job_mesh.h>
#include <usg.h>
#include <error.h>

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
	PieceArr *pPieceArr;
	BorderVert ***pppVertLookup;
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
	I32 outVert = stucMeshAddVert(pCtx, &pArgs->pBasic->outMesh, &realloced);
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
	I32 mapVert,
	BorderFace *pEntry,
	I32 *pVert,
	I32 k
) {
	MakePiecesJobArgs *pArgs = pState->pArgs;
	BorderInInfo inInfo = stucGetBorderEntryInInfo(pArgs->pBasic, pEntry, k);
	bool isOnInVert = stucGetIfOnInVert(pEntry, k);
	I32 base = isOnInVert ? inInfo.vert : inInfo.edge;
	I32 hash = stucFnvHash((U8 *)&base, 4, pArgs->pCTables->onLineTableSize);
	OnLine *pOnLineEntry = pArgs->pCTables->pOnLineTable + hash;
	if (!pOnLineEntry->type) {
		initOnLineTableEntry(
			pArgs,
			pOnLineEntry,
			&pArgs->pMappingJobArgs[pEntry->job].bufMesh,
			base,
			isOnInVert,
			mapVert,
			pVert
		);
	}
	else {
		do {
			bool match = base ==
				pOnLineEntry->baseEdgeOrCorner &&
				mapVert == pOnLineEntry->stucVert &&
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
					mapVert,
					pVert
				);
				break;
			}
			pOnLineEntry = pOnLineEntry->pNext;
		} while(pOnLineEntry);
	}
}

static
I32 addMapCorner(
	AddFaceState *pState,
	BufMesh *pBufMesh,
	Piece *pPiece,
	I32 *pVert,
#ifndef STUC_DISABLE_EDGES_IN_BUF
	I32 *pEdge,
#endif
	I32 idx,
	bool snapped
) {
	
	//is an stuc corner (this includes stuc corners sitting on in-edges or in-verts)

	//add an item to pEntry in mapToMesh, which denotes if an stuc
	//corner has a dot of 0 (is on a base edge).
	//Then add it to the edgetable if so, without calcing a wind of course.
	//Just use the base edge as the hash, instead of an stuc edge (cause there isnt one).
	//Or just make a new hash table just for stuc corners with zero dot.
	//That would probably be cleaner, and more memory concious tbh.
	bool onLine = stucGetIfOnLine(pPiece->pEntry, idx);
	I32 mapCorner = stucGetMapCorner(pPiece->pEntry, idx);
	MapCornerBuf *pMapCornerBuf = &pState->mapCornerBuf;
	StucMap pMap = pState->pArgs->pBasic->pMap;
	//important to make sure we're using the mapface from pPiece, incase this corner is snapped
	FaceRange mapFace = stucGetFaceRange(&pMap->pMesh->core, pPiece->pEntry->mapFace, false);
	I32 mapVert = pMap->pMesh->core.pCorners[mapFace.start + mapCorner];
	if (pMapCornerBuf->count &&
		(
			pMapCornerBuf->pBuf[0] == mapVert ||
			pMapCornerBuf->pBuf[pMapCornerBuf->count - 1] == mapVert
		)
	) {
		//already added this map corner
		// (this should only occur due to snapping)
		return 1;
	}
	pMapCornerBuf->pBuf[pMapCornerBuf->count] = mapVert;
	pMapCornerBuf->count++;

#ifndef STUC_DISABLE_EDGES_IN_BUF
	*pEdge = pBufMesh->mesh.core.pEdges[pPiece->bufFace.start - idx];
#endif
	if (onLine) {
		*pVert = stucBufMeshGetVertIdx(pPiece, pBufMesh, idx);
		STUC_ASSERT("",
			*pVert > pBufMesh->mesh.vertBufSize - 1 -
			pBufMesh->borderVertCount
		);
		STUC_ASSERT("", *pVert < pBufMesh->mesh.vertBufSize);
		addOnLineVert(pState, mapVert, pPiece->pEntry, pVert, idx);
	}
	//the vert and edge indices are local to the buf mesh,
	//so we need to offset them, so that they point to the
	//correct position in the out mesh. (these state are set
	//when the non-border mesh data is copied
	else {
		*pVert = pBufMesh->mesh.core.pCorners[pPiece->bufFace.start - idx];
		*pVert += pState->pArgs->pMappingJobBases[pPiece->pEntry->job].vertBase;
	}
#ifndef STUC_DISABLE_EDGES_IN_BUF
	*pEdge += pState->pArgs->pMappingJobBases[pPiece->pEntry->job].edgeBase;
#endif
	return 0;
}

static
I32 addCorner(
	AddFaceState *pState,
	Piece *pPiece,
	I32 corner,
	I32 order,
	bool snapped
) {
	STUC_ASSERT(
		"trying to snap to a corner not marked for add",
		(pPiece->add >> corner & 0x01)
	);
	BorderFace *pEntry = pPiece->pEntry;
	BufMesh *pBufMesh = &pState->pArgs->pMappingJobArgs[pPiece->pEntry->job].bufMesh;
	FaceRange face = pPiece->bufFace;
	I32 vert = 0;
#ifndef STUC_DISABLE_EDGES_IN_BUF
	I32 edge = 0;
#endif
	bool isStuc = stucGetIfStuc(pPiece->pEntry, corner);
	if (!isStuc) {
		//is not an stuc corner (is an intersection, or base corner))
		STUC_ASSERT("marked add but not sort", order > 0);
		I32 cornerIdxVirtual =
			stucGetVirtualBufIdx(pBufMesh, pPiece->bufFace.start - corner);
		BorderVert *pVertEntry =
			pState->pppVertLookup[pPiece->pEntry->job][cornerIdxVirtual];
		STUC_ASSERT("", pVertEntry);
		if (pVertEntry->mergeTo.snapped) {
			STUC_ASSERT("", pVertEntry && pVertEntry->mergeTo.snapped);
			CornerIdx *pMergeTo = &pVertEntry->mergeTo;
			if (pPiece == pMergeTo->pPiece) {
				return 0;
			}
			return addCorner(pState, pMergeTo->pPiece, pMergeTo->corner, order, true);
		}
		vert = pVertEntry->vert;
#ifndef STUC_DISABLE_EDGES_IN_BUF
		//when edges are reimplemented, use an intermediary struct like BorderVert.
		//Not storing out-mesh indices in buf-mesh anymore. It's confusing
		edge = pBufMesh->mesh.core.pEdges[face.start - corner];
#endif
	}
	else {
		STUC_ASSERT("stuc corner has no sort", order > 0);
		if (addMapCorner(
			pState,
			pBufMesh,
			pPiece,
#ifndef STUC_DISABLE_EDGES_IN_BUF
			&vert, &edge,
#else
			&vert,
#endif
			corner,
			snapped
		)) {
			return 1;
		}
	}
	//if border corner, or if corner edge has been intersected,
	//add new edge to mesh
	STUC_ASSERT("", order > 0);
	STUC_ASSERT("", order <= pState->bufSize);
	pState->pIdxTable[order - 1] = pState->cornerBuf.count;
	if (order > pState->cornerBuf.top) {
		pState->cornerBuf.top = order;
	}
	BoundsCornerBufEntry *pBufEntry = pState->cornerBuf.pBuf + pState->cornerBuf.count;
	pBufEntry->job = (I8)pEntry->job;
	pBufEntry->bufCorner = face.start - corner;
	pBufEntry->bufFace = pEntry->bufFace;
	pBufEntry->corner = vert;
#ifndef STUC_DISABLE_EDGES_IN_BUF
	pBufEntry->edge = edge;
#endif
	pBufEntry->uv = pBufMesh->mesh.pUvs[face.start - corner];
	pState->cornerBuf.count++;
	STUC_ASSERT("", pState->cornerBuf.count <= pState->bufSize);
	return 0;
}

static
void addCornersToBufAndVertsToMesh(AddFaceState *pState) {
	MakePiecesJobArgs *pArgs = pState->pArgs;
	Piece *pPiece = pState->pPieceRoot;
	do {
		FaceRange face = pPiece->bufFace;
		for (I32 i = 0; i < face.size; ++i) {
			if (!(pPiece->add >> i & 0x01)) {
				continue;
			}
			if (addCorner(pState, pPiece, i, pPiece->pOrder[i], false)) {
				//using piece as root, not point updating previous pieces
				stucCorrectSortAfterRemoval(pPiece, pPiece, i);
				//correct order in corner buf
				for (I32 j = pPiece->pOrder[i] - 1; j < pState->cornerBuf.top - 1; ++j) {
					pState->pIdxTable[j] = pState->pIdxTable[j + 1];
				}
			}
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
	I32 entryCount,
	FaceRange *pMapFace
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
			stucMeshAddCorner(pArgs->pBasic->pCtx, pMeshOut, &realloced);
		STUC_ASSERT("", outCorner == cornerBase + i);
		pMeshOut->core.pCorners[outCorner] = pState->cornerBuf.pBuf[bufIdx].corner;
#ifndef STUC_DISABLE_EDGES_IN_BUF
		STUC_ASSERT("", pState->cornerBuf.pBuf[bufIdx].edge >= 0);
		STUC_ASSERT("", pState->cornerBuf.pBuf[bufIdx].edge < pMeshOut->core.edgeCount);
#endif
#ifndef STUC_DISABLE_EDGES_IN_BUF
		pMeshOut->core.pEdges[outCorner] = pState->cornerBuf.pBuf[bufIdx].edge;
#endif
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
	I32 outFace = stucMeshAddFace(pArgs->pBasic->pCtx, pMeshOut, &realloced);
	if (pArgs->pBasic->pInFaceTable) {
		if (realloced) {
			//realloc to match meshOut face buf
			I32 byteCount = sizeof(InFaceArr) * pMeshOut->faceBufSize;
			pArgs->pBasic->pInFaceTable->pArr = pArgs->pBasic->pCtx->alloc.pRealloc(
				pArgs->pBasic->pInFaceTable->pArr,
				byteCount
			);
		}
		STUC_ASSERT("", outFace < pMeshOut->faceBufSize);
		//add face to inFace table
		InFaceArr *pInFaceEntry = pArgs->pBasic->pInFaceTable->pArr + outFace;
		STUC_ASSERT("", entryCount > 0);
		pInFaceEntry->count = entryCount;
		stucLinAlloc(
			pArgs->pBasic->pInFaceTable->pAlloc,
			&pInFaceEntry->pArr,
			pInFaceEntry->count
		);
		memcpy(pInFaceEntry->pArr, pInFaces, sizeof(I32) * pInFaceEntry->count);
		pInFaceEntry->usg = pMapFace->idx;
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
	BorderVert ***pppVertLookup,
	I32 entryCount
) {
	StucContext pCtx = pArgs->pBasic->pCtx;
	AddFaceState state = {0};
	state.pArgs = pArgs;
	state.pPieceRoot = pPieceArr->pArr + entryIdx;
	state.pPieceArr = pPieceArr;
	state.bufSize = pMergeBufHandles->size;
	state.cornerBuf.pBuf = pMergeBufHandles->pCornerBuf;
	state.mapCornerBuf.pBuf = pMergeBufHandles->pMapCornerBuf;
	state.pIdxTable = pMergeBufHandles->pIdxTable;
	state.pppVertLookup = pppVertLookup;
	BufMesh *pBufMesh = &pArgs->pMappingJobArgs[state.pPieceRoot->pEntry->job].bufMesh;
	I32 bufFaceVirtual = state.pPieceRoot->pEntry->bufFace;
	state.bufFace = stucConvertBorderFaceIdx(pBufMesh, bufFaceVirtual).realIdx;
	if (!state.pPieceRoot->pEntry) {
		return;
	}
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
			addFaceToOutMesh(
				&state,
				tris.pCorners + (i * 3),
				3,
				pInFaces,
				entryCount,
				pMapFace
			);
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
		addFaceToOutMesh(
			&state,
			pIndices,
			state.cornerBuf.count,
			pInFaces,
			entryCount,
			pMapFace
		);
		pCtx->alloc.pFree(pIndices);
	}
	//nullEntries(state.pPieceRoot);
}