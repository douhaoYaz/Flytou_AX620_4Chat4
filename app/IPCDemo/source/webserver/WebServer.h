/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "global.h"
#include "AXRingBuffer.h"
#include <thread>
#include <mutex>

/* One stream to dispatch OD/MD events, one stream to capture snapshot */
#define MAX_WS_CONN_NUM  (MAX_VENC_CHANNEL_NUM + 2)
#define WS_EVENTS_CHANNEL (MAX_VENC_CHANNEL_NUM)
#define WS_SNAPSHOT_CHANNEL (MAX_VENC_CHANNEL_NUM + 1)

#define MAX_EVENTS_CHN_SIZE 256

typedef enum _WS_CHANNEL_TYPE_E{
    E_WS_CHANNEL_TYPE_VENC = 0,
    E_WS_CHANNEL_TYPE_JENC,
    E_WS_CHANNEL_TYPE_EVENTS,
    E_WS_CHANNEL_TYPE_SNAPSHOT,
    E_WS_CHANNEL_TYPE_MAX
} WS_CHANNEL_TYPE_E;

typedef enum _WEB_EVENTS_TYPE_E {
    E_WEB_EVENTS_TYPE_ReStartPreview = 0,
    E_WEB_EVENTS_TYPE_MD,
    E_WEB_EVENTS_TYPE_OD,
    E_WEB_EVENTS_TYPE_SCD,
    E_WEB_EVENTS_TYPE_MAX
} WEB_EVENTS_TYPE_E;

typedef struct _AI_EVENTS_MD_INFO_T {
    AX_CHAR szDisplay[64];
    AX_U32 nAreaID;
    AX_U64 nReserved;

    _AI_EVENTS_MD_INFO_T() {
        memset(this, 0, sizeof(_AI_EVENTS_MD_INFO_T));
    }
} AI_EVENTS_MD_INFO_T;

typedef struct _AI_EVENTS_OD_INFO_T {
    AX_CHAR szDisplay[64];
    AX_U32 nAreaID;
    AX_U64 nReserved;

    _AI_EVENTS_OD_INFO_T() {
        memset(this, 0, sizeof(_AI_EVENTS_OD_INFO_T));
    }
} AI_EVENTS_OD_INFO_T;

typedef struct _AI_EVENTS_SCD_INFO_T {
    AX_CHAR szDisplay[64];
    AX_U32 nAreaID;
    AX_U64 nReserved;

    _AI_EVENTS_SCD_INFO_T() {
        memset(this, 0, sizeof(*this));
    }
} AI_EVENTS_SCD_INFO_T;

typedef struct _WEB_EVENTS_DATA {
    WEB_EVENTS_TYPE_E eType;
    AX_U64 nTime;
    AX_U32 nReserved;
    union {
        AI_EVENTS_MD_INFO_T tMD;
        AI_EVENTS_OD_INFO_T tOD;
        AI_EVENTS_SCD_INFO_T tSCD;
    };

    _WEB_EVENTS_DATA() {
        memset(this, 0, sizeof(_WEB_EVENTS_DATA));
    }

} WEB_EVENTS_DATA_T, *WEB_EVENTS_DATA_PTR;

typedef enum _JPEG_TYPE_E {
    JPEG_TYPE_BODY = 0,
    JPEG_TYPE_VEHICLE,
    JPEG_TYPE_CYCLE,
    JPEG_TYPE_FACE,
    JPEG_TYPE_PLATE,
    JPEG_TYPE_BUTT
} JPEG_TYPE_E;

typedef struct FaceInfo {
    AX_BOOL bExist;
    AX_U8   nGender; /* 0-female, 1-male */
    AX_U8   nAge;
    AX_CHAR szMask[32];
    AX_CHAR szInfo[32];
} FaceInfo;

typedef struct PlateInfo {
    AX_BOOL bExist;
    AX_BOOL bValid;
    AX_CHAR szNumVal[16];
    AX_CHAR szNum[16];
    AX_CHAR szColor[32];
} PlateInfo;

typedef struct JpegDataInfo {
    JPEG_TYPE_E eType; /* JPEG_TYPE_E */
    union
    {
        FaceInfo  tFaceInfo;
        PlateInfo tPlateInfo;
    };

    JpegDataInfo() {
        memset(this, 0, sizeof(JpegDataInfo));
    }
} JpegDataInfo;

typedef struct _JpegHead {
    AX_U32  nMagic;
    AX_U32  nTotalLen;
    AX_U32  nTag;
    AX_U32  nJsonLen;
    AX_CHAR szJsonData[256];

    _JpegHead() {
        nMagic = 0x54495841; // "AXIT" by default
        nTotalLen = 0;
        nTag = 0x4E4F534A;   // "JSON" by default
        nJsonLen = 0;
        memset(szJsonData, 0x0, sizeof(szJsonData));
    }

} JpegHead;


class CWebServer
{
public:
    CWebServer(AX_VOID);
    ~CWebServer(AX_VOID);

public:
    AX_VOID Init(const AX_VIN_CHN_ATTR_T& tAttr);
    AX_BOOL Start();
    AX_BOOL Stop();
    AX_VOID StopAction();
    AX_VOID SendPreviewData(AX_U8 nStreamID, AX_VOID* data, AX_U32 size, AX_U64 nPts=0, AX_BOOL bIFrame=AX_FALSE);
    AX_VOID SendCaptureData(AX_U8 nStreamID, AX_VOID* data, AX_U32 size, AX_U64 nPts=0, AX_BOOL bIFrame=AX_TRUE,
                            JpegDataInfo* pJpegInfo = nullptr);
    AX_VOID SendSnapshotData(AX_U8 nStreamID, AX_VOID* data, AX_U32 size);
    AX_VOID SendEventsData(WEB_EVENTS_DATA_T* data);
    AX_BOOL IsJencChannel(AX_U8 nStreamID);

    // AX_BOOL DispatchAIEvent(AX_U8 nEventType, AX_VOID* data);
    WS_CHANNEL_TYPE_E GetChnType(AX_U8 nStreamID);
    std::string FormatEventsJson(WEB_EVENTS_DATA_T* pEvent);

    AX_S32  GetWSChn(void* conn);
    AX_VOID UpdateConnStatus();

    AX_VOID RestartPreview();
    AX_VOID SendWSData();

private:
    static void* WebServerThreadFunc(void* pThis);
    static void* SendDataThreadFunc(void* pThis);

private:
    typedef struct ChannelData {
        CAXRingBuffer* pRingBuffer{nullptr};
        AX_U8 nChannel{(AX_U8)-1};
        AX_U8 nInnerIndex{(AX_U8)-1};
    } ChannelData;

    ChannelData m_arrChannelData[MAX_WS_CONN_NUM];
    AX_BOOL m_arrConnStatus[MAX_WS_CONN_NUM]{AX_FALSE};

    AX_BOOL m_bServerStarted{AX_FALSE};
    std::thread* m_pAppwebThread{nullptr};
    std::thread* m_pSendDataThread{nullptr};

    std::mutex m_mtxConnStatus;
    std::mutex m_mtxWSData;
};

#endif // WEB_SERVER_H