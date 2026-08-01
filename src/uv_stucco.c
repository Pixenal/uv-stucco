/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

#include <pixenals_alloc_utils.h>
#include <pixenals_thread_utils.h>
#include <pixenals_error_utils.h>
#include <cluster_tree_2d.h>

#include <io.h>
#include <attrib_utils.h>
#include <utils.h>
#include <interp_and_xform.h>
#include <merge_and_snap.h>
#include <cark_stages.h>

//TODO a lot of these funcs can be moved out of this file

//TODO add this as an option in ui?
#define STUC_CLUTRE_MIN_FACES 12

static
void setDefaultStageReport(StucCtx *pCtx) {
	pCtx->stageReport.outOf = 50,
	pCtx->stageReport.fpBegin = stucStageBegin;
	pCtx->stageReport.fpProgress = stucStageProgress;
	pCtx->stageReport.fpEnd = stucStageEnd;
}

static
StucErr initCarkOut(
	const StucAlloc *pAlloc,
	const PixioFPtrs *pIo,
	I32 threadCount,
	StucCark *pCark
) {
	StucErr err = PIX_ERR_SUCCESS;
	err = carkOutInit(pAlloc, pIo, threadCount, &pCark->ctx);
	PIX_ERR_RETURN_IFNOT(err, "");

	err = carkOutStageInit(
		&pCark->ctx,
		"island split",
		&STUC_STAGE_INFO_ISLAND_SPLIT,
		pCark->stageHandleArr + STUC_STAGE_ISLAND_SPLIT
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

StucErr stucInit(
	StucCtx *pCtx,
	StucAlloc *pAlloc,
	StucThreadPool *pThreadPool,
	PixioFPtrs *pIo,
	StucTypeDefaultConfig *pTypeDefaultConfig,
	StucStageReport *pStageReport,
	bool threadLogging
) {
	//TODO fix inconsistent use of PixErr & StucErr typedef
	//TODO StucAlloc typedefs PixalcFPtrs, but no typedef for PixioFPtrs?
	//inconsistent
	StucErr err = PIX_ERR_SUCCESS;
	stucIoInit();
#ifndef NDEBUG
	stucIoDataTagValidate();
#endif
	StucAlloc alloc = {0};
	if (pAlloc) {
		stucAllocSetCustom(&alloc, pAlloc);
	}
	else {
		stucAllocSetDefault(&alloc);
	}
	pCtx->alloc = alloc;
	if (pThreadPool) {
		err = stucThreadPoolSetCustom(pCtx, pThreadPool);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	else {
		stucThreadPoolSetDefault(pCtx);
	}
	if (pIo) {
		stucIoSetCustom(pCtx, pIo);
	}
	else {
		stucIoSetDefault(pCtx);
	}
	pCtx->threadCount = 12;
	err = pCtx->threadPool.fpInit(
		&pCtx->threadPool.handle,
		&pCtx->threadCount,
		&pCtx->alloc,
		threadLogging
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	if (pTypeDefaultConfig) {
		pCtx->typeDefaults = *pTypeDefaultConfig;
	}
	else {
		stucSetTypeDefaultConfig(pCtx);
	}
	if (pStageReport) {
		pCtx->stageReport = *pStageReport;
	}
	else {
		setDefaultStageReport(pCtx);
	}

	//TODO add ability to set custom specialAttrib names

	err = initCarkOut(&pCtx->alloc, &pCtx->io, pCtx->threadCount, &pCtx->cark);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	PIX_ERR_CATCH(0, err,
		stucContextDestroy(pCtx);
	);
	return err;
}

//TODO rename to stucDestroy
StucErr stucContextDestroy(StucCtx *pCtx) {
	if (pCtx->threadPool.fpDestroy) {
		pCtx->threadPool.fpDestroy(&pCtx->threadPool.handle);
	}
	carkOutDestroy(&pCtx->cark.ctx);
	*pCtx = (StucCtx){0};
	return PIX_ERR_SUCCESS;
}

//TODO replace these with StucUsg and StucObj arr structs, that combine arr and count
StucErr stucMapFileLoadForEdit(
	StucCtx *pCtx,
	const char *filePath,
	I32 *pObjCount,
	StucObject **ppObjArr,
	I32 *pUsgCount,
	StucUsg **ppUsgArr,
	I32 *pFlatCutoffCount,
	StucObject **ppFlatCutoffArr,
	StucAttribIndexedArr *pIndexedAttribs
) {
	//TODO reimplement
	return PIX_ERR_ERROR;
}

static
void triCacheBuild(const StucAlloc *pAlloc, StucMap *pMap) {
	bool ngons = checkForNgonsInMesh(&pMap->pMesh->core);
	if (!ngons) {
		return;
	}
	U8 triBuf[PIXMSH_NGON_MAX_SIZE];
	pMap->triCache.pArr =
		pAlloc->fpCalloc(pMap->pMesh->core.faceCount, sizeof(FaceTriangulated));
	pixalcLinAllocInit(pAlloc, &pMap->triCache.alloc, 3, 16, false);
	for (I32 i = 0; i < pMap->pMesh->core.faceCount; ++i) {
		FaceRange face = stucGetFaceRange(&pMap->pMesh->core, i);
		if (face.range.size <= 4) {
			continue;
		}
		FaceTriangulated *pTris = pMap->triCache.pArr + i;
		pTris->count = stucTriangulateFaceFromVerts(pAlloc, &face, pMap->pMesh, triBuf);
		if (!pTris->count) {
			continue;
		}
		void *pTrisMem = NULL;
		pTris->idx = pixalcLinAlloc(&pMap->triCache.alloc, &pTrisMem, pTris->count);
		memcpy(pTrisMem, triBuf, pTris->count * 3);
	}
}

StucErr initFlatCutoff(
	StucCtx *pCtx,
	Usg *pUsg,
	StucObject *pCutoffObj
) {
	StucErr err = PIX_ERR_SUCCESS;
	pUsg->pFlatCutoff = pCtx->alloc.fpCalloc(1, sizeof(Mesh));
	pUsg->pFlatCutoff->core = *(StucMesh *)pCutoffObj->pData;

	err = stucAttemptToSetMissingActiveDomains(&pUsg->pFlatCutoff->core);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = stucAssignActiveAliases(
		pCtx,
		pUsg->pFlatCutoff,
		0x1 << STUC_ATTRIB_USE_POS,
		STUC_DOMAIN_NONE
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	stucApplyObjTransform(
		&(StucObject){
			.pData = (StucObjectData *)pUsg->pFlatCutoff,
			.transform = pCutoffObj->transform
		}
	);
	return err;
}

static
void destroyMapOptsArr(const StucAlloc *pAlloc, ObjMapOptsArr *pArr) {
	for (I32 i = 0; i < pArr->count; ++i) {
		if (pArr->pArr[i].arr.pArr) {
			pAlloc->fpFree(pArr->pArr[i].arr.pArr);
		}
	}
	if (pArr->pArr) {
		pAlloc->fpFree(pArr->pArr);
	}
	*pArr = (ObjMapOptsArr){0};
}

static inline
PixtyRange stucClustFaceRange(const void *pMeshRaw, I32 face) {
	const Mesh *pMesh = pMeshRaw;
	PIX_ERR_ASSERT("", face >= 0 && face < pMesh->core.faceCount);
	return (PixtyRange) {
		.start = pMesh->core.pFaces[face],
		.end = pMesh->core.pFaces[face + 1]
	};
}

static
void buildFaceBBoxes(const StucAlloc *pAlloc, StucMap *pMap) {
	const Mesh *pMesh = pMap->pMesh;
	pMap->pFaceBBoxes = pAlloc->fpMalloc(pMesh->core.faceCount * sizeof(PixmshV2Bb));
	for (I32 i = 0; i < pMesh->core.faceCount; ++i) {
		FaceRange face = stucGetFaceRange(&pMesh->core, i);
		pMap->pFaceBBoxes[i] = pixmshV2BbGet(
			pMesh,
			stucGetVertPosAsV2,
			face.range
		);
	}
}

static
StucErr stucMapFileLoadIntern(
	StucCtx *pCtx,
	MapDepEntry *pEntry
) {
	PixErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pEntry->pName, "");
	StucMap *pMap = pCtx->alloc.fpCalloc(1, sizeof(StucMap));
	{
		I32 lenMax = pixioPathMaxGet();
		I32 nameLen = (I32)strnlen(pEntry->pName, lenMax);
		PIX_ERR_RETURN_IFNOT_COND(err, nameLen < lenMax, "");
		pMap->pName = pCtx->alloc.fpMalloc(nameLen + 1);
		memcpy(pMap->pName, pEntry->pName, nameLen + 1);
	}
	StucObjArr objArr = {0};
	StucUsgArr usgArr = {0};
	StucObjArr cutoffArr = {0};
	ObjMapOptsArr mapOptsArr = {0};
	err = stucMapImport(
		pCtx, pEntry->pPath,
		&objArr,
		&mapOptsArr,
		&usgArr,
		&cutoffArr,
		NULL,
		&pMap->indexedAttribs,
		true
	);
	PIX_ERR_THROW_IFNOT(err, "failed to load file from disk", 0);

	I32 targetIdx = 0;
	for (I32 i = 0; i < objArr.count; ++i) {
		Mesh *pMesh = (Mesh *)objArr.pArr[i].pData;
		
		err = stucAttemptToSetMissingActiveDomains(&pMesh->core);
		PIX_ERR_THROW_IFNOT(err, "", 0);

		if (targetIdx < mapOptsArr.count &&
			mapOptsArr.pArr[targetIdx].obj == i
		) {
			StucMapArr *pMapArr = &mapOptsArr.pArr[targetIdx].arr;
			for (I32 j = 0; j < pMapArr->count; ++j) {
				StucMapArrEntry *pArrEntry = pMapArr->pArr + j;
				pArrEntry->map.ptr = pEntry->deps.pArr[pArrEntry->map.idx]->pMap;
				PIX_ERR_ASSERT("", pMapArr->pArr[j].map.ptr);
			}
			StucMesh meshOut = {0};
			AttribIndexedArr outIdxAttribArr = {0};
			err = stucMapToMesh(
				pCtx,
				0,
				pMapArr,
				&pMesh->core,
				&pMap->indexedAttribs,
				&meshOut,
				&outIdxAttribArr,
				//TODO wscale and receivelen are per target rn, so just using idx 0
				pMapArr->pArr[0].wScale,
				pMapArr->pArr[0].receiveLen,
				false, //TODO should this be true? if not remove option from merge func,
				false
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			//TODO edge list returned from stucMapToMesh is broken, this is a temp fix
			if (meshOut.pEdges) {
				pCtx->alloc.fpFree(meshOut.pEdges);
				meshOut.pEdges = NULL;
			}
			err = stucBuildEdgeList(pCtx, &meshOut);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			stucAttribIndexedArrDestroy(pCtx, &pMap->indexedAttribs);
			pMap->indexedAttribs = outIdxAttribArr;
			stucMeshDestroy(pCtx, &pMesh->core);
			pMesh->core = meshOut;
			++targetIdx;
		}
		err = stucAssignActiveAliases(
			pCtx,
			pMesh,
			STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) {
				STUC_ATTRIB_USE_POS,
				STUC_ATTRIB_USE_UV,
				STUC_ATTRIB_USE_NORMAL,
				STUC_ATTRIB_USE_RECEIVE,
				STUC_ATTRIB_USE_IDX
			})),
			STUC_DOMAIN_NONE
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		stucApplyObjTransform(objArr.pArr + i);
	}
	Mesh *pMapMesh = pCtx->alloc.fpCalloc(1, sizeof(Mesh));
	pMapMesh->core.type.type = STUC_OBJECT_DATA_MESH_INTERN;
	err = stucMergeObjArr(pCtx, pMapMesh, &objArr, false);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	UBitField32 spToAppend = STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) {
		STUC_ATTRIB_USE_EDGE_LEN
	}));
	stucAppendSpAttribsToMesh(
		pCtx,
		pMapMesh,
		spToAppend | (usgArr.count ? 0x1 << STUC_ATTRIB_USE_USG : 0x0),
		STUC_ATTRIB_ORIGIN_MAP
	);

	stucSetAttribOrigins(&pMapMesh->core.meshAttribs, STUC_ATTRIB_ORIGIN_MAP);
	stucSetAttribOrigins(&pMapMesh->core.faceAttribs, STUC_ATTRIB_ORIGIN_MAP);
	stucSetAttribOrigins(&pMapMesh->core.cornerAttribs, STUC_ATTRIB_ORIGIN_MAP);
	stucSetAttribOrigins(&pMapMesh->core.edgeAttribs, STUC_ATTRIB_ORIGIN_MAP);
	stucSetAttribOrigins(&pMapMesh->core.vertAttribs, STUC_ATTRIB_ORIGIN_MAP);

	stucSetAttribCopyOpt(
		pCtx,
		&pMapMesh->core,
		STUC_ATTRIB_DONT_COPY,
		~STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) { //all except for
			STUC_ATTRIB_USE_POS,
			STUC_ATTRIB_USE_UV,
			STUC_ATTRIB_USE_NORMAL,
			STUC_ATTRIB_USE_IDX
		}))
	);
	err = stucAssignActiveAliases(
		pCtx,
		pMapMesh,
		STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) {
			STUC_ATTRIB_USE_POS,
			STUC_ATTRIB_USE_UV,
			STUC_ATTRIB_USE_NORMAL,
			STUC_ATTRIB_USE_RECEIVE,
			STUC_ATTRIB_USE_USG,
			STUC_ATTRIB_USE_IDX,
			STUC_ATTRIB_USE_EDGE_LEN,
			STUC_ATTRIB_USE_NONE
		})),
		STUC_DOMAIN_NONE
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	{
		V3_F32 offset = {.d = {.5f, .5f, .0f}};
		pMap->zBounds = (V2_F32){FLT_MAX, -FLT_MAX};
		for (I32 i = 0; i < pMapMesh->core.vertCount; ++i) {
			V3_F32 *pPos = pMapMesh->pPos + i;
			*pPos = _(_(*pPos V3MULS .5f) V3ADD offset);
			pMap->zBounds.d[0] = pPos->d[2] < pMap->zBounds.d[0] ?
				pPos->d[2] : pMap->zBounds.d[0];
			pMap->zBounds.d[1] = pPos->d[2] > pMap->zBounds.d[1] ?
				pPos->d[2] : pMap->zBounds.d[1];
		}
	}

	stucBuildEdgeLenList(pCtx, pMapMesh);

	//TODO some form of heap corruption when many objects
	//test with address sanitizer on CircuitPieces.stuc
	stucObjArrDestroy(pCtx, &objArr);

	//set corner attribs to interpolate by default
	//TODO make this an option in ui, even for non common attribs
	for (I32 i = 0; i < pMapMesh->core.cornerAttribs.count; ++i) {
		pMapMesh->core.cornerAttribs.pArr[i].interpolate = true;
	}

	pMap->pMesh = pMapMesh;

	triCacheBuild(&pCtx->alloc, pMap);
	buildFaceBBoxes(&pCtx->alloc, pMap);

	{
		ClutreMesh clustMesh = {
			.pUserData = pMapMesh,
			.faceCount = pMapMesh->core.faceCount,
			.fpFaceRange = stucClustFaceRange,
			.fpVert = stucClustVert,
			.fpPos = stucClustPos
		};
		err = clutreTreeInit(&pCtx->alloc, &clustMesh, &pMap->clustTree, STUC_CLUTRE_MIN_FACES);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}

	if (usgArr.count) {
		pMap->usgArr.count = usgArr.count;
		pMap->usgArr.pArr = pCtx->alloc.fpCalloc(pMap->usgArr.count, sizeof(Usg));
		for (I32 i = 0; i < pMap->usgArr.count; ++i) {
			Mesh *pUsgMesh = (Mesh *)usgArr.pArr[i].obj.pData;
			err = stucAttemptToSetMissingActiveDomains(&pUsgMesh->core);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			err = stucAssignActiveAliases(
				pCtx,
				pUsgMesh,
				0x1 << STUC_ATTRIB_USE_POS,
				STUC_DOMAIN_NONE
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
			Usg *pUsg = pMap->usgArr.pArr + i;
			pUsg->origin = *(V2_F32 *)&usgArr.pArr[i].obj.transform.d[3];
			pUsg->pMesh = pUsgMesh;
			stucApplyObjTransform(&usgArr.pArr[i].obj);
			if (usgArr.pArr[i].flatCutoff.enabled) {
				//TODO these shouldn't be duplicated for each usg,
				//store cutoffs in a separate arr
				I32 cutoffIdx = usgArr.pArr[i].flatCutoff.idx;
				initFlatCutoff(pCtx, pUsg, cutoffArr.pArr + cutoffIdx);
			}
		}
		Mesh *pSquares = pCtx->alloc.fpCalloc(1, sizeof(Mesh));
		stucAllocUsgSquaresMesh(pCtx, pMap, pSquares);
		stucFillUsgSquaresMesh(pMap, usgArr.pArr, pSquares);
		pMap->usgArr.pSquares = pSquares;
		stucAssignUsgsToVerts(&pCtx->alloc, pMap, usgArr.pArr);
		pMap->usgArr.pMemArr = usgArr.pArr;
	}

	pEntry->pMap = pMap;
	PIX_ERR_CATCH(0, err, stucMapFileUnload(pCtx, pMap);)
	destroyMapOptsArr(&pCtx->alloc, &mapOptsArr);

	return err;
}

StucErr stucMapFileLoadInit(
	StucCtx *pCtx,
	StucMapLoad *pState,
	const char *pFilepath,
	F64 timestamp,
	void *pUserData,
	PixErr (* fpMapGet)(void *, const char *, const char **, double *, StucMap ** const),
	PixErr (* fpMapStore)(
		void *,
		const char *,
		const char *,
		double,
		StucMap *,
		StucMapStatus,
		const PixtyStrArr *
	)
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pState && pFilepath, "");
	*pState = (StucMapLoad) {
		.pCtx = pCtx,
		.pFilepath = pFilepath,
		.timestamp = timestamp,
		.pUserData = pUserData,
		.fpMapGet = fpMapGet,
		.fpMapStore = fpMapStore
	};
	pixuctHTableInit(
		&pCtx->alloc,
		&pState->table,
		64,
		(I32Arr){.pArr = (I32[]){sizeof(MapDepEntry)}, .count = 1},
		NULL,
		NULL,
		true
	);
	return err;
}

StucErr stucMapLoadDestroy(StucMapLoad *pState) {
	if (!pState->table.pTable) {
		return PIX_ERR_SUCCESS;
	}
	PixalcLinAlloc *pLinAlloc = pixuctHTableAllocGet(&pState->table, 0);
	PixalcLinAllocIter iter = {0}; 
	pixalcLinAllocIterInit(pLinAlloc, (PixtyRange){0, INT32_MAX}, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		MapDepEntry *pEntry = pixalcLinAllocGetItem(&iter);
		if (pEntry->deps.pArr) {
			pState->pCtx->alloc.fpFree(pEntry->deps.pArr);
		}
		if (pEntry->pName) {
			pState->pCtx->alloc.fpFree(pEntry->pName);
		}
		if (pEntry->pPath) {
			pState->pCtx->alloc.fpFree(pEntry->pPath);
		}
		*pEntry = (MapDepEntry){0};
	}
	pixuctHTableDestroy(&pState->table);
	*pState = (StucMapLoad){0};
	return PIX_ERR_SUCCESS;
}

StucErr stucMapFileLoadGetDepStatus(StucMapLoad *pState, StucMapStatus *pStatus) {
	StucErr err = PIX_ERR_SUCCESS;
	PixalcLinAlloc *pLinAlloc = pixuctHTableAllocGet(&pState->table, 0);
	PixalcLinAllocIter iter = {0}; 
	pixalcLinAllocIterInit(pLinAlloc, (PixtyRange){0, INT32_MAX}, &iter);
	PIX_ERR_RETURN_IFNOT_COND(err, !pixalcLinAllocIterAtEnd(&iter), "");
	MapDepEntry *pEntry = pixalcLinAllocGetItem(&iter);
	*pStatus = pEntry->status;
	return err;
}

StucErr stucMapFileLoadDeps(StucMapLoad *pState) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_THROW_IFNOT_COND(err, pState, "", 0);
	PIX_ERR_THROW_IFNOT_COND(
		err,
		!pState->depsPassDone,
		"func already called",
		0
	);

	err = stucWalkMapDeps(pState, stucMapFileLoadIntern);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	pState->depsPassDone = true;
	
	PIX_ERR_CATCH(0, err, ;);
	return err;
}

StucErr stucMapFileLoad(StucMapLoad *pState) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_THROW_IFNOT_COND(err, pState, "state not provided", 0);
	PIX_ERR_THROW_IFNOT_COND(
		err,
		pState->depsPassDone,
		"deps not loaded, call stucMapFileLoadDeps first",
		0
	);
	err = stucWalkMapDeps(pState, stucMapFileLoadIntern);

	PIX_ERR_CATCH(0, err, ;);
	return err;
}

static
void triCacheDestroy(const StucAlloc *pAlloc, StucMap *pMap) {
	PIX_ERR_ASSERT("", !((pMap->triCache.pArr != NULL) ^ (pMap->triCache.alloc.valid)));
	if (pMap->triCache.pArr) {
		pAlloc->fpFree(pMap->triCache.pArr);
		pixalcLinAllocDestroy(&pMap->triCache.alloc);
		pMap->triCache = (TriCache) {0};
	}
}

StucErr stucMapFileUnload(StucCtx *pCtx, StucMap *pMap) {
	clutreTreeDestroy(&pMap->clustTree);
	if (pMap->pMesh) {
		stucMeshDestroy(pCtx, &pMap->pMesh->core);
		pCtx->alloc.fpFree((Mesh *)pMap->pMesh);
	}
	stucAttribIndexedArrDestroy(pCtx, &pMap->indexedAttribs);
	triCacheDestroy(&pCtx->alloc, pMap);
	if (pMap->pFaceBBoxes) {
		pCtx->alloc.fpFree(pMap->pFaceBBoxes);
	}
	if (pMap->usgArr.pSquares) {
		pCtx->alloc.fpFree((Mesh *)pMap->usgArr.pSquares);
	}
	if (pMap->pName) {
		pCtx->alloc.fpFree(pMap->pName);
	}
	pCtx->alloc.fpFree(pMap);
	return PIX_ERR_SUCCESS;
}

StucErr stucMapFileMeshGet(
	StucCtx *pCtx,
	StucMap *pMap,
	const StucMesh **ppMesh,
	StucAttribIndexedArr **ppIdxAttribs
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(
		err,
		pCtx && pMap && (ppMesh || ppIdxAttribs),
		"invalid args"
	);
	if (ppMesh) {
		*ppMesh = &pMap->pMesh->core;
	}
	if (ppIdxAttribs) {
		*ppIdxAttribs = &pMap->indexedAttribs;
	}
	return err;
}

StucErr stucMapNameGet(StucCtx *pCtx, StucMap *pMap, const char **ppName) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pMap && ppName, "");
	*ppName = pMap->pName;
	return err;
}

static
void initBlendOpt(
	StucCtx *pCtx,
	StucBlendOpt *pEntry,
	const StucAttrib *pAttrib,
	I32 attribIdx
) {
	const StucTypeDefault *pDefault = 
		stucGetTypeDefaultConfig(&pCtx->typeDefaults, pAttrib->core.type);
	pEntry->blendConfig = pDefault->blendConfig;
	pEntry->attrib = attribIdx;
}

static
StucErr getCommonAttribs(
	StucCtx *pCtx,
	const StucMesh *pMapMesh,
	const AttribArray *pMapAttribs,
	const StucMesh *pMesh,
	const AttribArray *pMeshAttribs,
	StucBlendOptArr *pOptArr
) {
	StucErr err = PIX_ERR_SUCCESS;
	//TODO ignore special attribs like StucTangent or StucTSign
	for (I32 i = 0; i < pMeshAttribs->count; ++i) {
		Attrib *pAttrib = pMeshAttribs->pArr + i;
		if (pAttrib->core.use == STUC_ATTRIB_USE_POS) {
			continue;
		}
		const Attrib *pMapAttrib = NULL;
		err = stucGetMatchingAttribConst(
			pCtx,
			pMapMesh, pMapAttribs,
			pMesh, pAttrib,
			true,
			true,
			&pMapAttrib
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
		if (!pMapAttrib) {
			continue;
		}
		PIX_ERR_THROW_IFNOT_COND(err, pOptArr->count < pOptArr->size, "", 0);
		initBlendOpt(pCtx, pOptArr->pArr + pOptArr->count, pAttrib, i);
		++pOptArr->count;
	}
	PIX_ERR_CATCH(0, err,
		*pOptArr = (StucBlendOptArr){0};
	);
	return err;
}

//TODO handle edge case, where attribute share the same name,
//but have incompatible types. Such as a F32 and a string.
StucErr stucQueryCommonAttribs(
	StucCtx *pCtx,
	const StucMap *pMap,
	const StucMesh *pMesh,
	StucBlendOptArr *pOptArr
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pMap && pMesh && pOptArr, "");
	const StucMesh *pMapMesh = &pMap->pMesh->core;
	StucMesh meshWrap = *pMesh;
	err = stucAttemptToSetMissingActiveDomains(&meshWrap);
	PIX_ERR_RETURN_IFNOT(err, "");
	for (I32 domain = STUC_DOMAIN_FACE; domain <= STUC_DOMAIN_MESH; ++domain) {
		err = getCommonAttribs(
			pCtx,
			pMapMesh,
			stucGetAttribArrFromDomainConst(pMapMesh, domain),
			&meshWrap,
			stucGetAttribArrFromDomainConst(&meshWrap, domain),
			pOptArr + domain
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	PIX_ERR_CATCH(0, err, ;);
	return err;
}

/*
StucErr stucDestroyBlendOptArr(
	StucCtx *pCtx,
	StucBlendOptArr *pOptArr
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pOptArr, "");
	for (I32 domain = STUC_DOMAIN_FACE; domain <= STUC_DOMAIN_MESH; ++domain) {
		StucBlendOptArr *pArr = pOptArr + domain;
		if (pArr) {
			pCtx->alloc.fpFree(pArr->pArr);
			pArr->pArr = NULL;
		}
		pArr->count = pArr->size = 0;
	}
	return err;
}
*/

static
void collapseInPieceArr(
	const PixalcFPtrs *pAlloc,
	BufMeshArr *pDest,
	const MapMeshForIslandJobArgs *pJobArgs,
	I32 jobCount,
	bool clip
) {
	I32 count = pDest->count;
	for (I32 i = 1; i < jobCount; ++i) {
		count += clip ? pJobArgs[i].bufMeshClipArr.count : pJobArgs[i].bufMeshArr.count;
	}
	if (!count) {
		return;
	}
	PIXALC_DYN_ARR_RESIZE(BufMesh, pAlloc, pDest, count);
	for (I32 i = 1; i < jobCount; ++i) {
		const BufMeshArr *pSrc = clip ?
			&pJobArgs[i].bufMeshClipArr : &pJobArgs[i].bufMeshArr;
		if (!pSrc->count) {
			continue;
		}
		PIX_ERR_ASSERT("", pSrc->pArr);
		memcpy(pDest->pArr + pDest->count, pSrc->pArr, pSrc->count * sizeof(BufMesh));
		pDest->count += pSrc->count;
		pAlloc->fpFree(pSrc->pArr);
	}
	PIX_ERR_ASSERT("", pDest->count <= pDest->size);
}

static
I32 mapMeshForIslandJobsGetRange(
	const StucCtx *pCtx,
	const void *pShared,
	void *pInitInfo
) {
	return ((MapToMeshBasic *)pShared)->pInIslands->count;
}

static
StucErr mapToMeshInternal(
	StucCtx *pCtx,
	I32 threadId,
	const StucInIslandArr *pInIslands,
	const StucMap *pMap,
	Mesh *pMeshIn,
	StucMesh *pOutMesh,
	I8 maskIdx,
	const StucBlendOptArr *pOptArr,
	InFaceTable *pInFaceTable,
	F32 wScale,
	F32 receiveLen
) {
	StucErr err = PIX_ERR_SUCCESS;
	if (stucCheckIfNoFacesHaveMaskIdx(pMeshIn, maskIdx)) {
		return err;
	}
	MapToMeshBasic basic = {
		.pCtx = pCtx,
		.pMap = pMap,
		.pInMesh = pMeshIn,
		.pInIslands = pInIslands,
		.pOptArr = pOptArr,
		.wScale = wScale,
		.receiveLen = receiveLen,
		.maskIdx = maskIdx,
		.pInFaceTable = pInFaceTable,
	};
	//printf("A\n");
	if (pInFaceTable) {
		pixalcLinAllocInit(
			&pCtx->alloc,
			&pInFaceTable->alloc,
			sizeof(I32),
			pMeshIn->core.faceCount,
			true
		);
	}
	I32 clustForIslandJobCount = 0;
	MapMeshForIslandJobArgs clustForIslandJobArgs[PIXTH_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pCtx,
		&basic,
		&clustForIslandJobCount, &clustForIslandJobArgs, sizeof(MapMeshForIslandJobArgs),
		NULL,
		mapMeshForIslandJobsGetRange, NULL
	);
	err = stucDoJobInParallel(
		pCtx,
		threadId,
		clustForIslandJobCount, clustForIslandJobArgs, sizeof(MapMeshForIslandJobArgs),
		stucMapMeshForIsland
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	bool empty = true;
	for (I32 i = 0; i < clustForIslandJobCount; ++i) {
		if (!clustForIslandJobArgs[i].empty) {
			empty = false;
			break;
		}
	}
	if (empty) {
		return err;
	}
	BufMeshArr *pBufMeshArr = &clustForIslandJobArgs[0].bufMeshArr;
	BufMeshArr *pBufMeshClipArr = &clustForIslandJobArgs[0].bufMeshClipArr;
	collapseInPieceArr(
		&pCtx->alloc,
		pBufMeshArr,
		clustForIslandJobArgs,
		clustForIslandJobCount,
		false
	);
	collapseInPieceArr(
		&pCtx->alloc,
		pBufMeshClipArr,
		clustForIslandJobArgs,
		clustForIslandJobCount,
		true
	);
	//printf("B\n");

	PixuctHTable mergeTable = {0};
	stucVertMergeTableInit(&basic, pBufMeshArr, pBufMeshClipArr, &mergeTable);
	stucMergeVerts(&basic, pBufMeshArr, false, &mergeTable);
	stucMergeVerts(&basic, pBufMeshClipArr, true, &mergeTable);
	//printf("E\n");

	//TODO implement vert snapping
	//(also func doesn't currently account for InPieceArr->pNext)
	/*
	I32 snappedVerts = 0;
	err = stucSnapIntersectVerts(
		&basic,
		threadId,
		pInPieces, pInPiecesClip,
		&mergeTable,
		&snappedVerts
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	//printf("F\n");
	*/

	stucInitOutMesh(&basic, &mergeTable/*, snappedVerts*/);
	stucAddVertsToOutMesh(&basic, &mergeTable, 0);
	stucAddVertsToOutMesh(&basic, &mergeTable, 1);//intersect verts
	BufOutRangeTable bufOutTable = {.size = pBufMeshArr->count + pBufMeshClipArr->count};
	bufOutTable.pArr = pCtx->alloc.fpMalloc(bufOutTable.size * sizeof(BufOutRange));
	OutBufIdxArr outBufIdxArr = {0};
	stucAddFacesAndCornersToOutMesh(
		&basic,
		pBufMeshArr,
		&mergeTable,
		&outBufIdxArr,
		&bufOutTable,
		false
	);
	stucAddFacesAndCornersToOutMesh(
		&basic,
		pBufMeshClipArr,
		&mergeTable,
		&outBufIdxArr,
		&bufOutTable,
		true
	);
	if (!basic.outMesh.core.faceCount) {
		goto cleanUp;
	}
	stucMeshSetLastFace(pCtx, &basic.outMesh);
	//printf("G\n");

	err = stucBuildTangentsForInPieces(
		pCtx,
		threadId,
		pMeshIn,
		pBufMeshArr, pBufMeshClipArr,
		&mergeTable
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	//printf("H\n");
	
	err = stucXFormAndInterpVerts(
		&basic,
		threadId,
		pBufMeshArr,
		pBufMeshClipArr,
		&mergeTable,
		0
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	//intersect verts
	err = stucXFormAndInterpVerts(
		&basic,
		threadId,
		pBufMeshArr,
		pBufMeshClipArr,
		&mergeTable,
		1
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = stucInterpAttribs(
		&basic,
		threadId,
		pBufMeshArr, pBufMeshClipArr,
		&mergeTable,
		&bufOutTable,
		&outBufIdxArr,
		STUC_DOMAIN_FACE, stucInterpFaceAttribs
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	//vert merge lin-idx is replaced with out-vert idx in corner-interp job,
	// so faces must be interpolated before corners
	err = stucInterpAttribs(
		&basic,
		threadId,
		pBufMeshArr, pBufMeshClipArr,
		&mergeTable,
		&bufOutTable,
		&outBufIdxArr,
		STUC_DOMAIN_CORNER, stucInterpCornerAttribs
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	//printf("I\n");

	stucReallocMeshToFit(pCtx, &basic.outMesh);
	*pOutMesh = basic.outMesh.core;
	//printf("J\n");
cleanUp:
	if (outBufIdxArr.pArr) {
		pCtx->alloc.fpFree(outBufIdxArr.pArr);
	}
	if (bufOutTable.pArr) {
		pCtx->alloc.fpFree(bufOutTable.pArr);
	}
	pixuctHTableDestroy(&mergeTable);
	if (pBufMeshArr->pArr) {
		stucBufMeshArrDestroy(pCtx, pBufMeshArr);
	}
	if (pBufMeshClipArr->pArr) {
		stucBufMeshArrDestroy(pCtx, pBufMeshClipArr);
	}
	//printf("K\n");
	PIX_ERR_CATCH(0, err, ;);
	return err;
}

static
void addEntryToInFaceTable(
	const StucAlloc *pAlloc,
	UsgInFace **ppHashTable,
	StucMap *pMap,
	InFaceArr *pInFaceTable,
	I32 squareIdx,
	I32 inFaceIdx
) {
	U32 sum = pInFaceTable[squareIdx].usg + pInFaceTable[squareIdx].pArr[inFaceIdx];
	I32 hash = stucFnvHash((U8 *)&sum, sizeof(sum), pMap->usgArr.tableSize);
	UsgInFace *pEntry = *ppHashTable + hash;
	if (!pEntry->pEntry) {
		pEntry->pEntry = pInFaceTable + squareIdx;
		pEntry->face = pInFaceTable[squareIdx].pArr[inFaceIdx];
		return;
	}
	do {
		if (!pEntry->pNext) {
			pEntry = pEntry->pNext = pAlloc->fpCalloc(1, sizeof(UsgInFace));
			pEntry->pEntry = pInFaceTable + squareIdx;
			pEntry->face = pInFaceTable[squareIdx].pArr[inFaceIdx];
			break;
		}
		pEntry = pEntry->pNext;
	} while(true);
}

static
void InFaceTableToHashTable(
	const StucAlloc *pAlloc,
	StucMap *pMap,
	I32 count,
	InFaceArr *pInFaceTable
) {
	UsgInFace **ppHashTable = &pMap->usgArr.pInFaceTable;
	pMap->usgArr.tableSize = count * 2;
	*ppHashTable = pAlloc->fpCalloc(pMap->usgArr.tableSize, sizeof(UsgInFace));
	for (I32 i = 0; i < count; ++i) {
		for (I32 j = 0; j < pInFaceTable[i].count; ++j) {
			addEntryToInFaceTable(pAlloc, ppHashTable, pMap, pInFaceTable, i, j);
		}
	}
}

static
StucErr getOriginIndexedAttrib(
	StucCtx *pCtx,
	Attrib *pAttrib,
	I32 attribIdx,
	const StucMapArr *pMapArr,
	I32 mapIdx,
	const AttribIndexed *pMapIndexedAttrib,
	const AttribIndexed *pInIndexedAttrib,
	const AttribIndexed **ppMatsToAdd,
	StucDomain domain
) {
	StucErr err = PIX_ERR_SUCCESS;
	switch (pAttrib->origin) {
		case STUC_ATTRIB_ORIGIN_MAP:
			*ppMatsToAdd = pMapIndexedAttrib;
			break;
		case STUC_ATTRIB_ORIGIN_MESH_IN:
			*ppMatsToAdd = pInIndexedAttrib;
			break;
		case STUC_ATTRIB_ORIGIN_COMMON: {
			const BlendOptArr *pOptArr = pMapArr->pArr[mapIdx].blendOptArr;
			const BlendOpt *pOpts = stucGetBlendOpt(pOptArr, attribIdx, domain);
			BlendConfig config = {0};
			if (pOpts) {
				config = pOpts->blendConfig;
			}
			else {
				const StucTypeDefault *pDefaultConfig =
					stucGetTypeDefaultConfig(&pCtx->typeDefaults, STUC_ATTRIB_STRING);
				config = pDefaultConfig->blendConfig;
			}
			*ppMatsToAdd = config.order ? pInIndexedAttrib : pMapIndexedAttrib;
			break;
		}
		default:
			PIX_ERR_ASSERT("invalid attrib origin for this function", false);
	}
	return err;
}

static
StucErr iterFacesAndCorrectIdxAttrib(
	StucCtx *pCtx,
	Attrib *pAttrib,
	Mesh *pMesh,
	AttribIndexed *pOutIndexedAttrib,
	const AttribIndexed *pOriginIndexedAttrib,
	StucDomain domain
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pAttrib->core.type == STUC_ATTRIB_I8, "");

	typedef struct TableEntry {
		I8 idx;
		bool hasRef;
	} TableEntry;
	TableEntry *pTable =
		pCtx->alloc.fpCalloc(pOriginIndexedAttrib->count, sizeof(TableEntry));

	I8 *pIdx = pAttrib->core.pData;
	I32 domainCount = stucDomainCountGetIntern(&pMesh->core, domain);
	for (I32 i = 0; i < domainCount; ++i) {
		I32 idx = pIdx[i];
		PIX_ERR_THROW_IFNOT_COND(err, idx >= 0 && idx < pOriginIndexedAttrib->count, "", 0);
		TableEntry *pEntry = pTable + idx;
		if (!pEntry->hasRef) {
			pEntry->hasRef = true;
			I32 newIdx = stucGetIdxInIndexedAttrib(
				pOutIndexedAttrib,
				pOriginIndexedAttrib,
				idx
			);
			if (newIdx >= 0) {
				pEntry->idx = (I8)newIdx;
			}
			else {
				pEntry->idx = (I8)pOutIndexedAttrib->count;
				stucAppendToIndexedAttrib(
					pCtx,
					pOutIndexedAttrib,
					&pOriginIndexedAttrib->core,
					idx
				);
			}
		}
		pIdx[i] = pEntry->idx;
	}
	PIX_ERR_CATCH(0, err, ;);
	pCtx->alloc.fpFree(pTable);
	return err;
}

static
StucErr getIndexedAttribInMaps(
	StucCtx *pCtx,
	const Mesh *pMesh,
	const StucMapArr *pMapArr,
	const Attrib *pAttrib,
	StucDomain domain,
	const AttribIndexed ***pppOut
) {
	StucErr err = PIX_ERR_SUCCESS;
	const AttribIndexed **ppAttribs =
		pCtx->alloc.fpCalloc(pMapArr->count, sizeof(void *));
	bool found = false;
	bool same = true;
	StucMap *pMapCache = NULL;
	for (I32 i = 0; i < pMapArr->count; ++i) {
		const StucMap *pMap = pMapArr->pArr[i].map.ptr;
		const char *pName = NULL;
		switch (pAttrib->origin) {
			case STUC_ATTRIB_ORIGIN_MAP:
				pName = pAttrib->core.name;
				break;
			case STUC_ATTRIB_ORIGIN_COMMON: {
				const AttribArray *pMapAttribArr =
					stucGetAttribArrFromDomainConst(&pMap->pMesh->core, domain);
				const Attrib *pMapAttrib = NULL;
				err = stucGetMatchingAttribConst(
					pCtx,
					&pMap->pMesh->core, pMapAttribArr,
					&pMesh->core, pAttrib,
					true,
					false,
					&pMapAttrib
				);
				PIX_ERR_THROW_IFNOT_COND(err, pMapAttrib, "", 0);
				pName = pMapAttrib->core.name;
				break;
			}
			default:
				PIX_ERR_ASSERT("invalid attrib origin", false);
		}
		const AttribIndexed *pIndexedAttrib =
			stucGetAttribIndexedInternConst(&pMap->indexedAttribs, pName);
		if (pIndexedAttrib) {
			found = true;
			ppAttribs[i] = pIndexedAttrib;
			if (!pMapCache) {
				pMapCache = pMapArr->pArr[i].map.ptr;
			}
			else if (same) {
				same = pMapCache == pMapArr->pArr[i].map.ptr;
			}
		}
	}
	if (found) {
		*pppOut = ppAttribs;
		return err;
	}
	PIX_ERR_CATCH(0, err, ;);
	pCtx->alloc.fpFree(ppAttribs);
	*pppOut = NULL;
	return err;
}

static
StucErr correctIdxIndices(
	StucCtx *pCtx,
	const char *pName,
	Mesh *pMeshArr,
	const StucMapArr *pMapArr,
	const AttribIndexedArr *pInIndexedAttribs,
	const AttribIndexed **ppMapAttribs,
	AttribIndexed *pOutIndexedAttrib,
	StucDomain domain
) {
	StucErr err = PIX_ERR_SUCCESS;
	const AttribIndexed *pInIndexedAttrib =
		stucGetAttribIndexedInternConst(pInIndexedAttribs, pName);
	for (I32 i = 0; i < pMapArr->count; ++i) {
		AttribArray *pAttribArr = stucGetAttribArrFromDomain(&pMeshArr[i].core, domain);
		I32 attribIdx = 0;
		Attrib *pAttrib =
			stucGetAttribIntern(pName, pAttribArr, false, NULL, NULL, &attribIdx);
		if (!ppMapAttribs[i] || !pAttrib) {
			continue;
		}
		Mesh *pMesh = pMeshArr + i;
		const AttribIndexed *pOriginIndexedAttrib = NULL;
		err = getOriginIndexedAttrib(
			pCtx,
			pAttrib,
			attribIdx,
			pMapArr,
			i,
			ppMapAttribs[i],
			pInIndexedAttrib,
			&pOriginIndexedAttrib,
			domain
		);
		PIX_ERR_RETURN_IFNOT(err, "");
		PIX_ERR_RETURN_IFNOT_COND(
			err,
			pOriginIndexedAttrib,
			"no indexed attrib found for idx attrib in mesh"
		);
		err = iterFacesAndCorrectIdxAttrib(
			pCtx,
			pAttrib,
			pMesh,
			pOutIndexedAttrib,
			pOriginIndexedAttrib,
			domain
		);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	return err;
}

static
StucErr appendOutIndexedAttrib(
	StucCtx *pCtx,
	const StucMapArr *pMapArr,
	const AttribIndexed **ppMapAttribs,
	const Attrib *pAttrib,
	const AttribIndexedArr *pInIndexedAttribs,
	AttribIndexedArr *pOutIndexedAttribs,
	AttribIndexed **ppIndexedAttrib,
	bool keepExisting
) {
	StucErr err = PIX_ERR_SUCCESS;
	const AttribIndexed *pRefAttrib = NULL;
	switch (pAttrib->origin) {
		case STUC_ATTRIB_ORIGIN_MAP:
			for (I32 l = 0; l < pMapArr->count; ++l) {
				if (ppMapAttribs[l]) {
					pRefAttrib = ppMapAttribs[l];
					break;
				}
			}
			PIX_ERR_ASSERT("", pRefAttrib);
			*ppIndexedAttrib = stucAppendIndexedAttrib(
				pCtx,
				pOutIndexedAttribs,
				pRefAttrib->core.name,
				0, //dont allocate pData
				pRefAttrib->core.type,
				pRefAttrib->core.use
			);
			break;
		case STUC_ATTRIB_ORIGIN_COMMON:
			pRefAttrib = stucGetAttribIndexedInternConst(
				pInIndexedAttribs,
				pAttrib->core.name
			);
			if (keepExisting) {
				err = stucAppendAndCopyIdxAttrib(
					pCtx,
					pRefAttrib,
					pOutIndexedAttribs
				);
				PIX_ERR_RETURN_IFNOT(err, "");
				*ppIndexedAttrib = pOutIndexedAttribs->pArr + pOutIndexedAttribs->count - 1;
			}
			else {
				*ppIndexedAttrib = stucAppendIndexedAttrib(
					pCtx,
					pOutIndexedAttribs,
					pRefAttrib->core.name,
					0, //dont allocate pData
					pRefAttrib->core.type,
					pRefAttrib->core.use
				);
			}
			break;
		default:
			PIX_ERR_ASSERT("invalid attrib origin", false);
	}
	return err;
}


static
StucErr mergeIndexedAttribs(
	StucCtx *pCtx,
	Mesh *pMeshArr,
	const StucMapArr *pMapArr,
	const AttribIndexedArr *pInIndexedAttribs,
	AttribIndexedArr *pOutIndexedAttribs,
	bool keepExisting
) {
	StucErr err = PIX_ERR_SUCCESS;
	const StucAlloc *pAlloc = &pCtx->alloc;
	pOutIndexedAttribs->size = pInIndexedAttribs->count;
	pOutIndexedAttribs->pArr =
		pAlloc->fpCalloc(pOutIndexedAttribs->size, sizeof(AttribIndexed));
	/*
	if (keepExisting) {
		for (I32 i = 0; i < pInIndexedAttribs->count; ++i) {
			err = stucAppendAndCopyIdxAttrib(
				pCtx,
				pInIndexedAttribs->pArr + i,
				pOutIndexedAttribs
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}
	}
	*/
	for (I32 i = 0; i < pMapArr->count; ++i) {
		Mesh *pMesh = pMeshArr + i;
		for (I32 j = STUC_DOMAIN_FACE; j <= STUC_DOMAIN_VERT; ++j) {
			AttribArray *pAttribArr = stucGetAttribArrFromDomain(&pMesh->core, j);
			for (I32 k = 0; k < pAttribArr->count; ++k) {
				Attrib *pAttrib = pAttribArr->pArr + k;
				if (pAttrib->core.use != STUC_ATTRIB_USE_IDX ||
					pAttrib->origin == STUC_ATTRIB_ORIGIN_MESH_OUT) {
					continue;
				}
				AttribIndexed *pIndexedAttrib = NULL;
				if (pAttrib->origin == STUC_ATTRIB_ORIGIN_MESH_IN) {
					if (!keepExisting) {
						err = stucAppendAndCopyIdxAttribFromName(
							pCtx,
							pAttrib->core.name,
							pInIndexedAttribs,
							pOutIndexedAttribs
						);
						PIX_ERR_THROW_IFNOT(err, "", 0);
					}
					continue;
				}
				stucGetAttribIndexed(pAttrib->core.name, pOutIndexedAttribs, &pIndexedAttrib);
				if (!pIndexedAttrib) {
					const AttribIndexed **ppMapAttribs = NULL;
					err = getIndexedAttribInMaps(
						pCtx,
						pMesh,
						pMapArr,
						pAttrib,
						j,
						&ppMapAttribs
					);
					PIX_ERR_THROW_IFNOT_COND(err, ppMapAttribs, "", 1);
					err = appendOutIndexedAttrib(
						pCtx,
						pMapArr,
						ppMapAttribs,
						pAttrib,
						pInIndexedAttribs,
						pOutIndexedAttribs,
						&pIndexedAttrib,
						keepExisting
					);
					PIX_ERR_THROW_IFNOT(err, "", 1);
					err = correctIdxIndices(
						pCtx,
						pAttrib->core.name,
						pMeshArr,
						pMapArr,
						pInIndexedAttribs,
						ppMapAttribs,
						pIndexedAttrib,
						j
					);
					PIX_ERR_THROW_IFNOT(err, "", 1);
					PIX_ERR_CATCH(1, err, ;);
					if (ppMapAttribs){
						pAlloc->fpFree(ppMapAttribs);
					}
					PIX_ERR_THROW_IFNOT(err, "", 0);
				}
			}
		}
	}
	PIX_ERR_CATCH(0, err, ;)
	return err;
}

typedef struct StucMapToMeshArgs {
	StucCtx *pCtx;
	StucMapArr *pMapArr;
	StucMesh *pMeshIn;
	StucAttribIndexedArr *pInIndexedAttribs;
	StucMesh *pMeshOut;
	StucAttribIndexedArr *pOutIndexedAttribs;
	F32 wScale;
	F32 receiveLen;
	bool triangulate;
} StucMapToMeshArgs;

static
StucErr mapToMeshFromJob(void *pArgsVoid, I32 threadId) {
	StucMapToMeshArgs *pArgs = pArgsVoid;
	StucErr err = stucMapToMesh(
		pArgs->pCtx,
		threadId,
		pArgs->pMapArr,
		pArgs->pMeshIn,
		pArgs->pInIndexedAttribs,
		pArgs->pMeshOut,
		pArgs->pOutIndexedAttribs,
		pArgs->wScale,
		pArgs->receiveLen,
		false,
		pArgs->triangulate
	);
	pArgs->pCtx->alloc.fpFree(pArgs);
	return err;
}

StucErr stucQueueMapToMesh(
	StucCtx *pCtx,
	PixthJob *pJobHandle,
	StucMapArr *pMapArr,
	StucMesh *pMeshIn,
	StucAttribIndexedArr *pInIndexedAttribs,
	StucMesh *pMeshOut,
	StucAttribIndexedArr *pOutIndexedAttribs,
	F32 wScale,
	F32 receiveLen,
	bool triangulate
) {
	StucMapToMeshArgs *pArgs = pCtx->alloc.fpMalloc(sizeof(StucMapToMeshArgs));
	*pArgs = (StucMapToMeshArgs) {
		.pCtx = pCtx,
		.pMapArr = pMapArr,
		.pMeshIn = pMeshIn,
		.pInIndexedAttribs = pInIndexedAttribs,
		.pMeshOut = pMeshOut,
		.pOutIndexedAttribs = pOutIndexedAttribs,
		.wScale = wScale,
		.receiveLen = receiveLen,
		.triangulate = triangulate
	};
	pixthJobsInit(pJobHandle, 1, mapToMeshFromJob, (void **)&pArgs);
	pCtx->threadPool.pJobStackPushJobs(
		&pCtx->threadPool.handle,
		0,
		1,
		pJobHandle
	);
	return PIX_ERR_SUCCESS;
}

static
StucErr logInMesh(StucCtx *pCtx, Mesh *pMesh) {
	StucErr err = PIX_ERR_SUCCESS;
	for (I32 i = 0; i < pMesh->core.cornerCount; ++i) {
		CarkLog log = {0};
		err = carkOutLogStart(&(pCtx->cark).ctx, 0, (pCtx->cark).stageHandleArr[STUC_STAGE_ISLAND_SPLIT], 1, i, &(log));
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogComp(&log, 0, pMesh->core.pCorners + i);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogEnd(&log);
		PIX_ERR_RETURN_IFNOT(err, "");

		err = CARK_LOG_START(pCtx->cark, 0, STUC_STAGE_ISLAND_SPLIT, 3, i, log);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogComp(&log, 0, pMesh->pUvs[i].d + 0);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogComp(&log, 1, pMesh->pUvs[i].d + 1);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogEnd(&log);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	for (I32 i = 0; i < pMesh->core.vertCount; ++i) {
		CarkLog log = {0};
		err = CARK_LOG_START(pCtx->cark, 0, STUC_STAGE_ISLAND_SPLIT, 2, i, log);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogComp(&log, 0, pMesh->pPos[i].d + 0);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogComp(&log, 1, pMesh->pPos[i].d + 1);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogComp(&log, 2, pMesh->pPos[i].d + 2);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogEnd(&log);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	return err;
}

static
StucErr mapMapArrToMesh(
	StucCtx *pCtx,
	I32 threadId,
	const StucMapArr *pMapArr,
	Mesh *pMeshIn,
	const StucAttribIndexedArr *pInIndexedAttribs,
	StucMesh *pMeshOut,
	StucAttribIndexedArr *pOutIndexedAttribs,
	F32 wScale,
	F32 receiveLen,
	bool keepExistingIdxAttribs
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr inIslands = {0};
	Mesh *pOutBufArr = NULL;
	StucObjArr outObjWrapArr = {0};
	err = logInMesh(pCtx, pMeshIn);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = StucSplitMeshToIslands(pCtx, pMeshIn, &inIslands);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = CARK_STAGE_END(pCtx->cark, STUC_STAGE_ISLAND_SPLIT);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	pOutBufArr = pCtx->alloc.fpCalloc(pMapArr->count, sizeof(Mesh));
	outObjWrapArr.size = outObjWrapArr.count = pMapArr->count;
	outObjWrapArr.pArr = pCtx->alloc.fpCalloc(outObjWrapArr.size, sizeof(StucObject));
	for (I32 i = 0; i < pMapArr->count; ++i) {
		outObjWrapArr.pArr[i].pData = (StucObjectData *)&pOutBufArr[i];
		const StucMap *pMap = pMapArr->pArr[i].map.ptr;
		I8 matIdx = pMapArr->pArr[i].matIdx;
		InFaceTable inFaceTable = {0};
		//TODO uncomment when usg are reimplemented
		/*
		if (pMap->usgArr.count) {
			//set preserve to null to prevent usg squares from being split
			if (pMeshIn->pEdgePreserve || pMeshIn->pVertPreserve) {
				pMeshIn->pEdgePreserve = NULL;
				pMeshIn->pVertPreserve = NULL;
			}
			StucMap squares = { .pMesh = pMap->usgArr.pSquares };
			buildFaceBBoxes(&pCtx->alloc, &squares);
			ClutreMesh clustMesh = {
				.pUserData = squares.pMesh,
				.faceCount = squares.pMesh->core.faceCount,
				.fpFaceRange = stucClustFaceRange,
				.fpVert = stucClustVert,
				.fpPos = stucClustPos
			};
			err = clutreTreeInit(&pCtx->alloc, &clustMesh, &squares.clustTree, STUC_CLUTRE_MIN_FACES);
			PIX_ERR_THROW_IFNOT(err, "", 0);

			StucMesh squaresOut = { 0 };
			err = mapToMeshInternal(
				pCtx,
				threadId,
				&inIslands,
				&squares,
				pMeshIn,
				&squaresOut,
				matIdx,
				pMapArr->pArr[i].blendOptArr,
				&inFaceTable,
				1.0f,
				-1.0f
			);
			PIX_ERR_THROW_IFNOT(err, "map to mesh usg failed", 1);
			err = stucSampleInAttribsAtUsgOrigins(
				pCtx,
				pMap,
				pMeshIn,
				&squaresOut,
				inFaceTable.pArr
			);
			PIX_ERR_THROW_IFNOT(err, "", 1);
			InFaceTableToHashTable(&pCtx->alloc, pMap, squaresOut.faceCount, inFaceTable.pArr);
			stucMeshDestroy(pCtx, &squaresOut);
			stucAssignActiveAliases(
				pCtx,
				(Mesh *)pMeshIn,
				STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) { //reassign preserve if present
					STUC_ATTRIB_USE_PRESERVE_EDGE,
					STUC_ATTRIB_USE_PRESERVE_VERT
				})),
				STUC_DOMAIN_NONE
			);
		}
		*/
		err = mapToMeshInternal(
			pCtx,
			threadId,
			&inIslands,
			pMap,
			pMeshIn,
			&pOutBufArr[i].core,
			matIdx,
			pMapArr->pArr[i].blendOptArr,
			NULL,
			wScale,
			receiveLen
		);
		PIX_ERR_THROW_IFNOT(err, "map to mesh failed", 1);
		PIX_ERR_CATCH(1, err, ;);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	pMeshOut->type.type = STUC_OBJECT_DATA_MESH;
	Mesh meshOutWrap = {.core = *pMeshOut};
	err = mergeIndexedAttribs(
		pCtx,
		pOutBufArr,
		pMapArr,
		pInIndexedAttribs,
		pOutIndexedAttribs,
		keepExistingIdxAttribs
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = stucMergeObjArr(pCtx, &meshOutWrap, &outObjWrapArr, false);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	*pMeshOut = meshOutWrap.core;
	PIX_ERR_CATCH(0, err,
		stucMeshDestroy(pCtx, pMeshOut);
	);
	stucInIslandsDestroy(pCtx, &inIslands);
	//meshes are stored on an arr buf, which we can't call stucObjArrDestroy
	if (pOutBufArr) {
		for (I32 i = 0; i < pMapArr->count; ++i) {
			stucMeshDestroy(pCtx, &pOutBufArr[i].core);
		}
		pCtx->alloc.fpFree(pOutBufArr);
	}
	if (outObjWrapArr.pArr) {
		pCtx->alloc.fpFree(outObjWrapArr.pArr);
	}
	return err;
}

static
StucErr appendSpAttribsToInMesh(
	const StucCtx *pCtx,
	Mesh *pWrap,
	const StucMesh *pMeshIn,
	UBitField32 flags
) {
	StucErr err = PIX_ERR_SUCCESS;
	UBitField32 has = 0;
	stucQuerySpAttribs(pCtx, pMeshIn, flags, &has);
	if (has) {
		PIX_ERR_RETURN(err, "in-mesh contains attribs it shouldn't");
	}
	Mesh meshInCpy = {.core = *pMeshIn};
	const Mesh *const pMeshInCpyPtr = &meshInCpy;
	stucAllocAttribsFromMeshArr(
		pCtx,
		pWrap,
		1, &pMeshInCpyPtr,
		0,
		false,
		false, //dont allocate data
		true, //alias pMeshIn's data instead
		false
	);
	stucAppendSpAttribsToMesh(
		pCtx,
		pWrap,
		flags, 
		STUC_ATTRIB_ORIGIN_MESH_IN
	);
	return err;
}

static
void destroyAppendedSpAttribs(StucCtx *pCtx, StucMesh *pMesh, UBitField32 flags) {
	for (I32 i = 1; i < STUC_ATTRIB_USE_SP_ENUM_COUNT; ++i) {
		if (!(flags >> i & 0x1)) {
			continue;
		}
		Attrib *pAttrib = stucGetActiveAttrib(pCtx, pMesh, i);
		if (pAttrib) {
			if (pAttrib->core.pData) {
				pCtx->alloc.fpFree(pAttrib->core.pData);
				pAttrib->core.pData = NULL;
			}
		}
	}
	if (pMesh->faceAttribs.pArr) {
		pCtx->alloc.fpFree(pMesh->faceAttribs.pArr);
		pMesh->faceAttribs.pArr = NULL;
	}
	if (pMesh->cornerAttribs.pArr) {
		pCtx->alloc.fpFree(pMesh->cornerAttribs.pArr);
		pMesh->cornerAttribs.pArr = NULL;
	}
	if (pMesh->edgeAttribs.pArr) {
		pCtx->alloc.fpFree(pMesh->edgeAttribs.pArr);
		pMesh->edgeAttribs.pArr = NULL;
	}
	if (pMesh->vertAttribs.pArr) {
		pCtx->alloc.fpFree(pMesh->vertAttribs.pArr);
		pMesh->vertAttribs.pArr = NULL;
	}
}

static
StucErr initMeshInWrap(
	StucCtx *pCtx,
	Mesh *pWrap,
	StucMesh meshIn, //passed by value so we can set active attrib domains if missing
	UBitField32 spAttribsToAppend,
	bool *pBuildEdges
) {
	StucErr err = PIX_ERR_SUCCESS;
	err = stucAttemptToSetMissingActiveDomains(&meshIn);
	PIX_ERR_RETURN_IFNOT(err, "");
	stucAliasMeshCoreNoAttribs(&pWrap->core, &meshIn);
	*pBuildEdges = !meshIn.edgeCount;
	if (*pBuildEdges) {
		printf("no edge list found, building one\n");
		PIX_ERR_RETURN_IFNOT_COND(
			err,
			!meshIn.edgeAttribs.count,
			"in-mesh has edge attribs, yet no edge list"
		);
		err = stucBuildEdgeList(pCtx, &pWrap->core);
		PIX_ERR_RETURN_IFNOT(err, "failed to build edge list");
		printf("finished building edge list\n");
	}
	err = appendSpAttribsToInMesh(pCtx, pWrap, &meshIn, spAttribsToAppend);
	PIX_ERR_RETURN_IFNOT(err, "");
	stucSetAttribOrigins(&pWrap->core.meshAttribs, STUC_ATTRIB_ORIGIN_MESH_IN);
	stucSetAttribOrigins(&pWrap->core.faceAttribs, STUC_ATTRIB_ORIGIN_MESH_IN);
	stucSetAttribOrigins(&pWrap->core.cornerAttribs, STUC_ATTRIB_ORIGIN_MESH_IN);
	stucSetAttribOrigins(&pWrap->core.edgeAttribs, STUC_ATTRIB_ORIGIN_MESH_IN);
	stucSetAttribOrigins(&pWrap->core.vertAttribs, STUC_ATTRIB_ORIGIN_MESH_IN);

	err = stucAssignActiveAliases(
		pCtx,
		pWrap,
		~STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) { //all except for
			STUC_ATTRIB_USE_RECEIVE,
			STUC_ATTRIB_USE_USG,
			STUC_ATTRIB_USE_EDGE_LEN
		})),
		STUC_DOMAIN_NONE
	);
	PIX_ERR_RETURN_IFNOT(err, "");

	stucBuildEdgeAdj(pWrap);
	stucBuildSeamAndPreserveTables(pWrap);

	//set sp
	stucSetAttribCopyOpt(
		pCtx,
		&pWrap->core, 
		STUC_ATTRIB_DONT_COPY,
		spAttribsToAppend | STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) {
			STUC_ATTRIB_USE_WSCALE,
			STUC_ATTRIB_USE_NORMALS_VERT
		}))
	);
	//set required
	stucSetAttribCopyOpt(
		pCtx,
		&pWrap->core,
		STUC_ATTRIB_COPY,
		STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) {
			STUC_ATTRIB_USE_POS,
			STUC_ATTRIB_USE_UV,
			STUC_ATTRIB_USE_NORMAL,
			STUC_ATTRIB_USE_IDX
		}))
	);

	return err;
}

StucErr stucMapToMesh(
	StucCtx *pCtx,
	I32 threadId,
	const StucMapArr *pMapArr,
	const StucMesh *pMeshIn,
	const StucAttribIndexedArr *pInIndexedAttribs,
	StucMesh *pMeshOut,
	StucAttribIndexedArr *pOutIndexedAttribs,
	F32 wScale,
	F32 receiveLen,
	bool keepExistingIdxAttribs,
	bool triangulate
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pMeshIn, "");
	err = stucValidateMesh(&pCtx->alloc, pMeshIn, false, false);
	PIX_ERR_RETURN_IFNOT(err, "invalid in-mesh");
	Mesh meshInWrap = {0};
	UBitField32 spAttribsToAppend = STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) {
		STUC_ATTRIB_USE_TANGENT,
		STUC_ATTRIB_USE_TSIGN,
		STUC_ATTRIB_USE_SEAM_EDGE,
		STUC_ATTRIB_USE_SEAM_VERT,
		STUC_ATTRIB_USE_NUM_ADJ_PRESERVE,
		STUC_ATTRIB_USE_EDGE_FACES,
		STUC_ATTRIB_USE_EDGE_CORNERS
	}));
	bool builtEdges = false;
	err = initMeshInWrap(
		pCtx,
		&meshInWrap,
		*(StucMesh *)pMeshIn,
		spAttribsToAppend,
		&builtEdges
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_THROW_IFNOT_COND(
		err,
		pMapArr && pMapArr->count && pMapArr->pArr,
		"", 0
	);
	err = mapMapArrToMesh(
		pCtx,
		threadId,
		pMapArr,
		&meshInWrap,
		pInIndexedAttribs,
		pMeshOut,
		pOutIndexedAttribs,
		wScale,
		receiveLen,
		keepExistingIdxAttribs
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	if (triangulate) {
		err = stucMeshTriangulate(pCtx, pMeshOut);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	const char *logPath = "C:/Users/scout/AppData/Local/Temp/Debug.cark";
	err = carkOutFileSave(&pCtx->cark.ctx, logPath, true);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err, ;);
	if (builtEdges && meshInWrap.core.pEdges) {
		if (meshInWrap.core.pEdges) {
			pCtx->alloc.fpFree(meshInWrap.core.pEdges);
			meshInWrap.core.pEdges = NULL;
		}
	}
	destroyAppendedSpAttribs(pCtx, &meshInWrap.core, spAttribsToAppend);
	carkOutClear(&pCtx->cark.ctx);
	return err;
}

StucErr stucUsgArrDestroy(const StucCtx *pCtx, I32 count, StucUsg *pUsgArr) {
	StucErr err = PIX_ERR_NOT_SET;
	for (I32 i = 0; i < count; ++i) {
		err = stucMeshDestroy(pCtx, (StucMesh *)pUsgArr[i].obj.pData);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	pCtx->alloc.fpFree(pUsgArr);
	PIX_ERR_CATCH(0, err, ;)
	return err;
}

StucErr stucAttribArrDestroy(const StucCtx *pCtx, StucAttribArray *pArr) {
	//TODO put this check in more destroy funcs
	PIX_ERR_ASSERT("", !(!pArr->pArr ^ !pArr->count));
	for (I32 i = 0; i < pArr->count; ++i) {
		if (pArr->pArr[i].core.pData) {
			pCtx->alloc.fpFree(pArr->pArr[i].core.pData);
		}
	}
	if (pArr->pArr) {
		pCtx->alloc.fpFree(pArr->pArr);
	}
	*pArr = (StucAttribArray){0};
	return PIX_ERR_SUCCESS;
}

StucErr stucMeshDestroy(const StucCtx *pCtx, StucMesh *pMesh) {
	stucAttribArrDestroy(pCtx, &pMesh->meshAttribs);
	stucAttribArrDestroy(pCtx, &pMesh->faceAttribs);
	stucAttribArrDestroy(pCtx, &pMesh->cornerAttribs);
	stucAttribArrDestroy(pCtx, &pMesh->edgeAttribs);
	stucAttribArrDestroy(pCtx, &pMesh->vertAttribs);
	if(pMesh->pFaces) {
		pCtx->alloc.fpFree(pMesh->pFaces);
	}
	if (pMesh->pCorners) {
		pCtx->alloc.fpFree(pMesh->pCorners);
	}
	if (pMesh->pEdges) {
		pCtx->alloc.fpFree(pMesh->pEdges);
	}
	return PIX_ERR_SUCCESS;
}

StucErr stucGetAttribSize(const StucAttribCore *pAttrib, I32 *pSize) {
	*pSize = stucGetAttribSizeIntern(pAttrib->type);
	return PIX_ERR_SUCCESS;
}

StucErr stucGetAttrib(const char *pName, StucAttribArray *pAttribs, StucAttrib **ppAttrib) {
	*ppAttrib = stucGetAttribIntern(pName, pAttribs, false, NULL, NULL, NULL);
	return PIX_ERR_SUCCESS;
}

StucErr stucAttribGetAsVoid(StucAttribCore *pAttrib, int32_t idx, void **ppOut) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pAttrib && ppOut && idx >= 0, "");
	*ppOut = stucAttribAsVoid(pAttrib, idx);
	return PIX_ERR_SUCCESS;
}

StucErr stucAttribActiveGet(
	StucCtx *pCtx,
	StucMesh *pMesh,
	StucAttribUse use,
	Attrib **ppAttrib
) {
	*ppAttrib = stucGetActiveAttrib(pCtx, pMesh, use);
	return PIX_ERR_SUCCESS;
}

StucErr stucGetAttribIndexed(
	const char *pName,
	StucAttribIndexedArr *pAttribs,
	StucAttribIndexed **ppAttrib
) {
	*ppAttrib = stucGetAttribIndexedIntern(pAttribs, pName);
	return PIX_ERR_SUCCESS;
}

void stucMapIndexedAttribsGet(
	StucCtx *pCtx,
	StucMap *pMap,
	StucAttribIndexedArr *pIndexedAttribs
) {
	*pIndexedAttribs = pMap->indexedAttribs;
}

//this should not be called by a callback called from uv-stucco
StucErr stucWaitForJobs(
	StucCtx *pCtx,
	I32 count,
	PixthJob *pHandles,
	bool wait,
	bool *pDone
) {
	return pCtx->threadPool.fpWaitForJobs(
		&pCtx->threadPool.handle,
		count,
		pHandles,
		0,
		wait,
		pDone
	);
}

StucErr stucJobGetErrs(
	StucCtx *pCtx,
	I32 jobCount,
	PixthJob *pJobHandles
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("", pCtx && pJobHandles);
	PIX_ERR_ASSERT("", jobCount > 0);
	for (I32 i = 0; i < jobCount; ++i) {
		StucErr jobErr = PIX_ERR_NOT_SET;
		err = pCtx->threadPool.fpGetJobErr(
			&pCtx->threadPool.handle,
			pJobHandles + i,
			&jobErr
		);
		PIX_ERR_THROW_IFNOT_COND(err, jobErr == PIX_ERR_SUCCESS, "", 0);
	}
	PIX_ERR_CATCH(0, err, ;);
	return err;
}

StucErr stucAttribSpIsValid(
	StucCtx *pCtx,
	const AttribCore *pCore,
	StucDomain domain
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pCore, "");
	return 
		stucAttribSpTypeGet(pCtx, pCore->use) == pCore->type &&
		stucAttribSpDomainGet(pCtx, pCore->use) == domain;
}

//TODO move funcs like these out of uv-stucco.c. eg move this one into attrib_utils.c
StucErr stucAttribGetAllDomains(
	StucCtx *pCtx,
	StucMesh *pMesh,
	const char *pName,
	StucAttrib **ppAttrib,
	I32 *pIdx,
	StucDomain *pDomain
) {
	for (I32 i = STUC_DOMAIN_FACE; i <= STUC_DOMAIN_VERT; ++i) {
		AttribArray *pArr = stucGetAttribArrFromDomain(pMesh, i);
		I32 idx = 0;
		Attrib *pAttrib = stucGetAttribIntern(pName, pArr, false, NULL, NULL, &idx);
		if (pAttrib) {
			if (ppAttrib) {
				*ppAttrib = pAttrib;
			}
			if (pIdx) {
				*pIdx = idx;
			}
			if (pDomain) {
				*pDomain = i;
			}
			break;
		}
	}
	return PIX_ERR_SUCCESS;
}

StucErr stucAttribGetAllDomainsConst(
	StucCtx *pCtx,
	const StucMesh *pMesh,
	const char *pName,
	const StucAttrib **ppAttrib,
	I32 *pIdx,
	StucDomain *pDomain
) {
	return stucAttribGetAllDomains(
		pCtx,
		(StucMesh *)pMesh,
		pName,
		(StucAttrib **)ppAttrib,
		pIdx,
		pDomain
	);
}

StucErr stucAttribArrGet(
	StucCtx *pCtx,
	StucMesh *pMesh,
	StucDomain domain,
	StucAttribArray **ppArr
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pMesh && ppArr, "");
	*ppArr = stucGetAttribArrFromDomain(pMesh, domain);
	return err;
}

StucErr stucAttribArrGetConst(
	StucCtx *pCtx,
	const StucMesh *pMesh,
	StucDomain domain,
	const StucAttribArray **ppArr
) {
	return stucAttribArrGet(
		pCtx,
		(StucMesh *)pMesh,
		domain,
		(AttribArray **)ppArr
	);
}

StucErr stucAttribGetCompType(
	StucCtx *pCtx,
	StucAttribType type,
	StucAttribType *pCompType
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pCompType, "");
	*pCompType = stucAttribGetCompTypeIntern(type);
	return err;
}

StucErr stucAttribTypeGetVecSize(
	StucCtx *pCtx,
	StucAttribType type,
	I32 *pSize
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pSize, "");
	*pSize = stucAttribTypeGetVecSizeIntern(type);
	return err;
}

StucErr stucDomainCountGet(
	StucCtx *pCtx,
	const StucMesh *pMesh,
	StucDomain domain,
	int32_t *pCount
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pMesh && pCount, "");
	*pCount = stucDomainCountGetIntern(pMesh, domain);
	return err;
}

StucErr stucAttribIndexedArrDestroy(StucCtx *pCtx, StucAttribIndexedArr *pArr) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pArr, "");
	PIX_ERR_ASSERT("", !(!pArr->pArr ^ !pArr->count));
	if (pArr->pArr) {
		for (I32 i = 0; i < pArr->count; ++i) {
			if (pArr->pArr[i].core.pData) {
				pCtx->alloc.fpFree(pArr->pArr[i].core.pData);
			}
		}
		pCtx->alloc.fpFree(pArr->pArr);
	}
	*pArr = (StucAttribIndexedArr){0};
	return err;
}

StucErr stucMapArrDestroy(StucCtx *pCtx, StucMapArr *pMapArr) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pMapArr, "");
	if (pMapArr->pArr) {
		/*
		for (I32 i = 0; i < pMapArr->count; ++i) {
			stucDestroyBlendOptArr(pCtx, pMapArr->pArr[i].blendOptArr);
		}
		*/
		pCtx->alloc.fpFree(pMapArr->pArr);
	}
	*pMapArr = (StucMapArr){0};
	return err;
}

StucErr stucObjectInit(
	StucCtx *pCtx,
	StucObject *pObj,
	StucMesh *pMesh,
	const Stuc_M4x4 *pTransform
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pObj, "");
	pObj->pData = (StucObjectData *)pMesh;
	if (pTransform) {
		pObj->transform = *pTransform;
	}
	else {
		pObj->transform = PIX_MATH_IDENT_MAT4X4;
	}
	return err;
}

void stucLogEnableSet(StucCtx *pCtx, bool value) {
	carkOutEnableSet(&pCtx->cark.ctx, value);
}
