#ifndef _NPU_CV_DRC_CALLBACKS_H_
#define _NPU_CV_DRC_CALLBACKS_H_
#include "ax_base_type.h"
#include "ax_global_type.h"
#include <string>
#include <vector>
#include "ax_interpreter_external_api.h"
#include "npu_common.h"

#ifdef __cplusplus
extern "C" {
#endif

float sigmoid(float x);

float rgb2lab_call_back(float x);

float gamma_call_back(float x);

#ifdef __cplusplus
}
#endif

#endif