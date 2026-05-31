# B 组交付说明：界面、滤波、事件展示

## 1. 主界面

`esp_outdoor/components/ui_lvgl/` 已从单行摘要升级为状态面板模型，当前可集中展示：

- 门禁结论：PASS / BLOCK、拒绝原因、门锁状态、管理员锁定状态。
- PPE 状态：是否有人、实验服、护目镜、识别置信度。
- 室内风险：在线状态、风险等级、烟雾、火焰、气体告警。
- 传感器值：温度、湿度、VOC 的滤波后值与原始值对比。
- 联动动作：报警、风扇、水泵。
- 最近 3 条事件：来自本地门禁事件和室内节点事件。

当前实现保留 `ui_lvgl_get_dashboard()`，真实接屏时可把这段状态文本拆成多个 LVGL label、badge 或列表控件。

## 2. 卡尔曼滤波

`esp_indoor/components/sensor_reader/` 已加入一维卡尔曼滤波：

- `temperature_c`：滤波后温度，用于风险融合和上报。
- `humidity_rh`：滤波后湿度，用于界面展示。
- `voc_index`：滤波后 VOC 指数，用于风险融合和上报。
- `temperature_raw_c`、`humidity_raw_rh`、`voc_raw_index`：原始采样值，用于展示滤波效果。
- `filtered=true`：标识该帧经过滤波处理。

答辩表述建议：系统在边缘端对传感器阵列数据进行卡尔曼滤波，降低瞬时噪声对风险等级的误触发影响，同时保留原始值与滤波值对比，便于调试和可解释展示。

## 3. 事件日志展示

`components/event_log/` 已从“仅保存最后一条事件”升级为最近事件环形缓存：

- `event_log_get_latest()`：兼容原有最后一条事件。
- `event_log_get_recent(index)`：读取最近第 index 条事件，`0` 为最新。
- `event_log_get_recent_count()`：当前缓存事件数量。

室外节点新增订阅：

- `labguard/indoor/sensor`
- `labguard/event`

因此门外屏可以同步显示室内传感器状态和室内风险事件，演示时更像完整系统而不是单板日志。

## 4. 演示建议

可通过 MQTT 命令切换场景：

```json
{"type":"command","command":"force_normal","target_node":"indoor","timestamp":1715000000}
{"type":"command","command":"force_alarm","target_node":"indoor","timestamp":1715000001}
{"type":"command","command":"force_emergency","target_node":"indoor","timestamp":1715000002}
{"type":"command","command":"force_warning","target_node":"outdoor","timestamp":1715000003}
```

建议演示顺序：

1. 正常场景：PPE 合规，门禁 PASS。
2. PPE 缺失：门外 BLOCK，事件列表记录拒绝原因。
3. 室内告警：室内风险变为 alarm/emergency，门外同步 BLOCK。
4. 展示滤波：说明界面中 raw 为原始采样，左侧主值为卡尔曼滤波结果。

