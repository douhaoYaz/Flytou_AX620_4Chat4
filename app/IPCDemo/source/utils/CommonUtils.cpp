/**********************************************************************************
 *
 * Copyright (c) 2019-2020 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/

#include "CommonUtils.h"
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <dirent.h>

CCommonUtils::CCommonUtils(void)
{

}

CCommonUtils::~CCommonUtils(void)
{

}

AX_U32 CCommonUtils::CalcImgSize(AX_U32 nW, AX_U32 nH, AX_IMG_FORMAT_E eType, AX_U32 nStride, AX_U32 nAlign)
{
    AX_U32 nBpp = 0;
    if (nW == 0 || nH == 0) {
        LOG_M_E("CommonUtil", "Invalid width %d or height %d!", nW, nH);
        return 0;
    }

    if (0 == nStride) {
        nStride = (0 == nAlign) ? nW : ALIGN_UP(nW, nAlign);
    } else {
        if (nAlign > 0) {
            if (nStride % nAlign) {
                LOG_M_E("CommonUtil", "stride: %u not %u aligned.!", __func__, nStride, nAlign);
                return 0;
            }
        }
    }

    switch(eType) {
        case AX_YUV420_PLANAR:
        case AX_YUV420_SEMIPLANAR:
        case AX_YUV420_SEMIPLANAR_VU:
            nBpp = 12;
            break;
        case AX_YUV422_INTERLEAVED_YUYV:
        case AX_YUV422_INTERLEAVED_UYVY:
            nBpp = 16;
            break;
        case AX_YUV444_PACKED:
        case AX_FORMAT_RGB888:
        case AX_FORMAT_BGR888:
            nBpp = 24;
            break;
        case AX_FORMAT_ARGB8888:
            nBpp = 32;
            break;
        default:
            nBpp = 8;
            break;
    }

    return nStride * nH * nBpp / 8;
}

AX_BOOL CCommonUtils::GetIP(AX_CHAR* pOutIPAddress, AX_U32 nLen)
{
    const std::vector<std::string> vNetType{"eth", "usb"};
    for (size_t i = 0; i < vNetType.size(); i++) {
        for (char c = '0'; c <= '9'; ++c) {
            string strDevice = vNetType[i] + c;
            int fd;
            int ret;
            struct ifreq ifr;
            fd = socket(AF_INET, SOCK_DGRAM, 0);
            strcpy(ifr.ifr_name, strDevice.c_str());
            ret = ioctl(fd, SIOCGIFADDR, &ifr);
            ::close(fd);
            if (ret < 0) {
                continue;
            }

            char* pIP = inet_ntoa(((struct sockaddr_in*)&(ifr.ifr_addr))->sin_addr);
            if (pIP) {
                strncpy((char *)pOutIPAddress, pIP, nLen - 1);
                pOutIPAddress[nLen - 1] = '\0';
                return AX_TRUE;
            }
        }
    }
    return AX_FALSE;
}

AX_BOOL CCommonUtils::LoadImage(const AX_CHAR *pszImge, AX_U64 *pPhyAddr, AX_VOID **ppVirAddr, AX_U32 nImgSize)
{
    if (nullptr == pszImge || 0 == nImgSize) {
        return AX_FALSE;
    }

    FILE* fp = fopen(pszImge, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        AX_U32 nFileSize = ftell(fp);
        if (nImgSize != nFileSize) {
             LOG_M_E("CommonUtil", "file size not right, %d != %d", nImgSize, nFileSize);
             fclose(fp);
             return AX_FALSE;
        }
        fseek(fp, 0, SEEK_SET);
        AX_S32 ret = AX_SYS_MemAlloc(pPhyAddr, ppVirAddr, nFileSize, 128, NULL);
        if (0 != ret) {
            LOG_M_E("CommonUtil", "AX_SYS_MemAlloc fail, ret=0x%x", ret);
            fclose(fp);
            return AX_FALSE;
        }
        if (fread(*ppVirAddr, 1, nFileSize, fp) != nFileSize) {
            LOG_M_E("CommonUtil", "fread fail, %s", strerror(errno));
            fclose(fp);
            return AX_FALSE;
        }

        fclose(fp);
        return AX_TRUE;
    } else {
        LOG_M_E("CommonUtil", "fopen %s fail, %s", pszImge,  strerror(errno));
        return AX_FALSE;
    }
}

AX_S32 CCommonUtils::CreateImage(AX_VIDEO_FRAME_S *pImage, AX_U32 nW, AX_U32 nH, AX_U32 nStride)
{
    AX_S32 ret = 0;
    pImage->u32Width = nW;
    pImage->u32Height = nH;
    pImage->u32PicStride[0] = nStride;
    pImage->enImgFormat = AX_YUV420_SEMIPLANAR;
    AX_U32 nDstImgSize = CalcImgSize(pImage->u32Width, pImage->u32Height, pImage->enImgFormat, pImage->u32PicStride[0], 0);
    ret = AX_SYS_MemAlloc(&(pImage->u64PhyAddr[0]), (AX_VOID **)&(pImage->u64VirAddr[0]), nDstImgSize, 128, NULL);
    if (0 != ret) {
        LOG_M_E("CommonUtil", "AX_SYS_MemAlloc fail, ret=0x%x", ret);
        return -1;
    } else {
        memset((AX_VOID *)pImage->u64VirAddr[0], 0x80, nDstImgSize);
    }

    return 0;
}

// AX_BOOL CCommonUtils::DumpAiKitImage(const AX_AI_KIT_Image *pImage, std::string strOutPath)
// {
//     FILE* pFile = fopen(strOutPath.c_str(), "wb");
//     if (pFile) {
//         AX_U32 nWrite = fwrite(pImage->pVir, 1, pImage->nSize, pFile);
//         fclose(pFile);

//         return nWrite == pImage->nSize ? AX_TRUE : AX_FALSE;
//     }

//     return AX_FALSE;
// }

AX_BOOL CCommonUtils::SaveAsBin(std::string strPath, AX_VOID* pData, AX_U32 nSize, AX_BOOL bAppend /*= AX_FALSE*/)
{
    if (nullptr == pData || 0 == nSize) {
        return AX_FALSE;
    }

    FILE* pFile = nullptr;
    if (bAppend) {
        pFile = fopen(strPath.c_str(), "wb+");
    } else {
        pFile = fopen(strPath.c_str(), "wb");
    }

    if (pFile) {
        AX_U32 nWriteCount = fwrite(pData, 1, nSize, pFile);
        fclose(pFile);
        return nWriteCount == nSize ? AX_TRUE : AX_FALSE;
    }

    return AX_FALSE;
}

AX_BOOL CCommonUtils::SaveAsBin(FILE* pFile, AX_VOID* pData, AX_U32 nSize, AX_BOOL bAppend /*= AX_FALSE*/)
{
    if (nullptr == pFile || nullptr == pData || 0 == nSize) {
        return AX_FALSE;
    }

    AX_U32 nWriteCount = fwrite(pData, 1, nSize, pFile);

    return nWriteCount == nSize ? AX_TRUE : AX_FALSE;
}

AX_BOOL CCommonUtils::FrameSkipCtrl(AX_S32 nSrcFrameRate, AX_S32 nDstFrameRate, AX_U32 nFrameSeqNum) {
    if (nFrameSeqNum < 1) {
        nFrameSeqNum = 1;
    }

    if (nSrcFrameRate == nDstFrameRate) {
        return AX_FALSE;
    }

    if (nDstFrameRate > nSrcFrameRate) {
        return AX_FALSE;
    }

    if ((nFrameSeqNum * nDstFrameRate / (nSrcFrameRate)) > ((nFrameSeqNum - 1) * nDstFrameRate / (nSrcFrameRate))) {
        return AX_FALSE;
    } else {
        return AX_TRUE;
    }
}

AX_U32 CCommonUtils::CalOsdOffsetX(AX_S32 nWidth, AX_S32 nOsdWidth, AX_S32 nXMargin, OSD_ALIGN_TYPE_E eAlign)
{
    AX_S32 Offset = 0;
    if (OSD_ALIGN_TYPE_LEFT_TOP == eAlign || OSD_ALIGN_TYPE_LEFT_BOTTOM == eAlign) {
        if (nWidth < nOsdWidth) {
            Offset = nXMargin;
        } else {
            if (nWidth - nOsdWidth > nXMargin) {
                Offset = nXMargin;
            } else {
                Offset = nWidth - nOsdWidth;
            }
        }
    } else if (OSD_ALIGN_TYPE_RIGHT_TOP == eAlign || OSD_ALIGN_TYPE_RIGHT_BOTTOM == eAlign) {
        if (nWidth < nOsdWidth) {
            Offset = 0;
        } else {
            if (nWidth - nOsdWidth > nXMargin) {
                Offset = nWidth - (nOsdWidth + nXMargin);
            } else {
                return 0;
            }
        }
    }

    Offset = ALIGN_UP(Offset, OSD_ALIGN_X_OFFSET);

    return Offset;
};

AX_U32 CCommonUtils::CalOsdOffsetY(AX_S32 nHeight, AX_S32 nOSDHeight, AX_S32 nYMargin, OSD_ALIGN_TYPE_E eAlign)
{
    AX_S32 Offset = 0;
    if (OSD_ALIGN_TYPE_LEFT_TOP == eAlign || OSD_ALIGN_TYPE_RIGHT_TOP == eAlign) {
        if (nHeight < nOSDHeight) {
            Offset = nYMargin;
        } else {
            if (nHeight - nOSDHeight > nYMargin) {
                Offset = nYMargin;
            } else {
                Offset = nHeight - nOSDHeight;
            }
        }
    } else if (OSD_ALIGN_TYPE_LEFT_BOTTOM == eAlign || OSD_ALIGN_TYPE_RIGHT_BOTTOM == eAlign) {
        if (nHeight < nOSDHeight) {
            Offset = 0;
        } else {
            if (nHeight - nOSDHeight > nYMargin) {
                Offset = nHeight - (nOSDHeight + nYMargin);
            } else {
                return 0;
            }
        }
    }
    Offset = ALIGN_UP(Offset, OSD_ALIGN_Y_OFFSET);

    return Offset;
};

string CCommonUtils::GetBinFile(std::string strBinRoot, string strSnsName, AX_SNS_HDR_MODE_E eSnsMode, string strLastTag)
{
    string strBinFile = "";

    if (strBinRoot.empty() || strSnsName.empty()) {
        LOG_M_E("CommonUtil", "strBinRoot(%s) or strSnsName(%s) is empty!", strBinRoot.c_str(), strSnsName.c_str());
        return strBinFile;
    }

    string strSnsMode = "";
    string strRoot = strBinRoot;

    if (strRoot[strRoot.length() - 1] != '/') {
        strRoot += '/';
    }

    switch(eSnsMode) {
        case AX_SNS_LINEAR_MODE:
            strSnsMode = "sdr";
            break;
        case AX_SNS_HDR_2X_MODE:
            strSnsMode = "hdr";
            break;
        default:
            LOG_M_E("CommonUtil", "eSnsMode(%d) is not support!", eSnsMode);
            return strBinFile;
    }

    string strBinNamePattern = strSnsName + "_" + strSnsMode + "_" + strLastTag + ".bin";
    LOG_M("CommonUtil", "Bin file pattern: %s", strBinNamePattern.c_str());

    DIR* dp = nullptr;
    struct dirent *dirp;

    if ((dp = opendir(strRoot.c_str())) == NULL) {
        LOG_M_E("CommonUtil", "Can not open bin root: %s", strRoot.c_str());
        return strBinFile;
    }

    regex reg_obj(strBinNamePattern);
    while ((dirp = readdir(dp)) != NULL)
    {
        if (dirp->d_type == 8) {
            // 4 means catalog; 8 means file; 0 means unknown
            if (regex_match(dirp->d_name, reg_obj)) {
                strBinFile = strRoot + dirp->d_name;
                break;
            }
        }
    }

    closedir(dp);

    return strBinFile;
}
