/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2023-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __FSM_TTI_INTR__
#define __FSM_TTI_INTR__

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include <linux/fsm_tti_intr_if.h>
#include <linux/fsm_logging.h>


#define FSM_TTI_MODULE_NAME	"fsm-tti"
#define FSM_TTI_DEV_CLASS_NAME	FSM_TTI_MODULE_NAME
#define FSM_TTI_CDEV_NAME	FSM_TTI_MODULE_NAME
#define FSM_TTI_GPIO_IRQ_AFFINITY_CORE	0

/* ipc logging */
extern void *fsm_tti_ipc_log;
extern fsm_log_level_t fsm_tti_log_level;

#define FSM_TTI_DEBUG(__msg, ...) \
	FSM_LOG_DEBUG(fsm_tti_ipc_log, fsm_tti_log_level, __msg, ##__VA_ARGS__)

#define FSM_TTI_INFO(__msg, ...) \
	FSM_LOG_INFO(fsm_tti_ipc_log, fsm_tti_log_level, __msg, ##__VA_ARGS__)

#define FSM_TTI_ERROR(__msg, ...) \
	FSM_LOG_ERROR(fsm_tti_ipc_log, fsm_tti_log_level, __msg, ##__VA_ARGS__)

#define FSM_TTI_WARN(__msg, ...) \
	FSM_LOG_WARN(fsm_tti_ipc_log, fsm_tti_log_level, __msg, ##__VA_ARGS__)

#define FSM_TTI_GPIO_NAME	"tti-gpio"
#define FSM_TTI_PAGE_SIZE	PAGE_SIZE
#define FSM_TTI_MAX_NAME_LEN	32

#define MAX_FSM_TTI_DEVICE 1

/* Info for tti interrupt gpio platform */
struct fsm_tti_gpio_platform_data {
	bool assert_falling_edge;
	bool capture_clear;
	unsigned int gpio_pin;
	const char *gpio_label;
};

/* Info for each registered platform device */
struct fsm_tti_gpio_device_data {
	int irq;			/* IRQ used as TTI source */
	bool assert_falling_edge;
	bool capture_clear;
	unsigned int gpio_pin;
	unsigned int tti_irq_affinity;
	char name[FSM_TTI_MAX_NAME_LEN];	/* symbolic name */
};

struct fsm_tti_intr_drv {
	struct device *dev;
	struct class *dev_class;
	struct cdev cdev;
	struct page *page;
	bool is_poll_enabled;
	bool is_seeding_done;
	atomic_t tti_updated;
	bool is_first_tti_intr;
	wait_queue_head_t tti_poll_waitqueue;
	struct tasklet_struct task;
	struct fsm_tti_gpio_device_data *device_data;
	struct fsm_tti_mmap_info *shared_data;
	struct fsm_tti_internal_stats debugfs_stats;
	unsigned int num_fsm;
};

int fsm_tti_cdev_init(struct fsm_tti_intr_drv *tti_intr_drv);
void fsm_tti_cdev_cleanup(struct fsm_tti_intr_drv *tti_intr_drv);
int fsm_tti_debugfs_init(struct fsm_tti_intr_drv *tti_intr_drv);
void fsm_tti_debugfs_cleanup(struct fsm_tti_intr_drv *tti_intr_drv);
void fsm_tti_set_affinity(struct fsm_tti_intr_drv *tti_intr_drv);

#endif /* __FSM_TTI_INTR__ */

