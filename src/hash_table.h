/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <pixenals_alloc_utils.h>

#include <types.h>

#ifndef STUC_FORCE_INLINE
#ifdef WIN32
#define STUC_FORCE_INLINE __forceinline
#else
#define STUC_FORCE_INLINE __attribute__((always_inline)) static inline
#endif
#endif

typedef struct HTableEntryCore {
	struct HTableEntryCore *pNext;
} HTableEntryCore;

typedef struct HTableBucket {
	HTableEntryCore *pList;
} HTableBucket;

#define STUC_HTABLE_ALLOC_HANDLES_MAX 2

typedef struct HTable {
	const PixalcFPtrs *pAlloc;
	PixalcLinAlloc allocHandles[STUC_HTABLE_ALLOC_HANDLES_MAX];
	HTableBucket *pTable;
	void *pUserData;
	I32 size;
} HTable;

typedef struct StucKey {
	const void *pKey;
	I32 size;
} StucKey;

typedef enum SearchResult {
	STUC_SEARCH_FOUND,
	STUC_SEARCH_NOT_FOUND,
	STUC_SEARCH_ADDED
} SearchResult;

void stucHTableInit(
	const PixalcFPtrs *pAlloc,
	HTable *pHandle,
	I32 targetSize,
	I32Arr allocTypeSizes,
	void *pUserData
);
void stucHTableDestroy(HTable *pHandle);
PixalcLinAlloc *stucHTableAllocGet(HTable *pHandle, I32 idx);
const PixalcLinAlloc *stucHTableAllocGetConst(const HTable *pHandle, I32 idx);
HTableBucket *stucHTableBucketGet(HTable *pHandle, StucKey key);
STUC_FORCE_INLINE
SearchResult stucHTableGet(
	HTable *pHandle,
	I32 alloc,
	const void *pKeyData,
	void **ppEntry,
	bool addEntry,
	void *pInitInfo,
	StucKey (* fpMakeKey)(const void *),
	bool (* fpAddPredicate)(const void *, const void *, const void *),
	void (* fpInitEntry)(void *, HTableEntryCore *, const void *, void *, I32),
	bool (* fpCompareEntry)(const HTableEntryCore *, const void *, const void *)
) {
	PIX_ERR_ASSERT("", pHandle->pTable && pHandle->size);
	PIX_ERR_ASSERT(
		"",
		alloc < STUC_HTABLE_ALLOC_HANDLES_MAX && pHandle->allocHandles[alloc].valid
	);
	PIX_ERR_ASSERT("", (!addEntry || fpInitEntry) && fpCompareEntry);
	HTableBucket *pBucket = stucHTableBucketGet(pHandle, fpMakeKey(pKeyData));
	if (!pBucket->pList) {
		if (!addEntry ||
			fpAddPredicate && !fpAddPredicate(pHandle->pUserData, pKeyData, pInitInfo)
		) {
			return STUC_SEARCH_NOT_FOUND;
		}
		I32 linIdx =
			pixalcLinAlloc(pHandle->allocHandles + alloc, (void **)&pBucket->pList, 1);
		fpInitEntry(pHandle->pUserData, pBucket->pList, pKeyData, pInitInfo, linIdx);
		if (ppEntry) {
			*ppEntry = pBucket->pList;
		}
		return STUC_SEARCH_ADDED;
	}
	HTableEntryCore *pEntry = pBucket->pList;
	do {
		if (fpCompareEntry(pEntry, pKeyData, pInitInfo)) {
			if (ppEntry) {
				*ppEntry = pEntry;
			}
			return STUC_SEARCH_FOUND;
		}
		if (!pEntry->pNext) {
			if (!addEntry ||
				fpAddPredicate && !fpAddPredicate(pHandle->pUserData, pKeyData, pInitInfo)
			) {
				return STUC_SEARCH_NOT_FOUND;
			}
			I32 linIdx =
				pixalcLinAlloc(pHandle->allocHandles + alloc, (void **)&pEntry->pNext, 1);
			fpInitEntry(pHandle->pUserData, pEntry->pNext, pKeyData, pInitInfo, linIdx);
			if (ppEntry) {
				*ppEntry = pEntry->pNext;
			}
			return STUC_SEARCH_ADDED;
		}
		pEntry = pEntry->pNext;
	} while(true);
}

STUC_FORCE_INLINE
SearchResult stucHTableGetConst(
	HTable *pHandle,
	I32 alloc,
	const void *pKeyData,
	void **ppEntry,
	bool addEntry,
	const void *pInitInfo,
	StucKey (* fpMakeKey)(const void *),
	bool (* fpAddPredicate)(const void *, const void *, const void *),
	void (* fpInitEntry)(void *, HTableEntryCore *, const void *, void *, I32),
	bool (* fpCompareEntry)(const HTableEntryCore *, const void *, const void *)
) {
	return stucHTableGet(
		pHandle,
		alloc,
		pKeyData,
		ppEntry,
		addEntry,
		(void *)pInitInfo,
		fpMakeKey,
		fpAddPredicate,
		fpInitEntry,
		fpCompareEntry
	);
}

static inline
bool stucHTableCmpFalse(
	const HTableEntryCore *pEntry,
	const void *pKeyData,
	const void *pInitInfo
) {
	return false;
}

static inline
StucKey stucKeyFromI32(const void *pKeyData) {
	return (StucKey){.pKey = pKeyData, .size = sizeof(I32)};
}

static inline
StucKey stucKeyFromI64(const void *pKeyData) {
	return (StucKey){.pKey = pKeyData, .size = sizeof(I64)};
}

static inline
StucKey stucKeyFromPath(const void *pKeyData) {
	I32 len = strnlen(pKeyData, pixioPathMaxGet());
	return (StucKey){.pKey = pKeyData, .size = len};
}
