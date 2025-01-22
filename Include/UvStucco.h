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

#define RUVM_DISABLE_TRIANGULATION


#ifdef WIN32
	#define RUVM_EXPORT __declspec(dllexport)
#elif PLATFORM_LINUX
	#define RUVM_EXPORT __attribute__((visibility("default")))
#endif

#define RUVM_ATTRIB_NAME_MAX_LEN 96
#define RUVM_ATTRIB_STRING_MAX_LEN 64

typedef struct RuvmContextInternal *RuvmContext;
typedef struct RuvmMapInternal *RuvmMap;

//TODO add array wrapper structs, so that you don't need to pass
//separate "count" variables to functions. IntArray, AttribArray,
//CommonAttribArray.

//TODO add semantic information to attribs, eg, quarternion, uvs, normals,
//colour, boolean, etc

//TODO unify naming. different structs and enums called "type", "attrib", "blend".
//Make it consistent. They're attribute types;
typedef enum {
	RUVM_ATTRIB_I8,
	RUVM_ATTRIB_I16,
	RUVM_ATTRIB_I32,
	RUVM_ATTRIB_I64,
	RUVM_ATTRIB_F32,
	RUVM_ATTRIB_F64,
	RUVM_ATTRIB_V2_I8,
	RUVM_ATTRIB_V2_I16,
	RUVM_ATTRIB_V2_I32,
	RUVM_ATTRIB_V2_I64,
	RUVM_ATTRIB_V2_F32,
	RUVM_ATTRIB_V2_F64,
	RUVM_ATTRIB_V3_I8,
	RUVM_ATTRIB_V3_I16,
	RUVM_ATTRIB_V3_I32,
	RUVM_ATTRIB_V3_I64,
	RUVM_ATTRIB_V3_F32,
	RUVM_ATTRIB_V3_F64,
	RUVM_ATTRIB_V4_I8,
	RUVM_ATTRIB_V4_I16,
	RUVM_ATTRIB_V4_I32,
	RUVM_ATTRIB_V4_I64,
	RUVM_ATTRIB_V4_F32,
	RUVM_ATTRIB_V4_F64,
	RUVM_ATTRIB_STRING
} RuvmAttribType;

typedef enum {
	RUVM_BLEND_REPLACE, //only replace & append can be used with strings
	RUVM_BLEND_MULTIPLY,
	RUVM_BLEND_DIVIDE,
	RUVM_BLEND_ADD,
	RUVM_BLEND_SUBTRACT,
	RUVM_BLEND_ADD_SUB,
	RUVM_BLEND_LIGHTEN,
	RUVM_BLEND_DARKEN,
	RUVM_BLEND_OVERLAY,
	RUVM_BLEND_SOFT_LIGHT,
	RUVM_BLEND_COLOR_DODGE,
	RUVM_BLEND_APPEND //strings only
} RuvmBlendMode;

typedef enum {
	RUVM_ATTRIB_ORIGIN_MAP,
	RUVM_ATTRIB_ORIGIN_MESH_IN,
	RUVM_ATTRIB_ORIGIN_MESH_OUT,
	RUVM_ATTRIB_ORIGIN_COMMON,
	RUVM_ATTRIB_ORIGIN_IGNORE
} RuvmAttribOrigin;

typedef enum {
	RUVM_IMAGE_UI8,
	RUVM_IMAGE_UI16,
	RUVM_IMAGE_UI32,
	RUVM_IMAGE_F32,
} RuvmImageType;

typedef enum {
	RUVM_NOT_SET,
	RUVM_SUCCESS,
	RUVM_ERROR
} RuvmResult;

typedef struct {
	float x;
	float y;
} RuvmVec2;

typedef struct {
	float x;
	float y;
	float z;
} RuvmVec3;

typedef struct {
	int8_t d[2];
} Ruvm_V2_I8;

typedef struct {
	int16_t d[2];
} Ruvm_V2_I16;

typedef struct {
	int32_t d[2];
} Ruvm_V2_I32;

typedef struct {
	int64_t d[2];
} Ruvm_V2_I64;

typedef struct {
	float d[2];
} Ruvm_V2_F32;

typedef struct {
	double d[2];
} Ruvm_V2_F64;

typedef struct {
	int8_t d[3];
} Ruvm_V3_I8;

typedef struct {
	int16_t d[3];
} Ruvm_V3_I16;

typedef struct {
	int32_t d[3];
} Ruvm_V3_I32;

typedef struct {
	int64_t d[3];
} Ruvm_V3_I64;

typedef struct {
	float d[3];
} Ruvm_V3_F32;

typedef struct {
	double d[3];
} Ruvm_V3_F64;

typedef struct {
	int8_t d[4];
} Ruvm_V4_I8;

typedef struct {
	int16_t d[4];
} Ruvm_V4_I16;

typedef struct {
	int32_t d[4];
} Ruvm_V4_I32;

typedef struct {
	int64_t d[4];
} Ruvm_V4_I64;

typedef struct {
	float d[4];
} Ruvm_V4_F32;

typedef struct {
	double d[4];
} Ruvm_V4_F64;

typedef struct {
	float d[4][4];
} Ruvm_M4x4_F32;

typedef struct {
	char d[RUVM_ATTRIB_STRING_MAX_LEN];
} Ruvm_String;

typedef struct {
	void *pData;
	char name[RUVM_ATTRIB_NAME_MAX_LEN];
	RuvmAttribType type;
	RuvmAttribOrigin origin;
	bool interpolate;
} RuvmAttrib;

typedef struct {
	void *pData;
	char name[RUVM_ATTRIB_NAME_MAX_LEN];
	RuvmAttribType type;
	int32_t count;
} RuvmAttribIndexed;

typedef struct {
	RuvmAttribIndexed *pArr;
	int32_t count;
	int32_t size;
} RuvmAttribIndexedArr;

typedef struct {
	RuvmAttrib *pArr;
	int32_t count;
	int32_t size;
} RuvmAttribArray;

typedef enum {
	RUVM_DOMAIN_FACE,
	RUVM_DOMAIN_LOOP,
	RUVM_DOMAIN_EDGE,
	RUVM_DOMAIN_VERT
} RuvmDomain;

typedef struct {
	RuvmBlendMode blend;
	int8_t order;
} RuvmBlendConfig;

typedef struct {
	char name[RUVM_ATTRIB_NAME_MAX_LEN];
	RuvmBlendConfig blendConfig;
} RuvmCommonAttrib;

typedef struct {
	RuvmBlendConfig blendConfig;
} RuvmTypeDefault;

typedef struct {
	RuvmTypeDefault i8;
	RuvmTypeDefault i16;
	RuvmTypeDefault i32;
	RuvmTypeDefault i64;
	RuvmTypeDefault f32;
	RuvmTypeDefault f64;
	RuvmTypeDefault v2_i8;
	RuvmTypeDefault v2_i16;
	RuvmTypeDefault v2_i32;
	RuvmTypeDefault v2_i64;
	RuvmTypeDefault v2_f32;
	RuvmTypeDefault v2_f64;
	RuvmTypeDefault v3_i8;
	RuvmTypeDefault v3_i16;
	RuvmTypeDefault v3_i32;
	RuvmTypeDefault v3_i64;
	RuvmTypeDefault v3_f32;
	RuvmTypeDefault v3_f64;
	RuvmTypeDefault v4_i8;
	RuvmTypeDefault v4_i16;
	RuvmTypeDefault v4_i32;
	RuvmTypeDefault v4_i64;
	RuvmTypeDefault v4_f32;
	RuvmTypeDefault v4_f64;
	RuvmTypeDefault string;
} RuvmTypeDefaultConfig;

typedef struct {
	int32_t meshCount;
	RuvmCommonAttrib *pMesh;
	int32_t faceCount;
	RuvmCommonAttrib *pFace;
	int32_t cornerCount;
	RuvmCommonAttrib *pCorner;
	int32_t edgeCount;
	RuvmCommonAttrib *pEdge;
	int32_t vertCount;
	RuvmCommonAttrib *pVert;
} RuvmCommonAttribList;

typedef enum {
	RUVM_OBJECT_DATA_NULL,
	RUVM_OBJECT_DATA_MESH,
	RUVM_OBJECT_DATA_MESH_INTERN,
	RUVM_OBJECT_DATA_MESH_BUF
} RuvmObjectType;

typedef struct {
	RuvmObjectType type;
} RuvmObjectData;

//TODO rename pCorners to pVerts
typedef struct {
	RuvmObjectData type;
	RuvmAttribArray meshAttribs;
	int32_t faceCount;
	int32_t *pFaces;
	RuvmAttribArray faceAttribs;
	int32_t cornerCount;
	int32_t *pCorners;
	RuvmAttribArray cornerAttribs;
	int32_t edgeCount;
	int32_t *pEdges;
	RuvmAttribArray edgeAttribs;
	int32_t vertCount;
	RuvmAttribArray vertAttribs;
} RuvmMesh;

typedef struct {
	RuvmObjectData *pData;
	Ruvm_M4x4_F32 transform;
} RuvmObject;

typedef struct {
	void *(*pMalloc)(size_t);
	void *(*pCalloc)(size_t, size_t);
	void (*pFree)(void *);
	void *(*pRealloc)(void *, size_t);
} RuvmAlloc;

typedef struct {
	void (*pInit)(void **, int32_t *, RuvmAlloc *);
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
} RuvmThreadPool;

typedef struct {
	int32_t (*pOpen)(void **, char *, int32_t, RuvmAlloc *);
	int32_t (*pWrite)(void *, unsigned char *, int32_t);
	int32_t (*pRead)(void *, unsigned char *, int32_t);
	int32_t (*pClose)(void *);
} RuvmIo;

typedef struct {
	void *pData;
	RuvmImageType type;
	int32_t res;
} RuvmImage;

typedef struct {
	RuvmObject obj;
	RuvmObject *pFlatCutoff;
} RuvmUsg;

#define RUVM_STAGE_NAME_LEN 64
typedef struct RuvmStageReport {
	char stage[RUVM_STAGE_NAME_LEN];
	int32_t progress;
	int32_t outOf;
	void (*pBegin)(void *, struct RuvmStageReport *, const char *);
	void (*pProgress)(void *, struct RuvmStageReport* , int32_t);
	void (*pEnd)(void *, struct RuvmStageReport *);
} RuvmStageReport;

RUVM_EXPORT
RuvmResult uvsThreadPoolSetCustom(RuvmContext context, RuvmThreadPool *pThreadPool);
RUVM_EXPORT
RuvmResult uvsContextInit(RuvmContext *pContext, RuvmAlloc *pAlloc,
                           RuvmThreadPool *pTheadPool, RuvmIo *pIo,
                           RuvmTypeDefaultConfig *pTypeDefaultConfig,
                           RuvmStageReport *pStageReport);
RUVM_EXPORT
RuvmResult uvsMapFileExport(RuvmContext context, const char *pName,
                             int32_t objCount, RuvmObject* pObjArr,
                             int32_t usgCount, RuvmUsg* pUsgArr,
                             RuvmAttribIndexedArr indexedAttribs);
RUVM_EXPORT
RuvmResult uvsMapFileLoadForEdit(RuvmContext pContext, char *filePath,
                                  int32_t *pObjCount, RuvmObject **ppObjArr,
                                  int32_t *pUsgCount, RuvmUsg **ppUsgArr,
                                  int32_t *pFlatCutoffCount, RuvmObject **ppFlatCutoffArr,
                                  RuvmAttribIndexedArr *pIndexedAttribs);
RUVM_EXPORT
RuvmResult uvsMapFileLoad(RuvmContext context, RuvmMap *pMapHandle,
                           char *filePath);
RUVM_EXPORT
RuvmResult uvsMapFileUnload(RuvmContext context, RuvmMap pMap);
RUVM_EXPORT
RuvmResult uvsQueryCommonAttribs(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMesh,
                                  RuvmCommonAttribList *pCommonAttribs);
RUVM_EXPORT
RuvmResult uvsDestroyCommonAttribs(RuvmContext pContext,
                                    RuvmCommonAttribList *pCommonAttribs);
RUVM_EXPORT
RuvmResult uvsMapToMesh(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMeshIn,
                         RuvmMesh *pMeshOut, RuvmCommonAttribList *pCommonAttribList,
                         float wScale);
RUVM_EXPORT
RuvmResult uvsObjArrDestroy(RuvmContext pContext,
                             int32_t objCount, RuvmObject *pObjArr);
RUVM_EXPORT
RuvmResult uvsUsgArrDestroy(RuvmContext pContext,
                                    int32_t count, RuvmUsg *pUsgArr);
RUVM_EXPORT
RuvmResult uvsMeshDestroy(RuvmContext pContext, RuvmMesh *pMesh);
RUVM_EXPORT
RuvmResult uvsContextDestroy(RuvmContext pContext);
RUVM_EXPORT
RuvmResult uvsGetAttribSize(RuvmAttrib *pAttrib, int32_t *pSize);
//TODO make all functions return error codes, ie, adjust functions like below, which return data,
//pass as param instead
RUVM_EXPORT
RuvmResult uvsGetAttrib(char *pName, RuvmAttribArray *pAttribs, RuvmAttrib **ppAttrib);
RUVM_EXPORT
RuvmResult uvsMapFileGenPreviewImage(RuvmContext pContext, RuvmMap pMap,  RuvmImage *pImage);
RUVM_EXPORT
void uvsMapIndexedAttribsGet(RuvmContext pContext, RuvmMap pMap,
                              RuvmAttribIndexedArr *pIndexedAttribs);
