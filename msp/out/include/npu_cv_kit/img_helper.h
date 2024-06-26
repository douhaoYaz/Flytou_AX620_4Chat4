#ifndef _NPU_CV_IMG_HELPER_H_
#define _NPU_CV_IMG_HELPER_H_
#include "ax_base_type.h"
#include "ax_global_type.h"
#include <string>
#include <vector>
#include "ax_interpreter_external_api.h"
#include "npu_common.h"
#include "drc_callbacks.h"

#ifdef __cplusplus
extern "C" {
#endif

AX_NPU_SDK_EX_ATTR_T get_npu_hard_mode(const std::string& mode);

AX_NPU_SDK_EX_MODEL_TYPE_T get_npu_mode_type(const std::string& mode_type);

uint32_t get_image_stride_w(const AX_NPU_CV_Image* pImg);

void load_file(const std::string& path, void** vir_addr, AX_U64 *phy_addr, long *file_size);

void release_image_buffer(AX_NPU_CV_Image * img);

int get_image_data_size(const AX_NPU_CV_Image* img);

AX_NPU_CV_DataType get_data_type(const std::string& data_type);

std::string get_data_type_name(AX_NPU_CV_DataType type);

AX_NPU_CV_FrameDataType get_color_space(const std::string& color_space);

std::vector<std::string> split_string(const std::string& content, const std::string delimitter);

int get_data_bit_width(const AX_NPU_CV_DataType eDtype);

AX_NPU_CV_Image * create_img_by_size(AX_U32 h, AX_U32 w, AX_NPU_CV_FrameDataType type);

AX_NPU_CV_Matrix2D create_matrix_by_size(AX_U32 nRow, AX_U32 nColumn,AX_NPU_CV_DataType type);

AX_NPU_CV_Matrix3D create_matrix_by_size_3d(AX_U32 N, AX_U32 nRow, AX_U32 nColumn, AX_NPU_CV_DataType type);

AX_NPU_CV_Matrix2D create_matrix_from_file(const std::string &path, int nRow, int nColumn, AX_NPU_CV_DataType type);

AX_NPU_CV_Matrix3D create_matrix_from_file_3d(const std::string &path, int N, int nRow, int nColumn, AX_NPU_CV_DataType type);

void release_matrix_memory(AX_NPU_CV_Matrix2D& matrix);

void release_matrix_memory_3d(AX_NPU_CV_Matrix3D& matrix);

void release_image_memory(AX_NPU_CV_Image * img);

void dump_matrix_to_file(const std::string& path, const AX_NPU_CV_Matrix2D& matrix_2d);

void dump_matrix_to_file_3d(const std::string& path, const AX_NPU_CV_Matrix3D& matrix_3d);

void dump_image_to_file(const std::string& path, AX_NPU_CV_Image* img);

void dump_mem_to_file(const std::string& path, void * ptr, int size);

void matrix_binary(AX_NPU_CV_Matrix2D * mat);

AX_NPU_CV_Matrix2D create_random_matrix(AX_U32 nRow, AX_U32 nColumn,AX_NPU_CV_DataType type, float x1=-1, float x2=1);

AX_NPU_CV_Matrix3D create_random_matrix_3d(AX_U32 N, AX_U32 nRow, AX_U32 nColumn,AX_NPU_CV_DataType type, float x1=-1, float x2=1);

AX_NPU_CV_Matrix2D create_matrix_universal(const std::string &path, int nRow, int nColumn, AX_NPU_CV_DataType type, float x1=-1, float x2=1);

AX_NPU_CV_Matrix3D create_matrix_universal_3d(const std::string &path, int N, int nRow, int nColumn, AX_NPU_CV_DataType type, float x1=-1, float x2=1);

void transposeMat(AX_NPU_CV_Matrix2D& matrix);

AX_NPU_SDK_EX_WORK_CYCLE_T get_current_work_cycle();

typedef AX_NPU_SDK_EX_WORK_CYCLE_T eu_work_cycle_info_t;

void print_work_cycle(eu_work_cycle_info_t result);

void print_matrix(AX_NPU_CV_Matrix2D& mat);

AX_NPU_CV_Matrix2D copy_from_matrix_3d(AX_NPU_CV_Matrix3D&mat, AX_U32 i);

void print_matrix_3d(AX_NPU_CV_Matrix3D& mat);

bool matrix_cmp(AX_NPU_CV_Matrix2D& mat1, AX_NPU_CV_Matrix2D& mat2);

bool matrix_cmp_3d(AX_NPU_CV_Matrix3D& mat1, AX_NPU_CV_Matrix3D& mat2);

void matrx_stats(AX_NPU_CV_Matrix2D& src, AX_NPU_CV_Matrix2D& dst, std::string method);

void matrix_transpose(AX_NPU_CV_Matrix2D& src);

void matrix_transpose_3d(AX_NPU_CV_Matrix3D& src);

void matrix_map(AX_NPU_CV_Matrix2D& src, AX_NPU_CV_Matrix2D& dst, float(*pFun)(float));

#ifdef __cplusplus
}
#endif

#endif