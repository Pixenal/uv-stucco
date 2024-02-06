#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef PLATFORM_WIN
	#define RUVM_EXPORT __declspec(dllexport)
#elif PLATFORM_LINUX
	#define RUVM_EXPORT __attribute__((visibility("default")))
#endif

typedef struct RuvmContextInternal *RuvmContext;
typedef struct RuvmMapInternal *RuvmMap;

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
	int32_t faceCount;
	int32_t *pFaces;
	int32_t loopCount;
	int32_t *pLoops;
	RuvmVec3 *pNormals;
	RuvmVec2 *pUvs;
	int32_t vertCount;
	RuvmVec3 *pVerts;
} RuvmMesh;

typedef struct {
	void *(*pMalloc)(size_t);
	void *(*pCalloc)(size_t, size_t);
	void (*pFree)(void *);
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
                                 RuvmThreadPool *pTheadPool, RuvmIo *pIo);
RUVM_EXPORT void ruvmMapFileExport(RuvmContext context, RuvmMesh *pMesh);
RUVM_EXPORT void ruvmMapFileLoad(RuvmContext context, RuvmMap *pMapHandle,
                                  char *filePath);
RUVM_EXPORT void ruvmMapFileUnload(RuvmContext context, RuvmMap pMap);
RUVM_EXPORT void ruvmMapToMesh(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMeshIn,
                               RuvmMesh *pMeshOut);
RUVM_EXPORT void ruvmMeshDestroy(RuvmContext pContext, RuvmMesh *pMesh);
RUVM_EXPORT void ruvmContextDestroy(RuvmContext context);
