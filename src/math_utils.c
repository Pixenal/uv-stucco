/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <math.h>

#include <math_utils.h>

typedef PixtyV2_I8 V2_I8;
typedef PixtyV2_I16 V2_I16;
typedef PixtyV2_I32 V2_I32;
typedef PixtyV2_I64 V2_I64;
typedef PixtyV2_F32 V2_F32;
typedef PixtyV2_F64 V2_F64;
typedef PixtyV3_I8 V3_I8;
typedef PixtyV3_I16 V3_I16;
typedef PixtyV3_I32 V3_I32;
typedef PixtyV3_I64 V3_I64;
typedef PixtyV3_F32 V3_F32;
typedef PixtyV3_F64 V3_F64;
typedef PixtyV4_I8 V4_I8;
typedef PixtyV4_I16 V4_I16;
typedef PixtyV4_I32 V4_I32;
typedef PixtyV4_I64 V4_I64;
typedef PixtyV4_F32 V4_F32;
typedef PixtyV4_F64 V4_F64;
typedef PixtyM4x4 M4x4;
typedef PixtyM3x3 M3x3;
typedef PixtyM2x2 M2x2;
typedef PixtyM2x3 M2x3;

typedef int8_t I8;
typedef int16_t I16;
typedef int32_t I32;
typedef int64_t I64;

typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef float F32;
typedef double F64;


V4_F32 pixmV4F32MultiplyM4x4(V4_F32 a, const M4x4 *pB) {
	V4_F32 c = {0};
	c.d[0] = 
		a.d[0] * pB->d[0][0] +
		a.d[1] * pB->d[1][0] +
		a.d[2] * pB->d[2][0] +
		a.d[3] * pB->d[3][0];
	c.d[1] =
		a.d[0] * pB->d[0][1] +
		a.d[1] * pB->d[1][1] +
		a.d[2] * pB->d[2][1] +
		a.d[3] * pB->d[3][1];
	c.d[2] =
		a.d[0] * pB->d[0][2] +
		a.d[1] * pB->d[1][2] +
		a.d[2] * pB->d[2][2] +
		a.d[3] * pB->d[3][2];
	return c;
}

void pixmV4F32MultiplyEqualM4x4(V4_F32 *pA, const M4x4 *pB) {
	*pA = pixmV4F32MultiplyM4x4(*pA, pB);
}

V3_F32 pixmDivideByW(const V4_F32 *pA) {
	return _(*(V3_F32 *)pA V3DIVS pA->d[3]);
}

V3_F32 pixmV3F32MultiplyScalar(V3_F32 a, F32 b) {
	return (V3_F32) {a.d[0] * b, a.d[1] * b, a.d[2] * b};
}

V3_F64 pixmV3F64MultiplyScalar(V3_F64 a, F64 b) {
	return (V3_F64) {a.d[0] * b, a.d[1] * b, a.d[2] * b};
}

void pixmV3F32DivideEqualScalar(V3_F32 *pA, F32 b) {
	pA->d[0] /= b;
	pA->d[1] /= b;
	pA->d[2] /= b;
}

V3_F32 pixmV3F32DivideScalar(V3_F32 a, F32 b) {
	return (V3_F32) {a.d[0] / b, a.d[1] / b, a.d[2] / b};
}

V3_F32 pixmV3F32SubtractScalar(V3_F32 a, F32 b) {
	return (V3_F32) {a.d[0] - b, a.d[1] - b, a.d[2] - b};
}

V3_F32 pixmV3F32Subtract(V3_F32 a, V3_F32 b) {
	return (V3_F32) {a.d[0] - b.d[0], a.d[1] - b.d[1], a.d[2] - b.d[2]};
}

V3_F64 pixmV3F64Subtract(V3_F64 a, V3_F64 b) {
	return (V3_F64) {a.d[0] - b.d[0], a.d[1] - b.d[1], a.d[2] - b.d[2]};
}

V3_F32 pixmV3F32AddScalar(V3_F32 a, F32 b) {
	return (V3_F32) {a.d[0] + b, a.d[1] + b, a.d[2] + b};
}

V3_F32 pixmV3F32Add(V3_F32 a, V3_F32 b) {
	return (V3_F32) {a.d[0] + b.d[0], a.d[1] + b.d[1], a.d[2] + b.d[2]};
}

V3_F64 pixmV3F64Add(V3_F64 a, V3_F64 b) {
	return (V3_F64) {a.d[0] + b.d[0], a.d[1] + b.d[1], a.d[2] + b.d[2]};
}

void pixmV3F32AddEqual(V3_F32 *pA, V3_F32 b) {
	pA->d[0] += b.d[0];
	pA->d[1] += b.d[1];
	pA->d[2] += b.d[2];
}

bool pixmV3F32Equal(V3_F32 a, V3_F32 b) {
	return
		_(a.d[0] F32_EQL b.d[0]) && _(a.d[1] F32_EQL b.d[1]) && _(a.d[2] F32_EQL b.d[2]);
}
bool pixmV3F64Equal(V3_F64 a, V3_F64 b) {
	return
		_(a.d[0] F64_EQL b.d[0]) && _(a.d[1] F64_EQL b.d[1]) && _(a.d[2] F64_EQL b.d[2]);
}

bool pixmV3F32GreaterThan(V3_F32 a, V3_F32 b) {
	return
		_(a.d[0] F32_GREAT b.d[0]) &&
		_(a.d[1] F32_GREAT b.d[1]) &&
		_(a.d[2] F32_GREAT b.d[2]);
}

bool pixmV3F32LessThan(V3_F32 a, V3_F32 b) {
	return
		_(a.d[0] F32_LESS b.d[0]) &&
		_(a.d[1] F32_LESS b.d[1]) &&
		_(a.d[2] F32_LESS b.d[2]);
}

V3_F32 pixmV3F32Lerp(V3_F32 a, V3_F32 b, F32 alpha) {
	F32 alphaInverse = 1.0f - alpha;
	return (V3_F32) {.d = {
		a.d[0] * alphaInverse + b.d[0] * alpha,
		a.d[1] * alphaInverse + b.d[1] * alpha,
		a.d[2] * alphaInverse + b.d[2] * alpha
	}};
}

V3_F32 pixmV3F32UnitFromPoints(V3_F32 a, V3_F32 b) {
	V3_F32 dir = _(b V3SUB a); //direction
	F32 magnitude = sqrtf(dir.d[0] * dir.d[0] + dir.d[1] * dir.d[1]);
	return _(dir V3DIVS magnitude);
}

V3_F32 pixmV3F32Cross(V3_F32 a, V3_F32 b) {
	return (V3_F32) {
		.d[0] = a.d[1] * b.d[2] - a.d[2] * b.d[1],
		.d[1] = a.d[2] * b.d[0] - a.d[0] * b.d[2],
		.d[2] = a.d[0] * b.d[1] - a.d[1] * b.d[0]
	};
}

V3_F64 pixmV3F64Cross(V3_F64 a, V3_F64 b) {
	return (V3_F64) {
		.d[0] = a.d[1] * b.d[2] - a.d[2] * b.d[1],
		.d[1] = a.d[2] * b.d[0] - a.d[0] * b.d[2],
		.d[2] = a.d[0] * b.d[1] - a.d[1] * b.d[0]
	};
}

V3_F32 pixmV3F32MultiplyM3x3(V3_F32 a, const M3x3 *pB) {
	return (V3_F32) {.d = {
		a.d[0] * pB->d[0][0] + a.d[1] * pB->d[1][0] + a.d[2] * pB->d[2][0],
		a.d[0] * pB->d[0][1] + a.d[1] * pB->d[1][1] + a.d[2] * pB->d[2][1],
		a.d[0] * pB->d[0][2] + a.d[1] * pB->d[1][2] + a.d[2] * pB->d[2][2]
	}};
}

void pixmV3F32MultiplyEqualM3x3(V3_F32 *pA, const M3x3 *pB) {
	*pA = pixmV3F32MultiplyM3x3(*pA, pB);
}

V3_F32 pixmV3F32Normalize(V3_F32 a) {
	F32 magnitude = sqrtf(a.d[0] * a.d[0] + a.d[1] * a.d[1] + a.d[2] * a.d[2]);
	return _(a V3DIVS magnitude);
}

bool pixmV3F32IsFinite(V3_F32 a) {
	return isfinite(a.d[0]) && isfinite(a.d[1]) && isfinite(a.d[2]);
}

F32 pixmV3F32SquareLen(V3_F32 a) {
	return a.d[0] * a.d[0] + a.d[1] * a.d[1] + a.d[2] * a.d[2];
}

F32 pixmV3F32Len(V3_F32 a) {
	return sqrtf(pixmV3F32SquareLen(a));
}

F32 pixmV3F32TriArea(V3_F32 a, V3_F32 b, V3_F32 c) {
	V3_F32 ba = _(a V3SUB b);
	V3_F32 bc = _(c V3SUB b);
	V3_F32 cross = _(ba V3CROSS bc);
	return pixmV3F32Len(cross) / 2.0f;
}

bool pixmV3F32DegenerateTri(V3_F32 a, V3_F32 b, V3_F32 c, F32 threshold) {
	V3_F32 ac = _(a V3SUB c);
	V3_F32 bc = _(b V3SUB c);
	V3_F32 cross = _(ac V3CROSS bc);
	F32 len = pixmV3F32Len(cross);
	return _(len F32_LESSEQL threshold) && _(len F32_GREATEQL -threshold);
}

F32 pixmV3F32TriHeight(V3_F32 a, V3_F32 b, V3_F32 c) {
	V3_F32 ac = _(a V3SUB c);
	V3_F32 bc = _(b V3SUB c);
	V3_F32 cross = _(ac V3CROSS bc);
	return pixmV3F32Len(cross);
}

F32 pixmV3F32Dot(V3_F32 a, V3_F32 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1] + a.d[2] * b.d[2];
}

F64 pixmV3F64Dot(V3_F64 a, V3_F64 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1] + a.d[2] * b.d[2];
}

V2_F32 pixmV2F32Abs(V2_F32 a) {
	return (V2_F32) {fabsf(a.d[0]), fabsf(a.d[1])};
}

V2_F32 pixmV2F32Multiply(V2_F32 a, V2_F32 b) {
	return (V2_F32) {.d = {a.d[0] * b.d[0], a.d[1] * b.d[1]}};
}

void pixmV2F32MultiplyEqual(V2_F32 *pA, V2_F32 b) {
	pA->d[0] *= b.d[0];
	pA->d[1] *= b.d[1];
}

V2_F32 pixmV2F32DivideScalar(V2_F32 a, F32 b) {
	return (V2_F32) {.d = {a.d[0] / b, a.d[1] / b}};
}

void pixmV2F32DivideEqualScalar(V2_F32 *pA, F32 b) {
	pA->d[0] /= b;
	pA->d[1] /= b;
}

V2_F32 pixmV2F32Subtract(V2_F32 a, V2_F32 b) {
	return (V2_F32) {a.d[0] - b.d[0], a.d[1] - b.d[1]};
}

V2_F64 pixmV2F64Subtract(V2_F64 a, V2_F64 b) {
	return (V2_F64) {a.d[0] - b.d[0], a.d[1] - b.d[1]};
}

void pixmV2F32SubtractEqual(V2_F32 *pA, V2_F32 b) {
	pA->d[0] -= b.d[0];
	pA->d[1] -= b.d[1];
}

V2_F32 pixmV2F32SubtractScalar(V2_F32 a, F32 b) {
	return (V2_F32) {a.d[0] - b, a.d[1] - b};
}

V2_F32 pixmV2F32Add(V2_F32 a, V2_F32 b) {
	return (V2_F32) {a.d[0] + b.d[0], a.d[1] + b.d[1]};
}

V2_F32 pixmV2F32AddScalar(V2_F32 a, F32 b) {
	return (V2_F32) {a.d[0] + b, a.d[1] + b};
}

void pixmV2F32AddEqual(V2_F32 *pA, V2_F32 b) {
	pA->d[0] += b.d[0];
	pA->d[1] += b.d[1];
}

void pixmV2F32AddEqualScalar(V2_F32 *pA, F32 b) {
	pA->d[0] += b;
	pA->d[1] += b;
}

void pixmV2F32MultiplyEqualScalar(V2_F32 *pA, F32 b) {
	pA->d[0] *= b;
	pA->d[1] *= b;
}

V2_F32 pixmV2F32MultiplyScalar(V2_F32 a, F32 b) {
	return (V2_F32) {a.d[0] * b, a.d[1] * b};
}

F32 pixmV2F32Dot(V2_F32 a, V2_F32 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1];
}

F64 pixmV2F64Dot(V2_F64 a, V2_F64 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1];
}

F32 pixmV2F32Cross(V2_F32 a, V2_F32 b) {
	return a.d[0] * b.d[1] - a.d[1] * b.d[0];
}

F64 pixmV2F64Cross(V2_F64 a, V2_F64 b) {
	return a.d[0] * b.d[1] - a.d[1] * b.d[0];
}

V2_F32 pixmV2F32LineNormal(V2_F32 a) {
	V2_F32 b = {a.d[1], -a.d[0]};
	return b;
}

V2_F64 pixmV2F64LineNormal(V2_F64 a) {
	V2_F64 b = {a.d[1], -a.d[0]};
	return b;
}

V2_F32 pixmV2F32ModScalar(V2_F32 a, F32 b) {
	return (V2_F32) {fmodf(a.d[0], b), fmodf(a.d[1], b)};
}

F32 pixmV2F32SquareLen(V2_F32 a) {
	return a.d[0] * a.d[0] + a.d[1] * a.d[1];
}

F32 pixmV2F32Len(V2_F32 a) {
	return sqrtf(pixmV2F32SquareLen(a));
}

F32 pixmV2F32TriArea(V2_F32 a, V2_F32 b, V2_F32 c) {
	V2_F32 ba = _(a V2SUB b);
	V2_F32 bc = _(c V2SUB b);
	V3_F32 ba3 = {ba.d[0], ba.d[1], .0f};
	V3_F32 bc3 = {bc.d[0], bc.d[1], .0f};
	V3_F32 cross = _(ba3 V3CROSS bc3);
	return fabsf(cross.d[2]) / 2.0f;
}

F32 pixmV2F32Determinate(V2_F32 a, V2_F32 b) {
	return a.d[0] * b.d[1] - a.d[1] * b.d[0];
}

F64 pixmV2F64Determinate(V2_F64 a, V2_F64 b) {
	return a.d[0] * b.d[1] - a.d[1] * b.d[0];
}

void pixmV2F32ModEqualScalar(V2_F32 *a, F32 b) {
	a->d[0] = fmodf(a->d[0], b);
	a->d[1] = fmodf(a->d[1], b);
}

bool pixmV2F32GreaterThanEqualTo(V2_F32 a, V2_F32 b) {
	return _(a.d[0] F32_GREATEQL b.d[0]) && _(a.d[1] F32_GREATEQL b.d[1]);
}

bool pixmV2F32GreaterThan(V2_F32 a, V2_F32 b) {
	return _(a.d[0] F32_GREAT b.d[0]) && _(a.d[1] F32_GREAT b.d[1]);
}

bool pixmV2F32GreaterThanScalar(V2_F32 a, F32 b) {
	return _(a.d[0] F32_GREAT b) && _(a.d[1] F32_GREAT b);
}

bool pixmV2F32LessThan(V2_F32 a, V2_F32 b) {
	return _(a.d[0] F32_LESS b.d[0]) && _(a.d[1] F32_LESS b.d[1]);
}

bool pixmV2F32LessThanScalar(V2_F32 a, F32 b) {
	return _(a.d[0] F32_LESS b) && _(a.d[1] F32_LESS b);
}

bool pixmV2F32LessThanEqualTo(V2_F32 a, V2_F32 b) {
	return _(a.d[0] F32_LESSEQL b.d[0]) && _(a.d[1] F32_LESSEQL b.d[1]);
}

bool pixmV2F32NotEqual(V2_F32 a, V2_F32 b) {
	return _(a.d[0] F32_NOTEQL b.d[0]) || _(a.d[1] F32_NOTEQL b.d[1]);
}

//TODO replace return with bool in comparison funcs like this
bool pixmV2F32Equal(V2_F32 a, V2_F32 b) {
	return _(a.d[0] F32_EQL b.d[0]) && _(a.d[1] F32_EQL b.d[1]);
}

bool pixmV2F64Equal(V2_F64 a, V2_F64 b) {
	return _(a.d[0] F64_EQL b.d[0]) && _(a.d[1] F64_EQL b.d[1]);
}

bool pixmV2F32AproxEqual(V2_F32 a, V2_F32 b) {
	V2_F32 bLow = _(b V2SUBS PIX_MATH_EPSILON);
	V2_F32 bHigh = _(b V2ADDS PIX_MATH_EPSILON);
	return _(a V2LESSEQL bHigh) && _(a V2GREATEQL bLow);
}

bool pixmV2F32AproxEqualThres(V2_F32 a, V2_F32 b, F32 threshold) {
	V2_F32 bLow = _(b V2SUBS threshold);
	V2_F32 bHigh = _(b V2ADDS threshold);
	return _(a V2LESSEQL bHigh) && _(a V2GREATEQL bLow);
}


bool pixmV2F32DegenerateTri(V2_F32 a, V2_F32 b, V2_F32 c, F32 threshold) {
	V2_F32 ac = _(a V2SUB c);
	V2_F32 bc = _(b V2SUB c);
	F32 cross = ac.d[0] * bc.d[1] - bc.d[0] * ac.d[1];
	return _(cross F32_LESSEQL threshold) && _(cross F32_GREATEQL -threshold);
}

F32 pixmV2F32TriHeight(V2_F32 a, V2_F32 b, V2_F32 c) {
	V2_F32 ac = _(a V2SUB c);
	V2_F32 bc = _(b V2SUB c);
	return ac.d[0] * bc.d[1] - bc.d[0] * ac.d[1];
}

bool pixmV2F32IsFinite(V2_F32 a) {
	return isfinite(a.d[0]) && isfinite(a.d[1]);
}

bool pixmV2I8Equal(V2_I8 a, V2_I8 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

bool pixmV2I16Equal(V2_I16 a, V2_I16 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

bool pixmV2I32Equal(V2_I32 a, V2_I32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

bool pixmV2I64Equal(V2_I64 a, V2_I64 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

bool pixmV3I8Equal(V3_I8 a, V3_I8 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}

bool pixmV3I16Equal(V3_I16 a, V3_I16 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}

bool pixmV3I32Equal(V3_I32 a, V3_I32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}

bool pixmV3I64Equal(V3_I64 a, V3_I64 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}

bool pixmV4I8Equal(V4_I8 a, V4_I8 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

bool pixmV4I16Equal(V4_I16 a, V4_I16 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

bool pixmV4I32Equal(V4_I32 a, V4_I32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

bool pixmV4I64Equal(V4_I64 a, V4_I64 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

M2x2 pixmM2x2Adjugate(M2x2 a) {
	M2x2 c = {0};
	c.d[0][0] = a.d[1][1];
	c.d[0][1] = -a.d[0][1];
	c.d[1][0] = -a.d[1][0];
	c.d[1][1] = a.d[0][0];
	return c;
}

F32 pixmM2x2Determinate(M2x2 a) {
	return a.d[0][0] * a.d[1][1] - a.d[0][1] * a.d[1][0];
}

void pixmM2x2MultiplyEqualScalar(M2x2 *pA, F32 b) {
	pA->d[0][0] *= b;
	pA->d[0][1] *= b;
	pA->d[1][0] *= b;
	pA->d[1][1] *= b;
}

M2x2 pixmM2x2Invert(M2x2 a) {
	F32 determinate = pixmM2x2Determinate(a);
	M2x2 inverse = pixmM2x2Adjugate(a);
	pixmM2x2MultiplyEqualScalar(&inverse, 1.0f / determinate);
	return inverse;
}

bool pixmM2x2IsFinite(const M2x2 *pA) {
	return isfinite(pA->d[0][0]) && isfinite(pA->d[0][1]) &&
	       isfinite(pA->d[1][0]) && isfinite(pA->d[1][1]);
}

static
F32 m3x3Determinate(const M3x3 *pA) {
	F32 aDet = pA->d[1][1] * pA->d[2][2] - pA->d[2][1] * pA->d[1][2];
	F32 bDet = pA->d[0][1] * pA->d[2][2] - pA->d[2][1] * pA->d[0][2];
	F32 cDet = pA->d[0][1] * pA->d[1][2] - pA->d[1][1] * pA->d[0][2];
	return pA->d[0][0] * aDet - pA->d[1][0] * bDet + pA->d[2][0] * cDet;
}

static
M3x3 m3x3Adjugate(const M3x3 *pA) {
	M3x3 c = {0};
	c.d[0][0] = pA->d[1][1] * pA->d[2][2] - pA->d[2][1] * pA->d[1][2];
	c.d[0][1] = pA->d[0][1] * pA->d[2][2] - pA->d[2][1] * pA->d[0][2];
	c.d[0][2] = pA->d[0][1] * pA->d[1][2] - pA->d[1][1] * pA->d[0][2];
	c.d[1][0] = pA->d[1][0] * pA->d[2][2] - pA->d[2][0] * pA->d[1][2];
	c.d[1][1] = pA->d[0][0] * pA->d[2][2] - pA->d[2][0] * pA->d[0][2];
	c.d[1][2] = pA->d[0][0] * pA->d[1][2] - pA->d[1][0] * pA->d[0][2];
	c.d[2][0] = pA->d[1][0] * pA->d[2][1] - pA->d[2][0] * pA->d[1][1];
	c.d[2][1] = pA->d[0][0] * pA->d[2][1] - pA->d[2][0] * pA->d[0][1];
	c.d[2][2] = pA->d[0][0] * pA->d[1][1] - pA->d[1][0] * pA->d[0][1];
	c.d[1][0] *= -1.0f;
	c.d[0][1] *= -1.0f;
	c.d[2][1] *= -1.0f;
	c.d[1][2] *= -1.0f;
	return c;
}

static
void m3x3MultiplyEqualScalar(M3x3 *pA, F32 b) {
	for (I32 i = 0; i < 3; ++i) {
		for (I32 j = 0; j < 3; ++j) {
			pA->d[i][j] *= b;
		}
	}
}

M3x3 m3x3FromV3_F32(V3_F32 a, V3_F32 b, V3_F32 c) {
	M3x3 mat = {0};
	mat.d[0][0] = a.d[0];
	mat.d[0][1] = a.d[1];
	mat.d[0][2] = a.d[2];
	mat.d[1][0] = b.d[0];
	mat.d[1][1] = b.d[1];
	mat.d[1][2] = b.d[2];
	mat.d[2][0] = c.d[0];
	mat.d[2][1] = c.d[1];
	mat.d[2][2] = c.d[2];
	return mat;
}

M3x3 pixmM3x3FromM4x4(const M4x4 *pA) {
	M3x3 b = {
		pA->d[0][0], pA->d[0][1], pA->d[0][2],
		pA->d[1][0], pA->d[1][1], pA->d[1][2],
		pA->d[2][0], pA->d[2][1], pA->d[2][2]
	};
	return b;
}

M3x3 pixmM3x3Invert(const M3x3 *pA) {
	F32 determinate = m3x3Determinate(pA);
	M3x3 inverse = m3x3Adjugate(pA);
	m3x3MultiplyEqualScalar(&inverse, 1.0f / determinate);
	return inverse;
}

bool pixmM3x3IsFinite(const M3x3 *pA) {
	return 
		isfinite(pA->d[0][0]) && isfinite(pA->d[0][1]) && isfinite(pA->d[0][2]) &&
		isfinite(pA->d[1][0]) && isfinite(pA->d[1][1]) && isfinite(pA->d[1][2]) &&
		isfinite(pA->d[2][0]) && isfinite(pA->d[2][1]) && isfinite(pA->d[2][2]);
}

M2x3 pixmM2x2MultiplyM2x3(M2x2 a, M2x3 b) {
	M2x3 c = {0};
	c.d[0][0] = a.d[0][0] * b.d[0][0] + a.d[0][1] * b.d[1][0];
	c.d[0][1] = a.d[0][0] * b.d[0][1] + a.d[0][1] * b.d[1][1];
	c.d[0][2] = a.d[0][0] * b.d[0][2] + a.d[0][1] * b.d[1][2];
	c.d[1][0] = a.d[1][0] * b.d[0][0] + a.d[1][1] * b.d[1][0];
	c.d[1][1] = a.d[1][0] * b.d[0][1] + a.d[1][1] * b.d[1][1];
	c.d[1][2] = a.d[1][0] * b.d[0][2] + a.d[1][1] * b.d[1][2];
	return c;
}

F32 pixmFloor(F32 a) {
	I32 aTrunc = (I32)a;
	aTrunc -= _((F32)aTrunc F32_NOTEQL a) && _(a F32_LESS .0f);
	return (F32)aTrunc;
}

V2_I32 pixmV2F32FloorAssign(V2_F32 *pA) {
	V2_I32 c = {0};
	pA->d[0] = pixmFloor((F32)pA->d[0]);
	pA->d[1] = pixmFloor((F32)pA->d[1]);
	c.d[0] = (I32)pA->d[0];
	c.d[1] = (I32)pA->d[1];
	return c;
}

bool pixmV4F32Equal(V4_F32 a, V4_F32 b) {
	return
		_(a.d[0] F32_EQL b.d[0]) &&
		_(a.d[1] F32_EQL b.d[1]) &&
		_(a.d[2] F32_EQL b.d[2]) &&
		_(a.d[3] F32_EQL b.d[3]);
}

bool pixmV4F64Equal(V4_F64 a, V4_F64 b) {
	return
		_(a.d[0] F64_EQL b.d[0]) &&
		_(a.d[1] F64_EQL b.d[1]) &&
		_(a.d[2] F64_EQL b.d[2]) &&
		_(a.d[3] F64_EQL b.d[3]);
}

V3_F32 pixmBarycentricToCartesian(const V3_F32 *pTri, V3_F32 point) {
	V3_F32 pointCartesian = {0};
	pointCartesian.d[0] =
		(point.d[0] * pTri[0].d[0]) +
		(point.d[1] * pTri[1].d[0]) +
		(point.d[2] * pTri[2].d[0]);
	pointCartesian.d[1] =
		(point.d[0] * pTri[0].d[1]) +
		(point.d[1] * pTri[1].d[1]) +
		(point.d[2] * pTri[2].d[1]);
	pointCartesian.d[2] =
		(point.d[0] * pTri[0].d[2]) +
		(point.d[1] * pTri[1].d[2]) +
		(point.d[2] * pTri[2].d[2]);
	return pointCartesian;
}

V3_F32 pixmCartesianToBarycentric(const V2_F32 *pTri32, const V2_F32 *pPoint32) {
	V3_F32 pointBc = {0};
	F64 derta = .0;
	F64 dertau = .0;
	F64 dertav = .0;

	//Convert to F64
	V3_F64 pPoint = {.d = {pPoint32->d[0], pPoint32->d[1]}};
	V3_F64 pTri[3] = {0};
	for (I32 i = 0; i < 3; ++i) {
		pTri[i].d[0] = pTri32[i].d[0];
		pTri[i].d[1] = pTri32[i].d[1];
	}

	//Perform cramers rule
	derta = (pTri[0].d[0] * pTri[1].d[1]) - (pTri[0].d[0] * pTri[2].d[1]) -
	        (pTri[1].d[0] * pTri[0].d[1]) + (pTri[1].d[0] * pTri[2].d[1]) +
	        (pTri[2].d[0] * pTri[0].d[1]) - (pTri[2].d[0] * pTri[1].d[1]);
	//Get determinate of Au
	dertau = (pPoint.d[0] * pTri[1].d[1]) - (pPoint.d[0] * pTri[2].d[1]) -
	         (pTri[1].d[0] * pPoint.d[1]) + (pTri[1].d[0] * pTri[2].d[1]) +
	         (pTri[2].d[0] * pPoint.d[1]) - (pTri[2].d[0] * pTri[1].d[1]);
	//Get determinate of Av
	dertav = (pTri[0].d[0] * pPoint.d[1]) - (pTri[0].d[0] * pTri[2].d[1]) -
	         (pPoint.d[0] * pTri[0].d[1]) + (pPoint.d[0] * pTri[2].d[1]) +
	         (pTri[2].d[0] * pTri[0].d[1]) - (pTri[2].d[0] * pPoint.d[1]);

	//u = dert(Au) / dert(A)
	pointBc.d[0] = (F32)(dertau / derta);
	//u = dert(Av) / dert(A)
	pointBc.d[1] = (F32)(dertav / derta);
	//w can be derived from u and v
	pointBc.d[2] = 1.0f - pointBc.d[0] - pointBc.d[1];

	return pointBc;
}