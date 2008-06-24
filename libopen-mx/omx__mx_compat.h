/*
 * Open-MX
 * Copyright © INRIA 2007-2008 (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

/*
 * This file copies the MX typedefs from myriexpress.h and mx_raw.h
 * in Myricom's MX distribution.
 */

#ifndef __omx__mx_compat_h__
#define __omx__mx_compat_h__

#include <stdint.h>

/***********
 * MX types
 */

#define MX_API 0x301

#define MX_MAX_STR_LEN 128

typedef struct mx_endpoint * mx_endpoint_t;

#define MX_SIZEOF_ADDR 16

typedef struct {
  uint64_t	stuff[MX_SIZEOF_ADDR/sizeof(uint64_t)];
} mx_endpoint_addr_t;

#define MX_ANY_NIC 0xffffffffU
#define MX_ANY_ENDPOINT 0xffffffffU

typedef union mx_request * mx_request_t;

typedef void * mx_segment_ptr_t;

typedef struct
{
  mx_segment_ptr_t segment_ptr;
  uint32_t segment_length;
}
mx_segment_t;

#define MX_INFINITE   0

enum mx_return_code
{
  /* The operation completed successfully. */
  MX_SUCCESS = 0,
  /* Something really bad happened */
  MX_BAD_BAD_BAD = 1,
  MX_FAILURE = 2,
  /* The MX library was already initialized. */
  MX_ALREADY_INITIALIZED = 3,
  /* The MX library is not initialized. */
  MX_NOT_INITIALIZED = 4,
  /* There are no mx device entries */
  MX_NO_DEV = 5,
  /* Driver is not loaded */
  MX_NO_DRIVER = 6,
  /* Permission denied */
  MX_NO_PERM = 7,
  /* The board index specified in the call does not exist. */
  MX_BOARD_UNKNOWN = 8,
  /* The MX endpoint is not valid or not open. */
  MX_BAD_ENDPOINT = 9,
  /* The list of segments is NULL but the counts is not 0*/
  MX_BAD_SEG_LIST = 10,
  /* The memory described by one of the segments is invalid */
  MX_BAD_SEG_MEM = 11,
  /* The total number of segments exceeds the limit */
  MX_BAD_SEG_CNT = 12,
  /* The pointer to the MX request object is not valid. */
  MX_BAD_REQUEST = 13,
  /* The matching info/mask is not an authorized value. */
  MX_BAD_MATCH_MASK = 14,
  /* MX was unable to perform the operation by lack of resource. */
  MX_NO_RESOURCES = 15,
  /* The list of MX addr is null */
  MX_BAD_ADDR_LIST = 16,
  /* The count of entries in the MX addr list is 0 */
  MX_BAD_ADDR_COUNT = 17,
  /* The index of the root of the broadcast is greater than the
     number of entries in the list of MX addr. */
  MX_BAD_ROOT = 18,
  /* One or more pending operations are not yet completed. */
  MX_NOT_COMPLETED = 19,
  /* This resource is busy. */
  MX_BUSY = 20,
  /* The key is not recognized */
  MX_BAD_INFO_KEY = 21,
  /* The pointer where the info is to be returned is invalid */
  MX_BAD_INFO_VAL = 22,
  /* The NIC identifier (MAC address) is not valid */
  MX_BAD_NIC = 23,
  /* The list of parameters is NULL but the count is not 0 */
  MX_BAD_PARAM_LIST = 24,
  /* The name of one of the parameters is not recognized */
  MX_BAD_PARAM_NAME = 25,
  /* The value of one of the parameters is not valid */
  MX_BAD_PARAM_VAL = 26,
  /* one of the arguments passed to mx_hostname_to_nic_id is not valid**/
  MX_BAD_HOSTNAME_ARGS = 27,
  /* hostname not found **/
  MX_HOST_NOT_FOUND = 28,
  /* The data associated with the request is not yet buffered */
  MX_REQUEST_PENDING = 29,
  /* The function returned because the timeout expired */
  MX_TIMEOUT = 30,
  /* No incoming message matches the matching information */
  MX_NO_MATCH = 31,
  /* An out-of-range endpoint ID was specified */
  MX_BAD_ENDPOINT_ID = 32,
  /* Connection refused -- no peer at this address */
  MX_CONNECTION_FAILED = 33,
  /* Connection denied -- bad key */
  MX_BAD_CONNECTION_KEY = 34,
  /* The length of the buffer for get_info is too small */
  MX_BAD_INFO_LENGTH = 35,
  /* The NIC was not found in the our network peer table */
  MX_NIC_NOT_FOUND = 36,
  /* mx library version is incompatible with kernel or mcp */
  MX_BAD_KERNEL_VERSION = 37,
  /* Application was compiled and linked with different mx versions */
  MX_BAD_LIB_VERSION = 38,
  /* The NIC has died */
  MX_NIC_DEAD = 39,
  /* Cancel not supported on this kind of request */
  MX_CANCEL_NOT_SUPPORTED = 40,
  /* Close not allowed in the handler */
  MX_CLOSE_IN_HANDLER = 41,
  /* Matching info does not respect context id mask */
  MX_BAD_MATCHING_FOR_CONTEXT_ID_MASK = 42,
  /* Feature not supported when context id are enabled */
  MX_NOT_SUPPORTED_WITH_CONTEXT_ID = 43
};

typedef enum mx_return_code mx_return_t;

enum mx_param_key
{
  MX_PARAM_ERROR_HANDLER = 0,
  MX_PARAM_UNEXP_QUEUE_MAX = 1,
  MX_PARAM_CONTEXT_ID = 2
};

typedef enum mx_param_key mx_param_key_t;

typedef mx_return_t (*mx_error_handler_t)(char *str, mx_return_t ret);

typedef struct {
  mx_param_key_t key;
  union {
    mx_error_handler_t error_handler;
    uint32_t unexp_queue_max;
    struct {
      uint8_t bits;
      uint8_t shift;
    } context_id;
  } val;
} mx_param_t;

#define MX_CONTEXT_ID_BITS_MAX 16

enum mx_status_code
{
  /* Successful completion */
  MX_STATUS_SUCCESS = 0,
  /* Request still pending */
  MX_STATUS_PENDING = 1,
  /* Request has been buffered, but still pending */
  MX_STATUS_BUFFERED = 2,
  /* Posted operation failed */
  MX_STATUS_REJECTED = 3,
  /* Posted operation timed out */
  MX_STATUS_TIMEOUT = 4,
  /* Operation completed, but data was truncated due to undersized buffer */
  MX_STATUS_TRUNCATED = 5,
  /* Pending receive was cancelled */
  MX_STATUS_CANCELLED = 6,
  /* Destination nic is unknown on the network fabric */
  MX_STATUS_ENDPOINT_UNKNOWN = 7,
  /* remoted endpoint is closed */
  MX_STATUS_ENDPOINT_CLOSED = 8,
  /* Connectivity is broken between the source and the destination */
  MX_STATUS_ENDPOINT_UNREACHABLE = 9,
  /* Bad session (no mx_connect done?) */
  MX_STATUS_BAD_SESSION = 10,
  /* Connect failed because of bad credentials */
  MX_STATUS_BAD_KEY = 11,
  /* Destination endpoint rank is out of range for the peer */
  MX_STATUS_BAD_ENDPOINT = 12,
  /* Invalid rdma window given to the mcp */
  MX_STATUS_BAD_RDMAWIN = 13,
  /* Operation aborted on peer nic */
  MX_STATUS_ABORTED = 14,
  /* Status internal to the lib/ never returned to user */
  MX_STATUS_EVENTQ_FULL = 15,
  /* MX was unable to perform the operation by lack of resource. */
  MX_STATUS_NO_RESOURCES = 16
};

typedef enum mx_status_code mx_status_code_t;

typedef struct mx_status {
  /* A code indicating status of the completion of this operation. */
  mx_status_code_t code;
  /* The endpoint of the sending endpoint for receive operations */
  mx_endpoint_addr_t source;
  /* The match data from the received message */
  uint64_t match_info;
  /* The original length of the message */
  uint32_t msg_length;
  /* The actual number of bytes transferred,  Note that for a send, this
     does not indicate the size of the buffer provided by the receiver */
  uint32_t xfer_length;
  void *context;
} mx_status_t;

/* MX API function declarations */

/* Initializes the MX library. */

#define mx_init() mx__init_api(MX_API)

typedef void (*mx_matching_callback_t)(void *context,
				       uint64_t match_value,
				       int length);

#define MX_MAX_SEGMENTS 256

#define MX_MATCH_MASK_NONE (~(uint64_t)0)

typedef enum mx_line_speed {
  MX_SPEED_2G,
  MX_SPEED_10G,
  MX_SPEED_OPEN_MX
} mx_line_speed_t;

enum mx_net_type {
  MX_NET_MYRI,
  MX_NET_ETHER
};

enum mx_get_info_key {
  MX_NIC_COUNT = 1,
  MX_NIC_IDS = 2,
  MX_MAX_NATIVE_ENDPOINTS = 3,
  MX_NATIVE_REQUESTS = 4,
  MX_COUNTERS_COUNT = 5,
  MX_COUNTERS_LABELS = 6,
  MX_COUNTERS_VALUES = 7,
  MX_PRODUCT_CODE = 8,
  MX_PART_NUMBER = 9,
  MX_SERIAL_NUMBER = 10,
  MX_PORT_COUNT = 11,
  MX_PIO_SEND_MAX = 12,
  MX_COPY_SEND_MAX = 13,
  MX_NUMA_NODE = 14,
#define MX_HAS_NET_TYPE
  MX_NET_TYPE = 15,
  MX_LINE_SPEED = 16
};

typedef enum mx_get_info_key mx_get_info_key_t;

enum mx_unexp_handler_action {
  MX_RECV_CONTINUE = 0,
  MX_RECV_FINISHED
};

typedef enum mx_unexp_handler_action mx_unexp_handler_action_t;

typedef mx_unexp_handler_action_t
(*mx_unexp_handler_t)(void *context, mx_endpoint_addr_t source,
                      uint64_t match_value, uint32_t length,
                      void * data_if_available);

/***************
 * MX raw types
 */

#define MX_RAW_NO_EVENT      0
#define MX_RAW_SEND_COMPLETE 1
#define MX_RAW_RECV_COMPLETE 2

typedef int mx_endpt_handle_t;

typedef struct mx_raw_endpoint * mx_raw_endpoint_t;

typedef int mx_raw_status_t;

typedef enum {
  MX_HOST_GM = 1,
  MX_HOST_XM = 2,
  MX_HOST_MX = 3,
  MX_HOST_MXvM = 4
} mx_host_type_t;

/***********************
 * API conversion tools
 */

#include "open-mx.h"
#include "omx_lib.h"

extern mx_return_t
omx_unlikely_return_to_mx(omx_return_t omxret);

static inline mx_return_t
omx_return_to_mx(omx_return_t omxret)
{
  if (likely(omxret == OMX_SUCCESS))
    return MX_SUCCESS;
  else
    return omx_unlikely_return_to_mx(omxret);
}

extern omx_return_t
omx_unlikely_return_from_mx(mx_return_t mxret);

static inline omx_return_t
omx_return_from_mx(mx_return_t mxret)
{
  if (likely(mxret == MX_SUCCESS))
    return OMX_SUCCESS;
  else
    return omx_unlikely_return_from_mx(mxret);
}

extern mx_status_code_t
omx_unlikely_status_code_to_mx(omx_return_t omxret);

static inline mx_status_code_t
omx_status_code_to_mx(omx_return_t omxret)
{
  if (likely(omxret == OMX_SUCCESS))
    return MX_STATUS_SUCCESS;
  else
    return omx_unlikely_status_code_to_mx(omxret);
}

extern omx_return_t
omx_unlikely_status_code_from_mx(mx_status_code_t mxcode);

static inline omx_return_t
omx_status_code_from_mx(mx_status_code_t mxcode)
{
  if (likely(mxcode == MX_STATUS_SUCCESS))
    return OMX_SUCCESS;
  else
    return omx_unlikely_status_code_from_mx(mxcode);
}

#define omx_endpoint_ptr_from_mx(epp) ((omx_endpoint_t *) (void *) (epp))
#define omx_endpoint_from_mx(ep) ((omx_endpoint_t) (ep))

#define omx_endpoint_param_ptr_from_mx(paramp) ((omx_endpoint_param_t *) (void *) (paramp))

#define omx_error_handler_to_mx(hdlr) ((mx_error_handler_t) (hdlr))
#define omx_error_handler_from_mx(hdlr) ((omx_error_handler_t) (hdlr))

#define omx_unexp_handler_from_mx(hdlr) ((omx_unexp_handler_t) (hdlr))

#define omx_seg_ptr_from_mx(segp) ((struct omx_seg *) (void *) (segp))

#define omx_endpoint_addr_from_mx(addr) (* (omx_endpoint_addr_t *) (void *) &(addr))
#define omx_endpoint_addr_ptr_from_mx(addr) ((omx_endpoint_addr_t *) (void *) (addr))

#define omx_request_ptr_from_mx(reqp) ((omx_request_t *) (void *) (reqp))
#define omx_request_from_mx(req) ((omx_request_t) (req))

static inline uint32_t
omx_timeout_from_mx(uint32_t mx_timeout)
{
  if (mx_timeout == MX_INFINITE)
    return OMX_TIMEOUT_INFINITE;
  else
    return mx_timeout;
}

static inline void
omx_status_to_mx(struct mx_status *mxst, struct omx_status *omxst)
{
  memcpy(mxst, omxst, sizeof(*mxst));
  mxst->code = omx_status_code_to_mx(omxst->code);
}

#define omx_raw_endpoint_ptr_from_mx(epp) ((omx_raw_endpoint_t *) (void *) (epp))
#define omx_raw_endpoint_from_mx(ep) ((omx_raw_endpoint_t) (ep))

#define omx_raw_status_ptr_from_mx(code) ((omx_raw_status_t *) (void *) (code))

#endif /* __omx__mx_compat_h__ */
