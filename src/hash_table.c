/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <hash_table.h>
#include <utils.h>

static
I32 getNearbyPrime(I32 num) {
	I32 primes[] = {
		1,
		3,
		5,
		11,
		17,
		37,
		67,
		131,
		257,
		521,
		1031,
		2053,
		4099,
		8209,
		16411,
		32771,
		65537,
		131101,
		262147,
		524309,
		1048583,
		2097169,
		4194319,
		8388617,
		16777259,
		33554467,
		67108879,
		134217757,
		268435459
	};
	F32 exp = log2f((F32)num);
	I32 expRound = roundf(exp);
	PIX_ERR_ASSERT("a value this high shouldn't've been passed", expRound <= 28);
	return primes[expRound];
}

void stucHTableInit(
	const StucAlloc *pAlloc,
	HTable *pHandle,
	I32 targetSize,
	I32Arr allocTypeSizes,
	void *pUserData
) {
	PIX_ERR_ASSERT("", targetSize > 0);
	I32 size = getNearbyPrime(targetSize);
	*pHandle = (HTable){
		.pAlloc = pAlloc,
		.pUserData = pUserData,
		.size = size,
		.pTable = pAlloc->fpCalloc(size, sizeof(HTableBucket))
	};
	PIX_ERR_ASSERT(
		"",
		allocTypeSizes.count > 0 && allocTypeSizes.count <= STUC_HTABLE_ALLOC_HANDLES_MAX
	);
	I32 allocInitSize = size / allocTypeSizes.count / 2 + 1;
	for (I32 i = 0; i < allocTypeSizes.count; ++i) {
		pixalcLinAllocInit(
			pAlloc,
			pHandle->allocHandles + i,
			allocTypeSizes.pArr[i],
			allocInitSize,
			true
		);
	}
}

void stucHTableDestroy(HTable *pHandle) {
	if (pHandle->pTable) {
		PIX_ERR_ASSERT("", pHandle->size);
		pHandle->pAlloc->fpFree(pHandle->pTable);
	}
	PIX_ERR_ASSERT(
		"at least 1 lin alloc handle should have been initialized",
		pHandle->allocHandles[0].valid
	);
	for (I32 i = 0; i < STUC_HTABLE_ALLOC_HANDLES_MAX; ++i) {
		if (pHandle->allocHandles[i].valid) {
			pixalcLinAllocDestroy(pHandle->allocHandles + i);
		}
	}
	*pHandle = (HTable) {0};
}

PixalcLinAlloc *stucHTableAllocGet(HTable *pHandle, I32 idx) {
	PIX_ERR_ASSERT("", idx >= 0 && idx < STUC_HTABLE_ALLOC_HANDLES_MAX);
	return pHandle->allocHandles + idx;
}

const PixalcLinAlloc *stucHTableAllocGetConst(const HTable *pHandle, I32 idx) {
	PIX_ERR_ASSERT("", idx >= 0 && idx < STUC_HTABLE_ALLOC_HANDLES_MAX);
	return pHandle->allocHandles + idx;
}

U64 stucKeyFromI32(const void *pKeyData) {
	return *(I32 *)pKeyData;
}

HTableBucket *stucHTableBucketGet(HTable *pHandle, U64 key) {
	U64 hash = stucFnvHash((U8 *)&key, sizeof(key), pHandle->size);
	return pHandle->pTable + hash;
}
