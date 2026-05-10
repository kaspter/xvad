Silero VAD



1. 下载官方 Silero VAD v6.2.1 模型

wget https://github.com/snakers4/silero-vad/raw/v6.2.1/src/silero_vad/data/silero_vad.onnx


2. 转换为 MNN FP32 模型
MNNConvert -f ONNX \
  --modelFile silero_vad.onnx \
  --MNNModel silero_vad_fp32.mnn \
  --bizCode XVAD \
  --doCompressOnnxConstants
  

3. 转换为 MNN INT8 量化模型（推荐，体积↓75%，速度↑40%）
# 准备校准数据（10分钟左右的16kHz单声道PCM音频）
MNNConvert -f ONNX \
  --modelFile silero_vad.onnx \
  --MNNModel silero_vad_int8.mnn \
  --bizCode XVAD \
  --quantize INT8 \
  --weightQuantizeBits 8 \
  --calibrationPath ./calib_data/ \
  --calibrationTable calibration.txt

4. 放置模型文件
将转换好的 silero_vad_int8.mnn 放到 xvad/model/ 目录下
