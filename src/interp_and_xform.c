/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <context.h>
#include <map.h>
#include <attrib_utils.h>
#include <interp_and_xform.h>
#include <merge_and_snap.h>

typedef struct xformAndInterpVertsJobArgs {
	JobArgs core;
	Mesh *pOutMesh;// outmesh in core.pBasic is const
	const InPieceArr *pInPieces;
	const InPieceArr *pInPiecesClip;
	PixalcLinAlloc *pVertAlloc;
	bool intersect;
} xformAndInterpVertsJobArgs;

typedef struct InterpAttribsJobArgs {
	JobArgs core;
	Mesh *pOutMesh;
	const InPieceArr *pInPieces;
	const InPieceArr *pInPiecesClip;
	const HTable *pMergeTable;
	const BufOutRangeTable *pBufOutTable;
	const OutBufIdxArr *pOutBufIdxArr;
} InterpAttribsJobArgs;

static
UsgInFace *findUsgForMapCorners(
	const MapToMeshBasic *pBasic,
	const FaceRange *pMapFace,
	I32 inFace,
	V3_F32 mapUvw,
	Usg **ppUsg
) {
	StucMap pMap = pBasic->pMap;
	for (I32 i = 0; i < pMapFace->size; ++i) {
		I32 mapVert = pMap->pMesh->core.pCorners[pMapFace->start + i];
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
	const InPiece *pInPiece,
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
		pInPiece,
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
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	InterpCacheLimited *pInInterpCache,
	M3x3 *pTbn
) {
	StucErr err = PIX_ERR_SUCCESS;
	V3_F32 tangent = {0};
	V3_F32 normal = {0};
	F32 tSign = .0f;
	err = interpActiveAttrib(
		pBasic,
		pInPiece,
		pBufMesh,
		bufCorner,
		pInInterpCache,
		&normal,
		STUC_ATTRIB_V3_F32,
		STUC_ATTRIB_USE_NORMAL
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = interpActiveAttrib(
		pBasic,
		pInPiece,
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
		pInPiece,
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
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	V3_F32 mapUvw,
	InterpCacheLimited *pInInterpCache,
	V3_F32 *pXyzFlat,
	M3x3 *pTbn
) {
	StucErr err = PIX_ERR_SUCCESS;
	F32 inVertWScale = 1.0;
	if (pBasic->pInMesh->pWScale) {
		InterpCacheLimited inVertInterpCache = {
			.domain = STUC_DOMAIN_VERT,
			.origin = STUC_ATTRIB_ORIGIN_MESH_IN
		};
		err = interpActiveAttrib(
			pBasic,
			pInPiece,
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

	err = getInterpolatedTbn(
		pBasic,
		pInPiece,
		pBufMesh,
		bufCorner,
		pInInterpCache,
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
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner bufCorner,
	InterpCaches *pInterpCaches,
	V3_F32 *pPos,
	M3x3 *pTbn
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT(
		"",
		pInterpCaches->in.domain == STUC_DOMAIN_CORNER &&
		pInterpCaches->map.domain == STUC_DOMAIN_VERT
	);
	V3_F32 mapUvw = {0};
	err = interpActiveAttrib(
		pBasic,
		pInPiece,
		pBufMesh,
		bufCorner,
		&pInterpCaches->map,
		&mapUvw,
		STUC_ATTRIB_V3_F32,
		STUC_ATTRIB_USE_POS
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	V2_F32 fTileMin = {.d = {pInPiece->pList->tile.d[0], pInPiece->pList->tile.d[1]}};
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
			pInPiece,
			pBufMesh,
			bufCorner,
			mapUvw,
			&pInterpCaches->in,
			&xyzFlat,
			&tbn
		);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	*pPos = _(xyzFlat V3ADD _(*(V3_F32 *)&tbn.d[2] V3MULS mapUvw.d[2] * pBasic->wScale));
	*pTbn = tbn;
	return err;
}

static
void blendCommonAttrib(
	const MapToMeshBasic *pBasic,
	const Attrib *pInAttrib,
	const Attrib *pMapAttrib,
	Attrib *pOutAttrib,
	I32 dataIdx,
	StucDomain domain
) {
	//TODO should this be using name even for active attribs?
	const StucCommonAttrib *pCommon = stucGetCommonAttribFromDomain(
		pBasic->pCommonAttribList,
		pOutAttrib->core.name,
		domain
	);
	StucBlendConfig blendConfig = {0};
	if (pCommon) {
		blendConfig = pCommon->blendConfig;
	}
	else {
		StucTypeDefault *pDefault =
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

static
void interpAndBlendAttribs(
	const MapToMeshBasic *pBasic,
	Mesh *pOutMesh,
	I32 dataIdx,
	StucDomain domain,
	const InPiece *pInPiece,//corners or verts
	const BufMesh *pBufMesh,//corners or verts
	const FaceCorner *pBufCorner,//corners or verts
	InterpCaches *pInterpCaches,//corners or verts
	const SrcFaces *pSrcFaces//faces
) {
	StucErr err = PIX_ERR_SUCCESS;
	if (domain == STUC_DOMAIN_FACE) {
		PIX_ERR_ASSERT("", pSrcFaces);
	}
	else if (domain == STUC_DOMAIN_CORNER || domain == STUC_DOMAIN_VERT) {
		PIX_ERR_ASSERT("", pInPiece && pBufMesh && pBufCorner && pInterpCaches);
	}
	else {
		PIX_ERR_ASSERT("invalid domain for this func", false);
	}
	AttribArray *pOutAttribArr = stucGetAttribArrFromDomain(&pOutMesh->core, domain);
	const AttribArray *pMapAttribArr =
		stucGetAttribArrFromDomainConst(&pBasic->pMap->pMesh->core, domain);
	const AttribArray *pInAttribArr =
		stucGetAttribArrFromDomainConst(&pBasic->pInMesh->core, domain);
	for (I32 i = 0; i < pOutAttribArr->count; ++i) {
		Attrib *pOutAttrib = pOutAttribArr->pArr + i;
		AttribType type = pOutAttrib->core.type;
		AttribUse use = pOutAttrib->core.use;
		PIX_ERR_ASSERT(
			"string attribs are only for internal use. This needs to be caught earlier",
			type != STUC_ATTRIB_STRING
		);
		if (pOutAttrib ==
			stucGetActiveAttrib(pBasic->pCtx, &pOutMesh->core, STUC_ATTRIB_USE_POS)
		) {
			continue;
		}
		const StucAttrib *pInAttrib = NULL;
		err = stucGetMatchingAttribConst(
			pBasic->pCtx,
			&pBasic->pInMesh->core, pInAttribArr,
			&pOutMesh->core, pOutAttrib,
			true,
			false,
			&pInAttrib
		);
		PIX_ERR_ASSERT("", err == PIX_ERR_SUCCESS);
		const StucAttrib *pMapAttrib = NULL;
		stucGetMatchingAttribConst(
			pBasic->pCtx,
			&pBasic->pMap->pMesh->core, pMapAttribArr,
			&pOutMesh->core, pOutAttrib,
			true,
			false,
			&pMapAttrib
		);
		PIX_ERR_ASSERT("", err == PIX_ERR_SUCCESS);

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
		switch (pOutAttrib->origin) {
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
			PIX_ERR_ASSERT("", pInAttrib->core.type == type && pInAttrib->core.use == use);
			if (domain == STUC_DOMAIN_FACE) {
				stucCopyAttribCore(
					&inAttribWrap.core, 0,
					&pInAttrib->core, pSrcFaces->in
				);
			}
			else {
				stucInterpBufAttrib(
					pBasic,
					pInPiece,
					pBufMesh,
					*pBufCorner,
					&inAttribWrap.core, 0,
					&pInAttrib->core,
					&pInterpCaches->in
				);
			}
		}
		if (interpMap) {
			PIX_ERR_ASSERT("", pMapAttrib->core.type == type && pMapAttrib->core.use == use);
			if (domain == STUC_DOMAIN_FACE) {
				stucCopyAttribCore(
					&mapAttribWrap.core, 0,
					&pMapAttrib->core, pSrcFaces->map
				);
			}
			else {
				stucInterpBufAttrib(
					pBasic,
					pInPiece,
					pBufMesh,
					*pBufCorner,
					&mapAttribWrap.core, 0,
					&pMapAttrib->core,
					&pInterpCaches->map
				);
			}
		}

		switch (pOutAttrib->origin) {
			case STUC_ATTRIB_ORIGIN_COMMON:
				blendCommonAttrib(
					pBasic,
					&inAttribWrap,
					&mapAttribWrap,
					pOutAttrib, dataIdx,
					domain
				);
				break;
			case STUC_ATTRIB_ORIGIN_MESH_IN:
				stucCopyAttribCore(&pOutAttrib->core, dataIdx, &inAttribWrap.core, 0);
				break;
			case STUC_ATTRIB_ORIGIN_MAP:
				stucCopyAttribCore(&pOutAttrib->core, dataIdx, &mapAttribWrap.core, 0);
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
StucErr xformAndInterpVertsInRange(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	xformAndInterpVertsJobArgs *pArgs = pArgsVoid;
	const MapToMeshBasic *pBasic = pArgs->core.pBasic;
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
		const InPiece *pInPiece = NULL;
		const BufMesh *pBufMesh = NULL;
		getBufMeshForVertMergeEntry(
			pArgs->pInPieces, pArgs->pInPiecesClip,
			pEntry,
			&pInPiece,
			&pBufMesh
		);
		InterpCaches interpCaches = {
			.in = {.domain = STUC_DOMAIN_CORNER, .origin = STUC_ATTRIB_ORIGIN_MESH_IN},
			.map = {.domain = STUC_DOMAIN_VERT, .origin = STUC_ATTRIB_ORIGIN_MAP}
		};
		xformVertFromUvwToXyz(
			pBasic,
			pInPiece,
			pBufMesh,
			pEntry->bufCorner.corner,
			&interpCaches,
			pArgs->pOutMesh->pPos + pEntry->outVert,
			&pEntry->transform.tbn
		);
		interpAndBlendAttribs(
			pBasic,
			pArgs->pOutMesh,
			pEntry->outVert,
			STUC_DOMAIN_VERT,
			pInPiece,
			pBufMesh,
			&pEntry->bufCorner.corner,
			&interpCaches,
			NULL
		);
		xformNormals(
			&pArgs->pOutMesh->core,
			pEntry->outVert,
			&pEntry->transform.tbn,
			STUC_DOMAIN_VERT
		);
	}
	return err;
}

static
const VertMerge *getVertMergeFromBufCorner(
	const InterpAttribsJobArgs *pArgs,
	BufVertType type,
	I32 mergedVert
) {
	const PixalcLinAlloc *pLinAlloc = stucHTableAllocGetConst(
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
	const InPiece **ppInPiece,
	const BufMesh **ppBufMesh,
	FaceCorner *pBufCorner
) {
	BufOutRange *pRange = pArgs->pBufOutTable->pArr + rangeIdx;
	*ppBufMesh = pRange->clip ?
		pArgs->pInPiecesClip->pBufMeshes->arr + pRange->bufMesh :
		pArgs->pInPieces->pBufMeshes->arr + pRange->bufMesh;

	//out-corner currently holds out-buf-idx-arr idx
	OutBufIdx outBufIdx = pArgs->pOutBufIdxArr->pArr[
		pArgs->pOutMesh->core.pCorners[corner]
	];
	BufFace bufFace = (*ppBufMesh)->faces.pArr[outBufIdx.corner.face];
	BufCorner bufCorner =
		(*ppBufMesh)->corners.pArr[bufFace.start + outBufIdx.corner.corner];
	const VertMerge *pVertEntry =
		getVertMergeFromBufCorner(pArgs, bufCorner.type, outBufIdx.mergedVert);

	*ppInPiece = pRange->clip ?
		pArgs->pInPiecesClip->pArr + bufFace.inPiece :
		pArgs->pInPieces->pArr + bufFace.inPiece;
	if (pBufCorner) {
		*pBufCorner = outBufIdx.corner;
	}
	return pVertEntry;
}

StucErr stucInterpCornerAttribs(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	InterpAttribsJobArgs *pArgs = pArgsVoid;
	I32 corner = pArgs->core.range.start;
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
			const InPiece *pInPiece = NULL;
			const BufMesh *pBufMesh = NULL;
			FaceCorner bufCorner = {0};
			const VertMerge *pVertEntry =
				getVertMergeEntry(pArgs, i, corner, &pInPiece, &pBufMesh, &bufCorner);
			InterpCaches interpCaches = {
				.in = {.domain = STUC_DOMAIN_CORNER, .origin = STUC_ATTRIB_ORIGIN_MESH_IN},
				.map = {.domain = STUC_DOMAIN_CORNER, .origin = STUC_ATTRIB_ORIGIN_MAP}
			};
			interpAndBlendAttribs(
				pArgs->core.pBasic,
				pArgs->pOutMesh,
				corner,
				STUC_DOMAIN_CORNER,
				pInPiece,
				pBufMesh,
				&bufCorner,
				&interpCaches,
				NULL
			);
			xformNormals(
				&pArgs->pOutMesh->core,
				corner,
				&pVertEntry->transform.tbn,
				STUC_DOMAIN_CORNER
			);
			pArgs->pOutMesh->core.pCorners[corner] = pVertEntry->outVert;
		}
	}
	return err;
}

StucErr stucInterpFaceAttribs(void *pArgsVoid) {
	StucErr err = PIX_ERR_SUCCESS;
	InterpAttribsJobArgs *pArgs = pArgsVoid;
	I32 rangeSize = pArgs->core.range.end - pArgs->core.range.start;
	for (I32 i = 0; i < rangeSize; ++i) {
		I32 face = pArgs->core.range.start + i;
		I32 corner = pArgs->pOutMesh->core.pFaces[face];
		I32 bufOutRange = bufOutTableGetStart(pArgs, corner);
		
		const InPiece *pInPiece = NULL;
		const BufMesh *pBufMesh = NULL;
		FaceCorner bufCorner = {0};
		const VertMerge *pVertEntry = getVertMergeEntry(
			pArgs,
			bufOutRange,
			corner,
			&pInPiece, &pBufMesh, &bufCorner
		);

		SrcFaces srcFaces = {0};
		stucGetSrcFacesForBufCorner(
			pArgs->core.pBasic,
			pInPiece,
			pBufMesh,
			bufCorner
		);
		//not actually interpolating faces,
		//just copying
		interpAndBlendAttribs(
			pArgs->core.pBasic,
			pArgs->pOutMesh,
			face,
			STUC_DOMAIN_FACE,
			NULL, NULL, NULL, NULL,
			&srcFaces
		);
		//TODO transforming face normals not supported atm
	}
	return err;
}

typedef struct XformVertsJobInitInfo {
	const InPieceArr *pInPieces;
	const InPieceArr *pInPiecesClip;
	HTable *pMergeTable;
	I32 vertAllocIdx;
} XformVertsJobInitInfo;

static
I32 xformVertsJobsGetRange(const MapToMeshBasic *pBasic, void *pInitInfoVoid) {
	XformVertsJobInitInfo *pInitInfo = pInitInfoVoid;
	PixalcLinAlloc *pVertAlloc =
		stucHTableAllocGet(pInitInfo->pMergeTable, pInitInfo->vertAllocIdx);
	return pixalcLinAllocGetCount(pVertAlloc);
}

static
void xformVertsJobInit(MapToMeshBasic *pBasic, void *pInitInfoVoid, void *pEntryVoid) {
	xformAndInterpVertsJobArgs *pEntry = pEntryVoid;
	XformVertsJobInitInfo *pInitInfo = pInitInfoVoid;
	PixalcLinAlloc *pVertAlloc =
		stucHTableAllocGet(pInitInfo->pMergeTable, pInitInfo->vertAllocIdx);
	pEntry->pVertAlloc = pVertAlloc;
	pEntry->pOutMesh = &pBasic->outMesh;
	pEntry->pInPieces = pInitInfo->pInPieces;
	pEntry->pInPiecesClip = pInitInfo->pInPiecesClip;
	 //TODO again, make an enum or something for lin-alloc handles
	pEntry->intersect = pInitInfo->vertAllocIdx == 1;
}

StucErr stucXFormAndInterpVerts(
	MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	HTable *pMergeTable,
	I32 vertAllocIdx
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	xformAndInterpVertsJobArgs jobArgs[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pBasic,
		&jobCount, jobArgs, sizeof(xformAndInterpVertsJobArgs),
		&(XformVertsJobInitInfo) {
			.pInPieces = pInPieces,
			.pInPiecesClip = pInPiecesClip,
			.pMergeTable = pMergeTable,
			.vertAllocIdx = vertAllocIdx
		},
		xformVertsJobsGetRange, xformVertsJobInit
	);
	err = stucDoJobInParallel(
		pBasic,
		jobCount, jobArgs, sizeof(xformAndInterpVertsJobArgs),
		xformAndInterpVertsInRange
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

typedef struct InterpAttribsJobInitInfo {
	const InPieceArr *pInPieces;
	const InPieceArr *pInPiecesClip;
	const HTable *pMergeTable;
	const BufOutRangeTable *pBufOutTable;
	const OutBufIdxArr *pOutBufIdxArr;
	StucDomain domain;
} InterpAttribsJobInitInfo;

static
I32 interpAttribsJobsGetRange(const MapToMeshBasic *pBasic, void *pInitInfo) {
	return stucDomainCountGetIntern(
		&pBasic->outMesh.core,
		((InterpAttribsJobInitInfo *)pInitInfo)->domain
	);
}

static
void interpAttribsJobInit(MapToMeshBasic *pBasic, void *pInitInfoVoid, void *pEntryVoid) {
	InterpAttribsJobArgs *pEntry = pEntryVoid;
	InterpAttribsJobInitInfo *pInitInfo = pInitInfoVoid;
	pEntry->pOutMesh = &pBasic->outMesh;
	pEntry->pInPieces = pInitInfo->pInPieces;
	pEntry->pInPiecesClip = pInitInfo->pInPiecesClip;
	pEntry->pMergeTable = pInitInfo->pMergeTable;
	pEntry->pBufOutTable = pInitInfo->pBufOutTable;
	pEntry->pOutBufIdxArr = pInitInfo->pOutBufIdxArr;
}

StucErr stucInterpAttribs(
	MapToMeshBasic *pBasic,
	const InPieceArr *pInPieces,
	const InPieceArr *pInPiecesClip,
	HTable *pMergeTable,
	const BufOutRangeTable *pBufOutTable,
	const OutBufIdxArr *pOutBufIdxArr,
	StucDomain domain,
	StucErr (* job)(void *)
) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 jobCount = 0;
	InterpAttribsJobArgs jobArgs[PIX_THREAD_MAX_SUB_MAPPING_JOBS] = {0};
	stucMakeJobArgs(
		pBasic,
		&jobCount, jobArgs, sizeof(InterpAttribsJobArgs),
		&(InterpAttribsJobInitInfo) {
			.pInPieces = pInPieces,
			.pInPiecesClip = pInPiecesClip,
			.pMergeTable = pMergeTable,
			.pBufOutTable = pBufOutTable,
			.pOutBufIdxArr = pOutBufIdxArr,
			.domain = domain
		},
		interpAttribsJobsGetRange, interpAttribsJobInit
	);
	err = stucDoJobInParallel(
		pBasic,
		jobCount, jobArgs, sizeof(InterpAttribsJobArgs),
		job
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

