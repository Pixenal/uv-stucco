#pragma once

#include <UvStucco.h>
#include <UvStuccoIntern.h>
#include <MathUtils.h>
#include <Mesh.h>

typedef StucAttribCore AttribCore;
typedef StucAttrib Attrib;
typedef StucAttribIndexed AttribIndexed;
typedef StucAttribType AttribType;
typedef StucAttribArray AttribArray;
typedef StucAttribIndexedArr AttribIndexedArr;
typedef StucAttribOrigin AttribOrigin;
typedef StucBlendConfig BlendConfig;

//TODO switch pAttrib pData ptr from void * to uint8_t *?

typedef enum {
	STUC_ATTRIB_SP_NONE,
	STUC_ATTRIB_SP_VERTS,
	STUC_ATTRIB_SP_UVS,
	STUC_ATTRIB_SP_NORMALS,
	STUC_ATTRIB_SP_PRESERVE,
	STUC_ATTRIB_SP_RECEIVE,
	STUC_ATTRIB_SP_PRESERVE_VERT,
	STUC_ATTRIB_SP_USG,
	STUC_ATTRIB_SP_TANGENTS,
	STUC_ATTRIB_SP_TSIGNS,
	STUC_ATTRIB_SP_WSCALE,
	STUC_ATTRIB_SP_MAT_IDX
} SpecialAttrib;

typedef enum {
	STUC_ATTRIB_SP_BUF_NONE,
	STUC_ATTRIB_SP_BUF_W,
	STUC_ATTRIB_SP_BUF_IN_NORMAL,
	STUC_ATTRIB_SP_BUF_IN_TANGENT,
	STUC_ATTRIB_SP_BUF_IN_T_SIGN,
	STUC_ATTRIB_SP_BUF_ALPHA
} SpecialBufAttrib;

void setDefaultSpecialAttribNames(StucContext pContext);
int32_t getAttribSize(AttribType type);
StucAttrib *getAttrib(char *pName, AttribArray *pAttribs);
V3_F32 *attribAsV3(AttribCore *pAttrib, int32_t idx);
V2_F32 *attribAsV2(AttribCore *pAttrib, int32_t idx);
int32_t *attribAsI32(AttribCore *pAttrib, int32_t idx);
int8_t *attribAsI8(AttribCore *pAttrib, int32_t idx);
void *attribAsVoid(AttribCore *pAttrib, int32_t idx);
int32_t copyAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrc, int32_t iSrc);
void copyAllAttribs(AttribArray *pDest, int32_t iDest,
                    AttribArray *pSrc, int32_t iSrc);
void stucSetTypeDefaultConfig(StucContext pContext);
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
void reallocAttribs(const StucAlloc *pAlloc, Mesh *pMesh,
                    AttribArray *pAttribArr, int32_t newLen);
void reallocAndMoveAttribs(const StucAlloc *pAlloc, BufMesh *pMesh,
                           AttribArray *pAttribArr, int32_t start,
						   int32_t offset, int32_t lenToCopy, int32_t newLen);
void setSpecialAttribs(StucContext pContext, Mesh *pMesh, UBitField16 flags);
void setSpecialBufAttribs(BufMesh *pMesh, UBitField16 flags);
void appendBufOnlySpecialAttribs(StucAlloc *pAlloc, BufMesh *pBufMesh);
void setAttribToDontCopy(StucContext pContext, Mesh *pMesh, UBitField16 flags);
void setAttribOrigins(AttribArray *pAttribs, AttribOrigin origin);
void allocAttribsFromMeshArr(StucAlloc *pAlloc, Mesh *pMeshDest,
                             int32_t srcCount, Mesh **ppMeshSrcs, bool setCommon);
void initAttrib(StucAlloc *pAlloc, Attrib *pAttrib, char *pName, int32_t dataLen,
                bool interpolate, AttribOrigin origin, AttribType type);
void appendAttrib(StucAlloc *pAlloc, AttribArray *pArr, Attrib **ppAttrib, char *pName,
                  int32_t dataLen, bool interpolate, AttribOrigin origin, AttribType type);