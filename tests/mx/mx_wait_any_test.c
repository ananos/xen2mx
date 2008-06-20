/*************************************************************************
 * The contents of this file are subject to the MYRICOM MYRINET          *
 * EXPRESS (MX) NETWORKING SOFTWARE AND DOCUMENTATION LICENSE (the       *
 * "License"); User may not use this file except in compliance with the  *
 * License.  The full text of the License can found in LICENSE.TXT       *
 *                                                                       *
 * Software distributed under the License is distributed on an "AS IS"   *
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See  *
 * the License for the specific language governing rights and            *
 * limitations under the License.                                        *
 *                                                                       *
 * Copyright 2003 - 2004 by Myricom, Inc.  All rights reserved.          *
 *************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "mx_auto_config.h"
#include "myriexpress.h"
#if !MX_OS_WINNT
#include <unistd.h>
#include <sys/time.h>
#else
#include "mx_uni.h"
#endif
#include "mx_byteswap.h"
#include "test_common.h"

#define FILTER 0x12345
#define DFLT_EID 1

/* all types of messages */
#define LEN1 0
#define LEN2 64
#define LEN3 4096
#define LEN4 131072

/* random 32bits id in the least significant bits */
#define MATCH_VAL1 0xabcdefULL
#define MATCH_VAL2 0xf0f0f0ULL
#define MATCH_VAL3 0x48c48cULL
#define MATCH_VAL4 0x654321ULL

/* bits 41 is used to distinguish sends from receives */

#define MATCH_SENDER_VAL (1ULL << 41)
#define MATCH_RECEIVER_VAL 0ULL
#define MATCH_SIDE_MASK (1ULL << 41)

void
usage()
{
  fprintf(stderr, "Usage: mx_wait_any_test [args]\n");
  fprintf(stderr, "-n nic_id - local NIC ID [MX_ANY_NIC]\n");
  fprintf(stderr, "-b board_id - local Board ID [MX_ANY_NIC]\n");
  fprintf(stderr, "-e local_eid - local endpoint ID [%d]\n", DFLT_EID);
  fprintf(stderr, "---- the following options are only used on the sender side -------\n");
  fprintf(stderr, "-d hostname - destination hostname, required for sender\n");
  fprintf(stderr, "-r remote_eid - remote endpoint ID [%d]\n", DFLT_EID);
  fprintf(stderr, "-f filter - remote filter [%x]\n", FILTER);
  fprintf(stderr, "-h - help\n");
}

static inline
mx_return_t
wait_any_sender(mx_endpoint_t ep,
		mx_endpoint_addr_t dest)
{
  mx_request_t recv1, recv2, recv3, recv4;
  mx_request_t send1, send2, send3, send4;
  mx_segment_t seg1, seg2, seg3, seg4;
  int i;
  mx_status_t status;
  uint32_t result;
  mx_return_t ret;

  seg1.segment_ptr = malloc(LEN1);
  seg1.segment_length = LEN1;
  seg2.segment_ptr = malloc(LEN2);
  seg2.segment_length = LEN2;
  seg3.segment_ptr = malloc(LEN3);
  seg3.segment_length = LEN3;
  seg4.segment_ptr = malloc(LEN4);
  seg4.segment_length = LEN4;

  ret = mx_irecv(ep, &seg1, 1, MATCH_VAL1 | MATCH_RECEIVER_VAL, MX_MATCH_MASK_NONE, (void*)1, &recv1);
  assert(ret == MX_SUCCESS);
  ret = mx_irecv(ep, &seg2, 1, MATCH_VAL2 | MATCH_RECEIVER_VAL, MX_MATCH_MASK_NONE, (void*)2, &recv2);
  assert(ret == MX_SUCCESS);
  ret = mx_irecv(ep, &seg3, 1, MATCH_VAL3 | MATCH_RECEIVER_VAL, MX_MATCH_MASK_NONE, (void*)3, &recv3);
  assert(ret == MX_SUCCESS);
  ret = mx_irecv(ep, &seg4, 1, MATCH_VAL4 | MATCH_RECEIVER_VAL, MX_MATCH_MASK_NONE, (void*)4, &recv4);
  assert(ret == MX_SUCCESS);

  /* 4 send, one each second */
  ret = mx_isend(ep, &seg1, 1, dest, MATCH_VAL1 | MATCH_SENDER_VAL, (void*)10, &send1);
  assert(ret == MX_SUCCESS);
  sleep(1);
  ret = mx_isend(ep, &seg2, 1, dest, MATCH_VAL2 | MATCH_SENDER_VAL, (void*)20, &send2);
  assert(ret == MX_SUCCESS);
  sleep(1);
  ret = mx_isend(ep, &seg3, 1, dest, MATCH_VAL3 | MATCH_SENDER_VAL, (void*)30, &send3);
  assert(ret == MX_SUCCESS);
  sleep(1);
  ret = mx_isend(ep, &seg4, 1, dest, MATCH_VAL4 | MATCH_SENDER_VAL, (void*)40, &send4);
  assert(ret == MX_SUCCESS);

  /* 4 send complete, 5s delay, and 4 receive complete */
  for(i=0; i<8; i++) {
    printf("\n");

    printf("waiting for a %s to complete... \n", i%2?"receiver message":"sender message");
    do {
      ret = mx_wait_any(ep, 1000, i%2 ? MATCH_RECEIVER_VAL : MATCH_SENDER_VAL, MATCH_SIDE_MASK, &status, &result);
      assert(ret == MX_SUCCESS);
      if (!result)
	printf("no request received during 1 second\n");
    } while (!result);
    printf("wait_any result %d\n", result);
    printf("test result %d status code %d\n", result, status.code);
    assert(status.code == MX_STATUS_SUCCESS);

    if (status.context == (void*)1)
      printf("receiver's message #1 completed... \n");
    else if (status.context == (void*)2)
      printf("receiver's message #2 completed... \n");
    else if (status.context == (void*)3)
      printf("receiver's message #3 completed... \n");
    else if (status.context == (void*)4)
      printf("receiver's message #4 completed... \n");
    else if (status.context == (void*)10)
      printf("sender's message #1 completed... \n");
    else if (status.context == (void*)20)
      printf("sender's message #2 completed... \n");
    else if (status.context == (void*)30)
      printf("sender's message #3 completed... \n");
    else if (status.context == (void*)40)
      printf("sender's message #4 completed... \n");
    else
      assert(0);
  }

  free(seg1.segment_ptr);
  free(seg2.segment_ptr);
  free(seg3.segment_ptr);
  free(seg4.segment_ptr);

  return 0;
}

static inline
mx_return_t
wait_any_receiver(mx_endpoint_t ep, uint32_t filter)
{
  mx_request_t recv1, recv2, recv3, recv4;
  mx_request_t send1, send2, send3, send4;
  mx_segment_t seg1, seg2, seg3, seg4;
  int i;
  mx_status_t status;
  uint32_t result;
  mx_endpoint_addr_t dest;
  mx_return_t ret;

  seg1.segment_ptr = malloc(LEN1);
  seg1.segment_length = LEN1;
  seg2.segment_ptr = malloc(LEN2);
  seg2.segment_length = LEN2;
  seg3.segment_ptr = malloc(LEN3);
  seg3.segment_length = LEN3;
  seg4.segment_ptr = malloc(LEN4);
  seg4.segment_length = LEN4;

  ret = mx_irecv(ep, &seg1, 1, MATCH_VAL1 | MATCH_SENDER_VAL, MX_MATCH_MASK_NONE, (void*)1, &recv1);
  assert(ret == MX_SUCCESS);
  ret = mx_irecv(ep, &seg2, 1, MATCH_VAL2 | MATCH_SENDER_VAL, MX_MATCH_MASK_NONE, (void*)2, &recv2);
  assert(ret == MX_SUCCESS);
  ret = mx_irecv(ep, &seg3, 1, MATCH_VAL3 | MATCH_SENDER_VAL, MX_MATCH_MASK_NONE, (void*)3, &recv3);
  assert(ret == MX_SUCCESS);
  ret = mx_irecv(ep, &seg4, 1, MATCH_VAL4 | MATCH_SENDER_VAL, MX_MATCH_MASK_NONE, (void*)4, &recv4);
  assert(ret == MX_SUCCESS);

  /* 4 recv are going to complete, one each second */
  for(i=0; i<4; i++) {
    printf("\n");

    printf("waiting for a sender's message to complete... \n");
    ret = mx_wait_any(ep, MX_INFINITE, MATCH_SENDER_VAL, MATCH_SIDE_MASK, &status, &result);
    assert(ret == MX_SUCCESS);
    printf("wait_any result %d\n", result);
    printf("test result %d status code %d\n", result, status.code);
    assert(status.code == MX_STATUS_SUCCESS);

    if (status.context == (void*)1)
      printf("sender's message #1 completed... \n");
    else if (status.context == (void*)2)
      printf("sender's message #2 completed... \n");
    else if (status.context == (void*)3)
      printf("sender's message #3 completed... \n");
    else if (status.context == (void*)4)
      printf("sender's message #4 completed... \n");
    else
      assert(0);

    memcpy(&dest, &status.source, sizeof(dest));

    if (!i) {
      uint64_t his_nic_id;
      uint32_t his_eid;
      mx_endpoint_addr_t his_addr;
      ret = mx_decompose_endpoint_addr(dest, &his_nic_id, &his_eid);
      assert(ret == MX_SUCCESS);
      ret = mx_connect(ep, his_nic_id, his_eid, filter, MX_INFINITE, &his_addr);
      assert(ret == MX_SUCCESS);
    }
  }

  /* wait 5s to test the timeout on the other side */
  printf("\nwaiting 5s just for fun...\n\n");
  sleep(5);

  ret = mx_isend(ep, &seg1, 1, dest, MATCH_VAL1 | MATCH_RECEIVER_VAL, (void*)10, &send1);
  assert(ret == MX_SUCCESS);
  ret = mx_isend(ep, &seg2, 1, dest, MATCH_VAL2 | MATCH_RECEIVER_VAL, (void*)20, &send2);
  assert(ret == MX_SUCCESS);
  ret = mx_isend(ep, &seg3, 1, dest, MATCH_VAL3 | MATCH_RECEIVER_VAL, (void*)30, &send3);
  assert(ret == MX_SUCCESS);
  ret = mx_isend(ep, &seg4, 1, dest, MATCH_VAL4 | MATCH_RECEIVER_VAL, (void*)40, &send4);
  assert(ret == MX_SUCCESS);

  /* 4 send are going to complete right now */
  for(i=0; i<4; i++) {
    printf("\n");

    printf("waiting for a receiver's message to complete... \n");
    ret = mx_wait_any(ep, MX_INFINITE, MATCH_RECEIVER_VAL, MATCH_SIDE_MASK, &status, &result);
    assert(ret == MX_SUCCESS);
    printf("wait_any result %d\n", result);
    printf("test result %d status code %d\n", result, status.code);
    assert(status.code == MX_STATUS_SUCCESS);

    if (status.context == (void*)10)
      printf("receiver's message #1 completed... \n");
    else if (status.context == (void*)20)
      printf("receiver's message #2 completed... \n");
    else if (status.context == (void*)30)
      printf("receiver's message #3 completed... \n");
    else if (status.context == (void*)40)
      printf("receiver's message #4 completed... \n");
    else
      assert(0);
  }

  free(seg1.segment_ptr);
  free(seg2.segment_ptr);
  free(seg3.segment_ptr);
  free(seg4.segment_ptr);

  return 0;
}

#if MX_OS_FREEBSD
/* hack around bug in pthreads that gets us into an unkillable state */
#include <signal.h>

void
terminate(int sig)
{

  fprintf(stderr, "Exiting due to signal %d\n", sig);
  _exit(sig == 0 ? 0 : 1);
}
#endif

int main(int argc, char **argv)
{
  mx_return_t rc;
  mx_endpoint_t ep;
  uint64_t nic_id;
  uint32_t my_eid;
  uint64_t his_nic_id;
  uint32_t board_id;
  uint32_t filter;
  uint16_t his_eid;
  mx_endpoint_addr_t his_addr;
  char *rem_host;
  int c;
  extern char *optarg;

  mx_param_t param;

#if DEBUG
  extern int mx_debug_mask;
  mx_debug_mask = 0xFFF;
#endif

  /* set up defaults */
  rem_host = NULL;
  filter = FILTER;
  my_eid = DFLT_EID;
  his_eid = DFLT_EID;
  board_id = MX_ANY_NIC;

#if MX_OS_FREEBSD
  (void)signal(SIGINT, terminate);
#endif

  mx_init();
  mx_set_error_handler(MX_ERRORS_RETURN);
  while ((c = getopt(argc, argv, "hd:e:f:n:b:r:")) != EOF) switch(c) {
  case 'd':
    rem_host = optarg;
    break;
  case 'e':
    my_eid = atoi(optarg);
    break;
  case 'f':
    filter = atoi(optarg);
    break;
  case 'n':
    sscanf(optarg, "%"SCNx64, &nic_id);
    rc = mx_nic_id_to_board_number(nic_id, &board_id);
    if (rc != MX_SUCCESS) {
      fprintf(stderr, "nic_id %012"PRIx64" can't be found\n", nic_id);
      exit(1);
    }
    break;
  case 'b':
    board_id = atoi(optarg);
    break;
  case 'r':
    his_eid = atoi(optarg);
    break;
  case 'h':
  default:
    usage();
    exit(1);
  }

  param.key = MX_PARAM_CONTEXT_ID;
  param.val.context_id.bits = 1;
  param.val.context_id.shift = 41;

  rc = mx_open_endpoint(board_id, my_eid, filter, &param, 1, &ep);
  if (rc != MX_SUCCESS) {
    fprintf(stderr, "mx_open_endpoint failed: %s\n", mx_strerror(rc));
    goto abort_with_init;
  }

  /* If no host, we are receiver */
  if (rem_host == NULL) {
    char hostname[MX_MAX_HOSTNAME_LEN];
    mx_endpoint_addr_t me;
    mx_get_endpoint_addr(ep, &me);
    mx_decompose_endpoint_addr(me, &nic_id, &my_eid);
    mx_nic_id_to_hostname(nic_id, hostname);
    printf("Starting wait_any receiver on %s, endpoint=%d\n", hostname, my_eid);

    rc = wait_any_receiver(ep, filter);

  /* remote hostname implies we are sender */
  } else {

    /* get address of destination */
    rc = mx_hostname_to_nic_id(rem_host, &his_nic_id);
    if (rc != MX_SUCCESS) {
      fprintf(stderr, "Error getting remote NIC ID: %s\n", mx_strerror(rc));
      goto abort_with_endpoint;
    }
    rc = mx_connect(ep, his_nic_id, his_eid, filter, MX_INFINITE, &his_addr);
    if (rc != MX_SUCCESS) {
      fprintf(stderr, "Error composing remote endpt: %s\n", mx_strerror(rc));
      goto abort_with_endpoint;
    }

    printf("Starting wait_any sender to host %s\n", rem_host);

    rc = wait_any_sender(ep, his_addr);
  }

 abort_with_endpoint:
  mx_close_endpoint(ep);
 abort_with_init:
  mx_finalize();

  exit(rc != MX_SUCCESS);
}
