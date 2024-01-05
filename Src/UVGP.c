#define CELL_MAX_VERTS 16

#include <string.h>
#include <stdio.h>
#include "Types.h"
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include "Platform.h"
#include <miniz.h>


typedef struct Cell {
	uint32_t localIndex;
	uint32_t initialized;
	struct Cell *children;
	int32_t vertAmount;
	int32_t *verts;
	int32_t cellIndex;
} Cell;

int32_t fillCell() {
	return 0;
}

int32_t cellIndex;
int32_t leafAmount;

// TODO
// - Reduce the bits written to the UVGP file for vert and loop indices, based on the total amount, in order to save space.
//   No point storing them as 32 bit if there's only like 4,000 verts
// - Split compressed data into chunks maybe?
// - Split whole quadtree into chunks?

void calcCellBounds(Cell *cell, Vec2 *boundsMin, Vec2 *boundsMax) {
	float xSide = (float)(cell->localIndex % 2);
	float ySide = (float)(((cell->localIndex + 2) / 2) % 2);
	boundsMin->x = xSide * .5;
	boundsMin->y = ySide * .5;
	boundsMax->x = 1.0 - (1.0 - xSide) * .5;
	boundsMax->y = 1.0 - (1.0 - ySide) * .5;
}

Cell *findEnclosingCell(Cell *rootCell, Vec2 pos) {
	Vec2 cellBoundsMin = {.x = .0, .y = .0};
	Vec2 cellBoundsMax = {.x = 1.0, .y = 1.0};
	Cell *cell = rootCell;
	int32_t depth = -1;
	while (true) {
		if (!cell->children) {
			return cell;
		}
		Vec2 midPoint = _(_(_(cellBoundsMax V2SUB cellBoundsMin) V2MULS .5) V2ADD cellBoundsMin);
		depth++;
		int32_t childIndex = (pos.x >= midPoint.x) + (pos.y < midPoint.y) * 2;
		cell = cell->children + childIndex;
		Vec2 parentBoundsMin = cellBoundsMin;
		calcCellBounds(cell, &cellBoundsMin, &cellBoundsMax);
		_(&cellBoundsMin V2DIVSEQL (float)pow(2.0, depth));
		_(&cellBoundsMax V2DIVSEQL (float)pow(2.0, depth));
		_(&cellBoundsMin V2ADDEQL parentBoundsMin);
		_(&cellBoundsMax V2ADDEQL parentBoundsMin);
	};
}

void allocateChildren(Cell *cell) {
	cell->children = malloc(sizeof(Cell) * 4);
	for (int32_t i = 0; i < 4; ++i) {
		cell->children[i].cellIndex = rand();
		cellIndex++;
		cell->children[i].localIndex = (uint32_t)i;
		cell->children[i].initialized = 0u;
		cell->children[i].children = NULL;
		cell->children[i].vertAmount = -1;
		cell->children[i].verts = NULL;
	}
	leafAmount += 4;
}

void addEnclosedVertsToCell(Cell **cellStack, Cell *cell, int32_t *cellStackPointer, int32_t *cellStackBase, Vert *vertBuffer) {
	Vec2 cellBoundsMin, cellBoundsMax;
	calcCellBounds(cell, &cellBoundsMin, &cellBoundsMax);
	_(&cellBoundsMin V2DIVSEQL (float)pow(2.0, *cellStackPointer - 1));
	_(&cellBoundsMax V2DIVSEQL (float)pow(2.0, *cellStackPointer - 1));
	for (int32_t i = (*cellStackPointer) - 1; i > *cellStackBase; --i) {
		Vec2 ancestorBoundsMin, ancestorBoundsMax;
		calcCellBounds(cellStack[i], &ancestorBoundsMin, &ancestorBoundsMax);
		_(&ancestorBoundsMin V2DIVSEQL (float)pow(2.0, i - 1));
		_(&cellBoundsMin V2ADDEQL ancestorBoundsMin);
		_(&cellBoundsMax V2ADDEQL ancestorBoundsMin);
	}
	// Get enclosed verts if not already present
	// First, determine which verts are enclosed, and mark them by negating
	cell->vertAmount = 0;
	Cell* parentCell = cellStack[*cellStackPointer - 1];
	for (int32_t i = 0; i < parentCell->vertAmount; ++i) {
		int32_t vert = parentCell->verts[i] - 1;
		int32_t isInside = (vertBuffer[vert].pos.x >= cellBoundsMin.x) &&
			(vertBuffer[vert].pos.y >= cellBoundsMin.y) &&
			(vertBuffer[vert].pos.x < cellBoundsMax.x) &&
			(vertBuffer[vert].pos.y < cellBoundsMax.y);
		if (isInside) {
			parentCell->verts[i] *= -1;
			cell->vertAmount++;
		}
	}
	// Now that the amount is known, allocate the array for the current cell,
	// and copy over the marked verts from the parent cell
	cell->verts = malloc(sizeof(int32_t) * cell->vertAmount);
	int32_t vertsNextIndex = 0;
	for (int32_t i = 0; i < parentCell->vertAmount; ++i) {
		if (parentCell->verts[i] < 0) {
			cell->verts[vertsNextIndex] = parentCell->verts[i] *= -1;
			vertsNextIndex++;
		}
	}
}

void processCell(Cell **cellStack, int32_t *cellStackPointer, int32_t *cellStackBase, Cell *rootCell, Vert *vertBuffer) {
	// First, calculate the positions of the cells bounding points
	Cell *cell = cellStack[*cellStackPointer];
	if (cell->vertAmount < 0) {
		addEnclosedVertsToCell(cellStack, cell, cellStackPointer, cellStackBase, vertBuffer);
	}

	// If more than CELL_MAX_VERTS in cell, then subdivide cell
	int32_t hasChildren = cell->vertAmount > CELL_MAX_VERTS;
	if (hasChildren) {
		// Get number of children
		int32_t childAmount = 0;
		if (!cell->children) {
			leafAmount--;
			allocateChildren(cell);
		}
		for (int32_t i = 0; i < 4; ++i) {
			childAmount += (int32_t)cell->children[i].initialized;
		}
		// If the cell has children, and they are not yet all initialized,
		// then add the next one to the stack
		if (childAmount < 4) {
			(*cellStackPointer)++;
			cellStack[*cellStackPointer] = cell->children + childAmount;
			return;
		}
	}
	// Otherwise, set the current cell as initialized, and pop it off the stack
	cell->initialized = 1;
	(*cellStackPointer)--;
}

void createQuadTree(Cell *rootCell, int32_t maxTreeDepth, int32_t vertAmount, Vert *vertBuffer) {
	Cell **cellStack = malloc(sizeof(Cell *) * maxTreeDepth);
	rootCell->cellIndex = cellIndex;
	cellIndex++;
	cellStack[0] = rootCell;
	rootCell->localIndex = 0;
	rootCell->initialized = 1;
	allocateChildren(rootCell);
	rootCell->vertAmount = vertAmount;
	rootCell->verts = malloc(sizeof(int32_t) * vertAmount);
	for (int32_t i = 0; i < vertAmount; ++i) {
		rootCell->verts[i] = i + 1;
	}
	cellStack[1] = rootCell->children;
	int32_t cellStackPointer = 1;
	int32_t cellStackBase = 0;
	do {
		processCell(cellStack, &cellStackPointer, &cellStackBase, rootCell, vertBuffer);
	} while(cellStackPointer >= 0);
	free(cellStack);
}

typedef struct {
	unsigned char *data;
	int32_t nextBitIndex;
	int32_t byteIndex;
} UvgpData;

void encodeValue(UvgpData *data, unsigned char *value, int32_t lengthInBits) {
	unsigned char valueCopy[10] = {0};
	int32_t lengthInBytes = lengthInBits / 8;
	lengthInBytes += (lengthInBits - lengthInBytes * 8) > 0;
	for (int32_t i = 1; i <= lengthInBytes; ++i) {
		valueCopy[i] |= value[i - 1];
	}
	for (int32_t i = 7; i >= 1; --i) {
		valueCopy[i] <<= data->nextBitIndex;
		unsigned char nextByteCopy = valueCopy[i - 1];
		nextByteCopy >>= 8 - data->nextBitIndex;
		valueCopy[i] |= nextByteCopy;
	}
	int32_t writeUpTo = lengthInBytes + (data->nextBitIndex > 0);
	for (int32_t i = 0; i < writeUpTo; ++i) {
		data->data[data->byteIndex + i] |= valueCopy[i + 1];
	}
	data->nextBitIndex = data->nextBitIndex + lengthInBits;
	data->byteIndex += data->nextBitIndex / 8;
	data->nextBitIndex %= 8;
}

void encodeString(UvgpData *data, unsigned char *value, int32_t lengthInBits) {
	unsigned char valueCopy[64] = {0};
	int32_t lengthInBytes = lengthInBits / 8;
	for (int32_t i = 1; i <= lengthInBytes; ++i) {
		valueCopy[i] |= value[i - 1];
	}
	for (int32_t i = lengthInBytes + 1; i >= 1; --i) {
		valueCopy[i] <<= data->nextBitIndex;
		unsigned char nextByteCopy = valueCopy[i - 1];
		nextByteCopy >>= 8 - data->nextBitIndex;
		valueCopy[i] |= nextByteCopy;
	}
	for (int32_t i = 1; i < lengthInBytes + 1; ++i) {
		data->data[data->byteIndex] |= valueCopy[i];
		data->byteIndex++;
	}
}

void encodeDataAndDestroyQuadTree(Cell *rootCell, int32_t maxTreeDepth, UvgpData *data) {
	Cell **cellStack = malloc(sizeof(Cell *) * maxTreeDepth);
	cellStack[0] = rootCell;
	int32_t cellStackPointer = 0;
	unsigned char internalCell = 0u;
	unsigned char leafCell = 1u;
	encodeValue(data, &internalCell, 1);
	do {
		Cell *cell = cellStack[cellStackPointer];
		int32_t nextChild = 0;
		if (cell->children) {
			if (cell->initialized) {
				encodeValue(data, &internalCell, 1);
			}
			cell->initialized = 0;
			for (int32_t i = 0; i < 4; ++i) {
				nextChild += cell->children[i].initialized == 0;
			}
			if (nextChild < 4) {
				cellStackPointer++;
				cellStack[cellStackPointer] = cell->children + nextChild;
				continue;
			}
			free(cell->children);
		}
		else {
			encodeValue(data, &leafCell, 1);
			cell->initialized = 0;
			for (int32_t i = 0; i < cell->vertAmount; ++i) {
				encodeValue(data, (unsigned char *)(cell->verts + i), 32);
			}
		}
		if (cell->verts) {
			free(cell->verts);
		}
		cellStackPointer--;
	} while(cellStackPointer >= 0);
	free(rootCell);
	free(cellStack);
}

__declspec(dllexport) void UvgpExportUvgpFile(int32_t vertAmount, Vert *vertBuffer, int32_t faceAmount, Face *faceBuffer) {
	printf("Piss from inside c\n");
	printf("%d vertices, and %d faces\n", vertAmount, faceAmount);


	
	//time_t t;
	//srand((unsigned)time(&t));

	cellIndex = 0;
	leafAmount = 0;

	Cell *rootCell = malloc(sizeof(Cell));
	int32_t maxTreeDepth = log(CELL_MAX_VERTS * vertAmount) / log(4) + 2;
	createQuadTree(rootCell, maxTreeDepth, vertAmount, vertBuffer);

	/*
	FILE* file;
	file = fopen("T:/workshop_folders/UVGP/DebugOutput.ppm", "w");
	fprintf(file, "P3\n%d %d\n255\n", 512, 512);
	for (int32_t i = 0; i < 512; ++i) {
		for (int32_t j = 0; j < 512; ++j) {
			unsigned char red, green, blue, alpha;
			int32_t linearIndex = (i * 512) + j;
			float pixelSize = 1.0 / (float)512;
			Vec2 pixelPos = {.x = pixelSize * j, .y = pixelSize * i};
			Cell *enclosingCell = findEnclosingCell(rootCell, pixelPos);
			red = (unsigned char)(enclosingCell->cellIndex % 256);
			green = (unsigned char)(enclosingCell->cellIndex << 3 % 256);
			blue = (unsigned char)(enclosingCell->cellIndex << 1 % 256);
			alpha = (unsigned char)1.0;
			fprintf(file, "%d %d %d\n", red, green, blue);
		}
	}
	fclose(file);
	*/

	UvgpData header;
	UvgpData data;
#define VERT_ATTRIBUTE_AMOUNT 3
	char *vertAttributes[VERT_ATTRIBUTE_AMOUNT];
	char *vertAttributeTypes[VERT_ATTRIBUTE_AMOUNT];
	int32_t vertAttributeSize[VERT_ATTRIBUTE_AMOUNT];
	vertAttributes[0] = "position.x";
	vertAttributeTypes[0] = "f";
	vertAttributeSize[0] = 32;
	vertAttributes[1] = "position.y";
	vertAttributeTypes[1] = "f";
	vertAttributeSize[1] = 32;
	vertAttributes[2] = "position.z";
	vertAttributeTypes[2] = "f";
	vertAttributeSize[2] = 32;
#define LOOP_ATTRIBUTE_AMOUNT 3
	char *loopAttributes[LOOP_ATTRIBUTE_AMOUNT];
	char *loopAttributeTypes[LOOP_ATTRIBUTE_AMOUNT];
	int32_t loopAttributeSize[LOOP_ATTRIBUTE_AMOUNT];
	loopAttributes[0] = "normal.x";
	loopAttributeTypes[0] = "f";
	loopAttributeSize[0] = 32;
	loopAttributes[1] = "normal.y";
	loopAttributeTypes[1] = "f";
	loopAttributeSize[1] = 32;
	loopAttributes[2] = "normal.z";
	loopAttributeTypes[2] = "f";
	loopAttributeSize[2] = 32;
	int32_t headerAttributesByteLength = 0;
	int32_t vertAttributeByteLength = 0;
	for (int32_t i = 0; i < VERT_ATTRIBUTE_AMOUNT; ++i) {
		headerAttributesByteLength += strlen(vertAttributes[i]) + 1;
		headerAttributesByteLength += strlen(vertAttributeTypes[i]) + 1;
		headerAttributesByteLength += 1;
		vertAttributeByteLength += vertAttributeSize[i];
	}
	int32_t loopAttributeByteLength = 0;
	for (int32_t i = 0; i < LOOP_ATTRIBUTE_AMOUNT; ++i) {
		headerAttributesByteLength += strlen(loopAttributes[i]) + 1;
		headerAttributesByteLength += strlen(loopAttributeTypes[i]) + 1;
		headerAttributesByteLength += 1;
		loopAttributeByteLength += loopAttributeSize[i];
	}
	int64_t headerLengthInBits = 32 + 
	                     headerAttributesByteLength * 8 +
	                     32 +
	                     32 +
	                     32;
	int64_t dataLengthInBits = vertAttributeByteLength * 8 * vertAmount +
	                     2 + (32 + loopAttributeByteLength * 8) * 4 * faceAmount +
	                     cellIndex +
	                     32 * leafAmount +
	                     32 * vertAmount;
	int32_t headerLengthInBytes = (int32_t)(headerLengthInBits / 8 + 2);
	int32_t dataLengthInBytes = (int32_t)(dataLengthInBits / 8 + 2);
	data.byteIndex = 0;
	data.nextBitIndex = 0;
	header.byteIndex = 0;
	header.nextBitIndex = 0;

	data.data = calloc(dataLengthInBytes, sizeof(unsigned char));
	for (int32_t i = 0; i < vertAmount; ++i) {
		encodeValue(&data, (unsigned char *)&vertBuffer[i].pos.x, 32);
		encodeValue(&data, (unsigned char *)&vertBuffer[i].pos.y, 32);
		encodeValue(&data, (unsigned char *)&vertBuffer[i].pos.z, 32);
	}
	for (int32_t i = 0; i < faceAmount; ++i) {
		encodeValue(&data, (unsigned char *)&faceBuffer[i].loopAmount, 2);
		for (int32_t j = 0; j < faceBuffer[i].loopAmount; ++j) {
			encodeValue(&data, (unsigned char *)&faceBuffer[i].loops[j].vert, 32);
			encodeValue(&data, (unsigned char *)&faceBuffer[i].loops[j].normal.x, 32);
			encodeValue(&data, (unsigned char *)&faceBuffer[i].loops[j].normal.y, 32);
			encodeValue(&data, (unsigned char *)&faceBuffer[i].loops[j].normal.z, 32);
		}
	}
	encodeDataAndDestroyQuadTree(rootCell, maxTreeDepth, &data);

	int64_t dataLength = data.byteIndex + (data.nextBitIndex > 0);
	int64_t dataLengthExtra = dataLength / 1000;
	dataLengthExtra += ((dataLength * 1000) - dataLength) > 0;
	dataLengthExtra += 12;
	unsigned long compressedDataLength = dataLength + dataLengthExtra;
	unsigned char *compressedData = malloc(compressedDataLength);
	int32_t zResult = compress(compressedData, &compressedDataLength, data.data, dataLength);
	switch(zResult) {
		case Z_OK:
			printf("Successfully compressed UVGP data\n");
			break;
		case Z_MEM_ERROR:
			printf("Failed to compress UVGP data, memory error\n");
			break;
		case Z_BUF_ERROR:
			printf("Failed to compress UVGP data, output buffer too small\n");
			break;
	}

	printf("Compressed data is %lu long\n", compressedDataLength);

	header.data = calloc(headerLengthInBytes, sizeof(unsigned char));
	encodeValue(&header, (unsigned char *)&compressedDataLength, 32);
	for (int32_t i = 0; i < VERT_ATTRIBUTE_AMOUNT; ++i) {
		encodeString(&header, (unsigned char *)vertAttributes[i], (strlen(vertAttributes[i]) + 1) * 8);
		encodeString(&header, (unsigned char *)vertAttributeTypes[i], (strlen(vertAttributeTypes[i]) + 1) * 8);
		encodeValue(&header, (unsigned char *)&vertAttributeSize[i], 8);
	}
	for (int32_t i = 0; i < LOOP_ATTRIBUTE_AMOUNT; ++i) {
		encodeString(&header, (unsigned char *)loopAttributes[i], (strlen(loopAttributes[i]) + 1) * 8);
		encodeString(&header, (unsigned char *)loopAttributeTypes[i], (strlen(loopAttributeTypes[i]) + 1) * 8);
		encodeValue(&header, (unsigned char *)&loopAttributeSize[i], 8);
	}
	encodeValue(&header, (unsigned char *)&vertAmount, 32);
	encodeValue(&header, (unsigned char *)&faceAmount, 32);
	encodeValue(&header, (unsigned char *)&cellIndex, 32);

	// CRC for uncompressed data, not compressed!
	
	UvgpFile file;
	uvgpFileOpen(&file, "T:/workshop_folders/UVGP/TestOutputDir/File.uvgp");
	uvgpFileWrite(&file, header.data, header.byteIndex + (header.nextBitIndex > 0));
	uvgpFileWrite(&file, compressedData, (int32_t)compressedDataLength);
	uvgpFileClose(&file);

	free(data.data);

	printf("Finished UVGP export\n");
}

__declspec(dllexport) void uvgpProjectOntoMesh(const char *objName) {
	printf("Obj name: %s\n", objName);
}
