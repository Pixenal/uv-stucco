#include <string.h>

#include <EnclosingCells.h>
#include <QuadTree.h>
#include <MapFile.h>
#include <MathUtils.h>
#include <Utils.h>
#include <Context.h>

typedef struct {
	int32_t averageRuvmFacesPerFace;
	int8_t *pCellInits;
	FaceInfo faceInfo;
	FaceBounds faceBounds;
	int8_t *pCellTable;
	Cell **ppCells;
	int8_t *pCellType;
} Vars;

static void checkIfFaceIsInsideTile(Vars *pVars, int32_t *pIsInsideBuffer,
                                    int32_t *pFaceVertInside, Mesh *pMesh,
									V2_I32 tileMin) {
	FaceInfo *pFaceInfo = &pVars->faceInfo;
	FaceBounds *pFaceBounds = &pVars->faceBounds;
	for (int32_t i = 0; i < pVars->faceInfo.size; ++i) {
		//check if current edge intersects tile
		V2_F32 *loop = pMesh->pUvs + pFaceInfo->start + i;
		int32_t nextLoopIndex = pFaceInfo->start + (i + 1) % pFaceInfo->size;
		V2_F32 *loopNext = pMesh->pUvs + nextLoopIndex;
		V2_F32 loopDir = _(*loopNext V2SUB *loop);
		V2_F32 loopCross = v2Cross(loopDir);
		for (int32_t j = 0; j < 4; ++j) {
			V2_F32 cellPoint = {tileMin.d[0] + j % 2, tileMin.d[1] + j / 2};
			V2_F32 cellDir = _(cellPoint V2SUB *loop);
			float dot = _(loopCross V2DOT cellDir);
			pIsInsideBuffer[j] *= dot < .0f;
		}
		//in addition, test for face verts inside tile
		//edge cases may not be cause by the above, like if a face entered the tile,
		//and then exited the same side, with a single vert in the tile.
		//Checking for verts will catch this:
		*pFaceVertInside += _(*loop V2GREAT pFaceBounds->fMin) &&
		                    _(*loop V2LESSEQL pFaceBounds->fMax);
	}
}

static int32_t getCellsForFaceWithinTile(RuvmAllocator *pAlloc, RuvmMap pMap,
                                         Mesh *pMeshIn, Vars *pVars,
                                         EnclosingCellsInfo *pCellsBuffer,
										 V2_I32 tileMin) {
	int32_t isInsideBuffer[4] = {1, 1, 1, 1};
	int32_t faceVertInside = 0;
	checkIfFaceIsInsideTile(pVars, isInsideBuffer, &faceVertInside,
	                        pMeshIn, tileMin);
	int32_t isInside = isInsideBuffer[0] || isInsideBuffer[1] ||
	                   isInsideBuffer[2] || isInsideBuffer[3];
	int32_t isFullyEnclosed = isInsideBuffer[0] && isInsideBuffer[1] &&
	                          isInsideBuffer[2] && isInsideBuffer[3];
	if (isFullyEnclosed) {
		return 1;
	}
	if (!faceVertInside && !isInside) {
		//face is not inside current tile
		return 0;
	}
	//find fully enclosing cell using clipped face
	ruvmGetAllEnclosingCells(pAlloc, pMap->quadTree.pRootCell, pCellsBuffer,
	                             pVars->pCellInits, pMeshIn, pVars->faceInfo, tileMin);
	return 0;
}

static int32_t checkBranchCellIsLinked(EnclosingCellsInfo *pCellsBuffer, int32_t index) {
	Cell *cell = pCellsBuffer->ppCells[index];
	for (int32_t j = 0; j < pCellsBuffer->cellSize; ++j) {
		if (pCellsBuffer->pCellType[j] || index == j) {
			continue;
		}
		Cell *leaf = pCellsBuffer->ppCells[j];
		for (int32_t k = 0; k < leaf->linkEdgeSize; ++k) {
			if (cell->cellIndex == leaf->pLinkEdges[k]) {
				return 1;
			}
		}
	}
	return 0;
}

static void removeNonLinkedBranchCells(EnclosingCellsInfo *pCellsBuffer) {
	for (int32_t i = 0; i < pCellsBuffer->cellSize;) {
		if (!pCellsBuffer->pCellType[i]) {
			i++;
			continue;
		}
		if (checkBranchCellIsLinked(pCellsBuffer, i)) {
			i++;
			continue;
		}
		Cell *pCell = pCellsBuffer->ppCells[i];
		pCellsBuffer->faceTotal -= pCell->edgeFaceSize;
		pCellsBuffer->faceTotalNoDup -= pCell->edgeFaceSize;
		for (int32_t j = i; j < pCellsBuffer->cellSize - 1; ++j) {
			pCellsBuffer->ppCells[j] = pCellsBuffer->ppCells[j + 1];
			pCellsBuffer->pCellType[j] = pCellsBuffer->pCellType[j + 1];
		}
		pCellsBuffer->cellSize--;
	}
}

static void copyCellsIntoTotalList(EnclosingCells *pEc, EnclosingCellsInfo *pCellsBuffer,
                            int32_t faceIndex, RuvmAllocator *pAlloc) {
	FaceCellsInfo *pEntry = pEc->pFaceCellsInfo + faceIndex;
	pEc->cellFacesTotal += pCellsBuffer->faceTotalNoDup;
	pEntry->pCells = pAlloc->pMalloc(sizeof(Cell *) * pCellsBuffer->cellSize);
	pEntry->pCellType = pAlloc->pMalloc(pCellsBuffer->cellSize);
	memcpy(pEntry->pCells, pCellsBuffer->ppCells, sizeof(Cell *) *
	       pCellsBuffer->cellSize);
	memcpy(pEntry->pCellType, pCellsBuffer->pCellType, pCellsBuffer->cellSize);
	pEntry->cellSize = pCellsBuffer->cellSize;
	pEntry->faceSize = pCellsBuffer->faceTotalNoDup;
	if (pCellsBuffer->faceTotalNoDup > pEc->cellFacesMax) {
		pEc->cellFacesMax = pCellsBuffer->faceTotalNoDup;
	}
}

static void recordCellsInTable(EnclosingCells *pEc, Vars *pVars,
                               EnclosingCellsInfo *pCellsBuffer) {
	for (int32_t i = 0; i < pCellsBuffer->cellSize; ++i) {
		Cell *pCell = pCellsBuffer->ppCells[i];
		//must be != 0, not > 0, so as to catch entries set to -1
		if (pVars->pCellTable[pCell->cellIndex] != 0) {
			continue;
		}
		int32_t cellType = pCellsBuffer->pCellType[i];
		pVars->pCellTable[pCell->cellIndex] = cellType + 1;
		Cell *pStack[32] = {0};
		int8_t childrenLeft[32] = {0};
		pStack[0] = pCell;
		int32_t stackPointer = 0;
		pEc->uniqueFaces += cellType == 1 ?
			pCell->edgeFaceSize : pCell->faceSize;
		if (cellType != 0 || pCell->pChildren == NULL) {
			continue;
		}
		//if cell is not a leaf, and if it isn't an edgefaces cell,
		//then subtract faces from any child cells that have been counted,
		//and/ or set their table entry to -1 so they're not added
		//to the count in future
		do {
			Cell *pChild = pStack[stackPointer];
			int32_t nextChild = childrenLeft[stackPointer];
			if (nextChild > 3) {
				stackPointer--;
				continue;
			}
			int32_t childType = pVars->pCellTable[pChild->cellIndex];
			if (pChild != pCell) {
				//must be > 0, so that cells with an entry of -1 arn't touched,
				// as they haven't been added to uniqueFaces
				if (childType > 0) {
					pEc->uniqueFaces -= childType == 2 ?
						pChild->edgeFaceSize : pChild->faceSize;
				}
				//set to -1 so this cell isn't added to the count in future
				pVars->pCellTable[pChild->cellIndex] = -1;
			}
			if (pChild->pChildren == NULL) {
				stackPointer--;
				continue;
			}
			childrenLeft[stackPointer]++;
			stackPointer++;
			pStack[stackPointer] = pChild->pChildren + nextChild;
			childrenLeft[stackPointer] = 0;
		} while (stackPointer >= 0);
	}
}

static int32_t getCellsForSingleFace(RuvmAllocator *pAlloc, RuvmMap pMap,
                                     Mesh *pMeshIn, EnclosingCells *pEc,
                                     Vars *pVars, int32_t faceIndex) {
	EnclosingCellsInfo cellsBuffer = {0};
	cellsBuffer.ppCells = pVars->ppCells;
	cellsBuffer.pCellType = pVars->pCellType;
	FaceBounds *pFaceBounds = &pVars->faceBounds;
	for (int32_t i = pFaceBounds->min.d[1]; i <= pFaceBounds->max.d[1]; ++i) {
		for (int32_t j = pFaceBounds->min.d[0]; j <= pFaceBounds->max.d[0]; ++j) {
			V2_I32 tileMin = {j, i};
			//continue until the smallest cell that fully encloses the face is found (result == 0).
			//if face fully encloses the while uv tile (result == 1), then return (root cell will be used).
			//if the face is not within the current tile, then skip tile (result == 2).
			if (getCellsForFaceWithinTile(pAlloc, pMap, pMeshIn, pVars,
				                          &cellsBuffer, tileMin)) {
				//fully enclosed
				return 1;
			}
		}
	}
	removeNonLinkedBranchCells(&cellsBuffer);
	recordCellsInTable(pEc, pVars, &cellsBuffer);
	copyCellsIntoTotalList(pEc, &cellsBuffer, faceIndex, pAlloc);
	return 0;
}

void ruvmGetEnclosingCellsForAllFaces(RuvmAllocator *pAlloc, RuvmMap pMap,
                                      Mesh *pMeshIn, EnclosingCells *pEc) {
	Vars vars = {0};
	pEc->pFaceCellsInfo =
		pAlloc->pMalloc(sizeof(FaceCellsInfo) * pMeshIn->mesh.faceCount);
	vars.pCellInits = pAlloc->pMalloc(pMap->quadTree.cellCount);
	vars.pCellTable = pAlloc->pCalloc(pMap->quadTree.cellCount, sizeof(int8_t));
	vars.ppCells = pAlloc->pMalloc(sizeof(void *) * pMap->quadTree.cellCount);
	vars.pCellType = pAlloc->pMalloc(pMap->quadTree.cellCount);
	for (int32_t i = 0; i < pMeshIn->mesh.faceCount; ++i) {
		int32_t start, end;
		start = vars.faceInfo.start = pMeshIn->mesh.pFaces[i];
		end = vars.faceInfo.end = pMeshIn->mesh.pFaces[i + 1];
		vars.faceInfo.size = end - start;
		getFaceBounds(&vars.faceBounds, pMeshIn->pUvs, vars.faceInfo);
		vars.faceBounds.fMinSmall = vars.faceBounds.fMin;
		vars.faceBounds.fMaxSmall = vars.faceBounds.fMax;
		vars.faceBounds.min = v2FloorAssign(&vars.faceBounds.fMin);
		vars.faceBounds.max = v2FloorAssign(&vars.faceBounds.fMax);
		_(&vars.faceBounds.fMax V2ADDEQLS 1.0f);
		pEc->pFaceCellsInfo[i].faceBounds = vars.faceBounds;
		if (getCellsForSingleFace(pAlloc, pMap, pMeshIn, pEc, &vars, i)) {
			Cell *rootCell = pMap->quadTree.pRootCell;
			pEc->pFaceCellsInfo[i].pCells = pAlloc->pMalloc(sizeof(Cell *));
			*pEc->pFaceCellsInfo[i].pCells = rootCell;
			pEc->cellFacesTotal += rootCell->faceSize;
		}
		vars.averageRuvmFacesPerFace += pEc->pFaceCellsInfo[i].faceSize;
		//printf("Total cell amount: %d\n", faceCellsInfo[i].cellSize);
	}
	pAlloc->pFree(vars.pCellTable);
	pAlloc->pFree(vars.pCellInits);
	pAlloc->pFree(vars.ppCells);
	pAlloc->pFree(vars.pCellType);
	vars.averageRuvmFacesPerFace /= pMeshIn->mesh.faceCount;
}

void ruvmDestroyEnclosingCells(RuvmAllocator *pAlloc, EnclosingCells *pEc) {
	pAlloc->pFree(pEc->pCellFaces);
	pAlloc->pFree(pEc->pFaceCellsInfo);
}
