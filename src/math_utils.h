/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <pixenals_types.h>

#define PIX_MATH_EPSILON .0000001f
#define pixmF32_EQL_INTERN(a, b) (fabsf((a) - (b)) <= PIX_MATH_EPSILON)
#define pixmF64_EQL_INTERN(a, b) (fabs((a) - (b)) <= PIX_MATH_EPSILON)
#define pixmF32_NOTEQL_INTERN(a, b) (fabsf((a) - (b)) > PIX_MATH_EPSILON)
#define pixmF64_NOTEQL_INTERN(a, b) (fabs((a) - (b)) > PIX_MATH_EPSILON)
#define pixmF32_GREAT_INTERN(a, b) ((a) - (b) > PIX_MATH_EPSILON)
#define pixmF64_GREAT_INTERN(a, b) pixmF32_GREAT_INTERN(a, b)
#define pixmF32_LESS_INTERN(a, b) ((a) - (b) < -PIX_MATH_EPSILON)
#define pixmF64_LESS_INTERN(a, b) pixmF32_LESS_INTERN(a, b)
#define pixmF32_GREATEQL_INTERN(a, b) (pixmF32_GREAT_INTERN(a, b) || pixmF32_EQL_INTERN(a, b))
#define pixmF64_GREATEQL_INTERN(a, b) (pixmF64_GREAT_INTERN(a, b) || pixmF64_EQL_INTERN(a, b))
#define pixmF32_LESSEQL_INTERN(a, b) (pixmF32_LESS_INTERN(a, b) || pixmF32_EQL_INTERN(a, b))
#define pixmF64_LESSEQL_INTERN(a, b) (pixmF64_LESS_INTERN(a, b) || pixmF64_EQL_INTERN(a, b))
#define F32_EQL ,F32_EQL_INTERN,
#define F64_EQL ,F64_EQL_INTERN,
#define F32_NOTEQL ,F32_NOTEQL_INTERN,
#define F64_NOTEQL ,F64_NOTEQL_INTERN,
#define F32_GREAT ,F32_GREAT_INTERN,
#define F64_GREAT ,F64_GREAT_INTERN,
#define F32_LESS ,F32_LESS_INTERN,
#define F64_LESS ,F64_LESS_INTERN,
#define F32_GREATEQL ,F32_GREATEQL_INTERN,
#define F64_GREATEQL ,F64_GREATEQL_INTERN,
#define F32_LESSEQL ,F32_LESSEQL_INTERN,
#define F64_LESSEQL ,F64_LESSEQL_INTERN,


#define PIX_MATH_IDENT_MAT4X4 (Stuc_M4x4) {\
	1.0, .0, .0, .0,\
	.0, 1.0, .0, .0,\
	.0, .0, 1.0, .0,\
	.0, .0, .0, 1.0\
}

#define PIXM_MIN(a, b) (a < b ? a : b)
#define PIXM_MAX(a, b) (a > b ? a : b)

static inline
float pixmF32Lerp(float a, float b, float alpha) {
	return b * alpha + (1.0 - alpha) * a;
}

static inline
double pixmF64Lerp(double a, double b, double alpha) {
	return b * alpha + (1.0 - alpha) * a;
}

PixtyV2_I32 pixmV2F32FloorAssign(PixtyV2_F32 *pA);

PixtyV4_F32 pixmV4F32MultiplyM4x4(PixtyV4_F32 a, const PixtyM4x4 *pB);
void pixmV4F32MultiplyEqualM4x4(PixtyV4_F32 *pA, const PixtyM4x4 *pB);
PixtyV3_F32 pixmDivideByW(const PixtyV4_F32 *pA);

PixtyV3_F32 pixmV3F32MultiplyScalar(PixtyV3_F32 a, float b);
PixtyV3_F64 pixmV3F64MultiplyScalar(PixtyV3_F64 a, double b);
void pixmV3F32DivideEqualScalar(PixtyV3_F32 *pA, float b);
PixtyV3_F32 pixmV3F32DivideScalar(PixtyV3_F32 a, float b);
PixtyV3_F32 pixmV3F32SubtractScalar(PixtyV3_F32 a, float b);
PixtyV3_F32 pixmV3F32AddScalar(PixtyV3_F32 a, float b);
PixtyV3_F32 pixmV3F32Add(PixtyV3_F32 a, PixtyV3_F32 b);
PixtyV3_F64 pixmV3F64Add(PixtyV3_F64 a, PixtyV3_F64 b);
PixtyV3_F32 pixmV3F32Subtract(PixtyV3_F32 a, PixtyV3_F32 b);
PixtyV3_F64 pixmV3F64Subtract(PixtyV3_F64 a, PixtyV3_F64 b);
void pixmV3F32AddEqual(PixtyV3_F32 *pA, PixtyV3_F32 b);
bool pixmV3F32Equal(PixtyV3_F32 a, PixtyV3_F32 b);
bool pixmV3F64Equal(PixtyV3_F64 a, PixtyV3_F64 b);
bool pixmV3F32GreaterThan(PixtyV3_F32 a, PixtyV3_F32 b);
bool pixmV3F32LessThan(PixtyV3_F32 a, PixtyV3_F32 b);
PixtyV3_F32 pixmV3F32Lerp(PixtyV3_F32 a, PixtyV3_F32 b, float alpha);
PixtyV3_F32 pixmV3F32Cross(PixtyV3_F32 a, PixtyV3_F32 b);
PixtyV3_F64 pixmV3F64Cross(PixtyV3_F64 a, PixtyV3_F64 b);
PixtyV3_F32 pixmV3F32UnitFromPoints(PixtyV3_F32 a, PixtyV3_F32 b);
PixtyV3_F32 pixmV3F32MultiplyM3x3(PixtyV3_F32 a, const PixtyM3x3 *pB);
void pixmV3F32MultiplyEqualM3x3(PixtyV3_F32 *pA, const PixtyM3x3 *pB);
PixtyV3_F32 pixmV3F32Normalize(PixtyV3_F32 a);
float pixmV3F32Dot(PixtyV3_F32 a, PixtyV3_F32 b);
double pixmV3F64Dot(PixtyV3_F64 a, PixtyV3_F64 b);
bool pixmV3F32IsFinite(PixtyV3_F32);
bool pixmV3F32DegenerateTri(PixtyV3_F32 a, PixtyV3_F32 b, PixtyV3_F32 c, float threshold);
float pixmV3F32TriHeight(PixtyV3_F32 a, PixtyV3_F32 b, PixtyV3_F32 c);
float pixmV3F32SquareLen(PixtyV3_F32);
float pixmV3F32Len(PixtyV3_F32 a);
float pixmV3F32TriArea(PixtyV3_F32 a, PixtyV3_F32 b, PixtyV3_F32 c);
PixtyV3_F32 pixmCartesianToBarycentric(const PixtyV2_F32 *pTri, const PixtyV2_F32 *pPoint);
PixtyV3_F32 pixmBarycentricToCartesian(const PixtyV3_F32 *pTri, PixtyV3_F32 point);

PixtyV2_F32 pixmV2F32Abs(PixtyV2_F32 a);
PixtyV2_F32 pixmV2F32Multiply(PixtyV2_F32 a, PixtyV2_F32 b);
void pixmV2F32MultiplyEqual(PixtyV2_F32 *pA, PixtyV2_F32 b);
PixtyV2_F32 pixmV2F32DivideScalar(PixtyV2_F32 a, float b);
void pixmV2F32DivideEqualScalar(PixtyV2_F32 *pA, float b);
PixtyV2_F32 pixmV2F32Subtract(PixtyV2_F32 a, PixtyV2_F32 b);
PixtyV2_F64 pixmV2F64Subtract(PixtyV2_F64 a, PixtyV2_F64 b);
void pixmV2F32SubtractEqual(PixtyV2_F32 *pA, PixtyV2_F32 b);
PixtyV2_F32 pixmV2F32SubtractScalar(PixtyV2_F32 a, float b);
PixtyV2_F32 pixmV2F32Add(PixtyV2_F32 a, PixtyV2_F32 b);
PixtyV2_F32 pixmV2F32AddScalar(PixtyV2_F32 a, float b);
void pixmV2F32AddEqual(PixtyV2_F32 *pA, PixtyV2_F32 b);
void pixmV2F32AddEqualScalar(PixtyV2_F32 *pA, float b);
void pixmV2F32MultiplyEqualScalar(PixtyV2_F32 *pA, float b);
PixtyV2_F32 pixmV2F32MultiplyScalar(PixtyV2_F32 a, float b);
float pixmV2F32Dot(PixtyV2_F32 a, PixtyV2_F32 b);
double pixmV2F64Dot(PixtyV2_F64 a, PixtyV2_F64 b);
float pixmV2F32Cross(PixtyV2_F32 a, PixtyV2_F32 b);
double pixmV2F64Cross(PixtyV2_F64 a, PixtyV2_F64 b);
PixtyV2_F32 pixmV2F32LineNormal(PixtyV2_F32 a);
PixtyV2_F64 pixmV2F64LineNormal(PixtyV2_F64 a);
PixtyV2_F32 pixmV2F32ModScalar(PixtyV2_F32 a, float b);
void pixmV2F32ModEqualScalar(PixtyV2_F32 *pA, float b);
float pixmV2F32SquareLen(PixtyV2_F32 a);
float pixmV2F32Len(PixtyV2_F32 a);
float pixmV2F32TriArea(PixtyV2_F32 a, PixtyV2_F32 b, PixtyV2_F32 c);
float pixmV2F32Determinate(PixtyV2_F32 a, PixtyV2_F32 b);
double pixmV2F64Determinate(PixtyV2_F64 a, PixtyV2_F64 b);
bool pixmV2F32GreaterThan(PixtyV2_F32 a, PixtyV2_F32 b);
bool pixmV2F32GreaterThanScalar(PixtyV2_F32 a, float b);
bool pixmV2F32GreaterThanEqualTo(PixtyV2_F32 a, PixtyV2_F32 b);
bool pixmV2F32LessThan(PixtyV2_F32 a, PixtyV2_F32 b);
bool pixmV2F32LessThanScalar(PixtyV2_F32 a, float b);
bool pixmV2F32LessThanEqualTo(PixtyV2_F32 a, PixtyV2_F32 b);
bool pixmV2F32NotEqual(PixtyV2_F32 a, PixtyV2_F32 b);
bool pixmV2F32Equal(PixtyV2_F32 a, PixtyV2_F32 b);
bool pixmV2F64Equal(PixtyV2_F64 a, PixtyV2_F64 b);
bool pixmV2F32AproxEqual(PixtyV2_F32 a, PixtyV2_F32 b);
bool pixmV2F32AproxEqualThres(PixtyV2_F32 a, PixtyV2_F32 b, float threshold);
bool pixmV2F32DegenerateTri(PixtyV2_F32 a, PixtyV2_F32 b, PixtyV2_F32 c, float threshold);
float pixmV2F32TriHeight(PixtyV2_F32 a, PixtyV2_F32 b, PixtyV2_F32 c);
bool pixmV2F32IsFinite(PixtyV2_F32);

bool pixmV2I8Equal(PixtyV2_I8 a, PixtyV2_I8 b);
bool pixmV2I16Equal(PixtyV2_I16 a, PixtyV2_I16 b);
bool pixmV2I32Equal(PixtyV2_I32 a, PixtyV2_I32 b);
bool pixmV2I64Equal(PixtyV2_I64 a, PixtyV2_I64 b);
bool pixmV3I8Equal(PixtyV3_I8 a, PixtyV3_I8 b);
bool pixmV3I16Equal(PixtyV3_I16 a, PixtyV3_I16 b);
bool pixmV3I32Equal(PixtyV3_I32 a, PixtyV3_I32 b);
bool pixmV3I64Equal(PixtyV3_I64 a, PixtyV3_I64 b);
bool pixmV4I8Equal(PixtyV4_I8 a, PixtyV4_I8 b);
bool pixmV4I16Equal(PixtyV4_I16 a, PixtyV4_I16 b);
bool pixmV4I32Equal(PixtyV4_I32 a, PixtyV4_I32 b);
bool pixmV4I64Equal(PixtyV4_I64 a, PixtyV4_I64 b);

PixtyM2x2 pixmM2x2Adjugate(PixtyM2x2 a);
float pixmM2x2Determinate(PixtyM2x2 a);
void pixmM2x2MultiplyEqualScalar(PixtyM2x2 *pA, float b);
PixtyM2x2 pixmM2x2Invert(PixtyM2x2 a);
PixtyM2x3 pixmM2x2MultiplyM2x3(PixtyM2x2 a, PixtyM2x3 b);
bool pixmM2x2IsFinite(const PixtyM2x2 *pA);
PixtyM3x3 pixmM3x3FromPxtyp_V3_F32(PixtyV3_F32 a, PixtyV3_F32 b, PixtyV3_F32 c);
PixtyM3x3 pixmM3x3FromM4x4(const PixtyM4x4 *pA);
PixtyM3x3 pixmM3x3Invert(const PixtyM3x3 *pA);
bool pixmM3x3IsFinite(const PixtyM3x3 *pA);

bool pixmV4F32Equal(PixtyV4_F32 a, PixtyV4_F32 b);
bool pixmV4F64Equal(PixtyV4_F64 a, PixtyV4_F64 b);

float pixmFloor(float a);

#define V4MULM4X4 ,V4F32MultiplyM4x4,
#define V4MULEQLM4X4 ,V4F32MultiplyEqualM4x4,

#define V3MULS ,V3F32MultiplyScalar,
#define V3DIVEQLS ,V3F32DivideEqualScalar,
#define V3DIVS ,V3F32DivideScalar,
#define V3SUB ,V3F32Subtract,
#define V3SUBS ,V3F32SubtractScalar,
#define V3ADDS ,V3F32AddScalar,
#define V3ADD ,V3F32Add,
#define V3ADDEQL ,V3F32AddEqual,
#define V3EQL ,V3F32Equal,
#define V364EQL ,V3F64Equal,
#define V3GREAT ,V3F32GreaterThan,
#define V3LESS ,V3F32LessThan,
#define V3CROSS ,V3F32Cross,
#define V3APROXEQL ,V3F32AproxEqual,
#define V3DOT ,V3F32Dot,
#define V3MULM3X3 ,V3F32MultiplyM3x3,
#define V3MULEQLM3X3 ,V3F32MultiplyEqualM3x3,

#define V2MUL ,V2F32Multiply,
#define V2MULEQL ,V2F32MultiplyEqual,
#define V2DIVS ,V2F32DivideScalar,
#define V2DIVSEQL ,V2F32DivideEqualScalar,
#define V2SUB ,V2F32Subtract,
#define V2SUBEQL ,V2F32SubtractEqual,
#define V2SUBS ,V2F32SubtractScalar,
#define V2ADD ,V2F32Add,
#define V2ADDS ,V2F32AddScalar,
#define V2ADDEQL ,V2F32AddEqual,
#define V2ADDEQLS ,V2F32AddEqualScalar,
#define V2MULSEQL ,V2F32MultiplyEqualScalar,
#define V2MULS ,V2F32MultiplyScalar,
#define V2DOT ,V2F32Dot,
#define V2DET ,V2F32Determinate,
#define V2CROSS ,V2F32Cross,
#define V2MODS ,V2F32ModScalar,
#define V2MODEQLS ,V2F32ModEqualScalar,
#define V2GREAT ,V2F32GreaterThan,
#define V2GREATS ,V2F32GreaterThanScalar,
#define V2GREATEQL ,V2F32GreaterThanEqualTo,
#define V2LESS ,V2F32LessThan,
#define V2LESSS ,V2F32LessThanScalar,
#define V2LESSEQL ,V2F32LessThanEqualTo,
#define V2NOTEQL ,V2F32NotEqual,
#define V2EQL ,V2F32Equal,
#define V264EQL ,V2F64Equal,
#define V2APROXEQL ,V2F32AproxEqual,

#define V4EQL ,V4F32Equal,
#define V464EQL ,V4F64Equal,

#define V2I8EQL ,V2I8Equal,
#define V2I16EQL ,V2I16Equal,
#define V2IEQL ,V2IEqual,
#define V2I64EQL ,V2I64Equal,
#define V3I8EQL ,V3I8Equal,
#define V3I16EQL ,V3I16Equal,
#define V3IEQL ,V3IEqual,
#define V3I64EQL ,V3I64Equal,
#define V4I8EQL ,V4I8Equal,
#define V4I16EQL ,V4I16Equal,
#define V4IEQL ,V4IEqual,
#define V4I64EQL ,V4I64Equal,

#define PIXM_INFIX(a,o,b) pixm##o((a),(b))
#define _(a) PIXM_INFIX(a)
