/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/


#include "SC1345.h"
#include <dlfcn.h>
#include "ax_sys_api.h"
#include "ax_isp_3a_api.h"
#include "StageOptionHelper.h"
#include "ConfigParser.h"
#include "OptionHelper.h"

#define SC1345 "SC1345"

extern COptionHelper gOptions;

CSC1345::CSC1345(SENSOR_CONFIG_T tSensorConfig)
    : CBaseSensor(tSensorConfig)
{
}

CSC1345::~CSC1345(AX_VOID)
{
}

AX_VOID CSC1345::InitSnsAttr()
{
    /* Referenced by AX_VIN_SetSnsAttr */
    m_tSnsAttr.nWidth               = 1280;
    m_tSnsAttr.nHeight              = 720;
    m_tSnsAttr.nFrameRate           = m_tSnsInfo.nFrameRate;
    m_tSnsAttr.eSnsMode             = m_tSnsInfo.eSensorMode;
    if (AX_SNS_HDR_2X_MODE == m_tSnsAttr.eSnsMode) {
        m_tSnsAttr.eRawType         = AX_RT_RAW10;
    } else {
        m_tSnsAttr.eRawType         = AX_RT_RAW10;
    }

    m_tSnsAttr.eSnsHcgLcg           = AX_LCG_NOTSUPPORT_MODE;
    m_tSnsAttr.eBayerPattern        = AX_BP_RGGB;
    m_tSnsAttr.bTestPatternEnable   = AX_FALSE;
    m_tSnsAttr.eMasterSlaveSel      = AX_SNS_MASTER;
    m_tSnsClkAttr.nSnsClkIdx = 0;
    m_tSnsClkAttr.eSnsClkRate = AX_SNS_CLK_24M;
}

AX_VOID CSC1345::InitMipiRxAttr()
{
    // fixme
#ifndef AX_MIPI_LANE_1
#define AX_MIPI_LANE_1 (1)
#endif
    /* Referenced by AX_MIPI_RX_SetAttr */
    m_tMipiRxAttr.eLaneNum      = (AX_MIPI_LANE_NUM_E)AX_MIPI_LANE_1;
    m_tMipiRxAttr.eDataRate     = AX_MIPI_DATA_RATE_80M;
    m_tMipiRxAttr.ePhySel       = AX_MIPI_RX_PHY0_SEL_LANE_0_1_2_3;
    m_tMipiRxAttr.nLaneMap[0]   = 0x00;
    m_tMipiRxAttr.nLaneMap[1]   = 0x01;
    m_tMipiRxAttr.nLaneMap[2]   = 0x03;
    m_tMipiRxAttr.nLaneMap[3]   = 0x04;
    m_tMipiRxAttr.nLaneMap[4]   = 0x02; /* clock lane */
}

AX_VOID CSC1345::InitDevAttr()
{
    /* Referenced by AX_VIN_SetDevAttr */
    m_tDevAttr.eSnsType         = AX_SNS_TYPE_MIPI;
    m_tDevAttr.tDevImgRgn.nStartX = 0;
    m_tDevAttr.tDevImgRgn.nStartY = 0;
    m_tDevAttr.tDevImgRgn.nWidth  = 1280;
    m_tDevAttr.tDevImgRgn.nHeight = 720;
    m_tDevAttr.bDolSplit        = AX_FALSE;
    m_tDevAttr.bHMirror         = AX_FALSE;
    m_tDevAttr.eBayerPattern    = AX_BP_RGGB;
    m_tDevAttr.eSkipFrame       = AX_SNS_SKIP_FRAME_NO_SKIP;
    m_tDevAttr.eSnsGainMode     = AX_SNS_GAIN_MODE_HCG;
    m_tDevAttr.eSnsMode         = m_tSnsInfo.eSensorMode;
    if (AX_SNS_HDR_2X_MODE == m_tDevAttr.eSnsMode) {
        m_tDevAttr.ePixelFmt    = AX_FORMAT_BAYER_RAW_10BPP;
    } else {
        m_tDevAttr.ePixelFmt    = AX_FORMAT_BAYER_RAW_10BPP;
    }
    //  m_tDevAttr.eSnsOutputMode   = AX_SNS_NORMAL;
}

AX_VOID CSC1345::InitPipeAttr()
{
    /* Referenced by AX_VIN_SetPipeAttr */
    m_tPipeAttr.nWidth          = 1280;
    m_tPipeAttr.nHeight         = 720;
    m_tPipeAttr.eBayerPattern   = AX_BP_RGGB;
    m_tPipeAttr.eSnsMode        = m_tSnsInfo.eSensorMode;
    if (AX_SNS_HDR_2X_MODE == m_tPipeAttr.eSnsMode) {
        m_tPipeAttr.ePixelFmt    = m_eImgFormatHDR;
    } else {
        m_tPipeAttr.ePixelFmt    = m_eImgFormatSDR;
    }
    if (AX_SNS_HDR_2X_MODE == m_tPipeAttr.eSnsMode) {
        m_tPipeAttr.ePipeDataSrc     = AX_PIPE_SOURCE_DEV_OFFLINE;
    } else {
        m_tPipeAttr.ePipeDataSrc     = AX_PIPE_SOURCE_DEV_OFFLINE;
    }
    m_tPipeAttr.eDataFlowType   = AX_DATAFLOW_TYPE_NORMAL;
    m_tPipeAttr.eDevSource      = AX_DEV_SOURCE_SNS_ID;
    m_tPipeAttr.ePreOutput      = AX_PRE_OUTPUT_FULL_MAIN;
}

AX_VOID CSC1345::InitChnAttr()
{
    /* Referenced by AX_VIN_SetChnAttr */
    m_tChnAttr.tChnAttr[0].nWidth = 1280;
    m_tChnAttr.tChnAttr[0].nHeight = 720;
    m_tChnAttr.tChnAttr[0].eImgFormat = AX_YUV420_SEMIPLANAR;
    m_tChnAttr.tChnAttr[0].bEnable = AX_TRUE;
    m_tChnAttr.tChnAttr[0].nWidthStride = 1280;
    m_tChnAttr.tChnAttr[0].nDepth = YUV_SC1345_DEPTH;

    if (gOptions.IsActivedDetect()) {
        AI_CONFIG_T tAiConfig;
        memset(&tAiConfig, 0x00, sizeof(tAiConfig));
        if (CConfigParser().GetInstance()->GetAiCfg(0, tAiConfig)) {
            m_tChnAttr.tChnAttr[1].nWidth = tAiConfig.nWidth;
            m_tChnAttr.tChnAttr[1].nHeight = tAiConfig.nHeight;
        } else {
            m_tChnAttr.tChnAttr[1].nWidth = DETECT_DEFAULT_WIDTH;
            m_tChnAttr.tChnAttr[1].nHeight = DETECT_DEFAULT_HEIGHT;
        }
    } else {
        m_tChnAttr.tChnAttr[1].nWidth = 640;
        m_tChnAttr.tChnAttr[1].nHeight = 320;
    }

    m_tChnAttr.tChnAttr[1].eImgFormat = AX_YUV420_SEMIPLANAR;
    m_tChnAttr.tChnAttr[1].bEnable = AX_TRUE;
    m_tChnAttr.tChnAttr[1].nWidthStride = ALIGN_UP(m_tChnAttr.tChnAttr[1].nWidth, 64);
    m_tChnAttr.tChnAttr[1].nDepth = YUV_SC1345_DEPTH;

    m_tChnAttr.tChnAttr[2].nWidth = 320;
    m_tChnAttr.tChnAttr[2].nHeight = 240;
    m_tChnAttr.tChnAttr[2].eImgFormat = AX_YUV420_SEMIPLANAR;
    m_tChnAttr.tChnAttr[2].bEnable = AX_TRUE;
    m_tChnAttr.tChnAttr[2].nWidthStride = 320;
    m_tChnAttr.tChnAttr[2].nDepth = YUV_SC1345_DEPTH;
}

AX_S32 CSC1345::RegisterSensor(AX_U8 nPipe)
{
    std::unique_lock<std::mutex> lck(m_mtxSnsObj);

    AX_S32 nRet = 0;

    m_pSnsLib = dlopen("libsns_sc1345.so", RTLD_LAZY);
    if (NULL == m_pSnsLib) {
        LOG_M_E(SC1345, "Load sensor lib failed!");
        return -1;
    }

    m_pSnsObj = (AX_SENSOR_REGISTER_FUNC_T *)dlsym(m_pSnsLib, "gSnssc1345Obj");
    if (NULL == m_pSnsObj) {
        LOG_M_E(SC1345, "AX_VIN Get Sensor Object Failed!");
        return -1;
    }

    nRet = AX_VIN_RegisterSensor(nPipe, m_pSnsObj); /* pipe 0 */
    if (nRet) {
        LOG_M_E(SC1345, "AX_ISP Register Sensor Failed, ret=0x%x.", nRet);
        return nRet;
    }

    AX_SNS_COMMBUS_T tSnsBusInfo = {0};
    memset(&tSnsBusInfo, 0, sizeof(AX_SNS_COMMBUS_T));
    tSnsBusInfo.I2cDev = GetI2cDevNode(nPipe);
    if (NULL != m_pSnsObj->pfn_sensor_set_bus_info) {
        nRet = m_pSnsObj->pfn_sensor_set_bus_info(nPipe, tSnsBusInfo); /* pipe 0 */
        if (0 != nRet) {
            LOG_M_E(SC1345, "Sensor set bus info Failed, ret=0x%x.", nRet);
            return nRet;
        }
        LOG_M(SC1345, "set sensor bus idx %d", tSnsBusInfo.I2cDev);
    } else {
        LOG_M(SC1345, "not support set sensor bus info!");
        return -1;
    }

    return 0;
}

AX_S32 CSC1345::UnRegisterSensor(AX_U8 nPipe)
{
    std::unique_lock<std::mutex> lck(m_mtxSnsObj);

    AX_VIN_UnRegisterSensor(nPipe); /* pipe 0 */

    if (m_pSnsLib) {
        dlclose(m_pSnsLib);
        m_pSnsLib = nullptr;
        m_pSnsObj = nullptr;
    }

    return 0;
}

AX_S32 CSC1345::RegisterAwbAlgLib(AX_U8 nPipe)
{
    AX_S32 nRet = 0;

    AX_ISP_AWB_REGFUNCS_T tAwbFuncs = {0};

    tAwbFuncs.pfnAwb_Init = AX_ISP_ALG_AwbInit;
    tAwbFuncs.pfnAwb_Exit = AX_ISP_ALG_AwbDeInit;
    tAwbFuncs.pfnAwb_Run  = AX_ISP_ALG_AwbRun;

    /* Register awb alg */
    nRet = AX_ISP_RegisterAwbLibCallback(nPipe, &tAwbFuncs);
    if (nRet) {
        LOG_M_E(SC1345, "AX_ISP_RegisterAwbLibCallback Failed, ret=0x%x", nRet);
        return nRet;
    }

    return 0;
}

AX_S32 CSC1345::UnRegisterAwbAlgLib(AX_U8 nPipe)
{
    AX_S32 nRet = 0;

    nRet = AX_ISP_UnRegisterAwbLibCallback(nPipe);
    if (nRet) {
        LOG_M_E(SC1345, "AX_ISP Unregister Sensor Failed, ret=0x%x", nRet);
        return nRet;
    }

    return nRet;
}

AX_S32 CSC1345::SetFps(AX_U8 nPipe, AX_F32 fFrameRate)
{
    std::unique_lock<std::mutex> lck(m_mtxSnsObj);

    if (m_pSnsObj && m_pSnsObj->pfn_sensor_set_fps) {
        AX_S32 nRet = m_pSnsObj->pfn_sensor_set_fps(nPipe, fFrameRate, NULL);

        if (nRet) {
            LOG_M_E(SC1345, "[%d] Set Sensor Fps  to %.1f failed, ret=0x%x", nPipe, fFrameRate, nRet);
            return nRet;
        }
        else {
            LOG_M(SC1345, "[%d] Set Sensor Fps to %.1f", nPipe, fFrameRate);
        }

        m_tSnsAttr.nFrameRate = (AX_U32)fFrameRate;
    }

    return 0;
}

AX_BOOL CSC1345::HdrModeSupport(AX_U8 nPipe)
{
    return AX_FALSE;
}
