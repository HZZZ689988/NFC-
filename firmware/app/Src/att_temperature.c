#include "att_temperature.h"

#include "stm32f4xx_hal.h"

#define ATT_TEMP_VREF_MV          3300L
#define ATT_TEMP_ADC_MAX          4095L
#define ATT_TEMP_V25_MV           760L
#define ATT_TEMP_AVG_SLOPE_UV     2500L
#define ATT_TEMP_SAMPLE_COUNT     8u
#define ATT_TEMP_ADC_TIMEOUT      100000u

static void short_adc_delay(void)
{
    for (volatile uint32_t i = 0u; i < 10000u; ++i) {
    }
}

static uint8_t read_adc1_temp_raw(uint16_t *raw)
{
    if (raw == NULL) {
        return 0u;
    }

    ADC1->SR = 0u;
    ADC1->CR2 |= ADC_CR2_SWSTART;

    uint32_t timeout = ATT_TEMP_ADC_TIMEOUT;
    while ((ADC1->SR & ADC_SR_EOC) == 0u) {
        if (timeout-- == 0u) {
            return 0u;
        }
    }

    *raw = (uint16_t)(ADC1->DR & 0x0FFFu);
    return 1u;
}

att_status_t att_temperature_read_centi_c(int16_t *centi_c)
{
    if (centi_c == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    __HAL_RCC_ADC1_CLK_ENABLE();

    uint32_t saved_ccr = ADC->CCR;
    uint32_t saved_sr = ADC1->SR;
    uint32_t saved_cr1 = ADC1->CR1;
    uint32_t saved_cr2 = ADC1->CR2;
    uint32_t saved_smpr1 = ADC1->SMPR1;
    uint32_t saved_smpr2 = ADC1->SMPR2;
    uint32_t saved_sqr1 = ADC1->SQR1;
    uint32_t saved_sqr2 = ADC1->SQR2;
    uint32_t saved_sqr3 = ADC1->SQR3;

    ADC1->CR2 &= ~ADC_CR2_ADON;
    ADC->CCR |= ADC_CCR_TSVREFE;
    ADC1->CR1 = 0u;
    ADC1->CR2 = 0u;
    ADC1->SMPR1 = (ADC1->SMPR1 & ~(7u << 18)) | (7u << 18);
    ADC1->SQR1 = 0u;
    ADC1->SQR2 = 0u;
    ADC1->SQR3 = ADC_CHANNEL_TEMPSENSOR;
    ADC1->CR2 |= ADC_CR2_ADON;
    short_adc_delay();

    uint32_t total = 0u;
    for (uint8_t i = 0u; i < ATT_TEMP_SAMPLE_COUNT; ++i) {
        uint16_t raw = 0u;
        if (read_adc1_temp_raw(&raw) == 0u) {
            ADC->CCR = saved_ccr;
            ADC1->SR = saved_sr;
            ADC1->CR1 = saved_cr1;
            ADC1->CR2 = saved_cr2;
            ADC1->SMPR1 = saved_smpr1;
            ADC1->SMPR2 = saved_smpr2;
            ADC1->SQR1 = saved_sqr1;
            ADC1->SQR2 = saved_sqr2;
            ADC1->SQR3 = saved_sqr3;
            return ATT_ERR;
        }
        total += raw;
    }

    ADC->CCR = saved_ccr;
    ADC1->SR = saved_sr;
    ADC1->CR1 = saved_cr1;
    ADC1->CR2 = saved_cr2;
    ADC1->SMPR1 = saved_smpr1;
    ADC1->SMPR2 = saved_smpr2;
    ADC1->SQR1 = saved_sqr1;
    ADC1->SQR2 = saved_sqr2;
    ADC1->SQR3 = saved_sqr3;

    uint32_t raw_avg = total / ATT_TEMP_SAMPLE_COUNT;
    int32_t mv = ((int32_t)raw_avg * ATT_TEMP_VREF_MV) / ATT_TEMP_ADC_MAX;
    int32_t centi = 2500L + (((mv - ATT_TEMP_V25_MV) * 100000L) / ATT_TEMP_AVG_SLOPE_UV);
    if (centi > INT16_MAX) {
        centi = INT16_MAX;
    } else if (centi < INT16_MIN) {
        centi = INT16_MIN;
    }
    *centi_c = (int16_t)centi;
    return ATT_OK;
}
