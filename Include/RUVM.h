#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef PLATFORM_WINDOWS
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
	RUVM_ATTRIB_ORIGIN_COMMON
} RUVM_ATTRIB_ORIGIN;

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
	char d[RUVM_ATTRIB_STRING_MAX_LEN];
} Ruvm_STRING;


typedef struct {
	void *pData;
	char name[RUVM_ATTRIB_NAME_MAX_LEN];
	RuvmAttribType type;
	RUVM_ATTRIB_ORIGIN origin;
	int8_t interpolate;
} RuvmAttrib;

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
	int32_t loopCount;
	RuvmCommonAttrib *pLoop;
	int32_t edgeCount;
	RuvmCommonAttrib *pEdge;
	int32_t vertCount;
	RuvmCommonAttrib *pVert;
} RuvmCommonAttribList;

typedef struct {
	int32_t meshAttribCount;
	RuvmAttrib *pMeshAttribs;
	int32_t faceCount;
	int32_t *pFaces;
	int32_t faceAttribCount;
	RuvmAttrib *pFaceAttribs;
	int32_t loopCount;
	int32_t *pLoops;
	int32_t loopAttribCount;
	RuvmAttrib *pLoopAttribs;
	int32_t edgeCount;
	int32_t *pEdges;
	int32_t edgeAttribCount;
	RuvmAttrib *pEdgeAttribs;
	int32_t vertCount;
	int32_t vertAttribCount;
	RuvmAttrib *pVertAttribs;
} RuvmMesh;

typedef struct {
	void *(*pMalloc)(size_t);
	void *(*pCalloc)(size_t, size_t);
	void (*pFree)(void *);
	void *(*pRealloc)(void *, size_t);
} RuvmAllocator;

typedef struct {
	void (*pInit)(void **, int32_t *, RuvmAllocator *);
	void (*pJobStackGetJob)(void *, void (**)(void *), void **);
	int32_t (*pJobStackPushJobs)(void *, int32_t, void(*)(void *), void **);
	void (*pMutexGet)(void *, void **);
	void (*pMutexLock)(void *, void *);
	void (*pMutexUnlock)(void *, void *);
	void (*pMutexDestroy)(void *, void *);
	void (*pDestroy)(void *);
} RuvmThreadPool;

typedef struct {
	int32_t (*pOpen)(void **, char *, int32_t, RuvmAllocator *);
	int32_t (*pWrite)(void *, unsigned char *, int32_t);
	int32_t (*pRead)(void *, unsigned char *, int32_t);
	int32_t (*pClose)(void *);
} RuvmIo;

RUVM_EXPORT void ruvmThreadPoolSetCustom(RuvmContext context, RuvmThreadPool *pThreadPool);
RUVM_EXPORT void ruvmContextInit(RuvmContext *pContext, RuvmAllocator *pAllocator,
                                 RuvmThreadPool *pTheadPool, RuvmIo *pIo,
					 		     RuvmTypeDefaultConfig *pTypeDefaultConfig);
RUVM_EXPORT void ruvmMapFileExport(RuvmContext context, RuvmMesh *pMesh);
RUVM_EXPORT void ruvmMapFileLoad(RuvmContext context, RuvmMap *pMapHandle,
                                  char *filePath);
RUVM_EXPORT void ruvmMapFileUnload(RuvmContext context, RuvmMap pMap);
RUVM_EXPORT void ruvmQueryCommonAttribs(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMesh,
                                        RuvmCommonAttribList *pCommonAttribs);
RUVM_EXPORT void ruvmDestroyCommonAttribs(RuvmContext pContext,
                                         RuvmCommonAttribList *pCommonAttribs);
RUVM_EXPORT int32_t ruvmMapToMesh(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMeshIn,
                                  RuvmMesh *pMeshOut, RuvmCommonAttribList *pCommonAttribList);
RUVM_EXPORT void ruvmMeshDestroy(RuvmContext pContext, RuvmMesh *pMesh);
RUVM_EXPORT void ruvmContextDestroy(RuvmContext pContext);
RUVM_EXPORT void ruvmGetAttribSize(RuvmAttrib *pAttrib, int32_t *pSize);
//TODO make all functions return error codes, ie, adjust functions like below, which return data,
//pass as param instead
RUVM_EXPORT RuvmAttrib *ruvmGetAttrib(char *pName, RuvmAttrib *pAttribs, int32_t attribCount);
