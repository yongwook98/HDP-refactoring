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

## 6. SW 아키텍처 (SWE.2)

```
Application Layer
  - State Machine (STATE_NORMAL/WARNING/DANGER/FAULT 전이)
  - Fuzzy Logic (Compute_Integrated_Risk)

Service Layer (신규)
  - Signal Monitor   : 채널별 alive_cnt 정지 감지 → 타임아웃 시 err_flag 강제 설정
  - Diag Manager     : err_flag/타임아웃 이벤트를 DTC로 기록(Active/History), RAM 저장

BSW Layer (재구성)
  - Comm Wrapper     : 기존 comm_manager.c (CAN/UART 파싱 + HAL 호출 캡슐화, 대규모 변경 없음)
  - Wdg Manager (신규): IWDG 초기화/refresh, 태스크 생존 신고 기반 감시

MCAL Layer
  - STM32 HAL (기존 그대로)
```

**데이터 흐름 변화**: `Update_System_State()`(100ms 주기)에서 로컬 스냅샷을 뜬 직후, 각 채널의 alive_cnt를 Signal Monitor에 전달 → stale 판정 시 Diag Manager에 통보 → DTC 기록. `current_state == STATE_FAULT` 판단 기준이 "Diag Manager에 Active DTC 존재 여부"로 대체된다.

## 7. 신규 BSW 모듈 상세설계 (SWE.3)

### 7.1 Signal Monitor (`signal_monitor.h/.c`)

```c
typedef enum { SIG_CH_VISION, SIG_CH_CHASSIS, SIG_CH_BODY, SIG_CH_COUNT } SignalChannel_t;

void    SigMon_Init(void);
void    SigMon_Update(SignalChannel_t ch, uint8_t current_alive_cnt); // 100ms tick마다 호출
uint8_t SigMon_IsStale(SignalChannel_t ch);
```

- 채널별로 `alive_cnt`가 바뀌지 않은 연속 tick 수를 카운트, 임계치 초과 시 stale
- 임계치 기본값: Chassis/Body 300ms(3틱), Vision 500ms(5틱, 카메라 파이프라인 지연 고려)

### 7.2 Diag Manager (`diag_manager.h/.c`)

```c
typedef enum { DTC_VISION_TIMEOUT, DTC_VISION_FAULT, DTC_CHASSIS_TIMEOUT,
               DTC_CHASSIS_FAULT, DTC_BODY_TIMEOUT, DTC_BODY_FAULT, DTC_COUNT } DtcId_t;
typedef enum { DTC_INACTIVE, DTC_ACTIVE, DTC_HISTORY } DtcStatus_t;

void        Diag_Init(void);
void        Diag_SetActive(DtcId_t id);
void        Diag_SetHealed(DtcId_t id);
DtcStatus_t Diag_GetStatus(DtcId_t id);
uint8_t     Diag_GetActiveCount(void);
```

- 고정 크기 RAM 테이블(`DTC_COUNT`개), 발생 횟수 카운트 포함
- 리셋 시 초기화 (RAM 전용 — Flash 영속화는 이번 범위 제외, 결정 근거: 작업량 대비 포트폴리오 목적에는 개념 증명으로 충분)

### 7.3 Wdg Manager (`wdg_manager.h/.c`)

```c
void WdgM_Init(void);       // IWDG 초기화 (타임아웃 예: 800ms)
void WdgM_TaskAlive(void);  // Update_System_State 정상 완료 시 호출 (생존 신고)
void WdgM_Refresh(void);    // 메인 루프에서 매 iteration 호출
```

- `WdgM_Refresh()`는 마지막 refresh 이후 `WdgM_TaskAlive()` 신고가 있었을 때만 `HAL_IWDG_Refresh()` 호출 — 메인 태스크가 멈추면 watchdog가 갱신되지 않아 MCU가 리셋됨

### 7.4 Comm Wrapper

기존 `comm_manager.c`가 이미 HAL 호출을 캡슐화하고 있어 대규모 리팩토링은 불필요. `Update_System_State()`에서 alive_cnt를 읽어 Signal Monitor로 넘기는 연결만 추가한다.

## 8. 빌드 변형 (CMake)

```
CMakeLists.txt
  gateway_release   # 운영 빌드: Mock/Unity 미포함, BSW 3종 전부 활성
  gateway_test      # 테스트 빌드: Unity + mock_can_data.c + 전체 테스트 스위트 포함
```

- `main.c`에서 `#ifdef CPU_TEST_MODE` / `#ifdef MOCK_CAN_TEST` 블록 제거
- `Send_Mock_CAN_Data()`와 CPU 부하테스트 코드는 `Core/Src/mock_can_data.c`로 분리, `gateway_test` 타겟에만 컴파일
- `gateway_release`는 순수 운영 로직만 포함

## 9. 테스트 전략 (SWE.4)

기존 관례(온타겟 Unity, `Run_ASPICE_Unit_Tests()` 러너)를 유지하며 모듈 분리에 맞춰 테스트 파일을 분리한다.

```
Core/Src/
  test_suite.c           # 기존 (Fuzzy Logic, Comm, Integration)
  test_signal_monitor.c  # 신규: 임계치 경계값(임계치-1, 임계치, 임계치+1) 검증
  test_diag_manager.c    # 신규: Active→History 전이, 발생횟수 카운팅 검증
  test_wdg_manager.c     # 신규: TaskAlive 신고 유무에 따른 Refresh 여부 검증
```

- `Run_ASPICE_Unit_Tests()`가 신규 테스트 3종도 등록하도록 확장, `gateway_test` 빌드에서만 실행
- 각 테스트케이스 ID는 `TraceabilityMatrix.xlsx`에서 해당 SWE.3 설계 항목과 연결

**디렉토리**: 기존 `Core/Inc`, `Core/Src` 평면 구조를 유지 (새 하위폴더 없음) — 신규 모듈들이 기존 `comm_manager.c`, `fuzzy_logic.c` 옆에 나란히 추가된다.

## 10. 마이그레이션 순서 (개요)

세부 단계는 다음 writing-plans 단계에서 구체화하되, 대략적인 순서는 다음과 같다.

1. QA 산출물 골격 작성 (`Docs/QA/*`) — 3장의 6개 이슈를 PR로 등록
2. SYS1REQ 완성 + SWE1REQ_Gateway 작성 (CR을 통해 반영)
3. SW_ARCH_Gateway.md 작성 (6장 내용 문서화)
4. Signal Monitor 구현 + 단위테스트
5. Diag Manager 구현 + 단위테스트 (Signal Monitor와 연동)
6. Wdg Manager 구현 + 단위테스트
7. CMake 빌드 변형 분리, Mock 코드 이동
8. `Update_System_State()`를 신규 모듈과 연동하도록 리팩토링
9. TraceabilityMatrix.xlsx 완성 (요구사항↔설계↔코드↔테스트 전체 연결)
10. QualityGateChecklist 기준 최종 점검