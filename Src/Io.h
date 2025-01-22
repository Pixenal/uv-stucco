#pragma once

#define MAP_FORMAT_NAME_MAX_LEN 19

#include <QuadTree.h>
#include <UvStucco.h>

typedef struct {
    unsigned char *pString;
    int32_t nextBitIdx;
    int32_t byteIdx;
} ByteString;

//void stucWriteDebugImage(Cell *pRootCell);
StucResult stucWriteStucFile(StucContext pContext, const char *pName,
                             int32_t objCount, StucObject *pObjArr,
                             int32_t usgCount, StucUsg *pUsgArr,
                             StucAttribIndexedArr indexedAttribs);
StucResult stucLoadStucFile(StucContext pContext, char *filePath,
                            int32_t *pObjCount, StucObject **ppObjArr,
                            int32_t *pUsgCount, StucUsg **ppUsgArr,
                            int32_t *pFlatCutoffCount, StucObject **ppFlatCutoffArr,
                            bool forEdit, StucAttribIndexedArr *pIndexedAttribs);

void stucIoSetCustom(StucContext pContext, StucIo *pIo);
void stucIoSetDefault(StucContext pContext);
void encodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits, int64_t *pSize);
void encodeString(ByteString *byteString, uint8_t *string, int64_t *pSize);
void decodeValue(ByteString *byteString, uint8_t *value, int32_t lengthInBits);
void decodeString(ByteString *byteString, char *string, int32_t maxLen);
