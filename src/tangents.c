/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <mikktspace.h>

#include <uv_stucco_intern.h>
#include <merge_and_snap.h>
#include <utils.h>

typedef struct TPieceBuf {
	U32 mergedWith : 31;
	U32 merged : 1;
	U32 idx : 31;
	U32 added : 1;
} TPieceBuf;

typedef struct TPieceBufArr {
	TPieceBuf *pArr;
	I32 size;
	I32 count;
} TPieceBufArr;

typedef struct TPieceInFace {
	U32 idx : 29;
	U32 size : 3; //used for in-faces, so max face size of 4
} TPieceInFace;

typedef struct TPieceInFaceArr {
	TPieceInFace *pArr;
	I32 size;
	I32 count;
} TPieceInFaceArr;

typedef struct TPiece {
	 TPieceInFaceArr inFaces;
} TPiece;

typedef struct TPieceArr {
	TPiece *pArr;
	I32 size;
	I32 count;
	I32 *pInFaces;
	I32 faceCount;
} TPieceArr;

typedef struct TPieceVert {
	PixuctHTableEntryCore core;
	I32 vert;
	I32 tPiece;
} TPieceVert;

typedef struct TangentJobArgs {
	JobArgs core;
	const TPieceArr *pTPieces;
	I32Arr faces;
	I32 cornerCount;
	V3_F32 *pTangents;
	F32 *pTSigns;
} TangentJobArgs;

typedef struct TangentTris {
	StucContext pCtx;
	Mesh *pMesh;
} TangentTris;


static
void tPieceVertInit(
	void *pUserData,
	PixuctHTableEntryCore *pEntryCore,
	const void *pKey,
	void *pInitInfo,
	I32 linIdx
) {
	TPieceVert *pEntry = (TPieceVert *)pEntryCore;
	pEntry->vert = *(I32 *)pKey;
}

static
bool tPieceVertCmp(
	const PixuctHTableEntryCore *pEntryCore,
	const void *pKey,
	const void *pInitInfo
) {
	TPieceVert *pEntry = (TPieceVert *)pEntryCore;
	return pEntry->vert == *(I32 *)pKey;
}

typedef struct TPieceVertSearch {
	TPieceVert *pEntry;
	SearchResult result;
} TPieceVertSearch;


static
void getLowestTPiece(
	const TPieceBufArr *pTPieces,
	const TPieceVertSearch *pEntries,
	FaceRange *pFace,
	I32 *pLowestTPiece
) {
	for (I32 i = 0; i < pFace->size; ++i) {
		if (pEntries[i].result != PIX_SEARCH_FOUND) {
			continue;
		}
		I32 tPieceIdx = pEntries[i].pEntry->tPiece;
		while (pTPieces->pArr[tPieceIdx].merged) {
			tPieceIdx = pTPieces->pArr[tPieceIdx].mergedWith;
		}
		PIX_ERR_ASSERT(
			"if t-piece is merged, it should be with a piece of a lower idx",
			tPieceIdx <= pEntries[i].pEntry->tPiece
		);
		if (tPieceIdx < *pLowestTPiece) {
			*pLowestTPiece = tPieceIdx;
		}
	}
}

static
void setVertsAndMergeTPieces(
	TPieceBufArr *pTPieces,
	TPieceVertSearch *pEntries,
	FaceRange *pFace,
	I32 tPiece
) {
	for (I32 i = 0; i < pFace->size; ++i) {
		if (pEntries[i].result == PIX_SEARCH_FOUND &&
			pEntries[i].pEntry->tPiece != tPiece
		) {
			PIX_ERR_ASSERT(
				"we only merge to pieces with a lower idx",
				pEntries[i].pEntry->tPiece > tPiece
			);
			TPieceBuf *pTPiece = pTPieces->pArr + pEntries[i].pEntry->tPiece;
			pTPiece->merged = true;
			pTPiece->mergedWith = tPiece;
			//even if the vert's current tpiece was merged,
			// updating the vert entry reduces time spent walking merge chains
			pEntries[i].pEntry->tPiece = tPiece;
		}
		else if (pEntries[i].result == PIX_SEARCH_ADDED) {
			pEntries[i].pEntry->tPiece = tPiece;
		}
	}
}

static
void addOrMergeFaceTPieces(
	StucContext pCtx,
	const Mesh *pInMesh,
	TPieceBufArr *pTPieces,
	PixuctHTable *pVertTable,
	I32 faceIdx,
	bool add
) {
	const StucMesh *pMesh = &pInMesh->core;
	FaceRange face = stucGetFaceRange(&pInMesh->core, faceIdx);
	TPieceVertSearch vertEntries[4] = {0};
	for (I32 i = 0; i < face.size; ++i) {
		vertEntries[i].result = pixuctHTableGet(
			pVertTable,
			0,
			pMesh->pCorners + face.start + i,
			(void **)&vertEntries[i].pEntry,
			add, NULL,
			stucKeyFromI32, NULL, tPieceVertInit, tPieceVertCmp
		);
	}
	I32 lowestTPiece = INT32_MAX;
	getLowestTPiece(pTPieces, vertEntries, &face, &lowestTPiece);
	I32 tPiece = -1;
	if (lowestTPiece == INT32_MAX) {
		if (!add) {
			return; //no entries were found for this face
		}
		//all entries are new, so append new tPiece to arr
		PIXALC_DYN_ARR_ADD(TPieceBuf, &pCtx->alloc, pTPieces, tPiece);
		pTPieces->pArr[tPiece] = (TPieceBuf) {0};
	}
	else {
		tPiece = lowestTPiece;
	}
	PIX_ERR_ASSERT("", tPiece >= 0 && tPiece < pTPieces->count);
	setVertsAndMergeTPieces(pTPieces, vertEntries, &face, tPiece);
}

static
void buildTPiecesForBufVerts(
	StucContext pCtx,
	const Mesh *pInMesh,
	const InPieceArr *pInPieces, const InPieceArr *pInPiecesClip,
	PixalcLinAlloc *pMergeAlloc,
	TPieceBufArr *pTPieces,
	PixuctHTable *pVertTable,
	bool *pChecked
) {
	PixalcLinAllocIter iter = {0};
	pixalcLinAllocIterInit(pMergeAlloc, (Range) {0, INT32_MAX}, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		VertMerge *pEntry = pixalcLinAllocGetItem(&iter);
		const InPiece *pInPiece = NULL;
		const BufMesh *pBufMesh = NULL;
		getBufMeshForVertMergeEntry(
			pInPieces, pInPiecesClip,
			pEntry,
			&pInPiece,
			&pBufMesh
		);
		SrcFaces srcFaces = stucGetSrcFacesForBufCorner(
			pInPiece,
			pBufMesh,
			pEntry->bufCorner.corner
		);
		PIX_ERR_ASSERT(
			"",
			srcFaces.in >= 0 && srcFaces.in < pInMesh->core.faceCount
		);
		if (!pChecked[srcFaces.in]) {
			addOrMergeFaceTPieces(pCtx, pInMesh, pTPieces, pVertTable, srcFaces.in, true);
			pChecked[srcFaces.in] = true;
		}
	}
}

static
void buildTPieces(
	StucContext pCtx,
	const Mesh *pInMesh,
	const InPieceArr *pInPieces, const InPieceArr *pInPiecesClip,
	PixuctHTable *pMergeTable,
	TPieceArr *pTPieces
) {
	PixalcLinAlloc *pMergeAlloc = pixuctHTableAllocGet(pMergeTable, 0);
	PixalcLinAlloc *pMergeAllocIntersect = pixuctHTableAllocGet(pMergeTable, 1);
	PixuctHTable vertTable = { 0 };
	pixuctHTableInit(
		&pCtx->alloc,
		&vertTable,
		pixalcLinAllocGetCount(pMergeAlloc) + pixalcLinAllocGetCount(pMergeAllocIntersect),
		(I32Arr) {.pArr = (I32[]){sizeof(TPieceVert)}, .count = 1},
		NULL
	);
	bool *pChecked =
		pCtx->alloc.fpCalloc(pInMesh->core.faceCount, sizeof(bool));
	TPieceBufArr tPiecesBuf = {0};
	buildTPiecesForBufVerts(
		pCtx,
		pInMesh,
		pInPieces, pInPiecesClip,
		pMergeAlloc,
		&tPiecesBuf,
		&vertTable,
		pChecked
	);
	buildTPiecesForBufVerts(
		pCtx,
		pInMesh,
		pInPieces, pInPiecesClip,
		pMergeAllocIntersect,
		&tPiecesBuf,
		&vertTable,
		pChecked
	);
	PIX_ERR_ASSERT("map-to-mesh should have returned earlier if empty", tPiecesBuf.pArr);
	//not adding new t-pieces, only merging existing ones
	const StucMesh *pInCore = &pInMesh->core;
	for (I32 i = 0; i < pInCore->faceCount; ++i) {
		if (!pChecked[i]) {
			addOrMergeFaceTPieces(pCtx, pInMesh, &tPiecesBuf, &vertTable, i, false);
			pChecked[i] = true;
		}
	}
	pCtx->alloc.fpFree(pChecked);
	
	for (I32 i = 0; i < pInCore->faceCount; ++i) {
		FaceRange face = stucGetFaceRange(pInCore, i);
		for (I32 j = 0; j < face.size; ++j) {
			TPieceVert *pEntry = NULL;
			SearchResult result = pixuctHTableGet(
				&vertTable,
				0,
				&pInCore->pCorners[face.start + j],
				(void **)&pEntry,
				false, NULL,
				stucKeyFromI32, NULL, NULL, tPieceVertCmp
			);
			if (result == PIX_SEARCH_NOT_FOUND) {
				continue;
			}
			I32 bufIdx = pEntry->tPiece;
			while (tPiecesBuf.pArr[bufIdx].merged) {
				bufIdx = tPiecesBuf.pArr[bufIdx].mergedWith;
			}
			//update so future searches don't need to walk merge chain
			pEntry->tPiece = bufIdx;
			TPieceBuf *pBuf = tPiecesBuf.pArr + bufIdx;
			if (!pBuf->added) {
				PIXALC_DYN_ARR_ADD(TPiece, &pCtx->alloc, pTPieces, pBuf->idx);
				pTPieces->pArr[pBuf->idx] = (TPiece) {0};
				pBuf->added = true;
			}
			PIX_ERR_ASSERT("", pBuf->idx >= 0u && pBuf->idx < pTPieces->count);
			I32 faceArrIdx = -1;
			PIXALC_DYN_ARR_ADD(
				TPieceInFace,
				&pCtx->alloc,
				(&pTPieces->pArr[pBuf->idx].inFaces),
				faceArrIdx
			);
			PIX_ERR_ASSERT("", faceArrIdx != -1);
			pTPieces->pArr[pBuf->idx].inFaces.pArr[faceArrIdx].idx = i;
			pTPieces->pArr[pBuf->idx].inFaces.pArr[faceArrIdx].size = face.size;
			pTPieces->faceCount++;
		}
	}
	pixuctHTableDestroy(&vertTable);
	pCtx->alloc.fpFree(tPiecesBuf.pArr);
}

static
I32 tangentJobGetRange(StucContext pCtx, const void *pShared, void *pInitInfo) {
	return ((TPieceArr *)pInitInfo)->faceCount;
}

static
void tangentJobInit(StucContext pCtx, void *pShared, void *pInitInfo, void *pEntryVoid) {
	TangentJobArgs *pEntry = pEntryVoid;
	pEntry->pTPieces = (TPieceArr *)pInitInfo;
}

static
void copyTangentsFromJobFaces(
	Mesh *pInMesh,
	const TPieceArr *pTPieces,
	const TangentJobArgs *pArgs
) {
	for (I32 i = 0; i < pArgs->faces.count; ++i) {
		I32 jobFaceStart = pArgs->faces.pArr[i];
		I32 inFaceIdx = pTPieces->pInFaces[pArgs->core.range.start + i];
		FaceRange inFace = stucGetFaceRange(&pInMesh->core, inFaceIdx);
		for (I32 j = 0; j < inFace.size; ++j) {
			pInMesh->pTangents[inFace.start + j] = pArgs->pTangents[jobFaceStart + j];
			pInMesh->pTSigns[inFace.start + j] = pArgs->pTSigns[jobFaceStart + j];
		}
	}
}

StucErr stucBuildTangentsForInPieces(
	StucContext pCtx,
	Mesh *pInMesh,
	const InPieceArr *pInPieces, const InPieceArr *pInPiecesClip,
	PixuctHTable *pMergeTable
) {
	StucErr err = PIX_ERR_SUCCESS;
	TPieceArr tPieces = {0};
	buildTPieces(pCtx, pInMesh, pInPieces, pInPiecesClip, pMergeTable, &tPieces);
	PIX_ERR_ASSERT("", tPieces.pArr);
	I32 jobCount = tPieces.count; //max jobs
	TangentJobArgs jobArgs[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pCtx,
		pInMesh,
		&jobCount,
		jobArgs, sizeof(TangentJobArgs),
		&tPieces,
		tangentJobGetRange, tangentJobInit
	);
	tPieces.pInFaces = pCtx->alloc.fpCalloc(tPieces.faceCount, sizeof(I32));
	{
		tPieces.faceCount = 0;
		I32 job = 0;
		for (I32 i = 0; i < tPieces.count; ++i) {
			if (job < jobCount - 1 && tPieces.faceCount >= jobArgs[job].core.range.end) {
				jobArgs[job].core.range.end = tPieces.faceCount;
				job++;
				jobArgs[job].core.range.start = tPieces.faceCount;
			}
			PIX_ERR_ASSERT("", tPieces.pArr[i].inFaces.pArr);
			for (I32 j = 0; j < tPieces.pArr[i].inFaces.count; ++j) {
				I32 faceJobLocal = -1;
				PIXALC_DYN_ARR_ADD(
					I32,
					&pCtx->alloc,
					(&jobArgs[job].faces),
					faceJobLocal
				);
				PIX_ERR_ASSERT("", faceJobLocal != -1);
				jobArgs[job].faces.pArr[faceJobLocal] = jobArgs[job].cornerCount;

				TPieceInFace face = tPieces.pArr[i].inFaces.pArr[j];
				tPieces.pInFaces[tPieces.faceCount] = face.idx;
				tPieces.faceCount++;
				jobArgs[job].cornerCount += face.size;
			}
			pCtx->alloc.fpFree(tPieces.pArr[i].inFaces.pArr);
		}
		//last job may not match jobcount depending on num faces in each t-piece,
		//so update that here
		jobArgs[job].core.range.end = tPieces.faceCount;
		jobCount = job + 1;
	}
	pCtx->alloc.fpFree(tPieces.pArr);
	tPieces = (TPieceArr) {.pInFaces = tPieces.pInFaces, .faceCount = tPieces.faceCount};
	err = stucDoJobInParallel(
		pCtx,
		jobCount, jobArgs, sizeof(TangentJobArgs),
		stucBuildTangents
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	for (I32 i = 0; i < jobCount; ++i) {
		copyTangentsFromJobFaces(pInMesh, &tPieces, jobArgs + i);
	}
	PIX_ERR_CATCH(0, err, ;);
	for (I32 i = 0; i < jobCount; ++i) {
		pCtx->alloc.fpFree(jobArgs[i].faces.pArr);
		pCtx->alloc.fpFree(jobArgs[i].pTangents);
		pCtx->alloc.fpFree(jobArgs[i].pTSigns);
	}
	pCtx->alloc.fpFree(tPieces.pInFaces);
	return err;
}

static
int mikktGetNumFaces(const SMikkTSpaceContext *pCtx) {
	TangentJobArgs *pArgs = pCtx->m_pUserData;
	return pArgs->faces.count;
}

static
int mikktTrisGetNumFaces(const SMikkTSpaceContext *pCtx) {
	TangentTris *pState = pCtx->m_pUserData;
	return pState->pMesh->core.faceCount;
}

static
I32 mikktGetInFace(const TangentJobArgs *pArgs, I32 iFace) {
	return pArgs->pTPieces->pInFaces[pArgs->core.range.start + iFace];
}

static
int mikktGetNumVertsOfFace(const SMikkTSpaceContext *pCtx, const int iFace) {
	TangentJobArgs *pArgs = pCtx->m_pUserData;
	I32 inFaceIdx = mikktGetInFace(pArgs, iFace);
	return stucGetFaceRange(&((const Mesh *)pArgs->core.pShared)->core, inFaceIdx).size;
}

static
int mikktTrisGetNumVertsOfFace(const SMikkTSpaceContext *pCtx, const int iFace) {
	return 3;
}

static
I32 mikktGetInFaceStart(const TangentJobArgs *pArgs, I32 inFaceIdx) {
	return ((const Mesh *)pArgs->core.pShared)->core.pFaces[inFaceIdx];
}

static
void mikktGetPos(
	const SMikkTSpaceContext *pCtx,
	F32 *pFvPosOut,
	const int iFace,
	const int iVert
) {
	TangentJobArgs *pArgs = pCtx->m_pUserData;
	I32 inFaceIdx = mikktGetInFace(pArgs, iFace);
	I32 inFaceStart = mikktGetInFaceStart(pArgs, inFaceIdx);
	const Mesh *pInMesh = pArgs->core.pShared;
	I32 vertIdx = pInMesh->core.pCorners[inFaceStart + iVert];
	*(V3_F32 *)pFvPosOut = pInMesh->pPos[vertIdx];
}

static
void mikktTrisGetPos(
	const SMikkTSpaceContext *pCtx,
	F32 *pFvPosOut,
	const int iFace,
	const int iVert
) {
	TangentTris *pState = pCtx->m_pUserData;
	I32 vertIdx = pState->pMesh->core.pCorners[iFace * 3 + iVert];
	*(V3_F32 *)pFvPosOut = pState->pMesh->pPos[vertIdx];
}

static
void mikktGetNormal(
	const SMikkTSpaceContext *pCtx,
	F32 *pFvNormOut,
	const int iFace,
	const int iVert
) {
	TangentJobArgs *pArgs = pCtx->m_pUserData;
	I32 inFaceIdx = mikktGetInFace(pArgs, iFace);
	I32 inFaceStart = mikktGetInFaceStart(pArgs, inFaceIdx);
	const Mesh *pInMesh = pArgs->core.pShared;
	*(V3_F32 *)pFvNormOut = pInMesh->pNormals[inFaceStart + iVert];
}

static
void mikktTrisGetNormal(
	const SMikkTSpaceContext *pCtx,
	F32 *pFvNormOut,
	const int iFace,
	const int iVert
) {
	TangentTris *pState = pCtx->m_pUserData;
	I32 corner = iFace * 3 + iVert;
	*(V3_F32 *)pFvNormOut = pState->pMesh->pNormals[corner];
}

static
void mikktGetTexCoord(
	const SMikkTSpaceContext *pCtx,
	F32 *pFvTexcOut,
	const int iFace,
	const int iVert
) {
	TangentJobArgs *pArgs = pCtx->m_pUserData;
	I32 inFaceIdx = mikktGetInFace(pArgs, iFace);
	I32 inFaceStart = mikktGetInFaceStart(pArgs, inFaceIdx);
	const Mesh *pInMesh = pArgs->core.pShared;
	*(V2_F32 *)pFvTexcOut = pInMesh->pUvs[inFaceStart + iVert];
}

static
void mikktTrisGetTexCoord(
	const SMikkTSpaceContext *pCtx,
	F32 *pFvTexcOut,
	const int iFace,
	const int iVert
) {
	TangentTris *pState = pCtx->m_pUserData;
	I32 corner = iFace * 3 + iVert;
	*(V2_F32 *)pFvTexcOut = pState->pMesh->pUvs[corner];
}

static
void mikktSetTSpaceBasic(
	const SMikkTSpaceContext *pCtx,
	const F32 *pFvTangent,
	const F32 fSign,
	const int iFace,
	const int iVert
) {
	TangentJobArgs *pArgs = pCtx->m_pUserData;
	I32 corner = pArgs->faces.pArr[iFace] + iVert;
	pArgs->pTangents[corner] = *(V3_F32 *)pFvTangent;
	pArgs->pTSigns[corner] = fSign;
}

static
void mikktTrisSetTSpaceBasic(
	const SMikkTSpaceContext *pCtx,
	const F32 *pFvTangent,
	const F32 fSign,
	const int iFace,
	const int iVert
) {
	TangentTris *pState = pCtx->m_pUserData;
	I32 corner = iFace * 3 + iVert;
	pState->pMesh->pTangents[corner] = *(V3_F32 *)pFvTangent;
	pState->pMesh->pTSigns[corner] = fSign;
}

static
StucErr stucBuildTangentsIntern(SMikkTSpaceContext *pMikktCtx) {
	StucErr err = PIX_ERR_SUCCESS;
	if (!genTangSpaceDefault(pMikktCtx)) {
		PIX_ERR_RETURN(err, "mikktspace func 'genTangSpaceDefault' returned error");
	}
	return err;
}

StucErr stucBuildTangents(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	SMikkTSpaceInterface mikktInterface = {
		.m_getNumFaces = mikktGetNumFaces,
		.m_getNumVerticesOfFace = mikktGetNumVertsOfFace,
		.m_getPosition = mikktGetPos,
		.m_getNormal = mikktGetNormal,
		.m_getTexCoord = mikktGetTexCoord,
		.m_setTSpaceBasic = mikktSetTSpaceBasic
	};
	SMikkTSpaceContext mikktCtx = {
		.m_pInterface = &mikktInterface,
		.m_pUserData = pArgsVoid
	};
	TangentJobArgs *pArgs = pArgsVoid;
	const StucAlloc *pAlloc = &pArgs->core.pCtx->alloc;
	pArgs->pTangents = pAlloc->fpCalloc(pArgs->cornerCount, sizeof(V3_F32));
	pArgs->pTSigns = pAlloc->fpCalloc(pArgs->cornerCount, sizeof(F32));
	err = stucBuildTangentsIntern(&mikktCtx);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

StucErr stucBuildTangentsForTris(StucContext pCtx, Mesh *pMesh) {
	StucErr err = PIX_ERR_SUCCESS;
	SMikkTSpaceInterface mikktInterface = {
		.m_getNumFaces = mikktTrisGetNumFaces,
		.m_getNumVerticesOfFace = mikktTrisGetNumVertsOfFace,
		.m_getPosition = mikktTrisGetPos,
		.m_getNormal = mikktTrisGetNormal,
		.m_getTexCoord = mikktTrisGetTexCoord,
		.m_setTSpaceBasic = mikktTrisSetTSpaceBasic
	};
	TangentTris state = {.pCtx = pCtx, .pMesh = pMesh};
	SMikkTSpaceContext mikktCtx = {
		.m_pInterface = &mikktInterface,
		.m_pUserData = &state
	};
	err = stucBuildTangentsIntern(&mikktCtx);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}
