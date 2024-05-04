#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <Context.h>
#include <MapToMesh.h>
#include <MapFile.h>
#include <MathUtils.h>
#include <AttribUtils.h>
#include <Utils.h>

#define FLOAT_BC_MARGIN .0001f

typedef struct {
	V3_F32 a;
	V3_F32 b;
	V3_F32 c;
} TriXyz;

typedef struct {
	V2_F32 a;
	V2_F32 b;
	V2_F32 c;
} TriUv;

static V3_F32 calcIntersection(LoopBufferWrap *pLoopBuf, LoopInfo *pBaseLoop,
                             int32_t i, int32_t vertNextIndex, int32_t *pAlpha) {
	V3_F32 *pRuvmVert = &pLoopBuf->buf[i].loop;
	V3_F32 *pRuvmVertNext = &pLoopBuf->buf[vertNextIndex].loop;
	V3_F32 ruvmDir = _(*pRuvmVert V3SUB *pRuvmVertNext);
	V3_F32 ruvmDirBack = _(*pRuvmVertNext V3SUB *pRuvmVert);
	float t = (pRuvmVert->d[0] - pBaseLoop->vert.d[0]) * pBaseLoop->dirBack.d[1];
	t -= (pRuvmVert->d[1] - pBaseLoop->vert.d[1]) * pBaseLoop->dirBack.d[0];
	t /= ruvmDir.d[0] * pBaseLoop->dirBack.d[1] - ruvmDir.d[1] * pBaseLoop->dirBack.d[0];
	float distance = sqrt(ruvmDir.d[0] * ruvmDir.d[0] + ruvmDir.d[1] * ruvmDir.d[1]);
	*pAlpha = t / distance;
	if (*pAlpha < .0f) {
		*pAlpha *= -1.0f;
	}
	return _(*pRuvmVert V3ADD _(ruvmDirBack V3MULS t));
}

static void clipRuvmFaceAgainstSingleLoop(LoopBufferWrap *pLoopBuf, LoopBufferWrap *pNewLoopBuf,
                                          int32_t *pInsideBuf, LoopInfo *pBaseLoop, V2_F32 baseLoopCross,
								          int32_t *pEdgeFace, int32_t preserve, int32_t faceWindingDir,
										  int32_t *pOnLine) {
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		V2_F32 ruvmVert = *(V2_F32 *)&pLoopBuf->buf[i].loop;
		V2_F32 uvRuvmDir = _(ruvmVert V2SUB pBaseLoop->vert);
		float dot = _(baseLoopCross V2DOT uvRuvmDir);
		pInsideBuf[i] = dot == .0f ? -1 : (dot < .0f) ^ !faceWindingDir;
	}
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		int32_t vertNextIndex = (i + 1) % pLoopBuf->size;
		if (pInsideBuf[i]) {
			//point is inside, or on the line
			pNewLoopBuf->buf[pNewLoopBuf->size] = pLoopBuf->buf[i];
			//using += so that base loops can be determined. ie, if an ruvm
			//vert has a dot of 0 twice, then it is sitting on a base vert,
			//but if once, then it's sitting on an edge.
			if (pInsideBuf[i] < 0) {
				if (pLoopBuf->buf[i].onLine) {
					//this loop already resided on a previous base edge,
					//it must then reside on a base vert, rather than an edge.
					//determine which vert in the edge it sits on:
					int32_t onLineBase;
					if (pLoopBuf->buf[i].loop.d[0] == pBaseLoop->vert.d[0] &&
						pLoopBuf->buf[i].loop.d[1] == pBaseLoop->vert.d[1]) {
						//on base vert
						onLineBase = pBaseLoop->localIndex;
					}
					else {
						//on next base vert
						onLineBase = pBaseLoop->localIndexNext;
					}
					pNewLoopBuf->buf[pNewLoopBuf->size].baseLoop = onLineBase;
				}
				else {
					//resides on base edge
					pNewLoopBuf->buf[pNewLoopBuf->size].baseLoop =
						pBaseLoop->localIndex;
				}
				*pOnLine = 1;
				pNewLoopBuf->buf[pNewLoopBuf->size].onLine = 1;
			}
			(pNewLoopBuf->size)++;
		}
		int32_t alpha;
		if (pInsideBuf[i] != 0 ^ pInsideBuf[vertNextIndex] != 0 &&
		    pInsideBuf[i] >= 0 && pInsideBuf[vertNextIndex] >= 0) {
			//the current point is inside, but the next is not (or visa versa),
			//so calc intersection point. The != and ^ are to account for the
			//fact that insideBuf can be negative if the point is on the line.
			//The != converts the value to absolute, thus ignoring this.
			*pEdgeFace += 1;
			LoopBuffer *pNewEntry = pNewLoopBuf->buf + pNewLoopBuf->size;
			if ((pLoopBuf->buf[i].baseLoop == pLoopBuf->buf[vertNextIndex].baseLoop ||
				 pLoopBuf->buf[i].isBaseLoop || pLoopBuf->buf[vertNextIndex].isBaseLoop)) {
				int32_t whichVert = pLoopBuf->buf[i].baseLoop == pBaseLoop->index - 1;
				pNewEntry->loop = calcIntersection(pLoopBuf, pBaseLoop,
				                                   i, vertNextIndex, &alpha);
				pNewEntry->baseLoop = whichVert ?
					pBaseLoop->localIndex : pBaseLoop->localIndexNext;
				pNewEntry->preserve = preserve;
				pNewEntry->isBaseLoop = 1;
			}
			else {
				pNewEntry->loop = calcIntersection(pLoopBuf, pBaseLoop,
				                                   i, vertNextIndex, &alpha);
				pNewEntry->baseLoop = pBaseLoop->index;
				pNewEntry->isBaseLoop = 0;
				pNewEntry->preserve = preserve;
			}
			//pNewEntry->normal = vec3Lerp(pLoopBuf->buf[i].normal,
			//                             pLoopBuf->buf[vertNextIndex].normal,
			//                             alpha);
			//TODO add proper lerp for normal (why was the above commented out?)
			pNewEntry->normal = pLoopBuf->buf[i].normal;
			pNewEntry->isRuvm = 0;
			pNewEntry->ruvmLoop = pLoopBuf->buf[i].ruvmLoop;
			pNewLoopBuf->size++;
		}
	}
}

static int32_t calcFaceWindingDirection(FaceInfo face, V2_F32 *pUvs) {
	V2_F32 centre = {0};
	for (int32_t i = 0; i < face.size; ++i) {
		_(&centre V2ADDEQL pUvs[face.start + i]);
	}
	_(&centre V2DIVSEQL (float)face.size);
	return v2WindingCompare(pUvs[face.start],
	                          pUvs[face.start + 1], centre, 0);
}

static void clipRuvmFaceAgainstBaseFace(ThreadArg *pArgs, FaceInfo baseFace,
                                 LoopBufferWrap *pLoopBuf, int32_t *pEdgeFace,
								 int32_t *pHasPreservedEdge, int32_t *pSeam,
								 int32_t faceWindingDir, int32_t *pOnLine) {
	for (int32_t i = 0; i < baseFace.size; ++i) {
		LoopInfo baseLoop;
		baseLoop.index = i;
		baseLoop.edgeIndex = pArgs->mesh.mesh.pEdges[baseFace.start + i];
		baseLoop.edgeIsSeam = checkIfEdgeIsSeam(baseLoop.edgeIndex, baseFace, i,
			                                    &pArgs->mesh, pArgs->pEdgeVerts);
		int8_t preserveEdge[2];
		preserveEdge[0] = checkIfEdgeIsPreserve(&pArgs->mesh, baseLoop.edgeIndex);
		baseLoop.vert = pArgs->mesh.pUvs[i + baseFace.start];
		int32_t uvNextIndexLocal = ((i + 1) % baseFace.size);
		int32_t uvNextIndex = uvNextIndexLocal + baseFace.start;
		baseLoop.edgeIndexNext = pArgs->mesh.mesh.pEdges[uvNextIndex];
		baseLoop.edgeNextIsSeam = checkIfEdgeIsSeam(baseLoop.edgeIndexNext, baseFace,
		                                            uvNextIndexLocal, &pArgs->mesh,
		                                            pArgs->pEdgeVerts);
		/*
		if (preserveEdge[0] && !baseLoop.edgeIsSeam && !pArgs->pInVertTable[vertIndex]) {
			pArgs->pInVertTable[vertIndex] = 1;
		}
		//cap at 3 to avoid integer overflow
		else if (baseLoop.edgeIsSeam && pArgs->pVertSeamTable[vertIndex] < 3) {
			pArgs->pVertSeamTable[vertIndex]++;
		}
		*/
		/*
		preserveEdge[1] = checkIfEdgeIsPreserve(&pArgs->mesh, baseLoop.edgeIndexNext);
		if (preserveEdge[1] && !baseLoop.edgeNextIsSeam) {
			int32_t nextVertIndex = pArgs->mesh.pLoops[uvNextIndex];
			pArgs->pInVertTable[nextVertIndex] = 1;
		}
		*/
		baseLoop.vertNext = pArgs->mesh.pUvs[uvNextIndex];
		baseLoop.indexNext = uvNextIndexLocal;
		baseLoop.localIndex = i;
		baseLoop.localIndexNext = uvNextIndexLocal;
		baseLoop.dir = _(baseLoop.vertNext V2SUB baseLoop.vert);
		baseLoop.dirBack = _(baseLoop.vert V2SUB baseLoop.vertNext);
		LoopBufferWrap newLoopBuf = {0};
		int32_t insideBuf[12] = {0};
		V2_F32 baseLoopCross = v2Cross(baseLoop.dir);
		int32_t edgeFacePre = *pEdgeFace;
		clipRuvmFaceAgainstSingleLoop(pLoopBuf, &newLoopBuf, insideBuf,
		         				      &baseLoop, baseLoopCross, pEdgeFace,
									  preserveEdge[0], faceWindingDir, pOnLine);
		int32_t intersects = edgeFacePre != *pEdgeFace;
		if (intersects && preserveEdge[0]) {
			*pHasPreservedEdge = 1;
		}
		if (intersects && baseLoop.edgeIsSeam) {
			*pSeam = 1;
		}

		if (newLoopBuf.size <= 2) {
			pLoopBuf->size = newLoopBuf.size;
			return;
		}
		memcpy(pLoopBuf->buf, newLoopBuf.buf, sizeof(LoopBuffer) * newLoopBuf.size);
		pLoopBuf->size = newLoopBuf.size;
	}
}

static void transformClippedFaceFromUvToXyz(LoopBufferWrap *pLoopBuf, 
											FaceInfo baseFace, BaseTriVerts *pBaseTri,
											Mesh *pMeshIn, V2_F32 tileMin,
											MapToMeshVars *pMmVars) {
	for (int32_t j = 0; j < pLoopBuf->size; ++j) {
		V3_F32 vert = pLoopBuf->buf[j].loop;
		//uv is just the vert position before transform, so set that here
		pLoopBuf->buf[j].uv = *(V2_F32 *)&vert;
		//find enclosing triangle
		_((V2_F32 *)&vert V2SUBEQL tileMin);
		V3_F32 vertBc = cartesianToBarycentric(pBaseTri->uv, &vert);
		if (baseFace.size == 4 && vertBc.d[1] < 0) {
			//base face is a quad, and vert is outside first tri,
			//so use the second tri
			
			//regarding the above condition,
			//because triangulation uses ear clipping,
			//and ngons never hit this block of code,
			//we only need to compare y. As it will always
			//be the point opposite the dividing edge in the quad.
			//This avoids us needing to worry about cases where verts
			//are slightly outside of the quad, by a margin of error.
			//A vert will always end up in one or the other tri.
			V2_F32 triBuf[3] =
				{pBaseTri->uv[2], pBaseTri->uv[3], pBaseTri->uv[0]};
			vertBc = cartesianToBarycentric(triBuf, &vert);
			pLoopBuf->buf[j].triLoops[0] = 2;
			pLoopBuf->buf[j].triLoops[1] = 3;
		}
		else {
			for (int32_t k = 0; k < 3; ++k) {
				pLoopBuf->buf[j].triLoops[k] = k;
			}
		}
		int8_t *pTriLoops = pLoopBuf->buf[j].triLoops;
		V3_F32 vertsXyz[3];
		for (int32_t i = 0; i < 3; ++i) {
			int32_t vertIndex = pMeshIn->mesh.pLoops[baseFace.start + pTriLoops[i]];
			vertsXyz[i] = pMeshIn->pVerts[vertIndex];
		}
		//transform vertex
		pLoopBuf->buf[j].loop = barycentricToCartesian(vertsXyz, &vertBc);
		V3_F32 normal = _(pMeshIn->pNormals[baseFace.start + pTriLoops[0]] V3MULS vertBc.d[0]);
		_(&normal V3ADDEQL _(pMeshIn->pNormals[baseFace.start + pTriLoops[1]] V3MULS vertBc.d[1]));
		_(&normal V3ADDEQL _(pMeshIn->pNormals[baseFace.start + pTriLoops[2]] V3MULS vertBc.d[2]));
		_(&normal V3DIVEQLS vertBc.d[0] + vertBc.d[1] + vertBc.d[2]);
		_(&pLoopBuf->buf[j].loop V3ADDEQL _(normal V3MULS vert.d[2] * 1.0f));
		//transform normal from tangent space to object space
		//TODO only multiply by TBN if an option is set to use map normals,
		//otherwise just use the above interpolated
		pLoopBuf->buf[j].normal = _(pLoopBuf->buf[j].normal V3MULM3X3 &pMmVars->tbn);
		pLoopBuf->buf[j].bc = vertBc;
	}
}

//NOTE map and mesh date index params are only used if interpolation is not enabled
//for the attrib. This is always the case on faces.
//Except for right now, because I havn't implemented map triangulation and interpolation,
//so the map data index is used temporarily until that's done.
static void blendMapAndInAttribs(BufMesh *pBufMesh, AttribArray *pDestAttribs,
                                 AttribArray *pMapAttribs, AttribArray *pMeshAttribs,
								 LoopBuffer *pLoopBuf, int32_t loopBufIndex,
								 int32_t dataIndex, int32_t mapDataIndex, int32_t meshDataIndex,
								 RuvmCommonAttrib *pCommonAttribs,
								 int32_t commonAttribCount, FaceInfo *pBaseFace) {
	//TODO make naming for MeshIn consistent
	for (int32_t i = 0; i < pDestAttribs->count; ++i) {
		if (pDestAttribs->pArr[i].origin == RUVM_ATTRIB_ORIGIN_COMMON) {
			if (pDestAttribs->pArr + i == pBufMesh->pVertAttrib ||
			    pDestAttribs->pArr + i == pBufMesh->pUvAttrib ||
				pDestAttribs->pArr + i == pBufMesh->pNormalAttrib) {

				continue;
			}
			RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs->pArr[i].name,
											   pMapAttribs);
			RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs->pArr[i].name,
											      pMeshAttribs);
			RuvmAttribType type = pDestAttribs->pArr[i].type;
			uint8_t mapDataBuf[RUVM_ATTRIB_STRING_MAX_LEN];
			RuvmAttrib mapBuf = {.pData = mapDataBuf, .type = type};
			uint8_t meshDataBuf[RUVM_ATTRIB_STRING_MAX_LEN];
			RuvmAttrib meshBuf = {.pData = meshDataBuf, .type = type};
			if (pMapAttrib->interpolate) {
				//TODO add correct map interpolation. to do this, you'll need
				//to triangulate the face, like with the Mesh in face, and you''
				//need to get baerycentry coords for baseLoops (not necessary
				//for intersection points, can just lerp in the clipping function).
				//so to summarise, only base loops will be interpolated here,
				//intersection loops will be lerped at clipping stage,
				//and map loops obviously don't need interpolation
				
				//temp memcpy until the above todo is handled
				memcpy(mapBuf.pData, attribAsVoid(pMapAttrib, mapDataIndex),
				       getAttribSize(pMapAttrib->type));
			}
			if (pMeshAttrib->interpolate) {
				//TODO skip interlopation if base loop? is it worth it? profile.
				interpolateAttrib(&meshBuf,
								  0,
								  pMeshAttrib,
				                  pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[0],
								  pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[1],
								  pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[2],
								  pLoopBuf[loopBufIndex].bc);
			}
			RuvmCommonAttrib *pCommon =
				getCommonAttrib(pCommonAttribs, commonAttribCount,
			                    pDestAttribs->pArr[i].name);
			RuvmAttrib *orderTable[2];
			int8_t order = pCommon->blendConfig.order;
			orderTable[0] = order ? &mapBuf : &meshBuf;
			orderTable[1] = !order ? &mapBuf : &meshBuf;
			blendAttribs(pDestAttribs->pArr + i, dataIndex, orderTable[0], 0,
			             orderTable[1], 0, pCommon->blendConfig);
		}
		else if (pDestAttribs->pArr[i].origin == RUVM_ATTRIB_ORIGIN_MAP) {
			RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs->pArr[i].name,
											   pMapAttribs);
			if (pMapAttrib->interpolate) {
				//temp memcpy until the above todo is handled
				memcpy(attribAsVoid(pDestAttribs->pArr + i, dataIndex),
				       attribAsVoid(pMapAttrib, mapDataIndex),
				       getAttribSize(pMapAttrib->type));
			}
			else {
				memcpy(attribAsVoid(pDestAttribs->pArr + i, dataIndex),
				       attribAsVoid(pMapAttrib, mapDataIndex),
				       getAttribSize(pMapAttrib->type));
			}
		}
		else if (pDestAttribs->pArr[i].origin == RUVM_ATTRIB_ORIGIN_MESH_IN) {
			RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs->pArr[i].name,
											      pMeshAttribs);
			if (pMeshAttrib->interpolate) {
				//TODO skip interlopation is base loop? is it worth it? profile.
				interpolateAttrib(pDestAttribs->pArr + i,
								  dataIndex,
								  pMeshAttrib,
				                  pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[0],
								  pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[1],
								  pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[2],
								  pLoopBuf[loopBufIndex].bc);
			}
			else {
				memcpy(attribAsVoid(pDestAttribs->pArr + i, dataIndex),
				       attribAsVoid(pMeshAttrib, meshDataIndex),
				       getAttribSize(pMeshAttrib->type));
			}
		}
	}
}

static void simpleCopyAttribs(AttribArray *pDestAttribs,
                       AttribArray *pMapAttribs,
					   AttribArray *pMeshAttribs,
					   int32_t destDataIndex, int32_t srcDataIndex,
					   int32_t indexOrigin) {
	for (int32_t i = 0; i < pDestAttribs->count; ++i) {
		switch (pDestAttribs->pArr[i].origin) {
			case (RUVM_ATTRIB_ORIGIN_COMMON): {
				RuvmAttrib *pSrcAttrib;
				if (indexOrigin) {
					pSrcAttrib = getAttrib(pDestAttribs->pArr[i].name, pMapAttribs);
				}
				else {
					pSrcAttrib = getAttrib(pDestAttribs->pArr[i].name, pMeshAttribs);
				}
				break;
			}
			case (RUVM_ATTRIB_ORIGIN_MAP): {
				if (!indexOrigin) {
					//index is a meshIn index
					continue;
				}
				RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs->pArr[i].name,
												   pMapAttribs);
				memcpy(attribAsVoid(pDestAttribs->pArr + i, destDataIndex),
					   attribAsVoid(pMapAttrib, srcDataIndex),
					   getAttribSize(pMapAttrib->type));
				break;
			}
			case (RUVM_ATTRIB_ORIGIN_MESH_IN): {
				if (indexOrigin) {
					//index is a map index
					continue;
				}
				RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs->pArr[i].name,
													  pMeshAttribs);
				memcpy(attribAsVoid(pDestAttribs->pArr + i, destDataIndex),
					   attribAsVoid(pMeshAttrib, srcDataIndex),
					   getAttribSize(pMeshAttrib->type));
				break;
			}
		}
	}
}

static void initEdgeTableEntry(ThreadArg *pArgs, MeshBufEdgeTable *pEntry,
                               AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
							   int32_t refEdge, int32_t refFace, LoopBuffer *pLoopBuf,
							   int32_t loopBufIndex, int32_t isMapEdge) {
	if (isMapEdge) {
		pEntry->edge = pBufMesh->mesh.edgeCount;
		pBufMesh->mesh.edgeCount++;
	}
	else {
		pEntry->edge = pBufMesh->boundaryEdgeSize;
		pBufMesh->boundaryEdgeSize--;
	}
	printf("%d\n", pEntry->edge);
	simpleCopyAttribs(&pBufMesh->mesh.edgeAttribs,
	                  &pArgs->pMap->mesh.mesh.edgeAttribs,
					  &pArgs->mesh.mesh.edgeAttribs,
					  pEntry->edge, refEdge, isMapEdge);
	pAcfVars->edgeIndex = pEntry->edge;
	pEntry->refEdge = refEdge;
	pEntry->refFace = refFace;
}

static int32_t getRefEdge(ThreadArg *pArgs, FaceInfo *pRuvmFace,
                       FaceInfo *pBaseFace, LoopBuffer *pLoopBuf,
					   int32_t loopBufIndex) {
	if (pLoopBuf[loopBufIndex].isRuvm) {
		int32_t ruvmLoop = pLoopBuf[loopBufIndex].ruvmLoop;
		return pArgs->pMap->mesh.mesh.pEdges[pRuvmFace->start + ruvmLoop];
	}
	else {
		int32_t baseLoop = pLoopBuf[loopBufIndex].baseLoop;
		return pArgs->mesh.mesh.pEdges[pBaseFace->start + baseLoop];
	}
}

static void addEdge(ThreadArg *pArgs, int32_t loopBufIndex, BufMesh *pBufMesh,
                    LoopBuffer *pLoopBuf, MapToMeshVars *pMmVars, RuvmAllocator *pAlloc,
					int32_t refFace, AddClippedFaceVars *pAcfVars, FaceInfo *pBaseFace,
					FaceInfo *pRuvmFace) {
	int32_t refEdge = getRefEdge(pArgs, pRuvmFace, pBaseFace, pLoopBuf, loopBufIndex);
	int32_t isMapEdge = pLoopBuf[loopBufIndex].isRuvm;
	int32_t key = isMapEdge ? refEdge : (refEdge + 1) * -1;
	int32_t hash = ruvmFnvHash((uint8_t *)&key, 4, pMmVars->edgeTableSize);
	MeshBufEdgeTable *pEntry = pMmVars->pEdgeTable + hash;
	pArgs->totalEdges++;
	do {
		if (!pEntry->loopCount) {
			initEdgeTableEntry(pArgs, pEntry, pAcfVars, pBufMesh, refEdge,
			                   refFace, pLoopBuf, loopBufIndex, isMapEdge);
			break;
		}
		int32_t match = pEntry->refEdge == refEdge &&
		                pEntry->refFace == refFace;
		if (match) {
			pAcfVars->edgeIndex = pEntry->edge;
			break;
		}
		if (!pEntry->pNext) {
			pEntry = pEntry->pNext = pAlloc->pCalloc(1, sizeof(MeshBufEdgeTable));
			initEdgeTableEntry(pArgs, pEntry,pAcfVars, pBufMesh, refEdge,
			                   refFace, pLoopBuf, loopBufIndex, isMapEdge);
			break;
		}
		pEntry = pEntry->pNext;
	} while(1);
	pEntry->loopCount++;
}

static void addNewLoopAndOrVert(ThreadArg *pArgs, int32_t loopBufIndex, int32_t *pVertIndex,
                                BufMesh *pBufMesh, LoopBuffer *pLoopBuffer, int32_t loopIndex,
								FaceInfo *pBaseFace) {
		*pVertIndex = pBufMesh->boundaryVertSize;
		pArgs->bufMesh.pVerts[*pVertIndex] = pLoopBuffer[loopBufIndex].loop;
		pBufMesh->boundaryVertSize--;
		//temporarily setting mesh data index to 0, as it's only needed if interpolation is disabled
		blendMapAndInAttribs(pBufMesh, &pBufMesh->mesh.vertAttribs,
							 &pArgs->pMap->mesh.mesh.vertAttribs,
							 &pArgs->mesh.mesh.vertAttribs,
							 pLoopBuffer, loopBufIndex, *pVertIndex, pLoopBuffer[loopBufIndex].ruvmLoop, 0,
							 pArgs->pCommonAttribList->pVert, pArgs->pCommonAttribList->vertCount,
							 pBaseFace);
}

static void initVertAdjEntry(ThreadArg *pArgs, int32_t loopBufferIndex,
                             int32_t *pVertIndex, BufMesh *pBufMesh, LoopBuffer *pLoopBuffer,
							 VertAdj *pVertAdj, FaceInfo baseFace,
							 int32_t ruvmVert) {
	pVertAdj->mapVert = ruvmVert;
	*pVertIndex = pBufMesh->mesh.vertCount++;
	pVertAdj->vert = *pVertIndex;
	pVertAdj->baseFace = baseFace.index;
	pArgs->bufMesh.pVerts[*pVertIndex] = pLoopBuffer[loopBufferIndex].loop;
	blendMapAndInAttribs(pBufMesh, &pBufMesh->mesh.vertAttribs,
						 &pArgs->pMap->mesh.mesh.vertAttribs,
						 &pArgs->mesh.mesh.vertAttribs,
						 pLoopBuffer, loopBufferIndex, *pVertIndex, pLoopBuffer[loopBufferIndex].ruvmLoop, 0,
						 pArgs->pCommonAttribList->pVert, pArgs->pCommonAttribList->vertCount,
						 &baseFace);
}

static void addRuvmLoopAndOrVert(ThreadArg *pArgs, int32_t loopBufIndex, AddClippedFaceVars *pAcfVars,
                          BufMesh *pBufMesh, LoopBuffer *pLoopBufEntry,
						  MapToMeshVars *pMmVars, RuvmAllocator *pAlloc,
						  FaceInfo baseFace, FaceInfo *pRuvmFace) {
	if (pAcfVars->firstRuvmVert < 0) {
		pAcfVars->firstRuvmVert = pLoopBufEntry[loopBufIndex].ruvmLoop;
	}
	int32_t ruvmLoop = pRuvmFace->start + pLoopBufEntry[loopBufIndex].ruvmLoop;
	uint32_t uRuvmVert = pArgs->pMap->mesh.mesh.pLoops[ruvmLoop];
	int32_t hash = ruvmFnvHash((uint8_t *)&uRuvmVert, 4, pMmVars->vertAdjSize);
	VertAdj *pVertAdj = pMmVars->pRuvmVertAdj + hash;
	do {
		if (!pVertAdj->loopSize) {
			initVertAdjEntry(pArgs, loopBufIndex, &pAcfVars->vertIndex, pBufMesh,
			                 pLoopBufEntry, pVertAdj, baseFace, uRuvmVert);
			break;
		}
		//TODO should you be checking tile here as well?
		int32_t match = pVertAdj->mapVert == uRuvmVert &&
		                pVertAdj->baseFace == baseFace.index;
		if (match) {
			pAcfVars->vertIndex = pVertAdj->vert;
			break;
		}
		if (!pVertAdj->pNext) {
			pVertAdj = pVertAdj->pNext = pAlloc->pCalloc(1, sizeof(VertAdj));
			initVertAdjEntry(pArgs, loopBufIndex, &pAcfVars->vertIndex, pBufMesh,
			                 pLoopBufEntry, pVertAdj, baseFace, uRuvmVert);
			break;
		}
		pVertAdj = pVertAdj->pNext;
	} while (1);
	pVertAdj->loopSize++;
}

static void initBoundaryBufferEntry(ThreadArg *pArgs, AddClippedFaceVars *pAcfVars,
                             BoundaryVert *pEntry, int32_t ruvmFaceIndex,
                             int32_t tile, LoopBufferWrap *pLoopBuf, FaceInfo baseFace,
							 int32_t hasPreservedEdge, int32_t seam) {
	pEntry->face = pArgs->bufMesh.boundaryFaceSize;
	pEntry->faceIndex = ruvmFaceIndex;
	pEntry->tile = tile;
	pEntry->job = pArgs->id;
	pEntry->baseFace = baseFace.index;
	pEntry->hasPreservedEdge = hasPreservedEdge;
	pEntry->seam = seam;
	if (pLoopBuf->size > 11) {
		printf("----------------------   Loopbuf size exceeded 11\n");
		abort();
	}

	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		pEntry->onLine |= (pLoopBuf->buf[i].onLine != 0) << i;
		pEntry->isRuvm |= (pLoopBuf->buf[i].isRuvm) << i;
		pEntry->ruvmLoop |= pLoopBuf->buf[i].ruvmLoop << i * 3;
		if (pLoopBuf->buf[i].isBaseLoop) {
			//online is used to indicate that a loop is a baseloop.
			//isRuvm == 0, and onLine == 1, indicates a baseLoop
			pEntry->onLine |= 1 << i;
		}
		if (pLoopBuf->buf[i].isRuvm && !pLoopBuf->buf[i].onLine) {
			continue;
		}
		pEntry->baseLoop |= pLoopBuf->buf[i].baseLoop << i * 2;
	}
	if (pLoopBuf->size > pArgs->maxLoopSize) {
		pArgs->maxLoopSize = pLoopBuf->size;
	}
	if (pAcfVars->firstRuvmVert < 0) {
		int32_t *pNonRuvmSort = (int32_t *)(pEntry + 1);
		for (int32_t i = 0; i < pLoopBuf->size; ++i) {
			pNonRuvmSort[i] = pLoopBuf->buf[i].ruvmLoop;
		}
	}
}


static void addEdgeFaceToBoundaryBuffer(ThreadArg *pArgs, AddClippedFaceVars *pAcfVars,
                                 LoopBufferWrap *pLoopBuf, int32_t ruvmFaceIndex,
								 int32_t tile, FaceInfo baseFace, int32_t hasPreservedEdge,
								 int32_t seam) {
	pArgs->bufMesh.mesh.pFaces[pArgs->bufMesh.boundaryFaceSize] = pAcfVars->boundaryLoopStart;
	int32_t hash = ruvmFnvHash((uint8_t *)&ruvmFaceIndex, 4, pArgs->boundaryBufferSize);
	BoundaryDir *pEntryDir = pArgs->pBoundaryBuffer + hash;
	BoundaryVert *pEntry = pEntryDir->pEntry;
	int32_t sizeToAllocate = sizeof(BoundaryVert);
	if (pAcfVars->firstRuvmVert < 0) {
		sizeToAllocate += sizeof(int32_t) * pLoopBuf->size;
	}
	if (!pEntry) {
		pEntry = pEntryDir->pEntry = pArgs->alloc.pCalloc(1, sizeToAllocate);
		initBoundaryBufferEntry(pArgs, pAcfVars, pEntry, ruvmFaceIndex,
		                        tile, pLoopBuf, baseFace, hasPreservedEdge, seam);
		pArgs->totalFaces++;
	}
	else {
		do {
			if (pEntry->faceIndex == ruvmFaceIndex) {
				while (pEntry->pNext) {
					pEntry = pEntry->pNext;
				}
				pEntry = pEntry->pNext = pArgs->alloc.pCalloc(1, sizeToAllocate);
				initBoundaryBufferEntry(pArgs, pAcfVars, pEntry, ruvmFaceIndex,
				                        tile, pLoopBuf, baseFace, hasPreservedEdge, seam);
				break;
			}
			if (!pEntryDir->pNext) {
				pEntryDir = pEntryDir->pNext = pArgs->alloc.pCalloc(1, sizeof(BoundaryDir));
				pEntry = pEntryDir->pEntry = pArgs->alloc.pCalloc(1, sizeToAllocate);
				initBoundaryBufferEntry(pArgs, pAcfVars, pEntry, ruvmFaceIndex,
				                        tile, pLoopBuf, baseFace, hasPreservedEdge, seam);
				pArgs->totalFaces++;
				break;
			}
			pEntryDir = pEntryDir->pNext;
			pEntry = pEntryDir->pEntry;
		} while (1);
	}
	pArgs->bufMesh.boundaryFaceSize--;
}

static void addClippedFaceToBufMesh(ThreadArg *pArgs, MapToMeshVars *pMmVars,
                               LoopBufferWrap *pLoopBuf, int32_t edgeFace,
							   FaceInfo ruvmFace, int32_t tile, FaceInfo baseFace,
                               int32_t hasPreservedEdge, int32_t seam, int32_t onLine) {
	AddClippedFaceVars acfVars;
	acfVars.loopStart = pArgs->bufMesh.mesh.loopCount;
	acfVars.boundaryLoopStart = pArgs->bufMesh.boundaryLoopSize;
	acfVars.ruvmLoops = 0;
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		pArgs->totalLoops++;
		acfVars.loopIndex = edgeFace || onLine ?
			pArgs->bufMesh.boundaryLoopSize-- : pArgs->bufMesh.mesh.loopCount++;
		int32_t refFace;
		int32_t isRuvm = pLoopBuf->buf[i].isRuvm;
		if (isRuvm) {
			acfVars.ruvmLoops++;
		}
		if (!isRuvm || pLoopBuf->buf[i].onLine) {
			addNewLoopAndOrVert(pArgs, i, &acfVars.vertIndex, &pArgs->bufMesh,
			                    pLoopBuf->buf, acfVars.loopIndex, &baseFace);
			refFace = ruvmFace.index;
		}
		else {
			addRuvmLoopAndOrVert(pArgs, i, &acfVars, &pArgs->bufMesh, pLoopBuf->buf,
			                     pMmVars, &pArgs->alloc, baseFace, &ruvmFace);
			refFace = baseFace.index;
		}
		pArgs->bufMesh.mesh.pLoops[acfVars.loopIndex] = acfVars.vertIndex;
		blendMapAndInAttribs(&pArgs->bufMesh, &pArgs->bufMesh.mesh.loopAttribs,
							 &pArgs->pMap->mesh.mesh.loopAttribs,
							 &pArgs->mesh.mesh.loopAttribs,
							 pLoopBuf->buf, i, acfVars.loopIndex,
							 ruvmFace.start + pLoopBuf->buf[i].ruvmLoop, baseFace.start,
							 pArgs->pCommonAttribList->pLoop, pArgs->pCommonAttribList->loopCount,
							 &baseFace);
		addEdge(pArgs, i, &pArgs->bufMesh, pLoopBuf->buf, pMmVars, &pArgs->alloc,
		        refFace, &acfVars, &baseFace, &ruvmFace);
		pArgs->bufMesh.mesh.pEdges[acfVars.loopIndex] = acfVars.edgeIndex;
		pArgs->bufMesh.pNormals[acfVars.loopIndex] = pLoopBuf->buf[i].normal;
		pArgs->bufMesh.pUvs[acfVars.loopIndex] = pLoopBuf->buf[i].uv;
	}
	if (edgeFace || onLine) {
		addEdgeFaceToBoundaryBuffer(pArgs, &acfVars, pLoopBuf,
		                            ruvmFace.index, tile, baseFace,
									hasPreservedEdge, seam);
	}
	else {
		pArgs->bufMesh.mesh.pFaces[pArgs->bufMesh.mesh.faceCount] = acfVars.loopStart;
		blendMapAndInAttribs(&pArgs->bufMesh, &pArgs->bufMesh.mesh.faceAttribs,
							 &pArgs->pMap->mesh.mesh.faceAttribs,
							 &pArgs->mesh.mesh.faceAttribs,
							 pLoopBuf->buf, 0, acfVars.loopIndex, ruvmFace.index, baseFace.index,
							 pArgs->pCommonAttribList->pFace, pArgs->pCommonAttribList->faceCount,
							 &baseFace);
		pArgs->bufMesh.mesh.faceCount++;
	}
}

void ruvmMapToSingleFace(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                     MapToMeshVars *pMmVars, DebugAndPerfVars *pDpVars,
					 V2_F32 fTileMin, int32_t tile, FaceInfo baseFace) {
	//struct timeval start, stop;
	FaceBounds bounds;
	getFaceBounds(&bounds, pArgs->mesh.pUvs, baseFace);
	BaseTriVerts baseTri;
	for (int32_t i = 0; i < baseFace.size; ++i) {
		int32_t loop = baseFace.start + i;
		baseTri.uv[i] = _(pArgs->mesh.pUvs[loop] V2SUB fTileMin);
		baseTri.xyz[i] = pArgs->mesh.pVerts[pArgs->mesh.mesh.pLoops[loop]];
	}
	for (int32_t i = 0; i < pEcVars->pFaceCellsInfo[baseFace.index].faceSize; ++i) {
		////CLOCK_START;
		FaceInfo ruvmFace;
		ruvmFace.index = pEcVars->pCellFaces[i];
		ruvmFace.start = pArgs->pMap->mesh.mesh.pFaces[ruvmFace.index];
		ruvmFace.end = pArgs->pMap->mesh.mesh.pFaces[ruvmFace.index + 1];
		ruvmFace.size = ruvmFace.end - ruvmFace.start;
		////CLOCK_START;
		pArgs->averageRuvmFacesPerFace++;
		if (!checkFaceIsInBounds(_(bounds.fMin V2SUB fTileMin),
			                     _(bounds.fMax V2SUB fTileMin),
								 ruvmFace, &pArgs->pMap->mesh)) {
			continue;
		}
		int32_t faceWindingDir = calcFaceWindingDirection(baseFace, pArgs->mesh.pUvs);
		if (faceWindingDir == 2) {
			//face is degenerate
			continue;
		}
		////CLOCK_STOP_NO_PRINT;
		//pDpVars->timeSpent[1] += getTimeDiff(&start, &stop);
		LoopBufferWrap loopBuf = {0};
		loopBuf.size = ruvmFace.size;
		for (int32_t j = 0; j < ruvmFace.size; ++j) {
			int32_t vertIndex = pArgs->pMap->mesh.mesh.pLoops[ruvmFace.start + j];
			loopBuf.buf[j].preserve = 0;
			loopBuf.buf[j].isRuvm = 1;
			loopBuf.buf[j].baseLoop = (vertIndex + 1) * -1;
			loopBuf.buf[j].loop = pArgs->pMap->mesh.pVerts[vertIndex];
			loopBuf.buf[j].loop.d[0] += fTileMin.d[0];
			loopBuf.buf[j].loop.d[1] += fTileMin.d[1];
			loopBuf.buf[j].ruvmLoop = j;
			loopBuf.buf[j].normal =
				pArgs->pMap->mesh.pNormals[ruvmFace.start + j];
		}
		////CLOCK_STOP_NO_PRINT;
		//pDpVars->timeSpent[0] += getTimeDiff(&start, &stop);
		int32_t edgeFace = 0;
		int32_t onLine = 0;
		int32_t hasPreservedEdge = 0;
		int32_t seam = 0;
		clipRuvmFaceAgainstBaseFace(pArgs, baseFace, &loopBuf, &edgeFace,
		                            &hasPreservedEdge, &seam, faceWindingDir, &onLine);
		if (loopBuf.size <= 2) {
			continue;
		}
		transformClippedFaceFromUvToXyz(&loopBuf, baseFace, &baseTri,
		                                &pArgs->mesh, fTileMin, pMmVars);
		////CLOCK_START;
		addClippedFaceToBufMesh(pArgs, pMmVars, &loopBuf, edgeFace,
		                          ruvmFace, tile, baseFace, hasPreservedEdge,
								  seam, onLine);
		////CLOCK_STOP_NO_PRINT;
		//pDpVars->timeSpent[2] += getTimeDiff(&start, &stop);
	}
	//debugFaceIndex++;
	//printf("Total vert adj: %d %d %d - depth: %d %d\n", totalEmpty, totalComputed, vertAdjSize, maxDepth, *averageDepth);
	////CLOCK_START;
	//memset(ruvmVertAdj, 0, sizeof(VertAdj) * pMap->header.vertSize);
	////CLOCK_STOP_NO_PRINT;
	//timeSpent[2] += getTimeDiff(&start, &stop);
}
