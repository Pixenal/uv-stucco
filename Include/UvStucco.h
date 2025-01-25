#pragma once
#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
#include <cstdbool>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#endif

#define STUC_DISABLE_TRIANGULATION


#ifdef WIN32
	#define STUC_EXPORT __declspec(dllexport)
#elif PLATFORM_LINUX
	#define STUC_EXPORT __attribute__((visibility("default")))
#endif

#define STUC_ATTRIB_NAME_MAX_LEN 96
#define STUC_ATTRIB_STRING_MAX_LEN 64

typedef struct StucContextInternal *StucContext;
typedef struct StucMapInternal *StucMap;

//TODO add array wrapper structs, so that you don't need to pass
//separate "count" variables to functions. IntArray, AttribArray,
//CommonAttribArray.

//TODO add semantic information to attribs, eg, quarternion, stuc, normals,
//colour, boolean, etc

//TODO unify naming. different structs and enums called "type", "attrib", "blend".
//Make it consistent. They're attribute types;
typedef enum {
	STUC_ATTRIB_I8,
	STUC_ATTRIB_I16,
	STUC_ATTRIB_I32,
	STUC_ATTRIB_I64,
	STUC_ATTRIB_F32,
	STUC_ATTRIB_F64,
	STUC_ATTRIB_V2_I8,
	STUC_ATTRIB_V2_I16,
	STUC_ATTRIB_V2_I32,
	STUC_ATTRIB_V2_I64,
	STUC_ATTRIB_V2_F32,
	STUC_ATTRIB_V2_F64,
	STUC_ATTRIB_V3_I8,
	STUC_ATTRIB_V3_I16,
	STUC_ATTRIB_V3_I32,
	STUC_ATTRIB_V3_I64,
	STUC_ATTRIB_V3_F32,
	STUC_ATTRIB_V3_F64,
	STUC_ATTRIB_V4_I8,
	STUC_ATTRIB_V4_I16,
	STUC_ATTRIB_V4_I32,
	STUC_ATTRIB_V4_I64,
	STUC_ATTRIB_V4_F32,
	STUC_ATTRIB_V4_F64,
	STUC_ATTRIB_STRING
} StucAttribType;

typedef enum {
	STUC_BLEND_REPLACE, //only replace & append can be used with strings
	STUC_BLEND_MULTIPLY,
	STUC_BLEND_DIVIDE,
	STUC_BLEND_ADD,
	STUC_BLEND_SUBTRACT,
	STUC_BLEND_ADD_SUB,
	STUC_BLEND_LIGHTEN,
	STUC_BLEND_DARKEN,
	STUC_BLEND_OVERLAY,
	STUC_BLEND_SOFT_LIGHT,
	STUC_BLEND_COLOR_DODGE,
	STUC_BLEND_APPEND //strings only
} StucBlendMode;

typedef enum {
	STUC_ATTRIB_ORIGIN_MAP,
	STUC_ATTRIB_ORIGIN_MESH_IN,
	STUC_ATTRIB_ORIGIN_MESH_OUT,
	STUC_ATTRIB_ORIGIN_COMMON,
	STUC_ATTRIB_DONT_COPY
} StucAttribOrigin;

typedef enum {
	STUC_IMAGE_UI8,
	STUC_IMAGE_UI16,
	STUC_IMAGE_UI32,
	STUC_IMAGE_F32,
} StucImageType;

typedef enum {
	STUC_NOT_SET,
	STUC_SUCCESS,
	STUC_ERROR
} StucResult;

typedef struct {
	float x;
	float y;
} StucVec2;

typedef struct {
	float x;
	float y;
	float z;
} StucVec3;

typedef struct {
	int8_t d[2];
} Stuc_V2_I8;

typedef struct {
	int16_t d[2];
} Stuc_V2_I16;

typedef struct {
	int32_t d[2];
} Stuc_V2_I32;

typedef struct {
	int64_t d[2];
} Stuc_V2_I64;

typedef struct {
	float d[2];
} Stuc_V2_F32;

typedef struct {
	double d[2];
} Stuc_V2_F64;

typedef struct {
	int8_t d[3];
} Stuc_V3_I8;

typedef struct {
	int16_t d[3];
} Stuc_V3_I16;

typedef struct {
	int32_t d[3];
} Stuc_V3_I32;

typedef struct {
	int64_t d[3];
} Stuc_V3_I64;

typedef struct {
	float d[3];
} Stuc_V3_F32;

typedef struct {
	double d[3];
} Stuc_V3_F64;

typedef struct {
	int8_t d[4];
} Stuc_V4_I8;

typedef struct {
	int16_t d[4];
} Stuc_V4_I16;

typedef struct {
	int32_t d[4];
} Stuc_V4_I32;

typedef struct {
	int64_t d[4];
} Stuc_V4_I64;

typedef struct {
	float d[4];
} Stuc_V4_F32;

typedef struct {
	double d[4];
} Stuc_V4_F64;

typedef struct {
	float d[4][4];
} Stuc_M4x4_F32;

typedef struct {
	char d[STUC_ATTRIB_STRING_MAX_LEN];
} Stuc_String;

typedef struct {
	void *pData;
	char name[STUC_ATTRIB_NAME_MAX_LEN];
	StucAttribType type;
	StucAttribOrigin origin;
	bool interpolate;
} StucAttrib;

typedef struct {
	void *pData;
	char name[STUC_ATTRIB_NAME_MAX_LEN];
	StucAttribType type;
	int32_t count;
} StucAttribIndexed;

typedef struct {
	StucAttribIndexed *pArr;
	int32_t count;
	int32_t size;
} StucAttribIndexedArr;

typedef struct {
	StucAttrib *pArr;
	int32_t count;
	int32_t size;
} StucAttribArray;

typedef enum {
	STUC_DOMAIN_FACE,
	STUC_DOMAIN_CORNER,
	STUC_DOMAIN_EDGE,
	STUC_DOMAIN_VERT
} StucDomain;

typedef struct {
	StucBlendMode blend;
	int8_t order;
} StucBlendConfig;

typedef struct {
	char name[STUC_ATTRIB_NAME_MAX_LEN];
	StucBlendConfig blendConfig;
} StucCommonAttrib;

typedef struct {
	StucBlendConfig blendConfig;
} StucTypeDefault;

typedef struct {
	StucTypeDefault i8;
	StucTypeDefault i16;
	StucTypeDefault i32;
	StucTypeDefault i64;
	StucTypeDefault f32;
	StucTypeDefault f64;
	StucTypeDefault v2_i8;
	StucTypeDefault v2_i16;
	StucTypeDefault v2_i32;
	StucTypeDefault v2_i64;
	StucTypeDefault v2_f32;
	StucTypeDefault v2_f64;
	StucTypeDefault v3_i8;
	StucTypeDefault v3_i16;
	StucTypeDefault v3_i32;
	StucTypeDefault v3_i64;
	StucTypeDefault v3_f32;
	StucTypeDefault v3_f64;
	StucTypeDefault v4_i8;
	StucTypeDefault v4_i16;
	StucTypeDefault v4_i32;
	StucTypeDefault v4_i64;
	StucTypeDefault v4_f32;
	StucTypeDefault v4_f64;
	StucTypeDefault string;
} StucTypeDefaultConfig;

typedef struct {
	int32_t meshCount;
	StucCommonAttrib *pMesh;
	int32_t faceCount;
	StucCommonAttrib *pFace;
	int32_t cornerCount;
	StucCommonAttrib *pCorner;
	int32_t edgeCount;
	StucCommonAttrib *pEdge;
	int32_t vertCount;
	StucCommonAttrib *pVert;
} StucCommonAttribList;

typedef enum {
	STUC_OBJECT_DATA_NULL,
	STUC_OBJECT_DATA_MESH,
	STUC_OBJECT_DATA_MESH_INTERN,
	STUC_OBJECT_DATA_MESH_BUF
} StucObjectType;

typedef struct {
	StucObjectType type;
} StucObjectData;

//TODO rename pCorners to pVerts
typedef struct {
	StucObjectData type;
	StucAttribArray meshAttribs;
	int32_t faceCount;
	int32_t *pFaces;
	StucAttribArray faceAttribs;
	int32_t cornerCount;
	int32_t *pCorners;
	StucAttribArray cornerAttribs;
	int32_t edgeCount;
	int32_t *pEdges;
	StucAttribArray edgeAttribs;
	int32_t vertCount;
	StucAttribArray vertAttribs;
} StucMesh;

typedef struct {
	StucObjectData *pData;
	Stuc_M4x4_F32 transform;
} StucObject;

typedef struct {
	void *(*pMalloc)(size_t);
	void *(*pCalloc)(size_t, size_t);
	void (*pFree)(void *);
	void *(*pRealloc)(void *, size_t);
} StucAlloc;

typedef struct {
	void (*pInit)(void **, int32_t *, StucAlloc *);
	void (*pJobStackGetJob)(void *, void (**)(void *), void **);
	int32_t (*pJobStackPushJobs)(void *, int32_t, void(*)(void *), void **);
	bool (*pGetAndDoJob)(void *);
	void (*pMutexGet)(void *, void **);
	void (*pMutexLock)(void *, void *);
	void (*pMutexUnlock)(void *, void *);
	void (*pMutexDestroy)(void *, void *);
	void (*pBarrierGet)(void *, void **, int32_t);
	bool (*pBarrierWait)(void *, void *);
	void (*pBarrierDestroy)(void *, void *);
	void (*pDestroy)(void *);
} StucThreadPool;

typedef struct {
	int32_t (*pOpen)(void **, char *, int32_t, StucAlloc *);
	int32_t (*pWrite)(void *, unsigned char *, int32_t);
	int32_t (*pRead)(void *, unsigned char *, int32_t);
	int32_t (*pClose)(void *);
} StucIo;

typedef struct {
	void *pData;
	StucImageType type;
	int32_t res;
} StucImage;

typedef struct {
	StucObject obj;
	StucObject *pFlatCutoff;
} StucUsg;

#define STUC_STAGE_NAME_LEN 64
typedef struct StucStageReport {
	char stage[STUC_STAGE_NAME_LEN];
	int32_t progress;
	int32_t outOf;
	void (*pBegin)(void *, struct StucStageReport *, const char *);
	void (*pProgress)(void *, struct StucStageReport* , int32_t);
	void (*pEnd)(void *, struct StucStageReport *);
} StucStageReport;

STUC_EXPORT
StucResult stucThreadPoolSetCustom(StucContext context, StucThreadPool *pThreadPool);
STUC_EXPORT
StucResult stucContextInit(StucContext *pContext, StucAlloc *pAlloc,
                           StucThreadPool *pTheadPool, StucIo *pIo,
                           StucTypeDefaultConfig *pTypeDefaultConfig,
                           StucStageReport *pStageReport);
STUC_EXPORT
StucResult stucMapFileExport(StucContext context, const char *pName,
                             int32_t objCount, StucObject* pObjArr,
                             int32_t usgCount, StucUsg* pUsgArr,
                             StucAttribIndexedArr indexedAttribs);
STUC_EXPORT
StucResult stucMapFileLoadForEdit(StucContext pContext, char *filePath,
                                  int32_t *pObjCount, StucObject **ppObjArr,
                                  int32_t *pUsgCount, StucUsg **ppUsgArr,
                                  int32_t *pFlatCutoffCount, StucObject **ppFlatCutoffArr,
                                  StucAttribIndexedArr *pIndexedAttribs);
STUC_EXPORT
StucResult stucMapFileLoad(StucContext context, StucMap *pMapHandle,
                           char *filePath);
STUC_EXPORT
StucResult stucMapFileUnload(StucContext context, StucMap pMap);
STUC_EXPORT
StucResult stucQueryCommonAttribs(StucContext pContext, StucMap pMap, StucMesh *pMesh,
                                  StucCommonAttribList *pCommonAttribs);
STUC_EXPORT
StucResult stucDestroyCommonAttribs(StucContext pContext,
                                    StucCommonAttribList *pCommonAttribs);
STUC_EXPORT
StucResult stucMapToMesh(StucContext pContext, StucMap pMap, StucMesh *pMeshIn,
                         StucMesh *pMeshOut, StucCommonAttribList *pCommonAttribList,
                         float wScale);
STUC_EXPORT
StucResult stucObjArrDestroy(StucContext pContext,
                             int32_t objCount, StucObject *pObjArr);
STUC_EXPORT
StucResult stucUsgArrDestroy(StucContext pContext,
                                    int32_t count, StucUsg *pUsgArr);
STUC_EXPORT
StucResult stucMeshDestroy(StucContext pContext, StucMesh *pMesh);
STUC_EXPORT
StucResult stucContextDestroy(StucContext pContext);
STUC_EXPORT
StucResult stucGetAttribSize(StucAttrib *pAttrib, int32_t *pSize);
//TODO make all functions return error codes, ie, adjust functions like below, which return data,
//pass as param instead
STUC_EXPORT
StucResult stucGetAttrib(char *pName, StucAttribArray *pAttribs, StucAttrib **ppAttrib);
STUC_EXPORT
StucResult stucMapFileGenPreviewImage(StucContext pContext, StucMap pMap,  StucImage *pImage);
STUC_EXPORT
void stucMapIndexedAttribsGet(StucContext pContext, StucMap pMap,
                              StucAttribIndexedArr *pIndexedAttribs);
