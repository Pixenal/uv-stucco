#pragma once

#include <QuadTree.h>
#include <RUVM.h>

void ruvmWriteDebugImage(Cell *pRootCell);
void ruvmWriteRuvmFile(RuvmContext pContext, const char *pName, RuvmMesh *pMesh);
void ruvmLoadRuvmFile(RuvmContext pContext, RuvmMap pMapFile, char *filePath);
void ruvmDestroyRuvmFile(RuvmContext pContext, RuvmMap pMapFIle);

void ruvmIoSetCustom(RuvmContext pContext, RuvmIo *pIo);
void ruvmIoSetDefault(RuvmContext pContext);
