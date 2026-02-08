/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <uv_stucco_intern.h>
#include <map.h>
#include <attrib_utils.h>
#include <interp_and_xform.h>

static
void interpCacheUpdateCopyIn(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 inFaceIdx, I32 inCorner,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_COPY_IN;
	FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, inFaceIdx);
	pCache->copyIn.a = inFace.start + inCorner;
	if (domain == STUC_DOMAIN_VERT) {
		pCache->copyIn.a = pBasic->pInMesh->core.pCorners[pCache->copyIn.a];
	}
	pCache->copyIn.inFace = inFace.idx;
}

static
void interpCacheUpdateCopyMap(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 mapFaceIdx, I32 mapCorner,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_COPY_MAP;
	FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaceIdx);
	pCache->copyMap.a = mapFace.start + mapCorner;
	if (domain == STUC_DOMAIN_VERT) {
		pCache->copyMap.a = pBasic->pMap->pMesh->core.pCorners[pCache->copyMap.a];
	}
	pCache->copyMap.mapFace = mapFace;
}

static
void interpCacheUpdateLerpIn(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 inFaceIdx, I32 inCorner,
	F32 t,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_LERP_IN;
	FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, inFaceIdx);
	pCache->lerpIn.a = inFace.start + inCorner;
	pCache->lerpIn.b = inFace.start + stucGetCornerNext(inCorner, &inFace);
	if (domain == STUC_DOMAIN_VERT) {
		pCache->lerpIn.a = pBasic->pInMesh->core.pCorners[pCache->lerpIn.a];
		pCache->lerpIn.b = pBasic->pInMesh->core.pCorners[pCache->lerpIn.b];
	}
	pCache->lerpIn.t = t;
}

static
void interpCacheUpdateLerpMap(
	const MapToMeshBasic *pBasic,
	StucDomain domain,
	I32 mapFaceIdx, I32 mapCorner,
	F32 t,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_LERP_MAP;
	FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaceIdx);
	pCache->lerpMap.a = mapFace.start + mapCorner;
	pCache->lerpMap.b =
		mapFace.start + stucGetCornerNext(mapCorner, &mapFace);
	if (domain == STUC_DOMAIN_VERT) {
		pCache->lerpMap.a = pBasic->pMap->pMesh->core.pCorners[pCache->lerpMap.a];
		pCache->lerpMap.b = pBasic->pMap->pMesh->core.pCorners[pCache->lerpMap.b];
	}
	pCache->lerpMap.t = t;
}

static
void interpCacheUpdateTriIn(
	const MapToMeshBasic *pBasic,
	V2_I16 tile,
	StucDomain domain,
	I32 mapFaceIdx, I32 mapCorner,
	I32 inFaceIdx,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_TRI_IN;
	FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaceIdx);
	FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, inFaceIdx);
	V2_F32 mapVertPos = *(V2_F32 *)&pBasic->pMap->pMesh->pPos[
		pBasic->pMap->pMesh->core.pCorners[mapFace.start + mapCorner]
	];
	I8 tri[3] = {0};
	pCache->triIn.bc = stucGetBarycentricInFaceFromUvs(
		pBasic->pInMesh,
		&inFace,
		tile,
		tri,
		mapVertPos
	);
	for (I32 i = 0; i < 3; ++i) {
		pCache->triIn.triReal[i] = inFace.start + tri[i];
		if (domain == STUC_DOMAIN_VERT) {
			pCache->triIn.triReal[i] =
				pBasic->pInMesh->core.pCorners[pCache->triIn.triReal[i]];
		}
	}
}

static
void interpCacheUpdateTriMap(
	const MapToMeshBasic *pBasic,
	V2_I16 tile,
	StucDomain domain,
	I32 inFaceIdx, I32 inCorner,
	I32 mapFaceIdx, I8 mapTri,
	InterpCache *pCache
) {
	pCache->active = STUC_INTERP_CACHE_TRI_MAP;
	FaceRange inFace = stucGetFaceRange(&pBasic->pInMesh->core, inFaceIdx);
	FaceRange mapFace = stucGetFaceRange(&pBasic->pMap->pMesh->core, mapFaceIdx);
	const U8 *pTri = stucTriGet(&pBasic->pMap->triCache, mapFace.idx, mapTri);
	V2_F32 fTile = {.d = {(F32)tile.d[0], (F32)tile.d[1]}};
	V2_F32 inUv = pBasic->pInMesh->pUvs[inFace.start + inCorner];
	_(&inUv V2SUBEQL fTile);
	I8 triBuf[3] = {0};
	if (pTri) {
		pCache->triMap.bc =
			stucGetBarycentricInTriFromVerts(pBasic->pMap->pMesh, &mapFace, pTri, inUv);
	}
	else {
		pCache->triMap.bc =
			stucGetBarycentricInFaceFromVerts(pBasic->pMap->pMesh, &mapFace, triBuf, inUv);
		pTri = triBuf;
	}
	for (I32 i = 0; i < 3; ++i) {
		pCache->triMap.triReal[i] = mapFace.start + pTri[i];
		if (domain == STUC_DOMAIN_VERT) {
			pCache->triMap.triReal[i] =
				pBasic->pMap->pMesh->core.pCorners[pCache->triMap.triReal[i]];
		}
	}
}

static
void interpBufVertIn(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	InOrMapVert *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_IN) {
				interpCacheUpdateCopyIn(
					pBasic,
					pInterpCache->domain,
					pVert->in.inFace, pVert->in.inCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyIn.a);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_TRI_MAP) {
				interpCacheUpdateTriMap(
					pBasic,
					pInPiece->pList->tile,
					pInterpCache->domain,
					pVert->in.inFace, pVert->in.inCorner,
					pInPiece->pList->mapFace, pVert->in.tri,
					&pInterpCache->cache
				);
			}
			stucTriInterpolateAttrib(
				pDest, iDest,
				pSrc,
				pInterpCache->cache.triMap.triReal[0],
				pInterpCache->cache.triMap.triReal[1],
				pInterpCache->cache.triMap.triReal[2],
				pInterpCache->cache.triMap.bc
			);
			break;
		default:
			PIX_ERR_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertMap(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	InOrMapVert *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_TRI_IN) {
				interpCacheUpdateTriIn(
					pBasic,
					pInPiece->pList->tile,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->map.mapCorner,
					pVert->map.inFace,
					&pInterpCache->cache
				);
			}
			stucTriInterpolateAttrib(
				pDest, iDest,
				pSrc, 
				pInterpCache->cache.triIn.triReal[0],
				pInterpCache->cache.triIn.triReal[1],
				pInterpCache->cache.triIn.triReal[2],
				pInterpCache->cache.triIn.bc
			);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_MAP) {
				interpCacheUpdateCopyMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->map.mapCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyMap.a);
			break;
		default:
			PIX_ERR_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertEdgeIn(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	BufVertOnEdge *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_IN) {
				interpCacheUpdateCopyIn(
					pBasic,
					pInterpCache->domain,
					pVert->in.inFace, pVert->in.inCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyIn.a);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_LERP_MAP) {
				interpCacheUpdateLerpMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->in.mapCorner,
					pVert->in.tMapEdge,
					&pInterpCache->cache
				);
			}
			stucLerpAttrib(
				pDest, iDest,
				pSrc, pInterpCache->cache.lerpMap.a,
				pSrc, pInterpCache->cache.lerpMap.b,
				pVert->in.tMapEdge
			);
			break;
		default:
			PIX_ERR_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertEdgeMap(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	BufVertOnEdge *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_LERP_IN) {
				interpCacheUpdateLerpIn(
					pBasic,
					pInterpCache->domain,
					pVert->map.inFace, pVert->map.inCorner,
					pVert->map.tInEdge,
					&pInterpCache->cache
				);
			}
			stucLerpAttrib(
				pDest, iDest,
				pSrc, pInterpCache->cache.lerpIn.a,
				pSrc, pInterpCache->cache.lerpIn.b,
				pVert->map.tInEdge
			);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_MAP) {
				interpCacheUpdateCopyMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->map.mapCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyMap.a);
			break;
		default:
			PIX_ERR_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertOverlap(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	OverlapVert *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_IN) {
				interpCacheUpdateCopyIn(
					pBasic,
					pInterpCache->domain,
					pVert->inFace, pVert->inCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyIn.a);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_COPY_MAP) {
				interpCacheUpdateCopyMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->mapCorner,
					&pInterpCache->cache
				);
			}
			stucCopyAttribCore(pDest, iDest, pSrc, pInterpCache->cache.copyMap.a);
			break;
		default:
			PIX_ERR_ASSERT("invalid origin override", false);
		}
	}
}

static
void interpBufVertIntersect(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	IntersectVert *pVert,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	switch (pInterpCache->origin) {
		case STUC_ATTRIB_ORIGIN_MESH_IN: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_LERP_IN) {
				interpCacheUpdateLerpIn(
					pBasic,
					pInterpCache->domain,
					pVert->inFace, pVert->inCorner,
					pVert->tInEdge,
					&pInterpCache->cache
				);
			}
			stucLerpAttrib(
				pDest, iDest,
				pSrc, pInterpCache->cache.lerpIn.a,
				pSrc, pInterpCache->cache.lerpIn.b,
				pVert->tInEdge
			);
			break;
		}
		case STUC_ATTRIB_ORIGIN_MAP: {
			if (pInterpCache->cache.active != STUC_INTERP_CACHE_LERP_MAP) {
				interpCacheUpdateLerpMap(
					pBasic,
					pInterpCache->domain,
					pInPiece->pList->mapFace, pVert->mapCorner,
					pVert->tMapEdge,
					&pInterpCache->cache
				);
			}
			stucLerpAttrib(
				pDest, iDest,
				pSrc, pInterpCache->cache.lerpMap.a,
				pSrc, pInterpCache->cache.lerpMap.b,
				pVert->tMapEdge
			);
			break;
		default:
			PIX_ERR_ASSERT("invalid origin override", false);
		}
	}
}

void stucInterpBufAttrib(
	const MapToMeshBasic *pBasic,
	const InPiece *pInPiece,
	const BufMesh *pBufMesh,
	FaceCorner corner,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	InterpCacheLimited *pInterpCache
) {
	StucDomain domain = pInterpCache->domain;
	AttribOrigin origin = pInterpCache->origin;
	PIX_ERR_ASSERT(
		"not interpolating face attribs",
		domain == STUC_DOMAIN_CORNER || domain == STUC_DOMAIN_VERT
	);
	PIX_ERR_ASSERT(
		"only in or map are valid for origin override",
		origin == STUC_ATTRIB_ORIGIN_MESH_IN || origin == STUC_ATTRIB_ORIGIN_MAP
	);
	BufFace bufFace = pBufMesh->faces.pArr[corner.face];
	BufCorner bufCorner = pBufMesh->corners.pArr[bufFace.start + corner.corner];
	switch (bufCorner.type) {
		case STUC_BUF_VERT_IN_OR_MAP: {
			InOrMapVert *pVert = pBufMesh->inOrMapVerts.pArr + bufCorner.vert;
			switch (pVert->in.type) {
				case STUC_BUF_VERT_SUB_TYPE_IN:
					interpBufVertIn(
						pBasic,
						pInPiece,
						pVert,
						pDest, iDest,
						pSrc,
						pInterpCache
					);
					break;
				case STUC_BUF_VERT_SUB_TYPE_MAP:
					interpBufVertMap(
						pBasic,
						pInPiece,
						pVert,
						pDest, iDest,
						pSrc,
						pInterpCache
					);
					break;
			}
			break;
		}
		case STUC_BUF_VERT_ON_EDGE: {
			BufVertOnEdge *pVert = pBufMesh->onEdgeVerts.pArr + bufCorner.vert;
			switch (pVert->in.type) {
				case STUC_BUF_VERT_SUB_TYPE_EDGE_IN:
					interpBufVertEdgeIn(
						pBasic,
						pInPiece,
						pVert,
						pDest, iDest,
						pSrc,
						pInterpCache
					);
					break;
				case STUC_BUF_VERT_SUB_TYPE_EDGE_MAP:
					interpBufVertEdgeMap(
						pBasic,
						pInPiece,
						pVert,
						pDest, iDest,
						pSrc,
						pInterpCache
					);
					break;
			}
			break;
		}
		case STUC_BUF_VERT_OVERLAP: {
			OverlapVert *pVert = pBufMesh->overlapVerts.pArr + bufCorner.vert;
			interpBufVertOverlap(
				pBasic,
				pInPiece,
				pVert,
				pDest, iDest,
				pSrc,
				pInterpCache
			);
			break;
		}
		case STUC_BUF_VERT_INTERSECT: {
			IntersectVert *pVert = pBufMesh->intersectVerts.pArr + bufCorner.vert;
			interpBufVertIntersect(
				pBasic,
				pInPiece,
				pVert,
				pDest, iDest,
				pSrc,
				pInterpCache
			);
			break;
		}
	}
}