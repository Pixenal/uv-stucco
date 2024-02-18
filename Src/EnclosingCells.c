#include <string.h>

#include <EnclosingCells.h>
#include <QuadTree.h>
#include <MapFile.h>

static void checkIfFaceIsInsideTile(EnclosingCellsVars *pEcVars, int32_t *pIsInsideBuffer,
		                     int32_t *pFaceVertInside, Mesh *pMesh, iVec2 tileMin) {
	FaceInfo *pFaceInfo = &pEcVars->faceInfo;
	FaceBounds *pFaceBounds = &pEcVars->faceBounds;
	for (int32_t i = 0; i < pEcVars->faceInfo.size; ++i) {
		//check if current edge intersects tile
		Vec2 *loop = pMesh->pUvs + pFaceInfo->start + i;
		int32_t nextLoopIndex = pFaceInfo->start + (i + 1) % pFaceInfo->size;
		Vec2 *loopNext = pMesh->pUvs + nextLoopIndex;
		Vec2 loopDir = _(*loopNext V2SUB *loop);
		Vec2 loopCross = vec2Cross(loopDir);
		for (int32_t j = 0; j < 4; ++j) {
			Vec2 cellPoint = {tileMin.x + j % 2, tileMin.y + j / 2};
			Vec2 cellDir = _(cellPoint V2SUB *loop);
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

static int32_t getCellsForFaceWithinTile(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                                  EnclosingCellsInfo *pCellsBuffer, iVec2 tileMin) {
	int32_t isInsideBuffer[4] = {1, 1, 1, 1};
	int32_t faceVertInside = 0;
	checkIfFaceIsInsideTile(pEcVars, isInsideBuffer, &faceVertInside, &pArgs->mesh, tileMin);
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
	ruvmGetAllEnclosingCells(pArgs->pMap->quadTree.pRootCell, pCellsBuffer,
	                             pEcVars->pCellInits, &pArgs->mesh, pEcVars->faceInfo, tileMin);
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

static void copyCellsIntoTotalList(EnclosingCellsVars *pEcVars, EnclosingCellsInfo *pCellsBuffer,
                            int32_t faceIndex, RuvmAllocator *pAlloc) {
	FaceCellsInfo *pEntry = pEcVars->pFaceCellsInfo + faceIndex;
	pEcVars->cellFacesTotal += pCellsBuffer->faceTotalNoDup;
	pEntry->pCells = pAlloc->pMalloc(sizeof(Cell *) * pCellsBuffer->cellSize);
	pEntry->pCellType = pAlloc->pMalloc(pCellsBuffer->cellSize);
	memcpy(pEntry->pCells, pCellsBuffer->ppCells, sizeof(Cell *) *
	       pCellsBuffer->cellSize);
	memcpy(pEntry->pCellType, pCellsBuffer->pCellType, pCellsBuffer->cellSize);
	pEntry->cellSize = pCellsBuffer->cellSize;
	pEntry->faceSize = pCellsBuffer->faceTotalNoDup;
	if (pCellsBuffer->faceTotalNoDup > pEcVars->cellFacesMax) {
		pEcVars->cellFacesMax = pCellsBuffer->faceTotalNoDup;
	}
}

static void recordCellsInTable(EnclosingCellsVars *pEcVars, EnclosingCellsInfo *pCellsBuffer) {
	for (int32_t i = 0; i < pCellsBuffer->cellSize; ++i) {
		Cell *pCell = pCellsBuffer->ppCells[i];
		//must be != 0, not > 0, so as to catch entries set to -1
		if (pEcVars->pCellTable[pCell->cellIndex] != 0) {
			continue;
		}
		int32_t cellType = pCellsBuffer->pCellType[i];
		pEcVars->pCellTable[pCell->cellIndex] = cellType + 1;
		Cell *pStack[32] = {0};
		int8_t childrenLeft[32] = {0};
		pStack[0] = pCell;
		int32_t stackPointer = 0;
		pEcVars->uniqueFaces += cellType == 1 ?
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
			int32_t childType = pEcVars->pCellTable[pChild->cellIndex];
			if (pChild != pCell) {
				//must be > 0, so that cells with an entry of -1 arn't touched,
				// as they haven't been added to uniqueFaces
				if (childType > 0) {
					pEcVars->uniqueFaces -= childType == 2 ?
						pChild->edgeFaceSize : pChild->faceSize;
				}
				//set to -1 so this cell isn't added to the count in future
				pEcVars->pCellTable[pChild->cellIndex] = -1;
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

static int32_t getCellsForSingleFace(ThreadArg *pArgs, EnclosingCellsVars *pEcVars,
                                     int32_t faceIndex) {
	EnclosingCellsInfo cellsBuffer = {0};
	cellsBuffer.ppCells = pEcVars->ppCells;
	cellsBuffer.pCellType = pEcVars->pCellType;
	FaceBounds *pFaceBounds = &pEcVars->faceBounds;
	for (int32_t i = pFaceBounds->min.y; i <= pFaceBounds->max.y; ++i) {
		for (int32_t j = pFaceBounds->min.x; j <= pFaceBounds->max.x; ++j) {
			iVec2 tileMin = {j, i};
			//continue until the smallest cell that fully encloses the face is found (result == 0).
			//if face fully encloses the while uv tile (result == 1), then return (root cell will be used).
			//if the face is not within the current tile, then skip tile (result == 2).
			if (getCellsForFaceWithinTile(pArgs, pEcVars, &cellsBuffer, tileMin)) {
				//fully enclosed
				return 1;
			}
		}
	}
	removeNonLinkedBranchCells(&cellsBuffer);
	recordCellsInTable(pEcVars, &cellsBuffer);
	copyCellsIntoTotalList(pEcVars, &cellsBuffer, faceIndex, &pArgs->alloc);
	return 0;
}

void ruvmGetEnclosingCellsForAllFaces(ThreadArg *pArgs, EnclosingCellsVars *pEcVars) {
	FaceBounds *pFaceBounds = &pEcVars->faceBounds;
	pEcVars->pCellInits = pArgs->alloc.pMalloc(pArgs->pMap->quadTree.cellCount);
	pEcVars->pCellTable = pArgs->alloc.pCalloc(pArgs->pMap->quadTree.cellCount, sizeof(int8_t));
	pEcVars->ppCells = pArgs->alloc.pMalloc(sizeof(void *) * pArgs->pMap->quadTree.cellCount);
	pEcVars->pCellType = pArgs->alloc.pMalloc(pArgs->pMap->quadTree.cellCount);
	for (int32_t i = 0; i < pArgs->mesh.faceCount; ++i) {
		int32_t start, end;
		start = pEcVars->faceInfo.start = pArgs->mesh.pFaces[i];
		end = pEcVars->faceInfo.end = pArgs->mesh.pFaces[i + 1];
		pEcVars->faceInfo.size = end - start;
		getFaceBounds(pFaceBounds, pArgs->mesh.pUvs, pEcVars->faceInfo);
		pFaceBounds->fMinSmall = pFaceBounds->fMin;
		pFaceBounds->fMaxSmall = pFaceBounds->fMax;
		pFaceBounds->min = vec2FloorAssign(&pFaceBounds->fMin);
		pFaceBounds->max = vec2FloorAssign(&pFaceBounds->fMax);
		_(&pFaceBounds->fMax V2ADDEQLS 1.0f);
		pEcVars->pFaceCellsInfo[i].faceBounds = *pFaceBounds;
		if (getCellsForSingleFace(pArgs, pEcVars, i)) {
			Cell *rootCell = pArgs->pMap->quadTree.pRootCell;
			pEcVars->pFaceCellsInfo[i].pCells = pArgs->alloc.pMalloc(sizeof(Cell *));
			*pEcVars->pFaceCellsInfo[i].pCells = rootCell;
			pEcVars->cellFacesTotal += rootCell->faceSize;
		}
		pEcVars->averageRuvmFacesPerFace += pEcVars->pFaceCellsInfo[i].faceSize;
		//printf("Total cell amount: %d\n", faceCellsInfo[i].cellSize);
	}
	pArgs->alloc.pFree(pEcVars->pCellTable);
	pArgs->alloc.pFree(pEcVars->pCellInits);
	pArgs->alloc.pFree(pEcVars->ppCells);
	pArgs->alloc.pFree(pEcVars->pCellType);
	pEcVars->averageRuvmFacesPerFace /= pArgs->mesh.faceCount;
}
