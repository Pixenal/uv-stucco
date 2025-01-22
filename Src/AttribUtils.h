#pragma once

#include <RUVM.h>
#include <RuvmInternal.h>
#include <MathUtils.h>
#include <Mesh.h>

typedef RuvmAttrib Attrib;
typedef RuvmAttribIndexed AttribIndexed;
typedef RuvmAttribType AttribType;
typedef RuvmAttribArray AttribArray;
typedef RuvmAttribIndexedArr AttribIndexedArr;
typedef RuvmAttribOrigin AttribOrigin;
typedef RuvmBlendConfig BlendConfig;

//TODO switch pAttrib pData ptr from void * to uint8_t *?

typedef enum {
	ATTRIB_SPECIAL_NONE,
	ATTRIB_SPECIAL_VERTS,
	ATTRIB_SPECIAL_UVS,
	ATTRIB_SPECIAL_NORMALS,
	ATTRIB_SPECIAL_PRESERVE,
	ATTRIB_SPECIAL_RECEIVE,
	ATTRIB_SPECIAL_PRESERVE_VERT,
	ATTRIB_SPECIAL_USG,
	ATTRIB_SPECIAL_TANGENTS,
	ATTRIB_SPECIAL_TSIGNS,
	ATTRIB_SPECIAL_WSCALE
} SpecialAttrib;

int32_t getAttribSize(AttribType type);
RuvmAttrib *getAttrib(char *pName, RuvmAttribArray *pAttribs);
V3_F32 *attribAsV3(Attrib *pAttrib, int32_t idx);
V2_F32 *attribAsV2(Attrib *pAttrib, int32_t idx);
int32_t *attribAsI32(Attrib *pAttrib, int32_t idx);
void *attribAsVoid(Attrib *pAttrib, int32_t idx);
int32_t copyAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrc, int32_t iSrc);
void copyAllAttribs(AttribArray *pDest, int32_t iDest,
                    AttribArray *pSrc, int32_t iSrc);
RuvmTypeDefault *getTypeDefaultConfig(RuvmTypeDefaultConfig *pConfig,
                                      AttribType type);
RuvmCommonAttrib *getCommonAttrib(RuvmCommonAttrib *pAttribs, int32_t attribCount,
                                  char *pName);
void lerpAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrcA,
                int32_t iSrcA, Attrib *pSrcB, int32_t iSrcB, float alpha);
void triInterpolateAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrc,
                          int32_t iSrcA, int32_t iSrcB, int32_t iSrcC, V3_F32 bc);
void blendAttribs(Attrib *pDest, int32_t iDest, Attrib *pA, int32_t iA,
                  Attrib *pB, int32_t iB, RuvmBlendConfig blendConfig);
void divideAttribByScalarInt(Attrib *pAttrib, int32_t idx, uint64_t scalar);
void allocAttribs(RuvmAlloc *pAlloc, AttribArray *pDest,
                  int32_t srcCount, Mesh **ppSrcArr,
				  int32_t dataLen, RuvmDomain domain, bool setCommon);
void castType(void *pValue, RuvmAttribType type);
void reallocAttribs(const RuvmAlloc *pAlloc, Mesh *pMesh,
                    AttribArray *pAttribArr, int32_t newLen);
void reallocAndMoveAttribs(const RuvmAlloc *pAlloc, BufMesh *pMesh,
                           AttribArray *pAttribArr, int32_t start,
						   int32_t offset, int32_t lenToCopy, int32_t newLen);
void setSpecialAttribs(Mesh *pMesh, UBitField16 flags);
void allocAttribsFromMeshArr(RuvmAlloc *pAlloc, Mesh *pMeshDest,
                             int32_t srcCount, Mesh **ppMeshSrcs, bool setCommon);
void initAttrib(RuvmAlloc *pAlloc, Attrib *pAttrib, char *pName, int32_t dataLen,
                bool interpolate, AttribOrigin origin, AttribType type);