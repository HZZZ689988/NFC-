#include "main.h"

#include "dma.h"
#include "gpio.h"
#include "delay_us.h"
#include "rc522.h"
#include "usart.h"
#include "uart_drv.h"

#include <stdio.h>

#define RC522_ONLY_SPI_RW_TEST_VALUE 0x7Au

static UartDrv_t s_diag_uart;

typedef struct {
    const char *name;
    GPIO_TypeDef *nss_port;
    uint16_t nss_pin;
    GPIO_TypeDef *sck_port;
    uint16_t sck_pin;
    GPIO_TypeDef *mosi_port;
    uint16_t mosi_pin;
    GPIO_TypeDef *miso_port;
    uint16_t miso_pin;
    GPIO_TypeDef *rst_port;
    uint16_t rst_pin;
} rc522_pin_map_t;

typedef struct {
    const char *name;
    GPIO_TypeDef *port;
    uint16_t pin;
} rc522_pin_t;

static const rc522_pin_map_t s_pin_maps[] = {
    {"DEMO", GPIOE, GPIO_PIN_15, GPIOD, GPIO_PIN_9, GPIOA, GPIO_PIN_0, GPIOB, GPIO_PIN_13, GPIOB, GPIO_PIN_15},
    {"H1", GPIOA, GPIO_PIN_2, GPIOA, GPIO_PIN_1, GPIOC, GPIO_PIN_4, GPIOB, GPIO_PIN_11, GPIOB, GPIO_PIN_13},
    {"H2", GPIOC, GPIO_PIN_1, GPIOA, GPIO_PIN_7, GPIOC, GPIO_PIN_5, GPIOB, GPIO_PIN_12, NULL, 0u},
};

static const rc522_pin_t s_h1_pins[] = {
    {"PA2", GPIOA, GPIO_PIN_2},
    {"PA1", GPIOA, GPIO_PIN_1},
    {"PC4", GPIOC, GPIO_PIN_4},
    {"PB11", GPIOB, GPIO_PIN_11},
    {"PB13", GPIOB, GPIO_PIN_13},
};

static const rc522_pin_t s_h2_pins[] = {
    {"PC1", GPIOC, GPIO_PIN_1},
    {"PA7", GPIOA, GPIO_PIN_7},
    {"PC5", GPIOC, GPIO_PIN_5},
    {"PB12", GPIOB, GPIO_PIN_12},
};

static uint8_t rc522_only_direct_transfer(uint8_t data, uint8_t delay, uint8_t sample_falling)
{
    uint8_t rx = 0u;

    for (int8_t bit = 7; bit >= 0; --bit) {
        HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_RESET);
        delay_us(delay);

        HAL_GPIO_WritePin(NFC_MOSI_GPIO_Port, NFC_MOSI_Pin,
                          (data & 0x80u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        delay_us(delay);

        HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_SET);
        delay_us(delay);

        if (sample_falling) {
            HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_RESET);
            delay_us(delay);
        }

        rx <<= 1;
        if (HAL_GPIO_ReadPin(NFC_MISO_GPIO_Port, NFC_MISO_Pin) == GPIO_PIN_SET) {
            rx |= 0x01u;
        }

        data <<= 1;
    }

    HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_RESET);
    delay_us(delay);
    return rx;
}

static uint8_t rc522_only_map_transfer(const rc522_pin_map_t *map,
                                       uint8_t data,
                                       uint8_t delay,
                                       uint8_t sample_falling)
{
    uint8_t rx = 0u;

    for (int8_t bit = 7; bit >= 0; --bit) {
        HAL_GPIO_WritePin(map->sck_port, map->sck_pin, GPIO_PIN_RESET);
        delay_us(delay);

        HAL_GPIO_WritePin(map->mosi_port, map->mosi_pin,
                          (data & 0x80u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        delay_us(delay);

        HAL_GPIO_WritePin(map->sck_port, map->sck_pin, GPIO_PIN_SET);
        delay_us(delay);

        if (sample_falling) {
            HAL_GPIO_WritePin(map->sck_port, map->sck_pin, GPIO_PIN_RESET);
            delay_us(delay);
        }

        rx <<= 1;
        if (HAL_GPIO_ReadPin(map->miso_port, map->miso_pin) == GPIO_PIN_SET) {
            rx |= 0x01u;
        }

        data <<= 1;
    }

    HAL_GPIO_WritePin(map->sck_port, map->sck_pin, GPIO_PIN_RESET);
    delay_us(delay);
    return rx;
}

static uint8_t rc522_only_direct_read_reg(uint8_t reg, uint8_t delay, uint8_t sample_falling)
{
    uint8_t value;

    HAL_GPIO_WritePin(NFC_NSS_GPIO_Port, NFC_NSS_Pin, GPIO_PIN_RESET);
    delay_us(delay);
    (void)rc522_only_direct_transfer((uint8_t)(((reg << 1) & 0x7Eu) | 0x80u),
                                     delay,
                                     sample_falling);
    value = rc522_only_direct_transfer(0x00u, delay, sample_falling);
    HAL_GPIO_WritePin(NFC_NSS_GPIO_Port, NFC_NSS_Pin, GPIO_PIN_SET);
    delay_us(delay);
    return value;
}

static void rc522_only_gpio_output(GPIO_TypeDef *port,
                                   uint16_t pin,
                                   GPIO_PinState level)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    HAL_GPIO_WritePin(port, pin, level);
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void rc522_only_gpio_input(GPIO_TypeDef *port, uint16_t pin, uint32_t pull)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = pull;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void rc522_only_gpio_output_speed(GPIO_TypeDef *port,
                                         uint16_t pin,
                                         GPIO_PinState level,
                                         uint32_t speed)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    HAL_GPIO_WritePin(port, pin, level);
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = speed;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void rc522_only_reset_candidate_pins(const rc522_pin_t *pins, size_t pin_count)
{
    for (size_t i = 0u; i < pin_count; ++i) {
        rc522_only_gpio_input(pins[i].port, pins[i].pin, GPIO_NOPULL);
    }
}

static void rc522_only_configure_map(const rc522_pin_map_t *map)
{
    HAL_GPIO_WritePin(NFC_GND_GPIO_Port, NFC_GND_Pin, GPIO_PIN_RESET);

    rc522_only_gpio_output(map->nss_port, map->nss_pin, GPIO_PIN_SET);
    rc522_only_gpio_output(map->sck_port, map->sck_pin, GPIO_PIN_RESET);
    rc522_only_gpio_output(map->mosi_port, map->mosi_pin, GPIO_PIN_RESET);
    rc522_only_gpio_input(map->miso_port, map->miso_pin, GPIO_NOPULL);

    if (map->rst_port != NULL) {
        rc522_only_gpio_output(map->rst_port, map->rst_pin, GPIO_PIN_RESET);
        HAL_Delay(10);
        HAL_GPIO_WritePin(map->rst_port, map->rst_pin, GPIO_PIN_SET);
        HAL_Delay(20);
    } else {
        HAL_Delay(20);
    }
}

static uint8_t rc522_only_map_read_reg(const rc522_pin_map_t *map,
                                       uint8_t reg,
                                       uint8_t delay,
                                       uint8_t sample_falling)
{
    uint8_t value;

    HAL_GPIO_WritePin(map->nss_port, map->nss_pin, GPIO_PIN_RESET);
    delay_us(delay);
    (void)rc522_only_map_transfer(map,
                                  (uint8_t)(((reg << 1) & 0x7Eu) | 0x80u),
                                  delay,
                                  sample_falling);
    value = rc522_only_map_transfer(map, 0x00u, delay, sample_falling);
    HAL_GPIO_WritePin(map->nss_port, map->nss_pin, GPIO_PIN_SET);
    delay_us(delay);
    return value;
}

static void rc522_only_configure_demo_low_speed(void)
{
    rc522_only_gpio_output_speed(NFC_GND_GPIO_Port, NFC_GND_Pin, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    rc522_only_gpio_output_speed(NFC_MOSI_GPIO_Port, NFC_MOSI_Pin, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    rc522_only_gpio_output_speed(NFC_NSS_GPIO_Port, NFC_NSS_Pin, GPIO_PIN_SET, GPIO_SPEED_FREQ_LOW);
    rc522_only_gpio_output_speed(NFC_RST_GPIO_Port, NFC_RST_Pin, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    rc522_only_gpio_input(NFC_MISO_GPIO_Port, NFC_MISO_Pin, GPIO_NOPULL);
    rc522_only_gpio_output_speed(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
}

static uint8_t rc522_only_demo_low_read_version(uint8_t delay, uint8_t sample_falling)
{
    rc522_only_configure_demo_low_speed();

    HAL_GPIO_WritePin(NFC_RST_GPIO_Port, NFC_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(NFC_RST_GPIO_Port, NFC_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(NFC_RST_GPIO_Port, NFC_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(20);

    return rc522_only_direct_read_reg(RC522_REG_VERSION, delay, sample_falling);
}

static uint8_t rc522_only_check_output_pin(GPIO_TypeDef *port, uint16_t pin)
{
    uint8_t result = 0u;

    rc522_only_gpio_output_speed(port, pin, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    delay_us(20);
    if (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) {
        result |= 0x01u;
    }

    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    delay_us(20);
    if (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) {
        result |= 0x02u;
    }

    return result;
}

static uint8_t rc522_only_check_gnd_low(void)
{
    rc522_only_gpio_output_speed(NFC_GND_GPIO_Port, NFC_GND_Pin, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    delay_us(20);
    return (uint8_t)(HAL_GPIO_ReadPin(NFC_GND_GPIO_Port, NFC_GND_Pin) == GPIO_PIN_RESET);
}

static void rc522_only_gpio_self_test(char *out, size_t out_len)
{
    uint8_t nss = rc522_only_check_output_pin(NFC_NSS_GPIO_Port, NFC_NSS_Pin);
    uint8_t sck = rc522_only_check_output_pin(NFC_SCK_GPIO_Port, NFC_SCK_Pin);
    uint8_t mosi = rc522_only_check_output_pin(NFC_MOSI_GPIO_Port, NFC_MOSI_Pin);
    uint8_t rst = rc522_only_check_output_pin(NFC_RST_GPIO_Port, NFC_RST_Pin);
    uint8_t gnd_low = rc522_only_check_gnd_low();

    snprintf(out,
             out_len,
             "NSS=%u/SCK=%u/MOSI=%u/RST=%u/GNDL=%u",
             (unsigned int)nss,
             (unsigned int)sck,
             (unsigned int)mosi,
             (unsigned int)rst,
             (unsigned int)gnd_low);

    RC522_Platform_Init();
}

static void rc522_only_scan_pin_maps(char *out, size_t out_len)
{
    size_t used = 0u;

    if (out_len == 0u) {
        return;
    }
    out[0] = '\0';

    for (size_t i = 0u; i < (sizeof(s_pin_maps) / sizeof(s_pin_maps[0])); ++i) {
        const rc522_pin_map_t *map = &s_pin_maps[i];
        rc522_only_configure_map(map);
        uint8_t rise_1us = rc522_only_map_read_reg(map, RC522_REG_VERSION, 1u, 0u);
        uint8_t rise_5us = rc522_only_map_read_reg(map, RC522_REG_VERSION, 5u, 0u);
        uint8_t fall_1us = rc522_only_map_read_reg(map, RC522_REG_VERSION, 1u, 1u);
        int written = snprintf(&out[used],
                               out_len - used,
                               "%s=%02X/%02X/%02X%s",
                               map->name,
                               rise_1us,
                               rise_5us,
                               fall_1us,
                               (i + 1u) < (sizeof(s_pin_maps) / sizeof(s_pin_maps[0])) ? "," : "");
        if (written < 0) {
            out[used] = '\0';
            return;
        }
        if ((size_t)written >= (out_len - used)) {
            out[out_len - 1u] = '\0';
            return;
        }
        used += (size_t)written;
    }

    RC522_Platform_Init();
}

static uint8_t rc522_only_is_version(uint8_t value)
{
    return (uint8_t)((value == 0x91u) || (value == 0x92u));
}

static uint8_t rc522_only_try_perm(const rc522_pin_t *pins,
                                   size_t nss_i,
                                   size_t sck_i,
                                   size_t mosi_i,
                                   size_t miso_i,
                                   int rst_i,
                                   uint8_t *version)
{
    rc522_pin_map_t map = {
        "P",
        pins[nss_i].port, pins[nss_i].pin,
        pins[sck_i].port, pins[sck_i].pin,
        pins[mosi_i].port, pins[mosi_i].pin,
        pins[miso_i].port, pins[miso_i].pin,
        NULL, 0u
    };

    if (rst_i >= 0) {
        map.rst_port = pins[(size_t)rst_i].port;
        map.rst_pin = pins[(size_t)rst_i].pin;
    }

    rc522_only_configure_map(&map);

    uint8_t v1 = rc522_only_map_read_reg(&map, RC522_REG_VERSION, 1u, 0u);
    uint8_t v2 = rc522_only_map_read_reg(&map, RC522_REG_VERSION, 5u, 0u);
    uint8_t v3 = rc522_only_map_read_reg(&map, RC522_REG_VERSION, 1u, 1u);

    if (rc522_only_is_version(v1)) {
        *version = v1;
        return 1u;
    }
    if (rc522_only_is_version(v2)) {
        *version = v2;
        return 1u;
    }
    if (rc522_only_is_version(v3)) {
        *version = v3;
        return 1u;
    }

    return 0u;
}

static uint8_t rc522_only_scan_one_header(const char *header_name,
                                          const rc522_pin_t *pins,
                                          size_t pin_count,
                                          uint8_t has_rst,
                                          char *out,
                                          size_t out_len)
{
    uint8_t version = 0u;

    for (size_t nss = 0u; nss < pin_count; ++nss) {
        for (size_t sck = 0u; sck < pin_count; ++sck) {
            if (sck == nss) continue;
            for (size_t mosi = 0u; mosi < pin_count; ++mosi) {
                if (mosi == nss || mosi == sck) continue;
                for (size_t miso = 0u; miso < pin_count; ++miso) {
                    if (miso == nss || miso == sck || miso == mosi) continue;

                    if (has_rst) {
                        for (size_t rst = 0u; rst < pin_count; ++rst) {
                            if (rst == nss || rst == sck || rst == mosi || rst == miso) continue;
                            rc522_only_reset_candidate_pins(pins, pin_count);
                            if (rc522_only_try_perm(pins, nss, sck, mosi, miso, (int)rst, &version)) {
                                snprintf(out,
                                         out_len,
                                         "%s=N%s/S%s/O%s/I%s/R%s:%02X",
                                         header_name,
                                         pins[nss].name,
                                         pins[sck].name,
                                         pins[mosi].name,
                                         pins[miso].name,
                                         pins[rst].name,
                                         version);
                                return 1u;
                            }
                        }
                    } else {
                        rc522_only_reset_candidate_pins(pins, pin_count);
                        if (rc522_only_try_perm(pins, nss, sck, mosi, miso, -1, &version)) {
                            snprintf(out,
                                     out_len,
                                     "%s=N%s/S%s/O%s/I%s:%02X",
                                     header_name,
                                     pins[nss].name,
                                     pins[sck].name,
                                     pins[mosi].name,
                                     pins[miso].name,
                                     version);
                            return 1u;
                        }
                    }
                }
            }
        }
    }

    snprintf(out, out_len, "%s=none", header_name);
    return 0u;
}

static void rc522_only_scan_permutations(char *out, size_t out_len)
{
    char h1[96];
    char h2[80];

    if (out_len == 0u) {
        return;
    }

    (void)rc522_only_scan_one_header("H1",
                                     s_h1_pins,
                                     sizeof(s_h1_pins) / sizeof(s_h1_pins[0]),
                                     1u,
                                     h1,
                                     sizeof(h1));
    (void)rc522_only_scan_one_header("H2",
                                     s_h2_pins,
                                     sizeof(s_h2_pins) / sizeof(s_h2_pins[0]),
                                     0u,
                                     h2,
                                     sizeof(h2));
    snprintf(out, out_len, "%s,%s", h1, h2);
    RC522_Platform_Init();
}

static uint8_t rc522_only_sample_miso(uint32_t pull)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = NFC_MISO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = pull;
    HAL_GPIO_Init(NFC_MISO_GPIO_Port, &GPIO_InitStruct);
    delay_us(20);
    return (uint8_t)(HAL_GPIO_ReadPin(NFC_MISO_GPIO_Port, NFC_MISO_Pin) == GPIO_PIN_SET);
}

static uint8_t rc522_only_miso_bias_bits(void)
{
    uint8_t bits = 0u;
    bits |= rc522_only_sample_miso(GPIO_NOPULL) ? 0x01u : 0u;
    bits |= rc522_only_sample_miso(GPIO_PULLUP) ? 0x02u : 0u;
    bits |= rc522_only_sample_miso(GPIO_PULLDOWN) ? 0x04u : 0u;
    (void)rc522_only_sample_miso(GPIO_NOPULL);
    return bits;
}

static void rc522_only_print_diag(void)
{
    uint8_t tag_type[2] = {0};
    uint8_t uid[4] = {0};

    uint8_t raw_version = RC522_ReadRegister(RC522_REG_VERSION);
    uint8_t raw_command = RC522_ReadRegister(RC522_REG_COMMAND);
    uint8_t raw_irq = RC522_ReadRegister(RC522_REG_COMIRQ);
    uint8_t raw_fifo = RC522_ReadRegister(RC522_REG_FIFOLEVEL);
    uint8_t raw_tx = RC522_ReadRegister(RC522_REG_TXCONTROL);
    uint8_t raw_error = RC522_ReadRegister(RC522_REG_ERROR);
    uint8_t raw_pins = RC522_Platform_ReadPins();
    uint8_t speed_before = RC522_ReadRegister(RC522_REG_SERIALSPEED);
    RC522_WriteRegister(RC522_REG_SERIALSPEED, RC522_ONLY_SPI_RW_TEST_VALUE);
    uint8_t speed_test = RC522_ReadRegister(RC522_REG_SERIALSPEED);
    RC522_WriteRegister(RC522_REG_SERIALSPEED, speed_before);
    uint8_t speed_after = RC522_ReadRegister(RC522_REG_SERIALSPEED);
    uint8_t spi_rw_ok = (uint8_t)((speed_test == RC522_ONLY_SPI_RW_TEST_VALUE) &&
                                  (speed_after == speed_before));

    RC522_ConfigISOType('A');

    uint8_t version = RC522_ReadRegister(RC522_REG_VERSION);
    uint8_t command = RC522_ReadRegister(RC522_REG_COMMAND);
    uint8_t irq = RC522_ReadRegister(RC522_REG_COMIRQ);
    uint8_t fifo = RC522_ReadRegister(RC522_REG_FIFOLEVEL);
    uint8_t tx = RC522_ReadRegister(RC522_REG_TXCONTROL);
    uint8_t error = RC522_ReadRegister(RC522_REG_ERROR);
    uint8_t pins = RC522_Platform_ReadPins();
    int request_status = (int)RC522_Request(RC522_PICC_REQALL, tag_type);
    int scan_status = (int)RC522_ScanCard(uid);
    uint8_t share = RC522_Platform_MosiSharesFlashCs();
    uint8_t miso_bias = rc522_only_miso_bias_bits();
    uint8_t direct_rise_1us = rc522_only_direct_read_reg(RC522_REG_VERSION, 1u, 0u);
    uint8_t direct_rise_5us = rc522_only_direct_read_reg(RC522_REG_VERSION, 5u, 0u);
    uint8_t direct_fall_1us = rc522_only_direct_read_reg(RC522_REG_VERSION, 1u, 1u);
    uint8_t demo_low_rise_1us = rc522_only_demo_low_read_version(1u, 0u);
    uint8_t demo_low_rise_5us = rc522_only_demo_low_read_version(5u, 0u);
    uint8_t demo_low_fall_1us = rc522_only_demo_low_read_version(1u, 1u);
    char map_scan[64];
    char gpio_self[80];
    static char perm_scan[180];
    static uint8_t perm_scan_done = 0u;

    rc522_only_scan_pin_maps(map_scan, sizeof(map_scan));
    if (!perm_scan_done) {
        rc522_only_scan_permutations(perm_scan, sizeof(perm_scan));
        perm_scan_done = 1u;
    }
    rc522_only_gpio_self_test(gpio_self, sizeof(gpio_self));

    printf("RC522_ONLY:RAW=0x%02X|VER=0x%02X|CMD=0x%02X->0x%02X|IRQ=0x%02X->0x%02X|FIFO=0x%02X->0x%02X|TX=0x%02X->0x%02X|ERR=0x%02X->0x%02X|PINS=0x%02X->0x%02X|SHARE=%u|SPD=0x%02X->0x%02X->0x%02X|RW=%u|MISO=0x%02X|DRVVER=%02X/%02X/%02X|DEMOLOW=%02X/%02X/%02X|GPIOCHK=%s|MAP=%s|PERM=%s|REQ=%d|TAG=%02X%02X|SCAN=%d|UID=%02X%02X%02X%02X\r\n",
           raw_version,
           version,
           raw_command,
           command,
           raw_irq,
           irq,
           raw_fifo,
           fifo,
           raw_tx,
           tx,
           raw_error,
           error,
           raw_pins,
           pins,
           (unsigned int)share,
           speed_before,
           speed_test,
           speed_after,
           (unsigned int)spi_rw_ok,
           miso_bias,
           direct_rise_1us,
           direct_rise_5us,
           direct_fall_1us,
           demo_low_rise_1us,
           demo_low_rise_5us,
           demo_low_fall_1us,
           gpio_self,
           map_scan,
           perm_scan,
           request_status,
           tag_type[0],
           tag_type[1],
           scan_status,
           uid[0],
           uid[1],
           uid[2],
           uid[3]);
}

void RC522_Only_Diag_Run(void)
{
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART1_UART_Init();

    UartDrv_Init(&s_diag_uart, &huart1);
    UartDrv_SetDebugPort(&s_diag_uart);

    printf("\r\nRC522-only diagnostic firmware started\r\n");
    printf("Pins: GND=PB10 NSS=PE15 SCK=PD9 MOSI=PA0 MISO=PB13 RST=PB15 3V3 physical\r\n");
    printf("Goal: VER should be 0x91 or 0x92 before card tests matter\r\n");

    RC522_Platform_Init();

    while (1) {
        rc522_only_print_diag();
        HAL_Delay(1000);
    }
}
