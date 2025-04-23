/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <mikktspace.h>

#include <uv_stucco_intern.h>
#include <merge_and_snap.h>

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
	HTableEntryCore core;
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


static
void tPieceVertInit(
	void *pUserData,
	HTableEntryCore *pEntryCore,
	const void *pKey,
	void *pInitInfo,
	I32 linIdx
) {
	TPieceVert *pEntry = (TPieceVert *)pEntryCore;
	pEntry->vert = *(I32 *)pKey;
}

static
bool tPieceVertCmp(
	const HTableEntryCore *pEntryCore,
	const void *pKey,
	const void *pInitInfo
) {
	TPieceVert *pEntry = (TPieceVert *)pEntryCore;
	return pEntry->vert == *(I32 *)pKey;
}

static
U64 tPieceVertMakeKey(const void *pKey) {
	return *(I32 *)pKey;
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
		if (pEntries[i].result != STUC_SEARCH_FOUND) {
			continue;
		}
		const TPieceBuf *pTPiece = pTPieces->pArr + pEntries[i].pEntry->tPiece;
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
		if (pEntries[i].result == STUC_SEARCH_FOUND &&
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
		else if (pEntries[i].result == STUC_SEARCH_ADDED) {
			pEntries[i].pEntry->tPiece = tPiece;
		}
	}
}

static
void addOrMergeFaceTPieces(
	const MapToMeshBasic *pBasic,
	TPieceBufArr *pTPieces,
	HTable *pVertTable,
	I32 faceIdx,
	bool add
) {
	const StucAlloc *pAlloc = &pBasic->pCtx->alloc;
	const StucMesh *pMesh = &pBasic->pInMesh->core;
	FaceRange face = stucGetFaceRange(&pBasic->pInMesh->core, faceIdx);
	TPieceVertSearch vertEntries[4] = {0};
	for (I32 i = 0; i < face.size; ++i) {
		vertEntries[i].result = stucHTableGet(
			pVertTable,
			0,
			pMesh->pCorners + face.start + i,
			&vertEntries[i].pEntry,
			add, NULL,
			tPieceVertMakeKey, NULL, tPieceVertInit, tPieceVertCmp
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
		PIXALC_DYN_ARR_ADD(TPieceBuf, &pBasic->pCtx->alloc, pTPieces, tPiece);
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
	const MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces, const InPieceArr *pInPiecesClip,
	PixalcLinAlloc *pMergeAlloc,
	TPieceBufArr *pTPieces,
	HTable *pVertTable,
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
			pBasic,
			pInPiece,
			pBufMesh,
			pEntry->bufCorner.corner
		);
		PIX_ERR_ASSERT(
			"",
			srcFaces.in >= 0 && srcFaces.in < pBasic->pInMesh->core.faceCount
		);
		if (!pChecked[srcFaces.in]) {
			addOrMergeFaceTPieces(pBasic, pTPieces, pVertTable, srcFaces.in, true);
			pChecked[srcFaces.in] = true;
		}
	}
}

static
void buildTPieces(
	const MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces, const InPieceArr *pInPiecesClip,
	HTable *pMergeTable,
	TPieceArr *pTPieces
) {
	PixalcLinAlloc *pMergeAlloc = stucHTableAllocGet(pMergeTable, 0);
	PixalcLinAlloc *pMergeAllocIntersect = stucHTableAllocGet(pMergeTable, 1);
	HTable vertTable = { 0 };
	stucHTableInit(
		&pBasic->pCtx->alloc,
		&vertTable,
		pixalcLinAllocGetCount(pMergeAlloc) + pixalcLinAllocGetCount(pMergeAllocIntersect),
		(I32Arr) {.pArr = (I32[]){sizeof(TPieceVert)}, .count = 1},
		NULL
	);
	bool *pChecked =
		pBasic->pCtx->alloc.fpCalloc(pBasic->pInMesh->core.faceCount, sizeof(bool));
	TPieceBufArr tPiecesBuf = {0};
	buildTPiecesForBufVerts(
		pBasic,
		pInPieces, pInPiecesClip,
		pMergeAlloc,
		&tPiecesBuf,
		&vertTable,
		pChecked
	);
	buildTPiecesForBufVerts(
		pBasic,
		pInPieces, pInPiecesClip,
		pMergeAllocIntersect,
		&tPiecesBuf,
		&vertTable,
		pChecked
	);
	PIX_ERR_ASSERT("map-to-mesh should have returned earlier if empty", tPiecesBuf.pArr);
	//not adding new t-pieces, only merging existing ones
	const StucMesh *pInMesh = &pBasic->pInMesh->core;
	for (I32 i = 0; i < pInMesh->faceCount; ++i) {
		if (!pChecked[i]) {
			addOrMergeFaceTPieces(pBasic, &tPiecesBuf, &vertTable, i, false);
			pChecked[i] = true;
		}
	}
	pBasic->pCtx->alloc.fpFree(pChecked);
	
	for (I32 i = 0; i < pInMesh->faceCount; ++i) {
		FaceRange face = stucGetFaceRange(pInMesh, i);
		for (I32 j = 0; j < face.size; ++j) {
			TPieceVert *pEntry = NULL;
			SearchResult result = stucHTableGet(
				&vertTable,
				0,
				&pInMesh->pCorners[face.start + j],
				&pEntry,
				false, NULL,
				tPieceVertMakeKey, NULL, NULL, tPieceVertCmp
			);
			if (result == STUC_SEARCH_NOT_FOUND) {
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
				PIXALC_DYN_ARR_ADD(TPiece, &pBasic->pCtx->alloc, pTPieces, pBuf->idx);
				pTPieces->pArr[pBuf->idx] = (TPiece) {0};
				pBuf->added = true;
			}
			PIX_ERR_ASSERT("", pBuf->idx >= 0 && pBuf->idx < pTPieces->count);
			I32 faceArrIdx = -1;
			PIXALC_DYN_ARR_ADD(
				TPieceInFace,
				&pBasic->pCtx->alloc,
				(&pTPieces->pArr[pBuf->idx].inFaces),
				faceArrIdx
			);
			PIX_ERR_ASSERT("", faceArrIdx != -1);
			pTPieces->pArr[pBuf->idx].inFaces.pArr[faceArrIdx].idx = i;
			pTPieces->pArr[pBuf->idx].inFaces.pArr[faceArrIdx].size = face.size;
			pTPieces->faceCount++;
		}
	}
	stucHTableDestroy(&vertTable);
	pBasic->pCtx->alloc.fpFree(tPiecesBuf.pArr);
}

static
I32 tangentJobGetRange(const MapToMeshBasic *pBasic, void *pInitInfo) {
	return ((TPieceArr *)pInitInfo)->faceCount;
}

static
void tangentJobInit(MapToMeshBasic *pBasic, void *pInitInfo, void *pEntryVoid) {
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
	MapToMeshBasic *pBasic,
	Mesh *pInMesh, //in-mesh is const in MapToMeshBasic
	const InPieceArr *pInPieces, const InPieceArr *pInPiecesClip,
	HTable *pMergeTable
) {
	StucErr err = PIX_ERR_SUCCESS;
	TPieceArr tPieces = {0};
	buildTPieces(pBasic, pInPieces, pInPiecesClip, pMergeTable, &tPieces);
	PIX_ERR_ASSERT("", tPieces.pArr);
	I32 jobCount = tPieces.count; //max jobs
	TangentJobArgs jobArgs[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pBasic,
		&jobCount,
		jobArgs, sizeof(TangentJobArgs),
		&tPieces,
		tangentJobGetRange, tangentJobInit
	);
	tPieces.pInFaces = pBasic->pCtx->alloc.fpCalloc(tPieces.faceCount, sizeof(I32));
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
					&pBasic->pCtx->alloc,
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
			pBasic->pCtx->alloc.fpFree(tPieces.pArr[i].inFaces.pArr);
		}
		//last job may not match jobcount depending on num faces in each t-piece,
		//so update that here
		jobArgs[job].core.range.end = tPieces.faceCount;
		jobCount = job + 1;
	}
	pBasic->pCtx->alloc.fpFree(tPieces.pArr);
	tPieces = (TPieceArr) {.pInFaces = tPieces.pInFaces, .faceCount = tPieces.faceCount};
	err = stucDoJobInParallel(
		pBasic,
		jobCount, jobArgs, sizeof(TangentJobArgs),
		stucBuildTangents
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	for (I32 i = 0; i < jobCount; ++i) {
		copyTangentsFromJobFaces(pInMesh, &tPieces, jobArgs + i);
	}
	PIX_ERR_CATCH(0, err, ;);
	for (I32 i = 0; i < jobCount; ++i) {
		pBasic->pCtx->alloc.fpFree(jobArgs[i].faces.pArr);
		pBasic->pCtx->alloc.fpFree(jobArgs[i].pTangents);
		pBasic->pCtx->alloc.fpFree(jobArgs[i].pTSigns);
	}
	pBasic->pCtx->alloc.fpFree(tPieces.pInFaces);
	return err;
}

static
int mikktGetNumFaces(const SMikkTSpaceContext *pCtx) {
	TangentJobArgs *pArgs = pCtx->m_pUserData;
	return pArgs->faces.count;
}

static
I32 mikktGetInFace(const TangentJobArgs *pArgs, I32 iFace) {
	return pArgs->pTPieces->pInFaces[pArgs->core.range.start + iFace];
}

static
int mikktGetNumVertsOfFace(const SMikkTSpaceContext *pCtx, const int iFace) {
	TangentJobArgs *pArgs = pCtx->m_pUserData;
	I32 inFaceIdx = mikktGetInFace(pArgs, iFace);
	return stucGetFaceRange(&pArgs->core.pBasic->pInMesh->core, inFaceIdx).size;
}

static
I32 mikktGetInFaceStart(const TangentJobArgs *pArgs, I32 inFaceIdx) {
	return pArgs->core.pBasic->pInMesh->core.pFaces[inFaceIdx];
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
	I32 vertIdx = pArgs->core.pBasic->pInMesh->core.pCorners[inFaceStart + iVert];
	*(V3_F32 *)pFvPosOut = pArgs->core.pBasic->pInMesh->pPos[vertIdx];
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
	*(V3_F32 *)pFvNormOut = pArgs->core.pBasic->pInMesh->pNormals[inFaceStart + iVert];
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
	*(V2_F32 *)pFvTexcOut = pArgs->core.pBasic->pInMesh->pUvs[inFaceStart + iVert];
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

StucErr stucBuildTangents(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	TangentJobArgs *pArgs = pArgsVoid;
	const StucAlloc *pAlloc = &pArgs->core.pBasic->pCtx->alloc;

	pArgs->pTangents = pAlloc->fpCalloc(pArgs->cornerCount, sizeof(V3_F32));
	pArgs->pTSigns = pAlloc->fpCalloc(pArgs->cornerCount, sizeof(F32));

	SMikkTSpaceInterface mikktInterface = {
		.m_getNumFaces = mikktGetNumFaces,
		.m_getNumVerticesOfFace = mikktGetNumVertsOfFace,
		.m_getPosition = mikktGetPos,
		.m_getNormal = mikktGetNormal,
		.m_getTexCoord = mikktGetTexCoord,
		.m_setTSpaceBasic = mikktSetTSpaceBasic
	};
	SMikkTSpaceContext mikktContext = {
		.m_pInterface = &mikktInterface,
		.m_pUserData = pArgs
	};
	if (!genTangSpaceDefault(&mikktContext)) {
		PIX_ERR_RETURN(err, "mikktspace func 'genTangSpaceDefault' returned error");
	}
	return err;
}
