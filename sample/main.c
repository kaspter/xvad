#include <stdio.h>
#include <stdlib.h>
#include "xvad.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage: %s input.pcm output.txt\n", argv[0]);
        return -1;
    }
    
    // 1. 创建任意-VAD实例
    xvad_handle_t* vad;
    xvad_error_t err = xvad_create(&vad, XVAD_BACKEND_TEN_VAD, NULL);
    if (err != XVAD_OK) {
        printf("VAD create failed: %d\n", err);
        return -1;
    }

    // 2. 创建帧适配器
    xvad_frame_adapter_t* adapter;
    err = xvad_frame_adapter_create(&adapter, vad);
    if (err != XVAD_OK) {
        printf("Frame adapter create failed: %d\n", err);
        xvad_destroy(vad);
        return -1;
    }

    // 3. 创建预处理器（使用原始麦克风预设）
    xvad_preprocessor_t* preprocessor;
    err = xvad_preprocessor_create(&preprocessor, &XVAD_PREPROCESSOR_RAW_MIC, 16000);
    if (err != XVAD_OK) {
        printf("Preprocessor create failed: %d\n", err);
        xvad_frame_adapter_destroy(adapter);
        xvad_destroy(vad);
        return -1;
    }
    
    // 4. 打开文件
    FILE* in_file = fopen(argv[1], "rb");
    FILE* out_file = fopen(argv[2], "w");
    if (!in_file || !out_file) {
        printf("File open failed\n");
        return -1;
    }
    
    // 5. 处理音频
    int16_t chunk[1024];
    float results[10];
    int frame_idx = 0;
    
    fprintf(out_file, "frame,probability\n");
    
    while (1) {
        size_t read = fread(chunk, sizeof(int16_t), 1024, in_file);
        if (read == 0) break;
        
        // 预处理 （去直流、降噪、归一化）
        int16_t cleaned[1024];
        xvad_preprocessor_process(preprocessor, chunk, cleaned, read);
        
        // 处理 (VAD检测)
        size_t frames = xvad_frame_adapter_process(adapter, cleaned, read, results, 10);
        
        // 写入结果
        for (size_t i = 0; i < frames; i++) {
            fprintf(out_file, "%d,%.3f\n", frame_idx++, results[i]);
        }
    }
    
    // 6. 清理
    fclose(in_file);
    fclose(out_file);
    
    xvad_preprocessor_destroy(preprocessor);
    xvad_frame_adapter_destroy(adapter);
    xvad_destroy(vad);
    
    return 0;
}
