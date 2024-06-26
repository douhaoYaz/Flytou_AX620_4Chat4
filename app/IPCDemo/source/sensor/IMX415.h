/**********************************************************************************
 *
 * Copyright (c) 2019-2023 Beijing AXera Technology Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Beijing AXera Technology Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Beijing AXera Technology Co., Ltd.
 *
 **********************************************************************************/

#pragma once

#include "BaseSensor.h"

class CIMX415 : public CBaseSensor
{
public:
    CIMX415(SENSOR_CONFIG_T tSensorConfig);
    virtual ~CIMX415(AX_VOID);

protected:
    virtual AX_VOID InitSnsAttr();
    virtual AX_VOID InitDevAttr();
    virtual AX_VOID InitPipeAttr();
    virtual AX_VOID InitMipiRxAttr();
    virtual AX_VOID InitChnAttr();
    virtual AX_S32  RegisterSensor(AX_U8 nPipe);
    virtual AX_S32  UnRegisterSensor(AX_U8 nPipe);
    virtual AX_S32  RegisterAwbAlgLib(AX_U8 nPipe);
    virtual AX_S32  UnRegisterAwbAlgLib(AX_U8 nPipe);
    virtual AX_S32  SetFps(AX_U8 nPipe, AX_F32 fFrameRate);
    virtual AX_BOOL HdrModeSupport(AX_U8 nPipe);
};
