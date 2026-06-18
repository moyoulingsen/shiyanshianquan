# LabGuard SingleP4 Firmware

`firmware/` 现在按**单板主工程 + 公共组件 + 视觉资料 + 工具脚本 + 模型资源**来组织。

## 目录结构

```text
firmware/
├─ CMakeLists.txt            单板工程入口
├─ main/                     单板主流程
├─ components/               公共组件
│  ├─ labguard_common/
│  ├─ labguard_net/
│  ├─ event_log/
│  └─ espdl_probe/
├─ models/                   模型文件与模型配置
│  └─ p4/
├─ vision/                   视觉推理资料与 POC
├─ tools/                    模型转换与数据集工具
├─ README.md                 目录总说明
├─ 项目介绍.md              项目展示版介绍
└─ B_DELIVERY_NOTES.md       阶段性交付说明
```

## 说明

- `main/` 放单板业务主循环：视觉、传感、风险融合、联动、上报。
- `components/` 放所有可复用模块，避免再按门外/门内拆工程。
- `models/` 放最终要烧进板子或从存储加载的模型资源。
- `vision/` 保留视觉方案资料、导出记录和验证文档。
- `tools/` 保留数据集准备、量化、导出脚本。

## 当前保留的历史内容

目前仓库里仍保留 `esp_indoor/` 旧工程，主要用于参考现有实现和迁移代码。
视觉资料 `vision/` 下也保留了部分历史双板研究记录，因此其中仍可能出现 `indoor/outdoor` 字样；这些内容属于归档资料，不再代表当前主方案。

## 建议

如果后面继续收敛代码，`esp_indoor/` 这类旧工程也应该逐步并入 `main/` 和 `components/`，不要再作为主入口对外描述。
