/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define RCC_OSC32_IN_Pin GPIO_PIN_14
#define RCC_OSC32_IN_GPIO_Port GPIOC
#define RCC_OSC32_OUT_Pin GPIO_PIN_15
#define RCC_OSC32_OUT_GPIO_Port GPIOC
#define RCC_OSC_IN_Pin GPIO_PIN_0
#define RCC_OSC_IN_GPIO_Port GPIOF
#define RCC_OSC_OUT_Pin GPIO_PIN_1
#define RCC_OSC_OUT_GPIO_Port GPIOF
#define V_BAT_Pin GPIO_PIN_0
#define V_BAT_GPIO_Port GPIOC
#define CH1_EN_Pin GPIO_PIN_1
#define CH1_EN_GPIO_Port GPIOC
#define RUN_STOP_Pin GPIO_PIN_2
#define RUN_STOP_GPIO_Port GPIOC
#define AUTO_Pin GPIO_PIN_3
#define AUTO_GPIO_Port GPIOC
#define CH3_Pin GPIO_PIN_0
#define CH3_GPIO_Port GPIOA
#define CH4_Pin GPIO_PIN_1
#define CH4_GPIO_Port GPIOA
#define CS_DISP_Pin GPIO_PIN_4
#define CS_DISP_GPIO_Port GPIOC
#define CH4_EN_Pin GPIO_PIN_0
#define CH4_EN_GPIO_Port GPIOB
#define CH1_Pin GPIO_PIN_1
#define CH1_GPIO_Port GPIOB
#define CH2_EN_Pin GPIO_PIN_10
#define CH2_EN_GPIO_Port GPIOB
#define CH3_EN_Pin GPIO_PIN_11
#define CH3_EN_GPIO_Port GPIOB
#define DCAC_CH3_Pin GPIO_PIN_12
#define DCAC_CH3_GPIO_Port GPIOB
#define DCAC_CH1_Pin GPIO_PIN_13
#define DCAC_CH1_GPIO_Port GPIOB
#define DCAC_CH2_Pin GPIO_PIN_14
#define DCAC_CH2_GPIO_Port GPIOB
#define CH2_Pin GPIO_PIN_15
#define CH2_GPIO_Port GPIOB
#define DCAC_CH4_Pin GPIO_PIN_6
#define DCAC_CH4_GPIO_Port GPIOC
#define LED_Pin GPIO_PIN_7
#define LED_GPIO_Port GPIOC
#define T_PUSH_Pin GPIO_PIN_10
#define T_PUSH_GPIO_Port GPIOA
#define T_A_Pin GPIO_PIN_11
#define T_A_GPIO_Port GPIOA
#define T_B_Pin GPIO_PIN_12
#define T_B_GPIO_Port GPIOA
#define T_SWDIO_Pin GPIO_PIN_13
#define T_SWDIO_GPIO_Port GPIOA
#define T_SWCLK_Pin GPIO_PIN_14
#define T_SWCLK_GPIO_Port GPIOA
#define Y_PUSH_Pin GPIO_PIN_15
#define Y_PUSH_GPIO_Port GPIOA
#define CS_REG_Pin GPIO_PIN_10
#define CS_REG_GPIO_Port GPIOC
#define DC_RS_Pin GPIO_PIN_11
#define DC_RS_GPIO_Port GPIOC
#define CAL_1KHZ_Pin GPIO_PIN_12
#define CAL_1KHZ_GPIO_Port GPIOC
#define RESET_Pin GPIO_PIN_2
#define RESET_GPIO_Port GPIOD
#define T_SWO_Pin GPIO_PIN_3
#define T_SWO_GPIO_Port GPIOB
#define X_A_Pin GPIO_PIN_4
#define X_A_GPIO_Port GPIOB
#define X_B_Pin GPIO_PIN_5
#define X_B_GPIO_Port GPIOB
#define Y_A_Pin GPIO_PIN_6
#define Y_A_GPIO_Port GPIOB
#define Y_B_Pin GPIO_PIN_7
#define Y_B_GPIO_Port GPIOB
#define X_PUSH_Pin GPIO_PIN_9
#define X_PUSH_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
