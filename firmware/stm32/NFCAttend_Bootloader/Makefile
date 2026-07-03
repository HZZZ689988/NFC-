TARGET = NFCAttend_Bootloader
BUILD_DIR = build

DEBUG ?= 1
OPT ?= -Os

BASE = ../NFCAttend_Base

C_SOURCES = \
Core/Src/boot_main.c \
Core/Src/boot_gpio.c \
Core/Src/boot_spi.c \
Core/Src/board_spi_bus.c \
Core/Src/boot_stm32f4xx_it.c \
$(BASE)/Core/Src/system_stm32f4xx.c \
$(BASE)/Core/Src/stm32f4xx_hal_msp.c \
$(BASE)/Core/Src/sysmem.c \
$(BASE)/Core/Src/syscalls.c \
../Bsp/w25qxx/w25qxx.c \
../../app/Src/att_crc16.c \
../../app/Src/att_crc32.c \
../../app/Src/att_lfs_port.c \
../../third_party/littlefs/lfs.c \
../../third_party/littlefs/lfs_util.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c \
$(BASE)/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c

ASM_SOURCES = $(BASE)/startup_stm32f407xx.s

PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
else
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

CPU = -mcpu=cortex-m4
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

C_DEFS = \
-DUSE_HAL_DRIVER \
-DSTM32F407xx

C_INCLUDES = \
-ICore/Inc \
-I$(BASE)/Core/Inc \
-I$(BASE)/Drivers/STM32F4xx_HAL_Driver/Inc \
-I$(BASE)/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy \
-I$(BASE)/Drivers/CMSIS/Device/ST/STM32F4xx/Include \
-I$(BASE)/Drivers/CMSIS/Include \
-I../Bsp/w25qxx \
-I../Bsp/NFC \
-I../../app/Inc \
-I../../third_party/littlefs

ASFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG),1)
CFLAGS += -g -gdwarf-2
endif

CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

LDSCRIPT = STM32F407XX_BOOTLOADER.ld
LIBS = -lc -lm -lnosys
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $@

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)
