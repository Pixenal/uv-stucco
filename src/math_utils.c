/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#include <math.h>

#include <math_utils.h>

V4_F32 v4F32MultiplyMat4x4(V4_F32 a, const Mat4x4 *pB) {
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

void v4F32MultiplyEqualMat4x4(V4_F32 *pA, const Mat4x4 *pB) {
	*pA = v4F32MultiplyMat4x4(*pA, pB);
}

V3_F32 divideByW(const V4_F32 *pA) {
	return _(*(V3_F32 *)pA V3DIVS pA->d[3]);
}

V3_F32 v3F32MultiplyScalar(V3_F32 a, F32 b) {
	V3_F32 c = {a.d[0] * b, a.d[1] * b, a.d[2] * b};
	return c;
}

V3_F64 v3F64MultiplyScalar(V3_F64 a, F64 b) {
	V3_F64 c = {a.d[0] * b, a.d[1] * b, a.d[2] * b};
	return c;
}

void v3F32DivideEqualScalar(V3_F32 *pA, F32 b) {
	pA->d[0] /= b;
	pA->d[1] /= b;
	pA->d[2] /= b;
}

V3_F32 v3F32DivideScalar(V3_F32 a, F32 b) {
	V3_F32 c = {a.d[0] / b, a.d[1] / b, a.d[2] / b};
	return c;
}

V3_F32 v3F32SubtractScalar(V3_F32 a, F32 b) {
	V3_F32 c = {a.d[0] - b, a.d[1] - b, a.d[2] - b};
	return c;
}

V3_F32 v3F32Subtract(V3_F32 a, V3_F32 b) {
	V3_F32 c = {a.d[0] - b.d[0], a.d[1] - b.d[1], a.d[2] - b.d[2]};
	return c;
}

V3_F64 v3F64Subtract(V3_F64 a, V3_F64 b) {
	V3_F64 c = {a.d[0] - b.d[0], a.d[1] - b.d[1], a.d[2] - b.d[2]};
	return c;
}

V3_F32 v3F32AddScalar(V3_F32 a, F32 b) {
	V3_F32 c = {a.d[0] + b, a.d[1] + b, a.d[2] + b};
	return c;
}

V3_F32 v3F32Add(V3_F32 a, V3_F32 b) {
	V3_F32 c = {a.d[0] + b.d[0], a.d[1] + b.d[1], a.d[2] + b.d[2]};
	return c;
}

V3_F64 v3F64Add(V3_F64 a, V3_F64 b) {
	V3_F64 c = {a.d[0] + b.d[0], a.d[1] + b.d[1], a.d[2] + b.d[2]};
	return c;
}

void v3F32AddEqual(V3_F32 *pA, V3_F32 b) {
	pA->d[0] += b.d[0];
	pA->d[1] += b.d[1];
	pA->d[2] += b.d[2];
}

bool v3F32Equal(V3_F32 a, V3_F32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}
bool v3F64Equal(V3_F64 a, V3_F64 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}

I32 v3F32GreaterThan(V3_F32 a, V3_F32 b) {
	return (a.d[0] > b.d[0]) && (a.d[1] > b.d[1]) && (a.d[2] > b.d[2]);
}

I32 v3F32LessThan(V3_F32 a, V3_F32 b) {
	return (a.d[0] < b.d[0]) && (a.d[1] < b.d[1]) && (a.d[2] < b.d[2]);
}

I32 v3F32AproxEqual(V3_F32 a, V3_F32 b) {
	V3_F32 bLow = _(b V3SUBS FLOAT_EQUAL_MARGIN);
	V3_F32 bHigh = _(b V3ADDS FLOAT_EQUAL_MARGIN);
	return _(a V3LESS bHigh) && _(a V3GREAT bLow);
}

V3_F32 v3F32Lerp(V3_F32 a, V3_F32 b, F32 alpha) {
	F32 alphaInverse = 1.0f - alpha;
	V3_F32 c = {0};
	c.d[0] = a.d[0] * alphaInverse + b.d[0] * alpha;
	c.d[1] = a.d[1] * alphaInverse + b.d[1] * alpha;
	c.d[2] = a.d[2] * alphaInverse + b.d[2] * alpha;
	return c;
}

V3_F32 v3F32UnitFromPoints(V3_F32 a, V3_F32 b) {
	V3_F32 dir = _(b V3SUB a); //direction
	F32 magnitude = sqrtf(dir.d[0] * dir.d[0] + dir.d[1] * dir.d[1]);
	return _(dir V3DIVS magnitude);
}

V3_F32 v3F32Cross(V3_F32 a, V3_F32 b) {
	V3_F32 c = {
		.d[0] = a.d[1] * b.d[2] - a.d[2] * b.d[1],
		.d[1] = a.d[2] * b.d[0] - a.d[0] * b.d[2],
		.d[2] = a.d[0] * b.d[1] - a.d[1] * b.d[0]
	};
	return c;
}

V3_F64 v3F64Cross(V3_F64 a, V3_F64 b) {
	V3_F64 c = {
		.d[0] = a.d[1] * b.d[2] - a.d[2] * b.d[1],
		.d[1] = a.d[2] * b.d[0] - a.d[0] * b.d[2],
		.d[2] = a.d[0] * b.d[1] - a.d[1] * b.d[0]
	};
	return c;
}

V3_F32 v3F32MultiplyMat3x3(V3_F32 a, const Mat3x3 *pB) {
	V3_F32 c = {0};
	c.d[0] = a.d[0] * pB->d[0][0] + a.d[1] * pB->d[1][0] + a.d[2] * pB->d[2][0];
	c.d[1] = a.d[0] * pB->d[0][1] + a.d[1] * pB->d[1][1] + a.d[2] * pB->d[2][1];
	c.d[2] = a.d[0] * pB->d[0][2] + a.d[1] * pB->d[1][2] + a.d[2] * pB->d[2][2];
	return c;
}

void v3F32MultiplyEqualMat3x3(V3_F32 *pA, const Mat3x3 *pB) {
	*pA = v3F32MultiplyMat3x3(*pA, pB);
}

V3_F32 v3F32Normalize(V3_F32 a) {
	F32 magnitude = sqrtf(a.d[0] * a.d[0] + a.d[1] * a.d[1] + a.d[2] * a.d[2]);
	return _(a V3DIVS magnitude);
}

I32 v3F32IsFinite(V3_F32 a) {
	return isfinite(a.d[0]) && isfinite(a.d[1]) && isfinite(a.d[2]);
}

F32 v3F32SquareLen(V3_F32 a) {
	return a.d[0] * a.d[0] + a.d[1] * a.d[1] + a.d[2] * a.d[2];
}

F32 v3F32Len(V3_F32 a) {
	return sqrtf(v3F32SquareLen(a));
}

F32 v3F32TriArea(V3_F32 a, V3_F32 b, V3_F32 c) {
	V3_F32 ba = _(a V3SUB b);
	V3_F32 bc = _(c V3SUB b);
	V3_F32 cross = _(ba V3CROSS bc);
	return v3F32Len(cross) / 2.0f;
}

bool v3F32DegenerateTri(V3_F32 a, V3_F32 b, V3_F32 c, F32 threshold) {
	V3_F32 ac = _(a V3SUB c);
	V3_F32 bc = _(b V3SUB c);
	V3_F32 cross = _(ac V3CROSS bc);
	F32 len = v3F32Len(cross);
	return len <= threshold && len >= -threshold;
}

F32 v3F32TriHeight(V3_F32 a, V3_F32 b, V3_F32 c) {
	V3_F32 ac = _(a V3SUB c);
	V3_F32 bc = _(b V3SUB c);
	V3_F32 cross = _(ac V3CROSS bc);
	return v3F32Len(cross);
}

F32 v3F32Dot(V3_F32 a, V3_F32 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1] + a.d[2] * b.d[2];
}

F64 v3F64Dot(V3_F64 a, V3_F64 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1] + a.d[2] * b.d[2];
}

V2_F32 v2F32Abs(V2_F32 a) {
	if (a.d[0] < .0f) {
		a.d[0] *= -1.0f;
	}
	if (a.d[1] < .0f) {
		a.d[1] *= -1.0f;
	}
	return a;
}

V2_F32 v2F32Multiply(V2_F32 a, V2_F32 b) {
	V2_F32 c = {0};
	c.d[0] = a.d[0] * b.d[0];
	c.d[1] = a.d[1] * b.d[1];
	return c;
}

void v2F32MultiplyEqual(V2_F32 *pA, V2_F32 b) {
	pA->d[0] *= b.d[0];
	pA->d[1] *= b.d[1];
}

V2_F32 v2F32DivideScalar(V2_F32 a, F32 b) {
	V2_F32 c = {0};
	c.d[0] = a.d[0] / b;
	c.d[1] = a.d[1] / b;
	return c;
}

void v2F32DivideEqualScalar(V2_F32 *pA, F32 b) {
	pA->d[0] /= b;
	pA->d[1] /= b;
}

V2_F32 v2F32Subtract(V2_F32 a, V2_F32 b) {
	V2_F32 c = {a.d[0] - b.d[0], a.d[1] - b.d[1]};
	return c;
}

V2_F64 v2F64Subtract(V2_F64 a, V2_F64 b) {
	V2_F64 c = {a.d[0] - b.d[0], a.d[1] - b.d[1]};
	return c;
}

void v2F32SubtractEqual(V2_F32 *pA, V2_F32 b) {
	pA->d[0] -= b.d[0];
	pA->d[1] -= b.d[1];
}

V2_F32 v2F32SubtractScalar(V2_F32 a, F32 b) {
	V2_F32 c = {a.d[0] - b, a.d[1] - b};
	return c;
}

V2_F32 v2F32Add(V2_F32 a, V2_F32 b) {
	V2_F32 c = {a.d[0] + b.d[0], a.d[1] + b.d[1]};
	return c;
}

V2_F32 v2F32AddScalar(V2_F32 a, F32 b) {
	V2_F32 c = {a.d[0] + b, a.d[1] + b};
	return c;
}

void v2F32AddEqual(V2_F32 *pA, V2_F32 b) {
	pA->d[0] += b.d[0];
	pA->d[1] += b.d[1];
}

void v2F32AddEqualScalar(V2_F32 *pA, F32 b) {
	pA->d[0] += b;
	pA->d[1] += b;
}

void v2F32MultiplyEqualScalar(V2_F32 *pA, F32 b) {
	pA->d[0] *= b;
	pA->d[1] *= b;
}

V2_F32 v2F32MultiplyScalar(V2_F32 a, F32 b) {
	V2_F32 c = {a.d[0] * b, a.d[1] * b};
	return c;
}

F32 v2F32Dot(V2_F32 a, V2_F32 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1];
}

F64 v2F64Dot(V2_F64 a, V2_F64 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1];
}

V2_F32 v2F32LineNormal(V2_F32 a) {
	V2_F32 b = {a.d[1], -a.d[0]};
	return b;
}

V2_F64 v2F64LineNormal(V2_F64 a) {
	V2_F64 b = {a.d[1], -a.d[0]};
	return b;
}

V2_F32 v2F32ModScalar(V2_F32 a, F32 b) {
	V2_F32 c = {fmodf(a.d[0], b), fmodf(a.d[1], b)};
	return c;
}

F32 v2F32SquareLen(V2_F32 a) {
	return a.d[0] * a.d[0] + a.d[1] * a.d[1];
}

F32 v2F32Len(V2_F32 a) {
	return sqrtf(v2F32SquareLen(a));
}

F32 v2F32TriArea(V2_F32 a, V2_F32 b, V2_F32 c) {
	V2_F32 ba = _(a V2SUB b);
	V2_F32 bc = _(c V2SUB b);
	V3_F32 ba3 = {ba.d[0], ba.d[1], .0f};
	V3_F32 bc3 = {bc.d[0], bc.d[1], .0f};
	V3_F32 cross = _(ba3 V3CROSS bc3);
	return fabsf(cross.d[2]) / 2.0f;
}

F32 v2F32Determinate(V2_F32 a, V2_F32 b) {
	return a.d[0] * b.d[1] - a.d[1] * b.d[0];
}

F64 v2F64Determinate(V2_F64 a, V2_F64 b) {
	return a.d[0] * b.d[1] - a.d[1] * b.d[0];
}

void v2F32ModEqualScalar(V2_F32 *a, F32 b) {
	a->d[0] = fmodf(a->d[0], b);
	a->d[1] = fmodf(a->d[1], b);
}

I32 v2F32GreaterThanEqualTo(V2_F32 a, V2_F32 b) {
	return (a.d[0] >= b.d[0]) && (a.d[1] >= b.d[1]);
}

I32 v2F32GreaterThan(V2_F32 a, V2_F32 b) {
	return (a.d[0] > b.d[0]) && (a.d[1] > b.d[1]);
}

I32 v2F32GreaterThanScalar(V2_F32 a, F32 b) {
	return (a.d[0] > b) && (a.d[1] > b);
}

I32 v2F32LessThan(V2_F32 a, V2_F32 b) {
	return (a.d[0] < b.d[0]) && (a.d[1] < b.d[1]);
}

I32 v2F32LessThanScalar(V2_F32 a, F32 b) {
	return (a.d[0] < b) && (a.d[1] < b);
}

I32 v2F32LessThanEqualTo(V2_F32 a, V2_F32 b) {
	return (a.d[0] <= b.d[0]) && (a.d[1] <= b.d[1]);
}

I32 v2F32NotEqual(V2_F32 a, V2_F32 b) {
	return a.d[0] != b.d[0] || a.d[1] != b.d[1];
}

//TODO replace return with bool in comparison funcs like this
I32 v2F32Equal(V2_F32 a, V2_F32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

I32 v2F64Equal(V2_F64 a, V2_F64 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

I32 v2F32AproxEqual(V2_F32 a, V2_F32 b) {
	V2_F32 bLow = _(b V2SUBS FLOAT_EQUAL_MARGIN);
	V2_F32 bHigh = _(b V2ADDS FLOAT_EQUAL_MARGIN);
	return _(a V2LESSEQL bHigh) && _(a V2GREATEQL bLow);
}

I32 v2F32AproxEqualThres(V2_F32 a, V2_F32 b, F32 threshold) {
	V2_F32 bLow = _(b V2SUBS threshold);
	V2_F32 bHigh = _(b V2ADDS threshold);
	return _(a V2LESSEQL bHigh) && _(a V2GREATEQL bLow);
}


I32 v2F32DegenerateTri(V2_F32 a, V2_F32 b, V2_F32 c, F32 threshold) {
	V2_F32 ac = _(a V2SUB c);
	V2_F32 bc = _(b V2SUB c);
	F32 cross = ac.d[0] * bc.d[1] - bc.d[0] * ac.d[1];
	return cross <= threshold && cross >= -threshold;
}

F32 v2F32TriHeight(V2_F32 a, V2_F32 b, V2_F32 c) {
	V2_F32 ac = _(a V2SUB c);
	V2_F32 bc = _(b V2SUB c);
	return ac.d[0] * bc.d[1] - bc.d[0] * ac.d[1];
}

I32 v2F32IsFinite(V2_F32 a) {
	return isfinite(a.d[0]) && isfinite(a.d[1]);
}

bool v2I8Equal(V2_I8 a, V2_I8 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

bool v2I16Equal(V2_I16 a, V2_I16 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

bool v2I32Equal(V2_I32 a, V2_I32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

bool v2I64Equal(V2_I64 a, V2_I64 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

bool v3I8Equal(V3_I8 a, V3_I8 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}

bool v3I16Equal(V3_I16 a, V3_I16 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}

bool v3I32Equal(V3_I32 a, V3_I32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}

bool v3I64Equal(V3_I64 a, V3_I64 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2];
}

bool v4I8Equal(V4_I8 a, V4_I8 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

bool v4I16Equal(V4_I16 a, V4_I16 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

bool v4I32Equal(V4_I32 a, V4_I32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

bool v4I64Equal(V4_I64 a, V4_I64 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

Mat2x2 mat2x2Adjugate(Mat2x2 a) {
	Mat2x2 c = {0};
	c.d[0][0] = a.d[1][1];
	c.d[0][1] = -a.d[0][1];
	c.d[1][0] = -a.d[1][0];
	c.d[1][1] = a.d[0][0];
	return c;
}

F32 mat2x2Determinate(Mat2x2 a) {
	return a.d[0][0] * a.d[1][1] - a.d[0][1] * a.d[1][0];
}

void mat2x2MultiplyEqualScalar(Mat2x2 *pA, F32 b) {
	pA->d[0][0] *= b;
	pA->d[0][1] *= b;
	pA->d[1][0] *= b;
	pA->d[1][1] *= b;
}

Mat2x2 mat2x2Invert(Mat2x2 a) {
	F32 determinate = mat2x2Determinate(a);
	Mat2x2 inverse = mat2x2Adjugate(a);
	mat2x2MultiplyEqualScalar(&inverse, 1.0f / determinate);
	return inverse;
}

I32 mat2x2IsFinite(const Mat2x2 *pA) {
	return isfinite(pA->d[0][0]) && isfinite(pA->d[0][1]) &&
	       isfinite(pA->d[1][0]) && isfinite(pA->d[1][1]);
}

static
F32 mat3x3Determinate(const Mat3x3 *pA) {
	F32 aDet = pA->d[1][1] * pA->d[2][2] - pA->d[2][1] * pA->d[1][2];
	F32 bDet = pA->d[0][1] * pA->d[2][2] - pA->d[2][1] * pA->d[0][2];
	F32 cDet = pA->d[0][1] * pA->d[1][2] - pA->d[1][1] * pA->d[0][2];
	return pA->d[0][0] * aDet - pA->d[1][0] * bDet + pA->d[2][0] * cDet;
}

static
Mat3x3 mat3x3Adjugate(const Mat3x3 *pA) {
	Mat3x3 c = {0};
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
void mat3x3MultiplyEqualScalar(Mat3x3 *pA, F32 b) {
	for (I32 i = 0; i < 3; ++i) {
		for (I32 j = 0; j < 3; ++j) {
			pA->d[i][j] *= b;
		}
	}
}

Mat3x3 mat3x3FromV3_F32(V3_F32 a, V3_F32 b, V3_F32 c) {
	Mat3x3 mat = {0};
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

Mat3x3 Mat3x3FromMat4x4(const Mat4x4 *pA) {
	Mat3x3 b = {
		pA->d[0][0], pA->d[0][1], pA->d[0][2],
		pA->d[1][0], pA->d[1][1], pA->d[1][2],
		pA->d[2][0], pA->d[2][1], pA->d[2][2]
	};
	return b;
}

Mat3x3 mat3x3Invert(const Mat3x3 *pA) {
	F32 determinate = mat3x3Determinate(pA);
	Mat3x3 inverse = mat3x3Adjugate(pA);
	mat3x3MultiplyEqualScalar(&inverse, 1.0f / determinate);
	return inverse;
}

I32 mat3x3IsFinite(const Mat3x3 *pA) {
	I32 isFinite = 0;
	isFinite += isfinite(pA->d[0][0]) && isfinite(pA->d[0][1]) && isfinite(pA->d[0][2]);
	isFinite += isfinite(pA->d[1][0]) && isfinite(pA->d[1][1]) && isfinite(pA->d[1][2]);
	isFinite += isfinite(pA->d[2][0]) && isfinite(pA->d[2][1]) && isfinite(pA->d[2][2]);
	return isFinite;
}

Mat2x3 mat2x2MultiplyMat2x3(Mat2x2 a, Mat2x3 b) {
	Mat2x3 c = {0};
	c.d[0][0] = a.d[0][0] * b.d[0][0] + a.d[0][1] * b.d[1][0];
	c.d[0][1] = a.d[0][0] * b.d[0][1] + a.d[0][1] * b.d[1][1];
	c.d[0][2] = a.d[0][0] * b.d[0][2] + a.d[0][1] * b.d[1][2];
	c.d[1][0] = a.d[1][0] * b.d[0][0] + a.d[1][1] * b.d[1][0];
	c.d[1][1] = a.d[1][0] * b.d[0][1] + a.d[1][1] * b.d[1][1];
	c.d[1][2] = a.d[1][0] * b.d[0][2] + a.d[1][1] * b.d[1][2];
	return c;
}

F32 customFloor(F32 a) {
	I32 aTrunc = (I32)a;
	aTrunc -= ((F32)aTrunc != a) && (a < .0f);
	return (F32)aTrunc;
}

V2_I32 v2F32FloorAssign(V2_F32 *pA) {
	V2_I32 c = {0};
	pA->d[0] = customFloor((F32)pA->d[0]);
	pA->d[1] = customFloor((F32)pA->d[1]);
	c.d[0] = (I32)pA->d[0];
	c.d[1] = (I32)pA->d[1];
	return c;
}

bool v4F32Equal(V4_F32 a, V4_F32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

bool v4F64Equal(V4_F64 a, V4_F64 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1] && a.d[2] == b.d[2] && a.d[3] == b.d[3];
}

V3_F32 stucBarycentricToCartesian(const V3_F32 *pTri, V3_F32 point) {
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

V3_F32 stucCartesianToBarycentric(const V2_F32 *pTri32, const V2_F32 *pPoint32) {
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