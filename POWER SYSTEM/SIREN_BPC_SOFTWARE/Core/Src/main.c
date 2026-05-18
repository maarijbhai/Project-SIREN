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
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    STATE_IDLE    = 0,
    STATE_HEATING = 1,
    STATE_FAULT   = 2
} SystemState_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TEMP_START_HEATING_C    (-10.0f)
#define TEMP_STOP_HEATING_C     (0.0f)
#define TEMP_FAULT_C            (45.0f)
#define CURRENT_FAULT_MA        (2000.0f)
#define VOLTAGE_CUTOFF_MV       (3000.0f)
#define NTC_R_FIXED             (10000.0f)
#define NTC_R25                 (10000.0f)
#define NTC_B                   (3900.0f)
#define NTC_T25_K               (298.15f)
#define ADC_VREF                (3.3f)
#define ADC_RESOLUTION          (4095.0f)
#define INA219_ADDR             (0x40 << 1)
#define INA219_REG_CONFIG       (0x00)
#define INA219_REG_SHUNT_V      (0x01)
#define INA219_REG_BUS_V        (0x02)
#define INA219_REG_CURRENT      (0x04)
#define INA219_REG_CALIBRATION  (0x05)
#define INA219_CONFIG_VALUE     (0x399F)
#define INA219_CAL_VALUE        (4096)
#define INA219_CURRENT_LSB_MA   (0.1f)
#define MONITOR_INTERVAL_MS     (1000)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
SystemState_t g_state         = STATE_IDLE;
float         g_temperature_c = 0.0f;
float         g_current_ma    = 0.0f;
float         g_voltage_mv    = 0.0f;
uint32_t      g_last_monitor  = 0;
char          g_tx_buf[128];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void BPC_Start(void);
void BPC_Stop(void);
HAL_StatusTypeDef INA219_Init(void);
HAL_StatusTypeDef INA219_WriteReg(uint8_t reg, uint16_t value);
HAL_StatusTypeDef INA219_ReadReg(uint8_t reg, uint16_t *value);
float INA219_ReadCurrent_mA(void);
float INA219_ReadBusVoltage_mV(void);
float ADC_ReadTemperature_C(void);
void StateMachine_Update(void);
void Data_Transmit(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void BPC_Start(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
}

void BPC_Stop(void)
{
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
}

HAL_StatusTypeDef INA219_WriteReg(uint8_t reg, uint16_t value)
{
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] =  value       & 0xFF;
    return HAL_I2C_Master_Transmit(&hi2c1, INA219_ADDR, buf, 3, 100);
}

HAL_StatusTypeDef INA219_ReadReg(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    HAL_StatusTypeDef ret;
    ret = HAL_I2C_Master_Transmit(&hi2c1, INA219_ADDR, &reg, 1, 100);
    if (ret != HAL_OK) return ret;
    ret = HAL_I2C_Master_Receive(&hi2c1, INA219_ADDR, buf, 2, 100);
    if (ret != HAL_OK) return ret;
    *value = ((uint16_t)buf[0] << 8) | buf[1];
    return HAL_OK;
}

HAL_StatusTypeDef INA219_Init(void)
{
    HAL_StatusTypeDef ret;
    ret = INA219_WriteReg(INA219_REG_CALIBRATION, INA219_CAL_VALUE);
    if (ret != HAL_OK) return ret;
    return INA219_WriteReg(INA219_REG_CONFIG, INA219_CONFIG_VALUE);
}

float INA219_ReadCurrent_mA(void)
{
    uint16_t raw = 0;
    if (INA219_ReadReg(INA219_REG_CURRENT, &raw) != HAL_OK) return 0.0f;
    return (float)((int16_t)raw) * INA219_CURRENT_LSB_MA;
}

float INA219_ReadBusVoltage_mV(void)
{
    uint16_t raw = 0;
    if (INA219_ReadReg(INA219_REG_BUS_V, &raw) != HAL_OK) return 0.0f;
    return (float)(raw >> 3) * 4.0f;
}

float ADC_ReadTemperature_C(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    float vadc = ((float)raw / ADC_RESOLUTION) * ADC_VREF;
    if (vadc <= 0.01f || vadc >= (ADC_VREF - 0.01f)) return -99.0f;
    float r_ntc = NTC_R_FIXED * vadc / (ADC_VREF - vadc);
    float inv_T = (1.0f / NTC_T25_K) + (1.0f / NTC_B) * logf(r_ntc / NTC_R25);
    return (1.0f / inv_T) - 273.15f;
}

void StateMachine_Update(void)
{
    switch (g_state)
    {
        case STATE_IDLE:
            if (g_temperature_c < TEMP_START_HEATING_C &&
                g_voltage_mv    > VOLTAGE_CUTOFF_MV)
            {
                BPC_Start();
                g_state = STATE_HEATING;
                HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            }
            break;

        case STATE_HEATING:
            if (g_temperature_c >= TEMP_STOP_HEATING_C) {
                BPC_Stop();
                g_state = STATE_IDLE;
                HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            }
            else if (g_current_ma > CURRENT_FAULT_MA) {
                BPC_Stop();
                g_state = STATE_FAULT;
                HAL_UART_Transmit(&huart1, (uint8_t*)"FAULT:OVERCURRENT\r\n", 19, 100);
            }
            else if (g_temperature_c > TEMP_FAULT_C) {
                BPC_Stop();
                g_state = STATE_FAULT;
                HAL_UART_Transmit(&huart1, (uint8_t*)"FAULT:OVERTEMP\r\n", 16, 100);
            }
            else if (g_voltage_mv < VOLTAGE_CUTOFF_MV) {
                BPC_Stop();
                g_state = STATE_IDLE;
                HAL_UART_Transmit(&huart1, (uint8_t*)"INFO:UNDERVOLTAGE\r\n", 19, 100);
            }
            break;

        case STATE_FAULT:
            BPC_Stop();
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            break;

        default:
            g_state = STATE_FAULT;
            break;
    }
}

void Data_Transmit(void)
{
    const char *state_str;
    switch (g_state) {
        case STATE_IDLE:    state_str = "IDLE";    break;
        case STATE_HEATING: state_str = "HEATING"; break;
        case STATE_FAULT:   state_str = "FAULT";   break;
        default:            state_str = "UNKNOWN"; break;
    }

    int temp_int  = (int)(g_temperature_c * 10);
    int curr_int  = (int)(g_current_ma * 10);
    int volt_int  = (int)(g_voltage_mv * 10);

    int len = snprintf(g_tx_buf, sizeof(g_tx_buf),
        "%s,%d.%d,%d.%d,%d.%d\r\n",
        state_str,
        temp_int / 10, abs(temp_int % 10),
        curr_int / 10, abs(curr_int % 10),
        volt_int / 10, abs(volt_int % 10));
    HAL_UART_Transmit(&huart1, (uint8_t*)g_tx_buf, (uint16_t)len, 200);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

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
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  BPC_Stop();
  if (INA219_Init() != HAL_OK) {
      g_state = STATE_FAULT;
  }
  HAL_UART_Transmit(&huart1, (uint8_t*)"SIREN BPC Ready\r\n", 17, 100);
  g_last_monitor = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      uint32_t now = HAL_GetTick();
      if ((now - g_last_monitor) >= MONITOR_INTERVAL_MS)
      {
          g_last_monitor    = now;
          g_temperature_c   = ADC_ReadTemperature_C();
          g_current_ma      = INA219_ReadCurrent_mA();
          g_voltage_mv      = INA219_ReadBusVoltage_mV();
          StateMachine_Update();
          Data_Transmit();
      }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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
