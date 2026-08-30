# HydraSeat

[English](README.md) · [简体中文](README.zh-CN.md)

HydraSeat는 **한 대의 Windows 10/11 x64 PC에서 두 개의 로컬 게임 Seat를 실행**하기 위한 프로젝트다. Seat는 디스플레이·입력·컨트롤러·오디오로 이루어진 물리적 좌석이며 별도 Windows 데스크톱이나 사용자 세션이 아니다. 기본 흐름은 게임 → Seat 1/Seat 2/Both → Player → 실행이다.

현재 저장소는 구현과 테스트가 상당히 진행됐지만 **완성된 공개 제품은 아니다**. 런타임, IPC, 실행, 롤백, 복구, provider, 호환성, 배포 기반은 구현되어 있고 controlled test가 폭넓게 존재한다. 두 키보드·두 마우스 물리 증거, 실제 게임 campaign, clean-machine installer/UAC/reboot 증거, 보호된 production signing은 아직 release gate다.

## 현재 실제 구성

| 구성 요소 | 빌드 target | 현재 책임 |
| --- | --- | --- |
| Management UI | `HydraSeat.exe` | Native Win32 game-first UI, Seat/profile/setup 선택, host 제어와 진단 |
| Runtime authority | `hydra_host.exe` | 세션 권위, bounded/versioned IPC, immutable launch plan, process tree와 Seat lifecycle |
| Seat UI | `hydra_seat_ui.exe` | Seat별 최소 launcher/status UI. 두 번째 desktop shell이 아님 |
| Recovery | `hydra_watchdog.exe`, `hydra_reset.exe` | crash journal 복구, 소유 상태 rollback, emergency reset |
| 운영/검증 도구 | `hydra_hostctl.exe`, `hydraseat_profilectl.exe`, 진단·acceptance 도구 | protocol 검사, profile/provider 작업, controlled probe와 evidence 수집 |
| 선택적 adapter | Gate C adapter/shim targets | 명시적 gate 아래 Raw Input과 Win32 polling/focus/cursor 호환성 작업 |

구형 Qt prototype UI는 빌드에 포함되지 않는다. 현재 UI는 native Win32이며 CLI/Seat UI와 동일한 host protocol을 사용한다.

## 런타임 구조

```text
HydraSeat.exe (normal-user management UI)
        | bounded host protocol v4
        v
hydra_host.exe (authoritative background runtime)
        +-- profile/session/Seat generation 검증
        +-- trusted requirement와 immutable launch plan 해석
        +-- Seat별 process tree 독립 실행·소유
        +-- window/display/controller/audio/input policy 조정
        +-- mutation journal과 rollback 검증
        +-- hydra_seat_ui.exe (Seat별 최소 UI)
        +-- hydra_watchdog.exe / hydra_reset.exe (복구 경계)
```

권위자는 UI가 아니라 host다. Production path가 정확한 plan을 설치하고 host가 승인·실행하기 전에는 UI 요청을 게임 실행 성공으로 표시하지 않는다. Seat 1의 중지/재시작이 불필요하게 Seat 2를 종료하지 않아야 한다. Protocol은 bounded/versioned/fixed-width라서 x86/x64 adapter가 handle이나 payload를 암묵적으로 다르게 해석할 수 없다.

## 제품 경계

v1은 최대 두 active Seat만 지원하며 **Seat**, **Player**, **Game**, **TwoPlayerSetup**, 현재 immutable **RuntimeSession**을 분리한다. VM, 독립 Windows logon, 일반 multiseat desktop, 모든 게임의 same-title 지원, DRM·anti-cheat·account·launcher·single-instance·보호 경계 우회는 약속하지 않는다. 지원되지 않는 요구는 fail closed로 처리하고 사용자에게 드러낸다.

현재 구현에는 안정적 hardware identity, controlled input-isolation adapter/evidence tooling, authoritative host IPC와 process-tree ownership, Seat 독립 stop/restart, window/display/controller/audio policy, Steam/custom provider와 profile/trusted requirement/typed setup package, local compatibility evidence, 선택적 community contract, crash/reset, least-privilege, privacy, transactional installer/update, signing/provenance validation이 포함된다.

정확한 packet 상태와 남은 증거는 [Implementation Status](docs/implementation/STATUS.md)가 관리한다. `CODE_COMPLETE`도 물리·게임·installer·reboot·signing·public-service 증거가 남아 있으면 `VALIDATED`가 아니다.

## 빌드와 테스트

필요 환경: Windows, Visual Studio 2022 MSVC C++ workload, CMake, Python 3, PowerShell.

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

2026-08-30 기준 새 x64와 Win32 tree는 warning 없이 빌드되었고 각각 **133/133** CTest를 통과했다. Production-launch/IPC focused regression도 통과했다. 이 자동 결과는 남은 물리·실게임 gate를 대신하지 않는다.

## 설치와 release 상태

`tools/install_hydraseat.ps1`에는 x64 package 검증, install/repair/uninstall transaction, owned-path 검사와 rollback이 구현되어 있다. Release binary는 `HydraSeat.exe`, `hydra_host.exe`, `hydra_seat_ui.exe`, `hydra_watchdog.exe`, `hydra_reset.exe`다.

아직 production installer 검증 완료로 간주하면 안 된다. Clean Windows machine, UAC, reboot/interruption recovery, uninstall postcondition, 보호된 signer, production certificate/timestamp provider, signed release-candidate 실행이 필요하다.

## 문서 지도

- [v1 제품 계약](docs/PRODUCT_V1.md) · [Architecture](docs/ARCHITECTURE.md) · [Implementation Status](docs/implementation/STATUS.md)
- [Packet roadmap](docs/implementation/README.md) · [Reference 연구 인덱스](docs/REFERENCE_RESEARCH_INDEX.md) · [관련 시스템 설계 메모](docs/RELATED_SYSTEMS_RESEARCH.md)
- [Clean-room policy](docs/CLEAN_ROOM_POLICY.md) · [Compatibility matrix](docs/COMPATIBILITY_MATRIX.md) · [병렬 chunk board](.agents/CHUNKS.md)

`C:\HydraSeat\references` 아래 reference tree는 읽기 전용 연구 입력이며 build input이 아니다. 연구 결과는 중립 요구사항과 독립 구현 test로 옮기고 호환되지 않는 외부 구현을 core에 복사하지 않는다.

## License

프로젝트 license와 contribution 조건은 아직 최종 선언되지 않았다. 이 gate가 해결되기 전에는 법적으로 open source라고 표현하거나 source reuse 권리가 있다고 가정하지 않는다.
