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
#include <pixenals_thread_utils.h>
#include <pixenals_structs.h>
#include <cark_vis_io.h>

#define STUC_DISABLE_EDGES_IN_BUF

#define STUC_DISABLE_TRIANGULATION


#ifdef WIN32
	#define STUC_EXPORT __declspec(dllexport)
#else
	#define STUC_EXPORT __attribute__((visibility("default")))
#endif

#define STUC_ATTRIB_NAME_MAX_LEN 96
#define STUC_ATTRIB_STRING_MAX_LEN 64

struct StucMap;

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
	//TODO remove _NORMALS_VERT and just used _NORMAL with a different active attrib
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
	STUC_BLEND_APPEND, //strings only
	STUC_BLEND_ENUM_COUNT
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

typedef enum StucMapStatus {
	STUC_MAP_PENDING_LOAD,
	STUC_MAP_LOADED,
	STUC_MAP_ERROR,
	STUC_MAP_MISSING_DEP
} StucMapStatus;

typedef struct StucObjectData {
	StucObjectType type;
} StucObjectData;

typedef union StucMapOrIdx {
	struct StucMap *ptr;
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
	PixErr (*fpInit)(PixthPoolCtx *, int32_t *, const StucAlloc *, bool);
	StucErr (*pJobStackPushJobs)(
		PixthPoolCtx *,
		int32_t,
		int32_t,
		PixthJob *
	);
	StucErr (*fpWaitForJobs)(PixthPoolCtx *, int32_t, PixthJob *, int32_t, bool, bool *);
	StucErr (*fpGetJobErr)(PixthPoolCtx *, PixthJob *, StucErr *);
	StucErr (*fpLogDump)(PixthPoolCtx *, PixtyI8Arr *);
	void (*fpDestroy)(PixthPoolCtx *);
	PixthPoolCtx handle;
} StucThreadPool;

typedef PixioFileOpenType StucFileOpenType;

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
// v unrelated ^ TODO above is old and largely unused, overhaul or remove
typedef enum StucStage {
	STUC_STAGE_NONE,
	STUC_STAGE_ISLAND_SPLIT,
	STUC_STAGE_MAP,
	STUC_STAGE_BUFMESH_INIT,
	STUC_STAGE_OUTMESH,
	STUC_STAGE_ENUM_COUNT
} StucStage;

typedef struct StucCark {
	CarkOutCtx ctx;
	int32_t stageHandleArr[STUC_STAGE_ENUM_COUNT];
	bool valid;
} StucCark;

typedef struct StucCtx {
	void *pCustom;
	StucThreadPool threadPool;
	StucAlloc alloc;
	PixioFPtrs io;
	PixtyStrSized logPath;
	I32 threadCount;
	StucTypeDefaultConfig typeDefaults;

	//pretty much unused rn, this is old
	StucStageReport stageReport;
	I32 stageInterval;

	bool logEnabled;
} StucCtx;

#define STUC_MAP_FORMAT_NAME_MAX_LEN 19
#define STUC_MAP_FORMAT_NAME "UV Stucco Map"

typedef struct StucHeader {
	char format[STUC_MAP_FORMAT_NAME_MAX_LEN];
	int64_t dataSize;
	int64_t dataSizeCompressed;
	int32_t version;
	int32_t idxAttribCount;
	int32_t objCount;
	int32_t usgCount;
	int32_t cutoffCount;
} StucHeader;

typedef struct StucMapExport {
	StucCtx *pCtx;
	char *pPath;
	StucHeader header;
	PixioByteArr data;
	int32_t cutoffIdxMax;
	PixuctHTable mapTable;
	StucAttribIndexedArr idxAttribs;
	PixtyI8Arr matMapTable;
	bool compress;
} StucMapExport;

struct StucMapDepEntry;

typedef struct StucMapDepPtrArr {
	struct StucMapDepEntry **pArr;
	int32_t size;
	int32_t count;
} StucMapDepPtrArr;

typedef struct StucMapDepEntry {
	PixuctHTableEntryCore core;
	StucMapDepPtrArr deps;
	struct StucMap *pMap;
	double timestamp;
	char *pNameInFile;
	char *pName;
	char *pPath;
	StucMapStatus status;
	bool onStack;
	bool depsAdded;
} StucMapDepEntry;

typedef struct StucMapLoad {
	StucCtx *pCtx;
	const char *pName;
	void *pUserData;
	StucErr (* fpMapGet)(
		void *,
		const char *,
		const char *,
		const char **,
		double *,
		struct StucMap ** const,
		bool *
	);
	StucErr (* fpMapStore)(
		void *,
		const char *,
		const char *,
		double,
		struct StucMap *,
		StucMapStatus,
		const StucMapDepPtrArr *
	);
	PixuctHTable table;
	bool depsPassDone;
} StucMapLoad;


#ifdef __cplusplus
extern "C" {
#endif
STUC_EXPORT
//TODO replace old param names, context should be pCtx
StucErr stucThreadPoolSetCustom(StucCtx *context, const StucThreadPool *pThreadPool);
STUC_EXPORT
StucErr stucInit(
	StucCtx *pCtx,
	StucAlloc *pAlloc,
	StucThreadPool *pTheadPool,
	PixioFPtrs *pIo,
	StucTypeDefaultConfig *pTypeDefaultConfig,
	StucStageReport *pStageReport,
	bool threadLogging
);
STUC_EXPORT
StucErr stucMapExportInit(
	StucCtx *pCtx,
	StucMapExport *pHandle,
	const char *pPath,
	bool compress
);
STUC_EXPORT
StucErr stucMapExportEnd(StucMapExport *pHandle);
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
StucErr stucMapLoadForEdit(
	StucCtx *pCtx,
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
StucErr stucMapLoadInit(
	StucCtx *pCtx,
	StucMapLoad *pLoadCtx,
	const char *pName,
	void *pUserData,
	PixErr (* fpMapGet)(
		void *,
		const char *,
		const char *,
		const char **,
		double *,
		struct StucMap ** const,
		bool *
	),
	PixErr (* fpMapStore)(
		void *,
		const char *,
		const char *,
		double,
		struct StucMap *,
		StucMapStatus,
		const StucMapDepPtrArr *
	)
);
STUC_EXPORT
StucErr stucMapLoadDeps(StucMapLoad *pLoadCtx);
STUC_EXPORT
StucErr stucMapLoad(StucMapLoad *pLoadCtx);
STUC_EXPORT
StucErr stucMapLoadIterInit(StucMapLoad *pLoadCtx, PixalcLinAllocIter *pIter);
STUC_EXPORT
int32_t stucMapLoadIterAtEnd(PixalcLinAllocIter *pIter);
STUC_EXPORT
void stucMapLoadIterInc(PixalcLinAllocIter *pIter);
STUC_EXPORT
StucMapDepEntry *stucMapLoadIterGetMap(PixalcLinAllocIter *pIter);
STUC_EXPORT
StucErr stucMapLoadDestroy(StucMapLoad *pLoadCtx);
STUC_EXPORT
StucErr stucMapUnload(StucCtx *pCtx, struct StucMap *pMap);
//Use this to access the mesh contaned within a StucMap handle.
//Objects are collapsed in map handles, so if you want the original geometry
//call stucMapLoadForEdit instead. The latter will also include usg and flat-cutoff objects.
STUC_EXPORT
StucErr stucMapMeshGet(
	StucCtx *pCtx,
	struct StucMap *pMap,
	const StucMesh **ppMesh,
	StucAttribIndexedArr **ppIdxAttribs
);
STUC_EXPORT
StucErr stucMapNameGet(StucCtx *pCtx, struct StucMap *pMap, const char **ppName);
STUC_EXPORT
StucErr stucQueryCommonAttribs(
	StucCtx *pCtx,
	const struct StucMap *pMap,
	const StucMesh *pMesh,
	StucBlendOptArr *pBlendOptArr
);
STUC_EXPORT
StucErr stucCommonBlendOptArrGetFromDomain(
	StucCtx *pCtx,
	StucBlendOptArr *pList,
	StucDomain domain,
	StucBlendOptArr **ppArr
);
/*
STUC_EXPORT
StucErr stucDestroyBlendOptArr(
	StucCtx *pCtx,
	StucBlendOptArr *pBlendOptArr
);
*/
STUC_EXPORT
StucErr stucQueueMapToMesh(
	StucCtx *pCtx,
	PixthJob *pJobHandle,
	StucMapArr *pMapArr,
	StucMesh *pMeshIn,
	StucAttribIndexedArr *pInIndexedAttribs,
	StucMesh *pMeshOut,
	StucAttribIndexedArr *pOutIndexedAttribs,
	float wScale,
	float receiveLen,
	bool triangulate
);
STUC_EXPORT
StucErr stucMapToMesh(
	StucCtx *pCtx,
	I32 threadId,
	const StucMapArr *pMapArr,
	const StucMesh *pMeshIn,
	const StucAttribIndexedArr *pInIndexedAttribs,
	StucMesh *pMeshOut,
	StucAttribIndexedArr *pOutIndexedAttribs,
	float wScale,
	float receiveLen,
	bool keepExistingIdxAttribs,
	bool triangulate
);
STUC_EXPORT
StucErr stucObjArrDestroy(const StucCtx *pCtx, StucObjArr *pArr);
STUC_EXPORT
StucErr stucUsgArrDestroy(const StucCtx *pCtx, int32_t count, StucUsg *pUsgArr);
STUC_EXPORT
StucErr stucAttribArrDestroy(const StucCtx *pCtx, StucAttribArray *pArr);
STUC_EXPORT
StucErr stucMeshDestroy(const StucCtx *pCtx, StucMesh *pMesh);
STUC_EXPORT
StucErr stucContextDestroy(StucCtx *pCtx);
STUC_EXPORT
StucErr stucGetAttribSize(const StucAttribCore *pAttrib, int32_t *pSize);
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
	StucCtx *pCtx,
	StucMesh *pMesh,
	StucAttribUse use,
	StucAttrib **ppAttrib
);
STUC_EXPORT
void stucMapIndexedAttribsGet(
	StucCtx *pCtx,
	struct StucMap *pMap,
	StucAttribIndexedArr *pIndexedAttribs
);
STUC_EXPORT
StucErr stucWaitForJobs(
	StucCtx *pCtx,
	int32_t count,
	PixthJob *pHandles,
	bool wait,
	bool *pDone
);
STUC_EXPORT
StucErr stucJobGetErrs(
	StucCtx *pCtx,
	int32_t jobCount,
	PixthJob *pJobHandles
);
STUC_EXPORT
const char *stucAttribSpNameGet(const StucCtx *pCtx, int32_t idx);
STUC_EXPORT
StucAttribType stucAttribSpTypeGet(const StucCtx *pCtx, int32_t idx);
STUC_EXPORT
StucDomain stucAttribSpDomainGet(const StucCtx *pCtx, int32_t idx);
STUC_EXPORT
StucErr stucAttribSpIsValid(
	StucCtx *pCtx,
	const StucAttribCore *pCore,
	StucDomain domain
);
//TODO replace 'AllDomains' funcs with single func that takes domains as bitflags
STUC_EXPORT
StucErr stucAttribGetAllDomains(
	StucCtx *pCtx,
	StucMesh *pMesh,
	const char *pName,
	StucAttrib **ppAttrib,
	int32_t *pIdx,
	StucDomain *pDomain
);
STUC_EXPORT
StucErr stucAttribGetAllDomainsConst(
	StucCtx *pCtx,
	const StucMesh *pMesh,
	const char *pName,
	const StucAttrib **ppAttrib,
	int32_t *pIdx,
	StucDomain *pDomain
);
STUC_EXPORT
StucErr stucAttribArrGet(
	StucCtx *pCtx,
	StucMesh *pMesh,
	StucDomain domain,
	StucAttribArray **ppArr
);
STUC_EXPORT
StucErr stucAttribArrGetConst(
	StucCtx *pCtx,
	const StucMesh *pMesh,
	StucDomain domain,
	const StucAttribArray **ppArr
);
STUC_EXPORT
StucErr stucAttribGetCompType(
	StucCtx *pCtx,
	StucAttribType type,
	StucAttribType *pCompType
);
STUC_EXPORT
StucErr stucAttribTypeGetVecSize(
	StucCtx *pCtx,
	StucAttribType type,
	int32_t *pSize
);
STUC_EXPORT
StucErr stucDomainCountGet(
	StucCtx *pCtx,
	const StucMesh *pMesh,
	StucDomain domain,
	int32_t *pCount
);
STUC_EXPORT
StucErr stucAttribIndexedArrDestroy(StucCtx *pCtx, StucAttribIndexedArr *pArr);
STUC_EXPORT
StucErr stucMapArrDestroy(StucCtx *pCtx, StucMapArr *pMapArr);
STUC_EXPORT
StucErr stucObjectInit(
	StucCtx *pCtx,
	StucObject *pObj,
	StucMesh *pMesh,
	const Stuc_M4x4 *pTransform
);
STUC_EXPORT
StucErr stucMeshAttribsCornerToVert(StucCtx *pCtx, StucMesh *pMesh);
STUC_EXPORT
StucErr stucMeshBuildTangentsForTris(StucCtx *pCtx, StucMesh *pMesh);
STUC_EXPORT
StucErr stucMeshTriangulate(StucCtx *pCtx, StucMesh *pMesh);
STUC_EXPORT
StucErr stucMeshAllocCopy(
	StucCtx *pCtx,
	StucMesh *pDest,
	const StucMesh *pSrc,
	bool activeOnly
);
STUC_EXPORT
StucErr stucCopyMesh(StucCtx *pCtx, StucMesh *pDest, const StucMesh *pSrc);
STUC_EXPORT
//TODO should StucCtx *be const in helper funcs like this?
StucErr stucMapZBoundsGet(StucCtx *pCtx, const struct StucMap *pMap, PixtyV2_F32 *pZBounds);
STUC_EXPORT
void stucLogEnableSet(StucCtx *pCtx, bool value);
STUC_EXPORT
StucErr stucLogPathSet(StucCtx *pCtx, const char *pPath);

#ifdef STUC_DEBUG_UTILS
static inline
StucErr stucThreadPoolLogDump(StucCtx *pCtx, const char *pPath) {
	StucErr err = PIX_ERR_SUCCESS;
	PixtyI8Arr log = {0};
	err = pCtx->threadPool.fpLogDump(&pCtx->threadPool.handle, &log);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	void *pFile = NULL;
	err = pixioFileOpen(&pFile, pPath, PIX_IO_FILE_OPEN_WRITE, &pCtx->alloc);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	err = pixioFileWrite(pFile, log.pArr, log.count);
	PIX_ERR_THROW_IFNOT(err, "", 1);
	PIX_ERR_CATCH(1, err, ;);
	err = pixioFileClose(pFile);
	PIX_ERR_THROW_IFNOT(err, "", 0);
	PIX_ERR_CATCH(0, err, ;);
	if (log.pArr) {
		pCtx->alloc.fpFree(log.pArr);
	}
	return err;
}
#endif

#ifdef __cplusplus
}
#endif
