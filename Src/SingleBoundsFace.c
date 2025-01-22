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
	int32_t *pBuf;
	int32_t count;
} MapCornerBuf;

typedef struct {
	BoundsCornerBuf cornerBuf;
	MapCornerBuf mapCornerBuf;
	int32_t *pIdxTable;
	int32_t *pIndices;
	int32_t *pSortedVertBuf;
	int32_t size;
} MergeBufs;

typedef struct {
	Mat3x3 tbnInv;
	FaceRange stucFace;
	int32_t bufSize;
	BoundsCornerBuf cornerBuf;
	MapCornerBuf mapCornerBuf;
	MergeSendOffArgs *pArgs;
	int32_t *pIdxTable;
	int32_t *pSortedVertBuf;
	PieceArr *pPieceArr;
	Piece *pPieceRoot;
	int32_t *pInFaces;
	int32_t infoBufSize;
	int32_t entryCount;
	int32_t bufFace;
	bool seamFace;
	bool fullSort;
} Vars;

/*
static int32_t getEdgeLocalCorner(int32_t *pEdgeCorners, FaceRange *pBaseFace) {
	for (int32_t i = pBaseFace->start; i < pBaseFace->end; ++i) {
		if (i == pEdgeCorners[0] || i == pEdgeCorners[1]) {
			return i;
		}
	}
	printf("Couldn't find corner for edge (winding compare for border face\n)");
	abort();
}

static void removeEdgeEntryFromList(StucContext pContext, SharedEdge *pEntry,
                                    void *pLastEntry) {
	//Both SharedEdgeWrap, and SharedEdge, have a pointer to a SharedEdge
	//as their first element. So we just cast and assign pLastEntry.
	*(SharedEdge **)pLastEntry = pEntry->pNext;
	pContext->alloc.pFree(pEntry);
}
*/

void stucDestroyMergeBufs(StucContext pContext, MergeBufHandles *pHandle) {
	if (pHandle->size) {
		pContext->alloc.pFree(pHandle->pCornerBuf);
		pContext->alloc.pFree(pHandle->pMapCornerBuf);
		pContext->alloc.pFree(pHandle->pIdxTable);
		pContext->alloc.pFree(pHandle->pSortedVerts);
	}
}

void stucAllocMergeBufs(StucContext pContext, MergeBufHandles *pHandle,
                        int32_t totalVerts) {
	STUC_ASSERT("", totalVerts >= 0 && totalVerts < 100000);
	pHandle->size = totalVerts;
	pHandle->pCornerBuf =
		pContext->alloc.pMalloc(sizeof(BoundsCornerBufEntry) * (pHandle->size + 1));
	pHandle->pMapCornerBuf =
		pContext->alloc.pMalloc(sizeof(int32_t) * pHandle->size);
	pHandle->pIdxTable =
		pContext->alloc.pMalloc(sizeof(int32_t) * pHandle->size);
	pHandle->pSortedVerts =
		pContext->alloc.pMalloc(sizeof(int32_t) * pHandle->size);
	pHandle->pIdxTable =
		pContext->alloc.pMalloc(sizeof(int32_t) * pHandle->size);
}

static
void buildApproximateTbnInverse(Vars *pVars) {
	V3_F32 normal = {0};
	int32_t entryCount = 0;
	Piece *pPiece = pVars->pPieceRoot;
	do {
		BorderFace *pEntry = pPiece->pEntry;
		BufMesh* pBufMesh = &pVars->pArgs->pJobArgs[pEntry->job].bufMesh;
		FaceRange face = pPiece->bufFace;
		Mesh *pMesh = asMesh(pBufMesh);
		STUC_ASSERT("", face.size >= 3);
		V3_F32 vertA = pMesh->pVerts[bufMeshGetVertIdx(pPiece, pBufMesh, 0)];
		V3_F32 vertB = pMesh->pVerts[bufMeshGetVertIdx(pPiece, pBufMesh, 1)];
		V3_F32 vertC = pMesh->pVerts[bufMeshGetVertIdx(pPiece, pBufMesh, 2)];
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
	float normalLen =
		sqrt(normal.d[0] * normal.d[0] +
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
bool checkIfDup(Vars *pVars, int32_t stucCorner) {
	for (int32_t i = 0; i < pVars->mapCornerBuf.count; ++i) {
		if (stucCorner == pVars->mapCornerBuf.pBuf[i]) {
			return true;
		}
		STUC_ASSERT("", i >= 0 && i < pVars->mapCornerBuf.count);
	}
	return false;
}

static void initOnLineTableEntry(MergeSendOffArgs *pArgs, OnLine *pEntry,
                                 BufMesh *pBufMesh, int32_t base,
								 bool isBaseCorner, int32_t stucVert,
								 int32_t *pVert) {
	StucContext pContext = pArgs->pContext;
	bool realloced = false;
	int32_t outVert = meshAddVert(&pContext->alloc, pArgs->pMeshOut, &realloced);
	copyAllAttribs(&pArgs->pMeshOut->mesh.vertAttribs, outVert,
				   &asMesh(pBufMesh)->mesh.vertAttribs, *pVert);
	*pVert = outVert;
	pEntry->outVert = *pVert;
	pEntry->baseEdgeOrCorner = base;
	pEntry->stucVert = stucVert;
	pEntry->type = isBaseCorner + 1;
}

static
void addOnLineVert(Vars *pVars, int32_t stucCorner,
                   BorderFace *pEntry, int32_t *pVert, int32_t k) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
	BorderInInfo inInfo = getBorderEntryInInfo(pEntry, pArgs->pJobArgs, k);
	bool isOnInVert = getIfOnInVert(pEntry, k);
	int32_t base = isOnInVert ? inInfo.vert : inInfo.edge;
	int32_t stucVert = pArgs->pMap->mesh.mesh.pCorners[pVars->stucFace.start + stucCorner];
	int32_t hash = stucFnvHash((uint8_t *)&base, 4, pArgs->pCTables->onLineTableSize);
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
		for (int32_t k = 0; k < face.size; ++k) {
			//CLOCK_START;
			if (!(pPiece->add >> k & 0x01)) {
				continue;
			}
			int32_t vert;
			int32_t edge;
			bool isStuc = getIfStuc(pEntry, k);
			if (!isStuc) {
				//is not an stuc corner (is an intersection, or base corner))
				
				STUC_ASSERT("marked add but not sort", pPiece->pOrder[k] > 0);
				vert = pBufMesh->mesh.mesh.pCorners[face.start - k];
				edge = pBufMesh->mesh.mesh.pEdges[face.start - k];
			}
			else {
				STUC_ASSERT("stuc corner has no sort", pPiece->pOrder[k] > 0);
				//is an stuc corner (this includes stuc corners sitting on base edges or verts)

				//add an item to pEntry in mapToMesh, which denotes if an stuc
				//corner has a dot of 0 (is on a base edge).
				//Then add it to the edgetable if so, without calcing a wind of course.
				//Just use the base edge as the hash, instead of an stuc edge (cause there isnt one).
				//Or just make a new hash table just for stuc corners with zero dot.
				//That would probably be cleaner, and more memory concious tbh.
				bool onLine = getIfOnLine(pEntry, k);
				int32_t mapCorner = getMapCorner(pEntry, k);
				edge = asMesh(pBufMesh)->mesh.pEdges[face.start - k];
				if (onLine) {
					vert = bufMeshGetVertIdx(pPiece, pBufMesh, k);
					STUC_ASSERT("", vert > asMesh(pBufMesh)->vertBufSize - 1 -
						   pBufMesh->borderVertCount);
					STUC_ASSERT("", vert < asMesh(pBufMesh)->vertBufSize);
					addOnLineVert(pVars, mapCorner, pEntry, &vert, k);
				}
				//the vert and edge indices are local to the buf mesh,
				//so we need to offset them, so that they point to the
				//correct position in the out mesh. (these vars are set
				//when the non-border mesh data is copied
				else {
					vert = asMesh(pBufMesh)->mesh.pCorners[face.start - k];
					vert += pArgs->pJobBases[pEntry->job].vertBase;
				}
				edge += pArgs->pJobBases[pEntry->job].edgeBase;
				
				//CLOCK_START;
				pVars->mapCornerBuf.pBuf[pVars->mapCornerBuf.count] = mapCorner;
				pVars->mapCornerBuf.count++;
				//CLOCK_STOP_NO_PRINT;
				//pTimeSpent[5] += CLOCK_TIME_DIFF(start, stop);
			}
			//if border corner, or if corner edge has been intersected,
			//add new edge to mesh
			//int32_t kNext = (k + 1) % faceSize;
			//int32_t vertNext = bufMesh->mesh.pCorners[face.start - kNext];
			//if (borderCorner || vertNext >= bufMesh->mesh.vertCount) {
			//}
			BoundsCornerBuf *pCornerBuf = &pVars->cornerBuf;
			STUC_ASSERT("", pPiece->pOrder[k] > 0);
			STUC_ASSERT("", pPiece->pOrder[k] <= pVars->bufSize);
			pVars->pIdxTable[pPiece->pOrder[k] - 1] = pCornerBuf->count;
			pCornerBuf->pBuf[pCornerBuf->count].job = pEntry->job;
			pCornerBuf->pBuf[pCornerBuf->count].bufCorner = face.start - k;
			pCornerBuf->pBuf[pCornerBuf->count].bufFace = pEntry->face;
			pCornerBuf->pBuf[pCornerBuf->count].corner = vert;
			pCornerBuf->pBuf[pCornerBuf->count].edge = edge;
			pCornerBuf->pBuf[pCornerBuf->count].uv =
				asMesh(pBufMesh)->pStuc[face.start - k];
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
void addFaceToOutMesh(Vars *pVars, int32_t *pIndices,
                      int32_t count, int32_t *pIdxTable) {
	MergeSendOffArgs *pArgs = pVars->pArgs;
	Mesh *pMeshOut = pVars->pArgs->pMeshOut;
	int32_t cornerBase = pMeshOut->mesh.cornerCount;
	bool realloced = false;
	for (int32_t i = 0; i < count; ++i) {
		int32_t bufIdx = pIdxTable[pIndices[i]];
		STUC_ASSERT("", pVars->cornerBuf.pBuf[bufIdx].corner >= 0);
		STUC_ASSERT("", pVars->cornerBuf.pBuf[bufIdx].corner < pMeshOut->mesh.vertCount);
		int32_t outCorner = meshAddCorner(&pArgs->pContext->alloc, pMeshOut, &realloced);
		STUC_ASSERT("", outCorner == cornerBase + i);
		pMeshOut->mesh.pCorners[outCorner] = pVars->cornerBuf.pBuf[bufIdx].corner;
		STUC_ASSERT("", pVars->cornerBuf.pBuf[bufIdx].edge >= 0);
		STUC_ASSERT("", pVars->cornerBuf.pBuf[bufIdx].edge < pMeshOut->mesh.edgeCount);
		pMeshOut->mesh.pEdges[outCorner] = pVars->cornerBuf.pBuf[bufIdx].edge;
		int32_t bufCorner = pVars->cornerBuf.pBuf[bufIdx].bufCorner;
		int32_t job = pVars->cornerBuf.pBuf[bufIdx].job;
		BufMesh *pBufMesh = &pArgs->pJobArgs[job].bufMesh;
		copyAllAttribs(&pMeshOut->mesh.cornerAttribs, outCorner,
					   &asMesh(pBufMesh)->mesh.cornerAttribs, bufCorner);
		STUC_ASSERT("", i >= 0 && i < count);
	}
	realloced = false;
	int32_t outFace = meshAddFace(&pArgs->pContext->alloc, pMeshOut, &realloced);
	if (pArgs->ppInFaceTable) {
		if (realloced) {
			//realloc to match meshOut face buf
			*pArgs->ppInFaceTable =
				pArgs->pContext->alloc.pRealloc(*pArgs->ppInFaceTable,
					sizeof(InFaceArr) * pMeshOut->faceBufSize);
		}
		STUC_ASSERT("", outFace < pMeshOut->faceBufSize);
		//add face to inFace table
		InFaceArr *pInFaceEntry = *pArgs->ppInFaceTable + outFace;
		STUC_ASSERT("", pVars->entryCount > 0);
		pInFaceEntry->pArr =
			pArgs->pContext->alloc.pCalloc(pVars->entryCount, sizeof(int32_t));
		memcpy(pInFaceEntry->pArr, pVars->pInFaces,
				sizeof(int32_t) * pVars->entryCount);
		pInFaceEntry->count = pVars->entryCount;
		pInFaceEntry->usg = pVars->stucFace.idx;
	}
	BufMesh *pBufMesh = &pArgs->pJobArgs[pVars->cornerBuf.pBuf[0].job].bufMesh;
	copyAllAttribs(&pMeshOut->mesh.faceAttribs,
				   outFace,
				   &asMesh(pBufMesh)->mesh.faceAttribs,
				   pVars->bufFace);
	pMeshOut->mesh.pFaces[outFace] = cornerBase;
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

void stucMergeSingleBorderFace(MergeSendOffArgs *pArgs, uint64_t *pTimeSpent,
                               int32_t entryIdx, PieceArr *pPieceArr,
							   FaceRange *pStucFace,
							   MergeBufHandles *pMergeBufHandles,
                               int32_t *pInFaces, int32_t entryCount) {
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
	int32_t bufFaceVirtual = vars.pPieceRoot->pEntry->face;
	vars.bufFace = convertBorderFaceIdx(pBufMesh, bufFaceVirtual).realIdx;
	if (!vars.pPieceRoot->pEntry) {
		return;
	}
	vars.stucFace = *pStucFace;
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
		for (int32_t i = 0; i < vars.cornerBuf.count; ++i) {
			int32_t vertIdx = vars.cornerBuf.pBuf[vars.pIdxTable[i]].corner;
			vars.pSortedVertBuf[i] = vertIdx;
			STUC_ASSERT("", i >= 0 && i < vars.cornerBuf.count);
		}
		FaceTriangulated tris = {0};
		tris = triangulateFace(pArgs->pContext->alloc, tempFace, pArgs->pMeshOut->pVerts,
		                       vars.pSortedVertBuf, false);
		for (int32_t i = 0; i < tris.triCount; ++i) {
			addFaceToOutMesh(&vars, tris.pCorners + (i * 3), 3,
			                 vars.pIdxTable, pInFaces);
			STUC_ASSERT("", i >= 0 && i < tris.triCount);
		}
		pArgs->pContext->alloc.pFree(tris.pCorners);
	}
	else {
		//STUC_ASSERT("", vars.cornerBuf.count <= 16);
		int32_t *pIndices =
			pArgs->pContext->alloc.pMalloc(sizeof(int32_t) * vars.cornerBuf.count);
		for (int32_t i = 0; i < vars.cornerBuf.count; ++i) {
			pIndices[i] = i;
		}
		addFaceToOutMesh(&vars, pIndices, vars.cornerBuf.count,
		                 vars.pIdxTable, pInFaces);
		pArgs->pContext->alloc.pFree(pIndices);
	}
	destroyEntries(pArgs->pContext, vars.pPieceRoot);
	CLOCK_STOP_NO_PRINT;
	pTimeSpent[5] += CLOCK_TIME_DIFF(start, stop);
}
