#pragma once

#include <RUVM.h>

#define FLOAT_EQUAL_MARGIN .0001f

typedef Ruvm_V2_I8 V2_I8;
typedef Ruvm_V2_I16 V2_I16;
typedef Ruvm_V2_I32 V2_I32;
typedef Ruvm_V2_I64 V2_I64;
typedef Ruvm_V2_F32 V2_F32;
typedef Ruvm_V2_F64 V2_F64;
typedef Ruvm_V3_I8 V3_I8;
typedef Ruvm_V3_I16 V3_I16;
typedef Ruvm_V3_I32 V3_I32;
typedef Ruvm_V3_I64 V3_I64;
typedef Ruvm_V3_F32 V3_F32;
typedef Ruvm_V3_F64 V3_F64;
typedef Ruvm_V4_I8 V4_I8;
typedef Ruvm_V4_I16 V4_I16;
typedef Ruvm_V4_I32 V4_I32;
typedef Ruvm_V4_I64 V4_I64;
typedef Ruvm_V4_F32 V4_F32;
typedef Ruvm_V4_F64 V4_F64;
typedef Ruvm_String String;

typedef struct {
	float d[2][3];
} Mat2x3;

typedef struct {
	float d[2][2];
} Mat2x2;

typedef struct {
	float d[3][3];
} Mat3x3;

V2_I32 v2FloorAssign(V2_F32 *pA);

V3_F32 v3MultiplyScalar(V3_F32 a, float b);
void v3DivideEqualScalar(V3_F32 *pA, float b);
V3_F32 v3DivideScalar(V3_F32 a, float b);
V3_F32 v3SubtractScalar(V3_F32 a, float b);
V3_F32 v3AddScalar(V3_F32 a, float b);
V3_F32 v3Add(V3_F32 a, V3_F32 b);
V3_F32 v3Subtract(V3_F32 a, V3_F32 b);
void v3AddEqual(V3_F32 *pA, V3_F32 b);
int32_t v3GreaterThan(V3_F32 a, V3_F32 b);
int32_t v3LessThan(V3_F32 a, V3_F32 b);
int32_t v3AproxEqual(V3_F32 a, V3_F32 b);
V3_F32 v3Lerp(V3_F32 a, V3_F32 b, float alpha);
V3_F32 v3Cross(V3_F32 a, V3_F32 b);
V3_F32 v3UnitFromPoints(V3_F32 a, V3_F32 b);
V3_F32 v3MultiplyMat3x3(V3_F32 a, Mat3x3 *pB);
V3_F32 v3Normalize(V3_F32 a);
float v3Dot(V3_F32 a, V3_F32 b);
int32_t v3IsFinite(V3_F32);
V3_F32 cartesianToBarycentric(V2_F32 *pTri, V2_F32 *pPoint);
V3_F32 barycentricToCartesian(V3_F32 *pTri, V3_F32 *pPoint);

V2_F32 v2Abs(V2_F32 a);
V2_F32 v2Multiply(V2_F32 a, V2_F32 b);
void v2MultiplyEqual(V2_F32 *pA, V2_F32 b);
V2_F32 v2DivideScalar(V2_F32 a, float b);
void v2DivideEqualScalar(V2_F32 *pA, float b);
V2_F32 v2Subtract(V2_F32 a, V2_F32 b);
void v2SubtractEqual(V2_F32 *pA, V2_F32 b);
V2_F32 v2SubtractScalar(V2_F32 a, float b);
V2_F32 v2Add(V2_F32 a, V2_F32 b);
V2_F32 v2AddScalar(V2_F32 a, float b);
void v2AddEqual(V2_F32 *pA, V2_F32 b);
void v2AddEqualScalar(V2_F32 *pA, float b);
void v2MultiplyEqualScalar(V2_F32 *pA, float b);
V2_F32 v2MultiplyScalar(V2_F32 a, float b);
float v2Dot(V2_F32 a, V2_F32 b);
V2_F32 v2Cross(V2_F32 a);
V2_F32 v2ModScalar(V2_F32 a, float b);
void v2ModEqualScalar(V2_F32 *pA, float b);
int32_t v2GreaterThan(V2_F32 a, V2_F32 b);
int32_t v2GreaterThanScalar(V2_F32 a, float b);
int32_t v2GreaterThanEqualTo(V2_F32 a, V2_F32 b);
int32_t v2LessThan(V2_F32 a, V2_F32 b);
int32_t v2LessThanScalar(V2_F32 a, float b);
int32_t v2LessThanEqualTo(V2_F32 a, V2_F32 b);
int32_t v2NotEqual(V2_F32 a, V2_F32 b);
int32_t v2Equal(V2_F32 a, V2_F32 b);
int32_t v2AproxEqual(V2_F32 a, V2_F32 b);
int32_t v2WindingCompare(V2_F32 a, V2_F32 b, V2_F32 centre, int32_t fallBack);
int32_t v2DegenerateTri(V2_F32 a, V2_F32 b, V2_F32 centre, float threshold);
int32_t v2IsFinite(V2_F32);

Mat2x2 mat2x2Adjugate(Mat2x2 a);
float mat2x2Determinate(Mat2x2 a);
void mat2x2MultiplyEqualScalar(Mat2x2 *pA, float b);
Mat2x2 mat2x2Invert(Mat2x2 a);
Mat2x3 mat2x2MultiplyMat2x3(Mat2x2 a, Mat2x3 b);
int32_t mat2x2IsFinite(Mat2x2 *pA);
Mat3x3 mat3x3FromV3_F32(V3_F32 a, V3_F32 b, V3_F32 c);
Mat3x3 mat3x3Invert(Mat3x3 *pA);
int32_t mat3x3IsFinite(Mat3x3 *pA);

float customFloor(float a);

#define V3MULS ,3MultiplyScalar,
#define V3DIVEQLS ,3DivideEqualScalar,
#define V3DIVS ,3DivideScalar,
#define V3SUB ,3Subtract,
#define V3SUBS ,3SubtractScalar,
#define V3ADDS ,3AddScalar,
#define V3ADD ,3Add,
#define V3ADDEQL ,3AddEqual,
#define V3GREAT ,3GreaterThan,
#define V3LESS ,3LessThan,
#define V3CROSS ,3Cross,
#define V3APROXEQL ,3AproxEqual,
#define V3DOT ,3Dot,
#define V3MULM3X3 ,3MultiplyMat3x3,

#define V2MUL ,2Multiply,
#define V2MULEQL ,2MultiplyEqual,
#define V2DIVS ,2DivideScalar,
#define V2DIVSEQL ,2DivideEqualScalar,
#define V2SUB ,2Subtract,
#define V2SUBEQL ,2SubtractEqual,
#define V2SUBS ,2SubtractScalar,
#define V2ADD ,2Add,
#define V2ADDS ,2AddScalar,
#define V2ADDEQL ,2AddEqual,
#define V2ADDEQLS ,2AddEqualScalar,
#define V2MULSEQL ,2MultiplyEqualScalar,
#define V2MULS ,2MultiplyScalar,
#define V2DOT ,2Dot,
#define V2MODS ,2ModScalar,
#define V2MODEQLS ,2ModEqualScalar,
#define V2GREAT ,2GreaterThan,
#define V2GREATS ,2GreaterThanScalar,
#define V2GREATEQL ,2GreaterThanEqualTo,
#define V2LESS ,2LessThan,
#define V2LESSS ,2LessThanScalar,
#define V2LESSEQL ,2LessThanEqualTo,
#define V2NOTEQL ,2NotEqual,
#define V2EQL ,2Equal,
#define V2APROXEQL ,2AproxEqual,

#define INFIX(a,o,b) v##o((a),(b))
#define _(a) INFIX(a)
