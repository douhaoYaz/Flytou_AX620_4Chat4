#ifndef _DEF_AX_EXT_IPT_H_
#define _DEF_AX_EXT_IPT_H_

#include "ax_ivps_api.h"

typedef struct {
    AX_U8 nEnable;    /* GDC Enable. Accuracy: U1.0 Range: [0, 1] */
    AX_S64 nCameraMatrix[9];
    AX_S64 nDistortionCoeff[8];
} AX_EXT_GDC_CFG_T;

typedef struct {
    AX_U8 nEnable; /* LDC Enable. Accuracy: U1.0 Range: [0, 1] */
    AX_U8 nAspect; /* Accuracy: U1.0 Range: [0, 1], Whether aspect ration is keep */
    AX_S16 nXRatio; /* Range: [0, 100], field angle ration of horizontal, valid when bAspect=0. */
    AX_S16 nYRatio; /* Range: [0, 100], field angle ration of vertical, valid when bAspect=0. */
    AX_S16 nXYRatio; /* Range: [0, 100], field angle ration of all,valid when bAspect=1. */
    AX_S16 nCenterXOffset; /* Range: [-511, 511], horizontal offset of the image distortion center relative to image center. */
    AX_S16 nCenterYOffset; /* Range: [-511, 511], vertical offset of the image distortion center relative to image center. */
    AX_S16 nDistortionRatio; /* Range: [0, 1023], LDC distortion ratio. 512 is best undistortion */
} AX_EXT_LDC_CFG_T;

AX_S32 TransferLDCCfg(AX_EXT_LDC_CFG_T *ext_ldc_config, AX_IVPS_GDC_CFG_S *gdc_config);

#endif