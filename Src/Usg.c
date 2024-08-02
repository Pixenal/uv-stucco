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
When projecting from uvw to xyz, check if the ruvm vert is part of a usg.
If it is, then index the aformentioned table, and use the cached normal for projection.
Keep in mind you'll also need to handle interpolation of intersection points and base loops.
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
bool addToHitEdges(RuvmAlloc *pAlloc, HitEdgeTable *pHitEdges,
                   int32_t *pVerts, int32_t a, int32_t b) {
	uint32_t sum = b < 0 ? a : a + b;
	int32_t hash = ruvmFnvHash(&sum, 4, pHitEdges->size);
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
}

static
bool hitTestTri(RuvmAlloc *pAlloc, V3_F32 point, V3_F32 *pTri,
                int32_t *pVerts, HitEdgeTable *pHitEdges) {
	//add bounding squares to speed up tests maybe?
	V2_F32 triV2[3] = {
		*(V2_F32 *)&pTri[0],
		*(V2_F32 *)&pTri[1],
		*(V2_F32 *)&pTri[2]
	};
	V3_F32 bc = cartesianToBarycentric(triV2, &point);
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
	pVerts[0] = pMesh->mesh.pLoops[pFace->start + a];
	pVerts[1] = pMesh->mesh.pLoops[pFace->start + b];
	pVerts[2] = pMesh->mesh.pLoops[pFace->start + c];
	pTri[0] = pMesh->pVerts[pVerts[0]];
	pTri[1] = pMesh->pVerts[pVerts[1]];
	pTri[2] = pMesh->pVerts[pVerts[2]];

}

bool isPointInsideMesh(RuvmAlloc *pAlloc, V3_F32 pointV3, Mesh *pMesh) {
	//winding number test, with ray aligned with z axis
	//(so flatten point and mesh into 2D (x,y))
	int32_t wind = 0;
	HitEdgeTable hitEdges = {0};
	hitEdges.pTable = pAlloc->pCalloc(pMesh->mesh.edgeCount, sizeof(HitEdge));
	for (int32_t i = 0; i < pMesh->mesh.faceCount; ++i) {
		FaceRange face = getFaceRange(&pMesh->mesh, i, false);
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
			                                        pMesh->mesh.pLoops, false);
			for (int32_t j = 0; j < tris.loopCount; j += 3) {
				getTri(tri, triVerts, pMesh, &face, tris.pLoops[j],
				       tris.pLoops[j + 1], tris.pLoops[j + 2]);
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
	for (int32_t j = 0; j < pSrcMesh->mesh.vertCount; ++j) {
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
	V3_F32 uvs[4] = {
		{min.d[0], min.d[1]},
		{max.d[0], min.d[1]},
		{max.d[0], max.d[1]},
		{min.d[0], max.d[1]}
	};
	RuvmMesh *pCore = &pMesh->mesh;
	pCore->pFaces[pCore->faceCount] = pCore->loopCount;
	for (int32_t i = 0; i < 4; ++i) {
		pCore->pLoops[pCore->loopCount] = pCore->vertCount;
		pCore->pEdges[pCore->loopCount] = pCore->edgeCount;
		pMesh->pVerts[pCore->vertCount] = uvs[i];
		//can probably delete the uvs once the groups are set,
		//they're only used for that
		pMesh->pUvs[pCore->loopCount] = *(V2_F32 *)&uvs[i];
		pCore->loopCount++;
		pCore->edgeCount++;
		pCore->vertCount++;
	}
	pCore->faceCount++;
	pCore->pFaces[pCore->faceCount] = pCore->loopCount;
}

RuvmResult allocUsgSquaresMesh(RuvmAlloc *pAlloc, RuvmMap pMap) {
	Mesh *pMesh = &pMap->usgArr.squares;
	RuvmMesh *pCore = &pMesh->mesh;
	pCore->type.type = RUVM_OBJECT_DATA_MESH_INTERN;
	int32_t usgCount = pMap->usgArr.count;
	pMesh->faceBufSize = usgCount + 1;
	pMesh->loopBufSize = usgCount * 4;
	pMesh->edgeBufSize = pMesh->loopBufSize;
	pMesh->vertBufSize = pMesh->loopBufSize;
	pCore->pFaces = pAlloc->pCalloc(pMesh->faceBufSize, sizeof(int32_t));
	pCore->pLoops = pAlloc->pCalloc(pMesh->loopBufSize * 4, sizeof(int32_t));
	pCore->pEdges = pAlloc->pCalloc(pMesh->edgeBufSize * 4, sizeof(int32_t));
	pCore->vertAttribs.pArr = pAlloc->pCalloc(1, sizeof(RuvmAttrib));
	char posName[RUVM_ATTRIB_NAME_MAX_LEN] = "position";
	Attrib *pPosAttrib = pCore->vertAttribs.pArr;
	initAttrib(pAlloc, pPosAttrib, posName, pMesh->vertBufSize, true,
		RUVM_ATTRIB_ORIGIN_MAP, RUVM_ATTRIB_V3_F32);
	pCore->vertAttribs.count = 1;
	pCore->loopAttribs.pArr = pAlloc->pCalloc(2, sizeof(RuvmAttrib));
	char uvName[RUVM_ATTRIB_NAME_MAX_LEN] = "UVMap";
	Attrib *pUvAttrib = pCore->loopAttribs.pArr;
	initAttrib(pAlloc, pUvAttrib, uvName, pMesh->loopBufSize, true,
		RUVM_ATTRIB_ORIGIN_MAP, RUVM_ATTRIB_V2_F32);
	char normalName[RUVM_ATTRIB_NAME_MAX_LEN] = "normal";
	Attrib *pNormalAttrib = pCore->loopAttribs.pArr + 1;
	initAttrib(pAlloc, pNormalAttrib, normalName, pMesh->loopBufSize, true,
		RUVM_ATTRIB_ORIGIN_MAP, RUVM_ATTRIB_V3_F32);
	pCore->loopAttribs.count = 2;
	setSpecialAttribs(pMesh, 0xe); // 1110 - set pos uvs and normals
}

RuvmResult fillUsgSquaresMesh(RuvmMap pMap, RuvmUsg *pUsgArr) {
	for (int32_t i = 0; i < pMap->usgArr.count; ++i) {
		Mesh *pUsgMesh = (Mesh *)pUsgArr[i].obj.pData;
		getUsgBoundsSquare(&pMap->usgArr.squares, pUsgMesh);
	}
	return RUVM_SUCCESS;
}

RuvmResult assignUsgsToVerts(RuvmAlloc *pAlloc,
                             RuvmMap pMap, RuvmUsg *pUsgArr) {
	Mesh *pSquares = &pMap->usgArr.squares;
	FaceCellsTable faceCellsTable = {0};
	int32_t averageMapFacesPerFace = 0;
	getEncasingCells(pAlloc, pMap, pSquares, &faceCellsTable,
	                 &averageMapFacesPerFace);
	for (int32_t i = 0; i < pMap->usgArr.count; ++i) {
		Mesh *pMesh = (Mesh *)pUsgArr[i].obj.pData;
		Mesh *pFlatCutoff = (Mesh *)pUsgArr[i].pFlatCutoff->pData;
		FaceRange squaresFace = getFaceRange(pSquares, i, false);
		for (int32_t j = 0; j < faceCellsTable.pFaceCells[i].cellSize; ++j) {
			//put this cell stuff into a generic function
			// v v v
			Cell *pCell = faceCellsTable.pFaceCells[i].pCells[j];
			RUVM_ASSERT("", pCell->localIndex >= 0 && pCell->localIndex < 4);
			RUVM_ASSERT("", pCell->initialized % 2 == pCell->initialized);
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
				FaceRange ruvmFace = getFaceRange(&pMap->mesh.mesh, pCellFaces[k], false);
				//the uv of loops 0 and 2 can be treated and min and max for the bounding square
				V2_F32 min = pSquares->pUvs[squaresFace.start];
				V2_F32 max = pSquares->pUvs[squaresFace.start + 2];
				if (!checkFaceIsInBounds(min, max, ruvmFace, &pMap->mesh)) {
					continue;
				}
				for (int32_t l = 0; l < ruvmFace.size; ++l) {
					int32_t vertIndex = pMap->mesh.mesh.pLoops[ruvmFace.start + l];
					V3_F32 vert = pMap->mesh.pVerts[vertIndex];
					if (isPointInsideMesh(pAlloc, vert, pMesh)) {
						pMap->mesh.pUsg[vertIndex] = i + 1;
						if (pFlatCutoff &&
						    isPointInsideMesh(pAlloc, vert, pFlatCutoff)) {

							//negative indicates the vert is above the cutoff
							pMap->mesh.pUsg[vertIndex] *= -1;
						}
					}
				}
			}
		}
		ruvmDestroyFaceCellsEntry(pAlloc, i, &faceCellsTable);
	}
	ruvmDestroyFaceCellsTable(pAlloc, &faceCellsTable);
	return RUVM_SUCCESS;
}

RuvmResult sampleInAttribsAtUsgOrigins(RuvmMap pMap, RuvmMesh *pInMesh,
                                       int32_t count, InFaceArr *pInFaceTable) {
	Mesh meshInWrap = {.mesh = *pInMesh};
	setSpecialAttribs(&meshInWrap, 0x5e); //1011110 - set all except for receive
	for (int32_t i = 0; i < count; ++i) {
		Usg *pUsg = pMap->usgArr.pArr + pInFaceTable[i].usg;
		V3_F32 closestBc = {FLT_MAX, FLT_MAX, FLT_MAX};
		FaceRange closestFace = {.index = -1};
		int32_t closestFaceLoops[3] = {0};
		float closestDist = FLT_MAX;
		for (int32_t j = 0; j < pInFaceTable[i].count; ++j) {
			FaceRange inFace =
				getFaceRange(&meshInWrap.mesh, pInFaceTable[i].pArr[j], false);
			int8_t triLoops[4] = {0};
			V2_F32 triUvs[4] = {0};
			for (int32_t k = 0; k < inFace.size; ++k) {
				triUvs[k] = meshInWrap.pUvs[inFace.start + k];
			}
			//TODO move in uvs to 0 - 1 space if in another tile
			//(don't move origin, conversion to barycentric causes problems if you do that).
			//Determine how many uv tiles this face spans, then test barycentric for each tile
			//make the tile rasterization in getEnclosingCells a generic function, and reuse it here.
			V3_F32 bc = getBarycentricInFace(triUvs,
			                                 triLoops, inFace.size, pUsg->origin);
			for (int32_t k = 0; k < inFace.size; ++k) {
				triUvs[k] = meshInWrap.pUvs[inFace.start + triLoops[k]];
			}
			int32_t inside = bc.d[0] >= .0f && bc.d[1] >= .0f && bc.d[2] >= .0f;
			int32_t edge = 0;
			float dist = .0f;
			if (!inside) {
				V3_F32 bcAbs = {fabs(bc.d[0]), fabs(bc.d[1]), fabs(bc.d[2])};
				V3_F32 closestAbs = {fabs(closestBc.d[0]), fabs(closestBc.d[1]), fabs(closestBc.d[2])};
				for (edge; edge < 3; ++edge) {
					int32_t last = edge == 0 ? 2 : edge - 1;
					int32_t next = (edge + 1) % 3;
					if (bcAbs.d[edge] < closestDist && bc.d[last] >= .0f && bc.d[next] >= .0f) {
						dist = bcAbs.d[edge];
						break;
					}
				}
			}
			if (inside || edge < 3) {
				closestBc = bc;
				closestFace = inFace;
				closestFaceLoops[0] = triLoops[0];
				closestFaceLoops[1] = triLoops[1];
				closestFaceLoops[2] = triLoops[2];
				if (inside) {
					break;
				}
				closestDist = dist;
			}
		}
		RUVM_ASSERT("", closestFace.index >= 0);
		pInFaceTable[i].tri[0] = closestFace.start + closestFaceLoops[0];
		pInFaceTable[i].tri[1] = closestFace.start + closestFaceLoops[1];
		pInFaceTable[i].tri[2] = closestFace.start + closestFaceLoops[2];
		Attrib attrib = {
			.pData = &pInFaceTable[i].normal,
			.interpolate = true,
			.type = RUVM_ATTRIB_V3_F32
		};
		interpolateAttrib(&attrib, 0, meshInWrap.pNormalAttrib,
		                  pInFaceTable[i].tri[0],
		                  pInFaceTable[i].tri[1],
		                  pInFaceTable[i].tri[2],
		                  closestBc);
		//pInFaceTable[i].tbn = buildFaceTbn(closestFace, &meshInWrap, closestFaceLoops);
		
		//TODO support usg for more than just normal maps,
		//     add a ui to select which attribs should be uniform.
		//     Ideally you should be able to do this per usg,
		//     though you'd need to store usg names for that to work.
	}
}

bool sampleUsg(int32_t ruvmLoop, V3_F32 uvw, V3_F32 *pPos, bool *pTransformed, 
               V3_F32 *pUsgBc, FaceRange ruvmFace, RuvmMap pMap, int32_t inFace,
               Mesh *pInMesh, V3_F32 *pNormal, bool useFlatCutoff) {
	Mesh *pMapMesh = &pMap->mesh;
	int32_t mapLoop = pMapMesh->mesh.pLoops[ruvmFace.start + ruvmLoop];
	int32_t usg = pMapMesh->pUsg[mapLoop];
	if (usg) {
		bool flatCutoff = usg < 0;
		usg = abs(usg) - 1;
		uint32_t sum = usg + inFace;
		int32_t hash = ruvmFnvHash((uint8_t *)&sum, 4, pMap->usgArr.tableSize);
		UsgInFace* pEntry = pMap->usgArr.pInFaceTable + hash;
		do {
			if (pEntry->face == inFace && pEntry->pEntry && pEntry->pEntry->usg == usg) {
				break;
			}
			pEntry = pEntry->pNext;
		} while(pEntry);
		//RUVM_ASSERT("", pEntry);
		if (pEntry) {
			if (useFlatCutoff && flatCutoff) {
				BaseTriVerts usgTri = {
					.uv = {pInMesh->pUvs[pEntry->pEntry->tri[0]],
							pInMesh->pUvs[pEntry->pEntry->tri[1]],
							pInMesh->pUvs[pEntry->pEntry->tri[2]]},
					.xyz = {pInMesh->pVerts[pInMesh->mesh.pLoops[pEntry->pEntry->tri[0]]],
							pInMesh->pVerts[pInMesh->mesh.pLoops[pEntry->pEntry->tri[1]]],
							pInMesh->pVerts[pInMesh->mesh.pLoops[pEntry->pEntry->tri[2]]]}
				};
				getTriScale(3, &usgTri);
				*pUsgBc = cartesianToBarycentric(usgTri.uv, (V2_F32 *)&uvw);
				*pPos = barycentricToCartesian(usgTri.xyz, pUsgBc);
				*pTransformed = true;
			}
			//V3_F32 mapRealNormal = getLoopRealNormal(pMapMesh, &ruvmFace, ruvmLoop);
			//V3_F32 up = { .0f, .0f, 1.0f };
			//float upMask = abs(_(mapRealNormal V3DOT up));
			//pLoop->projNormalMasked = v3Normalize(v3Lerp(normal, pEntry->pEntry->normal, upMask));
			//pLoop->projNormalMasked = pEntry->pEntry->normal;
			*pNormal = pEntry->pEntry->normal;
			return true;
		}
	}
	return false;
}