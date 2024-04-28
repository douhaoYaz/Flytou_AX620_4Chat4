/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/
#pragma once

#include "global.h"
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <semaphore.h>
#include "MediaFrame.h"
#include "ax_skel_api.h"
#include "TrackCropStage.h"
#include "Search.h"
#include "StageOptionHelper.h"
#include "Singleton.h"

/**
 * FHVP detection
 */
struct FRAME_ALGORITHM_RESULT_ST {
    AX_VIDEO_FRAME_S m_sVideoFrame;
    std::chrono::steady_clock::time_point m_tpStart;
};

/* Donot use memset/memcpy */
typedef struct _DETECTOR_CONFIG_PARAM_T {
    AX_SKEL_PPL_E ePPL;
    AX_U32 nTrackType;
    AX_U32 nDrawRectType;
    AX_U32 nFrameDepth;
    AX_U32 nCacheDepth;
    DETECTOR_PUSH_STRATEGY_T tPushStrategy;
    DETECTOR_STREAM_PARAM_T tStreamParam;
    AX_F32 fCropEncoderQpLevel;
    DETECTOR_TRACK_SIZE_T tTrackSize;
    DETECTOR_ROI_CONFIG_T tRoi;
    std::map<std::string, DETECTOR_OBJECT_FILTER_CONFIG_T> tObjectFliter;
    std::map<std::string, AX_SKEL_ATTR_FILTER_CONFIG_S> tAttrFliter;
    std::map<std::string, AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S> tCropThreshold;
    std::vector<std::string> tTargetVec;
    std::vector<std::string> tAnalyzerVec;
    std::vector<std::string> tPushTargetVec;
    AX_BOOL bPlateIdentify;
    AX_BOOL bPanoramaEnable;
    AX_BOOL bTrackEnable;
    AX_BOOL bPushEnable;
    AX_BOOL bPushAttrAlways;

    _DETECTOR_CONFIG_PARAM_T() {
        ePPL = AX_SKEL_PPL_HVCFP;
        nTrackType = 0;
        nDrawRectType = 0;
        nFrameDepth = AX_AI_DETECT_FRAME_DEPTH;
        nCacheDepth = AX_AI_DETECT_CACHE_LIST_DEPTH;
        fCropEncoderQpLevel = AI_TRACK_QPLEVEL_DEFAULT;
        memset(&tPushStrategy, 0x00, sizeof(tPushStrategy));
        memset(&tRoi, 0x00, sizeof(tRoi));

        bPlateIdentify = AX_TRUE;
        bPanoramaEnable = AX_FALSE;
        bTrackEnable = AX_TRUE;
        bPushEnable = AX_TRUE;
        bPushAttrAlways = AX_FALSE;

        // body fliter
        tObjectFliter["body"].nWidth = AI_BODY_FLITER_DEFAULT_WIDTH;
        tObjectFliter["body"].nHeight = AI_BODY_FLITER_DEFAULT_HEIGHT;
        tObjectFliter["body"].fConfidence = AI_BODY_FLITER_DEFAULT_CONFIDENCE;

        // vehicle fliter
        tObjectFliter["vehicle"].nWidth = AI_VEHICLE_FLITER_DEFAULT_WIDTH;
        tObjectFliter["vehicle"].nHeight = AI_VEHICLE_FLITER_DEFAULT_HEIGHT;
        tObjectFliter["vehicle"].fConfidence = AI_VEHICLE_FLITER_DEFAULT_CONFIDENCE;

        // cycle fliter
        tObjectFliter["cycle"].nWidth = AI_CYCLE_FLITER_DEFAULT_WIDTH;
        tObjectFliter["cycle"].nHeight = AI_CYCLE_FLITER_DEFAULT_HEIGHT;
        tObjectFliter["cycle"].fConfidence = AI_CYCLE_FLITER_DEFAULT_CONFIDENCE;

        // face fliter
        tObjectFliter["face"].nWidth = AI_FACE_FLITER_DEFAULT_WIDTH;
        tObjectFliter["face"].nHeight = AI_FACE_FLITER_DEFAULT_HEIGHT;
        tObjectFliter["face"].fConfidence = AI_FACE_FLITER_DEFAULT_CONFIDENCE;

        // plate fliter
        tObjectFliter["plate"].nWidth = AI_PLATE_FLITER_DEFAULT_WIDTH;
        tObjectFliter["plate"].nHeight = AI_PLATE_FLITER_DEFAULT_HEIGHT;
        tObjectFliter["plate"].fConfidence = AI_PLATE_FLITER_DEFAULT_CONFIDENCE;

        // body attribute fliter
        tAttrFliter["body"].stCommonAttrFilterConfig.fQuality = 0;

        // vehicle attribute fliter
        tAttrFliter["vehicle"].stCommonAttrFilterConfig.fQuality = 0;

        // cycle attribute fliter
        tAttrFliter["cycle"].stCommonAttrFilterConfig.fQuality = 0;

        // face attribute fliter
        tAttrFliter["face"].stFaceAttrFilterConfig.nWidth = 0;
        tAttrFliter["face"].stFaceAttrFilterConfig.nHeight = 0;
        tAttrFliter["face"].stFaceAttrFilterConfig.stPoseblur.fPitch = 180;
        tAttrFliter["face"].stFaceAttrFilterConfig.stPoseblur.fYaw = 180;
        tAttrFliter["face"].stFaceAttrFilterConfig.stPoseblur.fRoll = 180;
        tAttrFliter["face"].stFaceAttrFilterConfig.stPoseblur.fBlur = 1.0;

        // plate attribute fliter
        tAttrFliter["plate"].stCommonAttrFilterConfig.fQuality = 0;

        // body crop threhold fliter
        tCropThreshold["body"].fScaleLeft = 0;
        tCropThreshold["body"].fScaleRight = 0;
        tCropThreshold["body"].fScaleTop = 0;
        tCropThreshold["body"].fScaleBottom = 0;

        // vehicle crop threhold fliter
        tCropThreshold["vehicle"].fScaleLeft = 0;
        tCropThreshold["vehicle"].fScaleRight = 0;
        tCropThreshold["vehicle"].fScaleTop = 0;
        tCropThreshold["vehicle"].fScaleBottom = 0;

        // cycle crop threhold fliter
        tCropThreshold["cycle"].fScaleLeft = 0;
        tCropThreshold["cycle"].fScaleRight = 0;
        tCropThreshold["cycle"].fScaleTop = 0;
        tCropThreshold["cycle"].fScaleBottom = 0;

        // face crop threhold fliter
        tCropThreshold["face"].fScaleLeft = 0;
        tCropThreshold["face"].fScaleRight = 0;
        tCropThreshold["face"].fScaleTop = 0;
        tCropThreshold["face"].fScaleBottom = 0;

        // plate crop threhold fliter
        tCropThreshold["plate"].fScaleLeft = 0;
        tCropThreshold["plate"].fScaleRight = 0;
        tCropThreshold["plate"].fScaleTop = 0;
        tCropThreshold["plate"].fScaleBottom = 0;

        tAnalyzerVec.clear();
        tTargetVec.clear();
        tPushTargetVec.clear();
    }
} DETECTOR_CONFIG_PARAM_T;

typedef struct _PERF_TEST_INFO_T
{
    AX_BOOL m_bAiPerfTest;
    AX_U64 nPhyAddr;
    AX_VOID* pVirAddr;
    AX_U32 nSize;
    AX_U32 nWidth;
    AX_U32 nHeight;
} PERF_TEST_INFO_T;

class CDetector : public CSingleton<CDetector>
{
    friend class CSingleton<CDetector>;

public:
    CDetector(AX_VOID);
    virtual ~CDetector(AX_VOID);

    virtual AX_BOOL Startup(AX_VOID);
    virtual AX_VOID Cleanup(AX_VOID);

    AX_BOOL ProcessFrame(CMediaFrame* pFrame, AX_S32 nGaps = -1);
    AX_BOOL AsyncRecvDetectionResult(AX_VOID);
    AX_VOID BindCropStage(CTrackCropStage* pStage);
    DETECTOR_CONFIG_PARAM_T GetConfig(AX_VOID);
    AX_BOOL SetConfig(DETECTOR_CONFIG_PARAM_T *pConfig);
    AX_BOOL UpdateConfig(const AI_ATTR_T& tAiAttr);
    AX_BOOL SetRoi(DETECTOR_ROI_CONFIG_T *ptRoi);
    AX_BOOL SetPushStrategy(DETECTOR_PUSH_STRATEGY_T *ptPushStrategy);
    AX_BOOL SetObjectFilter(std::string strObject, const DETECTOR_OBJECT_FILTER_CONFIG_T &tObjectFliter);
    AX_BOOL SetAttrFilter(std::string strObject, const AX_SKEL_ATTR_FILTER_CONFIG_S &tAttrFliter);
    AX_BOOL SetTrackSize(DETECTOR_TRACK_SIZE_T *ptTrackSize);
    AX_BOOL SetCropEncoderQpLevel(AX_F32 fCropEncoderQpLevel);
    AX_BOOL SetPanoramaEnable(AX_BOOL bEnable);
    AX_BOOL SetCropThreshold(std::string strObject, const AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S &tCropThreshold);

private:
    /* virtual function of CSingleton */
    AX_BOOL Init(AX_VOID) override {
        return AX_TRUE;
    };

    AX_BOOL InitConfigParam(AX_VOID);
    AX_BOOL SetHandleConfig(DETECTOR_CONFIG_PARAM_T *ptConfigParam);
    AX_BOOL CreateHandle(AX_VOID);
    AX_BOOL DetectionResultHandler(AX_SKEL_RESULT_S *algorithm_result);
    AX_BOOL ClearAlgorithmData(AX_SKEL_RESULT_S *algorithm_result);
    AX_BOOL ClearAlgorithmData(AX_VOID);
    AX_BOOL WaitForFinish(AX_VOID);
    AX_BOOL LoadPerfTestInfo(AX_VOID);
    AX_BOOL UpdateAiAttr(AX_VOID);

public:
    AX_BOOL m_bGetResultThreadRunning;

protected:
    AX_BOOL m_bFinished;
    AX_BOOL m_bForcedExit;
    AX_U64 m_nFrame_id;

private:
    AX_SKEL_HANDLE m_stream_handle = nullptr;
    std::mutex m_mutex;
    std::map<AX_U64, FRAME_ALGORITHM_RESULT_ST> m_mapDataResult;
    AX_BOOL m_bThreadRunning;
    thread *m_pGetResultThread = nullptr;
    CTrackCropStage *m_pTrackCropStage = nullptr;
    CElapsedTimer m_apiElapsed;
    std::mutex m_stMutex;
    DETECTOR_CONFIG_PARAM_T m_tConfigParam;

    //Search
    CSearch *m_pObjectSearch = nullptr;

    //AI Perf Test
    PERF_TEST_INFO_T m_perfTestInfo;
};
