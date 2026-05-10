FireRed VAD



1. 下载官方 FireRedVAD 流式模型

# ModelScope 下载（国内推荐）
pip install -U modelscope
modelscope download --model FireRedTeam/FireRedVAD --local_dir ./pretrained_models/FireRedVAD

# 提取流式 ONNX 模型
cp ./pretrained_models/FireRedVAD/VAD/streaming.onnx ./firered_vad_streaming.onnx


2. 转换为 MNN FP32 模型
MNNConvert -f ONNX \
MNNConvert -f ONNX \
  --modelFile firered_vad_streaming.onnx \
  --MNNModel firered_vad_fp32.mnn \
  --bizCode XVAD \
  --doCompressOnnxConstants
  

3. 转换为 MNN INT8 量化模型（推荐）
# 准备校准数据（10分钟左右的16kHz单声道PCM音频）
MNNConvert -f ONNX \
  --modelFile firered_vad_streaming.onnx \
  --MNNModel firered_vad_int8.mnn \
  --bizCode XVAD \
  --quantize INT8 \
  --weightQuantizeBits 8 \
  --calibrationPath ./calib_data/ \
  --calibrationTable calibration.txt

4. 放置模型文件
将转换好的 firered_vad_int8.mnn 放到 xvad/model/ 目录下
