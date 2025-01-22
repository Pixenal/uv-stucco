#pragma once

#include <QuadTree.h>
#include <RUVM.h>

typedef struct {
    unsigned char *pString;
    int32_t nextBitIdx;
    int32_t byteIdx;
} ByteString;

//void uvsWriteDebugImage(Cell *pRootCell);
RuvmResult uvsWriteRuvmFile(RuvmContext pContext, const char *pName,
                             int32_t objCount, RuvmObject *pObjArr,
                             int32_t usgCount, RuvmUsg *pUsgArr,
                             RuvmAttribIndexedArr indexedAttribs);
RuvmResult uvsLoadRuvmFile(RuvmContext pContext, char *filePath,
                            int32_t *pObjCount, RuvmObject **ppObjArr,
                            int32_t *pUsgCount, RuvmUsg **ppUsgArr,
                            int32_t *pFlatCutoffCount, RuvmObject **ppFlatCutoffArr,
                            bool forEdit, RuvmAttribIndexedArr *pIndexedAttribs);

void uvsIoSetCustom(RuvmContext pContext, RuvmIo *pIo);
void uvsIoSetDefault(RuvmContext pContext);
void encodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits, int64_t *pSize);
void encodeString(ByteString *byteString, uint8_t *string, int64_t *pSize);
void decodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits);
void decodeString(ByteString *byteString, char *string, int32_t maxLen);
