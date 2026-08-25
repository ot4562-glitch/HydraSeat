# HydraSeat 🎮

[English](README.md) | [한국어](README.ko.md) | **简体中文**

HydraSeat 是一个实验性的 Windows 本地游戏多席位（multiseat）框架，目标是在一台物理 Windows PC 上提供多个彼此独立的本地游戏环境。仓库许可证目前尚未正式确定；复用外部代码前请先阅读[洁净室策略](docs/CLEAN_ROOM_POLICY.md)。

> 本文件是简体中文 README。实现状态、命令和稳定标识符以[英文 README](README.md)为准。

---

## 🎯 项目目标

HydraSeat 希望在不依赖虚拟机、远程桌面或游戏串流的前提下，让**一台物理 Windows PC 在体验上像多台独立的本地游戏 PC**。

核心抽象是 **Seat（席位）**，而不是单个显示器。一个 Seat 可以同时拥有多个显示器、输入设备、音频端点、进程和窗口。

```text
Physical Windows PC
├─ LG Monitor
├─ Samsung Monitor
├─ BenQ Monitor
├─ Keyboard A / Mouse A
├─ Keyboard B / Mouse B
├─ Controller A / Controller B
├─ Headset A
└─ Speakers B

HydraSeat
├─ Seat 1 — "Player 1 PC"
│  ├─ LG Monitor       (Primary)
│  ├─ Samsung Monitor  (Secondary)
│  ├─ Keyboard A
│  ├─ Mouse A
│  ├─ Controller A
│  ├─ Headset A
│  └─ Seat 1 拥有的游戏 / 应用
│
└─ Seat 2 — "Player 2 PC"
   ├─ BenQ Monitor     (Primary)
   ├─ Keyboard B
   ├─ Mouse B
   ├─ Controller B
   ├─ Speakers B
   └─ Seat 2 拥有的游戏 / 应用
```

目标体验并不是“一台 PC 接了三台显示器”，而更接近**“一台双显示器 PC + 一台单显示器 PC，只是底层共享同一套硬件”**。

Windows 默认把前台焦点、鼠标光标、键盘状态和桌面视为全局资源。HydraSeat 的长期工作是对本地游戏所需要的这些状态进行按 Seat 虚拟化或中介，使每个 Seat 尽可能独立运行。

### Management Seat 与后台运行

默认情况下，**Seat 1 是 Management Seat（管理席位）**。当分席会话处于活动状态时，打开 `HydraSeat.exe` 会把管理控制台放到 Seat 1 的主显示器上。

关闭管理窗口**不会**停止分席会话。`hydra_host.exe` 与 `hydra_watchdog.exe` 继续在后台工作；再次打开 HydraSeat 时会连接到已有 Host，并把控制台重新显示在 Management Seat 的显示器上。

```text
HydraSeat — Management Seat
├─ 当前状态: Normal Windows / Starting / Split Active / Degraded / Recovery Required
├─ Seat 1: LG + Samsung | Keyboard A | Mouse A | Controller A | Headset
├─ Seat 2: BenQ         | Keyboard B | Mouse B | Controller B | Speakers
│
├─ [ 启动分席会话 ]
├─ [ 停止 / 返回普通 Windows ]
├─ [ 重新配置显示器和输入设备 ]
├─ [ 识别 / 测试设备 ]
├─ [ 启动模式 ] Manual | Background Idle | Auto-Activate Validated Session
├─ [ 诊断 ]
└─ [ 恢复 / Reset ]
```

`Stop / Return to Windows` 不是简单的关闭窗口。Host 必须回滚本次 Seat 会话修改过的输入、设备、窗口、显示器、音频、控制器和 Shell 状态，并确认普通 Windows 输入与显示状态已经恢复，之后才可以报告 `Stopped`/`Idle`。

`Reconfigure` 默认采用安全流程：

```text
Active split session
  -> Stop / Return to Windows + 验证 rollback
  -> 在 Management Seat 显示器上打开配置界面
  -> 识别 / 测试 / 重新分配显示器、键盘、鼠标、控制器和音频
  -> validate + 保存新的 plan
  -> Start Now，或继续保持普通 Windows 状态
```

计划支持三种启动模式：

- **Manual** — 用户打开 HydraSeat 并按下 Start 之前不进行分席。
- **Background Idle** — 登录时 Host/Watchdog 静默启动，但 PC 仍保持普通单机状态，直到 Management Seat 手动 Start。
- **Auto-Activate Validated Session** — 只允许自动恢复一个明确选择、之前已验证的 Seat 布局。crash journal、safe mode、硬件/拓扑、capability、privilege、watchdog 与 rollback preflight 必须全部通过；否则安全地保持 Idle，而不是进入半完成的分席状态。

### 语言支持

UI/UX 的基准语言是英语 `en-US`。初始计划支持：

- English — `en-US`
- 한국어 — `ko-KR`
- 简体中文 — `zh-CN`

用户可见文本将使用稳定的 localization message ID，并以英语作为 fallback。以下内容保持稳定英文标识，不做本地化：

- 源代码注释与开发者 docstring；
- 协议和 schema key；
- CLI 参数；
- diagnostic/error code；
- capability/backend/profile/packet ID。

完整规则参见 [Localization Policy](docs/LOCALIZATION.md)。README 已提供多语言版本，并不表示运行时 UI 本地化已经实现；该工作由 `P7-I18N-01` 跟踪。

---

## 🪑 Seat 模型

```text
Seat
├─ Displays[]
│  └─ PrimaryDisplay
├─ Keyboards[]
├─ Mice[]
├─ Controllers[]
├─ AudioOutputs[]
├─ AudioInputs[]
├─ Seat-local cursor domain
├─ Seat-local display coordinate space
├─ Process group
├─ Window placement policy
├─ Input isolation policy
└─ Desktop / launcher profile
```

例如：

```text
Seat 1 = LG + Samsung + Keyboard A + Mouse A + DualSense + Headset
Seat 2 = BenQ + Keyboard B + Mouse B + Xbox Controller + Speakers
Seat 3 = Living-room TV + Controller C
```

HydraSeat 不假设“一台显示器就是一个 Seat”。多个显示器可以组成同一个 Seat display group。

---

## 🖥️ Seat 本地 PC 体验

HydraSeat 的目标是提供轻量级 **Seat Shell**，而不是创建完整 VM 或第二套 Windows 安装。

每个 Seat 长期可拥有：

- 独立 launcher / desktop surface；
- 类 taskbar 的窗口列表；
- wallpaper / profile；
- 独立 process/window group；
- 一个或多个显示器以及 primary display；
- 限制在本 Seat 显示器范围内的可见 cursor；
- 独立 audio input/output；
- 独立 game launcher/profile 设置。

真实 Windows 仍保留一个全局坐标空间，但 HydraSeat 维护 Seat-local 坐标变换：

```text
Seat 1
LG       -> local (0, 0), primary
Samsung  -> local (2560, 0)

Seat 2
BenQ     -> local (0, 0), primary
```

---

## ⌨️ 输入隔离目标

仅仅用 Raw Input 区分物理设备还不够。游戏仍可能直接读取全局 keyboard/mouse/cursor/focus 状态。

最终目标：

```text
Keyboard A + Mouse A -> Seat 1 applications only
Keyboard B + Mouse B -> Seat 2 applications only

Seat 1 输入不会进入 Seat 2 游戏
Seat 2 输入不会抢走 Seat 1 游戏的控制
```

Phase 3 把输入隔离视为兼容性问题，而不是简单的事件转发问题。根据具体 profile，可能需要组合：

- Win32 Raw Input routing；
- HID / SetupAPI physical-device identity；
- per-process input compatibility hooks；
- virtual keyboard/mouse/cursor state；
- foreground/focus mediation；
- 必要时使用 HidHide 一类可选 device visibility/isolation backend。

ProtoInput、Universal Split Screen、Nucleus Co-op、HidHide 与 Microsoft 官方 Windows API 文档可以作为研究材料。只有许可证兼容时才复用源代码；否则依据公开文档与可观察行为进行独立洁净室实现。

---

## 🎮 Process / Game 所有权

通过 HydraSeat 启动的应用属于 Seat，而不是仅属于某一台显示器。

```text
Seat 1 Process Group
├─ minecraft.exe
├─ discord.exe
└─ chrome.exe

Seat 2 Process Group
├─ fconline.exe
├─ launcher.exe
└─ browser.exe
```

Seat-aware window manager 只管理所属进程的窗口，并在新窗口创建、全屏切换或目标重启时重新验证所有权。

---

## 🔊 音频隔离

```text
Seat 1 games/apps -> Headset A
Seat 1 voice chat  -> Microphone A

Seat 2 games/apps -> Speakers B
Seat 2 voice chat  -> Microphone B
```

Per-application audio endpoint routing 是最终 Seat 模型的一部分。

---

## 🧭 设计原则

1. **不要求 VM** — 游戏直接运行在 host Windows 上。
2. **Seat 而非显示器是所有权单位** — 一个 Seat 可拥有多个 display。
3. **物理设备 identity 必须稳定且可重复** — 相同型号 USB/HID 设备仍需区分。
4. **目标是 zero cross-Seat input bleed** — 仅观察 Raw Input 不算完成。
5. **游戏应认为自己在本地处于 active 状态** — 可能需要处理 foreground/cursor/keyboard state/Raw Input API。
6. **不夸大实现状态** — 研究和原型必须如实标记。
7. **优先官方 Windows API 和许可证兼容的开源实现** — 否则采用 clean-room 独立实现。
8. **游戏优先** — 目标不是企业 VDI 或通用办公 multiseat。
9. **源代码注释使用英文** — UI 翻译文本与开发者稳定标识分离。

---

## 🚫 非目标

HydraSeat 不打算成为：

- 学校机房多席位系统；
- 企业办公 multiseat；
- 远程桌面软件；
- 云游戏服务；
- 企业 VM 管理器；
- hypervisor 或 Windows kernel/session manager 替代品。

---

## 🏗️ 架构

```text
HydraSeat.exe
  Management Seat 按需管理控制台
        │
        ▼
hydra_host.exe
  权威的 per-user background runtime
        │
        ├── hydra_watchdog.exe
        ├── Seat 1 process group / adapters / shell
        └── Seat 2 process group / adapters / shell

hydra_reset.exe
  不依赖 UI/Host 的紧急恢复路径
```

主要技术领域：

- **GUI / Seat Shell**: Qt 6 / Win32
- **Input Detection**: Win32 Raw Input, HID, SetupAPI / ConfigMgr
- **Input Compatibility**: Raw Input routing, 必要时 process-local compatibility hooks, optional HID visibility backends
- **Controllers**: XInput + HID/DirectInput
- **Display**: `EnumDisplayMonitors`, `EnumDisplayDevices`, DXGI, `QueryDisplayConfig`
- **Virtual Displays**: 后续 Phase 的 IddCx / IDD 或兼容 adapter
- **Process Management**: Windows process APIs / Job Objects
- **Audio**: Windows Core Audio per-application endpoint routing
- **Launcher**: Steam, Epic, EA, GOG, generic executable profiles

---

## 📍 当前开发状态

- **Phase 0 — Research & Foundation:** 已完成。
- **Phase 1 — Hardware Detection:** 已完成，并通过 Windows/MSVC CI 验证。
- **Phase 2 — Seat Composition / Assignment UI:** 当前 Win32 原型已完成，包括 multi-display Seat、primary display、exclusive device ownership 与 validated JSON profile。
- **Phase 3 — Input Compatibility / Isolation:** 当前阶段。Capability planner、Gate A/B observation、Gate C controlled-process protocol/adapter 基础已实现。**物理 acceptance、真实 Windows API interposition、device cloaking、游戏 zero-bleed 验证仍未完成。**
- **后续 Phase:** background runtime、process/window/display ownership、two-game MVP、profile/launcher、Seat shell、localization、watchdog/installer/update、SDK 与 release hardening。

最重要的里程碑不是“启动两个窗口”，而是证明**两个用户能同时运行不同游戏，而且任一用户的键盘/鼠标输入与 foreground 行为都不会干扰另一个 Seat**。

---

## 🧪 Phase 3 工具

`hydra_plan` 是只读诊断工具，用于分析 compatibility profile 与假定 backend environment。它不会执行 process injection、driver 安装、device hiding 或 physical suppression。

```text
hydra_plan --list
hydra_plan observation-harness
hydra_plan raw-input-game
hydra_plan polled-keyboard-mouse-game --protoinput --hidhide --allow-injection --admin --recovery-ready
```

### Gate A/B Input Lab

```powershell
.\build\Release\hydra_input_lab.exe --no-profile
.\build\Release\hydra_input_lab.exe --profile workspace_config.json
.\build\Release\hydra_input_lab.exe --profile workspace_config.json --trace phase3-input-lab.jsonl
.\build\Release\hydra_input_lab.exe --self-test
```

当前 Gate A/B lab 可以观察并分类物理输入，但不会抑制普通 Windows 输入。真实双键盘/双鼠标 physical acceptance 仍需执行。参见 [Gate A/B 测试文档](docs/PHASE3_GATE_A_B_TESTING.md)。

### Gate C Controlled Process Lab

```powershell
.\build\Release\hydra_gate_c_host.exe `
  --self-test `
  --target .\build\Release\hydra_gate_c_target.exe

.\build\Release\hydra_gate_c_host.exe `
  --profile workspace_config.json `
  --trace hydra_gate_c_host.jsonl
```

Gate C 仅运行 HydraSeat 自己的 controlled target。面向 controlled probe 的 process-local polling 与 cursor/clip/logical-focus/capture shim 已通过 x64/x86 Windows CI 验证。独立 Raw Input behavior probe 与 bounded trace/parser 也已在 Windows run `32800513365` 通过 native x64/x86 验证。XInput generation/snapshot 正确性修复已经在 fork PR #15 run `32832036967` 中针对 remediation head `b351afdd` 完成验证：native x64/x86 均为 36/36，并通过 x64-host→x64/x86 zero-cross controller acceptance，因此当前状态为 `VALIDATED`。旧 run `32816241577` 仅保留为修复前证据。当前不实现 remote injection、physical suppression 或第三方/商业 target 支持。

相关文档：

- [Related systems research](docs/RELATED_SYSTEMS_RESEARCH.md)
- [Phase 3 input isolation design](docs/PHASE3_INPUT_ISOLATION_DESIGN.md)
- [Clean-room policy](docs/CLEAN_ROOM_POLICY.md)
- [Gate A/B testing](docs/PHASE3_GATE_A_B_TESTING.md)
- [Gate C testing](docs/PHASE3_GATE_C_TESTING.md)
- [Localization policy](docs/LOCALIZATION.md)

---

## 🧱 Codex 实现路线图

后续工作拆分为 bounded packet，防止 Codex 自行重新设计架构、越过阶段或把未执行的物理/游戏验证标记为完成。

当前默认 packet：

```text
P3-CTRL-02 — DirectInput enumeration/visibility adapter
```

开始编码前阅读：

1. [Agent rules](.agents/AGENTS.md)
2. [Non-negotiable decisions](docs/implementation/DECISIONS.md)
3. [Master implementation roadmap](docs/implementation/README.md)
4. [Current packet status](docs/implementation/STATUS.md)
5. [Codex implementation playbook](docs/implementation/CODEX_PLAYBOOK.md)

```text
python tools/show_implementation_packet.py --current
python tools/show_implementation_packet.py --current --prompt
python tools/show_implementation_packet.py --ready
python tools/validate_implementation_roadmap.py
git diff --check
```

UI 国际化由 `P7-I18N-01` 实现。在英语/韩语/简体中文真实 UI acceptance 完成之前，不得把该功能标记为完成。

---

## 🛠️ 构建要求

- **OS**: Windows 10 / Windows 11 (64-bit)
- **Compiler**: Visual Studio 2022 / MSVC C++20
- **Build System**: CMake 3.20+
- **Framework**: Qt 6.x (Widgets / Core)
- **Windows SDK**: Windows 10/11 SDK

---

## 🚀 路线图 / 详细文档

- [Phase 0–10 Roadmap](docs/ROADMAP.md)
- [Master implementation roadmap](docs/implementation/README.md)
- [Current status](docs/implementation/STATUS.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Localization policy](docs/LOCALIZATION.md)
- [Phase 0 research](docs/PHASE0_RESEARCH.md)
- [Related systems research](docs/RELATED_SYSTEMS_RESEARCH.md)
- [Clean-room policy](docs/CLEAN_ROOM_POLICY.md)
- [Gate A/B testing](docs/PHASE3_GATE_A_B_TESTING.md)
- [Gate C testing](docs/PHASE3_GATE_C_TESTING.md)
