/*
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
#ifndef _UAPI_QCOTA_H
#define _UAPI_QCOTA_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define QCE_OTA_MAX_BEARER   31
#define OTA_KEY_SIZE 16   /* 128 bits of keys. */
#define OTA_MAC_SIZE 4
#define OTA_MAX_DATA_LENGTH (16 * 1024)
#define OTA_MAX_QUEUE_SIZE (32)
#define OTA_MAX_QUEUE_SIZE_MASK (OTA_MAX_QUEUE_SIZE - 1)

enum qce_ota_type_enum
{
	QCE_OTA_TYPE_CIPHERING = 0,
	QCE_OTA_TYPE_INTEGRITY = 1,
	QCE_OTA_TYPE_LAST
};

enum qce_ota_mode_enum
{
	QCE_OTA_MODE_SYNC = 0,
	QCE_OTA_MODE_ASYNC = 1,
	QCE_OTA_MODE_LAST
};

enum qce_ota_dir_enum {
	QCE_OTA_DIR_UPLINK   = 0,
	QCE_OTA_DIR_DOWNLINK = 1,
	QCE_OTA_DIR_LAST
};

enum qce_ota_algo_enum {
	QCE_OTA_ALGO_SNOW3G = 0,
	QCE_OTA_ALGO_ZUC = 1,
	QCE_OTA_ALGO_AES = 2,
	QCE_OTA_ALGO_LAST
};

/**
 * @brief
 * Result Queue Entry
 *
 * @req_id: Unique Request ID
 * @req_status: Result of the operation requested.
 *              0 - on success. errno - on failure
 * @req_served: If req_served is set (i.e., 1), queue entry is available
 *              for consumption. User space need to unset (i.e., 0) after
 *              consumption.
 * @data_length: Length of data in data buffer
 * @data: Data buffer of length OTA_MAX_DATA_LENGTH to capture
 *        ciphered data or mac_i.
 * @req_type: Cipering or integrity requests.
 *
 */
struct qce_res_queue_entry
{
	__u64 req_id;
	__s32 req_status;
	__u32 req_served;
	__u16 data_length;
	__u8 data[OTA_MAX_DATA_LENGTH];
	enum qce_ota_type_enum req_type;
};

/**
 * @brief
 * Result Queue Handler
 *
 * @cons: consumer index, used by user space library.
 * @prod: producer index used by eip client driver.
 * @res_queue: Response queue context sent to user space.
 *
 */
struct qce_res_queue_handle
{
	__u16 cons;
	__u16 prod;
	struct qce_res_queue_entry res_queue[OTA_MAX_QUEUE_SIZE];
};

/**
 * struct qce_f8_req - qce f8 request
 * @conn_id:	connection ID obtained from previous QCOTA_OPEN_EEA ioctl.
 *		algorithm of Zuc, or Snow3G is implied from conn_id;
 * @req_id:	Unique request ID across all integrity and cipering requests.
 * @data_in:	packets input data stream to be ciphered.
 * @data_out:	ciphered packets output data,
 *		applicable in case of sync mode only.
 * @data_len:	length of data_in and data_out in bytes.
 * @last_bits:	number of partial bits of last byte of data_in. Range 0-7.
 *		0 if last byte is full byte.
 * @count_c:	count-C, ciphering sequence number, 32 bit
 * @bearer:	5 bit of radio bearer identifier.
 * @ckey:	128 bits of confidentiality key,
 *		ckey[0] bit 127-120, ckey[1] bit 119-112,.., ckey[15] bit 7-0.
 * @direction:	uplink or donwlink.
 */
struct qce_f8_req {
	void  *conn_id;
	__u64  req_id;
	__u8  *data_in;
	__u8  *data_out;
	__u16  data_len;
	__u8   last_bits;
	__u32  count_c;
	__u8   bearer;
	__u8   ckey[OTA_KEY_SIZE];
	enum qce_ota_dir_enum  direction;
};

/**
 * struct qce_f9_req - qce f9 request
 * @conn_id:	connection ID obtained from previous QCOTA_OPEN_EIA ioctl.
 *		algorithm of Zuc, or Snow3G is implied from conn_id;
 * @req_id:	Unique request ID across all integrity and cipering requests.
 * @message:	message
 * @msize:	message size in bytes (include the last partial byte).
 * @last_bits:	number of partial bits of the last byte of message. Range 0-7.
 *		0 if last byte is full byte.
 * @mac_i:	4 byte message authentication code,
 *		applicable in case of sync mode only.
 * @count_i:	32 bit count-I integrity sequence number.
 * @fresh_bearer: random 32 bit number, one per user.
 * @ikey:	128 bits of integrity key,
 *		ikey[0] bit 127-120, ikey[1] bit 119-112,.., ikey[15] bit 7-0.
 * @direction:	uplink or donwlink.
 */
struct qce_f9_req {
	void  *conn_id;
	__u64  req_id;
	__u8   *message;
	__u16   msize;
	__u8    last_bits;
	__u8    mac_i[OTA_MAC_SIZE];
	__u32   count_i;
	__u32   fresh_bearer;
	__u8    ikey[OTA_KEY_SIZE];
	enum qce_ota_dir_enum direction;
};

/*
 * struct qce_ota_open_conn_req - qcota connection request
 * @alg:        algorithm of the open request
 * @conn_id:	connection ID, returned. Used in f8 or f9 requests
 *		after obtained from QCOTA_OPEN_EEA and QCOTA_OPEN_EIA
 *		ioctl requests, respectively.
 */
struct qce_ota_open_conn_req {
	enum qce_ota_algo_enum alg;
	void *conn_id;
};

#define QCOTA_IOC_MAGIC     'G'

#define QCOTA_F8_REQ _IOWR(QCOTA_IOC_MAGIC, 1, struct qce_f8_req)

#define QCOTA_F9_REQ _IOWR(QCOTA_IOC_MAGIC, 2, struct qce_f9_req)

#define QCOTA_OPEN_EEA _IOWR(QCOTA_IOC_MAGIC, 3, struct qce_ota_open_conn_req)

#define QCOTA_OPEN_EIA _IOWR(QCOTA_IOC_MAGIC, 4, struct qce_ota_open_conn_req)

#define QCOTA_CLOSE_OTA_CONN _IOWR(QCOTA_IOC_MAGIC, 5, void *)

#define QCOTA_SET_MODE _IOWR(QCOTA_IOC_MAGIC, 6, enum qce_ota_mode_enum)

#endif /* _UAPI_QCOTA_H */
