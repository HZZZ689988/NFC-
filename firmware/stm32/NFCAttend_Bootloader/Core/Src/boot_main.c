#include "main.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "att_crc16.h"
#include "att_crc32.h"
#include "att_lfs_port.h"
#include "att_ota_format.h"
#include "gpio.h"
#include "lfs.h"
#include "spi.h"
#include "w25qxx.h"

#define BL_BUF_SIZE             256u
#define BL_SRAM_START           0x20000000u
#define BL_SRAM_END             0x20020000u
#define BL_CCMRAM_START         0x10000000u
#define BL_CCMRAM_END           0x10010000u
#define BL_BOOT_PENDING_MAX     3u
#define BL_FLASH_ERROR_FLAGS    (FLASH_FLAG_EOP | FLASH_FLAG_OPERR | \
                                 FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | \
                                 FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)

typedef void (*bl_entry_fn_t)(void);

static lfs_t s_lfs;
static uint8_t s_buf[BL_BUF_SIZE];

static uint16_t ota_meta_crc(const att_ota_meta_t *meta)
{
    return att_crc16_ccitt_false(meta, offsetof(att_ota_meta_t, crc16));
}

static uint32_t load_u32_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint8_t address_in_range(uint32_t value, uint32_t start, uint32_t end)
{
    return (uint8_t)(value >= start && value < end);
}

static uint8_t slot_is_valid(uint32_t slot)
{
    return (uint8_t)(slot == 1u || slot == 2u);
}

static uint32_t slot_base(uint32_t slot)
{
    return slot == 2u ? ATT_APP_SLOT_B_BASE_ADDR : ATT_APP_SLOT_A_BASE_ADDR;
}

static uint32_t slot_size(uint32_t slot)
{
    return slot == 2u ? ATT_APP_SLOT_B_SIZE_BYTES : ATT_APP_SLOT_A_SIZE_BYTES;
}

static uint32_t slot_end(uint32_t slot)
{
    return slot_base(slot) + slot_size(slot);
}

static uint32_t slot_from_addr(uint32_t addr)
{
    if (address_in_range(addr, ATT_APP_SLOT_A_BASE_ADDR, ATT_APP_SLOT_A_END_ADDR)) {
        return 1u;
    }
    if (address_in_range(addr, ATT_APP_SLOT_B_BASE_ADDR, ATT_APP_SLOT_B_END_ADDR)) {
        return 2u;
    }
    return 0u;
}

static uint8_t stack_pointer_is_valid(uint32_t sp)
{
    if ((sp & 3u) != 0u) {
        return 0u;
    }

    return (uint8_t)((sp > BL_SRAM_START && sp <= BL_SRAM_END) ||
                     (sp > BL_CCMRAM_START && sp <= BL_CCMRAM_END));
}

static uint8_t reset_handler_is_valid(uint32_t reset, uint32_t slot)
{
    uint32_t addr = reset & ~1u;
    return (uint8_t)(((reset & 1u) != 0u) &&
                     slot_is_valid(slot) &&
                     address_in_range(addr, slot_base(slot), slot_end(slot)));
}

static uint8_t app_image_range_is_valid(uint32_t target_addr, uint32_t image_size)
{
    uint32_t slot = slot_from_addr(target_addr);
    if (image_size == 0u || (target_addr & 3u) != 0u) {
        return 0u;
    }

    if (!slot_is_valid(slot) ||
        target_addr != slot_base(slot) ||
        image_size > slot_size(slot)) {
        return 0u;
    }

    return 1u;
}

static uint8_t app_vector_is_valid(uint32_t app_addr)
{
    uint32_t slot = slot_from_addr(app_addr);
    uint32_t sp = *(const uint32_t *)app_addr;
    uint32_t reset = *(const uint32_t *)(app_addr + 4u);
    return (uint8_t)(slot_is_valid(slot) &&
                     stack_pointer_is_valid(sp) &&
                     reset_handler_is_valid(reset, slot));
}

static uint8_t image_header_is_valid(uint32_t target_addr, const uint8_t header[8])
{
    uint32_t slot = slot_from_addr(target_addr);
    uint32_t sp = load_u32_le(header);
    uint32_t reset = load_u32_le(header + 4u);
    return (uint8_t)(slot_is_valid(slot) &&
                     stack_pointer_is_valid(sp) &&
                     reset_handler_is_valid(reset, slot));
}

static uint16_t boot_state_crc(const att_boot_state_t *state)
{
    return att_crc16_ccitt_false(state, offsetof(att_boot_state_t, crc16));
}

static void boot_state_init(att_boot_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->magic = ATT_BOOT_STATE_MAGIC;
    state->version = ATT_BOOT_STATE_VERSION;
    state->size = sizeof(att_boot_state_t);
    if (app_vector_is_valid(ATT_APP_SLOT_A_BASE_ADDR)) {
        state->active_slot = 1u;
        state->confirmed_slot = 1u;
    } else if (app_vector_is_valid(ATT_APP_SLOT_B_BASE_ADDR)) {
        state->active_slot = 2u;
        state->confirmed_slot = 2u;
    }
}

static int ota_meta_read(att_ota_meta_t *meta)
{
    if (meta == NULL) {
        return -1;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_OTA_META_PATH, LFS_O_RDONLY) != 0) {
        return -1;
    }

    att_ota_meta_t stored;
    lfs_ssize_t got = lfs_file_read(&s_lfs, &file, &stored, sizeof(stored));
    (void)lfs_file_close(&s_lfs, &file);
    if (got != (lfs_ssize_t)sizeof(stored)) {
        return -1;
    }

    uint16_t expected = stored.crc16;
    stored.crc16 = 0u;
    uint16_t actual = ota_meta_crc(&stored);
    stored.crc16 = expected;

    if (stored.magic != ATT_OTA_META_MAGIC ||
        stored.version != ATT_OTA_META_VERSION ||
        stored.size != sizeof(att_ota_meta_t) ||
        expected != actual) {
        return -1;
    }

    *meta = stored;
    return 0;
}

static int ota_meta_write(att_ota_meta_t *meta)
{
    if (meta == NULL) {
        return -1;
    }

    meta->crc16 = 0u;
    meta->crc16 = ota_meta_crc(meta);

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_OTA_META_PATH,
                      LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0) {
        return -1;
    }

    lfs_ssize_t written = lfs_file_write(&s_lfs, &file, meta, sizeof(*meta));
    int close_err = lfs_file_close(&s_lfs, &file);
    return (written == (lfs_ssize_t)sizeof(*meta) && close_err == 0) ? 0 : -1;
}

static int boot_state_read(att_boot_state_t *state)
{
    if (state == NULL) {
        return -1;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_BOOT_STATE_PATH, LFS_O_RDONLY) != 0) {
        return -1;
    }

    att_boot_state_t stored;
    lfs_ssize_t got = lfs_file_read(&s_lfs, &file, &stored, sizeof(stored));
    (void)lfs_file_close(&s_lfs, &file);
    if (got != (lfs_ssize_t)sizeof(stored)) {
        return -1;
    }

    uint16_t expected = stored.crc16;
    stored.crc16 = 0u;
    uint16_t actual = boot_state_crc(&stored);
    stored.crc16 = expected;

    if (stored.magic != ATT_BOOT_STATE_MAGIC ||
        stored.version != ATT_BOOT_STATE_VERSION ||
        stored.size != sizeof(att_boot_state_t) ||
        expected != actual) {
        return -1;
    }

    *state = stored;
    return 0;
}

static int boot_state_write(att_boot_state_t *state)
{
    if (state == NULL) {
        return -1;
    }

    state->crc16 = 0u;
    state->crc16 = boot_state_crc(state);

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_BOOT_STATE_PATH,
                      LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0) {
        return -1;
    }

    lfs_ssize_t written = lfs_file_write(&s_lfs, &file, state, sizeof(*state));
    int close_err = lfs_file_close(&s_lfs, &file);
    return (written == (lfs_ssize_t)sizeof(*state) && close_err == 0) ? 0 : -1;
}

static void boot_state_load_or_init(att_boot_state_t *state)
{
    if (boot_state_read(state) == 0) {
        return;
    }

    boot_state_init(state);
    (void)boot_state_write(state);
}

static void ota_meta_set_state(att_ota_meta_t *meta,
                               att_ota_install_state_t state,
                               att_ota_install_error_t error)
{
    if (meta == NULL) {
        return;
    }

    meta->install_state = (uint32_t)state;
    meta->install_error = (uint32_t)error;
    (void)ota_meta_write(meta);
}

static int ota_image_validate_header(uint32_t target_addr)
{
    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_OTA_IMAGE_PATH, LFS_O_RDONLY) != 0) {
        return -1;
    }

    uint8_t header[8];
    lfs_ssize_t got = lfs_file_read(&s_lfs, &file, header, sizeof(header));
    (void)lfs_file_close(&s_lfs, &file);
    if (got != (lfs_ssize_t)sizeof(header)) {
        return -1;
    }

    return image_header_is_valid(target_addr, header) ? 0 : -1;
}

static int ota_image_crc(uint32_t image_size, uint32_t *out_crc)
{
    if (out_crc == NULL || image_size == 0u) {
        return -1;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_OTA_IMAGE_PATH, LFS_O_RDONLY) != 0) {
        return -1;
    }

    uint32_t crc = att_crc32_init();
    uint32_t remaining = image_size;
    while (remaining > 0u) {
        lfs_size_t want = remaining > sizeof(s_buf) ? sizeof(s_buf) : remaining;
        lfs_ssize_t got = lfs_file_read(&s_lfs, &file, s_buf, want);
        if (got != (lfs_ssize_t)want) {
            (void)lfs_file_close(&s_lfs, &file);
            return -1;
        }
        crc = att_crc32_update(crc, s_buf, want);
        remaining -= want;
    }

    if (lfs_file_close(&s_lfs, &file) != 0) {
        return -1;
    }

    *out_crc = att_crc32_finish(crc);
    return 0;
}

static int internal_flash_erase_slot(uint32_t slot)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0u;

    if (!slot_is_valid(slot)) {
        return -1;
    }

    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = slot == 2u ? FLASH_SECTOR_6 : FLASH_SECTOR_4;
    erase.NbSectors = 2u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(BL_FLASH_ERROR_FLAGS);
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &sector_error);
    HAL_FLASH_Lock();

    return (status == HAL_OK && sector_error == 0xFFFFFFFFu) ? 0 : -1;
}

static int program_word(uint32_t address, const uint8_t word_bytes[4])
{
    uint32_t word = load_u32_le(word_bytes);
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, word) == HAL_OK ? 0 : -1;
}

static int internal_flash_program_from_ota(uint32_t target_addr, uint32_t image_size)
{
    if (!app_image_range_is_valid(target_addr, image_size)) {
        return -1;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_OTA_IMAGE_PATH, LFS_O_RDONLY) != 0) {
        return -1;
    }

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(BL_FLASH_ERROR_FLAGS);

    uint32_t written = 0u;
    uint32_t program_addr = target_addr;
    uint8_t pending[4];
    uint8_t pending_len = 0u;
    int result = 0;

    while (written < image_size && result == 0) {
        uint32_t remaining = image_size - written;
        lfs_size_t want = remaining > sizeof(s_buf) ? sizeof(s_buf) : remaining;
        lfs_ssize_t got = lfs_file_read(&s_lfs, &file, s_buf, want);
        if (got != (lfs_ssize_t)want) {
            result = -1;
            break;
        }

        for (lfs_size_t i = 0; i < want; ++i) {
            pending[pending_len++] = s_buf[i];
            if (pending_len == sizeof(pending)) {
                if (program_word(program_addr, pending) != 0) {
                    result = -1;
                    break;
                }
                program_addr += sizeof(pending);
                pending_len = 0u;
            }
            written++;
        }
    }

    if (result == 0 && pending_len > 0u) {
        while (pending_len < sizeof(pending)) {
            pending[pending_len++] = 0xFFu;
        }
        if (program_word(program_addr, pending) != 0) {
            result = -1;
        }
    }

    HAL_FLASH_Lock();
    if (lfs_file_close(&s_lfs, &file) != 0) {
        result = -1;
    }
    return result;
}

static int internal_flash_crc(uint32_t addr, uint32_t size, uint32_t *out_crc)
{
    if (out_crc == NULL || size == 0u) {
        return -1;
    }

    uint32_t crc = att_crc32_init();
    uint32_t remaining = size;
    const uint8_t *cursor = (const uint8_t *)addr;
    while (remaining > 0u) {
        uint32_t want = remaining > BL_BUF_SIZE ? BL_BUF_SIZE : remaining;
        crc = att_crc32_update(crc, cursor, want);
        cursor += want;
        remaining -= want;
    }

    *out_crc = att_crc32_finish(crc);
    return 0;
}

static int ota_meta_is_installable(const att_ota_meta_t *meta)
{
    if (meta == NULL) {
        return 0;
    }

    if (meta->verified == 0u ||
        meta->received_bytes != meta->size_bytes ||
        !app_image_range_is_valid(meta->target_addr, meta->size_bytes) ||
        !slot_is_valid(meta->target_slot) ||
        meta->target_addr != slot_base(meta->target_slot)) {
        return 0;
    }

    return (meta->install_state == ATT_OTA_INSTALL_PENDING ||
            meta->install_state == ATT_OTA_INSTALL_INSTALLING) ? 1 : 0;
}

static int try_install_ota(void)
{
    att_ota_meta_t meta;
    if (ota_meta_read(&meta) != 0) {
        return -1;
    }

    if (!ota_meta_is_installable(&meta)) {
        return 0;
    }

    ota_meta_set_state(&meta, ATT_OTA_INSTALL_INSTALLING, ATT_OTA_INSTALL_ERR_NONE);

    if (ota_image_validate_header(meta.target_addr) != 0) {
        ota_meta_set_state(&meta, ATT_OTA_INSTALL_FAILED, ATT_OTA_INSTALL_ERR_VECTOR);
        return -1;
    }

    uint32_t image_crc = 0u;
    if (ota_image_crc(meta.size_bytes, &image_crc) != 0 || image_crc != meta.expected_crc32) {
        meta.actual_crc32 = image_crc;
        ota_meta_set_state(&meta, ATT_OTA_INSTALL_FAILED, ATT_OTA_INSTALL_ERR_IMAGE_CRC);
        return -1;
    }
    meta.actual_crc32 = image_crc;

    if (internal_flash_erase_slot(meta.target_slot) != 0) {
        ota_meta_set_state(&meta, ATT_OTA_INSTALL_FAILED, ATT_OTA_INSTALL_ERR_ERASE);
        return -1;
    }

    if (internal_flash_program_from_ota(meta.target_addr, meta.size_bytes) != 0) {
        ota_meta_set_state(&meta, ATT_OTA_INSTALL_FAILED, ATT_OTA_INSTALL_ERR_PROGRAM);
        return -1;
    }

    uint32_t flash_crc = 0u;
    if (internal_flash_crc(meta.target_addr, meta.size_bytes, &flash_crc) != 0 ||
        flash_crc != meta.expected_crc32 ||
        !app_vector_is_valid(meta.target_addr)) {
        meta.actual_crc32 = flash_crc;
        ota_meta_set_state(&meta, ATT_OTA_INSTALL_FAILED, ATT_OTA_INSTALL_ERR_VERIFY);
        return -1;
    }

    att_boot_state_t boot_state;
    boot_state_load_or_init(&boot_state);
    boot_state.pending_slot = meta.target_slot;
    boot_state.active_slot = meta.target_slot;
    boot_state.boot_count = 0u;
    boot_state.last_error = 0u;
    if (boot_state_write(&boot_state) != 0) {
        meta.actual_crc32 = flash_crc;
        ota_meta_set_state(&meta, ATT_OTA_INSTALL_FAILED, ATT_OTA_INSTALL_ERR_VERIFY);
        return -1;
    }

    meta.actual_crc32 = flash_crc;
    ota_meta_set_state(&meta, ATT_OTA_INSTALL_INSTALLED, ATT_OTA_INSTALL_ERR_NONE);
    return 1;
}

static uint32_t select_boot_slot(void)
{
    att_boot_state_t state;
    boot_state_load_or_init(&state);

    if (slot_is_valid(state.pending_slot) &&
        app_vector_is_valid(slot_base(state.pending_slot))) {
        if (state.confirmed_slot == state.pending_slot) {
            state.active_slot = state.pending_slot;
            state.pending_slot = 0u;
            state.boot_count = 0u;
            (void)boot_state_write(&state);
            return state.active_slot;
        }

        if (state.boot_count < BL_BOOT_PENDING_MAX) {
            state.active_slot = state.pending_slot;
            state.boot_count++;
            (void)boot_state_write(&state);
            return state.active_slot;
        }

        state.last_error = ATT_OTA_INSTALL_ERR_VERIFY;
        state.pending_slot = 0u;
        state.boot_count = 0u;
        if (slot_is_valid(state.confirmed_slot) &&
            app_vector_is_valid(slot_base(state.confirmed_slot))) {
            state.active_slot = state.confirmed_slot;
            (void)boot_state_write(&state);
            return state.active_slot;
        }
        (void)boot_state_write(&state);
    }

    if (slot_is_valid(state.confirmed_slot) &&
        app_vector_is_valid(slot_base(state.confirmed_slot))) {
        state.active_slot = state.confirmed_slot;
        state.boot_count = 0u;
        (void)boot_state_write(&state);
        return state.active_slot;
    }

    if (slot_is_valid(state.active_slot) &&
        app_vector_is_valid(slot_base(state.active_slot))) {
        return state.active_slot;
    }

    if (app_vector_is_valid(ATT_APP_SLOT_A_BASE_ADDR)) {
        state.active_slot = 1u;
        state.confirmed_slot = 1u;
        state.pending_slot = 0u;
        state.boot_count = 0u;
        (void)boot_state_write(&state);
        return 1u;
    }

    if (app_vector_is_valid(ATT_APP_SLOT_B_BASE_ADDR)) {
        state.active_slot = 2u;
        state.confirmed_slot = 2u;
        state.pending_slot = 0u;
        state.boot_count = 0u;
        (void)boot_state_write(&state);
        return 2u;
    }

    return 0u;
}

static void jump_to_app(uint32_t app_addr)
{
    uint32_t sp = *(const uint32_t *)app_addr;
    uint32_t reset = *(const uint32_t *)(app_addr + 4u);

    __disable_irq();
    HAL_RCC_DeInit();
    HAL_DeInit();

    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;

    for (uint32_t i = 0u; i < 8u; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }

    SCB->VTOR = app_addr;
    __DSB();
    __ISB();
    __set_CONTROL(0u);
    __set_MSP(sp);
    __enable_irq();
    ((bl_entry_fn_t)reset)();
}

static void error_loop(void)
{
    while (1) {
        HAL_GPIO_TogglePin(BL_LED_GPIO_Port, BL_LED_Pin);
        HAL_Delay(150u);
    }
}

int main(void)
{
    HAL_Init();
    MX_GPIO_Init();
    MX_SPI1_Init();
    W25QXX_Init();

    uint32_t boot_slot = 0u;
    if (lfs_mount(&s_lfs, &g_att_lfs_cfg) == 0) {
        (void)try_install_ota();
        boot_slot = select_boot_slot();
        (void)lfs_unmount(&s_lfs);
    }

    if (!slot_is_valid(boot_slot)) {
        if (app_vector_is_valid(ATT_APP_SLOT_A_BASE_ADDR)) {
            boot_slot = 1u;
        } else if (app_vector_is_valid(ATT_APP_SLOT_B_BASE_ADDR)) {
            boot_slot = 2u;
        }
    }

    if (slot_is_valid(boot_slot) && app_vector_is_valid(slot_base(boot_slot))) {
        jump_to_app(slot_base(boot_slot));
    }

    error_loop();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
