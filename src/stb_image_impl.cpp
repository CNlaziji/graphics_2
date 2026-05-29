/**
 * @file stb_image_impl.cpp
 * @brief stb_image 单文件库的实现编译单元
 * 
 * 在整个工程中，STB_IMAGE_IMPLEMENTATION 只能定义一次。
 * 将其隔离在此独立的 .cpp 文件中，避免多重定义链接错误。
 * 
 * @author 成员 C
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"   // include/ 目录已在 CMakeLists 中加入搜索路径
