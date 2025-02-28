#pragma once

#include <UvStucco.h>
#include <Types.h>

#define FLOAT_EQUAL_MARGIN .000002f

#define STUC_IDENT_MAT4X4 {\
	1.0, .0, .0, .0,\
	.0, 1.0, .0, .0,\
	.0, .0, 1.0, .0,\
	.0, .0, .0, 1.0\
}

static Stuc_M4x4_F32 identM4x4 = STUC_IDENT_MAT4X4;

typedef Stuc_V2_I8 V2_I8;
typedef Stuc_V2_I16 V2_I16;
typedef Stuc_V2_I32 V2_I32;
typedef Stuc_V2_I64 V2_I64;
typedef Stuc_V2_F32 V2_F32;
typedef Stuc_V2_F64 V2_F64;
typedef Stuc_V3_I8 V3_I8;
typedef Stuc_V3_I16 V3_I16;
typedef Stuc_V3_I32 V3_I32;
typedef Stuc_V3_I64 V3_I64;
typedef Stuc_V3_F32 V3_F32;
typedef Stuc_V3_F64 V3_F64;
typedef Stuc_V4_I8 V4_I8;
typedef Stuc_V4_I16 V4_I16;
typedef Stuc_V4_I32 V4_I32;
typedef Stuc_V4_I64 V4_I64;
typedef Stuc_V4_F32 V4_F32;
typedef Stuc_V4_F64 V4_F64;
typedef Stuc_String String;

typedef struct {
	F32 d[2][3];
} Mat2x3;

typedef struct {
	F32 d[2][2];
} Mat2x2;

typedef struct {
	F32 d[3][3];
} Mat3x3;

typedef Stuc_M4x4_F32 Mat4x4;

V2_I32 v2F32FloorAssign(V2_F32 *pA);

V4_F32 v4F32MultiplyMat4x4(V4_F32 a, const Mat4x4 *pB);
void v4F32MultiplyEqualMat4x4(V4_F32 *pA, const Mat4x4 *pB);
V3_F32 divideByW(const V4_F32 *pA);

V3_F32 v3F32MultiplyScalar(V3_F32 a, F32 b);
void v3F32DivideEqualScalar(V3_F32 *pA, F32 b);
V3_F32 v3F32DivideScalar(V3_F32 a, F32 b);
V3_F32 v3F32SubtractScalar(V3_F32 a, F32 b);
V3_F32 v3F32AddScalar(V3_F32 a, F32 b);
V3_F32 v3F32Add(V3_F32 a, V3_F32 b);
V3_F32 v3F32Subtract(V3_F32 a, V3_F32 b);
void v3F32AddEqual(V3_F32 *pA, V3_F32 b);
bool v3F32Equal(V3_F32 a, V3_F32 b);
bool v3F64Equal(V3_F64 a, V3_F64 b);
I32 v3F32GreaterThan(V3_F32 a, V3_F32 b);
I32 v3F32LessThan(V3_F32 a, V3_F32 b);
I32 v3F32AproxEqual(V3_F32 a, V3_F32 b);
V3_F32 v3F32Lerp(V3_F32 a, V3_F32 b, F32 alpha);
V3_F32 v3F32Cross(V3_F32 a, V3_F32 b);
V3_F32 v3F32UnitFromPoints(V3_F32 a, V3_F32 b);
V3_F32 v3F32MultiplyMat3x3(V3_F32 a, const Mat3x3 *pB);
void v3F32MultiplyEqualMat3x3(V3_F32 *pA, const Mat3x3 *pB);
V3_F32 v3F32Normalize(V3_F32 a);
F32 v3F32Dot(V3_F32 a, V3_F32 b);
I32 v3F32IsFinite(V3_F32);
bool v3F32DegenerateTri(V3_F32 a, V3_F32 b, V3_F32 c, F32 threshold);
F32 v3F32TriHeight(V3_F32 a, V3_F32 b, V3_F32 c);
F32 v3F32SquareLen(V3_F32);
F32 v3F32Len(V3_F32 a);
F32 v3F32TriArea(V3_F32 a, V3_F32 b, V3_F32 c);
V3_F32 cartesianToBarycentric(const V2_F32 *pTri, const V2_F32 *pPoint);
V3_F32 barycentricToCartesian(const V3_F32 *pTri, const V3_F32 *pPoint);

V2_F32 v2F32Abs(V2_F32 a);
V2_F32 v2F32Multiply(V2_F32 a, V2_F32 b);
void v2F32MultiplyEqual(V2_F32 *pA, V2_F32 b);
V2_F32 v2F32DivideScalar(V2_F32 a, F32 b);
void v2F32DivideEqualScalar(V2_F32 *pA, F32 b);
V2_F32 v2F32Subtract(V2_F32 a, V2_F32 b);
void v2F32SubtractEqual(V2_F32 *pA, V2_F32 b);
V2_F32 v2F32SubtractScalar(V2_F32 a, F32 b);
V2_F32 v2F32Add(V2_F32 a, V2_F32 b);
V2_F32 v2F32AddScalar(V2_F32 a, F32 b);
void v2F32AddEqual(V2_F32 *pA, V2_F32 b);
void v2F32AddEqualScalar(V2_F32 *pA, F32 b);
void v2F32MultiplyEqualScalar(V2_F32 *pA, F32 b);
V2_F32 v2F32MultiplyScalar(V2_F32 a, F32 b);
F32 v2F32Dot(V2_F32 a, V2_F32 b);
V2_F32 v2F32Cross(V2_F32 a);
V2_F32 v2F32ModScalar(V2_F32 a, F32 b);
void v2F32ModEqualScalar(V2_F32 *pA, F32 b);
F32 v2F32SquareLen(V2_F32 a);
F32 v2F32Len(V2_F32 a);
F32 v2F32TriArea(V2_F32 a, V2_F32 b, V2_F32 c);
F32 v2F32Determinate(V2_F32 a, V2_F32 b);
I32 v2F32GreaterThan(V2_F32 a, V2_F32 b);
I32 v2F32GreaterThanScalar(V2_F32 a, F32 b);
I32 v2F32GreaterThanEqualTo(V2_F32 a, V2_F32 b);
I32 v2F32LessThan(V2_F32 a, V2_F32 b);
I32 v2F32LessThanScalar(V2_F32 a, F32 b);
I32 v2F32LessThanEqualTo(V2_F32 a, V2_F32 b);
I32 v2F32NotEqual(V2_F32 a, V2_F32 b);
I32 v2F32Equal(V2_F32 a, V2_F32 b);
I32 v2F64Equal(V2_F64 a, V2_F64 b);
I32 v2F32AproxEqual(V2_F32 a, V2_F32 b);
I32 v2F32DegenerateTri(V2_F32 a, V2_F32 b, V2_F32 c, F32 threshold);
F32 v2F32TriHeight(V2_F32 a, V2_F32 b, V2_F32 c);
I32 v2F32IsFinite(V2_F32);

bool v2I8Equal(V2_I8 a, V2_I8 b);
bool v2I16Equal(V2_I16 a, V2_I16 b);
bool v2I32Equal(V2_I32 a, V2_I32 b);
bool v2I64Equal(V2_I64 a, V2_I64 b);
bool v3I8Equal(V3_I8 a, V3_I8 b);
bool v3I16Equal(V3_I16 a, V3_I16 b);
bool v3I32Equal(V3_I32 a, V3_I32 b);
bool v3I64Equal(V3_I64 a, V3_I64 b);
bool v4I8Equal(V4_I8 a, V4_I8 b);
bool v4I16Equal(V4_I16 a, V4_I16 b);
bool v4I32Equal(V4_I32 a, V4_I32 b);
bool v4I64Equal(V4_I64 a, V4_I64 b);

Mat2x2 mat2x2Adjugate(Mat2x2 a);
F32 mat2x2Determinate(Mat2x2 a);
void mat2x2MultiplyEqualScalar(Mat2x2 *pA, F32 b);
Mat2x2 mat2x2Invert(Mat2x2 a);
Mat2x3 mat2x2MultiplyMat2x3(Mat2x2 a, Mat2x3 b);
I32 mat2x2IsFinite(const Mat2x2 *pA);
Mat3x3 mat3x3FromV3_F32(V3_F32 a, V3_F32 b, V3_F32 c);
Mat3x3 Mat3x3FromMat4x4(const Mat4x4 *pA);
Mat3x3 mat3x3Invert(const Mat3x3 *pA);
I32 mat3x3IsFinite(const Mat3x3 *pA);

bool v4F32Equal(V4_F32 a, V4_F32 b);
bool v4F64Equal(V4_F64 a, V4_F64 b);

F32 customFloor(F32 a);

#define V4MULM4X4 ,4F32MultiplyMat4x4,
#define V4MULEQLM4X4 ,4F32MultiplyEqualMat4x4,

#define V3MULS ,3F32MultiplyScalar,
#define V3DIVEQLS ,3F32DivideEqualScalar,
#define V3DIVS ,3F32DivideScalar,
#define V3SUB ,3F32Subtract,
#define V3SUBS ,3F32SubtractScalar,
#define V3ADDS ,3F32AddScalar,
#define V3ADD ,3F32Add,
#define V3ADDEQL ,3F32AddEqual,
#define V3EQL ,3F32Equal,
#define V364EQL ,3F64Equal,
#define V3GREAT ,3F32GreaterThan,
#define V3LESS ,3F32LessThan,
#define V3CROSS ,3F32Cross,
#define V3APROXEQL ,3F32AproxEqual,
#define V3DOT ,3F32Dot,
#define V3MULM3X3 ,3F32MultiplyMat3x3,
#define V3MULEQLM3X3 ,3F32MultiplyEqualMat3x3,

#define V2MUL ,2F32Multiply,
#define V2MULEQL ,2F32MultiplyEqual,
#define V2DIVS ,2F32DivideScalar,
#define V2DIVSEQL ,2F32DivideEqualScalar,
#define V2SUB ,2F32Subtract,
#define V2SUBEQL ,2F32SubtractEqual,
#define V2SUBS ,2F32SubtractScalar,
#define V2ADD ,2F32Add,
#define V2ADDS ,2F32AddScalar,
#define V2ADDEQL ,2F32AddEqual,
#define V2ADDEQLS ,2F32AddEqualScalar,
#define V2MULSEQL ,2F32MultiplyEqualScalar,
#define V2MULS ,2F32MultiplyScalar,
#define V2DOT ,2F32Dot,
#define V2DET ,2F32Determinate,
#define V2MODS ,2F32ModScalar,
#define V2MODEQLS ,2F32ModEqualScalar,
#define V2GREAT ,2F32GreaterThan,
#define V2GREATS ,2F32GreaterThanScalar,
#define V2GREATEQL ,2F32GreaterThanEqualTo,
#define V2LESS ,2F32LessThan,
#define V2LESSS ,2F32LessThanScalar,
#define V2LESSEQL ,2F32LessThanEqualTo,
#define V2NOTEQL ,2F32NotEqual,
#define V2EQL ,2F32Equal,
#define V264EQL ,2F64Equal,
#define V2APROXEQL ,2F32AproxEqual,

#define V4EQL ,4F32Equal,
#define V464EQL ,4F64Equal,

#define V2I8EQL ,2I8Equal,
#define V2I16EQL ,2I16Equal,
#define V2IEQL ,2IEqual,
#define V2I64EQL ,2I64Equal,
#define V3I8EQL ,3I8Equal,
#define V3I16EQL ,3I16Equal,
#define V3IEQL ,3IEqual,
#define V3I64EQL ,3I64Equal,
#define V4I8EQL ,4I8Equal,
#define V4I16EQL ,4I16Equal,
#define V4IEQL ,4IEqual,
#define V4I64EQL ,4I64Equal,

#define INFIX(a,o,b) v##o((a),(b))
#define _(a) INFIX(a)
