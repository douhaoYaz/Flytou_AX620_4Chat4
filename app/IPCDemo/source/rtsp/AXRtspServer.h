/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/

#ifndef __AXRTSPSERVER_H__
#define __AXRTSPSERVER_H__

#include "global.h"

class RTSPServer;
class AXLiveServerMediaSession;
class UsageEnvironment;

class AXRtspServer {
public:
    AXRtspServer(void);
    virtual ~AXRtspServer(void);

public:
    AX_BOOL Init(AX_U16 uBasePort = 0);
    AX_BOOL Start(void);
    void    Stop(void);
    void    RestartSessons();

    void    SendNalu(AX_U8 nChn, const AX_U8* pBuf, AX_U32 nLen, AX_U64 nPts=0, AX_BOOL bIFrame=AX_FALSE);

public:
    RTSPServer*               m_rtspServer{nullptr};
    AX_U16                    m_uBasePort{0};
    AXLiveServerMediaSession* m_pLiveServerMediaSession[MAX_VENC_CHANNEL_NUM]{0};
    UsageEnvironment*         m_pUEnv{nullptr};

private:
    pthread_t                 m_tidServer{0};
};

#endif /*__AXRTSPSERVER_H__*/