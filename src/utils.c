/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <float.h>
#include <math.h>
#include <assert.h>
#include <string.h>

#include <mikktspace.h>

#include <utils.h>
#include <pixenals_math_utils.h>
#include <attrib_utils.h>
#include <pixenals_error_utils.h>
#include <pixenals_thread_utils.h>

void stucGetInFaceBounds(PixmshV2Bb *pBb, const V2_F32 *pUvs, FaceRange face) {
	PIX_ERR_ASSERT("", pBb && pUvs);
	PIX_ERR_ASSERT("", face.range.size >= 3 && face.range.start >= 0 && face.idx >= 0);
	pBb->min.d[0] = pBb->min.d[1] = FLT_MAX;
	pBb->max.d[0] = pBb->max.d[1] = -FLT_MAX;
	for (I32 i = 0; i < face.range.size; ++i) {
		const V2_F32 uv = pUvs[face.range.start + i];
		PIX_ERR_ASSERT("", pixmV2F32IsFinite(uv));
		if (uv.d[0] < pBb->min.d[0]) {
			pBb->min.d[0] = uv.d[0];
		}
		if (uv.d[1] < pBb->min.d[1]) {
			pBb->min.d[1] = uv.d[1];
		}
		if (uv.d[0] > pBb->max.d[0]) {
			pBb->max.d[0] = uv.d[0];
		}
		if (uv.d[1] > pBb->max.d[1]) {
			pBb->max.d[1] = uv.d[1];
		}
	}
	PIX_ERR_ASSERT("", _(pBb->max V2GREATEQL pBb->min));
}

bool stucCheckIfVertIsPreserve(const Mesh *pMesh, I32 vert) {
	PIX_ERR_ASSERT("", pMesh && vert >= 0);
	bool preserveVert = pMesh->pVertPreserve ? pMesh->pVertPreserve[vert] : false;
	PIX_ERR_ASSERT("", pMesh->pNumAdjPreserve);
	I32 numAdjSeam = pMesh->pNumAdjPreserve[vert] & 0xf;
	I32 numAdjPreserve = pMesh->pNumAdjPreserve[vert] >> 4 & 0xf;
	PIX_ERR_ASSERT("", numAdjSeam <= 3 && numAdjPreserve <= 3);
	return
		preserveVert ||
		//if a vert is adj to both a seam & a preserve edge, we keep it.
		// this avoids split edges in the final mesh.
		//note that an edge is only preserve if not a seam.
		numAdjSeam && numAdjPreserve ||
		//verts with 1, 3 or more (not 2) adj preserve edges are also kept,
		//(they're junction points, removing them would cause holes)
		numAdjPreserve == 1 || numAdjPreserve == 3;
}

bool stucCheckIfEdgeIsReceive(const Mesh *pMesh, I32 edge, F32 receiveLen) {
	PIX_ERR_ASSERT("", pMesh && edge >= 0);
	if (pMesh->pEdgeReceive) {
		PIX_ERR_ASSERT("", pMesh->pEdgeReceive[edge] % 2 == pMesh->pEdgeReceive[edge]);
	}
	if (receiveLen >= .0f) {
		PIX_ERR_ASSERT("", pMesh->pEdgeLen);
		return pMesh->pEdgeLen[edge] >= receiveLen;
	}
	else if (pMesh->pEdgeReceive) {
		return pMesh->pEdgeReceive[edge];
	}
	return true;
}

typedef struct AdjEntry {
	I32 face;
	I32 corner;
} AdjEntry;

typedef struct AdjBucket {
	AdjEntry* pArr;
	I32 count;
	I32 size;
} AdjBucket;

static
void adjTableDestroyBuckets(const StucAlloc *pAlloc, I32 count, AdjBucket *pAdjTable) {
	for (I32 i = 0; i < count; ++i) {
		if (pAdjTable[i].pArr) {
			pAlloc->fpFree(pAdjTable[i].pArr);
		}
	}
}

static
StucErr buildCornerAdjTable(
	const StucAlloc *pAlloc,
	const StucMesh *pMesh,
	AdjBucket *pAdjTable
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("", pAdjTable);
	for (I32 i = 0; i < pMesh->faceCount; ++i) {
		FaceRange face = stucGetFaceRange(pMesh, i);
		for (I32 j = 0; j < face.range.size; ++j) {
			AdjBucket* pBucket = pAdjTable + pMesh->pCorners[face.range.start + j];
			PIX_ERR_ASSERT("", pBucket->count <= pBucket->size);
			if (!pBucket->pArr) {
				pBucket->size = 2;
				pBucket->pArr =
					pAlloc->fpMalloc(sizeof(AdjEntry) * pBucket->size);
			}
			else if (pBucket->count == pBucket->size) {
				PIX_ERR_ASSERT("tried to realloc null arr", pBucket->pArr);
				pBucket->size *= 2;
				pBucket->pArr = pAlloc->fpRealloc(
					pBucket->pArr,
					sizeof(AdjEntry) * pBucket->size
				);
			}
			pBucket->pArr[pBucket->count].face = i;
			pBucket->pArr[pBucket->count].corner = j;
			pBucket->count++;
		}
	}
	PIX_ERR_CATCH(0, err,
		adjTableDestroyBuckets(pAlloc, pMesh->vertCount, pAdjTable);
	;);
	return err;
}

static
StucErr findEdgesForFace(StucMesh *pMesh, AdjBucket* pAdjTable, I32 idx) {
	StucErr err = PIX_ERR_SUCCESS;
	FaceRange face = stucGetFaceRange(pMesh, idx);
	for (I32 j = 0; j < face.range.size; ++j) {
		if (pMesh->pEdges[face.range.start + j] >= 0) {
			continue; //Already set
		}
		I32 edge = pMesh->edgeCount;
		pMesh->edgeCount++;
		AdjBucket* pBucket = pAdjTable + pMesh->pCorners[face.range.start + j];
		PIX_ERR_ASSERT("", pBucket->count > 0 && pBucket->size >= pBucket->count);
		for (I32 k = 0; k < pBucket->count; ++k) {
			AdjEntry* pEntry = pBucket->pArr + k;
			if (pEntry->face == idx) {
				PIX_ERR_RETURN_IFNOT_COND(
					err,
					pEntry->corner == j,
					"Invalid mesh, 2 corners in this face share 1 vert"
				);
				continue;
			}
			FaceRange otherFace = stucGetFaceRange(pMesh, pEntry->face);
			I32 nextCorner = (j + 1) % face.range.size;
			I32 otherPrevCorner = pEntry->corner ?
				pEntry->corner - 1 : otherFace.range.size - 1;
			if (pMesh->pEdges[otherFace.range.start + otherPrevCorner] >= 0) {
				continue; //Already set
			}
			if (pMesh->pCorners[face.range.start + nextCorner] !=
				pMesh->pCorners[otherFace.range.start + otherPrevCorner]) {
				continue; //Not connected
			}
			pMesh->pEdges[otherFace.range.start + otherPrevCorner] = edge;
			break;
		}
		pMesh->pEdges[face.range.start + j] = edge;
	}
	return err;
}

static
StucErr findEdges(StucMesh* pMesh, AdjBucket* pAdjTable) {
	StucErr err = PIX_ERR_SUCCESS;
	for (I32 i = 0; i < pMesh->faceCount; ++i) {
		err = findEdgesForFace(pMesh, pAdjTable, i);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	return err;
}

StucErr stucBuildEdgeList(StucCtx *pCtx, StucMesh *pMesh) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_RETURN_IFNOT_COND(err, !pMesh->pEdges, "");
	const StucAlloc *pAlloc = &pCtx->alloc;
	PIX_ERR_ASSERT("", pMesh->vertCount);
	AdjBucket* pAdjTable =
		pAlloc->fpCalloc(pMesh->vertCount, sizeof(AdjBucket));
	err = buildCornerAdjTable(pAlloc, pMesh, pAdjTable);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	{
		PIX_ERR_ASSERT("", pMesh->cornerCount);
		I32 dataSize = sizeof(I32) * pMesh->cornerCount;
		pMesh->pEdges = pAlloc->fpMalloc(dataSize);
		memset(pMesh->pEdges, -1, dataSize);
		err = findEdges(pMesh, pAdjTable);
		PIX_ERR_THROW_IFNOT(err, "'findEdges' returned error", 1);
		PIX_ERR_CATCH(1, err,
			pAlloc->fpFree(pMesh->pEdges);
		);
	}
	PIX_ERR_CATCH(0, err, ;);
	adjTableDestroyBuckets(pAlloc, pMesh->vertCount, pAdjTable);
	pAlloc->fpFree(pAdjTable);
	return err;
}

void stucProgressBarClear() {
	printf("\n");
	printf("\x1b[1F");
	printf("\x1b[2K");
}

void stucProgressBarPrint(StucCtx *pCtx, I32 progress) {
	printf("	");
	for (I32 i = 0u; i < pCtx->stageReport.outOf; ++i) {
		char character;
		if (i < progress) {
			character = '#';
		}
		else {
			character = '-';
		}
		printf("%c", character);
	}
}

void stucStageBegin(void *pCtx, StucStageReport *pReport, const char* pName) {
	return;
	stucSetStageName(pCtx, pName);
}
void stucStageProgress(void *pCtx, StucStageReport *pReport, I32 progress) {
	return;
	if (progress) {
		stucProgressBarClear();
	}
	printf("%s", pReport->stage);
	stucProgressBarPrint(pCtx, progress);
}
void stucStageEnd(void *pCtx, StucStageReport *pReport) {
	return;
	memset(pReport->stage, 0, STUC_STAGE_NAME_LEN);
	stucProgressBarClear();
}

void stucStageBeginWrap(StucCtx *pCtx, const char* pName, I32 max) {
	pCtx->stageReport.fpBegin(pCtx, &pCtx->stageReport, pName);
	//Only needed if using default stage report functions,
	//it's just used for the progress bar
	pCtx->stageInterval = max <= pCtx->stageReport.outOf ?
		1 : max / pCtx->stageReport.outOf;
}

void stucStageProgressWrap(StucCtx *pCtx, I32 progress) {
	if (pCtx->stageInterval != 1 && progress % pCtx->stageInterval) {
		return;
	}
	//Normalize progress within stageReport.outOf
	I32 normProgress = progress / pCtx->stageInterval;
	pCtx->stageReport.fpProgress(pCtx, &pCtx->stageReport, normProgress);
}

void stucStageEndWrap(StucCtx *pCtx) {
	pCtx->stageReport.fpEnd(pCtx, &pCtx->stageReport);
}

void stucSetStageName(StucCtx *pCtx, const char* pName) {
	strncpy(pCtx->stageReport.stage, pName, STUC_STAGE_NAME_LEN);
}

F32 stucGetT(V2_F32 point, V2_F32 lineA, V2_F32 lineUnit, F32 lineLen) {
	V2_F32 dir = _(point V2SUB lineA);
	return _(dir V2DOT lineUnit) / lineLen;
}

//does not bounds check
I32 stucIdxBitArray(UBitField8 *pArr, I32 idx, I32 len) {
	idx *= len;
	I32 byte = idx / 8;
	I32 bit = idx % 8;
	I32 mask = (0x1 << len) - 1;
	if (bit + len > 8) {
		//bit spans byte boundary
		return *(UBitField16 *)&pArr[byte] >> bit & mask;
	}
	else {
		return pArr[byte] >> bit & mask;
	}
}

//does not bounds check.
//Also, if value is 0, only 1 bit will be set, len is ignored
void stucSetBitArr(UBitField8 *pArr, I32 idx, I32 value, I32 len) {
	PIX_ERR_ASSERT("", (value & (0x1 << len) - 1) == value);
	idx *= len;
	I32 byte = idx / 8;
	I32 bit = idx % 8;
	if (value) {
		if (bit + len > 8) {
			//cast to 16 bit as value spans across byte boundary
			*(UBitField16 *)&pArr[byte] |= value << bit;
		}
		else {
			pArr[byte] |= value << bit;
		}
	}
	else {
		UBitField8 mask = -0x1 ^ (0x1 << bit);
		pArr[byte] &= mask;
	}
}

M3x3 stucGetInterpolatedTbn(
	const Mesh *pMesh,
	const FaceRange *pFace,
	const I8 *pTriCorners,
	V3_F32 bc
) {
	//TODO replace interpolation in this func with the attrib
	//     interpolation funcions or macros
	V3_F32 *pNormals = pMesh->pNormals;
	V3_F32 normal = _(pNormals[pFace->range.start + pTriCorners[0]] V3MULS bc.d[0]);
	_(&normal V3ADDEQL _(pNormals[pFace->range.start + pTriCorners[1]] V3MULS bc.d[1]));
	_(&normal V3ADDEQL _(pNormals[pFace->range.start + pTriCorners[2]] V3MULS bc.d[2]));
	_(&normal V3DIVEQLS bc.d[0] + bc.d[1] + bc.d[2]);
	V3_F32 *pTangents = pMesh->pTangents;
	V3_F32 tangent = _(pTangents[pFace->range.start + pTriCorners[0]] V3MULS bc.d[0]);
	_(&tangent V3ADDEQL _(pTangents[pFace->range.start + pTriCorners[1]] V3MULS bc.d[1]));
	_(&tangent V3ADDEQL _(pTangents[pFace->range.start + pTriCorners[2]] V3MULS bc.d[2]));
	_(&tangent V3DIVEQLS bc.d[0] + bc.d[1] + bc.d[2]);
	//TODO should this be interpolated? Or are such edge cases invalid?
	F32 tSign = pMesh->pTSigns[pFace->range.start + pTriCorners[0]];
	V3_F32 bitangent = _(_(normal V3CROSS tangent) V3MULS tSign);
	M3x3 tbn = {0};
	*(V3_F32 *)&tbn.d[0] = tangent;
	*(V3_F32 *)&tbn.d[1] = bitangent;
	*(V3_F32 *)&tbn.d[2] = normal;
	return tbn;
}

static
StucErr jobEntry(void *pArgs, I32 threadId) {
	JobArgs *pCore = pArgs;
	pCore->threadId = threadId;
	return pCore->fpJob(pArgs);
}

static
StucErr sendOffJobs(
	StucCtx *pCtx,
	I32 threadId,
	I32 jobCount,
	void *pJobArgs, I32 argStructSize,
	StucErr (* func)(void *, I32),
	PixthJob *pJobHandles
) {
	StucErr err = PIX_ERR_SUCCESS;
	void *jobArgPtrs[PIXTH_MAX_THREADS] = {0};
	for (I32 i = 0; i < jobCount; ++i) {
		jobArgPtrs[i] = (U8 *)pJobArgs + i * argStructSize;
	}
	pixthJobsInit(pJobHandles, jobCount, func, jobArgPtrs);
	err = pCtx->threadPool.pJobStackPushJobs(
		&pCtx->threadPool.handle,
		threadId,
		jobCount,
		pJobHandles
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	return err;
}

StucErr stucDoJobInParallel(
	StucCtx *pCtx,
	I32 threadId,
	I32 jobCount, void *pJobArgs, I32 argStructSize,
	StucErr (* func)(void *)
) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("", jobCount >= 0);
	if (!jobCount) {
		return err;
	}
	for (I32 i = 0; i < jobCount; ++i) {
		((JobArgs *)((U8 *)pJobArgs + argStructSize * i))->fpJob = func;
	}
	PixthJob jobHandles[PIXTH_MAX_THREADS] = {0};
	err = sendOffJobs(
		pCtx,
		threadId,
		jobCount,
		pJobArgs,
		argStructSize,
		jobEntry,
		jobHandles
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pCtx->threadPool.fpWaitForJobs(
		&pCtx->threadPool.handle,
		jobCount,
		jobHandles,
		threadId,
		true,
		NULL
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = stucJobGetErrs(pCtx, jobCount, jobHandles);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err, ;);
	return err;
}

InsideStatus stucIsPointInHalfPlane(
	V2_F32 point,
	V2_F32 lineA,
	V2_F32 halfPlane,
	bool wind
) {
	V2_F32 dir = _(point V2SUB lineA);
	F32 dot = _(halfPlane V2DOT dir);
	if (dot == .0f) {
		return STUC_INSIDE_STATUS_ON_LINE;
	}
	else {
		return (dot > .0f) ^ wind ? STUC_INSIDE_STATUS_INSIDE : STUC_INSIDE_STATUS_OUTSIDE;
	}
}

StucErr stucThreadPoolSetCustom(
	StucCtx *pCtx,
	const StucThreadPool *pThreadPool
) {
	if (!pThreadPool->fpInit ||
		!pThreadPool->fpWaitForJobs ||
		!pThreadPool->fpGetJobErr ||
		!pThreadPool->fpDestroy ||
		!pThreadPool->pJobStackPushJobs
	) {
		//TODO remove remaining uses of print for error
		//swap with PIX_ERROR_* macros
		printf("Failed to set custom thread pool. One or more functions were NULL");
		return PIX_ERR_ERROR;
	}
	pCtx->threadPool = *pThreadPool;
	return PIX_ERR_SUCCESS;
}

void stucThreadPoolSetDefault(StucCtx *pCtx) {
	pCtx->threadPool.fpInit = pixthThreadPoolInit;
	pCtx->threadPool.fpWaitForJobs = pixthWaitForJobs;
	pCtx->threadPool.fpGetJobErr = pixthGetJobErr;
	pCtx->threadPool.fpLogDump = pixthThreadPoolLogDump;
	pCtx->threadPool.fpDestroy = pixthThreadPoolDestroy;
	pCtx->threadPool.pJobStackPushJobs = pixthJobStackPushJobs;
}

void stucAllocSetCustom(PixalcFPtrs *pAlloc, PixalcFPtrs *pCustomAlloc) {
	PIX_ERR_ASSERT("", pAlloc && pCustomAlloc);
	if (!pCustomAlloc->fpMalloc || !pCustomAlloc->fpCalloc || !pCustomAlloc->fpFree) {
		printf("Failed to set custom alloc. One or more functions were NULL");
		return;
	}
	*pAlloc = *pCustomAlloc;
}

void stucAllocSetDefault(PixalcFPtrs *pAlloc) {
	PIX_ERR_ASSERT("", pAlloc);
	pAlloc->fpMalloc = malloc;
	pAlloc->fpCalloc = calloc;
	pAlloc->fpFree = free;
	pAlloc->fpRealloc = realloc;
}

static
I32 isEdgeSeam(const Mesh *pMesh, I32 edge) {
	PIX_ERR_ASSERT("", pMesh && pMesh->pEdgeFaces && pMesh->pEdgeCorners);
	V2_I32 faces = pMesh->pEdgeFaces[edge];
	if (faces.d[1] == -1) {
		return true;
	}
	FaceRange faceA = stucGetFaceRange(&pMesh->core, faces.d[0]);
	bool windA = stucCalcFaceWindFromUvs(faceA.range, pMesh);
	FaceRange faceB = stucGetFaceRange(&pMesh->core, faces.d[1]);
	bool windB = stucCalcFaceWindFromUvs(faceB.range, pMesh);
	if (windA != windB) {
		return true; //marking wind borders as seam for now
	}
	V2_I8 corners = pMesh->pEdgeCorners[edge];
	I32 aA = corners.d[0];
	I32 bA = corners.d[1];
	I32 bB = stucGetCornerNext(bA, &faceB);
	V2_F32 uvAA = pMesh->pUvs[faceA.range.start + aA];
	V2_F32 uvBB = pMesh->pUvs[faceB.range.start + bB];
	if (!_(uvAA V2EQL uvBB)) {
		return true;
	}
	I32 aB = stucGetCornerNext(aA, &faceA);
	V2_F32 uvAB = pMesh->pUvs[faceA.range.start + aB];
	V2_F32 uvBA = pMesh->pUvs[faceB.range.start + bA];
	return !_(uvAB V2EQL uvBA);
	V2_F32 halfPlane = pixmV2F32LineNormal(_(uvAB V2SUB uvAA));
	V2_F32 uvAC = pMesh->pUvs[faceA.range.start + stucGetCornerNext(aB, &faceA)];
	V2_F32 uvBC = pMesh->pUvs[faceB.range.start + stucGetCornerNext(bB, &faceB)];
	bool refSign = _(_(uvAC V2SUB uvAA) V2DOT halfPlane) > 0;
	if (_(_(uvBC V2SUB uvAA) V2DOT halfPlane) > 0 != refSign) {
		return true;
	}
	if (faceA.range.size == 4) {
		V2_F32 uvAD = pMesh->pUvs[faceA.range.start + stucGetCornerPrev(aA, &faceA)];
		if (_(_(uvAD V2SUB uvAA) V2DOT halfPlane) > 0 != refSign) {
			return true;
		}
	}
	if (faceB.range.size == 4) {
		V2_F32 uvBD = pMesh->pUvs[faceB.range.start + stucGetCornerPrev(bA, &faceB)];
		if (_(_(uvBD V2SUB uvAA) V2DOT halfPlane) > 0 != refSign) {
			return true;
		}
	}
	return false;
}

void stucBuildEdgeLenList(StucCtx *pCtx, Mesh *pMesh) {
	PIX_ERR_ASSERT("", pMesh->pEdgeLen);
	V3_F32 *pPosCache = pCtx->alloc.fpMalloc(pMesh->core.edgeCount * sizeof(V3_F32));
	I8 *pSet = pCtx->alloc.fpCalloc(pMesh->core.edgeCount, 1);
	for (I32 i = 0; i < pMesh->core.cornerCount; ++i) {
		V3_F32 pos = pMesh->pPos[pMesh->core.pCorners[i]];
		I32 edge = pMesh->core.pEdges[i];
		if (!pSet[edge]) {
			pPosCache[edge] = pos;
			pSet[edge] = 1;
			continue;
		}
		//this occurs usually due to interior faces,
		// it shouldn't be an issue for for map-meshes, more so for in-meshes.
		//TODO remove this assert if no issues arise
		//PIX_ERR_ASSERT("more than 2 corners refernce 1 edge", pSet[edge] < 2);
		V3_F32 diff = _(pos V3SUB pPosCache[edge]);
		pMesh->pEdgeLen[edge] = pixmV3F32Len(diff);
		pSet[edge]++;
	}
	pCtx->alloc.fpFree(pSet);
	pCtx->alloc.fpFree(pPosCache);
}

static
void addTri(
	StucMesh *pBufMesh,
	const StucMesh *pMesh,
	const FaceRange *pFace,
	const U8 *pTri
) {
	for (I32 i = 0; i < 3; ++i) {
		I32 vert = pMesh->pCorners[pFace->range.start + pTri[i]];
		pBufMesh->pCorners[pBufMesh->cornerCount + i] = vert;
		stucCopyAllAttribs(
			&pBufMesh->cornerAttribs, pBufMesh->cornerCount + i,
			&pMesh->cornerAttribs, pFace->range.start + pTri[i],
			true
		);
	}
	stucCopyAllAttribs(
		&pBufMesh->faceAttribs, pBufMesh->cornerCount / 3,
		&pMesh->faceAttribs, pFace->idx,
		true
	);
	pBufMesh->cornerCount += 3;
	++pBufMesh->faceCount;
}

StucErr stucMeshTriangulate(StucCtx *pCtx, StucMesh *pMesh) {
	StucErr err = PIX_ERR_SUCCESS;
	PIX_ERR_ASSERT("", pMesh->pFaces && pMesh->pCorners);

	Mesh wrap = {0};
	err = stucAttemptToSetMissingActiveDomains(pMesh);
	PIX_ERR_RETURN_IFNOT(err, "");
	wrap.core = *pMesh;
	err = stucAssignActiveAliases(
		pCtx,
		&wrap,
		0xffffffff,
		STUC_DOMAIN_NONE
	);
	PIX_ERR_RETURN_IFNOT(err, "");

	I32 triCount = pMesh->cornerCount - pMesh->faceCount * 2;
	StucMesh bufMesh = {.faceCount = triCount, .cornerCount = triCount * 3};
	bufMesh.pCorners = pCtx->alloc.fpMalloc(sizeof(I32) * bufMesh.cornerCount);
	StucDomain domain = STUC_DOMAIN_FACE;
	err = stucAllocAttribs(
		pCtx,
		domain,
		triCount,
		&bufMesh,
		1,
		(const StucMesh *const *)&pMesh,
		0,
		false, true, false, false
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	domain = STUC_DOMAIN_CORNER;
	err = stucAllocAttribs(
		pCtx,
		domain,
		triCount * 3,
		&bufMesh,
		1,
		(const StucMesh *const *)&pMesh,
		0,
		false, true, false, false
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);

	bufMesh.faceCount = 0;
	bufMesh.cornerCount = 0;
	U8 triBuf[PIXMSH_NGON_MAX_SIZE];
	for (I32 i = 0; i < pMesh->faceCount; ++i) {
		FaceRange face = stucGetFaceRange(pMesh, i);
		if (face.range.size == 3) {
			addTri(&bufMesh, pMesh, &face, (U8[]){0, 1, 2});
		}
		else if (face.range.size == 4) {
			addTri(&bufMesh, pMesh, &face, (U8[]){0, 1, 2});
			addTri(&bufMesh, pMesh, &face, (U8[]){2, 3, 0});
		}
		else {
			PIX_ERR_ASSERT(
				"invalid face size",
				face.range.size > 4 && face.range.size <= PIXMSH_NGON_MAX_SIZE
			);
			I32 count = stucTriangulateFaceFromVerts(&pCtx->alloc, &face, &wrap, triBuf);
			for (I32 j = 0; j < count; ++j) {
				addTri(&bufMesh, pMesh, &face, triBuf + j * 3);
			}
		}
	}
	/*
	if (pMesh->pEdges) {
		pCtx->alloc.fpFree(pMesh->pEdges);
		pMesh->pEdges = NULL;
	}
	if (pMesh->edgeAttribs.pArr) {
		pCtx->alloc.fpFree(pMesh->edgeAttribs.pArr);
		pMesh->edgeAttribs = (AttribArray){0};
	}
	*/
	stucAttribArrDestroy(pCtx, &pMesh->faceAttribs);
	stucAttribArrDestroy(pCtx, &pMesh->cornerAttribs);
	pCtx->alloc.fpFree(pMesh->pFaces);
	pCtx->alloc.fpFree(pMesh->pCorners);
	pMesh->pFaces = NULL;
	pMesh->pCorners = bufMesh.pCorners;
	pMesh->faceAttribs = bufMesh.faceAttribs;
	pMesh->cornerAttribs = bufMesh.cornerAttribs;
	pMesh->faceCount = bufMesh.faceCount;
	pMesh->cornerCount = bufMesh.cornerCount;

	PIX_ERR_CATCH(0, err,
		if (bufMesh.pCorners) {
			pCtx->alloc.fpFree(bufMesh.pCorners);
		}
		stucAttribArrDestroy(pCtx, &bufMesh.faceAttribs);
		stucAttribArrDestroy(pCtx, &bufMesh.cornerAttribs);
	);
	return err;
}

bool stucCheckIfNoFacesHaveMaskIdx(const Mesh *pMesh, I8 maskIdx) {
	if (!pMesh->pMatIdx) {
		return false;
	}
	for (I32 i = 0; i < pMesh->core.faceCount; ++i) {
		if (pMesh->pMatIdx[i] == maskIdx) {
			return false;
		}
	}
	return true;
}

void stucBuildEdgeAdj(Mesh *pMesh) {
	const StucMesh *pCore = &pMesh->core;
	memset(pMesh->pEdgeFaces, -1, sizeof(V2_I32) * pCore->edgeCount);
	memset(pMesh->pEdgeCorners, -1, sizeof(V2_I8) * pCore->edgeCount);
	for (I32 i = 0; i < pCore->faceCount; ++i) {
		FaceRange face = stucGetFaceRange(&pMesh->core, i);
		for (I32 j = 0; j < face.range.size; ++j) {
			I32 edge = pCore->pEdges[face.range.start + j];
			bool which = pMesh->pEdgeFaces[edge].d[0] >= 0;
			pMesh->pEdgeFaces[edge].d[which] = i;
			pMesh->pEdgeCorners[edge].d[which] = j;
		}
	}
}

static
void incNumAdjSeam(const Mesh *pMesh, I32 vert) {
	I32 numSeam = pMesh->pNumAdjPreserve[vert] & 0xf;
	if (numSeam < 3) {
		numSeam++;
		pMesh->pNumAdjPreserve[vert] &= 0xf0;
		pMesh->pNumAdjPreserve[vert] |= numSeam;
	}
}

static
void incNumAdjPreserve(const Mesh *pMesh, I32 vert) {
	I32 numPreserve = pMesh->pNumAdjPreserve[vert] >> 4 & 0xf;
	if (numPreserve < 3) { //only record up to 3
		numPreserve++;
		pMesh->pNumAdjPreserve[vert] &= 0xf;
		pMesh->pNumAdjPreserve[vert] |= numPreserve << 4;
	}
}

void stucBuildSeamAndPreserveTables(Mesh *pMesh) {
	for (I32 i = 0; i < pMesh->core.edgeCount; ++i) {
		bool seam = isEdgeSeam(pMesh, i);
		bool preserve = stucGetIfPreserveEdge(pMesh, i);
		if (seam || preserve) {
			V2_I32 faces = pMesh->pEdgeFaces[i];
			if (faces.d[0] == -1 && faces.d[1] == -1) {
				continue;//edge has no faces
			}
			V2_I8 corners = pMesh->pEdgeCorners[i];
			I32 vert = stucGetMeshVert(
				&pMesh->core,
				(FaceCorner) {.face = faces.d[0], .corner = corners.d[0]}
			);
			if (seam) {
				pMesh->pSeamEdge[i] = seam;
				incNumAdjSeam(pMesh, vert);
			}
			else if (preserve) {
				incNumAdjPreserve(pMesh, vert);
			}
			if (faces.d[1] >= 0) {
				vert = stucGetMeshVert(
					&pMesh->core,
					(FaceCorner) {
					.face = faces.d[1], .corner = corners.d[1]
				}
				);
				if (seam) {
					incNumAdjSeam(pMesh, vert);
				}
				else if (preserve) {
					incNumAdjPreserve(pMesh, vert);
				}
			}
		}
	}
}

//

static
I32 getEdge(const void *pMeshRaw, FaceCorner corner) {
	return stucGetMeshEdge(pMeshRaw, corner);
}

static
PixmshEdgeCorners getEdgeCorners(const void *pMeshRaw, I32 edge) {
	const Mesh *pMesh = pMeshRaw;
	PIX_ERR_ASSERT("", edge >= 0 && edge < pMesh->core.edgeCount);
	return (PixmshEdgeCorners){.corners = {
		{.face = pMesh->pEdgeFaces[edge].d[0], .corner = pMesh->pEdgeCorners[edge].d[0]},
		{.face = pMesh->pEdgeFaces[edge].d[1], .corner = pMesh->pEdgeCorners[edge].d[1]}
	}};
}

static
bool splitPredicate(const void *pMeshRaw, I32 edge) {
	I32 ret = stucCouldInEdgeIntersectMapFace(pMeshRaw, edge);
	return ret;
}

StucErr stucInIslandFacesInit(
	const PixalcFPtrs *pAlloc,
	void *pIslandsRaw,
	I32 count,
	I32 **ppOut
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslands = pIslandsRaw;
	PIXALC_DYN_ARR_RESIZE(I32, pAlloc, &pIslands->faces, count);
	*ppOut = pIslands->faces.pArr;
	return err;
}

StucErr stucInIslandBorderInit(const PixalcFPtrs *pAlloc, void *pIslandsRaw, I32 island, I32 *pIdx) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslandArr = pIslandsRaw;
	StucInIsland *pIsland = pIslandArr->pArr + island;
	I32 oldSize = pIsland->core.borders.size;
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(Border, pAlloc, &pIsland->core.borders, newIdx);
	if (newIdx >= oldSize) {
		memset(
			pIsland->core.borders.pArr + oldSize,
			0,
			sizeof(Border) * (pIsland->core.borders.size - oldSize)
		);
	}
	else {
		pIsland->core.borders.pArr[newIdx].arr.count = 0;
	}
	*pIdx = newIdx;
	return err;
}

StucErr stucInIslandBorderMarkAsOuter(
	void *pIslandsRaw,
	I32 island,
	I32 border,
	const PixmshV2Bb *pBb
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslandArr = pIslandsRaw;
	pIslandArr->pArr[island].core.borders.outer = border;
	pIslandArr->pArr[island].bb = (ClutreBb){.min = pBb->min, .max = pBb->max};
	return err;
}

StucErr stucInIslandBorderAddEdge(
	const PixalcFPtrs *pAlloc,
	void *pIslandsRaw,
	I32 island,
	I32 adjIsland,
	I32 border,
	FaceCorner corner,
	I32 edge
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslandArr = pIslandsRaw;
	StucInIsland *pIsland = pIslandArr->pArr + island;
	Border *pBorder = pIsland->core.borders.pArr + border;
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(BorderEdge, pAlloc, &pBorder->arr, newIdx);
	pBorder->arr.pArr[newIdx] = (BorderEdge){.corner = corner};
	StucBorderTable *pEntry = NULL;
	SearchResult result = pixuctHTableBasicGet(
		&pIsland->borderTable,
		0,
		&corner,
		&pEntry,
		true,
		NULL,
		stucInIslandBorderMakeKey, stucInIslandBorderCmpEntry
	);
	PIX_ERR_ASSERT("", result == PIX_SEARCH_ADDED);
	*pEntry = (StucBorderTable){
		.core = pEntry->core,
		.corner = corner,
		.border = border,
		.idx = newIdx
	};
	return err;
}

StucErr stucInIslandAdd(
	const PixalcFPtrs *pAlloc,
	void *pIslandsRaw,
	I32 splitTotal,
	I32 *pIdx
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslands = pIslandsRaw;
	I32 newIdx = 0;
	I32 oldSize = pIslands->size;
	PIXALC_DYN_ARR_ADD(StucInIsland, pAlloc, pIslands, newIdx);
	if (oldSize < pIslands->size) {
		memset(
			pIslands->pArr + oldSize,
			0,
			sizeof(StucInIsland) * (pIslands->size - oldSize)
		);
	}
	*pIdx = newIdx;
	StucInIsland *pIsland = pIslands->pArr + newIdx;
	pIsland->core.borders.count = 0;
	if (pIsland->borderTable.pTable) {
		pixuctHTableDestroy(&pIsland->borderTable);
	}
	pixuctHTableInit(
		pAlloc,
		&pIsland->borderTable,
		splitTotal / 2 + 2,
		(PixtyI32Arr){.pArr = (I32[]){sizeof(StucBorderTable)}, .count = 1},
		&pIslands->tableMem, NULL, false
	);
	return err;
}

StucErr stucInIslandRangeSet(void *pIslandsRaw, I32 island, PixtyRange range) {
	StucErr err = PIX_ERR_SUCCESS;
	StucInIslandArr *pIslands = pIslandsRaw;
	pIslands->pArr[island].core.faces = range;
	return err;
}

#ifdef STUC_USE_SUB_ISLANDS
static
StucErr subIslandFacesInit(
	const PixalcFPtrs *pAlloc,
	void *pIslandsRaw,
	I32 count,
	I32 **ppOut
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucSubIslandArr *pIslands = pIslandsRaw;
	pIslands->pFaces = pAlloc->fpMalloc(count * sizeof(I32));
	*ppOut = pIslands->pFaces;
	return err;
}

static
StucErr subBorderInit(const PixalcFPtrs *pAlloc, void *pIslandsRaw, I32 island, I32 *pIdx) {
	StucErr err = PIX_ERR_SUCCESS;
	StucSubIslandArr *pIslandArr = pIslandsRaw;
	StucSubIsland *pIsland = pIslandArr->pArr + island;
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(Border, pAlloc, &pIsland->core.borders, newIdx);
	*pIdx = newIdx;
	return err;
}

static
StucErr subBorderAddEdge(
	const PixalcFPtrs *pAlloc,
	void *pIslandsRaw,
	I32 island,
	I32 adjIsland,
	I32 border,
	FaceCorner corner
) {
	StucErr err = PIX_ERR_SUCCESS;
	StucSubIslandArr *pIslandArr = pIslandsRaw;
	StucSubIsland *pIsland = pIslandArr->pArr + island;
	Border *pBorder = pIsland->core.borders.pArr + border;
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(FaceCorner, pAlloc, &pBorder->arr, newIdx);
	pBorder->arr.pArr[newIdx] = (BorderEdge){.corner = corner, .adjIsland = adjIsland};
	return err;
}

static
StucErr subIslandAdd(const PixalcFPtrs *pAlloc, void *pIslandsRaw, I32 *pIdx) {
	StucErr err = PIX_ERR_SUCCESS;
	StucSubIslandArr *pIslands = pIslandsRaw;
	I32 newIdx = 0;
	PIXALC_DYN_ARR_ADD(StucSubIsland, pAlloc, pIslands, newIdx);
	*pIdx = newIdx;
	return err;
}

static
StucErr subIslandRangeSet(void *pIslandsRaw, I32 island, PixtyRange range) {
	StucErr err = PIX_ERR_SUCCESS;
	StucSubIslandArr *pIslands = pIslandsRaw;
	pIslands->pArr[island].core.faces = range;
	return err;
}

typedef struct SubIslandJobArgs {
	JobArgs core;
	StucInIslandArr *pIslands;
	JobArgsFoot foot;
} SubIslandJobArgs;

typedef struct SubIslandJobShared {
	StucCtx *pCtx;
	const Mesh *pInMesh;
} SubIslandJobShared;

static
I32 subIslandsJobGetRange(const StucCtx *pCtx, const void *pShared, void *pInitInfoVoid) {
	return ((StucInIslandArr *)pInitInfoVoid)->count;
}

static
void subIslandsJobInit(
	const StucCtx *pCtx,
	const void *pShared,
	void *pInitInfoVoid,
	void *pEntryVoid
) {
	((SubIslandJobArgs *)pEntryVoid)->pIslands = ((StucInIslandArr *)pInitInfoVoid);
}

typedef struct SubMesh {
	const Mesh *pMesh;
	const StucInIslandArr *pIslands;
	Range range;
	I32 island;
} SubMesh;

static
bool isFaceInIsland(const StucInIsland *pIsland, I32 face) {
	return face >= pIsland->core.faces.start && face < pIsland->core.faces.end;
}

static
PixmshFaceRange subFaceRange(const void *pMeshRaw, I32 faceRaw) {
	const SubMesh *pMesh = pMeshRaw;
	I32 face = pMesh->pIslands->faces.pArr[pMesh->range.start + faceRaw];
	PIX_ERR_ASSERT("", face >= 0 && face < pMesh->pMesh->core.faceCount);
	I32 start = pMesh->pMesh->core.pFaces[face];
	return (PixmshFaceRange) {
		.start = start,
		.size = pMesh->pMesh->core.pFaces[face + 1] - start 
	};
}

static
I32 subGetEdge(const void *pMeshRaw, FaceCorner corner) {
	const SubMesh *pMesh = pMeshRaw;
	I32 face = pMesh->pIslands->faces.pArr[pMesh->range.start + corner.face];
	return stucGetMeshEdge(
		&pMesh->pMesh->core,
		(FaceCorner){.face = face, .corner = corner.corner}
	);
}

static
PixtyV2_F32 subUv(const void *pMeshRaw, I32 corner) {
	return stucClustUv(((const SubMesh *)pMeshRaw)->pMesh, corner);
}

static
I32 faceToIslandRange(Range range, I32 face) {
	if (face == -1) {
		return face;
	}
	I32 faceOffset = face - range.start;
	PIX_ERR_ASSERT("", faceOffset >= 0);
	return faceOffset;
}

static
PixmshEdgeCorners subGetEdgeCorners(const void *pMeshRaw, I32 edge) {
	const SubMesh *pMesh = pMeshRaw;
	PixmshEdgeCorners corners = getEdgeCorners(pMesh->pMesh, edge);
	PIX_ERR_ASSERT(
		"edge is floating or invalid",
		corners.corners[0].face != corners.corners[1].face
	);
	corners.corners[0].face = corners.corners[0].face == -1 ?
		-1 : pMesh->pIslands->pFaceTable[corners.corners[0].face];
	corners.corners[1].face = corners.corners[1].face == -1 ?
		-1 : pMesh->pIslands->pFaceTable[corners.corners[1].face];
	if (!(corners.corners[0].face == -1) && !(corners.corners[1].face == -1)) {
		bool in[2] = {
			isFaceInIsland(pMesh->pIslands->pArr + pMesh->island, corners.corners[0].face),
			isFaceInIsland(pMesh->pIslands->pArr + pMesh->island, corners.corners[1].face)
		};
		PIX_ERR_ASSERT("edge isn't part of island", in[0] || in[1]);
		if (!in[0] || !in[1]) {
			corners.corners[in[0]] = (FaceCorner){.face = -1, .corner = -1};
		}
	}
	corners.corners[0].face = faceToIslandRange(pMesh->range, corners.corners[0].face);
	corners.corners[1].face = faceToIslandRange(pMesh->range, corners.corners[1].face);
	return corners;
}

static
FaceCorner subCallGetAdjCorner(const void *pMeshRaw, FaceCorner corner) {
	const SubMesh *pMesh = pMeshRaw;
	FaceCorner adj = {0};
	I32 face = pMesh->pIslands->faces.pArr[pMesh->range.start + corner.face];
	stucGetAdjCorner(
		pMesh->pMesh,
		(FaceCorner){.face = face, .corner = corner.corner},
		&adj
	);
	adj.face = pMesh->pIslands->pFaceTable[adj.face];
	if (!isFaceInIsland(pMesh->pIslands->pArr + pMesh->island, adj.face)) {
		return (FaceCorner){.face = -1, .corner = -1};
	}
	adj.face = faceToIslandRange(pMesh->range, adj.face);
	return adj;
}

static
bool subSplitPredicate(const void *pMeshRaw, I32 edge) {
	return stucGetIfPreserveEdge(((const SubMesh *)pMeshRaw)->pMesh, edge);
}

static
StucErr islandSplitToSub(void *pArgsRaw) {
	StucErr err = PIX_ERR_SUCCESS;
	SubIslandJobArgs *pArgs = pArgsRaw;
	const Mesh *pInMesh = ((const SubIslandJobShared *)pArgs->core.pShared)->pInMesh;
	StucCtx *pCtx = pArgs->core.pCtx;
	PixtyRange range = pArgs->core.range;
	I32 rangeSize = range.end - range.start;
	StucSubIslandArr *pBuf = pCtx->alloc.fpCalloc(rangeSize, sizeof(StucSubIslandArr));
	PixmshSplitMem splitMem = {0};
	SubMesh subMesh = {.pMesh = pInMesh, .pIslands = pArgs->pIslands};
	PixmshSplitIntfIn splitMesh = {
		.pUserData = &subMesh,
		.fpFaceRange = subFaceRange,
		.fpEdge = subGetEdge,
		.fpPos = subUv,
		.fpEdgeCorners = subGetEdgeCorners,
		.fpAdjCorner = subCallGetAdjCorner
	};
	PixmshSplitIntfOut splitIslands = {
		.fpBorderInit = borderInit,
		.fpBorderAddEdge = borderAddEdge,
		.fpFacesInit = islandFacesInit,
		.fpIslandAdd = islandAdd, //TODO using in-island func still, make sub one
		.fpRangeSet = islandRangeSet
	};
	for (I32 i = 0; i < rangeSize; ++i) {
		const StucInIsland *pIsland = pArgs->pIslands->pArr + range.start + i;
		splitMesh.faceCount = pIsland->core.faces.end - pIsland->core.faces.start;
		splitIslands.pUserData = pBuf + i;
		subMesh.range = pIsland->core.faces;
		subMesh.island = range.start + i;
		err = pixmshSplitToIslands(
			&pCtx->alloc,
			&splitMem,
			&splitMesh,
			&splitIslands,
			subSplitPredicate
		);
		PIX_ERR_THROW_IFNOT(err, "", 0);
	}
	for (I32 i = 0; i < rangeSize; ++i) {
		pArgs->pIslands->pArr[range.start + i].sub = pBuf[i];
	}
	PIX_ERR_CATCH(0, err, ;);
	pixmshSplitMemDestroy(&pCtx->alloc, &splitMem);
	pCtx->alloc.fpFree(pBuf);
	return err;
}
#endif

typedef struct BorderMesh {
	const BorderEdgeArr *pBorder;
	const Mesh *pMesh;
} BorderMesh;

static
V2_F32 borderPosGet(const void *pArgsRaw, PixmshFaceRange border, I32 idx) {
	const BorderMesh *pArgs = pArgsRaw;
	FaceCorner corner = pArgs->pBorder->pArr[idx].corner;
	I32 faceStart = pArgs->pMesh->core.pFaces[corner.face];
	return pArgs->pMesh->pUvs[faceStart + corner.corner];
}

static
PixtyV2_F32 clustUv(const void *pMeshRaw, I32 corner) {
	const Mesh *pMesh = pMeshRaw;
	PIX_ERR_ASSERT("", pMesh->pUvs && corner >= 0 && corner < pMesh->core.cornerCount);
	return pMesh->pUvs[corner];
}

static
FaceCorner callGetAdjCorner(const void *pMeshRaw, FaceCorner corner) {
	FaceCorner adj = {0};
	stucGetAdjCorner(pMeshRaw, corner, &adj);
	return adj;
}

static
PixmshFaceRange stucPixmshFaceRange(const void *pMeshRaw, I32 face) {
	const Mesh *pMesh = pMeshRaw;
	PIX_ERR_ASSERT("", face >= 0 && face < pMesh->core.faceCount);
	I32 start = pMesh->core.pFaces[face];
	return (PixmshFaceRange) {
		.start = start,
		.size = pMesh->core.pFaces[face + 1] - start
	};
}

static
PixErr logIslands(
	StucCark *pCark, 
	const Mesh *pMesh,
	const StucInIslandArr *pIslands,
	I32 islandIdx
) {
	PixErr err = PIX_ERR_SUCCESS;
	const StucIsland *pIsland = &pIslands->pArr[islandIdx].core;
	for (I32 i = pIsland->faces.start; i < pIsland->faces.end; ++i) {
		CarkLog log = {0};
		err = CARK_LOG_START(*pCark, 0, STUC_STAGE_ISLAND_SPLIT, 0, 0, i, log);
		PIX_ERR_RETURN_IFNOT(err, "");
		I32 realFaceIdx = pIslands->faces.pArr[i];
		I32 faceStart = pMesh->core.pFaces[realFaceIdx];
		err = carkOutLogComp(&log, 0, NULL, &faceStart);
		PIX_ERR_RETURN_IFNOT(err, "");
		I32 faceSize = pMesh->core.pFaces[realFaceIdx + 1] - faceStart;
		err = carkOutLogComp(&log, 1, NULL, &faceSize);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogComp(&log, 2, NULL, &islandIdx);
		PIX_ERR_RETURN_IFNOT(err, "");
		err = carkOutLogEnd(&log);
		PIX_ERR_RETURN_IFNOT(err, "");
	}
	return err;
}

StucErr StucSplitMeshToIslands(
	StucCtx *pCtx,
	StucCark *pCark,
	const Mesh *pMesh,
	StucInIslandArr *pIslands
) {
	StucErr err = PIX_ERR_SUCCESS;
	PixmshSplitIntfIn splitMesh = {
		.pUserData = pMesh,
		.faceCount = pMesh->core.faceCount,
		.fpFaceRange = stucPixmshFaceRange,
		.fpEdge = getEdge,
		.fpPos = clustUv,
		.fpEdgeCorners = getEdgeCorners,
		.fpAdjCorner = callGetAdjCorner
	};
	PixmshSplitIntfOut splitIslands = {
		.pUserData = pIslands,
		.fpBorderInit = stucInIslandBorderInit,
		.fpBorderAddEdge = stucInIslandBorderAddEdge,
		.fpFacesInit = stucInIslandFacesInit,
		.fpIslandAdd = stucInIslandAdd,
		.fpRangeSet = stucInIslandRangeSet,
		.fpBorderMarkAsOuter = stucInIslandBorderMarkAsOuter
	};
	PixmshSplitMem splitMem = {0};
	err = pixmshSplitToIslands(
		&pCtx->alloc,
		&splitMem,
		&splitMesh,
		&splitIslands,
		splitPredicate
	);
	pixmshSplitMemDestroy(&pCtx->alloc, &splitMem);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	for (I32 i = 0; i < pIslands->count; ++i) {
		StucIsland *pIsland = &pIslands->pArr[i].core;
		I32 outer = pIsland->borders.outer;
		const BorderEdgeArr *pBorder = &pIsland->borders.pArr[outer].arr;
		pIslands->pArr[i].wind = pixmshCalcFaceWind(
			(PixmshFaceRange){.start = 0, .size = pBorder->count},
			&(BorderMesh){.pBorder = pBorder, .pMesh = pMesh},
			borderPosGet
		);
		if (pCark->valid) {
			err = logIslands(pCark, pMesh, pIslands, i);
			PIX_ERR_THROW_IFNOT(err, "", 0);
		}
	}
	pIslands->pFaceTable = pCtx->alloc.fpMalloc(sizeof(I32) * pMesh->core.faceCount);
	for (I32 i = 0; i < pMesh->core.faceCount; ++i) {
		I32 face = pIslands->faces.pArr[i];
		PIX_ERR_ASSERT("", face >= 0 && face < pMesh->core.faceCount);
		pIslands->pFaceTable[face] = i;
	}
#ifdef STUC_USE_SUB_ISLANDS
	SubIslandJobShared shared = {.pCtx = pCtx, .pInMesh = pMesh};
	SubIslandJobArgs args[PIXTH_MAX_SUB_MAPPING_JOBS] = {0};
	I32 jobCount = 0;
	stucMakeJobArgs(
		pCtx,
		&shared,
		&jobCount,
		args, sizeof(SubIslandJobArgs),
		pIslands,
		subIslandsJobGetRange, subIslandsJobInit
	);
	err = stucDoJobInParallel(
		pCtx,
		threadId,
		jobCount,
		args,
		sizeof(SubIslandJobArgs),
		islandSplitToSub
	);
	PIX_ERR_THROW_IFNOT(err, "", 0);
#endif
	PIX_ERR_CATCH(0, err, ;);
	if (pIslands->pFaceTable) {
		pCtx->alloc.fpFree(pIslands->pFaceTable);
		pIslands->pFaceTable = NULL;
	}
	return err;
}

void stucInIslandsBorderArrDestroy(const StucCtx *pCtx, BorderArr *pArr) {
	if (pArr->pArr) {
		for (I32 i = 0; i < pArr->count; ++i) {
			if (pArr->pArr[i].arr.pArr) {
				pCtx->alloc.fpFree(pArr->pArr[i].arr.pArr);
			}
		}
		pCtx->alloc.fpFree(pArr->pArr);
	}
}

void stucInIslandsDestroy(const StucCtx *pCtx, StucInIslandArr *pArr) {
	if (pArr->faces.pArr) {
		pCtx->alloc.fpFree(pArr->faces.pArr);
	}
	if (pArr->pFaceTable) {
		pCtx->alloc.fpFree(pArr->pFaceTable);
	}
	if (!pArr->pArr) {
		*pArr = (StucInIslandArr){0};
		return;
	}
	for (I32 i = 0; i < pArr->size; ++i) {
		if (!pArr->pArr[i].core.faces.end) {
			break;
		}
		stucInIslandsBorderArrDestroy(pCtx, &pArr->pArr[i].core.borders);
		if (pArr->pArr[i].borderTable.pTable) {
			pixuctHTableDestroy(&pArr->pArr[i].borderTable);
		}
		StucSubIslandArr *pSub = &pArr->pArr[i].sub;
		for (I32 j = 0; j < pSub->count; ++j) {
			stucInIslandsBorderArrDestroy(pCtx, &pSub->pArr[j].core.borders);
		}
		if (pSub->pFaces) {
			pCtx->alloc.fpFree(pSub->pFaces);
		}
		if (pSub->pArr) {
			pCtx->alloc.fpFree(pSub->pArr);
		}
	}
	pixuctHTableMemDestroy(&pCtx->alloc, &pArr->tableMem);
	pCtx->alloc.fpFree(pArr->pArr);
	*pArr = (StucInIslandArr){0};
}

StucErr stucMeshAttribsCornerToVert(StucCtx *pCtx, StucMesh *pMesh) {
	StucErr err = PIX_ERR_SUCCESS;
	I32 newSize = pMesh->vertAttribs.count + pMesh->cornerAttribs.count;
	PIXALC_DYN_ARR_RESIZE(StucAttrib, &pCtx->alloc, &pMesh->vertAttribs, newSize);
	memcpy(
		pMesh->vertAttribs.pArr + pMesh->vertAttribs.count,
		pMesh->cornerAttribs.pArr,
		sizeof(Attrib) * pMesh->cornerAttribs.count
	);
	I8Arr flags = {
		.count = pMesh->vertCount,
		.pArr = pCtx->alloc.fpCalloc(pMesh->vertCount, 1)
	};
	for (I32 i = 0; i < pMesh->cornerAttribs.count; ++i) {
		AttribCore *pAttrib = &pMesh->vertAttribs.pArr[pMesh->vertAttribs.count + i].core;
		I32 attribSize = stucGetAttribSizeIntern(pAttrib->type);
		pAttrib->pData = pCtx->alloc.fpCalloc(pMesh->vertCount, attribSize);
	}
	I32 vertSize = pMesh->vertCount;
	for (I32 i = 0; i < pMesh->cornerCount; ++i) {
		I32 vert = pMesh->pCorners[i];
		if (!flags.pArr[vert]) {
			flags.pArr[vert] = true;
			stucCopyAttribs(&pMesh->vertAttribs, vert, &pMesh->cornerAttribs, i);
			continue;
		}
		bool split = !stucCmpAttribs(&pMesh->vertAttribs, vert, &pMesh->cornerAttribs, i);
		if (split) {
			stucReallocVertAttribsIfNeeded(pCtx, pMesh, &vertSize);
			stucCopyInSameAttrib(&pMesh->vertAttribs, pMesh->vertCount, vert);
			vert = pMesh->vertCount;
			++pMesh->vertCount;
			pMesh->pCorners[i] = vert;
		}
		stucCopyAttribs(&pMesh->vertAttribs, vert, &pMesh->cornerAttribs, i);
	}
	pMesh->vertAttribs.count = newSize;
	pCtx->alloc.fpFree(flags.pArr);
	for (I32 i = 0; i < pMesh->cornerAttribs.count; ++i) {
		pCtx->alloc.fpFree(pMesh->cornerAttribs.pArr[i].core.pData);
	}
	pCtx->alloc.fpFree(pMesh->cornerAttribs.pArr);
	pMesh->cornerAttribs = (AttribArray){0};
	return err;
}

StucErr stucMeshBuildTangentsForTris(StucCtx *pCtx, StucMesh *pMesh) {
	StucErr err = PIX_ERR_SUCCESS;
	Mesh wrap = {0};
	err = stucAttemptToSetMissingActiveDomains(pMesh);
	PIX_ERR_RETURN_IFNOT(err, "");
	wrap.core = *pMesh;
	UBitField32 spAttribsToAppend = STUC_ATTRIB_USE_FIELD(((StucAttribUse[]) {
		STUC_ATTRIB_USE_TANGENT,
		STUC_ATTRIB_USE_TSIGN
	}));
	stucAppendSpAttribsToMesh(
		pCtx,
		&wrap,
		spAttribsToAppend, 
		STUC_ATTRIB_ORIGIN_MESH_OUT
	);
	err = stucAssignActiveAliases(
		pCtx,
		&wrap,
		0xffffffff,
		STUC_DOMAIN_NONE
	);
	PIX_ERR_RETURN_IFNOT(err, "");
	err = stucBuildTangentsForTris(pCtx, &wrap);
	PIX_ERR_RETURN_IFNOT(err, "");
	*pMesh = wrap.core;
	return err;
}

void stucLogStageInstAdd(JobArgs *pJobArgs, StucStage stage) {
	PIX_ERR_ASSERT(
		"",
		stage > STUC_STAGE_NONE && stage < STUC_STAGE_ENUM_COUNT &&
		stucStageStructCountArr[stage] > 0
	);
	pJobArgs->logInst = -1;
	for (I32 i = 0; i < stucStageStructCountArr[stage]; ++i) {
		I32 inst = CARK_INST_ADD(*pJobArgs->pCark, pJobArgs->threadId, stage, i);
		if (pJobArgs->logInst == -1) {
			pJobArgs->logInst = inst;
			continue;
		}
		PIX_ERR_ASSERT("", inst == pJobArgs->logInst);
	}
}
