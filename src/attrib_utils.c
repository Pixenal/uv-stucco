#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <float.h>

#include <attrib_utils.h>
#include <math_utils.h>
#include <context.h>
#include <mesh.h>
#include <error.h>

//special buf attribs should not be set by user,
//so unlike special attribs, they're hardcoded here
static
char spBufAttribs[6][STUC_ATTRIB_NAME_MAX_LEN] = {
	"",
	"StucW",
	"StucInNormal",
	"StucInTangent",
	"StucInTSign",
	"StucAlpha"
};

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
	strcpy(pCtx->spAttribNames[16], "StucEdgeCorners");
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
}

static
F64 lerp(F64 a, F64 b, F64 alpha) {
	return b * alpha + (1.0 - alpha) * a;
}

#define CLAMP(a, min, max) (a <= min ? min : (a > max ? max : a))

static
F64 clamp(F64 a, F64 min, F64 max) {
	return CLAMP(a, min, max);
}

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
	*pDest = max(a, b);
}
static
void iBlendLighten(I64 *pDest, I64 a, I64 b) {
	*pDest = max(a, b);
}

static
void fBlendDarken(F64 *pDest, F64 a, F64 b) {
	*pDest = min(a, b);
}
static
void iBlendDarken(I64 *pDest, I64 a, I64 b) {
	*pDest = min(a, b);
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
	((t (*)[vec])pDest->core.pData)[idx][comp]

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

I32 stucGetAttribSizeIntern(AttribType type) {
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
		case STUC_ATTRIB_V2_I8:
			return 2;
		case STUC_ATTRIB_V2_I16:
			return 4;
		case STUC_ATTRIB_V2_I32:
			return 8;
		case STUC_ATTRIB_V2_I64:
			return 16;
		case STUC_ATTRIB_V2_F32:
			return 8;
		case STUC_ATTRIB_V2_F64:
			return 16;
		case STUC_ATTRIB_V3_I8:
			return 3;
		case STUC_ATTRIB_V3_I16:
			return 6;
		case STUC_ATTRIB_V3_I32:
			return 12;
		case STUC_ATTRIB_V3_I64:
			return 24;
		case STUC_ATTRIB_V3_F32:
			return 12;
		case STUC_ATTRIB_V3_F64:
			return 24;
		case STUC_ATTRIB_V4_I8:
			return 4;
		case STUC_ATTRIB_V4_I16:
			return 8;
		case STUC_ATTRIB_V4_I32:
			return 16;
		case STUC_ATTRIB_V4_I64:
			return 32;
		case STUC_ATTRIB_V4_F32:
			return 16;
		case STUC_ATTRIB_V4_F64:
			return 32;
		case STUC_ATTRIB_STRING:
			return STUC_ATTRIB_STRING_MAX_LEN;
		default:
			STUC_ASSERT("", false);
			return 0;
	}
}

Result stucAssignActiveAliases(
	StucContext pCtx,
	Mesh *pMesh,
	UBitField32 flags,
	StucDomain domain
) {
	Result err = STUC_SUCCESS;
	STUC_RETURN_ERR_IFNOT_COND(err, pCtx && pMesh, "");
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
				pMesh->pVerts = pAttrib->core.pData;
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
			case STUC_ATTRIB_USE_EDGE_CORNERS:
				pMesh->pEdgeCorners = pAttrib->core.pData;
				break;
			default:
				STUC_ASSERT("outside use special range", false);
		}
	}
	return err;
}

Attrib *stucGetActiveAttrib(StucContext pCtx, StucMesh *pMesh, StucAttribUse use) {
	STUC_ASSERT("", use >= 0);
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
	STUC_ASSERT("", idx.idx < pArr->count);
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
	const StucMesh *pMesh
) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		STUC_ASSERT(
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
	const StucMesh *pMesh
) {
	return stucGetAttribIntern(pName, (AttribArray *)pAttribs, excludeActive, pCtx, pMesh);
}

void stucSetAttribIdxActive(
	StucMesh *pMesh,
	I32 idx,
	StucAttribUse use,
	StucDomain domain
) {
	STUC_ASSERT("", use >= 0);
	if (use != STUC_ATTRIB_USE_NONE &&
		use != STUC_ATTRIB_USE_SP_ENUM_COUNT &&
		use < STUC_ATTRIB_USE_ENUM_COUNT
	) {
		pMesh->activeAttribs[use].active = true;
		pMesh->activeAttribs[use].idx = (I16)idx;
		pMesh->activeAttribs[use].domain = domain;
	}
}

V3_F32 *stucAttribAsV3(AttribCore *pAttrib, I32 idx) {
	return (V3_F32 *)pAttrib->pData + idx;
}

V2_F32 *stucAttribAsV2(AttribCore *pAttrib, I32 idx) {
	return (V2_F32 *)pAttrib->pData + idx;
}

I32 *stucAttribAsI32(AttribCore *pAttrib, I32 idx) {
	return (I32 *)pAttrib->pData + idx;
}

I8 *stucAttribAsI8(AttribCore *pAttrib, I32 idx) {
	return (I8 *)pAttrib->pData + idx;
}

const char *stucAttribAsStrConst(const AttribCore *pAttrib, I32 idx) {
	return ((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pAttrib->pData)[idx];
}

char *stucAttribAsStr(AttribCore *pAttrib, I32 idx) {
	return ((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pAttrib->pData)[idx];
}

void *stucAttribAsVoid(AttribCore *pAttrib, I32 idx) {
	switch (pAttrib->type) {
		case STUC_ATTRIB_I8:
			return ((I8 *)pAttrib->pData) + idx;
		case STUC_ATTRIB_I16:
			return ((I16 *)pAttrib->pData) + idx;
		case STUC_ATTRIB_I32:
			return ((I32 *)pAttrib->pData) + idx;
		case STUC_ATTRIB_I64:
			return ((I64 *)pAttrib->pData) + idx;
		case STUC_ATTRIB_F32:
			return ((F32 *)pAttrib->pData) + idx;
		case STUC_ATTRIB_F64:
			return ((F64 *)pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_I8:
			return ((I8 (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_I16:
			return ((I16 (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_I32:
			return ((I32 (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_I64:
			return ((I64 (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_F32:
			return ((F32 (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_F64:
			return ((F64 (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_I8:
			return ((I8 (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_I16:
			return ((I16 (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_I32:
			return ((I32 (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_I64:
			return ((I64 (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_F32:
			return ((F32 (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_F64:
			return ((F64 (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_I8:
			return ((I8 (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_I16:
			return ((I16 (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_I32:
			return ((I32 (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_I64:
			return ((I64 (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_F32:
			return ((F32 (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_F64:
			return ((F64 (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_STRING:
			return ((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pAttrib->pData)[idx];
		default:
			STUC_ASSERT("", false);
			return NULL;
	}
}

const void *stucAttribAsVoidConst(const AttribCore *pAttrib, I32 idx) {
	return stucAttribAsVoid((AttribCore *)pAttrib, idx);
}

I32 stucCopyAttrib(Attrib *pDest, I32 iDest, const Attrib *pSrc, I32 iSrc) {
	if (pSrc->copyOpt == STUC_ATTRIB_DONT_COPY) {
		return 0;
	}
	if (pSrc->core.type != pDest->core.type) {
		return 1;
	}
	switch (pSrc->core.type) {
		case STUC_ATTRIB_I8:
			((I8 *)pDest->core.pData)[iDest] = ((I8 *)pSrc->core.pData)[iSrc];
			break;
		case STUC_ATTRIB_I16:
			((I16 *)pDest->core.pData)[iDest] = ((I16 *)pSrc->core.pData)[iSrc];
			break;
		case STUC_ATTRIB_I32:
			((I32 *)pDest->core.pData)[iDest] = ((I32 *)pSrc->core.pData)[iSrc];
			break;
		case STUC_ATTRIB_I64:
			((I64 *)pDest->core.pData)[iDest] = ((I64 *)pSrc->core.pData)[iSrc];
			break;
		case STUC_ATTRIB_F32:
			((F32 *)pDest->core.pData)[iDest] = ((F32 *)pSrc->core.pData)[iSrc];
			break;
		case STUC_ATTRIB_F64:
			((F64 *)pDest->core.pData)[iDest] = ((F64 *)pSrc->core.pData)[iSrc];
			break;
		case STUC_ATTRIB_V2_I8:
			memcpy(((I8 (*)[2])pDest->core.pData)[iDest],
			       ((I8 (*)[2])pSrc->core.pData)[iSrc], sizeof(I8[2]));
			break;
		case STUC_ATTRIB_V2_I16:
			memcpy(((I16 (*)[2])pDest->core.pData)[iDest],
			       ((I16 (*)[2])pSrc->core.pData)[iSrc], sizeof(I16[2]));
			break;
		case STUC_ATTRIB_V2_I32:
			memcpy(((I32 (*)[2])pDest->core.pData)[iDest],
			       ((I32 (*)[2])pSrc->core.pData)[iSrc], sizeof(I32[2]));
			break;
		case STUC_ATTRIB_V2_I64:
			memcpy(((I64 (*)[2])pDest->core.pData)[iDest],
			       ((I64 (*)[2])pSrc->core.pData)[iSrc], sizeof(I64[2]));
			break;
		case STUC_ATTRIB_V2_F32:
			memcpy(((F32 (*)[2])pDest->core.pData)[iDest],
			       ((F32 (*)[2])pSrc->core.pData)[iSrc], sizeof(F32[2]));
			break;
		case STUC_ATTRIB_V2_F64:
			memcpy(((F64 (*)[2])pDest->core.pData)[iDest],
			       ((F64 (*)[2])pSrc->core.pData)[iSrc], sizeof(F64[2]));
			break;
		case STUC_ATTRIB_V3_I8:
			memcpy(((I8 (*)[3])pDest->core.pData)[iDest],
			       ((I8 (*)[3])pSrc->core.pData)[iSrc], sizeof(I8[3]));
			break;
		case STUC_ATTRIB_V3_I16:
			memcpy(((I16 (*)[3])pDest->core.pData)[iDest],
			       ((I16 (*)[3])pSrc->core.pData)[iSrc], sizeof(I16[3]));
			break;
		case STUC_ATTRIB_V3_I32:
			memcpy(((I32 (*)[3])pDest->core.pData)[iDest],
			       ((I32 (*)[3])pSrc->core.pData)[iSrc], sizeof(I32[3]));
			break;
		case STUC_ATTRIB_V3_I64:
			memcpy(((I64 (*)[3])pDest->core.pData)[iDest],
			       ((I64 (*)[3])pSrc->core.pData)[iSrc], sizeof(I64[3]));
			break;
		case STUC_ATTRIB_V3_F32:
			memcpy(((F32 (*)[3])pDest->core.pData)[iDest],
			       ((F32 (*)[3])pSrc->core.pData)[iSrc], sizeof(F32[3]));
			break;
		case STUC_ATTRIB_V3_F64:
			memcpy(((F64 (*)[3])pDest->core.pData)[iDest],
			       ((F64 (*)[3])pSrc->core.pData)[iSrc], sizeof(F64[3]));
			break;
		case STUC_ATTRIB_V4_I8:
			memcpy(((I8 (*)[4])pDest->core.pData)[iDest],
			       ((I8 (*)[4])pSrc->core.pData)[iSrc], sizeof(I8[4]));
			break;
		case STUC_ATTRIB_V4_I16:
			memcpy(((I16 (*)[4])pDest->core.pData)[iDest],
			       ((I16 (*)[4])pSrc->core.pData)[iSrc], sizeof(I16[4]));
			break;
		case STUC_ATTRIB_V4_I32:
			memcpy(((I32 (*)[4])pDest->core.pData)[iDest],
			       ((I32 (*)[4])pSrc->core.pData)[iSrc], sizeof(I32[4]));
			break;
		case STUC_ATTRIB_V4_I64:
			memcpy(((I64 (*)[4])pDest->core.pData)[iDest],
			       ((I64 (*)[4])pSrc->core.pData)[iSrc], sizeof(I64[4]));
			break;
		case STUC_ATTRIB_V4_F32:
			memcpy(((F32 (*)[4])pDest->core.pData)[iDest],
			       ((F32 (*)[4])pSrc->core.pData)[iSrc], sizeof(F32[4]));
			break;
		case STUC_ATTRIB_V4_F64:
			memcpy(((F64 (*)[4])pDest->core.pData)[iDest],
			       ((F64 (*)[4])pSrc->core.pData)[iSrc], sizeof(F64[4]));
			break;
		case STUC_ATTRIB_STRING:
			memcpy(((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pDest->core.pData)[iDest],
			       ((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pSrc->core.pData)[iSrc],
			       sizeof(F64[STUC_ATTRIB_STRING_MAX_LEN]));
			break;
	}
	return 0;
}

void stucCopyAllAttribs(
	AttribArray *pDest,
	I32 iDest,
	AttribArray *pSrc,
	I32 iSrc
) {
	for (I32 i = 0; i < pDest->count; ++i) {
		Attrib *pSrcAttrib = stucGetAttribIntern(pDest->pArr[i].core.name, pSrc, false, NULL, NULL);
		if (pSrcAttrib) {
			stucCopyAttrib(pDest->pArr + i, iDest, pSrcAttrib, iSrc);
		}
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
			STUC_ASSERT("", false);
			return 0;
	}
}

const StucCommonAttrib *stucGetCommonAttrib(
	const StucCommonAttribArr *pArr,
	char *pName
) {
	for (I32 i = 0; i < pArr->count; ++i) {
		if (!strncmp(pName, pArr->pArr[i].name, STUC_ATTRIB_NAME_MAX_LEN)) {
			return pArr->pArr + i;
		}
	}
	return NULL;
}

const StucCommonAttrib *stucGetCommonAttribFromDomain(
	const StucCommonAttribList *pList,
	char *pName,
	StucDomain domain
) {
	switch (domain) {
		case STUC_DOMAIN_FACE:
			return stucGetCommonAttrib(&pList->face, pName);
		case STUC_DOMAIN_CORNER:
			return stucGetCommonAttrib(&pList->corner, pName);
		case STUC_DOMAIN_EDGE:
			return stucGetCommonAttrib(&pList->edge, pName);
		case STUC_DOMAIN_VERT:
			return stucGetCommonAttrib(&pList->vert, pName);
	}
	STUC_ASSERT("invalid domain", false);
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
	Attrib *pDest,
	I32 iDest,
	Attrib *pSrcA,
	I32 iSrcA,
	Attrib *pSrcB,
	I32 iSrcB,
	F32 alpha
) {
	//TODO remove all uses of abort()
	STUC_ASSERT(
		"type mismatch in interpolateAttrib",
		pDest->core.type == pSrcA->core.type &&
		pDest->core.type == pSrcB->core.type
	);
	AttribType type = pDest->core.type;
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
	}
}

void stucTriInterpolateAttrib(
	Attrib *pDest,
	I32 iDest,
	const Attrib *pSrc,
	I32 iSrcA,
	I32 iSrcB,
	I32 iSrcC,
	V3_F32 bc
) {
	if (pDest->core.type != pSrc->core.type) {
		printf("Type mismatch in interpolateAttrib\n");
		//TODO remove all uses of abort(), and add proper exception handling
		abort();
	}
	AttribType type = pDest->core.type;
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
	}
}

I32 stucAttribTypeGetVecSizeIntern(AttribType type) {
	STUC_ASSERT("invalid type", type >= 0 && type < STUC_ATTRIB_TYPE_ENUM_COUNT);
	STUC_ASSERT("this func shouldn't be called on a string", type != STUC_ATTRIB_STRING);
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
F64 makeFloatWide(void *ptr, AttribType type) {
	STUC_ASSERT("invalid type", type == STUC_ATTRIB_F32 || type == STUC_ATTRIB_F64);
	return type == STUC_ATTRIB_F32 ? (F64)*(F32 *)ptr : *(F64 *)ptr; 
}

static
I64 makeIntWide(void *ptr, AttribType type, bool isSigned) {
	switch (type) {
		case STUC_ATTRIB_I8:
			return (I64)(isSigned ? *(I8 *)ptr : *(U8 *)ptr);
		case STUC_ATTRIB_I16:
			return (I64)(isSigned ? *(I16 *)ptr : *(U16 *)ptr);
		case STUC_ATTRIB_I32:
			return (I64)(isSigned ? *(I32 *)ptr : *(U32 *)ptr);
		case STUC_ATTRIB_I64:
			return (I64)(isSigned ? *(I64 *)ptr : *(U64 *)ptr);
	}
	STUC_ASSERT("invalid type", false);
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
	}
	STUC_ASSERT("invalid type", false);
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
	}
	STUC_ASSERT("invalid type", false);
	return 0;
}

#define SET_VOID_COMP(tDest ,pDestComp, src)\
	STUC_ASSERT("invalid type", tDest != STUC_ATTRIB_NONE && tDest < STUC_ATTRIB_V2_I8);\
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
			STUC_ASSERT("invalid type", false);\
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
		STUC_ASSERT("type must be floating point if normalizing", #t[0] == 'F');\
		aComp /= (t)aMax;\
	}\
	if (normalizeB) {\
		STUC_ASSERT("type must be floating point if normalizing", #t[0] == 'F');\
		bComp /= (t)bMax;\
	}\
\
	STUC_ASSERT("", pBlendFunc);\
	pBlendFunc(&destBuf, aComp, bComp);\
\
	if (blendConfig.clamp) {\
		destBuf = CLAMP(destBuf, clampMin, clampMax);\
	}\
	if (blendConfig.opacity != .0 && blendConfig.opacity != 1.0) {\
		/* lerp is done in F64 regardless of t */\
		destBuf = (t)lerp((F64)aComp, (F64)destBuf, (F64)blendConfig.opacity);\
	}\
	SET_VOID_COMP(destCompType, pDestComp, destBuf);\
}

static
void blendComponents(
	void (* pFBlendFunc)(F64 *,F64,F64),
	void (* pIBlendFunc)(I64 *,I64,I64),
	StucBlendConfig blendConfig,
	void *pDest, AttribType destCompType, I32 destVecSize,
	void *pA, AttribType aCompType, I32 aVecSize, bool normalizeA, I64 aMax,
	void *pB, AttribType bCompType, I32 bVecSize, bool normalizeB, I64 bMax,
	bool isSigned
) {
	bool aIsFloat = isAttribTypeFloat(aCompType);
	bool bIsFloat = isAttribTypeFloat(bCompType);
	I32 size = min(min(aVecSize, bVecSize), destVecSize);
	for (I32 i = 0; i < size; ++i) {
		void *pDestComp = (int8_t *)pDest + getAttribCompTypeSize(destCompType) * i;
		void *pAComp =
			(int8_t *)pA + getAttribCompTypeSize(aCompType) * i * (aVecSize != 1);
		void *pBComp =
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
	AttribCore *pA, I32 iA, bool normalizeA, I64 aMax,
	AttribCore *pB, I32 iB, bool normalizeB, I64 bMax,
	bool isSigned
) {
	void *pDestVal = stucAttribAsVoid(pDest, iDest);
	void *pAVal = stucAttribAsVoid(pA, iA);
	void *pBVal = stucAttribAsVoid(pB, iB);
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
	AttribCore *pA, I32 iA,
	AttribCore *pB, I32 iB
) {
	UBitField32 blendFlags = 0x7ff;  //all blends execpt for APPEND
	blendSwitch(blendConfig, blendFlags, pDest, iDest, pA, iA, false, 0, pB, iB, false, 0, true);
}

static
void blendUseIdx(
	StucBlendConfig blendConfig,
	AttribCore *pDest, I32 iDest,
	AttribCore *pA, I32 iA,
	AttribCore *pB, I32 iB
) {
	STUC_ASSERT(
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
	AttribCore *pA, I32 iA,
	AttribCore *pB, I32 iB
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
			iDestBuf[i] = destBuf[i] * destMax;
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
	AttribCore *pA, I32 iA,
	AttribCore *pB, I32 iB
) {
	//replace, multiply, divide, add, subtract, lighten, and darken
	UBitField32 blendFlags = 0xdf;
	blendSwitch(blendConfig, blendFlags, pDest, iDest, pA, iA, false, 0, pB, iB, false, 0, true);
}


//TODO this name should not be plural
void stucBlendAttribs(
	AttribCore *pDest, I32 iDest,
	AttribCore *pA, I32 iA,
	AttribCore *pB, I32 iB,
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
	}
}

void stucDivideAttribByScalarInt(Attrib *pAttrib, I32 idx, U64 scalar) {
	switch (pAttrib->core.type) {
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
			STUC_ASSERT("Can't divide a string by 1", false);
			break;
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
		default:
			STUC_ASSERT("invalid domain", false);
			return NULL;
	}
}

const AttribArray *stucGetAttribArrFromDomainConst(const StucMesh *pMesh, StucDomain domain) {
	return stucGetAttribArrFromDomain((StucMesh *)pMesh, domain);
}

Result stucGetMatchingAttrib(
	StucContext pCtx,
	StucMesh *pDest, AttribArray *pDestAttribArr,
	const StucMesh *pSrc, const Attrib *pSrcAttrib,
	bool searchActive,
	bool excludeActive,
	Attrib **ppOut
) {
	Result err = STUC_SUCCESS;
	bool srcIsActive = stucIsAttribActive(pCtx, pSrc, pSrcAttrib);
	*ppOut = NULL;
	if (!srcIsActive) {
		//exlude-active is set true here to ensure we arn't merging non-active with active
		*ppOut = stucGetAttribIntern(
			pSrcAttrib->core.name,
			pDestAttribArr,
			excludeActive,
			pCtx,
			pDest
		);
	}
	else if (searchActive) {
		//if merge_active is set, then we merge active in-mesh and map attribs,
		// regardless of whether they share the same name
		*ppOut = stucGetActiveAttrib(pCtx, pDest, pSrcAttrib->core.use);
	}
	if (*ppOut) {
		STUC_RETURN_ERR_IFNOT_COND(
			err,
			stucCheckAttribsAreCompatible(*ppOut, pSrcAttrib),
			"mismatch between common attribs"
		);
	}
	return err;
}

Result stucGetMatchingAttribConst(
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
Result allocAttribsFromArr(
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
	bool keepActive
) {
	Result err = STUC_SUCCESS;
	for (I32 j = 0; j < pSrcAttribs->count; ++j) {
		Attrib *pSrcAttrib = pSrcAttribs->pArr + j;
		if (pSrcAttrib->copyOpt == STUC_ATTRIB_DONT_COPY) {
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
		STUC_RETURN_ERR_IFNOT(err, "");
		if (pDestAttrib) {
			//if attribute already exists in destination,
			//set origin to common and set if active, then skip
			if (keepActive) {
				stucSetAttribIdxActive(pDest, j, pSrcAttrib->core.use, domain);
			}
			if (setCommon) {
				pDestAttrib->origin = STUC_ATTRIB_ORIGIN_COMMON;
			}
			continue;
		}
		STUC_ASSERT("", pDestAttribs->count <= pDestAttribs->size);
		if (pDestAttribs->count == pDestAttribs->size) {
			pDestAttribs->size *= 2;
			pDestAttribs->pArr = pCtx->alloc.pRealloc(
				pDestAttribs->pArr,
				pDestAttribs->size * sizeof(Attrib)
			);
		}
		pDestAttrib = pDestAttribs->pArr + pDestAttribs->count;
		pDestAttrib->core.type = pSrcAttrib->core.type;
		memcpy(
			pDestAttrib->core.name,
			pSrcAttrib->core.name,
			STUC_ATTRIB_NAME_MAX_LEN
		);
		//TODO check for mismatches between srcs, like if srcs have different types or sp
		pDestAttrib->origin = pSrcAttrib->origin;
		pDestAttrib->core.use = pSrcAttrib->core.use;
		if (keepActive && stucIsAttribActive(pCtx, pSrc, pSrcAttrib)) {
			stucSetAttribIdxActive(pDest, j, pSrcAttrib->core.use, domain);
		}
		pDestAttrib->interpolate = pSrcAttrib->interpolate;
		I32 attribSize = stucGetAttribSizeIntern(pSrcAttrib->core.type);
		if (allocData) {
			pDestAttrib->core.pData = pCtx->alloc.pCalloc(dataLen, attribSize);
		}
		else if (aliasData) {
			pDestAttrib->core.pData = pSrcAttrib->core.pData;
		}
		pDestAttribs->count++;
	}
	return err;
}

Result stucAllocAttribs(
	StucContext pCtx,
	StucDomain domain,
	Mesh *pDest,
	I32 srcCount,
	const Mesh *const *ppSrcArr,
	I32 activeSrc,
	bool setCommon,
	bool allocData,
	bool aliasData
) {
	Result err = STUC_SUCCESS;
	AttribArray *pDestAttribArr = stucGetAttribArrFromDomain(&pDest->core, domain);
	pDestAttribArr->size = 2;
	pDestAttribArr->pArr = pCtx->alloc.pCalloc(pDestAttribArr->size, sizeof(Attrib));
	for (I32 i = 0; i < srcCount; ++i) {
		const AttribArray *pSrcAttribArr =
			stucGetAttribArrFromDomainConst(&ppSrcArr[i]->core, domain);
		if (pSrcAttribArr && pSrcAttribArr->count) {
			err = allocAttribsFromArr(
				pCtx,
				&pDest->core,
				pDestAttribArr,
				&ppSrcArr[i]->core,
				pSrcAttribArr,
				stucGetDomainSize(pDest, domain),
				domain,
				setCommon,
				allocData,
				aliasData,
				i == activeSrc
			);
			STUC_THROW_IFNOT(err, "", 0);
		}
	}
	STUC_CATCH(0, err, ;);
	if (!pDestAttribArr->count) {
		pCtx->alloc.pFree(pDestAttribArr->pArr);
		pDestAttribArr->pArr = NULL;
		pDestAttribArr->size = 0;;
	}
	return err;
}

static
I32 checkIfSpecialBufAttrib(Attrib *pAttrib) {
	I32 size = sizeof(spBufAttribs) / STUC_ATTRIB_NAME_MAX_LEN;
	for (I32 i = 1; i < size; ++i) {
		if (!strncmp(pAttrib->core.name, spBufAttribs[i], STUC_ATTRIB_NAME_MAX_LEN)) {
			return i;
		}
	}
	return -1;
}

static
SpecialBufAttrib quickCheckIfSpecialBufAttrib(
	const BufMesh *pMesh,
	const AttribCore *pAttrib
) {
	//TODO put obj or mesh type check (either asserts or if statements)
	// at the start of more functions
	if (pMesh->mesh.core.type.type != STUC_OBJECT_DATA_MESH_BUF) {
		return STUC_ATTRIB_SP_BUF_NONE;
	}
	if (pAttrib->pData == pMesh->pW) {
		return STUC_ATTRIB_SP_BUF_W;
	}
	else if (pAttrib->pData == pMesh->pInNormal) {
		return STUC_ATTRIB_SP_BUF_IN_NORMAL;
	}
	else if (pAttrib->pData == pMesh->pInTangent) {
		return STUC_ATTRIB_SP_BUF_IN_TANGENT;
	}
	else if (pAttrib->pData == pMesh->pInTSign) {
		return STUC_ATTRIB_SP_BUF_IN_T_SIGN;
	}
	else if (pAttrib->pData == pMesh->pAlpha) {
		return STUC_ATTRIB_SP_BUF_ALPHA;
	}
	return STUC_ATTRIB_SP_BUF_NONE;
}

static
void reassignIfSpecialBuf(BufMesh *pMesh, AttribCore *pAttrib, SpecialBufAttrib special) {
	switch (special) {
		case (STUC_ATTRIB_SP_BUF_NONE):
			break;
		case (STUC_ATTRIB_SP_BUF_W):
			pMesh->pW = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_BUF_IN_NORMAL):
			pMesh->pInNormal = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_BUF_IN_TANGENT):
			pMesh->pInTangent = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_BUF_IN_T_SIGN):
			pMesh->pInTSign = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_BUF_ALPHA):
			pMesh->pAlpha = pAttrib->pData;
			break;
	}
}

void stucReallocAttrib(
	const StucAlloc *pAlloc,
	Mesh *pMesh,
	AttribCore *pAttrib,
	const I32 newLen
) {
	SpecialBufAttrib specialBuf = STUC_ATTRIB_SP_BUF_NONE;
	if (pMesh && pMesh->core.type.type == STUC_OBJECT_DATA_MESH_BUF) {
		specialBuf = quickCheckIfSpecialBufAttrib((BufMesh *)pMesh, pAttrib);
	}
	I8 oldFirstElement = *(I8 *)stucAttribAsVoid(pAttrib, 0);
	I32 attribSize = stucGetAttribSizeIntern(pAttrib->type);
	pAttrib->pData = pAlloc->pRealloc(pAttrib->pData, attribSize * newLen);
	I8 newFirstElement = *(I8 *)stucAttribAsVoid(pAttrib, 0);
	STUC_ASSERT("", newFirstElement == oldFirstElement);
	if (pMesh && pMesh->core.type.type == STUC_OBJECT_DATA_MESH_BUF) {
		reassignIfSpecialBuf((BufMesh *)pMesh, pAttrib, specialBuf);
	}
}

void stucReallocAttribArr(
	StucContext pCtx,
	StucDomain domain,
	Mesh *pMesh,
	AttribArray *pAttribArr,
	const I32 newLen
) {
	STUC_ASSERT("", newLen >= 0 && newLen < 100000000);
	for (I32 i = 0; i < pAttribArr->count; ++i) {
		Attrib *pAttrib = pAttribArr->pArr + i;
		//Check entry is valid
		STUC_ASSERT("corrupt attrib", pAttrib->interpolate % 2 == pAttrib->interpolate);
		stucReallocAttrib(&pCtx->alloc, pMesh, &pAttrib->core, newLen);
		STUC_ASSERT("", i >= 0 && i < pAttribArr->count);
	}
	stucAssignActiveAliases(pCtx, pMesh, 0xffffffff, domain); //0xffffffff for set all
}

void stucReallocAndMoveAttribs(
	const StucAlloc *pAlloc,
	const BufMesh *pMesh,
	AttribArray *pAttribArr,
	I32 start,
	I32 offset,
	I32 lenToCopy,
	I32 newLen
) {
	STUC_ASSERT("", newLen >= 0 && newLen < 100000000);
	STUC_ASSERT("", start >= 0 && start < newLen);
	for (I32 i = 0; i < pAttribArr->count; ++i) {
		//this func has stuff unique to bufmeshes, and so doesn't use stucReallocAttrib()
		Attrib *pAttrib = pAttribArr->pArr + i;
		SpecialBufAttrib specialBuf = quickCheckIfSpecialBufAttrib(pMesh, &pAttrib->core);
		//Check entry is valid
		STUC_ASSERT("", pAttrib->interpolate % 2 == pAttrib->interpolate);
		I8 oldFirstElement = *(I8 *)stucAttribAsVoid(&pAttrib->core, start);
		I8 oldLastElement = *(I8 *)stucAttribAsVoid(&pAttrib->core, start + lenToCopy - 1);
		I32 attribSize = stucGetAttribSizeIntern(pAttrib->core.type);
		pAttrib->core.pData =
			pAlloc->pRealloc(pAttrib->core.pData, attribSize * newLen);
		if (lenToCopy) {
			memmove(stucAttribAsVoid(&pAttrib->core, start + offset),
			        stucAttribAsVoid(&pAttrib->core, start), attribSize * lenToCopy);
			I8 newFirstElement = *(I8 *)stucAttribAsVoid(&pAttrib->core, start + offset);
			I8 newLastElement =
				*(I8 *)stucAttribAsVoid(&pAttrib->core, start + offset + lenToCopy - 1);
			STUC_ASSERT("", newFirstElement == oldFirstElement);
			STUC_ASSERT("", newLastElement == oldLastElement);
		}
		reassignIfSpecialBuf((BufMesh *)pMesh, &pAttrib->core, specialBuf);
		STUC_ASSERT("", i >= 0 && i < pAttribArr->count);
	}
}

Result stucSetSpecialBufAttribs(BufMesh *pMesh, UBitField32 flags) {
	Result err = STUC_SUCCESS;
	StucMesh *pCore = &pMesh->mesh.core;
	if (flags >> STUC_ATTRIB_SP_BUF_W & 0x01) {
		pMesh->pWAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_W],
			&pCore->cornerAttribs,
			false, NULL, NULL
		);
		STUC_RETURN_ERR_IFNOT_COND(err, pMesh->pWAttrib, "buf-mesh has no w attrib");
		pMesh->pW = pMesh->pWAttrib->core.pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_IN_NORMAL & 0x01) {
		pMesh->pInNormalAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_IN_NORMAL],
			&pCore->cornerAttribs,
			false, NULL, NULL
		);
		STUC_RETURN_ERR_IFNOT_COND(err, pMesh->pInNormalAttrib, "buf-mesh has no in-normal attrib");
		pMesh->pInNormal = pMesh->pInNormalAttrib->core.pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_IN_TANGENT & 0x01) {
		pMesh->pInTangentAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_IN_TANGENT],
			&pCore->cornerAttribs,
			false, NULL, NULL
		);
		STUC_RETURN_ERR_IFNOT_COND(err, pMesh->pInTangentAttrib, "buf-mesh has no in-tangent attrib");
		pMesh->pInTangent = pMesh->pInTangentAttrib->core.pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_IN_T_SIGN & 0x01) {
		pMesh->pInTSignAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_IN_T_SIGN],
			&pCore->cornerAttribs,
			false, NULL, NULL
		);
		STUC_RETURN_ERR_IFNOT_COND(err, pMesh->pInTSignAttrib, "buf-mesh has no t-sign attrib");
		pMesh->pInTSign = pMesh->pInTSignAttrib->core.pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_ALPHA & 0x01) {
		pMesh->pAlphaAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_ALPHA],
			&pCore->cornerAttribs,
			false, NULL, NULL
		);
		STUC_RETURN_ERR_IFNOT_COND(err, pMesh->pAlphaAttrib, "buf-mesh has no alpha attrib");
		pMesh->pAlpha = pMesh->pAlphaAttrib->core.pData;
	}
	return err;
}

void stucAppendBufOnlySpecialAttribs(const StucAlloc *pAlloc, BufMesh *pBufMesh) {
	Mesh *pMesh = &pBufMesh->mesh;
	AttribArray *pAttribArr = &pMesh->core.cornerAttribs;
	Attrib *pAttrib = NULL;
	stucAppendAttrib(
		pAlloc,
		pAttribArr,
		&pAttrib,
		spBufAttribs[STUC_ATTRIB_SP_BUF_W],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_ORIGIN_MESH_BUF,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_F32,
		STUC_ATTRIB_USE_SCALAR
	);
	pAttrib = NULL;
	stucAppendAttrib(
		pAlloc,
		pAttribArr,
		&pAttrib,
		spBufAttribs[STUC_ATTRIB_SP_BUF_IN_NORMAL],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_ORIGIN_MESH_BUF,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_V3_F32,
		STUC_ATTRIB_USE_NORMAL
	);
	pAttrib = NULL;
	stucAppendAttrib(
		pAlloc,
		pAttribArr,
		&pAttrib,
		spBufAttribs[STUC_ATTRIB_SP_BUF_IN_TANGENT],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_ORIGIN_MESH_BUF,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_V3_F32,
		STUC_ATTRIB_USE_TANGENT
	);
	pAttrib = NULL;
	stucAppendAttrib(
		pAlloc,
		pAttribArr,
		&pAttrib,
		spBufAttribs[STUC_ATTRIB_SP_BUF_IN_T_SIGN],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_ORIGIN_MESH_BUF,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_F32,
		STUC_ATTRIB_USE_TSIGN
	);
	pAttrib = NULL;
	stucAppendAttrib(
		pAlloc,
		pAttribArr,
		&pAttrib,
		spBufAttribs[STUC_ATTRIB_SP_BUF_ALPHA],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_ORIGIN_MESH_BUF,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_F32,
		STUC_ATTRIB_USE_SCALAR
	);
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

Result stucAllocAttribsFromMeshArr(
	StucContext pCtx,
	Mesh *pDest,
	I32 srcCount,
	const Mesh *const *ppMeshSrcs,
	I32 activeSrc,
	bool setCommon,
	bool allocData,
	bool aliasData
) {
	Result err = STUC_SUCCESS;
	bool skipEdge = false;
#ifdef STUC_DISABLE_EDGES_IN_BUF
	skipEdge = pDest->core.type.type == STUC_OBJECT_DATA_MESH_BUF;
#endif
	for (I32 i = STUC_DOMAIN_FACE; i <= STUC_DOMAIN_VERT; ++i) {
		if (i == STUC_DOMAIN_EDGE && skipEdge) {
			continue;
		}
		err = stucAllocAttribs(
			pCtx,
			i,
			pDest,
			srcCount,
			ppMeshSrcs,
			activeSrc,
			setCommon,
			allocData,
			aliasData
		);
		STUC_RETURN_ERR_IFNOT(err, "");
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
		pAttrib->pData = pAlloc->pCalloc(dataLen, stucGetAttribSizeIntern(type));
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
	STUC_ASSERT("", pArr->count <= pArr->size);
	if (pArr->size == 0) {
		pArr->size = 1;
		pArr->pArr = pAlloc->pMalloc(sizeof(Attrib) * pArr->size);
	}
	else if (pArr->count == pArr->size) {
		pArr->size *= 2;
		pArr->pArr = pAlloc->pRealloc(pArr->pArr, sizeof(Attrib) * pArr->size);
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
	v##size##t##Equal(*(const V##size##_##t *)a, *(const V##size##_##t *)b)

bool stucCmpAttribs(const AttribCore *pA, I32 iA, const AttribCore *pB, I32 iB) {
	const void *pAData = stucAttribAsVoidConst(pA, iA);
	const void *pBData = stucAttribAsVoidConst(pB, iB);
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
	}
	STUC_ASSERT("invalid attrib type", false);
	return false;
}

I32 stucGetIdxInIndexedAttrib(
	const AttribIndexed *pDest,
	const AttribIndexed *pSrc,
	I32 srcIdx
) {
	for (I32 k = 0; k < pDest->count; ++k) {
		if (stucCmpAttribs(&pDest->core, k, &pSrc->core, srcIdx)) {
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
		STUC_ASSERT("", pArr);
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
	STUC_ASSERT("", pIndexedAttribArr->count <= pIndexedAttribArr->size);
	if (pIndexedAttribArr->count == pIndexedAttribArr->size) {
		pIndexedAttribArr->size *= 2;
		pCtx->alloc.pRealloc(pIndexedAttribArr->pArr, pIndexedAttribArr->size);
	}
	AttribIndexed *pIndexedAttrib = pIndexedAttribArr->pArr + pIndexedAttribArr->count;
	stucInitAttribCore(&pCtx->alloc, &pIndexedAttrib->core, pName, dataLen, type, use);
	if (dataLen) {
		pIndexedAttrib->size = dataLen;
	}
	pIndexedAttribArr->count++;
	return pIndexedAttrib;
}

Result stucAppendAndCopyIndexedAttrib(
	StucContext pCtx,
	const char *pName,
	AttribIndexedArr *pDestArr,
	const AttribIndexedArr *pSrcArr
) {
	Result err = STUC_SUCCESS;
	AttribIndexed *pSrc = stucGetAttribIndexedInternConst(pSrcArr, pName);
	STUC_RETURN_ERR_IFNOT_COND(err, pSrc, "");
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

void stucAppendToIndexedAttrib(
	StucContext pCtx,
	AttribIndexed *pDest,
	const AttribCore *pSrc,
	I32 srcIdx
) {
	STUC_ASSERT("", pDest->core.type == pSrc->type);
	STUC_ASSERT("", pDest->count <= pDest->size);
	if (!pDest->size) {
		STUC_ASSERT("", !pDest->count);
		pDest->size = 1;
		pDest->core.pData =
			pCtx->alloc.pCalloc(pDest->size, stucGetAttribSizeIntern(pDest->core.type));
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