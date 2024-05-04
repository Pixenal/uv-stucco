#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <QuadTree.h>
#include <Context.h>
#include <MapFile.h>
#include <MathUtils.h>
#include <Utils.h>

static int32_t counter = 0;

static void calcCellBounds(Cell *cell) {
	float xSide = (float)(cell->localIndex % 2);
	float ySide = (float)(((cell->localIndex + 2) / 2) % 2);
	cell->boundsMin.d[0] = xSide * .5;
	cell->boundsMin.d[1] = ySide * .5;
	cell->boundsMax.d[0] = 1.0 - (1.0 - xSide) * .5;
	cell->boundsMax.d[1] = 1.0 - (1.0 - ySide) * .5;
}

static void addCellToEnclosingCells(Cell *cell, EnclosingCellsInfo *pEnclosingCellsInfo, int32_t edge) {
	int32_t faceSize = edge ? cell->edgeFaceSize : cell->faceSize;
	pEnclosingCellsInfo->faceTotal += faceSize;
	int32_t dupIndex = -1;
	for (int32_t i = 0; i < pEnclosingCellsInfo->cellSize; ++i) {
		if (pEnclosingCellsInfo->ppCells[i] == cell) {
			dupIndex = i;
			break;
		}
	}
	if (dupIndex >= 0) {
		if (!pEnclosingCellsInfo->pCellType[dupIndex] && edge) {
			pEnclosingCellsInfo->pCellType[dupIndex] = 2;
		}
		return;
	}
	pEnclosingCellsInfo->ppCells[pEnclosingCellsInfo->cellSize] = cell;
	pEnclosingCellsInfo->pCellType[pEnclosingCellsInfo->cellSize] = edge;
	pEnclosingCellsInfo->cellSize++;;
	pEnclosingCellsInfo->faceTotalNoDup += faceSize;
}

static int32_t findFaceQuadrantUv(RuvmAllocator* pAlloc, int32_t loopSize, int32_t loopStart,
							      V2_F32 *verts, V2_F32 midPoint, V2_I32 *commonSides, V2_I32 *signs) {
	commonSides->d[0] = commonSides->d[1] = 1;
	V2_I32* pSides = pAlloc->pMalloc(sizeof(V2_I32) * loopSize);
	for (int32_t i = 0; i < loopSize; ++i) {
		int32_t loopIndex = loopStart + i;
		pSides[i].d[0] = verts[loopIndex].d[0] >= midPoint.d[0];
		pSides[i].d[1] = verts[loopIndex].d[1] < midPoint.d[1];
		for (int32_t j = 0; j < i; ++j) {
			commonSides->d[0] *= pSides[i].d[0] == pSides[j].d[0];
			commonSides->d[1] *= pSides[i].d[1] == pSides[j].d[1];
		}
	}
	if (!commonSides->d[0] && !commonSides->d[1]) {
		pAlloc->pFree(pSides);
		return 0;
	}
	signs->d[0] = pSides[0].d[0];
	signs->d[1] = pSides[0].d[1];
	pAlloc->pFree(pSides);
	if (commonSides->d[0] && commonSides->d[1]) {
		return 1;
	}
	else {
		return 2;
	}
}

static int32_t findFaceQuadrant(RuvmAllocator *pAlloc, int32_t loopSize, int32_t faceStart,
                                Mesh *pMesh, V2_F32 midPoint,
						        V2_I32 *commonSides, V2_I32 *signs) {
	commonSides->d[0] = commonSides->d[1] = 1;
	V2_I32 *pSides = pAlloc->pMalloc(sizeof(V2_I32) * loopSize);
	for (int32_t i = 0; i < loopSize; ++i) {
		int32_t vertIndex = pMesh->mesh.pLoops[faceStart + i];
		pSides[i].d[0] = pMesh->pVerts[vertIndex].d[0] >= midPoint.d[0];
		pSides[i].d[1] = pMesh->pVerts[vertIndex].d[1] < midPoint.d[1];
		for (int32_t j = 0; j < i; ++j) {
			commonSides->d[0] *= pSides[i].d[0] == pSides[j].d[0];
			commonSides->d[1] *= pSides[i].d[1] == pSides[j].d[1];
		}
	}
	signs->d[0] = pSides[0].d[0];
	signs->d[1] = pSides[0].d[1];
	pAlloc->pFree(pSides);
	return commonSides->d[0] && commonSides->d[1];
}

void ruvmGetAllEnclosingCells(RuvmAllocator* pAlloc, Cell *pRootCell, EnclosingCellsInfo *pEnclosingCellsInfo,
                                  int8_t *pCellInits, Mesh *pMesh, FaceInfo faceInfo,
								  V2_I32 tileMin) {
	typedef struct {
		int32_t a;
		int32_t b;
		int32_t c;
		int32_t d;
	} Children;
	Cell *cellStack[16];
	Children children[16];
	cellStack[0] = pRootCell;
	pCellInits[0] = 0;
	int32_t cellStackPointer = 0;
	for (int32_t i = 0; i < 4; ++i) {
		pCellInits[pRootCell->pChildren[i].cellIndex] = 0;
	}
	do {
		Cell *cell = cellStack[cellStackPointer];
		if (!cell->pChildren) {
			addCellToEnclosingCells(cell, pEnclosingCellsInfo, 0);
			cellStackPointer--;
			pCellInits[cell->cellIndex] = 1;
			continue;
		}
		if (pCellInits[cell->cellIndex]) {
			int32_t nextChild = -1;
			for (int32_t i = 0; i < 4; ++i) {
				if (!pCellInits[cell->pChildren[i].cellIndex] &&
				    *((int32_t *)(children + cellStackPointer) + i)) {
					nextChild = i;
					break;
				}
			}
			if (nextChild == -1) {
				cellStackPointer--;
				continue;
			}
			cellStackPointer++;
			cellStack[cellStackPointer] = cell->pChildren + nextChild;
			continue;
		}
		V2_F32 midPoint = _(_(_(cell->boundsMax V2SUB cell->boundsMin) V2MULS .5) V2ADD cell->boundsMin);
		V2_I32 signs;
		V2_I32 commonSides;
		midPoint.d[0] += (float)tileMin.d[0];
		midPoint.d[1] += (float)tileMin.d[1];
		int32_t result = findFaceQuadrantUv(pAlloc, faceInfo.size, faceInfo.start, pMesh->pUvs, midPoint,
		                                    &commonSides, &signs);
		switch (result) {
			case 0: {
				//addCellToEnclosingCells(cell, pEnclosingCellsInfo, 0);
				//cellStackPointer--;
				//pCellInits[cell->cellIndex] = 1;
				//continue;
				children[cellStackPointer].a = 1;
				children[cellStackPointer].b = 1;
				children[cellStackPointer].c = 1;
				children[cellStackPointer].d = 1;
				break;
			}
			case 1: {
				children[cellStackPointer].a = !signs.d[0] && !signs.d[1];
				children[cellStackPointer].b = signs.d[0] && !signs.d[1];
				children[cellStackPointer].c = !signs.d[0] && signs.d[1];
				children[cellStackPointer].d = signs.d[0] && signs.d[1];
				break;
			}
			case 2: {
				int32_t top, bottom = top = commonSides.d[1];
				int32_t left, right = left = commonSides.d[0];
				top *= !signs.d[1];
				bottom *= signs.d[1];
				left *= !signs.d[0];
				right *= signs.d[0];
				children[cellStackPointer].a = top || left;
				children[cellStackPointer].b = top || right;
				children[cellStackPointer].c = bottom || left;
				children[cellStackPointer].d = bottom || right;
				break;
			}
		}
		addCellToEnclosingCells(cell, pEnclosingCellsInfo, 1);
		pCellInits[cell->cellIndex] = 1;
		for (int32_t i = 0; i < 4; ++i) {
			pCellInits[cell->pChildren[i].cellIndex] = 0;
		}
		int32_t nextChild = 0;
		for (int32_t i = 0; i < 4; ++i) {
			if (*((int32_t *)(children + cellStackPointer) + i)) {
				nextChild = i;
				break;
			}
		}
		cellStackPointer++;
		cellStack[cellStackPointer] = cell->pChildren + nextChild;
	} while (cellStackPointer >= 0);
}

Cell *findEnclosingCell(Cell *rootCell, V2_F32 pos) {
	V2_F32 cellBoundsMin = {.d[0] = .0, .d[1] = .0};
	V2_F32 cellBoundsMax = {.d[0] = 1.0, .d[1] = 1.0};
	Cell *cell = rootCell;
	int32_t depth = -1;
	while (true) {
		if (!cell->pChildren) {
			return cell;
		}
		V2_F32 midPoint = _(_(_(cellBoundsMax V2SUB cellBoundsMin) V2MULS .5) V2ADD cellBoundsMin);
		depth++;
		int32_t childIndex = (pos.d[0] >= midPoint.d[0]) + (pos.d[1] < midPoint.d[1]) * 2;
		cell = cell->pChildren + childIndex;
		cellBoundsMin = cell->boundsMin;
		cellBoundsMax = cell->boundsMax;
	};
}

static void setCellBounds(Cell *cell, Cell *parentCell, int32_t cellStackPointer) {
	calcCellBounds(cell);
	_(&cell->boundsMin V2DIVSEQL (float)pow(2.0, cellStackPointer));
	_(&cell->boundsMax V2DIVSEQL (float)pow(2.0, cellStackPointer));
	V2_F32 *ancestorBoundsMin = &parentCell->boundsMin;
	_(&cell->boundsMin V2ADDEQL *ancestorBoundsMin);
	_(&cell->boundsMax V2ADDEQL *ancestorBoundsMin);
}

static int32_t checkIfLinkedEdge(Cell *pChild, Cell *pAncestor, Mesh *pMesh) {
	for (int32_t k = 0; k < pAncestor->edgeFaceSize; ++k) {
		int32_t faceIndex = pAncestor->pEdgeFaces[k];
		FaceInfo face;
		face.start = pMesh->mesh.pFaces[faceIndex];
		face.end = pMesh->mesh.pFaces[faceIndex + 1];
		face.size = face.end - face.start;
		//doesn't catch cases where edge intersect with bounds,
		//replace with a better method
		if (checkFaceIsInBounds(pChild->boundsMin, pChild->boundsMax, face, pMesh)) {
			return 1;
		}
	}
	return 0;
}

void addLinkEdgesToCells(RuvmContext pContext, Cell* pParentCell, Mesh *pMesh,
                         Cell **pCellStack, int32_t cellStackPointer) {
	int32_t buf[32];
	int32_t bufSize;
	for (int32_t i = 0; i < 4; ++i) {
		bufSize = 0;
		Cell *pChild = pParentCell->pChildren + i;
		for (int32_t j = cellStackPointer; j >= 0; --j) {
			Cell *pAncestor = pCellStack[j];
			if (checkIfLinkedEdge(pChild, pAncestor, pMesh)) {
				buf[bufSize] = pAncestor->cellIndex;
				bufSize++;
			}
		}
		if (bufSize) {
			pChild->pLinkEdges = pContext->alloc.pMalloc(sizeof(int32_t) * bufSize);
			pChild->linkEdgeSize = bufSize;
			memcpy(pChild->pLinkEdges, buf, sizeof(int32_t) * bufSize);
		}
	}
}

static void addEnclosedVertsToCell(RuvmContext pContext, Cell *pParentCell,
                                   Mesh *pMesh, int8_t *pFaceFlag) {
	// Get enclosed verts if not already present
	// First, determine which verts are enclosed, and mark them by negating
	V2_F32 midPoint = pParentCell->pChildren[1].boundsMin;
	for (int32_t i = 0; i < pParentCell->faceSize; ++i) {
		int32_t face = pParentCell->pFaces[i];
		int32_t faceStart = pMesh->mesh.pFaces[face];
		int32_t faceEnd = pMesh->mesh.pFaces[face + 1];
		int32_t faceLoopSize = faceEnd - faceStart;
		V2_I32 signs;
		V2_I32 commonSides;
		int32_t result = findFaceQuadrant(&pContext->alloc, faceLoopSize, faceStart,
		                                  pMesh, midPoint,
		                                  &commonSides, &signs);
		if (result) {
			int32_t child = signs.d[0] + signs.d[1] * 2;
			pFaceFlag[i] = child + 1;
			pParentCell->pChildren[child].faceSize++;
		}
		else {
			pFaceFlag[i] = -1;
			pParentCell->edgeFaceSize++;
		}
	}
	for (int32_t i = 0; i < 4; ++i) {
		Cell *cell = pParentCell->pChildren + i;
		if (cell->faceSize) {
			cell->pFaces = pContext->alloc.pMalloc(sizeof(int32_t) * cell->faceSize);
		}
	}
	if (pParentCell->edgeFaceSize) {
		pParentCell->pEdgeFaces = pContext->alloc.pMalloc(sizeof(int32_t) * pParentCell->edgeFaceSize);
	}
	int32_t facesSize[4] = {0};
	int32_t edgeFacesSize = 0;
	for (int32_t i = 0; i < pParentCell->faceSize; ++i) {
		if (!pFaceFlag[i]) {
			continue;
		}
		if (pFaceFlag[i] > 0) {
			int32_t child = pFaceFlag[i] - 1;
			Cell *cell = pParentCell->pChildren + child;
			cell->pFaces[facesSize[child]] = pParentCell->pFaces[i];
			facesSize[child]++;
		}
		else {
			pParentCell->pEdgeFaces[edgeFacesSize] = pParentCell->pFaces[i];
			edgeFacesSize++;
		}
		pFaceFlag[i] = 0;
	}
}

static void allocateChildren(RuvmContext pContext, Cell *parentCell,
                             int32_t cellStackPointer, RuvmMap pMap) {
	parentCell->pChildren = pContext->alloc.pCalloc(4, sizeof(Cell));
	for (int32_t i = 0; i < 4; ++i) {
		// v for visualizing quadtree v
		//cell->children[i].cellIndex = rand();
		Cell *cell = parentCell->pChildren + i;
		cell->cellIndex = pMap->quadTree.cellCount;
		pMap->quadTree.cellCount++;
		cell->localIndex = (uint32_t)i;
		setCellBounds(cell, parentCell, cellStackPointer);
	}
	pMap->quadTree.leafCount += 4;
}


static void processCell(RuvmContext pContext, Cell **pCellStack,
                        int32_t *pCellStackPointer, Mesh *pMesh,
                        int8_t *pFaceFlag, RuvmMap pMap) {
	Cell *cell = pCellStack[*pCellStackPointer];
	// If more than CELL_MAX_VERTS in cell, then subdivide cell
	int32_t hasChildren = cell->faceSize > CELL_MAX_VERTS;
	if (hasChildren) {
		// Get number of children
		int32_t childSize = 0;
		if (!cell->pChildren) {
			pMap->quadTree.leafCount--;
			allocateChildren(pContext, cell,*pCellStackPointer, pMap);
			addEnclosedVertsToCell(pContext, cell, pMesh, pFaceFlag);
			addLinkEdgesToCells(pContext, cell, pMesh, pCellStack, *pCellStackPointer);
		}
		for (int32_t i = 0; i < 4; ++i) {
			childSize += (int32_t)cell->pChildren[i].initialized;
		}
		// If the cell has children, and they are not yet all initialized,
		// then add the next one to the stack
		if (childSize < 4) {
			(*pCellStackPointer)++;
			pCellStack[*pCellStackPointer] = cell->pChildren + childSize;
			return;
		}
	}
	// Otherwise, set the current cell as initialized, and pop it off the stack
	cell->initialized = 1;
	(*pCellStackPointer)--;
}

int32_t calculateMaxTreeDepth(int32_t vertSize) {
	return log(CELL_MAX_VERTS * vertSize) / log(4) + 2;
}

void ruvmCreateQuadTree(RuvmContext pContext, RuvmMap pMap) {
	QuadTree *pQuadTree = &pMap->quadTree;
	Mesh *pMesh = &pMap->mesh;
	pQuadTree->cellCount = 0;
	pQuadTree->leafCount = 0;
	counter = 0;

	pQuadTree->pRootCell = pContext->alloc.pCalloc(1, sizeof(Cell));
	Cell *rootCell = pQuadTree->pRootCell;
	pQuadTree->maxTreeDepth = 32;

	Cell *cellStack[256];
	rootCell->cellIndex = 0;
	pQuadTree->cellCount = 1;
	cellStack[0] = rootCell;
	rootCell->boundsMax.d[0] = rootCell->boundsMax.d[1] = 1.0f;
	rootCell->initialized = 1;
	int8_t *pFaceFlag = pContext->alloc.pCalloc(pMesh->mesh.faceCount, sizeof(int8_t));
	rootCell->faceSize = pMesh->mesh.faceCount;
	rootCell->pFaces = pContext->alloc.pMalloc(sizeof(int32_t) * pMesh->mesh.faceCount);
	for (int32_t i = 0; i < pMesh->mesh.faceCount; ++i) {
		rootCell->pFaces[i] = i;
	}
	allocateChildren(pContext, rootCell, 0, pMap);
	addEnclosedVertsToCell(pContext, rootCell, pMesh, pFaceFlag);
	addLinkEdgesToCells(pContext, rootCell, pMesh, cellStack, 0);
	cellStack[1] = rootCell->pChildren;
	int32_t cellStackPointer = 1;
	do {
		processCell(pContext, cellStack, &cellStackPointer, pMesh, pFaceFlag, pMap);
		counter++;
	} while(cellStackPointer >= 0);
	pContext->alloc.pFree(pFaceFlag);
	printf("Created quadTree -- cells: %d, leaves: %d\n", pQuadTree->cellCount, pQuadTree->leafCount);
}

void ruvmDestroyQuadTree(RuvmContext pContext, Cell *rootCell) {
	Cell *cellStack[32];
	cellStack[0] = rootCell;
	int32_t cellStackPointer = 0;
	do {
		Cell *cell = cellStack[cellStackPointer];
		int32_t nextChild = 0;
		if (cell->pChildren) {
			cell->initialized = 0;
			for (int32_t i = 0; i < 4; ++i) {
				nextChild += cell->pChildren[i].initialized == 0;
			}
			if (nextChild < 4) {
				cellStackPointer++;
				cellStack[cellStackPointer] = cell->pChildren + nextChild;
				continue;
			}
			pContext->alloc.pFree(cell->pChildren);
		}
		else {
			cell->initialized = 0;
		}
		if (cell->pLinkEdges) {
			pContext->alloc.pFree(cell->pLinkEdges);
		}
		if (cell->pFaces) {
			pContext->alloc.pFree(cell->pFaces);
		}
		if (cell->pEdgeFaces) {
			pContext->alloc.pFree(cell->pEdgeFaces);
		}
		cellStackPointer--;
	} while(cellStackPointer >= 0);
	pContext->alloc.pFree(rootCell);
}
