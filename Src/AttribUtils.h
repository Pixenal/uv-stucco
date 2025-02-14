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
typedef StucCommonAttrib CommonAttrib;
typedef StucCommonAttribList CommonAttribList;

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

void stucSetDefaultSpecialAttribNames(StucContext pContext);
int32_t stucGetAttribSizeIntern(AttribType type);
StucAttrib *stucGetAttribIntern(char *pName, AttribArray *pAttribs);
V3_F32 *stucAttribAsV3(AttribCore *pAttrib, int32_t idx);
V2_F32 *stucAttribAsV2(AttribCore *pAttrib, int32_t idx);
int32_t *stucAttribAsI32(AttribCore *pAttrib, int32_t idx);
int8_t *stucAttribAsI8(AttribCore *pAttrib, int32_t idx);
char *stucAttribAsStr(AttribCore *pAttrib, int32_t idx);
void *stucAttribAsVoid(AttribCore *pAttrib, int32_t idx);
int32_t stucCopyAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrc, int32_t iSrc);
void stucCopyAllAttribs(AttribArray *pDest, int32_t iDest,
                        AttribArray *pSrc, int32_t iSrc);
void stucSetTypeDefaultConfig(StucContext pContext);
StucTypeDefault *stucGetTypeDefaultConfig(StucTypeDefaultConfig *pConfig,
                                          AttribType type);
StucCommonAttrib *stucGetCommonAttrib(StucCommonAttrib *pAttribs, int32_t attribCount,
                                      char *pName);
AttribIndexed *stucGetAttribIndexedIntern(AttribIndexedArr *pAttribArr, char *pName);
void stucLerpAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrcA,
                    int32_t iSrcA, Attrib *pSrcB, int32_t iSrcB, float alpha);
void stucTriInterpolateAttrib(Attrib *pDest, int32_t iDest, Attrib *pSrc,
                              int32_t iSrcA, int32_t iSrcB, int32_t iSrcC, V3_F32 bc);
void stucBlendAttribs(Attrib *pDest, int32_t iDest, Attrib *pA, int32_t iA,
                      Attrib *pB, int32_t iB, StucBlendConfig blendConfig);
void stucDivideAttribByScalarInt(Attrib *pAttrib, int32_t idx, uint64_t scalar);
void stucAllocAttribs(StucAlloc *pAlloc, AttribArray *pDest,
                      int32_t srcCount, Mesh **ppSrcArr,
                      int32_t dataLen, StucDomain domain, bool setCommon);
void stucReallocAttrib(const StucAlloc *pAlloc, Mesh *pMesh,
                       AttribCore *pAttrib, const int32_t newLen);
void stucReallocAttribArr(const StucAlloc *pAlloc, Mesh *pMesh,
                          AttribArray *pAttribArr, int32_t newLen);
void stucReallocAndMoveAttribs(const StucAlloc *pAlloc, BufMesh *pMesh,
                               AttribArray *pAttribArr, int32_t start,
                               int32_t offset, int32_t lenToCopy, int32_t newLen);
void stucSetSpecialAttribs(StucContext pContext, Mesh *pMesh, UBitField16 flags);
void stucSetSpecialBufAttribs(BufMesh *pMesh, UBitField16 flags);
void stucAppendBufOnlySpecialAttribs(StucAlloc *pAlloc, BufMesh *pBufMesh);
void stucSetAttribToDontCopy(StucContext pContext, Mesh *pMesh, UBitField16 flags);
void stucSetAttribOrigins(AttribArray *pAttribs, AttribOrigin origin);
void stucAllocAttribsFromMeshArr(StucAlloc *pAlloc, Mesh *pMeshDest,
                                 int32_t srcCount, Mesh **ppMeshSrcs, bool setCommon);
void stucInitAttrib(StucAlloc *pAlloc, Attrib *pAttrib, char *pName, int32_t dataLen,
                    bool interpolate, AttribOrigin origin, AttribType type);
void stucInitAttribCore(StucAlloc *pAlloc, AttribCore *pAttrib, char *pName, int32_t dataLen,
                        AttribType type);
void stucAppendAttrib(StucAlloc *pAlloc, AttribArray *pArr, Attrib **ppAttrib, char *pName,
                      int32_t dataLen, bool interpolate, AttribOrigin origin, AttribType type);
int32_t stucGetStrIdxInIndexedAttrib(AttribIndexed *pMats, char *pMatName);