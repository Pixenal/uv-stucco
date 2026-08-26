/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <map.h>
#include <attrib_utils.h>
#include <interp_and_xform.h>
#include <merge_and_snap.h>

typedef struct xformAndInterpVertsJobArgs {
	JobArgs core;
	Mesh *pOutMesh;// outmesh in core.pBasic is const
	const BufMeshArr *pBufMeshArr;
	const BufMeshArr *pBufMeshClipArr;
	PixalcLinAlloc *pVertAlloc;
	bool intersect;
	JobArgsFoot foot;
} xformAndInterpVertsJobArgs;

typedef struct InterpAttribsJobArgs {
	JobArgs core;
	Mesh *pOutMesh;
	const BufMeshArr *pBufMeshArr;
	const BufMeshArr *pBufMeshClipArr;
	const PixuctHTable *pMergeTable;
	const BufOutRangeTable *pBufOutTable;
	const OutBufIdxArr *pOutBufIdxArr;
	JobArgsFoot foot;
} InterpAttribsJobArgs;

static
UsgInFace *findUsgForMapCorners(
	const MapToMeshBasic *pBasic,
	const FaceRange *pMapFace,
	I32 inFace,
	V3_F32 mapUvw,
	Usg **ppUsg
) {
	const StucMap *pMap = pBasic->pMap;
	for (I32 i = 0; i < pMapFace->range.size; ++i) {
		I32 mapVert = pMap->pMesh->core.pCorners[pMapFace->range.start + i];
		if (!pMap->pMesh->pUsg) {
			continue;
		}
		I32 usgIdx = pMap->pMesh->pUsg[mapVert];
		if (!usgIdx) {
			continue;
		}
		usgIdx = abs(usgIdx) - 1;
		*ppUsg = pMap->usgArr.pArr + usgIdx;
		if (stucIsPointInsideMesh(&pBasic->pCtx->alloc, mapUvw, (*ppUsg)->pMesh)) {
			//passing NULL for above cutoff,
			// we don't need to know cause using flatcutoff eitherway here
			UsgInFace *pUsgEntry = stucGetUsgForCorner(
				i,
				pMap,
				pMapFace,
				inFace,
				NULL
			);
			if (pUsgEntry) {
				return pUsgEntry;
			}
		}
	}
	return NULL;
}

static
void getUsgEntry(
	const MapToMeshBasic *pBasic,
	V3_F32 mapUvw,
	const InterpCacheLimited *pMapInterpCache,
	UsgInFace **ppUsgEntry,
	bool *pAboveCutoff
) {
	if (pMapInterpCache->cache.active == STUC_INTERP_CACHE_COPY_MAP) {
		*ppUsgEntry = stucGetUsgForCorner(
			pMapInterpCache->cache.copyMap.a,
			pBasic->pMap,
			&pMapInterpCache->cache.copyMap.mapFace,
			pMapInterpCache->cache.copyMap.inFace,
			pAboveCutoff
		);
	}
	else {
		Usg *pUsg = NULL;
		*ppUsgEntry = findUsgForMapCorners(
			pBasic,
			&pMapInterpCache->cache.copyMap.mapFace,
			pMapInterpCache->cache.copyMap.inFace,
			mapUvw,
			&pUsg
		);
		if (*ppUsgEntry) {
			PIX_ERR_ASSERT("", pUsg);
			bool insideUsg =
				stucIsPointInsideMesh(&pBasic->pCtx->alloc, mapUvw, pUsg->pFlatCutoff);
			*pAboveCutoff = pUsg->pFlatCutoff && insideUsg;
		}
	}
}

static
StucErr interpActiveAttrib(
	const MapToMeshBasic *pBasic,
	V2_I16 tile,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	InterpCacheLimited *pInterpCache,
	void *pData,
	AttribType type,
	AttribUse use
) {
	StucErr err = PIX_ERR_SUCCESS;
	AttribCore attribWrap = { .pData = pData, .type = type};
	const StucMesh *pSrcMesh = NULL;
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN:
			pSrcMesh = &pBasic->pInMesh->core;
			break;
		case STUC_ATTRIB_ORIGIN_MAP:
			pSrcMesh = &pBasic->pMap->pMesh->core;
			break;
		default:
			PIX_ERR_ASSERT("invalid origin override", false);
	}
	const Attrib *pSrcAttrib =
		stucGetActiveAttribConst(pBasic->pCtx, pSrcMesh, use);
	PIX_ERR_RETURN_IFNOT_COND(err, pSrcAttrib, "active attrib not found");
	stucInterpBufAttrib(
		pBasic,
		tile,
		pBufMesh,
		bufCorner,
		&attribWrap, 0,
		&pSrcAttrib->core,
		pInterpCache
	);
	return err;
}

static
StucErr getInterpolatedTbn(
	const MapToMeshBasic *pBasic,
	V2_I16 tile,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	InterpCacheLimited *pInInterpCache,
	const V3_F32 *pNormal,
	M3x3 *pTbn
) {
	StucErr err = PIX_ERR_SUCCESS;
	V3_F32 tangent = {0};
	V3_F32 normal = {0};
	F32 tSign = .0f;
	if (pNormal) {
		normal = *pNormal;
	}
	else {
		err = interpActiveAttrib(
			pBasic,
			tile,
			pBufMesh,
			bufCorner,
			pInInterpCache,
			&normal,
			STUC_ATTRIB_V3_F32,
			STUC_ATTRIB_USE_NORMAL
		);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	err = interpActiveAttrib(
		pBasic,
		tile,
		pBufMesh,
		bufCorner,
		pInInterpCache,
		&tangent,
		STUC_ATTRIB_V3_F32,
		STUC_ATTRIB_USE_TANGENT
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = interpActiveAttrib(
		pBasic,
		tile,
		pBufMesh,
		bufCorner,
		pInInterpCache,
		&tSign,
		STUC_ATTRIB_F32,
		STUC_ATTRIB_USE_TSIGN
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	V3_F32 bitangent = _(_(normal V3CROSS tangent) V3MULS tSign);
	*(V3_F32 *)&pTbn->d[0] = tangent;
	*(V3_F32 *)&pTbn->d[1] = bitangent;
	*(V3_F32 *)&pTbn->d[2] = normal;
	return err;
}

static
StucErr mapUvwToXyzFlat(
	const MapToMeshBasic *pBasic,
	V2_I16 tile,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	InterpCacheLimited *pInInterpCache,
	V3_F32 *pXyzFlat,
	M3x3 *pTbn
) {
	StucErr err = PIX_ERR_SUCCESS;
	err = getInterpolatedTbn(
		pBasic,
		tile,
		pBufMesh,
		bufCorner,
		pInInterpCache,
		NULL,
		pTbn
	);
	PIX_ERR_RETURN_IFNOT(err, "");

	const Mesh *pInMesh = pBasic->pInMesh;
	const InterpCache *pCache = &pInInterpCache->cache;
	switch (pCache->active) {
		case STUC_INTERP_CACHE_COPY_IN: {
			*pXyzFlat = pInMesh->pPos[pInMesh->core.pCorners[pCache->copyIn.a]];
			break;
		}
		case STUC_INTERP_CACHE_LERP_IN: {
			V3_F32 aPos = pInMesh->pPos[pInMesh->core.pCorners[pCache->lerpIn.a]];
			V3_F32 bPos = pInMesh->pPos[pInMesh->core.pCorners[pCache->lerpIn.b]];
			*pXyzFlat = pixmV3F32Lerp(aPos, bPos, pCache->lerpIn.t);
			break;
		}
		case STUC_INTERP_CACHE_TRI_IN: {
			V3_F32 inXyz[3] = {0};
			for (I32 i = 0; i < 3; ++i) {
				I32 vert = pInMesh->core.pCorners[pCache->triIn.triReal[i]];
				inXyz[i] = pInMesh->pPos[vert];
			}
			*pXyzFlat = pixmBarycentricToCartesian(inXyz, pCache->triIn.bc);
			break;
		}
		default:
			PIX_ERR_ASSERT(
				"invalid interp cache state,\
				this should have been set while interpolating tbn",
				false
			);
	}
	return err;
}

static
StucErr xformVertFromUvwToXyz(
	xformAndInterpVertsJobArgs *pArgs,
	V2_I16 tile,
	const BufMesh *pBufMesh,
	I32 vertIdx,
	FaceCorner bufCorner,
	InterpCaches *pInterpCaches,
	M3x3 *pTbn
) {
	StucErr err = PIX_ERR_SUCCESS;
	const MapToMeshBasic *pBasic = pArgs->core.pShared;
	StucStage stage = STUC_STAGE_OUTMESH;
	I32 thread = pArgs->core.threadId;
	PIX_ERR_ASSERT(
		"",
		pInterpCaches->in.domain == STUC_DOMAIN_CORNER &&
		pInterpCaches->map.domain == STUC_DOMAIN_VERT
	);
	V3_F32 mapUvw = {0};
	err = interpActiveAttrib(
		pBasic,
		tile,
		pBufMesh,
		bufCorner,
		&pInterpCaches->map,
		&mapUvw,
		STUC_ATTRIB_V3_F32,
		STUC_ATTRIB_USE_POS
	);
	InterpCacheLimited inVertInterpCache = {
		.domain = STUC_DOMAIN_VERT,
		.origin = STUC_ATTRIB_ORIGIN_MESH_IN
	};
	if (pBasic->pInMesh->pWScale) {
		F32 inVertWScale = 1.0;
		err = interpActiveAttrib(
			pBasic,
			tile,
			pBufMesh,
			bufCorner,
			&inVertInterpCache,
			&inVertWScale,
			STUC_ATTRIB_F32,
			STUC_ATTRIB_USE_WSCALE
		);
		PIX_ERR_RETURN_IFNOT(err, "");
		mapUvw.d[2] *= inVertWScale;
	}
	PIX_ERR_RETURN_IFNOT(err, "");
	V2_F32 fTileMin = {.d = {tile.d[0], tile.d[1]}};
	_((V2_F32 *)&mapUvw V2SUBEQL fTileMin);
	bool aboveCutoff = false;
	UsgInFace *pUsgEntry = NULL;
	if (pBasic->pMap->pMesh->pUsg) {
		getUsgEntry(pBasic, mapUvw, &pInterpCaches->map, &pUsgEntry, &aboveCutoff);
	}
	V3_F32 xyzFlat = {0};
	M3x3 tbn = {0};
	if (pUsgEntry && aboveCutoff) {
		V2_F32 uv = *(V2_F32 *)&mapUvw;
		stucUsgVertTransform(pUsgEntry, uv, &xyzFlat, pBasic->pInMesh, fTileMin, &tbn);
	}
	else {
		err = mapUvwToXyzFlat(
			pBasic,
			tile,
			pBufMesh,
			bufCorner,
			&pInterpCaches->in,
			&xyzFlat,
			&tbn
		);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = interpActiveAttrib(
			pBasic,
			tile,
			pBufMesh,
			bufCorner,
			&inVertInterpCache,
			tbn.d + 2,
			STUC_ATTRIB_V3_F32,
			STUC_ATTRIB_USE_NORMALS_VERT
		);
		PIX_ERR_RETURN_IFNOT(err, "");
		F32 vertNormalLen = pixmV3F32Len(*(PixtyV3_F32 *)&tbn.d[2]);
		PIX_ERR_RETURN_IFNOT_COND(err, vertNormalLen > .9f && vertNormalLen < 1.1f, "");
	}
	pArgs->pOutMesh->pPos[vertIdx] = _(
		xyzFlat V3ADD _(*(V3_F32 *)&tbn.d[2] V3MULS mapUvw.d[2] * pBasic->wScale)
	);
	*pTbn = tbn;

	if (!pArgs->core.pCark->valid) {
		return err;
	}
	CarkLog log = {0};
	err = CARK_LOG_START(*pArgs->core.pCark, thread, stage, 2, 0, vertIdx, log);
	PIX_ERR_RETURN_IFNOT(err, "");
	for (I32 i = 0; i < 3; ++i) {
		err = carkOutLogComp(&log, i, NULL, pArgs->pOutMesh->pPos[vertIdx].d + i);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	err = carkOutLogEnd(&log);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

static
void blendCommonAttrib(
	const MapToMeshBasic *pBasic,
	const Attrib *pInAttrib,
	const Attrib *pMapAttrib,
	Attrib *pOutAttrib,
	I32 outAttribIdx,
	I32 dataIdx,
	StucDomain domain
) {
	const StucBlendOpt *pOpts = stucGetBlendOpt(
		pBasic->pOptArr,
		outAttribIdx,
		domain
	);
	StucBlendConfig blendConfig = {0};
	if (pOpts) {
		blendConfig = pOpts->blendConfig;
	}
	else {
		const StucTypeDefault *pDefault =
			stucGetTypeDefaultConfig(&pBasic->pCtx->typeDefaults, pOutAttrib->core.type);
		blendConfig = pDefault->blendConfig;
	}
	const StucAttrib *orderTable[2] = {0};
	I8 order = blendConfig.order;
	orderTable[0] = order ? pMapAttrib : pInAttrib;
	orderTable[1] = !order ? pMapAttrib : pInAttrib;
	stucBlendAttribs(
		&pOutAttrib->core, dataIdx,
		&orderTable[0]->core, 0,
		&orderTable[1]->core, 0,
		blendConfig
	);
}

typedef struct AttribPair {
	Attrib *pOut;
	const Attrib *pIn;
	const Attrib *pMap;
} AttribPair;

typedef struct AttribCache {
	AttribPair *pArr;
	I32 size;
	I32 count;
} AttribCache;

static
void cacheAttribPairs(
	const MapToMeshBasic *pBasic,
	Mesh *pOutMesh,
	StucDomain domain,
	AttribCache *pCache
) {
	PixErr err = PIX_ERR_SUCCESS;
	AttribArray *pOutAttribArr = stucGetAttribArrFromDomain(&pOutMesh->core, domain);
	const AttribArray *pMapAttribArr =
		stucGetAttribArrFromDomainConst(&pBasic->pMap->pMesh->core, domain);
	const AttribArray *pInAttribArr =
		stucGetAttribArrFromDomainConst(&pBasic->pInMesh->core, domain);
	PIXALC_DYN_ARR_RESIZE(AttribPair, &pBasic->pCtx->alloc, pCache, pOutAttribArr->count);
	pCache->count = 0;
	for (I32 i = 0; i < pOutAttribArr->count; ++i) {
		pCache->pArr[i].pOut = pOutAttribArr->pArr + i;
		PIX_ERR_ASSERT(
			"string attribs are only for internal use. This needs to be caught earlier",
			pCache->pArr[i].pOut->core.type != STUC_ATTRIB_STRING
		);
		if (pCache->pArr[i].pOut ==
			stucGetActiveAttrib(pBasic->pCtx, &pOutMesh->core, STUC_ATTRIB_USE_POS)
		) {
			continue;
		}
		err = stucGetMatchingAttribConst(
			pBasic->pCtx,
			&pBasic->pInMesh->core, pInAttribArr,
			&pOutMesh->core, pCache->pArr[i].pOut,
			true,
			false,
			&pCache->pArr[i].pIn
		);
		PIX_ERR_ASSERT("", err == PIX_ERR_SUCCESS);
		stucGetMatchingAttribConst(
			pBasic->pCtx,
			&pBasic->pMap->pMesh->core, pMapAttribArr,
			&pOutMesh->core, pCache->pArr[i].pOut,
			true,
			false,
			&pCache->pArr[i].pMap
		);
		PIX_ERR_ASSERT("", err == PIX_ERR_SUCCESS);
		++pCache->count;
	}
}

static
void interpAndBlendAttribs(
	const MapToMeshBasic *pBasic,
	AttribCache *pCache,
	I32 dataIdx,
	StucDomain domain,
	V2_I16 *pTile,//corners or verts
	const BufMesh *pBufMesh,//corners or verts
	const FaceCorner *pBufCorner,//corners or verts
	InterpCaches *pInterpCaches,//corners or verts
	const SrcFaces *pSrcFaces,//faces
	V3_F32 *pNormal
) {
	if (domain == STUC_DOMAIN_FACE) {
		PIX_ERR_ASSERT("", pSrcFaces);
	}
	else if (domain == STUC_DOMAIN_CORNER || domain == STUC_DOMAIN_VERT) {
		PIX_ERR_ASSERT("", pBufMesh && pBufCorner && pInterpCaches);
	}
	else {
		PIX_ERR_ASSERT("invalid domain for this func", false);
	}

	for (I32 i = 0; i < pCache->count; ++i) {
		AttribPair attribs = pCache->pArr[i];
		AttribType type = attribs.pOut->core.type;
		AttribUse use = attribs.pOut->core.use;
		U64 inBuf[4] = {0};
		Attrib inAttribWrap = {
			.core = {.pData = inBuf, .type = type, .use = use},
			.interpolate = true
		};
		U64 mapBuf[4] = {0};
		Attrib mapAttribWrap = {
			.core = {.pData = mapBuf, .type = type, .use = use},
			.interpolate = true
		};

		bool interpIn = false;
		bool interpMap = false;
		switch (attribs.pOut->origin) {
			case STUC_ATTRIB_ORIGIN_COMMON:
				interpIn = interpMap = true;
				break;
			case STUC_ATTRIB_ORIGIN_MESH_IN:
				interpIn = true;
				break;
			case STUC_ATTRIB_ORIGIN_MAP:
				interpMap = true;
				break;
			default:
				PIX_ERR_ASSERT("invalid attrib origin", false);
		}
		if (interpIn) {
			PIX_ERR_ASSERT(
				"",
				attribs.pIn->core.type == type && attribs.pIn->core.use == use
			);
			if (domain == STUC_DOMAIN_FACE) {
				stucCopyAttribCore(
					&inAttribWrap.core, 0,
					&attribs.pIn->core, pSrcFaces->in
				);
			}
			else {
				stucInterpBufAttrib(
					pBasic,
					*pTile,
					pBufMesh,
					*pBufCorner,
					&inAttribWrap.core, 0,
					&attribs.pIn->core,
					&pInterpCaches->in
				);
			}
		}
		if (interpMap) {
			PIX_ERR_ASSERT(
				"",
				attribs.pMap->core.type == type && attribs.pMap->core.use == use
			);
			if (domain == STUC_DOMAIN_FACE) {
				stucCopyAttribCore(
					&mapAttribWrap.core, 0,
					&attribs.pMap->core, pSrcFaces->map
				);
			}
			else {
				stucInterpBufAttrib(
					pBasic,
					*pTile,
					pBufMesh,
					*pBufCorner,
					&mapAttribWrap.core, 0,
					&attribs.pMap->core,
					&pInterpCaches->map
				);
			}
		}
		
		if (pNormal && use == STUC_ATTRIB_USE_NORMAL) {
			memcpy(pNormal->d, inAttribWrap.core.pData, sizeof(pNormal->d));
		}

		switch (attribs.pOut->origin) {
			case STUC_ATTRIB_ORIGIN_COMMON:
				blendCommonAttrib(
					pBasic,
					&inAttribWrap,
					&mapAttribWrap,
					attribs.pOut, i,
					dataIdx,
					domain
				);
				break;
			case STUC_ATTRIB_ORIGIN_MESH_IN:
				stucCopyAttribCore(&attribs.pOut->core, dataIdx, &inAttribWrap.core, 0);
				break;
			case STUC_ATTRIB_ORIGIN_MAP:
				stucCopyAttribCore(&attribs.pOut->core, dataIdx, &mapAttribWrap.core, 0);
				break;
			default:
				PIX_ERR_ASSERT("invalid origin override", false);
		}
	}
}

static
void xformNormals(StucMesh *pMesh, I32 idx, const M3x3 *pTbn, StucDomain domain) {
	AttribArray *pAttribArr = stucGetAttribArrFromDomain(pMesh, domain);
	for (I32 i = 0; i < pAttribArr->count; ++i) {
		Attrib *pAttrib = pAttribArr->pArr + i;
		if (pAttrib->core.use == STUC_ATTRIB_USE_NORMAL) {
			if (pAttrib->core.type != STUC_ATTRIB_V3_F32) {
				//wrong type
				//TODO when warnings are implemented, warn about this
				continue;
			}
			V3_F32 *pNormal = stucAttribAsVoid(&pAttrib->core, idx);
			*pNormal = _(*pNormal V3MULM3X3 pTbn);
		}
	}
}

static
void attribCacheDestroy(const PixalcFPtrs *pAlloc, AttribCache *pCache) {
	if (pCache->pArr) {
		pAlloc->fpFree(pCache->pArr);
	}
}

static
StucErr xformAndInterpVertsInRange(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	xformAndInterpVertsJobArgs *pArgs = pArgsVoid;
	const MapToMeshBasic *pBasic = pArgs->core.pShared;
	AttribCache attribs = {0};
	cacheAttribPairs(pBasic, pArgs->pOutMesh, STUC_DOMAIN_VERT, &attribs);
	PixalcLinAllocIter iter = {0};
	pixalcLinAllocIterInit(pArgs->pVertAlloc, pArgs->core.range, &iter);
	for (; !pixalcLinAllocIterAtEnd(&iter); pixalcLinAllocIterInc(&iter)) {
		VertMerge *pEntry = pixalcLinAllocGetItem(&iter);
		PIX_ERR_ASSERT(
			"",
			!(pArgs->intersect ^ (pEntry->key.type == STUC_BUF_VERT_INTERSECT))
		);
		if (pEntry->removed) {
			continue;
		}
		if (pArgs->intersect) {
			VertMergeIntersect *pIntersect = (VertMergeIntersect *)pEntry;
			if (pIntersect->pSnapTo) {
				continue; //vert was snapped to another - skip
			}
		}
		const BufMesh *pBufMesh = NULL;
		getBufMeshForVertMergeEntry(
			pArgs->pBufMeshArr, pArgs->pBufMeshClipArr,
			pEntry,
			&pBufMesh
		);
		V2_I16 tile = pBufMesh->faces.pArr[pEntry->bufCorner.corner.face].tile;
		InterpCaches interpCaches = {
			.in = {.domain = STUC_DOMAIN_CORNER, .origin = STUC_ATTRIB_ORIGIN_MESH_IN},
			.map = {.domain = STUC_DOMAIN_VERT, .origin = STUC_ATTRIB_ORIGIN_MAP}
		};
		err = xformVertFromUvwToXyz(
			pArgs,
			tile,
			pBufMesh,
			pEntry->outVert,
			pEntry->bufCorner.corner,
			&interpCaches,
			&pEntry->transform.tbn
		);
		PIX_ERR_RETURN_IFNOT(err, "");
		interpAndBlendAttribs(
			pBasic,
			&attribs,
			pEntry->outVert,
			STUC_DOMAIN_VERT,
			&tile,
			pBufMesh,
			&pEntry->bufCorner.corner,
			&interpCaches,
			NULL,
			NULL
		);
		xformNormals(
			&pArgs->pOutMesh->core,
			pEntry->outVert,
			&pEntry->transform.tbn,
			STUC_DOMAIN_VERT
		);
	}
	attribCacheDestroy(&pBasic->pCtx->alloc, &attribs);
	return err;
}

static
const VertMerge *getVertMergeFromBufCorner(
	const InterpAttribsJobArgs *pArgs,
	BufVertType type,
	I32 mergedVert
) {
	const PixalcLinAlloc *pLinAlloc = pixuctHTableAllocGetConst(
		pArgs->pMergeTable,
		type == STUC_BUF_VERT_INTERSECT
	);
	PIX_ERR_ASSERT("", pLinAlloc);
	const VertMerge *pEntry = pixalcLinAllocIdxConst(pLinAlloc, mergedVert);
	PIX_ERR_ASSERT("", pEntry);
	return pEntry;
}

static
I32 bufOutTableGetStart(InterpAttribsJobArgs *pArgs, I32 start) {
	const BufOutRangeTable *pTable = pArgs->pBufOutTable;
	I32 idx = -1;
	for (I32 i = 0; i < pTable->count; ++i) {
		if (pTable->pArr[i].empty) {
			continue;
		}
		if (pTable->pArr[i].outCorners.start > start) {
			break;
		}
		idx = i;
	}
	PIX_ERR_ASSERT("unable to find bufmesh", idx != -1);
	return idx;
}

static
I32 bufOutTableGetNext(const InterpAttribsJobArgs *pArgs, I32 idx) {
	I32 next = -1;
	for (I32 i = idx + 1; i < pArgs->pBufOutTable->count; ++i) {
		if (!pArgs->pBufOutTable->pArr[i].empty) {
			next = i;
			break;
		}
	}
	return next;
}

static
bool bufOutTableAtEnd(const InterpAttribsJobArgs *pArgs, I32 idx) {
	return
		idx == -1 ||
		pArgs->pBufOutTable->pArr[idx].outCorners.start >= pArgs->core.range.end;
}

static
const VertMerge *getVertMergeEntry(
	const InterpAttribsJobArgs *pArgs,
	I32 rangeIdx,
	I32 corner,
	V2_I16 *pTile,
	const BufMesh **ppBufMesh,
	FaceCorner *pBufCorner
) {
	BufOutRange *pRange = pArgs->pBufOutTable->pArr + rangeIdx;
	*ppBufMesh = pRange->clip ?
		pArgs->pBufMeshClipArr->pArr + pRange->bufMesh :
		pArgs->pBufMeshArr->pArr + pRange->bufMesh;

	//out-corner currently holds out-buf-idx-arr idx
	OutBufIdx outBufIdx = pArgs->pOutBufIdxArr->pArr[
		pArgs->pOutMesh->core.pCorners[corner]
	];
	BufFace bufFace = (*ppBufMesh)->faces.pArr[outBufIdx.corner.face];
	BufCorner bufCorner =
		(*ppBufMesh)->corners.pArr[bufFace.start + outBufIdx.corner.corner];
	const VertMerge *pVertEntry =
		getVertMergeFromBufCorner(pArgs, bufCorner.type, outBufIdx.mergedVert);

	*pTile = bufFace.tile;
	if (pBufCorner) {
		*pBufCorner = outBufIdx.corner;
	}
	return pVertEntry;
}

StucErr stucInterpCornerAttribs(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	InterpAttribsJobArgs *pArgs = pArgsVoid;
	const MapToMeshBasic *pBasic = pArgs->core.pShared;
	AttribCache attribs = {0};
	cacheAttribPairs(pBasic, pArgs->pOutMesh, STUC_DOMAIN_CORNER, &attribs);
	I32 corner = pArgs->core.range.start;
	StucStage stage = STUC_STAGE_OUTMESH;
	StucCark *pCark = pArgs->core.pCark;
	for (
		I32 i = bufOutTableGetStart(pArgs, corner);
		!bufOutTableAtEnd(pArgs, i);
		i = bufOutTableGetNext(pArgs, i)
	) {
		BufOutRange *pRange = pArgs->pBufOutTable->pArr + i;
		for (;
			corner < pRange->outCorners.end && corner < pArgs->core.range.end;
			++corner
		) {
			const BufMesh *pBufMesh = NULL;
			FaceCorner bufCorner = {0};
			V2_I16 tile = {0};
			const VertMerge *pVertEntry =
				getVertMergeEntry(pArgs, i, corner, &tile, &pBufMesh, &bufCorner);
			InterpCaches interpCaches = {
				.in = {.domain = STUC_DOMAIN_CORNER, .origin = STUC_ATTRIB_ORIGIN_MESH_IN},
				.map = {.domain = STUC_DOMAIN_CORNER, .origin = STUC_ATTRIB_ORIGIN_MAP}
			};
			V3_F32 normal = {0};
			interpAndBlendAttribs(
				pBasic,
				&attribs,
				corner,
				STUC_DOMAIN_CORNER,
				&tile,
				pBufMesh,
				&bufCorner,
				&interpCaches,
				NULL,
				&normal
			);
			M3x3 tbn = {0};
			err = getInterpolatedTbn(
				pBasic,
				tile,
				pBufMesh,
				bufCorner,
				&interpCaches.in,
				&normal,
				&tbn
			);
			PIX_ERR_RETURN_IFNOT(err, "");
			xformNormals(
				&pArgs->pOutMesh->core,
				corner,
				&tbn,
				STUC_DOMAIN_CORNER
			);
			pArgs->pOutMesh->core.pCorners[corner] = pVertEntry->outVert;
			if (pCark->valid) {
				CarkLog log = {0};
				err = CARK_LOG_START(*pCark, pArgs->core.threadId, stage, 1, 0, corner, log);
				PIX_ERR_RETURN_IFNOT(err, "");
				err = carkOutLogComp(&log, 0, NULL, &pVertEntry->outVert);
				PIX_ERR_RETURN_IFNOT(err, "");
				err = carkOutLogEnd(&log);
				PIX_ERR_RETURN_IFNOT(err, "");
			}
		}
	}
	attribCacheDestroy(&pBasic->pCtx->alloc, &attribs);
	return err;
}

StucErr stucInterpFaceAttribs(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	InterpAttribsJobArgs *pArgs = pArgsVoid;
	const MapToMeshBasic *pBasic = pArgs->core.pShared;
	AttribCache attribs = {0};
	cacheAttribPairs(pBasic, pArgs->pOutMesh, STUC_DOMAIN_FACE, &attribs);
	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	for (I32 i = 0; i < rangeSize; ++i) {
		I32 face = pArgs->core.range.start + i;
		I32 corner = pArgs->pOutMesh->core.pFaces[face];
		I32 bufOutRange = bufOutTableGetStart(pArgs, corner);
		
		const BufMesh *pBufMesh = NULL;
		FaceCorner bufCorner = {0};
		V2_I16 tile = {0};
		getVertMergeEntry(
			pArgs,
			bufOutRange,
			corner,
			&tile,
			&pBufMesh,
			&bufCorner
		);

		SrcFaces srcFaces = stucGetSrcFacesForBufCorner(
			pBufMesh,
			bufCorner
		);
		PIX_ERR_ASSERT(
		"",
		srcFaces.in >= 0 && srcFaces.in < pBasic->pInMesh->core.faceCount &&
		srcFaces.map >= 0 && srcFaces.map < pBasic->pMap->pMesh->core.faceCount
		);
		//not actually interpolating faces,
		//just copying
		interpAndBlendAttribs(
			pBasic,
			&attribs,
			face,
			STUC_DOMAIN_FACE,
			NULL, NULL, NULL, NULL,
			&srcFaces,
			NULL
		);
		//TODO transforming face normals not supported atm
	}
	attribCacheDestroy(&pBasic->pCtx->alloc, &attribs);
	return err;
}

typedef struct XformVertsJobInitInfo {
	const BufMeshArr *pBufMeshArr;
	const BufMeshArr *pBufMeshClipArr;
	Mesh *pOutMesh;
	PixuctHTable *pMergeTable;
	I32 vertAllocIdx;
} XformVertsJobInitInfo;

static
I32 xformVertsJobsGetRange(const StucCtx *pCtx, const void *pShared, void *pInitInfoVoid) {
	XformVertsJobInitInfo *pInitInfo = pInitInfoVoid;
	PixalcLinAlloc *pVertAlloc =
		pixuctHTableAllocGet(pInitInfo->pMergeTable, pInitInfo->vertAllocIdx);
	return pixalcLinAllocGetCount(pVertAlloc);
}

static
void xformVertsJobInit(
	const StucCtx *pCtx,
	const void *pShared,
	void *pInitInfoVoid,
	void *pEntryVoid
) {
	xformAndInterpVertsJobArgs *pEntry = pEntryVoid;
	XformVertsJobInitInfo *pInitInfo = pInitInfoVoid;
	PixalcLinAlloc *pVertAlloc =
		pixuctHTableAllocGet(pInitInfo->pMergeTable, pInitInfo->vertAllocIdx);
	pEntry->pVertAlloc = pVertAlloc;
	pEntry->pOutMesh = pInitInfo->pOutMesh;
	pEntry->pBufMeshArr = pInitInfo->pBufMeshArr;
	pEntry->pBufMeshClipArr = pInitInfo->pBufMeshClipArr;
	 //TODO again, make an enum or something for lin-alloc handles
	pEntry->intersect = pInitInfo->vertAllocIdx == 1;
}

StucErr stucXFormAndInterpVerts(
	MapToMeshBasic *pBasic,
	I32 threadId,
	StucCark *pCark,
	const BufMeshArr *pBufMeshArr,
	const BufMeshArr *pBufMeshClipArr,
	PixuctHTable *pMergeTable,
	I32 vertAllocIdx
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	xformAndInterpVertsJobArgs jobArgs[PIXTH_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pBasic->pCtx,
		pCark,
		pBasic,
		&jobCount, jobArgs, sizeof(xformAndInterpVertsJobArgs),
		&(XformVertsJobInitInfo) {
			.pBufMeshArr = pBufMeshArr,
			.pBufMeshClipArr = pBufMeshClipArr,
			.pOutMesh = &pBasic->outMesh,
			.pMergeTable = pMergeTable,
			.vertAllocIdx = vertAllocIdx
		},
		xformVertsJobsGetRange, xformVertsJobInit
	);
	err = stucDoJobInParallel(
		pBasic->pCtx,
		threadId,
		jobCount, jobArgs, sizeof(xformAndInterpVertsJobArgs),
		xformAndInterpVertsInRange
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

typedef struct InterpAttribsJobInitInfo {
	const BufMeshArr *pBufMeshArr;
	const BufMeshArr *pBufMeshClipArr;
	const PixuctHTable *pMergeTable;
	const BufOutRangeTable *pBufOutTable;
	Mesh *pOutMesh;
	const OutBufIdxArr *pOutBufIdxArr;
	StucDomain domain;
} InterpAttribsJobInitInfo;

static
I32 interpAttribsJobsGetRange(const StucCtx *pCtx, const void *pShared, void *pInitInfo) {
	return stucDomainCountGetIntern(
		&((MapToMeshBasic *)pShared)->outMesh.core,
		((InterpAttribsJobInitInfo *)pInitInfo)->domain
	);
}

static
void interpAttribsJobInit(
	const StucCtx *pCtx,
	const void *pShared,
	void *pInitInfoVoid,
	void *pEntryVoid
) {
	InterpAttribsJobArgs *pEntry = pEntryVoid;
	InterpAttribsJobInitInfo *pInitInfo = pInitInfoVoid;
	pEntry->pOutMesh = pInitInfo->pOutMesh;
	pEntry->pBufMeshArr = pInitInfo->pBufMeshArr;
	pEntry->pBufMeshClipArr = pInitInfo->pBufMeshClipArr;
	pEntry->pMergeTable = pInitInfo->pMergeTable;
	pEntry->pBufOutTable = pInitInfo->pBufOutTable;
	pEntry->pOutBufIdxArr = pInitInfo->pOutBufIdxArr;
}

StucErr stucInterpAttribs(
	MapToMeshBasic *pBasic,
	I32 threadId,
	StucCark *pCark,
	const BufMeshArr *pBufMeshArr,
	const BufMeshArr *pBufMeshClipArr,
	PixuctHTable *pMergeTable,
	const BufOutRangeTable *pBufOutTable,
	const OutBufIdxArr *pOutBufIdxArr,
	StucDomain domain,
	StucErr (* job)(void *)
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	InterpAttribsJobArgs jobArgs[PIXTH_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pBasic->pCtx,
		pCark,
		pBasic,
		&jobCount, jobArgs, sizeof(InterpAttribsJobArgs),
		&(InterpAttribsJobInitInfo) {
			.pBufMeshArr = pBufMeshArr,
			.pBufMeshClipArr = pBufMeshClipArr,
			.pOutMesh = &pBasic->outMesh,
			.pMergeTable = pMergeTable,
			.pBufOutTable = pBufOutTable,
			.pOutBufIdxArr = pOutBufIdxArr,
			.domain = domain
		},
		interpAttribsJobsGetRange, interpAttribsJobInit
	);
	err = stucDoJobInParallel(
		pBasic->pCtx,
		threadId,
		jobCount, jobArgs, sizeof(InterpAttribsJobArgs),
		job
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

