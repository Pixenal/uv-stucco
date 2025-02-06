#include <float.h>
#include <string.h>
#include <math.h>

#include <QuadTree.h>
#include <Context.h>
#include <MapFile.h>
#include <AttribUtils.h>
#include <Usg.h>
#include <Error.h>

/*
Plan for actually mapping usg
so basically, run mapToMesh on the usg bounding squares used earlier (the ones used to get enclosing cell faces).
It needs to be a single face, not the compass mesh you sketched up earlier. Otherwise you'd need to somehow connect the faces,
which would introduce complexity.
If it's a single face per usg, then combineBoundsFaces will handle everything automatically.

Speaking of combineBoundsFaces, make sure to modify it to allow an option to output a list per outFace face,
of the inFaces that enclosed that face. Literally just compiling the baseFace component of the entry string into a list.

Now that we have a bunch of faces corresponding to USG, and their respective inFaces, we need to get the normal of the sample
point.
Iterate through each inFace, and find the tri that encloses the origin (keep the barycentric coords).
If none enclose, then the origin is outside the mesh.
In this case, find the nearest inEdge (whichever bc coord is closest to 0), and sample the normal from this edge (interpolated of course).

Cache the normal, as well as the list of inFaces, in a hash table, indexed with the sum of the usg index and all in faces.
With a sufficiently large table, including the inface indices in the sum should prevent time spent searching through entries of the same usg later on,
to find the one with the desired inFace.
Best case scenario, the usg entry is placed in a bucket with entries of different usg indices, allowing for a quick match and no searching inFace lists).

Now, run the main mapToMesh as usual.
When projecting from uvw to xyz, check if the stuc vert is part of a usg.
If it is, then index the aformentioned table, and use the cached normal for projection.
Keep in mind you'll also need to handle interpolation of intersection points and base corners.
*/

typedef struct HitEdge {
	struct HitEdge *pNext;
	int32_t verts[2];
	bool valid;
} HitEdge;

typedef struct {
	HitEdge *pTable;
	int32_t size;
} HitEdgeTable;

static initHitEdgeEntry(HitEdge *pEntry, int32_t a, int32_t b) {
	pEntry->verts[0] = a;
	pEntry->verts[1] = b;
	pEntry->valid = true;
}

static
bool addToHitEdges(StucAlloc *pAlloc, HitEdgeTable *pHitEdges,
                   int32_t *pVerts, int32_t a, int32_t b) {
	uint32_t sum = b < 0 ? a : a + b;
	int32_t hash = stucFnvHash((uint8_t *)&sum, 4, pHitEdges->size);
	HitEdge *pEntry = pHitEdges->pTable + hash;
	if (!pEntry->valid) {
		initHitEdgeEntry(pEntry, a, b);
		return true;
	}
	int32_t verta = pVerts[a];
	int32_t vertb = b < 0 ? -1 : pVerts[b];
	do {
		if (pEntry->verts[0] == verta || pEntry->verts[0] == vertb ||
			pEntry->verts[1] == verta || pEntry->verts[1] == vertb) {
			return false;
		}
		if (!pEntry->pNext) {
			pEntry = pEntry->pNext = pAlloc->pCalloc(1, sizeof(HitEdge));
			initHitEdgeEntry(pEntry, a, b);
			return true;
		}
		pEntry = pEntry->pNext;
	} while(pEntry);
	return true;
}

static
bool hitTestTri(StucAlloc *pAlloc, V3_F32 point, V3_F32 *pTri,
                int32_t *pVerts, HitEdgeTable *pHitEdges) {
	//add bounding squares to speed up tests maybe?
	V2_F32 triV2[3] = {
		*(V2_F32 *)&pTri[0],
		*(V2_F32 *)&pTri[1],
		*(V2_F32 *)&pTri[2]
	};
	V3_F32 bc = cartesianToBarycentric(triV2, (V2_F32 *)&point);
	if (!v3IsFinite(bc)) {
		//degenerate
		return false;
	}
	if (bc.d[0] < .0f || bc.d[1] < .0f || bc.d[2] < .0f) {
		return false;
	}
	V3_F32 intersection = barycentricToCartesian(pTri, &bc);
	if (point.d[2] < intersection.d[2]) {
		return false; //point is below triangle
	}
	if (bc.d[0] > .0f && bc.d[1] > .0f && bc.d[2] > .0f) {
		return true;
	}
	//point lies on an edge or vert
	int32_t verts[2] = {0};
	if (bc.d[0] == 1.0f) {
		verts[0] = 0;
		verts[1] = -1;
	}
	else if (bc.d[1] == 1.0f) {
		verts[0] = 1;
		verts[1] = -1;
	}
	else if (bc.d[2] == 1.0f) {
		verts[0] = 2;
		verts[1] = -1;
	}
	else if (bc.d[0] == .0f) {
		verts[0] = 1;
		verts[1] = 2;
	}
	else if (bc.d[1] == .0f) {
		verts[0] = 0;
		verts[1] = 2;
	}
	else if (bc.d[2] == .0f) {
		verts[0] = 0;
		verts[1] = 1;
	}
	return addToHitEdges(pAlloc, pHitEdges, pVerts, verts[0], verts[1]);
}

static
void getTri(V3_F32 *pTri, int32_t *pVerts, Mesh *pMesh, FaceRange *pFace,
            int32_t a, int32_t b, int32_t c) {
	pVerts[0] = pMesh->core.pCorners[pFace->start + a];
	pVerts[1] = pMesh->core.pCorners[pFace->start + b];
	pVerts[2] = pMesh->core.pCorners[pFace->start + c];
	pTri[0] = pMesh->pVerts[pVerts[0]];
	pTri[1] = pMesh->pVerts[pVerts[1]];
	pTri[2] = pMesh->pVerts[pVerts[2]];

}

bool isPointInsideMesh(StucAlloc *pAlloc, V3_F32 pointV3, Mesh *pMesh) {
	//winding number test, with ray aligned with z axis
	//(so flatten point and mesh into 2D (x,y))
	int32_t wind = 0;
	HitEdgeTable hitEdges = {0};
	hitEdges.size = pMesh->core.edgeCount;
	hitEdges.pTable = pAlloc->pCalloc(hitEdges.size, sizeof(HitEdge));
	for (int32_t i = 0; i < pMesh->core.faceCount; ++i) {
		FaceRange face = getFaceRange(&pMesh->core, i, false);
		V3_F32 tri[3] = {0};
		int32_t triVerts[3] = {0};
		if (face.size == 3) {
			getTri(tri, triVerts, pMesh, &face, 0, 1, 2);
			wind += hitTestTri(pAlloc, pointV3, tri, triVerts, &hitEdges);
		}
		else if (face.size == 4) {
			getTri(tri, triVerts, pMesh, &face, 0, 1, 2);
			wind += hitTestTri(pAlloc, pointV3, tri, triVerts, &hitEdges);
			getTri(tri, triVerts, pMesh, &face, 2, 3, 0);
			wind += hitTestTri(pAlloc, pointV3, tri, triVerts, &hitEdges);
		}
		else {
			FaceTriangulated tris = triangulateFace(*pAlloc, face, pMesh->pVerts,
			                                        pMesh->core.pCorners, false);
			for (int32_t j = 0; j < tris.cornerCount; j += 3) {
				getTri(tri, triVerts, pMesh, &face, tris.pCorners[j],
				       tris.pCorners[j + 1], tris.pCorners[j + 2]);
				wind += hitTestTri(pAlloc, pointV3, tri, triVerts, &hitEdges);
			}
		}
	}
	return wind % 2;
}

static
void getUsgBoundsSquare(Mesh *pMesh, Mesh *pSrcMesh) {
	V2_F32 min = {FLT_MAX, FLT_MAX};
	V2_F32 max = {-FLT_MAX, -FLT_MAX};
	for (int32_t j = 0; j < pSrcMesh->core.vertCount; ++j) {
		V3_F32 vert = pSrcMesh->pVerts[j];
		if (vert.d[0] < min.d[0]) {
			min.d[0] = vert.d[0];
		}
		if (vert.d[0] > max.d[0]) {
			max.d[0] = vert.d[0];
		}
		if (vert.d[1] < min.d[1]) {
			min.d[1] = vert.d[1];
		}
		if (vert.d[1] > max.d[1]) {
			max.d[1] = vert.d[1];
		}
	}
	V3_F32 stuc[4] = {
		{min.d[0], min.d[1]},
		{max.d[0], min.d[1]},
		{max.d[0], max.d[1]},
		{min.d[0], max.d[1]}
	};
	StucMesh *pCore = &pMesh->core;
	pCore->pFaces[pCore->faceCount] = pCore->cornerCount;
	for (int32_t i = 0; i < 4; ++i) {
		pCore->pCorners[pCore->cornerCount] = pCore->vertCount;
		pCore->pEdges[pCore->cornerCount] = pCore->edgeCount;
		pMesh->pVerts[pCore->vertCount] = stuc[i];
		//can probably delete the stuc once the groups are set,
		//they're only used for that
		pMesh->pUvs[pCore->cornerCount] = *(V2_F32 *)&stuc[i];
		pCore->cornerCount++;
		pCore->edgeCount++;
		pCore->vertCount++;
	}
	pCore->faceCount++;
	pCore->pFaces[pCore->faceCount] = pCore->cornerCount;
}

StucResult allocUsgSquaresMesh(StucContext pContext, StucAlloc *pAlloc, StucMap pMap) {
	Mesh *pMesh = &pMap->usgArr.squares;
	StucMesh *pCore = &pMesh->core;
	pCore->type.type = STUC_OBJECT_DATA_MESH_INTERN;
	int32_t usgCount = pMap->usgArr.count;
	pMesh->faceBufSize = usgCount + 1;
	pMesh->cornerBufSize = usgCount * 4;
	pMesh->edgeBufSize = pMesh->cornerBufSize;
	pMesh->vertBufSize = pMesh->cornerBufSize;
	pCore->pFaces = pAlloc->pCalloc(pMesh->faceBufSize, sizeof(int32_t));
	pCore->pCorners = pAlloc->pCalloc(pMesh->cornerBufSize * 4, sizeof(int32_t));
	pCore->pEdges = pAlloc->pCalloc(pMesh->edgeBufSize * 4, sizeof(int32_t));
	pCore->vertAttribs.pArr = pAlloc->pCalloc(1, sizeof(StucAttrib));
	char posName[STUC_ATTRIB_NAME_MAX_LEN] = "position";
	Attrib *pPosAttrib = pCore->vertAttribs.pArr;
	initAttrib(pAlloc, pPosAttrib, posName, pMesh->vertBufSize, true,
		STUC_ATTRIB_ORIGIN_MAP, STUC_ATTRIB_V3_F32);
	pCore->vertAttribs.count = 1;
	pCore->cornerAttribs.pArr = pAlloc->pCalloc(2, sizeof(StucAttrib));
	char uvName[STUC_ATTRIB_NAME_MAX_LEN] = "UVMap";
	Attrib *pUvAttrib = pCore->cornerAttribs.pArr;
	initAttrib(pAlloc, pUvAttrib, uvName, pMesh->cornerBufSize, true,
		STUC_ATTRIB_ORIGIN_MAP, STUC_ATTRIB_V2_F32);
	char normalName[STUC_ATTRIB_NAME_MAX_LEN] = "normal";
	Attrib *pNormalAttrib = pCore->cornerAttribs.pArr + 1;
	initAttrib(pAlloc, pNormalAttrib, normalName, pMesh->cornerBufSize, true,
		STUC_ATTRIB_ORIGIN_MAP, STUC_ATTRIB_V3_F32);
	pCore->cornerAttribs.count = 2;
	setSpecialAttribs(pContext, pMesh, 0xe); // 1110 - set pos stuc and normals
	return STUC_SUCCESS;
}

StucResult fillUsgSquaresMesh(StucMap pMap, StucUsg *pUsgArr) {
	for (int32_t i = 0; i < pMap->usgArr.count; ++i) {
		Mesh *pUsgMesh = (Mesh *)pUsgArr[i].obj.pData;
		getUsgBoundsSquare(&pMap->usgArr.squares, pUsgMesh);
	}
	return STUC_SUCCESS;
}

StucResult assignUsgsToVerts(StucAlloc *pAlloc,
                             StucMap pMap, StucUsg *pUsgArr) {
	Mesh *pSquares = &pMap->usgArr.squares;
	FaceCellsTable faceCellsTable = {0};
	int32_t averageMapFacesPerFace = 0;
	getEncasingCells(pAlloc, pMap, pSquares, &faceCellsTable,
	                 -1, &averageMapFacesPerFace);
	for (int32_t i = 0; i < pMap->usgArr.count; ++i) {
		Mesh *pMesh = (Mesh *)pUsgArr[i].obj.pData;
		Mesh *pFlatCutoff = (Mesh *)pUsgArr[i].pFlatCutoff->pData;
		FaceRange squaresFace = getFaceRange(pSquares, i, false);
		for (int32_t j = 0; j < faceCellsTable.pFaceCells[i].cellSize; ++j) {
			//put this cell stuff into a generic function
			// v v v
			int32_t cellIdx = faceCellsTable.pFaceCells[i].pCells[j];
			Cell *pCell = pMap->quadTree.cellTable.pArr + cellIdx;
			STUC_ASSERT("", pCell->localIdx >= 0 && pCell->localIdx < 4);
			STUC_ASSERT("", pCell->initialized % 2 == pCell->initialized);
			int32_t *pCellFaces;
			Range range = {0};
			if (faceCellsTable.pFaceCells[i].pCellType[j]) {
				pCellFaces = pCell->pEdgeFaces;
				range = faceCellsTable.pFaceCells[i].pRanges[j];
			}
			else if (faceCellsTable.pFaceCells[i].pCellType[j] != 1) {
				pCellFaces = pCell->pFaces;
				range.start = 0;
				range.end = pCell->faceSize;
			}
			else {
				continue;
			}
			// ^ ^ ^

			for (int32_t k = range.start; k < range.end; ++k) {
				FaceRange stucFace = getFaceRange(&pMap->mesh.core, pCellFaces[k], false);
				//the uv of corners 0 and 2 can be treated and min and max for the bounding square
				V2_F32 min = pSquares->pUvs[squaresFace.start];
				V2_F32 max = pSquares->pUvs[squaresFace.start + 2];
				if (!checkFaceIsInBounds(min, max, stucFace, &pMap->mesh)) {
					continue;
				}
				for (int32_t l = 0; l < stucFace.size; ++l) {
					int32_t vertIdx = pMap->mesh.core.pCorners[stucFace.start + l];
					V3_F32 vert = pMap->mesh.pVerts[vertIdx];
					if (isPointInsideMesh(pAlloc, vert, pMesh)) {
						pMap->mesh.pUsg[vertIdx] = i + 1;
						if (pFlatCutoff &&
						    isPointInsideMesh(pAlloc, vert, pFlatCutoff)) {

							//negative indicates the vert is above the cutoff
							pMap->mesh.pUsg[vertIdx] *= -1;
						}
					}
				}
			}
		}
		stucDestroyFaceCellsEntry(pAlloc, i, &faceCellsTable);
	}
	stucDestroyFaceCellsTable(pAlloc, &faceCellsTable);
	return STUC_SUCCESS;
}

static
StucResult getClosestTriToOrigin(Usg *pUsg, Mesh *pInMesh, InFaceArr *pInFaceTable,
                                 int32_t i, V3_F32 *pClosestBc, FaceRange *pClosestFace,
                                 int8_t *pClosestFaceCorners, float *pClosestDist) {
	for (int32_t j = 0; j < pInFaceTable[i].count; ++j) {
		FaceRange inFace =
			getFaceRange(&pInMesh->core, pInFaceTable[i].pArr[j], false);
		FaceBounds faceBounds = {0};
		getFaceBoundsForTileTest(&faceBounds, pInMesh, &inFace);
		V2_I32 minTile = faceBounds.min;
		V2_I32 maxTile = faceBounds.max;
		for (int32_t l = minTile.d[1]; l <= maxTile.d[1]; ++l) {
			for (int32_t m = minTile.d[0]; m <= maxTile.d[0]; ++m) {
				V2_I32 tileMin = {m, l};
				V2_F32 fTileMin = {(float)m, (float)l};
				int8_t triCorners[4] = {0};
				V2_F32 triStuc[4] = {0};
				for (int32_t k = 0; k < inFace.size; ++k) {
					triStuc[k] = pInMesh->pUvs[inFace.start + k];
				}
				//TODO move in stuc to 0 - 1 space if in another tile
				//(don't move origin, conversion to barycentric causes problems if you do that).
				//Determine how many uv tiles this face spans, then test barycentric for each tile
				//make the tile rasterization in getEnclosingCells a generic function, and reuse it here.
				int32_t in =
					checkIfFaceIsInsideTile(inFace.size, triStuc, &faceBounds, tileMin);
				if (!in) {
					return STUC_ERROR;
				}
				for (int32_t k = 0; k < inFace.size; ++k) {
					_(triStuc + k V2SUBEQL fTileMin);
				}
				V3_F32 bc = getBarycentricInFace(triStuc,
													triCorners, inFace.size, pUsg->origin);
				int32_t inside = bc.d[0] >= .0f && bc.d[1] >= .0f && bc.d[2] >= .0f;
				bool close = false;
				float dist = .0f;
				if (!inside) {
					V3_F32 bcAbs = {fabs(bc.d[0]), fabs(bc.d[1]), fabs(bc.d[2])};
					V3_F32 closestAbs = {fabs(pClosestBc->d[0]), fabs(pClosestBc->d[1]), fabs(pClosestBc->d[2])};
					for (int32_t edge = 0; edge < 3; ++edge) {
						int32_t last = edge == 0 ? 2 : edge - 1;
						int32_t next = (edge + 1) % 3;
						if (bcAbs.d[edge] < *pClosestDist && bc.d[last] >= .0f && bc.d[next] >= .0f) {
							dist = bcAbs.d[edge];
							close = true;
							break;
						}
					}
					if (!close) {
						//test tri verts
						for (int32_t vert = 0; vert < 3; ++vert) {
							if (bc.d[vert] < .0f) {
								continue;
							}
							int32_t last = vert == 0 ? 2 : vert - 1;
							int32_t next = (vert + 1) % 3;
							float thisDist = fabs(1.0f - bc.d[vert]);
							if (thisDist < *pClosestDist && bc.d[last] < .0f && bc.d[next] < .0f) {
								dist = thisDist;
								close = true;
								break;
							}
						}
					}
				}
				if (inside || close) {
					*pClosestBc = bc;
					*pClosestFace = inFace;
					pClosestFaceCorners[0] = triCorners[0];
					pClosestFaceCorners[1] = triCorners[1];
					pClosestFaceCorners[2] = triCorners[2];
					if (inside) {
						return STUC_SUCCESS;
					}
					*pClosestDist = dist;
				}
			}
		}
	}
	STUC_ASSERT("", pClosestFace->idx >= 0);
	return STUC_SUCCESS;
}

StucResult sampleInAttribsAtUsgOrigins(StucContext pContext, StucMap pMap, Mesh *pInMesh,
                                       StucMesh *pSquares, InFaceArr *pInFaceTable) {
	for (int32_t i = 0; i < pSquares->faceCount; ++i) {
		Usg *pUsg = pMap->usgArr.pArr + pInFaceTable[i].usg;
		V3_F32 closestBc = {FLT_MAX, FLT_MAX, FLT_MAX};
		FaceRange closestFace = {.idx = -1};
		int8_t closestFaceCorners[3] = {0};
		float closestDist = FLT_MAX;
		StucResult result = STUC_NOT_SET;
		result = getClosestTriToOrigin(pUsg, pInMesh, pInFaceTable, i, &closestBc,
		                               &closestFace, &closestFaceCorners, &closestDist);
		if (result != STUC_SUCCESS) {
			return result;
		}
		pInFaceTable[i].tri[0] = closestFace.start + closestFaceCorners[0];
		pInFaceTable[i].tri[1] = closestFace.start + closestFaceCorners[1];
		pInFaceTable[i].tri[2] = closestFace.start + closestFaceCorners[2];
		pInFaceTable[i].tbn = getInterpolatedTbn(pInMesh, &closestFace,
		                                         closestFaceCorners, closestBc);
		pInFaceTable[i].normal = *(V3_F32 *)&pInFaceTable[i].tbn.d[2];
		//add offset if current position will cause intersections with surface
		FaceRange face = getFaceRange(pSquares, i, false);
		int32_t triVerts[3] = {
			pInMesh->core.pCorners[pInFaceTable[i].tri[0]],
			pInMesh->core.pCorners[pInFaceTable[i].tri[1]],
			pInMesh->core.pCorners[pInFaceTable[i].tri[2]]
		};
		V3_F32 tri[3] = {
			pInMesh->pVerts[triVerts[0]],
			pInMesh->pVerts[triVerts[1]],
			pInMesh->pVerts[triVerts[2]]
		};
		Mesh squaresWrap = {.core = *pSquares};
		setSpecialAttribs(pContext, &squaresWrap, 0x02); //set only position
		// where a is the origin, ab is the normal, and c is the square vert
		V3_F32 a = barycentricToCartesian(tri, &closestBc);
		V3_F32 ab = pInFaceTable[i].normal;
		STUC_ASSERT("", face.start >= 0 && face.size >= 3);
		float tMax = -FLT_MAX;
		for (int32_t j = 0; j < face.size; ++j) {
			int32_t vert = pSquares->pCorners[face.start + j];
			V3_F32 ac = _(squaresWrap.pVerts[vert] V3SUB a);
			float t = _(ab V3DOT ac);
			if (t > tMax) {
				tMax = t;
			}
		}
		pInFaceTable[i].offset = tMax > .0f ? tMax : .0f;
		/*
		Attrib attrib = {
			.pData = &pInFaceTable[i].normal,
			.interpolate = true,
			.type = STUC_ATTRIB_V3_F32
		};
		interpolateAttrib(&attrib, 0, meshInWrap.pNormalAttrib,
		                  pInFaceTable[i].tri[0],
		                  pInFaceTable[i].tri[1],
		                  pInFaceTable[i].tri[2],
		                  closestBc);
		*/
		//pInFaceTable[i].tbn = buildFaceTbn(closestFace, &meshInWrap, closestFaceCorners);
		
		//TODO support usg for more than just normal maps,
		//     add a ui to select which attribs should be uniform.
		//     Ideally you should be able to do this per usg,
		//     though you'd need to store usg names for that to work.
	}
	return STUC_SUCCESS;
}

bool sampleUsg(int32_t stucCorner, V3_F32 uvw, V3_F32 *pPos, bool *pTransformed, 
               V3_F32 *pUsgBc, FaceRange stucFace, StucMap pMap, int32_t inFace,
               Mesh *pInMesh, V3_F32 *pNormal, V2_F32 tileMin,
               bool useFlatCutoff, bool flatCutoffOveride, Mat3x3 *pTbn) {
	Mesh *pMapMesh = &pMap->mesh;
	int32_t mapCorner = pMapMesh->core.pCorners[stucFace.start + stucCorner];
	int32_t usg = pMapMesh->pUsg[mapCorner];
	if (usg) {
		bool flatCutoff = flatCutoffOveride ? useFlatCutoff : usg < 0;
		usg = abs(usg) - 1;
		uint32_t sum = usg + inFace;
		int32_t hash = stucFnvHash((uint8_t *)&sum, 4, pMap->usgArr.tableSize);
		UsgInFace* pEntry = pMap->usgArr.pInFaceTable + hash;
		do {
			if (pEntry->face == inFace && pEntry->pEntry && pEntry->pEntry->usg == usg) {
				break;
			}
			pEntry = pEntry->pNext;
		} while(pEntry);
		//STUC_ASSERT("", pEntry);
		if (pEntry) {
			if (flatCutoff) {
				BaseTriVerts usgTri = {
					.uv = {pInMesh->pUvs[pEntry->pEntry->tri[0]],
							pInMesh->pUvs[pEntry->pEntry->tri[1]],
							pInMesh->pUvs[pEntry->pEntry->tri[2]]},
					.xyz = {pInMesh->pVerts[pInMesh->core.pCorners[pEntry->pEntry->tri[0]]],
							pInMesh->pVerts[pInMesh->core.pCorners[pEntry->pEntry->tri[1]]],
							pInMesh->pVerts[pInMesh->core.pCorners[pEntry->pEntry->tri[2]]]}
				};
				_(&usgTri.uv[0] V2SUBEQL tileMin);
				_(&usgTri.uv[1] V2SUBEQL tileMin);
				_(&usgTri.uv[2] V2SUBEQL tileMin);
				//getTriScale(3, &usgTri);
				*pUsgBc = cartesianToBarycentric(usgTri.uv, (V2_F32 *)&uvw);
				*pPos = barycentricToCartesian(usgTri.xyz, pUsgBc);
				if (pEntry->pEntry->offset) {
					_(pPos V3ADDEQL _(pEntry->pEntry->normal V3MULS pEntry->pEntry->offset));
				}
				*pTbn = pEntry->pEntry->tbn;
				*pTransformed = true;
			}
			//V3_F32 mapRealNormal = getCornerRealNormal(pMapMesh, &stucFace, stucCorner);
			//V3_F32 up = { .0f, .0f, 1.0f };
			//float upMask = abs(_(mapRealNormal V3DOT up));
			//pCorner->projNormalMasked = v3Normalize(v3Lerp(normal, pEntry->pEntry->normal, upMask));
			//pCorner->projNormalMasked = pEntry->pEntry->normal;
			*pNormal = pEntry->pEntry->normal;
			return true;
		}
	}
	return false;
}