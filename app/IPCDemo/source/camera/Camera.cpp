/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/

#include "Camera.h"
#include "global.h"
#include "OptionHelper.h"
#include "PrintHelper.h"
#include "StageOptionHelper.h"
#include "OS04a10.h"
#include "JsonCfgParser.h"
#include "CommonUtils.h"
#include "ax_isp_3a_api.h"

#include <stdio.h>
#include <iostream>
#include <fcntl.h>
#include <thread>

#define CAMERA "Camera"

#define HOT_NOISE_INVALID_HANDLE (-1)
#define HOT_NOISE_THERMAL_NODE_NAME "/sys/class/thermal/thermal_zone0/temp"

extern COptionHelper gOptions;
extern CPrintHelper  gPrintHelper;

AX_BOOL CCamera::s_bISPRestartRequired = AX_FALSE;
AX_BOOL CCamera::s_bResetDaynightModeRequired = AX_FALSE;
AX_BOOL CCamera::s_bResetNrModeRequired = AX_FALSE;
CCamera::CCamera()
    : m_eSnsID(E_SENSOR_ID_0)
    , m_nDevID(0)
    , m_nPipeID(0)
    , m_bRtpThreadRunning(AX_FALSE)
    , m_bRestarting(AX_FALSE)
    , m_bVinStreamOff(AX_FALSE)
    , m_pRtpThread(nullptr)
    , m_pSensorInstance(nullptr)
    , m_pIvpsStage(nullptr)
{
    for (AX_U8 i = 0; i < MAX_ISP_CHANNEL_NUM; ++i) {
        m_pYuvGetThread[i] = nullptr;
    }
}

CCamera::~CCamera()
{
}

AX_VOID CCamera::MediaFrameRelease(CMediaFrame *pMediaFrame)
{
    if (pMediaFrame) {
        AX_U32 nChn = pMediaFrame->nChannel;

        LOG_M_I(CAMERA, "[%d][%d] seq:%lld", m_nPipeID, pMediaFrame->nChannel, pMediaFrame->tFrame.tFrameInfo.stVFrame.u64SeqNum);

        m_mtxFrame[nChn].lock();
        for (list<CMediaFrame *>::iterator it = m_qFrame[nChn].begin(); it != m_qFrame[nChn].end(); it++ ) {
            if (*it == pMediaFrame) {
                m_qFrame[nChn].erase(it);
                AX_VIN_ReleaseYuvFrame(m_nPipeID, (AX_YUV_SOURCE_ID_E)nChn, &pMediaFrame->tFrame);

                delete pMediaFrame;

                gPrintHelper.UpdateQueueRemains(E_PH_MOD_CAMERA, nChn, m_qFrame[nChn].size());
                break;
            }
        }
        m_mtxFrame[nChn].unlock();

        gPrintHelper.Remove(E_PH_MOD_CAMERA, nChn);
    }
}

void CCamera::YuvGetThreadFunc(YUV_THREAD_PARAM_PTR pThreadParam)
{
    AX_U8 nPipeID = pThreadParam->nPipeID;
    AX_U8 nChn = pThreadParam->nIspChn;

    LOG_M(CAMERA, "[%d][%d] +++", nPipeID, nChn);

    AX_CHAR szName[50] = {0};
    sprintf(szName, "IPC_CAM%d_Get_%d", nPipeID, nChn);
    prctl(PR_SET_NAME, szName);

    AX_S32 nRet = 0;
    AX_S32 nSkipCnt = 50 * MAX_ISP_CHANNEL_NUM; /* Skip the head frames as sometimes they are not in correct orders */

    while (pThreadParam->bThreadRunning) {
        if (m_bVinStreamOff) {
            CTimeUtils::msSleep(10);
            continue;
        }

        CMediaFrame * pMediaFrame = new(std::nothrow) CMediaFrame();
        if (nullptr == pMediaFrame) {
            LOG_M_E(CAMERA, "Allocate buffer for YuvGetThread failed.");
            CTimeUtils::msSleep(10);
            continue;
        }

        nRet = AX_VIN_GetYuvFrame(nPipeID, (AX_YUV_SOURCE_ID_E)nChn, &pMediaFrame->tFrame, -1);
        if (AX_SDK_PASS != nRet) {
            if (pThreadParam->bThreadRunning) {
                LOG_M_E(CAMERA, "[%d] AX_VIN_GetYuvFrame failed, ret=0x%x, unreleased buffer=%d", nChn, nRet, m_qFrame[nChn].size());
            }
            SAFE_DELETE_PTR(pMediaFrame);
            continue;
        }

        LOG_M_I(CAMERA, "[%d][%d] Seq %llu, Size %d, blkID0:0x%x, blkID1:0x%x, Phy0=0x%llx Vir0=0x%llx, Phy1=0x%llx Vir1=0x%llx, w:%d h:%d stride:%d, PTS:%llu", m_nPipeID, nChn,
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u64SeqNum,
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u32FrameSize,
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u32BlkId[0],
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u32BlkId[1],
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u64PhyAddr[0],
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u64VirAddr[0],
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u64PhyAddr[1],
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u64VirAddr[1],
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u32Width,
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u32Height,
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u32PicStride[0],
                  pMediaFrame->tFrame.tFrameInfo.stVFrame.u64PTS);

        gPrintHelper.AddTimeSpan(E_PH_PIPE_PT_BASE, nChn, pMediaFrame->tFrame.tFrameInfo.stVFrame.u64SeqNum);
        gPrintHelper.AddTimeSpan(E_PH_PIPE_PT_CAMERA, nChn, pMediaFrame->tFrame.tFrameInfo.stVFrame.u64SeqNum);
        gPrintHelper.Add(E_PH_MOD_CAMERA, nPipeID, nChn);

        pMediaFrame->nPipeID = nPipeID;
        pMediaFrame->nChannel = nChn;
        pMediaFrame->nFrameID = pMediaFrame->tFrame.tFrameInfo.stVFrame.u64SeqNum;
        pMediaFrame->nFramePTS = pMediaFrame->tFrame.tFrameInfo.stVFrame.u64PTS;
        pMediaFrame->pFrameRelease = this;
        pMediaFrame->nStride = pMediaFrame->tFrame.tFrameInfo.stVFrame.u32Width;

        if (nSkipCnt > 0) {
            AX_VIN_ReleaseYuvFrame(nPipeID, (AX_YUV_SOURCE_ID_E)nChn, &pMediaFrame->tFrame);
            SAFE_DELETE_PTR(pMediaFrame);
            nSkipCnt--;
            continue;
        }

        m_mtxFrame[nChn].lock();
        if (m_qFrame[nChn].size() >= 6) {
            LOG_M_E(CAMERA, "[%d][%d] queue size is %d, drop this frame", nPipeID, nChn, m_qFrame[nChn].size());
            AX_VIN_ReleaseYuvFrame(nPipeID, (AX_YUV_SOURCE_ID_E)nChn, &pMediaFrame->tFrame);
            SAFE_DELETE_PTR(pMediaFrame);

            gPrintHelper.Remove(E_PH_MOD_CAMERA, nChn);

            m_mtxFrame[nChn].unlock();
            continue;
        }

        m_qFrame[nChn].push_back(pMediaFrame);
        m_mtxFrame[nChn].unlock();

        if (!m_pIvpsStage->EnqueueFrame(pMediaFrame)) {
            pMediaFrame->FreeMem();
        }
    }

    LOG_M(CAMERA, "[%d][%d] ---", nPipeID, nChn);
}

static AX_VOID RestartThreadFunc(AX_VOID *__this)
{
    CCamera* pCamera = (CCamera*)__this;

    pCamera->Restart();
}

void CCamera::ItpLoopThreadFunc()
{
    LOG_M(CAMERA, "[%d] +++", m_nPipeID);

    prctl(PR_SET_NAME, "IPC_CAM_ITP");

    AX_BOOL bNeedSleep = AX_TRUE;
    char *flag = getenv("IPC_ISPRUN_NO_SLEEP");
    if (flag) {
        AX_S32 nFlag = atoi(flag);
        if(nFlag == 1) {
            bNeedSleep = AX_FALSE;
        }
    }

    m_bRtpThreadRunning = AX_TRUE;
    while (m_bRtpThreadRunning) {
        if (CCamera::s_bISPRestartRequired) {
            CCamera::s_bISPRestartRequired = AX_FALSE;

            if (!m_bRestarting) {
                m_bRestarting = AX_TRUE;
                thread tr(RestartThreadFunc, this);
                tr.detach();
                break;
            } else {
                LOG_M(CAMERA, "Restart already in progress.");
            }
        }
        AX_ISP_Run(m_nPipeID);
        if (bNeedSleep) {
            CTimeUtils::nsSleep(1);
        }
    }

    LOG_M(CAMERA, "[%d] ---", m_nPipeID);
}

AX_BOOL CCamera::Init(AX_POOL_FLOORPLAN_T *stVbConf, AX_U8& nCount, AX_U8 nSensorID /*= E_SENSOR_ID_0*/, AX_U8 nDeviceID /*= 0*/, AX_U8 nPipeID /*= 0*/, AX_BOOL bUpdateAttrOnReset /*= AX_FALSE*/)
{
    LOG_M(CAMERA, "[%d][%d] +++", nSensorID, nPipeID);

    m_eSnsID = (SENSOR_ID_E)nSensorID;
    m_nDevID = nDeviceID;
    m_nPipeID = nPipeID;
    AX_BOOL bRet = AX_FALSE;

    m_pSensorInstance = CBaseSensor::NewInstance(nSensorID);    // 根据加载的 json 配置文件中配置的 sensor 类型，实例化不同的 sensor，并配置不同的 sensor出流所需属性
    if (m_pSensorInstance) {
        if (bUpdateAttrOnReset) {
            AX_SNS_HDR_MODE_E eNewSnsMode = CStageOptionHelper().GetInstance()->GetCamera().nHdrEnable == 1 ? AX_SNS_HDR_2X_MODE : AX_SNS_LINEAR_MODE;
            LOG_M(CAMERA, "Switch sensor mode: %d", eNewSnsMode);
            m_pSensorInstance->UpdateSnsMode(eNewSnsMode);

            AX_U8 nFramerate = CStageOptionHelper().GetInstance()->GetCamera().nFramerate;
            LOG_M(CAMERA, "Update sensor framerate: %d", nFramerate);
            UpdateFramerate(nFramerate);
        }

        bRet = m_pSensorInstance->Init(nSensorID, nDeviceID, nPipeID, stVbConf, nCount);
        if (!bRet) {
            LOG_M_E(CAMERA, "Init camera failed.");
        }
    } else {
        LOG_M_E(CAMERA, "Init sensor instance failed.");
    }

    if (!bRet && m_pSensorInstance) {
        SAFE_DELETE_PTR(m_pSensorInstance);
    }

    LOG_M(CAMERA, "[%d][%d]---", nSensorID, nPipeID);
    return bRet;
}

AX_BOOL CCamera::Open()
{
    LOG_M(CAMERA, "[%d][%d] +++", m_eSnsID, m_nPipeID);

    AX_BOOL bRet = AX_FALSE;

    if (m_pSensorInstance) {
        bRet = m_pSensorInstance->Open();
        if (!bRet) {
            LOG_M_E(CAMERA, "Open camera failed.");
        }
    }

    if (CCamera::s_bResetDaynightModeRequired) {
        AX_DAYNIGHT_MODE_E eDaynightMode = (AX_DAYNIGHT_MODE_E)CStageOptionHelper().GetInstance()->GetCamera().nDaynightMode;
        ChangeDaynightMode(eDaynightMode);
    }

    if (CCamera::s_bResetNrModeRequired) {
        AX_U8 nNrMode = CStageOptionHelper().GetInstance()->GetCamera().nNrMode;
        ChangeNrMode(nNrMode);
    }

    LOG_M(CAMERA, "[%d][%d] ---", m_eSnsID, m_nPipeID);
    return bRet;
}

AX_BOOL CCamera::Close()
{
    LOG_M(CAMERA, "[%d][%d] +++", m_eSnsID, m_nPipeID);

    if (m_pSensorInstance) {
        m_pSensorInstance->Close();
        SAFE_DELETE_PTR(m_pSensorInstance);
    }

    ResetHotNoiseBalance();

    ClearQFrame();

    LOG_M(CAMERA, "[%d][%d] ---", m_eSnsID, m_nPipeID);
    return AX_TRUE;
}

AX_BOOL CCamera::Start()
{
    LOG_M(CAMERA, "[%d][%d] +++", m_eSnsID, m_nPipeID);

    m_bVinStreamOff = AX_FALSE;
    m_bRtpThreadRunning = AX_TRUE;
    m_pRtpThread = new thread(&CCamera::ItpLoopThreadFunc, this);

    CTimeUtils::msSleep(50);

    for (AX_U8 i = 0; i < MAX_ISP_CHANNEL_NUM; ++i) {
        m_tYUVThreadParam[i].nPipeID = m_nPipeID;
        m_tYUVThreadParam[i].nIspChn = i;
        m_tYUVThreadParam[i].bThreadRunning = AX_FALSE;

        if (!gOptions.IsLinkMode()) {
            m_tYUVThreadParam[i].bValid = AX_TRUE;
        }
    }

    for (AX_U8 i = 0; i < MAX_ISP_CHANNEL_NUM; ++i) {
        if (m_tYUVThreadParam[i].bValid) {
            m_tYUVThreadParam[i].bThreadRunning = AX_TRUE;
            m_pYuvGetThread[i] = new thread(&CCamera::YuvGetThreadFunc, this, &m_tYUVThreadParam[i]);
        }
    }

    StartHotNoiseBalance();

    LOG_M(CAMERA, "[%d][%d] ---", m_eSnsID, m_nPipeID);

    return m_pRtpThread ? AX_TRUE : AX_FALSE;
}

AX_VOID CCamera::Stop()
{
    LOG_M(CAMERA, "[%d][%d] +++", m_eSnsID, m_nPipeID);

    for (AX_U8 i = 0; i < MAX_ISP_CHANNEL_NUM; i ++) {
        m_tYUVThreadParam[i].bThreadRunning = AX_FALSE;
        if (m_pYuvGetThread[i]) {
            m_pYuvGetThread[i]->join();
            SAFE_DELETE_PTR(m_pYuvGetThread[i]);
        }
    }

    m_bRtpThreadRunning = AX_FALSE;

    if (m_pRtpThread) {
        m_pRtpThread->join();
        SAFE_DELETE_PTR(m_pRtpThread)
    }

    LOG_M(CAMERA, "[%d][%d] ---", m_eSnsID, m_nPipeID);
}

AX_VOID CCamera::RestartNotify()
{
    s_bISPRestartRequired = AX_TRUE;
}

AX_VOID CCamera::Restart(AX_VOID)
{
    LOG_M(CAMERA, "Stopping ISP...");
    Stop();
    Close();

    LOG_M(CAMERA, "ISP stopped!");

    CTimeUtils::msSleep(100);

    LOG_M(CAMERA, "Starting ISP...");

    AX_POOL_FLOORPLAN_T tVBConfig = {0};
    AX_U8 nPoolCount = 0;
    Init(&tVBConfig, nPoolCount, m_eSnsID, m_nDevID, m_nPipeID, AX_TRUE);

    if (Open() && Start()) {
        LOG_M(CAMERA, "ISP started!");
    } else {
        LOG_M(CAMERA, "ISP start failed!");
    }

    m_bRestarting = AX_FALSE;
}

AX_VOID CCamera::ResetFPS(AX_U32 nFPS)
{
    AX_S32 nRet = AX_SUCCESS;

    m_bVinStreamOff = AX_TRUE;

    nRet = AX_VIN_StreamOff(m_nPipeID);
    if (AX_SUCCESS != nRet) {
        LOG_M_E(CAMERA, "AX_VIN_StreamOff failed, ret=0x%x.", nRet);
    }

    nRet = AX_MIPI_RX_Reset((AX_MIPI_RX_DEV_E)m_nPipeID);
    if (AX_SUCCESS != nRet) {
        LOG_M_E(CAMERA, "AX_MIPI_RX_Reset failed, ret=0x%x.", nRet);
    }

    m_pSensorInstance->ExitSensor(m_nPipeID);

    AX_SNS_ATTR_T tAttr;
    m_pSensorInstance->GetSnsAttr(tAttr);
    tAttr.nFrameRate = nFPS;
    AX_VIN_SetSnsAttr(m_nPipeID, &tAttr);
    LOG_M(CAMERA, "Update sensor framerate: %d", nFPS);

    nRet = AX_MIPI_RX_Start((AX_MIPI_RX_DEV_E)m_nPipeID);
    if (AX_SUCCESS != nRet) {
        LOG_M_E(CAMERA, "AX_MIPI_RX_Start failed, ret=0x%x.", nRet);
    }

    m_pSensorInstance->ResetSensor(m_nPipeID);
    m_pSensorInstance->InitSensor(m_nPipeID);
    nRet = AX_VIN_StreamOn(m_nPipeID);
    if (AX_SUCCESS != nRet) {
        LOG_M_E(CAMERA, "AX_VIN_StreamOn failed, ret=0x%x.", nRet);
    } else {
        m_bVinStreamOff = AX_FALSE;
    }
}

AX_VOID CCamera::DaynightChangeNotify(AX_DAYNIGHT_MODE_E eDaynightMode, AX_BOOL bImmediately)
{
    if (bImmediately) {
        ChangeDaynightMode(eDaynightMode);
    }

    /* Once daynight mode is changed, each time camera is reopened will use the setting in CStageOptionHelper */
    s_bResetDaynightModeRequired = AX_TRUE;
}

AX_VOID CCamera::NrModeChangeNotify(AX_U8 nNrMode, AX_BOOL bImmediately)
{
    if (bImmediately) {
        ChangeNrMode(nNrMode);
    }

    /* Once nr mode is changed, each time camera is reopened will use the setting in CStageOptionHelper */
    s_bResetNrModeRequired = AX_TRUE;
}

AX_VOID CCamera::ChangeNrMode(AX_U8 nNrMode)
{
    AX_ISP_IQ_NPU_PARAM_T tNpuParam;
    memset(&tNpuParam, 0, sizeof(AX_ISP_IQ_NPU_PARAM_T));
    AX_S32 nRet = AX_ISP_IQ_GetNpuParam(AX_PIPE_ID,  &tNpuParam);
    if (0 != nRet) {
        LOG_M_E(CAMERA, "AX_ISP_IQ_GetNpuParam failed, ret=0x%x.", nRet);
        return;
    }

    tNpuParam.bNrEnable = nNrMode;
    tNpuParam.bUpdateTable = 1;
    nRet = AX_ISP_IQ_SetNpuParam(AX_PIPE_ID,  &tNpuParam);
    if (0 != nRet) {
        LOG_M_E(CAMERA, "AX_ISP_IQ_SetNpuParam failed, ret=0x%x.", nRet);
        return;
    }

    LOG_M(CAMERA, "Update nr mode: %s", 0 == nNrMode ? "Dummy" : "NR");
}

AX_VOID CCamera::ChangeDaynightMode(AX_DAYNIGHT_MODE_E eDaynightMode)
{
    for (AX_U8 nChn = 0; nChn < MAX_ISP_CHANNEL_NUM; nChn++) {
        AX_S32 nRet = AX_VIN_SetChnDayNightMode(AX_PIPE_ID, nChn, eDaynightMode);
        if (0 != nRet) {
            LOG_M_E(CAMERA, "[%d] AX_VIN_SetChnDayNightMode failed, ret=0x%x.", nChn, nRet);
        }
    }

    LOG_M(CAMERA, "Update daynight mode: %s", AX_DAYNIGHT_MODE_DAY == eDaynightMode ? "day" : "night");
}

AX_BOOL CCamera::DoEISSwitch(AX_BOOL bIsEISOn)
{
    AX_ISP_IQ_EIS_PARAM_T tEISParam;
    AX_S32 nRet = 0;

    if (gOptions.IsEnableEIS() != bIsEISOn) {
        gOptions.SetEISEnable(bIsEISOn);
    } else {
        return AX_TRUE;
    }

    nRet = AX_ISP_IQ_GetEisParam(AX_PIPE_ID, &tEISParam);
    if (0 != nRet) {
        LOG_M_E(CAMERA, "AX_ISP_IQ_GetEisParam failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    tEISParam.bEisEnable    = bIsEISOn;
    tEISParam.nEisDelayNum  = gOptions.GetEISDelayNum();
    tEISParam.nCropRatioW   = gOptions.GetEISCropW();
    tEISParam.nCropRatioH   = gOptions.GetEISCropH();
    nRet = AX_ISP_IQ_SetEisParam(AX_PIPE_ID, &tEISParam);
    if (0 != nRet) {
        LOG_M_E(CAMERA, "AX_ISP_IQ_SetEisParam failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    if (tEISParam.bEisEnable) {
        LOG_M(CAMERA, "EIS Model: %s", tEISParam.tEisNpuParam.szModelName);
        LOG_M(CAMERA, "Wbt Model: %s", tEISParam.tEisNpuParam.szWbtModelName);
    }

    return AX_TRUE;
}

AX_VOID CCamera::BindIvpsStage(CIVPSStage* pStage)
{
    m_pIvpsStage = pStage;
}

AX_VOID CCamera::ClearQFrame()
{
    LOG_M(CAMERA, "[%d] +++", m_nPipeID);
    CMediaFrame *pMediaFrame = nullptr;

    for (AX_S32 i = 0; i < MAX_ISP_CHANNEL_NUM; i++) {
        m_mtxFrame[i].lock();
        for (list<CMediaFrame *>::iterator it = m_qFrame[i].begin(); it != m_qFrame[i].end(); it++ ) {
            pMediaFrame = *it;
            if ( pMediaFrame ) {
                AX_VIN_ReleaseYuvFrame(m_nPipeID, (AX_YUV_SOURCE_ID_E)i, &pMediaFrame->tFrame);
                delete pMediaFrame;
            }
        }
        m_qFrame[i].clear();
        m_mtxFrame[i].unlock();
    }
    LOG_M(CAMERA, "[%d] ---", m_nPipeID);
}

AX_SNS_ATTR_T CCamera::GetSnsAttr()
{
    AX_SNS_ATTR_T tSnsAttr;
    memset(&tSnsAttr, 0, sizeof(AX_SNS_ATTR_T));

    if (m_pSensorInstance) {
        if (m_pSensorInstance->GetSnsAttr(tSnsAttr)) {
            return tSnsAttr;
        }
    }

    return tSnsAttr;
}

AX_VIN_CHN_ATTR_T CCamera::GetChnAttr()
{
    AX_VIN_CHN_ATTR_T tChnAttr;
    memset(&tChnAttr, 0, sizeof(AX_VIN_CHN_ATTR_T));

    if (m_pSensorInstance) {
        if (m_pSensorInstance->GetChnAttr(tChnAttr)) {
            return tChnAttr;
        }
    }

    return tChnAttr;
}

AX_VOID CCamera::UpdateFramerate(AX_F32 fFramerate)
{
    if (m_pSensorInstance) {
        m_pSensorInstance->UpdateFramerate(fFramerate);
    }
}

AX_VOID CCamera::SwitchSdrNrModeForHotbalance(AX_U8 nNrMode)
{
    if (m_pSensorInstance) {
        SENSOR_CONFIG_T tSensorCfg;
        CConfigParser().GetInstance()->GetCameraCfg(tSensorCfg, (SENSOR_ID_E)m_eSnsID);

        if (nNrMode == 0) {
            m_pSensorInstance->LoadBinParams(tSensorCfg.aHotbalanceModeBin);
        }
        else {
            m_pSensorInstance->LoadBinParams(tSensorCfg.aNormalModeBin);
        }
    }
}

AX_VOID CCamera::UpdateAeForHotbalance(AX_BOOL bHotBalance)
{
    AX_ISP_IQ_AE_PARAM_T tIspAeParam;

    memset(&tIspAeParam, 0x00, sizeof(tIspAeParam));

    AX_S32 nRet = AX_ISP_IQ_GetAeParam(m_nPipeID, &tIspAeParam);

    if (0 != nRet) {
        LOG_M_E(CAMERA, "AX_ISP_IQ_GetAeParam failed, ret=0x%x.", nRet);
        return;
    }

    AX_ISP_IQ_AE_STATE_MACHINE_T  *pStaMacParam        = &(tIspAeParam.tAeAlgAuto.tTimeSmoothParam.tStateMachineParam);
    AX_ISP_IQ_AE_CONVERGE_SPEED_T *pConvergeSpeedParam = &(tIspAeParam.tAeAlgAuto.tTimeSmoothParam.tConvergeSpeedParam);

    if (bHotBalance) {
        /* change param to HotBalance */
        pStaMacParam->nToFastLumaThOver   = 3072;
        pStaMacParam->nToFastLumaThUnder  = 614;
        pStaMacParam->nToSlowLumaThOver   = 102;
        pStaMacParam->nToSlowLumaThUnder  = 307;
        pStaMacParam->nToSlowFrameTh      = 5;
        pStaMacParam->nToConvergedFrameTh = 5;

        AX_U32 fastOverlumaDiffThList[]      = { 10240, 20480, 30720, 51200, 153600, 209920, 262144};
        AX_U32 fastOverStepFactorList[]      = { 51, 51, 154, 205, 512, 512, 819};
        AX_U32 fastOverSpeedDownFactorList[] = { 0, 0, 0, 102, 102, 307, 410};
        AX_U32 fastOverSkipFrList[]          = { 0, 0, 0, 0, 0, 0, 0};
        AX_S32 fastOverKneeNum = sizeof(fastOverlumaDiffThList) / sizeof(fastOverlumaDiffThList[0]);

        AX_U32 fastUnderlumaDiffThList[]      = {10240, 20480, 30720, 51200, 153600, 209920, 262144};
        AX_U32 fastUnderStepFactorList[]      = {32, 32, 32, 102, 410, 410, 410};
        AX_U32 fastUnderSpeedDownFactorList[] = {0, 0, 0, 102, 102, 307, 307};
        AX_U32 fastUnderSkipFrList[]          = {0, 0, 0, 0, 0, 0, 0};
        AX_S32 fastUnderKneeNum = sizeof(fastUnderlumaDiffThList) / sizeof(fastUnderlumaDiffThList[0]);

        AX_U32 slowOverlumaDiffThList[]      = {5120, 10240, 15360, 20480, 25600, 30720, 262144};
        AX_U32 slowOverStepFactorList[]      = {20, 20, 20, 20, 20, 31, 154};
        AX_U32 slowOverSpeedDownFactorList[] = {0, 0, 0, 0, 0, 0, 0};
        AX_U32 slowOverSkipFrList[]          = {0, 0, 0, 0, 0, 0, 0};
        AX_S32 slowOverKneeNum = sizeof(slowOverlumaDiffThList) / sizeof(slowOverlumaDiffThList[0]);

        AX_U32 slowUnderlumaDiffThList[]      = {5120, 10240, 15360, 20480, 25600, 30720, 262144};
        AX_U32 slowUnderStepFactorList[]      = {20, 20, 20, 20, 20, 31, 154};
        AX_U32 slowUnderSpeedDownFactorList[] = {0, 0, 0, 0, 0, 0, 0};
        AX_U32 slowUnderSkipFrList[]          = {0, 0, 0, 0, 0, 0, 0};
        AX_S32 slowUnderKneeNum = sizeof(slowUnderlumaDiffThList) / sizeof(slowUnderlumaDiffThList[0]);

        pConvergeSpeedParam->nFastOverKneeCnt = fastOverKneeNum;
        for (int i = 0; i < fastOverKneeNum; i++) {
            pConvergeSpeedParam->nFastOverLumaDiffList[i]        = fastOverlumaDiffThList[i];
            pConvergeSpeedParam->nFastOverStepFactorList[i]      = fastOverStepFactorList[i];
            pConvergeSpeedParam->nFastOverSpeedDownFactorList[i] = fastOverSpeedDownFactorList[i];
            pConvergeSpeedParam->nFastOverSkipList[i]            = fastOverSkipFrList[i];
        }

        pConvergeSpeedParam->nFastUnderKneeCnt = fastUnderKneeNum;
        for (int i = 0; i < fastUnderKneeNum; i++) {
            pConvergeSpeedParam->nFastUnderLumaDiffList[i]        = fastUnderlumaDiffThList[i];
            pConvergeSpeedParam->nFastUnderStepFactorList[i]      = fastUnderStepFactorList[i];
            pConvergeSpeedParam->nFastUnderSpeedDownFactorList[i] = fastUnderSpeedDownFactorList[i];
            pConvergeSpeedParam->nFastUnderSkipList[i]            = fastUnderSkipFrList[i];
        }

        pConvergeSpeedParam->nSlowOverKneeCnt = slowOverKneeNum;
        for (int i = 0; i < slowOverKneeNum; i++) {
            pConvergeSpeedParam->nSlowOverLumaDiffList[i]        = slowOverlumaDiffThList[i];
            pConvergeSpeedParam->nSlowOverStepFactorList[i]      = slowOverStepFactorList[i];
            pConvergeSpeedParam->nSlowOverSpeedDownFactorList[i] = slowOverSpeedDownFactorList[i];
            pConvergeSpeedParam->nSlowOverSkipList[i]            = slowOverSkipFrList[i];
        }

        pConvergeSpeedParam->nSlowUnderKneeCnt = slowUnderKneeNum;
        for (int i = 0; i < slowUnderKneeNum; i++) {
            pConvergeSpeedParam->nSlowUnderLumaDiffList[i]        = slowUnderlumaDiffThList[i];
            pConvergeSpeedParam->nSlowUnderStepFactorList[i]      = slowUnderStepFactorList[i];
            pConvergeSpeedParam->nSlowUnderSpeedDownFactorList[i] = slowUnderSpeedDownFactorList[i];
            pConvergeSpeedParam->nSlowUnderSkipList[i]            = slowUnderSkipFrList[i];
        }
    }
    else {
        /* change param to normal mode */
        pStaMacParam->nToFastLumaThOver   = 3072;
        pStaMacParam->nToFastLumaThUnder  = 614;
        pStaMacParam->nToSlowLumaThOver   = 102;
        pStaMacParam->nToSlowLumaThUnder  = 307;
        pStaMacParam->nToSlowFrameTh      = 5;
        pStaMacParam->nToConvergedFrameTh = 5;

        AX_U32 fastOverlumaDiffThList[]      = { 10240, 20480, 30720, 51200, 153600, 209920, 262144};
        AX_U32 fastOverStepFactorList[]      = { 51, 51, 154, 205, 512, 512, 819};
        AX_U32 fastOverSpeedDownFactorList[] = { 0, 0, 0, 102, 102, 307, 410};
        AX_U32 fastOverSkipFrList[]          = { 0, 0, 0, 0, 0, 0, 0};
        AX_S32 fastOverKneeNum = sizeof(fastOverlumaDiffThList) / sizeof(fastOverlumaDiffThList[0]);

        AX_U32 fastUnderlumaDiffThList[]      = {10240, 20480, 30720, 51200, 153600, 209920, 262144};
        AX_U32 fastUnderStepFactorList[]      = {32, 32, 32, 102, 410, 410, 410};
        AX_U32 fastUnderSpeedDownFactorList[] = {0, 0, 0, 102, 102, 307, 307};
        AX_U32 fastUnderSkipFrList[]          = {0, 0, 0, 0, 0, 0, 0};
        AX_S32 fastUnderKneeNum = sizeof(fastUnderlumaDiffThList) / sizeof(fastUnderlumaDiffThList[0]);

        AX_U32 slowOverlumaDiffThList[]      = {5120, 10240, 15360, 20480, 25600, 30720, 262144};
        AX_U32 slowOverStepFactorList[]      = {20, 20, 20, 20, 20, 31, 154};
        AX_U32 slowOverSpeedDownFactorList[] = {0, 0, 0, 0, 0, 0, 0};
        AX_U32 slowOverSkipFrList[]          = {0, 0, 0, 0, 0, 0, 0};
        AX_S32 slowOverKneeNum = sizeof(slowOverlumaDiffThList) / sizeof(slowOverlumaDiffThList[0]);

        AX_U32 slowUnderlumaDiffThList[]      = {5120, 10240, 15360, 20480, 25600, 30720, 262144};
        AX_U32 slowUnderStepFactorList[]      = {20, 20, 20, 20, 20, 31, 154};
        AX_U32 slowUnderSpeedDownFactorList[] = {0, 0, 0, 0, 0, 0, 0};
        AX_U32 slowUnderSkipFrList[]          = {0, 0, 0, 0, 0, 0, 0};
        AX_S32 slowUnderKneeNum = sizeof(slowUnderlumaDiffThList) / sizeof(slowUnderlumaDiffThList[0]);

        pConvergeSpeedParam->nFastOverKneeCnt = fastOverKneeNum;
        for (int i = 0; i < fastOverKneeNum; i++) {
            pConvergeSpeedParam->nFastOverLumaDiffList[i]        = fastOverlumaDiffThList[i];
            pConvergeSpeedParam->nFastOverStepFactorList[i]      = fastOverStepFactorList[i];
            pConvergeSpeedParam->nFastOverSpeedDownFactorList[i] = fastOverSpeedDownFactorList[i];
            pConvergeSpeedParam->nFastOverSkipList[i]            = fastOverSkipFrList[i];
        }

        pConvergeSpeedParam->nFastUnderKneeCnt = fastUnderKneeNum;
        for (int i = 0; i < fastUnderKneeNum; i++) {
            pConvergeSpeedParam->nFastUnderLumaDiffList[i]        = fastUnderlumaDiffThList[i];
            pConvergeSpeedParam->nFastUnderStepFactorList[i]      = fastUnderStepFactorList[i];
            pConvergeSpeedParam->nFastUnderSpeedDownFactorList[i] = fastUnderSpeedDownFactorList[i];
            pConvergeSpeedParam->nFastUnderSkipList[i]            = fastUnderSkipFrList[i];
        }

        pConvergeSpeedParam->nSlowOverKneeCnt = slowOverKneeNum;
        for (int i = 0; i < slowOverKneeNum; i++) {
            pConvergeSpeedParam->nSlowOverLumaDiffList[i]        = slowOverlumaDiffThList[i];
            pConvergeSpeedParam->nSlowOverStepFactorList[i]      = slowOverStepFactorList[i];
            pConvergeSpeedParam->nSlowOverSpeedDownFactorList[i] = slowOverSpeedDownFactorList[i];
            pConvergeSpeedParam->nSlowOverSkipList[i]            = slowOverSkipFrList[i];
        }

        pConvergeSpeedParam->nSlowUnderKneeCnt = slowUnderKneeNum;
        for (int i = 0; i < slowUnderKneeNum; i++) {
            pConvergeSpeedParam->nSlowUnderLumaDiffList[i]        = slowUnderlumaDiffThList[i];
            pConvergeSpeedParam->nSlowUnderStepFactorList[i]      = slowUnderStepFactorList[i];
            pConvergeSpeedParam->nSlowUnderSpeedDownFactorList[i] = slowUnderSpeedDownFactorList[i];
            pConvergeSpeedParam->nSlowUnderSkipList[i]            = slowUnderSkipFrList[i];
        }
    }

    nRet = AX_ISP_IQ_SetAeParam(m_nPipeID, &tIspAeParam);

    if (0 != nRet) {
        LOG_M_E(CAMERA, "AX_ISP_IQ_SetAeParam failed, ret=0x%x.", nRet);
        return;
    }
}

AX_S32 CCamera::ThreadThermalMonitor(AX_VOID)
{
    AX_S32 nThermalHandle = HOT_NOISE_INVALID_HANDLE;

    SENSOR_CONFIG_T tSensorCfg;
    CConfigParser().GetInstance()->GetCameraCfg(tSensorCfg, (SENSOR_ID_E)m_eSnsID);

    prctl(PR_SET_NAME, "APP_SnSThermalMonitor");

    m_bHotNoiseBalanceThreadRunning = AX_TRUE;

    while (m_bHotNoiseBalanceThreadRunning && m_tHotNoiseBalance.bEnable) {
        if (gOptions.GetSnsHotNoiseBalanceTest()) {
            CTimeUtils::msSleep(60000);

            if (!m_bHotNoiseBalanceThreadRunning || !m_tHotNoiseBalance.bEnable) {
                break;
            }
        }

        // wait sensor ready
        if (!m_pSensorInstance) {
            CTimeUtils::msSleep(10000);
            continue;
        }

        AX_F32 fTemperature = 0;

        // get sensor temperature
        if (!GetSnsTemperature(fTemperature)) {
            // get temperature from chip
            if (nThermalHandle == HOT_NOISE_INVALID_HANDLE) {
                nThermalHandle = open(HOT_NOISE_THERMAL_NODE_NAME, O_RDONLY);
            }

            if (nThermalHandle != HOT_NOISE_INVALID_HANDLE) {
                AX_CHAR strThermal[50] = {0};

                lseek(nThermalHandle, 0, SEEK_SET);

                if (read(nThermalHandle, strThermal, 50) > 0) {
                    AX_S32 nThermal = atoi(strThermal);
                    fTemperature = (AX_F32)nThermal/1000.0;
                }
                else {
                    LOG_M(CAMERA, "read %s fail", HOT_NOISE_THERMAL_NODE_NAME);

                    close(nThermalHandle);

                    nThermalHandle = HOT_NOISE_INVALID_HANDLE;
                }
            }
            else {
                LOG_M(CAMERA, "open %s fail", HOT_NOISE_THERMAL_NODE_NAME);
            }
        }

        // process
        ProcessThermal(fTemperature, tSensorCfg);

        if (!gOptions.GetSnsHotNoiseBalanceTest()) {
            CTimeUtils::msSleep(5000);
        }
    }

    if (HOT_NOISE_INVALID_HANDLE != nThermalHandle) {
        close(nThermalHandle);
    }

    m_bHotNoiseBalanceThreadRunning = AX_FALSE;

    return 0;
}

AX_BOOL CCamera::ProcessThermal(AX_F32 fThermal, SENSOR_CONFIG_T &tSensorCfg)
{
    if (m_tHotNoiseBalance.bEnable) {
        if (!gOptions.GetSnsHotNoiseBalanceTest()) {
            if (fThermal >= m_tHotNoiseBalance.fThersholdM + m_tHotNoiseBalance.fGap) {
                // Escape
                if (!m_tHotNoiseBalance.bEscape) {
                    LOG_M(CAMERA, "Thermal: %.3f", fThermal);

                    std::string strBinName;
                    if (CStageOptionHelper().GetInstance()->GetCamera().nHdrEnable == 1) {
                        strBinName = (AX_CHAR *)tSensorCfg.aHdrHotNoiseBalanceModeBin;
                    }
                    else {
                        strBinName = (AX_CHAR *)tSensorCfg.aSdrHotNoiseBalanceModeBin;
                    }

                    if (m_pSensorInstance
                        && m_pSensorInstance->LoadBinParams(strBinName)) {
                        std::unique_lock<std::mutex> lck(m_mtxHotNoiseBalance);
                        m_tHotNoiseBalance.bEscape = AX_TRUE;
                    }
                }
            }
            else if (fThermal <= m_tHotNoiseBalance.fThersholdL - m_tHotNoiseBalance.fGap) {
                // Recovery
                if (m_tHotNoiseBalance.bEscape) {
                    LOG_M(CAMERA, "Thermal: %.3f", fThermal);

                    std::string strBinName;
                    if (CStageOptionHelper().GetInstance()->GetCamera().nHdrEnable == 1) {
                        strBinName = (AX_CHAR *)tSensorCfg.aHdrHotNoiseNormalModeBin;
                    }
                    else {
                        strBinName = (AX_CHAR *)tSensorCfg.aSdrHotNoiseNormalModeBin;
                    }

                    if (m_pSensorInstance
                        && m_pSensorInstance->LoadBinParams(strBinName)) {
                        std::unique_lock<std::mutex> lck(m_mtxHotNoiseBalance);
                        m_tHotNoiseBalance.bEscape = AX_FALSE;
                    }
                }
            }
        }
        else {
            // Escape
            if (!m_tHotNoiseBalance.bEscape) {
                LOG_M(CAMERA, "Thermal: %.3f", fThermal);

                std::string strBinName;
                if (CStageOptionHelper().GetInstance()->GetCamera().nHdrEnable == 1) {
                    strBinName = (AX_CHAR *)tSensorCfg.aHdrHotNoiseBalanceModeBin;
                }
                else {
                    strBinName = (AX_CHAR *)tSensorCfg.aSdrHotNoiseBalanceModeBin;
                }

                if (m_pSensorInstance
                    && m_pSensorInstance->LoadBinParams(strBinName)) {
                    std::unique_lock<std::mutex> lck(m_mtxHotNoiseBalance);
                    m_tHotNoiseBalance.bEscape = AX_TRUE;
                }
            }
            else {
                LOG_M(CAMERA, "Thermal: %.3f", fThermal);

                std::string strBinName;
                if (CStageOptionHelper().GetInstance()->GetCamera().nHdrEnable == 1) {
                    strBinName = (AX_CHAR *)tSensorCfg.aHdrHotNoiseNormalModeBin;
                }
                else {
                    strBinName = (AX_CHAR *)tSensorCfg.aSdrHotNoiseNormalModeBin;
                }

                if (m_pSensorInstance
                    && m_pSensorInstance->LoadBinParams(strBinName)) {
                    std::unique_lock<std::mutex> lck(m_mtxHotNoiseBalance);
                    m_tHotNoiseBalance.bEscape = AX_FALSE;
                }
            }
        }
    }

    return AX_TRUE;
}

AX_BOOL CCamera::StartHotNoiseBalance(AX_VOID)
{
    CConfigParser().GetInstance()->GetCameraHotNoiseBalanceCfg(m_tHotNoiseBalance, (SENSOR_ID_E)m_eSnsID);

    if (!m_tHotNoiseBalance.bEnable) {
        LOG_M(CAMERA, "Sensor hot noise balance is disabled");

        return AX_FALSE;
    }

    if (!m_bHotNoiseBalanceThreadRunning) {
        LOG_M(CAMERA, "Sensor hot noise balance monitor start: (M:%.2f,L:%.2f,Gap:%.2f)",
                        m_tHotNoiseBalance.fThersholdM,
                        m_tHotNoiseBalance.fThersholdL,
                        m_tHotNoiseBalance.fGap);

        std::thread t(&CCamera::ThreadThermalMonitor, this);
        t.detach();
    }

    return AX_FALSE;
}

AX_VOID CCamera::ResetHotNoiseBalance(AX_VOID)
{
    std::unique_lock<std::mutex> lck(m_mtxHotNoiseBalance);
    m_tHotNoiseBalance.bEscape = AX_FALSE;
}

AX_BOOL CCamera::GetSnsTemperature(AX_F32 &fTemperature)
{
    if (m_pSensorInstance) {
        return m_pSensorInstance->GetSnsTemperature(fTemperature);
    }

    return AX_FALSE;
}

AX_U8 CCamera::GetShutterMode(AX_VOID)
{
    AX_ISP_IQ_AE_PARAM_T tIspAeParam;

    memset(&tIspAeParam, 0x00, sizeof(tIspAeParam));

    AX_S32 nRet = AX_ISP_IQ_GetAeParam(m_nPipeID, &tIspAeParam);

    if (0 != nRet) {
        LOG_M_E(CAMERA, "AX_ISP_IQ_GetAeParam failed, ret=0x%x.", nRet);
        return 0;
    }

    /* 0: FIX FRAME RATE MODE; 1: SLOW SHUTTER MODE */
    return tIspAeParam.tAeAlgAuto.tSlowShutterParam.nFrameRateMode;
}

AX_BOOL CCamera::SetShutterMode(AX_U8 nShutterMode)
{
    AX_ISP_IQ_AE_PARAM_T tIspAeParam;

    LOG_M(CAMERA, "set shutter mode: %d", nShutterMode);

    memset(&tIspAeParam, 0x00, sizeof(tIspAeParam));

    AX_S32 nRet = AX_ISP_IQ_GetAeParam(m_nPipeID, &tIspAeParam);

    if (0 != nRet) {
        LOG_M_E(CAMERA, "AX_ISP_IQ_GetAeParam failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    /* 0: FIX FRAME RATE MODE; 1: SLOW SHUTTER MODE */
    tIspAeParam.tAeAlgAuto.tSlowShutterParam.nFrameRateMode = nShutterMode;

    nRet = AX_ISP_IQ_SetAeParam(m_nPipeID, &tIspAeParam);

    if (0 != nRet) {
        LOG_M_E(CAMERA, "AX_ISP_IQ_SetAeParam failed, ret=0x%x.", nRet);
        return AX_FALSE;
    }

    return AX_TRUE;
}
