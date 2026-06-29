#include "att_lfs_port.h"

#include "w25qxx.h"

#define ATT_FLASH_BLOCK_SIZE      4096u
#define ATT_FLASH_BLOCK_COUNT     4096u
#define ATT_FLASH_READ_SIZE       16u
#define ATT_FLASH_PROG_SIZE       256u
#define ATT_FLASH_CACHE_SIZE      256u
#define ATT_FLASH_LOOKAHEAD_SIZE  64u

static uint8_t s_lfs_read_buffer[ATT_FLASH_CACHE_SIZE];
static uint8_t s_lfs_prog_buffer[ATT_FLASH_CACHE_SIZE];
static uint8_t s_lfs_lookahead_buffer[ATT_FLASH_LOOKAHEAD_SIZE];

static int lfs_w25q_read(const struct lfs_config *cfg, lfs_block_t block,
                         lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)cfg;
    W25QXX_Read((uint8_t *)buffer, block * ATT_FLASH_BLOCK_SIZE + off, size);
    return 0;
}

static int lfs_w25q_prog(const struct lfs_config *cfg, lfs_block_t block,
                         lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)cfg;
    W25QXX_Write_NoCheck((uint8_t *)buffer, block * ATT_FLASH_BLOCK_SIZE + off, size);
    W25QXX_Wait_Busy();
    return 0;
}

static int lfs_w25q_erase(const struct lfs_config *cfg, lfs_block_t block)
{
    (void)cfg;
    W25QXX_Erase_Sector(block);
    W25QXX_Wait_Busy();
    return 0;
}

static int lfs_w25q_sync(const struct lfs_config *cfg)
{
    (void)cfg;
    W25QXX_Wait_Busy();
    return 0;
}

const struct lfs_config g_att_lfs_cfg = {
    .read = lfs_w25q_read,
    .prog = lfs_w25q_prog,
    .erase = lfs_w25q_erase,
    .sync = lfs_w25q_sync,

    .read_size = ATT_FLASH_READ_SIZE,
    .prog_size = ATT_FLASH_PROG_SIZE,
    .block_size = ATT_FLASH_BLOCK_SIZE,
    .block_count = ATT_FLASH_BLOCK_COUNT,
    .block_cycles = 500,
    .cache_size = ATT_FLASH_CACHE_SIZE,
    .lookahead_size = ATT_FLASH_LOOKAHEAD_SIZE,
    .read_buffer = s_lfs_read_buffer,
    .prog_buffer = s_lfs_prog_buffer,
    .lookahead_buffer = s_lfs_lookahead_buffer,
};
