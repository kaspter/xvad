cmake_minimum_required(VERSION 3.16)

if(CMAKE_TOOLCHAIN_FILE)
    get_filename_component(CMAKE_TOOLCHAIN_FILE_NAME ${CMAKE_TOOLCHAIN_FILE} NAME)
    find_file(CMAKE_TOOLCHAIN_FILE ${CMAKE_TOOLCHAIN_FILE_NAME} PATHS ${CMAKE_SOURCE_DIR} NO_DEFAULT_PATH)
    message(STATUS "CMAKE_TOOLCHAIN_FILE = ${CMAKE_TOOLCHAIN_FILE}")
endif()

project(xvad C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-O3 -Wall -Wextra -fPIC")

# 编译选项
option(USE_MNN "Enable GNN inference backend" OFF)

# FetchContent 配置
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
get_filename_component(third_party "third_party" REALPATH BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(FETCHCONTENT_BASE_DIR ${third_party})

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# 头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)

# 源码文件
file(GLOB SRC ${PROJECT_SOURCE_DIR}/src/*.c)
file(GLOB UTILS_SRC ${PROJECT_SOURCE_DIR}/src/utils/*.c)
file(GLOB WEBRTC_SRC ${PROJECT_SOURCE_DIR}/src/backends/webrtc/*.c)
file(GLOB TEN_VAD_SRC ${PROJECT_SOURCE_DIR}/src/backends/ten/*.c)
file(GLOB SILERO_SRC ${PROJECT_SOURCE_DIR}/src/backends/silero/*.c)
file(GLOB FIRERED_SRC ${PROJECT_SOURCE_DIR}/src/backends/firered/*.c)
file(GLOB RNNOISE_SRC ${PROJECT_SOURCE_DIR}/third_party/rnnoise/*.c)

# 编译静态库
add_library(xvad STATIC
    ${SRC}
    ${UTILS_SRC}
    ${WEBRTC_SRC}
    ${TEN_VAD_SRC}
    ${SILERO_SRC}
    ${FIRERED_SRC}
    ${RNNOISE_SRC}
)

# NEON FFT 支持
if(USE_NEON_FFT)
    include(neon_fft)
    target_compile_definitions(xvad PRIVATE USE_NEON_FFT)
endif()

# 系统库链接
target_link_libraries(xvad PRIVATE m pthread dl)

install(DIRECTORY include/ DESTINATION include)
