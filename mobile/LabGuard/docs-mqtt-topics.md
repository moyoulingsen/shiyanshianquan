# MQTT Topics

当前 React Native 版 LabGuard 直接复用现有网页端 topic：

- `labguard/device/sensor`
- `labguard/device/risk`
- `labguard/device/status`
- `labguard/device/camera`
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
  "target_node": "device",
  "level_pct": 80,
  "timestamp": 1710000000
}
```

当前手机端和网页端统一支持这些命令：

- `reset`
- `audio_on` / `audio_off`
- `light_on` / `light_off`
- `alarm_on` / `alarm_off`
- `fan_on` / `fan_off`
- `pump_on` / `pump_off`

风扇和水泵开启或调节档位时会携带 `level_pct`：

```json
{
  "node": "mobile",
  "type": "command",
  "command": "pump_on",
  "target_node": "device",
  "level_pct": 75,
  "timestamp": 1710000000
}
```

## 状态同步

`labguard/device/risk` 会同步风扇和水泵状态：

- `action_fan` / `action_pump`：板子最终输出
- `auto_fan` / `auto_pump`：自动风险联动输出
- `manual_fan_override` / `manual_pump_override`：是否手动接管
- `manual_fan` / `manual_pump`：手动接管值
- `fan_level_pct` / `pump_level_pct`：最终有效档位
- `manual_fan_level_pct` / `manual_pump_level_pct`：手动档位

`labguard/device/status` 会同步声音和灯带状态：

- `audio_looping`
- `light_on`

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
