/**********************************************************************************
 *
 * Copyright (c) 2019-2022 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/
#ifndef _AX_SKEL_STRUCT_H_
#define _AX_SKEL_STRUCT_H_

#include "ax_global_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief pipeline type
typedef enum axSKEL_PPL_E {
    AX_SKEL_PPL_BODY = 1,           /* body detection pipeline */
    AX_SKEL_PPL_POSE,               /* pose detection pipeline */
    AX_SKEL_PPL_FH,                 /* facehuman detection pipeline */
    AX_SKEL_PPL_HVCFP,              /* hvcfp detection pipeline */
    AX_SKEL_PPL_FACE_FEATURE,       /* face feature detection pipeline */
    AX_SKEL_PPL_HVCP,               /* hvcp detection pipeline */
    AX_SKEL_PPL_MAX,
} AX_SKEL_PPL_E;

/// @brief init parameter struct
typedef struct axSKEL_INIT_PARAM_S {
    const AX_CHAR *pStrModelDeploymentPath;
} AX_SKEL_INIT_PARAM_S;

/// @brief rect struct
typedef struct axSKEL_RECT_S {
    AX_F32 fX;
    AX_F32 fY;
    AX_F32 fW;
    AX_F32 fH;
} AX_SKEL_RECT_S;

/// @brief point struct
typedef struct axSKEL_POINT_S {
    AX_F32 fX;
    AX_F32 fY;
} AX_SKEL_POINT_S;

/// @brief point set struct
typedef struct axSKEL_POINT_SET_S {
    const AX_CHAR *pstrObjectCategory;
    AX_SKEL_POINT_S stPoint;
    AX_F32 fConfidence;
} AX_SKEL_POINT_SET_S;

/// @brief meta info struct
typedef struct axSKEL_META_INFO_S {
    AX_CHAR *pstrType;
    AX_CHAR *pstrValue;
} AX_SKEL_META_INFO_S;

/// @brief config item struct
typedef struct axSKEL_CONFIG_ITEM_S {
    AX_CHAR *pstrType;
    AX_VOID *pstrValue;
    AX_U32 nValueSize;
} AX_SKEL_CONFIG_ITEM_S;

/// @brief config struct
typedef struct axSKEL_CONFIG_S {
    AX_U32 nSize;
    AX_SKEL_CONFIG_ITEM_S *pstItems;
} AX_SKEL_CONFIG_S;

/// @brief handle parameter struct
typedef struct axSKEL_HANDLE_PARAM_S {
    AX_SKEL_PPL_E ePPL;
    AX_U32 nWidth;
    AX_U32 nHeight;
    AX_U32 nFrameDepth;
    AX_U32 nFrameCacheDepth;
    AX_SKEL_CONFIG_S stConfig;
} AX_SKEL_HANDLE_PARAM_S;

/// @brief feature item struct
typedef struct axSKEL_FEATURE_ITEM_S {
    AX_CHAR *pstrType;
    AX_VOID *pstrValue;
    AX_U32 nValueSize;
} AX_SKEL_FEATURE_ITEM_S;

/// @brief tract status
typedef enum axSKEL_TRACK_STATUS_E {
    AX_SKEL_TRACK_STATUS_NEW,
    AX_SKEL_TRACK_STATUS_UPDATE,
    AX_SKEL_TRACK_STATUS_DIE,
    AX_SKEL_TRACK_STATUS_SELECT,
    AX_SKEL_TRACK_STATUS_MAX,
} AX_SKEL_TRACK_STATUS_E;

/// @brief crop frame struct
typedef struct axSKEL_CROP_FRAME_S {
    AX_U64 nFrameId;
    AX_U8 *pFrameData;
    AX_U32 nFrameDataSize;
    AX_U32 nFrameWidth;
    AX_U32 nFrameHeight;
} AX_SKEL_CROP_FRAME_S;

/// @brief pose blur config
typedef struct axSKEL_POSE_BLUR_S {
    AX_F32 fPitch;
    AX_F32 fYaw;
    AX_F32 fRoll;
    AX_F32 fBlur; // 0 - 1
} AX_SKEL_POSE_BLUR_S;

/// @brief object binding
typedef struct axSKEL_OBJECT_BINDING_S {
    const AX_CHAR *pstrObjectCategoryBind;
    AX_U64 nTrackId;
} AX_SKEL_OBJECT_BINDING_S;

/// @brief object item struct
typedef struct axSKEL_OBJECT_ITEM_S {
    const AX_CHAR *pstrObjectCategory;
    AX_SKEL_RECT_S stRect;
    AX_U64 nFrameId;
    AX_U64 nTrackId;
    AX_SKEL_TRACK_STATUS_E eTrackState;
    AX_F32 fConfidence;
    AX_SKEL_POSE_BLUR_S stFacePoseBlur;
    AX_BOOL bCropFrame;
    AX_SKEL_CROP_FRAME_S stCropFrame;
    AX_BOOL bPanoraFrame;
    AX_SKEL_CROP_FRAME_S stPanoraFrame;
    AX_U8 nPointSetSize;
    AX_SKEL_POINT_SET_S *pstPointSet;
    AX_U8 nFeatureSize;
    AX_SKEL_FEATURE_ITEM_S *pstFeatureItem;
    AX_U32 nObjectBindSize;
    AX_SKEL_OBJECT_BINDING_S *pstObjectBind;
    AX_U32 nMetaInfoSize;
    AX_SKEL_META_INFO_S *pstMetaInfo;
} AX_SKEL_OBJECT_ITEM_S;

typedef struct axSKEL_FRAME_CACHE_LIST_S {
    AX_U64 nFrameId;
} AX_SKEL_FRAME_CACHE_LIST_S;

/// @brief result struct
typedef struct axSKEL_RESULT_S {
    AX_U64 nFrameId;
    AX_U32 nOriginalWidth;
    AX_U32 nOriginalHeight;
    AX_U32 nObjectSize;
    AX_SKEL_OBJECT_ITEM_S *pstObjectItems;
    AX_U32 nCacheListSize;
    AX_SKEL_FRAME_CACHE_LIST_S *pstCacheList;
    AX_VOID *pUserData;
    AX_VOID *pPrivate;
} AX_SKEL_RESULT_S;

/// @brief frame struct
typedef struct axSKEL_FRAME_S {
    AX_U64 nFrameId;
    AX_VIDEO_FRAME_S stFrame;
    AX_VOID *pUserData;
} AX_SKEL_FRAME_S;

/// @brief ppl config struct
typedef struct axSKEL_PPL_CONFIG_S {
    AX_SKEL_PPL_E ePPL;
    AX_CHAR *pstrPPLConfigKey;
} AX_SKEL_PPL_CONFIG_S;

/// @brief capability struct
typedef struct axSKEL_CAPABILITY_S {
    AX_U32 nPPLConfigSize;
    AX_SKEL_PPL_CONFIG_S *pstPPLConfig;
    AX_U32 nMetaInfoSize;
    AX_SKEL_META_INFO_S *pstMetaInfo;
    AX_VOID *pPrivate;
} AX_SKEL_CAPABILITY_S;

/// @brief version info struct
typedef struct axSKEL_VERSION_INFO_S {
    AX_CHAR *pstrVersion;
    AX_U32 nMetaInfoSize;
    AX_SKEL_META_INFO_S *pstMetaInfo;
    AX_VOID *pPrivate;
} AX_SKEL_VERSION_INFO_S;

/// @brief handle definition
typedef AX_VOID *AX_SKEL_HANDLE;

/// @brief callback definition
typedef AX_VOID (*AX_SKEL_RESULT_CALLBACK_FUNC)(AX_SKEL_HANDLE pHandle, AX_SKEL_RESULT_S *pstResult, AX_VOID *pUserData);

/* begin of config param */
/// @brief Common Threshold Config
typedef struct axSKEL_COMMON_THRESHOLD_CONFIG_S {
    AX_F32 fValue;
} AX_SKEL_COMMON_THRESHOLD_CONFIG_S;
// cmd: "body_max_target_count", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *
// cmd: "vehicle_max_target_count", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *
// cmd: "cycle_max_target_count", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *
// cmd: "body_confidence", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *
// cmd: "face_confidence", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *
// cmd: "vehicle_confidence", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *
// cmd: "cycle_confidence", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *
// cmd: "plate_confidence", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *
// cmd: "crop_encoder_qpLevel", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *
// cmd: "venc_attr_config", value_type: AX_SKEL_COMMON_THRESHOLD_CONFIG_S *

/// @brief Common Enable Config
typedef struct axSKEL_COMMON_ENABLE_CONFIG_S {
    AX_BOOL bEnable;
} AX_SKEL_COMMON_ENABLE_CONFIG_S;
// cmd: "push_bind_enable", value_type: AX_SKEL_COMMON_ENABLE_CONFIG_S *
// cmd: "track_enable", value_type: AX_SKEL_COMMON_ENABLE_CONFIG_S *
// cmd: "push_enable", value_type: AX_SKEL_COMMON_ENABLE_CONFIG_S *
// cmd: "push_attr_always", value_type: AX_SKEL_COMMON_ENABLE_CONFIG_S *

/// @brief object size filter config
typedef struct axSKEL_OBJECT_SIZE_FILTER_CONFIG_S {
    AX_U32 nWidth;
    AX_U32 nHeight;
} AX_SKEL_OBJECT_SIZE_FILTER_CONFIG_S;
// cmd: "body_min_size", value_type: AX_SKEL_OBJECT_SIZE_FILTER_CONFIG_S *
// cmd: "face_min_size", value_type: AX_SKEL_OBJECT_SIZE_FILTER_CONFIG_S *
// cmd: "vehicle_min_size", value_type: AX_SKEL_OBJECT_SIZE_FILTER_CONFIG_S *
// cmd: "cycle_min_size", value_type: AX_SKEL_OBJECT_SIZE_FILTER_CONFIG_S *
// cmd: "plate_min_size", value_type: AX_SKEL_OBJECT_SIZE_FILTER_CONFIG_S *

/// @brief roi config
typedef struct axSKEL_ROI_CONFIG_S {
    AX_BOOL bEnable;
    AX_SKEL_RECT_S stRect;
} AX_SKEL_ROI_CONFIG_S;
// cmd:"detect_roi", value_type: AX_SKEL_ROI_CONFIG_S*

/// @brief roi point config
#define AX_SKEL_ROI_POINT_MAX (10)

typedef struct axSKEL_ROI_POLYGON_CONFIG_S {
    AX_BOOL bEnable;
    AX_U32 nPointNum;
    AX_SKEL_POINT_S *pstPoint;      // points should as clockwise or anti-clockwise
} AX_SKEL_ROI_POLYGON_CONFIG_S;
// cmd:"detect_roi_polygon", value_type: AX_SKEL_ROI_POLYGON_CONFIG_S*

/// @brief push mode
typedef enum axSKEL_PUSH_MODE_E {
    AX_SKEL_PUSH_MODE_FAST = 1,
    AX_SKEL_PUSH_MODE_INTERVAL,
    AX_SKEL_PUSH_MODE_BEST,
    AX_SKEL_PUSH_MODE_MAX
} AX_SKEL_PUSH_MODE_E;

/// @brief push config
typedef struct axSKEL_PUSH_STRATEGY_S {
    AX_SKEL_PUSH_MODE_E ePushMode;
    AX_U32 nIntervalTimes;      // only for AX_SKEL_PUSH_MODE_INTERVAL or AX_SKEL_PUSH_MODE_FAST
    AX_U32 nPushCounts;         // only for AX_SKEL_PUSH_MODE_INTERVAL or AX_SKEL_PUSH_MODE_FAST
    AX_BOOL bPushSameFrame;       // AX_FALSE: push cross frame; AX_TRUE: push same frame
} AX_SKEL_PUSH_STRATEGY_S;
// cmd: "push_strategy", value_type: AX_SKEL_ROI_CONFIG_S *

/// @brief Crop encoder threshold config
typedef struct axSKEL_CROP_ENCODER_THRESHOLD_CONFIG_S {
    AX_F32 fScaleLeft;
    AX_F32 fScaleRight;
    AX_F32 fScaleTop;
    AX_F32 fScaleBottom;
} AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S;
// cmd:"body_crop_encoder", value_type: AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S *
// cmd:"vehicle_crop_encoder", value_type: AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S *
// cmd:"cycle_crop_encoder", value_type: AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S *
// cmd:"face_crop_encoder", value_type: AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S *
// cmd:"plate_crop_encoder", value_type: AX_SKEL_CROP_ENCODER_THRESHOLD_CONFIG_S *

/// @brief Reszie panorama config
typedef struct axSKEL_RESIZE_CONFIG_S {
    AX_F32 fW;
    AX_F32 fH;
} AX_SKEL_RESIZE_CONFIG_S;
// cmd:"resize_panorama_encoder_config",  value_type: AX_SKEL_RESIZE_CONFIG_S *

/// @brief push panorama config
typedef struct axSKEL_PUSH_PANORAMA_CONFIG_S {
    AX_BOOL bEnable;
    AX_U32 nQuality;
} AX_SKEL_PUSH_PANORAMA_CONFIG_S;
// cmd:"push_panorama",  value_type: AX_SKEL_PUSH_PANORAMA_CONFIG_S *

/// @brief face attr filter config
typedef struct axSKEL_FACE_ATTR_FILTER_CONFIG_S {
    AX_U32 nWidth;
    AX_U32 nHeight;
    AX_SKEL_POSE_BLUR_S stPoseblur;
} AX_SKEL_FACE_ATTR_FILTER_CONFIG_S;

/// @brief common attr filter config
typedef struct axSKEL_COMMON_ATTR_FILTER_CONFIG_S {
    AX_F32 fQuality; // 0 - 1
} AX_SKEL_COMMON_ATTR_FILTER_CONFIG_S;

/// @brief attr filter config
typedef struct axSKEL_ATTR_FILTER_CONFIG_S {
    union {
        AX_SKEL_FACE_ATTR_FILTER_CONFIG_S stFaceAttrFilterConfig;      // face
        AX_SKEL_COMMON_ATTR_FILTER_CONFIG_S stCommonAttrFilterConfig;  // body,vehicle,cycle,plate
    };
} AX_SKEL_ATTR_FILTER_CONFIG_S;
// cmd:"push_quality_face", value_type: AX_SKEL_ATTR_FILTER_CONFIG_S *
// cmd:"push_quality_body", value_type: AX_SKEL_ATTR_FILTER_CONFIG_S *
// cmd:"push_quality_vehicle", value_type: AX_SKEL_ATTR_FILTER_CONFIG_S *
// cmd:"push_quality_cycle", value_type: AX_SKEL_ATTR_FILTER_CONFIG_S *
// cmd:"push_quality_plate", value_type: AX_SKEL_ATTR_FILTER_CONFIG_S *

/// @brief config target item
typedef struct axSKEL_TARGET_ITEMG_S {
    const AX_CHAR *pstrObjectCategory;
} AX_SKEL_TARGET_ITEM_S;

/// @brief config object target
typedef struct axSKEL_TARGET_CONFIG_S {
    AX_U32 nSize;
    AX_SKEL_TARGET_ITEM_S *pstItems;
} AX_SKEL_TARGET_CONFIG_S;
// cmd:"target_config", value_type: AX_SKEL_TARGET_CONFIG_S *
// cmd:"push_target_config", value_type: AX_SKEL_TARGET_CONFIG_S *

typedef enum axSKEL_ANALYZER_ATTR_E {
    AX_SKEL_ANALYZER_ATTR_NONE = 0,
    AX_SKEL_ANALYZER_ATTR_FACE_FEATURE,
    AX_SKEL_ANALYZER_ATTR_FACE_ATTRIBUTE,
    AX_SKEL_ANALYZER_ATTR_PLATE_ATTRIBUTE,
    AX_SKEL_ANALYZER_ATTR_MAX,
} AX_SKEL_ANALYZER_ATTR_E;

/// @brief config target attribute analyzer
typedef struct axSKEL_ANALYZER_CONFIG_S {
    AX_U32 nSize;
    AX_SKEL_ANALYZER_ATTR_E *peItems;
} AX_SKEL_ANALYZER_CONFIG_S;
// cmd:"analyzer_attr_config", value_type: AX_SKEL_ATTR_FILTER_CONFIG_S *

/* end of config param */

/* begin of search */

/// @brief search: group feature param
typedef struct axSKEL_SEARCH_FEATURE_PARAM_S {
    AX_U32 nBatchSize;
    AX_U64* pObjectIds;    // 1 dimensional[batch_size] continuous storage
    AX_U8* pFeatures;       // 1 dimensional[batch_size*feature_size] continuous storage
    AX_VOID **ppObjectInfos;    // 1 dimensional[batch_size] continuous storage // 'object_infos' can be null
} AX_SKEL_SEARCH_FEATURE_PARAM_S;

/// @brief search: group insert feature param
typedef struct axSKEL_SEARCH_PARAM_S {
    AX_U32 nBatchSize;
    AX_U32 nTop_k;
    AX_U8 *pFeatures;    // 1 dimensional[batch_size*feature_size] continuous storage
    // If the following types are valid, the filter policy will be activated. If not, the filter
    // policy will not be performed
} AX_SKEL_SEARCH_PARAM_S;

/// @brief search: result of search
typedef struct axSKEL_SEARCH_RESULT_S {
    AX_U32 nBatchSize;      // object number of search
    AX_U32 nTop_k;           // object with k highest scores in object group
    AX_F32 *pfScores;          // scorces of object matching in object group
    AX_U64 *pObjectIds;    // object id of object matching in object group
    AX_VOID **ppObjectInfos;            // information of object matching in object group(pass-through by user)
    AX_VOID *pPrivate;
} AX_SKEL_SEARCH_RESULT_S;

/* end of search */

#ifdef __cplusplus
}
#endif

#endif /* _AX_SKEL_STRUCT_H_ */
