# HydraSeat

[English](README.md) · [한국어](README.ko.md)

HydraSeat 面向 Windows 10/11 x64，用于在**一台 PC 上运行两个本地游戏 Seat**。Seat 是由显示器、输入设备、控制器和音频策略组成的物理座位，不是第二个 Windows 桌面或用户会话。正常流程是：选择游戏 → Seat 1、Seat 2 或 Both → Player → 启动。

当前仓库已有较完整的实现和测试基础，但**还不是完成的公开产品**。运行时、IPC、启动、回滚、恢复、provider、兼容性和发布基础已有大量 controlled tests；双键盘/双鼠标物理证据、真实游戏 campaign、clean-machine installer/UAC/reboot 证据和受保护的 production signing 仍是 release gate。

## 当前实际结构

| 组件 | 构建目标 | 当前职责 |
| --- | --- | --- |
| Management UI | `HydraSeat.exe` | Native Win32 game-first UI、Seat/profile/setup 选择、host 控制与诊断 |
| Runtime authority | `hydra_host.exe` | 会话权威、bounded/versioned IPC、immutable launch plan、进程树和 Seat 生命周期 |
| Seat UI | `hydra_seat_ui.exe` | Seat 最小 launcher/status 界面，不是第二套 desktop shell |
| Recovery | `hydra_watchdog.exe`, `hydra_reset.exe` | crash journal 恢复、owned-state 回滚和 emergency reset |
| 运维/验证工具 | `hydra_hostctl.exe`, `hydraseat_profilectl.exe` 及诊断工具 | 协议检查、profile/provider 工作、controlled probe 与证据采集 |
| 可选适配器 | Gate C adapter/shim targets | 明确 gate 下的 Raw Input 与 Win32 polling/focus/cursor 兼容工作 |

旧 Qt prototype UI 不再参与构建。当前界面是 native Win32，并与 CLI 和 Seat UI 使用同一 host protocol。

## 运行时结构

```text
HydraSeat.exe (normal-user management UI)
        | bounded host protocol v4
        v
hydra_host.exe (authoritative background runtime)
        +-- 验证 profile/session/Seat generation
        +-- 解析 trusted requirement 与 immutable launch plan
        +-- 独立启动和拥有每个 Seat 的进程树
        +-- 协调 window/display/controller/audio/input policy
        +-- 记录 mutation journal 并验证 rollback
        +-- hydra_seat_ui.exe (Seat 最小界面)
        +-- hydra_watchdog.exe / hydra_reset.exe (恢复边界)
```

权威在 host，而不在 UI。Production path 安装精确 plan 且 host 接受并执行之前，UI 请求不能算游戏启动成功。Seat 1 的停止或重启不应无谓终止 Seat 2。协议使用 bounded、versioned、fixed-width 布局，防止 x86/x64 适配器静默误解 handle 或 payload。

## 产品边界

v1 最多支持两个 active Seat，并分离 **Seat**、**Player**、**Game**、**TwoPlayerSetup** 和当前 immutable **RuntimeSession**。v1 不承诺虚拟机、独立 Windows 登录、通用 multiseat desktop、所有游戏双开，也不绕过 DRM、anti-cheat、账号、launcher、single-instance 或其他保护边界。不支持的要求会 fail closed 并明确展示。

当前实现包含稳定硬件身份、controlled input-isolation adapter/evidence tooling、authoritative host IPC 和进程树所有权、Seat 独立 stop/restart、window/display/controller/audio policy、Steam/custom provider、profile/trusted requirement/typed setup package、local compatibility evidence、可选 community contract、crash/reset、least-privilege、privacy、transactional installer/update 和 signing/provenance validation。

精确 packet 状态及剩余证据见 [Implementation Status](docs/implementation/STATUS.md)。`CODE_COMPLETE` 若仍缺 physical、game、installer、reboot、signing 或 public-service 证据，就不能标记为 `VALIDATED`。

## 构建与测试

需要 Windows、Visual Studio 2022 MSVC C++ workload、CMake、Python 3 和 PowerShell。

```powershell
# clean x64
cmake -S . -B C:\HydraSeat\build-x64 -G "Visual Studio 17 2022" -A x64
cmake --build C:\HydraSeat\build-x64 --config Release --parallel
ctest --test-dir C:\HydraSeat\build-x64 -C Release --output-on-failure

# clean Win32/x86 compatibility build
cmake -S . -B C:\HydraSeat\build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build C:\HydraSeat\build-x86 --config Release --parallel
ctest --test-dir C:\HydraSeat\build-x86 -C Release --output-on-failure
```

截至 2026-08-30，全新 x64 和 Win32 构建均无 warning，并分别通过 **133/133** CTest；production-launch/IPC focused regression 也已通过。该自动化结果不能替代尚未完成的物理和真实游戏 gate。

## 安装与发布状态

`tools/install_hydraseat.ps1` 已实现 x64 package 验证、install/repair/uninstall transaction、owned-path 检查和 rollback。发布 binary 是 `HydraSeat.exe`、`hydra_host.exe`、`hydra_seat_ui.exe`、`hydra_watchdog.exe` 和 `hydra_reset.exe`。

目前不能宣称 installer 已完成 production 验证。仍需 clean Windows machine、UAC、reboot/interruption recovery、uninstall postcondition、受保护 signer、production certificate/timestamp provider 和 signed release-candidate 运行。

## 文档地图

- [v1 产品契约](docs/PRODUCT_V1.md) · [Architecture](docs/ARCHITECTURE.md) · [Implementation Status](docs/implementation/STATUS.md)
- [Packet roadmap](docs/implementation/README.md) · [Reference 研究索引](docs/REFERENCE_RESEARCH_INDEX.md) · [相关系统设计备忘](docs/RELATED_SYSTEMS_RESEARCH.md)
- [Clean-room policy](docs/CLEAN_ROOM_POLICY.md) · [Compatibility matrix](docs/COMPATIBILITY_MATRIX.md) · [并行 chunk board](.agents/CHUNKS.md)

`C:\HydraSeat\references` 下的 reference tree 是只读研究输入，不是 build input。研究结论只转化为中立需求和独立编写的测试，不复制许可证不兼容的第三方实现到 HydraSeat core。

## License

仓库尚未最终声明项目 license 和 contribution 条款。在该 gate 解决之前，不应将其描述为法律意义上的 open source，也不应假设拥有 source reuse 权利。
