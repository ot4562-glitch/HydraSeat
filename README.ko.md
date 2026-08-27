# HydraSeat 🎮

[English](README.md) | **한국어** | [简体中文](README.zh-CN.md)

> **성능 좋은 Windows 게이밍 PC 한 대. 로컬 사용자 두 명. 각자의 모니터·입력장치·컨트롤러·오디오. VM, 원격 데스크톱, 게임 스트리밍 없이.**

HydraSeat은 한 대의 게이밍 PC에 아직 쓸 수 있는 성능이 남아 있는데도, 두 사람이 동시에 게임하기 위해 두 번째 데스크톱 전체를 구매하고 관리해야 하는 부담을 줄여보려는 실험적 Windows 로컬 게이밍 멀티시트 프로젝트입니다.

v1의 목표는 의도적으로 좁습니다.

> **충분한 성능을 가진 Windows 게이밍 PC 한 대의 남는 리소스를 두 사람이 두 개의 로컬 게이밍 좌석으로 나눠 쓸 수 있게 한다.**

HydraSeat은 공개적으로 개발되고 있으며 장기적으로 오픈소스 배포를 목표로 합니다. 다만 **현재 저장소의 프로젝트 라이선스와 기여 조건은 아직 공식 확정되지 않았기 때문에**, 현 시점의 저장소를 법적으로 오픈소스라고 표현해서는 안 됩니다. 자세한 내용은 [클린룸 및 라이선스 정책](docs/CLEAN_ROOM_POLICY.md)을 참고하세요.

---

## 왜 만드는가

게이밍 PC의 CPU, GPU, 메모리 성능은 계속 높아지지만 많은 게임은 항상 컴퓨터의 모든 성능을 사용하지 않습니다. 그 결과 한 사람이 게임을 하고 있어도 상당한 처리 능력이 남는 경우가 있습니다.

하지만 같은 집의 두 번째 사람이 동시에 게임하려면 보통 또 하나의 완전한 데스크톱이 필요합니다.

HydraSeat은 이 지점을 겨냥합니다.

```text
Windows 게이밍 PC 한 대
│
├─ Seat 1
│  ├─ 모니터 A
│  ├─ 키보드 / 마우스 A
│  ├─ 컨트롤러 A
│  └─ 오디오 A
│
└─ Seat 2
   ├─ 모니터 B
   ├─ 키보드 / 마우스 B
   ├─ 컨트롤러 B
   └─ 오디오 B
```

주요 대상은 이미 성능 좋은 PC 한 대를 가지고 있고, 두 번째 데스크톱을 추가로 사는 대신 남는 성능을 함께 사용하고 싶은 커플, 형제자매, 룸메이트, 가족, 친구입니다.

물론 HydraSeat은 어떤 PC에서든 게임 두 개가 원활하게 돌아간다고 약속하지 않습니다. 선택한 게임 두 개를 동시에 감당할 CPU, GPU, 메모리, 저장장치, 디스플레이 출력과 주변기기가 실제로 필요합니다.

---

## v1은 정확히 2 Seat

HydraSeat v1은 **두 개의 로컬 게이밍 Seat**만 제품 범위로 잡습니다.

내부 코드는 구조적으로 자연스러운 경우 배열/컬렉션을 유지할 수 있지만, v1 UI, 인스톨러, 테스트, 제품 약속과 호환성 데이터는 최대 두 개의 활성 Seat를 기준으로 설계합니다.

3~4 Seat를 지원하면 모니터, 키보드, 마우스, 컨트롤러, 오디오 장치, 물리적 공간, 테스트 조합이 급격히 늘어납니다. 실제 가정 사용자는 훨씬 적을 가능성이 높으므로, HydraSeat은 먼저 **2 Seat를 실제 제품으로 완성하는 것**을 우선합니다.

---

## 핵심 개념

HydraSeat은 **Seat / Player / Game / 2인용 게임 설정 / Runtime Session**을 서로 분리합니다.

### 🪑 Seat = 물리적인 좌석

Seat는 PC방의 좌석과 비슷합니다. 사람이나 게임이 아니라 물리 장치를 설명합니다.

```text
Seat 1
├─ 디스플레이
├─ 키보드       필요할 때까지 미설정 가능
├─ 마우스       필요할 때까지 미설정 가능
├─ 컨트롤러     선택
└─ 오디오       선택
```

Seat 설정은 완전하지 않아도 됩니다. 최초 설정 마법사를 건너뛸 수 있고, 장치 항목을 `나중에 설정`으로 둘 수 있으며, 이후 Seat 설정에서 다시 지정할 수 있습니다.

게임 실행 시 HydraSeat이 그 게임이 실제로 요구하는 장치를 확인합니다. 예를 들어 컨트롤러만 쓰는 게임이라면 키보드가 없다는 이유만으로 Seat 전체를 오류로 만들지 않습니다.

### 👤 Player = 사람

Player는 Seat와 독립된 가벼운 사용자 프로필입니다.

Player에는 다음 정도를 기억할 수 있습니다.

- 표시 이름과 선택적 로컬 아바타
- 최근 게임
- 최근 사용 Seat
- 게임별 인스턴스/데이터 폴더 선택
- 지원되는 경우 기존 게임 런처가 이미 인증해 둔 계정에 대한 참조

Mario가 오늘 Seat 1을 쓰고 내일 Seat 2를 사용해도 게임 계정/인스턴스 연결은 Mario를 따라갑니다.

HydraSeat 자체를 비밀번호 관리자로 만들지는 않습니다. 가능하면 Steam, Minecraft 런처 등 원래 제공자가 인증 정보를 계속 소유하고, HydraSeat은 이미 인증된 계정 중 어떤 것을 사용할지 선택하는 최소한의 참조만 기억합니다.

### 🎮 Game = 설치된 게임

지원되는 런처와 로컬 설치 정보를 이용해 설치된 게임을 자동 감지하는 것이 기본 UX입니다.

모르는 게임이나 특수 설치는 파워유저가 직접 EXE를 추가할 수 있습니다.

게임 이미지는 대규모로 자체 재배포하기보다 가능한 경우 로컬 EXE, 바로가기, 런처 메타데이터에 이미 존재하는 아이콘을 이용합니다.

### 🔁 2인용 게임 설정 = 같은 게임을 두 번 실행하기 위한 레시피

Seat 1과 Seat 2가 서로 다른 게임을 실행할 때 사용자가 별도 Profile 개념을 의식할 필요는 없습니다.

두 Seat가 **같은 게임**을 선택했을 때만 HydraSeat이 해당 게임의 2인용 설정을 찾거나 생성합니다.

예를 들면 다음 정보가 들어갈 수 있습니다.

- 인스턴스별 데이터/설정 폴더
- 실행 인수와 working directory
- 런처/provider별 실행 방식
- 런처가 허용하는 경우 Player별 계정 참조
- 실행 순서
- 창 식별 및 배치
- 입력/컨트롤러/오디오 요구사항
- 알려진 제약

가능하면 HydraSeat이 자동으로 생성하고, 자동화가 이해하지 못하는 게임을 위해 **수동 Guided Editor**도 제공합니다.

> **HydraSeat은 게임과 제공자가 허용하는 범위에서 멀티인스턴스 구성을 자동화합니다. 제한을 깨서 멀티인스턴스를 만들지는 않습니다.**

### ▶ Runtime Session = 지금 실행 중인 조합

```text
Seat 1 + Mario + Minecraft 인스턴스 A
Seat 2 + Luigi + Minecraft 인스턴스 B
```

Seat는 하드웨어이고, Player는 사람이며, 2인용 설정은 재사용 가능한 게임 지식입니다. 현재 실행 조합만 Runtime Session입니다.

자세한 기준은 [HydraSeat v1 제품 명세](docs/PRODUCT_V1.md)를 참고하세요.

---

## 게임 중심 UI/UX

HydraSeat은 Windows 관리 도구가 아니라 **가벼운 게임 런처처럼 보여야 합니다.**

기본 흐름은 다음과 같습니다.

```text
HydraSeat 실행
    ↓
게임 선택
    ↓
Seat 1 / Seat 2 / Both 선택
    ↓
각 Seat의 Player 선택
    ↓
필요한 경고만 확인
    ↓
Play
```

클릭이 기본 조작이고, 게임 아이콘을 Seat 카드로 드래그하는 방식은 편의 기능으로 함께 제공할 수 있습니다.

Raw Input, HidHide, IAT, backend, plan hash, 장치 경로 같은 기술 용어는 일반 화면에서 숨기고 Diagnostics 또는 Expert 설정에만 둡니다.

예상 메인 화면은 최대한 단순합니다.

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

같은 게임을 양쪽에 배치하면:

```text
Seat 1                         Seat 2
Mario                          Luigi
Minecraft ═══════════════════ Minecraft

          2인용 설정 준비됨

                 ▶ PLAY
```

---

## 최초 Seat 설정 마법사는 선택 사항

처음 실행할 때 다음 마법사를 제안할 수 있습니다.

```text
Welcome
  → 모니터 식별
  → Seat 1 입력장치
  → Seat 2 입력장치
  → 선택적 컨트롤러
  → 선택적 오디오
  → 테스트
  → 저장
```

사용자는 전체 마법사를 건너뛰거나 각 항목을 **나중에 설정**으로 둘 수 있습니다.

게임 시작 직전에 해당 게임에 필요한 장치만 검사합니다.

---

## Seat별 독립 실행 수명주기

두 사람이 반드시 동시에 시작하고 동시에 종료할 필요가 없습니다.

Luigi가 Terraria를 종료하고 Mario가 Minecraft를 계속하면:

```text
Seat 1                         Seat 2
Mario                          Luigi
Minecraft                      Idle
Playing                        다른 게임 선택 가능
```

Seat 2는 Seat 1을 건드리지 않고 다른 게임을 실행하거나 오늘 플레이를 종료할 수 있습니다.

하나 이상의 Seat가 아직 게임 중이라면 비어 있는 Seat를 일반 Windows 바탕화면으로 돌려보내지 않고, 최소한의 HydraSeat 대기/게임 선택 화면을 유지합니다.

두 Seat가 모두 끝났거나 Management UI에서 명시적으로 `Windows로 돌아가기`를 선택하면 HydraSeat이 검증된 rollback을 수행해 한 대의 일반 Windows PC 상태로 복원합니다.

---

## Full Shell이 아니라 최소 Seat Launcher

HydraSeat v1은 **게임 전용**입니다.

선택된 게임과 그 게임에 필요한 런처, helper, child process, 창, 입력/컨트롤러/오디오 경로와 복구를 관리합니다.

Seat마다 독립적인 완전한 Windows 데스크톱을 만들지는 않습니다.

Idle Seat 화면은 이 정도면 충분합니다.

```text
Seat 2
Luigi

Minecraft
Terraria
Stardew Valley
More Games...

End Playing
```

게임이 실행되면 Seat Launcher는 사라지거나 방해되지 않는 상태가 됩니다.

v1에서 뒤로 미루는 기능:

- Seat별 독립 taskbar
- Seat별 wallpaper/desktop zone
- Chrome/Office/Discord 같은 임의 앱을 독립 Seat 앱으로 자유롭게 운영
- Seat별 clipboard 가상화
- Windows Shell 전체 대체

1인 개발 프로젝트의 범위를 **두 사람이 한 PC에서 게임한다**는 목적에 집중시키기 위한 결정입니다.

---

## 입력과 런타임 접근 방식

Windows는 foreground, cursor, keyboard state, 입력을 기본적으로 전역 개념으로 다루며 게임마다 사용하는 API도 다릅니다.

HydraSeat은 입력 격리를 단순한 이벤트 전달이 아니라 **게임 호환성 문제**로 봅니다.

현재 연구/구현 영역에는 다음이 포함됩니다.

- Win32 Raw Input 장치 식별 및 라우팅
- HID / SetupAPI / ConfigMgr 기반 안정적인 장치 ID
- 선언된 API에 한정한 controlled process-local 입력 가상화
- XInput / DirectInput 호환성 정책
- 프로세스/창 소유권
- 필요한 경우 HidHide 같은 선택적 장치 visibility/isolation backend
- watchdog, crash journal, emergency reset, rollback

HydraSeat은 anti-cheat, DRM, protected process, 계정 제한, launcher 정책, deliberate single-instance restriction을 숨기거나 우회하거나 비활성화하지 않습니다.

---

## 보호 게임: 막연히 안전하다고 하지 않는다

보호 시스템이 있는 게임도 미래의 HydraSeat 개선이나 제공자 변화로 특정 구성이 동작할 가능성은 있기 때문에 영구적으로 모든 시도를 차단할 필요는 없습니다.

대신 알려진 보호 게임을 HydraSeat에서 실험하려 할 때 강한 경고를 표시합니다.

> 이 게임은 anti-cheat, DRM 또는 기타 보호 시스템을 사용합니다. HydraSeat은 이 구성이 안전하거나 호환된다고 검증하지 않았습니다. 게임 또는 보호 시스템이 실행을 차단하거나, 연결을 종료하거나, 자체 정책에 따른 다른 조치를 할 수 있습니다. HydraSeat은 보호 기능을 우회하거나 비활성화하지 않습니다.

사용자가 명시적으로 고급 실험에 동의한 경우에만 진행합니다.

**실행에 성공했다고 anti-cheat 안전성이 입증되는 것은 아닙니다.** 보호 게임의 결과는 계속 Protected / Experimental로 구분합니다.

---

## 공식 지원 딱지 대신 실제 호환성 데이터

HydraSeat은 게임마다 `HydraSeat Certified` 같은 공식 지원 딱지를 만들 필요가 없습니다.

대신 실제 사용자들이 관찰한 결과를 투명하게 보여주는 방향을 목표로 합니다.

```text
Community results
87% 성공 (45 reports)
39 성공 / 6 실패

Launch              98%
Two instances       91%
Input isolation     89%
Audio routing       96%
Clean shutdown      99%
```

게임 버전, HydraSeat 버전, provider, Windows 버전, 사용된 호환성 경로가 크게 다르면 같은 통계에 무작정 합치지 않습니다.

사용자에게 보이는 상태는 단순하게 둘 수 있습니다.

- **Community results available** — 성공/실패 비율과 표본 수 표시
- **Untested** — 아직 의미 있는 데이터 없음, 로컬 테스트 가능
- **Protected / Experimental** — 보호 시스템 확인, 명시적 위험 동의 필요

퍼센트는 실제 관찰 데이터이지 보증이 아닙니다.

---

## 로컬 우선 호환성 테스트

한 명의 유지보수자가 모든 게임을 구매하고 검증할 수 없기 때문에 HydraSeat 자체가 사용자의 테스트를 구조화해야 합니다.

로컬 테스트는 다음과 같은 제한된 정보를 측정할 수 있습니다.

- 게임/provider/version
- HydraSeat 버전
- 프로세스/인스턴스 실행 성공 여부
- 예상 인스턴스 수
- Seat/process/window 소유권
- receiver-verified input 및 측정된 cross-Seat bleed
- 오디오 라우팅 결과
- 종료 및 rollback 결과
- 필요한 Windows/backend 정보

결과는 **기본적으로 로컬에만 저장**합니다.

커뮤니티 공유는 명시적 opt-in이며, 업로드 전 실제 redacted JSON을 사용자가 확인할 수 있게 합니다.

기본 제출 데이터에는 비밀번호, 토큰, raw typed text, Player 이름, 개인 경로, 불필요한 장치 serial 같은 정보가 들어가면 안 됩니다.

---

## Offline-first

HydraSeat 핵심 기능에는 HydraSeat 계정이나 지속적인 인터넷 연결이 필요하지 않아야 합니다.

오프라인에서 가능한 기능:

- Seat 설정
- Player 생성
- 로컬에 존재하는 설치 게임 감지
- 게임 라이브러리
- 자동/수동 2인용 게임 설정
- 로컬 호환성 테스트
- 저장된 게임/설정 실행
- 진단 및 복구

선택적 온라인 기능:

- compatibility/profile catalog 업데이트
- 사용자가 동의한 community result 업로드
- HydraSeat 프로그램 업데이트 확인

초기에는 별도 상시 서버를 운영하지 않고도 versioned JSON/catalog artifact로 커뮤니티 호환성 데이터를 배포할 수 있도록 설계합니다.

---

## 업데이트 정책

호환성 데이터와 HydraSeat 프로그램 본체의 업데이트 주기를 분리합니다.

**Compatibility/Profile 업데이트**는 작고 자주 변하기 때문에 가볍게 갱신할 수 있습니다. 사용자는 자동 갱신을 끄고 기존 로컬 캐시만 사용할 수도 있습니다.

**프로그램/런타임/드라이버 업데이트**는 사용자 승인을 받아야 합니다. 새 버전이 있다는 이유만으로 현재 정상 작동하는 버전을 강제로 교체하지 않습니다.

다운로드된 파일은 사용 전 버전/해시/신뢰 검증을 거칩니다.

---

## 최소 권한

Windows에서 가능한 작업은 일반 사용자 권한으로 수행하는 것을 기본으로 합니다.

설치, 선택적 driver/service 구성, 실제 시스템 수준 변경이나 복구처럼 관리자 권한이 필요한 좁은 작업에만 UAC를 요청합니다.

일반적인 게임 선택 → Play 과정에서 HydraSeat 메인 UI를 관리자 권한으로 실행하도록 요구하지 않는 것이 목표입니다.

---

## 인스톨러는 필수 제품 기능

일반 사용자가 HydraSeat을 사용하기 위해 Visual Studio, MSVC, Qt, CMake를 설치하고 직접 빌드해야 해서는 안 됩니다.

v1 Windows installer/uninstaller는 다음을 포함해야 합니다.

- prerequisites 및 architecture 검사
- 필요한 runtime 설치
- 필요한 경우에만 optional elevated component 설치
- 최초 Seat 설정
- repair/uninstall
- 안전한 update/rollback
- 설치 실패 진단

Uninstall 후에는 HydraSeat이 소유한 영구 상태만 제거되고 정상 Windows 사용이 가능해야 합니다.

---

## 현재 개발 상태

HydraSeat은 **아직 일반 사용자가 바로 설치해 쓰는 완성 제품이 아닙니다.**

현재까지 구축된 주요 기반:

- Phase 0 연구 및 clean-room 정책
- 안정적인 Windows hardware identity/detection
- 2 Seat 하드웨어 구성/저장 기반
- controlled Raw Input, polling/cursor/focus, XInput, DirectInput 호환성 실험
- input metrics 및 물리 acceptance 도구
- watchdog, crash journal, emergency reset 기반
- 실제 오픈소스 애플리케이션 compatibility test 경로
- 초기 Phase 4 background runtime / IPC / process / window / display foundation

특히 실제 두 입력장치 세트를 사용하는 물리 acceptance와 실제 게임 검증은 아직 중요한 미완료 게이트입니다. 합성 테스트나 HydraSeat 자체 controlled process 테스트를 일반 게임 지원으로 과장하지 않습니다.

자세한 상태:

- [구현 상태](docs/implementation/STATUS.md)
- [개발 로드맵](docs/ROADMAP.md)
- [아키텍처](docs/ARCHITECTURE.md)
- [v1 제품 명세](docs/PRODUCT_V1.md)
- [호환성 evidence](docs/COMPATIBILITY_MATRIX.md)

---

## 로드맵 방향

로드맵은 범용 Windows 멀티시트 데스크톱보다 **1인 개발자가 완성 가능한 2 Seat 게이밍 제품**에 맞춥니다.

```text
Phase 3  입력 격리 + 물리 evidence
   ↓
Phase 4  background runtime + 독립 Seat lifecycle + display/window ownership
   ↓
Phase 5  실제 2 Seat gaming MVP
   ↓
Phase 6  게임 자동 감지 + Player profile + 자동/수동 same-game setup
   ↓
Phase 7  최소 Idle Seat Launcher UX
   ↓
Phase 8  installer + reliability + least privilege + offline update/catalog sync
   ↓
Phase 9  community compatibility/profile ecosystem
   ↓
Phase 10 release/legal/security/performance hardening
```

세부 packet 로드맵은 [`docs/implementation/`](docs/implementation/README.md)을 참고하세요.

---

## v1 출시 성공 기준

실제 v1은 다음 전체 사용자 여정이 통과해야 합니다.

- clean install/uninstall
- 정확히 두 개의 v1 Seat
- 실제 두 모니터/두 입력장치 물리 검증
- 테스트 구성에서 objective input isolation evidence
- 서로 다른 실제 게임 두 개 동시 실행
- 게임/provider가 허용하는 실제 same-title/two-instance 사례 최소 1개
- Player profiles + game-first 선택
- 자동 게임 감지 + 수동 추가 fallback
- 자동/수동 2인용 게임 설정 생성
- 한 Seat 게임 종료/교체 중 다른 Seat 지속
- Idle Seat Launcher
- 두 Seat 종료 후 일반 Windows로 검증된 복귀
- watchdog/crash/emergency recovery
- local-first compatibility JSON + 선택적 community 공유
- 로컬 데이터 기반 offline 사용
- 사용자 승인 프로그램 업데이트
- 오픈소스라고 부르기 전에 프로젝트 license/contribution terms 해결

처음부터 수십~수백 개 게임의 공식 지원을 선언하는 것이 목표가 아닙니다. 실제 evidence와 재사용 가능한 커뮤니티 지식으로 호환성을 확장하는 것이 목표입니다.

---

## v1에서 하지 않는 것

HydraSeat v1은 다음을 약속하지 않습니다.

- 2개를 넘는 활성 Seat
- VM 또는 Seat별 독립 Windows 설치
- Seat별 별도 Windows 로그온 세션
- 범용 독립 Windows 데스크톱
- 모든 게임의 동일 게임 멀티인스턴스
- anti-cheat/DRM/account/launcher/single-instance 우회
- anti-cheat 안전 인증
- 필수 cloud account/telemetry
- 모든 게임 호환

---

## 관련 시스템과 클린룸 원칙

HydraSeat은 ASTER, ProtoInput, Nucleus Co-op, Universal Split Screen, HidHide, devreorder, Duo 및 Microsoft 공식 Windows API 문서의 공개 정보와 동작을 연구합니다.

연구 대상의 코드가 자동으로 HydraSeat 코드가 되는 것은 아닙니다. Proprietary 제품은 공개 문서와 일반적인 observable behavior만 참고하고, 외부 source 재사용은 라이선스와 [클린룸 정책](docs/CLEAN_ROOM_POLICY.md)을 따라야 합니다.

`C:\HydraSeat\references` 아래 reference checkout은 연구 입력일 뿐 build input이 아닙니다.

---

## 개발 및 검증

현재 개발 빌드는 C++20, CMake, Windows MSVC를 기본으로 하며 UI에는 Qt 6을 선택적으로 사용합니다.

저장소는 다음 evidence를 명확히 구분합니다.

- automated controlled evidence
- real-process evidence
- physical/manual evidence
- community compatibility evidence

합성 테스트가 성공했다고 실제 물리 환경 또는 게임 전체 지원으로 승격하지 않습니다.

개발 작업 전:

```text
python tools/show_implementation_packet.py --current
python tools/validate_implementation_roadmap.py
```

Repository agent는 구현 전에 [`.agents/AGENTS.md`](.agents/AGENTS.md)를 읽어야 합니다.

---

## 라이선스와 기여

장기 목표는 누구나 합법적으로 재사용하고 기여할 수 있는 오픈소스 프로젝트입니다. 하지만 현재 프로젝트 라이선스와 contribution terms는 아직 공식 선언되지 않았습니다.

그 전까지는:

- 현재 저장소를 법적으로 오픈소스라고 표현하지 않습니다.
- 외부 코드 재사용 권리를 임의로 가정하지 않습니다.
- [`docs/CLEAN_ROOM_POLICY.md`](docs/CLEAN_ROOM_POLICY.md)를 따릅니다.
- license/contribution 결정을 출시 전에 반드시 해결해야 하는 제품 게이트로 취급합니다.

이 법적 게이트가 해결된 이후에는 게임 설정, compatibility evidence, 문서와 기여 워크플로우를 커뮤니티가 쉽게 확장할 수 있도록 구성하는 것이 목표입니다.