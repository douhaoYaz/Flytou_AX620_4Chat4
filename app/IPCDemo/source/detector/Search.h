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
#include "AiSearch.h"
#include "Singleton.h"

/**
 * Search
 */
typedef struct _SEARCH_PARAM_ST {
    //PPL
    AX_SKEL_PPL_E ePPL;

    //Stream
    std::string config_path;
    DETECTOR_TRACK_SIZE_T tTrackSize;
    DETECTOR_ROI_CONFIG_T tRoi;
    std::map<std::string, DETECTOR_OBJECT_FILTER_CONFIG_T> tObjectFliter;

    //Search
    std::vector<std::string> object_types;
    float compare_score_threshold;
    AX_U32 capability;
    std::string base_img_path;
    std::string database_name;
    std::string feature_algo_type;

    _SEARCH_PARAM_ST (){
        ePPL = AX_SKEL_PPL_FACE_FEATURE;
        compare_score_threshold = AI_SERACH_FACE_COMPARE_SCORE_DEFAULT;
        capability = AI_SERACH_FACE_BASE_IMG_CAP_DEFAULT;

        memset(&tRoi, 0x00, sizeof(tRoi));

        // face fliter
        tObjectFliter["face"].nWidth = AI_FACE_FLITER_DEFAULT_WIDTH;
        tObjectFliter["face"].nHeight = AI_FACE_FLITER_DEFAULT_HEIGHT;
        tObjectFliter["face"].fConfidence = AI_FACE_FLITER_DEFAULT_CONFIDENCE;
    }
} SEARCH_PARAM_ST;

struct SEARCH_FEATURE_RESULT_ST {
    AX_BOOL m_bUsed;
    AX_VIDEO_FRAME_S m_sVideoFrame;
    std::chrono::steady_clock::time_point m_tpStart;
};

typedef struct {
    AX_U64 object_id;
    AX_U64 frame_id;
    AX_U32 track_id;
    std::string feature_info;
} SEARCH_OBJECT_INFO_ST;

#define SEARCH_API(_API_NAME_) pApi_##_API_NAME_
#define SEACRH_API_DEF(_API_NAME_, _API_RET_, _API_PARAM_) \
                            _API_RET_ (* SEARCH_API(_API_NAME_))_API_PARAM_ = nullptr

class CSearch : public CSingleton<CSearch>
{
    friend class CSingleton<CSearch>;

public:
    CSearch(AX_VOID);
    virtual ~CSearch(AX_VOID);

    virtual AX_BOOL Startup(AX_VOID);
    virtual AX_VOID Cleanup(AX_VOID);

    AX_BOOL AddFeatureToGroup(std::string infoStr, AX_SKEL_RESULT_S *algorithm_result);

    AX_BOOL CreateGroup(AX_U64 groupid);
    AX_BOOL DestoryGroup(AX_U64 groupid);
    AX_BOOL AddObjectToGroup(AX_U64 groupid, AX_U64 object_id, const AX_U8 *feature, AX_U32 feature_size,
                                       const SEARCH_OBJECT_INFO_ST &obj_info, AX_BOOL bSaveFeature = AX_TRUE);
    AX_BOOL DeleteObjectFromGroup(AX_U64 groupid, std::string infoStr);
    AX_BOOL IsGroupExist(AX_U64 groupid);

    //Feature
    AX_BOOL ProcessFeature(CMediaFrame* pFrame);
    AX_BOOL AsyncRecvFeatureResult(AX_VOID);
    AX_S32 GetFeatureInfo(const AX_CHAR *str);
    AX_S32 DeleteFeatureInfo(const AX_CHAR *str);

    //Search
    AX_BOOL SyncSearch(AX_SKEL_OBJECT_ITEM_S *object_item);

private:
    /* virtual function of CSingleton */
    AX_BOOL Init(AX_VOID) override {
        return AX_TRUE;
    };

    AX_BOOL WaitForFinish(AX_VOID);

    //Feature
    AX_BOOL SaveFeature(std::string featureInfo, const AX_U8 *feature, AX_U32 feature_size);
    AX_BOOL DeleteFeature(std::string featureInfo);
    AX_BOOL GenerateFeatureName(const AX_CHAR *str);
    AX_BOOL SetHandleConfig(AX_VOID);
    AX_BOOL InitFeatureHandle(AX_VOID);
    AX_BOOL FeatureResultHandler(AX_SKEL_RESULT_S *algorithm_result);
    AX_S32 Waitfor(AX_S32 msTimeOut = -1);

    //Load
    AX_BOOL LoadFeatureDataBase(AX_VOID);
    AX_VOID AsyncLoadFeatureThread(AX_VOID);

    //Search
    AX_BOOL Search(AX_U64 groupid, AX_U8 *features, int feature_cnt, int top_k,
                         AX_SKEL_SEARCH_RESULT_S **result);

public:
    //Feature
    AX_BOOL m_bGetFeatureResultThreadRunning;
    AX_S32 m_GetFeatureInfoStatus;

    //Load
    AX_BOOL m_bLoadFeatureDataBaseThreadRunning;

protected:
    AX_BOOL m_bFinished;
    AX_BOOL m_bForedExit;
    AX_BOOL m_bFeatureDataBaseLoaded;

private:
    SEARCH_PARAM_ST m_stSearchParam;
    std::string m_FeatureDataBaseName;
    std::map<std::string, AX_U64> m_map_object_group_id;
    std::map<AX_U64, AX_U64> m_map_group_object_id;
    AX_U64 m_total_object_num;

    CElapsedTimer m_apiElapsed;
    std::map<AX_U64, std::map<std::string, SEARCH_OBJECT_INFO_ST *>> m_mapGroupObjectInfo;
    std::map<AX_U64, AX_U64> m_setGroupId;
    std::mutex m_setMutex;
    std::mutex m_mapMutex;

    //Feature
    std::mutex m_featureMutex;
    SEARCH_FEATURE_RESULT_ST m_feature_result;
    AX_SKEL_HANDLE m_feature_handle = nullptr;
    thread *m_pGetFeatureResultThread = nullptr;
    AX_BOOL m_bGetFeatureInfo = AX_FALSE;
    std::mutex m_CaptureFeatureMutex;
    std::string m_FeatureInfoStr;

    //Load
    thread *m_pLoadFeatureDataBaseThread = nullptr;

    pthread_mutex_t m_condMutx;
    pthread_cond_t m_cond;
    pthread_condattr_t m_condattr;
};
