/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "can_message.h" // 공통 헤더
#include <gateway_defines.h>
#include <fuzzy_logic.h>
#include <comm_manager.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "unity.h"

// [테스트 스위치] 이 줄이 있으면 테스트 모드, 주석(//) 처리하면 정상 모드
//#define CPU_TEST_MODE
#define MOCK_CAN_TEST
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan;

TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

#ifdef CPU_TEST_MODE
  volatile uint32_t idle_counter = 0;
  uint32_t max_idle_count = 0;
  uint8_t  benchmark_done = 0; // 기준점 측정 완료 플래그
  volatile uint32_t rx_debug_cnt = 0; // 수신 인터럽트 횟수 카운터
#endif

volatile uint8_t timer_100ms_flag = 0;

// 전역 변수로 선언
DashboardPacket_t tx_packet;

SystemState_t current_state = STATE_NORMAL;

float prev_steering_angle = 0;

uint32_t no_op_timer = 0;

uint8_t risk_score = 0;

CAN_RxHeaderTypeDef RxHeader;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */
void Update_System_State();
extern void Run_ASPICE_Unit_Tests(void);
void Send_Mock_CAN_Data(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
	printf("system start...\r\n");
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
    printf("\r\n=======================================================\r\n");
    printf("   ASPICE SWE.4 Unit Verification Report               \r\n");
    printf("-------------------------------------------------------\r\n");
    printf("   Target Project : Drowsiness Prevention System       \r\n");
    printf("   SW Version     : V0.8                               \r\n");
    printf("   Test Date      : %s             \r\n",__DATE__);
    printf("=======================================================\r\n\r\n");

//    Run_ASPICE_Unit_Tests();
    printf("============================================\r\n");

  // === 1. CAN 필터 및 시작 설정  ===
    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;

    // 1. ID: 0x201 (Chassis) 기준
    sFilterConfig.FilterIdHigh = (0x201 << 5);
    sFilterConfig.FilterIdLow = 0x0000;

    // 2. Mask: 0x6FF (0x201과 0x301만 통과시키도록 설정)
    sFilterConfig.FilterMaskIdHigh = (0x6FF << 5);
    sFilterConfig.FilterMaskIdLow = 0x0000;

    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;

    HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
    HAL_CAN_Start(&hcan);
    HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);

    // === 2. UART(Vision) 수신 인터럽트 시작 ===
    HAL_UART_Receive_IT(&huart1, uart_rx_buffer, 8);

    // === 3. Timer3 인터럽트 시작 ===
    HAL_TIM_Base_Start_IT(&htim3);

	#ifdef CPU_TEST_MODE
	  printf("⚠️ TEST MODE: CPU Load Test Started (Loopback)\r\n");
	#else
	  printf("system start...\r\n");
	#endif


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
#ifdef CPU_TEST_MODE
		  idle_counter++; // 테스트 모드일 때만 카운팅
#endif
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  if (timer_100ms_flag == 1)
	  {
		  timer_100ms_flag = 0;

#ifdef MOCK_CAN_TEST
		  Send_Mock_CAN_Data();
#endif

		  Update_System_State();

		  DMS_Send_Dashboard_Data(&huart1, risk_score);

#ifdef CPU_TEST_MODE
        static int time_100ms = 0;
        time_100ms++;

        // 1. [초반 3초] 아무것도 안 하고 기준점(Max Idle) 잡기
        if (time_100ms <= 30)
        {
            if (time_100ms == 30) // 3초 되는 순간 기준점 확정
            {
                 // 지금까지 3초간 센 것을 100ms 단위 평균으로 환산하거나,
                 // 간단히 지금 순간의 카운터 속도를 기준으로 잡음 (여기선 간단화된 로직 사용)
                 // *정확한 방법*: 1초 단위로 끊어서 측정. 아래 로직으로 변경.
            	benchmark_done = 1; // 기준점 확정 플래그
            	printf("\r\n>>> Calibration Done! Starting Stress Test (500 msg/100ms) <<<\r\n");
            }
        }
        // 2. [3초 이후] CAN 폭격 시작 (부하 유발)
        else
        {
            CAN_TxHeaderTypeDef TxHeader;
            uint8_t TxData[8] = {0,};
            uint32_t TxMailbox;

            TxHeader.StdId = 0x123; // 쓰레기 ID (필터에 걸려야 함)
            TxHeader.RTR = CAN_RTR_DATA;
            TxHeader.IDE = CAN_ID_STD;
            TxHeader.DLC = 8;

            // [수정] 부하를 10배로 늘림 (50 -> 500회)
            // 0.1초 안에 500번 인터럽트면 CPU가 꽤 힘들어할 겁니다.
            for(int i=0; i<5000; i++) {
                HAL_CAN_AddTxMessage(&hcan, &TxHeader, TxData, &TxMailbox);
            }
        }

        // 3. 결과 출력 (1초 마다)
        if (time_100ms % 10 == 0)
        {
            // 아직 기준점 못 잡았으면 현재 값을 최대로 가정
            if (benchmark_done == 0) {
                max_idle_count = idle_counter;
                // 3초 지났으면 이제부터 이 값은 고정 (기준점 확정)
                if (time_100ms >= 30) benchmark_done = 1;
                printf("[CALIB] Measuring Baseline... Cnt: %lu\r\n", idle_counter);
            }
            else {
                // 기준점(max_idle_count) 대비 현재 카운트가 얼마나 줄었는지 계산
                float load = (1.0f - (float)idle_counter / max_idle_count) * 100.0f;
                if(load < 0) load = 0;
                printf("[TEST] Load: %.1f%% | Cnt: %lu | Rx Msg: %lu\r\n", load, idle_counter, rx_debug_cnt);
            }

            idle_counter = 0; // 리셋
        }
#endif
	  }

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL8;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CAN Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN_Init(void)
{

  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 4;

#if defined(CPU_TEST_MODE) || defined(MOCK_CAN_TEST)
  hcan.Init.Mode = CAN_MODE_LOOPBACK;
#else
  hcan.Init.Mode = CAN_MODE_NORMAL;
#endif


  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = DISABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = DISABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN_Init 2 */

  /* USER CODE END CAN_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 6400-1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1000-1;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// printf 출력을 위한 리타겟팅 함수
#ifdef __GNUC__
int _write(int file, char *ptr, int len)
{
    // 디버깅용 UART 채널.
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, 30);
    return len;
}
#endif

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
        timer_100ms_flag = 1;
    }
}

// 1. UART 수신 콜백 (Vision)
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    // 매니저에게 버퍼 넘기기
    DMS_Process_UART_Data(huart, uart_rx_buffer);
}

// 2. CAN 수신 콜백 (Chassis, Body)
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef RxHeader;
    uint8_t RxData[8];

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK)
    {
#ifdef CPU_TEST_MODE
    	rx_debug_cnt++;
#endif
        // 매니저에게 패킷 넘기기
        DMS_Process_CAN_Data(&RxHeader, RxData);
    }
}

void Update_System_State()
{
	VisionData_t  vision_data_local;
	ChassisData_t chassis_data_local;
	BodyData_t    body_data_local;

	__disable_irq();

	vision_data_local  = vision_data;
	chassis_data_local = chassis_data;
	body_data_local = body_data;

    __enable_irq();

    // 비전, 섀시, 바디에서 에러 플래그가 하나라도 0이 아니면 고장 처리
    if (vision_data_local.is_face_detected != 1 || chassis_data_local.err_flag != 0 || body_data_local.err_flag != 0)
    {
        current_state = STATE_FAULT;

        DMS_Send_Control_Signal(&huart3, STATE_FAULT, 1, 1);

        return;
    }

    float current_angle = chassis_data_local.steering_angle;
    float angle_diff = fabsf(current_angle - prev_steering_angle);

    if (angle_diff < 2.0f)
    {
        no_op_timer += 100; // 100ms 증가 (루프 주기)
    }
    else
    {
        no_op_timer = 0; // 조작 감지 시 리셋
        prev_steering_angle = current_angle; // 기준점 갱신
    }

    // 얼굴 인식 여부에 따른 데이터 필터링
    uint8_t safe_perclos = 0;

    if (vision_data_local.is_face_detected == 1)
    {
        safe_perclos = vision_data_local.perclos; // 얼굴 있으면 측정값 사용
    }
    else
    {
        safe_perclos = 0;
    }

    // ms -> sec 변환
    float no_op_sec = no_op_timer / 1000.0f;

    risk_score = Compute_Integrated_Risk(
                            safe_perclos,
                            chassis_data_local.steering_std_dev,
                            body_data_local.hands_off_sec,
                            body_data_local.head_delta_cm,
							no_op_sec
                         );

    if (current_state == STATE_NORMAL)
    {
    	if (risk_score >= 75)
    	{
    		current_state = STATE_WARNING;
    	}
    }
    else if (current_state == STATE_WARNING)
    {
    	if (risk_score >= 95)
    	{
    		current_state = STATE_DANGER;  // 95점 이상 위험
    	}
    	else if (risk_score < 60)
    	{
    		current_state = STATE_NORMAL; // 복귀
    	}
    }
    else if (current_state == STATE_DANGER)
    {
    	if (risk_score < 85)
    	{
    		current_state = STATE_WARNING;
    	}
    }

    // MRM 트리거 조건 판단 (예: Danger 상태이거나, 센서가 다 죽었거나)
    uint8_t mrm_cmd = 0;
    if (current_state == STATE_DANGER || current_state == STATE_FAULT)
    {
        mrm_cmd = 1; // 멈춰!
    }


    // 제어 신호 전송 (ICD V0.1.2 규격)
    DMS_Send_Control_Signal(&huart3, current_state, mrm_cmd, 0);


    printf("%3d Risk: %3d | Eye_safe : %3d%% | detected : %3d | Hands: %3.1fs | Head: %3.1f | Steer: %3.1f | NoOp: %3.1fs\r\n",
                vision_data_local.alive_cnt,
        		risk_score,
				safe_perclos,
				vision_data_local.is_face_detected,
                body_data_local.hands_off_sec,
                body_data_local.head_delta_cm,
                chassis_data_local.steering_std_dev,
                no_op_sec
                );
}

void Send_Mock_CAN_Data(){
	CAN_TxHeaderTypeDef TxHeader;
	uint8_t TxData[8] = {0};
	uint32_t TxMailBox;
	static uint8_t mock_alive = 0;

	// 가상 chassis 노드 데이터 생성．
	TxHeader.StdId = 0x201;
	TxHeader.RTR = CAN_RTR_DATA;
	TxHeader.IDE = CAN_ID_STD;
	TxHeader.DLC = 8;

	// 조향 표준 편차
	uint16_t raw_std = 150;
	TxData[0] = raw_std & 0xFF;
	TxData[1] = (raw_std >> 8) & 0xFF;

	// 조향각
	int16_t raw_angle = 105;
	TxData[2] = raw_angle & 0xFF;
	TxData[3] = (raw_angle >> 8) & 0xFF;

	// alive counter 설정
	TxData[7] = mock_alive & 0x0F;

	// 섀시 데이터 송신
	HAL_CAN_AddTxMessage(&hcan, &TxHeader, TxData, &TxMailBox);

	memset(TxData, 0, sizeof(TxData));

	// 가상 body 노드 데이터 생성
	TxHeader.StdId = 0x301;
	TxHeader.DLC = 8;

	// 머리 변위량
	TxData[0] = 3;

	// 손 뗀 시간
	TxData[1] = 5;

	// alive counter 설정
	TxData[7] = mock_alive & 0x0F;

	// 바디 데이터 송신
	HAL_CAN_AddTxMessage(&hcan, &TxHeader, TxData, &TxMailBox);

	mock_alive = (mock_alive + 1) % 16;

}

// UART 에러 발생 시(노이즈 등) 호출됨 -> 에러 풀고 수신 다시 켜기
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // 에러 플래그 클리어 및 재수신 시작
        HAL_UART_Receive_IT(huart, uart_rx_buffer, 8);
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
