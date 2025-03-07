#include <stdint.h>

#include <ImageUtils.h>
#include <Types.h>

void setPixelColor(const StucImage *pImage, I32 idx, const Color *pColor) {
	switch (pImage->type) {
		case STUC_IMAGE_UI8: {
			U8 *pPixel =
				(U8 *)((U8 (*) [4])pImage->pData + idx);
			pPixel[0] = (U8)(pColor->d[0] * (F32)UINT8_MAX);
			pPixel[1] = (U8)(pColor->d[1] * (F32)UINT8_MAX);
			pPixel[2] = (U8)(pColor->d[2] * (F32)UINT8_MAX);
			pPixel[3] = (U8)(pColor->d[3] * (F32)UINT8_MAX);
			return;
		}
		case STUC_IMAGE_UI16: {
			U16 *pPixel =
				(U16 *)((U16 (*) [4])pImage->pData + idx);
			pPixel[0] = (U8)(pColor->d[0] * (F32)UINT16_MAX);
			pPixel[1] = (U8)(pColor->d[1] * (F32)UINT16_MAX);
			pPixel[2] = (U8)(pColor->d[2] * (F32)UINT16_MAX);
			pPixel[3] = (U8)(pColor->d[3] * (F32)UINT16_MAX);
			return;
		 }
		case STUC_IMAGE_UI32: {
			U32 *pPixel =
				(U32 *)((U32 (*) [4])pImage->pData + idx);
			pPixel[0] = (U8)(pColor->d[0] * (F32)UINT32_MAX);
			pPixel[1] = (U8)(pColor->d[1] * (F32)UINT32_MAX);
			pPixel[2] = (U8)(pColor->d[2] * (F32)UINT32_MAX);
			pPixel[3] = (U8)(pColor->d[3] * (F32)UINT32_MAX);
			return;
		 }
		case STUC_IMAGE_F32: {
			((Color *)pImage->pData)[idx] = *pColor;
			return;
		 }
	}
}

I32 getPixelSize(StucImageType type) {
	switch (type) {
		case STUC_IMAGE_UI8:
			return 4;
		case STUC_IMAGE_UI16:
			return 8;
		case STUC_IMAGE_UI32:
			return 16;
		case STUC_IMAGE_F32:
			return 16;
		default:
			STUC_ASSERT("", false);
			return 0;
	}
}

void *offsetImagePtr(const StucImage *pImage, I32 offset) {
	switch (pImage->type) {
		case STUC_IMAGE_UI8:
			return (U8 (*) [4])pImage->pData + offset;
		case STUC_IMAGE_UI16:
			return (U16 (*) [4])pImage->pData + offset;
		case STUC_IMAGE_UI32:
			return (U32 (*) [4])pImage->pData + offset;
		case STUC_IMAGE_F32:
			return (Color *)pImage->pData + offset;
		default:
			STUC_ASSERT("", false);
			return NULL;
	}
}
