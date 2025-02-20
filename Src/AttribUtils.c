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

#define LERP_SIMPLE(a, b, o) (b * o + (1.0 - o) * a)

//t is type, pD is attrib, i is idx, v is vector len (scalar is 1, v2 is 2, etc),
//and c is component (ie, x is 0, y is 1, z is 2, etc)
#define INDEX_ATTRIB(t, pD, i, v, c) ((t (*)[v])pD->core.pData)[i][c]
//TODO you'll need to make an unsigned wide macro as well when you implement
//proper unsigned attribs
#define INDEX_ATTRIB_WIDE(t, pD, i, v, c) (F64)(((t (*)[v])pD->core.pData)[i][c])
//TODO add a way to pass the max value here rather than just using 255.0f,
//one may want to use 16 or 32 bit color depth
#define INDEX_ATTRIB_NORM(t, tMax, pD, i, v, c) \
	((F64)((t (*)[v])pD->core.pData)[i][c] / (F64)tMax)

#define CLAMP_AND_LERP(t, tMax, o, pA, iA, v, c, d) (\
	(d = LERP_SIMPLE(INDEX_ATTRIB_WIDE(t,pA,iA,v,c), d, o)),\
	(d = d < 0 ? 0 : (d > (F64)tMax ? tMax : d)),\
	(t)(d)\
)

#define CLAMP_AND_LERP_NORM(t, tMax, o, pA, iA, v, c, d) (\
	(d = LERP_SIMPLE(INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c), d, o)),\
	(d = d < .0 ? .0 : (d > 1.0 ? 1.0 : d)),\
	(t)(d * (F64)tMax)\
)

#define BLEND_REPLACE(t, tMax, o, pD, iD, pA, iA, pB, iB, v, c)\
	if (o == 1.0) {\
		INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pB,iB,v,c);\
	}\
	else if (o == .0) {\
		INDEX_ATTRIB(t,pD,iD,v,c) = INDEX_ATTRIB(t,pA,iA,v,c);\
	}\
	else {\
		F64 b = INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c);\
		INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,b);\
	}

#define BLEND_MULTIPLY(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	F64 d = INDEX_ATTRIB_WIDE(t,pA,iA,v,c) * INDEX_ATTRIB_WIDE(t,pB,iB,v,c);\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_MULTIPLY_NORM(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	F64 d = INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) *\
		INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c);\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_DIVIDE(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	F64 d = INDEX_ATTRIB_WIDE(t,pB,iB,v,c) != (t)0 ?\
		INDEX_ATTRIB_WIDE(t,pA,iA,v,c) / INDEX_ATTRIB_WIDE(t,pB,iB,v,c) : (F64)tMax;\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_DIVIDE_NORM(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	F64 d = INDEX_ATTRIB(t,pB,iB,v,c) != (t)0 ?\
		(INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) / INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c)) : 1.0f;\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_ADD(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	F64 d = INDEX_ATTRIB_WIDE(t,pA,iA,v,c) + INDEX_ATTRIB_WIDE(t,pB,iB,v,c);\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP(t,tMax,o,pA,iA,v,c,d);\
}

//TODO add clamping as an option
#define BLEND_SUBTRACT(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	F64 d = INDEX_ATTRIB_WIDE(t,pA,iA,v,c) - INDEX_ATTRIB_WIDE(t,pB,iB,v,c);\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP(t,tMax,o,pA,iA,v,c,d);\
}

//TODO addsub result is slightly off from other programs
#define BLEND_ADD_SUB(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	F64 d = INDEX_ATTRIB_WIDE(t,pA,iA,v,c) +\
		INDEX_ATTRIB_WIDE(t,pB,iB,v,c) - ((F64)tMax - INDEX_ATTRIB_WIDE(t,pB,iB,v,c));\
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
		F64 dNorm = (F64)d / (F64)tMax;\
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
		F64 dNorm = (F64)d / (F64)tMax;\
		INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,dNorm);\
	}\
}

#define BLEND_OVERLAY(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	F64 d = (INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) < .5 ?\
	2.0 * INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) * INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c) :\
	1.0 - 2.0 * (1.0 - INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c)) *\
	(1.0 - INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c)));\
\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,d);\
}

#define BLEND_SOFT_LIGHT(t, tMax, o, pDest, iDest, pA, iA, pB, iB, v, c) {\
	F64 d = (INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c) < .5 ?\
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
	F64 d = (1.0 - INDEX_ATTRIB_NORM(t,tMax,pB,iB,v,c));\
	d = d == .0 ? 1.0 : INDEX_ATTRIB_NORM(t,tMax,pA,iA,v,c) / d;\
	INDEX_ATTRIB(t,pD,iD,v,c) = CLAMP_AND_LERP_NORM(t,tMax,o,pA,iA,v,c,d);\
}

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

Attrib *stucGetAttribIntern(const char *pName, AttribArray *pAttribs) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
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

const Attrib *stucGetAttribInternConst(const char *pName, const AttribArray *pAttribs) {
	return stucGetAttribIntern(pName, (AttribArray *)pAttribs);
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
	if (pSrc->origin == STUC_ATTRIB_DONT_COPY) {
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
		Attrib *pSrcAttrib = stucGetAttribIntern(pDest->pArr[i].core.name, pSrc);
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
	const StucCommonAttrib *pAttribs,
	I32 attribCount,
	char *pName
) {
	for (I32 i = 0; i < attribCount; ++i) {
		if (!strncmp(pName, pAttribs[i].name, STUC_ATTRIB_NAME_MAX_LEN)) {
			return pAttribs + i;
		}
	}
	return NULL;
}

//TODO replace manual searches of indexed attribs with this func
AttribIndexed *stucGetAttribIndexedIntern(
	const AttribIndexedArr *pAttribArr,
	char *pName
) {
	for (I32 i = 0; i < pAttribArr->count; ++i) {
		AttribIndexed *pAttrib = pAttribArr->pArr + i;
		if (!strncmp(pName, pAttrib->core.name, STUC_ATTRIB_NAME_MAX_LEN)) {
			return pAttrib;
		}
	}
	return NULL;
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
	if (pDest->core.type != pSrcA->core.type ||
		pDest->core.type != pSrcB->core.type) {
		printf("Type mismatch in interpolateAttrib\n");
		//TODO remove all uses of abort(), and add proper exception handling
		abort();
	}
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

static
void appendOnNonString() {
	printf("Blend mode append reached on non string attrib!\n");
	abort();
}

//TODO this name should not be plural
void stucBlendAttribs(
	Attrib *pD,
	I32 iD,
	Attrib *pA,
	I32 iA,
	Attrib *pB,
	I32 iB,
	StucBlendConfig blendConfig
) {
	AttribType type = pD->core.type;
	F64 opacity = (F64)blendConfig.opacity; //casting as blending is done with F64s
	switch (type) {
		case STUC_ATTRIB_I8:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_I16:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_I32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_I64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_F32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_F64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 1, 0);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_I8:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_I16:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_I32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_I64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_F32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V2_F64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 0);
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 2, 1);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_I8:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_I16:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_I32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_I64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_F32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V3_F64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 0);
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 1);
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 3, 2);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_I8:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					switch (pD->core.use) {
					//TODO add a GENERIC item, in addition to NONE (none indicates not set),
					//and use that here. NONE should probably cause an error, or just default to
					//generic blending?
					case STUC_ATTRIB_USE_NONE:
						BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
						BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
						BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
						BLEND_REPLACE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
						break;
					case STUC_ATTRIB_USE_COLOR:
						BLEND_REPLACE(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
						BLEND_REPLACE(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
						BLEND_REPLACE(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
						BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
						break;
					}
					break;
				case STUC_BLEND_MULTIPLY:
					switch (pD->core.use) {
						//TODO have an option in blendConfig for whether to normalize or not.
						//This will only be relevent for non-color attribs. As if use is color,
						//attribs will always be normalized
						case STUC_ATTRIB_USE_NONE:
							BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_MULTIPLY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_MULTIPLY_NORM(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_MULTIPLY_NORM(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_MULTIPLY_NORM(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_DIVIDE:
					switch (pD->core.use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_DIVIDE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_DIVIDE_NORM(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_DIVIDE_NORM(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_DIVIDE_NORM(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_ADD:
					switch (pD->core.use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_ADD(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_ADD(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_ADD(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_ADD(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_SUBTRACT:
					switch (pD->core.use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_SUBTRACT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_SUBTRACT(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_SUBTRACT(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_SUBTRACT(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_ADD_SUB:
					switch (pD->core.use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_ADD_SUB(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_ADD_SUB(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_ADD_SUB(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_ADD_SUB(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							//TODO add options for replacing or blending alpha channel
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_LIGHTEN:
					switch (pD->core.use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_LIGHTEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_LIGHTEN(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_LIGHTEN(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_LIGHTEN(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_DARKEN:
					switch (pD->core.use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_DARKEN(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_DARKEN(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_DARKEN(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_DARKEN(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_OVERLAY:
					switch (pD->core.use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_OVERLAY(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_OVERLAY(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_OVERLAY(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_OVERLAY(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_SOFT_LIGHT:
					switch (pD->core.use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_SOFT_LIGHT(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_SOFT_LIGHT(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_SOFT_LIGHT(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_SOFT_LIGHT(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
					}
					break;
				case STUC_BLEND_COLOR_DODGE:
					switch (pD->core.use) {
						case STUC_ATTRIB_USE_NONE:
							BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_COLOR_DODGE(I8, INT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
							break;
						case STUC_ATTRIB_USE_COLOR:
							BLEND_COLOR_DODGE(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
							BLEND_COLOR_DODGE(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
							BLEND_COLOR_DODGE(U8, UINT8_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
							BLEND_REPLACE(U8, UINT8_MAX, .0, pD, iD, pA, iA, pB, iB, 4, 3);
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
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(I16, INT16_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_I32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(I32, INT32_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_I64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(I64, INT64_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_F32:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(F32, FLT_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_APPEND:
					appendOnNonString();
					break;
			}
			break;
		case STUC_ATTRIB_V4_F64:
			switch (blendConfig.blend) {
				case STUC_BLEND_REPLACE:
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_REPLACE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_MULTIPLY:
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_MULTIPLY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DIVIDE:
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DIVIDE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD:
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SUBTRACT:
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SUBTRACT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_ADD_SUB:
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_ADD_SUB(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_LIGHTEN:
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_LIGHTEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_DARKEN:
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_DARKEN(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_OVERLAY:
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_OVERLAY(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_SOFT_LIGHT:
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_SOFT_LIGHT(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
					break;
				case STUC_BLEND_COLOR_DODGE:
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 0);
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 1);
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 2);
					BLEND_COLOR_DODGE(F64, DBL_MAX, opacity, pD, iD, pA, iA, pB, iB, 4, 3);
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

static
const AttribArray *getAttribArrFromDomainConst(const StucMesh *pMesh, StucDomain domain) {
	return getAttribArrFromDomain((StucMesh *)pMesh, domain);
}

static
void allocAttribsFromArr(
	const StucAlloc *pAlloc,
	AttribArray *pDest,
	const AttribArray *pSrc,
	I32 dataLen,
	bool setCommon,
	bool allocData,
	bool aliasData
) {
	for (I32 j = 0; j < pSrc->count; ++j) {
		if (pSrc->pArr[j].origin == STUC_ATTRIB_DONT_COPY) {
			continue;
		}
		Attrib *pAttrib = stucGetAttribIntern(pSrc->pArr[j].core.name, pDest);
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
		pDest->pArr[pDest->count].core.type = pSrc->pArr[j].core.type;
		memcpy(
			pDest->pArr[pDest->count].core.name,
			pSrc->pArr[j].core.name,
			STUC_ATTRIB_NAME_MAX_LEN
		);
		pDest->pArr[pDest->count].origin = pSrc->pArr[j].origin;
		pDest->pArr[pDest->count].core.use = pSrc->pArr[j].core.use;
		pDest->pArr[pDest->count].interpolate = pSrc->pArr[j].interpolate;
		I32 attribSize = stucGetAttribSizeIntern(pSrc->pArr[j].core.type);
		if (allocData) {
			pDest->pArr[pDest->count].core.pData = pAlloc->pCalloc(dataLen, attribSize);
		}
		else if (aliasData) {
			pDest->pArr[pDest->count].core.pData = pSrc->pArr[j].core.pData;
		}
		pDest->count++;
	}
}

void stucAllocAttribs(
	const StucAlloc *pAlloc,
	AttribArray *pDest,
	I32 srcCount,
	const Mesh *const *ppSrcArr,
	I32 dataLen,
	StucDomain domain,
	bool setCommon,
	bool allocData,
	bool aliasData
) {
	pDest->size = 2;
	pDest->pArr = pAlloc->pCalloc(pDest->size, sizeof(Attrib));
	for (I32 i = 0; i < srcCount; ++i) {
		const AttribArray *pSrc =
			getAttribArrFromDomainConst((StucMesh *)&ppSrcArr[i]->core, domain);
		if (pSrc && pSrc->count) {
			allocAttribsFromArr(
				pAlloc,
				pDest,
				pSrc,
				dataLen,
				setCommon,
				allocData,
				aliasData
			);
		}
	}
	if (!pDest->count) {
		pAlloc->pFree(pDest->pArr);
		pDest->pArr = NULL;
		pDest->size = 0;
		return;
	}
}

static
I32 checkIfSpecialAttrib(StucContext pCtx, Attrib *pAttrib) {
	I32 size = sizeof(pCtx->spAttribNames) / STUC_ATTRIB_NAME_MAX_LEN;
	for (I32 i = 1; i < size; ++i) {
		if (!strncmp(pAttrib->core.name, pCtx->spAttribNames[i], STUC_ATTRIB_NAME_MAX_LEN)) {
			return i;
		}
	}
	return -1;
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

//differs from regular func, as it checks if special attrib
//by comparing pointers, to avoid needing to compare a bunch of strings
static
SpecialAttrib quickCheckIfSpecialAttrib(const Mesh *pMesh, const AttribCore *pAttrib) {
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
	else if (pAttrib->pData == pMesh->pEdgeLen) {
		return STUC_ATTRIB_SP_EDGE_LEN;
	}
	else if (pAttrib->pData == pMesh->pSeamEdge) {
		return STUC_ATTRIB_SP_SEAM_EDGE;
	}
	else if (pAttrib->pData == pMesh->pSeamVert) {
		return STUC_ATTRIB_SP_SEAM_VERT;
	}
	else if (pAttrib->pData == pMesh->pNumAdjPreserve) {
		return STUC_ATTRIB_SP_NUM_ADJ_PRESERVE;
	}
	else if (pAttrib->pData == pMesh->pEdgeCorners) {
		return STUC_ATTRIB_SP_EDGE_CORNERS;
	}
	return STUC_ATTRIB_SP_NONE;
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
void reassignIfSpecial(Mesh *pMesh, AttribCore *pAttrib, SpecialAttrib special) {
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
		case (STUC_ATTRIB_SP_EDGE_LEN):
			pMesh->pEdgeLen = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_SEAM_EDGE):
			pMesh->pSeamEdge = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_SEAM_VERT):
			pMesh->pSeamVert = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_NUM_ADJ_PRESERVE):
			pMesh->pNumAdjPreserve = pAttrib->pData;
			break;
		case (STUC_ATTRIB_SP_EDGE_CORNERS):
			pMesh->pEdgeCorners = pAttrib->pData;
			break;
	}
}

static
Attrib **getSpAttribRef(Mesh *pMesh, SpecialAttrib special) {
	switch (special) {
		case (STUC_ATTRIB_SP_NONE):
			return NULL;
		case (STUC_ATTRIB_SP_VERTS):
			return &pMesh->pVertAttrib;
		case (STUC_ATTRIB_SP_UVS):
			return &pMesh->pUvAttrib;
		case (STUC_ATTRIB_SP_NORMALS):
			return &pMesh->pNormalAttrib;
		case (STUC_ATTRIB_SP_PRESERVE):
			return &pMesh->pEdgePreserveAttrib;
		case (STUC_ATTRIB_SP_RECEIVE):
			return &pMesh->pEdgeReceiveAttrib;
		case (STUC_ATTRIB_SP_PRESERVE_VERT):
			return &pMesh->pVertPreserveAttrib;
		case (STUC_ATTRIB_SP_USG):
			return &pMesh->pUsgAttrib;
		case (STUC_ATTRIB_SP_TANGENTS):
			return &pMesh->pTangentAttrib;
		case (STUC_ATTRIB_SP_TSIGNS):
			return &pMesh->pTSignAttrib;
		case (STUC_ATTRIB_SP_WSCALE):
			return &pMesh->pWScaleAttrib;
		case (STUC_ATTRIB_SP_MAT_IDX):
			return &pMesh->pMatIdxAttrib;
		case (STUC_ATTRIB_SP_EDGE_LEN):
			return &pMesh->pEdgeLenAttrib;
		case (STUC_ATTRIB_SP_SEAM_EDGE):
			return &pMesh->pSeamEdgeAttrib;
		case (STUC_ATTRIB_SP_SEAM_VERT):
			return &pMesh->pSeamVertAttrib;
		case (STUC_ATTRIB_SP_NUM_ADJ_PRESERVE):
			return &pMesh->pNumAdjPreserveAttrib;
		case (STUC_ATTRIB_SP_EDGE_CORNERS):
			return &pMesh->pEdgeCornersAttrib;
	}
	return NULL;
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
	SpecialAttrib special = quickCheckIfSpecialAttrib(pMesh, pAttrib);
	SpecialBufAttrib specialBuf = STUC_ATTRIB_SP_BUF_NONE;
	if (pMesh->core.type.type == STUC_OBJECT_DATA_MESH_BUF) {
		specialBuf = quickCheckIfSpecialBufAttrib((BufMesh *)pMesh, pAttrib);
	}
	I8 oldFirstElement = *(I8 *)stucAttribAsVoid(pAttrib, 0);
	I32 attribSize = stucGetAttribSizeIntern(pAttrib->type);
	pAttrib->pData = pAlloc->pRealloc(pAttrib->pData, attribSize * newLen);
	I8 newFirstElement = *(I8 *)stucAttribAsVoid(pAttrib, 0);
	STUC_ASSERT("", newFirstElement == oldFirstElement);
	reassignIfSpecial(pMesh, pAttrib, special);
	if (pMesh->core.type.type == STUC_OBJECT_DATA_MESH_BUF) {
		reassignIfSpecialBuf((BufMesh *)pMesh, pAttrib, specialBuf);
	}
}

void stucReallocAttribArr(
	const StucAlloc *pAlloc,
	Mesh *pMesh,
	AttribArray *pAttribArr,
	const I32 newLen
) {
	STUC_ASSERT("", newLen >= 0 && newLen < 100000000);
	for (I32 i = 0; i < pAttribArr->count; ++i) {
		Attrib *pAttrib = pAttribArr->pArr + i;
		//Check entry is valid
		STUC_ASSERT("corrupt attrib", pAttrib->interpolate % 2 == pAttrib->interpolate);
		stucReallocAttrib(pAlloc, pMesh, &pAttrib->core, newLen);
		STUC_ASSERT("", i >= 0 && i < pAttribArr->count);
	}
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
		SpecialAttrib special = quickCheckIfSpecialAttrib((Mesh *)pMesh, &pAttrib->core);
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
		reassignIfSpecial((Mesh *)pMesh, &pAttrib->core, special);
		reassignIfSpecialBuf((BufMesh *)pMesh, &pAttrib->core, specialBuf);
		STUC_ASSERT("", i >= 0 && i < pAttribArr->count);
	}
}

Result stucSetSpecialAttribs(StucContext pCtx, Mesh *pMesh, UBitField32 flags) {
	Result err = STUC_SUCCESS;
	STUC_RETURN_ERR_IFNOT_COND(err, pCtx && pMesh, "");
	//TODO replace hard coded attribute pCtx->spAttribNames with function parameters.
	//User can specify which attributes should be treated as vert, uv, and normal.

	for (I32 i = 1; i < STUC_ATTRIB_SP_COUNT; ++i) {
		if (!(flags >> i & 0x1)) {
			continue;
		}
		Attrib **ppAttribRef = getSpAttribRef(pMesh, i);
		*ppAttribRef = stucGetSpAttrib(pCtx, &pMesh->core, i);
		if (*ppAttribRef) {
			reassignIfSpecial(pMesh, &(*ppAttribRef)->core, i);
		}
	}
	return err;
}

Result stucSetSpecialBufAttribs(BufMesh *pMesh, UBitField32 flags) {
	Result err = STUC_SUCCESS;
	StucMesh *pCore = &pMesh->mesh.core;
	if (flags >> STUC_ATTRIB_SP_BUF_W & 0x01) {
		pMesh->pWAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_W],
			&pCore->cornerAttribs
		);
		STUC_RETURN_ERR_IFNOT_COND(err, pMesh->pWAttrib, "buf-mesh has no w attrib");
		pMesh->pW = pMesh->pWAttrib->core.pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_IN_NORMAL & 0x01) {
		pMesh->pInNormalAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_IN_NORMAL],
			&pCore->cornerAttribs
		);
		STUC_RETURN_ERR_IFNOT_COND(err, pMesh->pInNormalAttrib, "buf-mesh has no in-normal attrib");
		pMesh->pInNormal = pMesh->pInNormalAttrib->core.pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_IN_TANGENT & 0x01) {
		pMesh->pInTangentAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_IN_TANGENT],
			&pCore->cornerAttribs
		);
		STUC_RETURN_ERR_IFNOT_COND(err, pMesh->pInTangentAttrib, "buf-mesh has no in-tangent attrib");
		pMesh->pInTangent = pMesh->pInTangentAttrib->core.pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_IN_T_SIGN & 0x01) {
		pMesh->pInTSignAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_IN_T_SIGN],
			&pCore->cornerAttribs
		);
		STUC_RETURN_ERR_IFNOT_COND(err, pMesh->pInTSignAttrib, "buf-mesh has no t-sign attrib");
		pMesh->pInTSign = pMesh->pInTSignAttrib->core.pData;
	}
	if (flags >> STUC_ATTRIB_SP_BUF_ALPHA & 0x01) {
		pMesh->pAlphaAttrib = stucGetAttribIntern(
			spBufAttribs[STUC_ATTRIB_SP_BUF_ALPHA],
			&pCore->cornerAttribs
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
		spBufAttribs[1],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_F32
	);
	pAttrib = NULL;
	stucAppendAttrib(
		pAlloc,
		pAttribArr,
		&pAttrib,
		spBufAttribs[2],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_V3_F32
	);
	pAttrib = NULL;
	stucAppendAttrib(
		pAlloc,
		pAttribArr,
		&pAttrib,
		spBufAttribs[3],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_V3_F32
	);
	pAttrib = NULL;
	stucAppendAttrib(
		pAlloc,
		pAttribArr,
		&pAttrib,
		spBufAttribs[4],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_F32
	);
	pAttrib = NULL;
	stucAppendAttrib(
		pAlloc,
		pAttribArr,
		&pAttrib,
		spBufAttribs[5],
		pMesh->cornerBufSize,
		false,
		STUC_ATTRIB_DONT_COPY,
		STUC_ATTRIB_F32
	);
}

static
void setAttribArrToDontCopy(StucContext pCtx, AttribArray *pArr, UBitField32 flags) {
	if (!flags) {
		return;
	}
	for (I32 i = 0; i < pArr->count; ++i) {
		Attrib *pAttrib = pArr->pArr + i;
		I32 specIdx = checkIfSpecialAttrib(pCtx, pAttrib);
		STUC_ASSERT("there's no 0 special attrib", specIdx != 0);
		if (specIdx <= 0) {
			continue; // not a special attrib, skip
		}
		if (flags >> specIdx & 0x01) {
			pAttrib->origin = STUC_ATTRIB_DONT_COPY;
		}
	}
}

void stucSetAttribToDontCopy(StucContext pCtx, Mesh *pMesh, UBitField32 flags) {
	setAttribArrToDontCopy(pCtx, &pMesh->core.meshAttribs, flags);
	setAttribArrToDontCopy(pCtx, &pMesh->core.faceAttribs, flags);
	setAttribArrToDontCopy(pCtx, &pMesh->core.cornerAttribs, flags);
	setAttribArrToDontCopy(pCtx, &pMesh->core.edgeAttribs, flags);
	setAttribArrToDontCopy(pCtx, &pMesh->core.vertAttribs, flags);
}

void stucSetAttribOrigins(AttribArray *pAttribs, AttribOrigin origin) {
	for (I32 i = 0; i < pAttribs->count; ++i) {
		pAttribs->pArr[i].origin = origin;
	}
}

void stucAllocAttribsFromMeshArr(
	const StucAlloc *pAlloc,
	Mesh *pMeshDest,
	I32 srcCount,
	const Mesh *const *ppMeshSrcs,
	bool setCommon,
	bool allocData,
	bool aliasData
) {
	stucAllocAttribs(
		pAlloc,
		&pMeshDest->core.faceAttribs,
		srcCount,
		ppMeshSrcs,
		pMeshDest->faceBufSize,
		STUC_DOMAIN_FACE,
		setCommon,
		allocData,
		aliasData
	);
	stucAllocAttribs(
		pAlloc,
		&pMeshDest->core.cornerAttribs,
		srcCount,
		ppMeshSrcs,
		pMeshDest->cornerBufSize,
		STUC_DOMAIN_CORNER,
		setCommon,
		allocData,
		aliasData
	);
#ifdef STUC_DISABLE_EDGES_IN_BUF
	if (pMeshDest->core.type.type != STUC_OBJECT_DATA_MESH_BUF) {
#endif
		stucAllocAttribs(
			pAlloc,
			&pMeshDest->core.edgeAttribs,
			srcCount,
			ppMeshSrcs,
			pMeshDest->edgeBufSize,
			STUC_DOMAIN_EDGE,
			setCommon,
			allocData,
			aliasData
		);
#ifdef STUC_DISABLE_EDGES_IN_BUF
	}
#endif
	stucAllocAttribs(
		pAlloc,
		&pMeshDest->core.vertAttribs,
		srcCount,
		ppMeshSrcs,
		pMeshDest->vertBufSize,
		STUC_DOMAIN_VERT,
		setCommon,
		allocData,
		aliasData
	);
}

void stucInitAttrib(
	const StucAlloc *pAlloc,
	Attrib *pAttrib,
	char *pName,
	I32 dataLen,
	bool interpolate,
	AttribOrigin origin,
	AttribType type
) {
	stucInitAttribCore(pAlloc, &pAttrib->core, pName, dataLen, type);
	pAttrib->interpolate = interpolate;
	pAttrib->origin = origin;
}

void stucInitAttribCore(
	const StucAlloc *pAlloc,
	AttribCore *pAttrib,
	char *pName,
	I32 dataLen,
	AttribType type
) {
	memcpy(pAttrib->name, pName, STUC_ATTRIB_NAME_MAX_LEN);
	if (dataLen) {
		pAttrib->pData = pAlloc->pCalloc(dataLen, stucGetAttribSizeIntern(type));
	}
	pAttrib->type = type;
}

void stucAppendAttrib(
	const StucAlloc *pAlloc,
	AttribArray *pArr,
	Attrib **ppAttrib,
	char *pName,
	I32 dataLen,
	bool interpolate,
	AttribOrigin origin,
	AttribType type
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
	pArr->count++;
	stucInitAttrib(pAlloc, *ppAttrib, pName, dataLen, interpolate, origin, type);
}

I32 stucGetStrIdxInIndexedAttrib(AttribIndexed *pMats, const char *pMatName) {
	for (I32 k = 0; k < pMats->count; ++k) {
		if (!strncmp(stucAttribAsStr(&pMats->core, k), pMatName,
		    STUC_ATTRIB_STRING_MAX_LEN)) {

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
	for (I32 i = 1; i < STUC_ATTRIB_SP_COUNT; ++i) {
		if (!(pFlags >> i & 0x1)) {
			continue;
		}
		AttribArray *pArr = NULL;
		I32 dataLen = 0;
		switch (pCtx->spAttribDomains[i]){
			case STUC_DOMAIN_FACE:
				pArr = &pMesh->core.faceAttribs;
				dataLen = pMesh->core.faceCount;
				break;
			case STUC_DOMAIN_CORNER:
				pArr = &pMesh->core.cornerAttribs;
				dataLen = pMesh->core.cornerCount;
				break;
			case STUC_DOMAIN_EDGE:
				pArr = &pMesh->core.edgeAttribs;
				dataLen = pMesh->core.edgeCount;
				break;
			case STUC_DOMAIN_VERT:
				pArr = &pMesh->core.vertAttribs;
				dataLen = pMesh->core.vertCount;
				break;
			default:
				STUC_ASSERT("invalid attrib domain", false);
		}
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
			pCtx->spAttribTypes[i]
		);
	}
}

Attrib *stucGetSpAttrib(StucContext pCtx, StucMesh *pMesh, SpecialAttrib special) {
	AttribArray *pArr = getAttribArrFromDomain(pMesh, pCtx->spAttribDomains[special]);
	return stucGetAttribIntern(pCtx->spAttribNames[special], pArr);
}

const Attrib *stucGetSpAttribConst(
	StucContext pCtx,
	const StucMesh *pMesh,
	SpecialAttrib special
) {
	return stucGetSpAttrib(pCtx, (StucMesh *)pMesh, special);
}

void stucQuerySpAttribs(
	StucContext pCtx,
	const StucMesh *pMesh,
	UBitField32 toCheck,
	UBitField32 *pHas
) {
	*pHas = 0;
	for (I32 i = 1; i < STUC_ATTRIB_SP_COUNT; ++i) {
		if (!(toCheck >> i & 0x1)) {
			continue;
		}
		const Attrib *pAttrib = stucGetSpAttribConst(pCtx, pMesh, i);
		if (pAttrib) {
			*pHas |= 0x1 << i;
		}
	}
}