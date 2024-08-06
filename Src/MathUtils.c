#include <math.h>

#include <MathUtils.h>

V4_F32 v4MultiplyMat4x4(V4_F32 a, Mat4x4 *pB) {
	V4_F32 c;
	c.d[0] = a.d[0] * pB->d[0][0] + a.d[1] * pB->d[1][0] + a.d[2] * pB->d[2][0] + a.d[3] * pB->d[3][0];
	c.d[1] = a.d[0] * pB->d[0][1] + a.d[1] * pB->d[1][1] + a.d[2] * pB->d[2][1] + a.d[3] * pB->d[3][1];
	c.d[2] = a.d[0] * pB->d[0][2] + a.d[1] * pB->d[1][2] + a.d[2] * pB->d[2][2] + a.d[3] * pB->d[3][2];
	return c;
}

void v4MultiplyEqualMat4x4(V4_F32 *pA, Mat4x4 *pB) {
	V4_F32 c;
	c.d[0] = pA->d[0] * pB->d[0][0] + pA->d[1] * pB->d[1][0] + pA->d[2] * pB->d[2][0] + pA->d[3] * pB->d[3][0];
	c.d[1] = pA->d[0] * pB->d[0][1] + pA->d[1] * pB->d[1][1] + pA->d[2] * pB->d[2][1] + pA->d[3] * pB->d[3][1];
	c.d[2] = pA->d[0] * pB->d[0][2] + pA->d[1] * pB->d[1][2] + pA->d[2] * pB->d[2][2] + pA->d[3] * pB->d[3][2];
	*pA = c;
}

V3_F32 divideByW(V4_F32 a) {
	return _(*(V3_F32 *)&a V3DIVS a.d[3]);
}

V3_F32 v3MultiplyScalar(V3_F32 a, float b) {
	V3_F32 c = {a.d[0] * b, a.d[1] * b, a.d[2] * b};
	return c;
}

void v3DivideEqualScalar(V3_F32 *pA, float b) {
	pA->d[0] /= b;
	pA->d[1] /= b;
	pA->d[2] /= b;
}

V3_F32 v3DivideScalar(V3_F32 a, float b) {
	V3_F32 c = {a.d[0] / b, a.d[1] / b, a.d[2] / b};
	return c;
}

V3_F32 v3SubtractScalar(V3_F32 a, float b) {
	V3_F32 c = {a.d[0] - b, a.d[1] - b, a.d[2] - b};
	return c;
}

V3_F32 v3Subtract(V3_F32 a, V3_F32 b) {
	V3_F32 c = {a.d[0] - b.d[0], a.d[1] - b.d[1], a.d[2] - b.d[2]};
	return c;
}

V3_F32 v3AddScalar(V3_F32 a, float b) {
	V3_F32 c = {a.d[0] + b, a.d[1] + b, a.d[2] + b};
	return c;
}

V3_F32 v3Add(V3_F32 a, V3_F32 b) {
	V3_F32 c = {a.d[0] + b.d[0], a.d[1] + b.d[1], a.d[2] + b.d[2]};
	return c;
}

void v3AddEqual(V3_F32 *pA, V3_F32 b) {
	pA->d[0] += b.d[0];
	pA->d[1] += b.d[1];
	pA->d[2] += b.d[2];
}

int32_t v3GreaterThan(V3_F32 a, V3_F32 b) {
	return (a.d[0] > b.d[0]) && (a.d[1] > b.d[1]) && (a.d[2] > b.d[2]);
}

int32_t v3LessThan(V3_F32 a, V3_F32 b) {
	return (a.d[0] < b.d[0]) && (a.d[1] < b.d[1]) && (a.d[2] < b.d[2]);
}

int32_t v3AproxEqual(V3_F32 a, V3_F32 b) {
	V3_F32 bLow = _(b V3SUBS FLOAT_EQUAL_MARGIN);
	V3_F32 bHigh = _(b V3ADDS FLOAT_EQUAL_MARGIN);
	return _(a V3LESS bHigh) && _(a V3GREAT bLow);
}

V3_F32 v3Lerp(V3_F32 a, V3_F32 b, float alpha) {
	float alphaInverse = 1.0f - alpha;
	V3_F32 c;
	c.d[0] = a.d[0] * alphaInverse + b.d[0] * alpha;
	c.d[1] = a.d[1] * alphaInverse + b.d[1] * alpha;
	c.d[2] = a.d[2] * alphaInverse + b.d[2] * alpha;
	return c;
}

V3_F32 v3UnitFromPoints(V3_F32 a, V3_F32 b) {
	V3_F32 dir = _(b V3SUB a); //direction
	float magnitude = sqrt(dir.d[0] * dir.d[0] + dir.d[1] * dir.d[1]);
	return _(dir V3DIVS magnitude);
}

V3_F32 v3Cross(V3_F32 a, V3_F32 b) {
	V3_F32 c = {
		.d[0] = a.d[1] * b.d[2] - a.d[2] * b.d[1],
		.d[1] = a.d[2] * b.d[0] - a.d[0] * b.d[2],
		.d[2] = a.d[0] * b.d[1] - a.d[1] * b.d[0]
	};
	return c;
}

V3_F32 v3MultiplyMat3x3(V3_F32 a, Mat3x3 *pB) {
	V3_F32 c;
	c.d[0] = a.d[0] * pB->d[0][0] + a.d[1] * pB->d[1][0] + a.d[2] * pB->d[2][0];
	c.d[1] = a.d[0] * pB->d[0][1] + a.d[1] * pB->d[1][1] + a.d[2] * pB->d[2][1];
	c.d[2] = a.d[0] * pB->d[0][2] + a.d[1] * pB->d[1][2] + a.d[2] * pB->d[2][2];
	return c;
}

V3_F32 v3Normalize(V3_F32 a) {
	float magnitude = sqrt(a.d[0] * a.d[0] + a.d[1] * a.d[1] + a.d[2] * a.d[2]);
	return _(a V3DIVS magnitude);
}

int32_t v3IsFinite(V3_F32 a) {
	return isfinite(a.d[0]) && isfinite(a.d[1]) && isfinite(a.d[2]);
}

float v3SquareLen(V3_F32 a) {
	return a.d[0] * a.d[0] + a.d[1] * a.d[1] + a.d[2] * a.d[2];
}

float v3Len(V3_F32 a) {
	return sqrt(v3SquareLen(a));
}

float v3TriArea(V3_F32 a, V3_F32 b, V3_F32 c) {
	V3_F32 ba = _(a V3SUB b);
	V3_F32 bc = _(c V3SUB b);
	V3_F32 cross = _(ba V3CROSS bc);
	return v3Len(cross) / 2.0f;
}

_Bool v3DegenerateTri(V3_F32 a, V3_F32 b, V3_F32 c, float threshold) {
	V3_F32 ac = _(a V3SUB c);
	V3_F32 bc = _(b V3SUB c);
	V3_F32 cross = _(ac V3CROSS bc);
	float len = v3Len(cross);
	return len < threshold && len > -threshold;
}

float v3TriHeight(V3_F32 a, V3_F32 b, V3_F32 c) {
	V3_F32 ac = _(a V3SUB c);
	V3_F32 bc = _(b V3SUB c);
	V3_F32 cross = _(ac V3CROSS bc);
	return v3Len(cross);
}

float v3Dot(V3_F32 a, V3_F32 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1] + a.d[2] * b.d[2];
}

V2_F32 v2Abs(V2_F32 a) {
	if (a.d[0] < .0f) {
		a.d[0] *= -1.0f;
	}
	if (a.d[1] < .0f) {
		a.d[1] *= -1.0f;
	}
	return a;
}

V2_F32 v2Multiply(V2_F32 a, V2_F32 b) {
	V2_F32 c;
	c.d[0] = a.d[0] * b.d[0];
	c.d[1] = a.d[1] * b.d[1];
	return c;
}

void v2MultiplyEqual(V2_F32 *a, V2_F32 b) {
	a->d[0] *= b.d[0];
	a->d[1] *= b.d[1];
}

V2_F32 v2DivideScalar(V2_F32 a, float b) {
	V2_F32 c;
	c.d[0] = a.d[0] / b;
	c.d[1] = a.d[1] / b;
	return c;
}

void v2DivideEqualScalar(V2_F32 *a, float b) {
	a->d[0] /= b;
	a->d[1] /= b;
}

V2_F32 v2Subtract(V2_F32 a, V2_F32 b) {
	V2_F32 c = {a.d[0] - b.d[0], a.d[1] - b.d[1]};
	return c;
}

void v2SubtractEqual(V2_F32 *a, V2_F32 b) {
	a->d[0] -= b.d[0];
	a->d[1] -= b.d[1];
}

V2_F32 v2SubtractScalar(V2_F32 a, float b) {
	V2_F32 c = {a.d[0] - b, a.d[1] - b};
	return c;
}

V2_F32 v2Add(V2_F32 a, V2_F32 b) {
	V2_F32 c = {a.d[0] + b.d[0], a.d[1] + b.d[1]};
	return c;
}

V2_F32 v2AddScalar(V2_F32 a, float b) {
	V2_F32 c = {a.d[0] + b, a.d[1] + b};
	return c;
}

void v2AddEqual(V2_F32 *a, V2_F32 b) {
	a->d[0] += b.d[0];
	a->d[1] += b.d[1];
}

void v2AddEqualScalar(V2_F32 *pA, float b) {
	pA->d[0] += b;
	pA->d[1] += b;
}

void v2MultiplyEqualScalar(V2_F32 *a, float b) {
	a->d[0] *= b;
	a->d[1] *= b;
}

V2_F32 v2MultiplyScalar(V2_F32 a, float b) {
	V2_F32 c = {a.d[0] * b, a.d[1] * b};
	return c;
}

float v2Dot(V2_F32 a, V2_F32 b) {
	return a.d[0] * b.d[0] + a.d[1] * b.d[1];
}

V2_F32 v2Cross(V2_F32 a) {
	V2_F32 b = {a.d[1], -a.d[0]};
	return b;
}

V2_F32 v2ModScalar(V2_F32 a, float b) {
	V2_F32 c = {fmod(a.d[0], b), fmod(a.d[1], b)};
	return c;
}

float v2SquareLen(V2_F32 a) {
	return a.d[0] * a.d[0] + a.d[1] * a.d[1];
}

float v2Len(V2_F32 a) {
	return sqrt(v2SquareLen(a));
}

float v2TriArea(V2_F32 a, V2_F32 b, V2_F32 c) {
	V2_F32 ba = _(a V2SUB b);
	V2_F32 bc = _(c V2SUB b);
	V3_F32 ba3 = {ba.d[0], ba.d[1], .0f};
	V3_F32 bc3 = {bc.d[0], bc.d[1], .0f};
	V3_F32 cross = _(ba3 V3CROSS bc3);
	return fabs(cross.d[2]) / 2.0f;
}

float v2Determinate(V2_F32 a, V2_F32 b) {
	return a.d[0] * b.d[1] - a.d[1] * b.d[0];
}

void v2ModEqualScalar(V2_F32 *a, float b) {
	a->d[0] = fmod(a->d[0], b);
	a->d[1] = fmod(a->d[1], b);
}

int32_t v2GreaterThanEqualTo(V2_F32 a, V2_F32 b) {
	return (a.d[0] >= b.d[0]) && (a.d[1] >= b.d[1]);
}

int32_t v2GreaterThan(V2_F32 a, V2_F32 b) {
	return (a.d[0] > b.d[0]) && (a.d[1] > b.d[1]);
}

int32_t v2GreaterThanScalar(V2_F32 a, float b) {
	return (a.d[0] > b) && (a.d[1] > b);
}

int32_t v2LessThan(V2_F32 a, V2_F32 b) {
	return (a.d[0] < b.d[0]) && (a.d[1] < b.d[1]);
}

int32_t v2LessThanScalar(V2_F32 a, float b) {
	return (a.d[0] < b) && (a.d[1] < b);
}

int32_t v2LessThanEqualTo(V2_F32 a, V2_F32 b) {
	return (a.d[0] <= b.d[0]) && (a.d[1] <= b.d[1]);
}

int32_t v2NotEqual(V2_F32 a, V2_F32 b) {
	return a.d[0] != b.d[0] || a.d[1] != b.d[1];
}

int32_t v2Equal(V2_F32 a, V2_F32 b) {
	return a.d[0] == b.d[0] && a.d[1] == b.d[1];
}

int32_t v2AproxEqual(V2_F32 a, V2_F32 b) {
	V2_F32 bLow = _(b V2SUBS FLOAT_EQUAL_MARGIN);
	V2_F32 bHigh = _(b V2ADDS FLOAT_EQUAL_MARGIN);
	return _(a V2LESSEQL bHigh) && _(a V2GREATEQL bLow);
}

int32_t v2WindingCompare(V2_F32 a, V2_F32 b, V2_F32 centre, int32_t fallBack) {
	V2_F32 aDiff = _(a V2SUB centre);
	V2_F32 bDiff = _(b V2SUB centre);
	float cross = aDiff.d[0] * bDiff.d[1] - bDiff.d[0] * aDiff.d[1];
	if (cross != .0f) {
		return cross > .0f;
	}
	if (fallBack) {
	float aDist = aDiff.d[0] * aDiff.d[0] + aDiff.d[1] * aDiff.d[1];
	float bDist = bDiff.d[0] * bDiff.d[0] + bDiff.d[1] * bDiff.d[1];
	return bDist > aDist;
	}
	else {
		return 2;
	}
}

int32_t v2DegenerateTri(V2_F32 a, V2_F32 b, V2_F32 c, float threshold) {
	V2_F32 ac = _(a V2SUB c);
	V2_F32 bc = _(b V2SUB c);
	float cross = ac.d[0] * bc.d[1] - bc.d[0] * ac.d[1];
	return cross < threshold && cross > -threshold;
}

float v2TriHeight(V2_F32 a, V2_F32 b, V2_F32 c) {
	V2_F32 ac = _(a V2SUB c);
	V2_F32 bc = _(b V2SUB c);
	return ac.d[0] * bc.d[1] - bc.d[0] * ac.d[1];
}

int32_t v2IsFinite(V2_F32 a) {
	return isfinite(a.d[0]) && isfinite(a.d[1]);
}

Mat2x2 mat2x2Adjugate(Mat2x2 a) {
	Mat2x2 c;
	c.d[0][0] = a.d[1][1];
	c.d[0][1] = -a.d[0][1];
	c.d[1][0] = -a.d[1][0];
	c.d[1][1] = a.d[0][0];
	return c;
}

float mat2x2Determinate(Mat2x2 a) {
	return a.d[0][0] * a.d[1][1] - a.d[0][1] * a.d[1][0];
}

void mat2x2MultiplyEqualScalar(Mat2x2 *pA, float b) {
	pA->d[0][0] *= b;
	pA->d[0][1] *= b;
	pA->d[1][0] *= b;
	pA->d[1][1] *= b;
}

Mat2x2 mat2x2Invert(Mat2x2 a) {
	float determinate = mat2x2Determinate(a);
	Mat2x2 inverse = mat2x2Adjugate(a);
	mat2x2MultiplyEqualScalar(&inverse, 1.0f / determinate);
	return inverse;
}

int32_t mat2x2IsFinite(Mat2x2 *pA) {
	return isfinite(pA->d[0][0]) && isfinite(pA->d[0][1]) &&
	       isfinite(pA->d[1][0]) && isfinite(pA->d[1][1]);
}

float mat3x3Determinate(Mat3x3 *pA) {
	float aDet = pA->d[1][1] * pA->d[2][2] - pA->d[2][1] * pA->d[1][2];
	float bDet = pA->d[0][1] * pA->d[2][2] - pA->d[2][1] * pA->d[0][2];
	float cDet = pA->d[0][1] * pA->d[1][2] - pA->d[1][1] * pA->d[0][2];
	return pA->d[0][0] * aDet - pA->d[1][0] * bDet + pA->d[2][0] * cDet;
}

Mat3x3 mat3x3Adjugate(Mat3x3 *pA) {
	Mat3x3 c;
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

void mat3x3MultiplyEqualScalar(Mat3x3 *pA, float b) {
	for (int32_t i = 0; i < 3; ++i) {
		for (int32_t j = 0; j < 3; ++j) {
			pA->d[i][j] *= b;
		}
	}
}

Mat3x3 mat3x3FromV3_F32(V3_F32 a, V3_F32 b, V3_F32 c) {
	Mat3x3 mat;
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

Mat3x3 mat3x3Invert(Mat3x3 *pA) {
	float determinate = mat3x3Determinate(pA);
	Mat3x3 inverse = mat3x3Adjugate(pA);
	mat3x3MultiplyEqualScalar(&inverse, 1.0f / determinate);
	return inverse;
}

int32_t mat3x3IsFinite(Mat3x3 *pA) {
	int32_t isFinite = 0;
	isFinite += isfinite(pA->d[0][0]) && isfinite(pA->d[0][1]) && isfinite(pA->d[0][2]);
	isFinite += isfinite(pA->d[1][0]) && isfinite(pA->d[1][1]) && isfinite(pA->d[1][2]);
	isFinite += isfinite(pA->d[2][0]) && isfinite(pA->d[2][1]) && isfinite(pA->d[2][2]);
	return isFinite;
}

Mat2x3 mat2x2MultiplyMat2x3(Mat2x2 a, Mat2x3 b) {
	Mat2x3 c;
	c.d[0][0] = a.d[0][0] * b.d[0][0] + a.d[0][1] * b.d[1][0];
	c.d[0][1] = a.d[0][0] * b.d[0][1] + a.d[0][1] * b.d[1][1];
	c.d[0][2] = a.d[0][0] * b.d[0][2] + a.d[0][1] * b.d[1][2];
	c.d[1][0] = a.d[1][0] * b.d[0][0] + a.d[1][1] * b.d[1][0];
	c.d[1][1] = a.d[1][0] * b.d[0][1] + a.d[1][1] * b.d[1][1];
	c.d[1][2] = a.d[1][0] * b.d[0][2] + a.d[1][1] * b.d[1][2];
	return c;
}

float customFloor(float a) {
	int32_t aTrunc = a;
	aTrunc -= ((float)aTrunc != a) && (a < .0f);
	return aTrunc;
}

V2_I32 v2FloorAssign(V2_F32 *pA) {
	V2_I32 c;
	c.d[0] = pA->d[0] = customFloor(pA->d[0]);
	c.d[1] = pA->d[1] = customFloor(pA->d[1]);
	return c;
}


V3_F32 barycentricToCartesian(V3_F32 *pTri, V3_F32 *pPoint) {
	V3_F32 pointCartesian;
	pointCartesian.d[0] = (pPoint->d[0] * pTri[0].d[0]) +
	                      (pPoint->d[1] * pTri[1].d[0]) +
	                      (pPoint->d[2] * pTri[2].d[0]);
	pointCartesian.d[1] = (pPoint->d[0] * pTri[0].d[1]) +
	                      (pPoint->d[1] * pTri[1].d[1]) +
						  (pPoint->d[2] * pTri[2].d[1]);
	pointCartesian.d[2] = (pPoint->d[0] * pTri[0].d[2]) +
	                      (pPoint->d[1] * pTri[1].d[2]) +
						  (pPoint->d[2] * pTri[2].d[2]);
	return pointCartesian;
}

V3_F32 cartesianToBarycentric(V2_F32 *pTri32, V2_F32 *pPoint32) {
	V3_F32 pointBc;
	double derta = .0;
	double dertau = .0;
	double dertav = .0;

	//Convert to double
	V3_F64 pPoint = {.d = {pPoint32->d[0], pPoint32->d[1]}};
	V3_F64 pTri[3];
	for (int32_t i = 0; i < 3; ++i) {
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
	pointBc.d[0] = dertau / derta;
	//u = dert(Av) / dert(A)
	pointBc.d[1] = dertav / derta;
	//w can be derived from u and v
	pointBc.d[2] = 1.0 - pointBc.d[0] - pointBc.d[1];

	return pointBc;
}
