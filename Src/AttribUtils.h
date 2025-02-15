#pragma once

#include <UvStucco.h>
#include <UvStuccoIntern.h>
#include <MathUtils.h>
#include <Mesh.h>
#include <Types.h>

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

//TODO switch pAttrib pData ptr from void * to U8 *?

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

void stucSetDefaultSpecialAttribNames(StucContext pCtx);
I32 stucGetAttribSizeIntern(AttribType type);
StucAttrib *stucGetAttribIntern(const char *pName, AttribArray *pAttribs);
const Attrib *stucGetAttribInternConst(const char *pName, const AttribArray *pAttribs);
V3_F32 *stucAttribAsV3(AttribCore *pAttrib, I32 idx);
V2_F32 *stucAttribAsV2(AttribCore *pAttrib, I32 idx);
I32 *stucAttribAsI32(AttribCore *pAttrib, I32 idx);
I8 *stucAttribAsI8(AttribCore *pAttrib, I32 idx);
char *stucAttribAsStr(AttribCore *pAttrib, I32 idx);
void *stucAttribAsVoid(AttribCore *pAttrib, I32 idx);
const void *stucAttribAsVoidConst(const AttribCore *pAttrib, I32 idx);
I32 stucCopyAttrib(Attrib *pDest, I32 iDest, const Attrib *pSrc, I32 iSrc);
void stucCopyAllAttribs(
	AttribArray *pDest,
	I32 iDest,
	AttribArray *pSrc,
	I32 iSrc
);
void stucSetTypeDefaultConfig(StucContext pCtx);
StucTypeDefault *stucGetTypeDefaultConfig(
	StucTypeDefaultConfig *pConfig,
	AttribType type
);
StucCommonAttrib *stucGetCommonAttrib(
	StucCommonAttrib *pAttribs,
	I32 attribCount,
	char *pName
);
AttribIndexed *stucGetAttribIndexedIntern(AttribIndexedArr *pAttribArr, char *pName);
void stucLerpAttrib(
	Attrib *pDest,
	I32 iDest,
	Attrib *pSrcA,
	I32 iSrcA,
	Attrib *pSrcB,
	I32 iSrcB,
	F32 alpha);
void stucTriInterpolateAttrib(
	Attrib *pDest,
	I32 iDest,
	const Attrib *pSrc,
	I32 iSrcA,
	I32 iSrcB,
	I32 iSrcC,
	V3_F32 bc
);
void stucBlendAttribs(
	Attrib *pDest,
	I32 iDest,
	Attrib *pA,
	I32 iA,
	Attrib *pB,
	I32 iB,
	StucBlendConfig blendConfig
);
void stucDivideAttribByScalarInt(Attrib *pAttrib, I32 idx, U64 scalar);
void stucAllocAttribs(
	const StucAlloc *pAlloc,
	AttribArray *pDest,
	I32 srcCount,
	const Mesh *const *ppSrcArr,
	I32 dataLen,
	StucDomain domain,
	bool setCommon
);
void stucReallocAttrib(
	const StucAlloc *pAlloc,
	Mesh *pMesh,
	AttribCore *pAttrib,
	const I32 newLen
);
void stucReallocAttribArr(
	const StucAlloc *pAlloc,
	Mesh *pMesh,
	AttribArray *pAttribArr,
	I32 newLen
);
void stucReallocAndMoveAttribs(
	const StucAlloc *pAlloc,
	const BufMesh *pMesh,
	AttribArray *pAttribArr,
	I32 start,
	I32 offset,
	I32 lenToCopy,
	I32 newLen
);
void stucSetSpecialAttribs(StucContext pCtx, Mesh *pMesh, UBitField16 flags);
void stucSetSpecialBufAttribs(BufMesh *pMesh, UBitField16 flags);
void stucAppendBufOnlySpecialAttribs(const StucAlloc *pAlloc, BufMesh *pBufMesh);
void stucSetAttribToDontCopy(StucContext pCtx, Mesh *pMesh, UBitField16 flags);
void stucSetAttribOrigins(AttribArray *pAttribs, AttribOrigin origin);
void stucAllocAttribsFromMeshArr(
	const StucAlloc *pAlloc,
	Mesh *pMeshDest,
	I32 srcCount,
	const Mesh *const *ppMeshSrcs,
	bool setCommon
);
void stucInitAttrib(
	const StucAlloc *pAlloc,
	Attrib *pAttrib,
	char *pName,
	I32 dataLen,
	bool interpolate,
	AttribOrigin origin,
	AttribType type
);
void stucInitAttribCore(
	const StucAlloc *pAlloc,
	AttribCore *pAttrib,
	char *pName,
	I32 dataLen,
	AttribType type
);
void stucAppendAttrib(
	const StucAlloc *pAlloc,
	AttribArray *pArr,
	Attrib **ppAttrib,
	char *pName,
	I32 dataLen,
	bool interpolate,
	AttribOrigin origin,
	AttribType type
);
I32 stucGetStrIdxInIndexedAttrib(AttribIndexed *pMats, char *pMatName);