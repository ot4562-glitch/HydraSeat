# HydraSeat 🎮

[English](README.md) | **한국어** | [简体中文](README.zh-CN.md)

하나의 물리 Windows PC를 여러 대의 독립적인 로컬 게이밍 PC처럼 느끼게 만드는 실험적 Windows 멀티시트 프레임워크입니다. 저장소 라이선스는 아직 공식 확정되지 않았습니다. 외부 코드 재사용 전 [클린룸 정책](docs/CLEAN_ROOM_POLICY.md)을 확인하세요.

> 이 문서는 한국어 README입니다. 구현 상태와 명령/식별자의 기준 문서는 [영문 README](README.md)입니다.

---

## 🎯 프로젝트 목표

HydraSeat의 목표는 VM, 원격 데스크톱, 게임 스트리밍 없이 **한 대의 물리 Windows PC를 여러 대의 독립적인 로컬 게이밍 PC처럼 느끼게 만드는 것**입니다.

핵심 단위는 모니터가 아니라 **Seat**입니다. 하나의 Seat는 여러 모니터와 입력/오디오/프로세스 자원을 함께 소유할 수 있습니다.

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
│  └─ Seat 1 소유 게임 / 앱
│
└─ Seat 2 — "Player 2 PC"
   ├─ BenQ Monitor     (Primary)
   ├─ Keyboard B
   ├─ Mouse B
   ├─ Controller B
   ├─ Speakers B
   └─ Seat 2 소유 게임 / 앱
```

목표 UX는 “모니터 세 대가 붙은 PC 한 대”가 아니라 **“같은 하드웨어를 공유하는 듀얼 모니터 PC 한 대 + 싱글 모니터 PC 한 대”**에 가깝습니다.

Windows의 foreground focus, cursor, keyboard state, 전역 desktop은 기본적으로 공유됩니다. HydraSeat는 로컬 게임에 필요한 부분을 Seat별로 가상화/중재해서 서로의 입력과 상태가 섞이지 않도록 하는 방향으로 개발됩니다.

### 제품의 북극성: 고성능 PC 한 대를 여러 명의 로컬 게임 자리로

HydraSeat는 연구용 입력 격리 실험에 머무르지 않고, 실제 가정/친구 단위에서 사용할 수 있는 제품을 목표로 합니다. PC에 충분한 CPU, GPU, 메모리, 디스플레이와 입력 장치 여유가 있다면 가족이나 친구가 Windows 게이밍 PC 한 대를 두 개 이상의 로컬 게이밍 자리로 나누어 동시에 사용할 수 있어야 합니다.

대표 사용 시나리오는 다음과 같습니다.

- 두 사람이 서로 다른 게임을 각자의 Seat에서 동시에 플레이;
- 게임, 런처, 계정/라이선스 규칙, 단일 실행 제한, HydraSeat 호환성 프로필이 모두 허용하는 경우에만 같은 멀티플레이 게임의 서로 다른 인스턴스를 각 Seat에서 실행;
- 한 사람은 게임을 하고 다른 Seat에서는 다른 게임이나 일반 앱을 사용;
- 재부팅이나 개발자 전용 복구 절차 없이 일반적인 Windows 한 대 상태로 되돌리기.

HydraSeat는 DRM, 안티치트, 계정 제한, 런처 제한, 보호 프로세스, 게임의 단일 실행 보호를 우회하지 않습니다. 따라서 같은 게임을 여러 Seat에서 동시에 실행하는 기능은 게임별 호환성 기능이지 모든 게임에 대한 보장이 아닙니다.

제품화의 성공 기준에는 쉬운 장치/Seat 설정, 측정된 낮은 오버헤드, 정확한 호환성 근거, 안전한 설치/업데이트/제거/복구, 그리고 향후 커뮤니티가 프로필·호환성 결과·진단·확장 기능을 기여하기 쉬운 형식도 포함됩니다. 장기적으로는 널리 사용할 수 있는 오픈소스 프로젝트로 배포하는 것을 목표로 하지만, 현재 저장소는 라이선스와 기여 조건이 아직 공식 확정되지 않았으므로 그 법적 게이트가 해결되기 전에는 현재 상태를 오픈소스라고 표현하지 않습니다.

### Management Seat와 백그라운드 동작

기본적으로 **Seat 1이 Management Seat**입니다. 분할 세션이 활성 상태일 때 `HydraSeat.exe`를 열면 관리 콘솔은 Seat 1의 주 모니터에 표시됩니다.

관리창을 닫는 것은 분할 세션 종료가 아닙니다. `hydra_host.exe`와 `hydra_watchdog.exe`는 백그라운드에서 계속 동작하며, HydraSeat를 다시 열면 기존 Host에 연결해 Management Seat 화면에 관리 콘솔을 다시 표시합니다.

```text
HydraSeat — Management Seat
├─ 현재 상태: Normal Windows / Starting / Split Active / Degraded / Recovery Required
├─ Seat 1: LG + Samsung | Keyboard A | Mouse A | Controller A | Headset
├─ Seat 2: BenQ         | Keyboard B | Mouse B | Controller B | Speakers
│
├─ [ 분할 세션 시작 ]
├─ [ Windows 한 PC로 되돌리기 ]
├─ [ 모니터 및 입력 재구성 ]
├─ [ 장치 식별 / 테스트 ]
├─ [ 시작 방식 ] Manual | Background Idle | Auto-Activate Validated Session
├─ [ 진단 ]
└─ [ 복구 / Reset ]
```

`Stop / Return to Windows`는 단순히 창을 닫는 버튼이 아닙니다. Seat별 입력/장치/창/디스플레이/오디오/컨트롤러/Shell 상태를 역순으로 원복하고, 일반 Windows 입력과 디스플레이 상태가 실제로 복구되었는지 확인한 뒤에만 `Stopped`/`Idle` 상태가 됩니다.

`Reconfigure` 기본 흐름은 다음과 같습니다.

```text
Active split session
  -> Stop / Return to Windows + rollback 검증
  -> Management Seat 화면에서 구성 UI 열기
  -> 모니터 / 키보드 / 마우스 / 컨트롤러 / 오디오 식별 및 재할당
  -> validate + 새 plan 저장
  -> Start Now 또는 일반 Windows 상태 유지
```

지원할 시작 방식은 세 가지입니다.

- **Manual** — 사용자가 HydraSeat를 열고 Start를 누를 때만 시작합니다.
- **Background Idle** — 로그인 시 Host/Watchdog만 조용히 실행되고, PC는 평범한 한 대 상태로 유지됩니다. 필요할 때 관리 콘솔에서 Start를 누릅니다.
- **Auto-Activate Validated Session** — 명시적으로 선택된 이전 검증 구성만 자동 복원합니다. crash journal, safe mode, 하드웨어/토폴로지, capability, privilege, watchdog, rollback preflight가 모두 통과해야 하며 실패하면 안전하게 Idle로 남습니다.

### 언어 지원

UI/UX의 기준 언어는 영어 `en-US`입니다. 초기 출시 대상 언어는 다음과 같습니다.

- English — `en-US`
- 한국어 — `ko-KR`
- 简体中文 — `zh-CN`

사용자에게 보이는 문자열은 안정적인 localization message ID와 영어 fallback을 사용하도록 설계합니다. 반대로 다음 항목은 영어 식별자를 유지하고 번역하지 않습니다.

- 소스코드 주석과 개발자용 docstring;
- 프로토콜/스키마 키;
- CLI 옵션;
- diagnostic/error code;
- capability/backend/profile/packet ID.

세부 규칙은 [Localization Policy](docs/LOCALIZATION.md)를 참고하세요. 현재 README 번역이 존재한다고 해서 런타임 UI 번역 기능까지 이미 구현됐다는 뜻은 아닙니다. UI 국제화 구현은 `P7-I18N-01`에 추적됩니다.

---

## 🪑 Seat 모델

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

예를 들면 다음 구성이 1급 사용 사례입니다.

```text
Seat 1 = LG + Samsung + Keyboard A + Mouse A + DualSense + Headset
Seat 2 = BenQ + Keyboard B + Mouse B + Xbox Controller + Speakers
Seat 3 = Living-room TV + Controller C
```

하나의 모니터가 하나의 Seat라는 가정은 하지 않습니다. 여러 모니터가 하나의 Seat display group으로 묶일 수 있습니다.

---

## 🖥️ Seat별 로컬 PC 경험

HydraSeat는 VM이나 독립 Windows 설치를 만드는 대신 가벼운 **Seat Shell**을 목표로 합니다.

각 Seat는 장기적으로 다음을 가질 수 있습니다.

- 자기 launcher/desktop surface;
- taskbar 형태의 자기 창 목록;
- wallpaper/profile;
- 자기 process/window group;
- 하나 이상의 모니터와 primary display;
- Seat 모니터 그룹 안에서 움직이는 자기 cursor;
- 자기 audio input/output;
- 자기 game launcher/profile 설정.

실제 Windows 좌표는 하나의 전역 desktop에 남아 있지만, HydraSeat는 Seat-local 좌표 변환을 유지합니다.

```text
Seat 1
LG       -> local (0, 0), primary
Samsung  -> local (2560, 0)

Seat 2
BenQ     -> local (0, 0), primary
```

---

## ⌨️ 입력 격리 목표

Raw Input으로 물리 장치를 구분하는 것만으로는 충분하지 않습니다. 게임은 전역 keyboard/mouse/cursor/focus 상태를 직접 읽을 수 있습니다.

최종 목표는 다음과 같습니다.

```text
Keyboard A + Mouse A -> Seat 1 applications only
Keyboard B + Mouse B -> Seat 2 applications only

Seat 1 입력은 Seat 2 게임에 들어가지 않음
Seat 2 입력은 Seat 1 게임의 제어권을 빼앗지 않음
```

Phase 3은 입력 격리를 단순 이벤트 전달 문제가 아닌 호환성 문제로 다룹니다. 프로필에 따라 다음 조합이 필요할 수 있습니다.

- Win32 Raw Input routing;
- HID / SetupAPI physical-device identity;
- per-process input compatibility hooks;
- virtual keyboard/mouse/cursor state;
- foreground/focus mediation;
- 필요 시 HidHide 같은 선택적 device visibility/isolation backend.

ProtoInput, Universal Split Screen, Nucleus Co-op, HidHide와 Microsoft 공식 Windows API 문서는 연구 자료로 활용할 수 있습니다. 소스 재사용은 라이선스가 호환되는 경우에만 하며, 그렇지 않으면 공개 문서와 관찰 가능한 동작을 바탕으로 독립 구현합니다.

---

## 🎮 Process / Game 소유권

HydraSeat를 통해 실행한 앱은 단순히 모니터가 아니라 Seat에 속합니다.

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

Seat-aware window manager는 해당 프로세스의 창만 올바른 display group 안에 유지하고, 새 창 생성/전체화면 전환/재시작 때도 소유권을 다시 검증합니다.

---

## 🔊 오디오 격리

```text
Seat 1 games/apps -> Headset A
Seat 1 voice chat  -> Microphone A

Seat 2 games/apps -> Speakers B
Seat 2 voice chat  -> Microphone B
```

Per-application audio endpoint routing은 최종 Seat 모델의 일부입니다.

---

## 🧭 설계 원칙

1. **VM 필수 아님** — 게임은 host Windows에서 직접 실행합니다.
2. **모니터가 아니라 Seat가 소유 단위** — 한 Seat는 여러 display를 가질 수 있습니다.
3. **물리 장치 identity는 결정적이어야 함** — 같은 모델 USB/HID도 구분되어야 합니다.
4. **Zero cross-Seat input bleed가 목표** — Raw Input 관찰만으로 성공 처리하지 않습니다.
5. **게임이 자신이 로컬에서 active라고 느끼도록 호환성 처리** — foreground/cursor/keyboard state/Raw Input API 대응이 필요할 수 있습니다.
6. **구현 상태를 과장하지 않음** — 연구/프로토타입은 그대로 표시합니다.
7. **공식 Windows API와 라이선스 호환 오픈소스 우선** — 필요한 경우 clean-room 독립 구현합니다.
8. **게이밍 우선** — enterprise VDI나 일반 사무용 멀티시트 제품이 목표가 아닙니다.
9. **소스코드 주석은 영어** — UI 문자열과 개발자 식별자를 섞지 않습니다.

---

## 🚫 비목표

HydraSeat는 다음을 목표로 하지 않습니다.

- 학교 전산실 멀티시트 소프트웨어
- 기업용 사무 멀티시트
- 원격 데스크톱
- 클라우드 게임 서비스
- 기업용 VM 관리자
- hypervisor 또는 Windows kernel/session manager 대체품

---

## 🏗️ 아키텍처

```text
HydraSeat.exe
  Management Seat용 온디맨드 제어 콘솔
        │
        ▼
hydra_host.exe
  권위 있는 per-user background runtime
        │
        ├── hydra_watchdog.exe
        ├── Seat 1 process group / adapters / shell
        └── Seat 2 process group / adapters / shell

hydra_reset.exe
  UI/Host와 독립적인 응급 원복 경로
```

주요 기술 영역:

- **GUI / Seat Shell**: Qt 6 / Win32
- **Input Detection**: Win32 Raw Input, HID, SetupAPI / ConfigMgr
- **Input Compatibility**: Raw Input routing, 필요 시 process-local compatibility hooks, optional HID visibility backends
- **Controllers**: XInput + HID/DirectInput
- **Display**: `EnumDisplayMonitors`, `EnumDisplayDevices`, DXGI, `QueryDisplayConfig`
- **Virtual Displays**: 이후 Phase의 IddCx / IDD 또는 호환 adapter
- **Process Management**: Windows process APIs / Job Objects
- **Audio**: Windows Core Audio per-application endpoint routing
- **Launcher**: Steam, Epic, EA, GOG, generic executable profiles

---

## 📍 현재 개발 상태

- **Phase 0 — Research & Foundation:** 완료.
- **Phase 1 — Hardware Detection:** 완료, Windows/MSVC CI 검증됨.
- **Phase 2 — Seat Composition / Assignment UI:** 현재 Win32 프로토타입에서 완료. Multi-display Seat, primary display, exclusive device ownership, validated JSON profile 포함.
- **Phase 3 — Input Compatibility / Isolation:** 현재 단계. Capability planner, Gate A/B observation, Gate C controlled-process protocol/adapter 기반까지 구현됨. **물리 acceptance, 실제 Windows API interposition, device cloaking, 게임 zero-bleed 검증은 아직 완료되지 않음.**
- **이후 Phase:** background runtime, process/window/display ownership, two-game MVP, profile/launcher, Seat shell, localization, watchdog/installer/update, SDK, release hardening.

가장 중요한 성공 조건은 창 두 개를 띄우는 것이 아니라 **서로 다른 두 사용자가 서로 다른 게임을 동시에 실행하면서 상대 Seat의 키보드/마우스 입력과 focus 동작에 간섭하지 않는 것**입니다.

---

## 🧪 Phase 3 도구

`hydra_plan`은 compatibility profile을 가정된 backend environment에 대해 분석하는 진단 전용 도구입니다. process injection, driver 설치, device hiding, physical suppression을 수행하지 않습니다.

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

현재 Gate A/B lab은 physical input을 관찰/분류하지만 일반 Windows 입력을 억제하지 않습니다. 물리 두 키보드/두 마우스 acceptance는 아직 실행이 필요합니다. [Gate A/B 테스트 문서](docs/PHASE3_GATE_A_B_TESTING.md)를 참고하세요.

### Gate C Controlled Process Lab

```powershell
.\build\Release\hydra_gate_c_host.exe `
  --self-test `
  --target .\build\Release\hydra_gate_c_target.exe

.\build\Release\hydra_gate_c_host.exe `
  --profile workspace_config.json `
  --trace hydra_gate_c_host.jsonl
```

Gate C는 HydraSeat 소유 controlled target만 실행합니다. Controlled probe용 process-local polling 및 cursor/clip/logical-focus/capture shim은 x64/x86 Windows CI에서 검증됐습니다. 별도 Raw Input behavior probe와 bounded trace/parser도 Windows run `32800513365`에서 native x64/x86 검증됐습니다. XInput generation/snapshot 정확성 수정은 remediation head `b351afdd`를 대상으로 한 fork PR #15 run `32832036967`에서 native x64/x86 36/36과 x64-host→x64/x86 zero-cross controller acceptance를 통과해 `VALIDATED` 상태입니다. 기존 run `32816241577`은 수정 전 근거로만 남습니다. Remote injection, physical suppression, third-party/commercial target 지원은 구현하지 않습니다.

관련 문서:

- [Related systems research](docs/RELATED_SYSTEMS_RESEARCH.md)
- [Phase 3 input isolation design](docs/PHASE3_INPUT_ISOLATION_DESIGN.md)
- [Clean-room policy](docs/CLEAN_ROOM_POLICY.md)
- [Gate A/B testing](docs/PHASE3_GATE_A_B_TESTING.md)
- [Gate C testing](docs/PHASE3_GATE_C_TESTING.md)
- [Localization policy](docs/LOCALIZATION.md)

---

## 🧱 Codex 구현 로드맵

향후 작업은 Codex가 아키텍처를 재발명하거나 물리/게임 검증을 임의 완료 처리하지 않도록 bounded packet으로 나뉩니다.

현재 기본 packet:

```text
P8-RESET-01 — 비상 리셋 CLI (CODE_COMPLETE; reset-focused 테스트는 로컬 x64/x86에서 각각 3/3 PASS이고 x64 host→x64/x86 Gate C cross 회귀도 PASS. 현재 interactive 전체 suite는 기존 live-desktop cursor/focus assertion 1건 때문에 양쪽 모두 55/56이며, exact-head non-interactive fork CI와 실제 emergency shortcut/task 실행은 VALIDATED 전까지 PENDING; P3-D-02는 P3-HW-01 물리 승인과 VALIDATED된 P8-RESET-01 때문에 계속 BLOCKED)
```

작업 시작 전:

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

UI 국제화는 `P7-I18N-01`에서 구현하며, 영어/한국어/중국어 UI 실제 acceptance 전에는 완료로 표시하지 않습니다.

---

## 🛠️ 빌드 요구사항

- **OS**: Windows 10 / Windows 11 (64-bit)
- **Compiler**: Visual Studio 2022 / MSVC C++20
- **Build System**: CMake 3.20+
- **Framework**: Qt 6.x (Widgets / Core)
- **Windows SDK**: Windows 10/11 SDK

---

## 🚀 로드맵 / 상세 문서

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
