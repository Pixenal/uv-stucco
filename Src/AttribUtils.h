#pragma once

#include <UvStucco.h>
#include <UvStuccoIntern.h>
#include <MathUtils.h>
#include <Mesh.h>
#include <Types.h>

typedef StucAttribCore AttribCore;
typedef StucAttrib Attrib;
typedef StucAttribIndexed AttribIndexed;
typedef StucAttribActive AttribActive;
typedef StucAttribType AttribType;
typedef StucAttribArray AttribArray;
typedef StucAttribIndexedArr AttribIndexedArr;
typedef StucAttribOrigin AttribOrigin;
typedef StucAttribCopyOpt AttribCopyOpt;
typedef StucBlendConfig BlendConfig;
typedef StucCommonAttrib CommonAttrib;
typedef StucCommonAttribList CommonAttribList;

//TODO switch pAttrib pData ptr from void * to U8 *?

typedef enum {
	STUC_ATTRIB_SP_BUF_NONE,
	STUC_ATTRIB_SP_BUF_W,
	STUC_ATTRIB_SP_BUF_IN_NORMAL,
	STUC_ATTRIB_SP_BUF_IN_TANGENT,
	STUC_ATTRIB_SP_BUF_IN_T_SIGN,
	STUC_ATTRIB_SP_BUF_ALPHA
} SpecialBufAttrib;

void stucSetDefaultSpAttribNames(StucContext pCtx);
void stucSetDefaultSpAttribDomains(StucContext pCtx);
void stucSetDefaultSpAttribTypes(StucContext pCtx);
I32 stucGetAttribSizeIntern(AttribType type);
Result stucAssignActiveAliases(
	StucContext pCtx,
	Mesh *pMesh,
	UBitField32 flags,
	StucDomain domain
);
Attrib *stucGetActiveAttrib(StucContext pCtx, StucMesh *pMesh, StucAttribUse use);
const Attrib *stucGetActiveAttribConst(
	StucContext pCtx,
	const StucMesh *pMesh,
	StucAttribUse use
);
bool stucIsAttribActive(
	const StucContext pCtx,
	const StucMesh *pMesh,
	const Attrib *pAttrib
);
bool stucIsAttribIdxActive(const StucMesh *pMesh, const AttribArray *pArr, I32 idx);
//pCtx and pMesh can be NULL if excludeActive is false
StucAttrib *stucGetAttribIntern(
	const char *pName,
	AttribArray *pAttribs,
	bool excludeActive,
	const StucContext pCtx,
	const StucMesh *pMesh
);
//pCtx and pMesh can be NULL if excludeActive is false
const Attrib *stucGetAttribInternConst(
	const char *pName,
	const AttribArray *pAttribs,
	bool excludeActive,
	const StucContext pCtx,
	const StucMesh *pMesh
);
void stucSetAttribIdxActive(StucMesh *pMesh, I32 idx, StucAttribUse use);
V3_F32 *stucAttribAsV3(AttribCore *pAttrib, I32 idx);
V2_F32 *stucAttribAsV2(AttribCore *pAttrib, I32 idx);
I32 *stucAttribAsI32(AttribCore *pAttrib, I32 idx);
I8 *stucAttribAsI8(AttribCore *pAttrib, I32 idx);
const char *stucAttribAsStrConst(const AttribCore *pAttrib, I32 idx);
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
AttribArray *stucGetAttribArrFromDomain(StucMesh *pMesh, StucDomain domain);
const AttribArray *stucGetAttribArrFromDomainConst(const StucMesh *pMesh, StucDomain domain);
const StucCommonAttrib *stucGetCommonAttrib(
	const StucCommonAttribArr *pArr,
	char *pName
);
Result stucGetMatchingAttrib(
	StucContext pCtx,
	StucMesh *pDest,
	AttribArray *pDestAttribArr,
	const StucMesh *pSrc,
	const Attrib *pSrcAttrib,
	bool searchActive,
	Attrib **ppOut
);
Result stucGetMatchingAttribConst(
	StucContext pCtx,
	const StucMesh *pDest,
	const AttribArray *pDestAttribArr,
	const StucMesh *pSrc,
	const Attrib *pSrcAttrib,
	bool searchActive,
	const Attrib **ppOut
);
const StucCommonAttrib *stucGetCommonAttribFromDomain(
	const StucCommonAttribList *pList,
	char *pName,
	StucDomain domain
);
AttribIndexed *stucGetAttribIndexedIntern(
	AttribIndexedArr *pAttribArr,
	const char *pName
);
AttribIndexed *stucGetAttribIndexedInternConst(
	const AttribIndexedArr *pAttribArr,
	const char *pName
);
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
);
void stucReallocAttrib(
	const StucAlloc *pAlloc,
	Mesh *pMesh,
	AttribCore *pAttrib,
	const I32 newLen
);
void stucReallocAttribArr(
	StucContext pCtx,
	StucDomain domain,
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
StucResult stucSetSpecialBufAttribs(BufMesh *pMesh, UBitField32 flags);
void stucAppendBufOnlySpecialAttribs(const StucAlloc *pAlloc, BufMesh *pBufMesh);
void stucSetAttribCopyOpt(
	StucContext pCtx,
	StucMesh *pMesh,
	StucAttribCopyOpt opt,
	UBitField32 flags
);
void stucSetAttribOrigins(AttribArray *pAttribs, AttribOrigin origin);
Result stucAllocAttribsFromMeshArr(
	StucContext pCtx,
	Mesh *pDest,
	I32 srcCount,
	const Mesh *const *ppMeshSrcs,
	I32 activeSrc,
	bool setCommon,
	bool allocData,
	bool aliasData
);
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
);
void stucInitAttribCore(
	const StucAlloc *pAlloc,
	AttribCore *pAttrib,
	const char *pName,
	I32 dataLen,
	AttribType type,
	StucAttribUse use
);
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
);
bool stucCmpAttribs(const AttribCore *pA, I32 iA, const AttribCore *pB, I32 iB);
I32 stucGetIdxInIndexedAttrib(
	const AttribIndexed *pDest,
	const AttribIndexed *pSrc,
	I32 srcIdx
);
void stucAppendSpAttribsToMesh(
	StucContext pCtx,
	Mesh *pMesh,
	UBitField32 pFlags,
	StucAttribOrigin origin
);
void stucQuerySpAttribs(
	StucContext pCtx,
	const StucMesh *pMesh,
	UBitField32 toCheck,
	UBitField32 *pHas
);
bool stucCheckAttribsAreCompatible(const Attrib *pA, const Attrib *pB);
AttribIndexed *stucAppendIndexedAttrib(
	StucContext pCtx,
	AttribIndexedArr *pIndexedAttribArr,
	const char *pName,
	I32 dataLen,
	StucAttribType type,
	StucAttribUse use
);
Result stucAppendAndCopyIndexedAttrib(
	StucContext pCtx,
	const char *pName,
	AttribIndexedArr *pDestArr,
	const AttribIndexedArr *pSrcArr
);
void stucAppendToIndexedAttrib(
	StucContext pCtx,
	AttribIndexed *pDest,
	const AttribCore *pSrc,
	I32 srcIdx
);