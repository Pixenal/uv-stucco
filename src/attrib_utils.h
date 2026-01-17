/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <pixenals_math_utils.h>

#include <uv_stucco_intern.h>

typedef StucAttribCore AttribCore;
typedef StucAttrib Attrib;
typedef StucAttribIndexed AttribIndexed;
typedef StucAttribActive AttribActive;
typedef StucAttribType AttribType;
typedef StucAttribUse AttribUse;
typedef StucAttribArray AttribArray;
typedef StucAttribIndexedArr AttribIndexedArr;
typedef StucAttribOrigin AttribOrigin;
typedef StucAttribCopyOpt AttribCopyOpt;
typedef StucBlendConfig BlendConfig;
typedef StucBlendOpt BlendOpt;
typedef StucBlendOptArr BlendOptArr;

//TODO switch pAttrib pData ptr from void * to U8 *?

static const I8 attribSizes[STUC_ATTRIB_TYPE_ENUM_COUNT] = {
	1,// I8
	2,// I16
	4,// I32
	8,// I64
	4,// F32
	8,// F64
	2,// V2_I8
	4,// V2_I16
	8,// V2_I32
	16,// V2_I64
	8,// V2_F32
	16,// V2_F64
	3,// V3_I8
	6,// V3_I16
	12,// V3_I32
	24,// V3_I64
	12,// V3_F32
	24,// V3_F64
	4,// V4_I8
	8,// V4_I16
	16,// V4_I32
	32,// V4_I64
	16,// V4_F32
	32,// V4_F64
	STUC_ATTRIB_STRING_MAX_LEN,// STRING
	0 //None
};

static inline
I32 stucGetAttribSizeIntern(AttribType type) {
	return (I32)attribSizes[type];
}

static inline
V3_F32 *stucAttribAsV3(AttribCore *pAttrib, I32 idx) {
	return (V3_F32 *)pAttrib->pData + idx;
}

static inline
V2_F32 *stucAttribAsV2(AttribCore *pAttrib, I32 idx) {
	return (V2_F32 *)pAttrib->pData + idx;
}

static inline
I32 *stucAttribAsI32(AttribCore *pAttrib, I32 idx) {
	return (I32 *)pAttrib->pData + idx;
}

static inline
I8 *stucAttribAsI8(AttribCore *pAttrib, I32 idx) {
	return (I8 *)pAttrib->pData + idx;
}

static inline
const char *stucAttribAsStrConst(const AttribCore *pAttrib, I32 idx) {
	return ((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pAttrib->pData)[idx];
}

static inline
char *stucAttribAsStr(AttribCore *pAttrib, I32 idx) {
	return ((char (*)[STUC_ATTRIB_STRING_MAX_LEN])pAttrib->pData)[idx];
}

static inline
void *stucAttribAsVoid(AttribCore *pAttrib, I32 idx) {
	return ((U8 *)pAttrib->pData) + idx * stucGetAttribSizeIntern(pAttrib->type);
}

static inline
const void *stucAttribAsVoidConst(const AttribCore *pAttrib, I32 idx) {
	return stucAttribAsVoid((AttribCore *)pAttrib, idx);
}

//TODO some of these funcs still don't use PixErr
static inline
I32 stucCopyAttribCore(AttribCore *pDest, I32 iDest, const AttribCore *pSrc, I32 iSrc) {
	PIX_ERR_ASSERT("", pSrc->type == pDest->type);
	I32 size = stucGetAttribSizeIntern(pSrc->type);
	memcpy(
		((U8 *)pDest->pData) + iDest * size,
		((U8 *)pSrc->pData) + iSrc * size,
		size
	);
	return 0;
}

static inline
I32 stucCopyAttrib(Attrib *pDest, I32 iDest, const Attrib *pSrc, I32 iSrc) {
	PIX_ERR_ASSERT("", pSrc->copyOpt == STUC_ATTRIB_COPY);
	return stucCopyAttribCore(&pDest->core, iDest, &pSrc->core, iSrc);
}

typedef enum SpecialBufAttrib {
	STUC_ATTRIB_SP_BUF_NONE,
	STUC_ATTRIB_SP_BUF_W,
	STUC_ATTRIB_SP_BUF_IN_NORMAL,
	STUC_ATTRIB_SP_BUF_IN_TANGENT,
	STUC_ATTRIB_SP_BUF_IN_T_SIGN,
	STUC_ATTRIB_SP_BUF_ALPHA,
	STUC_ATTRIB_SP_BUF_IN_MAP_FACE_PAIR,
	STUC_ATTRIB_SP_BUF_ENUM_COUNT
} SpecialBufAttrib;

void stucSetDefaultSpAttribNames(StucContext pCtx);
void stucSetDefaultSpAttribDomains(StucContext pCtx);
void stucSetDefaultSpAttribTypes(StucContext pCtx);
StucErr stucAssignActiveAliases(
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
	const StucMesh *pMesh,
	I32 *pIdx
);
//pCtx and pMesh can be NULL if excludeActive is false
const Attrib *stucGetAttribInternConst(
	const char *pName,
	const AttribArray *pAttribs,
	bool excludeActive,
	const StucContext pCtx,
	const StucMesh *pMesh,
	I32 *pIdx
);
void stucSetAttribIdxActive(
	StucMesh *pMesh,
	I32 idx,
	StucAttribUse use,
	StucDomain domain
);
void stucSetTypeDefaultConfig(StucContext pCtx);
StucTypeDefault *stucGetTypeDefaultConfig(
	StucTypeDefaultConfig *pConfig,
	AttribType type
);
AttribArray *stucGetAttribArrFromDomain(StucMesh *pMesh, StucDomain domain);
const AttribArray *stucGetAttribArrFromDomainConst(const StucMesh *pMesh, StucDomain domain);
StucErr stucGetMatchingAttrib(
	StucContext pCtx,
	StucMesh *pDest, AttribArray *pDestAttribArr,
	const StucMesh *pSrc, const Attrib *pSrcAttrib,
	bool searchActive,
	bool excludeActive,
	Attrib **ppOut
);
StucErr stucGetMatchingAttribConst(
	StucContext pCtx,
	const StucMesh *pDest, const AttribArray *pDestAttribArr,
	const StucMesh *pSrc, const Attrib *pSrcAttrib,
	bool searchActive,
	bool excludeActive,
	const Attrib **ppOut
);
const StucBlendOpt *stucGetBlendOpt(
	const StucBlendOptArr *pOptArr,
	I32 attribIdx,
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
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrcA, I32 iSrcA,
	const AttribCore *pSrcB, I32 iSrcB,
	F32 alpha
);
void stucTriInterpolateAttrib(
	AttribCore *pDest, I32 iDest,
	const AttribCore *pSrc,
	I32 iSrcA, I32 iSrcB, I32 iSrcC,
	V3_F32 bc
);
void stucBlendAttribs(
	AttribCore *pDest, I32 iDest,
	const AttribCore *pA, I32 iA,
	const AttribCore *pB, I32 iB,
	StucBlendConfig blendConfig
);
void stucDivideAttribByScalarInt(AttribCore *pAttrib, I32 idx, U64 scalar);
StucErr stucAllocAttribs(
	StucContext pCtx,
	StucDomain domain,
	I32 domainSize,
	StucMesh *pDest,
	I32 srcCount,
	const StucMesh *const *ppSrcArr,
	I32 activeSrc,
	bool setCommon,
	bool allocData,
	bool aliasData,
	bool activeOnly
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
void stucSetAttribCopyOpt(
	StucContext pCtx,
	StucMesh *pMesh,
	StucAttribCopyOpt opt,
	UBitField32 flags
);
void stucSetAttribOrigins(AttribArray *pAttribs, AttribOrigin origin);
StucErr stucAllocAttribsFromMeshArr(
	StucContext pCtx,
	Mesh *pDest,
	I32 srcCount,
	const Mesh *const *ppMeshSrcs,
	I32 activeSrc,
	bool setCommon,
	bool allocData,
	bool aliasData,
	bool activeOnly
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
bool stucCmpIdxAttribs(const AttribCore *pA, I32 iA, const AttribCore *pB, I32 iB);
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
StucErr stucAppendAndCopyIdxAttrib(
	StucContext pCtx,
	const AttribIndexed *pSrc,
	AttribIndexedArr *pDestArr
);
StucErr stucAppendAndCopyIdxAttribFromName(
	StucContext pCtx,
	const char *pName,
	const AttribIndexedArr *pSrcArr,
	AttribIndexedArr *pDestArr
);
void stucAppendToIndexedAttrib(
	StucContext pCtx,
	AttribIndexed *pDest,
	const AttribCore *pSrc,
	I32 srcIdx
);
AttribType stucAttribGetCompTypeIntern(AttribType type);
I32 stucAttribTypeGetVecSizeIntern(AttribType type);
bool stucIsAttribUseRequired(StucAttribUse use);
UBitField32 stucAttribUseField(const StucAttribUse *pArr, I32 count);

#define STUC_ATTRIB_USE_FIELD(arr)\
	stucAttribUseField((arr), sizeof(arr) / sizeof(StucAttribUse))

StucErr stucAttemptToSetMissingActiveDomains(StucMesh *pMesh);

static inline
void stucCopyAttribs(AttribArray *pDest, I32 iDest, AttribArray *pSrc, I32 iSrc) {
	for (I32 i = 0; i < pSrc->count; ++i) {
		AttribCore *pDestAttrib = &pDest->pArr[pDest->count + i].core;
		AttribCore *pSrcAttrib = &pSrc->pArr[i].core;
		memcpy(
			stucAttribAsVoid(pDestAttrib, iDest),
			stucAttribAsVoid(pSrcAttrib, iSrc),
			stucGetAttribSizeIntern(pSrcAttrib->type)
		);
	}
}

static inline
void stucCopyInSameAttrib(AttribArray *pArr, I32 iDest, I32 iSrc) {
	for (I32 i = 0; i < pArr->count; ++i) {
		AttribCore *pAttrib = &pArr->pArr[i].core;
		memcpy(
			stucAttribAsVoid(pAttrib, iDest),
			stucAttribAsVoid(pAttrib, iSrc),
			stucGetAttribSizeIntern(pAttrib->type)
		);
	}
}

static inline
bool stucCmpAttribs(AttribArray *pDest, I32 iDest, AttribArray *pSrc, I32 iSrc) {
	for (I32 i = 0; i < pSrc->count; ++i) {
		AttribCore *pDestAttrib = &pDest->pArr[pDest->count + i].core;
		AttribCore *pSrcAttrib = &pSrc->pArr[i].core;
		if (!memcmp(
			stucAttribAsVoid(pDestAttrib, iDest),
			stucAttribAsVoid(pSrcAttrib, iSrc),
			stucGetAttribSizeIntern(pSrcAttrib->type)
		)) {
			return false;
		}
	}
	return true;
}

static inline
void stucCopyAllAttribs(
	AttribArray *pDest,
	I32 iDest,
	const AttribArray *pSrc,
	I32 iSrc,
	bool idxAligned
) {
	for (I32 i = 0; i < pDest->count; ++i) {
		const Attrib *pSrcAttrib;
		if (idxAligned) {
			pSrcAttrib = pSrc->pArr + i;
		}
		else {
			pSrcAttrib = stucGetAttribInternConst(
				pDest->pArr[i].core.name,
				pSrc,
				false,
				NULL, NULL, NULL
			);
		}
		if (pSrcAttrib) {
			stucCopyAttrib(pDest->pArr + i, iDest, pSrcAttrib, iSrc);
		}
	}
}

static inline
void stucReallocVertAttribsIfNeeded(StucContext pCtx, StucMesh *pMesh, I32 *pVertSize) {
	PIX_ERR_ASSERT("", pMesh->vertCount <= *pVertSize);
	if (pMesh->vertCount == *pVertSize) {
		*pVertSize *= 2;
		for (I32 i = 0; i < pMesh->vertAttribs.size; ++i) {
			AttribCore *pAttrib = &pMesh->vertAttribs.pArr[i].core;
			if (pAttrib->pData) {
				stucReallocAttrib(&pCtx->alloc, NULL, pAttrib, *pVertSize);
			}
		}
	}
}
