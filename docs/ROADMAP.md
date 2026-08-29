# 开发路线图

> 3 个月 MVP，单人开发 + AI 辅助。本文为计划骨架，进度按实际推进更新。

## Phase 0 — 环境搭建（Week 1）

- [x] D1：项目脚手架（目录 / CMake / VS stub / 文档 / 类骨架）
- [x] D1：安装 **Visual Studio 2026 Community**（v145 工具集，回退 v143 保留；路径 `E:\visual_C`）
- [x] D1：配置 git 身份（全局 `Tcool / thankword@outlook.com`；.gitconfig 4 域名级 openssl+SteamTools 根 CA 绕过 Watt MITM 证书链不完整）
- [x] D2：克隆 Rainmeter 源码到 `rainmeter-upstream/`（`--depth 1`，HEAD abd0b5ae；.gitignore 排除）
- [~] D2：打开 `Rainmeter.sln`（上游）编译 Debug（M1 未执行；上游依赖 GDI+/WTL/vcpkg 未就绪，M2 前置或按需验证）
- [x] D3：架构笔记就位（= [ARCHITECTURE.md](ARCHITECTURE.md)）
- [x] D4：本项目目录结构就绪 + 骨架首次 Debug 构建通过（2026-08-26，0 error 0 warning）
- [ ] D5：Duilib 开发环境，跑通 Hello World（vendor `third_party/duilib`，Dock 阶段启用 `RAINDOCK_USE_DUILIB`）

## Phase 1 — 核心引擎提取（Week 2）

- [x] D6-7：从 Rainmeter **提取核心骨架并对齐**到 `Library/`（见 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §7 M1 细节）
  - `ConfigParser.cpp/h`（真实 INI 解析 + 变量展开 + 颜色/数值/段序）
  - `Measure.cpp/h`（基类：Initialize→ReadOptions→Update 跳帧→UpdateValue=0 / Invert/Substitute/Min/Max/Divider）
  - `MeasureTime`（真实格式化 + 时区/DST 禁用兼容）
  - `MeasureCPU`（整机 `GetSystemTimes` 差分 %CPU；单核 M1 降级）
  - `MeasureMemory`（Phys+PageFile 口径 / Mode / Total 键）
  - `MeasureNet` / `MeasurePlugin`（占位对齐新签名；真采样留 M2）
  - `Meter*/Skin/CommandHandler/Rainmeter` 骨架保留，M2→M5 陆续升真
- [x] **M1 里程碑（2026-08-28）**：vs2026-x64 Debug **clean rebuild 0 error / 0 warning / 4 目标全产出**（RainDeskPlusCore.lib + RainDeskPlusDock.lib + RainDeskPlusUI.lib + RainDeskPlus.exe）
- [x] **M2 里程碑（2026-08-28）**：`MeasureNet` 真采样（GetIfTable2 64 位计数器 / Best·Total·Index / 速率·累计）+ `MeasurePlugin` DLL 加载骨架 + **Smoke 测试 11/0 全过**（Time/CPU/Memory/Net 全链路，Net 累计值与系统计数器一致）；修 2 个真 bug（Memory UsedPercent 缩放语义 / Measure.h 默认 clamp 误伤无界 Measure）。详见 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §7。
- [x] **M3 里程碑（2026-08-29）**：`MeterString` 经 Direct2D/DirectWrite 真渲染出文本（`Canvas` 共享工厂 + 离屏 WIC 渲染目标；Text=%1 绑定 Measure；AutoSize metrics；Smoke **17/0 全过**，渲染像素 1222 命中 + PNG 落盘核验）。详见 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §7 M3。
- [x] **M4 里程碑（2026-08-29）**：`Skin` 加载最简皮肤并**真实显示**（WS_POPUP 挂件窗口 + D2D HwndRenderTarget + WM_TIMER 驱动；INI→Measure/Meter 工厂构建绑定；Smoke **22/0 全过**，窗口可见 + 5 帧渲染）。详见 [MODULE_EXTRACTION.md](MODULE_EXTRACTION.md) §7 M4。
- [ ] D8-9：M5 编译通过（CRainmeter+Bang）
- [ ] D10：【代码审查 Agent】审查提取代码

## Phase 1 末 / Phase 2 — Dock 栏（Week 3）

- [ ] D11-12：【架构师 Agent】设计 `DockBar` 结构（= [DOCK_DESIGN.md](DOCK_DESIGN.md)）
- [ ] D13-14：【编码助手 Agent】实现 `DockBar` 核心功能
- [ ] D15：图标拖拽添加
- [ ] D16-17：鼠标悬停放大效果（`DockMagnify`）

## Phase 2 — 功能集成（Week 4）

- [ ] D18-19：系统监控挂件（CPU / 内存 / 磁盘 / 网络）
- [ ] D20-21：天气挂件（OpenWeatherMap API）
- [ ] D22-23：时钟 / 日历挂件
- [ ] D24：媒体播放控制
- [ ] D25：音频可视化

## Phase 3 — 皮肤系统（Week 5-6）

- [ ] D26-30：完善皮肤 / 主题系统（挂件 + Dock 统一主题）

## Phase 4 — 配置与持久化（Week 7）

- [ ] D31-35：统一配置管理、INI 持久化、Dock items.ini

## Phase 5 — 测试与发布（Week 8）

- [ ] D36-40：集成测试 + 性能优化
- [ ] D41-42：文档（【文档工程师 Agent】）+ README + 发布

## 每日 AI 协作流程

```
晨：向【架构师 Agent】描述当日功能 → 拿设计方案
   ↓ 向【编码助手 Agent】索要代码
   ↓ 集成、编译、测试
   ↓ 有错 → 把错误发给【编码助手 Agent】修复
暮：【代码审查 Agent】审查当日代码
```

## Agent 角色与提示词

见 [AGENT_PROMPTS.md](AGENT_PROMPTS.md)。

| 阶段 | 主 Agent | 任务 |
|------|---------|------|
| Phase 0 | 架构师 + 编码助手 | VS 配置 / 编译脚本 / 目录结构 |
| Phase 1 | 架构师 | 模块依赖、裁剪方案 |
| Phase 1 | 编码助手 | 提取后的核心引擎代码 |
| Phase 1 | 架构师 + 编码助手 | DockBar 设计与实现 |
| Phase 2 | 编码助手 | Measure/Meter 集成 |
| Phase 3 | 编码助手 | 皮肤加载 / 切换 / 主题 |
| Phase 5 | 测试 + 文档工程师 | 测试用例 / 用户文档 / README |
