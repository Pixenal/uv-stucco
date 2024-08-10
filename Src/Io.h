#pragma once

#include <QuadTree.h>
#include <RUVM.h>

typedef struct {
    unsigned char *pString;
    int32_t nextBitIndex;
    int32_t byteIndex;
} ByteString;

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
void encodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits, int64_t *pSize);
void encodeString(ByteString *byteString, uint8_t *string, int64_t *pSize);
void decodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits);
void decodeString(ByteString *byteString, char *string, int32_t maxLen);
