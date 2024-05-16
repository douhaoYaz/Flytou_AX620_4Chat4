/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/

#include "Camera.h"
#include "Capture.h"
#include "IVPSStage.h"
#include "MediaFrame.h"
#include "OptionHelper.h"
#include "PrintHelper.h"
#include "ConfigParser.h"
#include "StageOptionHelper.h"
#include "OSDHandler.h"
#include "CommonUtils.h"
#include "MemMgr.h"
#include "unicode.h"
#include <thread>
#include <opencv2/opencv.hpp>

#define IVPS "IVPS"

#define MAX_IPC_IVPS_FRAME_TIMEOUT  (1000)
#define MAX_IMAGE_WIDTH             (3840)
#define MAX_IMAGE_HEIGHT            (3840)
#define MAX_OSD_TIME_CHAR_LEN       (32)
#define MAX_OSD_STRING_CHAR_LEN     (128)
#define BASE_FONT_SIZE              (16)
#define ROTATION_WIDTH_ALIGEMENT    (8)


extern COptionHelper gOptions;
extern CPrintHelper  gPrintHelper;
extern END_POINT_OPTIONS g_tEPOptions[MAX_VENC_CHANNEL_NUM];

vector<CVideoEncoder*>* CIVPSStage::m_pVecEncoders = nullptr;
vector<CJpgEncoder*>* CIVPSStage::m_pVecJecEncoders = nullptr;
CDetectStage* CIVPSStage::m_pDetectStage = nullptr;
IVPS_GRP_T CIVPSStage::m_arrIvpsGrp[IVPS_GROUP_NUM];
extern AX_BOOL g_isSleeped;

// added by Yang
AX_BOOL g_bOpenCVTrack = AX_FALSE;
cv::Rect bbox = {160, 90, 320, 180};
FDSSTTracker *tracker = nullptr;


IVPS_GROUP_CFG_T g_tIvpsGroupConfig[IVPS_GROUP_NUM] = {
    {1, AX_IVPS_ENGINE_BUTT, {AX_IVPS_ENGINE_TDP, AX_IVPS_ENGINE_TDP, AX_IVPS_ENGINE_TDP}, {{-1, -1}, {-1, 12}, {-1, 1}}, {{-1, -1, 64}, {-1, -1, 64}, {-1, -1, 64}},   {1, 1, 1}},
    {1, AX_IVPS_ENGINE_BUTT, {AX_IVPS_ENGINE_GDC, AX_IVPS_ENGINE_TDP, AX_IVPS_ENGINE_TDP}, {{-1, 15}, {-1, 12}, {-1, -1}}, {{720, 576, 64}, {-1, -1, 64}, {-1, -1, 64}},   {0, 1, 1}},
    {1, AX_IVPS_ENGINE_BUTT, {AX_IVPS_ENGINE_TDP, AX_IVPS_ENGINE_TDP, AX_IVPS_ENGINE_TDP}, {{-1, -1}, {-1, 12}, {-1, -1}}, {{-1, -1, 64}, {-1, -1, 64}, {-1, -1, 64}}, {1, 1, 1}},
};

AX_VOID IVPSConfigDetectModeRelated(CDetectStage *pStage)
{
    if (gOptions.IsActivedDetect()) {
        //VENC0 display using detection result(should check Definition:VENC_USE_DETECTION in VideoEncoder.cpp)
        g_tIvpsGroupConfig[0].arrLinkModeFlag[0] = ((AX_AI_VENC0_USING_DETECTION == 0) ? 1 : 0);

        //VENC1 display using detection result(should check Definition:VENC_USE_DETECTION in VideoEncoder.cpp)
        g_tIvpsGroupConfig[2].arrLinkModeFlag[0] = ((AX_AI_VENC1_USING_DETECTION == 0) ? 1 : 0);

        //JENC use to detect and Track (channel: 1)
        g_tIvpsGroupConfig[1].arrLinkModeFlag[0] = 0;

        // Change to GDC engine
        g_tIvpsGroupConfig[1].arrChnEngineType[0] = AX_IVPS_ENGINE_GDC;

        CStageOptionHelper *pStageOption = CStageOptionHelper().GetInstance();

        //Change framerate for detection
        g_tIvpsGroupConfig[1].arrFramerate[0][1] = pStageOption->GetAiAttr().tConfig.nDetectFps;

        //Change resolution for detection
        g_tIvpsGroupConfig[1].arrOutResolution[0][0] = pStageOption->GetAiAttr().tConfig.nWidth;
        g_tIvpsGroupConfig[1].arrOutResolution[0][1] = pStageOption->GetAiAttr().tConfig.nHeight;
        g_tIvpsGroupConfig[1].arrOutResolution[0][2] = 64;

        VIDEO_ATTR_T videoAttr = pStageOption->GetVideo(DETECTOR_IVPS_CHANNEL_NO);
        videoAttr.width  = g_tIvpsGroupConfig[1].arrOutResolution[0][0];
        videoAttr.height = g_tIvpsGroupConfig[1].arrOutResolution[0][1];
        pStageOption->SetVideo(DETECTOR_IVPS_CHANNEL_NO, videoAttr);

        LOG_M(IVPS, "AI Config: Resolution(%d x %d), fps(IVPS: %d, AI:%d, IVES: %d)",
                    pStageOption->GetAiAttr().tConfig.nWidth, pStageOption->GetAiAttr().tConfig.nHeight,
                    pStageOption->GetAiAttr().tConfig.nDetectFps, pStageOption->GetAiAttr().tConfig.nAiFps,
                    pStageOption->GetAiAttr().tConfig.nIvesFps);
    }

    if (pStage) {
        /* fixme: GROUP 1 is detect stage, hard code here */
        DETECT_STAGE_INFO_T tInfo;
        tInfo.nFrmWidth  = g_tIvpsGroupConfig[1].arrOutResolution[0][0];
        tInfo.nFrmHeight = g_tIvpsGroupConfig[1].arrOutResolution[0][1];
        tInfo.nVinFps    = g_tIvpsGroupConfig[1].arrFramerate[0][0];
        tInfo.nFrmFps    = g_tIvpsGroupConfig[1].arrFramerate[0][1];
        pStage->ConfigStageInfo(tInfo);
    }
}

AX_BOOL IsIVPSFromISPLinkable(AX_U8 nGrp)
{
    if (nGrp >= IVPS_GROUP_NUM) {
        return AX_FALSE;
    }
    if (gOptions.IsLinkMode()) {
        return AX_TRUE;
    }

    return AX_FALSE;
}

AX_VOID LinkIVPSFromISP(AX_U8 nGrp, AX_BOOL bLink)
{
    AX_MOD_INFO_S tPreMode;
    memset(&tPreMode, 0, sizeof(AX_MOD_INFO_S));
    AX_MOD_INFO_S tCurMode;
    memset(&tCurMode, 0, sizeof(AX_MOD_INFO_S));

    tPreMode.enModId = AX_ID_VIN;
    tPreMode.s32GrpId = AX_PIPE_ID;
    tPreMode.s32ChnId = nGrp;
    tCurMode.enModId = AX_ID_IVPS;
    tCurMode.s32GrpId = nGrp;
    tCurMode.s32ChnId = 0;

    if (bLink) {
        AX_SYS_Link(&tPreMode, &tCurMode);
        LOG_M(IVPS, "Setup syslink: ISP(Pipe: %d, Chn %d) => IVPS(Grp: %d)", tPreMode.s32GrpId, tPreMode.s32ChnId, tCurMode.s32GrpId);
    } else {
        AX_SYS_UnLink(&tPreMode, &tCurMode);
        LOG_M(IVPS, "Release syslink: ISP(Pipe: %d, Chn %d) => IVPS(Grp: %d)", tPreMode.s32GrpId, tPreMode.s32ChnId, tCurMode.s32GrpId);
    }
}

AX_BOOL AttrConvert(AX_U8 nGrp, AX_U8 nChn, AX_IVPS_ROTATION_E eRotation, AX_BOOL bMirror, VIDEO_ATTR_T &tAttr)
{
    AX_BOOL bChanged = AX_FALSE;

    switch (eRotation) {
        case AX_IVPS_ROTATION_0: {
            if (bMirror) {
                tAttr.x  = ALIGN_UP(tAttr.width, ROTATION_WIDTH_ALIGEMENT) - tAttr.width;
                tAttr.y  = 0;
                tAttr.w  = tAttr.width;
                tAttr.h  = tAttr.height;
                tAttr.width = ALIGN_UP(tAttr.width, ROTATION_WIDTH_ALIGEMENT);
                bChanged = AX_TRUE;
            }
            else {
                tAttr.x  = 0;
                tAttr.y  = 0;
                tAttr.w  = 0;
                tAttr.h  = 0;
            }
            break;
        }
        case AX_IVPS_ROTATION_90:
        case AX_IVPS_ROTATION_270: {
            ::swap(tAttr.width, tAttr.height);
            tAttr.x  = 0;
            tAttr.y  = 0;
            tAttr.w  = 0;
            tAttr.h  = 0;
            bChanged = AX_TRUE;
            break;
        };
        case AX_IVPS_ROTATION_180: {
            tAttr.x  = ALIGN_UP(tAttr.width, ROTATION_WIDTH_ALIGEMENT) - tAttr.width;
            tAttr.y  = 0;
            tAttr.w  = tAttr.width;
            tAttr.h  = tAttr.height;
            tAttr.width = ALIGN_UP(tAttr.width, ROTATION_WIDTH_ALIGEMENT);
            bChanged = AX_TRUE;
            break;
        };
        default: break;
    }

    return bChanged;
}

CIVPSStage::CIVPSStage(AX_VOID)
 : CStage(IVPS)
{

}

CIVPSStage::~CIVPSStage(AX_VOID)
{

}

AX_VOID CIVPSStage::MediaFrameRelease(CMediaFrame *pMediaFrame)
{
    if (pMediaFrame) {
        LOG_M_I(IVPS, "[%d][%d] seq:%d", pMediaFrame->nIvpsReleaseGrp, pMediaFrame->nReleaseChannel, pMediaFrame->nFrameID);
        AX_IVPS_ReleaseChnFrame(pMediaFrame->nIvpsReleaseGrp, pMediaFrame->nReleaseChannel, &pMediaFrame->tVideoFrame);
        delete pMediaFrame;
    }
}

AX_BOOL CIVPSStage::ProcessFrame(CMediaFrame *pFrame)
{
    if (!pFrame) {
        return AX_FALSE;
    }

    if (pFrame->nChannel >= IVPS_GROUP_NUM) {
        pFrame->FreeMem();
        return AX_TRUE;
    }

    const IVPS_GRP_PTR p = &m_arrIvpsGrp[pFrame->nChannel];     // 猜测是根据这一帧属于ISP的哪个CHANNEL来设置对应的IVPS的GROUP
    LOG_M_I(IVPS, "[Grp %d] Seq: %d, blkID_0: 0x%x, blkID_1 : 0x%x", p->nIvpsGrp, pFrame->nFrameID, pFrame->tFrame.tFrameInfo.stVFrame.u32BlkId[0], pFrame->tFrame.tFrameInfo.stVFrame.u32BlkId[1]);

    // 用户向IVPS发送帧数据。指定发送到IVPS的哪个GROUP
    AX_S32 ret = AX_IVPS_SendFrame(p->nIvpsGrp, &pFrame->tFrame.tFrameInfo.stVFrame, MAX_IPC_IVPS_FRAME_TIMEOUT);
    if (AX_IVPS_SUCC != ret) {
        LOG_M_E(IVPS, "AX_IVPS_SendFrame(Grp %d, ID %d) failed, ret=0x%x", p->nIvpsGrp, pFrame->nFrameID, ret);
        return AX_FALSE;
    }

    return AX_TRUE;
}

// 大概就是创建一个临时帧pMediaFrame，将它指向 从IVPS的CHANNEL(一个线程对应一个CHANNEL)里获取的一帧，最后让这临时帧入队对应的END_POINT处理，例如VENC和DET
AX_VOID CIVPSStage::FrameGetThreadFunc(IVPS_GET_THREAD_PARAM_PTR pThreadParam)
{
    // 测试OpenCV +++
    int width = 0;
    int height = 0;
    // void* y_plane = nullptr;
    // unsigned char *y_plane = nullptr;
    // uint8_t *y_plane = nullptr;
    uchar *y_plane = nullptr;
    // unsigned int *data;
    unsigned long long int *data;
    int count = 500;      // 用于测试，循环count次后执行测试
    // 测试OpenCV ---

    // 测试OpenCV追踪任务+++
    cv::Mat gray;
    // 测试OpenCV追踪任务---

    AX_S32 nRet = AX_IVPS_SUCC;

    // pThreadParam是由StartIVPS()函数里创建该线程时传进来的
    AX_U8 nIvpsGrp = pThreadParam->nIvpsGrp;
    AX_U8 nIvpsChn = pThreadParam->nIvpsChn;
    AX_U8 nIvpsChnIndex = pThreadParam->nIvpsChnIndex;

    LOG_M(IVPS, "[%d][%d] +++", nIvpsGrp, nIvpsChn);

    AX_CHAR szName[50] = {0};
    sprintf(szName, "IPC_IVPS_Get_%d", nIvpsChnIndex);
    prctl(PR_SET_NAME, szName);

    pThreadParam->bExit = AX_FALSE;
    while (!pThreadParam->bExit) {
        const END_POINT_OPTIONS& endpintOptions = g_tEPOptions[nIvpsChnIndex];
        if (g_isSleeped) {
            CTimeUtils::msSleep(1);
            continue;
        }

        CMediaFrame *pMediaFrame = new (std::nothrow) CMediaFrame();
        if (!pMediaFrame) {
            LOG_M_E(IVPS, "alloc MediaFrame instance fail");
            continue;
        }

        // 用户从IVPS指定的GROUP和CHANNEL通道处获取一帧处理完成的图像。
        nRet = AX_IVPS_GetChnFrame(nIvpsGrp, nIvpsChn, &pMediaFrame->tVideoFrame, 200);

        if (AX_IVPS_SUCC != nRet) {
            if (AX_ERR_IVPS_BUF_EMPTY == nRet) {
                delete pMediaFrame;
                CTimeUtils::msSleep(1);
                continue;
            }
            delete pMediaFrame;
            LOG_M(IVPS, "Get ivps frame failed. ret=0x%x", nRet);
            CTimeUtils::msSleep(1);
            continue;
        }

        LOG_M_I(IVPS, "[%d][%d] Seq: %lld, w:%d, h:%d, blkID_0:0x%x, blkID_1:0x%x", nIvpsGrp, nIvpsChn, pMediaFrame->tVideoFrame.u64SeqNum, pMediaFrame->tVideoFrame.u32Width, pMediaFrame->tVideoFrame.u32Height, pMediaFrame->tVideoFrame.u32BlkId[0], pMediaFrame->tVideoFrame.u32BlkId[1]);

        pMediaFrame->bIvpsFrame                         = AX_TRUE;
        pMediaFrame->nIvpsReleaseGrp                    = nIvpsGrp;
        pMediaFrame->nReleaseChannel                    = nIvpsChn;
        pMediaFrame->pFrameRelease                      = pThreadParam->pReleaseStage;
        pMediaFrame->nFrameID                           = pMediaFrame->tVideoFrame.u64SeqNum;       // 图像帧序列号
        pMediaFrame->tVideoFrame.u64VirAddr[0]          = (AX_U32)AX_POOL_GetBlockVirAddr(pMediaFrame->tVideoFrame.u32BlkId[0]);    // 图像数据虚拟地址
        pMediaFrame->tVideoFrame.u64PhyAddr[0]          = AX_POOL_Handle2PhysAddr(pMediaFrame->tVideoFrame.u32BlkId[0]);            // 图像数据物理地址
        pMediaFrame->tVideoFrame.u32FrameSize           = pMediaFrame->tVideoFrame.u32PicStride[0] * pMediaFrame->tVideoFrame.u32Height * 3 / 2;
        pMediaFrame->nStride                            = pMediaFrame->tVideoFrame.u32PicStride[0];

        if (CCapture::GetInstance()->GetCaptureStat(nIvpsChnIndex)) {
            if (CCapture::GetInstance()->ProcessFrame(nIvpsChnIndex, pMediaFrame)) {
                continue;
            }
        }

        // 这里究竟是不是LinkMode？
        // if(count == 1){
        //     LOG_M(IVPS, "Check if LinkMode ++++++");
        //     if(gOptions.IsLinkMode()){
        //         LOG_M(IVPS, "It is LinkMode.");
        //     }
        //     else{
        //         LOG_M(IVPS, "It is not LinkMode.");
        //     }
        //     LOG_M(IVPS, "Check if LinkMode ------");

        //     LOG_M(IVPS, "IVPS GROUP%d LinkModeFlag is %d", nIvpsGrp, g_tIvpsGroupConfig[nIvpsGrp].arrLinkModeFlag[nIvpsChn]);
        // }
        if (gOptions.IsLinkMode() && 1 == g_tIvpsGroupConfig[nIvpsGrp].arrLinkModeFlag[nIvpsChn]) {
            /* Skip if link mode */
            pMediaFrame->FreeMem();
            continue;
        }

        // 此处增加YUV转Mat的测试代码+++
        // if(count > 0){
        //     // LOG_M(IVPS, "YUV type is %d", pMediaFrame->tVideoFrame.enImgFormat);

        //     // grp2的图像分辨率是640×360
        //     if (nIvpsGrp == 2 && count == 1){
        //         LOG_M(IVPS, "grp is %d, frame resolution is %d x %d", nIvpsGrp, pMediaFrame->tVideoFrame.u32Width, pMediaFrame->tVideoFrame.u32Height);
            
        //         // 失败原因有可能是找不准确yuv存的内存地址、构造yuvImg的高宽和通道不正确、cvtColor的参数不正确

        //         LOG_M(IVPS, "ram address is %d", pMediaFrame->tVideoFrame.u32BlkId[0]);
        //         LOG_M(IVPS, "ram address is %lld", pMediaFrame->tVideoFrame.u64VirAddr[0]);
                
        //         width = pMediaFrame->tVideoFrame.u32Width;
        //         height = pMediaFrame->tVideoFrame.u32Height;
                
        //         // data = reinterpret_cast<unsigned long long int *>(pMediaFrame->tVideoFrame.u64VirAddr[0]);
        //         // y_plane = (uint8_t *)data;
        //         y_plane = reinterpret_cast<uchar *>(pMediaFrame->tVideoFrame.u64VirAddr[0]);

        //         // cv::Mat yuvImg(height + height / 2, width, CV_8UC3, y_plane);
        //         cv::Mat yuvImg(height + height / 2, width, CV_8UC1, y_plane);
        //         // cv::Mat yuvImg(height + height / 2, width, CV_8UC1, pMediaFrame->tVideoFrame.u32BlkId[2]);
                
        //         cv::Mat bgrImg(height, width, CV_8UC3);
        //         cv::cvtColor(yuvImg, bgrImg, cv::COLOR_YUV2BGR_NV12);
                
        //         cv::rectangle(bgrImg, {160, 90, 320, 180}, cv::Scalar(0, 0, 255), 2, 1);
        //         // cv::imwrite("/opt/yang_test/yuv_test/bgrImg.jpg", bgrImg);

        //         // 先将BGR转换为YUVI420格式，再将I420转换为NV12
        //         cv::Mat yuvI420Img;
        //         cv::cvtColor(bgrImg, yuvI420Img, cv::COLOR_BGR2YUV_I420);
        //         // LOG_M(IVPS, "yuvImg rows:%d, cols:%d", yuvImg.rows, yuvImg.cols);
        //         // LOG_M(IVPS, "yuvI420Img rows:%d, cols:%d", yuvI420Img.rows, yuvI420Img.cols);
        //         cv::Mat yuvNV12Img(yuvI420Img.rows, yuvI420Img.cols, CV_8UC1);
        //         // 复制Y分量
        //         yuvI420Img.rowRange(0, bgrImg.rows).copyTo(yuvNV12Img.rowRange(0, bgrImg.rows));
        //         // 交错U和V分量来创建UV分量
        //         for (int i = 0; i < bgrImg.rows / 4; i++) {
        //             for (int j = 0; j < yuvI420Img.cols / 2; j++) {
        //                 yuvNV12Img.at<uchar>(bgrImg.rows + i * 2, j * 2) = yuvI420Img.at<uchar>(bgrImg.rows + i, j);
        //                 yuvNV12Img.at<uchar>(bgrImg.rows + i * 2 + 1, j * 2) = yuvI420Img.at<uchar>(bgrImg.rows + i, j + yuvI420Img.cols / 2);
        //                 yuvNV12Img.at<uchar>(bgrImg.rows + i * 2, j * 2 + 1) = yuvI420Img.at<uchar>(bgrImg.rows + bgrImg.rows / 4 + i, j);
        //                 yuvNV12Img.at<uchar>(bgrImg.rows + i * 2 + 1, j * 2 + 1) = yuvI420Img.at<uchar>(bgrImg.rows + bgrImg.rows / 4 + i, j + yuvI420Img.cols / 2);
        //             }
        //         }

        //         // 把yuvNV12Img转回bgr写成jpg查看测试结果
        //         // cv::Mat newBgrImg(height, width, CV_8UC3);
        //         // cv::cvtColor(yuvNV12Img, newBgrImg, cv::COLOR_YUV2BGR_NV12);
        //         // cv::imwrite("/opt/yang_test/yuv_test/newBgrImg.jpg", newBgrImg);

        //         memcpy(y_plane, yuvNV12Img.data, (height + height / 2) * width * sizeof(uint8_t));

        //         // LOG_M(IVPS, "Successfully write yuvImg to /opt/yangt_test/yuv_test/ ???");
        //     }
        //     count--;
        // }

        // 测试OpenCV---



        // 测试YUV转BGR画框后再转YUV+++
        // if(count > 0){
        //     count--;
        // }
        // else{
        //     if(nIvpsGrp == 2){
        //         width = pMediaFrame->tVideoFrame.u32Width;
        //         height = pMediaFrame->tVideoFrame.u32Height;

        //         // data = reinterpret_cast<unsigned long long int *>(pMediaFrame->tVideoFrame.u64VirAddr[0]);
        //         // 强制转换为 void *、 uint8_t *、 uchar *都行
        //         // y_plane = (void *)data;
        //         // y_plane = (uint8_t *)data;
        //         // y_plane = (uchar *)data;
        //         // 其实可以强制转换后直接赋值给y_plane
        //         y_plane = reinterpret_cast<uchar *>(pMediaFrame->tVideoFrame.u64VirAddr[0]);

        //         cv::Mat yuvImg(height + height / 2, width, CV_8UC1, y_plane);
        //         cv::Mat bgrImg(height, width, CV_8UC3);
        //         cv::cvtColor(yuvImg, bgrImg, cv::COLOR_YUV2BGR_NV12);

        //         cv::rectangle(bgrImg, {160, 90, 320, 180}, cv::Scalar(0, 0, 255), 2, 1);

        //         // 先将BGR转换为YUVI420格式，再将I420转换为NV12
        //         cv::Mat yuvI420Img;
        //         cv::cvtColor(bgrImg, yuvI420Img, cv::COLOR_BGR2YUV_I420);
        //         cv::Mat yuvNV12Img(yuvI420Img.rows, yuvI420Img.cols, CV_8UC1);
        //         // 复制Y分量
        //         yuvI420Img.rowRange(0, bgrImg.rows).copyTo(yuvNV12Img.rowRange(0, bgrImg.rows));
        //         // 交错U和V分量来创建UV分量
        //         /*
        //             I420：  YYYYYYYY        NV12：  YYYYYYYY
        //                     YYYYYYYY                YYYYYYYY
        //                     YYYYYYYY                YYYYYYYY
        //                     YYYYYYYY                YYYYYYYY
        //                     UUUUUUUU                UVUVUVUV
        //                     VVVVVVVV                UVUVUVUV
        //         */
        //        // bgrImg.rows表示Y平面行数，因为是4:2:0，即4个Y共用一个U和一个V，所以bgrImg.rows / 4 表示I420中U平面或V平面的行数
        //         for (int i = 0; i < bgrImg.rows / 4; i++) {
        //             for (int j = 0; j < yuvI420Img.cols / 2; j++) {
        //                 yuvNV12Img.at<uchar>(bgrImg.rows + i * 2, j * 2) = yuvI420Img.at<uchar>(bgrImg.rows + i, j);    // I420的U平面的每行的前半行U
        //                 yuvNV12Img.at<uchar>(bgrImg.rows + i * 2 + 1, j * 2) = yuvI420Img.at<uchar>(bgrImg.rows + i, j + yuvI420Img.cols / 2);  // I420的U平面的每行的后半行U
        //                 yuvNV12Img.at<uchar>(bgrImg.rows + i * 2, j * 2 + 1) = yuvI420Img.at<uchar>(bgrImg.rows + bgrImg.rows / 4 + i, j);      // I420的V平面的每行的前半行V
        //                 yuvNV12Img.at<uchar>(bgrImg.rows + i * 2 + 1, j * 2 + 1) = yuvI420Img.at<uchar>(bgrImg.rows + bgrImg.rows / 4 + i, j + yuvI420Img.cols / 2);    // I420的V平面的每行的后半行V
        //             }
        //         }
        //         // memcpy(y_plane, yuvNV12Img.data, (height + height / 2) * width * sizeof(uint8_t));
        //         memcpy(y_plane, yuvNV12Img.data, (height + height / 2) * width * sizeof(uchar));
        //     }
        // }

        // 测试YUV转BGR画框后再转YUV---



        // 测试OpenCV追踪任务+++
        if(count > 0){
            count--;
        }
        else{
            g_bOpenCVTrack = AX_TRUE;
        }

        if(g_bOpenCVTrack && nIvpsGrp == 2){
            if(tracker){
                // cvtColor()把bgr图像转换为gray，然后tracker->update()得到bbox
                y_plane = reinterpret_cast<uchar *>(pMediaFrame->tVideoFrame.u64VirAddr[0]);
                cv::Mat yuvImg(height + height / 2, width, CV_8UC1, y_plane);
                cv::Mat bgrImg(height, width, CV_8UC3);
                cv::cvtColor(yuvImg, bgrImg, cv::COLOR_YUV2BGR_NV12);
                cv::cvtColor(bgrImg, gray, cv::COLOR_BGR2GRAY);

                bbox = tracker->update(gray);

                // rectangle()用bbox对帧图像画框
                cv::rectangle(bgrImg, bbox, cv::Scalar(0, 0, 255), 1, 1);

                // 把帧图像从bgr转换回YUV，写回pMediaFrame里
                cv::Mat yuvI420Img;
                cv::cvtColor(bgrImg, yuvI420Img, cv::COLOR_BGR2YUV_I420);
                cv::Mat yuvNV12Img(yuvI420Img.rows, yuvI420Img.cols, CV_8UC1);
                yuvI420Img.rowRange(0, bgrImg.rows).copyTo(yuvNV12Img.rowRange(0, bgrImg.rows));
                for (int i = 0; i < bgrImg.rows / 4; i++) {
                    for (int j = 0; j < yuvI420Img.cols / 2; j++) {
                        yuvNV12Img.at<uchar>(bgrImg.rows + i * 2, j * 2) = yuvI420Img.at<uchar>(bgrImg.rows + i, j);    // I420的U平面的每行的前半行U
                        yuvNV12Img.at<uchar>(bgrImg.rows + i * 2 + 1, j * 2) = yuvI420Img.at<uchar>(bgrImg.rows + i, j + yuvI420Img.cols / 2);  // I420的U平面的每行的后半行U
                        yuvNV12Img.at<uchar>(bgrImg.rows + i * 2, j * 2 + 1) = yuvI420Img.at<uchar>(bgrImg.rows + bgrImg.rows / 4 + i, j);      // I420的V平面的每行的前半行V
                        yuvNV12Img.at<uchar>(bgrImg.rows + i * 2 + 1, j * 2 + 1) = yuvI420Img.at<uchar>(bgrImg.rows + bgrImg.rows / 4 + i, j + yuvI420Img.cols / 2);    // I420的V平面的每行的后半行V
                    }
                }
                memcpy(y_plane, yuvNV12Img.data, (height + height / 2) * width * sizeof(uchar));
            }
            else{
                // 初始化tracker
                LOG_M(IVPS, "Init OpenCV Tracker!");
                // cvtColor()把bgr图像转换为gray，然后tracker->init()
                width = pMediaFrame->tVideoFrame.u32Width;
                height = pMediaFrame->tVideoFrame.u32Height;
                y_plane = reinterpret_cast<uchar *>(pMediaFrame->tVideoFrame.u64VirAddr[0]);
                cv::Mat yuvImg(height + height / 2, width, CV_8UC1, y_plane);
                cv::Mat bgrImg(height, width, CV_8UC3);
                cv::cvtColor(yuvImg, bgrImg, cv::COLOR_YUV2BGR_NV12);
                cv::cvtColor(bgrImg, gray, cv::COLOR_BGR2GRAY);

                tracker = new FDSSTTracker(true, true, true, true);
                tracker->init(bbox, gray);
            }
        }
        
        // 记得delete tracker释放动态内存
        // 测试OpenCV追踪任务---

        gPrintHelper.Add(E_PH_MOD_IVPS, nIvpsGrp, nIvpsChn);
        if (E_END_POINT_JENC == endpintOptions.eEPType) {
            if (!m_pVecJecEncoders->at(endpintOptions.nInnerIndex)->EnqueueFrame(pMediaFrame)) {
                pMediaFrame->FreeMem();
            }
        } else if (E_END_POINT_VENC == endpintOptions.eEPType) {
            if (!m_pVecEncoders->at(endpintOptions.nInnerIndex)->EnqueueFrame(pMediaFrame)) {
                pMediaFrame->FreeMem();
            }
        } else if (E_END_POINT_DET == endpintOptions.eEPType) {
            if (m_pDetectStage
                && gOptions.IsActivedDetect() && gOptions.IsActivedDetectFromWeb()) {
                if (!m_pDetectStage->EnqueueFrame(pMediaFrame)) {
                    pMediaFrame->FreeMem();
                }
            } else {
                pMediaFrame->FreeMem();
            }
        } else {
            pMediaFrame->FreeMem();
        }
    }


    // delete tracker释放动态内存
    if(tracker && nIvpsGrp == 2){
        LOG_M(IVPS, "Delete tracker");
        delete tracker;
        tracker = nullptr;  // 这个不知道要不要加
    }


    LOG_M(IVPS, "[%d][%d] ---", nIvpsGrp, nIvpsChn);
}

AX_VOID CIVPSStage::RgnThreadFunc(IVPS_REGION_PARAM_PTR pThreadParam) {
    if (nullptr == pThreadParam) {
        return;
    }

    prctl(PR_SET_NAME, "IPC_IVPS_RGN");

    AX_IVPS_FILTER nFilter = pThreadParam->nFilter;
    IVPS_GRP nIvpsGrp = pThreadParam->nGroup;

    LOG_M(IVPS, "[Grp:%d][Filter:0x%x][handle:%d] +++", nIvpsGrp, nFilter, pThreadParam->hChnRgn);

    COSDHandler *pOsdHandle = m_osdWrapper.NewInstance();
    if (nullptr == pOsdHandle) {
        LOG_M_E(IVPS, "Get osd handle failed.");
        return;
    }

    if (AX_FALSE == m_osdWrapper.InitHandler(pOsdHandle, gOptions.GetTtfPath().c_str())) {
        LOG_M_E(IVPS, "AX_OSDInitHandler failed, ttf: %s.", gOptions.GetTtfPath().c_str());
        m_osdWrapper.ReleaseInstance(&pOsdHandle);
        return;
    }

    AX_U16 *pArgbData = nullptr;

    wchar_t wszOsdDate[MAX_OSD_TIME_CHAR_LEN] = {0};
    memset(&wszOsdDate[0], 0, sizeof(wchar_t) * MAX_OSD_TIME_CHAR_LEN);

    AX_S32 ret = AX_IVPS_SUCC;
    pThreadParam->bExit = AX_FALSE;
    while (!pThreadParam->bExit) {
        IVPS_GRP nIvpsGrp = pThreadParam->nGroup;

        AX_IVPS_RGN_DISP_GROUP_S tDisp;
        memset(&tDisp, 0, sizeof(AX_IVPS_RGN_DISP_GROUP_S));

        tDisp.nNum = 1;
        tDisp.tChnAttr.nAlpha = 1024;
        tDisp.tChnAttr.eFormat = AX_FORMAT_ARGB1555;
        tDisp.tChnAttr.nZindex = 0;

        memset(&tDisp.arrDisp[0], 0, sizeof(AX_IVPS_RGN_DISP_S));

        memset(&wszOsdDate[0], 0, sizeof(wchar_t) * MAX_OSD_TIME_CHAR_LEN);

        // 应该是将当前时间作为将要进行OSD叠加到帧上面的字符
        AX_S32 nCharLen = 0;
        if (nullptr == CTimeUtils::GetCurrDateStr(&wszOsdDate[0], OSD_DATE_FORMAT_YYMMDDHHmmSS, nCharLen)) {
            LOG_M_E(IVPS, "Failed to get current date string.");
            break;
        }

        AX_U8 nRotation = CStageOptionHelper().GetInstance()->GetCamera().nRotation;
        AX_U8 nMirror = CStageOptionHelper().GetInstance()->GetCamera().nMirror;

        VIDEO_ATTR_T tAttr = CStageOptionHelper().GetInstance()->GetVideo(nIvpsGrp);
        if (AX_IVPS_ROTATION_90 == nRotation || AX_IVPS_ROTATION_270 == nRotation) {
            ::swap(tAttr.width, tAttr.height);
        }

        AX_U32 nSrcOffset = 0;
        if (nMirror || AX_IVPS_ROTATION_180 == nRotation) {
            nSrcOffset  = ALIGN_UP(tAttr.width, ROTATION_WIDTH_ALIGEMENT) - tAttr.width;
        }

        AX_U32 nSrcWidth = tAttr.width;
        AX_U32 nSrcHeight = tAttr.height;

        AX_U32 nFontSize = (0 == nIvpsGrp ? 128 : 24);
        AX_U32 nMarginX = (0 == nIvpsGrp ? 48 : 14);
        AX_U32 nMarginY = (0 == nIvpsGrp ? 20 : 8);;
        OSD_ALIGN_TYPE_E eAlign = OSD_ALIGN_TYPE_LEFT_TOP;

        AX_U32 nPicOffset = nMarginX % OSD_ALIGN_WIDTH;
        AX_U32 nPicOffsetBlock = nMarginX / OSD_ALIGN_WIDTH;
        AX_U32 nARGB = 0xFFFFFFFF;

        AX_U32 nPixWidth = ALIGN_UP(nFontSize, BASE_FONT_SIZE) * nCharLen;
        AX_U32 nPixHeight = ALIGN_UP(nFontSize, BASE_FONT_SIZE);
        nPixWidth = ALIGN_UP(nPixWidth + nPicOffset, OSD_ALIGN_WIDTH);
        nPixHeight = ALIGN_UP(nPixHeight, OSD_ALIGN_HEIGHT);

        AX_U32 nPicSize = nPixWidth * nPixHeight * 2;
        AX_U32 nFontColor = nARGB;
        nFontColor |= (1 << 24);

        pArgbData = (AX_U16 *)malloc(nPicSize);

        // 将时间字符串生成出ARGB数据，存储到pArgbData指向的buffer
        if (nullptr == m_osdWrapper.GenARGB(pOsdHandle, (wchar_t *)&wszOsdDate[0], (AX_U16 *)pArgbData, nPixWidth, nPixHeight,
                                        nPicOffset, 0, nFontSize, AX_TRUE, nFontColor, 0xFFFFFF, 0xFF000000,
                                        eAlign)) {
            LOG_M_E(IVPS, "Failed to generate bitmap for date string.");
            break;
        }

        tDisp.arrDisp[0].eType = AX_IVPS_RGN_TYPE_OSD;
        tDisp.arrDisp[0].bShow = AX_TRUE;
        tDisp.arrDisp[0].uDisp.tOSD.u32Zindex = 1;
        tDisp.arrDisp[0].uDisp.tOSD.enRgbFormat = AX_FORMAT_ARGB1555;
        tDisp.arrDisp[0].uDisp.tOSD.u16Alpha = (AX_F32)(nARGB >> 24) / 0xFF * 1024;
        tDisp.arrDisp[0].uDisp.tOSD.u32ColorKey = 0x0;
        tDisp.arrDisp[0].uDisp.tOSD.u32BgColorLo = 0xFFFFFFFF;
        tDisp.arrDisp[0].uDisp.tOSD.u32BgColorHi = 0xFFFFFFFF;
        tDisp.arrDisp[0].uDisp.tOSD.u32BmpWidth = nPixWidth;
        tDisp.arrDisp[0].uDisp.tOSD.u32BmpHeight = nPixHeight;
        tDisp.arrDisp[0].uDisp.tOSD.u32DstXoffset = nSrcOffset + CCommonUtils::CalOsdOffsetX(
            nSrcWidth, nPixWidth, (nPicOffset > 0 ? nPicOffsetBlock * OSD_ALIGN_WIDTH : nMarginX), eAlign);
        tDisp.arrDisp[0].uDisp.tOSD.u32DstYoffset = CCommonUtils::CalOsdOffsetY(nSrcHeight, nPixHeight, nMarginY, eAlign);
        tDisp.arrDisp[0].uDisp.tOSD.u64PhyAddr = 0;
        tDisp.arrDisp[0].uDisp.tOSD.pBitmap = (AX_U8 *)pArgbData;

        LOG_M_I(IVPS, "[%d] OSD (TIME): hHandle: %d, u32BmpWidth: %d, u32BmpHeight: %d, xOffset: %d, yOffset: %d, alpha: %d",
            nIvpsGrp,
            pThreadParam->hChnRgn,
            tDisp.arrDisp[0].uDisp.tOSD.u32BmpWidth,
            tDisp.arrDisp[0].uDisp.tOSD.u32BmpHeight,
            tDisp.arrDisp[0].uDisp.tOSD.u32DstXoffset,
            tDisp.arrDisp[0].uDisp.tOSD.u32DstYoffset,
            tDisp.arrDisp[0].uDisp.tOSD.u16Alpha);

        /* Region update */
        ret = AX_IVPS_RGN_Update(pThreadParam->hChnRgn, &tDisp);    // 更新 hRegion 对应的 region 显示信息
        if (AX_IVPS_SUCC != ret) {
            LOG_M_E(IVPS, "[%d][0x%02x] AX_IVPS_RGN_Update fail, ret=0x%x, hChnRgn=%d", nIvpsGrp, nFilter, ret, pThreadParam->hChnRgn);
        }

        /* Free time osd resource */
        free(pArgbData);
        pArgbData = nullptr;

        CTimeUtils::msSleep(1000);
    }

    m_osdWrapper.ReleaseInstance(&pOsdHandle);

    LOG_M(IVPS, "[%d][0x%x] ---", nIvpsGrp, nFilter);
}

AX_BOOL CIVPSStage::SetLogo(AX_U32 nIvpsGrp, AX_S32 hLogoHandle, string strPicPath, AX_U32 nPicWidth, AX_U32 nPicHeight)
{
    AX_U8 nRotation = CStageOptionHelper().GetInstance()->GetCamera().nRotation;
    AX_U8 nMirror = CStageOptionHelper().GetInstance()->GetCamera().nMirror;

    VIDEO_ATTR_T tAttr = CStageOptionHelper().GetInstance()->GetVideo(nIvpsGrp);
    if (AX_IVPS_ROTATION_90 == nRotation || AX_IVPS_ROTATION_270 == nRotation) {
        ::swap(tAttr.width, tAttr.height);
    }

    if (nMirror || AX_IVPS_ROTATION_180 == nRotation) {
        tAttr.x  = ALIGN_UP(tAttr.width, ROTATION_WIDTH_ALIGEMENT) - tAttr.width;
    }

    AX_U32 nSrcWidth = tAttr.width;
    AX_U32 nSrcHeight = tAttr.height;
    AX_U32 nSrcOffset = tAttr.x;

    AX_IVPS_RGN_DISP_GROUP_S tDisp;
    memset(&tDisp, 0, sizeof(AX_IVPS_RGN_DISP_GROUP_S));

    tDisp.nNum = 1;
    tDisp.tChnAttr.nAlpha = 1024;
    tDisp.tChnAttr.eFormat = AX_FORMAT_ARGB1555;
    tDisp.tChnAttr.nZindex = 1;

    memset(&tDisp.arrDisp[0], 0, sizeof(AX_IVPS_RGN_DISP_S));

    /* Config picture OSD */
    AX_U32 nPicMarginX = (0 == nIvpsGrp ? 32 : 8);
    AX_U32 nPicMarginY = (0 == nIvpsGrp ? 48 : 8);

    AX_U32 nSrcBlock = nSrcWidth / OSD_ALIGN_X_OFFSET;
    AX_U32 nGap = nSrcWidth % OSD_ALIGN_X_OFFSET;
    AX_U32 nBlockBollowed = ceil((AX_F32)(nPicWidth + nPicMarginX - nGap) / OSD_ALIGN_X_OFFSET);
    if (nBlockBollowed < 0) {
        nBlockBollowed = 0;
    }
    AX_U32 nOffsetX = nSrcOffset + (nSrcBlock - nBlockBollowed) * OSD_ALIGN_X_OFFSET;
    AX_U32 nOffsetY = CCommonUtils::CalOsdOffsetY(nSrcHeight, nPicHeight, nPicMarginY, OSD_ALIGN_TYPE_RIGHT_BOTTOM);

    if (AX_FALSE == CCommonUtils::LoadImage(strPicPath.c_str(), &tDisp.arrDisp[0].uDisp.tOSD.u64PhyAddr,
                                            (AX_VOID **)&tDisp.arrDisp[0].uDisp.tOSD.pBitmap,
                                            nPicWidth * nPicHeight * 2)) {
        LOG_M_E(IVPS, "Load logo(%s) failed.", strPicPath.c_str());
        return AX_FALSE;
    }

    tDisp.arrDisp[0].eType = AX_IVPS_RGN_TYPE_OSD;
    tDisp.arrDisp[0].bShow = AX_TRUE;
    tDisp.arrDisp[0].uDisp.tOSD.u32Zindex = 0;
    tDisp.arrDisp[0].uDisp.tOSD.enRgbFormat = AX_FORMAT_ARGB1555;
    tDisp.arrDisp[0].uDisp.tOSD.u16Alpha = 1024;
    tDisp.arrDisp[0].uDisp.tOSD.u32ColorKey = 0x0;
    tDisp.arrDisp[0].uDisp.tOSD.u32BgColorLo = 0xFFFFFFFF;
    tDisp.arrDisp[0].uDisp.tOSD.u32BgColorHi = 0xFFFFFFFF;
    tDisp.arrDisp[0].uDisp.tOSD.u32BmpWidth = nPicWidth;
    tDisp.arrDisp[0].uDisp.tOSD.u32BmpHeight = nPicHeight;
    tDisp.arrDisp[0].uDisp.tOSD.u32DstXoffset = nOffsetX;
    tDisp.arrDisp[0].uDisp.tOSD.u32DstYoffset = nOffsetY;

    LOG_M_I(IVPS, "[%d] OSD(PICTURE): hHandle: %d, u32BmpWidth: %d, u32BmpHeight: %d, xOffset: %d, yOffset: %d, alpha: %d",
        nIvpsGrp,
        hLogoHandle,
        tDisp.arrDisp[0].uDisp.tOSD.u32BmpWidth,
        tDisp.arrDisp[0].uDisp.tOSD.u32BmpHeight,
        tDisp.arrDisp[0].uDisp.tOSD.u32DstXoffset,
        tDisp.arrDisp[0].uDisp.tOSD.u32DstYoffset,
        tDisp.arrDisp[0].uDisp.tOSD.u16Alpha);

    AX_S32 ret = AX_IVPS_RGN_Update(hLogoHandle, &tDisp);
    if (AX_IVPS_SUCC != ret) {
        LOG_M_E(IVPS, "AX_IVPS_RGN_Update fail, ret=0x%x, hChnRgn=%d", ret, hLogoHandle);
    }

    /* Free picture osd resource */
    AX_SYS_MemFree(tDisp.arrDisp[0].uDisp.tOSD.u64PhyAddr, tDisp.arrDisp[0].uDisp.tOSD.pBitmap);

    return AX_SUCCESS == ret ? AX_TRUE : AX_FALSE;
}

AX_BOOL CIVPSStage::Init()
{
    if (gOptions.IsEnableOSD() && !InitOsd()) {
        LOG_M_E(IVPS, "Init OSD failed.");
        return AX_FALSE;
    }

    if (!InitPPL()) {
        LOG_M_E(IVPS, "Init IVPS failed.");
        return AX_FALSE;
    }

    if (!StartIVPS()) {
        LOG_M_E(IVPS, "Start IVPS failed.");
        return AX_FALSE;
    }

    if (gOptions.IsEnableOSD() && !StartOSD()) {
        LOG_M_E(IVPS, "Start OSD failed.");
        return AX_FALSE;
    }

    return CStage::Init();
}

AX_VOID CIVPSStage::DeInit()
{
    if (!StopIVPS()) {
        LOG_M_E(IVPS, "Stop IVPS failed");
        return;
    }

    if (gOptions.IsEnableOSD() && !StopOSD()) {
        LOG_M_E(IVPS, "Stop OSD failed.");
        return;
    }

    CStage::DeInit();
}

AX_BOOL CIVPSStage::InitOsd()
{
    m_arrOsdAttr[0].nIvpsGrp        = 0;
    m_arrOsdAttr[0].nFilter         = 0x11;
    m_arrOsdAttr[0].eOsdType        = OSD_TYPE_TIME;
    m_arrOsdAttr[0].nZIndex         = 0;
    m_arrOsdAttr[0].bThreadUpdate   = AX_TRUE;

    m_arrOsdAttr[1].nIvpsGrp        = 0;
    m_arrOsdAttr[1].nFilter         = 0x11;
    m_arrOsdAttr[1].eOsdType        = OSD_TYPE_PICTURE;
    m_arrOsdAttr[1].nZIndex         = 1;
    m_arrOsdAttr[1].bThreadUpdate   = AX_FALSE;

    m_arrOsdAttr[2].nIvpsGrp        = 2;
    m_arrOsdAttr[2].nFilter         = 0x11;
    m_arrOsdAttr[2].eOsdType        = OSD_TYPE_TIME;
    m_arrOsdAttr[2].nZIndex         = 0;
    m_arrOsdAttr[2].bThreadUpdate   = AX_TRUE;

    m_arrOsdAttr[3].nIvpsGrp        = 2;
    m_arrOsdAttr[3].nFilter         = 0x11;
    m_arrOsdAttr[3].eOsdType        = OSD_TYPE_PICTURE;
    m_arrOsdAttr[3].nZIndex         = 1;
    m_arrOsdAttr[3].bThreadUpdate   = AX_FALSE;

    return AX_TRUE;
}

AX_BOOL CIVPSStage::InitPPL()
{
    memset(&m_arrIvpsGrp[0], 0, sizeof(IVPS_GRP_T) * IVPS_GROUP_NUM);   // 应该是将结构体清0，给后面的StartIVPS()里的AX_IVPS_CreateGrp()和AX_IVPS_SetPipelineAttr作为实参作准备

    if (gOptions.IsEISSupport()) {
        // Make sure use AX_IVPS_ENGINE_GDC for EIS
        for (AX_U8 i = 0; i < IVPS_GROUP_NUM; i++) {
            g_tIvpsGroupConfig[i].arrChnEngineType[0] = AX_IVPS_ENGINE_GDC;
        }

        if (gOptions.IsEnableEISEffectComp() && (IVPS_GROUP_NUM > 1)) { // Make sure last grp use AX_IVPS_ENGINE_TDP(escape from EIS)
            g_tIvpsGroupConfig[IVPS_GROUP_NUM - 1].arrChnEngineType[0] = AX_IVPS_ENGINE_TDP;
        }
    }

    AX_U8 nIvpsChnIndex = 0;
    for (AX_U32 i = 0; i < IVPS_GROUP_NUM; ++i) {
        if (MAX_IMAGE_WIDTH < m_tChnAttr.tChnAttr[i].nWidth || MAX_IMAGE_HEIGHT < m_tChnAttr.tChnAttr[i].nHeight) {
            LOG_M_E(IVPS, "Image size is larger than the max size(%dx%d > %dx%d).",
                m_tChnAttr.tChnAttr[i].nWidth,
                m_tChnAttr.tChnAttr[i].nHeight,
                MAX_IMAGE_WIDTH,
                MAX_IMAGE_HEIGHT);
            return AX_FALSE;
        }

        m_arrIvpsGrp[i].nIvpsGrp                = i;
        m_arrIvpsGrp[i].tIvpsGrp.ePipeline      = AX_IVPS_PIPELINE_DEFAULT;
        m_arrIvpsGrp[i].tIvpsGrp.nInFifoDepth   = AX_IVPS_IN_FIFO_DEPTH;

        /* Config group filters */
        m_arrIvpsGrp[i].tPipelineAttr.nOutChnNum                = g_tIvpsGroupConfig[i].nGrpChnNum;
        m_arrIvpsGrp[i].tPipelineAttr.tFbInfo.bInPlace          = AX_FALSE;
        m_arrIvpsGrp[i].tPipelineAttr.tFbInfo.PoolId            = -1;
        m_arrIvpsGrp[i].tPipelineAttr.tFilter[0][0].bEnable     = AX_FALSE;
        m_arrIvpsGrp[i].tPipelineAttr.tFilter[0][1].bEnable     = AX_FALSE;

        /* Config channel filters */
        for (AX_U8 nChn = 0; nChn < g_tIvpsGroupConfig[i].nGrpChnNum; ++nChn) {
            AX_U8 nChnFilter = nChn + 1;
            m_arrIvpsGrp[i].tPipelineAttr.nOutFifoDepth[nChn]                       = AX_IVPS_OUT_FIFO_DEPTH;
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].bEnable            = AX_TRUE;
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].eEngine            = g_tIvpsGroupConfig[i].arrChnEngineType[nChn];
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].eDstPicFormat      = AX_YUV420_SEMIPLANAR;
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicOffsetX0    = 0;
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicOffsetY0    = 0;
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicWidth       = g_tIvpsGroupConfig[i].arrOutResolution[nChn][0];
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicHeight      = g_tIvpsGroupConfig[i].arrOutResolution[nChn][1];

            if (IsOSDChannel(i, nChn)) {
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][1].bEnable            = AX_TRUE;
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][1].eEngine            = AX_IVPS_ENGINE_VO;
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][1].eDstPicFormat      = AX_YUV420_SEMIPLANAR;
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][1].nDstPicOffsetX0    = 0;
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][1].nDstPicOffsetY0    = 0;
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][1].nDstPicWidth       = g_tIvpsGroupConfig[i].arrOutResolution[nChn][0];
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][1].nDstPicHeight      = g_tIvpsGroupConfig[i].arrOutResolution[nChn][1];

                LOG_M(IVPS, "Enable OSD on (group %d, filter 0x%x)", i, nChnFilter << 4 | 0x1);
            }

            AX_IVPS_ROTATION_E eRotation = (AX_IVPS_ROTATION_E)CStageOptionHelper().GetInstance()->GetCamera().nRotation;
            AX_BOOL bMirror = (AX_BOOL)CStageOptionHelper().GetInstance()->GetCamera().nMirror;
            AX_BOOL bFlip = (AX_BOOL)CStageOptionHelper().GetInstance()->GetCamera().nFlip;

            /* Config channel engine attributes */
            if (AX_IVPS_ENGINE_TDP == m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].eEngine) {
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tTdpCfg.eRotation = eRotation;
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tTdpCfg.bMirror = bMirror;
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tTdpCfg.bFlip = bFlip;
            } else if (AX_IVPS_ENGINE_GDC == m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].eEngine) {
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tGdcCfg.eRotation = eRotation;
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tGdcCfg.bMirror = bMirror;
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tGdcCfg.bFlip = bFlip;
            }

            /* Config channel framerate */
            if (-1 != g_tIvpsGroupConfig[i].arrFramerate[nChn][0] || -1 != g_tIvpsGroupConfig[i].arrFramerate[nChn][1]) {
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tFRC.nSrcFrameRate = g_tIvpsGroupConfig[i].arrFramerate[nChn][0];
                m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tFRC.nDstFrameRate = g_tIvpsGroupConfig[i].arrFramerate[nChn][1];
            }

            const auto& endpointOptions = g_tEPOptions[nIvpsChnIndex];
            AX_BOOL bRestart = AX_FALSE;
            VIDEO_ATTR_T tAttr = CStageOptionHelper().GetInstance()->GetVideo(nIvpsChnIndex);

            bRestart = AttrConvert(i, nChn, eRotation, bMirror, tAttr);

            /* Config channel resolution */
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicWidth = tAttr.width;
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicHeight = tAttr.height;

            /* Align up for stride */
            m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicStride = ALIGN_UP(m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicWidth, g_tIvpsGroupConfig[i].arrOutResolution[nChn][2]);

            LOG_M(IVPS, "[grp %d][chn %d] SRC(w:%d, h:%d, framerate:%d) => DST(w:%d, s:%d, h:%d, framerate:%d)"
                , i
                , nChn
                , m_tChnAttr.tChnAttr[i].nWidth
                , m_tChnAttr.tChnAttr[i].nHeight
                , m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tFRC.nSrcFrameRate
                , m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicWidth
                , m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicStride
                , m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].nDstPicHeight
                , m_arrIvpsGrp[i].tPipelineAttr.tFilter[nChnFilter][0].tFRC.nDstFrameRate);

            if (bRestart) {
                if (E_END_POINT_VENC == endpointOptions.eEPType) {
                    m_pVecEncoders->at(endpointOptions.nInnerIndex)->Stop();
                    m_pVecEncoders->at(endpointOptions.nInnerIndex)->ResetByResolution(tAttr, AX_FALSE);
                }
                else if (E_END_POINT_JENC == endpointOptions.eEPType) {
                    JPEG_CONFIG_T tJencAttr;
                    tJencAttr.nWidth = tAttr.width;
                    tJencAttr.nHeight = tAttr.height;
                    tJencAttr.nStride = ALIGN_UP(tJencAttr.nWidth, g_tIvpsGroupConfig[i].arrOutResolution[nChn][2]);
                    m_pVecEncoders->at(endpointOptions.nInnerIndex)->Stop();
                    m_pVecJecEncoders->at(endpointOptions.nInnerIndex)->ResetByResolution(tJencAttr, AX_FALSE);
                }
                else if (E_END_POINT_DET == endpointOptions.eEPType) {
                    if (m_pDetectStage) {
                        DETECT_STAGE_INFO_T tInfo;
                        tInfo.nFrmWidth  = tAttr.width;
                        tInfo.nFrmHeight = tAttr.height;
                        tInfo.nVinFps = g_tIvpsGroupConfig[i].arrFramerate[nChn][0];
                        tInfo.nFrmFps = g_tIvpsGroupConfig[i].arrFramerate[nChn][1];
                        m_pDetectStage->Reset(tInfo);
                    }
                }
            }

            nIvpsChnIndex ++;
        }
    }

    return AX_TRUE;
}

// 大概就是LinkIVPSFromISP，然后创建IVPS的GROUP、PIPELINE和CHANNEL，并启动它们。最后创建FrameGetThreadFunc线程来获取帧
AX_BOOL CIVPSStage::StartIVPS()
{
    LOG_M(IVPS, "+++");

    AX_U8 nIvpsChnIndex = 0;
    // 有3个GROUP所以循环设置3次
    for (AX_U8 i = 0; i < IVPS_GROUP_NUM; i++) {
        // 获取第i个GROUP的设置 m_arrIvpsGrp[i]
        IVPS_GRP_T& tGrp = m_arrIvpsGrp[i];

        if (0 == tGrp.tPipelineAttr.nOutChnNum) {
            continue;
        }

        if (IsIVPSFromISPLinkable(i)) {
            LinkIVPSFromISP(i, AX_TRUE);
        }

        IVPS_GRP nIvpsGrp = tGrp.nIvpsGrp;
        AX_S32 ret = AX_IVPS_CreateGrp(nIvpsGrp, &tGrp.tIvpsGrp);   // 创建一个IVPS GROUP。输入IVPS GROUP号和IVPS GROUP属性指针，该IVPS GROUP属性指针实参已在StartIVPS()的上一步InitPPL()中清0
        if (AX_IVPS_SUCC != ret) {
            LOG_M_E(IVPS, "AX_IVPS_CreateGrp(Grp: %d) failed, ret=0x%x", nIvpsGrp, ret);
            return AX_FALSE;
        }

        ret = AX_IVPS_SetPipelineAttr(nIvpsGrp, &tGrp.tPipelineAttr);   // 设置GROUP 的Pipeline 属性。输入IVPS GROUP号和Pipeline 属性值指针，该IVPS GROUP属性指针实参已在StartIVPS()的上一步InitPPL()中清0
        if (AX_IVPS_SUCC != ret) {
            LOG_M_E(IVPS, "AX_IVPS_SetPipelineAttr(Grp: %d) failed, ret=0x%x", nIvpsGrp, ret);
            return AX_FALSE;
        }

        LOG_M(IVPS, "Create group %d(w:%d, h:%d)", nIvpsGrp, m_tChnAttr.tChnAttr[i].nWidth, m_tChnAttr.tChnAttr[i].nHeight);

        for (AX_U8 chn = 0; chn < tGrp.tPipelineAttr.nOutChnNum; ++chn) {
            const auto& endpointOptions = g_tEPOptions[nIvpsChnIndex];
            if (gOptions.IsLinkMode()) {
                END_POINT_TYPE eType = endpointOptions.eEPType;
                if (1 == g_tIvpsGroupConfig[nIvpsGrp].arrLinkModeFlag[chn]) {
                    AX_MOD_INFO_S tPreMode;
                    memset(&tPreMode, 0, sizeof(AX_MOD_INFO_S));
                    AX_MOD_INFO_S tCurMode;
                    memset(&tCurMode, 0, sizeof(AX_MOD_INFO_S));

                    tPreMode.enModId = AX_ID_IVPS;
                    tPreMode.s32GrpId = i;
                    tPreMode.s32ChnId = chn;
                    tCurMode.enModId = (E_END_POINT_VENC == eType ? AX_ID_VENC : AX_ID_JENC);
                    tCurMode.s32GrpId = 0;
                    tCurMode.s32ChnId = endpointOptions.nChannel;
                    AX_SYS_Link(&tPreMode, &tCurMode);

                    LOG_M(IVPS, "Setup syslink: IVPS(Grp: %d, Chn %d) => %s(Chn %d)", tPreMode.s32GrpId, tPreMode.s32ChnId, (E_END_POINT_VENC == eType ? "VENC" : "JENC"), tCurMode.s32ChnId);
                }
            }

            /* get thread param */
            // 设置用于FrameGetThreadFunc的参数
            m_tGetThreadParam[nIvpsChnIndex].bValid = AX_TRUE;
            m_tGetThreadParam[nIvpsChnIndex].nIvpsGrp = nIvpsGrp;
            m_tGetThreadParam[nIvpsChnIndex].nIvpsChn = chn;
            m_tGetThreadParam[nIvpsChnIndex].nIvpsChnIndex = nIvpsChnIndex;
            m_tGetThreadParam[nIvpsChnIndex].pReleaseStage = this;
            m_tGetThreadParam[nIvpsChnIndex].bExit = AX_FALSE;

            nIvpsChnIndex++;

            // 启用IVPS CHANNEL。输入IVPS GROUP 号和IVPS CHANNEL通道号
            ret = AX_IVPS_EnableChn(nIvpsGrp, chn);
            if (AX_IVPS_SUCC != ret) {
                LOG_M_E(IVPS, "AX_IVPS_EnableChn(Grp: %d, Chn: %d) failed, ret=0x%x", nIvpsGrp, chn, ret);
                return AX_FALSE;
            }

            // 用户获取一个CHANNEL通道的设备节点。文档说它的返回值是设备节点句柄，但这里并没有使用，不明白这里的作用
            AX_IVPS_GetChnFd(nIvpsGrp, chn);

            LOG_M(IVPS, "Enable channel (Grp: %d, Chn: %d)", nIvpsGrp, chn);
        }

        // 启用IVPS GROUP
        ret = AX_IVPS_StartGrp(nIvpsGrp);
        if (AX_IVPS_SUCC != ret) {
            LOG_M_E(IVPS, "AX_IVPS_StartGrp(Grp: %d) failed, ret=0x%x", nIvpsGrp, ret);
            return AX_FALSE;
        }
    }

    /* Start frame get thread */
    // 这里应该是将获取到的帧数据Enqueue入队到VENC做处理，所以是根据VENC的CHANNEL数量来创建多少个线程
    for (AX_U8 i = 0; i < MAX_VENC_CHANNEL_NUM; i++) {
        if (m_tGetThreadParam[i].bValid) {
            m_hGetThread[i] = std::thread(&CIVPSStage::FrameGetThreadFunc, this, &m_tGetThreadParam[i]);
        }
    }

    LOG_M(IVPS, "---");

    return AX_TRUE;
}

AX_BOOL CIVPSStage::StopIVPS()
{
    LOG_M(IVPS, "+++");
    AX_S32 ret = AX_IVPS_SUCC;

    StopWorkThread();

    AX_U8 nIvpsChnIndex = 0;
    for (AX_U8 i = 0; i < IVPS_GROUP_NUM; i++) {
        IVPS_GRP_T tGrp = m_arrIvpsGrp[i];
        IVPS_GRP nGrp = tGrp.nIvpsGrp;

        ret = AX_IVPS_StopGrp(nGrp);
        if (AX_IVPS_SUCC != ret) {
            LOG_M_E(IVPS, "AX_IVPS_StopGrp(Grp: %d) failed, ret=0x%x", nGrp, ret);
        }

        for (AX_U8 chn = 0; chn < tGrp.tPipelineAttr.nOutChnNum; ++chn) {
            const auto& endpointOptions = g_tEPOptions[nIvpsChnIndex];
            ret = AX_IVPS_DisableChn(nGrp, chn);
            if (AX_IVPS_SUCC != ret) {
                LOG_M_E(IVPS, "AX_IVPS_DisableChn(Grp: %d, Channel: %d) failed, ret=0x%x", nGrp, chn, ret);
            }

            if (gOptions.IsLinkMode()) {
                END_POINT_TYPE eType = endpointOptions.eEPType;
                if (1 == g_tIvpsGroupConfig[nGrp].arrLinkModeFlag[chn]) {
                    AX_MOD_INFO_S tPreMode;
                    memset(&tPreMode, 0, sizeof(AX_MOD_INFO_S));
                    AX_MOD_INFO_S tCurMode;
                    memset(&tCurMode, 0, sizeof(AX_MOD_INFO_S));

                    tPreMode.enModId = AX_ID_IVPS;
                    tPreMode.s32GrpId = i;
                    tPreMode.s32ChnId = chn;
                    tCurMode.enModId = (E_END_POINT_VENC == eType ? AX_ID_VENC : AX_ID_JENC);
                    tCurMode.s32GrpId = 0;
                    tCurMode.s32ChnId = endpointOptions.nChannel;
                    AX_SYS_UnLink(&tPreMode, &tCurMode);

                    LOG_M(IVPS, "Release syslink: IVPS(Grp: %d, Chn %d) => %s(Chn %d)", tPreMode.s32GrpId, tPreMode.s32ChnId, (E_END_POINT_VENC == eType ? "VENC" : "JENC"), tCurMode.s32ChnId);
                }
            }

            nIvpsChnIndex++;
        }

        if (IsIVPSFromISPLinkable(i)) {
            LinkIVPSFromISP(i, AX_FALSE);
        }

        ret = AX_IVPS_DestoryGrp(nGrp);
        if (AX_IVPS_SUCC != ret) {
            LOG_M_E(IVPS, "AX_IVPS_DestoryGrp(Grp: %d) failed, ret=0x%x", nGrp, ret);
        }
    }

    AX_IVPS_CloseAllFd();

    LOG_M(IVPS, "---");
    return AX_TRUE;
}

AX_BOOL CIVPSStage::StopWorkThread()
{
    for (AX_U8 i = 0; i < MAX_VENC_CHANNEL_NUM; i ++) {
        m_tGetThreadParam[i].bExit = AX_TRUE;
        if (m_hGetThread[i].joinable()) {
            m_hGetThread[i].join();
        }
    }

    if (gOptions.IsEnableOSD()) {
        for (AX_U32 i = 0; i < OSD_ATTACH_NUM; i++) {
            OSD_ATTR_INFO* pAttr = &m_arrOsdAttr[i];

            if (pAttr->bThreadUpdate) {
                m_arrRgnThreadParam[i].bExit = AX_TRUE;
                if (m_arrRgnThread[i].joinable()) {
                    m_arrRgnThread[i].join();
                }
            }
        }
    }

    return AX_TRUE;
}

AX_BOOL CIVPSStage::FillCameraAttr(CCamera* pCameraInstance)
{
    if (nullptr == pCameraInstance) {
        return AX_FALSE;
    }

    m_tChnAttr = pCameraInstance->GetChnAttr();

    for (AX_U32 nGrp = 0; nGrp < IVPS_GROUP_NUM; ++nGrp) {
        for (AX_S32 nChn = 0; nChn < g_tIvpsGroupConfig[nGrp].nGrpChnNum; ++nChn) {
            /* Fill default src framerate */
            if (-1 == g_tIvpsGroupConfig[nGrp].arrFramerate[nChn][0]) {
                g_tIvpsGroupConfig[nGrp].arrFramerate[nChn][0] = pCameraInstance->GetSnsAttr().nFrameRate;
            }

            /* Fill default dest framerate */
            if (-1 == g_tIvpsGroupConfig[nGrp].arrFramerate[nChn][1]) {
                g_tIvpsGroupConfig[nGrp].arrFramerate[nChn][1] = pCameraInstance->GetSnsAttr().nFrameRate;
            }

            /* Fill default width */
            if (-1 == g_tIvpsGroupConfig[nGrp].arrOutResolution[nChn][0]) {
                g_tIvpsGroupConfig[nGrp].arrOutResolution[nChn][0] = m_tChnAttr.tChnAttr[nGrp].nWidth;
            }

            /* Fill default height */
            if (-1 == g_tIvpsGroupConfig[nGrp].arrOutResolution[nChn][1]) {
                g_tIvpsGroupConfig[nGrp].arrOutResolution[nChn][1] = m_tChnAttr.tChnAttr[nGrp].nHeight;
            }
        }
    }

    if (m_pDetectStage) {
        /* fixme: GROUP 1 is detect stage, hard code here */
        DETECT_STAGE_INFO_T tInfo;
        tInfo.nFrmWidth  = g_tIvpsGroupConfig[1].arrOutResolution[0][0];
        tInfo.nFrmHeight = g_tIvpsGroupConfig[1].arrOutResolution[0][1];
        tInfo.nVinFps    = g_tIvpsGroupConfig[1].arrFramerate[0][0];
        tInfo.nFrmFps    = g_tIvpsGroupConfig[1].arrFramerate[0][1];
        m_pDetectStage->ConfigStageInfo(tInfo);
    }

    return AX_TRUE;
}

AX_VOID CIVPSStage::AttrChangeNotify(AX_IVPS_ROTATION_E eOriRotation, AX_IVPS_ROTATION_E eNewRotation, AX_BOOL bMirror, AX_BOOL bFlip)
{
    LOG_M(IVPS, "+++ Rotation=%d, mirror=%d, flip=%d", eNewRotation, bMirror, bFlip);

    AX_U8 nIvpsChnIndex = 0;
    for (AX_U32 nGrp = 0; nGrp < IVPS_GROUP_NUM; ++nGrp) {
        if (IsIVPSFromISPLinkable(nGrp)) {
            LinkIVPSFromISP(nGrp, AX_FALSE);
        }

        for (AX_S32 nChn = 0; nChn < g_tIvpsGroupConfig[nGrp].nGrpChnNum; ++nChn) {
            const auto& endpointOptions = g_tEPOptions[nIvpsChnIndex];

            AX_IVPS_ENGINE_E eEngine = m_arrIvpsGrp[nGrp].tPipelineAttr.tFilter[nChn + 1][0].eEngine;

            if (gOptions.IsLinkMode()) {
                if (1 == g_tIvpsGroupConfig[nGrp].arrLinkModeFlag[nChn]) {
                    AX_MOD_INFO_S tPreMode;
                    memset(&tPreMode, 0, sizeof(AX_MOD_INFO_S));
                    AX_MOD_INFO_S tCurMode;
                    memset(&tCurMode, 0, sizeof(AX_MOD_INFO_S));

                    tPreMode.enModId = AX_ID_IVPS;
                    tPreMode.s32GrpId = nGrp;
                    tPreMode.s32ChnId = nChn;
                    tCurMode.enModId = (E_END_POINT_VENC == endpointOptions.eEPType ? AX_ID_VENC : AX_ID_JENC);
                    tCurMode.s32GrpId = 0;
                    tCurMode.s32ChnId = endpointOptions.nChannel;
                    AX_SYS_UnLink(&tPreMode, &tCurMode);

                    LOG_M(IVPS, "Release syslink: IVPS(Grp: %d, Chn %d) => %s(Chn %d)", tPreMode.s32GrpId, tPreMode.s32ChnId, "VENC", tCurMode.s32ChnId);
                }
            }

            if (AX_IVPS_ENGINE_TDP == eEngine || AX_IVPS_ENGINE_GDC == eEngine) {
                VIDEO_ATTR_T tAttr = CStageOptionHelper().GetInstance()->GetVideo(nIvpsChnIndex);

                AttrConvert(nGrp, nChn, eNewRotation, bMirror, tAttr);

                ChangeChnAttr(nGrp, nChn, eNewRotation, bMirror, bFlip, tAttr);

                if (E_END_POINT_VENC == endpointOptions.eEPType) {
                    m_pVecEncoders->at(endpointOptions.nInnerIndex)->Stop();
                    m_pVecEncoders->at(endpointOptions.nInnerIndex)->ResetByResolution(tAttr, AX_FALSE);
                }
                else if (E_END_POINT_JENC == endpointOptions.eEPType) {
                    JPEG_CONFIG_T tJencAttr;
                    tJencAttr.nWidth = tAttr.width;
                    tJencAttr.nHeight = tAttr.height;
                    tJencAttr.nStride = ALIGN_UP(tJencAttr.nWidth, g_tIvpsGroupConfig[nGrp].arrOutResolution[nChn][2]);
                    m_pVecEncoders->at(endpointOptions.nInnerIndex)->Stop();
                    m_pVecJecEncoders->at(endpointOptions.nInnerIndex)->ResetByResolution(tJencAttr, AX_FALSE);
                }
                else if (E_END_POINT_DET == endpointOptions.eEPType) {
                    if (m_pDetectStage) {
                        DETECT_STAGE_INFO_T tInfo;
                        tInfo.nFrmWidth  = tAttr.width;
                        tInfo.nFrmHeight = tAttr.height;
                        tInfo.nVinFps = g_tIvpsGroupConfig[nGrp].arrFramerate[nChn][0];
                        tInfo.nFrmFps = g_tIvpsGroupConfig[nGrp].arrFramerate[nChn][1];
                        m_pDetectStage->Reset(tInfo);
                    }
                }
            }

            if (gOptions.IsLinkMode()) {
                if (1 == g_tIvpsGroupConfig[nGrp].arrLinkModeFlag[nChn]) {
                    AX_MOD_INFO_S tPreMode;
                    memset(&tPreMode, 0, sizeof(AX_MOD_INFO_S));
                    AX_MOD_INFO_S tCurMode;
                    memset(&tCurMode, 0, sizeof(AX_MOD_INFO_S));

                    tPreMode.enModId = AX_ID_IVPS;
                    tPreMode.s32GrpId = nGrp;
                    tPreMode.s32ChnId = nChn;
                    tCurMode.enModId = (E_END_POINT_VENC == endpointOptions.eEPType ? AX_ID_VENC : AX_ID_JENC);
                    tCurMode.s32GrpId = 0;
                    tCurMode.s32ChnId = endpointOptions.nChannel;
                    AX_SYS_Link(&tPreMode, &tCurMode);

                    LOG_M(IVPS, "Setup syslink: IVPS(Grp: %d, Chn %d) => %s(Chn %d)", tPreMode.s32GrpId, tPreMode.s32ChnId, "VENC", tCurMode.s32ChnId);
                }
            }

            nIvpsChnIndex ++;
        }

        if (IsIVPSFromISPLinkable(nGrp)) {
            LinkIVPSFromISP(nGrp, AX_TRUE);
        }
    }

    LOG_M(IVPS, "--- Rotation=%d, mirror=%d, flip=%d", eNewRotation, bMirror, bFlip);
}

AX_BOOL CIVPSStage::ChangeChnAttr(IVPS_GRP nGrp, IVPS_CHN nChn, AX_IVPS_ROTATION_E eRotation, AX_BOOL bMirror, AX_BOOL bFlip, VIDEO_ATTR_T tNewAttr)
{
    AX_S32 nRet = AX_IVPS_SUCC;

    nRet = AX_IVPS_DisableChn(nGrp, nChn);
    if (AX_IVPS_SUCC != nRet) {
        LOG_M_E(IVPS, "AX_IVPS_DisableChn(Grp %d, Chn %d) failed, ret=0x%x", nGrp, nChn, nRet);
        return AX_FALSE;
    }

    AX_IVPS_PIPELINE_ATTR_S &tPipelineAttr = m_arrIvpsGrp[nGrp].tPipelineAttr;

    AX_U32 nChnFilter = nChn + 1;
    AX_U32 nStride = g_tIvpsGroupConfig[nGrp].arrOutResolution[nChn][2];
    tPipelineAttr.tFilter[nChnFilter][0].nDstPicHeight = tNewAttr.height;
    tPipelineAttr.tFilter[nChnFilter][0].nDstPicWidth = tNewAttr.width;
    tPipelineAttr.tFilter[nChnFilter][0].nDstPicStride = ALIGN_UP(tPipelineAttr.tFilter[nChnFilter][0].nDstPicWidth, nStride);

    LOG_M(IVPS, "IVPS change attr for rotation: rotation:%d, mirror:%d, flip:%d, w:%d, h:%d, s:%d"
        , eRotation
        , bMirror
        , bFlip
        , tPipelineAttr.tFilter[nChnFilter][0].nDstPicWidth
        , tPipelineAttr.tFilter[nChnFilter][0].nDstPicHeight
        , tPipelineAttr.tFilter[nChnFilter][0].nDstPicStride);

    if (AX_IVPS_ENGINE_TDP == m_arrIvpsGrp[nGrp].tPipelineAttr.tFilter[nChnFilter][0].eEngine) {
        tPipelineAttr.tFilter[nChnFilter][0].tTdpCfg.eRotation = eRotation;
        tPipelineAttr.tFilter[nChnFilter][0].tTdpCfg.bMirror = bMirror;
        tPipelineAttr.tFilter[nChnFilter][0].tTdpCfg.bFlip = bFlip;
    } else if (AX_IVPS_ENGINE_GDC == m_arrIvpsGrp[nGrp].tPipelineAttr.tFilter[nChnFilter][0].eEngine) {
        tPipelineAttr.tFilter[nChnFilter][0].tGdcCfg.eRotation = eRotation;
        tPipelineAttr.tFilter[nChnFilter][0].tGdcCfg.bMirror = bMirror;
        tPipelineAttr.tFilter[nChnFilter][0].tGdcCfg.bFlip = bFlip;
    }

    nRet = AX_IVPS_SetPipelineAttr(nGrp, &tPipelineAttr);
    if (AX_IVPS_SUCC != nRet) {
        LOG_M_E(IVPS, "AX_IVPS_SetPipelineAttr(Grp %d) failed, ret=0x%x", nGrp, nRet);
        return AX_FALSE;
    }

    nRet = AX_IVPS_EnableChn(nGrp, nChn);
    if (AX_IVPS_SUCC != nRet) {
        LOG_M_E(IVPS, "AX_IVPS_EnableChn(Grp %d, Chn %d) failed, ret=0x%x", nGrp, nChn, nRet);
        return AX_FALSE;
    }

    return AX_TRUE;
}

AX_U8 CIVPSStage::GetIvpsChnNum()
{
    AX_U8 nCount = 0;
    for (AX_U32 i = 0; i < IVPS_GROUP_NUM; ++i) {
        nCount += g_tIvpsGroupConfig[i].nGrpChnNum;
    }

    return nCount;
}

AX_U8 CIVPSStage::GetIspChnIndex(AX_U8 nChannel)
{
    switch (nChannel) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        default: {
            LOG_M_W(IVPS, "Channel index out of range.");
            return -1;
        }
    }

    return -1;
}

AX_U8 CIVPSStage::GetIvpsChnIndex(AX_U8 nChannel)
{
    switch (nChannel) {
        case 0: return 0;
        case 1: return 0;
        case 2: return 0;
        default: {
            LOG_M_W(IVPS, "Channel index out of range.");
            return -1;
        }
    }

    return -1;
}

AX_BOOL CIVPSStage::UpdateFramerate(AX_U32 nSnsFramerate)
{
    g_tIvpsGroupConfig[0].arrFramerate[0][0] = nSnsFramerate;
    g_tIvpsGroupConfig[0].arrFramerate[0][1] = nSnsFramerate;

    if (gOptions.IsActivedDetect()) {
        CStageOptionHelper *pStageOption = CStageOptionHelper().GetInstance();

        g_tIvpsGroupConfig[1].arrFramerate[0][0] = nSnsFramerate;
        g_tIvpsGroupConfig[1].arrFramerate[0][1] = pStageOption->GetAiAttr().tConfig.nDetectFps;
    }
    else {
        g_tIvpsGroupConfig[1].arrFramerate[0][0] = nSnsFramerate;
        g_tIvpsGroupConfig[1].arrFramerate[0][1] = nSnsFramerate/2;
    }

    g_tIvpsGroupConfig[2].arrFramerate[0][0] = nSnsFramerate;
    g_tIvpsGroupConfig[2].arrFramerate[0][1] = nSnsFramerate;

    AX_S32 nRet = AX_IVPS_SUCC;

    for (AX_U8 i = 0; i < IVPS_GROUP_NUM; i++) {
        IVPS_GRP_T tGrp = m_arrIvpsGrp[i];
        IVPS_GRP nGrp = tGrp.nIvpsGrp;

        for (AX_U8 chn = 0; chn < tGrp.tPipelineAttr.nOutChnNum; ++chn) {
            nRet = AX_IVPS_DisableChn(nGrp, chn);

            if (AX_IVPS_SUCC != nRet) {
                LOG_M_E(IVPS, "AX_IVPS_DisableChn(Grp: %d, Channel: %d) failed, ret=0x%x", nGrp, chn, nRet);
            }

            AX_IVPS_PIPELINE_ATTR_S &tPipelineAttr = m_arrIvpsGrp[nGrp].tPipelineAttr;

            AX_U8 nChnFilter = chn + 1;

            /* Framerate */
            tPipelineAttr.tFilter[nChnFilter][0].tFRC.nSrcFrameRate = g_tIvpsGroupConfig[nGrp].arrFramerate[chn][0];
            tPipelineAttr.tFilter[nChnFilter][0].tFRC.nDstFrameRate = g_tIvpsGroupConfig[nGrp].arrFramerate[chn][1];

            nRet = AX_IVPS_SetPipelineAttr(nGrp, &tPipelineAttr);
            if (AX_IVPS_SUCC != nRet) {
                LOG_M_E(IVPS, "AX_IVPS_SetPipelineAttr(Grp %d) failed, ret=0x%x", nGrp, nRet);
                return AX_FALSE;
            }

            nRet = AX_IVPS_EnableChn(nGrp, chn);
            if (AX_IVPS_SUCC != nRet) {
                LOG_M_E(IVPS, "AX_IVPS_EnableChn(Grp %d, Chn %d) failed, ret=0x%x", nGrp, chn, nRet);
                return AX_FALSE;
            }
        }
    }

    return AX_TRUE;
}

AX_BOOL CIVPSStage::IsOSDChannel(IVPS_GRP nGrp, IVPS_CHN nChn)
{
    if (!gOptions.IsEnableOSD()) {
        return AX_FALSE;
    }

    for (AX_U32 i = 0; i < OSD_ATTACH_NUM; i++) {
        OSD_ATTR_INFO* pAttr = &m_arrOsdAttr[i];

        if (nGrp == pAttr->nIvpsGrp && ((pAttr->nFilter >> 4) == (AX_U32)nChn + 1)) {
            return AX_TRUE;
        }
    }

    return AX_FALSE;
}

AX_BOOL CIVPSStage::StartOSD()
{
    AX_S32 nRet = AX_IVPS_SUCC;

    for (AX_U32 i = 0; i < OSD_ATTACH_NUM; i++) {
        OSD_ATTR_INFO* pAttr = &m_arrOsdAttr[i];
        IVPS_RGN_HANDLE hChnRgn = AX_IVPS_RGN_Create();     // 用户创建一个REGION
        if (AX_IVPS_INVALID_REGION_HANDLE != hChnRgn) {
            AX_U32 nIvpsGrp = pAttr->nIvpsGrp;
            AX_S32 nFilter = pAttr->nFilter;

            nRet = AX_IVPS_RGN_AttachToFilter(hChnRgn, nIvpsGrp, nFilter);  // 将Region绑定到IVPS Filter上
            if (AX_IVPS_SUCC != nRet) {
                LOG_M_E(IVPS, "[%d] AX_IVPS_RGN_AttachToFilter(Grp: %d, Filter: 0x%x, Handle: %d) failed, ret=0x%x", i, nIvpsGrp, nFilter, hChnRgn, nRet);
                return AX_FALSE;
            }

            pAttr->nHandle = hChnRgn;

            if (pAttr->bThreadUpdate) {
                m_arrRgnThreadParam[i].hChnRgn = hChnRgn;
                m_arrRgnThreadParam[i].nGroup  = nIvpsGrp;
                m_arrRgnThreadParam[i].nFilter = nFilter;

                m_arrRgnThread[i] = std::thread(&CIVPSStage::RgnThreadFunc, this, &m_arrRgnThreadParam[i]);
            } else {
                if (!SetOSD(pAttr)) {
                    LOG_M_E(IVPS, "Set OSD (Index: %d, Type: %d) failed.", i, pAttr->eOsdType);
                }
            }

            LOG_M(IVPS, "AX_IVPS_RGN_AttachToFilter(Grp: %d, Filter: 0x%x, Handle: %d) successfully.", nIvpsGrp, nFilter, hChnRgn);
        } else {
            LOG_M_E(IVPS, "AX_IVPS_RGN_Create(OSD index: %d) failed.", i);
            return AX_FALSE;
        }
    }

    return AX_TRUE;
}

AX_BOOL CIVPSStage::StopOSD()
{
    AX_S32 nRet = AX_IVPS_SUCC;
    for (AX_U32 i = 0; i < OSD_ATTACH_NUM; i++) {
        OSD_ATTR_INFO* pAttr = &m_arrOsdAttr[i];

        AX_S32 hChnRgn = pAttr->nHandle;
        AX_U32 nIvpsGrp = pAttr->nIvpsGrp;
        AX_S32 nFilter = pAttr->nFilter;
        nRet = AX_IVPS_RGN_DetachFromFilter(hChnRgn, nIvpsGrp, nFilter);
        if (AX_IVPS_SUCC != nRet) {
            LOG_M_E(IVPS, "AX_IVPS_RGN_DetachFromFilter(Grp: %d, Filter: %x, Handle: %d) failed, ret=0x%x", nIvpsGrp, nFilter, hChnRgn, nRet);
            return AX_FALSE;
        } else {
            LOG_M(IVPS, "AX_IVPS_RGN_DetachFromFilter(Grp: %d, Filter: %x, Handle: %d) successfully.", nIvpsGrp, nFilter, hChnRgn);
        }

        nRet = AX_IVPS_RGN_Destroy(hChnRgn);
        if (AX_IVPS_SUCC != nRet) {
            LOG_M_E(IVPS, "AX_IVPS_RGN_Destroy(Handle: %d) failed, ret=0x%x", hChnRgn, nRet);
            return AX_FALSE;
        } else {
            LOG_M(IVPS, "AX_IVPS_RGN_Destroy(Handle: %d) successfully.", hChnRgn, nRet);
        }
    }

    return AX_TRUE;
}

AX_BOOL CIVPSStage::SetOSD(OSD_ATTR_INFO* pOsdAttr)
{
    if (nullptr == pOsdAttr) {
        return AX_FALSE;
    }

    switch (pOsdAttr->eOsdType) {
        case OSD_TYPE_TIME: {
            LOG_M_W(IVPS, "Time OSD is supposed to update in thread.");
            break;
        };
        case OSD_TYPE_PICTURE: {
            if (0 == pOsdAttr->nIvpsGrp) {
                SetLogo(pOsdAttr->nIvpsGrp, pOsdAttr->nHandle, "./res/axera_logo_256x64.argb1555", 256, 64);
            } else if (2 == pOsdAttr->nIvpsGrp) {
                SetLogo(pOsdAttr->nIvpsGrp, pOsdAttr->nHandle, "./res/axera_logo_96x28.argb1555", 96, 28);
            }

            break;
        };
        case OSD_TYPE_STRING: {
            break;
        };
        case OSD_TYPE_PRIVACY: {
            break;
        };
        default: {
            LOG_M_E(IVPS, "Unknown OSD type: %d", pOsdAttr->eOsdType);
            break;
        }
    }

    return AX_TRUE;
}

AX_VOID CIVPSStage::RefreshOSD(AX_U32 nOsdIndex)
{
    OSD_ATTR_INFO* pAttr = &m_arrOsdAttr[nOsdIndex];
    SetOSD(pAttr);
}
