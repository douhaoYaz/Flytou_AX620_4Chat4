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

#include "Detector.h"
#include "OptionHelper.h"
#include "PrintHelper.h"
#include "WebServer.h"
#include "picojson.h"
#include "ConfigParser.h"

#define DETECTION           "DETECTION"

#define DETECTION_PERF_TEST_ENABLE_ENV_STR "AI_PERF_TEST_ENABLE"
#define DETECTION_PERF_TEST_YUV_PATH_ENV_STR "AI_PERF_TEST_YUV_PATH"
#define AI_SKEL_OUTPUT_BODY_PATH "body"
#define AI_SKEL_OUTPUT_VEHICLE_PATH "vehicle"
#define AI_SKEL_OUTPUT_CYCLE_PATH "cycle"
#define AI_SKEL_OUTPUT_FACE_PATH "face"
#define AI_SKEL_OUTPUT_PLATE_PATH "plate"
#define AI_SKEL_OUTPUT_LOG_FILE "output.txt"

#define DETECTION_DEFAULT_PUSH_MODE AX_SKEL_PUSH_MODE_BEST
#define DETECTION_DEFAULT_PUSH_INTERVAL 2000
#define DETECTION_DEFAULT_PUSH_COUNTS 1
#define DETECTION_DEFAULT_PUSH_SAME_FRAME AX_TRUE

#define DETECTION_DEFAULT_ALGO_TYPE "skel_hvcp_algo"
#define DETECTION_DEFAULT_ALGO_CONFIG_PATH "./config/skel_hvcp_config.json"

#define PICO_OBJECT get<picojson::object>()
#define PICO_OBJECT_SIZE PICO_OBJECT.size()
#define PICO_ARRAY get<picojson::array>()
#define PICO_ARRAY_SIZE PICO_ARRAY.size()
#define PICO_VALUE get<double>()
#define PICO_BOOL get<bool>()
#define PICO_STRING get<std::string>()
#define PICO_ROOT obj.PICO_OBJECT

#define DETECTION_WAITING_TIMEOUT 5000

#define DETECTOR_API_RUN_START(_API_NAME_) m_apiElapsed.reset()
#define DETECTOR_API_RUN_END(_API_NAME_) LOG_M_I(DETECTION, "Run API(%s) elapsed: %d(ms)", #_API_NAME_, m_apiElapsed.ms());

// Object Define
#define ObjectDefinition(Obj) \
    int Obj##_size = 0; \
    int Obj##_size_index = 0;

// Object Calculate
#define ObjectCalculate(Obj) \
    do { \
        if (strcasecmp(pstrObjectCategory, #Obj) == 0) { \
            AX_SKEL_TRACK_STATUS_E eTrackState = stItem.eTrackState; \
            Obj##_size ++; \
            if (eTrackState == AX_SKEL_TRACK_STATUS_NEW \
                || eTrackState == AX_SKEL_TRACK_STATUS_UPDATE) { \
                    tPerfInfo.tTargets.n##Obj##s ++; \
                } \
            if (eTrackState == AX_SKEL_TRACK_STATUS_SELECT \
                && stItem.bCropFrame \
                && stItem.stCropFrame.pFrameData \
                && 0 < stItem.stCropFrame.nFrameDataSize) { \
                    tPerfInfo.tTracks.n##Obj##s ++; \
                } \
        } \
    } while(0)

// Object Create
#define ObjectCreate(Obj) \
    do { \
        if (Obj##_size > 0) { \
            pDetectionResult->n##Obj##Size = Obj##_size; \
            pDetectionResult->p##Obj##s = new AI_Detection_##Obj##Result_t[Obj##_size]; \
            if (strcasecmp("body", #Obj) == 0) { \
                pDetectionResult->nPoseSize = Obj##_size; \
                pDetectionResult->pPoses = new AI_Detection_PoseResult_t[Obj##_size]; \
            } \
        } \
    } while(0)

// Object Destory
#define ObjectDestroy(Obj) \
    do { \
        if (pDetectionResult->p##Obj##s != nullptr) { \
            delete[] pDetectionResult->p##Obj##s; \
            pDetectionResult->p##Obj##s = nullptr; \
            if ((strcasecmp("body", #Obj) == 0) && (pDetectionResult->pPoses != nullptr)) { \
                delete[] pDetectionResult->pPoses; \
                pDetectionResult->pPoses = nullptr; \
            } \
        } \
    } while(0)

// Object Assign
#define ObjectAssign(Obj) \
    do { \
        if (strcasecmp(pstrObjectCategory, #Obj) == 0) { \
            if (Obj##_size_index < Obj##_size) { \
                if (pDetectionResult->p##Obj##s) { \
                    auto* _obj = pDetectionResult->p##Obj##s + Obj##_size_index; \
                    _obj->tBox = tBox; \
                    _obj->fConfidence = fConfidence;                              \
                    _obj->u64TrackId = u64TrackId;                                \
                    _obj->eTrackState = eTrackState;                              \
                    Obj##AttrResult(stItem, _obj->t##Obj##Attr, JpegInfo); \
                } \
                if ((strcasecmp(pstrObjectCategory, "body") == 0) && pDetectionResult->pPoses) { \
                    auto *_objPose = pDetectionResult->pPoses + Obj##_size_index; \
                    _objPose->nPointNum = AX_MIN(stItem.nPointSetSize, DETECT_POSE_POINT_COUNT); \
                    if (_objPose->nPointNum > 0) { \
                        size_t nPointIndex = 0; \
                        for (size_t j = 0; j < _objPose->nPointNum; j++) { \
                            if ((strcasecmp(stItem.pstPointSet[j].pstrObjectCategory, "pose") == 0)) { \
                                _objPose->tPoint[nPointIndex].fX = stItem.pstPointSet[j].stPoint.fX / nWidth; \
                                _objPose->tPoint[nPointIndex].fY = stItem.pstPointSet[j].stPoint.fY / nHeight; \
                                nPointIndex ++; \
                            } \
                        } \
                        _objPose->nPointNum = nPointIndex; \
                    } \
                } \
                Obj##_size_index ++; \
            } \
        } \
    } while (0)

// Object Track
#define ObjectTrack(Obj) \
    do { \
        if (strcasecmp(pstrObjectCategory, #Obj) == 0) { \
            DoTracking(stItem); \
        } \
    } while (0)

extern COptionHelper gOptions;
extern CPrintHelper gPrintHelper;

using namespace std;

namespace {
void transform_algotype_string_to_ppl_type(std::string &attr_string, AX_SKEL_PPL_E &ePPL) {
    if (attr_string == "skel_body_algo") {
        ePPL = AX_SKEL_PPL_BODY;
    } else if (attr_string == "skel_pose_algo") {
        ePPL = AX_SKEL_PPL_POSE;
    } else if (attr_string == "skel_hvcp_algo") {
        ePPL = AX_SKEL_PPL_HVCP;
    } else if (attr_string == "facehuman_video_algo") {
        ePPL = AX_SKEL_PPL_FH;
    } else if (attr_string == "hvcfp_video_algo") {
        ePPL = AX_SKEL_PPL_HVCFP;
    } else if (attr_string == "facehuman_image_algo") {
        ePPL = AX_SKEL_PPL_FACE_FEATURE;
    } else if (attr_string == "hvcfp_image_algo") {
        ePPL = AX_SKEL_PPL_MAX;
    } else if (attr_string == "search_video_algo") {
        ePPL = AX_SKEL_PPL_FH;
    } else if (attr_string == "facehuman_video_detect_algo") {
        ePPL = AX_SKEL_PPL_FH;
    } else if (attr_string == "facehuman_video_lite_algo") {
        ePPL = AX_SKEL_PPL_FH;
    } else {
        ePPL = AX_SKEL_PPL_MAX;
    }

    return;
}

void transform_analyze_string_to_enum(std::string attr_string,
                                             AX_SKEL_ANALYZER_ATTR_E &analyze_attribute) {
    if (attr_string == "ANALYZE_ATTR_FACE_FEATURE") {
        analyze_attribute = AX_SKEL_ANALYZER_ATTR_FACE_FEATURE;
    } else if (attr_string == "ANALYZE_ATTR_FACE_ATTRIBUTE") {
        analyze_attribute = AX_SKEL_ANALYZER_ATTR_FACE_ATTRIBUTE;
    } else if (attr_string == "ANALYZE_ATTR_PLATE_ATTRIBUTE") {
        analyze_attribute = AX_SKEL_ANALYZER_ATTR_PLATE_ATTRIBUTE;
    } else {
        analyze_attribute = AX_SKEL_ANALYZER_ATTR_NONE;
    }

    return;
}

void transform_push_mode_to_enum(std::string mode_string, AX_U8 &push_mode)
{
    if (mode_string == "PUSH_MODE_FAST") {
        push_mode = (AX_U8)AX_SKEL_PUSH_MODE_FAST;
    } else if (mode_string == "PUSH_MODE_INTERVAL") {
        push_mode = (AX_U8)AX_SKEL_PUSH_MODE_INTERVAL;
    } else if (mode_string == "PUSH_MODE_BEST") {
        push_mode = (AX_U8)AX_SKEL_PUSH_MODE_BEST;
    }
}

AX_BOOL get_detector_init_param(DETECTOR_CONFIG_PARAM_T &stConfigParam) {
    std::ifstream ifs(stConfigParam.tStreamParam.config_path);

    if (ifs.fail()) {
        LOG_M_E(DETECTION, "%s not exist", stConfigParam.tStreamParam.config_path.c_str());
        return AX_FALSE;
    }

    picojson::value obj;
    ifs >> obj;

    string err = picojson::get_last_error();
    if (!err.empty() || !obj.is<picojson::object>()) {
        LOG_M_E(DETECTION, "Failed to load json config file: %s", stConfigParam.tStreamParam.config_path.c_str());
        return AX_FALSE;
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("detect_models_path")) {
        stConfigParam.tStreamParam.models_path = PICO_ROOT["detect_models_path"].PICO_STRING;
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("detect_algo_type")) {
        stConfigParam.tStreamParam.algo_type = PICO_ROOT["detect_algo_type"].PICO_STRING;
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("detect_track_active")) {
        AX_BOOL bTrackActive = PICO_ROOT["detect_track_active"].PICO_STRING == "true" ? AX_TRUE : AX_FALSE;

        gOptions.SetTrackActived(bTrackActive);
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("detect_search_active")) {
        AX_BOOL bSearchActive = PICO_ROOT["detect_search_active"].PICO_STRING == "true" ? AX_TRUE : AX_FALSE;

        gOptions.SetSearchActived(bSearchActive);
    }

    return AX_TRUE;
}

AX_BOOL get_detector_handle_param(DETECTOR_CONFIG_PARAM_T &stConfigParam) {
    std::ifstream ifs(stConfigParam.tStreamParam.config_path);

    if (ifs.fail()) {
        LOG_M_E(DETECTION, "%s not exist", stConfigParam.tStreamParam.config_path.c_str());
        return AX_FALSE;
    }

    picojson::value obj;
    ifs >> obj;

    string err = picojson::get_last_error();
    if (!err.empty() || !obj.is<picojson::object>()) {
        LOG_M_E(DETECTION, "Failed to load json config file: %s", stConfigParam.tStreamParam.config_path.c_str());
        return AX_FALSE;
    }

    CStageOptionHelper *pStageOption = CStageOptionHelper().GetInstance();
    AX_U32 nWidth = pStageOption->GetAiAttr().tConfig.nWidth;
    AX_U32 nHeight = pStageOption->GetAiAttr().tConfig.nHeight;

    stConfigParam.tRoi.bEnable = PICO_ROOT["roi"].PICO_OBJECT["enable"].PICO_STRING == "true" ? AX_TRUE : AX_FALSE;
    stConfigParam.tRoi.tBox.fX = (AX_F32)(PICO_ROOT["roi"].PICO_OBJECT["points"].PICO_ARRAY[0].PICO_OBJECT["x"].PICO_VALUE * nWidth);
    stConfigParam.tRoi.tBox.fY = (AX_F32)(PICO_ROOT["roi"].PICO_OBJECT["points"].PICO_ARRAY[0].PICO_OBJECT["y"].PICO_VALUE * nHeight);
    stConfigParam.tRoi.tBox.fW = (AX_F32)(PICO_ROOT["roi"].PICO_OBJECT["points"].PICO_ARRAY[1].PICO_OBJECT["x"].PICO_VALUE * nWidth -
                                          stConfigParam.tRoi.tBox.fX + 1);
    stConfigParam.tRoi.tBox.fH = (AX_F32)(PICO_ROOT["roi"].PICO_OBJECT["points"].PICO_ARRAY[1].PICO_OBJECT["y"].PICO_VALUE * nHeight -
                                          stConfigParam.tRoi.tBox.fY + 1);

    if (PICO_ROOT.end() != PICO_ROOT.find("push_strategy")) {
        transform_push_mode_to_enum(PICO_ROOT["push_strategy"].PICO_OBJECT["push_mode"].PICO_STRING,
                                    stConfigParam.tPushStrategy.nPushMode);

        stConfigParam.tPushStrategy.nInterval =
            PICO_ROOT["push_strategy"].PICO_OBJECT["interval_times"].PICO_VALUE;
        stConfigParam.tPushStrategy.nPushCounts =
            PICO_ROOT["push_strategy"].PICO_OBJECT["push_counts"].PICO_VALUE;
        stConfigParam.tPushStrategy.bPushSameFrame =
            PICO_ROOT["push_strategy"].PICO_OBJECT["push_same_frame"].PICO_STRING == "true" ? AX_TRUE : AX_FALSE;
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("crop_encoder_qplevel")) {
        stConfigParam.fCropEncoderQpLevel = PICO_ROOT["crop_encoder_qplevel"].PICO_VALUE;
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("max_track_size")) {
        if (PICO_ROOT["max_track_size"].PICO_OBJECT.end() !=
            PICO_ROOT["max_track_size"].PICO_OBJECT.find("human")) {
            stConfigParam.tTrackSize.nTrackHumanSize = PICO_ROOT["max_track_size"].PICO_OBJECT["human"].PICO_VALUE;
        }
        if (PICO_ROOT["max_track_size"].PICO_OBJECT.end() !=
            PICO_ROOT["max_track_size"].PICO_OBJECT.find("vehicle")) {
            stConfigParam.tTrackSize.nTrackVehicleSize = PICO_ROOT["max_track_size"].PICO_OBJECT["vehicle"].PICO_VALUE;
        }
        if (PICO_ROOT["max_track_size"].PICO_OBJECT.end() !=
            PICO_ROOT["max_track_size"].PICO_OBJECT.find("cycle")) {
            stConfigParam.tTrackSize.nTrackCycleSize = PICO_ROOT["max_track_size"].PICO_OBJECT["cycle"].PICO_VALUE;
        }
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("object_filter_size") && PICO_ROOT.end() != PICO_ROOT.find("object_filter")) {
        AX_U32 object_filter_size = PICO_ROOT["object_filter_size"].PICO_VALUE;

        for (size_t i = 0; i < object_filter_size; i++) {
            std::string fliter_pstrObjectCategory = PICO_ROOT["object_filter"].PICO_ARRAY[i].PICO_OBJECT["object_category"].PICO_STRING;
            stConfigParam.tObjectFliter[fliter_pstrObjectCategory].nWidth =
                PICO_ROOT["object_filter"].PICO_ARRAY[i].PICO_OBJECT["width"].PICO_VALUE;
            stConfigParam.tObjectFliter[fliter_pstrObjectCategory].nHeight =
                PICO_ROOT["object_filter"].PICO_ARRAY[i].PICO_OBJECT["height"].PICO_VALUE;
            stConfigParam.tObjectFliter[fliter_pstrObjectCategory].fConfidence =
                PICO_ROOT["object_filter"].PICO_ARRAY[i].PICO_OBJECT["confidence"].PICO_VALUE;

            // target fliter
            stConfigParam.tTargetVec.emplace_back(fliter_pstrObjectCategory);
        }
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("push_object_filter_size") && PICO_ROOT.end() != PICO_ROOT.find("push_object_filter")) {
        AX_U32 push_object_filter_size = PICO_ROOT["push_object_filter_size"].PICO_VALUE;

        for (size_t i = 0; i < push_object_filter_size; i++) {
            std::string fliter_pstrObjectCategory = PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT["object_category"].PICO_STRING;
            if (PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT.end()
                != PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT.find("width")) {
                stConfigParam.tAttrFliter[fliter_pstrObjectCategory].stFaceAttrFilterConfig.nWidth =
                    PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT["width"].PICO_VALUE;
            }
            if (PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT.end()
                    != PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT.find("height")) {
                stConfigParam.tAttrFliter[fliter_pstrObjectCategory].stFaceAttrFilterConfig.nHeight =
                    PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT["height"].PICO_VALUE;
            }
            if (PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT.end()
                    != PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT.find("pose_blur")) {
                stConfigParam.tAttrFliter[fliter_pstrObjectCategory].stFaceAttrFilterConfig.stPoseblur.fPitch =
                    PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT["pose_blur"].PICO_OBJECT["pitch"].PICO_VALUE;
                stConfigParam.tAttrFliter[fliter_pstrObjectCategory].stFaceAttrFilterConfig.stPoseblur.fYaw =
                    PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT["pose_blur"].PICO_OBJECT["yaw"].PICO_VALUE;
                stConfigParam.tAttrFliter[fliter_pstrObjectCategory].stFaceAttrFilterConfig.stPoseblur.fRoll =
                    PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT["pose_blur"].PICO_OBJECT["roll"].PICO_VALUE;
                stConfigParam.tAttrFliter[fliter_pstrObjectCategory].stFaceAttrFilterConfig.stPoseblur.fBlur =
                    PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT["pose_blur"].PICO_OBJECT["blur"].PICO_VALUE;
            }
            if (PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT.end()
                    != PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT.find("quality")) {
                stConfigParam.tAttrFliter[fliter_pstrObjectCategory].stCommonAttrFilterConfig.fQuality =
                    PICO_ROOT["push_object_filter"].PICO_ARRAY[i].PICO_OBJECT["quality"].PICO_VALUE;
            }

            // push target fliter
            stConfigParam.tPushTargetVec.emplace_back(fliter_pstrObjectCategory);
        }
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("push_object_crop_scale_size") && PICO_ROOT.end() != PICO_ROOT.find("push_object_crop_scale")) {
        AX_U32 push_object_crop_scale_size = PICO_ROOT["push_object_crop_scale_size"].PICO_VALUE;

        for (size_t i = 0; i < push_object_crop_scale_size; i++) {
            std::string fliter_pstrObjectCategory = PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT["object_category"].PICO_STRING;
            if (PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT.end()
                    != PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT.find("scaleleft")) {
                stConfigParam.tCropThreshold[fliter_pstrObjectCategory].fScaleLeft =
                    PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT["scaleleft"].PICO_VALUE;
            }

            if (PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT.end()
                    != PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT.find("scaleright")) {
                stConfigParam.tCropThreshold[fliter_pstrObjectCategory].fScaleRight =
                    PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT["scaleright"].PICO_VALUE;
            }

            if (PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT.end()
                    != PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT.find("scaletop")) {
                stConfigParam.tCropThreshold[fliter_pstrObjectCategory].fScaleTop =
                    PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT["scaletop"].PICO_VALUE;
            }

            if (PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT.end()
                    != PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT.find("scalebottom")) {
                stConfigParam.tCropThreshold[fliter_pstrObjectCategory].fScaleBottom =
                    PICO_ROOT["push_object_crop_scale"].PICO_ARRAY[i].PICO_OBJECT["scalebottom"].PICO_VALUE;
            }
        }
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("analyze_attribute_size") && PICO_ROOT.end() != PICO_ROOT.find("analyze_attribute")) {
        AX_U32 analyze_attribute_size = PICO_ROOT["analyze_attribute_size"].PICO_VALUE;

        for (size_t i = 0; i < analyze_attribute_size; i++) {
            std::string pstrAnalyze = PICO_ROOT["analyze_attribute"].PICO_ARRAY[i].PICO_STRING;
            stConfigParam.tAnalyzerVec.emplace_back(pstrAnalyze);
        }
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("panorama_enabled")) {
        stConfigParam.bPanoramaEnable = PICO_ROOT["panorama_enabled"].PICO_STRING == "true" ? AX_TRUE : AX_FALSE;
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("track_enabled")) {
        stConfigParam.bTrackEnable = PICO_ROOT["track_enabled"].PICO_STRING == "true" ? AX_TRUE : AX_FALSE;
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("push_enabled")) {
        stConfigParam.bPushEnable = PICO_ROOT["push_enabled"].PICO_STRING == "true" ? AX_TRUE : AX_FALSE;
    }

    if (PICO_ROOT.end() != PICO_ROOT.find("push_attr_always")) {
        stConfigParam.bPushAttrAlways = PICO_ROOT["push_attr_always"].PICO_STRING == "true" ? AX_TRUE : AX_FALSE;
    }

    return AX_TRUE;
}

void BodyAttrResult(const AX_SKEL_OBJECT_ITEM_S &stObjectItem, AI_Body_Attr_t &human_attr, JpegDataInfo &JpegInfo)
{
    picojson::value obj;

    JpegInfo.eType = JPEG_TYPE_BODY;

    human_attr.bExist = AX_FALSE;
    human_attr.strSafetyCap = "";
    human_attr.strHairLength = "";

    for (size_t i = 0; i < stObjectItem.nMetaInfoSize; i++) {
        if (!strcmp(stObjectItem.pstMetaInfo[i].pstrType, "human_safety_cap")) {
            std::string value = stObjectItem.pstMetaInfo[i].pstrValue;
            std::string strParseRet = picojson::parse(obj, value);
            if (!strParseRet.empty() || !obj.is<picojson::object>()) {
                break;
            }

            human_attr.bExist = AX_TRUE;
            human_attr.strSafetyCap = PICO_ROOT["safety_cap"].PICO_OBJECT["name"].PICO_STRING;
        } else if (!strcmp(stObjectItem.pstMetaInfo[i].pstrType, "human_attr")) {
            std::string value = stObjectItem.pstMetaInfo[i].pstrValue;
            std::string strParseRet = picojson::parse(obj, value);
            if (!strParseRet.empty() || !obj.is<picojson::object>()) {
                break;
            }

            human_attr.bExist = AX_TRUE;
            human_attr.strHairLength = PICO_ROOT["hair_length"].PICO_OBJECT["name"].PICO_STRING;
        }
    }
}

void VehicleAttrResult(const AX_SKEL_OBJECT_ITEM_S &stObjectItem, AI_Vehicle_Attr_t &vehicle_attr, JpegDataInfo &JpegInfo) {
    DETECTOR_CONFIG_PARAM_T tConf = CDetector::GetInstance()->GetConfig();

    picojson::value obj;

    JpegInfo.eType = JPEG_TYPE_VEHICLE;

    vehicle_attr.bExist = AX_FALSE;
    vehicle_attr.strVehicleColor = "";
    vehicle_attr.strVehicleSubclass = "";
    vehicle_attr.plate_attr.bExist = AX_FALSE;
    vehicle_attr.plate_attr.bValid = AX_FALSE;
    vehicle_attr.plate_attr.strPlateColor = "";
    vehicle_attr.plate_attr.strPlateType = "";
    vehicle_attr.plate_attr.strPlateCode = "";

    for (size_t i = 0; i < stObjectItem.nMetaInfoSize; i++) {
        if (!strcmp(stObjectItem.pstMetaInfo[i].pstrType, "vehicle_cls")) {
            std::string value = stObjectItem.pstMetaInfo[i].pstrValue;
            std::string strParseRet = picojson::parse(obj, value);
            if (!strParseRet.empty() || !obj.is<picojson::object>()) {
                break;
            }

            vehicle_attr.bExist = AX_TRUE;
            vehicle_attr.strVehicleSubclass = PICO_ROOT["classifications"].PICO_OBJECT["name"].PICO_STRING;
        } else if (!strcmp(stObjectItem.pstMetaInfo[i].pstrType, "vehicle_attr")) {
            std::string value = stObjectItem.pstMetaInfo[i].pstrValue;
            std::string strParseRet = picojson::parse(obj, value);
            if (!strParseRet.empty() || !obj.is<picojson::object>()) {
                break;
            }

            vehicle_attr.bExist = AX_TRUE;
            vehicle_attr.strVehicleColor = PICO_ROOT["color"].PICO_OBJECT["name"].PICO_STRING;
        } else if (!strcmp(stObjectItem.pstMetaInfo[i].pstrType, "plate_attr")) {
            std::string value = stObjectItem.pstMetaInfo[i].pstrValue;
            std::string strParseRet = picojson::parse(obj, value);
            if (!strParseRet.empty() || !obj.is<picojson::object>()) {
                break;
            }

            vehicle_attr.plate_attr.bExist = AX_TRUE;
            // color
            vehicle_attr.plate_attr.strPlateColor = "unknown";
            if (PICO_ROOT.end() != PICO_ROOT.find("color")) {
                vehicle_attr.plate_attr.strPlateColor = PICO_ROOT["color"].PICO_OBJECT["name"].PICO_STRING;
            }

            // style
            vehicle_attr.plate_attr.strPlateType = "unknown";
            if (PICO_ROOT.end() != PICO_ROOT.find("style")) {
                vehicle_attr.plate_attr.strPlateType = PICO_ROOT["style"].PICO_OBJECT["name"].PICO_STRING;
            }

            // code
            vehicle_attr.plate_attr.strPlateCode = PICO_ROOT["code_result"].PICO_STRING;

            if (PICO_ROOT["code_killed"].PICO_BOOL) {
                vehicle_attr.plate_attr.bValid = AX_FALSE;
            } else {
                vehicle_attr.plate_attr.bValid = AX_TRUE;
            }

            if (tConf.bPlateIdentify) {
                strncpy(JpegInfo.tPlateInfo.szColor, (const AX_CHAR *)vehicle_attr.plate_attr.strPlateColor.c_str(), sizeof(JpegInfo.tPlateInfo.szColor) - 1);

                JpegInfo.tPlateInfo.bExist = AX_TRUE;
                JpegInfo.tPlateInfo.bValid = vehicle_attr.plate_attr.bValid;
                strncpy(JpegInfo.tPlateInfo.szNumVal, (const AX_CHAR *)vehicle_attr.plate_attr.strPlateCode.c_str(), sizeof(JpegInfo.tPlateInfo.szNum) - 1);

                if (vehicle_attr.plate_attr.bValid) {
                    strncpy(JpegInfo.tPlateInfo.szNum, (const AX_CHAR *)vehicle_attr.plate_attr.strPlateCode.c_str(), sizeof(JpegInfo.tPlateInfo.szNum) - 1);
                }
                else {
                    strncpy(JpegInfo.tPlateInfo.szNum, "unknown", sizeof(JpegInfo.tPlateInfo.szNum) - 1);
                }
            }

            LOG_M(DETECTION, "Vehicle: [Plate] color:%s, style:%s, code:%s, valid:%d",
                    vehicle_attr.plate_attr.strPlateColor.c_str(), vehicle_attr.plate_attr.strPlateType.c_str(),
                    vehicle_attr.plate_attr.strPlateCode.c_str(), vehicle_attr.plate_attr.bValid);
        }
    }
}

void CycleAttrResult(const AX_SKEL_OBJECT_ITEM_S &stObjectItem, AI_Cycle_Attr_t &cycle_attr, JpegDataInfo &JpegInfo) {
    picojson::value obj;

    JpegInfo.eType = JPEG_TYPE_CYCLE;

    cycle_attr.bExist = AX_FALSE;
    cycle_attr.strCycleSubclass = "";

    for (size_t i = 0; i < stObjectItem.nMetaInfoSize; i++) {
        if (!strcmp(stObjectItem.pstMetaInfo[i].pstrType, "cycle_attr")) {
            std::string value = stObjectItem.pstMetaInfo[i].pstrValue;
            std::string strParseRet = picojson::parse(obj, value);
            if (!strParseRet.empty() || !obj.is<picojson::object>()) {
                break;
            }

            cycle_attr.bExist = AX_TRUE;
            cycle_attr.strCycleSubclass = PICO_ROOT["classifications"].PICO_OBJECT["name"].PICO_STRING;
        }
    }
}

void FaceAttrResult(const AX_SKEL_OBJECT_ITEM_S &stObjectItem, AI_Face_Attr_t &face_attr, JpegDataInfo &JpegInfo)
{
    picojson::value obj;

    JpegInfo.eType = JPEG_TYPE_FACE;
    JpegInfo.tFaceInfo.bExist = AX_FALSE;

    face_attr.bExist = AX_FALSE;
    face_attr.nAge = 0;
    face_attr.nGender = 0;
    face_attr.strRespirator = "";

    for (size_t i = 0; i < stObjectItem.nMetaInfoSize; i++) {
        if (!strcmp(stObjectItem.pstMetaInfo[i].pstrType, "face_attr")) {
            std::string value = stObjectItem.pstMetaInfo[i].pstrValue;
            std::string strParseRet = picojson::parse(obj, value);
            if (!strParseRet.empty() || !obj.is<picojson::object>()) {
                break;
            }

            face_attr.bExist = AX_TRUE;
            // age
            face_attr.nAge = PICO_ROOT["age"].PICO_VALUE;

            // gender
            if (PICO_ROOT["gender"].PICO_OBJECT["name"].PICO_STRING == "male") {
                face_attr.nGender = 1;
            }
            else {
                face_attr.nGender = 0;
            }

            // respirator
            face_attr.strRespirator = PICO_ROOT["respirator"].PICO_OBJECT["name"].PICO_STRING;

            JpegInfo.tFaceInfo.bExist = AX_TRUE;
            JpegInfo.tFaceInfo.nAge = face_attr.nAge;
            JpegInfo.tFaceInfo.nGender = face_attr.nGender;
            strncpy(JpegInfo.tFaceInfo.szMask, (const AX_CHAR *)face_attr.strRespirator.c_str(), sizeof(JpegInfo.tFaceInfo.szMask) - 1);

            LOG_M(DETECTION, "Face: age:%d, gender:%s, respirator:%s",
                    face_attr.nAge, face_attr.nGender ? "male":"female",
                    face_attr.strRespirator.c_str());
        }
    }
}

void PlateAttrResult(const AX_SKEL_OBJECT_ITEM_S &stObjectItem, AI_Plat_Attr_t &plat_attr, JpegDataInfo &JpegInfo) {
    DETECTOR_CONFIG_PARAM_T tConf = CDetector::GetInstance()->GetConfig();

    picojson::value obj;

    JpegInfo.eType = JPEG_TYPE_PLATE;
    JpegInfo.tPlateInfo.bExist = AX_FALSE;

    plat_attr.bExist = AX_FALSE;
    plat_attr.bValid = AX_FALSE;
    plat_attr.strPlateColor = "";
    plat_attr.strPlateType = "";
    plat_attr.strPlateCode = "";

    for (size_t i = 0; i < stObjectItem.nMetaInfoSize; i++) {
        if (!strcmp(stObjectItem.pstMetaInfo[i].pstrType, "plate_attr")) {
            std::string value = stObjectItem.pstMetaInfo[i].pstrValue;
            std::string strParseRet = picojson::parse(obj, value);
            if (!strParseRet.empty() || !obj.is<picojson::object>()) {
                break;
            }

            plat_attr.bExist = AX_TRUE;
            // color
            plat_attr.strPlateColor = "unknown";
            if (PICO_ROOT.end() != PICO_ROOT.find("color")) {
                plat_attr.strPlateColor = PICO_ROOT["color"].PICO_OBJECT["name"].PICO_STRING;
            }

            // style
            plat_attr.strPlateType = "unknown";
            if (PICO_ROOT.end() != PICO_ROOT.find("style")) {
                plat_attr.strPlateType = PICO_ROOT["style"].PICO_OBJECT["name"].PICO_STRING;
            }

            // code
            plat_attr.strPlateCode = PICO_ROOT["code_result"].PICO_STRING;

            if (PICO_ROOT["code_killed"].PICO_BOOL) {
                plat_attr.bValid = AX_FALSE;
            } else {
                plat_attr.bValid = AX_TRUE;
            }

            if (tConf.bPlateIdentify) {
                strncpy(JpegInfo.tPlateInfo.szColor, (const AX_CHAR *)plat_attr.strPlateColor.c_str(), sizeof(JpegInfo.tPlateInfo.szColor) - 1);

                JpegInfo.tPlateInfo.bExist = AX_TRUE;
                JpegInfo.tPlateInfo.bValid = plat_attr.bValid;
                strncpy(JpegInfo.tPlateInfo.szNumVal, (const AX_CHAR *)plat_attr.strPlateCode.c_str(), sizeof(JpegInfo.tPlateInfo.szNum) - 1);

                if (plat_attr.bValid) {
                    strncpy(JpegInfo.tPlateInfo.szNum, (const AX_CHAR *)plat_attr.strPlateCode.c_str(), sizeof(JpegInfo.tPlateInfo.szNum) - 1);
                }
                else {
                    strncpy(JpegInfo.tPlateInfo.szNum, "unknown", sizeof(JpegInfo.tPlateInfo.szNum) - 1);
                }
            }

            LOG_M(DETECTION, "Plate: color:%s, style:%s, code:%s, valid:%d",
                    plat_attr.strPlateColor.c_str(), plat_attr.strPlateType.c_str(),
                    plat_attr.strPlateCode.c_str(), plat_attr.bValid);
        }
    }
}

AX_VOID AsyncRecvAlgorithmResultThread(AX_VOID *__this)
{
    CDetector *pThis = (CDetector *)__this;

    prctl(PR_SET_NAME, "RecvAlgoThread");

    while (pThis->m_bGetResultThreadRunning) {
        if (!pThis->AsyncRecvDetectionResult()) {
            CTimeUtils::msSleep(1);
        }
    }
}
} //namespace

//////////////////////////////////////////////////////////////////
CDetector::CDetector(AX_VOID)
{
    m_bFinished = AX_TRUE;
    m_bForcedExit = AX_FALSE;
    m_nFrame_id = 1;
    m_bGetResultThreadRunning = AX_FALSE;
    m_pGetResultThread = nullptr;

    //Search
    m_pObjectSearch = nullptr;

    //AI Perf Test
    AI_CONFIG_T tConfig;
    memset(&tConfig, 0x00, sizeof(tConfig));
    CConfigParser().GetInstance()->GetAiCfg(0, tConfig);

    memset(&m_perfTestInfo, 0x00, sizeof(PERF_TEST_INFO_T));

    m_perfTestInfo.nWidth = tConfig.nWidth;
    m_perfTestInfo.nHeight = tConfig.nHeight;
    m_perfTestInfo.nSize = (m_perfTestInfo.nWidth * m_perfTestInfo.nHeight * 3 / 2);
}

CDetector::~CDetector(AX_VOID)
{

}

AX_VOID CDetector::BindCropStage(CTrackCropStage* pStage)
{
    m_pTrackCropStage = pStage;
}

AX_BOOL CDetector::InitConfigParam(AX_VOID)
{
    //Running config
    m_tConfigParam.tPushStrategy.nPushMode = (AX_U8)DETECTION_DEFAULT_PUSH_MODE;
    m_tConfigParam.tPushStrategy.nInterval = DETECTION_DEFAULT_PUSH_INTERVAL;
    m_tConfigParam.tPushStrategy.nPushCounts = DETECTION_DEFAULT_PUSH_COUNTS;
    m_tConfigParam.tPushStrategy.bPushSameFrame = DETECTION_DEFAULT_PUSH_SAME_FRAME;

    m_tConfigParam.tStreamParam.algo_type = DETECTION_DEFAULT_ALGO_TYPE;
    m_tConfigParam.tStreamParam.config_path = DETECTION_DEFAULT_ALGO_CONFIG_PATH;

    //Track
    m_tConfigParam.nTrackType = 0;
    if (AI_TRACK_BODY_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nTrackType, AI_TRACK_TYPE_BODY);
    }
    if (AI_TRACK_VEHICLE_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nTrackType, AI_TRACK_TYPE_VEHICLE);
    }
    if (AI_TRACK_CYCLE_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nTrackType, AI_TRACK_TYPE_CYCLE);
    }
    if (AI_TRACK_FACE_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nTrackType, AI_TRACK_TYPE_FACE);
    }
    if (AI_TRACK_PLATE_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nTrackType, AI_TRACK_TYPE_PLATE);
    }

    //Draw rect
    m_tConfigParam.nDrawRectType = 0;
    if (AI_DRAW_RECT_BODY_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_BODY);
    }
    if (AI_DRAW_RECT_VEHICLE_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_VEHICLE);
    }
    if (AI_DRAW_RECT_CYCLE_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_CYCLE);
    }
    if (AI_DRAW_RECT_FACE_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_FACE);
    }
    if (AI_DRAW_RECT_PLATE_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_PLATE);
    }
    if (AI_DRAW_RECT_POSE_ENABLE) {
        AX_BIT_SET(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_POSE);
    }

    // Init Param
    m_tConfigParam.tStreamParam.config_path = gOptions.GetDetectionConfigPath();

    if (!get_detector_init_param(m_tConfigParam)) {
        return AX_FALSE;
    }

    std::string strAlgo = m_tConfigParam.tStreamParam.algo_type;
    if (strstr(strAlgo.c_str(), "skel")) {
        AX_BIT_SET(m_tConfigParam.nTrackType, AI_TRACK_TYPE_BODY);
        AX_BIT_SET(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_BODY);
        AX_BIT_SET(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_POSE);
    }

    m_tConfigParam.bPlateIdentify = AX_TRUE;

    return AX_TRUE;
}

AX_BOOL CDetector::Startup(AX_VOID)
{
    LOG_M(DETECTION, "+++");

    if (!gOptions.IsActivedDetect()) {
        return AX_TRUE;
    }

    if (!InitConfigParam()) {
        LOG_M(DETECTION, "Init config param fail");
        return AX_FALSE;
    }

    AX_SKEL_INIT_PARAM_S stInitParam = {0};

    std::string strModPath = m_tConfigParam.tStreamParam.models_path;

    LOG_M(DETECTION, "Load pro path: %s", strModPath.c_str());

    stInitParam.pStrModelDeploymentPath = strModPath.c_str();

    DETECTOR_API_RUN_START(AX_SKEL_Init);
    AX_S32 nRet = AX_SKEL_Init(&stInitParam);
    DETECTOR_API_RUN_END(AX_SKEL_Init);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "SKEL init fail, ret = 0x%x", nRet);

        return AX_FALSE;
    }

    // get version
    {
        const AX_SKEL_VERSION_INFO_S *pstVersion = NULL;
        nRet = AX_SKEL_GetVersion(&pstVersion);

        if (0 != nRet) {
            LOG_M_E(DETECTION, "SKEL get version fail, ret = 0x%x", nRet);

            if (pstVersion) {
                AX_SKEL_Release((AX_VOID *)pstVersion);
            }

            return AX_FALSE;
        }

        if (pstVersion) {
            LOG_M(DETECTION, "%s", pstVersion->pstrVersion);
            AX_SKEL_Release((AX_VOID *)pstVersion);
        }
    }

    LOG_M(DETECTION, "algo config: %s", m_tConfigParam.tStreamParam.algo_type.c_str());

    transform_algotype_string_to_ppl_type(m_tConfigParam.tStreamParam.algo_type, m_tConfigParam.ePPL);

    if (!CreateHandle()) {
        LOG_M_E(DETECTION, "CreateHandle error");
        return AX_FALSE;
    }

    //Search
    if (gOptions.IsActivedSearch()) {
        //Search
        m_pObjectSearch = CSearch::GetInstance();

        if (!m_pObjectSearch->Startup()) {
            gOptions.SetSearchActived(AX_FALSE);
            LOG_M_W(DETECTION, "Search init failed");
        }
    }

    //update AI Attribute
    UpdateAiAttr();

    m_bFinished = AX_TRUE;
    m_bForcedExit = AX_FALSE;

    //Load perf test info
    LoadPerfTestInfo();

    LOG_M(DETECTION, "---");

    return AX_TRUE;
}

AX_VOID CDetector::Cleanup(AX_VOID)
{
    LOG_M(DETECTION, "+++");

    m_bForcedExit = AX_TRUE;

    WaitForFinish();

    m_bGetResultThreadRunning = AX_FALSE;

    if (m_stream_handle) {
        DETECTOR_API_RUN_START(AX_SKEL_Destroy);
        AX_SKEL_Destroy(m_stream_handle);
        DETECTOR_API_RUN_END(AX_SKEL_Destroy);
        m_stream_handle = nullptr;
    }

    if (m_pObjectSearch) {
        m_pObjectSearch->Cleanup();
    }

    DETECTOR_API_RUN_START(AX_SKEL_DeInit);
    AX_SKEL_DeInit();
    DETECTOR_API_RUN_END(AX_SKEL_DeInit);

    if (m_pGetResultThread && m_pGetResultThread->joinable()) {
        m_pGetResultThread->join();

        delete m_pGetResultThread;
        m_pGetResultThread = nullptr;
    }

    ClearAlgorithmData();

    //AI Perf Test
    if (m_perfTestInfo.nPhyAddr != 0) {
        AX_SYS_MemFree(m_perfTestInfo.nPhyAddr, &m_perfTestInfo.pVirAddr);
    }

    LOG_M(DETECTION, "---");
}

AX_BOOL CDetector::LoadPerfTestInfo(AX_VOID)
{
    //get perf test env
    const char *perfTestEnableStr = getenv(DETECTION_PERF_TEST_ENABLE_ENV_STR);
    if (perfTestEnableStr) {
        AX_BOOL bAiPerfTest = (AX_BOOL)atoi(perfTestEnableStr);

        if (bAiPerfTest) {
            const char *perfTestYUVPathStr = getenv(DETECTION_PERF_TEST_YUV_PATH_ENV_STR);

            if (perfTestYUVPathStr) {
                FILE *fp = fopen(perfTestYUVPathStr, "rb");
                if (fp) {
                    fseek(fp, 0, SEEK_END);
                    AX_U32 nFileSize = ftell(fp);
                    fseek(fp, 0, SEEK_SET);

                    if (nFileSize == m_perfTestInfo.nSize) {
                        AX_S32 ret = AX_SYS_MemAlloc(&m_perfTestInfo.nPhyAddr, &m_perfTestInfo.pVirAddr, nFileSize, 256, NULL);

                        if (AX_SDK_PASS == ret) {
                            AX_U64 *pData = (AX_U64 *)m_perfTestInfo.pVirAddr;
                            fread(pData, nFileSize, 1, fp);
                            m_perfTestInfo.m_bAiPerfTest = AX_TRUE;

                            LOG_M_E(DETECTION, "Load YUV file(%s) success.", perfTestYUVPathStr);
                        }
                        else {
                            LOG_M_E(DETECTION, "AX_SYS_MemAlloc() failed, size:0x%x, error: 0x%x", nFileSize, ret);
                        }
                    }
                    else {
                        LOG_M_E(DETECTION, "YUV file(%s) should be %d*%d.", perfTestYUVPathStr, m_perfTestInfo.nWidth, m_perfTestInfo.nHeight);
                    }

                    fclose(fp);
                }
                else {
                    LOG_M_E(DETECTION, "open YUV file(%s) fail.", perfTestYUVPathStr);
                }
            }
        }
    }

    return AX_TRUE;
}

AX_BOOL CDetector::ProcessFrame(CMediaFrame* pFrame, AX_S32 nGaps /* = -1 */)
{
    if (m_bForcedExit) {
        return AX_FALSE;
    }

    if (!gOptions.IsActivedDetect() || !gOptions.IsActivedDetectFromWeb()) {
         return AX_FALSE;
    }

    if (!m_stream_handle) {
        LOG_M_I(DETECTION, "Stream handle not init");
        return AX_FALSE;
    }
    LOG_M_I(DETECTION, "+++");

    if (!m_bForcedExit) {
        AX_U64 nFrame_id = m_nFrame_id ++;

        AX_VIDEO_FRAME_INFO_S tFrame = {0};
        if (pFrame->bIvpsFrame) {
            tFrame.stVFrame = pFrame->tVideoFrame;
        } else {
            tFrame = pFrame->tFrame.tFrameInfo;
        }

        if (nFrame_id == 0) {
            LOG_M_E(DETECTION, "Skip FrameId(0)");
            return AX_FALSE;
        }

        //Get feature info
        if (gOptions.IsActivedSearch() && m_pObjectSearch) {
            if (m_pObjectSearch->ProcessFeature(pFrame)) {
                return AX_TRUE;
            }
        }

        m_bFinished = AX_FALSE;

        AX_SKEL_FRAME_S stFrame = {0};
        stFrame.nFrameId = (AX_U64)nFrame_id;
        stFrame.pUserData = this;

        if (!m_perfTestInfo.m_bAiPerfTest) {
            stFrame.stFrame = tFrame.stVFrame;
        } else {
            stFrame.stFrame.u32Width = tFrame.stVFrame.u32Width;
            stFrame.stFrame.u32Height = tFrame.stVFrame.u32Height;
            stFrame.stFrame.enImgFormat = AX_YUV420_SEMIPLANAR;
            stFrame.stFrame.u32FrameSize = tFrame.stVFrame.u32FrameSize;
            stFrame.stFrame.u32PicStride[0] = stFrame.stFrame.u32Width;
            stFrame.stFrame.u32PicStride[1] = stFrame.stFrame.u32Width;
            stFrame.stFrame.u32PicStride[2] = stFrame.stFrame.u32Width;
            stFrame.stFrame.u64PhyAddr[0] = m_perfTestInfo.nPhyAddr;
            stFrame.stFrame.u64PhyAddr[1] = m_perfTestInfo.nPhyAddr;
            stFrame.stFrame.u64PhyAddr[2] = m_perfTestInfo.nPhyAddr;
            stFrame.stFrame.u64VirAddr[0] = (AX_U64)((AX_U32)m_perfTestInfo.pVirAddr);
            stFrame.stFrame.u64VirAddr[1] = (AX_U64)((AX_U32)m_perfTestInfo.pVirAddr);
            stFrame.stFrame.u64VirAddr[2] = (AX_U64)((AX_U32)m_perfTestInfo.pVirAddr);
        }

        {
            m_mutex.lock();
            // here,save track result in m_mapDataResult
            FRAME_ALGORITHM_RESULT_ST stAlgorithmResult;
            stAlgorithmResult.m_sVideoFrame = tFrame.stVFrame;
            stAlgorithmResult.m_tpStart = std::chrono::steady_clock::now();
            m_mapDataResult[nFrame_id] = stAlgorithmResult;
            m_mutex.unlock();
        }

        AX_S32 nRet = AX_SKEL_SendFrame(m_stream_handle, &stFrame, nGaps);

        m_bFinished = AX_TRUE;

        // send fail
        if (nRet != AX_SKEL_SUCC) {
            if (AX_ERR_SKEL_QUEUE_FULL != nRet
                && AX_ERR_SKEL_TIMEOUT != nRet) {
                LOG_M_E(DETECTION, "AX_SKEL_SendFrame(FrameId:%lld): error code(0x%X)", nFrame_id, nRet);
            }

            m_mutex.lock();
            auto iter = m_mapDataResult.find(nFrame_id);
            if (iter != m_mapDataResult.end()) {
                m_mapDataResult.erase(iter);
            }
            m_mutex.unlock();

            return AX_FALSE;
        }
        else {
            m_mutex.lock();
            auto iter = m_mapDataResult.find(nFrame_id);
            if (iter != m_mapDataResult.end()) {
                // update time point
                iter->second.m_tpStart = std::chrono::steady_clock::now();
            }
            m_mutex.unlock();
        }
    }

    LOG_M_I(DETECTION, "---");

    return AX_TRUE;
}

AX_BOOL CDetector::CreateHandle(AX_VOID) {
    AX_S32 nRet = AX_SKEL_SUCC;
    AX_BOOL bRet = AX_FALSE;

    // get capability
    {
        const AX_SKEL_CAPABILITY_S *pstCapability = NULL;

        nRet = AX_SKEL_GetCapability(&pstCapability);

        if (0 != nRet) {
            LOG_M_E(DETECTION, "SKEL get capability fail, ret = 0x%x", nRet);

            if (pstCapability) {
                AX_SKEL_Release((AX_VOID *)pstCapability);
            }
            return AX_FALSE;
        }

        LOG_M(DETECTION, "Use algo: %s", m_tConfigParam.tStreamParam.algo_type.c_str());

        AX_BOOL find_algo_type = AX_FALSE;
        for (size_t i = 0; i < pstCapability->nPPLConfigSize; i++) {
            LOG_M_I(DETECTION, "SKEL capability[%d]: (ePPL: %d, PPLConfigKey: %s)", i, pstCapability->pstPPLConfig[i].ePPL,
                    pstCapability->pstPPLConfig[i].pstrPPLConfigKey);
            if (pstCapability->pstPPLConfig[i].ePPL == m_tConfigParam.ePPL) {
                find_algo_type = AX_TRUE;
                break;
            }
        }

        if (pstCapability) {
            AX_SKEL_Release((AX_VOID *)pstCapability);
        }

        if (!find_algo_type) {
            LOG_M_E(DETECTION, "not find algo_type for %s", m_tConfigParam.tStreamParam.algo_type.c_str());
            return AX_FALSE;
        }
    }

    if (m_tConfigParam.tStreamParam.config_path.size() > 0) {
        bRet = get_detector_handle_param(m_tConfigParam);
    } else {
        LOG_M_E(DETECTION, "config_path invalid");
        return AX_FALSE;
    }

    if (!bRet) {
        LOG_M_E(DETECTION, "get handle param error");
        return AX_FALSE;
    }

    AX_SKEL_HANDLE_PARAM_S stHandleParam;
    CStageOptionHelper *pStageOption = CStageOptionHelper().GetInstance();

    m_tConfigParam.nFrameDepth = pStageOption->GetAiAttr().tConfig.nFrameDepth ? pStageOption->GetAiAttr().tConfig.nFrameDepth : AX_AI_DETECT_FRAME_DEPTH;
    m_tConfigParam.nCacheDepth = pStageOption->GetAiAttr().tConfig.nCacheDepth ? pStageOption->GetAiAttr().tConfig.nCacheDepth : AX_AI_DETECT_CACHE_LIST_DEPTH;

    // at least one frame depth
    m_tConfigParam.nFrameDepth = m_tConfigParam.nFrameDepth ? m_tConfigParam.nFrameDepth : 1;

    // at least one frame depth
    m_tConfigParam.nCacheDepth = m_tConfigParam.nCacheDepth ? m_tConfigParam.nCacheDepth : 1;

    LOG_M(DETECTION, "AI frame depth: %d, cache depth: %d", m_tConfigParam.nFrameDepth, m_tConfigParam.nCacheDepth);

    memset(&stHandleParam, 0x00, sizeof(AX_SKEL_HANDLE_PARAM_S));
    stHandleParam.ePPL = m_tConfigParam.ePPL;
    stHandleParam.nFrameDepth = m_tConfigParam.nFrameDepth;
    stHandleParam.nFrameCacheDepth = m_tConfigParam.nCacheDepth;
    stHandleParam.nWidth = pStageOption->GetAiAttr().tConfig.nWidth;
    stHandleParam.nHeight = pStageOption->GetAiAttr().tConfig.nHeight;

    AX_SKEL_CONFIG_S stConfig = {0};
    std::vector<AX_SKEL_CONFIG_ITEM_S> vContent;
    AX_SKEL_CONFIG_ITEM_S tConfigItem;

    vContent.clear();

    // target config
    AX_SKEL_TARGET_CONFIG_S stTarget = {0};
    std::vector<AX_SKEL_TARGET_ITEM_S> stTargetItemVec;
    if (m_tConfigParam.tTargetVec.size() > 0) {
        for (AX_U32 i = 0; i < m_tConfigParam.tTargetVec.size(); i ++) {
            AX_SKEL_TARGET_ITEM_S stItem = {0};
            stItem.pstrObjectCategory = m_tConfigParam.tTargetVec[i].c_str();
            stTargetItemVec.emplace_back(stItem);
        }

        stTarget.nSize = stTargetItemVec.size();
        stTarget.pstItems = stTargetItemVec.data();

        memset(&tConfigItem, 0x00, sizeof(tConfigItem));
        tConfigItem.pstrType = (AX_CHAR *)"target_config";
        tConfigItem.pstrValue = (AX_VOID *)&stTarget;
        tConfigItem.nValueSize = sizeof(AX_SKEL_TARGET_CONFIG_S);

        vContent.emplace_back(tConfigItem);
    }

    // analyzer config
    AX_SKEL_ANALYZER_CONFIG_S stAnalyze = {0};
    std::vector<AX_SKEL_ANALYZER_ATTR_E> eAnalyzerVec;
    if (m_tConfigParam.tAnalyzerVec.size() > 0) {
        for (AX_U32 i = 0; i < m_tConfigParam.tAnalyzerVec.size(); i ++) {
            AX_SKEL_ANALYZER_ATTR_E analyze_attribute = AX_SKEL_ANALYZER_ATTR_NONE;
            transform_analyze_string_to_enum(m_tConfigParam.tAnalyzerVec[i], analyze_attribute);
            eAnalyzerVec.emplace_back(analyze_attribute);
        }

        stAnalyze.nSize = eAnalyzerVec.size();
        stAnalyze.peItems = eAnalyzerVec.data();

        memset(&tConfigItem, 0x00, sizeof(tConfigItem));
        tConfigItem.pstrType = (AX_CHAR *)"analyzer_attr_config";
        tConfigItem.pstrValue = (AX_VOID *)&stAnalyze;
        tConfigItem.nValueSize = sizeof(AX_SKEL_ANALYZER_CONFIG_S);

        vContent.emplace_back(tConfigItem);
    }

    // push target config
    AX_SKEL_TARGET_CONFIG_S stPushTarget = {0};
    std::vector<AX_SKEL_TARGET_ITEM_S> stPushTargetItemVec;
    if (m_tConfigParam.tPushTargetVec.size() > 0) {
        for (AX_U32 i = 0; i < m_tConfigParam.tPushTargetVec.size(); i ++) {
            AX_SKEL_TARGET_ITEM_S stItem;
            stItem.pstrObjectCategory = m_tConfigParam.tPushTargetVec[i].c_str();
            stPushTargetItemVec.emplace_back(stItem);
        }

        stPushTarget.nSize = stPushTargetItemVec.size();
        stPushTarget.pstItems = stPushTargetItemVec.data();

        memset(&tConfigItem, 0x00, sizeof(tConfigItem));
        tConfigItem.pstrType = (AX_CHAR *)"push_target_config";
        tConfigItem.pstrValue = (AX_VOID *)&stPushTarget;
        tConfigItem.nValueSize = sizeof(AX_SKEL_TARGET_CONFIG_S);

        vContent.emplace_back(tConfigItem);
    }

    // track enable
    AX_SKEL_COMMON_ENABLE_CONFIG_S stTrackEnable;
    if (!m_tConfigParam.bTrackEnable) {
        memset(&stTrackEnable, 0x00, sizeof(stTrackEnable));
        stTrackEnable.bEnable = (AX_BOOL)m_tConfigParam.bTrackEnable;

        memset(&tConfigItem, 0x00, sizeof(tConfigItem));
        tConfigItem.pstrType = (AX_CHAR *)"track_enable";
        tConfigItem.pstrValue = (AX_VOID *)&stTrackEnable;
        tConfigItem.nValueSize = sizeof(AX_SKEL_COMMON_ENABLE_CONFIG_S);

        vContent.emplace_back(tConfigItem);
    }

    // push enable
    AX_SKEL_COMMON_ENABLE_CONFIG_S stPushEnable;
    if (!m_tConfigParam.bPushEnable) {
        memset(&stPushEnable, 0x00, sizeof(stPushEnable));
        stPushEnable.bEnable = (AX_BOOL)m_tConfigParam.bPushEnable;

        memset(&tConfigItem, 0x00, sizeof(tConfigItem));
        tConfigItem.pstrType = (AX_CHAR *)"push_enable";
        tConfigItem.pstrValue = (AX_VOID *)&stPushEnable;
        tConfigItem.nValueSize = sizeof(AX_SKEL_COMMON_ENABLE_CONFIG_S);

        vContent.emplace_back(tConfigItem);
    }

    // push attr always
    AX_SKEL_COMMON_ENABLE_CONFIG_S stPushAttrAlways;
    if (!m_tConfigParam.bPushAttrAlways) {
        memset(&stPushAttrAlways, 0x00, sizeof(stPushAttrAlways));
        stPushAttrAlways.bEnable = (AX_BOOL)m_tConfigParam.bPushAttrAlways;

        memset(&tConfigItem, 0x00, sizeof(tConfigItem));
        tConfigItem.pstrType = (AX_CHAR *)"push_attr_always";
        tConfigItem.pstrValue = (AX_VOID *)&stPushAttrAlways;
        tConfigItem.nValueSize = sizeof(AX_SKEL_COMMON_ENABLE_CONFIG_S);

        vContent.emplace_back(tConfigItem);
    }

    stConfig.nSize = vContent.size();
    stConfig.pstItems = vContent.data();

    stHandleParam.stConfig = stConfig;

    DETECTOR_API_RUN_START(AX_SKEL_Create);
    nRet = AX_SKEL_Create(&stHandleParam, &m_stream_handle);
    DETECTOR_API_RUN_END(AX_SKEL_Create);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "SKEL Create Handle fail, ret = 0x%x", nRet);

        return AX_FALSE;
    }

    bRet = SetHandleConfig(&m_tConfigParam);

    if (!bRet) {
        LOG_M_E(DETECTION, "SetHandleConfig error");
        return AX_FALSE;
    }

    m_bGetResultThreadRunning = AX_TRUE;
    m_pGetResultThread = new thread(AsyncRecvAlgorithmResultThread, this);

    return AX_TRUE;
}

AX_BOOL CDetector::SetHandleConfig(DETECTOR_CONFIG_PARAM_T *ptConfigParam) {
    if (!m_stream_handle) {
        LOG_M_E(DETECTION, "handle null..");

        return AX_FALSE;
    }

    // update push strategy
    SetPushStrategy(&ptConfigParam->tPushStrategy);

    // update crop qp level
    SetCropEncoderQpLevel(ptConfigParam->fCropEncoderQpLevel);

    // update panorama
    SetPanoramaEnable(ptConfigParam->bPanoramaEnable);

    // update roi
    SetRoi(&ptConfigParam->tRoi);

    // update track size
    SetTrackSize(&ptConfigParam->tTrackSize);

    // update object crop threshold
    for (auto iter = ptConfigParam->tCropThreshold.begin(); iter != ptConfigParam->tCropThreshold.end();) {
        SetCropThreshold(iter->first, iter->second);

        ++iter;
    }

    // update object fliter
    for (auto iter = ptConfigParam->tObjectFliter.begin(); iter != ptConfigParam->tObjectFliter.end();) {
        SetObjectFilter(iter->first, iter->second);

        ++iter;
    }

    // update attr object fliter
    for (auto iter = ptConfigParam->tAttrFliter.begin(); iter != ptConfigParam->tAttrFliter.end();) {
        SetAttrFilter(iter->first, iter->second);

        ++iter;
    }

    return AX_TRUE;
}

AX_BOOL CDetector::AsyncRecvDetectionResult(AX_VOID)
{
    AX_SKEL_RESULT_S *algorithm_result = nullptr;

    AX_S32 nRet = AX_SKEL_GetResult(m_stream_handle, &algorithm_result, -1);

    if (AX_SKEL_SUCC != nRet) {
        if (AX_ERR_SKEL_QUEUE_EMPTY != nRet) {
            LOG_M_E(DETECTION, "AX_SKEL_GetResult error: %d", nRet);
        }

        return AX_FALSE;
    }

    return DetectionResultHandler(algorithm_result);
}

AX_BOOL CDetector::DetectionResultHandler(AX_SKEL_RESULT_S *algorithm_result)
{
    if (!algorithm_result) {
        return AX_FALSE;
    }

    AX_U64 frame_id = (AX_U32)algorithm_result->nFrameId;
    AX_U32 nWidth = algorithm_result->nOriginalWidth;
    AX_U32 nHeight = algorithm_result->nOriginalHeight;
    auto endTime = std::chrono::steady_clock::now();
    DET_PERF_INFO_T tPerfInfo = {0};
    AX_BOOL bAddPerfInfo = AX_FALSE;

    m_mutex.lock();
    AX_U64 nActualFrameId = frame_id;
    auto iter = m_mapDataResult.find(frame_id);
    if (iter != m_mapDataResult.end()) {
        AX_U32 nElapsed = (AX_U32)(std::chrono::duration_cast<std::chrono::milliseconds>(endTime - iter->second.m_tpStart).count());

        bAddPerfInfo = AX_TRUE;

        tPerfInfo.nElapsed = nElapsed;

        nActualFrameId = (AX_U32)iter->second.m_sVideoFrame.u64SeqNum;

        LOG_M_I(DETECTION, "Frame_id(%lld) detect elapsed %d(ms)", frame_id, nElapsed);
    } else {
        LOG_M_I(DETECTION, "m_mapDataResult frame_id(%lld) not found", frame_id);
    }

    ClearAlgorithmData(algorithm_result);

    m_mutex.unlock();

    do {
        if (algorithm_result->nObjectSize > 0
            && nWidth > 0
            && nHeight > 0) {
            // Object Definition
            ObjectDefinition(Face)
            ObjectDefinition(Body)
            ObjectDefinition(Vehicle)
            ObjectDefinition(Plate)
            ObjectDefinition(Cycle)

            AI_Detection_Result_t DetectionResult;
            AI_Detection_Result_t *pDetectionResult = &DetectionResult;
            pDetectionResult->nErrorCode = AX_SUCCESS;
            pDetectionResult->nFrameId = nActualFrameId;

            for (size_t i = 0; i < algorithm_result->nObjectSize; i++) {
                AX_SKEL_OBJECT_ITEM_S &stItem = algorithm_result->pstObjectItems[i];
                const char *pstrObjectCategory = (const char *)stItem.pstrObjectCategory;
                if (pstrObjectCategory) {
                    // Object Calculate
                    ObjectCalculate(Face);
                    ObjectCalculate(Body);
                    ObjectCalculate(Vehicle);
                    ObjectCalculate(Plate);
                    ObjectCalculate(Cycle);
                }
            }

            // Object Create
            ObjectCreate(Face);
            ObjectCreate(Body);
            ObjectCreate(Vehicle);
            ObjectCreate(Plate);
            ObjectCreate(Cycle);

            for (size_t i = 0; i < algorithm_result->nObjectSize; i++) {
                AX_SKEL_OBJECT_ITEM_S &stItem = algorithm_result->pstObjectItems[i];
                const char *pstrObjectCategory = (const char *)stItem.pstrObjectCategory;
                if (pstrObjectCategory) {
                    JpegDataInfo JpegInfo;

                    AI_Detection_Box_t tBox = {0};
                    tBox.fX = stItem.stRect.fX/nWidth;
                    tBox.fY = stItem.stRect.fY/nHeight;
                    tBox.fW = stItem.stRect.fW/nWidth;
                    tBox.fH = stItem.stRect.fH/nHeight;

                    AX_U64 u64TrackId = stItem.nTrackId;
                    AX_F32 fConfidence = stItem.fConfidence;
                    AX_SKEL_TRACK_STATUS_E eTrackState = stItem.eTrackState;

                    // Object Assign
                    ObjectAssign(Face);
                    ObjectAssign(Body);
                    ObjectAssign(Vehicle);
                    ObjectAssign(Plate);
                    ObjectAssign(Cycle);

                    //search
                    AX_BOOL isObjectTrack = AX_TRUE;

                    if (gOptions.IsActivedSearchFromWeb() && m_pObjectSearch) {
                        isObjectTrack = m_pObjectSearch->SyncSearch(&stItem);

                        if (isObjectTrack) {
                            strncpy(JpegInfo.tFaceInfo.szInfo, (const AX_CHAR *)"identified", sizeof(JpegInfo.tFaceInfo.szInfo) - 1);
                        }
                        else {
                            strncpy(JpegInfo.tFaceInfo.szInfo, (const AX_CHAR *)"unknown", sizeof(JpegInfo.tFaceInfo.szInfo) - 1);
                        }
                    }

                    // Object Track
                    if (m_pTrackCropStage) {
                        auto DoTracking = [&](AX_SKEL_OBJECT_ITEM_S stObjectItem) {
                                if (AX_SKEL_TRACK_STATUS_SELECT == eTrackState
                                    && stObjectItem.bCropFrame
                                    && stObjectItem.stCropFrame.pFrameData
                                    && 0 < stObjectItem.stCropFrame.nFrameDataSize) {
                                    m_pTrackCropStage->DoTracking((AX_VOID *)stObjectItem.stCropFrame.pFrameData,
                                                                    stObjectItem.stCropFrame.nFrameDataSize,
                                                                    &JpegInfo);
                            }
                        };

                        // Object Track
                        if (isObjectTrack) {
                            if (AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_BODY)) {
                                ObjectTrack(Body);
                            }
                            if (AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_VEHICLE)) {
                                ObjectTrack(Vehicle);
                            }
                            if (AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_CYCLE)) {
                                ObjectTrack(Cycle);
                            }
                            if (AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_FACE)) {
                                ObjectTrack(Face);
                            }
                            if (AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_PLATE)) {
                                ObjectTrack(Plate);
                            }
                        }
                    }
                }
            }

            gOptions.SetDetectResult(0, pDetectionResult);

            // Object Destroy
            ObjectDestroy(Face);
            ObjectDestroy(Body);
            ObjectDestroy(Vehicle);
            ObjectDestroy(Plate);
            ObjectDestroy(Cycle);
        }
        else {
            gOptions.SetDetectResult(0, NULL);
        }
    }while(0);

    if (bAddPerfInfo) {
        gPrintHelper.Add(E_PH_MOD_DET_PERF, (AX_VOID *)&tPerfInfo);
    }

    AX_SKEL_Release(algorithm_result);

    return AX_TRUE;
}

AX_BOOL CDetector::ClearAlgorithmData(AX_SKEL_RESULT_S *algorithm_result) {
    for (auto iter = m_mapDataResult.begin(); iter != m_mapDataResult.end();) {
        if (iter->first > algorithm_result->nFrameId) {//TODO
            break;
        }

        AX_BOOL need_clear_algorithm_data = AX_TRUE;
        for (AX_U32 i = 0; i < algorithm_result->nCacheListSize; i++) {
            if (iter->first == (AX_U32)algorithm_result->pstCacheList[i].nFrameId) {
                need_clear_algorithm_data = AX_FALSE;
                break;
            }
        }

        if (need_clear_algorithm_data) {
            m_mapDataResult.erase(iter++);
        } else {
            ++iter;
        }
    }

    return AX_TRUE;
}

AX_BOOL CDetector::ClearAlgorithmData(AX_VOID) {
    m_mutex.lock();
    for (auto iter = m_mapDataResult.begin(); iter != m_mapDataResult.end();) {
        m_mapDataResult.erase(iter++);
    }
    m_mutex.unlock();

    return AX_TRUE;
}

AX_BOOL CDetector::WaitForFinish(AX_VOID) {
    //Wait sent finish
    while(!m_bFinished) {
        CTimeUtils::msSleep(10);
    }

    return AX_TRUE;
}

DETECTOR_CONFIG_PARAM_T CDetector::GetConfig(AX_VOID)
{
    std::lock_guard<std::mutex> lck(m_stMutex);

    return (DETECTOR_CONFIG_PARAM_T)m_tConfigParam;
}

AX_BOOL CDetector::SetConfig(DETECTOR_CONFIG_PARAM_T *pConfig)
{
    if (!pConfig || !m_stream_handle) {
        return AX_FALSE;
    }

    // update param
    {
        std::lock_guard<std::mutex> lck(m_stMutex);

        m_tConfigParam.nTrackType = pConfig->nTrackType;
        m_tConfigParam.nDrawRectType = pConfig->nDrawRectType;
        m_tConfigParam.bPlateIdentify = pConfig->bPlateIdentify;
    }

    // update roi
    SetRoi(&pConfig->tRoi);

    // update push strategy
    SetPushStrategy(&pConfig->tPushStrategy);

    // update object crop threshold
    for (auto iter = pConfig->tCropThreshold.begin(); iter != pConfig->tCropThreshold.end();) {
        SetCropThreshold(iter->first, iter->second);

        ++iter;
    }

    // update object fliter
    for (auto iter = pConfig->tObjectFliter.begin(); iter != pConfig->tObjectFliter.end();) {
        SetObjectFilter(iter->first, iter->second);

        ++iter;
    }

    // update attr object fliter
    for (auto iter = pConfig->tAttrFliter.begin(); iter != pConfig->tAttrFliter.end();) {
        SetAttrFilter(iter->first, iter->second);

        ++iter;
    }

    return AX_TRUE;
}

AX_BOOL CDetector::SetRoi(DETECTOR_ROI_CONFIG_T *ptRoi)
{
    std::lock_guard<std::mutex> lck(m_stMutex);

    if (!ptRoi || !m_stream_handle) {
        return AX_FALSE;
    }

    std::vector<AX_SKEL_CONFIG_ITEM_S> vContent;
    AX_SKEL_CONFIG_S tConfigParam;
    AX_SKEL_CONFIG_ITEM_S tConfigItem;
    AX_SKEL_ROI_POLYGON_CONFIG_S tRoiParam;

    vContent.clear();
    memset(&tConfigParam, 0, sizeof(AX_SKEL_CONFIG_S));

    AX_F32 x1 = ptRoi->tBox.fX;
    AX_F32 y1 = ptRoi->tBox.fY;
    AX_F32 x2 = x1 + ptRoi->tBox.fW + 1;
    AX_F32 y2 = y1 + ptRoi->tBox.fH + 1;
    AX_SKEL_POINT_S stPoint[4] = {{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}};

    tRoiParam.bEnable = ptRoi->bEnable;
    tRoiParam.nPointNum = 4;
    tRoiParam.pstPoint = (AX_SKEL_POINT_S *)stPoint;

    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (AX_CHAR *)"detect_roi_polygon";
    tConfigItem.pstrValue = (AX_VOID *)&tRoiParam;
    tConfigItem.nValueSize = sizeof(AX_SKEL_ROI_POLYGON_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    tConfigParam.pstItems = vContent.data();
    tConfigParam.nSize = vContent.size();

    AX_S32 nRet = AX_SKEL_SetConfig(m_stream_handle, &tConfigParam);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "AX_SKEL_SetConfig error: %d", nRet);

        return AX_FALSE;
    }

    m_tConfigParam.tRoi = *ptRoi;

    return AX_TRUE;
}

AX_BOOL CDetector::SetPushStrategy(DETECTOR_PUSH_STRATEGY_T *ptPushStrategy)
{
    std::lock_guard<std::mutex> lck(m_stMutex);

    if (!ptPushStrategy || !m_stream_handle) {
        return AX_FALSE;
    }

    std::vector<AX_SKEL_CONFIG_ITEM_S> vContent;
    AX_SKEL_CONFIG_S tConfigParam;
    AX_SKEL_CONFIG_ITEM_S tConfigItem;

    vContent.clear();
    memset(&tConfigParam, 0, sizeof(AX_SKEL_CONFIG_S));

    AX_SKEL_PUSH_STRATEGY_S stPushStrategy;

    //set push_strategy
    stPushStrategy.ePushMode = (AX_SKEL_PUSH_MODE_E)ptPushStrategy->nPushMode;
    stPushStrategy.nIntervalTimes = ptPushStrategy->nInterval;
    stPushStrategy.nPushCounts = ptPushStrategy->nPushCounts;
    stPushStrategy.bPushSameFrame = ptPushStrategy->bPushSameFrame;

    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)"push_strategy";
    tConfigItem.pstrValue = (void *)&stPushStrategy;
    tConfigItem.nValueSize = sizeof(AX_SKEL_PUSH_STRATEGY_S);
    vContent.emplace_back(tConfigItem);

    tConfigParam.pstItems = vContent.data();
    tConfigParam.nSize = vContent.size();

    AX_S32 nRet = AX_SKEL_SetConfig(m_stream_handle, &tConfigParam);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "AX_SKEL_SetConfig error: %d", nRet);

        return AX_FALSE;
    }

    m_tConfigParam.tPushStrategy = *ptPushStrategy;

    return AX_TRUE;
}

AX_BOOL CDetector::SetObjectFilter(std::string strObject, const DETECTOR_OBJECT_FILTER_CONFIG_T &tObjectFliter)
{
    std::lock_guard<std::mutex> lck(m_stMutex);

    if (!m_stream_handle) {
        return AX_FALSE;
    }

    if (strObject != "body"
        && strObject != "vehicle"
        && strObject != "cycle"
        && strObject != "face"
        && strObject != "plate") {
        LOG_M_E(DETECTION, "wrong object: %s", strObject.c_str());
        return AX_FALSE;
    }

    std::vector<AX_SKEL_CONFIG_ITEM_S> vContent;
    AX_SKEL_CONFIG_S tConfigParam;
    AX_SKEL_CONFIG_ITEM_S tConfigItem;
    AX_SKEL_COMMON_THRESHOLD_CONFIG_S tConfidenceParam;
    AX_SKEL_OBJECT_SIZE_FILTER_CONFIG_S tObjectSizeFilterParam;

    vContent.clear();
    memset(&tConfigParam, 0, sizeof(AX_SKEL_CONFIG_S));

    memset(&tObjectSizeFilterParam, 0x00, sizeof(tObjectSizeFilterParam));
    tObjectSizeFilterParam.nWidth = tObjectFliter.nWidth;
    tObjectSizeFilterParam.nHeight = tObjectFliter.nHeight;

    std::string aObject;
    aObject = strObject + "_min_size";

    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)aObject.c_str();
    tConfigItem.pstrValue = (AX_VOID *)&tObjectSizeFilterParam;
    tConfigItem.nValueSize = sizeof(AX_SKEL_OBJECT_SIZE_FILTER_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    std::string aObjectConfidence;
    aObjectConfidence = strObject + "_confidence";

    tConfidenceParam.fValue = (AX_F32)tObjectFliter.fConfidence;
    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)aObjectConfidence.c_str();
    tConfigItem.pstrValue = (AX_VOID *)&tConfidenceParam;
    tConfigItem.nValueSize = sizeof(AX_SKEL_COMMON_THRESHOLD_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    tConfigParam.pstItems = vContent.data();
    tConfigParam.nSize = vContent.size();

    AX_S32 nRet = AX_SKEL_SetConfig(m_stream_handle, &tConfigParam);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "AX_SKEL_SetConfig error: %d", nRet);

        return AX_FALSE;
    }

    m_tConfigParam.tObjectFliter[strObject] = tObjectFliter;

    LOG_M(DETECTION, "%s filter(%d X %d, confidence: %.2f)", strObject.c_str(), m_tConfigParam.tObjectFliter[strObject].nWidth,
          m_tConfigParam.tObjectFliter[strObject].nHeight, m_tConfigParam.tObjectFliter[strObject].fConfidence);

    return AX_TRUE;
}

AX_BOOL CDetector::SetAttrFilter(std::string strObject, const AX_SKEL_ATTR_FILTER_CONFIG_S &tAttrFliter)
{
    std::lock_guard<std::mutex> lck(m_stMutex);

    if (!m_stream_handle) {
        return AX_FALSE;
    }

    if (strObject != "body"
        && strObject != "vehicle"
        && strObject != "cycle"
        && strObject != "face"
        && strObject != "plate") {
        LOG_M_E(DETECTION, "wrong object: %s", strObject.c_str());
        return AX_FALSE;
    }

    std::vector<AX_SKEL_CONFIG_ITEM_S> vContent;
    AX_SKEL_CONFIG_S tConfigParam;
    AX_SKEL_CONFIG_ITEM_S tConfigItem;

    vContent.clear();
    memset(&tConfigParam, 0, sizeof(AX_SKEL_CONFIG_S));

    std::string aObject;
    aObject = "push_quality_" + strObject;

    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)aObject.c_str();
    tConfigItem.pstrValue = (AX_VOID *)&tAttrFliter;
    tConfigItem.nValueSize = sizeof(AX_SKEL_ATTR_FILTER_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    tConfigParam.pstItems = vContent.data();
    tConfigParam.nSize = vContent.size();

    AX_S32 nRet = AX_SKEL_SetConfig(m_stream_handle, &tConfigParam);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "AX_SKEL_SetConfig error: %d", nRet);

        return AX_FALSE;
    }

    m_tConfigParam.tAttrFliter[strObject] = tAttrFliter;

    if (strObject == "face") {
        LOG_M(DETECTION, "%s Attr filter(%d X %d, [P:%.2f, Y:%.2f, R:%.2f, B:%.2f])",
            strObject.c_str(),
            m_tConfigParam.tAttrFliter[strObject].stFaceAttrFilterConfig.nWidth,
            m_tConfigParam.tAttrFliter[strObject].stFaceAttrFilterConfig.nHeight,
            m_tConfigParam.tAttrFliter[strObject].stFaceAttrFilterConfig.stPoseblur.fPitch,
            m_tConfigParam.tAttrFliter[strObject].stFaceAttrFilterConfig.stPoseblur.fYaw,
            m_tConfigParam.tAttrFliter[strObject].stFaceAttrFilterConfig.stPoseblur.fRoll,
            m_tConfigParam.tAttrFliter[strObject].stFaceAttrFilterConfig.stPoseblur.fBlur);
    }
    else {
        LOG_M(DETECTION, "%s Attr filter(quality: %.2f)",
            strObject.c_str(),
            m_tConfigParam.tAttrFliter[strObject].stCommonAttrFilterConfig.fQuality);
    }

    return AX_TRUE;
}

AX_BOOL CDetector::SetTrackSize(DETECTOR_TRACK_SIZE_T *ptTrackSize)
{
    std::lock_guard<std::mutex> lck(m_stMutex);

    if (!ptTrackSize || !m_stream_handle) {
        return AX_FALSE;
    }

    std::vector<AX_SKEL_CONFIG_ITEM_S> vContent;
    AX_SKEL_CONFIG_S tConfigParam;
    AX_SKEL_CONFIG_ITEM_S tConfigItem;

    vContent.clear();
    memset(&tConfigParam, 0, sizeof(AX_SKEL_CONFIG_S));

    //set max track human size
    AX_SKEL_COMMON_THRESHOLD_CONFIG_S max_track_human_size;
    max_track_human_size.fValue = (float)ptTrackSize->nTrackHumanSize;
    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)"body_max_target_count";
    tConfigItem.pstrValue = (void *)&max_track_human_size;
    tConfigItem.nValueSize = sizeof(AX_SKEL_COMMON_THRESHOLD_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    //set max track vehicle size
    AX_SKEL_COMMON_THRESHOLD_CONFIG_S max_track_vehicle_size;
    max_track_vehicle_size.fValue = (float)ptTrackSize->nTrackVehicleSize;
    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)"vehicle_max_target_count";
    tConfigItem.pstrValue = (void *)&max_track_vehicle_size;
    tConfigItem.nValueSize = sizeof(AX_SKEL_COMMON_THRESHOLD_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    //set max track cycle size
    AX_SKEL_COMMON_THRESHOLD_CONFIG_S max_track_cycle_size;
    max_track_cycle_size.fValue = (float)ptTrackSize->nTrackCycleSize;
    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)"cycle_max_target_count";
    tConfigItem.pstrValue = (void *)&max_track_cycle_size;
    tConfigItem.nValueSize = sizeof(AX_SKEL_COMMON_THRESHOLD_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    tConfigParam.pstItems = vContent.data();
    tConfigParam.nSize = vContent.size();

    AX_S32 nRet = AX_SKEL_SetConfig(m_stream_handle, &tConfigParam);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "AX_SKEL_SetConfig error: %d", nRet);

        return AX_FALSE;
    }

    m_tConfigParam.tTrackSize = *ptTrackSize;

    LOG_M(DETECTION, "Track size(human: %d, vehicle: %d, cycle: %d)",
                            m_tConfigParam.tTrackSize.nTrackHumanSize,
                            m_tConfigParam.tTrackSize.nTrackVehicleSize,
                            m_tConfigParam.tTrackSize.nTrackCycleSize);

    return AX_TRUE;
}

AX_BOOL CDetector::SetPanoramaEnable(AX_BOOL bEnable)
{
    std::lock_guard<std::mutex> lck(m_stMutex);

    if (!m_stream_handle) {
        return AX_FALSE;
    }

    std::vector<AX_SKEL_CONFIG_ITEM_S> vContent;
    AX_SKEL_CONFIG_S tConfigParam;
    AX_SKEL_CONFIG_ITEM_S tConfigItem;

    vContent.clear();
    memset(&tConfigParam, 0, sizeof(AX_SKEL_CONFIG_S));

    //set panorama
    AX_SKEL_PUSH_PANORAMA_CONFIG_S stPanoramaConfig;
    stPanoramaConfig.bEnable = bEnable;
    stPanoramaConfig.nQuality = m_tConfigParam.fCropEncoderQpLevel;
    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)"push_panorama";
    tConfigItem.pstrValue = (void *)&stPanoramaConfig;
    tConfigItem.nValueSize = sizeof(AX_SKEL_PUSH_PANORAMA_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    tConfigParam.pstItems = vContent.data();
    tConfigParam.nSize = vContent.size();

    AX_S32 nRet = AX_SKEL_SetConfig(m_stream_handle, &tConfigParam);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "AX_SKEL_SetConfig error: %d", nRet);

        return AX_FALSE;
    }

    m_tConfigParam.bPanoramaEnable = bEnable;

    return AX_TRUE;
}

AX_BOOL CDetector::SetCropThreshold(std::string strObject, const AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S &tCropThreshold)
{
    std::lock_guard<std::mutex> lck(m_stMutex);

    if (!m_stream_handle) {
        return AX_FALSE;
    }

    if (strObject != "body"
        && strObject != "vehicle"
        && strObject != "cycle"
        && strObject != "face"
        && strObject != "plate") {
        LOG_M_E(DETECTION, "wrong object: %s", strObject.c_str());
        return AX_FALSE;
    }

    std::vector<AX_SKEL_CONFIG_ITEM_S> vContent;
    AX_SKEL_CONFIG_S tConfigParam;
    AX_SKEL_CONFIG_ITEM_S tConfigItem;

    vContent.clear();
    memset(&tConfigParam, 0, sizeof(AX_SKEL_CONFIG_S));

    std::string aObject;
    aObject = strObject + "_crop_encoder";

    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)aObject.c_str();
    tConfigItem.pstrValue = (AX_VOID *)&tCropThreshold;
    tConfigItem.nValueSize = sizeof(AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    tConfigParam.pstItems = vContent.data();
    tConfigParam.nSize = vContent.size();

    AX_S32 nRet = AX_SKEL_SetConfig(m_stream_handle, &tConfigParam);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "AX_SKEL_SetConfig error: %d", nRet);

        return AX_FALSE;
    }

    m_tConfigParam.tCropThreshold[strObject] = tCropThreshold;

    LOG_M(DETECTION, "%s CropThreshold(%.2f, %.2f, %.2f, %.2f)",
            strObject.c_str(),
            m_tConfigParam.tCropThreshold[strObject].fScaleLeft,
            m_tConfigParam.tCropThreshold[strObject].fScaleRight,
            m_tConfigParam.tCropThreshold[strObject].fScaleTop,
            m_tConfigParam.tCropThreshold[strObject].fScaleBottom);

    return AX_TRUE;
}

AX_BOOL CDetector::SetCropEncoderQpLevel(AX_F32 fCropEncoderQpLevel)
{
    std::lock_guard<std::mutex> lck(m_stMutex);

    if (!m_stream_handle) {
        return AX_FALSE;
    }

    std::vector<AX_SKEL_CONFIG_ITEM_S> vContent;
    AX_SKEL_CONFIG_S tConfigParam;
    AX_SKEL_CONFIG_ITEM_S tConfigItem;

    vContent.clear();
    memset(&tConfigParam, 0, sizeof(AX_SKEL_CONFIG_S));

    //set crop encoder qpLevel
    AX_SKEL_COMMON_THRESHOLD_CONFIG_S crop_encoder_qpLevel_threshold;
    crop_encoder_qpLevel_threshold.fValue = (float)fCropEncoderQpLevel;
    memset(&tConfigItem, 0, sizeof(AX_SKEL_CONFIG_ITEM_S));
    tConfigItem.pstrType = (char *)"crop_encoder_qpLevel";
    tConfigItem.pstrValue = (void *)&crop_encoder_qpLevel_threshold;
    tConfigItem.nValueSize = sizeof(AX_SKEL_COMMON_THRESHOLD_CONFIG_S);
    vContent.emplace_back(tConfigItem);

    tConfigParam.pstItems = vContent.data();
    tConfigParam.nSize = vContent.size();

    AX_S32 nRet = AX_SKEL_SetConfig(m_stream_handle, &tConfigParam);

    if (0 != nRet) {
        LOG_M_E(DETECTION, "AX_SKEL_SetConfig error: %d", nRet);

        return AX_FALSE;
    }

    m_tConfigParam.fCropEncoderQpLevel = fCropEncoderQpLevel;

    return AX_TRUE;
}

AX_BOOL CDetector::UpdateAiAttr(AX_VOID)
{
    CStageOptionHelper *pStageOption = CStageOptionHelper().GetInstance();
    AI_ATTR_T tAiAttr = pStageOption->GetAiAttr();

    tAiAttr.nEnable = AX_TRUE;

    if (m_tConfigParam.ePPL == AX_SKEL_PPL_HVCFP
        || m_tConfigParam.ePPL == AX_SKEL_PPL_HVCP) {
        tAiAttr.eDetectModel = E_AI_DETECT_MODEL_TYPE_HVCFP;
        tAiAttr.nDetectOnly = 0;

        tAiAttr.tHvcfpSetting.tFace.nEnable = AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_FACE) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHvcfpSetting.tFace.nDrawRect = AX_BIT_CHECK(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_FACE) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHvcfpSetting.tBody.nEnable = AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_BODY) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHvcfpSetting.tBody.nDrawRect = AX_BIT_CHECK(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_BODY) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHvcfpSetting.tVechicle.nEnable = AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_VEHICLE) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHvcfpSetting.tVechicle.nDrawRect = AX_BIT_CHECK(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_VEHICLE) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHvcfpSetting.tCycle.nEnable = AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_CYCLE) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHvcfpSetting.tCycle.nDrawRect = AX_BIT_CHECK(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_CYCLE) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHvcfpSetting.tPlate.nEnable = AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_PLATE) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHvcfpSetting.tPlate.nDrawRect = AX_BIT_CHECK(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_PLATE) ? AX_TRUE : AX_FALSE;

        tAiAttr.tHvcfpSetting.nEnablePI = (AX_U16)m_tConfigParam.bPlateIdentify;
    }
    else {
        if (m_tConfigParam.ePPL == AX_SKEL_PPL_BODY || m_tConfigParam.ePPL == AX_SKEL_PPL_POSE) {
            tAiAttr.tHumanFaceSetting.tFace.nEnable = 0;
            tAiAttr.tHumanFaceSetting.tBody.nEnable = 0;
            tAiAttr.tHumanFaceSetting.nEnableFI = 0;
            tAiAttr.nDetectOnly = 1;
        }
        else {
            tAiAttr.tHumanFaceSetting.tFace.nEnable = AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_FACE) ? AX_TRUE : AX_FALSE;
            tAiAttr.tHumanFaceSetting.tBody.nEnable = AX_BIT_CHECK(m_tConfigParam.nTrackType, AI_TRACK_TYPE_BODY) ? AX_TRUE : AX_FALSE;
            tAiAttr.tHumanFaceSetting.nEnableFI = gOptions.IsActivedSearchFromWeb();
            tAiAttr.nDetectOnly = 0;
        }

        tAiAttr.eDetectModel = E_AI_DETECT_MODEL_TYPE_FACEHUMAN;
        tAiAttr.tHumanFaceSetting.tFace.nDrawRect = AX_BIT_CHECK(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_FACE) ? AX_TRUE : AX_FALSE;
        tAiAttr.tHumanFaceSetting.tBody.nDrawRect = AX_BIT_CHECK(m_tConfigParam.nDrawRectType, AI_DRAW_RECT_TYPE_BODY) ? AX_TRUE : AX_FALSE;
    }

    tAiAttr.tPushStrgy.eMode = (AI_DETECT_PUSH_MODE_TYPE_E)m_tConfigParam.tPushStrategy.nPushMode;
    tAiAttr.tPushStrgy.nInterval = (AX_U16)m_tConfigParam.tPushStrategy.nInterval;
    tAiAttr.tPushStrgy.nCount = (AX_U16)m_tConfigParam.tPushStrategy.nPushCounts;
    tAiAttr.tPushStrgy.nPushSameFrame = (AX_U8)m_tConfigParam.tPushStrategy.bPushSameFrame;

    pStageOption->SetAiAttr(tAiAttr);

    return AX_TRUE;
}

AX_BOOL CDetector::UpdateConfig(const AI_ATTR_T& tAiAttr)
{
    gOptions.SetDetectActived((AX_BOOL)tAiAttr.nEnable);

    AX_U32 nTrackType = 0;
    AX_U32 nDrawRectType = 0;
    AX_BOOL bPlateIdentify = AX_TRUE;
    DETECTOR_PUSH_STRATEGY_T tPushStrategy = {0};

    if (E_AI_DETECT_MODEL_TYPE_FACEHUMAN == tAiAttr.eDetectModel) {
        //face
        if (tAiAttr.tHumanFaceSetting.tFace.nEnable) {
            AX_BIT_SET(nTrackType, AI_TRACK_TYPE_FACE);
        }
        else {
            AX_BIT_CLEAR(nTrackType, AI_TRACK_TYPE_FACE);
        }
        if (tAiAttr.tHumanFaceSetting.tFace.nDrawRect) {
            AX_BIT_SET(nDrawRectType, AI_DRAW_RECT_TYPE_FACE);
        }
        else {
            AX_BIT_CLEAR(nDrawRectType, AI_DRAW_RECT_TYPE_FACE);
        }

        //body
        if (tAiAttr.tHumanFaceSetting.tBody.nEnable) {
            AX_BIT_SET(nTrackType, AI_TRACK_TYPE_BODY);
        }
        else {
            AX_BIT_CLEAR(nTrackType, AI_TRACK_TYPE_BODY);
        }
        if (tAiAttr.tHumanFaceSetting.tBody.nDrawRect) {
            AX_BIT_SET(nDrawRectType, AI_DRAW_RECT_TYPE_BODY);
            AX_BIT_SET(nDrawRectType, AI_DRAW_RECT_TYPE_POSE);
        }
        else {
            AX_BIT_CLEAR(nDrawRectType, AI_DRAW_RECT_TYPE_BODY);
            AX_BIT_CLEAR(nDrawRectType, AI_DRAW_RECT_TYPE_POSE);
        }

        gOptions.SetSearchActived((AX_BOOL)tAiAttr.tHumanFaceSetting.nEnableFI);
    }
    else if (E_AI_DETECT_MODEL_TYPE_HVCFP == tAiAttr.eDetectModel) {
        //face
        if (tAiAttr.tHvcfpSetting.tFace.nEnable) {
            AX_BIT_SET(nTrackType, AI_TRACK_TYPE_FACE);
        }
        else {
            AX_BIT_CLEAR(nTrackType, AI_TRACK_TYPE_FACE);
        }
        if (tAiAttr.tHvcfpSetting.tFace.nDrawRect) {
            AX_BIT_SET(nDrawRectType, AI_DRAW_RECT_TYPE_FACE);
        }
        else {
            AX_BIT_CLEAR(nDrawRectType, AI_DRAW_RECT_TYPE_FACE);
        }

        //body
        if (tAiAttr.tHvcfpSetting.tBody.nEnable) {
            AX_BIT_SET(nTrackType, AI_TRACK_TYPE_BODY);
        }
        else {
            AX_BIT_CLEAR(nTrackType, AI_TRACK_TYPE_BODY);
        }
        if (tAiAttr.tHvcfpSetting.tBody.nDrawRect) {
            AX_BIT_SET(nDrawRectType, AI_DRAW_RECT_TYPE_BODY);
            AX_BIT_SET(nDrawRectType, AI_DRAW_RECT_TYPE_POSE);
        }
        else {
            AX_BIT_CLEAR(nDrawRectType, AI_DRAW_RECT_TYPE_BODY);
            AX_BIT_CLEAR(nDrawRectType, AI_DRAW_RECT_TYPE_POSE);
        }

        //vehicle
        if (tAiAttr.tHvcfpSetting.tVechicle.nEnable) {
            AX_BIT_SET(nTrackType, AI_TRACK_TYPE_VEHICLE);
        }
        else {
            AX_BIT_CLEAR(nTrackType, AI_TRACK_TYPE_VEHICLE);
        }
        if (tAiAttr.tHvcfpSetting.tVechicle.nDrawRect) {
            AX_BIT_SET(nDrawRectType, AI_DRAW_RECT_TYPE_VEHICLE);
        }
        else {
            AX_BIT_CLEAR(nDrawRectType, AI_DRAW_RECT_TYPE_VEHICLE);
        }

        //cycle
        if (tAiAttr.tHvcfpSetting.tCycle.nEnable) {
            AX_BIT_SET(nTrackType, AI_TRACK_TYPE_CYCLE);
        }
        else {
            AX_BIT_CLEAR(nTrackType, AI_TRACK_TYPE_CYCLE);
        }
        if (tAiAttr.tHvcfpSetting.tCycle.nDrawRect) {
            AX_BIT_SET(nDrawRectType, AI_DRAW_RECT_TYPE_CYCLE);
        }
        else {
            AX_BIT_CLEAR(nDrawRectType, AI_DRAW_RECT_TYPE_CYCLE);
        }

        //plate
        if (tAiAttr.tHvcfpSetting.tPlate.nEnable) {
            AX_BIT_SET(nTrackType, AI_TRACK_TYPE_PLATE);
        }
        else {
            AX_BIT_CLEAR(nTrackType, AI_TRACK_TYPE_PLATE);
        }
        if (tAiAttr.tHvcfpSetting.tPlate.nDrawRect) {
            AX_BIT_SET(nDrawRectType, AI_DRAW_RECT_TYPE_PLATE);
        }
        else {
            AX_BIT_CLEAR(nDrawRectType, AI_DRAW_RECT_TYPE_PLATE);
        }

        bPlateIdentify = (AX_BOOL)tAiAttr.tHvcfpSetting.nEnablePI;
    }

    tPushStrategy.nPushMode = (AX_U8)tAiAttr.tPushStrgy.eMode;
    tPushStrategy.nInterval = (AX_U32)tAiAttr.tPushStrgy.nInterval;
    tPushStrategy.nPushCounts = (AX_U32)tAiAttr.tPushStrgy.nCount;
    tPushStrategy.bPushSameFrame = (AX_BOOL)tAiAttr.tPushStrgy.nPushSameFrame;

    //update
    DETECTOR_CONFIG_PARAM_T tConf = GetConfig();

    tConf.nTrackType = nTrackType;
    tConf.nDrawRectType = nDrawRectType;
    tConf.bPlateIdentify = bPlateIdentify;
    tConf.tPushStrategy = tPushStrategy;

    SetConfig(&tConf);

    return AX_TRUE;
}
