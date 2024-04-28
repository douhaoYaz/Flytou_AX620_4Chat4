/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/

#include "OptionHelper.h"
#include "StageOptionHelper.h"
#include "StringUtils.h"
#include <sstream>

#define DEFAULT_WEB_FRAME_SIZE_RATIO (0.125)

COptionHelper::COptionHelper(void)
    : m_nLogTarget(E_LOG_TARGET_STDERR)
    , m_nLogLevel(E_LOG_LV_ERROR)
    , m_nRTSPMaxFrmSize(700 * 1024)
    , m_nWebFrmSizeRatio(DEFAULT_WEB_FRAME_SIZE_RATIO)
    , m_bActiveDetect(AX_FALSE)
    , m_bActiveDetectFromWeb(AX_FALSE)
    , m_bActiveTrack(AX_TRUE)
    , m_bActiveSearch(AX_FALSE)
    , m_bActiveSearchFromWeb(AX_FALSE)
    , m_bActiveOD(AX_FALSE)
    , m_bActiveMD(AX_FALSE)
    , m_bNrEnable(AX_TRUE)
    , m_bPrintFPS(AX_FALSE)
    , m_bUseWebDeamon(AX_TRUE)
    , m_bMp4Record(AX_FALSE)
    , m_bActiveMp4Record(AX_TRUE)
    , m_bLoopCoverMp4Record(AX_TRUE)
    , m_bEnableDewarp(AX_TRUE)
    , m_bEnableOSD(AX_FALSE)
    , m_nSensorID(0)
    , m_bLinkMode(AX_TRUE)
    , m_nMp4MaxFileNum(0)
    , m_nMp4MaxFileSize(0)
    , m_nMp4TotalSpace(0)
    , m_bEnableAutoSleep(AX_FALSE)
    , m_nAutoSleepFrameNum(0)
    , m_nVenc0SeqNum(0)
    , m_nEnableEIS(AX_FALSE)
    , m_nEISDelayNum(4)
    , m_nEISCropW(25)
    , m_nEISCropH(25)
    , m_nEnableEISEffectComp(AX_FALSE)
    , m_nEISSupport(AX_FALSE)
    , m_eRotation(AX_IVPS_ROTATION_0)
    , m_bMirror(AX_FALSE)
    , m_bFlip(AX_FALSE)
    , m_bHotBalanceTest(AX_FALSE)
    , m_bSnsHotNoiseBalanceTest(AX_FALSE)
    , m_bNoneBiasSupport(AX_FALSE)
    , m_bEnableNoneBias(AX_FALSE)
    , m_strJsonCfgFile("")
    , m_strSnsName("")
{
}

COptionHelper::~COptionHelper(void)
{
}

AX_BOOL COptionHelper::ParseArgs(AX_S32 argc, AX_CHAR const *argv[])
{
    /*
        argv[0] : System
        argv[1] : Camera config json file path
        argv[2] : Log target, [0 - 3]
        argv[3] : Application log level, [0 - 4]
        argv[4] : RTSP Max Frame Size
        argv[5] : Web Frame Size Ratio
        argv[6] : Active detection, 1: detect  0: not detect
        argv[7] : Show FPS printing
        argv[8] : Use Web Deamon thread
        argv[9] : Mp4 Recoder, [0: off; 1: on;]
        argv[10] : Mp4 Saved Path
        argv[11] : Mp4 Max File Number
        argv[12] : Mp4 Max File Size(MB)
        argv[13] : Active Link Mode, 1: Active 0: Not Active
        argv[14] : Dewarp, 1: Enable 0: Disable
        argv[15] : Enable group overlay
        argv[16] : DetectConfigPath
        argv[17] : Active occlusion detection (OD)
        argv[18] : Active motion detection (MD)
        argv[19] : enable Auto Sleep
        argv[20] : Auto Sleep Frame Number
        argv[21] : Enable EIS [0: off; 1: on]
        argv[22] : EIS delay frame number
        argv[23] : EIS crop width
        argv[24] : EIS crop height
        argv[25] : Enable EIS Effect Comparison [0: off; 1: on]
        argv[26] : Rotation [0: 0; 1: 90; 2: 180; 3: 270]
        argv[27] : Mirror
        argv[28] : Flip
        argv[29] : HotBalance Test
        argv[30] : Sensor HotNoiseBalance Test
        argv[31] : LENS Drive config path
    */
    #define MIN_ARG_COUNT         (32)

    if (argc != MIN_ARG_COUNT) {
        printf("[ERROR] Incorrect number of input args: %d!", argc);
        return AX_FALSE;
    }

    /* Json config file path */
    m_strJsonCfgFile = (char *)argv[1];
    static const std::pair<AX_U32, std::string> s_pairSensors[] = {
        std::make_pair(E_SNS_TYPE_OS04A10, "os04a10"),
        std::make_pair(E_SNS_TYPE_IMX334, "imx334"),
        std::make_pair(E_SNS_TYPE_GC4653, "gc4653"),
        std::make_pair(E_SNS_TYPE_OS08A20, "os08a20"),
        std::make_pair(E_SNS_TYPE_SC1345, "sc1345"),
        std::make_pair(E_SNS_TYPE_SC530AI, "sc530ai"),
        std::make_pair(E_SNS_TYPE_SC230AI, "sc230ai"),
        std::make_pair(E_SNS_TYPE_IMX327, "imx327"),
        std::make_pair(E_SNS_TYPE_IMX464, "imx464"),
        std::make_pair(E_SNS_TYPE_GC5603, "gc5603"),
        std::make_pair(E_SNS_TYPE_IMX415, "imx415"),
        std::make_pair(E_SNS_TYPE_IMX415_5M, "imx415_5M"),
    };

    if (!m_strJsonCfgFile.empty()) {
        AX_BOOL bFound = AX_FALSE;
        for (const auto& item : s_pairSensors) {
            if (string::npos != m_strJsonCfgFile.find(item.second)) {
                m_nSensorID = item.first;
                m_strSnsName = item.second;
                bFound = AX_TRUE;
                break;
            }
        }
        if (!bFound) {
            printf("[ERROR] Unkonown json configure file: %s!", m_strJsonCfgFile.c_str());
            return AX_FALSE;
        }
    }

    /* Log target */
    m_nLogTarget = atoi((char *)argv[2]);

    /* Application log level (path:/var/log/ipcdemo.log) */
    m_nLogLevel = atoi((char *)argv[3]);

    /* RTSP Max Frame Size */
    m_nRTSPMaxFrmSize = atoi((char *)argv[4]);

    /* Web Frame Size Ratio */
    m_nWebFrmSizeRatio = atof((char *)argv[5]);
    if (m_nWebFrmSizeRatio <= 0) {
        printf("[Warning] m_nWebFrmSizeRatio(%f) is invalid, reset to default value: %f\n",
                                         m_nWebFrmSizeRatio, DEFAULT_WEB_FRAME_SIZE_RATIO);
        m_nWebFrmSizeRatio = DEFAULT_WEB_FRAME_SIZE_RATIO;
    } else if (m_nWebFrmSizeRatio > 1) {
        printf("[Warning] m_nWebFrmSizeRatio(%f) is invalid, reset to value: 1.0\n", m_nWebFrmSizeRatio);
        m_nWebFrmSizeRatio = 1.0;
    }

    /* NPU detection */
    m_bActiveDetect = ((1 == atoi((char *)argv[6])) ? AX_TRUE : AX_FALSE);
    m_bActiveDetectFromWeb = m_bActiveDetect;

    m_bPrintFPS = (1 == atoi((char *)argv[7])) ? AX_TRUE : AX_FALSE;
    m_bUseWebDeamon = (1 == atoi((char *)argv[8])) ? AX_TRUE : AX_FALSE;

    /* Mp4 recoder */
    m_bMp4Record = ((1 == atoi((char *)argv[9])) ? AX_TRUE : AX_FALSE);

    /* Mp4 saved path*/
    m_strMp4SavedPath = (char *)argv[10];

    /* Mp4 Max File Number */
    m_nMp4MaxFileNum = atoi((char *)argv[11]);

    /* Mp4 Max File Size */
    m_nMp4MaxFileSize = atoi((char *)argv[12]);

    /* Active Link Mode */
    m_bLinkMode = ((1 == atoi((char *)argv[13])) ? AX_TRUE : AX_FALSE);

    /* Dewarp */
    m_bEnableDewarp = ((1 == atoi((char *)argv[14])) ? AX_TRUE : AX_FALSE);

    /* OSD */
    m_bEnableOSD = ((1 == atoi((char *)argv[15])) ? AX_TRUE : AX_FALSE);

    /* Detection config path */
    m_strDetectConfigPath = (char *)argv[16];

    /* occlusion detection */
    if (m_bActiveDetect) {
        m_bActiveOD = ((1 == atoi((char *)argv[17])) ? AX_TRUE : AX_FALSE);
    } else {
        m_bActiveOD = AX_FALSE;
    }

    /* motion detection */
    if (m_bActiveDetect) {
        m_bActiveMD = ((1 == atoi((char *)argv[18])) ? AX_TRUE : AX_FALSE);
    } else {
        m_bActiveMD = AX_FALSE;
    }

    /* Auto Sleep feature */
    m_bEnableAutoSleep = ((1 == atoi((char *)argv[19])) ? AX_TRUE : AX_FALSE);
    m_nAutoSleepFrameNum = atoll((char *)argv[20]);

    /* Enable EIS */
    m_nEnableEIS = ((1 == atoi((char *)argv[21])) ? AX_TRUE : AX_FALSE);

    /* EIS delay frame number */
    m_nEISDelayNum = atoi((char *)argv[22]);

    /* EIS crop width */
    m_nEISCropW = atoi((char *)argv[23]);

    /* EIS crop height */
    m_nEISCropH = atoi((char *)argv[24]);

    /* Enable EIS Effect Comparison */
    m_nEnableEISEffectComp = ((1 == atoi((char *)argv[25])) ? AX_TRUE : AX_FALSE);

    /* Rotation */
    m_eRotation = (AX_IVPS_ROTATION_E)atoi((char *)argv[26]);
    if (m_eRotation >= AX_IVPS_ROTATION_BUTT) {
        m_eRotation = AX_IVPS_ROTATION_0;
    }

    /* Mirror */
    m_bMirror = ((1 == atoi((char *)argv[27])) ? AX_TRUE : AX_FALSE);

    /* Flip */
    m_bFlip = ((1 == atoi((char *)argv[28])) ? AX_TRUE : AX_FALSE);

    /* HotBalanceTest */
    m_bHotBalanceTest = ((1 == atoi((char *)argv[29])) ? AX_TRUE : AX_FALSE);

    /* Sensor HotNoiseBalanceTest */
    m_bSnsHotNoiseBalanceTest = ((1 == atoi((char *)argv[30])) ? AX_TRUE : AX_FALSE);

    /* Lens Af config file */
    std::string strLensDrivePath = (char *)argv[31];
    if (0 == m_ini.Load(strLensDrivePath))
    {
        std::string mkey = "lens";
        std::string stringValue;
        if (m_ini.HasSection(mkey))
        {
            if (0 == m_ini.GetStringValue(mkey, "nLensDCIrisLibName", &stringValue))
                m_stLensDriveCfg.strLensDCIrisLibName = stringValue;
            if (0 == m_ini.GetStringValue(mkey, "nLensDCIrisObjName", &stringValue))
                m_stLensDriveCfg.strLensDCIrisObjName = stringValue;
            if (0 == m_ini.GetStringValue(mkey, "nLensIrisLibName", &stringValue))
                m_stLensDriveCfg.strLensIrisLibName = stringValue;
            if (0 == m_ini.GetStringValue(mkey, "nLensIrisObjName", &stringValue))
                m_stLensDriveCfg.strLensIrisObjName = stringValue;
        }
    }

    /* TTF file */
    m_strTtfFile = "./res/GB2312.ttf";

    const char *szTtfFile = getenv("AX_IPCD_OSD_FONT_FILE");
    if (nullptr != szTtfFile) {
        m_strTtfFile = szTtfFile;
    }

    return AX_TRUE;
}

AX_U32 COptionHelper::GetLogTarget(void)const
{
    return m_nLogTarget;
}

AX_U32 COptionHelper::GetLogLevel(void)const
{
    return m_nLogLevel;
}

AX_U32 COptionHelper::GetRTSPMaxFrmSize(void)const
{
    return m_nRTSPMaxFrmSize;
}

AX_F32 COptionHelper::GetWebFrmSizeRatio(void)const
{
    return m_nWebFrmSizeRatio;
}

AX_VOID COptionHelper::GetMp4FileInfo(AX_U32 & nMaxFileSize, AX_U32 & nMaxFileNum) const
{
    nMaxFileSize = m_nMp4MaxFileSize;
    nMaxFileNum  = m_nMp4MaxFileNum;
}

AX_VOID COptionHelper::SetMp4FileInfo(AX_U32 nMaxFileSize, AX_U32 nMaxFileNum)
{
    m_nMp4MaxFileSize = nMaxFileSize;
    m_nMp4MaxFileNum  = nMaxFileNum;
}

AX_U64 COptionHelper::GetMp4TotalSpace(void)const
{
    return m_nMp4TotalSpace;
}

AX_VOID COptionHelper::SetMp4TotalSpace(AX_U64 nTotalSpace)
{
    m_nMp4TotalSpace = nTotalSpace;
}

AX_BOOL COptionHelper::IsActivedDetect(void) const
{
    return m_bActiveDetect;
}

AX_BOOL COptionHelper::IsActivedDetectFromWeb(void) const
{
    return m_bActiveDetectFromWeb;
}

AX_BOOL COptionHelper::IsActivedTrack(void) const
{
    if (IsActivedDetect() && IsActivedDetectFromWeb()) {
        return m_bActiveTrack;
    }

    return AX_FALSE;
}

AX_BOOL COptionHelper::IsActivedSearch(void) const
{
    if (IsActivedDetect() && IsActivedDetectFromWeb()) {
        return m_bActiveSearch;
    }

    return AX_FALSE;
}

AX_BOOL COptionHelper::IsActivedSearchFromWeb(void) const
{
    if (IsActivedSearch()) {
        return m_bActiveSearchFromWeb;
    }

    return AX_FALSE;
}

AX_BOOL COptionHelper::IsEnableDewarp(void)const
{
    return m_bEnableDewarp;
}

AX_BOOL COptionHelper::IsEnableAutoSleep(void) const {
    return m_bEnableAutoSleep;
}

AX_U64 COptionHelper::GetAutoSleepFrameNum(void) const {
    return m_nAutoSleepFrameNum;
}

AX_U64 COptionHelper::GetVenc0SeqNum() const {
    return m_nVenc0SeqNum;
}

void COptionHelper::SetVenc0SeqNum(AX_U64 nSeqNum) {
    m_nVenc0SeqNum = nSeqNum;
}

AX_BOOL COptionHelper::IsEnableOSD(void)const
{
    return m_bEnableOSD;
}

AX_BOOL COptionHelper::IsLinkMode(void)const
{
    return m_nEISSupport ? AX_FALSE : m_bLinkMode; //TODO: linkmode is not support EIS currently
}

const std::string &COptionHelper::GetJsonFile(void)const
{
    return m_strJsonCfgFile;
}

const std::string &COptionHelper::GetMp4SavedPath(void)const
{
    return m_strMp4SavedPath;
}

AX_VOID COptionHelper::SetMp4SavedPath(AX_CHAR* pszPath)
{
    m_strMp4SavedPath = pszPath;
}

AX_U32 COptionHelper::GetSensorID()const
{
    return m_nSensorID;
}

std::string COptionHelper::GetSensorName()const
{
    return m_strSnsName;
}

AX_U32 COptionHelper::GetSnsNum()const
{
    return 1;
}

AX_BOOL COptionHelper::IsPrintFPS(void) const
{
    return m_bPrintFPS;
}

AX_BOOL COptionHelper::IsEnableMp4Record(void) const
{
    return m_bMp4Record;
}

AX_BOOL COptionHelper::IsActiveMp4Record() const
{
    return m_bActiveMp4Record;
}

AX_BOOL COptionHelper::IsLoopCoverMp4Record() const
{
    return m_bLoopCoverMp4Record;
}

AX_VOID COptionHelper::SetMp4RecordActived(AX_BOOL bActive)
{
    m_bActiveMp4Record = bActive;
}

AX_VOID COptionHelper::SetLoopCoverMp4Record(AX_BOOL bLoopCover)
{
    m_bLoopCoverMp4Record = bLoopCover;
}

AX_VOID COptionHelper::SetDetectActived(AX_BOOL bActive)
{
    m_bActiveDetectFromWeb = bActive;

    CStageOptionHelper *pStageOption = CStageOptionHelper().GetInstance();
    AI_ATTR_T tAiAttr = pStageOption->GetAiAttr();

    tAiAttr.nEnable = bActive;

    pStageOption->SetAiAttr(tAiAttr);
}

AX_VOID COptionHelper::SetTrackActived(AX_BOOL bActive)
{
    m_bActiveTrack = bActive;
}

AX_VOID COptionHelper::SetSearchActived(AX_BOOL bActive)
{
    m_bActiveSearch = bActive;
    m_bActiveSearchFromWeb = bActive;
}

AX_VOID COptionHelper::SetNrEnabled(AX_BOOL bEnable)
{
    // AX_ISP_NPU_PARAM_T tIspNpuParam = {0};

    // if (m_bNrEnable != bEnable) {
    //     AX_S32 nRet = AX_ISP_GetNpuParam(0, &tIspNpuParam);
    //     if (AX_SUCCESS != nRet) {
    //         LOG_E("AX_ISP_GetNpuParam failed, pipe=%d, enable=%d, ret=0x%x.", 0, bEnable, nRet);
    //     }

    //     tIspNpuParam.bNrEnable = bEnable;
    //     nRet = AX_ISP_SetNpuParam(0, &tIspNpuParam);
    //     if (AX_SUCCESS != nRet) {
    //         LOG_E("AX_ISP_SetNpuParam failed, pipe=%d, enable=%d, ret=0x%x.", 0, bEnable, nRet);
    //     }
    //     else {
    //         m_bNrEnable = bEnable;
    //     }
    // }
}

AX_VOID COptionHelper::SetDetectResult(AX_U32 nPipeID, AI_Detection_Result_t *pResult)
{
    std::lock_guard<std::mutex> lck(m_mtxOption);
    if (pResult) {
        auto BodyAttrCopy = [&](_AI_Body_Attr_t &dst_attr, _AI_Body_Attr_t src_attr) {
            dst_attr.bExist = src_attr.bExist;
            dst_attr.strSafetyCap = src_attr.strSafetyCap;
            dst_attr.strHairLength = src_attr.strHairLength;
            };
        auto VehicleAttrCopy = [&](AI_Vehicle_Attr_t &dst_attr, AI_Vehicle_Attr_t src_attr) {
            dst_attr.bExist = src_attr.bExist;
            dst_attr.strVehicleColor = src_attr.strVehicleColor;
            dst_attr.strVehicleSubclass = src_attr.strVehicleSubclass;
            };
        auto CycleAttrCopy = [&](AI_Cycle_Attr_t &dst_attr, AI_Cycle_Attr_t src_attr) {
            dst_attr.bExist = src_attr.bExist;
            dst_attr.strCycleSubclass = src_attr.strCycleSubclass;
            };
        auto FaceAttrCopy = [&](AI_Face_Attr_t &dst_attr, AI_Face_Attr_t src_attr) {
            dst_attr.bExist = src_attr.bExist;
            dst_attr.nAge = src_attr.nAge;
            dst_attr.nGender = src_attr.nGender;
            dst_attr.strRespirator = src_attr.strRespirator;
            };
        auto PlateAttrCopy = [&](AI_Plat_Attr_t &dst_attr, AI_Plat_Attr_t src_attr) {
            dst_attr.bExist = src_attr.bExist;
            dst_attr.bValid = src_attr.bValid;
            dst_attr.strPlateColor = src_attr.strPlateColor;
            dst_attr.strPlateType = src_attr.strPlateType;
            dst_attr.strPlateCode = src_attr.strPlateCode;
            };

        #define ObjectCopy(Obj) \
            do { \
                m_arrDetectResult[nPipeID].n##Obj##Size = AX_MIN(pResult->n##Obj##Size, MAX_DECT_BOX_COUNT); \
                if (pResult->p##Obj##s && m_arrDetectResult[nPipeID].n##Obj##Size > 0) { \
                    for (AX_U8 i = 0; i < m_arrDetectResult[nPipeID].n##Obj##Size; i++) { \
                        memcpy(&m_arrDetectResult[nPipeID].t##Obj##s[i].tBox, &pResult->p##Obj##s[i].tBox, sizeof(pResult->p##Obj##s[i].tBox)); \
                        m_arrDetectResult[nPipeID].t##Obj##s[i].u64TrackId = pResult->p##Obj##s[i].u64TrackId; \
                        m_arrDetectResult[nPipeID].t##Obj##s[i].fConfidence = pResult->p##Obj##s[i].fConfidence; \
                        m_arrDetectResult[nPipeID].t##Obj##s[i].eTrackState = pResult->p##Obj##s[i].eTrackState; \
                        Obj##AttrCopy(m_arrDetectResult[nPipeID].t##Obj##s[i].t##Obj##Attr, pResult->p##Obj##s[i].t##Obj##Attr); \
                    } \
                } \
            } while(0)

        #define ObjectPoseCopy(Obj) \
            do { \
                m_arrDetectResult[nPipeID].n##Obj##Size = AX_MIN(pResult->n##Obj##Size, MAX_DECT_BOX_COUNT); \
                if (pResult->p##Obj##s && m_arrDetectResult[nPipeID].n##Obj##Size > 0) { \
                    for (AX_U8 i = 0; i < m_arrDetectResult[nPipeID].n##Obj##Size; i++) { \
                        m_arrDetectResult[nPipeID].t##Obj##s[i].nPointNum = AX_MIN(pResult->p##Obj##s[i].nPointNum, DETECT_POSE_POINT_COUNT); \
                        if (m_arrDetectResult[nPipeID].t##Obj##s[i].nPointNum > 0) { \
                            memcpy(&m_arrDetectResult[nPipeID].t##Obj##s[i].tPoint[0], &pResult->p##Obj##s[i].tPoint[0], sizeof(pResult->p##Obj##s[i].tPoint)); \
                        } \
                    } \
                } \
            } while(0)

        m_arrDetectResult[nPipeID].nFrameId = pResult->nFrameId;

        ObjectCopy(Face);
        ObjectCopy(Body);
        ObjectCopy(Vehicle);
        ObjectCopy(Plate);
        ObjectCopy(Cycle);
        ObjectPoseCopy(Pose);
    }
    else {
        m_arrDetectResult[nPipeID].Clear();
    }
}

DETECT_RESULT_T COptionHelper::GetDetectResult(AX_U32 nPipeID)
{
    std::lock_guard<std::mutex> lck(m_mtxOption);
    return m_arrDetectResult[nPipeID];
}

const std::string &COptionHelper::GetDetectionConfigPath(void) const
{
    return m_strDetectConfigPath;
}

AX_BOOL COptionHelper::IsActivedOcclusionDetect(void) const
{
    if (IsActivedDetect() && IsActivedDetectFromWeb()) {
        return m_bActiveOD;
    }

    return AX_FALSE;
}

AX_BOOL COptionHelper::IsActivedMotionDetect(void) const
{
    if (IsActivedDetect() && IsActivedDetectFromWeb()) {
        return m_bActiveMD;
    }

    return AX_FALSE;
}

AX_VOID COptionHelper::ActiveOcclusionDetect(AX_BOOL bActive)
{
    m_bActiveOD = bActive;
}

AX_VOID COptionHelper::ActiveMotionDetect(AX_BOOL bActive)
{
    m_bActiveMD = bActive;
}

AX_BOOL COptionHelper::IsEnableEIS(void) const
{
    return m_nEnableEIS;
}

AX_VOID COptionHelper::SetEISEnable(AX_BOOL bEnable)
{
    m_nEnableEIS = bEnable;
}

AX_U32 COptionHelper::GetEISDelayNum(void) const
{
    return m_nEISDelayNum;
}

AX_U32 COptionHelper::GetEISCropW(void) const
{
    return m_nEISCropW;
}

AX_U32 COptionHelper::GetEISCropH(void) const
{
    return m_nEISCropH;
}

AX_BOOL COptionHelper::IsEnableEISEffectComp(void) const
{
    return m_nEnableEIS ? m_nEnableEISEffectComp : AX_FALSE;
}

AX_BOOL COptionHelper::IsEISSupport(void) const
{
#ifdef AX_SIMPLIFIED_MEM_VER
    return AX_FALSE; // Low mem ver does not support EIS
#else
    return m_nEISSupport;
#endif
}

AX_VOID COptionHelper::SetEISSupport(AX_BOOL bSupport)
{
    m_nEISSupport = bSupport;
    if (!m_nEISSupport && m_nEnableEIS) {
        LOG_M_E("OptionHelper", "Do not support EIS, reset m_nEnableEIS to AX_FALSE!");
        m_nEnableEIS = AX_FALSE;
    }
}

AX_IVPS_ROTATION_E COptionHelper::GetRotation() const
{
    return m_eRotation;
}

AX_BOOL COptionHelper::GetMirror() const
{
    return m_bMirror;
}

AX_BOOL COptionHelper::GetFlip() const
{
    return m_bFlip;
}

AX_BOOL COptionHelper::GetHotBalanceTest() const
{
    return m_bHotBalanceTest;
}

AX_BOOL COptionHelper::GetSnsHotNoiseBalanceTest() const
{
    return m_bSnsHotNoiseBalanceTest;
}

const std::string &COptionHelper::GetTtfPath(void) const
{
    return m_strTtfFile;
}

AX_VOID COptionHelper::ActiveSceneChangeDetect(AX_BOOL bActive) {
    m_bActiveSceneChangeDetect = bActive;
}

AX_BOOL COptionHelper::IsActivedSceneChangeDetect(void) const {
    return m_bActiveSceneChangeDetect;
}

AX_LENS_DRIVE_T COptionHelper::GetLensCfg() const
{
    return m_stLensDriveCfg;
}

AX_BOOL COptionHelper::IsLoadNoneBiasBinSupport(void) const
{
    return m_bNoneBiasSupport;
}

AX_VOID COptionHelper::SetLoadNoneBiasBinSupport(AX_BOOL bSupport)
{
    m_bNoneBiasSupport = bSupport;
}

AX_BOOL COptionHelper::IsLoadNoneBiasBinEnable(void) const
{
    return m_bNoneBiasSupport ? m_bEnableNoneBias : AX_FALSE;
}

AX_VOID COptionHelper::SetLoadNoneBiasBinEnable(AX_BOOL bEnable)
{
    m_bEnableNoneBias = bEnable;
}
