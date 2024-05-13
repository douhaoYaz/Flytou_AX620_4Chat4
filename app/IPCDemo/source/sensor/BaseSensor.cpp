/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/

#include "BaseSensor.h"
#include "OS04a10.h"
#include "IMX334.h"
#include "GC4653.h"
#include "OS08a20.h"
#include "SC1345.h"
#include "SC530AI.h"
#include "SC230AI.h"
#include "IMX327.h"
#include "IMX415.h"
#include "IMX415_5M.h"
#include "IMX464.h"
#include "GC5603.h"
#include "JsonCfgParser.h"
#include "OptionHelper.h"
#include <dlfcn.h>
#include "CommonUtils.h"

#define SENSOR "SENSOR"
#define LENS_OBJ_NUM_MAX  2

extern COptionHelper gOptions;

CBaseSensor::CBaseSensor(SENSOR_CONFIG_T tSensorConfig)
{
    m_nSensorID = 0;
    m_nDeviceID = 0;
    m_nPipeID = 0;
    m_pSnsLib = nullptr;
    m_pSnsObj = nullptr;
    m_eImgFormatSDR = AX_FORMAT_BAYER_RAW_10BPP;
    m_eImgFormatHDR = AX_FORMAT_BAYER_RAW_10BPP;

    FillValues(tSensorConfig);
}

CBaseSensor::~CBaseSensor(AX_VOID)
{

}

CBaseSensor *CBaseSensor::NewInstance(AX_U8 nSensorIndex)
{
    if (nSensorIndex >= E_SENSOR_ID_MAX) {
        LOG_M_E(SENSOR, "Sensor index out of range.");
        return nullptr;
    }

    // 应该是解析运行程序时命令行输入的Sensor配置路径，创建对应的Sensor
    SENSOR_CONFIG_T tSensorCfg;
    if (!CConfigParser().GetInstance()->GetCameraCfg(tSensorCfg, (SENSOR_ID_E)nSensorIndex)) {
        LOG_M_W(SENSOR, "Load camera configure for sensor %d failed.", nSensorIndex);
        return nullptr;
    }

    switch (tSensorCfg.eSensorType) {
        case E_SNS_TYPE_OS04A10: {
            return new COS04a10(tSensorCfg);
        }
        case E_SNS_TYPE_IMX334: {
            return new CIMX334(tSensorCfg);
        }
        case E_SNS_TYPE_GC4653: {
            return new CGC4653(tSensorCfg);
        }
        case E_SNS_TYPE_OS08A20: {
            return new COS08a20(tSensorCfg);
        }
        case E_SNS_TYPE_SC1345: {
            return new CSC1345(tSensorCfg);
        }
        case E_SNS_TYPE_SC530AI: {
            return new CSC530AI(tSensorCfg);
        }
        case E_SNS_TYPE_SC230AI: {
            return new CSC230AI(tSensorCfg);
        }
        case E_SNS_TYPE_IMX327: {
            return new CIMX327(tSensorCfg);
        }
        case E_SNS_TYPE_IMX464: {
            return new CIMX464(tSensorCfg);
        }
        case E_SNS_TYPE_GC5603: {
            return new CGC5603(tSensorCfg);
        }
        case E_SNS_TYPE_IMX415: {
            return new CIMX415(tSensorCfg);
        }
        case E_SNS_TYPE_IMX415_5M:{
            return new CIMX415_5M(tSensorCfg);
        }
        default: {
            LOG_M_E(SENSOR, "unkown senser type %d", tSensorCfg.eSensorType);
            return nullptr;
        }
    }
}

AX_VOID CBaseSensor::FillValues(SENSOR_CONFIG_T tSensorConfig)
{
    m_tSnsInfo.eSensorType    = tSensorConfig.eSensorType;
    m_tSnsInfo.eSensorMode    = tSensorConfig.eSensorMode;
    m_tSnsInfo.nFrameRate     = tSensorConfig.nFrameRate;
    m_tSnsInfo.eRunMode       = tSensorConfig.eRunMode;
    m_tSnsInfo.bSupportTuning = tSensorConfig.bTuning;
    m_tSnsInfo.nTuningPort    = tSensorConfig.nTuningPort;
}

AX_BOOL CBaseSensor::InitISP(AX_VOID)
{
    memset(&m_tSnsAttr, 0, sizeof(AX_SNS_ATTR_T));
    memset(&m_tSnsClkAttr, 0, sizeof(SENSOR_CLK_T));
    memset(&m_tDevAttr, 0, sizeof(AX_DEV_ATTR_T));
    memset(&m_tPipeAttr, 0, sizeof(AX_PIPE_ATTR_T));
    memset(&m_tMipiRxAttr, 0, sizeof(AX_MIPI_RX_ATTR_S));
    memset(&m_tChnAttr, 0, sizeof(AX_VIN_CHN_ATTR_T));

    InitNPU();
    InitMipiRxAttr();
    InitSnsAttr();      // 设置Sensor的属性m_tSnsAttr和m_tSnsClkAttr结构体，作为AX_VIN_SetSnsAttr的参数输入
    InitDevAttr();      // 设置Dev设备的属性m_tDevAttr结构体，作为AX_VIN_SetDevAttr的参数输入
    InitPipeAttr();     // 设置Pipe的属性m_tPipeAttr结构体，作为AX_VIN_SetPipeAttr的参数输入
    InitChnAttr();      // 设置输出通道Channel的属性m_tChnAttr结构体，作为AX_VIN_SetChnAttr的参数输入
    if (gOptions.IsEISSupport()) { //ALIGN_UP for GDC
        for (AX_U8 i = 0; i < MAX_ISP_CHANNEL_NUM; i++) {
            m_tChnAttr.tChnAttr[i].nWidthStride = ALIGN_UP(m_tChnAttr.tChnAttr[i].nWidthStride, GDC_STRIDE_ALIGNMENT);
            m_tChnAttr.tChnAttr[i].nDepth = gOptions.GetEISDelayNum() + 2; // set EIS yuv depth
        }
    }

    if (!gOptions.IsLinkMode()) {
        for (auto& tChnAttr : m_tChnAttr.tChnAttr) {
            tChnAttr.nDepth = (tChnAttr.nDepth == 0) ? YUV_NONLINK_DEPTH : tChnAttr.nDepth;
        }
    }

    if (gOptions.IsEnableEISEffectComp() && (MAX_ISP_CHANNEL_NUM > 1)) {
        // last chn ivps linked will use TDP(no eis) and scale down 1/4 size of camere resolution for comparison
        m_tChnAttr.tChnAttr[MAX_ISP_CHANNEL_NUM - 1].nWidth       = m_tChnAttr.tChnAttr[0].nWidth / 4;
        m_tChnAttr.tChnAttr[MAX_ISP_CHANNEL_NUM - 1].nHeight      = m_tChnAttr.tChnAttr[0].nHeight / 4;
        m_tChnAttr.tChnAttr[MAX_ISP_CHANNEL_NUM - 1].nWidthStride = m_tChnAttr.tChnAttr[0].nWidth / 4;
        LOG_M(SENSOR, "EIS Effect comparison is on, reset chn: %d (w, h, s) to (%d, %d, %d)",
                       MAX_ISP_CHANNEL_NUM - 1, m_tChnAttr.tChnAttr[MAX_ISP_CHANNEL_NUM - 1].nWidth,
                       m_tChnAttr.tChnAttr[MAX_ISP_CHANNEL_NUM - 1].nHeight, m_tChnAttr.tChnAttr[MAX_ISP_CHANNEL_NUM - 1].nWidthStride);
    }

    return AX_TRUE;
}

AX_VOID CBaseSensor::InitNPU()
{
    /* NPU */
    AX_NPU_SDK_EX_ATTR_T npuAttr;
    npuAttr.eHardMode = AX_NPU_VIRTUAL_DISABLE;
    if (AX_ISP_PIPELINE_NORMAL == m_tSnsInfo.eRunMode) {
        npuAttr = CBaseSensor::GetNpuAttr(m_tSnsInfo.eSensorType, m_tSnsInfo.eSensorMode);
    }

    AX_S32 nRet = AX_NPU_SDK_EX_Init_with_attr(&npuAttr);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_NPU_SDK_EX_Init_with_attr(%d) failed, ret=0x%x.", npuAttr.eHardMode, nRet);
    }
}

AX_BOOL CBaseSensor::Init(AX_U8 nSensorID, AX_U8 nDevID, AX_U8 nPipeID, AX_POOL_FLOORPLAN_T *stVbConf, AX_U8 &nCount)
{
    LOG_M(SENSOR, "[%d][%d][%d] +++", nSensorID, nDevID, nPipeID);
    m_nSensorID = nSensorID;
    m_nDeviceID = nDevID;
    m_nPipeID   = nPipeID;

    if (!InitISP()) {
        LOG_M_E(SENSOR, "Sensor %d init failed.", nSensorID);
        return AX_FALSE;
    }

    CalcBufSize(nPipeID, stVbConf, nCount, 10, 5);

    LOG_M(SENSOR, "[%d][%d][%d] ---", nSensorID, nDevID, nPipeID);
    return AX_TRUE;
}

AX_BOOL CBaseSensor::Open()
{
    LOG_M(SENSOR, "+++");

    LOG_M(SENSOR, "Sensor Attr => w:%d h:%d framerate:%d sensor mode:%d run mode: %d rawType:%d"
          , m_tSnsAttr.nWidth
          , m_tSnsAttr.nHeight
          , m_tSnsAttr.nFrameRate
          , m_tSnsAttr.eSnsMode
          , m_tSnsInfo.eRunMode
          , m_tSnsAttr.eRawType);

    AX_S32 nRet = 0;
    AX_VIN_SNS_DUMP_ATTR_T  tDumpAttr;

    nRet = AX_VIN_Create(m_nPipeID);        // 创建一个 VIN PIPE，为一路 VIN Pipeline 处理创建必要的资源
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VIN_Create failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    nRet = RegisterSensor(m_nPipeID);       // 调用AX_VIN_RegisterSensor()，VIN 提供的 sensor 注册回调接口
    if (AX_SDK_PASS != nRet) {
        LOG_M_E(SENSOR, "RegisterSensor failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    nRet = AX_VIN_SetRunMode(m_nPipeID, m_tSnsInfo.eRunMode);   // VIN 提供的设置 Isp Pipeline 运行模式的接口，用户通过该接口，可以配置是否在 Pipeline 中串联 NPU 模块
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VIN_SetRunMode failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    // 用户需要在调用 AX_VIN_Start()接口之前配置 Sensor 属性。
    // 这里通过传入m_tSnsAttr结构体作为实参来设置 Camera Sensor 的属性，包含 Sensor 的宽高、 Bayer 格式、数据位宽、帧率、 HDR 模式、 HCG/LCG 模式等。而m_tSnsAttr结构体是在InitISP()函数中调用InitSnsAttr()函数设置的。
    nRet = AX_VIN_SetSnsAttr(m_nPipeID, &m_tSnsAttr);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VIN_SetSnsAttr failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    nRet = AX_VIN_SetDevAttr(m_nPipeID, &m_tDevAttr);       //设置 VIN 设备属性。支持对 sensor 输出的原始分辨率做裁剪，用户需要了解 sensor 输出的实际分辨率，然后在这里做必要的裁剪，将裁剪之后的宽高配置给 DEV，从而获取到真正有效的宽高
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VIN_SetDevAttr failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    if (m_pSnsObj && m_pSnsObj->pfn_sensor_reset) {
        m_pSnsObj->pfn_sensor_reset(m_nPipeID);
    }

    nRet = AX_MIPI_RX_Reset((AX_MIPI_RX_DEV_E)m_nPipeID);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_MIPI_RX_Reset failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    nRet = AX_MIPI_RX_SetAttr((AX_MIPI_RX_DEV_E)m_nPipeID, &m_tMipiRxAttr);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_MIPI_RX_SetAttr failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    nRet = AX_VIN_OpenSnsClk(m_nPipeID, m_tSnsClkAttr.nSnsClkIdx, m_tSnsClkAttr.eSnsClkRate);   // 配置 Sensor MCLK，并且使能 MCLK，包含频率和 Clk 的 Index
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VIN_OpenSnsClk failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    nRet = AX_VIN_SetChnAttr(m_nPipeID, &m_tChnAttr);           // 设置 VIN 输出通道属性。通道的输入是 ISP Pipeline 处理完成之后的 YUV 图像，每一路的pipe 均支持三个物理通道，通道 0 用来处理与 pipe 相同分辨率的图像，通道 1/通道 2 用来处理下采样之后的图像
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VIN_SetChnAttr failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    nRet = AX_VIN_SetPipeAttr(m_nPipeID, &m_tPipeAttr);         // 设置 PIPE 属性
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VI_SetPipeAttr failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    AX_VIN_DEV_BIND_PIPE_T tDevBindPipe;
    memset(&tDevBindPipe, 0, sizeof(AX_VIN_DEV_BIND_PIPE_T));
    tDevBindPipe.nNum = 1;                                      // Pipe 数量
    tDevBindPipe.nPipeId[0] = m_nPipeID;
    nRet = AX_VIN_SetDevBindPipe(m_nPipeID, &tDevBindPipe);     // 设置 dev 与 pipe 之间的对应关系。
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VIN_SetDevBindPipe failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    // ISP pipeline 各个模块的注册，包含 3A 模块。用户需要在调用该接口之前，完成 3A 算法库的注册
    nRet = AX_ISP_Open(m_nPipeID);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_ISP_Open failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    // Load nonebias bin
    string strSnsName = gOptions.GetSensorName();
    string strNoneBiasBin = CCommonUtils::GetBinFile(AX_LOAD_BIN_ROOT, strSnsName, m_tSnsAttr.eSnsMode, AX_TAG_NONE_BIAS);
    if (strNoneBiasBin.empty()) {
        gOptions.SetLoadNoneBiasBinSupport(AX_FALSE);
    } else {
        gOptions.SetLoadNoneBiasBinSupport(AX_TRUE);
        if (gOptions.IsLoadNoneBiasBinEnable()) {
            LoadBinParams(strNoneBiasBin);
        }
    }

    //EIS Load bin, call after AX_ISP_Open and before AX_VIN_Start
    if (gOptions.IsEISSupport()) {
        SENSOR_CONFIG_T tSensorCfg;
        CConfigParser().GetInstance()->GetCameraCfg(tSensorCfg, (SENSOR_ID_E)m_nSensorID);
        AX_CHAR *chEISBinPath = (AX_SNS_LINEAR_MODE == m_tSnsAttr.eSnsMode) ? tSensorCfg.aEISSdrBin : tSensorCfg.aEISHdrBin;

        if (!LoadBinParams(chEISBinPath)) {
            return AX_FALSE;
        }
    }

    LoadIqBin();

    nRet = RegisterAeAlgLib(m_nPipeID);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "RegisterAeAlgLib failed, ret=0x%x", nRet);
    }

    nRet = RegisterAwbAlgLib(m_nPipeID);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "RegisterAwbAlgLib failed, ret=0x%x.", nRet);
    }

    nRet = AX_VIN_Start(m_nPipeID);         // VIN pipe 启动运行，完成 VIN 模块的启动、 Sensor 的初始化等功能
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VIN_Start failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    nRet = AX_VIN_EnableDev(m_nPipeID);     // 使能 VIN Dev 设备
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_VIN_EnableDev failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    AX_PIPE_ATTR_T tPipeAttr = {0};
    AX_VIN_GetPipeAttr(m_nPipeID, &tPipeAttr);  // 获取 VIN PIPE 属性
    if (AX_PIPE_SOURCE_DEV_OFFLINE == tPipeAttr.ePipeDataSrc) {
        tDumpAttr.bEnable = AX_TRUE;
        tDumpAttr.nDepth = 2;
        nRet = AX_VIN_SetSnsDumpAttr(m_nPipeID, &tDumpAttr);
        if (0 != nRet) {
            LOG_M_E(SENSOR, " failed, ret=0x%x.\n", nRet);
            return AX_FALSE;
        }
    }

    AX_VIN_DUMP_ATTR_T tVinDumpAttr;
    memset(&tVinDumpAttr, 0x00, sizeof(tVinDumpAttr));
    tVinDumpAttr.bEnable = AX_TRUE;
    tVinDumpAttr.nDepth = (VIN_IFE_DUMP_ENABLE ? 1 : 0); // the depth will affect the block of raw10
    AX_VIN_SetPipeDumpAttr(m_nPipeID, VIN_DUMP_SOURCE_IFE, &tVinDumpAttr);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "set VIN_DUMP_SOURCE_IFE fail, ret=0x%x.\n", nRet);
        return AX_FALSE;
    }

    memset(&tVinDumpAttr, 0x00, sizeof(tVinDumpAttr));
    tVinDumpAttr.bEnable = AX_TRUE;
    tVinDumpAttr.nDepth = (VIN_NPU_DUMP_ENABLE ? 1 : 0); // the depth will affect the block of raw16
    AX_VIN_SetPipeDumpAttr(m_nPipeID, VIN_DUMP_SOURCE_NPU, &tVinDumpAttr);  // 设置 ife/npu/itp dump 数据属性
    if (0 != nRet) {
        LOG_M_E(SENSOR, "set VIN_DUMP_SOURCE_NPU fail, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    // Set EIS IQ Params
    if (gOptions.IsEISSupport()) {
        if (!EnableEIS(gOptions.IsEnableEIS())) {
            LOG_M_E(SENSOR, "Enable EIS failed!");
            return AX_FALSE;
        }
    }

    // Sensor 开流，打开之后 Sensor 能够正常输出视频数据。该接口需要在 AX_VIN_Start、 AX_VIN_EnableDev、 AX_VIN_SetSnsDumpAttr 接口之后调用。 也就是客户在调用该接口之前，需要保证 sensor 处于关流的状态，这样能够保证时序上的完全匹配。
    AX_VIN_StreamOn(m_nPipeID); /* stream on */

    if (m_tSnsInfo.bSupportTuning) {
        nRet = AX_NT_StreamInit(6000);
        if (0 != nRet) {
            LOG_M_E(SENSOR, "AX_NT_StreamInit failed, ret=0x%x.", nRet);
            return AX_FALSE;
        }

        nRet = AX_NT_CtrlInit(m_tSnsInfo.nTuningPort);
        if (0 != nRet) {
            LOG_M_E(SENSOR, "AX_NT_CtrlInit failed, ret=0x%x.", nRet);
            return AX_FALSE;
        } else {
            LOG_M(SENSOR, "Enable tunning on port: %d\n", m_tSnsInfo.nTuningPort);
        }

        AX_NT_SetStreamSource(m_nPipeID);
    }

    LOG_M(SENSOR, "---");
    return AX_TRUE;
}

AX_BOOL CBaseSensor::Close()
{
    AX_VIN_SNS_DUMP_ATTR_T  tDumpAttr;

    LOG_M(SENSOR, "[%d][%d][%d] +++", m_nSensorID, m_nDeviceID, m_nPipeID);

    AX_VIN_StreamOff(m_nPipeID);

    AX_PIPE_ATTR_T tPipeAttr = {0};
    AX_VIN_GetPipeAttr(m_nPipeID, &tPipeAttr);
    if (AX_PIPE_SOURCE_DEV_OFFLINE == tPipeAttr.ePipeDataSrc) {
        tDumpAttr.bEnable = AX_FALSE;
        AX_VIN_SetSnsDumpAttr(m_nPipeID, &tDumpAttr);
    }
    AX_VIN_CloseSnsClk(m_tSnsClkAttr.nSnsClkIdx);
    AX_VIN_DisableDev(m_nPipeID);

    AX_VIN_Stop(m_nPipeID);
    UnRegisterAeAlgLib(m_nPipeID);
    UnRegisterAwbAlgLib(m_nPipeID);
    AX_ISP_Close(m_nPipeID);
    UnRegisterSensor(m_nPipeID);
    AX_VIN_Destory(m_nPipeID);

    if (m_tSnsInfo.bSupportTuning) {
        AX_NT_CtrlDeInit();
        AX_NT_StreamDeInit();
    }

    LOG_M(SENSOR, "[%d][%d][%d] ---", m_nSensorID, m_nDeviceID, m_nPipeID);

    return AX_TRUE;
}

AX_NPU_SDK_EX_ATTR_T CBaseSensor::GetNpuAttr(SNS_TYPE_E eSnsType, AX_SNS_HDR_MODE_E eSnsMode)
{
    AX_NPU_SDK_EX_ATTR_T npuAttr;

    switch (eSnsType) {
    case E_SNS_TYPE_OS04A10:
        if (eSnsMode == AX_SNS_HDR_3X_MODE) {
            npuAttr.eHardMode = AX_NPU_VIRTUAL_DISABLE;
        } else {
            npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        }
        break;
    case E_SNS_TYPE_IMX334:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    case E_SNS_TYPE_GC4653:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    case E_SNS_TYPE_OS08A20:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    case E_SNS_TYPE_SC1345:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    case E_SNS_TYPE_SC530AI:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    case E_SNS_TYPE_SC230AI:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    case E_SNS_TYPE_IMX327:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    case E_SNS_TYPE_IMX464:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    case E_SNS_TYPE_GC5603:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    case E_SNS_TYPE_IMX415:
    case E_SNS_TYPE_IMX415_5M:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_1_1;
        break;
    default:
        npuAttr.eHardMode = AX_NPU_VIRTUAL_DISABLE;
        break;
    }

    return npuAttr;
}

AX_BOOL CBaseSensor::GetSnsAttr(AX_SNS_ATTR_T& attr)
{
    attr = m_tSnsAttr;
    return AX_TRUE;
}

AX_BOOL CBaseSensor::GetChnAttr(AX_VIN_CHN_ATTR_T& attr)
{
    attr = m_tChnAttr;
    return AX_TRUE;
}

AX_VOID CBaseSensor::UpdateSnsMode(AX_SNS_HDR_MODE_E eMode)
{
    m_tSnsInfo.eSensorMode = eMode;
}

AX_VOID CBaseSensor::UpdateFramerate(AX_F32 fFramerate)
{
    m_tSnsInfo.nFrameRate = (AX_U32)fFramerate;

    SetFps(m_nPipeID, fFramerate);
}

AX_BOOL CBaseSensor::EnableEIS(AX_BOOL bEnableEIS)
{
    AX_ISP_IQ_EIS_PARAM_T tEISParam;
    AX_S32 nRet = 0;

    nRet = AX_ISP_IQ_GetEisParam(m_nPipeID, &tEISParam);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_ISP_IQ_GetEisParam failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    tEISParam.bEisEnable    = bEnableEIS;
    tEISParam.nEisDelayNum  = gOptions.GetEISDelayNum();
    tEISParam.nCropRatioW   = gOptions.GetEISCropW();
    tEISParam.nCropRatioH   = gOptions.GetEISCropH();
    nRet = AX_ISP_IQ_SetEisParam(m_nPipeID, &tEISParam);
    if (0 != nRet) {
        LOG_M_E(SENSOR, "AX_ISP_IQ_SetEisParam failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    if (tEISParam.bEisEnable) {
        LOG_M(SENSOR, "EIS Model: %s", tEISParam.tEisNpuParam.szModelName);
        LOG_M(SENSOR, "Wbt Model: %s", tEISParam.tEisNpuParam.szWbtModelName);
    }

    return AX_TRUE;
}

AX_BOOL CBaseSensor::BuffPoolInit(AX_POOL_FLOORPLAN_T *stVbConf, AX_U8 nCount)
{
    AX_S32 nRet = 0;

    AX_U8 i = 0, j = 0;
    AX_POOL_CONFIG_T v;

    /* Sort in ascending order */
    for (i = 0; i < nCount - 1; i++) {
        for (j = i + 1; j < nCount; j++) {
            if (stVbConf->CommPool[i].BlkSize > stVbConf->CommPool[j].BlkSize) {
                v.BlkSize = stVbConf->CommPool[i].BlkSize;
                v.BlkCnt = stVbConf->CommPool[i].BlkCnt;
                stVbConf->CommPool[i].BlkSize = stVbConf->CommPool[j].BlkSize;
                stVbConf->CommPool[i].BlkCnt = stVbConf->CommPool[j].BlkCnt;
                stVbConf->CommPool[j].BlkSize = v.BlkSize;
                stVbConf->CommPool[j].BlkCnt = v.BlkCnt;
            }
        }
    }

    /* Merge by size */
    for (i = 1, j = 0; i < nCount; i++) {
        if (stVbConf->CommPool[j].BlkSize != stVbConf->CommPool[i].BlkSize) {
            j += 1;
            stVbConf->CommPool[j].BlkSize = stVbConf->CommPool[i].BlkSize;
            stVbConf->CommPool[j].BlkCnt = stVbConf->CommPool[i].BlkCnt;
            if (i != j) {
                stVbConf->CommPool[i].BlkSize = 0;
                stVbConf->CommPool[i].BlkCnt = 0;
            }
        } else {
            stVbConf->CommPool[j].BlkCnt += stVbConf->CommPool[i].BlkCnt;
            stVbConf->CommPool[i].BlkSize = 0;
            stVbConf->CommPool[i].BlkCnt = 0;
        }
    }
    nCount = j + 1;

    for (AX_U32 i = 0; i < nCount; ++i) {
        LOG_M(SENSOR, "[%d] BlkSize:%lld, BlkCnt:%d", i, stVbConf->CommPool[i].BlkSize, stVbConf->CommPool[i].BlkCnt);
    }

    nRet = AX_POOL_SetConfig(stVbConf);
    if (nRet) {
        LOG_M_E(SENSOR, "AX_POOL_SetConfig fail!Error Code:0x%X", nRet);
        return AX_FALSE;
    } else {
        LOG_M(SENSOR, "AX_POOL_SetConfig success!");
    }

    nRet = AX_POOL_Init();
    if (nRet) {
        LOG_M_E(SENSOR, "AX_POOL_Init fail!!Error Code:0x%X", nRet);
    } else {
        LOG_M(SENSOR, "AX_POOL_Init success!");
    }

    return (nRet == 0) ? AX_TRUE : AX_FALSE;
}

AX_BOOL CBaseSensor::BuffPoolExit()
{
    AX_S32 nRet = AX_POOL_Exit();
    if (nRet) {
        LOG_M_E(SENSOR, "AX_POOL_Exit fail, ret=0x%x.", nRet);
    } else {
        LOG_M(SENSOR, "AX_POOL_Exit success!");
    }
    return (nRet == 0) ? AX_TRUE : AX_FALSE;
}

AX_VOID CBaseSensor::CalcBufSize(AX_U8 nPipe, AX_POOL_FLOORPLAN_T *stVbConf, AX_U8 &nCount, AX_U32 nPreISPBlkCnt, AX_U32 nNpuBlkCnt)
{
    AX_S32 nImgWidth = m_tDevAttr.tDevImgRgn.nWidth;
    AX_S32 nImgHeight = m_tDevAttr.tDevImgRgn.nHeight;
    AX_IMG_FORMAT_E eMaxImgFormat = (m_eImgFormatSDR >= m_eImgFormatHDR ? m_eImgFormatSDR : m_eImgFormatHDR);
    AX_IMG_FORMAT_E eFormat = (AX_ISP_PIPELINE_NONE_NPU == m_tSnsInfo.eRunMode ? AX_FORMAT_BAYER_RAW_14BPP : eMaxImgFormat);
    // fbc header
    if (AX_RAW_BLOCK_COUNT == 0) {
        if (HdrModeSupport(nPipe)) {
            stVbConf->CommPool[nCount].BlkCnt = (nPreISPBlkCnt + 3 * AX_SNS_HDR_2X_MODE);
        }
        else {
            stVbConf->CommPool[nCount].BlkCnt = (nPreISPBlkCnt + 3 * m_tSnsInfo.eSensorMode);
        }
    }
    else {
        if (HdrModeSupport(nPipe)) {
            stVbConf->CommPool[nCount].BlkCnt = AX_RAW_BLOCK_COUNT;
        }
        else {
            stVbConf->CommPool[nCount].BlkCnt = AX_RAW_BLOCK_COUNT_SDR;
        }
    }
    stVbConf->CommPool[nCount].BlkSize   = AX_VIN_GetImgBufferSize(nImgHeight, nImgWidth, eFormat, AX_TRUE);
    stVbConf->CommPool[nCount].MetaSize  = AX_RAW_META_SIZE * 1024;
    memset(stVbConf->CommPool[nCount].PartitionName, 0, sizeof(stVbConf->CommPool[nCount].PartitionName));
    strcpy((char *)stVbConf->CommPool[nCount].PartitionName, "anonymous");
    nCount += 1;

    nImgWidth = m_tPipeAttr.nWidth;
    nImgHeight = m_tPipeAttr.nHeight;
    // fbc header
    stVbConf->CommPool[nCount].BlkCnt    = (AX_NPU_BLOCK_COUNT == 0) ? nNpuBlkCnt : AX_NPU_BLOCK_COUNT;
    stVbConf->CommPool[nCount].BlkSize   = AX_VIN_GetImgBufferSize(nImgHeight, nImgWidth, AX_FORMAT_BAYER_RAW_16BPP, AX_TRUE);
    stVbConf->CommPool[nCount].MetaSize  = AX_NPU_META_SIZE * 1024;
    memset(stVbConf->CommPool[nCount].PartitionName, 0, sizeof(stVbConf->CommPool[nCount].PartitionName));
    strcpy((char *)stVbConf->CommPool[nCount].PartitionName, "anonymous");
    nCount += 1;

    AX_U64 nBlkSizeNormal = 0;
    AX_U64 nBlkSizeRotate90 = 0;
    for (AX_U32 nChn = 0; nChn < MAX_ISP_CHANNEL_NUM; nChn++) {
        if (m_tChnAttr.tChnAttr[nChn].bEnable) {
            nBlkSizeNormal = AX_VIN_GetImgBufferSize(m_tChnAttr.tChnAttr[nChn].nHeight, m_tChnAttr.tChnAttr[nChn].nWidthStride, m_tChnAttr.tChnAttr[nChn].eImgFormat, AX_TRUE);
            nBlkSizeRotate90 = AX_VIN_GetImgBufferSize(m_tChnAttr.tChnAttr[nChn].nWidth, ALIGN_UP(m_tChnAttr.tChnAttr[nChn].nHeight, 64), m_tChnAttr.tChnAttr[nChn].eImgFormat, AX_TRUE);
            stVbConf->CommPool[nCount].MetaSize   = AX_YUV_META_SIZE * 1024;
            stVbConf->CommPool[nCount].BlkSize    = AX_MAX(nBlkSizeNormal, nBlkSizeRotate90);
            stVbConf->CommPool[nCount].BlkCnt     = m_tChnAttr.tChnAttr[nChn].nDepth + GetAdditionYUVBLKCount(nChn);

            //for WBT_SDR_WNR
            if ((0 == nChn)/* && (m_tSnsInfo.eSensorMode == AX_SNS_LINEAR_MODE)*/) {
                stVbConf->CommPool[nCount].BlkCnt += YUV_BLOCK_COUNT_ADDATION_WBT_SDR_WNR;
            }

            //DETECTION
            if (gOptions.IsActivedDetect()) {
                // ai detection
                if (1 == nChn) {
                    // ai ppl should remove venc block requirement
                    stVbConf->CommPool[nCount].BlkCnt -= 1;

                    // ai block count
                    AI_CONFIG_T tAiConfig;
                    memset(&tAiConfig, 0x00, sizeof(tAiConfig));
                    if (CConfigParser().GetInstance()->GetAiCfg(m_nSensorID, tAiConfig)) {
                        AX_U32 nFrameBlkCnt = tAiConfig.nFrameDepth ? tAiConfig.nFrameDepth
                                                : AX_AI_DETECT_FRAME_DEPTH_BLOCK_COUNT;
                        AX_U32 nCacheBlkCnt = tAiConfig.nCacheDepth ? tAiConfig.nCacheDepth
                                                : AX_AI_DETECT_CACHE_LIST_DEPTH_BLOCK_COUNT;

                        // at least one frame block
                        nFrameBlkCnt = nFrameBlkCnt ? nFrameBlkCnt : 1;

                        // at least one cache block
                        nCacheBlkCnt = nCacheBlkCnt ? nCacheBlkCnt : 1;

                        stVbConf->CommPool[nCount].BlkCnt += nFrameBlkCnt + nCacheBlkCnt;
                    }
                    else {
                        stVbConf->CommPool[nCount].BlkCnt += AX_AI_DETECT_BLOCK_COUNT;
                    }
                }
            }

            //for EIS
            if (gOptions.IsEISSupport()) {
                stVbConf->CommPool[nCount].BlkCnt += 1; // depth + 1 blocks for isp
            }

            stVbConf->CommPool[nCount].CacheMode  = POOL_CACHE_MODE_NONCACHE;

            memset(stVbConf->CommPool[nCount].PartitionName, 0, sizeof(stVbConf->CommPool[nCount].PartitionName));
            strcpy((char *)stVbConf->CommPool[nCount].PartitionName, "anonymous");

            nCount += 1;
        }
    }
}

AX_S8 CBaseSensor::GetI2cDevNode(AX_U8 nPipe)
{
    AX_S8 nBusNum = 0;
    AX_U8 board_id = 0;
    FILE *pFile = NULL;
    AX_CHAR id[10] = {0};

    pFile = fopen("/sys/devices/platform/hwinfo/board_id", "r");
    if (pFile) {
        fread(&id[0], 10, 1, pFile);
        fclose(pFile);
    } else {
        LOG_M_E(SENSOR, "Failed to open /sys/devices/platform/hwinfo/board_id!");
    }

    board_id = atoi(id);
    if (0 == strncasecmp("F", id, 1)) {
       board_id = 15;
    }

    LOG_M(SENSOR, "Board id: %d", board_id);

    if (0 == board_id || 1 == board_id) {
        if (0 == nPipe || 1 == nPipe) {
            nBusNum = 0;
        } else {
            nBusNum = 1;
        }
    } else if (2 == board_id || 3 == board_id || 15 == board_id) {
        if (0 == nPipe) {
            nBusNum = 0;
        } else if (1 == nPipe) {
            nBusNum = 1;
        } else {
            nBusNum = 6;
        }
    } else {
        LOG_M_E(SENSOR, "Board id: %d error!", board_id);
        return -1;
    }

    return nBusNum;
}

AX_VOID CBaseSensor::ResetSensor(AX_U8 nPipe)
{
    if (m_pSnsObj && m_pSnsObj->pfn_sensor_reset) {
        m_pSnsObj->pfn_sensor_reset(nPipe);
    }
}

AX_VOID CBaseSensor::InitSensor(AX_U8 nPipe)
{
    std::unique_lock<std::mutex> lck(m_mtxSnsObj);

    if (m_pSnsObj && m_pSnsObj->pfn_sensor_init) {
        m_pSnsObj->pfn_sensor_init(nPipe);
    }
}

AX_VOID CBaseSensor::ExitSensor(AX_U8 nPipe)
{
    std::unique_lock<std::mutex> lck(m_mtxSnsObj);

    if (m_pSnsObj && m_pSnsObj->pfn_sensor_exit) {
        m_pSnsObj->pfn_sensor_exit(nPipe);
    }
}

AX_U32 CBaseSensor::GetAdditionYUVBLKCount(AX_U32 nChn)
{
    AX_U32 nBlkCnt = 0;

#ifdef AX_SIMPLIFIED_MEM_VER
    nBlkCnt += YUV_BLOCK_COUNT_ADDATION;
#else
    //ivps
    nBlkCnt += AX_IVPS_IN_FIFO_DEPTH + AX_IVPS_OUT_FIFO_DEPTH;

    //venc
    nBlkCnt += (0 == AX_VENC_IN_FIFO_DEPTH) ? 4 : AX_VENC_IN_FIFO_DEPTH;

    nBlkCnt += (0 == AX_VENC_OUT_FIFO_DEPTH) ? 4 : AX_VENC_OUT_FIFO_DEPTH;
#endif
    LOG_M(SENSOR, "Chn[%d] addition block count: %d", nChn, nBlkCnt);

    return nBlkCnt;
}

AX_BOOL CBaseSensor::GetSnsTemperature(AX_F32 &fTemperature)
{
    std::unique_lock<std::mutex> lck(m_mtxSnsObj);

    if (m_pSnsObj && m_pSnsObj->pfn_sensor_get_temperature_info) {
        AX_S32 nTemperature = 0;

        AX_S32 nRet = m_pSnsObj->pfn_sensor_get_temperature_info(m_nPipeID, &nTemperature);

        if (0 == nRet) {
            fTemperature = (AX_F32)nTemperature/1000.0;

            return AX_TRUE;
        }
    }

    return AX_FALSE;
}

AX_BOOL CBaseSensor::LoadIqBin()
{
    SENSOR_CONFIG_T tSensorCfg;
    if (!CConfigParser().GetInstance()->GetCameraCfg(tSensorCfg, (SENSOR_ID_E)m_nSensorID)) {
        LOG_M_W(SENSOR, "Load camera configure for sensor %d failed.", m_nSensorID);
        return AX_FALSE;
    }

    AX_CHAR* pBinPath = nullptr;
    AX_SNS_HDR_MODE_E eNewSnsMode = m_tSnsInfo.eSensorMode;
    if (AX_SNS_HDR_2X_MODE == eNewSnsMode) {
        pBinPath = tSensorCfg.aIqHdrBin;
    } else {
        pBinPath = tSensorCfg.aIqSdrBin;
    }

    if (strlen(pBinPath) == 0) {
        LOG_M_W(SENSOR, "iq bin not configured.");
        return AX_TRUE;
    }

    return LoadBinParams(pBinPath);
}

AX_BOOL CBaseSensor::LoadBinParams(const std::string &strBinName) {
    if (access(strBinName.c_str(), F_OK) != 0) {
        LOG_M(SENSOR, "(%s) not exist.", strBinName.c_str());
    }
    else {
        AX_S32 nRet = AX_ISP_LoadBinParams(m_nPipeID, strBinName.c_str());

        if (0 != nRet) {
            LOG_M_E(SENSOR, "AX_ISP_LoadBinParams (%s) failed, ret=0x%x.", strBinName.c_str(), nRet);
        }
        else {
            LOG_M(SENSOR, "AX_ISP_LoadBinParams (%s) successfully.", strBinName.c_str());

            return AX_TRUE;
        }
    }

    return AX_FALSE;
}

AX_BOOL CBaseSensor::Load3ABinParams(const std::string &strBinName) {
    if (access(strBinName.c_str(), F_OK) != 0) {
        LOG_M(SENSOR, "(%s) not exist.", strBinName.c_str());
    }
    else {
        AX_S32 nRet = AX_ISP_3A_LoadBinParams(m_nPipeID, strBinName.c_str());

        if (0 != nRet) {
            LOG_M_E(SENSOR, "AX_ISP_3A_LoadBinParams (%s) failed, ret=0x%x.", strBinName.c_str(), nRet);
        }
        else {
            LOG_M(SENSOR, "AX_ISP_3A_LoadBinParams (%s) successfully.", strBinName.c_str());

            return AX_TRUE;
        }
    }

    return AX_FALSE;
}
AX_S32 CBaseSensor::RegisterAeAlgLib(AX_U8 nPipe)
{
    std::unique_lock<std::mutex> lck(m_mtxSnsObj);

    std::string driverFilename;
    AX_LENS_ACTUATOR_IRIS_FUNC_T *st_pSnsDCIrisObj = nullptr;
    AX_LENS_ACTUATOR_IRIS_FUNC_T *st_pSnsIrisObj = nullptr;
    if (!m_pSnsObj)
    {
        LOG_M_E(SENSOR, "RegisterAeAlgLib: m_pSnsObj is null!");
        return -1;
    }
    AX_S32 nRet = 0;

    AX_ISP_AE_REGFUNCS_T tAeFuncs = {0};

    tAeFuncs.pfnAe_Init = AX_ISP_ALG_AeInit;
    tAeFuncs.pfnAe_Exit = AX_ISP_ALG_AeDeInit;
    tAeFuncs.pfnAe_Run = AX_ISP_ALG_AeRun;

    /* Register the sensor driven interface TO the AE library */
    nRet = AX_ISP_ALG_AeRegisterSensor(nPipe, m_pSnsObj);
    if (nRet)
    {
        LOG_M_E(SENSOR, "AX_ISP_ALG_AeRegisterSensor Failed, ret=0x%x", nRet);
        // return axRet;     /* FIX debug */
    }

    AX_VOID *tLensDCIrisObj[DEF_VIN_PIPE_MAX_NUM][LENS_OBJ_NUM_MAX] = {{AX_NULL}};
    AX_LENS_DRIVE_T tIniLensParams = gOptions.GetLensCfg();

    /* dciris */
    if (!tIniLensParams.strLensDCIrisLibName.empty())
    {
        driverFilename = tIniLensParams.strLensDCIrisLibName;
        m_pLensLenDCIrisLib[nPipe] = dlopen(driverFilename.c_str(), RTLD_LAZY);
        if (!m_pLensLenDCIrisLib[nPipe])
        {
            LOG_M(SENSOR, "Lens DCIris lib dlopen failed:%s, pipe:%d\n.", dlerror(), nPipe);
            return -1;
        }

        tLensDCIrisObj[nPipe][0] = dlsym(m_pLensLenDCIrisLib[nPipe], tIniLensParams.strLensDCIrisObjName.c_str());
        if (!tLensDCIrisObj[nPipe][0])
        {
            LOG_M(SENSOR, "Lens DCIris obj dlsym failed:%s\n", dlerror());
            return -1;
        }
        st_pSnsDCIrisObj = (AX_LENS_ACTUATOR_IRIS_FUNC_T *)tLensDCIrisObj[nPipe][0];
    }

    /* iris */
    if (!tIniLensParams.strLensIrisLibName.empty())
    {
        driverFilename = tIniLensParams.strLensIrisLibName;
        m_pLensLenIrisLib[nPipe] = dlopen(driverFilename.c_str(), RTLD_LAZY);
        if (!m_pLensLenIrisLib[nPipe])
        {
            LOG_M(SENSOR, "Lens Iris lib dlopen failed:%s, pipe:%d\n.", dlerror(), nPipe);
            return -1;
        }

        tLensDCIrisObj[nPipe][1] = dlsym(m_pLensLenIrisLib[nPipe], tIniLensParams.strLensIrisObjName.c_str());
        if (!tLensDCIrisObj[nPipe][1])
        {
            LOG_M(SENSOR, "Lens Iris obj dlsym failed:%s\n", dlerror());
            return -1;
        }
        st_pSnsIrisObj = (AX_LENS_ACTUATOR_IRIS_FUNC_T *)tLensDCIrisObj[nPipe][1];
    }

    /* Register Lens DCIRIS */
    if (st_pSnsDCIrisObj)
    {
        nRet = AX_ISP_ALG_AeRegisterLensIris(nPipe, st_pSnsDCIrisObj);
        if (nRet)
        {
            LOG_M_E(SENSOR, "AX_ISP_ALG_AeRegisterLensIris DCIRIS Failed, ret=0x%x.\n", nRet);
        }
        else
        {
            LOG_M(SENSOR, "Lens DCIRIS Register to AE success!\n");
        }
    }

    /* Register Lens IRIS */
    if (st_pSnsIrisObj)
    {
        nRet = AX_ISP_ALG_AeRegisterLensIris(nPipe, st_pSnsIrisObj);
        if (nRet)
        {
            LOG_M_E(SENSOR, "AX_ISP_ALG_AeRegisterLensIris IRIS Failed, ret=0x%x.\n", nRet);
        }
        else
        {
            LOG_M(SENSOR, "Lens IRIS Register to AE success!\n");
        }
    }

    /* Register ae alg */
    nRet = AX_ISP_RegisterAeLibCallback(nPipe, &tAeFuncs);
    if (nRet)
    {
        LOG_M_E(SENSOR, "AX_ISP_RegisterAeLibCallback Failed, ret=0x%x", nRet);
        return nRet;
    }

    return 0;
}

AX_S32 CBaseSensor::UnRegisterAeAlgLib(AX_U8 nPipe)
{
    AX_S32 nRet = 0;

    nRet = AX_ISP_ALG_AeUnRegisterSensor(nPipe);
    if (nRet)
    {
        LOG_M_E(SENSOR, "AX_ISP ae un register sensor Failed, ret=0x%x", nRet);
        return nRet;
    }

    if (m_pLensLenDCIrisLib[nPipe])
        dlclose(m_pLensLenDCIrisLib[nPipe]);
    if (m_pLensLenIrisLib[nPipe])
        dlclose(m_pLensLenIrisLib[nPipe]);

    nRet = AX_ISP_UnRegisterAeLibCallback(nPipe);
    if (nRet)
    {
        LOG_M_E(SENSOR, "AX_ISP Unregister Sensor Failed, ret=0x%x", nRet);
        return nRet;
    }

    return nRet;
}