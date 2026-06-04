# MQTT Topics

当前 React Native 版 LabGuard 直接复用现有网页端 topic：

- `labguard/indoor/sensor`
- `labguard/indoor/risk`
- `labguard/indoor/status`
- `labguard/indoor/camera`
- `labguard/event`
- `labguard/cmd/test`

## 订阅

App 会订阅：

- 传感器数据
- 风险状态
- 节点状态
- 摄像头帧
- 事件流

## 发布

App 会向以下 topic 发布：

- `labguard/cmd/test`：控制命令
- `labguard/event`：手机端连接/断开/控制事件

## 命令格式

示例：

```json
{
  "node": "mobile",
  "type": "command",
  "command": "fan_on",
  "target_node": "indoor",
  "level_pct": 80,
  "timestamp": 1710000000
}
```

## 摄像头格式

当前按 dashboard 兼容格式处理：

```json
{
  "type": "camera_frame",
  "format": "image/jpeg",
  "image_base64": "...",
  "width": 320,
  "height": 240,
  "sequence": 123
}
```
