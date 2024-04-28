/**********************************************************************************
 *
 * Copyright (c) 2019-2022 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/
#include "Scd.h"
#include <string>
#include <string.h>
#include "AppLog.h"
#include "inifile.h"

#define SCD "SCD"

AX_VOID CSCD::SetWebServer(CWebServer *pWebServer) {
    m_pWebServer = pWebServer;
}

AX_BOOL CSCD::LoadConfig(AX_VOID) {
    inifile::IniFile f;
    std::string strIniFile{"./config/ive.conf"};
    AX_S32 ret = f.Load(strIniFile);
    if (0 != ret) {
        LOG_M_E(SCD, "load %s fail", strIniFile.c_str());
        return AX_FALSE;
    }

    f.GetIntValueOrDefault("SCD", "threshold", &m_nThrd, 60);
    f.GetIntValueOrDefault("SCD", "confidence", &m_nConfidence, 60);

    return AX_TRUE;
}

AX_BOOL CSCD::Startup(AX_U32 nWidth, AX_U32 nHeight) {
    LoadConfig();

    AX_S32 ret = AX_IVES_SCD_Init();
    if (0 != ret) {
        LOG_M_E(SCD, "AX_IVES_SCD_Init() fail, ret = 0x%x", ret);
        return AX_FALSE;
    }

    m_chn = 1;

    AX_SCD_CHN_ATTR_S stChnAttr;
    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.chn = m_chn;
    stChnAttr.stArea.u32X = 0;
    stChnAttr.stArea.u32Y = 0;
    stChnAttr.stArea.u32W = nWidth;
    stChnAttr.stArea.u32H = nHeight;
    stChnAttr.u8Thrd = (AX_U8)m_nThrd;
    stChnAttr.u8Confidence = (AX_U8)m_nConfidence;
    ret = AX_IVES_SCD_CreateChn(m_chn, &stChnAttr);
    if (0 != ret) {
        AX_IVES_SCD_DeInit();
        LOG_M_E(SCD, "AX_IVES_SCD_CreateChn(chn: %d) fail, ret = 0x%x", m_chn, ret);
        m_chn = 0;
        return AX_FALSE;
    }

    return AX_TRUE;
}

AX_VOID CSCD::Cleanup(AX_VOID) {
    if (m_chn > 0) {
        AX_IVES_SCD_DestoryChn(m_chn);
        m_chn = 0;
    }

    AX_IVES_SCD_DeInit();
}

AX_BOOL CSCD::ProcessFrame(const CMediaFrame *pFrame) {
    if (m_chn <= 0) {
        LOG_M_E(SCD, "SCD channel is not created yet.");
        return AX_FALSE;
    }

    const AX_IVES_IMAGE_S *pImage = (const AX_IVES_IMAGE_S *)&pFrame->tVideoFrame;
    AX_U8 nChanged;
    AX_S32 ret = AX_IVES_SCD_Process(m_chn, pImage, &nChanged);
    if (0 != ret) {
        LOG_M_E(SCD, "AX_IVES_SCD_Process(frame: %lld) fail, ret = 0x%x", pImage->u64SeqNum, ret);
        return AX_FALSE;
    }

    if (m_pWebServer && (1 == nChanged)) {
        WEB_EVENTS_DATA_T tEvent;
        memset(&tEvent, 0, sizeof(tEvent));
        tEvent.eType = E_WEB_EVENTS_TYPE_SCD;
        m_pWebServer->SendEventsData(&tEvent);
    }

    return AX_TRUE;
}

AX_VOID CSCD::SetThreshold(AX_S32 nThreshold, AX_S32 nConfidence) {
    if (m_chn > 0) {
        AX_SCD_CHN_ATTR_S stChnAttr;
        AX_S32 ret = AX_IVES_SCD_GetChnAttr(m_chn, &stChnAttr);
        if (0 != ret) {
            LOG_M_E(SCD, "AX_IVES_SCD_GetChnAttr(chn: %d) fail, ret = 0x%x", m_chn, ret);
            return;
        }

        stChnAttr.u8Thrd = (AX_U8)nThreshold;
        stChnAttr.u8Confidence = (AX_U8)m_nConfidence;
        ret = AX_IVES_SCD_SetChnAttr(m_chn, &stChnAttr);
        if (0 != ret) {
            LOG_M_E(SCD, "AX_IVES_SCD_SetChnAttr(chn: %d) fail, ret = 0x%x", m_chn, ret);
            return;
        }
    }

    m_nThrd = nThreshold;
    m_nConfidence = nConfidence;
}

AX_VOID CSCD::GetThreshold(AX_S32 &nThreshold, AX_S32 &nConfidence) const {
    nThreshold = m_nThrd;
    nConfidence = m_nConfidence;
}

AX_VOID CSCD::Update(AX_U32 u32W, AX_U32 u32H) {

    if (m_chn <= 0) {
        return;
    }

    AX_SCD_CHN_ATTR_S stChnAttr;
    AX_S32 ret = AX_IVES_SCD_GetChnAttr(m_chn, &stChnAttr);
    if (0 != ret) {
        LOG_M_E(SCD, "AX_IVES_SCD_GetChnAttr(chn: %d) fail, ret = 0x%x", m_chn, ret);
        return;
    }

    stChnAttr.stArea.u32W = u32W;
    stChnAttr.stArea.u32H = u32H;
    ret = AX_IVES_SCD_SetChnAttr(m_chn, &stChnAttr);
    if (0 != ret) {
        LOG_M_E(SCD, "AX_IVES_SCD_SetChnAttr(chn: %d) fail, ret = 0x%x", m_chn, ret);
        return;
    }
}