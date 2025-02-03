/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __FSM_LOGGING_H__
#define __FSM_LOGGING_H__

#include <linux/debugfs.h>

#if defined(CONFIG_IPC_LOGGING)
#include <linux/ipc_logging.h>
#include <linux/string.h>
#endif

#define FSM_DEFAULT_IPC_LOG_PAGES 10

typedef enum {
	FSM_LOG_LEVEL_EMERG,
	FSM_LOG_LEVEL_ALERT,
	FSM_LOG_LEVEL_CRIT,
	FSM_LOG_LEVEL_ERROR,
	FSM_LOG_LEVEL_WARN,
	FSM_LOG_LEVEL_NOTICE,
	FSM_LOG_LEVEL_INFO,
	FSM_LOG_LEVEL_DEBUG,
	FSM_LOG_LEVEL_MAX
} fsm_log_level_t;

static const char * const
	fsm_log_level_str[FSM_LOG_LEVEL_MAX] = {
		"FSM_LOG_LEVEL_EMERG",
		"FSM_LOG_LEVEL_ALERT",
		"FSM_LOG_LEVEL_CRIT",
		"FSM_LOG_LEVEL_ERROR",
		"FSM_LOG_LEVEL_WARN",
		"FSM_LOG_LEVEL_NOTICE",
		"FSM_LOG_LEVEL_INFO",
		"FSM_LOG_LEVEL_DEBUG",
};

static int fsm_log_level_show(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < FSM_LOG_LEVEL_MAX; i++)
		seq_printf(m, "\t%d:\t %s\n", i, fsm_log_level_str[i]);

	return 0;
}

static inline int fsm_log_level_open(
	struct inode *inode,
	struct file *file)
{
	return single_open(file, fsm_log_level_show, NULL);
}

#if defined(CONFIG_IPC_LOGGING)

#define MAX_IPC_LOG_NAME_LEN 25

#define FSM_LOG_DEBUG(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_DEBUG <= fsm_log_level) {\
		pr_debug("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[D][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_INFO(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_INFO <= fsm_log_level) { \
		pr_info("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[I][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_ERROR(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_ERROR <= fsm_log_level) { \
		pr_err("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[E][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_WARN(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_WARN <= fsm_log_level) { \
		pr_warn("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[W][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_CRIT(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_CRIT <= fsm_log_level) { \
		pr_crit("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[C][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_ALERT(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_ALERT <= fsm_log_level) { \
		pr_alert("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[A][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_EMERG(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_EMERG <= fsm_log_level) { \
		pr_emerg("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[EMER][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_DEBUG_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_DEBUG <= fsm_log_level) { \
		pr_debug_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[D][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_INFO_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_INFO <= fsm_log_level) { \
		pr_info_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[I][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_ERROR_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_ERROR <= fsm_log_level) { \
		pr_err_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[E][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_WARN_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_WARN <= fsm_log_level) { \
		pr_warn_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[E][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_CRIT_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_CRIT <= fsm_log_level) { \
		pr_crit_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[E][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_ALERT_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_ALERT <= fsm_log_level) { \
		pr_alert_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[E][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

#define FSM_LOG_EMERG_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_EMERG <= fsm_log_level) { \
		pr_emerg_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
		if (fsm_ipc_log_ctxt) \
			ipc_log_string(fsm_ipc_log_ctxt, \
				"[E][%s]: "__msg, __func__, ##__VA_ARGS__); \
	} \
} while (0)

/*
 * fsm_enable_ipc_logging: Wrapper to ipc_log_context_create()
 *
 * @fsm_ipc_log_ctxt_ptr: Pointer to IPC log context
 * @max_num_pages: Number of pages of logging space required
 * @mod_name     : Name of the directory entry under DEBUGFS
 *
 */
static inline void fsm_enable_ipc_logging(
	void **fsm_ipc_log_ctxt_ptr, int max_num_pages,
	const char *modname, fsm_log_level_t log_level)
{
	char ipc_log_name[MAX_IPC_LOG_NAME_LEN];

	strlcpy(ipc_log_name, modname, MAX_IPC_LOG_NAME_LEN);
	*fsm_ipc_log_ctxt_ptr = ipc_log_context_create(
		max_num_pages,
		ipc_log_name, 0);
	if (*fsm_ipc_log_ctxt_ptr == NULL)
		pr_err("%s: unable to create IPC log context for %s\n",
			__func__, ipc_log_name);
	else
		FSM_LOG_DEBUG(*fsm_ipc_log_ctxt_ptr, log_level,
			"IPC logging: %s is enabled", ipc_log_name);
}

/*
 * fsm_disable_ipc_logging: Wrapper to ipc_log_context_destroy()
 *
 * @fsm_ipc_log_ctxt_ptr: Pointer to IPC log context
 */
static inline void fsm_disable_ipc_logging(void **fsm_ipc_log_ctxt_ptr)
{
	if (*fsm_ipc_log_ctxt_ptr) {
		ipc_log_context_destroy(*fsm_ipc_log_ctxt_ptr);
		*fsm_ipc_log_ctxt_ptr = NULL;
	}
}

#else

#define FSM_LOG_DEBUG(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_DEBUG <= fsm_log_level) \
		pr_debug("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_INFO(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_INFO <= fsm_log_level) \
		pr_info("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_ERROR(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_ERROR <= fsm_log_level) \
		pr_err("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_WARN(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_WARN <= fsm_log_level) \
		pr_warn("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_CRIT(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_CRIT <= fsm_log_level) \
		pr_crit("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_ALERT(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_ALERT <= fsm_log_level) \
		pr_alert("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_EMERG(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_EMERG <= fsm_log_level) \
		pr_emerg("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_DEBUG_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_DEBUG <= fsm_log_level) \
		pr_debug_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_INFO_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_INFO <= fsm_log_level) \
		pr_info_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_ERROR_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_ERROR <= fsm_log_level) \
		pr_err_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_WARN_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_WARN <= fsm_log_level) \
		pr_warn_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_CRIT_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_CRIT <= fsm_log_level) \
		pr_crit_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_ALERT_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_ALERT <= fsm_log_level) \
		pr_alert_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

#define FSM_LOG_EMERG_RATELIMITED(fsm_ipc_log_ctxt, fsm_log_level, __msg, ...) \
do { \
	if ((fsm_log_level_t)FSM_LOG_LEVEL_EMERG <= fsm_log_level) \
		pr_emerg_ratelimited("[%s]: "__msg, __func__, ##__VA_ARGS__); \
} while (0)

static inline void fsm_enable_ipc_logging(
	void **fsm_ipc_log_ctxt_ptr, int max_num_pages,
	const char *modname, fsm_log_level_t log_level)
{
	*fsm_ipc_log_ctxt_ptr = NULL;
}

static inline void fsm_disable_ipc_logging(void **fsm_ipc_log_ctxt_ptr)
{
	*fsm_ipc_log_ctxt_ptr = NULL;
}

#endif /* CONFIG_IPC_LOGGING */
#endif /* __FSM_LOGGING_H__ */
