#include <float.h>

#include <Usg.h>
#include <QuadTree.h>
#include <Context.h>
#include <MapFile.h>
#include <Error.h>

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
	int32_t hash = ruvmFnvHash(sum, 4, pHitEdges->size);
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
bool hitTestTri(RuvmAlloc *pAlloc, V2_F32 point, V2_F32 *pTri,
                int32_t *pVerts, HitEdgeTable *pHitEdges) {
	//add bounding squares to speed up tests maybe?
	V3_F32 bc = cartesianToBarycentric(pTri, &point);
	if (!v3IsFinite(bc)) {
		//degenerate
		return false;
	}
	if (bc.d[0] < .0f || bc.d[1] < .0f || bc.d[2] < .0f) {
		return false;
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
void getTri(V2_F32 *pTri, int32_t *pVerts, Mesh *pMesh, FaceRange *pFace,
            int32_t a, int32_t b, int32_t c) {
	pVerts[0] = pMesh->mesh.pLoops[pFace->start + a];
	pVerts[1] = pMesh->mesh.pLoops[pFace->start + b];
	pVerts[2] = pMesh->mesh.pLoops[pFace->start + c];
	pTri[0] = *(V2_F32 *)&pMesh->pVerts[pVerts[0]];
	pTri[1] = *(V2_F32 *)&pMesh->pVerts[pVerts[1]];
	pTri[2] = *(V2_F32 *)&pMesh->pVerts[pVerts[2]];

}

static
bool isPointInsideMesh(RuvmAlloc *pAlloc, V3_F32 pointV3, Mesh *pMesh) {
	//winding number test, with ray aligned with z axis
	//(so flatten point and mesh into 2D (x,y))
	V2_F32 point = {pointV3.d[0], pointV3.d[1]};
	int32_t wind = 0;
	HitEdgeTable hitEdges = {0};
	hitEdges.pTable = pAlloc->pCalloc(pMesh->mesh.edgeCount, sizeof(HitEdge));
	for (int32_t i = 0; i < pMesh->mesh.faceCount; ++i) {
		FaceRange face = getFaceRange(&pMesh->mesh, i, false);
		V2_F32 tri[3] = {0};
		int32_t triVerts[3] = {0};
		if (face.size == 3) {
			getTri(tri, triVerts, pMesh, &face, 0, 1, 2);
			wind += hitTestTri(pAlloc, point, tri, triVerts, &hitEdges);
		}
		else if (face.size == 4) {
			getTri(tri, triVerts, pMesh, &face, 0, 1, 2);
			wind += hitTestTri(pAlloc, point, tri, triVerts, &hitEdges);
			getTri(tri, triVerts, pMesh, &face, 2, 3, 0);
			wind += hitTestTri(pAlloc, point, tri, triVerts, &hitEdges);
		}
		else {
			FaceTriangulated tris = triangulateFace(*pAlloc, face, pMesh->pVerts,
			                                        pMesh->mesh.pLoops, false);
			for (int32_t j = 0; j < tris.loopCount; j += 3) {
				getTri(tri, triVerts, pMesh, &face, tris.pLoops[j],
				       tris.pLoops[j + 1], tris.pLoops[j + 2]);
				wind += hitTestTri(pAlloc, point, tri, triVerts, &hitEdges);
			}
		}
	}
}

RuvmResult assignUsgsToVerts(RuvmContext pContext,
                             RuvmMap pMap, RuvmObject *pUsgArr) {
	for (int32_t i = 0; i < pMap->usgArr.count; ++i) {
		Mesh *pMesh = (Mesh *)pUsgArr[i].pData;
		V2_F32 min = {FLT_MAX, FLT_MAX};
		V2_F32 max = {-FLT_MAX, -FLT_MAX};
		for (int32_t j = 0; j < pMesh->mesh.vertCount; ++j) {
			V3_F32 vert = pMesh->pVerts[j];
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
		int32_t faces[2] = {0, 4};
		int32_t loops[4] = {0, 1, 2, 3};
		int32_t edges[4] = {0, 1, 2, 3};
		V2_F32 uvs[4] = {
			{min.d[0], min.d[1]},
			{max.d[0], min.d[1]},
			{max.d[0], max.d[1]},
			{min.d[0], max.d[1]}
		};
		Mesh bounds = {
			.mesh.type.type = RUVM_OBJECT_DATA_MESH_INTERN,
			.mesh.faceCount = 1,
			.mesh.loopCount = 4,
			.mesh.edgeCount = 4,
			.mesh.vertCount = 4,
			.mesh.pFaces = &faces,
			.mesh.pLoops = &loops,
			.mesh.pEdges = &edges,
			.pUvs = &uvs,
		};
		FaceCellsTable faceCellsTable = {0};
		int32_t averageMapFacesPerFace = 0;
		getEncasingCells(&pContext->alloc, pMap, &bounds, &faceCellsTable,
						 &averageMapFacesPerFace);
		for (int32_t j = 0; j < faceCellsTable.pFaceCells[0].cellSize; ++j) {
			//put this cell stuff into a generic function
			// v v v
			Cell *pCell = faceCellsTable.pFaceCells[0].pCells[j];
			RUVM_ASSERT("", pCell->localIndex >= 0 && pCell->localIndex < 4);
			RUVM_ASSERT("", pCell->initialized % 2 == pCell->initialized);
			int32_t *pCellFaces;
			Range range = { 0 };
			if (faceCellsTable.pFaceCells[0].pCellType[j]) {
				pCellFaces = pCell->pEdgeFaces;
				range = faceCellsTable.pFaceCells[0].pRanges[j];
			}
			else if (faceCellsTable.pFaceCells[0].pCellType[j] != 1) {
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
				if (!checkFaceIsInBounds(min, max, ruvmFace, &pMap->mesh)) {
					continue;
				}
				for (int32_t l = 0; l < ruvmFace.size; ++l) {
					int32_t vertIndex = pMap->mesh.mesh.pLoops[ruvmFace.start + l];
					V3_F32 vert = pMap->mesh.pVerts[vertIndex];
					if (isPointInsideMesh(&pContext->alloc, vert, pMesh)) {
						pMap->mesh.pUsg[vertIndex] = i + 1;
					}
				}
			}
		}
	}
	return RUVM_SUCCESS;
}