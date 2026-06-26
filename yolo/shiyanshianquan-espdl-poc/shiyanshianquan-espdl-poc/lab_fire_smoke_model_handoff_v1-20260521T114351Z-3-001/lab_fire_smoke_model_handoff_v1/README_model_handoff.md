# Lab Fire/Smoke Model Handoff V1

## 1. 模型说明

本模型用于实验室场景下的明火和烟雾目标检测。

当前模型：
- YOLOv8n
- 输入尺寸：320 x 320
- 类别数：2

类别映射：
- 0: fire
- 1: smoke

## 2. 文件说明

- model/best.pt：Ultralytics YOLO 训练权重，用于 PC 端测试和继续导出
- model/best.onnx：ONNX 中间模型，用于后续 ESP-DL / ESP-Detection 转换
- model/data.yaml：类别定义文件
- metrics/results.png：训练过程指标图
- metrics/confusion_matrix.png：混淆矩阵
- predict_samples/：预测效果样例图

说明：
PR_curve.png、F1_curve.png、P_curve.png、R_curve.png 如果没有生成，不影响板端系统接入。

## 3. 板端接入建议

ESP32-P4 不能直接运行 .pt 文件。

推荐后续路线：
best.pt
→ best.onnx
→ ESP-PPQ / ESP-DL 量化
→ .espdl
→ ESP32-P4 本地推理

## 4. 推理输出接口建议

AI 模块最终向系统状态机输出：

fire_confidence: 0.0 ~ 1.0
smoke_confidence: 0.0 ~ 1.0
bbox_count: 检测框数量
detections: 检测框列表

每个检测框建议格式：

{
  "class_id": 0,
  "class_name": "fire",
  "confidence": 0.82,
  "bbox": [x1, y1, x2, y2]
}

## 5. 风险融合建议

- fire_confidence > 0.6：
  三级应急，启动声光报警、风扇、水泵，并通知门外板禁入

- smoke_confidence > 0.5 且 MQ-2 异常：
  二级告警，启动声光报警和通风

- smoke_confidence > 0.4 或 MQ-2 异常或温度过高：
  一级预警，屏幕和网络端提示

## 6. 当前阶段说明

当前模型已在云端完成训练和 predict 测试。

下一步建议：
1. ESP32-P4 摄像头出图
2. 传感器读取
3. 执行器联动
4. 使用 mock_ai_infer() 先模拟 AI 输出
5. 后续再接入真实 .espdl 模型
