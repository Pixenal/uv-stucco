/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

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

#include <pixenals_types.h>
#include <pixenals_alloc_utils.h>
#include <pixenals_io_utils.h>

#define STUC_DISABLE_EDGES_IN_BUF

#define STUC_DISABLE_TRIANGULATION


#ifdef WIN32
	#define STUC_EXPORT __declspec(dllexport)
#else
	#define STUC_EXPORT __attribute__((visibility("default")))
#endif

#define STUC_ATTRIB_NAME_MAX_LEN 96
#define STUC_ATTRIB_STRING_MAX_LEN 64

//TODO typedefs should not make pointer types
typedef struct StucContextInternal *StucContext;
typedef struct StucMapInternal *StucMap;
typedef struct StucMapExportIntern StucMapExport;

//TODO add array wrapper structs, so that you don't need to pass
//separate "count" variables to functions. IntArray, AttribArray,
//CommonAttribArray.

//TODO add semantic information to attribs, eg, quarternion, stuc, normals,
//colour, boolean, etc

//TODO unify naming. different structs and enums called "type", "attrib", "blend".
//Make it consistent. They're attribute types;
//or maybe just have STUC_I8? can they be generic like that?
typedef enum StucAttribType {
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
	STUC_ATTRIB_STRING,
	STUC_ATTRIB_NONE, //TODO move this to idx 0
	STUC_ATTRIB_TYPE_ENUM_COUNT
} StucAttribType;

typedef enum StucAttribUse {
	STUC_ATTRIB_USE_NONE,
	STUC_ATTRIB_USE_POS,
	STUC_ATTRIB_USE_UV,
	STUC_ATTRIB_USE_NORMAL,
	STUC_ATTRIB_USE_PRESERVE_EDGE,
	STUC_ATTRIB_USE_RECEIVE,
	STUC_ATTRIB_USE_PRESERVE_VERT,
	STUC_ATTRIB_USE_USG,
	STUC_ATTRIB_USE_TANGENT,
	STUC_ATTRIB_USE_TSIGN,
	STUC_ATTRIB_USE_WSCALE,
	STUC_ATTRIB_USE_IDX,
	STUC_ATTRIB_USE_EDGE_LEN,
	STUC_ATTRIB_USE_SEAM_EDGE,
	STUC_ATTRIB_USE_SEAM_VERT,
	STUC_ATTRIB_USE_NUM_ADJ_PRESERVE,
	STUC_ATTRIB_USE_EDGE_FACES,
	STUC_ATTRIB_USE_EDGE_CORNERS,
	STUC_ATTRIB_USE_NORMALS_VERT,
	STUC_ATTRIB_USE_SP_ENUM_COUNT,//denotes number of sp uses
	STUC_ATTRIB_USE_COLOR,
	STUC_ATTRIB_USE_MASK,
	STUC_ATTRIB_USE_SCALAR,
	STUC_ATTRIB_USE_MISC,
	STUC_ATTRIB_USE_ENUM_COUNT
} StucAttribUse;

typedef enum StucBlendMode {
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

typedef enum StucAttribOrigin {
	STUC_ATTRIB_ORIGIN_MAP,
	STUC_ATTRIB_ORIGIN_MESH_IN,//TODO put this at idx 0, so its set when user makes a struct
	STUC_ATTRIB_ORIGIN_MESH_OUT,
	STUC_ATTRIB_ORIGIN_COMMON,
	STUC_ATTRIB_ORIGIN_MESH_BUF
} StucAttribOrigin;

typedef enum StucAttribCopyOpt {
	STUC_ATTRIB_COPY,
	STUC_ATTRIB_DONT_COPY
} StucAttribCopyOpt;

typedef enum StucImageType {
	STUC_IMAGE_UI8,
	STUC_IMAGE_UI16,
	STUC_IMAGE_UI32,
	STUC_IMAGE_F32,
} StucImageType;

typedef enum StucDomain {
	STUC_DOMAIN_NONE,
	STUC_DOMAIN_FACE,
	STUC_DOMAIN_CORNER,
	STUC_DOMAIN_EDGE,
	STUC_DOMAIN_VERT,
	STUC_DOMAIN_MESH
} StucDomain;

typedef PixErr StucErr;

typedef PixtyV2_I8 Stuc_V2_I8;
typedef PixtyV2_I16 Stuc_V2_I16;
typedef PixtyV2_I32 Stuc_V2_I32;
typedef PixtyV2_I64 Stuc_V2_I64;
typedef PixtyV2_F32 Stuc_V2_F32;
typedef PixtyV2_F64 Stuc_V2_F64;
typedef PixtyV3_I8 Stuc_V3_I8;
typedef PixtyV3_I16 Stuc_V3_I16;
typedef PixtyV3_I32 Stuc_V3_I32;
typedef PixtyV3_I64 Stuc_V3_I64;
typedef PixtyV3_F32 Stuc_V3_F32;
typedef PixtyV3_F64 Stuc_V3_F64;
typedef PixtyV4_I8 Stuc_V4_I8;
typedef PixtyV4_I16 Stuc_V4_I16;
typedef PixtyV4_I32 Stuc_V4_I32;
typedef PixtyV4_I64 Stuc_V4_I64;
typedef PixtyV4_F32 Stuc_V4_F32;
typedef PixtyV4_F64 Stuc_V4_F64;
typedef PixtyM4x4 Stuc_M4x4;

typedef struct Stuc_String {
	char d[STUC_ATTRIB_STRING_MAX_LEN];
} Stuc_String;

typedef struct StucAttribCore {
	void *pData;
	char name[STUC_ATTRIB_NAME_MAX_LEN];
	StucAttribType type;
	StucAttribUse use;
} StucAttribCore;

typedef struct StucAttrib {
	StucAttribCore core;
	StucAttribOrigin origin;
	StucAttribCopyOpt copyOpt;
	bool interpolate;
} StucAttrib;

typedef struct StucAttribIndexed {
	StucAttribCore core;
	int32_t size;//add size to existing vars that only use count
	int32_t count;
} StucAttribIndexed;

typedef struct StucAttribIndexedArr {
	StucAttribIndexed *pArr;
	int32_t size;
	int32_t count;
} StucAttribIndexedArr;

typedef struct StucAttribArray {
	StucAttrib *pArr;
	int32_t size;
	int32_t count;
} StucAttribArray;

typedef struct StucBlendConfig {
	double fMin;
	double fMax;
	int64_t iMin;
	int64_t iMax;
	StucBlendMode blend;
	float opacity;
	bool clamp;
	bool order;
} StucBlendConfig;

typedef struct StucBlendOpt {
	StucBlendConfig blendConfig;
	int32_t attrib;
} StucBlendOpt;

typedef struct StucBlendOptArr {
	StucBlendOpt *pArr;
	int32_t size;
	int32_t count;
} StucBlendOptArr;

typedef struct StucTypeDefault {
	StucBlendConfig blendConfig;
} StucTypeDefault;

typedef struct StucTypeDefaultConfig {
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

typedef enum StucObjectType {
	STUC_OBJECT_DATA_NULL,
	STUC_OBJECT_DATA_MESH,
	STUC_OBJECT_DATA_MESH_INTERN,
	STUC_OBJECT_DATA_MESH_BUF
} StucObjectType;

typedef struct StucObjectData {
	StucObjectType type;
} StucObjectData;

typedef union StucMapOrIdx {
	StucMap ptr;
	int64_t idx;
} StucMapOrIdx;

typedef struct StucMapArrEntry {
	StucMapOrIdx map;
	StucBlendOptArr blendOptArr[STUC_DOMAIN_MESH];
	float wScale;
	float receiveLen;
	int8_t matIdx;
} StucMapArrEntry;

typedef struct StucMapArr {
	StucMapArrEntry *pArr;
	int32_t size;
	int32_t count;
} StucMapArr;

typedef struct StucAttribActive {
	StucDomain domain;
	int16_t idx;
	bool active;
} StucAttribActive;

typedef struct StucMesh {
	StucObjectData type;
	StucAttribActive activeAttribs[STUC_ATTRIB_USE_ENUM_COUNT];
	int32_t *pFaces;
	int32_t *pCorners;
	int32_t *pEdges;
	StucAttribArray meshAttribs;
	StucAttribArray faceAttribs;
	StucAttribArray cornerAttribs;
	StucAttribArray edgeAttribs;
	StucAttribArray vertAttribs;
	int32_t faceCount;
	int32_t cornerCount;
	int32_t edgeCount;
	int32_t vertCount;
} StucMesh;

typedef struct StucObject {
	StucObjectData *pData;
	Stuc_M4x4 transform;
} StucObject;

typedef struct StucObjArr {
	StucObject *pArr;
	int32_t size;
	int32_t count;
} StucObjArr;

typedef PixalcFPtrs StucAlloc;

typedef struct StucThreadPool {
	void (*fpInit)(void **, int32_t *, const StucAlloc *);
	void (*fpJobStackGetJob)(void *, void **);
	StucErr (*pJobStackPushJobs)(
		void *,
		int32_t,
		void **,
		StucErr (*)(void *),
		void **
	);
	StucErr (*fpWaitForJobs)(void *, int32_t, void **, bool, bool *);
	StucErr (*fpJobHandleDestroy)(void *, void **);
	StucErr (*fpGetJobErr)(void *, void *, StucErr *);
	bool (*fpGetAndDoJob)(void *);
	void (*fpMutexGet)(void *, void **);
	void (*fpMutexLock)(void *, void *);
	void (*fpMutexUnlock)(void *, void *);
	void (*fpMutexDestroy)(void *, void *);
	void (*fpBarrierGet)(void *, void **, int32_t);
	bool (*fpBarrierWait)(void *, void *);
	void (*fpBarrierDestroy)(void *, void *);
	void (*fpDestroy)(void *);
} StucThreadPool;

typedef PixioFileOpenType StucFileOpenType;

typedef struct StucIo {
	StucErr (*fpOpen)(void **, const char *, StucFileOpenType, const StucAlloc *);
	StucErr (*fpWrite)(void *, const unsigned char *, int32_t);
	StucErr (*fpRead)(void *, unsigned char *, int32_t);
	StucErr (*fpClose)(void *);
} StucIo;

typedef struct StucImage {
	void *pData;
	StucImageType type;
	int32_t res;
} StucImage;

typedef struct StucFlatCutoffIdx {
	int32_t idx;
	bool enabled;
} StucFlatCutoffIdx;

typedef struct StucUsg {
	StucObject obj;
	StucFlatCutoffIdx flatCutoff;
} StucUsg;

typedef struct StucUsgArr {
	StucUsg *pArr;
	int32_t size;
	int32_t count;
} StucUsgArr;

#define STUC_STAGE_NAME_LEN 64
typedef struct StucStageReport {
	char stage[STUC_STAGE_NAME_LEN];
	void (*fpBegin)(void *, struct StucStageReport *, const char *);
	void (*fpProgress)(void *, struct StucStageReport* , int32_t);
	void (*fpEnd)(void *, struct StucStageReport *);
	int32_t progress;
	int32_t outOf;
} StucStageReport;

#ifdef __cplusplus
extern "C" {
#endif
STUC_EXPORT
StucErr stucThreadPoolSetCustom(StucContext context, const StucThreadPool *pThreadPool);
STUC_EXPORT
StucErr stucContextInit(
	StucContext *ppCtx,
	StucAlloc *pAlloc,
	StucThreadPool *pTheadPool,
	StucIo *pIo,
	StucTypeDefaultConfig *pTypeDefaultConfig,
	StucStageReport *pStageReport
);
STUC_EXPORT
StucErr stucMapExportInit(
	StucContext pCtx,
	StucMapExport **ppHandle,
	const char *pPath,
	bool compress
);
STUC_EXPORT
StucErr stucMapExportEnd(StucMapExport **ppHandle);
STUC_EXPORT
StucErr stucMapExportTargetAdd(
	StucMapExport *pHandle,
	const StucMapArr *pMapArr,
	const StucObject *pObj,
	const StucAttribIndexedArr *pIndexedAttribs,
	float wScale,
	float receiveLen
);
STUC_EXPORT
StucErr stucMapExportObjAdd(
	StucMapExport *pHandle,
	const StucObject *pObj,
	const StucAttribIndexedArr *pIndexedAttribs
);
STUC_EXPORT
StucErr stucMapExportUsgAdd(StucMapExport *pHandle, StucUsg *pUsg);
STUC_EXPORT
StucErr stucMapExportUsgCutoffAdd(StucMapExport *pHandle, StucObject *pFlatCutoff);
STUC_EXPORT
StucErr stucMapFileLoadForEdit(
	StucContext pCtx,
	const char *filePath,
	int32_t *pObjCount,
	StucObject **ppObjArr,
	int32_t *pUsgCount,
	StucUsg **ppUsgArr,
	int32_t *pFlatCutoffCount,
	StucObject **ppFlatCutoffArr,
	StucAttribIndexedArr *pIndexedAttribs
);
STUC_EXPORT
StucErr stucMapFileLoad(
	StucContext pCtx,
	const char *filePath,
	PixErr (* fpMapGet)(const char *, const char **, StucMap * const),
	PixErr (* fpMapStore)(const char *, StucMap)
);
STUC_EXPORT
StucErr stucMapFileUnload(StucContext pCtx, StucMap pMap);
//Use this to access the mesh contaned within a StucMap handle.
//Objects are collapsed in map handles, so if you want the original geometry
//call stucMapFileLoadForEdit instead. The latter will also include usg and flat-cutoff objects.
STUC_EXPORT
StucErr stucMapFileMeshGet(StucContext pCtx, StucMap pMap, const StucMesh **ppMesh);
STUC_EXPORT
StucErr stucMapNameGet(StucContext pCtx, StucMap pMap, const char **ppName);
STUC_EXPORT
StucErr stucMapPathGet(StucContext pCtx, StucMap pMap, const char **ppPath);
STUC_EXPORT
StucErr stucQueryCommonAttribs(
	StucContext pCtx,
	const StucMap pMap,
	const StucMesh *pMesh,
	StucBlendOptArr *pBlendOptArr
);
STUC_EXPORT
StucErr stucCommonBlendOptArrGetFromDomain(
	StucContext pCtx,
	StucBlendOptArr *pList,
	StucDomain domain,
	StucBlendOptArr **ppArr
);
STUC_EXPORT
StucErr stucDestroyBlendOptArr(
	StucContext pCtx,
	StucBlendOptArr *pBlendOptArr
);
STUC_EXPORT
StucErr stucQueueMapToMesh(
	StucContext pCtx,
	void **ppJobHandle,
	StucMapArr *pMapArr,
	StucMesh *pMeshIn,
	StucAttribIndexedArr *pInIndexedAttribs,
	StucMesh *pMeshOut,
	StucAttribIndexedArr *pOutIndexedAttribs,
	float wScale,
	float receiveLen
);
STUC_EXPORT
StucErr stucMapToMesh(
	StucContext pCtx,
	const StucMapArr *pMapArr,
	const StucMesh *pMeshIn,
	const StucAttribIndexedArr *pInIndexedAttribs,
	StucMesh *pMeshOut,
	StucAttribIndexedArr *pOutIndexedAttribs,
	float wScale,
	float receiveLen,
	bool keepExistingIdxAttribs
);
STUC_EXPORT
StucErr stucObjArrDestroy(const StucContext pCtx, StucObjArr *pArr);
STUC_EXPORT
StucErr stucUsgArrDestroy(StucContext pCtx, int32_t count, StucUsg *pUsgArr);
STUC_EXPORT
StucErr stucMeshDestroy(StucContext pCtx, StucMesh *pMesh);
STUC_EXPORT
StucErr stucContextDestroy(StucContext pCtx);
STUC_EXPORT
StucErr stucGetAttribSize(StucAttribCore *pAttrib, int32_t *pSize);
STUC_EXPORT
StucErr stucGetAttrib(const char *pName, StucAttribArray *pAttribs, StucAttrib **ppAttrib);
STUC_EXPORT
StucErr stucAttribGetAsVoid(StucAttribCore *pAttrib, int32_t idx, void **ppOut);
STUC_EXPORT
StucErr stucGetAttribIndexed(
	const char *pName,
	StucAttribIndexedArr *pAttribs,
	StucAttribIndexed **ppAttrib
);
STUC_EXPORT
StucErr stucAttribActiveGet(
	StucContext pCtx,
	StucMesh *pMesh,
	StucAttribUse use,
	StucAttrib **ppAttrib
);
STUC_EXPORT
void stucMapIndexedAttribsGet(
	StucContext pCtx,
	StucMap pMap,
	StucAttribIndexedArr *pIndexedAttribs
);
STUC_EXPORT
StucErr stucWaitForJobs(
	StucContext pCtx,
	int32_t count,
	void **ppHandles,
	bool wait,
	bool *pDone
);
STUC_EXPORT
StucErr stucJobGetErrs(
	StucContext pCtx,
	int32_t jobCount,
	void ***pppJobHandles
);
STUC_EXPORT
void stucJobDestroyHandles(
	StucContext pCtx,
	int32_t jobCount,
	void **ppJobHandles
);
STUC_EXPORT
StucErr stucAttribSpTypesGet(StucContext pCtx, const StucAttribType **ppTypes);
STUC_EXPORT
StucErr stucAttribSpDomainsGet(StucContext pCtx, const StucDomain **ppDomains);
STUC_EXPORT
StucErr stucAttribSpIsValid(
	StucContext pCtx,
	const StucAttribCore *pCore,
	StucDomain domain
);
STUC_EXPORT
StucErr stucAttribGetAllDomains(
	StucContext pCtx,
	StucMesh *pMesh,
	const char *pName,
	StucAttrib **ppAttrib,
	StucDomain *pDomain
);
STUC_EXPORT
StucErr stucAttribGetAllDomainsConst(
	StucContext pCtx,
	const StucMesh *pMesh,
	const char *pName,
	const StucAttrib **ppAttrib,
	StucDomain *pDomain
);
STUC_EXPORT
StucErr stucAttribArrGet(
	StucContext pCtx,
	StucMesh *pMesh,
	StucDomain domain,
	StucAttribArray **ppArr
);
STUC_EXPORT
StucErr stucAttribArrGetConst(
	StucContext pCtx,
	const StucMesh *pMesh,
	StucDomain domain,
	const StucAttribArray **ppArr
);
STUC_EXPORT
StucErr stucAttribGetCompType(
	StucContext pCtx,
	StucAttribType type,
	StucAttribType *pCompType
);
STUC_EXPORT
StucErr stucAttribTypeGetVecSize(
	StucContext pCtx,
	StucAttribType type,
	int32_t *pSize
);
STUC_EXPORT
StucErr stucDomainCountGet(
	StucContext pCtx,
	const StucMesh *pMesh,
	StucDomain domain,
	int32_t *pCount
);
STUC_EXPORT
StucErr stucAttribIndexedArrDestroy(StucContext pCtx, StucAttribIndexedArr *pArr);
STUC_EXPORT
StucErr stucMapArrDestroy(StucContext pCtx, StucMapArr *pMapArr);
STUC_EXPORT
StucErr stucObjectInit(
	StucContext pCtx,
	StucObject *pObj,
	StucMesh *pMesh,
	const Stuc_M4x4 *pTransform
);
#ifdef __cplusplus
}
#endif
