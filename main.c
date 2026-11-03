
#include "main.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Peripheral handles */
ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart2;

/* Function prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
void Error_Handler(void);
static uint32_t Read_ADC(uint32_t channel, uint8_t *ok);

/* ADC conversion constants */
#define ADC_MAX_VALUE       4095.0f
#define ADC_REFERENCE_V     3.3f

/*
 * ACS712 sensitivity:
 * 5 A version  = 0.185 V/A
 * 20 A version = 0.100 V/A
 * 30 A version = 0.066 V/A
 *
 * Change this value to match your sensor.
 */
#define ACS712_ZERO_V       2.5f
#define ACS712_SENSITIVITY  0.185f

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_USART2_UART_Init();

    char msg[220];
    int len;

    const char *startup =
        "\r\nEV MOTOR DIGITAL TWIN STARTED\r\n"
        "UART: 115200 baud, 8-N-1\r\n";

    HAL_UART_Transmit(&huart2, (uint8_t *)startup,
                      (uint16_t)strlen(startup), 1000);

    while (1)
    {
        uint8_t pot_ok = 0;
        uint8_t lm35_ok = 0;
        uint8_t acs_ok = 0;

        /* STM32F401CB ADC channels */
        uint32_t adc_pot  = Read_ADC(ADC_CHANNEL_0, &pot_ok); /* PA0 */
        uint32_t adc_lm35 = Read_ADC(ADC_CHANNEL_1, &lm35_ok);/* PA1 */
        uint32_t adc_acs  = Read_ADC(ADC_CHANNEL_8, &acs_ok); /* PB0 */

        if (!pot_ok || !lm35_ok || !acs_ok)
        {
            const char *err = "ADC_READ_ERROR\r\n";
            HAL_UART_Transmit(&huart2, (uint8_t *)err,
                              (uint16_t)strlen(err), 1000);
            HAL_Delay(1000);
            continue;
        }

        /* Convert ADC counts to voltage */
        float pot_voltage =
            ((float)adc_pot * ADC_REFERENCE_V) / ADC_MAX_VALUE;

        float lm35_voltage =
            ((float)adc_lm35 * ADC_REFERENCE_V) / ADC_MAX_VALUE;

        float acs_voltage =
            ((float)adc_acs * ADC_REFERENCE_V) / ADC_MAX_VALUE;

        /* LM35: 10 mV per degree Celsius */
        float temperature_c = lm35_voltage * 100.0f;

        /*
         * ACS712 current estimate.
         * This assumes 2.5 V zero-current offset.
         * Calibrate the offset and sensitivity for your sensor.
         */
        float current_a =
            (acs_voltage - ACS712_ZERO_V) / ACS712_SENSITIVITY;

        long current_mA = (long)(current_a * 1000.0f);

        /* Potentiometer-based illustrative load and speed */
        uint32_t load_pct =
            (adc_pot * 100U) / 4095U;

        uint32_t speed_rpm =
            500U + (adc_pot * 2000U) / 4095U;

        /*
         * Integer-scaled output:
         * Temp_x10 = temperature in tenths of a degree C
         * Pot_mV   = potentiometer voltage in millivolts
         * ACS_mV   = ACS712 output voltage in millivolts
         */
        len = snprintf(
            msg, sizeof(msg),
            "ADC_POT=%lu,ADC_LM35=%lu,ADC_ACS=%lu,"
            "Load_pct=%lu,Speed_RPM=%lu,"
            "Pot_mV=%lu,Temp_x10=%lu,"
            "ACS_mV=%lu,Current_mA=%ld\r\n",
            (unsigned long)adc_pot,
            (unsigned long)adc_lm35,
            (unsigned long)adc_acs,
            (unsigned long)load_pct,
            (unsigned long)speed_rpm,
            (unsigned long)(pot_voltage * 1000.0f),
            (unsigned long)(temperature_c * 10.0f),
            (unsigned long)(acs_voltage * 1000.0f),
            current_mA
        );

        if (len > 0 && len < (int)sizeof(msg))
        {
            if (HAL_UART_Transmit(&huart2, (uint8_t *)msg,
                                  (uint16_t)len, 1000) != HAL_OK)
            {
                Error_Handler();
            }
        }

        HAL_Delay(1000);
    }
}

/* Read one ADC channel and report success/failure separately */
static uint32_t Read_ADC(uint32_t channel, uint8_t *ok)
{
    ADC_ChannelConfTypeDef config = {0};
    uint32_t value = 0;

    *ok = 0;

    config.Channel = channel;
    config.Rank = 1;
    config.SamplingTime = ADC_SAMPLETIME_84CYCLES;

    if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK)
    {
        return 0;
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return 0;
    }

    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
    {
        value = HAL_ADC_GetValue(&hadc1);
        *ok = 1;
    }

    HAL_ADC_Stop(&hadc1);
    return value;
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_NONE;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2;

    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef config = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;

    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    config.Channel = ADC_CHANNEL_0;
    config.Rank = 1;
    config.SamplingTime = ADC_SAMPLETIME_84CYCLES;

    if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_USART2_UART_Init(void)
{
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
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
}

void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
        /* Stop here if a fatal initialization or UART error occurs. */
    }
}
