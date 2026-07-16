#include "comm_manager.h"
#include <string.h>

// 실제 변수 메모리 할당
VisionData_t  vision_data  	= {0};
ChassisData_t chassis_data 	= {0};
BodyData_t    body_data    	= {0};
uint8_t uart_rx_buffer[8];
// [TEST SUPPORT]
uint8_t g_last_tx_packet[10] = {0};

// === 1. UART 수신 처리 (Vision) ===
void DMS_Process_UART_Data(UART_HandleTypeDef *huart, uint8_t *buffer)
{
    // Vision 센서가 연결된 UART 채널인지 확인 (예: USART1)
    if (huart->Instance == USART1)
    {
        // ICD V0.1.1 파싱
        vision_data.perclos = buffer[0];

        uint8_t flags = buffer[1];
        vision_data.is_eye_closed    = (flags & 0x01);
        vision_data.is_face_detected = (flags >> 1) & 0x01;

        uint8_t status = buffer[7];
        vision_data.alive_cnt = status & 0x0F;
        vision_data.err_flag  = (status >> 4) & 0x0F;

        HAL_UART_Receive_IT(huart, buffer, 8);
    }
}

// === 2. CAN 수신 처리 (Chassis / Body) ===
void DMS_Process_CAN_Data(CAN_RxHeaderTypeDef *header, uint8_t *data)
{
    // (1) Chassis Node (0x201)
    if (header->StdId == 0x201)
    {
        // Byte 0-1: Steering Std (Factor 0.01)
        uint16_t raw_std = (data[1] << 8) | data[0];
        chassis_data.steering_std_dev = raw_std * 0.01f;

        // Byte 2-3: Steering Angle (Factor 0.1, int16)
        int16_t raw_angle = (data[3] << 8) | data[2];
        chassis_data.steering_angle = raw_angle * 0.1f; // (주의: Factor 적용 전 Raw 값 저장 or float 변환 선택)

        // Alive & Err
        chassis_data.alive_cnt = data[7] & 0x0F;
        chassis_data.err_flag  = (data[7] >> 4) & 0x0F;
    }

    // (2) Body Node (0x301) - 전처리된 센서 값
    else if (header->StdId == 0x301)
    {
        // Byte 0: Head Delta (int8)
        int8_t raw_delta = (int8_t)data[0];
        body_data.head_delta_cm = (float)raw_delta;

        // Byte 1: Hands Off Time (uint8, Factor 0.1)
        uint8_t raw_time = data[1];
        body_data.hands_off_sec = raw_time * 0.1f;

        // Byte 7 : alive counter and error flag (uint8)
        body_data.alive_cnt = data[7] & 0x0F;
        body_data.err_flag  = (data[7] >> 4) & 0x0F;

    }
}

static uint8_t gateway_alive_cnt = 0;

void DMS_Send_Control_Signal(UART_HandleTypeDef *huart, SystemState_t state, uint8_t mrm_active, uint8_t err_flag)
{
    // [구조 변경] Header(2) + Payload(8) = 총 10바이트
    uint8_t tx_packet[10] = {0,};

    // --------------------------------------------------------
    // [Header] ID: 0x401 (16bit) -> Split into 2 Bytes
    // --------------------------------------------------------
    tx_packet[0] = 0x04; // High Byte
    tx_packet[1] = 0x01; // Low Byte (0x0401)

    // --------------------------------------------------------
    // [Payload Byte 0] Alert_Level (Bit 0~7) -> tx_packet[2]
    // --------------------------------------------------------
    // 0:Normal, 1:Warning, 2:Danger, 3:Fault
    tx_packet[2] = (uint8_t)state;

    // --------------------------------------------------------
    // [Payload Byte 1] MRM_Trigger (Bit 0) -> tx_packet[3]
    // --------------------------------------------------------
    if (mrm_active)
    {
        tx_packet[3] |= 0x01;
    }

    // [Payload Byte 2~6] Reserved (tx_packet[4] ~ tx_packet[8])
    // 0으로 초기화됨

    // --------------------------------------------------------
    // [Payload Byte 7] Alive & Err -> tx_packet[9]
    // --------------------------------------------------------
    tx_packet[9] |= (gateway_alive_cnt & 0x0F);       // Alive Count
    tx_packet[9] |= ((err_flag & 0x0F) << 4);         // Error Flag

    // Alive Count 증가
    gateway_alive_cnt = (gateway_alive_cnt + 1) % 16;

    memcpy(g_last_tx_packet, tx_packet, 10);

    // --------------------------------------------------------
    // [전송] UART Transmit (총 10 Bytes)
    // --------------------------------------------------------
    // main.c에서 넘겨준 huart 핸들러(UART3)로 전송
    HAL_UART_Transmit(huart, tx_packet, 10, 10);
}

void DMS_Send_Dashboard_Data(UART_HandleTypeDef *huart, uint8_t risk_score)
{
    DashboardPacket_t tx_packet;

    // 1. 헤더 (Sync)
    tx_packet.header = 0xFCFD; // 리틀 엔디안: FD FC 순으로 전송됨

    // 2. 데이터 채우기 (전역 변수 활용)
    // [Vision]
    tx_packet.perclos          = vision_data.perclos;
    tx_packet.is_eye_closed    = vision_data.is_eye_closed;
    tx_packet.is_face_detected = vision_data.is_face_detected;
    tx_packet.v_alive_cnt      = vision_data.alive_cnt;
    tx_packet.v_err_flag       = vision_data.err_flag;

    // [Chassis]
    tx_packet.steering_std_dev = chassis_data.steering_std_dev;
    tx_packet.steering_angle   = chassis_data.steering_angle;
    tx_packet.c_alive_cnt      = chassis_data.alive_cnt;
    tx_packet.c_err_flag       = chassis_data.err_flag;

    // [Body]
    tx_packet.head_delta_cm    = body_data.head_delta_cm;
    tx_packet.hands_off_sec    = body_data.hands_off_sec;
    tx_packet.no_op_sec        = body_data.no_op_sec;
    tx_packet.b_alive_cnt      = body_data.alive_cnt;
    tx_packet.b_err_flag       = body_data.err_flag;

    // [Result]
    tx_packet.risk_level       = risk_score;

    // 3. UART 전송 (Blocking 방식, 10ms 타임아웃)
    HAL_UART_Transmit(huart, (uint8_t*)&tx_packet, sizeof(DashboardPacket_t), 10);
}
