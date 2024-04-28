/**********************************************************************************
 *
 * Copyright (c) 2019-2022 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/
#pragma once
#include "Singleton.h"
#include "WebServer.h"
#include "MediaFrame.h"
#include "ax_ives_api.h"

class CSCD : public CSingleton<CSCD> {
    friend class CSingleton<CSCD>;

public:
    AX_VOID SetWebServer(CWebServer *pWebServer);
    AX_BOOL Startup(AX_U32 nWidth, AX_U32 nHeight);
    AX_VOID Cleanup(AX_VOID);
    AX_BOOL ProcessFrame(const CMediaFrame *pFrame);
    AX_VOID SetThreshold(AX_S32 nThreshold, AX_S32 nConfidence);
    AX_VOID GetThreshold(AX_S32 &nThreshold, AX_S32 &nConfidence)const;
    AX_VOID Update(AX_U32 u32W, AX_U32 u32H);

protected:
    AX_BOOL LoadConfig(AX_VOID);

private:
    CSCD(AX_VOID) noexcept = default;
    virtual ~CSCD(AX_VOID) = default;

    /* virtual function of CSingleton */
    AX_BOOL Init(AX_VOID) override {
        LoadConfig();
        return AX_TRUE;
    };

private:
    CWebServer *m_pWebServer{nullptr};
    SCD_CHN m_chn{0};
    AX_S32 m_nThrd{60};
    AX_S32 m_nConfidence{60};
};