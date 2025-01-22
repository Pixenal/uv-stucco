#include <stdint.h>

#include <ImageUtils.h>

void setPixelColor(RuvmImage *pImage, int32_t idx, Color *pColor) {
	switch (pImage->type) {
		case RUVM_IMAGE_UI8: {
			uint8_t *pPixel =
				(uint8_t *)((uint8_t (*) [4])pImage->pData + idx);
			pPixel[0] = pColor->d[0] * (float)UINT8_MAX;
			pPixel[1] = pColor->d[1] * (float)UINT8_MAX;
			pPixel[2] = pColor->d[2] * (float)UINT8_MAX;
			pPixel[3] = pColor->d[3] * (float)UINT8_MAX;
			return;
		}
		case RUVM_IMAGE_UI16: {
			uint16_t *pPixel =
				(uint16_t *)((uint16_t (*) [4])pImage->pData + idx);
			pPixel[0] = pColor->d[0] * (float)UINT16_MAX;
			pPixel[1] = pColor->d[1] * (float)UINT16_MAX;
			pPixel[2] = pColor->d[2] * (float)UINT16_MAX;
			pPixel[3] = pColor->d[3] * (float)UINT16_MAX;
			return;
		 }
		case RUVM_IMAGE_UI32: {
			uint32_t *pPixel =
				(uint32_t *)((uint32_t (*) [4])pImage->pData + idx);
			pPixel[0] = pColor->d[0] * (float)UINT32_MAX;
			pPixel[1] = pColor->d[1] * (float)UINT32_MAX;
			pPixel[2] = pColor->d[2] * (float)UINT32_MAX;
			pPixel[3] = pColor->d[3] * (float)UINT32_MAX;
			return;
		 }
		case RUVM_IMAGE_F32: {
			((Color *)pImage->pData)[idx] = *pColor;
			return;
		 }
	}
}

int32_t getPixelSize(RuvmImageType type) {
	switch (type) {
		case RUVM_IMAGE_UI8:
			return 4;
		case RUVM_IMAGE_UI16:
			return 8;
		case RUVM_IMAGE_UI32:
			return 16;
		case RUVM_IMAGE_F32:
			return 16;
		default:
			RUVM_ASSERT("", false);
			return 0;
	}
}

void *offsetImagePtr(RuvmImage *pImage, int32_t offset) {
	switch (pImage->type) {
		case RUVM_IMAGE_UI8:
			return (uint8_t (*) [4])pImage->pData + offset;
		case RUVM_IMAGE_UI16:
			return (uint16_t (*) [4])pImage->pData + offset;
		case RUVM_IMAGE_UI32:
			return (uint32_t (*) [4])pImage->pData + offset;
		case RUVM_IMAGE_F32:
			return (Color *)pImage->pData + offset;
		default:
			RUVM_ASSERT("", false);
			return NULL;
	}
}
