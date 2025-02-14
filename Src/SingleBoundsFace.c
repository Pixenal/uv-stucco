#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <UvStucco.h>
#include <CombineJobMeshes.h>
#include <MapFile.h>
#include <Mesh.h>
#include <Context.h>
#include <Clock.h>
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
	Mat3x3 tbnInv;
	FaceRange mapFace;
	I32 bufSize;
	BoundsCornerBuf cornerBuf;
	MapCornerBuf mapCornerBuf;
	MergeSendOffArgs *pArgs;
	I32 *pIdxTable;
	I32 *pSortedVertBuf;
	PieceArr *pPieceArr;
	Piece *pPieceRoot;
	I32 *pInFaces;
	I32 infoBufSize;
	I32 entryCount;
	I32 bufFace;
	bool seamFace;
	bool fullSort;
} Vars;

void stucDestroyMergeBufs(StucContext pContext, MergeBufHandles *pHandle) {
	if (pHandle->size) {
		pContext->alloc.pFree(pHandle->pCornerBuf);
		pContext->alloc.pFree(pHandle->pMapCornerBuf);
		pContext->alloc.pFree(pHandle->pIdxTable);
		pContext->alloc.pFree(pHandle->pSortedVerts);
	}
}

void stucAllocMergeBufs(StucContext pContext, MergeBufHandles *pHandle,
                        I32 totalVerts) {
	STUC_ASSERT("", totalVerts >= 0 && totalVerts < 100000);
	pHandle->size = totalVerts;
	pHandle->pCornerBuf =
		pContext->alloc.pMalloc(sizeof(BoundsCornerBufEntry) * (pHandle->size + 1));
	pHandle->pMapCornerBuf = pContext->alloc.pMalloc(sizeof(I32) * pHandle->size);
	pHandle->pIdxTable = pContext->alloc.pMalloc(sizeof(I32) * pHandle->size);
	pHandle->pSortedVerts = pContext->alloc.pMalloc(sizeof(I32) * pHandle->size);
	pHandle->pIdxTable = pContext->alloc.pMalloc(sizeof(I32) * pHandle->size);
}

static
void buildApproximateTbnInverse(Vars *pVars) {
	V3_F32 normal = {0};
	I32 entryCount = 0;
	Piece *pPiece = pVars->pPieceRoot;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		BufMesh* pBufMesh = &pVars->pArgs->pJobArgs[pEntry->job].bufMesh;
		FaceRange face = pPiece->bufFace;
		Mesh *pMesh = &pBufMesh->mesh;
		STUC_ASSERT("", face.size >= 3);
		V3_F32 vertA = pMesh->pVerts[stucBufMeshGetVertIdx(pPiece, pBufMesh, 0)];
		V3_F32 vertB = pMesh->pVerts[stucBufMeshGetVertIdx(pPiece, pBufMesh, 1)];
		V3_F32 vertC = pMesh->pVerts[stucBufMeshGetVertIdx(pPiece, pBufMesh, 2)];
		STUC_ASSERT("", v3IsFinite(vertA) && v3IsFinite(vertB) && v3IsFinite(vertC));
		V3_F32 ab = _(vertB V3SUB vertA);
		V3_F32 ac = _(vertC V3SUB vertA);
		_(&normal V3ADDEQL v3Cross(ab, ac));
		STUC_ASSERT("", v3IsFinite(normal));
		STUC_ASSERT("", entryCount >= 0 && entryCount < 100000);
		entryCount++;
		pPiece = pPiece->pNext;
	} while (pPiece);
	STUC_ASSERT("", entryCount > 0 && entryCount < 100000);
	_(&normal V3DIVEQLS entryCount);
	F32 normalLen = sqrt(normal.d[0] * normal.d[0] +
	                       normal.d[1] * normal.d[1] +
	                       normal.d[2] * normal.d[2]);
	_(&normal V3DIVEQLS normalLen);
	STUC_ASSERT("", v3IsFinite(normal));
	V3_F32 axis = {0};
	if (normal.d[2] > .99f || normal.d[2] < -.99f) {
		axis.d[0] = 1.0f;
	}
	else {
		axis.d[2] = 1.0f;
	}
	V3_F32 tangent = v3Cross(normal, axis);
	V3_F32 bitangent = v3Cross(normal, tangent);
	Mat3x3 tbn = mat3x3FromV3_F32(tangent, bitangent, normal);
	pVars->tbnInv = mat3x3Invert(&tbn);
	STUC_ASSERT("", mat3x3IsFinite(&pVars->tbnInv));
}

static
bool checkIfDup(Vars *pVars, I32 stucCorner) {
	for (I32 i = 0; i < pVars->mapCornerBuf.count; ++i) {
		if (stucCorner == pVars->mapCornerBuf.pBuf[i]) {
			return true;
		}
		STUC_ASSERT("", i >= 0 && i < pVars->mapCornerBuf.count);
	}
	return false;
}

static
void initOnLineTableEntry(MergeSendOffArgs *pArgs, OnLine *pEntry, BufMesh *pBufMesh,
                          I32 base, bool isBaseCorner, I32 stucVert,
                          I32 *pVert) {
	StucContext pContext = pArgs->pContext;
	bool realloced = false;
	I32 outVert = stucMeshAddVert(&pContext->alloc, pArgs->pMeshOut, &realloced);
	stucCopyAllAttribs(&pArgs->pMeshOut->core.vertAttribs, outVert,
	                   &pBufMesh->mesh.core.vertAttribs, *pVert);
	*pVert = outVert;
	pEntry->outVert = *pVert;
	pEntry->baseEdgeOrCorner = base;
	pEntry->stucVert = stucVert;
	pEntry->type = isBaseCorner + 1;
}

static
void addOnLineVert(Vars *pVars, I32 stucCorner,
                   BorderFace *pEntry, I32 *pVert, I32 k) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
	BorderInInfo inInfo = stucGetBorderEntryInInfo(pEntry, pArgs->pJobArgs, k);
	bool isOnInVert = stucGetIfOnInVert(pEntry, k);
	I32 base = isOnInVert ? inInfo.vert : inInfo.edge;
	I32 stucVert = pArgs->pMap->mesh.core.pCorners[pVars->mapFace.start + stucCorner];
	I32 hash = stucFnvHash((U8 *)&base, 4, pArgs->pCTables->onLineTableSize);
	OnLine *pOnLineEntry = pArgs->pCTables->pOnLineTable + hash;
	if (!pOnLineEntry->type) {
		initOnLineTableEntry(pArgs, pOnLineEntry,
		                     &pArgs->pJobArgs[pEntry->job].bufMesh,
		                     base, isOnInVert, stucVert, pVert);
	}
	else {
		do {
			bool match = base == pOnLineEntry->baseEdgeOrCorner &&
			             stucVert == pOnLineEntry->stucVert &&
			             isOnInVert + 1 == pOnLineEntry->type;
			if (match) {
				*pVert = pOnLineEntry->outVert;
				break;
			}
			if (!pOnLineEntry->pNext) {
				pOnLineEntry = pOnLineEntry->pNext =
					pArgs->pContext->alloc.pCalloc(1, sizeof(OnLine));
				initOnLineTableEntry(pArgs, pOnLineEntry,
				                     &pArgs->pJobArgs[pEntry->job].bufMesh,
				                     base, isOnInVert, stucVert, pVert);
				break;
			}
			pOnLineEntry = pOnLineEntry->pNext;
		} while(pOnLineEntry);
	}
}

static
void addMapCorner(Vars *pVars, BufMesh *pBufMesh, Piece *pPiece, BorderFace *pEntry,
                  I32 *pVert, I32 *pEdge, I32 idx) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
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
		STUC_ASSERT("", *pVert > pBufMesh->mesh.vertBufSize - 1 -
		                pBufMesh->borderVertCount);
		STUC_ASSERT("", *pVert < pBufMesh->mesh.vertBufSize);
		addOnLineVert(pVars, mapCorner, pEntry, pVert, idx);
	}
	//the vert and edge indices are local to the buf mesh,
	//so we need to offset them, so that they point to the
	//correct position in the out mesh. (these vars are set
	//when the non-border mesh data is copied
	else {
		*pVert = pBufMesh->mesh.core.pCorners[face.start - idx];
		*pVert += pArgs->pJobBases[pEntry->job].vertBase;
	}
	*pEdge += pArgs->pJobBases[pEntry->job].edgeBase;
				
	//CLOCK_START;
	pVars->mapCornerBuf.pBuf[pVars->mapCornerBuf.count] = mapCorner;
	pVars->mapCornerBuf.count++;
	//CLOCK_STOP_NO_PRINT;
	//pTimeSpent[5] += CLOCK_TIME_DIFF(start, stop);
}

static
bool addCornersToBufAndVertsToMesh(Vars *pVars) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
	//CLOCK_INIT;
	//pieces should be called sub pieces here
	Piece *pPiece = pVars->pPieceRoot;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		//Check entry is valid
		BufMesh *pBufMesh = &pArgs->pJobArgs[pEntry->job].bufMesh;
		FaceRange face = pPiece->bufFace;
		for (I32 k = 0; k < face.size; ++k) {
			//CLOCK_START;
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
				addMapCorner(pVars, pBufMesh, pPiece, pEntry, &vert, &edge, k);
			}
			//if border corner, or if corner edge has been intersected,
			//add new edge to mesh
			BoundsCornerBuf *pCornerBuf = &pVars->cornerBuf;
			STUC_ASSERT("", pPiece->pOrder[k] > 0);
			STUC_ASSERT("", pPiece->pOrder[k] <= pVars->bufSize);
			pVars->pIdxTable[pPiece->pOrder[k] - 1] = pCornerBuf->count;
			pCornerBuf->pBuf[pCornerBuf->count].job = pEntry->job;
			pCornerBuf->pBuf[pCornerBuf->count].bufCorner = face.start - k;
			pCornerBuf->pBuf[pCornerBuf->count].bufFace = pEntry->bufFace;
			pCornerBuf->pBuf[pCornerBuf->count].corner = vert;
			pCornerBuf->pBuf[pCornerBuf->count].edge = edge;
			pCornerBuf->pBuf[pCornerBuf->count].uv = pBufMesh->mesh.pUvs[face.start - k];
			//CLOCK_START;
			//CLOCK_STOP_NO_PRINT;
			//pTimeSpent[6] += CLOCK_TIME_DIFF(start, stop);
			pVars->cornerBuf.count++;
			STUC_ASSERT("", pCornerBuf->count <= pVars->bufSize);
			pVars->infoBufSize++;
			STUC_ASSERT("", k >= 0 && k < face.size);
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
	return false;
}

static
void addFaceToOutMesh(Vars *pVars, I32 *pIndices,
                      I32 count, I32 *pIdxTable) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
	Mesh *pMeshOut = pVars->pArgs->pMeshOut;
	I32 cornerBase = pMeshOut->core.cornerCount;
	bool realloced = false;
	for (I32 i = 0; i < count; ++i) {
		I32 bufIdx = pIdxTable[pIndices[i]];
		STUC_ASSERT("", pVars->cornerBuf.pBuf[bufIdx].corner >= 0);
		STUC_ASSERT("", pVars->cornerBuf.pBuf[bufIdx].corner < pMeshOut->core.vertCount);
		I32 outCorner = stucMeshAddCorner(&pArgs->pContext->alloc, pMeshOut, &realloced);
		STUC_ASSERT("", outCorner == cornerBase + i);
		pMeshOut->core.pCorners[outCorner] = pVars->cornerBuf.pBuf[bufIdx].corner;
		STUC_ASSERT("", pVars->cornerBuf.pBuf[bufIdx].edge >= 0);
		STUC_ASSERT("", pVars->cornerBuf.pBuf[bufIdx].edge < pMeshOut->core.edgeCount);
		pMeshOut->core.pEdges[outCorner] = pVars->cornerBuf.pBuf[bufIdx].edge;
		I32 bufCorner = pVars->cornerBuf.pBuf[bufIdx].bufCorner;
		I32 job = pVars->cornerBuf.pBuf[bufIdx].job;
		BufMesh *pBufMesh = &pArgs->pJobArgs[job].bufMesh;
		stucCopyAllAttribs(&pMeshOut->core.cornerAttribs, outCorner,
		                   &pBufMesh->mesh.core.cornerAttribs, bufCorner);
		STUC_ASSERT("", i >= 0 && i < count);
	}
	realloced = false;
	I32 outFace = stucMeshAddFace(&pArgs->pContext->alloc, pMeshOut, &realloced);
	if (pArgs->ppInFaceTable) {
		if (realloced) {
			//realloc to match meshOut face buf
			I32 byteCount = sizeof(InFaceArr) * pMeshOut->faceBufSize;
			*pArgs->ppInFaceTable =
				pArgs->pContext->alloc.pRealloc(*pArgs->ppInFaceTable, byteCount);
		}
		STUC_ASSERT("", outFace < pMeshOut->faceBufSize);
		//add face to inFace table
		InFaceArr *pInFaceEntry = *pArgs->ppInFaceTable + outFace;
		STUC_ASSERT("", pVars->entryCount > 0);
		pInFaceEntry->pArr =
			pArgs->pContext->alloc.pCalloc(pVars->entryCount, sizeof(I32));
		memcpy(pInFaceEntry->pArr, pVars->pInFaces, sizeof(I32) * pVars->entryCount);
		pInFaceEntry->count = pVars->entryCount;
		pInFaceEntry->usg = pVars->mapFace.idx;
	}
	BufMesh *pBufMesh = &pArgs->pJobArgs[pVars->pPieceRoot->pEntry->job].bufMesh;
	stucCopyAllAttribs(&pMeshOut->core.faceAttribs, outFace,
	                   &pBufMesh->mesh.core.faceAttribs, pVars->bufFace);
	pMeshOut->core.pFaces[outFace] = cornerBase;
}

static
void destroyEntries(StucContext pContext, Piece *pPiece) {
	do {
		if (pPiece->pEntry) {
			pContext->alloc.pFree(pPiece->pEntry);
			pPiece->pEntry = NULL;
		}
		if (pPiece->pOrder) {
			pContext->alloc.pFree(pPiece->pOrder);
			pPiece->pOrder = NULL;
		}
		if (pPiece->pEdges) {
			pContext->alloc.pFree(pPiece->pEdges);
			pPiece->pEdges = NULL;
		}
		pPiece = pPiece->pNext;
	} while(pPiece);
}

void stucMergeSingleBorderFace(MergeSendOffArgs *pArgs, U64 *pTimeSpent,
                               I32 entryIdx, PieceArr *pPieceArr,
                               FaceRange *pMapFace, MergeBufHandles *pMergeBufHandles,
                               I32 *pInFaces, I32 entryCount) {
	CLOCK_INIT
	CLOCK_START;
	Vars vars = {0};
	vars.pArgs = pArgs;
	vars.pPieceArr = pPieceArr;
	vars.pPieceRoot = pPieceArr->pArr + entryIdx;
	vars.bufSize = pMergeBufHandles->size;
	vars.cornerBuf.pBuf = pMergeBufHandles->pCornerBuf;
	vars.mapCornerBuf.pBuf = pMergeBufHandles->pMapCornerBuf;
	vars.pIdxTable = pMergeBufHandles->pIdxTable;
	vars.pSortedVertBuf = pMergeBufHandles->pSortedVerts;
	vars.pInFaces = pInFaces;
	vars.entryCount = entryCount;
	BufMesh *pBufMesh = &pArgs->pJobArgs[vars.pPieceRoot->pEntry->job].bufMesh;
	I32 bufFaceVirtual = vars.pPieceRoot->pEntry->bufFace;
	vars.bufFace = stucConvertBorderFaceIdx(pBufMesh, bufFaceVirtual).realIdx;
	if (!vars.pPieceRoot->pEntry) {
		return;
	}
	vars.mapFace = *pMapFace;
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[2] += CLOCK_TIME_DIFF(start, stop);
	CLOCK_START;
	if (addCornersToBufAndVertsToMesh(&vars)) {
		return;
	}
	if (vars.cornerBuf.count <= 2) {
		return;
	}
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[4] += CLOCK_TIME_DIFF(start, stop);
	CLOCK_START;
	if (vars.pPieceRoot->triangulate) {
		FaceRange tempFace = {0};
		tempFace.end = tempFace.size = vars.cornerBuf.count;
		for (I32 i = 0; i < vars.cornerBuf.count; ++i) {
			I32 vertIdx = vars.cornerBuf.pBuf[vars.pIdxTable[i]].corner;
			vars.pSortedVertBuf[i] = vertIdx;
			STUC_ASSERT("", i >= 0 && i < vars.cornerBuf.count);
		}
		FaceTriangulated tris = {0};
		tris = stucTriangulateFace(pArgs->pContext->alloc, &tempFace, pArgs->pMeshOut->pVerts,
		                           vars.pSortedVertBuf, false);
		for (I32 i = 0; i < tris.triCount; ++i) {
			addFaceToOutMesh(&vars, tris.pCorners + (i * 3), 3, vars.pIdxTable);
			STUC_ASSERT("", i >= 0 && i < tris.triCount);
		}
		pArgs->pContext->alloc.pFree(tris.pCorners);
	}
	else {
		//STUC_ASSERT("", vars.cornerBuf.count <= 16);
		I32 *pIndices =
			pArgs->pContext->alloc.pMalloc(sizeof(I32) * vars.cornerBuf.count);
		for (I32 i = 0; i < vars.cornerBuf.count; ++i) {
			pIndices[i] = i;
		}
		addFaceToOutMesh(&vars, pIndices, vars.cornerBuf.count, vars.pIdxTable);
		pArgs->pContext->alloc.pFree(pIndices);
	}
	destroyEntries(pArgs->pContext, vars.pPieceRoot);
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[5] += CLOCK_TIME_DIFF(start, stop);
}