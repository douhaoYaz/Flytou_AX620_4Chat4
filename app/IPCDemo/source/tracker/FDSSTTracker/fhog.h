/*
 * @Description: 
 * @Author: vincent_xjw@163.com
 * @Date: 2023-11-28 16:33:46
 * @LastEditTime: 2023-12-07 13:39:58
 * @LastEditors: vincent_xjw@163.com
 */
#ifndef FHOG_H
#define FHOG_H

#include <cstdlib>
#include <cmath>
#include <cstring>
#include <memory>
#include "sse.hpp"

#include <opencv2/core/core.hpp>

/**
    Inputs:
        float* I        - a gray or color image matrix with shape = channel x width x height
        int *h, *w, *d  - return the size of the returned hog features
        int binSize     -[8] spatial bin size
        int nOrients    -[9] number of orientation bins
        float clip      -[.2] value at which to clip histogram bins
        bool crop       -[false] if true crop boundaries

    Return:
        float* H        - computed hog features with shape: (nOrients*3+5) x (w/binSize) x (h/binSize), if not crop

    Author:
        Sophia
    Date:
        2015-01-15
**/

float* fhog(float* I,int height,int width,int channel,int *h,int *w,int *d,int binSize = 4,int nOrients = 9,float clip=0.2f,bool crop = false);
cv::Mat fhog(const cv::Mat& input, int binSize = 4,int nOrients = 9,float clip=0.2f,bool crop = false);

void change_format(float *des,float *source,int height,int width,int channel);

// wrapper functions if compiling from C/C++
inline void wrError(const char *errormsg) { throw errormsg; }
inline void* wrCalloc( size_t num, size_t size ) { return calloc(num,size); }
inline void* wrMalloc( size_t size ) { return malloc(size); }
inline void wrFree( void * ptr ) { free(ptr); }

#endif
