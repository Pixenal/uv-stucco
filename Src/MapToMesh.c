#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <Context.h>
#include <MapToMesh.h>
#include <MapFile.h>

#define FLOAT_BC_MARGIN .0001f

static Vec3 calcIntersection(LoopBufferWrap *pLoopBuf, LoopInfo *pBaseLoop,
                             int32_t i, int32_t vertNextIndex, int32_t *pAlpha) {
	Vec3 *pRuvmVert = &pLoopBuf->buf[i].loop;
	Vec3 *pRuvmVertNext = &pLoopBuf->buf[vertNextIndex].loop;
	Vec3 ruvmDir = _(*pRuvmVert V3SUB *pRuvmVertNext);
	Vec3 ruvmDirBack = _(*pRuvmVertNext V3SUB *pRuvmVert);
	float t = (pRuvmVert->x - pBaseLoop->vert.x) * pBaseLoop->dirBack.y;
	t -= (pRuvmVert->y - pBaseLoop->vert.y) * pBaseLoop->dirBack.x;
	t /= ruvmDir.x * pBaseLoop->dirBack.y - ruvmDir.y * pBaseLoop->dirBack.x;
	float distance = sqrt(ruvmDir.x * ruvmDir.x + ruvmDir.y * ruvmDir.y);
	*pAlpha = t / distance;
	if (*pAlpha < .0f) {
		*pAlpha *= -1.0f;
	}
	return _(*pRuvmVert V3ADD _(ruvmDirBack V3MULS t));
}

static void clipRuvmFaceAgainstSingleLoop(LoopBufferWrap *pLoopBuf, LoopBufferWrap *pNewLoopBuf,
                                          int32_t *pInsideBuf, LoopInfo *pBaseLoop, Vec2 baseLoopCross,
								          int32_t *pEdgeFace, int32_t preserve, int32_t faceWindingDir,
										  int32_t *pOnLine) {
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		Vec2 ruvmVert = *(Vec2 *)&pLoopBuf->buf[i].loop;
		Vec2 uvRuvmDir = _(ruvmVert V2SUB pBaseLoop->vert);
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
					if (pLoopBuf->buf[i].loop.x == pBaseLoop->vert.x &&
						pLoopBuf->buf[i].loop.y == pBaseLoop->vert.y) {
						//on base vert
						onLineBase = (pBaseLoop->localIndex + 1) * -1;
					}
					else {
						//on next base vert
						onLineBase = (pBaseLoop->localIndexNext + 1) * -1;
					}
					pNewLoopBuf->buf[pNewLoopBuf->size].onLineBase = onLineBase;
				}
				else {
					//resides on base edge
					*pOnLine = 1;
					pNewLoopBuf->buf[pNewLoopBuf->size].onLine = 1;
					pNewLoopBuf->buf[pNewLoopBuf->size].onLineBase =
						pBaseLoop->localIndex + 1;
				}
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
				 pLoopBuf->buf[i].isBaseLoop < 0 || pLoopBuf->buf[vertNextIndex].isBaseLoop < 0)) {
				int32_t whichVert = pLoopBuf->buf[i].baseLoop == pBaseLoop->index - 1;
				pNewEntry->loop = calcIntersection(pLoopBuf, pBaseLoop,
				                                   i, vertNextIndex, &alpha);
				pNewEntry->isBaseLoop = whichVert ?
					pBaseLoop->localIndex : pBaseLoop->localIndexNext;
				pNewEntry->baseLoop = pNewEntry->isBaseLoop;
				pNewEntry->isBaseLoop += 1;
				pNewEntry->isBaseLoop *= -1;
				pNewEntry->seam = whichVert ?
					pBaseLoop->edgeIsSeam : pBaseLoop->edgeNextIsSeam;
				pNewEntry->preserve = preserve;
			}
			else {
				pNewEntry->loop = calcIntersection(pLoopBuf, pBaseLoop,
				                                   i, vertNextIndex, &alpha);
				pNewEntry->isBaseLoop = pBaseLoop->localIndex + 1;
				pNewEntry->baseLoop = pBaseLoop->index;
				pNewEntry->seam = pBaseLoop->edgeIsSeam;
				pNewEntry->preserve = preserve;
			}
			//pNewEntry->normal = vec3Lerp(pLoopBuf->buf[i].normal,
			//                             pLoopBuf->buf[vertNextIndex].normal,
			//                             alpha);
			//TODO add proper lerp for normal (why was the above commented out?)
			pNewEntry->normal = pLoopBuf->buf[i].normal;
			pNewEntry->index = -1;
			pNewEntry->refEdge = pBaseLoop->edgeIndex * -1; //negate, so that edge table entries are
															//different
			pNewEntry->fSort = ((pLoopBuf->buf[vertNextIndex].fSort - 
			                    pLoopBuf->buf[i].fSort) / 2) +
			                  pLoopBuf->buf[i].fSort;
			pNewEntry->sort = pLoopBuf->buf[vertNextIndex].sort;
			pNewLoopBuf->size++;
		}
	}
}

static int32_t calcFaceWindingDirection(FaceInfo face, RuvmAttrib *pUvs) {
	Vec2 centre = {0};
	for (int32_t i = 0; i < face.size; ++i) {
		_(&centre V2ADDEQL *attribAsV2(pUvs, face.start + i));
	}
	_(&centre V2DIVSEQL (float)face.size);
	return vec2WindingCompare(*attribAsV2(pUvs, face.start),
	                          *attribAsV2(pUvs, face.start + 1), centre, 0);
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
		baseLoop.vert = *attribAsV2(pArgs->mesh.pUvs, i + baseFace.start);
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
		baseLoop.vertNext = *attribAsV2(pArgs->mesh.pUvs, uvNextIndex);
		baseLoop.indexNext = uvNextIndexLocal;
		baseLoop.localIndex = i;
		baseLoop.localIndexNext = uvNextIndexLocal;
		baseLoop.dir = _(baseLoop.vertNext V2SUB baseLoop.vert);
		baseLoop.dirBack = _(baseLoop.vert V2SUB baseLoop.vertNext);
		LoopBufferWrap newLoopBuf = {0};
		int32_t insideBuf[12] = {0};
		Vec2 baseLoopCross = vec2Cross(baseLoop.dir);
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
											Mesh *pMeshIn, Vec2 tileMin,
											MapToMeshVars *pMmVars) {
	for (int32_t j = 0; j < pLoopBuf->size; ++j) {
		Vec3 vert = pLoopBuf->buf[j].loop;
		//uv is just the vert position before transform, so set that here
		pLoopBuf->buf[j].uv = *(Vec2 *)&vert;
		//find enclosing triangle
		_((Vec2 *)&vert V2SUBEQL tileMin);
		Vec3 vertBc = cartesianToBarycentric(pBaseTri->uv, &vert);
		if (baseFace.size == 4 && vertBc.y < 0) {
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
			Vec2 triBuf[3] =
				{pBaseTri->uv[2], pBaseTri->uv[3], pBaseTri->uv[0]};
			vertBc = cartesianToBarycentric(triBuf, &vert);
			pLoopBuf->buf[j].triLoops[0] = baseFace.start + 2;
			pLoopBuf->buf[j].triLoops[1] = baseFace.start + 3;
			pLoopBuf->buf[j].triLoops[2] = baseFace.start;
		}
		else {
			for (int32_t k = 0; k < 3; ++k) {
				pLoopBuf->buf[j].triLoops[k] = baseFace.start + k;
			}
		}
		int32_t *pTriLoops = pLoopBuf->buf[j].triLoops;
		Vec3 vertsXyz[3];
		for (int32_t i = 0; i < 3; ++i) {
			int32_t vertIndex = pMeshIn->mesh.pLoops[pTriLoops[i]];
			vertsXyz[i] = *attribAsV3(pMeshIn->pVerts, vertIndex);
		}
		//transform vertex
		pLoopBuf->buf[j].loop = barycentricToCartesian(vertsXyz, &vertBc);
		Vec3 normal = _(*attribAsV3(pMeshIn->pNormals, pTriLoops[0]) V3MULS vertBc.x);
		_(&normal V3ADDEQL _(*attribAsV3(pMeshIn->pNormals, pTriLoops[1]) V3MULS vertBc.y));
		_(&normal V3ADDEQL _(*attribAsV3(pMeshIn->pNormals, pTriLoops[2]) V3MULS vertBc.z));
		_(&normal V3DIVEQLS vertBc.x + vertBc.y + vertBc.z);
		_(&pLoopBuf->buf[j].loop V3ADDEQL _(normal V3MULS vert.z * 1.0f));
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
static void blendMapAndInAttribs(BufMesh *pBufMesh, RuvmAttrib *pDestAttribs, int32_t destAttribCount,
                                 RuvmAttrib *pMapAttribs, int32_t mapAttribCount,
								 RuvmAttrib *pMeshAttribs, int32_t meshAttribCount,
								 LoopBuffer *pLoopBuf, int32_t loopBufIndex,
								 int32_t dataIndex, int32_t mapDataIndex, int32_t meshDataIndex,
								 RuvmCommonAttrib *pCommonAttribs,
								 int32_t commonAttribCount) {
	//TODO make naming for MeshIn consistent
	for (int32_t i = 0; i < destAttribCount; ++i) {
		if (pDestAttribs[i].origin == RUVM_ATTRIB_ORIGIN_COMMON) {
			if (pDestAttribs + i == pBufMesh->pVerts ||
			    pDestAttribs + i == pBufMesh->pUvs ||
				pDestAttribs + i == pBufMesh->pNormals) {

				continue;
			}
			RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs[i].name,
											   pMapAttribs,
											   mapAttribCount);
			RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs[i].name,
											      pMeshAttribs,
											      meshAttribCount);
			RuvmAttribType type = pDestAttribs->type;
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
				                  pLoopBuf[loopBufIndex].triLoops[0],
								  pLoopBuf[loopBufIndex].triLoops[1],
								  pLoopBuf[loopBufIndex].triLoops[2],
								  pLoopBuf[loopBufIndex].bc);
			}
			RuvmCommonAttrib *pCommon =
				getCommonAttrib(pCommonAttribs, commonAttribCount,
			                    pDestAttribs->name);
			RuvmAttrib *orderTable[2];
			int8_t order = pCommon->blendConfig.order;
			orderTable[0] = order ? &mapBuf : &meshBuf;
			orderTable[1] = !order ? &mapBuf : &meshBuf;
			blendAttribs(pDestAttribs + i, dataIndex, orderTable[0], 0,
			             orderTable[1], 0, pCommon->blendConfig);
		}
		else if (pDestAttribs[i].origin == RUVM_ATTRIB_ORIGIN_MAP) {
			RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs[i].name,
											   pMapAttribs,
											   mapAttribCount);
			if (pMapAttrib->interpolate) {
				//temp memcpy until the above todo is handled
				memcpy(attribAsVoid(pDestAttribs + i, dataIndex),
				       attribAsVoid(pMapAttrib, mapDataIndex),
				       getAttribSize(pMapAttrib->type));
			}
			else {
				memcpy(attribAsVoid(pDestAttribs + i, dataIndex),
				       attribAsVoid(pMapAttrib, mapDataIndex),
				       getAttribSize(pMapAttrib->type));
			}
		}
		else if (pDestAttribs[i].origin == RUVM_ATTRIB_ORIGIN_MESH_IN) {
			RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs[i].name,
											      pMeshAttribs,
											      meshAttribCount);
			if (pMeshAttrib->interpolate) {
				//TODO skip interlopation is base loop? is it worth it? profile.
				interpolateAttrib(pDestAttribs + i,
								  dataIndex,
								  pMeshAttrib,
				                  pLoopBuf[loopBufIndex].triLoops[0],
								  pLoopBuf[loopBufIndex].triLoops[1],
								  pLoopBuf[loopBufIndex].triLoops[2],
								  pLoopBuf[loopBufIndex].bc);
			}
			else {
				memcpy(attribAsVoid(pDestAttribs + i, dataIndex),
				       attribAsVoid(pMeshAttrib, meshDataIndex),
				       getAttribSize(pMeshAttrib->type));
			}
		}
	}
}

static void simpleCopyAttribs(RuvmAttrib *pDestAttribs, int32_t destAttribCount,
                       RuvmAttrib *pMapAttribs, int32_t mapAttribCount,
					   RuvmAttrib *pMeshAttribs, int32_t meshAttribCount,
					   int32_t destDataIndex, int32_t srcDataIndex) {
	for (int32_t i = 0; i < destAttribCount; ++i) {
		int32_t indexOrigin = srcDataIndex >= 0;
		if (!indexOrigin) {
			srcDataIndex *= -1;
		}
		switch (pDestAttribs[i].origin) {
			case (RUVM_ATTRIB_ORIGIN_COMMON): {
				RuvmAttrib *pSrcAttrib;
				if (indexOrigin) {
					pSrcAttrib = getAttrib(pDestAttribs[i].name, pMapAttribs,
					                       mapAttribCount);
				}
				else {
					pSrcAttrib = getAttrib(pDestAttribs[i].name, pMeshAttribs,
					                       meshAttribCount);
				}
				break;
			}
			case (RUVM_ATTRIB_ORIGIN_MAP): {
				if (!indexOrigin) {
					//index is a meshIn index
					continue;
				}
				RuvmAttrib *pMapAttrib = getAttrib(pDestAttribs[i].name,
												   pMapAttribs,
												   mapAttribCount);
				memcpy(attribAsVoid(pDestAttribs + i, destDataIndex),
					   attribAsVoid(pMapAttrib, srcDataIndex),
					   getAttribSize(pMapAttrib->type));
				break;
			}
			case (RUVM_ATTRIB_ORIGIN_MESH_IN): {
				if (indexOrigin) {
					//index is a map index
					continue;
				}
				RuvmAttrib *pMeshAttrib = getAttrib(pDestAttribs[i].name,
													  pMeshAttribs,
													  meshAttribCount);
				memcpy(attribAsVoid(pDestAttribs + i, destDataIndex),
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
							   int32_t loopBufIndex) {
	if (refEdge >= 0) { //refEdge positive if a it's map edge,
						//and is negative if it's a meshIn edge,
		pEntry->edge = pBufMesh->mesh.edgeCount;
		pBufMesh->mesh.edgeCount++;
	}
	else {
		pEntry->edge = pBufMesh->boundaryEdgeSize;
		pBufMesh->boundaryEdgeSize--;
	}
	simpleCopyAttribs(pBufMesh->mesh.pEdgeAttribs, pBufMesh->mesh.edgeAttribCount,
	                  pArgs->pMap->mesh.mesh.pEdgeAttribs, pArgs->pMap->mesh.mesh.edgeAttribCount,
					  pArgs->mesh.mesh.pEdgeAttribs, pArgs->mesh.mesh.edgeAttribCount,
					  pEntry->edge, refEdge);
	pAcfVars->edgeIndex = pEntry->edge;
	pEntry->refEdge = refEdge;
	pEntry->refFace = refFace;
}

static void addEdge(ThreadArg *pArgs, int32_t loopBufIndex, BufMesh *pBufMesh,
                    LoopBuffer *pLoopBuf, MapToMeshVars *pMmVars, RuvmAllocator *pAlloc,
					int32_t refFace, AddClippedFaceVars *pAcfVars) {
	int32_t refEdge = pLoopBuf[loopBufIndex].refEdge;
	int32_t hash = ruvmFnvHash((uint8_t *)&refEdge, 4, pMmVars->edgeTableSize);
	MeshBufEdgeTable *pEntry = pMmVars->pEdgeTable + hash;
	pArgs->totalEdges++;
	do {
		if (!pEntry->loopCount) {
			initEdgeTableEntry(pArgs, pEntry, pAcfVars, pBufMesh, refEdge, refFace, pLoopBuf, loopBufIndex);
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
			initEdgeTableEntry(pArgs, pEntry,pAcfVars, pBufMesh, refEdge, refFace, pLoopBuf, loopBufIndex);
			break;
		}
		pEntry = pEntry->pNext;
	} while(1);
	pEntry->loopCount++;
}

static void addNewLoopAndOrVert(ThreadArg *pArgs, int32_t loopBufIndex, int32_t *pVertIndex,
                                BufMesh *pBufMesh, LoopBuffer *pLoopBuffer, int32_t loopIndex) {
		*pVertIndex = pBufMesh->boundaryVertSize;
		*attribAsV3(pArgs->bufMesh.pVerts, *pVertIndex) = pLoopBuffer[loopBufIndex].loop;
		pBufMesh->boundaryVertSize--;
		//temporarily setting mesh data index to 0, as it's only needed if interpolation is disabled
		blendMapAndInAttribs(pBufMesh, pBufMesh->mesh.pVertAttribs, pBufMesh->mesh.vertAttribCount,
							 pArgs->pMap->mesh.mesh.pVertAttribs, pArgs->pMap->mesh.mesh.vertAttribCount,
							 pArgs->mesh.mesh.pVertAttribs, pArgs->mesh.mesh.vertAttribCount,
							 pLoopBuffer, loopBufIndex, *pVertIndex, pLoopBuffer[loopBufIndex].sort, 0,
							 pArgs->pCommonAttribList->pVert, pArgs->pCommonAttribList->vertCount);
}

static void initVertAdjEntry(ThreadArg *pArgs, int32_t loopBufferIndex, int32_t *pVertIndex,
		 					 BufMesh *pBufMesh, LoopBuffer *pLoopBuffer, VertAdj *pVertAdj,
							 FaceInfo baseFace) {
	pVertAdj->mapVert = *pVertIndex;
	*pVertIndex = pBufMesh->mesh.vertCount++;
	pVertAdj->vert = *pVertIndex;
	pVertAdj->baseFace = baseFace.index;
	*attribAsV3(pArgs->bufMesh.pVerts, *pVertIndex) = pLoopBuffer[loopBufferIndex].loop;
	blendMapAndInAttribs(pBufMesh, pBufMesh->mesh.pVertAttribs, pBufMesh->mesh.vertAttribCount,
						 pArgs->pMap->mesh.mesh.pVertAttribs, pArgs->pMap->mesh.mesh.vertAttribCount,
						 pArgs->mesh.mesh.pVertAttribs, pArgs->mesh.mesh.vertAttribCount,
						 pLoopBuffer, loopBufferIndex, *pVertIndex, pLoopBuffer[loopBufferIndex].sort, 0,
						 pArgs->pCommonAttribList->pVert, pArgs->pCommonAttribList->vertCount);
}

static void addRuvmLoopAndOrVert(ThreadArg *pArgs, int32_t loopBufIndex, AddClippedFaceVars *pAcfVars,
                          BufMesh *pBufMesh, LoopBuffer *pLoopBufEntry,
						  MapToMeshVars *pMmVars, RuvmAllocator *pAlloc,
						  FaceInfo baseFace) {
	if (pAcfVars->firstRuvmVert < 0) {
		pAcfVars->firstRuvmVert = pLoopBufEntry[loopBufIndex].sort;
	}
	pAcfVars->lastRuvmVert = pLoopBufEntry[loopBufIndex].sort;
	uint32_t uVertIndex = pAcfVars->vertIndex;
	int32_t hash = ruvmFnvHash((uint8_t *)&uVertIndex, 4, pMmVars->vertAdjSize);
	VertAdj *pVertAdj = pMmVars->pRuvmVertAdj + hash;
	do {
		if (!pVertAdj->loopSize) {
			initVertAdjEntry(pArgs, loopBufIndex, &pAcfVars->vertIndex, pBufMesh,
			                 pLoopBufEntry, pVertAdj, baseFace);
			break;
		}
		//TODO should you be checking tile here as well?
		int32_t match = pVertAdj->mapVert == pAcfVars->vertIndex &&
		                pVertAdj->baseFace == baseFace.index;
		if (match) {
			pAcfVars->vertIndex = pVertAdj->vert;
			break;
		}
		if (!pVertAdj->pNext) {
			pVertAdj = pVertAdj->pNext = pAlloc->pCalloc(1, sizeof(VertAdj));
			initVertAdjEntry(pArgs, loopBufIndex, &pAcfVars->vertIndex, pBufMesh,
			                 pLoopBufEntry, pVertAdj, baseFace);
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
	//pEntry->firstVert = pAcfVars->firstRuvmVert;
	//pEntry->lastVert = pAcfVars->lastRuvmVert;
	pEntry->faceIndex = ruvmFaceIndex;
	pEntry->tile = tile;
	pEntry->job = pArgs->id;
	//pEntry->type = pAcfVars->ruvmLoops;
	pEntry->baseFace = baseFace.index;
	pEntry->hasPreservedEdge = hasPreservedEdge;
	pEntry->seam = seam;
	if (pLoopBuf->size > 11) {
		printf("----------------------   Loopbuf size exceeded 11\n");
		abort();
	}

	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		//pEntry->seams |= pLoopBuf->buf[i].seam << i;;
		//pEntry->fSorts[i] =  pLoopBuf->buf[i].fSort;
		pEntry->onLine |= (pLoopBuf->buf[i].onLine != 0) << i;
		pEntry->isRuvm |= (pLoopBuf->buf[i].index >= 0) << i;
		pEntry->sort |= pLoopBuf->buf[i].sort << i * 3;
		if (pLoopBuf->buf[i].onLine) {
			pLoopBuf->buf[i].isBaseLoop = pLoopBuf->buf[i].onLineBase;
		}
		else if (pLoopBuf->buf[i].isBaseLoop < .0f) {
			pLoopBuf->buf[i].isBaseLoop *= -1.0f;
			pEntry->onLine |= 1 << i;
		}
		pLoopBuf->buf[i].isBaseLoop -= 1;
		pLoopBuf->buf[i].isBaseLoop %= baseFace.size;
		pEntry->baseLoop |= pLoopBuf->buf[i].isBaseLoop << i * 2;
	}
	if (pLoopBuf->size > pArgs->maxLoopSize) {
		pArgs->maxLoopSize = pLoopBuf->size;
	}
	if (pAcfVars->firstRuvmVert < 0) {
		int32_t *pNonRuvmSort = (int32_t *)(pEntry + 1);
		for (int32_t i = 0; i < pLoopBuf->size; ++i) {
			pNonRuvmSort[i] = pLoopBuf->buf[i].sort;
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
	acfVars.firstRuvmVert = -1;
	acfVars.lastRuvmVert = -1;
	acfVars.ruvmLoops = 0;
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		acfVars.vertIndex = pLoopBuf->buf[i].index;
		pArgs->totalLoops++;
		acfVars.loopIndex = edgeFace || onLine ?
			pArgs->bufMesh.boundaryLoopSize-- : pArgs->bufMesh.mesh.loopCount++;
		int32_t refFace;
		if (acfVars.vertIndex >= 0) {
			acfVars.ruvmLoops++;
		}
		if (acfVars.vertIndex < 0 || pLoopBuf->buf[i].onLine) {
			addNewLoopAndOrVert(pArgs, i, &acfVars.vertIndex, &pArgs->bufMesh,
			                    pLoopBuf->buf, acfVars.loopIndex);
			refFace = ruvmFace.index;
		}
		else {
			addRuvmLoopAndOrVert(pArgs, i, &acfVars, &pArgs->bufMesh, pLoopBuf->buf,
			                     pMmVars, &pArgs->alloc, baseFace);
			refFace = baseFace.index;
		}
		pArgs->bufMesh.mesh.pLoops[acfVars.loopIndex] = acfVars.vertIndex;
		blendMapAndInAttribs(&pArgs->bufMesh, pArgs->bufMesh.mesh.pLoopAttribs, pArgs->bufMesh.mesh.loopAttribCount,
							 pArgs->pMap->mesh.mesh.pLoopAttribs, pArgs->pMap->mesh.mesh.loopAttribCount,
							 pArgs->mesh.mesh.pLoopAttribs, pArgs->mesh.mesh.loopAttribCount,
							 pLoopBuf->buf, i, acfVars.loopIndex, ruvmFace.start + pLoopBuf->buf[i].sort, baseFace.start,
							 pArgs->pCommonAttribList->pLoop, pArgs->pCommonAttribList->loopCount);
		addEdge(pArgs, i, &pArgs->bufMesh, pLoopBuf->buf, pMmVars, &pArgs->alloc,
		        refFace, &acfVars);
		pArgs->bufMesh.mesh.pEdges[acfVars.loopIndex] = acfVars.edgeIndex;
		*attribAsV3(pArgs->bufMesh.pNormals, acfVars.loopIndex) = pLoopBuf->buf[i].normal;
		*attribAsV2(pArgs->bufMesh.pUvs, acfVars.loopIndex) = pLoopBuf->buf[i].uv;
	}
	if (edgeFace || onLine) {
		addEdgeFaceToBoundaryBuffer(pArgs, &acfVars, pLoopBuf,
		                            ruvmFace.index, tile, baseFace,
									hasPreservedEdge, seam);
	}
	else {
		pArgs->bufMesh.mesh.pFaces[pArgs->bufMesh.mesh.faceCount] = acfVars.loopStart;
		blendMapAndInAttribs(&pArgs->bufMesh, pArgs->bufMesh.mesh.pFaceAttribs, pArgs->bufMesh.mesh.faceAttribCount,
							 pArgs->pMap->mesh.mesh.pFaceAttribs, pArgs->pMap->mesh.mesh.faceAttribCount,
							 pArgs->mesh.mesh.pFaceAttribs, pArgs->mesh.mesh.faceAttribCount,
							 pLoopBuf->buf, 0, acfVars.loopIndex, ruvmFace.index, baseFace.index,
							 pArgs->pCommonAttribList->pFace, pArgs->pCommonAttribList->faceCount);
		pArgs->bufMesh.mesh.faceCount++;
	}
}

void ruvmMapToSingleFace(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                     MapToMeshVars *pMmVars, DebugAndPerfVars *pDpVars,
					 Vec2 fTileMin, int32_t tile, FaceInfo baseFace) {
	//struct timeval start, stop;
	FaceBounds bounds;
	getFaceBounds(&bounds, pArgs->mesh.pUvs, baseFace);
	BaseTriVerts baseTri;
	for (int32_t i = 0; i < baseFace.size; ++i) {
		int32_t loop = baseFace.start + i;
		baseTri.uv[i] =
			_(*attribAsV2(pArgs->mesh.pUvs, loop) V2SUB fTileMin);
		baseTri.xyz[i] =
			*attribAsV3(pArgs->mesh.pVerts, pArgs->mesh.mesh.pLoops[loop]);
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
			loopBuf.buf[j].seam = 0;
			loopBuf.buf[j].preserve = 0;
			loopBuf.buf[j].refEdge = pArgs->pMap->mesh.mesh.pEdges[ruvmFace.start + j];
			loopBuf.buf[j].index = vertIndex;
			loopBuf.buf[j].baseLoop = vertIndex;
			loopBuf.buf[j].loop = *attribAsV3(pArgs->pMap->mesh.pVerts, vertIndex);
			loopBuf.buf[j].loop.x += fTileMin.x;
			loopBuf.buf[j].loop.y += fTileMin.y;
			loopBuf.buf[j].sort = j;
			loopBuf.buf[j].fSort = j * 100;
			loopBuf.buf[j].normal =
				*attribAsV3(pArgs->pMap->mesh.pNormals, ruvmFace.start + j);
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
