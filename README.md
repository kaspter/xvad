# xvad
**高性能、跨平台、多后端统一语音活动检测库**

## 特性
- **极致性能**：基于libgnn统一推理引擎，比原生ONNX Runtime快2-3倍
- **多后端支持**：Silero VAD、TEN-VAD、FireRedVAD、WebRTC VAD
- **零依赖**：所有依赖自动下载编译，无需手动安装
- **纯C API**：兼容C/C++/Android JNI/嵌入式等所有场景
- **实时优化**：专为实时音频处理设计，低延迟、低内存占用

## 支持的VAD算法
| 后端 | 精度(F1) | 单帧延迟 | RTF | 模型大小 |
|------|----------|----------|-----|----------|
| WebRTC | 0.895 | 2.7µs | 0.0001 | 0KB |
| Silero VAD v6 | 0.938 | 0.21ms | 0.0037 | 1.2MB |
| TEN-VAD | 0.928 | 0.12ms | 0.0039 | 800KB |
| FireRedVAD | 0.913 | 0.45ms | 0.0543 | 2.5MB |

## 快速开始
### 编译
```bash
git clone --recursive https://github.com/kaspter/xvad.git
cd xvad
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
