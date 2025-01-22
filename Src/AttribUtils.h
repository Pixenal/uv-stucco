#pragma once

#include <UvStucco.h>
#include <UvStuccoIntern.h>
#include <MathUtils.h>
#include <Mesh.h>

typedef StucAttrib Attrib;
typedef StucAttribIndexed AttribIndexed;
typedef StucAttribType AttribType;
typedef StucAttribArray AttribArray;
typedef StucAttribIndexedArr AttribIndexedArr;
typedef StucAttribOrigin AttribOrigin;
typedef StucBlendConfig BlendConfig;

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
StucAttrib *getAttrib(char *pName, StucAttribArray *pAttribs);
V3_F32 *attribAsV3(Attrib *pAttrib, int32_t idx);
V2_F32 *attribAsV2(Attrib *pAttrib, int32_t idx);
int32_t *attribAsI32(Attrib *pAttrib, int32_t idx);
void *attribAsVoid(Attrib *pAttrib, int32_t idx);
int32_t copyAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrc, int32_t iSrc);
void copyAllAttribs(AttribArray *pDest, int32_t iDest,
                    AttribArray *pSrc, int32_t iSrc);
StucTypeDefault *getTypeDefaultConfig(StucTypeDefaultConfig *pConfig,
                                      AttribType type);
StucCommonAttrib *getCommonAttrib(StucCommonAttrib *pAttribs, int32_t attribCount,
                                  char *pName);
void lerpAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrcA,
                int32_t iSrcA, Attrib *pSrcB, int32_t iSrcB, float alpha);
void triInterpolateAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrc,
                          int32_t iSrcA, int32_t iSrcB, int32_t iSrcC, V3_F32 bc);
void blendAttribs(Attrib *pDest, int32_t iDest, Attrib *pA, int32_t iA,
                  Attrib *pB, int32_t iB, StucBlendConfig blendConfig);
void divideAttribByScalarInt(Attrib *pAttrib, int32_t idx, uint64_t scalar);
void allocAttribs(StucAlloc *pAlloc, AttribArray *pDest,
                  int32_t srcCount, Mesh **ppSrcArr,
				  int32_t dataLen, StucDomain domain, bool setCommon);
void castType(void *pValue, StucAttribType type);
void reallocAttribs(const StucAlloc *pAlloc, Mesh *pMesh,
                    AttribArray *pAttribArr, int32_t newLen);
void reallocAndMoveAttribs(const StucAlloc *pAlloc, BufMesh *pMesh,
                           AttribArray *pAttribArr, int32_t start,
						   int32_t offset, int32_t lenToCopy, int32_t newLen);
void setSpecialAttribs(Mesh *pMesh, UBitField16 flags);
void allocAttribsFromMeshArr(StucAlloc *pAlloc, Mesh *pMeshDest,
                             int32_t srcCount, Mesh **ppMeshSrcs, bool setCommon);
void initAttrib(StucAlloc *pAlloc, Attrib *pAttrib, char *pName, int32_t dataLen,
                bool interpolate, AttribOrigin origin, AttribType type);