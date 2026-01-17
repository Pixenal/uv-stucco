/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <float.h>

#include <pixenals_math_utils.h>
#include <pixenals_error_utils.h>

#include <attrib_utils.h>
#include <context.h>
#include <mesh.h>

void stucSetDefaultSpAttribNames(StucContext pCtx) {
	strcpy(pCtx->spAttribNames[1], "position");
	strcpy(pCtx->spAttribNames[2], "UVMap");
	strcpy(pCtx->spAttribNames[3], "normal");
	strcpy(pCtx->spAttribNames[4], "StucPreserve");
	strcpy(pCtx->spAttribNames[5], "StucPreserveReceive");
	strcpy(pCtx->spAttribNames[6], "StucPreserveVert");
	strcpy(pCtx->spAttribNames[7], "StucUsg");
	strcpy(pCtx->spAttribNames[8], "StucTangent");
	strcpy(pCtx->spAttribNames[9], "StucTSign");
	strcpy(pCtx->spAttribNames[10], "StucWScale");
	strcpy(pCtx->spAttribNames[11], "StucMaterialIndices");//TODO StucMatIdx is easier to type
	strcpy(pCtx->spAttribNames[12], "StucEdgeLen");
	strcpy(pCtx->spAttribNames[13], "StucSeamEdge");
	strcpy(pCtx->spAttribNames[14], "StucSeamVert");
	strcpy(pCtx->spAttribNames[15], "StucNumAdjPreserve");
	strcpy(pCtx->spAttribNames[16], "StucEdgeFaces");
	strcpy(pCtx->spAttribNames[17], "StucEdgeCorners");
	strcpy(pCtx->spAttribNames[18], "StucVertNormals");
}

void stucSetDefaultSpAttribDomains(StucContext pCtx) {
	pCtx->spAttribDomains[1] = STUC_DOMAIN_VERT;
	pCtx->spAttribDomains[2] = STUC_DOMAIN_CORNER;
	pCtx->spAttribDomains[3] = STUC_DOMAIN_CORNER;
	pCtx->spAttribDomains[4] = STUC_DOMAIN_EDGE;
	pCtx->spAttribDomains[5] = STUC_DOMAIN_EDGE;
	pCtx->spAttribDomains[6] = STUC_DOMAIN_VERT;
	pCtx->spAttribDomains[7] = STUC_DOMAIN_VERT;
	pCtx->spAttribDomains[8] = STUC_DOMAIN_CORNER;
	pCtx->spAttribDomains[9] = STUC_DOMAIN_CORNER;
	pCtx->spAttribDomains[10] = STUC_DOMAIN_VERT;
	pCtx->spAttribDomains[11] = STUC_DOMAIN_FACE;
	pCtx->spAttribDomains[12] = STUC_DOMAIN_EDGE;
	pCtx->spAttribDomains[13] = STUC_DOMAIN_EDGE;
	pCtx->spAttribDomains[14] = STUC_DOMAIN_VERT;
	pCtx->spAttribDomains[15] = STUC_DOMAIN_VERT;
	pCtx->spAttribDomains[16] = STUC_DOMAIN_EDGE;
	pCtx->spAttribDomains[17] = STUC_DOMAIN_EDGE;
	pCtx->spAttribDomains[18] = STUC_DOMAIN_VERT;
}

void stucSetDefaultSpAttribTypes(StucContext pCtx) {
	pCtx->spAttribTypes[1] = STUC_ATTRIB_V3_F32;
	pCtx->spAttribTypes[2] = STUC_ATTRIB_V2_F32;
	pCtx->spAttribTypes[3] = STUC_ATTRIB_V3_F32;
	pCtx->spAttribTypes[4] = STUC_ATTRIB_I8;
	pCtx->spAttribTypes[5] = STUC_ATTRIB_I8;
	pCtx->spAttribTypes[6] = STUC_ATTRIB_I8;
	pCtx->spAttribTypes[7] = STUC_ATTRIB_I32;
	pCtx->spAttribTypes[8] = STUC_ATTRIB_V3_F32;
	pCtx->spAttribTypes[9] = STUC_ATTRIB_F32;
	pCtx->spAttribTypes[10] = STUC_ATTRIB_F32;
	pCtx->spAttribTypes[11] = STUC_ATTRIB_I8;
	pCtx->spAttribTypes[12] = STUC_ATTRIB_F32;
	pCtx->spAttribTypes[13] = STUC_ATTRIB_I8;
	pCtx->spAttribTypes[14] = STUC_ATTRIB_I8;
	pCtx->spAttribTypes[15] = STUC_ATTRIB_I8;
	pCtx->spAttribTypes[16] = STUC_ATTRIB_V2_I32;
	pCtx->spAttribTypes[17] = STUC_ATTRIB_V2_I8;
	pCtx->spAttribTypes[18] = STUC_ATTRIB_V3_F32;
}

#define CLAMP(a, min, max) (a <= min ? min : (a > max ? max : a))

/*
static
F64 clamp(F64 a, F64 min, F64 max) {
	return CLAMP(a, min, max);
}
*/

static
void fBlendReplace(F64 *pDest, F64 a, F64 b) {
	*pDest = b;
}
static
void iBlendReplace(I64 *pDest, I64 a, I64 b) {
	*pDest = b;
}

static
void fBlendMultiply(F64 *pDest, F64 a, F64 b) {
	*pDest = a * b;
}
static
void iBlendMultiply(I64 *pDest, I64 a, I64 b) {
	*pDest = a * b;
}

static
void fBlendDivide(F64 *pDest, F64 a, F64 b) {
	*pDest = a / b;
}
static
void iBlendDivide(I64 *pDest, I64 a, I64 b) {
	*pDest = a / b;
}

static
void fBlendAdd(F64 *pDest, F64 a, F64 b) {
	*pDest = a + b;
}
static
void iBlendAdd(I64 *pDest, I64 a, I64 b) {
	*pDest = a + b;
}

//TODO addsub result is slightly off from other programs
static
void fBlendSubtract(F64 *pDest, F64 a, F64 b) {
	*pDest = a - b;
}
static
void iBlendSubtract(I64 *pDest, I64 a, I64 b) {
	*pDest = a - b;
}

//TODO addsub result is slightly off from other programs
static
void fBlendAddSub(F64 *pDest, F64 a, F64 b) {
	*pDest = a + b - (1 - b);
}

static
void fBlendLighten(F64 *pDest, F64 a, F64 b) {
	*pDest = PIXM_MAX(a, b);
}
static
void iBlendLighten(I64 *pDest, I64 a, I64 b) {
	*pDest = PIXM_MAX(a, b);
}

static
void fBlendDarken(F64 *pDest, F64 a, F64 b) {
	*pDest = PIXM_MIN(a, b);
}
static
void iBlendDarken(I64 *pDest, I64 a, I64 b) {
	*pDest = PIXM_MIN(a, b);
}
	
static
void fBlendOverlay(F64 *pDest, F64 a, F64 b) {
	*pDest = a < .5 ?
		2.0 * a * b :
		1.0 - 2.0 * (1.0 - a) * (1.0 - b);
}
	
static
void fBlendSoftLight(F64 *pDest, F64 a, F64 b) {
	*pDest = b < .5 ?
		2.0 * a * b + a * a * (1.0 - 2.0 * b) :
		2.0 * a * (1.0 - b) + sqrt(a) * (2.0 * b - 1.0);
}

static
void fBlendColorDodge(F64 *pDest, F64 a, F64 b) {
	*pDest = (1.0 - b);
	*pDest = *pDest == .0 ? 1.0 : a / *pDest;
}

#define INDEX_ATTRIB(t, pDest, idx, vec, comp)\
	((t (*)[vec])pDest->pData)[idx][comp]

#define LERP_SIMPLE(a, b, o) (b * o + (1.0 - o) * a)

#define DIVIDE_BY_SCALAR(t, pAttrib, idx, v, c, scalar) \
	INDEX_ATTRIB(t,pAttrib,idx,v,c) /= (t)scalar;

#define LERP_SCALAR(t, pD, iD, pA, iA, pB, iB, a)\
	INDEX_ATTRIB(t, pD, iD, 1, 0) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 1, 0), (F32)INDEX_ATTRIB(t, pB, iB, 1, 0), a));

#define LERP_V2(t, pD, iD, pA, iA, pB, iB, a)\
	INDEX_ATTRIB(t, pD, iD, 2, 0) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 2, 0), (F32)INDEX_ATTRIB(t, pB, iB, 2, 0), a));\
	INDEX_ATTRIB(t, pD, iD, 2, 1) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 2, 1), (F32)INDEX_ATTRIB(t, pB, iB, 2, 1), a));

#define LERP_V3(t, pD, iD, pA, iA, pB, iB, a)\
	INDEX_ATTRIB(t, pD, iD, 3, 0) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 3, 0), (F32)INDEX_ATTRIB(t, pB, iB, 3, 0), a));\
	INDEX_ATTRIB(t, pD, iD, 3, 1) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 3, 1), (F32)INDEX_ATTRIB(t, pB, iB, 3, 1), a));\
	INDEX_ATTRIB(t, pD, iD, 3, 2) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 3, 2), (F32)INDEX_ATTRIB(t, pB, iB, 3, 2), a));

#define LERP_V4(t, pD, iD, pA, iA, pB, iB, a)\
	INDEX_ATTRIB(t, pD, iD, 4, 0) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 4, 0), (F32)INDEX_ATTRIB(t, pB, iB, 4, 0), a));\
	INDEX_ATTRIB(t, pD, iD, 4, 1) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 4, 1), (F32)INDEX_ATTRIB(t, pB, iB, 4, 1), a));\
	INDEX_ATTRIB(t, pD, iD, 4, 2) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 4, 2), (F32)INDEX_ATTRIB(t, pB, iB, 4, 2), a));\
	INDEX_ATTRIB(t, pD, iD, 4, 3) = (t)(LERP_SIMPLE((F32)INDEX_ATTRIB(t, pA, iA, 4, 3), (F32)INDEX_ATTRIB(t, pB, iB, 4, 3), a));

#define TRI_INTERPOLATE_SCALAR(t, pD, iD, pS, iA, iB, iC, bc)\
	INDEX_ATTRIB(t, pD, iD, 1, 0) = (t)((F32)INDEX_ATTRIB(t, pS, iA, 1, 0) * bc.d[0]);\
	INDEX_ATTRIB(t, pD, iD, 1, 0) += (t)((F32)INDEX_ATTRIB(t, pS, iB, 1, 0) * bc.d[1]);\
	INDEX_ATTRIB(t, pD, iD, 1, 0) += (t)((F32)INDEX_ATTRIB(t, pS, iC, 1, 0) * bc.d[2]);\
	INDEX_ATTRIB(t, pD, iD, 1, 0) = (t)((F32)INDEX_ATTRIB(t, pD, iD, 1, 0) / (bc.d[0] + bc.d[1] + bc.d[2]));

#define TRI_INTERPOLATE_V2(t, pD, iD, pS, iA, iB, iC, bc) {\
	INDEX_ATTRIB(t,pD,iD,2,0) = (t)((F32)INDEX_ATTRIB(t,pS,iA,2,0) * bc.d[0]);\
	INDEX_ATTRIB(t,pD,iD,2,1) = (t)((F32)INDEX_ATTRIB(t,pS,iA,2,1) * bc.d[0]);\
	INDEX_ATTRIB(t,pD,iD,2,0) += (t)((F32)INDEX_ATTRIB(t,pS,iB,2,0) * bc.d[1]);\
	INDEX_ATTRIB(t,pD,iD,2,1) += (t)((F32)INDEX_ATTRIB(t,pS,iB,2,1) * bc.d[1]);\
	INDEX_ATTRIB(t,pD,iD,2,0) += (t)((F32)INDEX_ATTRIB(t,pS,iC,2,0) * bc.d[2]);\
	INDEX_ATTRIB(t,pD,iD,2,1) += (t)((F32)INDEX_ATTRIB(t,pS,iC,2,1) * bc.d[2]);\
	F32 sum = bc.d[0] + bc.d[1] + bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,2,0) = (t)((F32)INDEX_ATTRIB(t,pD,iD,2,0) / sum);\
	INDEX_ATTRIB(t,pD,iD,2,1) = (t)((F32)INDEX_ATTRIB(t,pD,iD,2,1) / sum);\
}

#define TRI_INTERPOLATE_V3(t, pD, iD, pS, iA, iB, iC, bc) {\
	INDEX_ATTRIB(t,pD,iD,3,0) = (t)((F32)INDEX_ATTRIB(t,pS,iA,3,0) * bc.d[0]);\
	INDEX_ATTRIB(t,pD,iD,3,1) = (t)((F32)INDEX_ATTRIB(t,pS,iA,3,1) * bc.d[0]);\
	INDEX_ATTRIB(t,pD,iD,3,2) = (t)((F32)INDEX_ATTRIB(t,pS,iA,3,2) * bc.d[0]);\
	INDEX_ATTRIB(t,pD,iD,3,0) += (t)((F32)INDEX_ATTRIB(t,pS,iB,3,0) * bc.d[1]);\
	INDEX_ATTRIB(t,pD,iD,3,1) += (t)((F32)INDEX_ATTRIB(t,pS,iB,3,1) * bc.d[1]);\
	INDEX_ATTRIB(t,pD,iD,3,2) += (t)((F32)INDEX_ATTRIB(t,pS,iB,3,2) * bc.d[1]);\
	INDEX_ATTRIB(t,pD,iD,3,0) += (t)((F32)INDEX_ATTRIB(t,pS,iC,3,0) * bc.d[2]);\
	INDEX_ATTRIB(t,pD,iD,3,1) += (t)((F32)INDEX_ATTRIB(t,pS,iC,3,1) * bc.d[2]);\
	INDEX_ATTRIB(t,pD,iD,3,2) += (t)((F32)INDEX_ATTRIB(t,pS,iC,3,2) * bc.d[2]);\
	F32 sum = bc.d[0] + bc.d[1] + bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,3,0) = (t)((F32)INDEX_ATTRIB(t,pD,iD,3,0) / sum);\
	INDEX_ATTRIB(t,pD,iD,3,1) = (t)((F32)INDEX_ATTRIB(t,pD,iD,3,1) / sum);\
	INDEX_ATTRIB(t,pD,iD,3,2) = (t)((F32)INDEX_ATTRIB(t,pD,iD,3,2) / sum);\
}

#define TRI_INTERPOLATE_V4(t, pD, iD, pS, iA, iB, iC, bc) {\
	INDEX_ATTRIB(t,pD,iD,4,0) = (t)((F32)INDEX_ATTRIB(t,pS,iA,4,0) * bc.d[0]);\
	INDEX_ATTRIB(t,pD,iD,4,1) = (t)((F32)INDEX_ATTRIB(t,pS,iA,4,1) * bc.d[0]);\
	INDEX_ATTRIB(t,pD,iD,4,2) = (t)((F32)INDEX_ATTRIB(t,pS,iA,4,2) * bc.d[0]);\
	INDEX_ATTRIB(t,pD,iD,4,3) = (t)((F32)INDEX_ATTRIB(t,pS,iA,4,3) * bc.d[0]);\
	INDEX_ATTRIB(t,pD,iD,4,0) += (t)((F32)INDEX_ATTRIB(t,pS,iB,4,0) * bc.d[1]);\
	INDEX_ATTRIB(t,pD,iD,4,1) += (t)((F32)INDEX_ATTRIB(t,pS,iB,4,1) * bc.d[1]);\
	INDEX_ATTRIB(t,pD,iD,4,2) += (t)((F32)INDEX_ATTRIB(t,pS,iB,4,2) * bc.d[1]);\
	INDEX_ATTRIB(t,pD,iD,4,3) += (t)((F32)INDEX_ATTRIB(t,pS,iB,4,3) * bc.d[1]);\
	INDEX_ATTRIB(t,pD,iD,4,0) += (t)((F32)INDEX_ATTRIB(t,pS,iC,4,0) * bc.d[2]);\
	INDEX_ATTRIB(t,pD,iD,4,1) += (t)((F32)INDEX_ATTRIB(t,pS,iC,4,1) * bc.d[2]);\
	INDEX_ATTRIB(t,pD,iD,4,2) += (t)((F32)INDEX_ATTRIB(t,pS,iC,4,2) * bc.d[2]);\
	INDEX_ATTRIB(t,pD,iD,4,3) += (t)((F32)INDEX_ATTRIB(t,pS,iC,4,3) * bc.d[2]);\
	F32 sum = bc.d[0] + bc.d[1] + bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,4,0) = (t)((F32)INDEX_ATTRIB(t,pD,iD,4,0) / sum);\
	INDEX_ATTRIB(t,pD,iD,4,1) = (t)((F32)INDEX_ATTRIB(t,pD,iD,4,1) / sum);\
	INDEX_ATTRIB(t,pD,iD,4,2) = (t)((F32)INDEX_ATTRIB(t,pD,iD,4,2) / sum);\
	INDEX_ATTRIB(t,pD,iD,4,3) = (t)((F32)INDEX_ATTRIB(t,pD,iD,4,3) / sum);\
}

StucErr stucAssignActiveAliases(
	StucContext pCtx,
	Mesh *pMesh,
	UBitField32 flags,
	StucDomain domain
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, pCtx && pMesh, "");
	for (I32 i = 1; i < STUC_ATTRIB_USE_SP_ENUM_COUNT; ++i) {
		if (!(flags >> i & 0x1) ||
			(domain != STUC_DOMAIN_NONE && domain != pCtx->spAttribDomains[i])) {
			continue;
		}
		Attrib *pAttrib = stucGetActiveAttrib(pCtx, &pMesh->core, i);
		if (!pAttrib) {
			continue;
		}
		switch (i) {
			case STUC_ATTRIB_USE_POS:
				pMesh->pPos = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_UV:
				pMesh->pUvs = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_NORMAL:
				pMesh->pNormals = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_PRESERVE_EDGE:
				pMesh->pEdgePreserve = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_RECEIVE:
				pMesh->pEdgeReceive = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_PRESERVE_VERT:
				pMesh->pVertPreserve = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_USG:
				pMesh->pUsg = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_TANGENT:
				pMesh->pTangents = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_TSIGN:
				pMesh->pTSigns = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_WSCALE:
				pMesh->pWScale = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_IDX:
				pMesh->pMatIdx = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_EDGE_LEN:
				pMesh->pEdgeLen = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_SEAM_EDGE:
				pMesh->pSeamEdge = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_SEAM_VERT:
				pMesh->pSeamVert = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_NUM_ADJ_PRESERVE:
				pMesh->pNumAdjPreserve = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_EDGE_FACES:
				pMesh->pEdgeFaces = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_EDGE_CORNERS:
				pMesh->pEdgeCorners = pAttrib->core.pData;
				break;
			case STUC_ATTRIB_USE_NORMALS_VERT:
				pMesh->pVertNormals = pAttrib->core.pData;
				break;
			default:
				PIX_ERR_ASSERT("outside use special range", false);
		}
	}
	return err;
}

Attrib *stucGetActiveAttrib(StucContext pCtx, StucMesh *pMesh, StucAttribUse use) {
	PIX_ERR_ASSERT("", use >= 0);
	if (use == STUC_ATTRIB_USE_NONE ||
		use == STUC_ATTRIB_USE_SP_ENUM_COUNT ||
		use >= STUC_ATTRIB_USE_ENUM_COUNT
	) {
		return NULL;
	}
	AttribActive idx = pMesh->activeAttribs[use];
	if (!idx.active) {
		return NULL;
	}
	AttribArray *pArr = stucGetAttribArrFromDomain(pMesh, idx.domain);
	PIX_ERR_ASSERT("", idx.idx < pArr->count);
	return pArr->pArr + idx.idx;
}

const Attrib *stucGetActiveAttribConst(
	StucContext pCtx,
	const StucMesh *pMesh,
	StucAttribUse use
) {
	return stucGetActiveAttrib(pCtx, (StucMesh *)pMesh, use);
}

bool stucIsAttribActive(
	const StucContext pCtx,
	const StucMesh *pMesh,
	const Attrib *pAttrib
) {
	return pAttrib == stucGetActiveAttribConst(pCtx, pMesh, pAttrib->core.use);
}

bool stucIsAttribIdxActive(const StucMesh *pMesh, const AttribArray *pArr, I32 idx) {
	Attrib *pAttrib = pArr->pArr + idx;
	if (pAttrib->core.use == STUC_ATTRIB_USE_NONE ||
		pAttrib->core.use == STUC_ATTRIB_USE_SP_ENUM_COUNT ||
		pAttrib->core.use >= STUC_ATTRIB_USE_ENUM_COUNT
	) {
		return false;
	}
	AttribActive activeIdx = pMesh->activeAttribs[pAttrib->core.use];
	return activeIdx.active && activeIdx.idx == idx;
}

Attrib *stucGetAttribIntern(
	const char *pName,
	AttribArray *pAttribs,
	bool excludeActive,
	const StucContext pCtx,
	const StucMesh *pMesh,
	I32 *pIdx
) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		PIX_ERR_ASSERT(
			"if excludeActive is true, pCtx and pMesh must not be NULL",
			((excludeActive << 1) | (pCtx && pMesh)) != 0x2
		);
		if (excludeActive && stucIsAttribIdxActive(pMesh, pAttribs, i)) {
			continue;
		}
		if (!strncmp(
			pName,
			pAttribs->pArr[i].core.name,
			STUC_ATTRIB_NAME_MAX_LEN)
			) {

			if (pIdx) {
				*pIdx = i;
			}
			return pAttribs->pArr + i;
		}
	}
	return NULL;
}

const Attrib *stucGetAttribInternConst(
	const char *pName,
	const AttribArray *pAttribs,
	bool excludeActive,
	const StucContext pCtx,
	const StucMesh *pMesh,
	I32 *pIdx
) {
	return stucGetAttribIntern(pName, (AttribArray *)pAttribs, excludeActive, pCtx, pMesh, NULL);
}

void stucSetAttribIdxActive(
	StucMesh *pMesh,
	I32 idx,
	StucAttribUse use,
	StucDomain domain
) {
	PIX_ERR_ASSERT("", use >= 0);
	if (use != STUC_ATTRIB_USE_NONE &&
		use != STUC_ATTRIB_USE_SP_ENUM_COUNT &&
		use < STUC_ATTRIB_USE_ENUM_COUNT
	) {
		pMesh->activeAttribs[use].active = true;
		pMesh->activeAttribs[use].idx = (I16)idx;
		pMesh->activeAttribs[use].domain = domain;
	}
}

void stucSetTypeDefaultConfig(StucContext pCtx) {
	StucTypeDefaultConfig config = {0};
	config.i8.blendConfig.opacity = 1.0f;
	config.i16.blendConfig.opacity = 1.0f;
	config.i32.blendConfig.opacity = 1.0f;
	config.i64.blendConfig.opacity = 1.0f;
	config.f32.blendConfig.opacity = 1.0f;
	config.f64.blendConfig.opacity = 1.0f;
	config.v2_i8.blendConfig.opacity = 1.0f;
	config.v2_i16.blendConfig.opacity = 1.0f;
	config.v2_i32.blendConfig.opacity = 1.0f;
	config.v2_i64.blendConfig.opacity = 1.0f;
	config.v2_f32.blendConfig.opacity = 1.0f;
	config.v2_f64.blendConfig.opacity = 1.0f;
	config.v3_i8.blendConfig.opacity = 1.0f;
	config.v3_i16.blendConfig.opacity = 1.0f;
	config.v3_i32.blendConfig.opacity = 1.0f;
	config.v3_i64.blendConfig.opacity = 1.0f;
	config.v3_f32.blendConfig.opacity = 1.0f;
	config.v3_f64.blendConfig.opacity = 1.0f;
	config.v4_i8.blendConfig.opacity = 1.0f;
	config.v4_i16.blendConfig.opacity = 1.0f;
	config.v4_i32.blendConfig.opacity = 1.0f;
	config.v4_i64.blendConfig.opacity = 1.0f;
	config.v4_f32.blendConfig.opacity = 1.0f;
	config.v4_f64.blendConfig.opacity = 1.0f;
	config.string.blendConfig.opacity = 1.0f;
	pCtx->typeDefaults = config;
}

StucTypeDefault *stucGetTypeDefaultConfig(
	StucTypeDefaultConfig *pConfig,
	AttribType type
) {
	switch (type) {
		case STUC_ATTRIB_I8:
			return &pConfig->i8;
		case STUC_ATTRIB_I16:
			return &pConfig->i16;
		case STUC_ATTRIB_I32:
			return &pConfig->i32;
		case STUC_ATTRIB_I64:
			return &pConfig->i64;
		case STUC_ATTRIB_F32:
			return &pConfig->f32;
		case STUC_ATTRIB_F64:
			return &pConfig->f64;
		case STUC_ATTRIB_V2_I8:
			return &pConfig->v2_i8;
		case STUC_ATTRIB_V2_I16:
			return &pConfig->v2_i16;
		case STUC_ATTRIB_V2_I32:
			return &pConfig->v2_i32;
		case STUC_ATTRIB_V2_I64:
			return &pConfig->v2_i64;
		case STUC_ATTRIB_V2_F32:
			return &pConfig->v2_f32;
		case STUC_ATTRIB_V2_F64:
			return &pConfig->v2_f64;
		case STUC_ATTRIB_V3_I8:
			return &pConfig->v3_i8;
		case STUC_ATTRIB_V3_I16:
			return &pConfig->v3_i16;
		case STUC_ATTRIB_V3_I32:
			return &pConfig->v3_i32;
		case STUC_ATTRIB_V3_I64:
			return &pConfig->v3_i64;
		case STUC_ATTRIB_V3_F32:
			return &pConfig->v3_f32;
		case STUC_ATTRIB_V3_F64:
			return &pConfig->v3_f64;
		case STUC_ATTRIB_V4_I8:
			return &pConfig->v4_i8;
		case STUC_ATTRIB_V4_I16:
			return &pConfig->v4_i16;
		case STUC_ATTRIB_V4_I32:
			return &pConfig->v4_i32;
		case STUC_ATTRIB_V4_I64:
			return &pConfig->v4_i64;
		case STUC_ATTRIB_V4_F32:
			return &pConfig->v4_f32;
		case STUC_ATTRIB_V4_F64:
			return &pConfig->v4_f64;
		case STUC_ATTRIB_STRING:
			return &pConfig->string;
		default:
			PIX_ERR_ASSERT("", false);
			return 0;
	}
}

const StucBlendOpt *stucGetBlendOpt(
	const StucBlendOptArr *pOptArr,
	I32 attribIdx,
	StucDomain domain
) {
	for (I32 i = 0; i < pOptArr[domain].count; ++i) {
		if (pOptArr[domain].pArr[i].attrib == attribIdx) {
			return pOptArr[domain].pArr + i;
		}
	}
	return NULL;
}

//TODO replace manual searches of indexed attribs with this func
AttribIndexed *stucGetAttribIndexedIntern(
	AttribIndexedArr *pAttribArr,
	const char *pName
) {
	for (I32 i = 0; i < pAttribArr->count; ++i) {
		AttribIndexed *pAttrib = pAttribArr->pArr + i;
		if (!strncmp(pName, pAttrib->core.name, STUC_ATTRIB_NAME_MAX_LEN)) {
			return pAttrib;
		}
	}
	return NULL;
}

AttribIndexed *stucGetAttribIndexedInternConst(
	const AttribIndexedArr *pAttribArr,
	const char *pName
) {
	return stucGetAttribIndexedIntern((AttribIndexedArr *)pAttribArr, pName);
}

void stucLerpAttrib(
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrcA, I32 iSrcA,
	const AttribCore *pSrcB, I32 iSrcB,
	F32 alpha
) {
	//TODO remove all uses of abort()
	PIX_ERR_ASSERT(
		"type mismatch in interpolateAttrib",
		pDest->type == pSrcA->type &&
		pDest->type == pSrcB->type
	);
	AttribType type = pDest->type;
	switch (type) {
		case STUC_ATTRIB_I8:
			LERP_SCALAR(I8, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_I16:
			LERP_SCALAR(I16, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_I32:
			LERP_SCALAR(I32, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_I64:
			LERP_SCALAR(I64, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_F32:
			LERP_SCALAR(F32, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_F64:
			LERP_SCALAR(F64, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_I8:
			LERP_V2(I8, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_I16:
			LERP_V2(I16, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_I32:
			LERP_V2(I32, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_I64:
			LERP_V2(I64, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_F32:
			LERP_V2(F32, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_F64:
			LERP_V2(F64, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_I8:
			LERP_V3(I8, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_I16:
			LERP_V3(I16, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_I32:
			LERP_V3(I32, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_I64:
			LERP_V3(I64, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_F32:
			LERP_V3(F32, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_F64:
			LERP_V3(F64, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_I8:
			//TODO using unsigned here temporarily for vert color
			//make a proper set of attrib types for unsigned types
			LERP_V4(U8, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_I16:
			LERP_V4(I16, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_I32:
			LERP_V4(I32, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_I64:
			LERP_V4(I64, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_F32:
			LERP_V4(F32, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_F64:
			LERP_V4(F64, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_STRING:
			break;
		default:
			PIX_ERR_ASSERT("invalid attrib type", false);
	}
}

void stucTriInterpolateAttrib(
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	I32 iSrcA, I32 iSrcB, I32 iSrcC,
	V3_F32 bc
) {
	if (pDest->type != pSrc->type) {
		printf("Type mismatch in interpolateAttrib\n");
		//TODO remove all uses of abort(), and add proper exception handling
		abort();
	}
	AttribType type = pDest->type;
	switch (type) {
		case STUC_ATTRIB_I8:
			TRI_INTERPOLATE_SCALAR(I8, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_I16:
			TRI_INTERPOLATE_SCALAR(I16, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_I32:
			TRI_INTERPOLATE_SCALAR(I32, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_I64:
			TRI_INTERPOLATE_SCALAR(I64, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_F32:
			TRI_INTERPOLATE_SCALAR(F32, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_F64:
			TRI_INTERPOLATE_SCALAR(F64, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_I8:
			TRI_INTERPOLATE_V2(I8, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_I16:
			TRI_INTERPOLATE_V2(I16, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_I32:
			TRI_INTERPOLATE_V2(I32, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_I64:
			TRI_INTERPOLATE_V2(I64, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_F32:
			TRI_INTERPOLATE_V2(F32, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_F64:
			TRI_INTERPOLATE_V2(F64, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_I8:
			TRI_INTERPOLATE_V3(I8, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_I16:
			TRI_INTERPOLATE_V3(I16, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_I32:
			TRI_INTERPOLATE_V3(I32, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_I64:
			TRI_INTERPOLATE_V3(I64, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_F32:
			TRI_INTERPOLATE_V3(F32, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_F64:
			TRI_INTERPOLATE_V3(F64, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_I8:
			//TODO using unsigned here temporarily for vert color
			//make a proper set of attrib types for unsigned types
			TRI_INTERPOLATE_V4(U8, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_I16:
			TRI_INTERPOLATE_V4(I16, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_I32:
			TRI_INTERPOLATE_V4(I32, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_I64:
			TRI_INTERPOLATE_V4(I64, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_F32:
			TRI_INTERPOLATE_V4(F32, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_F64:
			TRI_INTERPOLATE_V4(F64, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_STRING:
			break;
		default:
			PIX_ERR_ASSERT("invalid attrib type", false);
	}
}

I32 stucAttribTypeGetVecSizeIntern(AttribType type) {
	PIX_ERR_ASSERT("invalid type", type >= 0 && type < STUC_ATTRIB_TYPE_ENUM_COUNT);
	PIX_ERR_ASSERT("this func shouldn't be called on a string", type != STUC_ATTRIB_STRING);
	if (type == STUC_ATTRIB_NONE) {
		return 0;
	}
	else if (type < STUC_ATTRIB_V2_I8) {
		return 1;
	}
	else if (type < STUC_ATTRIB_V3_I8) {
		return 2;
	}
	else if (type < STUC_ATTRIB_V4_I8) {
		return 3;
	}
	return 4;
}

static
F64 makeFloatWide(const void *ptr, AttribType type) {
	PIX_ERR_ASSERT("invalid type", type == STUC_ATTRIB_F32 || type == STUC_ATTRIB_F64);
	return type == STUC_ATTRIB_F32 ? (F64)*(F32 *)ptr : *(F64 *)ptr; 
}

static
I64 makeIntWide(const void *ptr, AttribType type, bool isSigned) {
	switch (type) {
		case STUC_ATTRIB_I8:
			return (I64)(isSigned ? *(I8 *)ptr : *(U8 *)ptr);
		case STUC_ATTRIB_I16:
			return (I64)(isSigned ? *(I16 *)ptr : *(U16 *)ptr);
		case STUC_ATTRIB_I32:
			return (I64)(isSigned ? *(I32 *)ptr : *(U32 *)ptr);
		case STUC_ATTRIB_I64:
			return (I64)(isSigned ? *(I64 *)ptr : *(U64 *)ptr);
		default:
			PIX_ERR_ASSERT("invalid attrib type", false);
	}
	PIX_ERR_ASSERT("invalid type", false);
	return 0;
}

AttribType stucAttribGetCompTypeIntern(AttribType type) {
	return (type -  STUC_ATTRIB_I8) % (STUC_ATTRIB_V2_I8 - STUC_ATTRIB_I8);
}

static
I32 getAttribCompTypeSize(AttribType type) {
	if (type >= STUC_ATTRIB_V2_I8) {
		type = stucAttribGetCompTypeIntern(type);
	}
	switch (type) {
		case STUC_ATTRIB_I8:
			return 1;
		case STUC_ATTRIB_I16:
			return 2;
		case STUC_ATTRIB_I32:
			return 4;
		case STUC_ATTRIB_I64:
			return 8;
		case STUC_ATTRIB_F32:
			return 4;
		case STUC_ATTRIB_F64:
			return 8;
		default:
			PIX_ERR_ASSERT("invalid attrib type", false);
	}
	PIX_ERR_ASSERT("invalid type", false);
	return 0;
}

static
bool isAttribTypeFloat(AttribType type) {
	if (type >= STUC_ATTRIB_V2_I8) {
		type = stucAttribGetCompTypeIntern(type);
	}
	return
		type == STUC_ATTRIB_F32 || type == STUC_ATTRIB_F64;
}

static
I64 getIntTypeMax(AttribType type, bool getSigned) {
	if (type >= STUC_ATTRIB_V2_I8) {
		type = stucAttribGetCompTypeIntern(type);
	}
	switch (type) {
		case STUC_ATTRIB_I8:
			return getSigned ? INT8_MAX : UINT8_MAX;
		case STUC_ATTRIB_I16:
			return getSigned ? INT16_MAX : UINT16_MAX;
		case STUC_ATTRIB_I32:
			return getSigned ? INT32_MAX : UINT32_MAX;
		case STUC_ATTRIB_I64:
			return getSigned ? INT64_MAX : UINT64_MAX;
		default:
			PIX_ERR_ASSERT("invalid attrib type", false);
	}
	PIX_ERR_ASSERT("invalid type", false);
	return 0;
}

#define SET_VOID_COMP(tDest ,pDestComp, src)\
	PIX_ERR_ASSERT("invalid type", tDest != STUC_ATTRIB_NONE && tDest < STUC_ATTRIB_V2_I8);\
	switch (tDest) {\
		case STUC_ATTRIB_I8:\
			*(I8 *)pDestComp = (I8)src;\
			break;\
		case STUC_ATTRIB_I16:\
			*(I16 *)pDestComp = (I16)src;\
			break;\
		case STUC_ATTRIB_I32:\
			*(I32 *)pDestComp = (I32)src;\
			break;\
		case STUC_ATTRIB_I64:\
			*(I64 *)pDestComp = (I64)src;\
			break;\
		case STUC_ATTRIB_F32:\
			*(F32 *)pDestComp = (F32)src;\
			break;\
		case STUC_ATTRIB_F64:\
			*(F64 *)pDestComp = (F64)src;\
			break;\
		default:\
			PIX_ERR_ASSERT("invalid type", false);\
	}

#define CALL_BLEND_FUNC(\
	t,\
	pBlendFunc,\
	blendConfig, clampMin, clampMax,\
	pDestComp, destCompType,\
	pAComp, aCompType, aIsFloat, normalizeA, aMax,\
	pBComp, bCompType, bIsFloat, normalizeB, bMax,\
	isSigned\
) {\
	t destBuf = 0;\
	t aComp = (t)(aIsFloat ?\
		makeFloatWide(pAComp, aCompType) : makeIntWide(pAComp, aCompType, isSigned));\
	t bComp = (t)(bIsFloat ?\
		makeFloatWide(pBComp, bCompType) : makeIntWide(pBComp, bCompType, isSigned));\
	if (normalizeA) {\
		PIX_ERR_ASSERT("type must be floating point if normalizing", #t[0] == 'F');\
		aComp /= (t)aMax;\
	}\
	if (normalizeB) {\
		PIX_ERR_ASSERT("type must be floating point if normalizing", #t[0] == 'F');\
		bComp /= (t)bMax;\
	}\
\
	PIX_ERR_ASSERT("", pBlendFunc);\
	pBlendFunc(&destBuf, aComp, bComp);\
\
	if (blendConfig.clamp) {\
		destBuf = CLAMP(destBuf, clampMin, clampMax);\
	}\
	if (blendConfig.opacity != .0 && blendConfig.opacity != 1.0) {\
		/* lerp is done in F64 regardless of t */\
		destBuf = (t)pixmF64Lerp((F64)aComp, (F64)destBuf, (F64)blendConfig.opacity);\
	}\
	SET_VOID_COMP(destCompType, pDestComp, destBuf);\
}

static
void blendComponents(
	void (* pFBlendFunc)(F64 *,F64,F64),
	void (* pIBlendFunc)(I64 *,I64,I64),
	StucBlendConfig blendConfig,
	void *pDest, AttribType destCompType, I32 destVecSize,
	const void *pA, AttribType aCompType, I32 aVecSize, bool normalizeA, I64 aMax,
	const void *pB, AttribType bCompType, I32 bVecSize, bool normalizeB, I64 bMax,
	bool isSigned
) {
	bool aIsFloat = isAttribTypeFloat(aCompType);
	bool bIsFloat = isAttribTypeFloat(bCompType);
	I32 size = PIXM_MIN(PIXM_MIN(aVecSize, bVecSize), destVecSize);
	for (I32 i = 0; i < size; ++i) {
		void *pDestComp = (int8_t *)pDest + getAttribCompTypeSize(destCompType) * i;
		const void *pAComp =
			(int8_t *)pA + getAttribCompTypeSize(aCompType) * i * (aVecSize != 1);
		const void *pBComp =
			(int8_t *)pB + getAttribCompTypeSize(bCompType) * i * (bVecSize != 1);
		if (isAttribTypeFloat(destCompType)) {
			CALL_BLEND_FUNC(
				F64,
				pFBlendFunc,
				blendConfig, blendConfig.fMin, blendConfig.fMax,
				pDestComp, destCompType,
				pAComp, aCompType, aIsFloat, normalizeA, aMax,
				pBComp, bCompType, bIsFloat, normalizeB, bMax,
				isSigned
			)
		}
		else {
			CALL_BLEND_FUNC(
				I64,
				pIBlendFunc,
				blendConfig, blendConfig.iMin, blendConfig.iMax,
				pDestComp, destCompType,
				pAComp, aCompType, aIsFloat, normalizeA, aMax,
				pBComp, bCompType, bIsFloat, normalizeB, bMax,
				isSigned
			)
		}
	}\
}

static
void blendSwitch(
	StucBlendConfig blendConfig,
	UBitField32 blendFlags,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pA, I32 iA, bool normalizeA, I64 aMax,
	const AttribCore *pB, I32 iB, bool normalizeB, I64 bMax,
	bool isSigned
) {
	void *pDestVal = stucAttribAsVoid(pDest, iDest);
	const void *pAVal = stucAttribAsVoidConst(pA, iA);
	const void *pBVal = stucAttribAsVoidConst(pB, iB);
	AttribType destCompType = stucAttribGetCompTypeIntern(pDest->type);
	AttribType aCompType = stucAttribGetCompTypeIntern(pA->type);
	AttribType bCompType = stucAttribGetCompTypeIntern(pB->type);
	I32 destVecSize = stucAttribTypeGetVecSizeIntern(pDest->type);
	I32 aVecSize = stucAttribTypeGetVecSizeIntern(pA->type);
	I32 bVecSize = stucAttribTypeGetVecSizeIntern(pB->type);
	switch (blendConfig.blend) {
		case STUC_BLEND_REPLACE:
			if (blendFlags >> STUC_BLEND_REPLACE & 0x1) {
				blendComponents(
					fBlendReplace,
					iBlendReplace,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_MULTIPLY:
			if (blendFlags >> STUC_BLEND_MULTIPLY & 0x1) {
				blendComponents(
					fBlendMultiply,
					iBlendMultiply,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_DIVIDE:
			if (blendFlags >> STUC_BLEND_DIVIDE & 0x1) {
				blendComponents(
					fBlendDivide,
					iBlendDivide,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_ADD:
			if (blendFlags >> STUC_BLEND_ADD & 0x1) {
				blendComponents(
					fBlendAdd,
					iBlendAdd,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_SUBTRACT:
			if (blendFlags >> STUC_BLEND_SUBTRACT & 0x1) {
				blendComponents(
					fBlendSubtract,
					iBlendSubtract,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_ADD_SUB:
			if (blendFlags >> STUC_BLEND_ADD_SUB & 0x1) {
				blendComponents(
					fBlendAddSub,
					NULL,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_LIGHTEN:
			if (blendFlags >> STUC_BLEND_LIGHTEN & 0x1) {
				blendComponents(
					fBlendLighten,
					iBlendLighten,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_DARKEN:
			if (blendFlags >> STUC_BLEND_DARKEN & 0x1) {
				blendComponents(
					fBlendDarken,
					iBlendDarken,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_OVERLAY:
			if (blendFlags >> STUC_BLEND_OVERLAY & 0x1) {
				blendComponents(
					fBlendOverlay,
					NULL,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_SOFT_LIGHT:
			if (blendFlags >> STUC_BLEND_SOFT_LIGHT & 0x1) {
				blendComponents(
					fBlendSoftLight,
					NULL,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_COLOR_DODGE:
			if (blendFlags >> STUC_BLEND_COLOR_DODGE & 0x1) {
				blendComponents(
					fBlendColorDodge,
					NULL,
					blendConfig,
					pDestVal, destCompType, destVecSize,
					pAVal, aCompType, aVecSize, normalizeA, aMax,
					pBVal, bCompType, bVecSize, normalizeB, bMax,
					isSigned
				);
			}
			break;
		case STUC_BLEND_APPEND:
			if (blendFlags >> STUC_BLEND_APPEND & 0x1) {
				//TODO
			}
			break;
	}
}

static
void blendUseVec(
	StucBlendConfig blendConfig,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pA, I32 iA,
	const AttribCore *pB, I32 iB
) {
	UBitField32 blendFlags = 0x7ff;  //all blends execpt for APPEND
	blendSwitch(blendConfig, blendFlags, pDest, iDest, pA, iA, false, 0, pB, iB, false, 0, true);
}

static
void blendUseIdx(
	StucBlendConfig blendConfig,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pA, I32 iA,
	const AttribCore *pB, I32 iB
) {
	PIX_ERR_ASSERT(
		"this should have been picked up in mesh validation",
		pDest->type == STUC_ATTRIB_I8
	);
	blendConfig.opacity = 1.0f;
	blendConfig.blend = STUC_BLEND_REPLACE;
	//TODO replace literal bitflags with bitshifts enum expressions,
	// these will break code when enum elements are moved around
	UBitField32 blendFlags =  0xc1;  //only replace, lighten, and darken
	blendSwitch(blendConfig, blendFlags, pDest, iDest, pA, iA, false, 0, pB, iB, false, 0, true);
}

static
void blendUseColor(
	StucBlendConfig blendConfig,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pA, I32 iA,
	const AttribCore *pB, I32 iB
) {
	AttribCore *pFDest = pDest;
	I32 iFDest = iDest;
	bool destIsFloat = isAttribTypeFloat(pDest->type);
	I32 destVecSize = 0;
	F64 destBuf[4] = {0};
	AttribCore destBufAttrib = {0};
	if (!destIsFloat) {
		destVecSize = stucAttribTypeGetVecSizeIntern(pDest->type);
		AttribType bufType = destVecSize * (STUC_ATTRIB_V2_I8 - STUC_ATTRIB_I8) - 1;
		destBufAttrib.pData = destBuf;
		destBufAttrib.type = bufType;
		destBufAttrib.use = pDest->use;
		pFDest = &destBufAttrib;
		iFDest = 0;
	}
	UBitField32 blendFlags = 0x7ff;  //all blends execpt for APPEND
	//if attrib is float, we assume it's already normalized
	bool normalizeA = !isAttribTypeFloat(pA->type);
	bool normalizeB = !isAttribTypeFloat(pB->type);
	blendSwitch(
		blendConfig,
		blendFlags,
		pFDest, iFDest,
		pA, iA, normalizeA, normalizeA ? getIntTypeMax(pA->type, false) : 0,
		pB, iB, normalizeB, normalizeB ? getIntTypeMax(pB->type, false) : 0,
		false
	);
	if (!destIsFloat) {
		F64 destMax = (F64)getIntTypeMax(pDest->type, false);
		I64 iDestBuf[4] = {0};
		StucAttribType compType = stucAttribGetCompTypeIntern(pDest->type);
		U8 *pDestVoid = stucAttribAsVoid(pDest, iDest);
		I32 compSize = stucGetAttribSizeIntern(compType);
		for (I32 i = 0; i < destVecSize; ++i) {
			iDestBuf[i] = (I64)(destBuf[i] * destMax);
			memcpy(
				pDestVoid + compSize * i,
				iDestBuf + i,
				compSize
			);
		}
	}
}

static
void blendUseScalar(
	StucBlendConfig blendConfig,
	AttribCore *pDest, I32 iDest,
	const AttribCore *pA, I32 iA,
	const AttribCore *pB, I32 iB
) {
	//replace, multiply, divide, add, subtract, lighten, and darken
	UBitField32 blendFlags = 0xdf;
	blendSwitch(blendConfig, blendFlags, pDest, iDest, pA, iA, false, 0, pB, iB, false, 0, true);
}


//TODO this name should not be plural
void stucBlendAttribs(
	AttribCore *pDest, I32 iDest,
	const AttribCore *pA, I32 iA,
	const AttribCore *pB, I32 iB,
	StucBlendConfig blendConfig
) {
	switch (pDest->use) {
		case STUC_ATTRIB_USE_POS:
			blendUseVec(blendConfig, pDest, iDest, pA, iA, pB, iB);
			break;
		case STUC_ATTRIB_USE_UV:
			blendUseVec(blendConfig, pDest, iDest, pA, iA, pB, iB);
			break;
		case STUC_ATTRIB_USE_NORMAL:
			blendUseVec(blendConfig, pDest, iDest, pA, iA, pB, iB);
			break;
		case STUC_ATTRIB_USE_IDX:
			blendUseIdx(blendConfig, pDest, iDest, pA, iA, pB, iB);
			break;
		case STUC_ATTRIB_USE_COLOR:
			blendUseColor(blendConfig, pDest, iDest, pA, iA, pB, iB);
			break;
		case STUC_ATTRIB_USE_MASK:
			blendUseIdx(blendConfig, pDest, iDest, pA, iA, pB, iB);
			break;
		case STUC_ATTRIB_USE_SCALAR:
			blendUseScalar(blendConfig, pDest, iDest, pA, iA, pB, iB);
			break;
		default:
			//blending not currently supported for this use - copying instead
			//TODO add warning
			stucCopyAttribCore(pDest, iDest, pA, iB);
	}
}

void stucDivideAttribByScalarInt(AttribCore *pAttrib, I32 idx, U64 scalar) {
	switch (pAttrib->type) {
		case STUC_ATTRIB_I8:
			DIVIDE_BY_SCALAR(I8, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_I16:
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_I32:
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_I64:
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_F32:
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_F64:
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_V2_I8:
			DIVIDE_BY_SCALAR(I8, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(I8, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_I16:
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_I32:
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_I64:
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_F32:
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_F64:
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V3_I8:
			DIVIDE_BY_SCALAR(I8, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(I8, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(I8, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_I16:
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_I32:
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_I64:
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_F32:
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_F64:
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V4_I8:
			DIVIDE_BY_SCALAR(U8, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(U8, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(U8, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(U8, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_I16:
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(I16, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_I32:
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(I32, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_I64:
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(I64, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_F32:
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(F32, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_F64:
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(F64, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_STRING:
			PIX_ERR_ASSERT("Can't divide a string by 1", false);
			break;
		default:
			PIX_ERR_ASSERT("invalid attrib type", false);
	}
}

AttribArray *stucGetAttribArrFromDomain(StucMesh *pMesh, StucDomain domain) {
	switch (domain) {
		case STUC_DOMAIN_FACE:
			return &pMesh->faceAttribs;
		case STUC_DOMAIN_CORNER:
			return &pMesh->cornerAttribs;
		case STUC_DOMAIN_EDGE:
			return &pMesh->edgeAttribs;
		case STUC_DOMAIN_VERT:
			return &pMesh->vertAttribs;
		case STUC_DOMAIN_MESH:
			return &pMesh->meshAttribs;
		default:
			PIX_ERR_ASSERT("invalid domain", false);
			return NULL;
	}
}

const AttribArray *stucGetAttribArrFromDomainConst(const StucMesh *pMesh, StucDomain domain) {
	return stucGetAttribArrFromDomain((StucMesh *)pMesh, domain);
}

StucErr stucGetMatchingAttrib(
	StucContext pCtx,
	StucMesh *pDest, AttribArray *pDestAttribArr,
	const StucMesh *pSrc, const Attrib *pSrcAttrib,
	bool searchActive,
	bool excludeActive,
	Attrib **ppOut
) {
	StucErr err = PIX_ERR_SUCCESS;
	bool srcIsActive = stucIsAttribActive(pCtx, pSrc, pSrcAttrib);
	*ppOut = NULL;
	if (!srcIsActive) {
		//exlude-active is set true here to ensure we arn't merging non-active with active
		*ppOut = stucGetAttribIntern(
			pSrcAttrib->core.name,
			pDestAttribArr,
			excludeActive,
			pCtx,
			pDest,
			NULL
		);
	}
	else if (searchActive) {
		//if merge_active is set, then we merge active in-mesh and map attribs,
		// regardless of whether they share the same name
		*ppOut = stucGetActiveAttrib(pCtx, pDest, pSrcAttrib->core.use);
	}
	if (*ppOut) {
		PIX_ERR_RETURN_IFNOT_COND(
			err,
			stucCheckAttribsAreCompatible(*ppOut, pSrcAttrib),
			"mismatch between common attribs"
		);
	}
	return err;
}

StucErr stucGetMatchingAttribConst(
	StucContext pCtx,
	const StucMesh *pDest, const AttribArray *pDestAttribArr,
	const StucMesh *pSrc, const Attrib *pSrcAttrib,
	bool searchActive,
	bool excludeActive,
	const Attrib **ppOut
) {
	return stucGetMatchingAttrib(
		pCtx,
		(StucMesh *)pDest, (AttribArray *)pDestAttribArr,
		pSrc, pSrcAttrib,
		searchActive,
		excludeActive,
		(Attrib **)ppOut
	);
}

static
StucErr allocAttribsFromArr(
	StucContext pCtx,
	StucMesh *pDest,
	AttribArray *pDestAttribs,
	const StucMesh *pSrc,
	const AttribArray *pSrcAttribs,
	I32 dataLen,
	StucDomain domain,
	bool setCommon,
	bool allocData,
	bool aliasData,
	bool keepActive,
	bool activeOnly
) {
	StucErr err = PIX_ERR_SUCCESS;
	for (I32 j = 0; j < pSrcAttribs->count; ++j) {
		Attrib *pSrcAttrib = pSrcAttribs->pArr + j;
		if (pSrcAttrib->copyOpt == STUC_ATTRIB_DONT_COPY) {
			continue;
		}
		bool srcIsActive = stucIsAttribActive(pCtx, pSrc, pSrcAttrib);
		if (activeOnly && !srcIsActive) {
			continue;
		}
		Attrib *pDestAttrib = NULL;
		err = stucGetMatchingAttrib(
			pCtx,
			pDest, pDestAttribs,
			pSrc, pSrcAttrib,
			true,
			true,
			&pDestAttrib
		);
		PIX_ERR_RETURN_IFNOT(err, "");
		if (pDestAttrib) {
			//if attribute already exists in destination,
			//set origin to common and set if active, then skip
			if (keepActive && srcIsActive &&
				!pDest->activeAttribs[pSrcAttrib->core.use].active) {
				stucSetAttribIdxActive(
					pDest, pDestAttribs->count,
					pSrcAttrib->core.use,
					domain
				);
			}
			if (setCommon) {
				pDestAttrib->origin = STUC_ATTRIB_ORIGIN_COMMON;
			}
			continue;
		}
		PIX_ERR_ASSERT("", pDestAttribs->count <= pDestAttribs->size);
		if (pDestAttribs->count == pDestAttribs->size) {
			pDestAttribs->size *= 2;
			pDestAttribs->pArr = pCtx->alloc.fpRealloc(
				pDestAttribs->pArr,
				pDestAttribs->size * sizeof(Attrib)
			);
		}
		pDestAttrib = pDestAttribs->pArr + pDestAttribs->count;
		if (keepActive && stucIsAttribActive(pCtx, pSrc, pSrcAttrib)) {
			stucSetAttribIdxActive(
				pDest, pDestAttribs->count,
				pSrcAttrib->core.use,
				domain
			);
		}
		stucInitAttrib(
			&pCtx->alloc,
			pDestAttrib,
			pSrcAttrib->core.name,
			aliasData || !allocData ? 0 : dataLen,
			pSrcAttrib->interpolate,
			pSrcAttrib->origin,
			pSrcAttrib->copyOpt,
			pSrcAttrib->core.type,
			pSrcAttrib->core.use
		);
		if (aliasData) {
			PIX_ERR_ASSERT("", !allocData);
			pDestAttrib->core.pData = pSrcAttrib->core.pData;
		}
		pDestAttribs->count++;
	}
	return err;
}

StucErr stucAllocAttribs(
	StucContext pCtx,
	StucDomain domain,
	I32 domainSize,
	StucMesh *pDest,
	I32 srcCount,
	const StucMesh *const *ppSrcArr,
	I32 activeSrc,
	bool setCommon,
	bool allocData,
	bool aliasData,
	bool activeOnly
) {
	StucErr err = PIX_ERR_SUCCESS;
	AttribArray *pDestAttribArr = stucGetAttribArrFromDomain(pDest, domain);
	pDestAttribArr->size = 2;
	pDestAttribArr->pArr = pCtx->alloc.fpCalloc(pDestAttribArr->size, sizeof(Attrib));
	for (I32 i = 0; i < srcCount; ++i) {
		const AttribArray *pSrcAttribArr =
			stucGetAttribArrFromDomainConst(ppSrcArr[i], domain);
		if (pSrcAttribArr && pSrcAttribArr->count) {
			err = allocAttribsFromArr(
				pCtx,
				pDest,
				pDestAttribArr,
				ppSrcArr[i],
				pSrcAttribArr,
				domainSize,
				domain,
				setCommon,
				allocData,
				aliasData,
				activeSrc < 0 ? true : i == activeSrc,
				activeOnly
			);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}
	}
	PIX_ERR_CATCH(0, err, ;);
	if (!pDestAttribArr->count) {
		pCtx->alloc.fpFree(pDestAttribArr->pArr);
		pDestAttribArr->pArr = NULL;
		pDestAttribArr->size = 0;;
	}
	return err;
}

void stucReallocAttrib(
	const StucAlloc *pAlloc,
	Mesh *pMesh,
	AttribCore *pAttrib,
	const I32 newLen
) {
	I8 oldFirstElement = *(I8 *)stucAttribAsVoid(pAttrib, 0);
	I32 attribSize = stucGetAttribSizeIntern(pAttrib->type);
	pAttrib->pData = pAlloc->fpRealloc(pAttrib->pData, attribSize * newLen);
	I8 newFirstElement = *(I8 *)stucAttribAsVoid(pAttrib, 0);
	PIX_ERR_ASSERT("", newFirstElement == oldFirstElement);
}


void stucReallocAttribArr(
	StucContext pCtx,
	StucDomain domain,
	Mesh *pMesh,
	AttribArray *pAttribArr,
	const I32 newLen
) {
	PIX_ERR_ASSERT("", newLen >= 0 && newLen < 100000000);
	for (I32 i = 0; i < pAttribArr->count; ++i) {
		Attrib *pAttrib = pAttribArr->pArr + i;
		//Check entry is valid
		PIX_ERR_ASSERT("corrupt attrib", pAttrib->interpolate % 2 == pAttrib->interpolate);
		stucReallocAttrib(&pCtx->alloc, pMesh, &pAttrib->core, newLen);
	}
	stucAssignActiveAliases(pCtx, pMesh, 0xffffffff, domain); //set all
}


void stucSetAttribCopyOpt(
	StucContext pCtx,
	StucMesh *pMesh,
	StucAttribCopyOpt opt,
	UBitField32 flags
) {
	if (!flags) {
		return;
	}
	for (I32 i = 1; i < STUC_ATTRIB_USE_ENUM_COUNT; ++i) {
		if (flags >> i & 0x1) {
			Attrib *pAttrib = stucGetActiveAttrib(pCtx, pMesh, i);
			if (pAttrib) {
				pAttrib->copyOpt = opt;
			}
		}
	}
}

void stucSetAttribOrigins(AttribArray *pAttribs, AttribOrigin origin) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		pAttribs->pArr[i].origin = origin;
	}
}

StucErr stucAllocAttribsFromMeshArr(
	StucContext pCtx,
	Mesh *pDest,
	I32 srcCount,
	const Mesh *const *ppMeshSrcs,
	I32 activeSrc,
	bool setCommon,
	bool allocData,
	bool aliasData,
	bool activeOnly
) {
	StucErr err = PIX_ERR_SUCCESS;
	bool skipEdge = false;
#ifdef STUC_DISABLE_EDGES_IN_BUF
	skipEdge = pDest->core.type.type == STUC_OBJECT_DATA_MESH_BUF;
#endif
	const StucMesh **ppMeshSrcsCore = pCtx->alloc.fpMalloc(sizeof(void *) * srcCount);
	for (I32 i = 0; i < srcCount; ++i) {
		ppMeshSrcsCore[i] = &ppMeshSrcs[i]->core;
	}
	for (I32 i = STUC_DOMAIN_FACE; i <= STUC_DOMAIN_VERT; ++i) {
		if (i == STUC_DOMAIN_EDGE && skipEdge) {
			continue;
		}
		err = stucAllocAttribs(
			pCtx,
			i,
			stucGetDomainSize(pDest, i),
			&pDest->core,
			srcCount,
			ppMeshSrcsCore,
			activeSrc,
			setCommon,
			allocData,
			aliasData,
			activeOnly
		);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	return err;
}

void stucInitAttrib(
	const StucAlloc *pAlloc,
	Attrib *pAttrib,
	const char *pName,
	I32 dataLen,
	bool interpolate,
	AttribOrigin origin,
	AttribCopyOpt copyOpt,
	AttribType type,
	StucAttribUse use
) {
	stucInitAttribCore(pAlloc, &pAttrib->core, pName, dataLen, type, use);
	pAttrib->interpolate = interpolate;
	pAttrib->origin = origin;
	pAttrib->copyOpt = copyOpt;
}

void stucInitAttribCore(
	const StucAlloc *pAlloc,
	AttribCore *pAttrib,
	const char *pName,
	I32 dataLen,
	AttribType type,
	StucAttribUse use
) {
	memcpy(pAttrib->name, pName, STUC_ATTRIB_NAME_MAX_LEN);
	if (dataLen) {
		pAttrib->pData = pAlloc->fpCalloc(dataLen, stucGetAttribSizeIntern(type));
	}
	pAttrib->type = type;
	pAttrib->use = use;
}

void stucAppendAttrib(
	const StucAlloc *pAlloc,
	AttribArray *pArr,
	Attrib **ppAttrib,
	const char *pName,
	I32 dataLen,
	bool interpolate,
	AttribOrigin origin,
	AttribCopyOpt copyOpt,
	AttribType type,
	StucAttribUse use
) {
	PIX_ERR_ASSERT("", pArr->count <= pArr->size);
	if (pArr->size == 0) {
		pArr->size = 1;
		pArr->pArr = pAlloc->fpMalloc(sizeof(Attrib) * pArr->size);
	}
	else if (pArr->count == pArr->size) {
		pArr->size *= 2;
		pArr->pArr = pAlloc->fpRealloc(pArr->pArr, sizeof(Attrib) * pArr->size);
	}
	*ppAttrib = pArr->pArr + pArr->count;
	stucInitAttrib(
		pAlloc,
		*ppAttrib,
		pName,
		dataLen,
		interpolate,
		origin,
		copyOpt,
		type,
		use
	);
	pArr->count++;
}

#define CMP_VEC_ATTRIBS(t, size, a, b)\
	pixmV##size##t##Equal(*(const V##size##_##t *)a, *(const V##size##_##t *)b)

bool stucCmpIdxAttribs(const AttribCore *pA, I32 iA, const AttribCore *pB, I32 iB) {
	const void *pAData = stucAttribAsVoidConst(pA, iA);
	const void *pBData = stucAttribAsVoidConst(pB, iB);
	//TODO replace switch statement with a single memcmp
	switch (pA->type) {
		case STUC_ATTRIB_I8:
			return (I8 *)pAData == (I8 *)pBData;
		case STUC_ATTRIB_I16:
			return (I16 *)pAData == (I16 *)pBData;
		case STUC_ATTRIB_I32:
			return (I32 *)pAData == (I32 *)pBData;
		case STUC_ATTRIB_I64:
			return (I64 *)pAData == (I64 *)pBData;
		case STUC_ATTRIB_F32:
			return (F32 *)pAData == (F32 *)pBData;
		case STUC_ATTRIB_F64:
			return (F64 *)pAData == (F64 *)pBData;
		case STUC_ATTRIB_V2_I8:
			return CMP_VEC_ATTRIBS(I8, 2, pAData, pBData);
		case STUC_ATTRIB_V2_I16:
			return CMP_VEC_ATTRIBS(I16, 2, pAData, pBData);
		case STUC_ATTRIB_V2_I32:
			return CMP_VEC_ATTRIBS(I32, 2, pAData, pBData);
		case STUC_ATTRIB_V2_I64:
			return CMP_VEC_ATTRIBS(I64, 2, pAData, pBData);
		case STUC_ATTRIB_V2_F32:
			return CMP_VEC_ATTRIBS(F32, 2, pAData, pBData);
		case STUC_ATTRIB_V2_F64:
			return CMP_VEC_ATTRIBS(F64, 2, pAData, pBData);
		case STUC_ATTRIB_V3_I8:
			return CMP_VEC_ATTRIBS(I8, 3, pAData, pBData);
		case STUC_ATTRIB_V3_I16:
			return CMP_VEC_ATTRIBS(I16, 3, pAData, pBData);
		case STUC_ATTRIB_V3_I32:
			return CMP_VEC_ATTRIBS(I32, 3, pAData, pBData);
		case STUC_ATTRIB_V3_I64:
			return CMP_VEC_ATTRIBS(I64, 3, pAData, pBData);
		case STUC_ATTRIB_V3_F32:
			return CMP_VEC_ATTRIBS(F32, 3, pAData, pBData);
		case STUC_ATTRIB_V3_F64:
			return CMP_VEC_ATTRIBS(F64, 3, pAData, pBData);
		case STUC_ATTRIB_V4_I8:
			return CMP_VEC_ATTRIBS(I8, 4, pAData, pBData);
		case STUC_ATTRIB_V4_I16:
			return CMP_VEC_ATTRIBS(I16, 4, pAData, pBData);
		case STUC_ATTRIB_V4_I32:
			return CMP_VEC_ATTRIBS(I32, 4, pAData, pBData);
		case STUC_ATTRIB_V4_I64:
			return CMP_VEC_ATTRIBS(I64, 4, pAData, pBData);
		case STUC_ATTRIB_V4_F32:
			return CMP_VEC_ATTRIBS(F32, 4, pAData, pBData);
		case STUC_ATTRIB_V4_F64:
			return CMP_VEC_ATTRIBS(F64, 4, pAData, pBData);
		case STUC_ATTRIB_STRING:
			return !strncmp(
				(const char *)pAData,
				(const char *)pBData,
				STUC_ATTRIB_STRING_MAX_LEN
			);
		default:
			PIX_ERR_ASSERT("invalid attrib type", false);
	}
	PIX_ERR_ASSERT("invalid attrib type", false);
	return false;
}

I32 stucGetIdxInIndexedAttrib(
	const AttribIndexed *pDest,
	const AttribIndexed *pSrc,
	I32 srcIdx
) {
	for (I32 k = 0; k < pDest->count; ++k) {
		if (stucCmpIdxAttribs(&pDest->core, k, &pSrc->core, srcIdx)) {
			return k;
		}
	}
	return -1;
}

void stucAppendSpAttribsToMesh(
	StucContext pCtx,
	Mesh *pMesh,
	UBitField32 pFlags,
	StucAttribOrigin origin
) {
	for (I32 i = 1; i < STUC_ATTRIB_USE_SP_ENUM_COUNT; ++i) {
		if (!(pFlags >> i & 0x1)) {
			continue;
		}
		StucDomain domain = pCtx->spAttribDomains[i];
		AttribArray *pArr = stucGetAttribArrFromDomain(&pMesh->core, domain);
		I32 dataLen = stucDomainCountGetIntern(&pMesh->core, domain);
		PIX_ERR_ASSERT("", pArr);
		Attrib *pAttrib = NULL;
		stucAppendAttrib(
			&pCtx->alloc,
			pArr,
			&pAttrib,
			pCtx->spAttribNames[i],
			dataLen,
			false,
			origin,
			STUC_ATTRIB_DONT_COPY,
			pCtx->spAttribTypes[i],
			i
		);
		stucSetAttribIdxActive(&pMesh->core,  pArr->count - 1, i, domain);
	}
}

void stucQuerySpAttribs(
	StucContext pCtx,
	const StucMesh *pMesh,
	UBitField32 toCheck,
	UBitField32 *pHas
) {
	*pHas = 0;
	for (I32 i = 1; i < STUC_ATTRIB_USE_SP_ENUM_COUNT; ++i) {
		if (!(toCheck >> i & 0x1)) {
			continue;
		}
		if (pMesh->activeAttribs[i].active) {
			*pHas |= 0x1 << i;
		}
	}
}

bool stucCheckAttribsAreCompatible(const Attrib *pA, const Attrib *pB) {
	return
		//not that important if interpolate doesn't match,
		// throwing an err because of it would be annoying
		//pA->interpolate == pB->interpolate && 
		
		//TODO readd type checking, but be more specific,
		//make a table of compatible types per attrib use
		//pA->core.type == pB->core.type &&
		pA->core.use == pB->core.use;
}

AttribIndexed *stucAppendIndexedAttrib(
	StucContext pCtx,
	AttribIndexedArr *pIndexedAttribArr,
	const char *pName,
	I32 dataLen,
	StucAttribType type,
	StucAttribUse use
) {
	I32 newIdx;
	PIXALC_DYN_ARR_ADD(StucAttribIndexed, &pCtx->alloc, pIndexedAttribArr, newIdx);
	AttribIndexed *pIndexedAttrib = pIndexedAttribArr->pArr + newIdx;
	stucInitAttribCore(&pCtx->alloc, &pIndexedAttrib->core, pName, dataLen, type, use);
	if (dataLen) {
		*pIndexedAttrib = (AttribIndexed){.core = pIndexedAttrib->core, .size = dataLen};
	}
	return pIndexedAttrib;
}

StucErr stucAppendAndCopyIdxAttrib(
	StucContext pCtx,
	const AttribIndexed *pSrc,
	AttribIndexedArr *pDestArr
) {
	StucErr err = PIX_ERR_SUCCESS;
	AttribIndexed *pIndexedAttrib = stucAppendIndexedAttrib(
		pCtx,
		pDestArr,
		pSrc->core.name,
		pSrc->count,
		pSrc->core.type,
		pSrc->core.use
	);
	memcpy(
		pIndexedAttrib->core.pData,
		pSrc->core.pData,
		STUC_ATTRIB_STRING_MAX_LEN * pSrc->count
	);
	pIndexedAttrib->count = pSrc->count;
	return err;
}

StucErr stucAppendAndCopyIdxAttribFromName(
	StucContext pCtx,
	const char *pName,
	const AttribIndexedArr *pSrcArr,
	AttribIndexedArr *pDestArr
) {
	StucErr err = PIX_ERR_SUCCESS;
	AttribIndexed *pSrc = stucGetAttribIndexedInternConst(pSrcArr, pName);
	PIX_ERR_RETURN_IFNOT_COND(err, pSrc, "");
	stucAppendAndCopyIdxAttrib(pCtx, pSrc, pDestArr);
	return err;
}

void stucAppendToIndexedAttrib(
	StucContext pCtx,
	AttribIndexed *pDest,
	const AttribCore *pSrc,
	I32 srcIdx
) {
	PIX_ERR_ASSERT("", pDest->core.type == pSrc->type);
	PIX_ERR_ASSERT("", pDest->count <= pDest->size);
	if (!pDest->size) {
		PIX_ERR_ASSERT("", !pDest->count);
		pDest->size = 1;
		pDest->core.pData =
			pCtx->alloc.fpCalloc(pDest->size, stucGetAttribSizeIntern(pDest->core.type));
	}
	else if (pDest->count == pDest->size) {
		pDest->size *= 2;
		stucReallocAttrib(&pCtx->alloc, NULL, &pDest->core, pDest->size);
	}
	memcpy(
		stucAttribAsVoid(&pDest->core, pDest->count),
		stucAttribAsVoidConst(pSrc, srcIdx),
		stucGetAttribSizeIntern(pSrc->type)
	);
	pDest->count++;
}

bool stucIsAttribUseRequired(StucAttribUse use) {
	return
		use == STUC_ATTRIB_USE_POS ||
		use == STUC_ATTRIB_USE_UV ||
		use == STUC_ATTRIB_USE_NORMAL ||
		use == STUC_ATTRIB_USE_IDX;
}

UBitField32 stucAttribUseField(const StucAttribUse *pArr, I32 count) {
	PIX_ERR_ASSERT("", count <= STUC_ATTRIB_USE_ENUM_COUNT);
	UBitField32 field = 0;
	for (I32 i = 0; i < count; ++i) {
		field |= 0x1 << pArr[i];
	}
	return field;
}

StucErr stucAttemptToSetMissingActiveDomains(StucMesh *pMesh) {
	StucErr err = PIX_ERR_SUCCESS;
	for (I32 i = 1; i < STUC_ATTRIB_USE_ENUM_COUNT; ++i) {
		if (i == STUC_ATTRIB_USE_SP_ENUM_COUNT) {
			continue;
		}
		AttribActive *pIdx = pMesh->activeAttribs + i;
		if (pIdx->domain != STUC_DOMAIN_NONE) {
			continue;
		}
		for (I32 j = STUC_DOMAIN_FACE; j <= STUC_DOMAIN_VERT; ++j) {
			const AttribArray *pAttribArr = stucGetAttribArrFromDomainConst(pMesh, j);
			if (pIdx->idx >= pAttribArr->count ||
				pAttribArr->pArr[pIdx->idx].core.use != i
			) {
				continue;
			}
			//the below is false, 2 domains have their own candidate.
			//the intended attrib is ambiguous, so return error
			PIX_ERR_RETURN_IFNOT_COND(
				err,
				pIdx->domain == STUC_DOMAIN_NONE,
				"Unable to determine active attrib domain"
			);
			pIdx->domain = j;
		}
	}
	return err;
}

