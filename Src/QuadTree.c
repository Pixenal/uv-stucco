#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <QuadTree.h>
#include <Context.h>
#include <MapFile.h>

static int32_t counter = 0;

static void calcCellBounds(Cell *cell) {
	float xSide = (float)(cell->localIndex % 2);
	float ySide = (float)(((cell->localIndex + 2) / 2) % 2);
	cell->boundsMin.x = xSide * .5;
	cell->boundsMin.y = ySide * .5;
	cell->boundsMax.x = 1.0 - (1.0 - xSide) * .5;
	cell->boundsMax.y = 1.0 - (1.0 - ySide) * .5;
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
							      RuvmVec2 *verts, Vec2 midPoint, iVec2 *commonSides, iVec2 *signs) {
	commonSides->x = commonSides->y = 1;
	iVec2* pSides = pAlloc->pMalloc(sizeof(iVec2) * loopSize);
	for (int32_t i = 0; i < loopSize; ++i) {
		int32_t loopIndex = loopStart + i;
		pSides[i].x = verts[loopIndex].x >= midPoint.x;
		pSides[i].y = verts[loopIndex].y < midPoint.y;
		for (int32_t j = 0; j < i; ++j) {
			commonSides->x *= pSides[i].x == pSides[j].x;
			commonSides->y *= pSides[i].y == pSides[j].y;
		}
	}
	if (!commonSides->x && !commonSides->y) {
		pAlloc->pFree(pSides);
		return 0;
	}
	signs->x = pSides[0].x;
	signs->y = pSides[0].y;
	pAlloc->pFree(pSides);
	if (commonSides->x && commonSides->y) {
		return 1;
	}
	else {
		return 2;
	}
}

static int32_t findFaceQuadrant(RuvmAllocator *pAlloc, int32_t loopSize, int32_t faceStart,
                                Mesh *pMesh, Vec2 midPoint,
						        iVec2 *commonSides, iVec2 *signs) {
	commonSides->x = commonSides->y = 1;
	iVec2 *pSides = pAlloc->pMalloc(sizeof(iVec2) * loopSize);
	for (int32_t i = 0; i < loopSize; ++i) {
		int32_t vertIndex = pMesh->mesh.pLoops[faceStart + i];
		pSides[i].x = pMesh->pVerts[vertIndex].x >= midPoint.x;
		pSides[i].y = pMesh->pVerts[vertIndex].y < midPoint.y;
		for (int32_t j = 0; j < i; ++j) {
			commonSides->x *= pSides[i].x == pSides[j].x;
			commonSides->y *= pSides[i].y == pSides[j].y;
		}
	}
	signs->x = pSides[0].x;
	signs->y = pSides[0].y;
	pAlloc->pFree(pSides);
	return commonSides->x && commonSides->y;
}

void ruvmGetAllEnclosingCells(RuvmAllocator* pAlloc, Cell *pRootCell, EnclosingCellsInfo *pEnclosingCellsInfo,
                                  int8_t *pCellInits, Mesh *pMesh, FaceInfo faceInfo,
								  iVec2 tileMin) {
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
		Vec2 midPoint = _(_(_(cell->boundsMax V2SUB cell->boundsMin) V2MULS .5) V2ADD cell->boundsMin);
		iVec2 signs;
		iVec2 commonSides;
		midPoint.x += (float)tileMin.x;
		midPoint.y += (float)tileMin.y;
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
				children[cellStackPointer].a = !signs.x && !signs.y;
				children[cellStackPointer].b = signs.x && !signs.y;
				children[cellStackPointer].c = !signs.x && signs.y;
				children[cellStackPointer].d = signs.x && signs.y;
				break;
			}
			case 2: {
				int32_t top, bottom = top = commonSides.y;
				int32_t left, right = left = commonSides.x;
				top *= !signs.y;
				bottom *= signs.y;
				left *= !signs.x;
				right *= signs.x;
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

Cell *findEnclosingCell(Cell *rootCell, Vec2 pos) {
	Vec2 cellBoundsMin = {.x = .0, .y = .0};
	Vec2 cellBoundsMax = {.x = 1.0, .y = 1.0};
	Cell *cell = rootCell;
	int32_t depth = -1;
	while (true) {
		if (!cell->pChildren) {
			return cell;
		}
		Vec2 midPoint = _(_(_(cellBoundsMax V2SUB cellBoundsMin) V2MULS .5) V2ADD cellBoundsMin);
		depth++;
		int32_t childIndex = (pos.x >= midPoint.x) + (pos.y < midPoint.y) * 2;
		cell = cell->pChildren + childIndex;
		cellBoundsMin = cell->boundsMin;
		cellBoundsMax = cell->boundsMax;
	};
}

static void setCellBounds(Cell *cell, Cell *parentCell, int32_t cellStackPointer) {
	calcCellBounds(cell);
	_(&cell->boundsMin V2DIVSEQL (float)pow(2.0, cellStackPointer));
	_(&cell->boundsMax V2DIVSEQL (float)pow(2.0, cellStackPointer));
	Vec2 *ancestorBoundsMin = &parentCell->boundsMin;
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
	Vec2 midPoint = pParentCell->pChildren[1].boundsMin;
	for (int32_t i = 0; i < pParentCell->faceSize; ++i) {
		int32_t face = pParentCell->pFaces[i];
		int32_t faceStart = pMesh->mesh.pFaces[face];
		int32_t faceEnd = pMesh->mesh.pFaces[face + 1];
		int32_t faceLoopSize = faceEnd - faceStart;
		iVec2 signs;
		iVec2 commonSides;
		int32_t result = findFaceQuadrant(&pContext->alloc, faceLoopSize, faceStart,
		                                  pMesh, midPoint,
		                                  &commonSides, &signs);
		if (result) {
			int32_t child = signs.x + signs.y * 2;
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
	rootCell->boundsMax.x = rootCell->boundsMax.y = 1.0f;
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
