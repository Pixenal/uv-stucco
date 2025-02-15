#include <float.h>
#include <string.h>
#include <math.h>

#include <QuadTree.h>
#include <Context.h>
#include <MapFile.h>
#include <AttribUtils.h>
#include <Usg.h>
#include <Error.h>


typedef struct HitEdge {
	struct HitEdge *pNext;
	I32 verts[2];
	bool valid;
} HitEdge;

typedef struct {
	HitEdge *pTable;
	I32 size;
} HitEdgeTable;

static
void initHitEdgeEntry(HitEdge *pEntry, I32 a, I32 b) {
	pEntry->verts[0] = a;
	pEntry->verts[1] = b;
	pEntry->valid = true;
}

static
bool addToHitEdges(
	const StucAlloc *pAlloc,
	HitEdgeTable *pHitEdges,
	I32 *pVerts,
	I32 a, I32 b
) {
	U32 sum = b < 0 ? a : a + b;
	I32 hash = stucFnvHash((U8 *)&sum, 4, pHitEdges->size);
	HitEdge *pEntry = pHitEdges->pTable + hash;
	if (!pEntry->valid) {
		initHitEdgeEntry(pEntry, a, b);
		return true;
	}
	I32 verta = pVerts[a];
	I32 vertb = b < 0 ? -1 : pVerts[b];
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
bool hitTestTri(
	const StucAlloc *pAlloc,
	V3_F32 point,
	V3_F32 *pTri,
	I32 *pVerts,
	HitEdgeTable *pHitEdges
) {
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
	I32 verts[2] = {0};
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
void getTri(
	V3_F32 *pTri,
	I32 *pVerts,
	Mesh *pMesh,
	FaceRange *pFace,
	I32 a, I32 b, I32 c
) {
	pVerts[0] = pMesh->core.pCorners[pFace->start + a];
	pVerts[1] = pMesh->core.pCorners[pFace->start + b];
	pVerts[2] = pMesh->core.pCorners[pFace->start + c];
	pTri[0] = pMesh->pVerts[pVerts[0]];
	pTri[1] = pMesh->pVerts[pVerts[1]];
	pTri[2] = pMesh->pVerts[pVerts[2]];

}

bool stucIsPointInsideMesh(const StucAlloc *pAlloc, V3_F32 pointV3, Mesh *pMesh) {
	//winding number test, with ray aligned with z axis
	//(so flatten point and mesh into 2D (x,y))
	I32 wind = 0;
	HitEdgeTable hitEdges = {0};
	hitEdges.size = pMesh->core.edgeCount;
	hitEdges.pTable = pAlloc->pCalloc(hitEdges.size, sizeof(HitEdge));
	for (I32 i = 0; i < pMesh->core.faceCount; ++i) {
		FaceRange face = stucGetFaceRange(&pMesh->core, i, false);
		V3_F32 tri[3] = {0};
		I32 triVerts[3] = {0};
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
			FaceTriangulated tris = stucTriangulateFace(
				*pAlloc,
				&face,
				pMesh->pVerts,
				pMesh->core.pCorners,
				false
			);
			for (I32 j = 0; j < tris.cornerCount; j += 3) {
				getTri(
					tri,
					triVerts,
					pMesh,
					&face,
					tris.pCorners[j],
					tris.pCorners[j + 1],
					tris.pCorners[j + 2]
				);
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
	for (I32 j = 0; j < pSrcMesh->core.vertCount; ++j) {
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
	for (I32 i = 0; i < 4; ++i) {
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

StucResult stucAllocUsgSquaresMesh(
	StucContext pCtx,
	const StucAlloc *pAlloc,
	StucMap pMap
) {
	Mesh *pMesh = &pMap->usgArr.squares;
	StucMesh *pCore = &pMesh->core;
	pCore->type.type = STUC_OBJECT_DATA_MESH_INTERN;
	I32 usgCount = pMap->usgArr.count;
	pMesh->faceBufSize = usgCount + 1;
	pMesh->cornerBufSize = usgCount * 4;
	pMesh->edgeBufSize = pMesh->cornerBufSize;
	pMesh->vertBufSize = pMesh->cornerBufSize;
	pCore->pFaces = pAlloc->pCalloc(pMesh->faceBufSize, sizeof(I32));
	pCore->pCorners = pAlloc->pCalloc(pMesh->cornerBufSize * 4, sizeof(I32));
	pCore->pEdges = pAlloc->pCalloc(pMesh->edgeBufSize * 4, sizeof(I32));
	pCore->vertAttribs.pArr = pAlloc->pCalloc(1, sizeof(StucAttrib));
	char posName[STUC_ATTRIB_NAME_MAX_LEN] = "position";
	Attrib *pPosAttrib = pCore->vertAttribs.pArr;
	stucInitAttrib(
		pAlloc,
		pPosAttrib,
		posName,
		pMesh->vertBufSize,
		true,
		STUC_ATTRIB_ORIGIN_MAP,
		STUC_ATTRIB_V3_F32
	);
	pCore->vertAttribs.count = 1;
	pCore->cornerAttribs.pArr = pAlloc->pCalloc(2, sizeof(StucAttrib));
	char uvName[STUC_ATTRIB_NAME_MAX_LEN] = "UVMap";
	Attrib *pUvAttrib = pCore->cornerAttribs.pArr;
	stucInitAttrib(
		pAlloc,
		pUvAttrib,
		uvName,
		pMesh->cornerBufSize,
		true,
		STUC_ATTRIB_ORIGIN_MAP,
		STUC_ATTRIB_V2_F32
	);
	char normalName[STUC_ATTRIB_NAME_MAX_LEN] = "normal";
	Attrib *pNormalAttrib = pCore->cornerAttribs.pArr + 1;
	stucInitAttrib(
		pAlloc,
		pNormalAttrib,
		normalName,
		pMesh->cornerBufSize,
		true,
		STUC_ATTRIB_ORIGIN_MAP,
		STUC_ATTRIB_V3_F32
	);
	pCore->cornerAttribs.count = 2;
	stucSetSpecialAttribs(pCtx, pMesh, 0xe); // 1110 - set pos stuc and normals
	return STUC_SUCCESS;
}

StucResult stucFillUsgSquaresMesh(StucMap pMap, StucUsg *pUsgArr) {
	for (I32 i = 0; i < pMap->usgArr.count; ++i) {
		Mesh *pUsgMesh = (Mesh *)pUsgArr[i].obj.pData;
		getUsgBoundsSquare(&pMap->usgArr.squares, pUsgMesh);
	}
	return STUC_SUCCESS;
}

static
void assignUsgToVertsInFace(
	const StucAlloc *pAlloc,
	StucMap pMap,
	Mesh *pMesh,
	Mesh *pSquares,
	Mesh *pFlatCutoff,
	I32 usgIdx,
	I32 faceIdx,
	I32 *pCellFaces,
	FaceRange *pSquaresFace
) {
	FaceRange mapFace = stucGetFaceRange(&pMap->mesh.core, pCellFaces[faceIdx], false);
	//the uv of corners 0 and 2 can be treated and min and max for the bounding square
	V2_F32 min = pSquares->pUvs[pSquaresFace->start];
	V2_F32 max = pSquares->pUvs[pSquaresFace->start + 2];
	if (!stucCheckFaceIsInBounds(min, max, mapFace, &pMap->mesh)) {
		return;
	}
	for (I32 l = 0; l < mapFace.size; ++l) {
		I32 vertIdx = pMap->mesh.core.pCorners[mapFace.start + l];
		V3_F32 vert = pMap->mesh.pVerts[vertIdx];
		if (stucIsPointInsideMesh(pAlloc, vert, pMesh)) {
			pMap->mesh.pUsg[vertIdx] = usgIdx + 1;
			if (pFlatCutoff &&
				stucIsPointInsideMesh(pAlloc, vert, pFlatCutoff)) {

				//negative indicates the vert is above the cutoff
				pMap->mesh.pUsg[vertIdx] *= -1;
			}
		}
	}
}

StucResult stucAssignUsgsToVerts(
	const StucAlloc *pAlloc,
	StucMap pMap,
	StucUsg *pUsgArr
) {
	Mesh *pSquares = &pMap->usgArr.squares;
	FaceCellsTable faceCellsTable = {0};
	I32 averageMapFacesPerFace = 0;
	Range faceRange = {.start = 0, .end = pSquares->core.faceCount};
	stucGetEncasingCells(
		pAlloc,
		pMap,
		pSquares,
		-1,
		faceRange,
		&faceCellsTable,
		&averageMapFacesPerFace
	);
	for (I32 i = 0; i < pMap->usgArr.count; ++i) {
		Mesh *pMesh = (Mesh *)pUsgArr[i].obj.pData;
		Mesh *pFlatCutoff = (Mesh *)pUsgArr[i].pFlatCutoff->pData;
		FaceRange squaresFace = stucGetFaceRange(&pSquares->core, i, false);
		FaceCells *pFaceCellsEntry = stucIdxFaceCells(&faceCellsTable, i, 0);
		for (I32 j = 0; j < pFaceCellsEntry->cellSize; ++j) {
			//put this cell stuff into a generic function
			// v v v
			I32 cellIdx = pFaceCellsEntry->pCells[j];
			Cell *pCell = pMap->quadTree.cellTable.pArr + cellIdx;
			STUC_ASSERT("", pCell->localIdx >= 0 && pCell->localIdx < 4);
			STUC_ASSERT("", pCell->initialized % 2 == pCell->initialized);
			I32 *pCellFaces;
			Range range = {0};
			if (pFaceCellsEntry->pCellType[j]) {
				pCellFaces = pCell->pEdgeFaces;
				range = pFaceCellsEntry->pRanges[j];
			}
			else if (pFaceCellsEntry->pCellType[j] != 1) {
				pCellFaces = pCell->pFaces;
				range.start = 0;
				range.end = pCell->faceSize;
			}
			else {
				continue;
			}
			// ^ ^ ^

			for (I32 k = range.start; k < range.end; ++k) {
				assignUsgToVertsInFace(
					pAlloc,
					pMap,
					pMesh,
					pSquares,
					pFlatCutoff,
					i,
					k,
					pCellFaces,
					&squaresFace
				);
			}
		}
		stucDestroyFaceCellsEntry(pAlloc, stucIdxFaceCells(&faceCellsTable, i, 0));
	}
	stucDestroyFaceCellsTable(pAlloc, &faceCellsTable);
	return STUC_SUCCESS;
}

static
void checkIfFaceIsClose(V3_F32 *pBc, F32 *pDist, bool *pClose, F32 *pClosestDist) {
	V3_F32 bcAbs = {fabs(pBc->d[0]), fabs(pBc->d[1]), fabs(pBc->d[2])};
	for (I32 edge = 0; edge < 3; ++edge) {
		I32 last = edge == 0 ? 2 : edge - 1;
		I32 next = (edge + 1) % 3;
		if (bcAbs.d[edge] < *pClosestDist && pBc->d[last] >= .0f && pBc->d[next] >= .0f) {
			*pDist = bcAbs.d[edge];
			*pClose = true;
			break;
		}
	}
	if (!*pClose) {
		//test tri verts
		for (I32 vert = 0; vert < 3; ++vert) {
			if (pBc->d[vert] < .0f) {
				continue;
			}
			I32 last = vert == 0 ? 2 : vert - 1;
			I32 next = (vert + 1) % 3;
			F32 thisDist = fabs(1.0f - pBc->d[vert]);
			if (thisDist < *pClosestDist && pBc->d[last] < .0f && pBc->d[next] < .0f) {
				*pDist = thisDist;
				*pClose = true;
				return;
			}
		}
	}
}

static
Result isFaceClosestToOrigin(
	Usg *pUsg,
	Mesh *pInMesh,
	V2_I32 tileMin,
	FaceRange *pInFace,
	FaceBounds *pFaceBounds,
	V3_F32 *pClosestBc,
	FaceRange *pClosestFace,
	I8 *pClosestFaceCorners,
	F32 *pClosestDist,
	bool *pRet
) {
	Result err = STUC_SUCCESS;
	V2_F32 fTileMin = {(F32)tileMin.d[0], (F32)tileMin.d[1]};
	I8 triCorners[4] = {0};
	V2_F32 triStuc[4] = {0};
	for (I32 k = 0; k < pInFace->size; ++k) {
		triStuc[k] = pInMesh->pUvs[pInFace->start + k];
	}
	//TODO move in stuc to 0 - 1 space if in another tile
	//(don't move origin, conversion to barycentric causes problems if you do that).
	//Determine how many uv tiles this face spans, then test barycentric for each tile
	//make the tile rasterization in getEnclosingCells a generic function, and reuse it here.
	I32 in = stucCheckIfFaceIsInsideTile(pInFace->size, triStuc, pFaceBounds, tileMin);
	STUC_THROW_IF(err, in, "", 0);
	for (I32 k = 0; k < pInFace->size; ++k) {
		_(triStuc + k V2SUBEQL fTileMin);
	}
	V3_F32 bc = stucGetBarycentricInFace(
		triStuc,
		triCorners,
		pInFace->size,
		pUsg->origin
	);
	I32 inside = bc.d[0] >= .0f && bc.d[1] >= .0f && bc.d[2] >= .0f;
	bool close = false;
	F32 dist = .0f;
	if (!inside) {
		checkIfFaceIsClose(&bc, &dist, &close, pClosestDist);
	}
	if (inside || close) {
		*pClosestBc = bc;
		*pClosestFace = *pInFace;
		pClosestFaceCorners[0] = triCorners[0];
		pClosestFaceCorners[1] = triCorners[1];
		pClosestFaceCorners[2] = triCorners[2];
		if (inside) {
			*pRet = true;
			return err;
		}
		*pClosestDist = dist;
	}
	*pRet = false;
	STUC_CATCH(0, err, ;);
	return err;
}

static
StucResult getClosestTriToOrigin(
	Usg *pUsg,
	Mesh *pInMesh,
	InFaceArr *pInFaceTable,
	I32 i,
	V3_F32 *pClosestBc,
	FaceRange *pClosestFace,
	I8 *pClosestFaceCorners,
	F32 *pClosestDist
) {
	Result err = STUC_SUCCESS;
	for (I32 j = 0; j < pInFaceTable[i].count; ++j) {
		FaceRange inFace = stucGetFaceRange(&pInMesh->core, pInFaceTable[i].pArr[j], false);
		FaceBounds faceBounds = {0};
		stucGetFaceBoundsForTileTest(&faceBounds, pInMesh, &inFace);
		V2_I32 minTile = faceBounds.min;
		V2_I32 maxTile = faceBounds.max;
		for (I32 l = minTile.d[1]; l <= maxTile.d[1]; ++l) {
			for (I32 m = minTile.d[0]; m <= maxTile.d[0]; ++m) {
				V2_I32 tileMin = {m, l};
				bool ret = false;
				err = isFaceClosestToOrigin(
					pUsg,
					pInMesh,
					tileMin,
					&inFace,
					&faceBounds,
					pClosestBc,
					pClosestFace,
					pClosestFaceCorners,
					pClosestDist,
					&ret
				);
				STUC_THROW_IF(err, true, "", 0);
				if (ret) {
					return err;
				}
			}
		}
	}
	STUC_ASSERT("", pClosestFace->idx >= 0);
	STUC_CATCH(0, err, ;);
	return err;
}

StucResult stucSampleInAttribsAtUsgOrigins(
	StucContext pCtx,
	StucMap pMap,
	Mesh *pInMesh,
	StucMesh *pSquares,
	InFaceArr *pInFaceTable
) {
	Result err = STUC_SUCCESS;
	STUC_THROW_IF(err, pSquares, "", 0);
	for (I32 i = 0; i < pSquares->faceCount; ++i) {
		Usg *pUsg = pMap->usgArr.pArr + pInFaceTable[i].usg;
		V3_F32 closestBc = {FLT_MAX, FLT_MAX, FLT_MAX};
		FaceRange closestFace = {.idx = -1};
		I8 closestFaceCorners[3] = {0};
		F32 closestDist = FLT_MAX;
		err = getClosestTriToOrigin(
			pUsg,
			pInMesh,
			pInFaceTable,
			i,
			&closestBc,
			&closestFace,
			closestFaceCorners,
			&closestDist
		);
		STUC_THROW_IF(err, true, "", 0);
		pInFaceTable[i].tri[0] = closestFace.start + closestFaceCorners[0];
		pInFaceTable[i].tri[1] = closestFace.start + closestFaceCorners[1];
		pInFaceTable[i].tri[2] = closestFace.start + closestFaceCorners[2];
		pInFaceTable[i].tbn =
			stucGetInterpolatedTbn(pInMesh, &closestFace, closestFaceCorners, closestBc);
		pInFaceTable[i].normal = *(V3_F32 *)&pInFaceTable[i].tbn.d[2];
		//add offset if current position will cause intersections with surface
		FaceRange face = stucGetFaceRange(pSquares, i, false);
		I32 triVerts[3] = {
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
		stucSetSpecialAttribs(pCtx, &squaresWrap, 0x02); //set only position
		// where a is the origin, ab is the normal, and c is the square vert
		V3_F32 a = barycentricToCartesian(tri, &closestBc);
		V3_F32 ab = pInFaceTable[i].normal;
		STUC_ASSERT("", face.start >= 0 && face.size >= 3);
		F32 tMax = -FLT_MAX;
		for (I32 j = 0; j < face.size; ++j) {
			I32 vert = pSquares->pCorners[face.start + j];
			V3_F32 ac = _(squaresWrap.pVerts[vert] V3SUB a);
			F32 t = _(ab V3DOT ac);
			if (t > tMax) {
				tMax = t;
			}
		}
		pInFaceTable[i].offset = tMax > .0f ? tMax : .0f;
		
		//TODO support usg for more than just normal maps,
		//     add a ui to select which attribs should be uniform.
		//     Ideally you should be able to do this per usg,
		//     though you'd need to store usg names for that to work.
	}
	STUC_CATCH(0, err, ;);
	return err;
}

static
void sampleUsgEntry(
	UsgInFace *pEntry,
	bool flatCutoff,
	V3_F32 uvw,
	V3_F32 *pPos,
	bool *pTransformed,
	V3_F32 *pUsgBc,
	const Mesh *pInMesh,
	V3_F32 *pNormal,
	V2_F32 tileMin,
	Mat3x3 *pTbn
) {
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
	//V3_F32 mapRealNormal = getCornerRealNormal(pMapMesh, &mapFace, stucCorner);
	//V3_F32 up = { .0f, .0f, 1.0f };
	//F32 upMask = abs(_(mapRealNormal V3DOT up));
	//pCorner->projNormalMasked = v3Normalize(v3Lerp(normal, pEntry->pEntry->normal, upMask));
	//pCorner->projNormalMasked = pEntry->pEntry->normal;
	*pNormal = pEntry->pEntry->normal;
}

bool stucSampleUsg(
	I32 stucCorner,
	V3_F32 uvw,
	V3_F32 *pPos,
	bool *pTransformed,
	V3_F32 *pUsgBc,
	FaceRange *pMapFace,
	StucMap pMap,
	I32 inFace,
	const Mesh *pInMesh,
	V3_F32 *pNormal,
	V2_F32 tileMin,
	bool useFlatCutoff,
	bool flatCutoffOveride,
	Mat3x3 *pTbn
) {
	Mesh *pMapMesh = &pMap->mesh;
	I32 mapCorner = pMapMesh->core.pCorners[pMapFace->start + stucCorner];
	I32 usg = pMapMesh->pUsg[mapCorner];
	if (usg) {
		bool flatCutoff = flatCutoffOveride ? useFlatCutoff : usg < 0;
		usg = abs(usg) - 1;
		U32 sum = usg + inFace;
		I32 hash = stucFnvHash((I8 *)&sum, 4, pMap->usgArr.tableSize);
		UsgInFace *pEntry = pMap->usgArr.pInFaceTable + hash;
		do {
			if (pEntry->face == inFace && pEntry->pEntry && pEntry->pEntry->usg == usg) {
				break;
			}
			pEntry = pEntry->pNext;
		} while(pEntry);
		//STUC_ASSERT("", pEntry);
		if (pEntry) {
			sampleUsgEntry(
				pEntry,
				flatCutoff,
				uvw,
				pPos,
				pTransformed,
				pUsgBc,
				pInMesh,
				pNormal,
				tileMin,
				pTbn
			);
			return true;
		}
	}
	return false;
}