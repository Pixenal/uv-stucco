#pragma once

#include <QuadTree.h>
#include <RUVM.h>

//void ruvmWriteDebugImage(Cell *pRootCell);
RuvmResult ruvmWriteRuvmFile(RuvmContext pContext, const char *pName,
                             int32_t objCount, RuvmObject *pObjArr,
                             int32_t usgCount, RuvmUsg *pUsgArr);
RuvmResult ruvmLoadRuvmFile(RuvmContext pContext, char *filePath,
                            int32_t *pObjCount, RuvmObject **ppObjArr,
                            int32_t *pUsgCount, RuvmUsg **ppUsgArr,
                            int32_t *pFlatCutoffCount, RuvmObject **ppFlatCutoffArr,
                            bool forEdit);

void ruvmIoSetCustom(RuvmContext pContext, RuvmIo *pIo);
void ruvmIoSetDefault(RuvmContext pContext);
