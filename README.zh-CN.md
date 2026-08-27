# HydraSeat 🎮

[English](README.md) | [한국어](README.ko.md) | **简体中文**

> **一台性能充足的 Windows 游戏 PC，两名本地玩家，各自的显示器、输入设备、手柄和音频——不需要虚拟机、远程桌面或游戏串流。**

HydraSeat 是一个实验性的 Windows 本地游戏多座席项目，面向这样一种家庭场景：现有游戏 PC 仍有明显的 CPU、GPU、内存和 I/O 余量，但仅仅为了让第二个人同时玩游戏，再购买和维护一整台台式机成本过高或不方便。

v1 的目标有意保持狭窄：

> **让两个人把一台性能足够的 Windows 游戏 PC 的剩余性能，作为两个本地游戏座席来共享。**

HydraSeat 正在公开开发，并以未来的开源发布模式为目标。不过，**当前仓库尚未正式声明项目许可证和贡献条款**，因此在该法律门槛解决之前，不能把当前仓库描述为法律意义上的开源项目。请参阅 [Clean-room 与许可策略](docs/CLEAN_ROOM_POLICY.md)。

---

## 为什么要做 HydraSeat

现代游戏 PC 的性能不断提高，但许多游戏并不会持续占满全部 CPU 核心、内存和 GPU 能力。于是，一个人在玩游戏时，机器往往仍有可利用的性能余量。

而同一家庭中的第二个人如果也想同时玩游戏，通常仍需要另一台完整 PC。

HydraSeat 尝试改变这个取舍：

```text
一台 Windows 游戏 PC
│
├─ Seat 1
│  ├─ 显示器 A
│  ├─ 键盘 / 鼠标 A
│  ├─ 手柄 A
│  └─ 音频 A
│
└─ Seat 2
   ├─ 显示器 B
   ├─ 键盘 / 鼠标 B
   ├─ 手柄 B
   └─ 音频 B
```

目标用户包括情侣、兄弟姐妹、室友、家庭成员和朋友：他们已经拥有一台足够强的 PC，希望利用空闲性能，而不是再购买第二台完整台式机。

HydraSeat 并不保证任意 PC 都能流畅同时运行两个游戏。所选工作负载仍然需要足够的 CPU、GPU、内存、存储、显示输出和外设能力。

---

## v1：只做两个 Seat

HydraSeat v1 的正式产品范围是 **两个本地游戏 Seat**。

内部实现可以在合理时继续使用通用集合结构，但 v1 的 UI、安装程序、测试矩阵、产品承诺和兼容性证据都以最多两个活动 Seat 为准。

支持第三、第四个 Seat 会迅速增加显示器、键盘、鼠标、手柄、音频设备、物理空间和测试组合的数量，而实际家庭用户需求可能很小。因此 HydraSeat 优先把 **两个 Seat 真正做成可用产品**。

---

## 核心模型

HydraSeat 明确区分 **Seat / Player / Game / Two-player setup / Runtime Session**。

### 🪑 Seat = 物理座位

Seat 类似网吧中的一个座位。它描述硬件，不描述某个人或某个游戏。

```text
Seat 1
├─ 显示器
├─ 键盘       可暂不设置
├─ 鼠标       可暂不设置
├─ 手柄       可选
└─ 音频       可选
```

Seat 配置允许不完整。首次设置向导可以跳过，某些设备可以选择“以后设置”，之后再进入 Seat 设置补充。

启动游戏时，HydraSeat 根据该游戏的真实需求进行 preflight。例如，仅使用手柄的游戏不应因为 Seat 没有键盘就被判定为整体无效。

### 👤 Player = 玩家本人

Player 是独立于 Seat 的轻量个人资料。

它可以记住：

- 显示名称和可选本地头像；
- 最近游戏；
- 最近使用的 Seat；
- 每个游戏的实例/数据目录偏好；
- 在 provider 支持时，对已由原始启动器认证的账号的引用。

Player 可以在 Seat 1 和 Seat 2 之间交换位置而不丢失这些关联。

HydraSeat 不应成为通用密码保险箱。尽可能让 Steam、Minecraft 启动器等原始 provider 继续持有登录凭据和认证 token，HydraSeat 只保存选择已有认证身份所需的最小引用。

### 🎮 Game = 已安装游戏

正常 UX 应从支持的启动器和本地安装信息中自动发现游戏。

对于未知游戏或特殊安装，仍保留手动添加 EXE 的高级用户路径。

UI 尽量使用本机 EXE、快捷方式或 provider 元数据中已经存在的游戏图标，而不是随 HydraSeat 打包大量第三方美术资源。

### 🔁 Two-player setup = 同一游戏双实例配方

Seat 1 和 Seat 2 运行不同游戏时，用户无需感知额外的 Profile 概念。

只有两个 Seat 选择 **同一个游戏** 时，HydraSeat 才查找或创建该游戏的 Two-player setup。

其中可以包含：

- 独立实例/配置/数据目录；
- 启动参数与 working directory；
- provider 特定启动方式；
- provider 支持时的 Player 账号引用；
- 启动顺序；
- 窗口识别和放置；
- 输入/手柄/音频要求；
- 已知限制。

HydraSeat 应尽可能自动生成，同时保留 **引导式手动编辑器**，让自动化暂时不理解的游戏仍能被有耐心的高级用户尝试。

> **HydraSeat 只在游戏和 provider 本身允许的范围内自动化多实例配置，不会通过突破限制来制造多实例支持。**

### ▶ Runtime Session = 当前正在运行的组合

```text
Seat 1 + Mario + Minecraft 实例 A
Seat 2 + Luigi + Minecraft 实例 B
```

Seat 是硬件，Player 是人，Two-player setup 是可复用的游戏知识，只有当前运行映射属于 Runtime Session。

完整定义见 [HydraSeat v1 产品规格](docs/PRODUCT_V1.md)。

---

## 以游戏为中心的 UI/UX

HydraSeat 应该看起来像一个轻量游戏启动器，而不是 Windows 系统管理工具。

正常流程：

```text
打开 HydraSeat
    ↓
选择游戏
    ↓
选择 Seat 1 / Seat 2 / Both
    ↓
为每个 Seat 选择 Player
    ↓
只确认必要风险
    ↓
Play
```

点击/轻触是主要交互方式。把游戏图标拖到 Seat 卡片上的 Drag & Drop 可以作为快捷方式同时提供。

Raw Input、HidHide、IAT、backend、plan hash、设备路径等技术词汇应该隐藏在 Diagnostics 或 Expert 设置中。

示例主界面：

```text
HydraSeat
──────────────────────────────────────

Games
[Minecraft] [Terraria] [Stardew] [...]

Seat 1                         Seat 2
Mario                          Luigi
Minecraft                      Terraria
Ready                          Ready

                 ▶ PLAY
```

当两边选择同一游戏：

```text
Seat 1                         Seat 2
Mario                          Luigi
Minecraft ═══════════════════ Minecraft

        Two-player setup ready

                 ▶ PLAY
```

---

## 首次 Seat 设置向导是可选的

第一次运行时可以提供：

```text
Welcome
  → 识别显示器
  → 分配 Seat 1 输入设备
  → 分配 Seat 2 输入设备
  → 可选手柄
  → 可选音频
  → 测试
  → 保存
```

用户可以跳过整个向导，也可以对单个项目选择 **以后设置**。

游戏启动前只检查该游戏真正需要的资源。

---

## Seat 独立生命周期

两名玩家不必同时开始，也不必同时结束。

如果 Luigi 退出 Terraria，而 Mario 继续 Minecraft：

```text
Seat 1                         Seat 2
Mario                          Luigi
Minecraft                      Idle
Playing                        可选择其他游戏
```

Seat 2 可以在不影响 Seat 1 的情况下启动另一款游戏，或者结束本次游玩。

只要还有一个 Seat 在运行游戏，空闲 Seat 就保持在简单的 HydraSeat 等待/游戏选择界面，而不是直接恢复为不受限制的普通 Windows 桌面。

当两个 Seat 都结束，或 Management UI 明确选择返回 Windows 时，HydraSeat 执行经过验证的 rollback，恢复普通单 PC Windows 状态。

---

## 最小 Seat Launcher，而不是完整桌面 Shell

HydraSeat v1 **只正式支持游戏场景**。

它管理选中的游戏，以及游戏所需的启动器、helper、子进程、窗口、输入/手柄/音频路由和恢复路径。

v1 不尝试为每个 Seat 制作完整独立的 Windows 桌面。

Idle Seat 只需要类似：

```text
Seat 2
Luigi

Minecraft
Terraria
Stardew Valley
More Games...

End Playing
```

游戏启动后 Seat Launcher 应消失或保持不干扰。

以下功能推迟到 v1 之后：

- Seat 独立 taskbar；
- Seat 独立 wallpaper / desktop zone；
- 把 Chrome、Office、Discord 等任意应用作为独立 Seat 应用自由运行；
- Seat 独立 clipboard 虚拟化；
- 完整替换 Windows Shell。

这是为了让单人开发项目保持聚焦：**两个人在一台 PC 上本地玩游戏。**

---

## 输入与运行时方案

Windows 的 foreground、cursor、keyboard state 和输入天然具有全局特征，不同游戏使用的 API 也不同。

因此 HydraSeat 把输入隔离视为 **兼容性问题**，而不是简单事件转发。

当前研究/实现覆盖或评估：

- Win32 Raw Input 设备识别与路由；
- HID / SetupAPI / ConfigMgr 稳定设备身份；
- 对声明 API 的 controlled process-local 输入虚拟化；
- XInput / DirectInput 兼容策略；
- 进程/窗口所有权；
- 适当情况下的 HidHide 等可选设备 visibility/isolation backend；
- watchdog、crash journal、emergency reset 和 rollback。

HydraSeat 不隐藏、绕过、禁用或规避 anti-cheat、DRM、protected process、账号限制、launcher 政策或游戏故意设置的 single-instance restriction。

---

## 受保护游戏：明确风险，而不是假装安全

受保护游戏仍可能值得实验，因为未来 HydraSeat 的改进、provider 变化或更干净的兼容路径可能使某些配置可用。因此没有必要永久硬性屏蔽所有 protected title。

但在对已知受保护游戏进行 HydraSeat 实验前，必须显示醒目警告：

> 该游戏使用 anti-cheat、DRM 或其他保护系统。HydraSeat 尚未证明此配置安全或兼容。游戏或保护系统可能阻止软件、拒绝启动、断开连接，或依据自身政策采取其他措施。HydraSeat 不会绕过或禁用保护功能。

只有用户明确选择高级实验后才继续。

**成功启动不等于证明 anti-cheat 安全。** 受保护游戏的数据必须持续标记为 Protected / Experimental。

---

## 不做官方支持徽章，展示真实兼容性证据

HydraSeat 不需要给每款游戏制作 `HydraSeat Certified` 官方徽章。

更诚实的方式是显示真实用户观察结果：

```text
Community results
87% 成功 (45 reports)
39 成功 / 6 失败

Launch              98%
Two instances       91%
Input isolation     89%
Audio routing       96%
Clean shutdown      99%
```

如果游戏版本、HydraSeat 版本、provider、Windows 版本或兼容路径明显不同，就不应盲目合并成一个误导性的百分比。

用户可看到简单状态：

- **Community results available** — 显示成功/失败比例与样本数量；
- **Untested** — 尚无有效证据，可执行本地测试；
- **Protected / Experimental** — 已知存在保护系统，需要明确风险确认。

百分比代表观察结果，不是保证。

---

## Local-first 兼容性测试

单一维护者不可能购买并测试所有游戏，所以 HydraSeat 本身应帮助用户结构化测试过程。

本地测试可以记录有限的证据，例如：

- 游戏/provider/version；
- HydraSeat 版本；
- 进程/实例启动结果；
- 预期实例数量；
- Seat/process/window 所有权；
- receiver-verified input 与测量到的 cross-Seat bleed；
- 音频路由结果；
- 干净退出与 rollback；
- 必要的 Windows/backend 元数据。

结果默认 **只保存在本地**。

提交社区数据必须显式 opt-in，并在上传前向用户显示实际 redacted JSON。

默认提交不能包含密码、token、raw typed text、Player 名称、个人绝对路径或不必要的稳定设备序列号。

---

## Offline-first

HydraSeat 核心功能不应要求 HydraSeat 云账号或持续联网。

离线可用：

- Seat 设置；
- Player 创建；
- 从本地 metadata 发现已安装游戏；
- 本地游戏库；
- 自动/手动 Two-player setup；
- 本地兼容测试；
- 启动已保存游戏/配置；
- 诊断与恢复。

可选在线功能：

- compatibility/profile catalog 更新；
- 用户明确同意的 community result 上传；
- HydraSeat 程序更新检查。

早期兼容数据可以通过 versioned JSON/catalog artifact 发布，无需一开始就维护自定义常在线后端服务。

---

## 更新策略

兼容性数据和 HydraSeat 程序本体采用不同更新周期。

**Compatibility/Profile 更新**体积小、变化频繁，可以独立刷新。用户可以关闭自动刷新并继续使用本地缓存。

**程序/runtime/driver 更新**必须得到用户明确批准。仅仅因为存在新版本，不应强制替换当前可正常工作的版本。

下载内容使用前必须验证版本、hash 和信任策略。

---

## 最小权限

只要 Windows 允许，HydraSeat 默认使用普通用户权限完成工作。

仅对真正需要权限提升的狭窄操作请求 UAC，例如安装、可选 driver/service 配置、某些系统级恢复或配置操作。

正常的游戏选择 → Play 流程不应要求以管理员身份运行主 UI。

---

## 安装程序是产品的一部分

普通用户不应该为了尝试 HydraSeat 而安装 Visual Studio、MSVC、Qt、CMake 并自行构建。

v1 Windows installer/uninstaller 必须覆盖：

- prerequisite 与 architecture 检查；
- 必要 runtime 安装；
- 仅在需要时安装可选 elevated component；
- 首次 Seat 设置；
- repair/uninstall；
- 安全 update/rollback；
- 安装失败诊断。

卸载后只删除 HydraSeat 自己拥有的持久状态，并确保普通 Windows 可正常使用。

---

## 当前开发状态

HydraSeat **目前还不是可直接安装使用的完成产品**。

已经建立的重要基础包括：

- Phase 0 研究与 clean-room policy；
- 稳定的 Windows hardware identity/detection；
- 两个 Seat 的硬件组合/保存基础；
- controlled Raw Input、polling/cursor/focus、XInput、DirectInput 兼容实验；
- input metrics 与物理 acceptance 工具；
- watchdog、crash journal、emergency reset 基础；
- 真实开源应用 compatibility test 路径；
- 早期 Phase 4 background runtime / IPC / process / window / display foundation。

真实两套输入设备的物理 acceptance 和真实游戏验证仍然是重要未完成门槛。合成测试或 HydraSeat 自有 controlled process 测试不会被夸大成通用游戏支持。

相关文档：

- [Implementation status](docs/implementation/STATUS.md)
- [Development roadmap](docs/ROADMAP.md)
- [Architecture](docs/ARCHITECTURE.md)
- [v1 product specification](docs/PRODUCT_V1.md)
- [Compatibility evidence](docs/COMPATIBILITY_MATRIX.md)

---

## Roadmap 方向

路线图现在针对 **单人开发者可完成的两 Seat 游戏产品**，而不是通用 Windows 多座席桌面平台。

```text
Phase 3  输入隔离 + 物理 evidence
   ↓
Phase 4  background runtime + 独立 Seat lifecycle + display/window ownership
   ↓
Phase 5  真实两 Seat gaming MVP
   ↓
Phase 6  游戏发现 + Player profile + 自动/手动同游戏 setup
   ↓
Phase 7  最小 Idle Seat Launcher UX
   ↓
Phase 8  installer + reliability + least privilege + offline update/catalog sync
   ↓
Phase 9  community compatibility/profile ecosystem
   ↓
Phase 10 release/legal/security/performance hardening
```

完整 packet 路线图见 [`docs/implementation/`](docs/implementation/README.md)。

---

## v1 发布成功条件

真正的 v1 需要完整用户流程通过，包括：

- clean install/uninstall；
- 恰好两个 v1 Seat；
- 真实双显示器/双输入设备验证；
- 测试配置中的客观 input isolation evidence；
- 两个不同真实游戏同时运行；
- 至少一个游戏/provider 本身允许的真实 same-title/two-instance 案例；
- Player profiles 与 game-first 选择；
- 自动游戏发现 + 手动添加 fallback；
- 自动/手动 Two-player setup 创建；
- 一个 Seat 退出或切换游戏时另一个 Seat 不被停止；
- Idle Seat Launcher；
- 两个 Seat 结束后验证恢复普通 Windows；
- watchdog/crash/emergency recovery；
- local-first compatibility JSON 与可选 community 分享；
- 使用本地数据时的 offline 运行；
- 用户批准的软件更新；
- 在把发行版称为开源之前解决项目 license/contribution terms。

目标不是第一天就宣布数十或数百款游戏的官方支持，而是通过真实 evidence 和可复用的社区知识持续扩大兼容性。

---

## v1 明确不做的事情

HydraSeat v1 不承诺：

- 超过两个活动 Seat；
- VM 或 Seat 独立 Windows 安装；
- Seat 独立 Windows 登录 session；
- 通用独立 Windows 桌面；
- 所有游戏的同游戏多实例；
- anti-cheat/DRM/account/launcher/single-instance 绕过；
- anti-cheat 安全认证；
- 强制 cloud account/telemetry；
- 与所有游戏兼容。

---

## 相关系统与 Clean-room 边界

HydraSeat 会研究 ASTER、ProtoInput、Nucleus Co-op、Universal Split Screen、HidHide、devreorder、Duo 以及 Microsoft 官方 Windows API 的公开资料和可观察行为。

研究对象的代码不会自动成为 HydraSeat 代码。专有系统只作为公开文档和普通 observable behavior 参考，任何外部 source 复用都必须符合许可证和 [Clean-room policy](docs/CLEAN_ROOM_POLICY.md)。

`C:\HydraSeat\references` 下的 reference checkout 只用于研究，不是 build input。

---

## 开发与验证

当前开发使用 C++20、CMake、Windows MSVC，UI 可选 Qt 6。

仓库明确区分：

- automated controlled evidence；
- real-process evidence；
- physical/manual evidence；
- community compatibility evidence。

合成测试通过不会自动提升为真实物理环境或通用游戏支持声明。

开发前：

```text
python tools/show_implementation_packet.py --current
python tools/validate_implementation_roadmap.py
```

Repository agent 在实现前必须阅读 [`.agents/AGENTS.md`](.agents/AGENTS.md)。

---

## 许可证与贡献

长期目标是一个可以合法复用和贡献的开源项目，但目前项目许可证和贡献条款尚未正式声明。

在解决之前：

- 不把当前仓库描述为法律意义上的开源；
- 不擅自假定外部代码的复用权；
- 遵循 [`docs/CLEAN_ROOM_POLICY.md`](docs/CLEAN_ROOM_POLICY.md)；
- 把 license/contribution 决策视为发布前必须完成的产品 gate。

法律门槛解决后，HydraSeat 的游戏设置、compatibility evidence、文档和贡献流程应尽量方便社区合法扩展。