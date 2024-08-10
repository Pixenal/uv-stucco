#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include <AttribUtils.h>
#include <MathUtils.h>
#include <Context.h>
#include <Mesh.h>
#include <Error.h>

#define INDEX_ATTRIB(t, pD, i, v, c) ((t (*)[v])pD->pData)[i][c]

#define BLEND_REPLACE(t, pD, iD, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t, pD, iD, v, c) = INDEX_ATTRIB(t, pB, iB, v, c)

#define BLEND_MULTIPLY(t, pDest, iDest, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t,pD,iD,v,c) =\
		INDEX_ATTRIB(t,pA,iA,v,c) * INDEX_ATTRIB(t,pB,iB,v,c)

#define BLEND_DIVIDE(t, pDest, iDest, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pB,iB,v,c) != (t)0 ?\
		INDEX_ATTRIB(t,pA,iA,v,c) / INDEX_ATTRIB(t,pB,iB,v,c) : (t)0

#define BLEND_ADD(t, pDest, iDest, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t,pD,iD,v,c) =\
		INDEX_ATTRIB(t,pA,iA,v,c) + INDEX_ATTRIB(t,pB,iB,v,c)

#define BLEND_SUBTRACT(t, pDest, iDest, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t,pD,iD,v,c) =\
		INDEX_ATTRIB(t,pA,iA,v,c) - INDEX_ATTRIB(t,pB,iB,v,c)

#define BLEND_ADD_SUB(t, pDest, iDest, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pA,iA,v,c) +\
		INDEX_ATTRIB(t,pB,iB,v,c) - ((t)1 - INDEX_ATTRIB(t,pB,iB,v,c))

#define BLEND_LIGHTEN(t, pDest, iDest, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t,pD,iD,v,c) =\
		INDEX_ATTRIB(t,pA,iA,v,c) > INDEX_ATTRIB(t,pB,iB,v,c) ?\
		INDEX_ATTRIB(t,pA,iA,v,c) : INDEX_ATTRIB(t,pB,iB,v,c)

#define BLEND_DARKEN(t, pDest, iDest, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t,pD,iD,v,c) =\
		INDEX_ATTRIB(t,pA,iA,v,c) < INDEX_ATTRIB(t,pB,iB,v,c) ?\
		INDEX_ATTRIB(t,pA,iA,v,c) : INDEX_ATTRIB(t,pB,iB,v,c)

#define BLEND_OVERLAY(t, pDest, iDest, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pA,iA,v,c) > .5 ?\
		2.0 * INDEX_ATTRIB(t,pA,iA,v,c) * INDEX_ATTRIB(t,pB,iB,v,c) :\
		1.0 - 2.0 * (1.0 - INDEX_ATTRIB(t,pA,iA,v,c)) *\
		(1.0 - INDEX_ATTRIB(t,pB,iB,v,c))

#define BLEND_SOFT_LIGHT(t, pDest, iDest, pA, iA, pB, iB, v, c)\
	INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pB,iB,v,c) < .5 ?\
\
		2.0 * INDEX_ATTRIB(t,pA,iA,v,c) * INDEX_ATTRIB(t,pB,iB,v,c) +\
		INDEX_ATTRIB(t,pA,iA,v,c) * INDEX_ATTRIB(t,pA,iA,v,c) *\
		(1.0 - 2.0 * INDEX_ATTRIB(t,pB,iB,v,c)) :\
\
		2.0 * INDEX_ATTRIB(t,pA,iA,v,c) * (1.0 - INDEX_ATTRIB(t,pB,iB,v,c)) +\
		sqrt(INDEX_ATTRIB(t,pA,iA,v,c)) * (2.0 * INDEX_ATTRIB(t,pB,iB,v,c) - 1.0)

#define BLEND_COLOR_DODGE(t, pDest, iDest, pA, iA, pB, iB, v, c) {\
	t d =INDEX_ATTRIB(t,pA,iA,v,c) / (1.0 - INDEX_ATTRIB(t,pB,iB,v,c));\
	t e = d < 1.0 ? d : 1.0;\
	INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pB,iB,v,c) == 1.0 ?\
		INDEX_ATTRIB(t,pB,iB,v,c) : e;\
}

#define INTERPOLATE_SCALAR(t, pD, iD, pS, iA, iB, iC, bc)\
	INDEX_ATTRIB(t, pD, iD, 1, 0) = INDEX_ATTRIB(t, pS, iA, 1, 0) * bc.d[0];\
 	INDEX_ATTRIB(t, pD, iD, 1, 0) += INDEX_ATTRIB(t, pS, iB, 1, 0) * bc.d[1];\
	INDEX_ATTRIB(t, pD, iD, 1, 0) += INDEX_ATTRIB(t, pS, iC, 1, 0) * bc.d[2];\
	INDEX_ATTRIB(t, pD, iD, 1, 0) /= bc.d[0] + bc.d[1] + bc.d[2];

#define INTERPOLATE_V2(t, pD, iD, pS, iA, iB, iC, bc) {\
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

#define INTERPOLATE_V3(t, pD, iD, pS, iA, iB, iC, bc) {\
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

#define INTERPOLATE_V4(t, pD, iD, pS, iA, iB, iC, bc) {\
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

int32_t getAttribSize(RuvmAttribType type) {
	switch (type) {
		case RUVM_ATTRIB_I8:
			return 1;
		case RUVM_ATTRIB_I16:
			return 2;
		case RUVM_ATTRIB_I32:
			return 4;
		case RUVM_ATTRIB_I64:
			return 8;
		case RUVM_ATTRIB_F32:
			return 4;
		case RUVM_ATTRIB_F64:
			return 8;
		case RUVM_ATTRIB_V2_I8:
			return 2;
		case RUVM_ATTRIB_V2_I16:
			return 4;
		case RUVM_ATTRIB_V2_I32:
			return 8;
		case RUVM_ATTRIB_V2_I64:
			return 16;
		case RUVM_ATTRIB_V2_F32:
			return 8;
		case RUVM_ATTRIB_V2_F64:
			return 16;
		case RUVM_ATTRIB_V3_I8:
			return 3;
		case RUVM_ATTRIB_V3_I16:
			return 6;
		case RUVM_ATTRIB_V3_I32:
			return 12;
		case RUVM_ATTRIB_V3_I64:
			return 24;
		case RUVM_ATTRIB_V3_F32:
			return 12;
		case RUVM_ATTRIB_V3_F64:
			return 24;
		case RUVM_ATTRIB_V4_I8:
			return 4;
		case RUVM_ATTRIB_V4_I16:
			return 8;
		case RUVM_ATTRIB_V4_I32:
			return 16;
		case RUVM_ATTRIB_V4_I64:
			return 32;
		case RUVM_ATTRIB_V4_F32:
			return 16;
		case RUVM_ATTRIB_V4_F64:
			return 32;
		case RUVM_ATTRIB_STRING:
			return RUVM_ATTRIB_STRING_MAX_LEN;
		default:
			RUVM_ASSERT("", false);
			return 0;
	}
}

Attrib *getAttrib(char *pName, AttribArray *pAttribs) {
	for (int32_t i = 0; i < pAttribs->count; ++i) {
		if (0 == strncmp(pName, pAttribs->pArr[i].name,
		                 RUVM_ATTRIB_NAME_MAX_LEN)) {
			return pAttribs->pArr + i;
		}
	}
	return NULL;
}

V3_F32 *attribAsV3(RuvmAttrib *pAttrib, int32_t index) {
	return (V3_F32 *)pAttrib->pData + index;
}

V2_F32 *attribAsV2(RuvmAttrib *pAttrib, int32_t index) {
	return (V2_F32 *)pAttrib->pData + index;
}

int32_t *attribAsI32(RuvmAttrib *pAttrib, int32_t index) {
	return (int32_t *)pAttrib->pData + index;
}

void *attribAsVoid(RuvmAttrib *pAttrib, int32_t index) {
	switch (pAttrib->type) {
		case RUVM_ATTRIB_I8:
			return ((int8_t *)pAttrib->pData) + index;
		case RUVM_ATTRIB_I16:
			return ((int16_t *)pAttrib->pData) + index;
		case RUVM_ATTRIB_I32:
			return ((int32_t *)pAttrib->pData) + index;
		case RUVM_ATTRIB_I64:
			return ((int64_t *)pAttrib->pData) + index;
		case RUVM_ATTRIB_F32:
			return ((float *)pAttrib->pData) + index;
		case RUVM_ATTRIB_F64:
			return ((double *)pAttrib->pData) + index;
		case RUVM_ATTRIB_V2_I8:
			return ((int8_t (*)[2])pAttrib->pData) + index;
		case RUVM_ATTRIB_V2_I16:
			return ((int16_t (*)[2])pAttrib->pData) + index;
		case RUVM_ATTRIB_V2_I32:
			return ((int32_t (*)[2])pAttrib->pData) + index;
		case RUVM_ATTRIB_V2_I64:
			return ((int64_t (*)[2])pAttrib->pData) + index;
		case RUVM_ATTRIB_V2_F32:
			return ((float (*)[2])pAttrib->pData) + index;
		case RUVM_ATTRIB_V2_F64:
			return ((double (*)[2])pAttrib->pData) + index;
		case RUVM_ATTRIB_V3_I8:
			return ((int8_t (*)[3])pAttrib->pData) + index;
		case RUVM_ATTRIB_V3_I16:
			return ((int16_t (*)[3])pAttrib->pData) + index;
		case RUVM_ATTRIB_V3_I32:
			return ((int32_t (*)[3])pAttrib->pData) + index;
		case RUVM_ATTRIB_V3_I64:
			return ((int64_t (*)[3])pAttrib->pData) + index;
		case RUVM_ATTRIB_V3_F32:
			return ((float (*)[3])pAttrib->pData) + index;
		case RUVM_ATTRIB_V3_F64:
			return ((double (*)[3])pAttrib->pData) + index;
		case RUVM_ATTRIB_V4_I8:
			return ((int8_t (*)[4])pAttrib->pData) + index;
		case RUVM_ATTRIB_V4_I16:
			return ((int16_t (*)[4])pAttrib->pData) + index;
		case RUVM_ATTRIB_V4_I32:
			return ((int32_t (*)[4])pAttrib->pData) + index;
		case RUVM_ATTRIB_V4_I64:
			return ((int64_t (*)[4])pAttrib->pData) + index;
		case RUVM_ATTRIB_V4_F32:
			return ((float (*)[4])pAttrib->pData) + index;
		case RUVM_ATTRIB_V4_F64:
			return ((double (*)[4])pAttrib->pData) + index;
		case RUVM_ATTRIB_STRING:
			return ((char (*)[RUVM_ATTRIB_STRING_MAX_LEN])pAttrib->pData) + index;
		default:
			RUVM_ASSERT("", false);
			return NULL;
	}
}

int32_t copyAttrib(RuvmAttrib *pDest, int32_t iDest,
                   RuvmAttrib *pSrc, int32_t iSrc) {
	if (pSrc->origin == RUVM_ATTRIB_ORIGIN_IGNORE) {
		return 0;
	}
	if (pSrc->type != pDest->type) {
		return 1;
	}
	switch (pSrc->type) {
		case RUVM_ATTRIB_I8:
			((int8_t *)pSrc->pData)[iSrc] = ((int8_t *)pDest->pData)[iDest];
			break;
		case RUVM_ATTRIB_I16:
			((int16_t *)pSrc->pData)[iSrc] = ((int16_t *)pDest->pData)[iDest];
			break;
		case RUVM_ATTRIB_I32:
			((int32_t *)pSrc->pData)[iSrc] = ((int32_t *)pDest->pData)[iDest];
			break;
		case RUVM_ATTRIB_I64:
			((int64_t *)pSrc->pData)[iSrc] = ((int64_t *)pDest->pData)[iDest];
			break;
		case RUVM_ATTRIB_F32:
			((float *)pSrc->pData)[iSrc] = ((float *)pDest->pData)[iDest];
			break;
		case RUVM_ATTRIB_F64:
			((double *)pSrc->pData)[iSrc] = ((double *)pDest->pData)[iDest];
			break;
		case RUVM_ATTRIB_V2_I8:
			memcpy(((int8_t (*)[2])pDest->pData)[iDest],
			       ((int8_t (*)[2])pSrc->pData)[iSrc], sizeof(int8_t[2]));
			break;
		case RUVM_ATTRIB_V2_I16:
			memcpy(((int16_t (*)[2])pDest->pData)[iDest],
			       ((int16_t (*)[2])pSrc->pData)[iSrc], sizeof(int16_t[2]));
			break;
		case RUVM_ATTRIB_V2_I32:
			memcpy(((int32_t (*)[2])pDest->pData)[iDest],
			       ((int32_t (*)[2])pSrc->pData)[iSrc], sizeof(int32_t[2]));
			break;
		case RUVM_ATTRIB_V2_I64:
			memcpy(((int64_t (*)[2])pDest->pData)[iDest],
			       ((int64_t (*)[2])pSrc->pData)[iSrc], sizeof(int64_t[2]));
			break;
		case RUVM_ATTRIB_V2_F32:
			memcpy(((float (*)[2])pDest->pData)[iDest],
			       ((float (*)[2])pSrc->pData)[iSrc], sizeof(float[2]));
			break;
		case RUVM_ATTRIB_V2_F64:
			memcpy(((double (*)[2])pDest->pData)[iDest],
			       ((double (*)[2])pSrc->pData)[iSrc], sizeof(double[2]));
			break;
		case RUVM_ATTRIB_V3_I8:
			memcpy(((int8_t (*)[3])pDest->pData)[iDest],
			       ((int8_t (*)[3])pSrc->pData)[iSrc], sizeof(int8_t[3]));
			break;
		case RUVM_ATTRIB_V3_I16:
			memcpy(((int16_t (*)[3])pDest->pData)[iDest],
			       ((int16_t (*)[3])pSrc->pData)[iSrc], sizeof(int16_t[3]));
			break;
		case RUVM_ATTRIB_V3_I32:
			memcpy(((int32_t (*)[3])pDest->pData)[iDest],
			       ((int32_t (*)[3])pSrc->pData)[iSrc], sizeof(int32_t[3]));
			break;
		case RUVM_ATTRIB_V3_I64:
			memcpy(((int64_t (*)[3])pDest->pData)[iDest],
			       ((int64_t (*)[3])pSrc->pData)[iSrc], sizeof(int64_t[3]));
			break;
		case RUVM_ATTRIB_V3_F32:
			memcpy(((float (*)[3])pDest->pData)[iDest],
			       ((float (*)[3])pSrc->pData)[iSrc], sizeof(float[3]));
			break;
		case RUVM_ATTRIB_V3_F64:
			memcpy(((double (*)[3])pDest->pData)[iDest],
			       ((double (*)[3])pSrc->pData)[iSrc], sizeof(double[3]));
			break;
		case RUVM_ATTRIB_V4_I8:
			memcpy(((int8_t (*)[4])pDest->pData)[iDest],
			       ((int8_t (*)[4])pSrc->pData)[iSrc], sizeof(int8_t[4]));
			break;
		case RUVM_ATTRIB_V4_I16:
			memcpy(((int16_t (*)[4])pDest->pData)[iDest],
			       ((int16_t (*)[4])pSrc->pData)[iSrc], sizeof(int16_t[4]));
			break;
		case RUVM_ATTRIB_V4_I32:
			memcpy(((int32_t (*)[4])pDest->pData)[iDest],
			       ((int32_t (*)[4])pSrc->pData)[iSrc], sizeof(int32_t[4]));
			break;
		case RUVM_ATTRIB_V4_I64:
			memcpy(((int64_t (*)[4])pDest->pData)[iDest],
			       ((int64_t (*)[4])pSrc->pData)[iSrc], sizeof(int64_t[4]));
			break;
		case RUVM_ATTRIB_V4_F32:
			memcpy(((float (*)[4])pDest->pData)[iDest],
			       ((float (*)[4])pSrc->pData)[iSrc], sizeof(float[4]));
			break;
		case RUVM_ATTRIB_V4_F64:
			memcpy(((double (*)[4])pDest->pData)[iDest],
			       ((double (*)[4])pSrc->pData)[iSrc], sizeof(double[4]));
			break;
		case RUVM_ATTRIB_STRING:
			memcpy(((char (*)[RUVM_ATTRIB_STRING_MAX_LEN])pDest->pData)[iDest],
			       ((char (*)[RUVM_ATTRIB_STRING_MAX_LEN])pSrc->pData)[iSrc],
				   sizeof(double[RUVM_ATTRIB_STRING_MAX_LEN]));
			break;
	}
	return 0;
}

void copyAllAttribs(AttribArray *pDest, int32_t iDest,
                    AttribArray *pSrc, int32_t iSrc) {
	for (int32_t i = 0; i < pSrc->count; ++i) {
		copyAttrib(pDest->pArr + i, iDest, pSrc->pArr + i, iSrc);
	}
}

RuvmTypeDefault *getTypeDefaultConfig(RuvmTypeDefaultConfig *pConfig,
                                      RuvmAttribType type) {
	switch (type) {
		case RUVM_ATTRIB_I8:
			return &pConfig->i8;
		case RUVM_ATTRIB_I16:
			return &pConfig->i16;
		case RUVM_ATTRIB_I32:
			return &pConfig->i32;
		case RUVM_ATTRIB_I64:
			return &pConfig->i64;
		case RUVM_ATTRIB_F32:
			return &pConfig->f32;
		case RUVM_ATTRIB_F64:
			return &pConfig->f64;
		case RUVM_ATTRIB_V2_I8:
			return &pConfig->v2_i8;
		case RUVM_ATTRIB_V2_I16:
			return &pConfig->v2_i16;
		case RUVM_ATTRIB_V2_I32:
			return &pConfig->v2_i32;
		case RUVM_ATTRIB_V2_I64:
			return &pConfig->v2_i64;
		case RUVM_ATTRIB_V2_F32:
			return &pConfig->v2_f32;
		case RUVM_ATTRIB_V2_F64:
			return &pConfig->v2_f64;
		case RUVM_ATTRIB_V3_I8:
			return &pConfig->v3_i8;
		case RUVM_ATTRIB_V3_I16:
			return &pConfig->v3_i16;
		case RUVM_ATTRIB_V3_I32:
			return &pConfig->v3_i32;
		case RUVM_ATTRIB_V3_I64:
			return &pConfig->v3_i64;
		case RUVM_ATTRIB_V3_F32:
			return &pConfig->v3_f32;
		case RUVM_ATTRIB_V3_F64:
			return &pConfig->v3_f64;
		case RUVM_ATTRIB_V4_I8:
			return &pConfig->v4_i8;
		case RUVM_ATTRIB_V4_I16:
			return &pConfig->v4_i16;
		case RUVM_ATTRIB_V4_I32:
			return &pConfig->v4_i32;
		case RUVM_ATTRIB_V4_I64:
			return &pConfig->v4_i64;
		case RUVM_ATTRIB_V4_F32:
			return &pConfig->v4_f32;
		case RUVM_ATTRIB_V4_F64:
			return &pConfig->v4_f64;
		case RUVM_ATTRIB_STRING:
			return &pConfig->string;
		default:
			RUVM_ASSERT("", false);
			return 0;
	}
}

RuvmCommonAttrib *getCommonAttrib(RuvmCommonAttrib *pAttribs, int32_t attribCount,
                                  char *pName) {
	//this is it's own function (rather than a general getAttrib function),
	//because of the below todo:
	//TODO replace linear search with hash table.
	for (int32_t i = 0; i < attribCount; ++i) {
		if (0 == strncmp(pName, pAttribs[i].name, RUVM_ATTRIB_NAME_MAX_LEN)) {
			return pAttribs + i;
		}
	}
	return NULL;
}

void interpolateAttrib(RuvmAttrib *pDest, int32_t iDest, RuvmAttrib *pSrc,
                       int32_t iSrcA, int32_t iSrcB, int32_t iSrcC, V3_F32 bc) {
	if (pDest->type != pSrc->type) {
		printf("Type mismatch in interpolateAttrib\n");
		//TODO remove all uses of abort(), and add proper exception handling
		abort();
	}
	RuvmAttribType type = pDest->type;
	switch (type) {
		case RUVM_ATTRIB_I8:
			INTERPOLATE_SCALAR(int8_t, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case RUVM_ATTRIB_I16:
			INTERPOLATE_SCALAR(int16_t, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case RUVM_ATTRIB_I32:
			INTERPOLATE_SCALAR(int32_t, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case RUVM_ATTRIB_I64:
			INTERPOLATE_SCALAR(int64_t, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case RUVM_ATTRIB_F32:
			INTERPOLATE_SCALAR(float, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case RUVM_ATTRIB_F64:
			INTERPOLATE_SCALAR(double, pDest, iDest, pSrc, iSrcA, iSrcB,
			                   iSrcC, bc);
			break;
		case RUVM_ATTRIB_V2_I8:
			INTERPOLATE_V2(int8_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V2_I16:
			INTERPOLATE_V2(int16_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V2_I32:
			INTERPOLATE_V2(int32_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V2_I64:
			INTERPOLATE_V2(int64_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V2_F32:
			INTERPOLATE_V2(float, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V2_F64:
			INTERPOLATE_V2(double, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V3_I8:
			INTERPOLATE_V3(int8_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V3_I16:
			INTERPOLATE_V3(int16_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V3_I32:
			INTERPOLATE_V3(int32_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V3_I64:
			INTERPOLATE_V3(int64_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V3_F32:
			INTERPOLATE_V3(float, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V3_F64:
			INTERPOLATE_V3(double, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V4_I8:
			//TODO using unsigned here temporarily for vert color
			//make a proper set of attrib types for unsigned types
			INTERPOLATE_V4(uint8_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V4_I16:
			INTERPOLATE_V4(int16_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V4_I32:
			INTERPOLATE_V4(int32_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V4_I64:
			INTERPOLATE_V4(int64_t, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V4_F32:
			INTERPOLATE_V4(float, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_V4_F64:
			INTERPOLATE_V4(double, pDest, iDest, pSrc, iSrcA, iSrcB, iSrcC, bc);
			break;
		case RUVM_ATTRIB_STRING:
			break;
	}
}

static void appendOnNonString() {
	printf("Blend mode append reached on non string attrib!\n");
	abort();
}

void blendAttribs(RuvmAttrib *pD, int32_t iD, RuvmAttrib *pA, int32_t iA,
                  RuvmAttrib *pB, int32_t iB, RuvmBlendConfig blendConfig) {
	RuvmAttribType type = pD->type;
	switch (type) {
		case RUVM_ATTRIB_I8:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_I16:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_I32:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_I64:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_F32:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_F64:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V2_I8:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V2_I16:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V2_I32:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V2_I64:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V2_F32:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V2_F64:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V3_I8:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V3_I16:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V3_I32:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V3_I64:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V3_F32:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V3_F64:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V4_I8:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(int8_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V4_I16:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(int16_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V4_I32:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(int32_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V4_I64:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(int64_t, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V4_F32:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(float, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_V4_F64:
			switch (blendConfig.blend) {
				case RUVM_BLEND_REPLACE:
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_MULTIPLY:
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DIVIDE:
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD:
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SUBTRACT:
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_ADD_SUB:
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_LIGHTEN:
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_DARKEN:
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_OVERLAY:
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(double, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case RUVM_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case RUVM_ATTRIB_STRING:
			//TODO add string append
			break;
	}
}

static
AttribArray *getAttribArrFromDomain(RuvmMesh *pMesh, RuvmDomain domain) {
	switch (domain) {
	case RUVM_DOMAIN_FACE:
		return &pMesh->faceAttribs;
	case RUVM_DOMAIN_LOOP:
		return &pMesh->loopAttribs;
	case RUVM_DOMAIN_EDGE:
		return &pMesh->edgeAttribs;
	case RUVM_DOMAIN_VERT:
		return &pMesh->vertAttribs;
	default:
		return NULL;
	}
}

void allocAttribs(RuvmAlloc *pAlloc, AttribArray *pDest,
                  int32_t srcCount, Mesh **ppSrcArr,
				  int32_t dataLen, RuvmDomain domain, bool setCommon) {
	pDest->size = 2;
	pDest->pArr = pAlloc->pCalloc(pDest->size, sizeof(Attrib));
	for (int32_t i = 0; i < srcCount; ++i) {
		AttribArray *pSrc = getAttribArrFromDomain(&ppSrcArr[i]->mesh, domain);
		if (pSrc && pSrc->count) {
			for (int32_t j = 0; j < pSrc->count; ++j) {
				if (pSrc->pArr[j].origin == RUVM_ATTRIB_ORIGIN_IGNORE) {
					continue;
				}
				Attrib *pAttrib = getAttrib(pSrc->pArr[j].name, pDest);
				if (pAttrib) {
					//if attribute already exists in destination,
					//set origin to common, then skip
					if (setCommon) {
						pAttrib->origin = RUVM_ATTRIB_ORIGIN_COMMON;
					}
					continue;
				}
				pDest->pArr[pDest->count].type = pSrc->pArr[j].type;
				memcpy(pDest->pArr[pDest->count].name, pSrc->pArr[j].name,
				       RUVM_ATTRIB_NAME_MAX_LEN);
				pDest->pArr[pDest->count].origin = pSrc->pArr[j].origin;
				pDest->pArr[pDest->count].interpolate = pSrc->pArr[j].interpolate;
				int32_t attribSize = getAttribSize(pSrc->pArr[j].type);
				pDest->pArr[pDest->count].pData = pAlloc->pCalloc(dataLen, attribSize);
				pDest->count++;
				RUVM_ASSERT("", pDest->count <= pDest->size);
				if (pDest->count == pDest->size) {
					pDest->size *= 2;
					pDest->pArr = pAlloc->pRealloc(pDest->pArr, pDest->size * sizeof(Attrib));
				}
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
SpecialAttrib getIfSpecialAttrib(Mesh *pMesh, Attrib *pAttrib) {
		if (pAttrib->pData == pMesh->pVerts) {
			return ATTRIB_SPECIAL_VERTS;
		}
		else if (pAttrib->pData == pMesh->pUvs) {
			return ATTRIB_SPECIAL_UVS;
		}
		else if (pAttrib->pData == pMesh->pNormals) {
			return ATTRIB_SPECIAL_NORMALS;
		}
		else if (pAttrib->pData == pMesh->pEdgePreserve) {
			return ATTRIB_SPECIAL_PRESERVE;
		}
		else if (pAttrib->pData == pMesh->pEdgeReceive) {
			return ATTRIB_SPECIAL_RECEIVE;
		}
		else if (pAttrib->pData == pMesh->pVertPreserve) {
			return ATTRIB_SPECIAL_PRESERVE_VERT;
		}
		else if (pAttrib->pData == pMesh->pUsg) {
			return ATTRIB_SPECIAL_USG;
		}
		return ATTRIB_SPECIAL_NONE;
}

static
void reassignIfSpecial(Mesh *pMesh, Attrib *pAttrib, SpecialAttrib special) {
	bool valid = false;
	switch (special) {
		case (ATTRIB_SPECIAL_NONE):
			valid = true;
			break;
		case (ATTRIB_SPECIAL_VERTS):
			pMesh->pVerts = pAttrib->pData;
			valid = true;
			break;
		case (ATTRIB_SPECIAL_UVS):
			pMesh->pUvs = pAttrib->pData;
			valid = true;
			break;
		case (ATTRIB_SPECIAL_NORMALS):
			pMesh->pNormals = pAttrib->pData;
			valid = true;
			break;
		case (ATTRIB_SPECIAL_PRESERVE):
			pMesh->pEdgePreserve = pAttrib->pData;
			valid = true;
			break;
		case (ATTRIB_SPECIAL_RECEIVE):
			pMesh->pEdgeReceive = pAttrib->pData;
			valid = true;
			break;
		case (ATTRIB_SPECIAL_PRESERVE_VERT):
			pMesh->pVertPreserve = pAttrib->pData;
			valid = true;
			break;
		case (ATTRIB_SPECIAL_USG):
			pMesh->pUsg = pAttrib->pData;
			valid = true;
			break;
	}
	RUVM_ASSERT("", valid);
}

void reallocAttribs(const RuvmAlloc *pAlloc, Mesh *pMesh,
                    AttribArray *pAttribArr, const int32_t newLen) {
	RUVM_ASSERT("", newLen >= 0 && newLen < 100000000);
	for (int32_t i = 0; i < pAttribArr->count; ++i) {
		Attrib *pAttrib = pAttribArr->pArr + i;
		SpecialAttrib special = getIfSpecialAttrib(pMesh, pAttrib);
		//Check entry is valid
		RUVM_ASSERT("", pAttrib->interpolate % 2 == pAttrib->interpolate);
		int8_t oldFirstElement = *(int8_t *)attribAsVoid(pAttrib, 0);
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData =
			pAlloc->pRealloc(pAttrib->pData, attribSize * newLen);
		int8_t newFirstElement = *(int8_t *)attribAsVoid(pAttrib, 0);
		RUVM_ASSERT("", newFirstElement == oldFirstElement);
		reassignIfSpecial((Mesh *)pMesh, pAttrib, special);
		RUVM_ASSERT("", i >= 0 && i < pAttribArr->count);
	}
}

void reallocAndMoveAttribs(const RuvmAlloc *pAlloc, BufMesh *pMesh,
                           AttribArray *pAttribArr, const int32_t start,
						   const int32_t offset, const int32_t lenToCopy,
						   const int32_t newLen) {
	RUVM_ASSERT("", newLen >= 0 && newLen < 100000000);
	RUVM_ASSERT("", start >= 0 && start < newLen);
	for (int32_t i = 0; i < pAttribArr->count; ++i) {
		Attrib *pAttrib = pAttribArr->pArr + i;
		SpecialAttrib special = getIfSpecialAttrib(asMesh(pMesh), pAttrib);
		//Check entry is valid
		RUVM_ASSERT("", pAttrib->interpolate % 2 == pAttrib->interpolate);
		int8_t oldFirstElement =
			*(int8_t *)attribAsVoid(pAttrib, start);
		int8_t oldLastElement =
			*(int8_t *)attribAsVoid(pAttrib, start + lenToCopy - 1);
		int32_t attribSize = getAttribSize(pAttrib->type);
		pAttrib->pData =
			pAlloc->pRealloc(pAttrib->pData, attribSize * newLen);
		memmove(attribAsVoid(pAttrib, start + offset),
				attribAsVoid(pAttrib, start), attribSize * lenToCopy);
		int8_t newFirstElement =
			*(int8_t *)attribAsVoid(pAttrib, start + offset);
		int8_t newLastElement =
			*(int8_t *)attribAsVoid(pAttrib, start + offset + lenToCopy - 1);
		RUVM_ASSERT("", newFirstElement == oldFirstElement);
		RUVM_ASSERT("", newLastElement == oldLastElement);
		reassignIfSpecial((Mesh *)pMesh, pAttrib, special);
		RUVM_ASSERT("", i >= 0 && i < pAttribArr->count);
	}
}

void setSpecialAttribs(Mesh *pMesh, UBitField8 flags) {
	RuvmMesh *pCore = &pMesh->mesh;
	if (flags >> ATTRIB_SPECIAL_VERTS & 0x01) {
		pMesh->pVertAttrib = getAttrib("position", &pCore->vertAttribs);
		RUVM_ASSERT("", pMesh->pVertAttrib);
		pMesh->pVerts = pMesh->pVertAttrib->pData;
	}
	if (flags >> ATTRIB_SPECIAL_UVS & 0x01) {
		pMesh->pUvAttrib = getAttrib("UVMap", &pCore->loopAttribs);
		RUVM_ASSERT("", pMesh->pUvAttrib);
		pMesh->pUvs = pMesh->pUvAttrib->pData;
	}
	if (flags >> ATTRIB_SPECIAL_NORMALS & 0x01) {
		pMesh->pNormalAttrib = getAttrib("normal", &pCore->loopAttribs);
		RUVM_ASSERT("", pMesh->pNormalAttrib);
		pMesh->pNormals = pMesh->pNormalAttrib->pData;
	}
	if (flags >> ATTRIB_SPECIAL_PRESERVE & 0x01) {
		pMesh->pEdgePreserveAttrib = getAttrib("RuvmPreserve", &pCore->edgeAttribs);
		if (pMesh->pEdgePreserveAttrib) {
			pMesh->pEdgePreserve = pMesh->pEdgePreserveAttrib->pData;
		}
	}
	if (flags >> ATTRIB_SPECIAL_RECEIVE & 0x01) {
		pMesh->pEdgeReceiveAttrib = getAttrib("RuvmPreserveReceive", &pCore->edgeAttribs);
		if (pMesh->pEdgeReceiveAttrib) {
			pMesh->pEdgeReceive = pMesh->pEdgeReceiveAttrib->pData;
		}
	}
	if (flags >> ATTRIB_SPECIAL_PRESERVE_VERT & 0x01) {
		pMesh->pVertPreserveAttrib = getAttrib("RuvmPreserveVert", &pCore->vertAttribs);
		if (pMesh->pVertPreserveAttrib) {
			pMesh->pVertPreserve = pMesh->pVertPreserveAttrib->pData;
		}
	}
	if (flags >> ATTRIB_SPECIAL_USG & 0x01) {
		pMesh->pUsgAttrib = getAttrib("RuvmUsg", &pCore->vertAttribs);
		if (pMesh->pUsgAttrib) {
			pMesh->pUsg = pMesh->pUsgAttrib->pData;
		}
	}
}

void allocAttribsFromMeshArr(RuvmAlloc *pAlloc, Mesh *pMeshDest,
                             int32_t srcCount, Mesh **ppMeshSrcs, bool setCommon) {
	allocAttribs(pAlloc, &pMeshDest->mesh.faceAttribs, srcCount,
	             ppMeshSrcs, pMeshDest->faceBufSize, RUVM_DOMAIN_FACE, setCommon);
	allocAttribs(pAlloc, &pMeshDest->mesh.loopAttribs, srcCount,
	             ppMeshSrcs, pMeshDest->loopBufSize, RUVM_DOMAIN_LOOP, setCommon);
	allocAttribs(pAlloc, &pMeshDest->mesh.edgeAttribs, srcCount,
	             ppMeshSrcs, pMeshDest->edgeBufSize, RUVM_DOMAIN_EDGE, setCommon);
	allocAttribs(pAlloc, &pMeshDest->mesh.vertAttribs, srcCount,
	             ppMeshSrcs, pMeshDest->vertBufSize, RUVM_DOMAIN_VERT, setCommon);
}

void initAttrib(RuvmAlloc *pAlloc, Attrib *pAttrib, char *pName, int32_t dataLen,
                bool interpolate, AttribOrigin origin, AttribType type) {
	memcpy(pAttrib->name, pName, RUVM_ATTRIB_NAME_MAX_LEN);
	pAttrib->pData = pAlloc->pCalloc(dataLen, getAttribSize(type));
	pAttrib->type = type;
	pAttrib->interpolate = interpolate;
	pAttrib->origin = origin;
}