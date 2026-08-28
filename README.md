# RainDeskPlus

> 桌面美化集成平台 — 基于 Rainmeter (GPL v2) 派生的系统监控挂件 + Dock 栏 + 皮肤系统。

本仓库为**脚手架 + 核心类骨架阶段**：目录结构、构建系统、架构文档、各核心类的头/实现骨架已就位，但**尚未链接 Rainmeter 实际源码与 Duilib**，当前不保证可编译。等本地工具链（VS 2022 / git / Windows SDK / CMake）就位后，按 `docs/BUILD.md` 配置并克隆 Rainmeter 即可逐步打通构建。

## 项目定位

| 维度 | 说明 |
|------|------|
| 上游 | [Rainmeter](https://github.com/rainmeter/rainmeter) — GPL v2，C++ 核心 + C# 插件 |
| 新增 | Dock 栏（参考 Cedro Modern Dock / RocketDock 设计，全新实现，不使用专有 Nexus 源码） |
| UI 框架 | Duilib（支持透明 / 异形窗口），通过 CMake 选项 `RAINDOCK_USE_DUILIB` 开关 |
| 渲染 | Direct2D + DirectWrite |
| 语言标准 | C++17，MSVC v145（VS 2026）或 v143（VS 2022，回退）；v145 与 v143 二进制兼容 |
| 构建 | CMake 为主；可由 CMake 生成 VS 2026/2022 `.sln`；另附最小手写 VS stub 供直接打开 |
| 许可证 | GPL v2（保留上游版权声明，详见 [LICENSE](LICENSE)） |

## 目录结构

```
RainDeskPlus/
├── App/            主程序入口
├── Library/        核心引擎（Rainmeter 派生：Measure / Meter / Skin / ConfigParser ...）
├── Dock/           Dock 栏（全新实现：DockBar / DockItem / DockMagnify ...）
├── UI/             Duilib 窗口适配层
├── Plugins/        插件 DLL（占位）
├── Skins/          默认皮肤（示例 .ini）
├── Themes/         主题（占位）
├── Tests/          单元测试占位
├── docs/           架构 / 模块提取 / Dock 设计 / 构建 / 路线图 / Agent 提示词
├── scripts/        构建 / 配置 / 拉取 Rainmeter 脚本
├── CMakeLists.txt  主构建脚本
├── CMakePresets.json
├── RainDeskPlus.sln          VS 原生 staging stub（可直接双击打开）
└── README.md
```

## 构建状态（当前不可用）

本机工具链缺失：无 VS 2022 / git / CMake / Windows SDK，仅有不可用的 MinGW GCC 4.9.2。详见 `docs/BUILD.md` 的前置条件清单与 `docs/MODULE_EXTRACTION.md` 的 Rainmeter 源码拉取步骤。

## 文档导航

- [架构设计](docs/ARCHITECTURE.md)
- [Rainmeter 模块提取方案](docs/MODULE_EXTRACTION.md)
- [Dock 栏设计](docs/DOCK_DESIGN.md)
- [构建指南](docs/BUILD.md)
- [开发路线图](docs/ROADMAP.md)
- [AI Agent 提示词模板](docs/AGENT_PROMPTS.md)

## 许可证

Copyright (C) 2014-2025 Rainmeter Project  
Copyright (C) 2026 RainDeskPlus Project

本程序为自由软件，依据 GNU 通用公共许可证第 2 版（GPL v2）条款发布。详见 [LICENSE](LICENSE)。

> Rainmeter 源码派生部分保留其原作者版权声明；Dock 模块为全新实现，同样以 GPL v2 发布。
