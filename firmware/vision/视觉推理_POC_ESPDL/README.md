# 视觉推理 POC / ESP-DL 资料区

这个目录保存的是 **LabGuard 视觉推理链路的迁移资料、验证脚本和隔离 POC**，方便继续把火焰/烟雾检测从训练产物推进到 ESP32-P4 板端。

它的定位是：

- 给当前仓库提供一个**可读、可追溯**的视觉资料入口
- 保存 `espdl_probe`、模型导出脚本、A7 交接文档等资料
- 作为正式主线接入前后的**参考区**

> 正式接入主线的组件路径在 [../components/espdl_probe/](../components/espdl_probe/) 和 [../esp_indoor/components/hazard_infer/](../esp_indoor/components/hazard_infer/)。

## 目录结构

```text
视觉推理_POC_ESPDL/
├─ README.md                      本说明
├─ docs/                          交接文档、尺寸分析、硬件检查项
├─ components/espdl_probe/        隔离 ESP-DL 探针组件参考副本
├─ tools/                         POC 导出/预检查脚本
└─ configs/                       非默认 POC sdkconfig 覆盖项
```

## 里面各部分是做什么的

### 1. `docs/`

这部分记录了 ESP-DL / `.espdl` 路线的调研和交接信息，建议优先看：

- [docs/espdl_a7_handoff.md](docs/espdl_a7_handoff.md)
- [docs/espdl_flash_partition_plan.md](docs/espdl_flash_partition_plan.md)
- [docs/espdl_hardware_checklist.md](docs/espdl_hardware_checklist.md)
- [docs/espdl_pre_research.md](docs/espdl_pre_research.md)

适合回答这些问题：

- 当前 `.espdl` 产物是什么状态
- 为什么默认构建能过、ESP-DL POC 构建会超分区
- 板端验证还差哪些步骤
- 后面是该走 rodata 还是 SD 卡路径

### 2. `components/espdl_probe/`

这是一个**隔离的 ESP-DL 探针组件**，作用不是直接参与业务风险判定，而是单独验证：

- `.espdl` 能否被加载
- `model->test()` 是否通过
- 内存和模块 profile 情况如何

参考说明：

- [components/espdl_probe/README.md](components/espdl_probe/README.md)

正式组件已接入到：

- [../components/espdl_probe/](../components/espdl_probe/)

这里保留副本主要是为了让视觉资料自包含，方便回看 POC 来源。

### 3. `tools/`

这部分是视觉模型导出与探针辅助脚本，主要包括：

- [tools/espdl_export_poc.py](tools/espdl_export_poc.py)
- [tools/espdl_export_poc.sh](tools/espdl_export_poc.sh)
- [tools/espdl_probe_preflight.sh](tools/espdl_probe_preflight.sh)

用途分别是：

- 把 handoff 的 `best.onnx` 导出成真实 `.espdl`
- 用 shell 包一层固定导出流程
- 在板端验证前做环境/文件预检查

## 推荐工作流

如果你要继续推进板端火焰/烟雾检测，建议按这个顺序走：

1. 阅读 `docs/`，确认当前尺寸、分区、硬件状态
2. 用 `tools/` 导出或复核 `.espdl` 模型产物
3. 用正式组件 [../components/espdl_probe/](../components/espdl_probe/) 做隔离验证
4. 确认 `.espdl` 可加载后，再走室内主业务链路：
   - [../esp_indoor/components/hazard_infer/](../esp_indoor/components/hazard_infer/)
5. 在室内节点上验证：
   - SD 卡挂载
   - 模型路径可见
   - `hazard_infer` 真实推理输出是否合理
   - 缺模型/推理失败时是否能回退

## 和主工程的关系

当前主工程里的职责分工建议是：

- **正式探针组件**： [../components/espdl_probe/](../components/espdl_probe/)
- **正式 hazard 推理链路**： [../esp_indoor/components/hazard_infer/](../esp_indoor/components/hazard_infer/)
- **本目录**：保留资料、脚本、配置和上下文说明

也就是说，这里不是主工程唯一入口，而是**视觉资料中台**。

## 当前已知边界

- `espdl_probe` 仍然是**隔离探针**，不是最终业务推理链路
- 室内 `hazard_infer` 才是火焰/烟雾检测正式入口
- Outdoor 节点目前没有现成的 SD 卡探针运行路径
- 大模型和 `.espdl` 产物可能较大，提交前要确认是否应该纳入版本控制

## 下一步最值得做的事

1. 跑通 indoor 的 `.espdl` SD 卡推理
2. 捕获 `espdl_probe` 的 `model->test()` / `profile_memory()` / `profile_module()` 日志
3. 根据真实尺寸结果，决定最终分区和部署路径
