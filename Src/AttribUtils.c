#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <float.h>

#include <AttribUtils.h>
#include <MathUtils.h>
#include <Context.h>
#include <Mesh.h>
#include <Error.h>

//special buf attribs should not be set by user,
//so unlike special attribs, they're hardcoded here
static char spBufAttribs[6][STUC_ATTRIB_NAME_MAX_LEN] = {
	"",
	"StucW",
	"StucInNormal",
	"StucInTangent",
	"StucInTSign",
	"StucAlpha"
};

void setDefaultSpecialAttribNames(StucContext pContext) {
	strcpy(pContext->spAttribs[1], "position");
	strcpy(pContext->spAttribs[2], "UVMap");
	strcpy(pContext->spAttribs[3], "normal");
	strcpy(pContext->spAttribs[4], "StucPreserve");
	strcpy(pContext->spAttribs[5], "StucPreserveReceive");
	strcpy(pContext->spAttribs[6], "StucPreserveVert");
	strcpy(pContext->spAttribs[7], "StucUsg");
	strcpy(pContext->spAttribs[8], "StucTangent");
	strcpy(pContext->spAttribs[9], "StucTSign");
	strcpy(pContext->spAttribs[10], "StucWScale");
	strcpy(pContext->spAttribs[11], "StucMaterialIndices");//TODO StucMatIdx is easier to type
}

#define LERP_SIMPLE(a, b, o) (b * o + (1.0 - o) * a)

//t is type, pD is attrib, i is idx, v is vector len (scalar is 1, v2 is 2, etc),
//and c is component (ie, x is 0, y is 1, z is 2, etc)
#define INDEX_ATTRIB(t, pD, i, v, c) ((t (*)[v])pD->pData)[i][c]
//TODO you'll need to make an unsigned wide macro as well when you implement
//proper unsigned attribs
#define INDEX_ATTRIB_WIDE(t, pD, i, v, c) (double)(((t (*)[v])pD->pData)[i][c])
//TODO add a way to pass the max value here rather than just using 255.0f,
//one may want to use 16 or 32 bit color depth
#define INDEX_ATTRIB_NORM(t, tMax, pD, i, v, c) \
	((double)((t (*)[v])pD->pData)[i][c] / (double)tMax)

#define CLAMP_AND_LERP(t, tMax, o, pA, iA, v, c, d) (\
	(d = LERP_SIMPLE(INDEX_ATTRIB_WIDE(t,pA,iA,v,c), d, o)),\
	(d = d < 0 ? 0 : (d > (double)tMax ? tMax : d)),\
	(t)(d)\
)

#define CLAMP_AND_LERP_NORM(t, tMax, o, pA, iA, v, c, d) (\
	(d = LERP_SIMPLE(INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c), d, o)),\
	(d = d < .0 ? .0 : (d > 1.0 ? 1.0 : d)),\
	(t)(d * (double)tMax)\
)

#define BLEND_REPLACE(t, tMax, o, pD, iD, pA, iA, pB, iB, v, c)\
	if (o == 1.0) {\
		INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pB,iB,v,c);\
	}\
	else if (o == .0) {\
		INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pA,iA,v,c);\
	}\
	else {\
		double b = INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c);\
		INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,b);\
	}

#define BLEND_MULTIPLY(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = INDEX_ATTRIB_WIDE(t,pA,iA,v,c) * INDEX_ATTRIB_WIDE(t,pB,iB,v,c);\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_MULTIPLY_NORM(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) *\
		INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c);\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_DIVIDE(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = INDEX_ATTRIB_WIDE(t,pB,iB,v,c) != (t)0 ?\
		INDEX_ATTRIB_WIDE(t,pA,iA,v,c) / INDEX_ATTRIB_WIDE(t,pB,iB,v,c) : (double)tMax;\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_DIVIDE_NORM(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = INDEX_ATTRIB(t,pB,iB,v,c) != (t)0 ?\
		(INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) / INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c)) : 1.0f;\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_ADD(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = INDEX_ATTRIB_WIDE(t,pA,iA,v,c) + INDEX_ATTRIB_WIDE(t,pB,iB,v,c);\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP(t,tMax,o,pA,iA,v,c,d);\
}

//TODO add clamping as an option
#define BLEND_SUBTRACT(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = INDEX_ATTRIB_WIDE(t,pA,iA,v,c) - INDEX_ATTRIB_WIDE(t,pB,iB,v,c);\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP(t,tMax,o,pA,iA,v,c,d);\
}

//TODO addsub result is slightly off from other programs
#define BLEND_ADD_SUB(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = INDEX_ATTRIB_WIDE(t,pA,iA,v,c) +\
		INDEX_ATTRIB_WIDE(t,pB,iB,v,c) - ((double)tMax - INDEX_ATTRIB_WIDE(t,pB,iB,v,c));\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_LIGHTEN(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	t d =\
			INDEX_ATTRIB(t,pA,iA,v,c) > INDEX_ATTRIB(t,pB,iB,v,c) ?\
			INDEX_ATTRIB(t,pA,iA,v,c) : INDEX_ATTRIB(t,pB,iB,v,c);\
	if (o == 1.0) {\
		INDEX_ATTRIB(t,pD,iD,v,c) = d;\
	}\
	else if (o == .0) {\
		INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pA,iA,v,c);\
	}\
	else {\
		double dNorm = (double)d / (double)tMax;\
		INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,dNorm);\
	}\
}

#define BLEND_DARKEN(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	t d =\
		INDEX_ATTRIB(t,pA,iA,v,c) < INDEX_ATTRIB(t,pB,iB,v,c) ?\
		INDEX_ATTRIB(t,pA,iA,v,c) : INDEX_ATTRIB(t,pB,iB,v,c);\
	if (o == 1.0) {\
		INDEX_ATTRIB(t,pD,iD,v,c) = d;\
	}\
	else if (o == .0) {\
		INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pA,iA,v,c);\
	}\
	else {\
		double dNorm = (double)d / (double)tMax;\
		INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,dNorm);\
	}\
}

#define BLEND_OVERLAY(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = (INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) < .5 ?\
	2.0 * INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) * INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c) :\
	1.0 - 2.0 * (1.0 - INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c)) *\
	(1.0 - INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c)));\
\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_SOFT_LIGHT(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = (INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c) < .5 ?\
\
	2.0 * INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) * INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c) +\
	INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) * INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) *\
	(1.0 - 2.0 * INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c)) :\
\
	2.0 * INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) * (1.0 - INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c)) +\
	sqrt(INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c)) * (2.0 * INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c) - 1.0));\
\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_COLOR_DODGE(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	double d = (1.0 - INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c));\
	d = d == .0 ? 1.0 : INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) / d;\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,d);\
}

#define DIVIDE_BY_SCALAR(t, pAttrib, idx, v, c, scalar) \
	INDEX_ATTRIB(t,pAttrib,idx,v,c) /= (t)scalar;

#define LERP_SCALAR(t, pD, iD, pA, iA, pB, iB, a) { \
	float aInverse = 1.0f - a; \
	INDEX_ATTRIB(t, pD, iD, 1, 0) = INDEX_ATTRIB(t, pA, iA, 1, 0) * aInverse + INDEX_ATTRIB(t, pB, iB, 1, 0) * a; \
}

#define LERP_V2(t, pD, iD, pA, iA, pB, iB, a) { \
	float aInverse = 1.0f - a; \
	INDEX_ATTRIB(t, pD, iD, 2, 0) = INDEX_ATTRIB(t, pA, iA, 2, 0) * aInverse + INDEX_ATTRIB(t, pB, iB, 2, 0) * a; \
	INDEX_ATTRIB(t, pD, iD, 2, 1) = INDEX_ATTRIB(t, pA, iA, 2, 1) * aInverse + INDEX_ATTRIB(t, pB, iB, 2, 1) * a; \
}

#define LERP_V3(t, pD, iD, pA, iA, pB, iB, a) { \
	float aInverse = 1.0f - a; \
	INDEX_ATTRIB(t, pD, iD, 3, 0) = INDEX_ATTRIB(t, pA, iA, 3, 0) * aInverse + INDEX_ATTRIB(t, pB, iB, 3, 0) * a; \
	INDEX_ATTRIB(t, pD, iD, 3, 1) = INDEX_ATTRIB(t, pA, iA, 3, 1) * aInverse + INDEX_ATTRIB(t, pB, iB, 3, 1) * a; \
	INDEX_ATTRIB(t, pD, iD, 3, 2) = INDEX_ATTRIB(t, pA, iA, 3, 2) * aInverse + INDEX_ATTRIB(t, pB, iB, 3, 2) * a; \
}

#define LERP_V4(t, pD, iD, pA, iA, pB, iB, a) { \
	float aInverse = 1.0f - a; \
	INDEX_ATTRIB(t, pD, iD, 4, 0) = INDEX_ATTRIB(t, pA, iA, 4, 0) * aInverse + INDEX_ATTRIB(t, pB, iB, 4, 0) * a; \
	INDEX_ATTRIB(t, pD, iD, 4, 1) = INDEX_ATTRIB(t, pA, iA, 4, 1) * aInverse + INDEX_ATTRIB(t, pB, iB, 4, 1) * a; \
	INDEX_ATTRIB(t, pD, iD, 4, 2) = INDEX_ATTRIB(t, pA, iA, 4, 2) * aInverse + INDEX_ATTRIB(t, pB, iB, 4, 2) * a; \
	INDEX_ATTRIB(t, pD, iD, 4, 3) = INDEX_ATTRIB(t, pA, iA, 4, 3) * aInverse + INDEX_ATTRIB(t, pB, iB, 4, 3) * a; \
}

#define TRI_INTERPOLATE_SCALAR(t, pD, iD, pS, iA, iB, iC, bc)\
	INDEX_ATTRIB(t, pD, iD, 1, 0) = INDEX_ATTRIB(t, pS, iA, 1, 0) * bc.d[0];\
 	INDEX_ATTRIB(t, pD, iD, 1, 0) += INDEX_ATTRIB(t, pS, iB, 1, 0) * bc.d[1];\
	INDEX_ATTRIB(t, pD, iD, 1, 0) += INDEX_ATTRIB(t, pS, iC, 1, 0) * bc.d[2];\
	INDEX_ATTRIB(t, pD, iD, 1, 0) /= bc.d[0] + bc.d[1] + bc.d[2];

#define TRI_INTERPOLATE_V2(t, pD, iD, pS, iA, iB, iC, bc) {\
	INDEX_ATTRIB(t,pD,iD,2,0) = INDEX_ATTRIB(t,pS,iA,2,0) * bc.d[0];\
	INDEX_ATTRIB(t,pD,iD,2,1) = INDEX_ATTRIB(t,pS,iA,2,1) * bc.d[0];\
	INDEX_ATTRIB(t,pD,iD,2,0) += INDEX_ATTRIB(t,pS,iB,2,0) * bc.d[1];\
	INDEX_ATTRIB(t,pD,iD,2,1) += INDEX_ATTRIB(t,pS,iB,2,1) * bc.d[1];\
	INDEX_ATTRIB(t,pD,iD,2,0) += INDEX_ATTRIB(t,pS,iC,2,0) * bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,2,1) += INDEX_ATTRIB(t,pS,iC,2,1) * bc.d[2];\
	float sum = bc.d[0] + bc.d[1] + bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,2,0) /= sum;\
	INDEX_ATTRIB(t,pD,iD,2,1) /= sum;\
}

#define TRI_INTERPOLATE_V3(t, pD, iD, pS, iA, iB, iC, bc) {\
	INDEX_ATTRIB(t,pD,iD,3,0) = INDEX_ATTRIB(t,pS,iA,3,0) * bc.d[0];\
	INDEX_ATTRIB(t,pD,iD,3,1) = INDEX_ATTRIB(t,pS,iA,3,1) * bc.d[0];\
	INDEX_ATTRIB(t,pD,iD,3,2) = INDEX_ATTRIB(t,pS,iA,3,2) * bc.d[0];\
	INDEX_ATTRIB(t,pD,iD,3,0) += INDEX_ATTRIB(t,pS,iB,3,0) * bc.d[1];\
	INDEX_ATTRIB(t,pD,iD,3,1) += INDEX_ATTRIB(t,pS,iB,3,1) * bc.d[1];\
	INDEX_ATTRIB(t,pD,iD,3,2) += INDEX_ATTRIB(t,pS,iB,3,2) * bc.d[1];\
	INDEX_ATTRIB(t,pD,iD,3,0) += INDEX_ATTRIB(t,pS,iC,3,0) * bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,3,1) += INDEX_ATTRIB(t,pS,iC,3,1) * bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,3,2) += INDEX_ATTRIB(t,pS,iC,3,2) * bc.d[2];\
	float sum = bc.d[0] + bc.d[1] + bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,3,0) /= sum;\
	INDEX_ATTRIB(t,pD,iD,3,1) /= sum;\
	INDEX_ATTRIB(t,pD,iD,3,2) /= sum;\
}

#define TRI_INTERPOLATE_V4(t, pD, iD, pS, iA, iB, iC, bc) {\
	INDEX_ATTRIB(t,pD,iD,4,0) = INDEX_ATTRIB(t,pS,iA,4,0) * bc.d[0];\
	INDEX_ATTRIB(t,pD,iD,4,1) = INDEX_ATTRIB(t,pS,iA,4,1) * bc.d[0];\
	INDEX_ATTRIB(t,pD,iD,4,2) = INDEX_ATTRIB(t,pS,iA,4,2) * bc.d[0];\
	INDEX_ATTRIB(t,pD,iD,4,3) = INDEX_ATTRIB(t,pS,iA,4,3) * bc.d[0];\
	INDEX_ATTRIB(t,pD,iD,4,0) += INDEX_ATTRIB(t,pS,iB,4,0) * bc.d[1];\
	INDEX_ATTRIB(t,pD,iD,4,1) += INDEX_ATTRIB(t,pS,iB,4,1) * bc.d[1];\
	INDEX_ATTRIB(t,pD,iD,4,2) += INDEX_ATTRIB(t,pS,iB,4,2) * bc.d[1];\
	INDEX_ATTRIB(t,pD,iD,4,3) += INDEX_ATTRIB(t,pS,iB,4,3) * bc.d[1];\
	INDEX_ATTRIB(t,pD,iD,4,0) += INDEX_ATTRIB(t,pS,iC,4,0) * bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,4,1) += INDEX_ATTRIB(t,pS,iC,4,1) * bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,4,2) += INDEX_ATTRIB(t,pS,iC,4,2) * bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,4,3) += INDEX_ATTRIB(t,pS,iC,4,3) * bc.d[2];\
	float sum = bc.d[0] + bc.d[1] + bc.d[2];\
	INDEX_ATTRIB(t,pD,iD,4,0) /= sum;\
	INDEX_ATTRIB(t,pD,iD,4,1) /= sum;\
	INDEX_ATTRIB(t,pD,iD,4,2) /= sum;\
	INDEX_ATTRIB(t,pD,iD,4,3) /= sum;\
}

int32_t getAttribSize(AttribType type) {
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

Attrib *getAttrib(char *pName, AttribArray *pAttribs) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		if (!strncmp(pName, pAttribs->pArr[i].name,
		             STUC_ATTRIB_NAME_MAX_LEN)) {
			return pAttribs->pArr + i;
		}
	}
	return NULL;
}

V3_F32 *attribAsV3(Attrib *pAttrib, int32_t idx) {
	return (V3_F32 *)pAttrib->pData + idx;
}

V2_F32 *attribAsV2(Attrib *pAttrib, int32_t idx) {
	return (V2_F32 *)pAttrib->pData + idx;
}

int32_t *attribAsI32(Attrib *pAttrib, int32_t idx) {
	return (int32_t *)pAttrib->pData + idx;
}

int8_t *attribAsI8(Attrib *pAttrib, int32_t idx) {
	return (int8_t *)pAttrib->pData + idx;
}

void *attribAsVoid(Attrib *pAttrib, int32_t idx) {
	switch (pAttrib->type) {
		case STUC_ATTRIB_I8:
			return ((int8_t *)pAttrib->pData) + idx;
		case STUC_ATTRIB_I16:
			return ((int16_t *)pAttrib->pData) + idx;
		case STUC_ATTRIB_I32:
			return ((int32_t *)pAttrib->pData) + idx;
		case STUC_ATTRIB_I64:
			return ((int64_t *)pAttrib->pData) + idx;
		case STUC_ATTRIB_F32:
			return ((float *)pAttrib->pData) + idx;
		case STUC_ATTRIB_F64:
			return ((double *)pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_I8:
			return ((int8_t (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_I16:
			return ((int16_t (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_I32:
			return ((int32_t (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_I64:
			return ((int64_t (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_F32:
			return ((float (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V2_F64:
			return ((double (*)[2])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_I8:
			return ((int8_t (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_I16:
			return ((int16_t (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_I32:
			return ((int32_t (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_I64:
			return ((int64_t (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_F32:
			return ((float (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V3_F64:
			return ((double (*)[3])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_I8:
			return ((int8_t (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_I16:
			return ((int16_t (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_I32:
			return ((int32_t (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_I64:
			return ((int64_t (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_F32:
			return ((float (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_V4_F64:
			return ((double (*)[4])pAttrib->pData) + idx;
		case STUC_ATTRIB_STRING:
			return ((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pAttrib->pData) + idx;
		default:
			STUC_ASSERT("", false);
			return NULL;
	}
}

int32_t copyAttrib(Attrib *pDest, int32_t iDest,
                   Attrib *pSrc, int32_t iSrc) {
	if (pSrc->origin == STUC_ATTRIB_DONT_COPY) {
		return 0;
	}
	if (pSrc->type != pDest->type) {
		return 1;
	}
	switch (pSrc->type) {
		case STUC_ATTRIB_I8:
			((int8_t *)pDest->pData)[iDest] = ((int8_t *)pSrc->pData)[iSrc];
			break;
		case STUC_ATTRIB_I16:
			((int16_t *)pDest->pData)[iDest] = ((int16_t *)pSrc->pData)[iSrc];
			break;
		case STUC_ATTRIB_I32:
			((int32_t *)pDest->pData)[iDest] = ((int32_t *)pSrc->pData)[iSrc];
			break;
		case STUC_ATTRIB_I64:
			((int64_t *)pDest->pData)[iDest] = ((int64_t *)pSrc->pData)[iSrc];
			break;
		case STUC_ATTRIB_F32:
			((float *)pDest->pData)[iDest] = ((float *)pSrc->pData)[iSrc];
			break;
		case STUC_ATTRIB_F64:
			((double *)pDest->pData)[iDest] = ((double *)pSrc->pData)[iSrc];
			break;
		case STUC_ATTRIB_V2_I8:
			memcpy(((int8_t (*)[2])pDest->pData)[iDest],
			       ((int8_t (*)[2])pSrc->pData)[iSrc], sizeof(int8_t[2]));
			break;
		case STUC_ATTRIB_V2_I16:
			memcpy(((int16_t (*)[2])pDest->pData)[iDest],
			       ((int16_t (*)[2])pSrc->pData)[iSrc], sizeof(int16_t[2]));
			break;
		case STUC_ATTRIB_V2_I32:
			memcpy(((int32_t (*)[2])pDest->pData)[iDest],
			       ((int32_t (*)[2])pSrc->pData)[iSrc], sizeof(int32_t[2]));
			break;
		case STUC_ATTRIB_V2_I64:
			memcpy(((int64_t (*)[2])pDest->pData)[iDest],
			       ((int64_t (*)[2])pSrc->pData)[iSrc], sizeof(int64_t[2]));
			break;
		case STUC_ATTRIB_V2_F32:
			memcpy(((float (*)[2])pDest->pData)[iDest],
			       ((float (*)[2])pSrc->pData)[iSrc], sizeof(float[2]));
			break;
		case STUC_ATTRIB_V2_F64:
			memcpy(((double (*)[2])pDest->pData)[iDest],
			       ((double (*)[2])pSrc->pData)[iSrc], sizeof(double[2]));
			break;
		case STUC_ATTRIB_V3_I8:
			memcpy(((int8_t (*)[3])pDest->pData)[iDest],
			       ((int8_t (*)[3])pSrc->pData)[iSrc], sizeof(int8_t[3]));
			break;
		case STUC_ATTRIB_V3_I16:
			memcpy(((int16_t (*)[3])pDest->pData)[iDest],
			       ((int16_t (*)[3])pSrc->pData)[iSrc], sizeof(int16_t[3]));
			break;
		case STUC_ATTRIB_V3_I32:
			memcpy(((int32_t (*)[3])pDest->pData)[iDest],
			       ((int32_t (*)[3])pSrc->pData)[iSrc], sizeof(int32_t[3]));
			break;
		case STUC_ATTRIB_V3_I64:
			memcpy(((int64_t (*)[3])pDest->pData)[iDest],
			       ((int64_t (*)[3])pSrc->pData)[iSrc], sizeof(int64_t[3]));
			break;
		case STUC_ATTRIB_V3_F32:
			memcpy(((float (*)[3])pDest->pData)[iDest],
			       ((float (*)[3])pSrc->pData)[iSrc], sizeof(float[3]));
			break;
		case STUC_ATTRIB_V3_F64:
			memcpy(((double (*)[3])pDest->pData)[iDest],
			       ((double (*)[3])pSrc->pData)[iSrc], sizeof(double[3]));
			break;
		case STUC_ATTRIB_V4_I8:
			memcpy(((int8_t (*)[4])pDest->pData)[iDest],
			       ((int8_t (*)[4])pSrc->pData)[iSrc], sizeof(int8_t[4]));
			break;
		case STUC_ATTRIB_V4_I16:
			memcpy(((int16_t (*)[4])pDest->pData)[iDest],
			       ((int16_t (*)[4])pSrc->pData)[iSrc], sizeof(int16_t[4]));
			break;
		case STUC_ATTRIB_V4_I32:
			memcpy(((int32_t (*)[4])pDest->pData)[iDest],
			       ((int32_t (*)[4])pSrc->pData)[iSrc], sizeof(int32_t[4]));
			break;
		case STUC_ATTRIB_V4_I64:
			memcpy(((int64_t (*)[4])pDest->pData)[iDest],
			       ((int64_t (*)[4])pSrc->pData)[iSrc], sizeof(int64_t[4]));
			break;
		case STUC_ATTRIB_V4_F32:
			memcpy(((float (*)[4])pDest->pData)[iDest],
			       ((float (*)[4])pSrc->pData)[iSrc], sizeof(float[4]));
			break;
		case STUC_ATTRIB_V4_F64:
			memcpy(((double (*)[4])pDest->pData)[iDest],
			       ((double (*)[4])pSrc->pData)[iSrc], sizeof(double[4]));
			break;
		case STUC_ATTRIB_STRING:
			memcpy(((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pDest->pData)[iDest],
			       ((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pSrc->pData)[iSrc],
				   sizeof(double[STUC_ATTRIB_STRING_MAX_LEN]));
			break;
	}
	return 0;
}

void copyAllAttribs(AttribArray *pDest, int32_t iDest,
                    AttribArray *pSrc, int32_t iSrc) {
	for (int32_t i = 0; i < pDest->count; ++i) {
		Attrib *pSrcAttrib = getAttrib(pDest->pArr[i].name, pSrc);
		if (pSrcAttrib) {
			copyAttrib(pDest->pArr + i, iDest, pSrcAttrib, iSrc);
		}
	}
}

void stucSetTypeDefaultConfig(StucContext pContext) {
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
	pContext->typeDefaults = config;
}

StucTypeDefault *getTypeDefaultConfig(StucTypeDefaultConfig *pConfig,
                                      AttribType type) {
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

StucCommonAttrib *getCommonAttrib(StucCommonAttrib *pAttribs, int32_t attribCount,
                                  char *pName) {
	//this is it's own function (rather than a general getAttrib function),
	//because of the below todo:
	//TODO replace linear search with hash table.
	for (int32_t i = 0; i < attribCount; ++i) {
		if (!strncmp(pName, pAttribs[i].name, STUC_ATTRIB_NAME_MAX_LEN)) {
			return pAttribs + i;
		}
	}
	return NULL;
}

void lerpAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrcA,
                int32_t iSrcA, Attrib *pSrcB, int32_t iSrcB, float alpha) {
	if (pDest->type != pSrcA->type ||
		pDest->type != pSrcB->type) {
		printf("Type mismatch in interpolateAttrib\n");
		//TODO remove all uses of abort(), and add proper exception handling
		abort();
	}
	AttribType type = pDest->type;
	switch (type) {
		case STUC_ATTRIB_I8:
			LERP_SCALAR(int8_t, pDest, iDest, pSrcA, iSrcA, pSrcB,
			                   iSrcB, alpha);
			break;
		case STUC_ATTRIB_I16:
			LERP_SCALAR(int16_t, pDest, iDest, pSrcA, iSrcA, pSrcB,
			                   iSrcB, alpha);
			break;
		case STUC_ATTRIB_I32:
			LERP_SCALAR(int32_t, pDest, iDest, pSrcA, iSrcA, pSrcB,
			                   iSrcB, alpha);
			break;
		case STUC_ATTRIB_I64:
			LERP_SCALAR(int64_t, pDest, iDest, pSrcA, iSrcA, pSrcB,
			                   iSrcB, alpha);
			break;
		case STUC_ATTRIB_F32:
			LERP_SCALAR(float, pDest, iDest, pSrcA, iSrcA, pSrcB,
			                   iSrcB, alpha);
			break;
		case STUC_ATTRIB_F64:
			LERP_SCALAR(double, pDest, iDest, pSrcA, iSrcA, pSrcB,
			                   iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_I8:
			LERP_V2(int8_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_I16:
			LERP_V2(int16_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_I32:
			LERP_V2(int32_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_I64:
			LERP_V2(int64_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_F32:
			LERP_V2(float, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V2_F64:
			LERP_V2(double, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_I8:
			LERP_V3(int8_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_I16:
			LERP_V3(int16_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_I32:
			LERP_V3(int32_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_I64:
			LERP_V3(int64_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_F32:
			LERP_V3(float, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V3_F64:
			LERP_V3(double, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_I8:
			//TODO using unsigned here temporarily for vert color
			//make a proper set of attrib types for unsigned types
			LERP_V4(uint8_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_I16:
			LERP_V4(int16_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_I32:
			LERP_V4(int32_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_I64:
			LERP_V4(int64_t, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_F32:
			LERP_V4(float, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_V4_F64:
			LERP_V4(double, pDest, iDest, pSrcA, iSrcA, pSrcB, iSrcB, alpha);
			break;
		case STUC_ATTRIB_STRING:
			break;
	}
}

void triInterpolateAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrc,
                          int32_t iSrcA, int32_t iSrcB, int32_t iSrcC, V3_F32 bc) {
	if (pDest->type != pSrc->type) {
		printf("Type mismatch in interpolateAttrib\n");
		//TODO remove all uses of abort(), and add proper exception handling
		abort();
	}
	AttribType type = pDest->type;
	switch (type) {
		case STUC_ATTRIB_I8:
			TRI_INTERPOLATE_SCALAR(int8_t, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case STUC_ATTRIB_I16:
			TRI_INTERPOLATE_SCALAR(int16_t, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case STUC_ATTRIB_I32:
			TRI_INTERPOLATE_SCALAR(int32_t, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case STUC_ATTRIB_I64:
			TRI_INTERPOLATE_SCALAR(int64_t, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case STUC_ATTRIB_F32:
			TRI_INTERPOLATE_SCALAR(float, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case STUC_ATTRIB_F64:
			TRI_INTERPOLATE_SCALAR(double, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_I8:
			TRI_INTERPOLATE_V2(int8_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_I16:
			TRI_INTERPOLATE_V2(int16_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_I32:
			TRI_INTERPOLATE_V2(int32_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_I64:
			TRI_INTERPOLATE_V2(int64_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_F32:
			TRI_INTERPOLATE_V2(float, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V2_F64:
			TRI_INTERPOLATE_V2(double, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_I8:
			TRI_INTERPOLATE_V3(int8_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_I16:
			TRI_INTERPOLATE_V3(int16_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_I32:
			TRI_INTERPOLATE_V3(int32_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_I64:
			TRI_INTERPOLATE_V3(int64_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_F32:
			TRI_INTERPOLATE_V3(float, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V3_F64:
			TRI_INTERPOLATE_V3(double, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_I8:
			//TODO using unsigned here temporarily for vert color
			//make a proper set of attrib types for unsigned types
			TRI_INTERPOLATE_V4(uint8_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_I16:
			TRI_INTERPOLATE_V4(int16_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_I32:
			TRI_INTERPOLATE_V4(int32_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_I64:
			TRI_INTERPOLATE_V4(int64_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_F32:
			TRI_INTERPOLATE_V4(float, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_V4_F64:
			TRI_INTERPOLATE_V4(double, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case STUC_ATTRIB_STRING:
			break;
	}
}

static void appendOnNonString() {
	printf("Blend mode append reached on non string attrib!\n");
	abort();
}

//TODO this name should not be plural
void blendAttribs(Attrib *pD, int32_t iD, Attrib *pA, int32_t iA,
                  Attrib *pB, int32_t iB, StucBlendConfig blendConfig) {
	AttribType type = pD->type;
	double opacity = (double)blendConfig.opacity; //casting as blending is done with doubles
	switch (type) {
		case STUC_ATTRIB_I8:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_I16:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_I32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_I64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_F32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_F64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_I8:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_I16:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_I32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_I64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_F32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_F64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_I8:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_I16:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_I32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_I64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_F32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_F64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_I8:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					switch (pD->use) {
					//TODO add a GENERIC item, in addition to NONE (none indicates not set),
					//and use that here. NONE should probably cause an error, or just default to
					//generic blending?
					case STUC_ATTRIB_USE_NONE:
						BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
						BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
						BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
						BLEND_REPLACE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
						break;
					case STUC_ATTRIB_USE_COLOR:
						BLEND_REPLACE(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
						BLEND_REPLACE(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
						BLEND_REPLACE(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
						BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
						break;
					}
					break;
				case STUC_BLEND_MULTIPLY:
					switch (pD->use) {
						//TODO have an option in blendConfig for whether to normalize or not.
						//This will only be relevent for non-color attribs. As if use is color,
						//attribs will always be normalized
						case STUC_ATTRIB_USE_NONE:
							BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_MULTIPLY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_MULTIPLY_NORM(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_MULTIPLY_NORM(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_MULTIPLY_NORM(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_DIVIDE:
					switch (pD->use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_DIVIDE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_DIVIDE_NORM(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_DIVIDE_NORM(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_DIVIDE_NORM(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_ADD:
					switch (pD->use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_ADD(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_ADD(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_ADD(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_ADD(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_SUBTRACT:
					switch (pD->use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_SUBTRACT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_SUBTRACT(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_SUBTRACT(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_SUBTRACT(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_ADD_SUB:
					switch (pD->use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_ADD_SUB(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_ADD_SUB(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_ADD_SUB(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_ADD_SUB(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							//TODO add options for replacing or blending alpha channel
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_LIGHTEN:
					switch (pD->use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_LIGHTEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_LIGHTEN(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_LIGHTEN(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_LIGHTEN(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_DARKEN:
					switch (pD->use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_DARKEN(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_DARKEN(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_DARKEN(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_DARKEN(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_OVERLAY:
					switch (pD->use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_OVERLAY(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_OVERLAY(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_OVERLAY(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_OVERLAY(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_SOFT_LIGHT:
					switch (pD->use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_SOFT_LIGHT(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_SOFT_LIGHT(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_SOFT_LIGHT(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_SOFT_LIGHT(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_COLOR_DODGE:
					switch (pD->use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_COLOR_DODGE(int8_t, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_COLOR_DODGE(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_COLOR_DODGE(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_COLOR_DODGE(uint8_t, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(uint8_t, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_I16:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(int16_t, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_I32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(int32_t, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_I64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(int64_t, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_F32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(float, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_F64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(double, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_STRING:
			//TODO add string append
			break;
	}
}

void divideAttribByScalarInt(Attrib *pAttrib, int32_t idx, uint64_t scalar) {
	switch (pAttrib->type) {
		case STUC_ATTRIB_I8:
			DIVIDE_BY_SCALAR(int8_t, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_I16:
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_I32:
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_I64:
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_F32:
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_F64:
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 1, 0, scalar);
			break;
		case STUC_ATTRIB_V2_I8:
			DIVIDE_BY_SCALAR(int8_t, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(int8_t, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_I16:
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_I32:
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_I64:
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_F32:
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V2_F64:
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 2, 0, scalar);
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 2, 1, scalar);
			break;
		case STUC_ATTRIB_V3_I8:
			DIVIDE_BY_SCALAR(int8_t, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(int8_t, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(int8_t, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_I16:
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_I32:
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_I64:
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_F32:
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V3_F64:
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 3, 0, scalar);
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 3, 1, scalar);
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 3, 2, scalar);
			break;
		case STUC_ATTRIB_V4_I8:
			DIVIDE_BY_SCALAR(uint8_t, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(uint8_t, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(uint8_t, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(uint8_t, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_I16:
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(int16_t, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_I32:
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(int32_t, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_I64:
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(int64_t, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_F32:
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(float, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_V4_F64:
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 4, 0, scalar);
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 4, 1, scalar);
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 4, 2, scalar);
			DIVIDE_BY_SCALAR(double, pAttrib, idx, 4, 3, scalar);
			break;
		case STUC_ATTRIB_STRING:
			STUC_ASSERT("Can't divide a string by 1", false);
			break;
	}
}

static
AttribArray *getAttribArrFromDomain(StucMesh *pMesh, StucDomain domain) {
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
		return NULL;
	}
}

void allocAttribs(StucAlloc *pAlloc, AttribArray *pDest,
                  int32_t srcCount, Mesh **ppSrcArr,
				  int32_t dataLen, StucDomain domain, bool setCommon) {
	pDest->size = 2;
	pDest->pArr = pAlloc->pCalloc(pDest->size, sizeof(Attrib));
	for (int32_t i = 0; i < srcCount; ++i) {
		AttribArray *pSrc = getAttribArrFromDomain(&ppSrcArr[i]->core, domain);
		if (pSrc && pSrc->count) {
			for (int32_t j = 0; j < pSrc->count; ++j) {
				if (pSrc->pArr[j].origin == STUC_ATTRIB_DONT_COPY) {
					continue;
				}
				Attrib *pAttrib = getAttrib(pSrc->pArr[j].name, pDest);
				if (pAttrib) {
					//if attribute already exists in destination,
					//set origin to common, then skip
					if (setCommon) {
						pAttrib->origin = STUC_ATTRIB_ORIGIN_COMMON;
					}
					continue;
				}
				STUC_ASSERT("", pDest->count <= pDest->size);
				if (pDest->count == pDest->size) {
					pDest->size *= 2;
					pDest->pArr = pAlloc->pRealloc(pDest->pArr, pDest->size * sizeof(Attrib));
				}
				pDest->pArr[pDest->count].type = pSrc->pArr[j].type;
				memcpy(pDest->pArr[pDest->count].name, pSrc->pArr[j].name,
				       STUC_ATTRIB_NAME_MAX_LEN);
				pDest->pArr[pDest->count].origin = pSrc->pArr[j].origin;
				pDest->pArr[pDest->count].use = pSrc->pArr[j].use;
				pDest->pArr[pDest->count].interpolate = pSrc->pArr[j].interpolate;
				int32_t attribSize = getAttribSize(pSrc->pArr[j].type);
				pDest->pArr[pDest->count].pData = pAlloc->pCalloc(dataLen, attribSize);
				pDest->count++;
			}
		}
	}
	if (!pDest->count) {
		pAlloc->pFree(pDest->pArr);
		pDest->pArr = NULL;
		return;
	}
}

static
int32_t checkIfSpecialAttrib(StucContext pContext, Attrib *pAttrib) {
	int32_t size = sizeof(pContext->spAttribs) / STUC_ATTRIB_NAME_MAX_LEN;
	for (int32_t i = 1; i < size; ++i) {
		if (!strncmp(pAttrib->name, pContext->spAttribs[i], STUC_ATTRIB_NAME_MAX_LEN)) {
			return i;
		}
	}
	return -1;
}

static
int32_t checkIfSpecialBufAttrib(Attrib *pAttrib) {
	int32_t size = sizeof(spBufAttribs) / STUC_ATTRIB_NAME_MAX_LEN;
	for (int32_t i = 1; i < size; ++i) {
		if (!strncmp(pAttrib->name, spBufAttribs[i], STUC_ATTRIB_NAME_MAX_LEN)) {
			return i;
		}
	}
	return -1;
}

//differs from regular func, as it checks if special attrib
//by comparing pointers, to avoid needing to compare a bunch of strings
static
SpecialAttrib quickCheckIfSpecialAttrib(Mesh *pMesh, Attrib *pAttrib) {
	if (pAttrib->pData == pMesh->pVerts) {
		return STUC_ATTRIB_SP_VERTS;
	}
	else if (pAttrib->pData == pMesh->pUvs) {
		return STUC_ATTRIB_SP_UVS;
	}
	else if (pAttrib->pData == pMesh->pNormals) {
		return STUC_ATTRIB_SP_NORMALS;
	}
	else if (pAttrib->pData == pMesh->pEdgePreserve) {
		return STUC_ATTRIB_SP_PRESERVE;
	}
	else if (pAttrib->pData == pMesh->pEdgeReceive) {
		return STUC_ATTRIB_SP_RECEIVE;
	}
	else if (pAttrib->pData == pMesh->pVertPreserve) {
		return STUC_ATTRIB_SP_PRESERVE_VERT;
	}
	else if (pAttrib->pData == pMesh->pUsg) {
		return STUC_ATTRIB_SP_USG;
	}
	else if (pAttrib->pData == pMesh->pTangents) {
		return STUC_ATTRIB_SP_TANGENTS;
	}
	else if (pAttrib->pData == pMesh->pTSigns) {
		return STUC_ATTRIB_SP_TSIGNS;
	}
	else if (pAttrib->pData == pMesh->pWScale) {
		return STUC_ATTRIB_SP_WSCALE;
	}
	else if (pAttrib->pData == pMesh->pMatIdx) {
		return STUC_ATTRIB_SP_MAT_IDX;
	}
	return STUC_ATTRIB_SP_NONE;
}

static
SpecialBufAttrib quickCheckIfSpecialBufAttrib(BufMesh *pMesh, Attrib *pAttrib) {
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
void reassignIfSpecial(Mesh *pMesh, Attrib *pAttrib, SpecialAttrib special) {
	switch (special) {
		case (STUC_ATTRIB_SP_NONE):
			break;
		case (STUC_ATTRIB_SP_VERTS):
			pMesh->pVerts = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_UVS):
			pMesh->pUvs = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_NORMALS):
			pMesh->pNormals = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_PRESERVE):
			pMesh->pEdgePreserve = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_RECEIVE):
			pMesh->pEdgeReceive = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_PRESERVE_VERT):
			pMesh->pVertPreserve = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_USG):
			pMesh->pUsg = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_TANGENTS):
			pMesh->pTangents = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_TSIGNS):
			pMesh->pTSigns = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_WSCALE):
			pMesh->pWScale = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_MAT_IDX):
			pMesh->pMatIdx = pAttrib->pData;
			break;
	}
}

static
void reassignIfSpecialBuf(BufMesh *pMesh, Attrib *pAttrib, SpecialBufAttrib special) {
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

void reallocAttribs(const StucAlloc *pAlloc, Mesh *pMesh,
                    AttribArray *pAttribArr, const int32_t newLen) {
	STUC_ASSERT("", newLen >= 0 && newLen < 100000000);
	for (int32_t i = 0; i < pAttribArr->count; ++i) {
		Attrib *pAttrib = pAttribArr->pArr + i;
		SpecialAttrib special = quickCheckIfSpecialAttrib(pMesh, pAttrib);
		SpecialBufAttrib specialBuf = quickCheckIfSpecialBufAttrib((BufMesh *)pMesh, pAttrib);
		//Check entry is valid
		STUC_ASSERT("", pAttrib->interpolate % 2 == pAttrib->interpolate);
		int8_t oldFirstElement = *(int8_t *)attribAsVoid(pAttrib, 0);
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData =
			pAlloc->pRealloc(pAttrib->pData, attribSize * newLen);
		int8_t newFirstElement = *(int8_t *)attribAsVoid(pAttrib, 0);
		STUC_ASSERT("", newFirstElement == oldFirstElement);
		reassignIfSpecial(pMesh, pAttrib, special);
		reassignIfSpecialBuf((BufMesh *)pMesh, pAttrib, specialBuf);
		STUC_ASSERT("", i >= 0 && i < pAttribArr->count);
	}
}

void reallocAndMoveAttribs(const StucAlloc *pAlloc, BufMesh *pMesh,
                           AttribArray *pAttribArr, const int32_t start,
						   const int32_t offset, const int32_t lenToCopy,
						   const int32_t newLen) {
	STUC_ASSERT("", newLen >= 0 && newLen < 100000000);
	STUC_ASSERT("", start >= 0 && start < newLen);
	for (int32_t i = 0; i < pAttribArr->count; ++i) {
		Attrib *pAttrib = pAttribArr->pArr + i;
		SpecialAttrib special = quickCheckIfSpecialAttrib((Mesh *)pMesh, pAttrib);
		SpecialBufAttrib specialBuf = quickCheckIfSpecialBufAttrib(pMesh, pAttrib);
		//Check entry is valid
		STUC_ASSERT("", pAttrib->interpolate % 2 == pAttrib->interpolate);
		int8_t oldFirstElement =
			*(int8_t *)attribAsVoid(pAttrib, start);
		int8_t oldLastElement =
			*(int8_t *)attribAsVoid(pAttrib, start + lenToCopy - 1);
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData =
			pAlloc->pRealloc(pAttrib->pData, attribSize * newLen);
		if (lenToCopy) {
			memmove(attribAsVoid(pAttrib, start + offset),
					attribAsVoid(pAttrib, start), attribSize * lenToCopy);
			int8_t newFirstElement =
				*(int8_t *)attribAsVoid(pAttrib, start + offset);
			int8_t newLastElement =
				*(int8_t *)attribAsVoid(pAttrib, start + offset + lenToCopy - 1);
			STUC_ASSERT("", newFirstElement == oldFirstElement);
			STUC_ASSERT("", newLastElement == oldLastElement);
		}
		reassignIfSpecial((Mesh *)pMesh, pAttrib, special);
		reassignIfSpecialBuf((BufMesh *)pMesh, pAttrib, specialBuf);
		STUC_ASSERT("", i >= 0 && i < pAttribArr->count);
	}
}

void setSpecialAttribs(StucContext pContext, Mesh *pMesh, UBitField16 flags) {
	//TODO replace hard coded attribute pContext->spAttribs with function parameters.
	//User can specify which attributes should be treated as vert, uv, and normal.

	StucMesh *pCore = &pMesh->core;
	if (flags >> STUC_ATTRIB_SP_VERTS & 0x01) {
		//TODO this should return STUC_ERROR instead of an assert
		pMesh->pVertAttrib =
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_VERTS], &pCore->vertAttribs);
		STUC_ASSERT("", pMesh->pVertAttrib);
		pMesh->pVerts = pMesh->pVertAttrib->pData;
	}
	if (flags >> STUC_ATTRIB_SP_UVS & 0x01) {
		pMesh->pUvAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_UVS], &pCore->cornerAttribs);
		STUC_ASSERT("", pMesh->pUvAttrib);
		pMesh->pUvs = pMesh->pUvAttrib->pData;
	}
	if (flags >> STUC_ATTRIB_SP_NORMALS & 0x01) {
		pMesh->pNormalAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_NORMALS], &pCore->cornerAttribs);
		STUC_ASSERT("", pMesh->pNormalAttrib);
		pMesh->pNormals = pMesh->pNormalAttrib->pData;
	}
	if (flags >> STUC_ATTRIB_SP_PRESERVE & 0x01) {
		pMesh->pEdgePreserveAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_PRESERVE], &pCore->edgeAttribs);
		if (pMesh->pEdgePreserveAttrib) {
			pMesh->pEdgePreserve = pMesh->pEdgePreserveAttrib->pData;
		}
	}
	if (flags >> STUC_ATTRIB_SP_RECEIVE & 0x01) {
		pMesh->pEdgeReceiveAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_RECEIVE], &pCore->edgeAttribs);
		if (pMesh->pEdgeReceiveAttrib) {
			pMesh->pEdgeReceive = pMesh->pEdgeReceiveAttrib->pData;
		}
	}
	if (flags >> STUC_ATTRIB_SP_PRESERVE_VERT & 0x01) {
		pMesh->pVertPreserveAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_PRESERVE_VERT], &pCore->vertAttribs);
		if (pMesh->pVertPreserveAttrib) {
			pMesh->pVertPreserve = pMesh->pVertPreserveAttrib->pData;
		}
	}
	if (flags >> STUC_ATTRIB_SP_USG & 0x01) {
		pMesh->pUsgAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_USG], &pCore->vertAttribs);
		if (pMesh->pUsgAttrib) {
			pMesh->pUsg = pMesh->pUsgAttrib->pData;
		}
	}
	if (flags >> STUC_ATTRIB_SP_TANGENTS & 0x01) {;
		pMesh->pTangentAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_TANGENTS], &pCore->cornerAttribs);
		STUC_ASSERT("", pMesh->pTangentAttrib);
		pMesh->pTangents = pMesh->pTangentAttrib->pData;
	}
	if (flags >> STUC_ATTRIB_SP_TSIGNS & 0x01) {
		pMesh->pTSignAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_TSIGNS], &pCore->cornerAttribs);
		STUC_ASSERT("", pMesh->pTSignAttrib);
		pMesh->pTSigns = pMesh->pTSignAttrib->pData;
	}
	if (flags >> STUC_ATTRIB_SP_WSCALE & 0x01) {
		pMesh->pWScaleAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_WSCALE], &pCore->vertAttribs);
		if (pMesh->pWScaleAttrib) {
			pMesh->pWScale = pMesh->pWScaleAttrib->pData;
		}
	}
	if (flags >> STUC_ATTRIB_SP_MAT_IDX & 0x01) {
		pMesh->pMatIdxAttrib = 
			getAttrib(pContext->spAttribs[STUC_ATTRIB_SP_MAT_IDX], &pCore->faceAttribs);
		if (pMesh->pMatIdxAttrib) {
			pMesh->pMatIdx = pMesh->pMatIdxAttrib->pData;
		}
	}
}

void setSpecialBufAttribs(BufMesh *pMesh, UBitField16 flags) {
	StucMesh *pCore = &pMesh->mesh.core;
	if (flags >> STUC_ATTRIB_SP_BUF_W & 0x01) {
		pMesh->pWAttrib =
			getAttrib(spBufAttribs[STUC_ATTRIB_SP_BUF_W], &pCore->cornerAttribs);
		STUC_ASSERT("", pMesh->pWAttrib);
		pMesh->pW = pMesh->pWAttrib->pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_IN_NORMAL & 0x01) {
		pMesh->pInNormalAttrib =
			getAttrib(spBufAttribs[STUC_ATTRIB_SP_BUF_IN_NORMAL], &pCore->cornerAttribs);
		STUC_ASSERT("", pMesh->pInNormalAttrib);
		pMesh->pInNormal = pMesh->pInNormalAttrib->pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_IN_TANGENT & 0x01) {
		pMesh->pInTangentAttrib =
			getAttrib(spBufAttribs[STUC_ATTRIB_SP_BUF_IN_TANGENT], &pCore->cornerAttribs);
		STUC_ASSERT("", pMesh->pInTangentAttrib);
		pMesh->pInTangent = pMesh->pInTangentAttrib->pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_IN_T_SIGN & 0x01) {
		pMesh->pInTSignAttrib =
			getAttrib(spBufAttribs[STUC_ATTRIB_SP_BUF_IN_T_SIGN], &pCore->cornerAttribs);
		STUC_ASSERT("", pMesh->pInTSignAttrib);
		pMesh->pInTSign = pMesh->pInTSignAttrib->pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_ALPHA & 0x01) {
		pMesh->pAlphaAttrib =
			getAttrib(spBufAttribs[STUC_ATTRIB_SP_BUF_ALPHA], &pCore->cornerAttribs);
		STUC_ASSERT("", pMesh->pAlphaAttrib);
		pMesh->pAlpha = pMesh->pAlphaAttrib->pData;
	}
}

void appendBufOnlySpecialAttribs(StucAlloc *pAlloc, BufMesh *pBufMesh) {
	Mesh *pMesh = &pBufMesh->mesh;
	AttribArray *pAttribArr = &pMesh->core.cornerAttribs;
	Attrib *pAttrib = NULL;
	appendAttrib(pAlloc, pAttribArr, &pAttrib, spBufAttribs[1], pMesh->cornerBufSize, false,
		STUC_ATTRIB_DONT_COPY, STUC_ATTRIB_F32);
	pAttrib = NULL;
	appendAttrib(pAlloc, pAttribArr, &pAttrib, spBufAttribs[2], pMesh->cornerBufSize, false,
		STUC_ATTRIB_DONT_COPY, STUC_ATTRIB_V3_F32);
	pAttrib = NULL;
	appendAttrib(pAlloc, pAttribArr, &pAttrib, spBufAttribs[3], pMesh->cornerBufSize, false,
		STUC_ATTRIB_DONT_COPY, STUC_ATTRIB_V3_F32);
	pAttrib = NULL;
	appendAttrib(pAlloc, pAttribArr, &pAttrib, spBufAttribs[4], pMesh->cornerBufSize, false,
		STUC_ATTRIB_DONT_COPY, STUC_ATTRIB_F32);
	pAttrib = NULL;
	appendAttrib(pAlloc, pAttribArr, &pAttrib, spBufAttribs[5], pMesh->cornerBufSize, false,
		STUC_ATTRIB_DONT_COPY, STUC_ATTRIB_F32);
}

static
void setAttribArrToDontCopy(StucContext pContext, AttribArray *pArr, UBitField16 flags) {
	for (int32_t i = 0; i < pArr->count; ++i) {
		Attrib *pAttrib = pArr->pArr + i;
		int32_t specIdx = checkIfSpecialAttrib(pContext, pAttrib);
		STUC_ASSERT("there's no 0 special attrib", specIdx != 0);
		if (specIdx <= 0) {
			continue; // not a special attrib, skip
		}
		if (flags >> specIdx & 0x01) {
			pAttrib->origin = STUC_ATTRIB_DONT_COPY;
		}
	}
}

void setAttribToDontCopy(StucContext pContext, Mesh *pMesh, UBitField16 flags) {
	setAttribArrToDontCopy(pContext, &pMesh->core.meshAttribs, flags);
	setAttribArrToDontCopy(pContext, &pMesh->core.faceAttribs, flags);
	setAttribArrToDontCopy(pContext, &pMesh->core.cornerAttribs, flags);
	setAttribArrToDontCopy(pContext, &pMesh->core.edgeAttribs, flags);
	setAttribArrToDontCopy(pContext, &pMesh->core.vertAttribs, flags);
}

void setAttribOrigins(AttribArray *pAttribs, AttribOrigin origin) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		pAttribs->pArr[i].origin = origin;
	}
}

void allocAttribsFromMeshArr(StucAlloc *pAlloc, Mesh *pMeshDest,
                             int32_t srcCount, Mesh **ppMeshSrcs, bool setCommon) {
	allocAttribs(pAlloc, &pMeshDest->core.faceAttribs, srcCount,
	             ppMeshSrcs, pMeshDest->faceBufSize, STUC_DOMAIN_FACE, setCommon);
	allocAttribs(pAlloc, &pMeshDest->core.cornerAttribs, srcCount,
	             ppMeshSrcs, pMeshDest->cornerBufSize, STUC_DOMAIN_CORNER, setCommon);
	allocAttribs(pAlloc, &pMeshDest->core.edgeAttribs, srcCount,
	             ppMeshSrcs, pMeshDest->edgeBufSize, STUC_DOMAIN_EDGE, setCommon);
	allocAttribs(pAlloc, &pMeshDest->core.vertAttribs, srcCount,
	             ppMeshSrcs, pMeshDest->vertBufSize, STUC_DOMAIN_VERT, setCommon);
}

void initAttrib(StucAlloc *pAlloc, Attrib *pAttrib, char *pName, int32_t dataLen,
                bool interpolate, AttribOrigin origin, AttribType type) {
	memcpy(pAttrib->name, pName, STUC_ATTRIB_NAME_MAX_LEN);
	pAttrib->pData = pAlloc->pCalloc(dataLen, getAttribSize(type));
	pAttrib->type = type;
	pAttrib->interpolate = interpolate;
	pAttrib->origin = origin;
}

void appendAttrib(StucAlloc *pAlloc, AttribArray *pArr, Attrib **ppAttrib, char *pName,
                  int32_t dataLen, bool interpolate, AttribOrigin origin, AttribType type) {
	STUC_ASSERT("", pArr->count <= pArr->size);
	if (pArr->count == pArr->size) {
		pArr->size *= 2;
		pArr->pArr = pAlloc->pRealloc(pArr->pArr, sizeof(Attrib) * pArr->size);
	}
	*ppAttrib = pArr->pArr + pArr->count;
	pArr->count++;
	initAttrib(pAlloc, *ppAttrib, pName, dataLen, interpolate, origin, type);
}