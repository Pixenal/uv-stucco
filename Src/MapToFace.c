#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>

#include <Context.h>
#include <MapToJobMesh.h>
#include <MapFile.h>
#include <MathUtils.h>
#include <AttribUtils.h>
#include <Utils.h>
#include <Error.h>

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

typedef struct {
	int32_t index;
	int32_t edgeIndex;
	int32_t edgeIndexNext;
	int8_t edgeIsSeam;
	int8_t edgeNextIsSeam;
	int32_t indexNext;
	int8_t localIndex;
	int8_t localIndexNext;
	V2_F32 vert;
	V2_F32 vertNext;
	V2_F32 dir;
	V2_F32 dirBack;
} LoopInfo;

typedef struct {
	int32_t loopStart;
	int32_t face;
	int32_t loop;
	int32_t  edge;
	int32_t  vert;
} AddClippedFaceVars;

typedef struct {
	V2_F32 uv[4];
	V3_F32 xyz[4];
} BaseTriVerts;

static
V3_F32 calcIntersection(LoopBufWrap *pLoopBuf, LoopInfo *pBaseLoop,
                        int32_t i, int32_t vertNextIndex, int32_t *pAlpha) {
	V3_F32 *pRuvmVert = &pLoopBuf->buf[i].loop;
	V3_F32 *pRuvmVertNext = &pLoopBuf->buf[vertNextIndex].loop;
	V3_F32 ruvmDir = _(*pRuvmVert V3SUB *pRuvmVertNext);
	V3_F32 ruvmDirBack = _(*pRuvmVertNext V3SUB *pRuvmVert);
	float t;
	t = (pRuvmVert->d[0] - pBaseLoop->vert.d[0]) * pBaseLoop->dirBack.d[1];
	t -= (pRuvmVert->d[1] - pBaseLoop->vert.d[1]) * pBaseLoop->dirBack.d[0];
	t /= ruvmDir.d[0] * pBaseLoop->dirBack.d[1] -
         ruvmDir.d[1] * pBaseLoop->dirBack.d[0];
	float distance =
		sqrt(ruvmDir.d[0] * ruvmDir.d[0] + ruvmDir.d[1] * ruvmDir.d[1]);
	*pAlpha = t / distance;
	if (*pAlpha < .0f) {
		*pAlpha *= -1.0f;
	}
	return _(*pRuvmVert V3ADD _(ruvmDirBack V3MULS t));
}

static
bool checkIfOnVert(LoopBufWrap *pLoopBuf, int32_t i, int32_t iNext) {
	return (pLoopBuf->buf[i].baseLoop == pLoopBuf->buf[iNext].baseLoop ||
	        pLoopBuf->buf[i].isBaseLoop || pLoopBuf->buf[iNext].isBaseLoop) &&
		    !pLoopBuf->buf[i].isRuvm && !pLoopBuf->buf[iNext].isRuvm;
}

static
void addInsideLoopToBuf(LoopBufWrap *pNewLoopBuf, LoopBufWrap *pLoopBuf,
                        int32_t *pInsideBuf, int32_t i, int32_t iNext, LoopInfo *pBaseLoop,
						int32_t *pOnLine) {
	pNewLoopBuf->buf[pNewLoopBuf->size] = pLoopBuf->buf[i];
	if (pInsideBuf[i] == 2) {
		//pNewLoopBuf->buf[pNewLoopBuf->size].onLine = true;
	}
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
			pNewLoopBuf->buf[pNewLoopBuf->size].isBaseLoop = true;
		}
		else if (pLoopBuf->buf[i].isRuvm) {
			//resides on base edge
			pNewLoopBuf->buf[pNewLoopBuf->size].baseLoop =
				pBaseLoop->localIndex;
		}
		*pOnLine = 1;
		pNewLoopBuf->buf[pNewLoopBuf->size].onLine = 1;
	}
	pNewLoopBuf->size++;
}

static
void addIntersectionToBuf(LoopBufWrap *pNewLoopBuf, LoopBufWrap *pLoopBuf,
                          int32_t i, int32_t *pEdgeFace, LoopInfo *pBaseLoop,
						  int32_t vertNextIndex, int32_t preserve,
                          bool flippedWind) {
	int32_t alpha = 0;
	*pEdgeFace += 1;
	LoopBuf *pNewEntry = pNewLoopBuf->buf + pNewLoopBuf->size;
	if (checkIfOnVert(pLoopBuf, i, vertNextIndex)) {
		int32_t lastBaseLoop = flippedWind ?
			pBaseLoop->index + 1 : pBaseLoop->index - 1;
		int32_t whichVert = pLoopBuf->buf[i].baseLoop == lastBaseLoop;
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

static
void clipRuvmFaceAgainstSingleLoop(LoopBufWrap *pLoopBuf, LoopBufWrap *pNewLoopBuf,
                                   int32_t *pInsideBuf, LoopInfo *pBaseLoop,
								   V2_F32 baseLoopCross, int32_t *pEdgeFace,
								   int32_t preserve, bool flippedWind,
								   int32_t *pOnLine) {
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		V2_F32 ruvmVert = *(V2_F32 *)&pLoopBuf->buf[i].loop;
		V2_F32 uvRuvmDir = _(ruvmVert V2SUB pBaseLoop->vert);
		float dot = _(baseLoopCross V2DOT uvRuvmDir);
		_Bool onLine = dot == .0f;
		pInsideBuf[i] = onLine ? -1 : (dot < .0f) ^ flippedWind;
	}
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		int32_t vertNextIndex = (i + 1) % pLoopBuf->size;
		if (pInsideBuf[i]) {
			//point is inside, or on the line
			addInsideLoopToBuf(pNewLoopBuf, pLoopBuf, pInsideBuf, i, vertNextIndex, pBaseLoop,
			                   pOnLine);
		}
		if (pInsideBuf[i] != 0 ^ pInsideBuf[vertNextIndex] != 0 &&
		    pInsideBuf[i] >= 0 && pInsideBuf[vertNextIndex] >= 0) {
			//the current point is inside, but the next is not (or visa versa),
			//so calc intersection point. The != and ^ are to account for the
			//fact that insideBuf can be negative if the point is on the line.
			//The != converts the value to absolute, thus ignoring this.
			addIntersectionToBuf(pNewLoopBuf, pLoopBuf, i, pEdgeFace, pBaseLoop,
			                     vertNextIndex, preserve, flippedWind);
		}
	}
}

static
int32_t calcFaceWindingDirection(FaceRange face, V2_F32 *pUvs) {
	V2_F32 centre = {0};
	for (int32_t i = 0; i < face.size; ++i) {
		_(&centre V2ADDEQL pUvs[face.start + i]);
	}
	_(&centre V2DIVSEQL (float)face.size);
	return v2WindingCompare(pUvs[face.start],
	                          pUvs[face.start + 1], centre, 0);
}

static
int32_t calcMapFaceWindingDirection(LoopBufWrap *pLoopBuf) {
	V2_F32 centre = { 0 };
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		_(&centre V2ADDEQL *(V2_F32 *)&pLoopBuf->buf[i].loop);
	}
	_(&centre V2DIVSEQL(float)pLoopBuf->size);
	//TODO This can fail on concave faces, use average wind instead maybe?
	return v2WindingCompare(*(V2_F32*)&pLoopBuf->buf[0].loop,
	                        *(V2_F32*)&pLoopBuf->buf[1].loop, centre, 0);
}

static
void loopBufDecrementBaseLoops(LoopBufWrap* pLoopBuf, FaceRange* pBaseFace) {
	for (int i = 0; i < pLoopBuf->size; ++i) {
		int8_t* pBaseLoop = &pLoopBuf->buf[i].baseLoop;
		*pBaseLoop = *pBaseLoop ? *pBaseLoop - 1 : pBaseFace->size - 1;
	}
}

static
void clipRuvmFaceAgainstBaseFace(MappingJobVars *pVars, FaceRange baseFace,
                                 LoopBufWrap *pLoopBuf, int32_t *pEdgeFace,
								 int32_t *pHasPreservedEdge, int32_t *pSeam,
								 int32_t faceWindingDir, int32_t mapFaceWindDir,
                                 int32_t *pOnLine) {
	bool flippedWind = !faceWindingDir || !mapFaceWindDir;
	int32_t start = mapFaceWindDir ? 0 : baseFace.size - 1;
	for (int32_t i = start; mapFaceWindDir ? i < baseFace.size : i >= 0; mapFaceWindDir ? ++i : --i) {
		LoopInfo baseLoop;
		baseLoop.index = i;
		baseLoop.edgeIndex = pVars->mesh.mesh.pEdges[baseFace.start + i];
		baseLoop.edgeIsSeam = checkIfEdgeIsSeam(baseLoop.edgeIndex, baseFace, i,
			                                    &pVars->mesh, pVars->pEdgeVerts);
		int8_t preserveEdge[2];
		preserveEdge[0] = checkIfEdgeIsPreserve(&pVars->mesh, baseLoop.edgeIndex);
		baseLoop.vert = pVars->mesh.pUvs[i + baseFace.start];
		int32_t uvNextIndexLocal;
		if (mapFaceWindDir) {
			uvNextIndexLocal = ((i + 1) % baseFace.size);
		}
		else {
			uvNextIndexLocal = i ? i - 1 : baseFace.size - 1;
		}
		int32_t uvNextIndex = uvNextIndexLocal + baseFace.start;
		baseLoop.edgeIndexNext = pVars->mesh.mesh.pEdges[uvNextIndex];
		baseLoop.edgeNextIsSeam =
			checkIfEdgeIsSeam(baseLoop.edgeIndexNext, baseFace, uvNextIndexLocal,
			                  &pVars->mesh, pVars->pEdgeVerts);
		/*
		if (preserveEdge[0] && !baseLoop.edgeIsSeam && !pVars->pInVertTable[vertIndex]) {
			pVars->pInVertTable[vertIndex] = 1;
		}
		//cap at 3 to avoid integer overflow
		else if (baseLoop.edgeIsSeam && pVars->pVertSeamTable[vertIndex] < 3) {
			pVars->pVertSeamTable[vertIndex]++;
		}
		*/
		/*
		preserveEdge[1] = checkIfEdgeIsPreserve(&pVars->mesh, baseLoop.edgeIndexNext);
		if (preserveEdge[1] && !baseLoop.edgeNextIsSeam) {
			int32_t nextVertIndex = pVars->mesh.pLoops[uvNextIndex];
			pVars->pInVertTable[nextVertIndex] = 1;
		}
		*/
		baseLoop.vertNext = pVars->mesh.pUvs[uvNextIndex];
		baseLoop.indexNext = uvNextIndexLocal;
		baseLoop.localIndex = i;
		baseLoop.localIndexNext = uvNextIndexLocal;
		baseLoop.dir = _(baseLoop.vertNext V2SUB baseLoop.vert);
		baseLoop.dirBack = _(baseLoop.vert V2SUB baseLoop.vertNext);
		LoopBufWrap newLoopBuf = {0};
		int32_t insideBuf[12] = {0};
		V2_F32 baseLoopCross = v2Cross(baseLoop.dir);
		int32_t edgeFacePre = *pEdgeFace;
		clipRuvmFaceAgainstSingleLoop(pLoopBuf, &newLoopBuf, insideBuf,
		         				      &baseLoop, baseLoopCross, pEdgeFace,
									  preserveEdge[0], flippedWind, pOnLine);
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
		memcpy(pLoopBuf->buf, newLoopBuf.buf, sizeof(LoopBuf) * newLoopBuf.size);
		pLoopBuf->size = newLoopBuf.size;
	}
	if (!mapFaceWindDir) {
		loopBufDecrementBaseLoops(pLoopBuf, &baseFace);
	}
}

static
V3_F32 getLoopRealNormal(Mesh *pMesh, FaceRange *pFace, int32_t loop) {
	int32_t a = loop == 0 ? pFace->size - 1 : loop - 1;
	int32_t c = (loop + 1) % pFace->size;
	int32_t aIndex = pMesh->mesh.pLoops[pFace->start + a];
	int32_t bIndex = pMesh->mesh.pLoops[pFace->start + loop];
	int32_t cIndex = pMesh->mesh.pLoops[pFace->start + c];
	V3_F32 ba = _(pMesh->pVerts[aIndex] V3SUB pMesh->pVerts[bIndex]);
	V3_F32 bc = _(pMesh->pVerts[cIndex] V3SUB pMesh->pVerts[bIndex]);
	return v3Normalize(_(ba V3CROSS bc));
}

static
void transformClippedFaceFromUvToXyz(LoopBufWrap *pLoopBuf, FaceRange ruvmFace,
									 FaceRange baseFace, BaseTriVerts *pBaseTri,
									 MappingJobVars *pVars, V2_F32 tileMin) {
	Mesh *pMapMesh = &pVars->pMap->mesh;
	if (pMapMesh->pUsg) {
		for (int32_t i = 0; i < pLoopBuf->size; ++i) {

		}
	}
	//replace j, k, l, etc, in code that was moved to a func, but not updated,
	//eg, the below loop should use i, not j
	for (int32_t j = 0; j < pLoopBuf->size; ++j) {
		V3_F32 vert = pLoopBuf->buf[j].loop;
		//uv is just the vert position before transform, so set that here
		pLoopBuf->buf[j].uv = *(V2_F32 *)&vert;
		//find enclosing triangle
		_((V2_F32 *)&vert V2SUBEQL tileMin);
		V3_F32 vertBc = getBarycentricInFace(pBaseTri->uv, pLoopBuf->buf[j].triLoops,
		                                     baseFace.size, *(V2_F32 *)&vert);
		int8_t *pTriLoops = pLoopBuf->buf[j].triLoops;
		V3_F32 vertsXyz[3];
		for (int32_t i = 0; i < 3; ++i) {
			int32_t vertIndex =
				pVars->mesh.mesh.pLoops[baseFace.start + pTriLoops[i]];
			vertsXyz[i] = pVars->mesh.pVerts[vertIndex];
		}
		//transform vertex
		pLoopBuf->buf[j].loop = barycentricToCartesian(vertsXyz, &vertBc);
		pLoopBuf->buf[j].bc = vertBc;
		V3_F32 *pInNormals = pVars->mesh.pNormals;
		V3_F32 normal =
			_(pInNormals[baseFace.start + pTriLoops[0]] V3MULS vertBc.d[0]);
		_(&normal V3ADDEQL
				_(pInNormals[baseFace.start + pTriLoops[1]] V3MULS vertBc.d[1]));
		_(&normal V3ADDEQL
				_(pInNormals[baseFace.start + pTriLoops[2]] V3MULS vertBc.d[2]));
		_(&normal V3DIVEQLS vertBc.d[0] + vertBc.d[1] + vertBc.d[2]);
		if (pMapMesh->pUsg && pLoopBuf->buf[j].isRuvm) {
			int32_t mapLoop = pVars->pMap->mesh.mesh.pLoops[ruvmFace.start + pLoopBuf->buf[j].ruvmLoop];
			int32_t usg = pMapMesh->pUsg[mapLoop];
			RUVM_ASSERT("", usg >= 0);
			if (usg) {
				usg--;
				uint32_t sum = usg + baseFace.index;
				int32_t hash = ruvmFnvHash((uint8_t *)&sum, 4, pVars->pMap->usgArr.tableSize);
				UsgInFace* pEntry = pVars->pMap->usgArr.pInFaceTable + hash;
				do {
					if (pEntry->face == baseFace.index && pEntry->pEntry->usg == usg) {
						break;
					}
					pEntry = pEntry->pNext;
				} while (pEntry);
				//RUVM_ASSERT("", pEntry);
				if (pEntry) {
					V3_F32 mapRealNormal = getLoopRealNormal(pMapMesh, &ruvmFace, pLoopBuf->buf[j].ruvmLoop);
					V3_F32 up = { .0f, .0f, 1.0f };
					float upMask = abs(_(mapRealNormal V3DOT up));
					//pLoopBuf->buf[j].projNormalMasked = v3Normalize(v3Lerp(normal, pEntry->pEntry->normal, upMask));
					pLoopBuf->buf[j].projNormalMasked = pEntry->pEntry->normal;
					normal = pEntry->pEntry->normal;
				}
			}
		}
		pLoopBuf->buf[j].projNormal = normal;
		pLoopBuf->buf[j].z = vert.d[2];
	}
	for (int32_t j = 0; j < pLoopBuf->size; ++j) {
		float uvScale = fabs(pBaseTri->uv[0].d[0] - pBaseTri->uv[1].d[0]);
		float xyzScale = fabs(pBaseTri->xyz[0].d[0] - pBaseTri->xyz[1].d[0]);
		float scale = xyzScale / uvScale;
		_(&pLoopBuf->buf[j].loop V3ADDEQL _(pLoopBuf->buf[j].projNormal V3MULS pLoopBuf->buf[j].z * scale));
		//transform normal from tangent space to object space
		//TODO only multiply by TBN if an option is set to use map normals,
		//otherwise just use the above interpolated
		Mat3x3 tbn = pVars->tbn;
		V3_F32 normal;
		if (pLoopBuf->buf[j].isRuvm) {
			normal = pLoopBuf->buf[j].projNormalMasked;
		}
		else {
			normal = pLoopBuf->buf[j].projNormal;
		}
		//TODO your currently using a single tbn per face, which is going to give flat faces on dense maps.
		//     Generate tangents per vertex, and interpolate across the face, and build a tbn using that instead.
		//     Presumably using mikktspace.
		pLoopBuf->buf[j].normal = _(pLoopBuf->buf[j].normal V3MULM3X3 &tbn);

	}
}

//NOTE map and mesh date index params are only used if interpolation is not enabled
//for the attrib. This is always the case on faces.
//Except for right now, because I havn't implemented map triangulation and interpolation,
//so the map data index is used temporarily until that's done.
static
void blendMapAndInAttribs(BufMesh *pBufMesh, AttribArray *pDestAttribs,
                          AttribArray *pMapAttribs, AttribArray *pMeshAttribs,
						  LoopBuf *pLoopBuf, int32_t loopBufIndex,
						  int32_t dataIndex, int32_t mapDataIndex,
						  int32_t meshDataIndex, RuvmCommonAttrib *pCommonAttribs,
						  int32_t commonAttribCount, FaceRange *pBaseFace) {
	//TODO make naming for MeshIn consistent
	for (int32_t i = 0; i < pDestAttribs->count; ++i) {
		if (pDestAttribs->pArr[i].origin == RUVM_ATTRIB_ORIGIN_COMMON) {
			if (pDestAttribs->pArr + i == asMesh(pBufMesh)->pVertAttrib ||
			    pDestAttribs->pArr + i == asMesh(pBufMesh)->pUvAttrib ||
				pDestAttribs->pArr + i == asMesh(pBufMesh)->pNormalAttrib) {

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
				interpolateAttrib(
					&meshBuf,
					0,
					pMeshAttrib,
				    pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[0],
					pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[1],
					pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[2],
					pLoopBuf[loopBufIndex].bc
				);
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
				interpolateAttrib(
					pDestAttribs->pArr + i,
					dataIndex,
					pMeshAttrib,
				    pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[0],
					pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[1],
					pBaseFace->start + pLoopBuf[loopBufIndex].triLoops[2],
					pLoopBuf[loopBufIndex].bc
				);
			}
			else {
				memcpy(attribAsVoid(pDestAttribs->pArr + i, dataIndex),
				       attribAsVoid(pMeshAttrib, meshDataIndex),
				       getAttribSize(pMeshAttrib->type));
			}
		}
	}
}

static
void simpleCopyAttribs(AttribArray *pDestAttribs, AttribArray *pMapAttribs,
					   AttribArray *pMeshAttribs, int32_t destDataIndex,
					   int32_t srcDataIndex, int32_t indexOrigin) {
	for (int32_t i = 0; i < pDestAttribs->count; ++i) {
		switch (pDestAttribs->pArr[i].origin) {
			case (RUVM_ATTRIB_ORIGIN_COMMON): {
				RuvmAttrib *pSrcAttrib;
				if (indexOrigin) {
					pSrcAttrib =
						getAttrib(pDestAttribs->pArr[i].name, pMapAttribs);
				}
				else {
					pSrcAttrib =
						getAttrib(pDestAttribs->pArr[i].name, pMeshAttribs);
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

static
void initEdgeTableEntry(MappingJobVars *pVars, LocalEdge *pEntry,
                        AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						int32_t refEdge, int32_t refFace, int32_t isMapEdge) {
	BufMeshIndex edge = bufMeshAddEdge(&pVars->alloc, pBufMesh, !isMapEdge, pVars->pDpVars);
	pAcfVars->edge = edge.index;
	pEntry->edge = edge.index;
	simpleCopyAttribs(&asMesh(pBufMesh)->mesh.edgeAttribs,
	                  &pVars->pMap->mesh.mesh.edgeAttribs,
					  &pVars->mesh.mesh.edgeAttribs,
					  edge.realIndex, refEdge, isMapEdge);
	pEntry->refEdge = refEdge;
	pEntry->refFace = refFace;
}

static
int32_t getRefEdge(MappingJobVars *pVars, FaceRange *pRuvmFace,
                   FaceRange *pBaseFace, LoopBuf *pLoopBuf,
				   int32_t loopBufIndex) {
	if (pLoopBuf[loopBufIndex].isRuvm) {
		int32_t ruvmLoop = pLoopBuf[loopBufIndex].ruvmLoop;
		return pVars->pMap->mesh.mesh.pEdges[pRuvmFace->start + ruvmLoop];
	}
	else {
		int32_t baseLoop = pLoopBuf[loopBufIndex].baseLoop;
		return pVars->mesh.mesh.pEdges[pBaseFace->start + baseLoop];
	}
}

static
void addEdge(MappingJobVars *pVars, int32_t loopBufIndex, BufMesh *pBufMesh,
             LoopBuf *pLoopBuf, RuvmAlloc *pAlloc, int32_t refFace,
			 AddClippedFaceVars *pAcfVars, FaceRange *pBaseFace,
			 FaceRange *pRuvmFace) {
	int32_t refEdge =
		getRefEdge(pVars, pRuvmFace, pBaseFace, pLoopBuf, loopBufIndex);
	int32_t isMapEdge = pLoopBuf[loopBufIndex].isRuvm;
	int32_t key = isMapEdge ? refEdge : (refEdge + 1) * -1;
	int32_t hash =
		ruvmFnvHash((uint8_t *)&key, 4, pVars->localTables.edgeTableSize);
	LocalEdge *pEntry = pVars->localTables.pEdgeTable + hash;
	int32_t depth = 0;
	do {
		if (!pEntry->loopCount) {
			initEdgeTableEntry(pVars, pEntry, pAcfVars, pBufMesh, refEdge,
			                   refFace, isMapEdge);
			break;
		}
		int32_t match = pEntry->refEdge == refEdge &&
		                pEntry->refFace == refFace;
		if (match) {
			pAcfVars->edge = pEntry->edge;
			break;
		}
		if (!pEntry->pNext) {
			pEntry = pEntry->pNext = pAlloc->pCalloc(1, sizeof(LocalEdge));
			initEdgeTableEntry(pVars, pEntry,pAcfVars, pBufMesh, refEdge,
			                   refFace, isMapEdge);
			break;
		}
		RUVM_ASSERT("", pEntry->pNext && pEntry->pNext->pNext <= (LocalEdge *)1000000000000000);
		pEntry = pEntry->pNext;
		RUVM_ASSERT("", depth >= 0 && depth < 1000);
		depth++;
	} while(1);
	pEntry->loopCount++;
}

static
void addNewLoopAndOrVert(MappingJobVars *pVars, int32_t loopBufIndex,
                         AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						 LoopBuf *pLoopBuf, FaceRange *pBaseFace) {
		BufMeshIndex vert = bufMeshAddVert(&pVars->alloc, pBufMesh, true, pVars->pDpVars);
		pAcfVars->vert = vert.index;
		asMesh(pBufMesh)->pVerts[vert.realIndex] = pLoopBuf[loopBufIndex].loop;
		//temporarily setting mesh data index to 0, as it's only needed if interpolation is disabled
		blendMapAndInAttribs(
			pBufMesh, &asMesh(pBufMesh)->mesh.vertAttribs,
			&pVars->pMap->mesh.mesh.vertAttribs,
			&pVars->mesh.mesh.vertAttribs,
			pLoopBuf, loopBufIndex, vert.realIndex,
			pLoopBuf[loopBufIndex].ruvmLoop, 0,
			pVars->pCommonAttribList->pVert,
			pVars->pCommonAttribList->vertCount,
			pBaseFace
		);
}

static
void initMapVertTableEntry(MappingJobVars *pVars, int32_t loopBufIndex,
                           AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						   LoopBuf *pLoopBuf, LocalVert *pEntry,
						   FaceRange baseFace, int32_t ruvmVert) {
	BufMeshIndex vert = bufMeshAddVert(&pVars->alloc, pBufMesh, false, pVars->pDpVars);
	pAcfVars->vert = vert.index;
	asMesh(pBufMesh)->pVerts[vert.realIndex] = pLoopBuf[loopBufIndex].loop;
	pEntry->vert = vert.index;
	pEntry->mapVert = ruvmVert;
	pEntry->baseFace = baseFace.index;
	blendMapAndInAttribs(pBufMesh, &asMesh(pBufMesh)->mesh.vertAttribs,
						 &pVars->pMap->mesh.mesh.vertAttribs,
						 &pVars->mesh.mesh.vertAttribs,
						 pLoopBuf, loopBufIndex, vert.realIndex,
						 pLoopBuf[loopBufIndex].ruvmLoop, 0,
						 pVars->pCommonAttribList->pVert,
						 pVars->pCommonAttribList->vertCount,
						 &baseFace);
}

static
void addRuvmLoopAndOrVert(MappingJobVars *pVars, int32_t loopBufIndex,
                          AddClippedFaceVars *pAcfVars, BufMesh *pBufMesh,
						  LoopBuf *pLoopBufEntry, RuvmAlloc *pAlloc,
						  FaceRange baseFace, FaceRange *pRuvmFace) {
	int32_t ruvmLoop = pRuvmFace->start + pLoopBufEntry[loopBufIndex].ruvmLoop;
	uint32_t uRuvmVert = pVars->pMap->mesh.mesh.pLoops[ruvmLoop];
	int32_t hash =
		ruvmFnvHash((uint8_t *)&uRuvmVert, 4, pVars->localTables.vertTableSize);
	LocalVert *pEntry = pVars->localTables.pVertTable + hash;
	do {
		if (!pEntry->loopSize) {
			initMapVertTableEntry(pVars, loopBufIndex, pAcfVars,
			                      pBufMesh, pLoopBufEntry, pEntry, baseFace,
								  uRuvmVert);
			break;
		}
		//TODO should you be checking tile here as well?
		int32_t match = pEntry->mapVert == uRuvmVert &&
		                pEntry->baseFace == baseFace.index;
		if (match) {
			pAcfVars->vert = pEntry->vert;
			break;
		}
		if (!pEntry->pNext) {
			pEntry = pEntry->pNext = pAlloc->pCalloc(1, sizeof(LocalVert));
			initMapVertTableEntry(pVars, loopBufIndex, pAcfVars,
			                      pBufMesh, pLoopBufEntry, pEntry, baseFace,
								  uRuvmVert);
			break;
		}
		pEntry = pEntry->pNext;
	} while (1);
	pEntry->loopSize++;
}

static
void initBorderTableEntry(MappingJobVars *pVars, AddClippedFaceVars *pAcfVars,
                          BorderFace *pEntry, int32_t ruvmFaceIndex,
                          int32_t tile, LoopBufWrap *pLoopBuf, FaceRange baseFace,
						  int32_t hasPreservedEdge, int32_t seam) {
	pEntry->face = pAcfVars->face;
	pEntry->faceIndex = ruvmFaceIndex;
	pEntry->tile = tile;
	pEntry->job = pVars->id;
	pEntry->baseFace = baseFace.index;
	pEntry->hasPreservedEdge = hasPreservedEdge;
	pEntry->seam = seam;

	RUVM_ASSERT("", pLoopBuf->size <= 12);
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		RUVM_ASSERT("", (pLoopBuf->buf[i].onLine & 0x01) ==
		                pLoopBuf->buf[i].onLine);
		pEntry->onLine |= (pLoopBuf->buf[i].onLine != 0) << i;
		RUVM_ASSERT("", (pLoopBuf->buf[i].isRuvm & 0x01) ==
		                pLoopBuf->buf[i].isRuvm);
		pEntry->isRuvm |= (pLoopBuf->buf[i].isRuvm) << i;
		RUVM_ASSERT("", (pLoopBuf->buf[i].ruvmLoop & 0x07) ==
		                pLoopBuf->buf[i].ruvmLoop);
		pEntry->ruvmLoop |= pLoopBuf->buf[i].ruvmLoop << i * 3;
		RUVM_ASSERT("", (pLoopBuf->buf[i].isBaseLoop & 0x01) ==
		                pLoopBuf->buf[i].isBaseLoop);
		pEntry->onInVert |= pLoopBuf->buf[i].isBaseLoop << i;
		if (pLoopBuf->buf[i].isRuvm && !pLoopBuf->buf[i].onLine) {
			// Only add baseloop for ruvm if online, otherwise value will
			// will not fit within 2 bits
			continue;
		}
		RUVM_ASSERT("", (pLoopBuf->buf[i].baseLoop & 0x03) ==
		                pLoopBuf->buf[i].baseLoop);
		pEntry->baseLoop |= pLoopBuf->buf[i].baseLoop << i * 2;
	}
}


static
void addFaceToBorderTable(MappingJobVars *pVars, AddClippedFaceVars *pAcfVars,
                          LoopBufWrap *pLoopBuf, int32_t ruvmFaceIndex,
						  int32_t tile, FaceRange baseFace, int32_t hasPreservedEdge,
						  int32_t seam) {
	int32_t hash =
		ruvmFnvHash((uint8_t *)&ruvmFaceIndex, 4, pVars->borderTable.size);
	BorderBucket *pBucket = pVars->borderTable.pTable + hash;
	BorderFace *pEntry = pBucket->pEntry;
	if (!pEntry) {
		pEntry = pBucket->pEntry = pVars->alloc.pCalloc(1, sizeof(BorderFace));
		initBorderTableEntry(pVars, pAcfVars, pEntry, ruvmFaceIndex, tile,
		                     pLoopBuf, baseFace, hasPreservedEdge, seam);
	}
	else {
		do {
			if (pEntry->faceIndex == ruvmFaceIndex) {
				while (pEntry->pNext) {
					pEntry = pEntry->pNext;
				}
				pEntry = pEntry->pNext = pVars->alloc.pCalloc(1, sizeof(BorderFace));
				initBorderTableEntry(pVars, pAcfVars, pEntry, ruvmFaceIndex, tile,
				                     pLoopBuf, baseFace, hasPreservedEdge, seam);
				break;
			}
			if (!pBucket->pNext) {
				pBucket = pBucket->pNext =
					pVars->alloc.pCalloc(1, sizeof(BorderBucket));
				pEntry =
					pBucket->pEntry = pVars->alloc.pCalloc(1, sizeof(BorderFace));
				initBorderTableEntry(pVars, pAcfVars, pEntry, ruvmFaceIndex, tile,
				                     pLoopBuf, baseFace, hasPreservedEdge, seam);
				break;
			}
			pBucket = pBucket->pNext;
			pEntry = pBucket->pEntry;
		} while (1);
	}
}

static
void addClippedFaceToBufMesh(MappingJobVars *pVars,
                             LoopBufWrap *pLoopBuf, int32_t edgeFace,
							 FaceRange ruvmFace, int32_t tile, FaceRange baseFace,
                             int32_t hasPreservedEdge, int32_t seam, int32_t onLine) {
	AddClippedFaceVars acfVars = {0};
	BufMesh *pBufMesh = &pVars->bufMesh;
	_Bool isBorderFace = edgeFace || onLine;
	for (int32_t i = 0; i < pLoopBuf->size; ++i) {
		int32_t refFace;
		int32_t isRuvm = pLoopBuf->buf[i].isRuvm;
		if (!isRuvm || pLoopBuf->buf[i].onLine) {
			addNewLoopAndOrVert(pVars, i, &acfVars, &pVars->bufMesh,
			                    pLoopBuf->buf, &baseFace);
			refFace = ruvmFace.index;
		}
		else {
			addRuvmLoopAndOrVert(pVars, i, &acfVars, &pVars->bufMesh,
			                     pLoopBuf->buf, &pVars->alloc, baseFace,
								 &ruvmFace);
			refFace = baseFace.index;
		}
		BufMeshIndex loop = bufMeshAddLoop(&pVars->alloc, pBufMesh, isBorderFace, pVars->pDpVars);
		acfVars.loop = loop.index;
		if (!i) {
			acfVars.loopStart = loop.index;
		}
		asMesh(pBufMesh)->mesh.pLoops[loop.realIndex] = acfVars.vert;
		asMesh(pBufMesh)->pNormals[loop.realIndex] = pLoopBuf->buf[i].normal;
		asMesh(pBufMesh)->pUvs[loop.realIndex] = pLoopBuf->buf[i].uv;
		blendMapAndInAttribs(&pVars->bufMesh, &asMesh(pBufMesh)->mesh.loopAttribs,
							 &pVars->pMap->mesh.mesh.loopAttribs,
							 &pVars->mesh.mesh.loopAttribs,
							 pLoopBuf->buf, i, loop.realIndex,
							 ruvmFace.start + pLoopBuf->buf[i].ruvmLoop,
							 baseFace.start,
							 pVars->pCommonAttribList->pLoop,
							 pVars->pCommonAttribList->loopCount, &baseFace);
		addEdge(pVars, i, &pVars->bufMesh, pLoopBuf->buf, &pVars->alloc,
		        refFace, &acfVars, &baseFace, &ruvmFace);
		asMesh(pBufMesh)->mesh.pEdges[loop.realIndex] = acfVars.edge;
	}
	BufMeshIndex face = bufMeshAddFace(&pVars->alloc, pBufMesh, isBorderFace, pVars->pDpVars);
	acfVars.face = face.index;
	asMesh(pBufMesh)->mesh.pFaces[face.realIndex] = acfVars.loopStart;
	blendMapAndInAttribs(&pVars->bufMesh, &asMesh(pBufMesh)->mesh.faceAttribs,
						 &pVars->pMap->mesh.mesh.faceAttribs,
						 &pVars->mesh.mesh.faceAttribs,
						 pLoopBuf->buf, 0, face.realIndex,
						 ruvmFace.index, baseFace.index,
						 pVars->pCommonAttribList->pFace,
						 pVars->pCommonAttribList->faceCount, &baseFace);
	if (isBorderFace) {
		addFaceToBorderTable(pVars, &acfVars, pLoopBuf, ruvmFace.index,
		                     tile, baseFace, hasPreservedEdge, seam);
	}
}

Result ruvmMapToSingleFace(MappingJobVars *pVars, FaceCellsTable *pFaceCellsTable,
                           DebugAndPerfVars *pDpVars,
					       V2_F32 fTileMin, int32_t tile, FaceRange baseFace) {
	FaceBounds bounds = {0};
	getFaceBounds(&bounds, pVars->mesh.pUvs, baseFace);
	BaseTriVerts baseTri;
	pDpVars->facesNotSkipped++;
	RUVM_ASSERT("", baseFace.size >= 3 && baseFace.size <= 4);
	for (int32_t i = 0; i < baseFace.size; ++i) {
		int32_t loop = baseFace.start + i;
		baseTri.uv[i] = _(pVars->mesh.pUvs[loop] V2SUB fTileMin);
		baseTri.xyz[i] = pVars->mesh.pVerts[pVars->mesh.mesh.pLoops[loop]];
	}
	_Bool degenerate;
	degenerate = v3DegenerateTri(baseTri.xyz[0], baseTri.xyz[1],
	                             baseTri.xyz[2], .000001f);
	if (baseFace.size == 4) {
		degenerate = v3DegenerateTri(baseTri.xyz[2], baseTri.xyz[3],
		                             baseTri.xyz[0], .000001f);
	}
	if (degenerate) {
		return RUVM_ERROR;
	}
	for (int32_t i = 0; i < pFaceCellsTable->pFaceCells[baseFace.index].cellSize; ++i) {
		RUVM_ASSERT("", asMesh(&pVars->bufMesh)->mesh.faceCount >= 0);
		RUVM_ASSERT("", asMesh(&pVars->bufMesh)->mesh.faceCount <
		                asMesh(&pVars->bufMesh)->faceBufSize);
		Cell* pCell = pFaceCellsTable->pFaceCells[baseFace.index].pCells[i];
		RUVM_ASSERT("", pCell->localIndex >= 0 && pCell->localIndex < 4);
		RUVM_ASSERT("", pCell->initialized % 2 == pCell->initialized);
		int32_t* pCellFaces;
		Range range = {0};
		if (pFaceCellsTable->pFaceCells[baseFace.index].pCellType[i]) {
			pCellFaces = pCell->pEdgeFaces;
			range = pFaceCellsTable->pFaceCells[baseFace.index].pRanges[i];
			//range.start = 0;
			//range.end = pCell->edgeFaceSize;
		}
		else if (pFaceCellsTable->pFaceCells[baseFace.index].pCellType[i] != 1) {
			pCellFaces = pCell->pFaces;
			range.start = 0;
			range.end = pCell->faceSize;
		}
		else {
			continue;
		}
		for (int32_t j = range.start; j < range.end; ++j) {
			pDpVars->totalFacesComp++;
			FaceRange ruvmFace =
				getFaceRange(&pVars->pMap->mesh.mesh, pCellFaces[j], false);
			if (!checkFaceIsInBounds(_(bounds.fMin V2SUB fTileMin),
									 _(bounds.fMax V2SUB fTileMin),
									 ruvmFace, &pVars->pMap->mesh)) {
				continue;
			}
			pDpVars->facesNotSkipped++;
			int32_t faceWindingDir =
				calcFaceWindingDirection(baseFace, pVars->mesh.pUvs);
			if (faceWindingDir == 2) {
				//face is degenerate
				continue;
			}
			LoopBufWrap loopBuf = {0};
			loopBuf.size = ruvmFace.size;
			for (int32_t k = 0; k < ruvmFace.size; ++k) {
				int32_t vertIndex = pVars->pMap->mesh.mesh.pLoops[ruvmFace.start + k];
				loopBuf.buf[k].preserve = 0;
				loopBuf.buf[k].isRuvm = 1;
				loopBuf.buf[k].baseLoop = (vertIndex + 1) * -1;
				loopBuf.buf[k].loop = pVars->pMap->mesh.pVerts[vertIndex];
				loopBuf.buf[k].loop.d[0] += fTileMin.d[0];
				loopBuf.buf[k].loop.d[1] += fTileMin.d[1];
				loopBuf.buf[k].ruvmLoop = k;
				loopBuf.buf[k].normal =
					pVars->pMap->mesh.pNormals[ruvmFace.start + k];
			}
			int32_t mapFaceWindDir = calcMapFaceWindingDirection(&loopBuf);
			int32_t edgeFace = 0;
			int32_t onLine = 0;
			int32_t hasPreservedEdge = 0;
			int32_t seam = 0;
			clipRuvmFaceAgainstBaseFace(pVars, baseFace, &loopBuf, &edgeFace,
										&hasPreservedEdge, &seam, faceWindingDir,
										mapFaceWindDir, &onLine);
			if (loopBuf.size <= 2) {
				continue;
			}
			transformClippedFaceFromUvToXyz(&loopBuf, ruvmFace, baseFace, &baseTri,
											pVars, fTileMin);
			int32_t faceIndex = pVars->bufMesh.mesh.mesh.faceCount;
			addClippedFaceToBufMesh(pVars, &loopBuf, edgeFace,
									  ruvmFace, tile, baseFace, hasPreservedEdge,
									  seam, onLine);
			if (pVars->getInFaces) {
				InFaceArr *pInFaceEntry = pVars->pInFaces + faceIndex;
				pInFaceEntry->pArr = pVars->alloc.pMalloc(sizeof(int32_t));
				*pInFaceEntry->pArr = baseFace.index;
				pInFaceEntry->count = 1;
				pInFaceEntry->usg = ruvmFace.index;
				int32_t faceCount = pVars->bufMesh.mesh.mesh.faceCount;
				RUVM_ASSERT("", pVars->inFaceSize <= faceCount);
				if (pVars->inFaceSize == faceCount) {
					pVars->inFaceSize *= 2;
					pVars->pInFaces =
						pVars->alloc.pRealloc(pVars->pInFaces, sizeof(InFaceArr) * pVars->inFaceSize);
				}
			}
		}
	}
	return RUVM_SUCCESS;
}
