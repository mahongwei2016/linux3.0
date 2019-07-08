
# Kernel makefile for motions sensors
#
#

# Untar inv_mpu6050.tgz
#ifeq ($(CONFIG_PREVENT_PROPRIETARY_SOURCE_BUILD),y)
# Binary source for magiclego4x12_tb
	TAR_FILE="binary/inv_mpu6050/itopeet_4x12/inv_mpu6050.tgz"

exec := $(shell if [ -f $(TAR_FILE) ]; then tar -zxf $(TAR_FILE) -C $(src); fi )
#endif


# MPU
ifdef CONFIG_MPU_SENSORS_MPU3050_TOPEET
INV_MODULE_NAME := mpu3050
endif

ifdef CONFIG_MPU_SENSORS_MPU6050A2
INV_MODULE_NAME := mpu6050
endif

ifdef CONFIG_MPU_SENSORS_MPU6050B1
INV_MODULE_NAME := mpu6050
endif

obj-$(CONFIG_INV_SENSORS_MPU6050)	+= $(INV_MODULE_NAME).o

$(INV_MODULE_NAME)-objs += mpuirq.o
$(INV_MODULE_NAME)-objs += slaveirq.o
$(INV_MODULE_NAME)-objs += mpu-dev.o
$(INV_MODULE_NAME)-objs += mlsl-kernel.o
$(INV_MODULE_NAME)-objs += mldl_cfg.o
$(INV_MODULE_NAME)-objs += mldl_print_cfg.o

ifdef CONFIG_MPU_SENSORS_MPU6050A2
$(INV_MODULE_NAME)-objs += accel/mpu6050.o
endif

ifdef CONFIG_MPU_SENSORS_MPU6050B1
$(INV_MODULE_NAME)-objs += accel/mpu6050.o
endif

EXTRA_CFLAGS += -Idrivers/misc/inv_mpu
EXTRA_CFLAGS += -D__C99_DESIGNATED_INITIALIZER
EXTRA_CFLAGS += -DINV_CACHE_DMP=1

obj-$(CONFIG_MPU_SENSORS_TIMERIRQ_TOPEET)+= timerirq.o

obj-y			+= accel/
obj-y			+= compass/
obj-y			+= pressure/
