#ifndef __openmx_h__
#define __openmx_h__

#include <stdint.h>

typedef struct omx_endpoint * omx_endpoint_t;

typedef union omx_request * omx_request_t;

enum omx_return {
  OMX_SUCCESS=0,
  OMX_BAD_ERROR,
  OMX_ALREADY_INITIALIZED,
  OMX_NOT_INITIALIZED,
  OMX_NO_DEVICE,
  OMX_ACCESS_DENIED,
  OMX_NO_RESOURCES,
  OMX_NO_SYSTEM_RESOURCES,
  OMX_INVALID_PARAMETER,
  OMX_NOT_IMPLEMENTED,
};
typedef enum omx_return omx_return_t;

enum omx_status_code {
  OMX_STATUS_SUCCESS=0,
  OMX_STATUS_FAILED,
};
typedef enum omx_status_code omx_status_code_t;

struct omx_status {
  enum omx_status_code code;
  uint64_t board_addr;
  uint32_t ep;
  unsigned long msg_length;
  unsigned long xfer_length;
  uint64_t match_info;
  void *context;
};
typedef struct omx_status omx_status_t;

#define OMX_API 0x0

omx_return_t
omx__init_api(int api);

static inline omx_return_t omx_init(void) { return omx__init_api(OMX_API); }

const char *
omx_strerror(omx_return_t ret);

const char *
omx_strstatus(omx_status_code_t code);

omx_return_t
omx_board_number_to_nic_id(uint32_t board_number,
			   uint64_t *nic_id);

omx_return_t
omx_nic_id_to_board_number(uint64_t nic_id,
			   uint32_t *board_number);

omx_return_t
omx_open_endpoint(uint32_t board_index, uint32_t index,
		  omx_endpoint_t *epp);

omx_return_t
omx_close_endpoint(omx_endpoint_t ep);

omx_return_t
omx_isend(omx_endpoint_t ep,
	  void *buffer, size_t length,
	  uint64_t match_info,
	  uint64_t dest_addr, uint32_t dest_endpoint,
	  void * context, omx_request_t * request);

omx_return_t
omx_irecv(omx_endpoint_t ep,
	  void *buffer, size_t length,
	  uint64_t match_info, uint64_t match_mask,
	  void *context, omx_request_t * request);

omx_return_t
omx_test(omx_endpoint_t ep, omx_request_t * request,
	 struct omx_status *status, uint32_t * result);

omx_return_t
omx_wait(omx_endpoint_t ep, omx_request_t * request,
	 struct omx_status *status, uint32_t * result);

omx_return_t
omx_ipeek(omx_endpoint_t ep, omx_request_t * request,
	  uint32_t *result);

omx_return_t
omx_peek(omx_endpoint_t ep, omx_request_t * request,
	 uint32_t *result);

enum omx_info_key {
  /* return the maximum number of boards */
  OMX_INFO_BOARD_MAX,
  /* return the maximum number of endpoints per board */
  OMX_INFO_ENDPOINT_MAX,
  /* return the current number of boards */
  OMX_INFO_BOARD_COUNT,
  /* return the board name of an endpoint or index (given as uint8_t) */
  OMX_INFO_BOARD_NAME,
  /* return the board addr of an endpoint or index (given as uint8_t) */
  OMX_INFO_BOARD_ADDR,
  /* return the board number of an endpoint or name */
  OMX_INFO_BOARD_INDEX_BY_NAME,
  /* return the board number of an endpoint or addr */
  OMX_INFO_BOARD_INDEX_BY_ADDR,
};
typedef enum omx_info_key omx_info_key_t;

omx_return_t
omx_get_info(omx_endpoint_t ep, omx_info_key_t key,
	     const void * in_val, uint32_t in_len,
	     void * out_val, uint32_t out_len);

#define OMX_HOSTNAMELEN_MAX 80

#define OMX_BOARD_ADDR_STRLEN 18

omx_return_t
omx_hostname_to_nic_id(char *hostname,
		       uint64_t *board_addr);

omx_return_t
omx_nic_id_to_hostname(uint64_t board_addr,
		       char *hostname);

#endif /* __openmx_h__ */
