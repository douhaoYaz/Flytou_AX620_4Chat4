/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/

#include "VideoEncoder.h"
#include "OptionHelper.h"
#include "StageOptionHelper.h"
#include "PrintHelper.h"
#include "CommonUtils.h"
#include "AXRtspServer.h"
#include "global.h"
#include "JsonCfgParser.h"
#include "IVPSStage.h"
#include <sys/time.h>
#include <atomic>
#include <mutex>
#include <queue>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <map>
#include "Detector.h"

#define VENC "VENC"
#define SAVE_MAX_FPS(arg) if(arg > g_nVENCMaxFPS) { g_nVENCMaxFPS = arg; }

extern COptionHelper gOptions;
extern CPrintHelper gPrintHelper;
extern END_POINT_OPTIONS g_tEPOptions[MAX_VENC_CHANNEL_NUM];
extern IVPS_GROUP_CFG_T g_tIvpsGroupConfig[IVPS_GROUP_NUM];

AX_U32 g_nVENCMaxFPS = 25;

namespace
{
struct skeleton
{
    int connection[2];
    int left_right_neutral;
};

static const std::vector<skeleton> pairs = {
                               {15, 13, 0},
                               {13, 11, 0},
                               {16, 14, 0},
                               {14, 12, 0},
                               {11, 12, 0},
                               {5, 11, 0},
                               {6, 12, 0},
                               {5, 6, 0},
                               {5, 7, 0},
                               {6, 8, 0},
                               {7, 9, 0},
                               {8, 10, 0},
                               {1, 2, 0},
                               {0, 1, 0},
                               {0, 2, 0},
                               {1, 3, 0},
                               {2, 4, 0},
                               {0, 5, 0},
                               {0, 6, 0}};
}  // namespace

CVideoEncoder::CVideoEncoder(AX_U8 nChannel, AX_U8 nInnerIndex)
    : CStage((string)VENC + (char)('0' + nChannel))
    , m_nInnerIndex(nInnerIndex)
    , m_nSnsID(E_SENSOR_ID_0)
    , m_nChannel(nChannel)
    , m_bH265(AX_FALSE)
    , m_bGetThreadRunning(AX_FALSE)
    , m_pRtspServer(nullptr)
    , m_pWebServer(nullptr)
    , m_pMpeg4Encoder(nullptr)
    , m_pGetThread(nullptr)
    , bEnableProcessFrame(AX_TRUE)
{
    memset(&m_tChnAttr, 0, sizeof(m_tChnAttr));
}

CVideoEncoder::~CVideoEncoder()
{
}

AX_BOOL CVideoEncoder::ProcessFrame(CMediaFrame* pFrame)
{
    AX_S32 nRet = 0;

    if (!bEnableProcessFrame) {
        return AX_FALSE;
    }

    ProcOSD(pFrame);

    LOG_M_I(VENC, "[%d] AX_VENC_SendFrame, w:%d h:%d", m_nChannel, pFrame->tVideoFrame.u32Width, pFrame->tVideoFrame.u32Height);

    AX_VIDEO_FRAME_INFO_S tFrame = {0};
    if (pFrame->bIvpsFrame) {
        tFrame.stVFrame = pFrame->tVideoFrame;
    } else {
        tFrame = pFrame->tFrame.tFrameInfo;
    }

    tFrame.stVFrame.u32PicStride[1] = tFrame.stVFrame.u32PicStride[0];
    tFrame.stVFrame.u32PicStride[2] = 0;
    tFrame.stVFrame.enImgFormat = AX_YUV420_SEMIPLANAR;

    nRet = AX_VENC_SendFrame(m_nChannel, &tFrame, -1);
    if (AX_SUCCESS != nRet) {
        LOG_M_E(VENC, "[%d] AX_VENC_SendFrame failed, ret=0x%x", m_nChannel, nRet);
        return AX_FALSE;
    }

    return AX_TRUE;
}

static AX_VOID *GetThreadFunc(AX_VOID *__this)
{
    CVideoEncoder *pThis = (CVideoEncoder *)__this;
    LOG_M(VENC, "[%d] +++", pThis->m_nChannel);

    AX_CHAR szName[50] = {0};
    sprintf(szName, "IPC_VENC_Get_%d", pThis->m_nChannel);
    prctl(PR_SET_NAME, szName);

    AX_VENC_STREAM_S stStream;
    memset(&stStream, 0, sizeof(AX_VENC_STREAM_S));

    AX_S32 ret = 0;
    bool bEnableAutoSleep = (gOptions.IsEnableAutoSleep() == AX_TRUE) &&
                            (pThis->m_nChannel == 0);

    while (pThis->m_bGetThreadRunning) {
        ret = AX_VENC_GetStream(pThis->m_nChannel, &stStream, -1);
        if (AX_SUCCESS != ret) {
            if (AX_ERR_VENC_FLOW_END == ret) {
                pThis->m_bGetThreadRunning = AX_FALSE;
                break;
            }

            if (AX_ERR_VENC_QUEUE_EMPTY == ret) {
                CTimeUtils::msSleep(1);
                continue;
            }

            LOG_M_E(VENC, "AX_VENC_GetStream failed with %#x!", ret);
            continue;
        }

        LOG_M_I(VENC, "[%d] seq=%d", pThis->m_nChannel, stStream.stPack.u64SeqNum);

        if (stStream.stPack.u64SeqNum > 0) {
            gPrintHelper.AddTimeSpan(E_PH_PIPE_PT_CAMERA_VENC, pThis->m_nChannel, stStream.stPack.u64SeqNum);
        }

        if (stStream.stPack.pu8Addr && stStream.stPack.u32Len > 0) {
            gPrintHelper.Add(E_PH_MOD_VENC, 0, pThis->m_nChannel);

            CStageOptionHelper::GetInstance()->StatVencOutBytes(pThis->m_nInnerIndex, stStream.stPack.u32Len);

            AX_BOOL bIFrame = (VENC_INTRA_FRAME == stStream.stPack.enCodingType) ? AX_TRUE : AX_FALSE;

            if (pThis->m_pRtspServer) {
                pThis->m_pRtspServer->SendNalu(pThis->m_nChannel, (AX_U8 *)stStream.stPack.pu8Addr, stStream.stPack.u32Len, stStream.stPack.u64PTS, bIFrame);
            }

            if (pThis->m_pWebServer) {
                pThis->m_pWebServer->SendPreviewData(pThis->m_nChannel, (AX_U8 *)stStream.stPack.pu8Addr, stStream.stPack.u32Len, stStream.stPack.u64PTS, bIFrame);
            }

            if (0 == pThis->m_nChannel && pThis->m_pMpeg4Encoder) {
                pThis->m_pMpeg4Encoder->SendRawFrame(pThis->m_nChannel, (AX_U8 *)stStream.stPack.pu8Addr, stStream.stPack.u32Len, stStream.stPack.u64PTS, bIFrame);
            }

            if (bEnableAutoSleep) {
                gOptions.SetVenc0SeqNum(stStream.stPack.u64SeqNum);
            }
        }

        ret = AX_VENC_ReleaseStream(pThis->m_nChannel, &stStream);
        if (AX_SUCCESS != ret) {
            LOG_M_E(VENC, "AX_VENC_ReleaseStream failed!");
            continue;
        }
    }

    LOG_M(VENC, "[%d] ---", pThis->m_nChannel);

    return nullptr;
}

AX_BOOL CVideoEncoder::LoadConfig()
{
    LOG_M(VENC, "[%d] +++", m_nChannel);
    VIDEO_CONFIG_T tVEncConfig;
    if (!CConfigParser().GetInstance()->GetVideoCfgByID(m_nInnerIndex, tVEncConfig)) {
        LOG_M_W(VENC, "Load venc configuration failed.");
        return AX_FALSE;
    }

    m_tVideoConfig = tVEncConfig;

    AX_U8 nISPChn = CIVPSStage::GetIspChnIndex(m_nChannel);
    m_tVideoConfig.nInWidth     = m_tChnAttr.tChnAttr[nISPChn].nWidth;
    m_tVideoConfig.nInHeight    = m_tChnAttr.tChnAttr[nISPChn].nHeight;
    m_tVideoConfig.nOutWidth    = m_tChnAttr.tChnAttr[nISPChn].nWidth;
    m_tVideoConfig.nOutHeight   = m_tChnAttr.tChnAttr[nISPChn].nHeight;

    AX_U8 nIvpsInnerIndex = CIVPSStage::GetIvpsChnIndex(m_nChannel);
    if (E_END_POINT_VENC == g_tEPOptions[m_nChannel].eEPType) {
        if (-1 != g_tIvpsGroupConfig[nISPChn].arrOutResolution[nIvpsInnerIndex][0]
            && -1 != g_tIvpsGroupConfig[nISPChn].arrOutResolution[nIvpsInnerIndex][1]
            && -1 != g_tIvpsGroupConfig[nISPChn].arrOutResolution[nIvpsInnerIndex][2]) {
            m_tVideoConfig.nInWidth     = g_tIvpsGroupConfig[nISPChn].arrOutResolution[nIvpsInnerIndex][0];
            m_tVideoConfig.nInHeight    = g_tIvpsGroupConfig[nISPChn].arrOutResolution[nIvpsInnerIndex][1];
            m_tVideoConfig.nOutWidth    = g_tIvpsGroupConfig[nISPChn].arrOutResolution[nIvpsInnerIndex][0];
            m_tVideoConfig.nOutHeight   = g_tIvpsGroupConfig[nISPChn].arrOutResolution[nIvpsInnerIndex][1];
        }
    }

    m_tVideoConfig.nStride = ALIGN_UP(m_tVideoConfig.nInWidth, g_tIvpsGroupConfig[nISPChn].arrOutResolution[nIvpsInnerIndex][2]);

    m_bH265 = (m_tVideoConfig.ePayloadType == PT_H265 ? AX_TRUE : AX_FALSE);

    LOG_M(VENC, "[%d] ---", m_nChannel);

    return InitParams(m_tVideoConfig);
}

AX_VOID CVideoEncoder::SetChnAttr(AX_VIN_CHN_ATTR_T tAttr)
{
    m_tChnAttr = tAttr;
}

AX_VOID CVideoEncoder::SetRTSPServer(AXRtspServer *pServer)
{
    m_pRtspServer = pServer;
}

AX_VOID CVideoEncoder::SetWebServer(CWebServer *pServer)
{
    m_pWebServer = pServer;
}

AX_VOID CVideoEncoder::SetMp4ENC(CMPEG4Encoder* pMpeg4Encoder)
{
    m_pMpeg4Encoder = pMpeg4Encoder;
}

AX_BOOL CVideoEncoder::Start(AX_BOOL bReload /*= AX_TRUE*/)
{
    LOG_M(VENC, "[%d] +++", m_nChannel);

    if (m_bGetThreadRunning) {
        LOG_M_E(VENC, "VENC %d already started.");
        return AX_TRUE;
    }

    if (bReload) {
        if (!LoadConfig()) {
            return AX_FALSE;
        }
    } else {
        AX_BOOL bRet = InitParams(m_tVideoConfig);
        if (!bRet) {
            LOG_M_E(VENC, "Failed to start VENC.");
        }
    }

    m_bGetThreadRunning = AX_TRUE;
    m_pGetThread = new thread(GetThreadFunc, this);

    if (m_pGetThread) {
        AX_VENC_RECV_PIC_PARAM_S tRecvParam;
        tRecvParam.s32RecvPicNum = -1;
        AX_VENC_StartRecvFrame(m_nChannel, &tRecvParam);
    }

    AX_BOOL bLinkMode = AX_FALSE;
    AX_U8 nISPChn = CIVPSStage::GetIspChnIndex(m_nChannel);
    AX_U8 nIvpsInnerIndex = CIVPSStage::GetIvpsChnIndex(m_nChannel);

    bLinkMode = (gOptions.IsLinkMode() && 1 == g_tIvpsGroupConfig[nISPChn].arrLinkModeFlag[nIvpsInnerIndex]) ? AX_TRUE : AX_FALSE;

    LOG_M(VENC, "[%d] ---", m_nChannel);
    return CStage::Start(bLinkMode ? AX_FALSE : AX_TRUE);
}

AX_VOID CVideoEncoder::Stop()
{
    CStage::Stop();

    LOG_M(VENC, "[%d] +++", m_nChannel);

    if (m_bGetThreadRunning) {
        AX_VENC_StopRecvFrame((VENC_CHN)m_nChannel);
        m_bGetThreadRunning = AX_FALSE;

        CTimeUtils::msSleep(50);
        AX_VENC_DestroyChn(m_nChannel);

        if (m_pGetThread && m_pGetThread->joinable()) {
            m_pGetThread->join();

            delete m_pGetThread;
            m_pGetThread = nullptr;
        }
    }

    LOG_M(VENC, "[%d] ---", m_nChannel);
}

AX_BOOL CVideoEncoder::UpdateFramerate(AX_F32 fSrcFrameRate, AX_F32 fDstFrameRate/* = -1*/)
{
    LOG_M(VENC, "[%d] +++", m_nChannel);

    AX_S32 nRet = 0;

    if (fDstFrameRate <= 0) {
        fDstFrameRate = (AX_F32)fSrcFrameRate;
    }

    m_tVideoConfig.nFrameRate = (AX_U32)fDstFrameRate;

    if (m_bGetThreadRunning) {
        AX_VENC_RC_PARAM_S stRcParam;
        memset(&stRcParam, 0x00, sizeof(stRcParam));

        nRet = AX_VENC_GetRcParam(m_nChannel, &stRcParam);

        if (nRet) {
            LOG_M_E(VENC, "AX_VENC_GetRcParam fail, ret=0x%x", nRet);

            return AX_FALSE;
        }

        switch (m_tVideoConfig.ePayloadType) {
            case PT_H265: {
                if (stRcParam.enRcMode == VENC_RC_MODE_H265CBR) {
                    stRcParam.stH265Cbr.u32SrcFrameRate = (AX_U32)fSrcFrameRate; /* input frame rate */
                    stRcParam.stH265Cbr.fr32DstFrameRate = fDstFrameRate; /* target frame rate */
                } else if (stRcParam.enRcMode == VENC_RC_MODE_H265VBR) {
                    stRcParam.stH265Vbr.u32SrcFrameRate = (AX_U32)fSrcFrameRate; /* input frame rate */
                    stRcParam.stH265Vbr.fr32DstFrameRate = fDstFrameRate; /* target frame rate */
                } else if (stRcParam.enRcMode == VENC_RC_MODE_H265FIXQP) {
                    stRcParam.stH265FixQp.u32SrcFrameRate = (AX_U32)fSrcFrameRate; /* input frame rate */
                    stRcParam.stH265FixQp.fr32DstFrameRate = fDstFrameRate; /* target frame rate */
                }
                break;
            }
            case PT_H264: {
                if (stRcParam.enRcMode == VENC_RC_MODE_H264CBR) {
                    stRcParam.stH264Cbr.u32SrcFrameRate = (AX_U32)fSrcFrameRate; /* input frame rate */
                    stRcParam.stH264Cbr.fr32DstFrameRate = fDstFrameRate; /* target frame rate */
                } else if (stRcParam.enRcMode == VENC_RC_MODE_H264VBR) {
                    stRcParam.stH264Vbr.u32SrcFrameRate = (AX_U32)fSrcFrameRate; /* input frame rate */
                    stRcParam.stH264Vbr.fr32DstFrameRate = fDstFrameRate; /* target frame rate */
                } else if (stRcParam.enRcMode == VENC_RC_MODE_H264FIXQP) {
                    stRcParam.stH264FixQp.u32SrcFrameRate = (AX_U32)fSrcFrameRate; /* input frame rate */
                    stRcParam.stH264FixQp.fr32DstFrameRate = fDstFrameRate; /* target frame rate */
                }
                break;
            }
            default:
                LOG_M_E(VENC, "Payload type unrecognized.");
                break;
        }

        nRet = AX_VENC_SetRcParam(m_nChannel, &stRcParam);

        if (nRet) {
            LOG_M_E(VENC, "AX_VENC_SetRcParam fail, ret=0x%x", nRet);
        }
    }

    LOG_M(VENC, "[%d] ---", m_nChannel);
    return AX_TRUE;
}

AX_BOOL CVideoEncoder::ChangeRCType(AX_U32 nRcType, AX_U32 u32MaxIprop)
{
    AX_BOOL bRet = AX_TRUE;
    AX_S32 nRet = -1;

    bEnableProcessFrame = AX_FALSE;

    StopRecv();

    AX_VENC_CHN_ATTR_S tAttr;
    nRet = AX_VENC_GetChnAttr(m_nChannel, &tAttr);
    if (0 != nRet) {
        LOG_M_E(VENC, "AX_VENC_GetChnAttr failed, ret:0x%x", nRet);
        StartRecv();
        return AX_FALSE;
    }

    switch (m_tVideoConfig.ePayloadType) {
        case PT_H265: {
            switch (nRcType) {
                case 0: { // H265CBR
                    tAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
                    memcpy(&tAttr.stRcAttr.stH265Cbr, &m_tH265Cbr, sizeof(AX_VENC_H265_CBR_S));
                    if (u32MaxIprop >= tAttr.stRcAttr.stH265Cbr.u32MinIprop) {
                        tAttr.stRcAttr.stH265Cbr.u32MaxIprop = u32MaxIprop;
                        m_tH265Cbr.u32MaxIprop = u32MaxIprop;
                    } else {
                        bRet = AX_FALSE;
                        LOG_M_E(VENC, "H265 u32MaxIprop(%d) must be bigger than u32MinIprop(%d)", u32MaxIprop, tAttr.stRcAttr.stH265Cbr.u32MinIprop);
                    }
                    break;
                }
                case 1: { // H265VBR
                    tAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
                    memcpy(&tAttr.stRcAttr.stH265Vbr, &m_tH265Vbr, sizeof(AX_VENC_H265_VBR_S));
                    break;
                }
                case 2: { // H265FIXQP
                    tAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265FIXQP;
                    memcpy(&tAttr.stRcAttr.stH265FixQp, &m_tH265FixQp, sizeof(AX_VENC_H265_FIXQP_S));
                    break;
                }
                default:
                    LOG_M_E(VENC, "H265 nRcType(%d) invalid!", nRcType);
                    break;
            }
            break;
        }
        case PT_H264: {
            switch (nRcType) {
                case 0: { // H264CBR
                    tAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
                    memcpy(&tAttr.stRcAttr.stH264Cbr, &m_tH264Cbr, sizeof(AX_VENC_H264_CBR_S));
                    if (u32MaxIprop >= tAttr.stRcAttr.stH264Cbr.u32MinIprop) {
                        tAttr.stRcAttr.stH264Cbr.u32MaxIprop = u32MaxIprop;
                        m_tH264Cbr.u32MaxIprop = u32MaxIprop;
                    } else {
                        bRet = AX_FALSE;
                        LOG_M_E(VENC, "H264 u32MaxIprop(%d) must be bigger than u32MinIprop(%d)", u32MaxIprop, tAttr.stRcAttr.stH264Cbr.u32MinIprop);
                    }
                    break;
                }
                case 1: { // H264VBR
                    tAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
                    memcpy(&tAttr.stRcAttr.stH264Vbr, &m_tH264Vbr, sizeof(AX_VENC_H264_VBR_S));
                    break;
                }
                case 2: { // H264FIXQP
                    tAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264FIXQP;
                    memcpy(&tAttr.stRcAttr.stH264FixQp, &m_tH264FixQp, sizeof(AX_VENC_H264_FIXQP_S));
                    break;
                }
                default:
                    LOG_M_E(VENC, "H264 nRcType(%d) invalid!", nRcType);
                    break;
            }
            break;
        }
        default:
            LOG_M_E(VENC, "Payload type(%d) unrecognized.", m_tVideoConfig.ePayloadType);
            break;
    }

    nRet = AX_VENC_SetChnAttr(m_nChannel, &tAttr);
    if (0 != nRet) {
        bRet = AX_FALSE;
        LOG_M_E(VENC, "AX_VENC_SetChnAttr failed, ret:0x%x", nRet);
    }

    StartRecv();

    bEnableProcessFrame = AX_TRUE;

    return bRet;
}

AX_BOOL CVideoEncoder::UpdateRCParam(AX_U32 nRcType, AX_BOOL bRcTypeChange, AX_U32 u32MaxIprop)
{
    LOG_M(VENC, "[%d] +++", m_nChannel);

    AX_S32 nRet = 0;

    if (m_bGetThreadRunning) {
        if (bRcTypeChange) {
           return ChangeRCType(nRcType, u32MaxIprop);
        }

        AX_VENC_RC_PARAM_S stRcParam;
        memset(&stRcParam, 0x00, sizeof(stRcParam));

        nRet = AX_VENC_GetRcParam(m_nChannel, &stRcParam);
        if (nRet) {
            LOG_M_E(VENC, "AX_VENC_GetRcParam fail, ret=0x%x", nRet);
            return AX_FALSE;
        }

        switch (m_tVideoConfig.ePayloadType) {
            case PT_H265: {
                if (u32MaxIprop >= stRcParam.stH265Cbr.u32MinIprop) {
                    stRcParam.stH265Cbr.u32MaxIprop = u32MaxIprop;
                    m_tH265Cbr.u32MaxIprop = u32MaxIprop;
                } else {
                    LOG_M_E(VENC, "H265 u32MaxIprop(%d) must be bigger than u32MinIprop(%d)", u32MaxIprop, stRcParam.stH265Cbr.u32MinIprop);
                    return AX_FALSE;
                }
                break;
            }
            case PT_H264: {
                if (u32MaxIprop >= stRcParam.stH264Cbr.u32MinIprop) {
                    stRcParam.stH264Cbr.u32MaxIprop = u32MaxIprop;
                    m_tH264Cbr.u32MaxIprop = u32MaxIprop;
                } else {
                    LOG_M_E(VENC, "H264 u32MaxIprop(%d) must be bigger than u32MinIprop(%d)", u32MaxIprop, stRcParam.stH264Cbr.u32MinIprop);
                    return AX_FALSE;
                }
                break;
            }
            default:
                LOG_M_E(VENC, "Payload type(%d) unrecognized.", m_tVideoConfig.ePayloadType);
                break;
        }

        nRet = AX_VENC_SetRcParam(m_nChannel, &stRcParam);
        if (nRet) {
            LOG_M_E(VENC, "AX_VENC_SetRcParam fail, ret=0x%x", nRet);
            return AX_FALSE;
        }
    }

    LOG_M(VENC, "[%d] ---", m_nChannel);
    return AX_TRUE;
}

AX_BOOL CVideoEncoder::InitParams(VIDEO_CONFIG_T &config)
{
    LOG_M(VENC, "[%d] +++", m_nChannel);

    if (0 == m_nChannel) {
        LoadFont();
    }

    MPEG4EC_INFO_T stMp4EcInfo;
    /* ENC */
    AX_VENC_CHN_ATTR_S stVencChnAttr;
    memset(&stVencChnAttr, 0, sizeof(AX_VENC_CHN_ATTR_S));

    stVencChnAttr.stVencAttr.u32MaxPicWidth = 0;
    stVencChnAttr.stVencAttr.u32MaxPicHeight = 0;

    stVencChnAttr.stVencAttr.u32PicWidthSrc = config.nInWidth;   /*the picture width*/
    stVencChnAttr.stVencAttr.u32PicHeightSrc = config.nInHeight; /*the picture height*/

    stVencChnAttr.stVencAttr.u32CropOffsetX = config.nOffsetCropX;
    stVencChnAttr.stVencAttr.u32CropOffsetY = config.nOffsetCropY;
    stVencChnAttr.stVencAttr.u32CropWidth = config.nOffsetCropW;
    stVencChnAttr.stVencAttr.u32CropHeight = config.nOffsetCropH;
    stVencChnAttr.stVencAttr.u32VideoRange = 1; /* 0: Narrow Range(NR), Y[16,235], Cb/Cr[16,240]; 1: Full Range(FR), Y/Cb/Cr[0,255] */

    LOG_M(VENC, "[%d] w:%d, h:%d, s:%d, Crop:(%d, %d, %d, %d) rcType:%d, payload:%d"
        , m_nChannel
        , stVencChnAttr.stVencAttr.u32PicWidthSrc
        , stVencChnAttr.stVencAttr.u32PicHeightSrc
        , config.nStride
        , stVencChnAttr.stVencAttr.u32CropOffsetX
        , stVencChnAttr.stVencAttr.u32CropOffsetY
        , stVencChnAttr.stVencAttr.u32CropWidth
        , stVencChnAttr.stVencAttr.u32CropHeight
        , config.eRCType
        , config.ePayloadType);

    stVencChnAttr.stVencAttr.u32BufSize = config.nStride * config.nInHeight * 3/2; /*stream buffer size*/
    stVencChnAttr.stVencAttr.u32MbLinesPerSlice = 0; /*get stream mode is slice mode or frame mode?*/

    AX_U8 nISPChn = CIVPSStage::GetIspChnIndex(m_nChannel);
    AX_U8 nIvpsInnerIndex = CIVPSStage::GetIvpsChnIndex(m_nChannel);
    stVencChnAttr.stVencAttr.u8InFifoDepth = AX_VENC_IN_FIFO_DEPTH;
    stVencChnAttr.stVencAttr.u8OutFifoDepth = AX_VENC_OUT_FIFO_DEPTH;
    if (gOptions.IsLinkMode() && 1 == g_tIvpsGroupConfig[nISPChn].arrLinkModeFlag[nIvpsInnerIndex]) {
        stVencChnAttr.stVencAttr.enLinkMode = AX_LINK_MODE;
    } else {
        stVencChnAttr.stVencAttr.enLinkMode = AX_NONLINK_MODE;
    }
    stVencChnAttr.stVencAttr.u32GdrDuration = 0;
    stVencChnAttr.stVencAttr.enMemSource = AX_MEMORY_SOURCE_POOL;

    stVencChnAttr.stVencAttr.enType = config.ePayloadType;

    InitRcParam(stVencChnAttr.stVencAttr.enType,
                g_tIvpsGroupConfig[nISPChn].arrFramerate[nIvpsInnerIndex][1],
                config);

    switch (stVencChnAttr.stVencAttr.enType) {
        case PT_H265: {
            stVencChnAttr.stVencAttr.enProfile = VENC_HEVC_MAIN_PROFILE;                  //main profile
            stVencChnAttr.stVencAttr.enLevel = VENC_HEVC_LEVEL_6;
            stVencChnAttr.stVencAttr.enTier = VENC_HEVC_MAIN_TIER;

            if (config.eRCType == RcType::VENC_RC_CBR) {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
                stVencChnAttr.stRcAttr.s32FirstFrameStartQp = -1;
                memcpy(&stVencChnAttr.stRcAttr.stH265Cbr, &m_tH265Cbr, sizeof(AX_VENC_H265_CBR_S));
                SAVE_MAX_FPS(config.nFrameRate)
            } else if (config.eRCType == RcType::VENC_RC_VBR) {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
                stVencChnAttr.stRcAttr.s32FirstFrameStartQp = -1;
                memcpy(&stVencChnAttr.stRcAttr.stH265Vbr, &m_tH265Vbr, sizeof(AX_VENC_H265_VBR_S));
                SAVE_MAX_FPS(config.nFrameRate)
            } else if (config.eRCType == RcType::VENC_RC_FIXQP) {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265FIXQP;
                memcpy(&stVencChnAttr.stRcAttr.stH265FixQp, &m_tH265FixQp, sizeof(AX_VENC_H265_FIXQP_S));
                SAVE_MAX_FPS(config.nFrameRate)
            }
            break;
        }
        case PT_H264: {
            stVencChnAttr.stVencAttr.enProfile = VENC_H264_MAIN_PROFILE;                  //main profile
            stVencChnAttr.stVencAttr.enLevel = VENC_H264_LEVEL_5_2;

            if (config.eRCType == RcType::VENC_RC_CBR) {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
                stVencChnAttr.stRcAttr.s32FirstFrameStartQp = -1;
                memcpy(&stVencChnAttr.stRcAttr.stH264Cbr, &m_tH264Cbr, sizeof(AX_VENC_H264_CBR_S));
                SAVE_MAX_FPS(config.nFrameRate)
            } else if (config.eRCType == RcType::VENC_RC_VBR) {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
                stVencChnAttr.stRcAttr.s32FirstFrameStartQp = -1;
                memcpy(&stVencChnAttr.stRcAttr.stH264Vbr, &m_tH264Vbr, sizeof(AX_VENC_H264_VBR_S));
                SAVE_MAX_FPS(config.nFrameRate)
            } else if (config.eRCType == RcType::VENC_RC_FIXQP) {
                stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264FIXQP;
                memcpy(&stVencChnAttr.stRcAttr.stH264FixQp, &m_tH264FixQp, sizeof(AX_VENC_H264_FIXQP_S));
                SAVE_MAX_FPS(config.nFrameRate)
            }
            break;
        }
        default:
            LOG_M_E(VENC, "Payload type unrecognized.");
            break;
    }

    /* GOP Setting */
    stVencChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;

    // mpeg4 encoder init
    stMp4EcInfo.nBitrate = config.nBitrate;
    stMp4EcInfo.nFrameRate = g_tIvpsGroupConfig[nISPChn].arrFramerate[nIvpsInnerIndex][1];
    stMp4EcInfo.nfrHeight = config.nOutHeight;
    stMp4EcInfo.nfrWidth = config.nOutWidth;
    if (0 == m_nChannel && m_pMpeg4Encoder) {
        m_pMpeg4Encoder->InitParam(stMp4EcInfo);
    }

    AX_S32 ret = AX_VENC_CreateChn(m_nChannel, &stVencChnAttr);
    if (AX_SUCCESS != ret) {
        LOG_M_E(VENC, "[%d] AX_VENC_CreateChn failed, ret=0x%x", m_nChannel, ret);
        return AX_FALSE;
    }

    LOG_M(VENC, "[%d] ---", m_nChannel);
    return AX_TRUE;
}

AX_VOID CVideoEncoder::InitRcParam(AX_PAYLOAD_TYPE_E eType, AX_U32 nSrcFrameRate, const VIDEO_CONFIG_T& tConfig)
{
    switch (eType) {
        case PT_H265: {
            const RC_INFO_T& stRCInfoCBR = tConfig.stRCInfoCBR;
            // set h265 cbr
            m_tH265Cbr.u32Gop = tConfig.nGOP;
            m_tH265Cbr.u32SrcFrameRate = nSrcFrameRate;
            m_tH265Cbr.fr32DstFrameRate = tConfig.nFrameRate;
            m_tH265Cbr.u32BitRate = tConfig.nBitrate;
            m_tH265Cbr.u32MinQp = ADAPTER_RANGE(stRCInfoCBR.nMinQp, 0, 51);
            m_tH265Cbr.u32MaxQp = ADAPTER_RANGE(stRCInfoCBR.nMaxQp, 0, 51);
            m_tH265Cbr.u32MinIQp = ADAPTER_RANGE(stRCInfoCBR.nMinIQp, 0, 51);
            m_tH265Cbr.u32MaxIQp = ADAPTER_RANGE(stRCInfoCBR.nMaxIQp, 0, 51);
            m_tH265Cbr.u32MinIprop = ADAPTER_RANGE(stRCInfoCBR.nMinIProp, 0, 100);
            m_tH265Cbr.u32MaxIprop = ADAPTER_RANGE(stRCInfoCBR.nMaxIProp, 0, 100);
            m_tH265Cbr.s32IntraQpDelta = ADAPTER_RANGE(stRCInfoCBR.nIntraQpDelta, -51, 51);

            // set h265 vbr
            const RC_INFO_T& stRCInfoVBR = tConfig.stRCInfoVBR;
            m_tH265Vbr.u32Gop = tConfig.nGOP;
            m_tH265Vbr.u32SrcFrameRate = nSrcFrameRate;
            m_tH265Vbr.fr32DstFrameRate = tConfig.nFrameRate;
            m_tH265Vbr.u32MaxBitRate = tConfig.nBitrate;
            m_tH265Vbr.u32MinQp = ADAPTER_RANGE(stRCInfoVBR.nMinQp, 0, 51);
            m_tH265Vbr.u32MaxQp = ADAPTER_RANGE(stRCInfoVBR.nMaxQp, 0, 51);
            m_tH265Vbr.u32MinIQp = ADAPTER_RANGE(stRCInfoVBR.nMinIQp, 0, 51);
            m_tH265Vbr.u32MaxIQp = ADAPTER_RANGE(stRCInfoVBR.nMaxIQp, 0, 51);
            m_tH265Vbr.s32IntraQpDelta = ADAPTER_RANGE(stRCInfoVBR.nIntraQpDelta, -51, 51);

            // set h265 fixQp
            m_tH265FixQp.u32Gop = tConfig.nGOP;
            m_tH265FixQp.u32SrcFrameRate = nSrcFrameRate;
            m_tH265FixQp.fr32DstFrameRate = tConfig.nFrameRate;
            m_tH265FixQp.u32IQp = 25;
            m_tH265FixQp.u32PQp = 30;
            m_tH265FixQp.u32BQp = 32;

            break;
        }
        case PT_H264: {
            // set h264 cbr
            const RC_INFO_T& stRCInfoCBR = tConfig.stRCInfoCBR;
            m_tH264Cbr.u32Gop = tConfig.nGOP;
            m_tH264Cbr.u32SrcFrameRate = nSrcFrameRate;
            m_tH264Cbr.fr32DstFrameRate = tConfig.nFrameRate;
            m_tH264Cbr.u32BitRate = tConfig.nBitrate;
            m_tH264Cbr.u32MinQp = ADAPTER_RANGE(stRCInfoCBR.nMinQp, 0, 51);
            m_tH264Cbr.u32MaxQp = ADAPTER_RANGE(stRCInfoCBR.nMaxQp, 0, 51);
            m_tH264Cbr.u32MinIQp = ADAPTER_RANGE(stRCInfoCBR.nMinIQp, 0, 51);
            m_tH264Cbr.u32MaxIQp = ADAPTER_RANGE(stRCInfoCBR.nMaxIQp, 0, 51);
            m_tH264Cbr.u32MinIprop = ADAPTER_RANGE(stRCInfoCBR.nMinIProp, 0, 100);
            m_tH264Cbr.u32MaxIprop = ADAPTER_RANGE(stRCInfoCBR.nMaxIProp, 0, 100);
            m_tH264Cbr.s32IntraQpDelta = ADAPTER_RANGE(stRCInfoCBR.nIntraQpDelta, -51, 51);

            // set h264 vbr
            const RC_INFO_T& stRCInfoVBR = tConfig.stRCInfoVBR;
            m_tH264Vbr.u32Gop = tConfig.nGOP;
            m_tH264Vbr.u32SrcFrameRate = nSrcFrameRate;
            m_tH264Vbr.fr32DstFrameRate = tConfig.nFrameRate;
            m_tH264Vbr.u32MaxBitRate = tConfig.nBitrate;
            m_tH264Vbr.u32MinQp = ADAPTER_RANGE(stRCInfoVBR.nMinQp, 0, 51);
            m_tH264Vbr.u32MaxQp = ADAPTER_RANGE(stRCInfoVBR.nMaxQp, 0, 51);
            m_tH264Vbr.u32MinIQp = ADAPTER_RANGE(stRCInfoVBR.nMinIQp, 0, 51);
            m_tH264Vbr.u32MaxIQp = ADAPTER_RANGE(stRCInfoVBR.nMaxIQp, 0, 51);
            m_tH264Vbr.s32IntraQpDelta = ADAPTER_RANGE(stRCInfoVBR.nIntraQpDelta, -51, 51);

            // set h264 fixQp
            m_tH264FixQp.u32Gop = tConfig.nGOP;
            m_tH264FixQp.u32SrcFrameRate = nSrcFrameRate;
            m_tH264FixQp.fr32DstFrameRate = tConfig.nFrameRate;
            m_tH264FixQp.u32IQp = 25;
            m_tH264FixQp.u32PQp = 30;
            m_tH264FixQp.u32BQp = 32;

            break;
        }
        break;
        default:
            LOG_M_E(VENC, "Payload type(%d) unrecognized.", eType);
            break;
    }
}

AX_BOOL CVideoEncoder::ResetByResolution(const VIDEO_ATTR_T& attr, AX_BOOL bStop /*= AX_TRUE*/)
{
    LOG_M(VENC, "[%d] +++", m_nChannel);
    if (bStop) {
        Stop();
    }

    AX_U8 nISPChn = CIVPSStage::GetIspChnIndex(m_nChannel);
    AX_U8 nIvpsInnerIndex = CIVPSStage::GetIvpsChnIndex(m_nChannel);

    m_tVideoConfig.nInHeight    = attr.height;
    m_tVideoConfig.nInWidth     = attr.width;
    m_tVideoConfig.nStride      = ALIGN_UP(attr.width, g_tIvpsGroupConfig[nISPChn].arrOutResolution[nIvpsInnerIndex][2]);
    m_tVideoConfig.nOutHeight   = attr.height;
    m_tVideoConfig.nOutWidth    = attr.width;
    m_tVideoConfig.nBitrate     = attr.bit_rate;
    m_tVideoConfig.nOffsetCropX = attr.x;
    m_tVideoConfig.nOffsetCropY = attr.y;
    m_tVideoConfig.nOffsetCropW = attr.w;
    m_tVideoConfig.nOffsetCropH = attr.h;

    Start(AX_FALSE);

    LOG_M(VENC, "[%d] ---", m_nChannel);
    return AX_TRUE;
}

AX_BOOL CVideoEncoder::IsParamChange(const VIDEO_ATTR_T& attr)
{
    return (m_tVideoConfig.nOutWidth != attr.width
            || m_tVideoConfig.nOutHeight != attr.height
            || m_tVideoConfig.nBitrate != attr.bit_rate) ? AX_TRUE : AX_FALSE;
}

AX_VOID CVideoEncoder::ProcOSD(CMediaFrame* pFrame)
{
    AX_U64 nVirAddr = pFrame->bIvpsFrame ? pFrame->tVideoFrame.u64VirAddr[0] : pFrame->tFrame.tFrameInfo.stVFrame.u64VirAddr[0];
    AX_VIDEO_FRAME_INFO_S tFrame = {0};
    if (pFrame->bIvpsFrame) {
        tFrame.stVFrame = pFrame->tVideoFrame;
    } else {
        tFrame = pFrame->tFrame.tFrameInfo;
    }

    AX_U32 nWidth = tFrame.stVFrame.u32Width;
    AX_U32 nHeight = tFrame.stVFrame.u32Height;
    CYuvHandler YUV((const AX_U8 *)nVirAddr, tFrame.stVFrame.u32PicStride[0], nHeight, AX_YUV420_SEMIPLANAR, 0);

    if (gOptions.IsActivedDetect() && gOptions.IsActivedDetectFromWeb()) {
        auto OSDRect = [&](AI_Detection_Box_t *p, CYuvHandler::YUV_COLOR eColor) {
            AX_S16 x0 = p->fX * nWidth;
            AX_S16 y0 = p->fY * nHeight;
            AX_U16 w  = p->fW * nWidth;
            AX_U16 h  = p->fH * nHeight;

            if (x0 < (AX_S16)nWidth && y0 < (AX_S16)nHeight && w < nWidth && h < nHeight) {
                YUV.DrawRect(x0, y0, w, h, eColor);
            }
        };

        auto OSDPoint = [&](AI_Detection_Point_t *p, CYuvHandler::YUV_COLOR eColor) {
            if (p->fX > 0 || p->fY > 0) {
                AX_S16 x0 = p->fX * nWidth;
                AX_S16 y0 = p->fY * nHeight;

                if (x0 < (AX_S16)nWidth && y0 < (AX_S16)nHeight) {
                    YUV.DrawPoint(x0, y0, 8, x0*(8-1), y0*(8-1), eColor);
                }
            }
        };

        auto OSDDrawPoseLine = [&](AI_Detection_Point_t *p) {
            CYuvHandler::YUV_COLOR LineColor = CYuvHandler::YUV_RED;

            for (auto& element : pairs) {
                switch (element.left_right_neutral) {
                case 0:
                    LineColor = CYuvHandler::YUV_BLUE;
                    break;
                case 1:
                    LineColor = CYuvHandler::YUV_DARK_GREEN;
                    break;
                default:
                    LineColor = CYuvHandler::YUV_RED;
                    break;
                }

                AX_S32 x1 = (AX_S32)(p[element.connection[0]].fX * nWidth);
                AX_S32 y1 = (AX_S32)(p[element.connection[0]].fY * nHeight);
                AX_S32 x2 = (AX_S32)(p[element.connection[1]].fX * nWidth);
                AX_S32 y2 = (AX_S32)(p[element.connection[1]].fY * nHeight);

                x1 = AX_MIN(x1, (AX_S32)(nWidth - 1));
                x1 = AX_MAX(x1, 0);
                y1 = AX_MIN(y1, (AX_S32)(nHeight - 1));
                y1 = AX_MAX(y1, 0);
                x2 = AX_MIN(x2, (AX_S32)(nWidth - 1));
                x2 = AX_MAX(x2, 0);
                y2 = AX_MIN(y2, (AX_S32)(nHeight - 1));
                y2 = AX_MAX(y2, 0);

                YUV.DrawLine(x1, y1, x2, y2, LineColor, 4);
            }
        };

        #define ObjectDraw(Obj, Color) \
            do { \
                for (AX_U32 i = 0; i < tResult.n##Obj##Size; ++i) { \
                    /* draw rect on src image */           \
                    if (tResult.t##Obj##s[i].eTrackState == AX_SKEL_TRACK_STATUS_NEW \
                        || tResult.t##Obj##s[i].eTrackState == AX_SKEL_TRACK_STATUS_UPDATE) { \
                        AI_Detection_Box_t *p = &tResult.t##Obj##s[i].tBox; \
                        OSDRect(p, Color); \
                    } \
                } \
            } while (0)

        #define ObjectDrawPose(Obj, Color) \
                    do { \
                        for (AX_U32 i = 0; i < tResult.n##Obj##Size; ++i) { \
                            if (tResult.t##Obj##s[i].nPointNum > 0) { \
                                for (AX_U8 j = 0; j < tResult.t##Obj##s[i].nPointNum; ++j) { \
                                        AI_Detection_Point_t *p = &tResult.t##Obj##s[i].tPoint[j]; \
                                        OSDPoint(p, Color); \
                                    }\
                                OSDDrawPoseLine(&tResult.t##Obj##s[i].tPoint[0]); \
                            } \
                        } \
                    } while (0)

        DETECT_RESULT_T tResult = gOptions.GetDetectResult(0);

        DETECTOR_CONFIG_PARAM_T Conf = CDetector::GetInstance()->GetConfig();

        if (AX_BIT_CHECK(Conf.nDrawRectType, AI_DRAW_RECT_TYPE_BODY)) {
            ObjectDraw(Body, CYuvHandler::YUV_WHITE);
        }
        if (AX_BIT_CHECK(Conf.nDrawRectType, AI_DRAW_RECT_TYPE_VEHICLE)) {
            ObjectDraw(Vehicle, CYuvHandler::YUV_PURPLE);
        }
        if (AX_BIT_CHECK(Conf.nDrawRectType, AI_DRAW_RECT_TYPE_CYCLE)) {
            ObjectDraw(Cycle, CYuvHandler::YUV_PURPLE);
        }
        if (AX_BIT_CHECK(Conf.nDrawRectType, AI_DRAW_RECT_TYPE_FACE)) {
            ObjectDraw(Face, CYuvHandler::YUV_YELLOW);
        }
        if (AX_BIT_CHECK(Conf.nDrawRectType, AI_DRAW_RECT_TYPE_PLATE)) {
            ObjectDraw(Plate, CYuvHandler::YUV_RED);
        }
        if (AX_BIT_CHECK(Conf.nDrawRectType, AI_DRAW_RECT_TYPE_POSE)) {
            ObjectDrawPose(Pose, CYuvHandler::YUV_DARK_GREEN);
        }
    }
}

AX_BOOL CVideoEncoder::LoadFont()
{
    AX_U16 u16W = 0;
    AX_U16 u16H = 0;
    AX_U32 u32Size = 0;
    if (!m_sfont.LoadBmp((AX_CHAR *)"./res/font.bmp", u16W, u16H, u32Size)) {
        LOG_M_E(VENC, "load font.bmp failed");
        return AX_FALSE;
    }

    return AX_TRUE;
}

AX_VOID CVideoEncoder::StopRecv()
{
    LOG_M(VENC, "[%d] +++", m_nChannel);

    AX_VENC_StopRecvFrame((VENC_CHN)m_nChannel);

    LOG_M(VENC, "[%d] ---", m_nChannel);
}

AX_VOID CVideoEncoder::StartRecv()
{
    LOG_M(VENC, "[%d] +++", m_nChannel);

    AX_VENC_RECV_PIC_PARAM_S tRecvParam;
    tRecvParam.s32RecvPicNum = -1;
    AX_VENC_StartRecvFrame(m_nChannel, &tRecvParam);

    LOG_M(VENC, "[%d] ---", m_nChannel);

    return;
}

CBmpOSD CVideoEncoder::m_sfont;
