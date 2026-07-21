# Gateway 리팩토링 설계 문서

- 작성일: 2026-07-22
- 대상: Highway Drowsiness Prevention 프로젝트 - Gateway ECU (`Firmware/Gateway/Gateway_FW`)
- 브랜치: `refactoring/gateway`

## 1. 배경 및 목적

이 프로젝트는 SeSAC 모빌리티 임베디드 엔지니어 양성과정에서 진행된 고속도로 졸음운전 방지 시스템으로, Automotive SPICE 4.0의 V-model 프로세스를 준수하려는 시도로 시작되었다. Gateway ECU는 Vision(RPi, UART)과 Chassis/Body(STM32, CAN) 센서 데이터를 취합해 퍼지 로직으로 위험도를 산출하고, Control ECU에 제어 신호를 전달하는 중심 노드다.

이번 리팩토링의 목적은 코드 수정에 그치지 않고 **요구사항 단계부터 재점검**하여, 다음 세 가지 관점을 프로젝트에 반영하는 것이다.

- **ASPICE 준수의 엄밀함**: 요구사항(SYS.1)부터 아키텍처(SWE.2), 상세설계(SWE.3), 단위검증(SWE.4)까지 추적 가능한 체인을 구성
- **BSW(Basic Software) 개발자 관점**: 현재 애플리케이션 로직과 뒤섞여 있는 통신/진단/감시 기능을 계층화된 BSW 모듈로 분리·확대
- **QA 엔지니어 관점**: 문제 발견부터 요구사항 변경, 반영, 게이트 통과까지의 흐름을 SUP 프로세스(SUP.1/8/9/10)로 형식화

## 2. 범위

**포함**
- Gateway ECU(`Firmware/Gateway/Gateway_FW`)의 요구사항, 아키텍처, 상세설계, 구현, 단위테스트
- Gateway가 다른 노드(Vision/Chassis/Body/Control)와 주고받는 인터페이스 요구사항 및 ICD (수신측 관점)
- 시스템 레벨 요구사항(SYS.1) 재정립 — 이미 시작된 `HDP_refactoring_SYS1REQ.xlsx` 작업을 이어받아 완성
- QA 프로세스 산출물(Problem Report / Change Request / Quality Gate / Config Mgmt Plan)

**제외**
- Vision(RPi/Python), Chassis, Body, Control ECU 자체의 코드 리팩토링
- Flash 기반 NVM(DTC 영속 저장) — 이번 범위에서는 RAM 저장만 다룸
- 통신 프레임 CRC/체크섬 추가는 이번 범위의 이슈 목록에는 기록하되, 실제 설계/구현은 후속 CR로 미룸 (범위 과다 방지)

## 3. 현황 분석 — 발견된 이슈

Gateway_FW 코드 리뷰를 통해 다음 6개 이슈를 식별했다. 이는 4장의 QA 프로세스에서 최초 Problem Report로 등록된다.

| # | 이슈 | 위치 | 영향 |
|---|------|------|------|
| 1 | `main.c`가 초기화/Mock데이터생성/상태머신/통신을 모두 담당 — 계층 분리 없음 | `main.c` | 유지보수성, 테스트 용이성 저하 |
| 2 | 신호 결손(staleness) 미탐지 — `alive_cnt`가 멈춰도 감지하는 타임아웃 로직이 없음 | `comm_manager.c`, `main.c` | **안전 결함**: 센서 노드가 죽어도 마지막 값으로 계속 판단 |
| 3 | `err_flag`가 순간 판단에만 쓰이고 고장 이력으로 기록되지 않음 | `main.c` (`Update_System_State`) | 고장 진단/이력 추적 불가 |
| 4 | `CPU_TEST_MODE`/`MOCK_CAN_TEST`가 `#ifdef`로 운영 코드와 혼재 | `main.c` | 빌드 변형 관리 미흡, 운영 바이너리에 테스트 코드 혼입 위험 |
| 5 | Watchdog(IWDG/WWDG) 미사용 | 전역 | 시스템 행(hang) 시 자동 복구 수단 없음 |
| 6 | UART/CAN 페이로드에 CRC/체크섬 없음 | `comm_manager.c` | 데이터 무결성 보장 미흡 (이번 범위에서는 기록만, 설계는 후속) |

## 4. QA 프로세스 적용 (SUP.9 / SUP.10 / SUP.1 / SUP.8)

문제 발견 → 기록 → 영향분석 → 변경 승인 → 반영이라는 흐름을 형식화하여, 3장의 이슈들을 실제 사례로 사용한다.

```
Docs/QA/
├── ProblemReportLog.xlsx      # SUP.9: PR-ID, 발견일, 발견단계, 설명, 심각도, 상태
├── ChangeRequestLog.xlsx      # SUP.10: CR-ID, 관련PR-ID, 변경대상, 영향분석, 승인상태
├── QualityGateChecklist.md    # SUP.1: 단계별(요구사항/아키텍처/설계/코드/테스트) 완료기준 체크리스트
└── ConfigMgmtPlan.md          # SUP.8: 브랜치 전략, 베이스라인 태깅 규칙 (git 기반)
```

**적용 흐름 (이슈 #2 예시)**
1. `PR-002`: "신호 staleness 미탐지" 등록 (SUP.9)
2. 영향분석: SYS-REQ 중 "시스템 신뢰성 및 고장 진단" 카테고리에 결손 감지 요구사항이 없음을 확인
3. `CR-002`: SYS1REQ에 신호 결손 감지 요구사항 추가, SWE1REQ에 Signal Monitor 요구사항 추가 (SUP.10, 영향범위: SYS.1/SWE.1/SWE.2/SWE.3/SWE.4)
4. 승인 → 문서/코드 반영 → `TraceabilityMatrix.xlsx`에 CR-002 ↔ SYS-REQ-xx ↔ SWE-REQ-xx ↔ 설계문서 ↔ 코드 ↔ 테스트케이스 연결
5. `QualityGateChecklist.md` 기준으로 각 단계 산출물이 게이트를 통과했는지 점검

나머지 5개 이슈(#1, #3~#6)도 동일한 PR→CR 흐름을 거쳐 반영한다. `ConfigMgmtPlan.md`에는 기존 git 브랜치 전략(`main`/`refactoring/gateway` 등)과 베이스라인 태깅 규칙을 간단히 정리한다.

## 5. 산출물 구조

```
HDP-refactoring/
├── Docs/
│   ├── Requirements/
│   │   ├── SYS1REQ.xlsx          # 시스템 요구사항 (기존 작업 이어받음)
│   │   ├── SWE1REQ_Gateway.xlsx  # Gateway SW 요구사항 (신규)
│   │   └── TraceabilityMatrix.xlsx
│   ├── Design/
│   │   ├── SW_ARCH_Gateway.md
│   │   └── SWE3_DetailedDesign/
│   │       ├── signal_monitor.md
│   │       ├── diag_manager.md
│   │       └── wdg_manager.md
│   └── QA/
│       ├── ProblemReportLog.xlsx
│       ├── ChangeRequestLog.xlsx
│       ├── QualityGateChecklist.md
│       └── ConfigMgmtPlan.md
```

- 표 형태가 자연스러운 요구사항/추적성/PR/CR 로그는 xlsx (기존 관례 유지)
- 서술+다이어그램이 많은 아키텍처/상세설계 문서는 md (git diff 추적 용이)

## 6. SW 아키텍처 예비 아이디어 (SWE.2 — 확정 아님)

> **주의**: 이 장은 요구사항(SYS.1/SWE.1)이 아직 확정되지 않은 시점에 나온 예비 아이디어다. SWE.2 아키텍처는 원칙적으로 베이스라인이 확정된 SWE.1 요구사항을 입력으로 삼아야 하므로, 이 계층 구조는 **요구사항 확정 후 SWE.1 요구사항 항목과 하나씩 대조하여 재검토·확정**해야 한다. 여기서는 "이런 방향의 계층 분리가 가능하다"는 아이디어 수준으로만 남겨둔다. 세부 모듈 인터페이스·상세설계(舊 SWE.3)는 이 시점에 논하기엔 이르다고 판단해 이번 문서에서 제외했다 — 요구사항 확정 후 별도 브레인스토밍으로 다시 다룬다.

```
Application Layer
  - State Machine (STATE_NORMAL/WARNING/DANGER/FAULT 전이)
  - Fuzzy Logic (Compute_Integrated_Risk)

Service Layer (아이디어)
  - Signal Monitor   : 채널별 alive_cnt 정지 감지 → 타임아웃 시 err_flag 강제 설정
  - Diag Manager     : err_flag/타임아웃 이벤트를 DTC로 기록(Active/History), RAM 저장

BSW Layer (아이디어)
  - Comm Wrapper     : 기존 comm_manager.c (CAN/UART 파싱 + HAL 호출 캡슐화)
  - Wdg Manager      : IWDG 초기화/refresh, 태스크 생존 신고 기반 감시

MCAL Layer
  - STM32 HAL (기존 그대로)
```

## 7. 빌드 관리 방침 (일반론)

- 운영 빌드와 테스트 빌드를 분리 관리한다 (예: CMake 타겟 분리). Mock 데이터 생성이나 부하테스트 같은 테스트 전용 코드가 운영 바이너리에 섞여 들어가지 않도록 한다.
- 이슈 #4(`CPU_TEST_MODE`/`MOCK_CAN_TEST` 혼재)에 대한 구체적인 해결 방식(타겟 이름, 파일 분리 범위 등)은 요구사항·아키텍처가 확정된 뒤 설계 단계에서 정한다.

## 8. 테스트 방침 (일반론)

- 기존 관례(온타겟 Unity, `Run_ASPICE_Unit_Tests()` 러너)를 유지한다.
- 신규로 추가되는 기능은 모듈 단위로 테스트 파일을 분리하는 것을 원칙으로 한다.
- 테스트케이스는 `TraceabilityMatrix.xlsx`에서 대응하는 SWE.1/SWE.3 항목과 연결한다.
- 구체적인 테스트 파일 구성은 상세설계가 나온 뒤 정한다.

## 9. 마이그레이션 순서 (개요)

세부 단계는 다음 writing-plans 단계에서 구체화하되, 대략적인 순서는 다음과 같다. **요구사항 확정과 검토(게이트 통과)가 아키텍처·설계 작업보다 먼저 와야 한다는 원칙**을 반영했다.

1. QA 산출물 골격 작성 (`Docs/QA/*`) — 3장의 6개 이슈를 PR로 등록
2. SYS1REQ 완성 + SWE1REQ_Gateway 작성 (CR을 통해 반영)
3. **요구사항 검토 및 베이스라인 확정** (`QualityGateChecklist.md`의 요구사항 게이트 통과) — 이 게이트를 통과하기 전에는 아키텍처/상세설계를 확정하지 않는다
4. SW_ARCH_Gateway.md 확정 (6장의 예비 아이디어를 확정된 SWE.1 요구사항과 대조하며 재작성)
5. BSW 모듈별 상세설계(SWE.3) 및 구현 + 단위테스트
6. 빌드 변형 분리, 테스트 전용 코드 이관
7. `Update_System_State()`를 신규 모듈과 연동하도록 리팩토링
8. TraceabilityMatrix.xlsx 완성 (요구사항↔설계↔코드↔테스트 전체 연결)
9. QualityGateChecklist 기준 최종 점검