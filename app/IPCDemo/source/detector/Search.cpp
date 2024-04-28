/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/
#include <fstream>
#include <iostream>
#include <mutex>
#include <dlfcn.h>

#include "Search.h"
#include "OptionHelper.h"
#include "PrintHelper.h"
#include "FileUtils.h"
#include "CommonUtils.h"
#include "picojson.h"

#define SEARCH           "SEARCH"

#define PICO_OBJECT get<picojson::object>()
#define PICO_OBJECT_SIZE PICO_OBJECT.size()
#define PICO_ARRAY get<picojson::array>()
#define PICO_ARRAY_SIZE PICO_ARRAY.size()
#define PICO_VALUE get<double>()
#define PICO_BOOL get<bool>()
#define PICO_STRING get<std::string>()
#define PICO_ROOT obj.PICO_OBJECT

#define FEATURE_INFO_SIZE 32
//#define FEATURE_DATA_SIZE 280

#define SEARCH_DEFAULT_ALGO_TYPE "facehuman_image_algo"
#define SEARCH_DEFAULT_ALGO_CONFIG_PATH "./config/search_config.json"

#define SEARCH_WAITING_TIMEOUT 5000

typedef struct {
    AX_U8 Info[FEATURE_INFO_SIZE];
    AX_U32 FeatureSize;
} SEARCH_FEATURE_HEADER_ST;

typedef struct {
    SEARCH_FEATURE_HEADER_ST header;
    AX_U8 Feature[0];
} SEARCH_FEATURE_DB_ST;

#define SEARCH_API_RUN_START(_API_NAME_) m_apiElapsed.reset()
#define SEARCH_API_RUN_END(_API_NAME_) LOG_M_I(SEARCH, "Run API(%s) elapsed: %d(ms)", #_API_NAME_, m_apiElapsed.ms());

extern COptionHelper gOptions;
extern CPrintHelper gPrintHelper;

using namespace std;

// Object Calculate
#define ObjectCalculate(Obj) \
    do { \
        if (strcasecmp(object_category, #Obj) == 0) { \
            Obj##_size ++; \
        } \
    } while(0)

namespace {
void transform_algotype_string_to_ppl_type(std::string &attr_string, AX_SKEL_PPL_E &ePPL) {
    if (attr_string == "skel_body_algo") {
        ePPL = AX_SKEL_PPL_BODY;
    } else if (attr_string == "skel_pose_algo") {
        ePPL = AX_SKEL_PPL_POSE;
    } else if (attr_string == "facehuman_video_algo") {
        ePPL = AX_SKEL_PPL_FH;
    } else if (attr_string == "hvcfp_video_algo") {
        ePPL = AX_SKEL_PPL_HVCFP;
    } else if (attr_string == "facehuman_image_algo") {
        ePPL = AX_SKEL_PPL_FACE_FEATURE;
    } else if (attr_string == "hvcfp_image_algo") {
        ePPL = AX_SKEL_PPL_MAX;
    } else if (attr_string == "search_video_algo") {
        ePPL = AX_SKEL_PPL_MAX;
    } else if (attr_string == "facehuman_video_detect_algo") {
        ePPL = AX_SKEL_PPL_FH;
    } else if (attr_string == "facehuman_video_lite_algo") {
        ePPL = AX_SKEL_PPL_FH;
    } else {
        ePPL = AX_SKEL_PPL_MAX;
    }

    return;
}

AX_BOOL get_search_handle_param(SEARCH_PARAM_ST stSearchParam) {
    std::ifstream ifs(stSearchParam.config_path);

    if (ifs.fail()) {
        LOG_M_E(SEARCH, "%s not exist", stSearchParam.config_path.c_str());
        return AX_FALSE;
    }

    picojson::value obj;
    ifs >> obj;

    string err = picojson::get_last_error();
    if (!err.empty() || !obj.is<picojson::object>()) {
        LOG_M_E(SEARCH, "Failed to load json config file: %s", stSearchParam.config_path.c_str());
        return AX_FALSE;
    }

    CStageOptionHelper *pStageOption = CStageOptionHelper().GetInstance();
    AX_U32 nWidth = pStageOption->GetAiAttr().tConfig.nWidth;
    AX_U32 nHeight = pStageOption->GetAiAttr().tConfig.nHeight;

    stSearchParam.tRoi.bEnable = PICO_ROOT["roi"].PICO_OBJECT["enable"].PICO_STRING == "true" ? AX_TRUE : AX_FALSE;
    stSearchParam.tRoi.tBox.fX = (AX_F32)(PICO_ROOT["roi"].PICO_OBJECT["points"].PICO_ARRAY[0].PICO_OBJECT["x"].PICO_VALUE * nWidth);
    stSearchParam.tRoi.tBox.fY = (AX_F32)(PICO_ROOT["roi"].PICO_OBJECT["points"].PICO_ARRAY[0].PICO_OBJECT["y"].PICO_VALUE * nHeight);
    stSearchParam.tRoi.tBox.fW = (AX_F32)(PICO_ROOT["roi"].PICO_OBJECT["points"].PICO_ARRAY[1].PICO_OBJECT["x"].PICO_VALUE * nWidth -
                                          stSearchParam.tRoi.tBox.fX + 1);
    stSearchParam.tRoi.tBox.fH = (AX_F32)(PICO_ROOT["roi"].PICO_OBJECT["points"].PICO_ARRAY[1].PICO_OBJECT["y"].PICO_VALUE * nHeight -
                                          stSearchParam.tRoi.tBox.fY + 1);

    if (PICO_ROOT.end() != PICO_ROOT.find("max_track_size")) {
        if (PICO_ROOT["max_track_size"].PICO_OBJECT.end() !=
            PICO_ROOT["max_track_size"].PICO_OBJECT.find("human")) {
            stSearchParam.tTrackSize.nTrackHumanSize = PICO_ROOT["max_track_size"].PICO_OBJECT["human"].PICO_VALUE;
        }
        if (PICO_ROOT["max_track_size"].PICO_OBJECT.end() !=
            PICO_ROOT["max_track_size"].PICO_OBJECT.find("vehicle")) {
            stSearchParam.tTrackSize.nTrackVehicleSize = PICO_ROOT["max_track_size"].PICO_OBJECT["vehicle"].PICO_VALUE;
        }
        if (PICO_ROOT["max_track_size"].PICO_OBJECT.end() !=
            PICO_ROOT["max_track_size"].PICO_OBJECT.find("cycle")) {
            stSearchParam.tTrackSize.nTrackCycleSize = PICO_ROOT["max_track_size"].PICO_OBJECT["cycle"].PICO_VALUE;
        }
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("object_filter_size") && PICO_ROOT.end() != PICO_ROOT.find("object_filter")) {
        AX_U32 object_filter_size = PICO_ROOT["object_filter_size"].PICO_VALUE;

        for (size_t i = 0; i < object_filter_size; i++) {
            std::string fliter_pstrObjectCategory = PICO_ROOT["object_filter"].PICO_ARRAY[i].PICO_OBJECT["object_category"].PICO_STRING;
            stSearchParam.tObjectFliter[fliter_pstrObjectCategory].nWidth =
                PICO_ROOT["object_filter"].PICO_ARRAY[i].PICO_OBJECT["width"].PICO_VALUE;
            stSearchParam.tObjectFliter[fliter_pstrObjectCategory].nHeight =
                PICO_ROOT["object_filter"].PICO_ARRAY[i].PICO_OBJECT["height"].PICO_VALUE;
            stSearchParam.tObjectFliter[fliter_pstrObjectCategory].fConfidence =
                PICO_ROOT["object_filter"].PICO_ARRAY[i].PICO_OBJECT["confidence"].PICO_VALUE;
        }
    }

    return AX_TRUE;
}

AX_BOOL get_search_init_param(SEARCH_PARAM_ST &stSearchParam, std::string config_path)
{
    std::ifstream ifs(config_path);

    if (ifs.fail()) {
        LOG_M_E(SEARCH, "%s not exist", config_path.c_str());
        return AX_FALSE;
    }

    picojson::value obj;
    ifs >> obj;

    string err = picojson::get_last_error();
    if (!err.empty() || !obj.is<picojson::object>()) {
        LOG_M_E(SEARCH, "Failed to load json config file: %s", stSearchParam.config_path.c_str());
        return AX_FALSE;
    }

    stSearchParam.object_types.resize(PICO_ROOT["search_info"].PICO_OBJECT["object_types"].PICO_ARRAY_SIZE);
    for (size_t i = 0; i < PICO_ROOT["search_info"].PICO_OBJECT["object_types"].PICO_ARRAY_SIZE; i++) {
        stSearchParam.object_types[i] =
            PICO_ROOT["search_info"].PICO_OBJECT["object_types"].PICO_ARRAY[i].PICO_STRING;
    }
    stSearchParam.compare_score_threshold =
        PICO_ROOT["search_info"].PICO_OBJECT["compare_score_threshold"].PICO_VALUE;
    stSearchParam.capability =
        PICO_ROOT["search_info"].PICO_OBJECT["capability"].PICO_VALUE;
    stSearchParam.base_img_path =
        PICO_ROOT["search_info"].PICO_OBJECT["base_img_path"].PICO_STRING;
    stSearchParam.database_name =
        PICO_ROOT["search_info"].PICO_OBJECT["database_name"].PICO_STRING;
    stSearchParam.feature_algo_type =
        PICO_ROOT["search_info"].PICO_OBJECT["feature_algo_type"].PICO_STRING;

    //default
    if (stSearchParam.compare_score_threshold <= 0) {
        stSearchParam.compare_score_threshold = AI_SERACH_FACE_COMPARE_SCORE_DEFAULT;
    }

    //default
    if (stSearchParam.capability <= 0) {
        stSearchParam.capability = AI_SERACH_FACE_BASE_IMG_CAP_DEFAULT;
    }

    return AX_TRUE;
}
}

//////////////////////////////////////////////////////////////////
CSearch::CSearch(AX_VOID)
{
    m_map_object_group_id = {{"face", 1}, {"body", 2}, {"vehicle", 3}, {"cycle", 4}};
    m_map_group_object_id = {{1, 1}, {2, 1}, {3, 1}, {4, 1}};

    m_total_object_num = 0;

    m_bFinished = AX_TRUE;
    m_bForedExit = AX_FALSE;
    m_bFeatureDataBaseLoaded = AX_FALSE;

    //Feature
    memset(&m_feature_result, 0x00, sizeof(m_feature_result));
    m_GetFeatureInfoStatus = 0;
    m_feature_handle = nullptr;
    m_bGetFeatureResultThreadRunning = AX_FALSE;
    m_pGetFeatureResultThread = nullptr;
    m_bGetFeatureInfo = AX_FALSE;

    //Load
    m_bLoadFeatureDataBaseThreadRunning = AX_FALSE;
    m_pLoadFeatureDataBaseThread = nullptr;

    m_stSearchParam.compare_score_threshold = 71.0;
    m_stSearchParam.capability = 10000;
    m_stSearchParam.feature_algo_type = SEARCH_DEFAULT_ALGO_TYPE;
    m_stSearchParam.config_path = SEARCH_DEFAULT_ALGO_CONFIG_PATH;

    // init mutex
    if (0 != pthread_mutex_init(&m_condMutx, NULL)) {
        LOG_M_W(SEARCH, "pthread_mutex_init() fail");
    }

    if (0 != pthread_condattr_init(&m_condattr)) {
        LOG_M_W(SEARCH, "pthread_condattr_init() fail");
    }

    if (0 != pthread_condattr_setclock(&m_condattr, CLOCK_MONOTONIC)) {
        LOG_M_W(SEARCH, "pthread_condattr_setclock() fail");
     }

     if (0 != pthread_cond_init(&m_cond, &m_condattr)) {
         LOG_M_W(SEARCH, "pthread_cond_init() fail");
     }
}

CSearch::~CSearch(AX_VOID)
{

}

AX_BOOL CSearch::Startup(AX_VOID)
{
    AX_S32 nRet = AX_SKEL_SUCC;

    LOG_M(SEARCH, "+++");

    if (!gOptions.IsActivedSearch()) {
        return AX_FALSE;
    }

    nRet = AX_SKEL_Search_Init();

    if (AX_SKEL_SUCC != nRet) {
        LOG_M_E(SEARCH, "AX_SKEL_Search_Init error nRet=0x%x", nRet);

        return AX_FALSE;
    }

    //Stream Param
    m_stSearchParam.config_path = gOptions.GetDetectionConfigPath();

    //get search parameter
    get_search_init_param(m_stSearchParam, m_stSearchParam.config_path);

    transform_algotype_string_to_ppl_type(m_stSearchParam.feature_algo_type, m_stSearchParam.ePPL);

    //Check search param
    if (m_stSearchParam.base_img_path.size() == 0) {
        LOG_M_E(SEARCH, "base_img path empty");

        return AX_FALSE;
    }

    if (m_stSearchParam.database_name.size() == 0) {
        LOG_M_E(SEARCH, "database name empty");

        return AX_FALSE;
    }

    if (!InitFeatureHandle()) {
        LOG_M_E(SEARCH, "InitFeatureHandle error");

        return AX_FALSE;
    }

    for (size_t i = 0; i < m_stSearchParam.object_types.size(); i ++) {
        AX_U64 groupid = m_map_object_group_id[m_stSearchParam.object_types[i]];

        if (!CreateGroup(groupid)) {
            return AX_FALSE;
        }

        LOG_M(SEARCH, "CreateGroup(%lld) for feature: %s", groupid, m_stSearchParam.object_types[0].c_str());
    }

    //Load database
    LoadFeatureDataBase();

    m_bFinished = AX_TRUE;
    m_bForedExit = AX_FALSE;

    LOG_M(SEARCH, "---");

    return AX_TRUE;
}

AX_VOID CSearch::Cleanup(AX_VOID)
{
    LOG_M(SEARCH, "+++");

    m_bForedExit = AX_TRUE;

    m_bGetFeatureResultThreadRunning = AX_FALSE;

    WaitForFinish();

    for (size_t i = 0; i < m_stSearchParam.object_types.size(); i++) {
        AX_U64 groupid = m_map_object_group_id[m_stSearchParam.object_types[i]];

        DestoryGroup(groupid);
    }

    m_featureMutex.lock();
    if (m_feature_result.m_bUsed) {
        memset(&m_feature_result, 0x00, sizeof(m_feature_result));
    }
    m_featureMutex.unlock();

    m_mapMutex.lock();
    for (auto &groupObjectInfo : m_mapGroupObjectInfo) {
        for (auto &objectInfo : groupObjectInfo.second) {
            delete objectInfo.second;
        }
        groupObjectInfo.second.clear();
    }

    m_mapGroupObjectInfo.clear();
    m_mapMutex.unlock();

    m_setMutex.lock();
    for (auto &groupId : m_setGroupId) {
        AX_SKEL_Search_Destroy(groupId.second);
    }
    m_setGroupId.clear();
    m_setMutex.unlock();

    if (m_feature_handle) {
        AX_SKEL_Destroy(m_feature_handle);
    }

    AX_SKEL_Search_DeInit();

    if (m_pGetFeatureResultThread && m_pGetFeatureResultThread->joinable()) {
        m_pGetFeatureResultThread->join();

        delete m_pGetFeatureResultThread;
        m_pGetFeatureResultThread = nullptr;
    }

    LOG_M(SEARCH, "---");
}

AX_VOID CSearch::AsyncLoadFeatureThread(AX_VOID)
{
    FILE* pFile = NULL;
    AX_U32 db_file_size = 0;
    AX_U32 total_len_read = 0;

    LOG_M(SEARCH, "Start load feature database");

    std::chrono::steady_clock::time_point beginTime = std::chrono::steady_clock::now();

    //open database
    pFile = fopen(m_FeatureDataBaseName.c_str(), "rb");

    if (!pFile) {
        goto EXIT;
    }

    fseek(pFile, 0, SEEK_END);
    db_file_size = ftell(pFile);
    rewind(pFile);

    total_len_read = 0;

    while(!m_bForedExit && total_len_read < db_file_size) {
        AX_U64 groupid = m_map_object_group_id["face"];
        AX_U64 object_id = m_map_group_object_id[groupid];

        if (m_mapGroupObjectInfo[groupid].size() > m_stSearchParam.capability) {
            LOG_M_E(SEARCH, "Exceed the search capability (%d)", m_stSearchParam.capability);
            break;
        }

        //SEARCH_FEATURE_DB_ST
        SEARCH_FEATURE_HEADER_ST feature_item_header = {0};

        //read header
        AX_U32 head_size = fread((AX_U8 *)&feature_item_header, 1, sizeof(feature_item_header), pFile);

        //check header
        if (head_size != sizeof(feature_item_header)) {
            break;
        }

        //check feature size
        if (feature_item_header.FeatureSize == 0) {
            break;
        }

        total_len_read += head_size;

        AX_U8 *feature = (AX_U8 *)malloc(feature_item_header.FeatureSize);

        if (!feature) {
            LOG_M_E(SEARCH, "Memory alloc (%d) fail", feature_item_header.FeatureSize);
            break;
        }

        //read feature data
        AX_U32 feature_size = fread(feature, 1, feature_item_header.FeatureSize, pFile);

        //check feature data
        if (feature_size != feature_item_header.FeatureSize) {
            free(feature);
            break;
        }

        total_len_read += feature_size;

        SEARCH_OBJECT_INFO_ST object_info;
        m_mapMutex.lock();
        object_info.object_id = m_map_group_object_id[groupid];
        m_mapMutex.unlock();
        object_info.frame_id = 0;
        object_info.track_id = 0;
        object_info.feature_info = (AX_CHAR *)feature_item_header.Info;

        if (!AddObjectToGroup(groupid, object_id, feature, feature_size, object_info, AX_FALSE)) {
            free(feature);
            continue;
        }

        free(feature);

        m_mapMutex.lock();
        m_map_group_object_id[groupid] ++;
        m_total_object_num ++;
        m_mapMutex.unlock();
    }

EXIT:
    m_bFeatureDataBaseLoaded = AX_TRUE;

    if (pFile) {
        fclose(pFile);
    }

    auto endTime = std::chrono::steady_clock::now();
    AX_U32 nElapsed = (AX_U32)(std::chrono::duration_cast<std::chrono::milliseconds>(endTime - beginTime).count());

    LOG_M(SEARCH, "Finish load feature database, loaded: %lld, elapsed: %d(ms)", m_total_object_num, nElapsed);

    return;
}

AX_BOOL CSearch::LoadFeatureDataBase(AX_VOID)
{
    //Init database file
    if (m_stSearchParam.base_img_path.size() > 0 && m_stSearchParam.database_name.size() > 0) {
        AX_CHAR sz_dat[256] = {0};
        size_t sz_dat_len = 0;

        sz_dat_len = snprintf(sz_dat, sizeof(sz_dat) - 1, "%s", m_stSearchParam.base_img_path.c_str());

        if (sz_dat[sz_dat_len - 1] == '/') {
            sz_dat[sz_dat_len - 1] = '\0';
        }

        snprintf(sz_dat + strlen(sz_dat), sizeof(sz_dat) - 1, "/%s", m_stSearchParam.database_name.c_str());

        m_FeatureDataBaseName = sz_dat;

         //try making the dir if not exist
        if (access(m_stSearchParam.base_img_path.c_str(), 0) != 0) {
            if (mkdir(m_stSearchParam.base_img_path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
                LOG_M_E(SEARCH, "Could not create base_img path: %s", m_stSearchParam.base_img_path.c_str());
            }
        }
    }
    else {
        m_bFeatureDataBaseLoaded = AX_TRUE;

        return AX_FALSE;
    }

    std::thread t(&CSearch::AsyncLoadFeatureThread, this);

    t.detach();

    return AX_TRUE;
}

AX_BOOL CSearch::CreateGroup(AX_U64 groupid)
{
    AX_S32 nRet = AX_SKEL_SUCC;

    if (IsGroupExist(groupid)) {
        LOG_M_E(SEARCH, "groupid(%lld) already exist");

        return AX_FALSE;
    }
    else {
        nRet = AX_SKEL_Search_Create(groupid);

        if (AX_SKEL_SUCC == nRet) {
            m_setMutex.lock();
            m_setGroupId[groupid] = groupid;
            m_setMutex.unlock();
        }
        else {
            LOG_M_E(SEARCH, "AX_SKEL_Search_Create groupid(%lld) failed", groupid);

            return AX_FALSE;
        }
    }

    return AX_TRUE;
}

AX_BOOL CSearch::DestoryGroup(AX_U64 groupid)
{
    AX_S32 nRet = AX_SKEL_SUCC;

    if (IsGroupExist(groupid)) {
        nRet = AX_SKEL_Search_Destroy(groupid);

        if (AX_SKEL_SUCC == nRet) {
            m_setMutex.lock();
            m_setGroupId.erase(groupid);
            m_setMutex.unlock();
        }
        else {
            LOG_M_E(SEARCH, "AX_SKEL_Search_Destroy groupid(%lld) failed", groupid);
        }
    }
    else {
        LOG_M_E(SEARCH, "groupid(%lld) not exist", groupid);
    }

    return AX_TRUE;
}

AX_BOOL CSearch::AddFeatureToGroup(std::string infoStr, AX_SKEL_RESULT_S *algorithm_result)
{
    auto check_object = [&](const char *object_category) -> AX_BOOL {
        auto iterator = std::find_if(
            m_stSearchParam.object_types.begin(), m_stSearchParam.object_types.end(),
            [&](std::string &group_object) { return group_object == object_category; });
        if (iterator == m_stSearchParam.object_types.end()) {
            return AX_FALSE;
        } else {
            return AX_TRUE;
        }
    };

    if (!algorithm_result || algorithm_result->nObjectSize == 0) {
        LOG_M_W(SEARCH, "no object and face");
        return AX_FALSE;
    }

    // Object Definition
    int Face_size = 0;

    for (size_t i = 0; i < algorithm_result->nObjectSize; i++) {
        const char *object_category = (const char *)algorithm_result->pstObjectItems[i].pstrObjectCategory;
        if (object_category) {
            // Object Calculate
            ObjectCalculate(Face);
        }
    }

    if (Face_size == 0) {
        LOG_M_W(SEARCH, "no face");
        return AX_FALSE;
    }

    //TODO
    if (Face_size > 1) {
        LOG_M_W(SEARCH, "Face_size: %d, exceed one face", Face_size);
        return AX_FALSE;
    }

    AX_BOOL bRet = AX_FALSE;

    for (size_t i = 0; i < algorithm_result->nObjectSize; i ++) {
        if (check_object(algorithm_result->pstObjectItems[i].pstrObjectCategory) &&
            algorithm_result->pstObjectItems[i].nFeatureSize > 0) {
            char *object_category = (char *)algorithm_result->pstObjectItems[i].pstrObjectCategory;
            AX_U8 *feature = (AX_U8 *)algorithm_result->pstObjectItems[i].pstFeatureItem[0].pstrValue;
            AX_U32 feature_size = algorithm_result->pstObjectItems[i].pstFeatureItem[0].nValueSize;

            AX_U64 groupid = m_map_object_group_id[object_category];
            AX_U64 object_id = m_map_group_object_id[groupid];
            SEARCH_OBJECT_INFO_ST object_info;
            object_info.object_id = object_id;
            object_info.frame_id = algorithm_result->pstObjectItems[i].nFrameId;
            object_info.track_id = algorithm_result->pstObjectItems[i].nTrackId;
            object_info.feature_info = infoStr;

            if (m_mapGroupObjectInfo[groupid].size() > m_stSearchParam.capability) {
                LOG_M_E(SEARCH, "Exceed the search capability (%d)", m_stSearchParam.capability);
                return AX_FALSE;
            }

            if (!AddObjectToGroup(groupid, object_id, feature, feature_size, object_info)) {
                return AX_FALSE;
            }

            m_mapMutex.lock();
            m_map_group_object_id[groupid] ++;
            m_total_object_num ++;
            m_mapMutex.unlock();

            bRet = AX_TRUE;
        }
    }

    return bRet;
}

AX_BOOL CSearch::AddObjectToGroup(AX_U64 groupid, AX_U64 object_id, const AX_U8 *feature, AX_U32 feature_size,
                                   const SEARCH_OBJECT_INFO_ST &obj_info, AX_BOOL bSaveFeature /* = AX_TRUE*/) {
    AX_BOOL bRet = AX_FALSE;
    AX_S32 nRet = AX_SKEL_SUCC;

    if (IsGroupExist(groupid)) {
        auto iter = m_mapGroupObjectInfo[groupid].find(obj_info.feature_info.c_str());
        if (iter != m_mapGroupObjectInfo[groupid].end()) {
            LOG_M(SEARCH, "groupid(%lld) %s already exist", groupid, obj_info.feature_info.c_str());
            return bRet;
        }

        SEARCH_OBJECT_INFO_ST *pobject_info = new SEARCH_OBJECT_INFO_ST(obj_info);
        AX_SKEL_SEARCH_FEATURE_PARAM_S optparam = {0};
        AX_U64 faceids[1];
        void *faceinfos[1];
        faceids[0] = object_id;
        faceinfos[0] = (void *)pobject_info;
        pobject_info->feature_info = obj_info.feature_info;
        pobject_info->object_id = object_id;
        pobject_info->frame_id = 0;
        pobject_info->track_id = 0;

        optparam.nBatchSize = 1;
        optparam.pObjectIds = faceids;
        optparam.pFeatures = (AX_U8 *)feature;
        optparam.ppObjectInfos = faceinfos;

        nRet = AX_SKEL_Search_InsertFeature(groupid, &optparam);

        if (AX_SKEL_SUCC == nRet) {
            if (bSaveFeature) {
                bRet = SaveFeature(obj_info.feature_info, feature, feature_size);

                if (!bRet) {
                    AX_SKEL_Search_DeleteFeature(groupid, object_id, (AX_VOID **)&pobject_info);

                    delete pobject_info;
                }
                else {
                    m_mapMutex.lock();
                    m_mapGroupObjectInfo[groupid][obj_info.feature_info] = pobject_info;
                    m_mapMutex.unlock();
                }
            }
            else {
                m_mapMutex.lock();
                m_mapGroupObjectInfo[groupid][obj_info.feature_info] = pobject_info;
                m_mapMutex.unlock();

                bRet = AX_TRUE;
            }
        }
        else {
            delete pobject_info;

            LOG_M_E(SEARCH, "AX_SKEL_Search_InsertFeature groupid(%lld) failed: 0x%X", groupid, nRet);
        }
    }
    else {
        LOG_M_E(SEARCH, "groupid(%lld) not exist", groupid);
    }

    return bRet;
}

AX_BOOL CSearch::DeleteObjectFromGroup(AX_U64 groupid, std::string infoStr)
{
    AX_BOOL bRet = AX_FALSE;
    AX_S32 nRet = AX_SKEL_SUCC;
    SEARCH_OBJECT_INFO_ST *obj_info = NULL;

    if (IsGroupExist(groupid)) {
        auto iter = m_mapGroupObjectInfo[groupid].find(infoStr);
        if (iter != m_mapGroupObjectInfo[groupid].end()) {
            AX_U64 object_id = iter->second->object_id;

            nRet = AX_SKEL_Search_DeleteFeature(groupid, object_id, (AX_VOID **)&obj_info);

            if (AX_SKEL_SUCC == nRet) {
                m_mapMutex.lock();
                delete m_mapGroupObjectInfo[groupid][infoStr];
                m_mapGroupObjectInfo[groupid].erase(iter);
                m_mapMutex.unlock();

                bRet = DeleteFeature(infoStr);

                LOG_M(SEARCH, "feature groupid(%lld)(%lld) %s deleted", groupid, object_id, infoStr.c_str());
            } else {
                LOG_M_E(SEARCH, "AX_SKEL_Search_DeleteFeature groupid(%lld)(%lld) %s failed", groupid, object_id, infoStr.c_str());
            }
        }
        else {
            LOG_M_E(SEARCH, "groupid(%lld) %s not find", groupid, infoStr.c_str());
        }
    }
    else {
        LOG_M_E(SEARCH, "groupid(%lld) not exist", groupid);
    }

    return bRet;
}

AX_BOOL CSearch::Search(AX_U64 groupid, AX_U8 *features, int feature_cnt, int top_k,
                         AX_SKEL_SEARCH_RESULT_S **result)
{
    AX_BOOL bRet = AX_FALSE;
    AX_S32 nRet = AX_SKEL_SUCC;

    if (IsGroupExist(groupid)) {
        AX_SKEL_SEARCH_PARAM_S optparam;
        optparam.nBatchSize = 1;
        optparam.nTop_k = top_k;
        optparam.pFeatures = features;

        nRet = AX_SKEL_Search(groupid, &optparam, result);

        if (AX_SKEL_SUCC == nRet) {
            bRet = AX_TRUE;
        }
        else {
            LOG_M_E(SEARCH, "AX_SKEL_Search groupid(%lld) failed nRet: 0x%x", groupid, nRet);
        }
    }
    else {
        LOG_M_E(SEARCH, "groupid(%lld) not exist", groupid);
    }

    return bRet;
}

AX_BOOL CSearch::SyncSearch(AX_SKEL_OBJECT_ITEM_S *object_item)
{
    AX_BOOL bRet = AX_FALSE;

    if (!gOptions.IsActivedSearchFromWeb()) {
        return AX_TRUE;
    }

    if (!object_item) {
        return AX_FALSE;
    }

    if (m_total_object_num == 0) {
        LOG_M_I(SEARCH, "no feature data list, ignore");
        return AX_FALSE;
    }

    auto check_object = [&](char *object_category) -> AX_BOOL {
        auto iterator = std::find_if(
            m_stSearchParam.object_types.begin(), m_stSearchParam.object_types.end(),
            [&](std::string &group_object) { return group_object == object_category; });
        if (iterator == m_stSearchParam.object_types.end()) {
            return AX_FALSE;
        } else {
            return AX_TRUE;
        }
    };

    char *object_category = (char *)object_item->pstrObjectCategory;
    if (check_object(object_category) && object_item->nFeatureSize > 0) {
        AX_U64 groupid = m_map_object_group_id[object_category];
        AX_U8 *feature = (AX_U8 *)object_item->pstFeatureItem[0].pstrValue;
        AX_SKEL_SEARCH_RESULT_S *search_result = nullptr;

        bRet = Search(groupid, feature, 1, 1, &search_result);

        if (!bRet) {
            return AX_FALSE;
        }

        bRet = AX_FALSE;

        for (size_t i = 0; i < search_result->nBatchSize; i++) {
            for (size_t j = 0; j < search_result->nTop_k; j++) {
                if (search_result->pfScores[i * search_result->nTop_k + j] < m_stSearchParam.compare_score_threshold) {
                    continue;
                }

                int object_group_index = search_result->pObjectIds[i * search_result->nTop_k + j];
                SEARCH_OBJECT_INFO_ST *object_info =
                                        (SEARCH_OBJECT_INFO_ST *)search_result->ppObjectInfos[i * search_result->nTop_k + j];

                LOG_M(SEARCH, "Found(feature: %s, track_id: %lld, score: %.2f, name: %s, index: %d)",
                                object_category,
                                object_item->nTrackId,
                                search_result->pfScores[i * search_result->nTop_k + j],
                                object_info->feature_info.c_str(),
                                object_group_index);

                bRet = AX_TRUE;
            }
        }

        AX_SKEL_Release(search_result);
    }

    return bRet;
}

AX_BOOL CSearch::IsGroupExist(AX_U64 groupid)
{
    AX_BOOL bRet = AX_FALSE;

    m_setMutex.lock();
    auto iter = m_setGroupId.find(groupid);
    if (iter != m_setGroupId.end()) {
        bRet = AX_TRUE;
    } else {
        bRet = AX_FALSE;
    }
    m_setMutex.unlock();

    return bRet;
}

AX_BOOL CSearch::ProcessFeature(CMediaFrame* pFrame)
{
    AX_BOOL bRet = AX_FALSE;

    if (m_bForedExit) {
        return bRet;
    }

    //Get feature info
    if (!gOptions.IsActivedSearchFromWeb() || !m_bGetFeatureInfo) {
        return bRet;
    }

    m_CaptureFeatureMutex.lock();
    m_bGetFeatureInfo = AX_FALSE;
    m_CaptureFeatureMutex.unlock();

    if (!m_feature_handle) {
        LOG_M_E(SEARCH, "Stream handle not init");
        goto PROC_EXIT;
    }

    if (m_feature_result.m_bUsed) {
        LOG_M_E(SEARCH, "Feature is running");
        goto PROC_EXIT;
    }

    LOG_M_I(SEARCH, "+++");

    if (!m_bForedExit) {
        AX_VIDEO_FRAME_INFO_S tFrame = {0};
        if (pFrame->bIvpsFrame) {
            tFrame.stVFrame = pFrame->tVideoFrame;
        } else {
            tFrame = pFrame->tFrame.tFrameInfo;
        }

        m_bFinished = AX_FALSE;

        AX_SKEL_FRAME_S stFrame = {0};
        stFrame.nFrameId = (AX_U64)pFrame->nFrameID;
        stFrame.pUserData = this;
        
        stFrame.stFrame = tFrame.stVFrame;

        AX_S32 nRet = AX_SKEL_SUCC;

        {
            m_featureMutex.lock();
            m_feature_result.m_bUsed = AX_TRUE;
            m_feature_result.m_sVideoFrame = tFrame.stVFrame;
            m_feature_result.m_tpStart = std::chrono::steady_clock::now();

            m_featureMutex.unlock();
        }

        nRet = AX_SKEL_SendFrame(m_feature_handle, &stFrame, 0);

        m_bFinished = AX_TRUE;

        if (AX_SKEL_SUCC != nRet) {
            m_featureMutex.lock();
            m_feature_result.m_bUsed = AX_FALSE;
            m_featureMutex.unlock();
        }
        else {
            bRet = AX_TRUE;
        }
    }

    LOG_M_I(SEARCH, "---");

PROC_EXIT:
    if (!bRet) {
        m_GetFeatureInfoStatus = 1;

        pthread_mutex_lock(&m_condMutx);
        pthread_cond_signal(&m_cond);    // send signal
        pthread_mutex_unlock(&m_condMutx);
    }

    return bRet;
}

AX_BOOL CSearch::WaitForFinish(AX_VOID)
{
    while(!m_bFinished) {
        CTimeUtils::msSleep(10);
    }

    //Wait handle finish
    auto waitStart = std::chrono::steady_clock::now();
    do {
        m_featureMutex.lock();
        AX_BOOL bFeatureRunning = m_feature_result.m_bUsed;
        m_featureMutex.unlock();

        if (!bFeatureRunning) {
            break;
        }

        auto waitEnd = std::chrono::steady_clock::now();
        AX_U32 nElapsed = (AX_U32)(std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count());

        if (nElapsed >= SEARCH_WAITING_TIMEOUT) {
            break;
        }

        CTimeUtils::msSleep(10);
    } while(1);

    return AX_TRUE;
}

static AX_VOID AsyncRecvAlgorithmFeatureResultThread(AX_VOID *__this)
{
    CSearch *pThis = (CSearch *)__this;

    prctl(PR_SET_NAME, "RecvFeatureThread");

    while (pThis->m_bGetFeatureResultThreadRunning) {
        if (!pThis->AsyncRecvFeatureResult()) {
            CTimeUtils::msSleep(1);
        }
    }
}

AX_BOOL CSearch::AsyncRecvFeatureResult(AX_VOID)
{
    AX_SKEL_RESULT_S *palgorithm_result = nullptr;

    AX_S32 nRet = AX_SKEL_GetResult(m_feature_handle, &palgorithm_result, -1);

    if (AX_SKEL_SUCC != nRet) {
        if (AX_ERR_SKEL_QUEUE_EMPTY != nRet) {
            LOG_M_E(SEARCH, "AX_SKEL_GetResult error: %d", nRet);
        }

        return AX_FALSE;
    }

    return FeatureResultHandler(palgorithm_result);
}

AX_BOOL CSearch::FeatureResultHandler(AX_SKEL_RESULT_S *algorithm_result)
{
    AX_BOOL bRet = AX_FALSE;
    auto endTime = std::chrono::steady_clock::now();

    if (!algorithm_result) {
        return AX_FALSE;
    }

    bRet = AddFeatureToGroup(m_FeatureInfoStr, algorithm_result);

    m_featureMutex.lock();
    if (m_feature_result.m_bUsed) {
        AX_U32 nElapsed = (AX_U32)(std::chrono::duration_cast<std::chrono::milliseconds>(endTime - m_feature_result.m_tpStart).count());

        LOG_M_I(SEARCH, "Get feature elapsed %d(ms)", nElapsed);

        memset(&m_feature_result, 0x00, sizeof(m_feature_result));
    }
    m_featureMutex.unlock();

    AX_SKEL_Release(algorithm_result);

    if (bRet) {
        m_GetFeatureInfoStatus = 0;
    }
    else {
        m_GetFeatureInfoStatus = 1;
    }

    pthread_mutex_lock(&m_condMutx);
    pthread_cond_signal(&m_cond);    // send signal
    pthread_mutex_unlock(&m_condMutx);

    return bRet;
}

AX_BOOL CSearch::InitFeatureHandle(AX_VOID)
{
    AX_S32 nRet = AX_SKEL_SUCC;
    AX_BOOL bRet = AX_TRUE;

    // get capability
    {
        const AX_SKEL_CAPABILITY_S *pstCapability = NULL;

        nRet = AX_SKEL_GetCapability(&pstCapability);

        if (0 != nRet) {
            LOG_M_E(SEARCH, "SKEL get capability fail, ret = 0x%x", nRet);

            if (pstCapability) {
                AX_SKEL_Release((AX_VOID *)pstCapability);
            }
            return AX_FALSE;
        }

        LOG_M(SEARCH, "Use algo: %s", m_stSearchParam.feature_algo_type.c_str());

        AX_BOOL find_algo_type = AX_FALSE;
        for (size_t i = 0; i < pstCapability->nPPLConfigSize; i++) {
            LOG_M_I(SEARCH, "SKEL capability[%d]: (ePPL: %d, PPLConfigKey: %s)", i, pstCapability->pstPPLConfig[i].ePPL,
                    pstCapability->pstPPLConfig[i].pstrPPLConfigKey);
            if (pstCapability->pstPPLConfig[i].ePPL == m_stSearchParam.ePPL) {
                find_algo_type = AX_TRUE;
                break;
            }
        }

        if (pstCapability) {
            AX_SKEL_Release((AX_VOID *)pstCapability);
        }

        if (!find_algo_type) {
            LOG_M_E(SEARCH, "not find algo_type for %s", m_stSearchParam.feature_algo_type.c_str());
            return AX_FALSE;
        }
    }

    if (m_stSearchParam.config_path.size() > 0) {
        bRet = get_search_handle_param(m_stSearchParam);
    } else {
        LOG_M_E(SEARCH, "config_path invalid");
        return AX_FALSE;
    }

    if (!bRet) {
        LOG_M_E(SEARCH, "get handle param error");
        return AX_FALSE;
    }

    AX_SKEL_HANDLE_PARAM_S stHandleParam;
    CStageOptionHelper *pStageOption = CStageOptionHelper().GetInstance();

    memset(&stHandleParam, 0x00, sizeof(AX_SKEL_HANDLE_PARAM_S));
    stHandleParam.ePPL = m_stSearchParam.ePPL;
    stHandleParam.nFrameDepth = 1;
    stHandleParam.nFrameCacheDepth = 1;
    stHandleParam.nWidth = pStageOption->GetAiAttr().tConfig.nWidth;
    stHandleParam.nHeight = pStageOption->GetAiAttr().tConfig.nHeight;

    nRet = AX_SKEL_Create(&stHandleParam, &m_feature_handle);

    if (0 != nRet) {
        LOG_M_E(SEARCH, "SKEL Create Handle fail, ret = 0x%x", nRet);

        return AX_FALSE;
    }

    bRet = SetHandleConfig();

    if (!bRet) {
        LOG_M_E(SEARCH, "SetStreamConfig error");
        return AX_FALSE;
    }

    m_bGetFeatureResultThreadRunning = AX_TRUE;
    m_pGetFeatureResultThread = new thread(AsyncRecvAlgorithmFeatureResultThread, this);

    return AX_TRUE;
}

AX_BOOL CSearch::SetHandleConfig(AX_VOID)
{
    if (!m_feature_handle) {
        LOG_M_E(SEARCH, "stream handle null..");

        return AX_FALSE;
    }

    return AX_TRUE;
}

AX_BOOL CSearch::GenerateFeatureName(const AX_CHAR *str)
{
    AX_CHAR sz_dat[128] = {0};

    if (!str) {
        time_t t;
        struct tm tm;

        t = time(NULL);
        localtime_r(&t, &tm);

        snprintf(sz_dat, sizeof(sz_dat) - 1, "face_%04d%02d%02d%02d%02d%02d",
                tm.tm_year + 1900, tm.tm_mon + 1,tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    else {
        snprintf(sz_dat, sizeof(sz_dat) - 1, "face_%s", str);
    }

    m_FeatureInfoStr = sz_dat;

    return AX_TRUE;
}

AX_BOOL CSearch::SaveFeature(std::string featureInfo, const AX_U8 *feature, AX_U32 feature_size)
{
    if (m_FeatureDataBaseName.size() > 0
        && featureInfo.size() > 0
        && feature
        && feature_size > 0) {
         //try making the dir if not exist
        if (access(m_stSearchParam.base_img_path.c_str(), 0) != 0) {
            if (mkdir(m_stSearchParam.base_img_path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
                LOG_M_E(SEARCH, "Could not create base_img path: %s", m_stSearchParam.base_img_path.c_str());
                return AX_FALSE;
            }
        }

        //SEARCH_FEATURE_DB_ST
        AX_U32 data_size = sizeof(SEARCH_FEATURE_HEADER_ST) + feature_size;
        AX_U8 *data_ptr = (AX_U8 *)malloc(data_size);

        if (!data_ptr) {
            LOG_M_E(SEARCH, "Memory alloc (%d) fail", data_size);

            return AX_FALSE;
        }

        memset(data_ptr, 0x00, data_size);
        memcpy(data_ptr, featureInfo.c_str(), (featureInfo.size() > (FEATURE_INFO_SIZE - 1)) ? (FEATURE_INFO_SIZE - 1) : featureInfo.size());
        memcpy(data_ptr + FEATURE_INFO_SIZE, (AX_U8 *)&feature_size, sizeof(AX_U16));
        memcpy(data_ptr + sizeof(SEARCH_FEATURE_HEADER_ST), feature, feature_size);

        //open database
        FILE* pFile = fopen(m_FeatureDataBaseName.c_str(), "a+");

        if (!pFile) {
            LOG_M_E(SEARCH, "Open feature databae: %s fail", m_FeatureDataBaseName.c_str());

            free(data_ptr);

            return AX_FALSE;
        }

        AX_U32 nWriteCount = fwrite(data_ptr, 1, data_size, pFile);

        fclose(pFile);
        free(data_ptr);

        if (nWriteCount == data_size) {
            LOG_M_I(SEARCH, "Save feature to file_name: %s", m_FeatureDataBaseName.c_str());

            return AX_TRUE;
        }
        else {
            LOG_M_E(SEARCH, "Save feature to file_name: %s fail", m_FeatureDataBaseName.c_str());

            return AX_FALSE;
        }
    }

    return AX_FALSE;
}

AX_BOOL CSearch::DeleteFeature(std::string featureInfo)
{
    AX_BOOL bRet = AX_FALSE;

    if (m_FeatureDataBaseName.size() > 0
        && featureInfo.size() > 0) {
        FILE* pFile = NULL;
        AX_U32 db_file_size = 0;

        //open database
        pFile = fopen(m_FeatureDataBaseName.c_str(), "rb");

        if (!pFile) {
            LOG_M_E(SEARCH, "%s not exist", m_FeatureDataBaseName.c_str());
            return AX_FALSE;
        }

        fseek(pFile, 0, SEEK_END);
        db_file_size = ftell(pFile);
        rewind(pFile);

        AX_U8 *db_content = (AX_U8 *)malloc(db_file_size);

        if (!db_content) {
            LOG_M_E(SEARCH, "Memory alloc (%d) fail", db_file_size);

            fclose(pFile);

            return AX_FALSE;
        }

        fread(db_content, 1, db_file_size, pFile);

        fclose(pFile);

        AX_U32 total_len_read = 0;
        AX_U8 *tmp_db_ptr = (AX_U8 *)db_content;
        while(total_len_read < db_file_size) {
            //SEARCH_FEATURE_DB_ST
            SEARCH_FEATURE_HEADER_ST *feature_item_header = (SEARCH_FEATURE_HEADER_ST *)tmp_db_ptr;
            AX_U32 nItemSize = sizeof(SEARCH_FEATURE_HEADER_ST) + feature_item_header->FeatureSize;

            if (featureInfo == (AX_CHAR *)feature_item_header->Info) {
                // delete data
                if (db_file_size > total_len_read + nItemSize) {
                    memcpy(tmp_db_ptr, tmp_db_ptr + nItemSize, db_file_size - (total_len_read + nItemSize));
                }

                //update to file
                pFile = fopen(m_FeatureDataBaseName.c_str(), "wb");

                if (pFile) {
                    if (db_file_size > nItemSize) {
                        fwrite(db_content, 1, db_file_size - nItemSize, pFile);
                    }
                    fclose(pFile);

                    LOG_M(SEARCH, "db updated");

                    bRet = AX_TRUE;
                }
                break;
            }

            total_len_read += nItemSize;
            tmp_db_ptr += nItemSize;
        }

        free(db_content);
    }

    if (!bRet) {
        LOG_M_E(SEARCH, "%s not find", featureInfo.c_str());
    }

    return bRet;
}

AX_S32 CSearch::GetFeatureInfo(const AX_CHAR *str)
{
    if (!gOptions.IsActivedSearchFromWeb()) {
        //not support
        m_GetFeatureInfoStatus = 2;

        return m_GetFeatureInfoStatus;
    }

    if (!m_bFeatureDataBaseLoaded) {
        //Database loading
        m_GetFeatureInfoStatus = 3;

        return m_GetFeatureInfoStatus;
    }

    m_CaptureFeatureMutex.lock();
    GenerateFeatureName(str);
    m_CaptureFeatureMutex.unlock();

    auto iter = m_mapGroupObjectInfo[1].find(m_FeatureInfoStr);
    if (iter != m_mapGroupObjectInfo[1].end()) {
        LOG_M_E(SEARCH, "%s already exist", m_FeatureInfoStr.c_str());
        return 1;
    }

    m_CaptureFeatureMutex.lock();
    m_bGetFeatureInfo = AX_TRUE;
    m_CaptureFeatureMutex.unlock();

    m_GetFeatureInfoStatus = 1;

    Waitfor(2000);

    return m_GetFeatureInfoStatus;
}

AX_S32 CSearch::DeleteFeatureInfo(const AX_CHAR *str)
{
    if (!str) {
        return 1;
    }

    m_CaptureFeatureMutex.lock();
    GenerateFeatureName(str);
    m_CaptureFeatureMutex.unlock();

    if (!DeleteObjectFromGroup(1, m_FeatureInfoStr)) {
        return 1;
    }

    return 0;
}

AX_S32 CSearch::Waitfor(AX_S32 msTimeOut /* = -1 */)
{
    AX_S32 ret = 0;

    pthread_mutex_lock(&m_condMutx);

    // msTimeOut = 0, code exec block
    if (msTimeOut < 0) {
        pthread_cond_wait(&m_cond, &m_condMutx);
    }
    else { // wiat for time = msTimeOut
        struct timespec tv;
        clock_gettime(CLOCK_MONOTONIC, &tv);
        tv.tv_sec += msTimeOut / 1000;
        tv.tv_nsec += (msTimeOut  % 1000) * 1000000;
        if (tv.tv_nsec >= 1000000000) {
            tv.tv_nsec -= 1000000000;
            tv.tv_sec += 1;
        }

        ret = pthread_cond_timedwait(&m_cond, &m_condMutx, &tv);
        if (ret != 0) {
            LOG_M_E(SEARCH, "pthread_cond_timedwait error: %s", strerror(errno));
        }
    }

    pthread_mutex_unlock(&m_condMutx);

    return ret;
}

