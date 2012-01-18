/*
 * Open-MX
 * Copyright © inria 2007-2011
 * Copyright © CNRS 2009
 * (see AUTHORS file)
 *
 * The development of this software has been funded by Myricom, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License in COPYING.GPL for more details.
 */

#define _BSD_SOURCE 1 /* for strdup */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <getopt.h>
#include <assert.h>
#include <malloc.h>	/* memalign() */
#include <arpa/inet.h>	/* htonl() */
#include <sched.h>	/* sched_yield() */

#include "open-mx.h"

#define BID 0
#define EID 0
#define RID 0
#define ITER 1000
#define WARMUP 10
#define MIN 0
#define MAX (1024*4096+1)
#define MULTIPLIER 2
#define INCREMENT 0
#define BUFFER_ALIGN (64*1024) /* page-aligned on any arch */
#define UNIDIR 0
#define SYNC 0
#define YIELD 0
#define PAUSE_MS 100

static unsigned long long
next_length(unsigned long long length, unsigned long long multiplier, unsigned long long increment)
{
  if (length)
    return length*multiplier+increment;
  else if (increment)
    return increment;
  else
    return 1;
}

static inline omx_return_t
omx_test_or_wait(int wait, int yield,
		 omx_endpoint_t ep, omx_request_t * request,
		 struct omx_status *status, uint32_t * result)
{
  if (wait)
    return omx_wait(ep, request, status, result, OMX_TIMEOUT_INFINITE);
  else {
    omx_return_t ret;
    do {
      ret = omx_test(ep, request, status, result);
      if (ret != OMX_SUCCESS)
	return ret;
      if (yield)
	sched_yield();
    } while (!*result);
    return OMX_SUCCESS;
  }
}

static inline omx_return_t
omx_isend_or_issend(int sync,
		    omx_endpoint_t ep,
		    void *buffer, size_t length,
		    omx_endpoint_addr_t dest_endpoint,
		    uint64_t match_info,
		    void * context, omx_request_t * request)
{
  if (sync)
    return omx_issend(ep, buffer, length, dest_endpoint, match_info, context, request);
  else
    return omx_isend(ep, buffer, length, dest_endpoint, match_info, context, request);
}

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, " -s\tswitch to slave receiver mode\n");
  fprintf(stderr, " -w\tsleep instead of busy polling\n");
  fprintf(stderr, " -y\tyield the processor between busy polling loops\n");
  fprintf(stderr, " -v\tverbose\n");
  fprintf(stderr, "Sender options:\n");
  fprintf(stderr, " -a\tuse page-aligned buffers on both hosts\n");
  fprintf(stderr, " -d <hostname>\tset remote peer name and switch to sender mode\n");
  fprintf(stderr, " -r <n>\tchange remote endpoint id [%d]\n", RID);
  fprintf(stderr, " -S <n>\tchange the start length [%d]\n", MIN);
  fprintf(stderr, " -E <n>\tchange the end length [%d]\n", MAX);
  fprintf(stderr, " -M <n>\tchange the length multiplier [%d]\n", MULTIPLIER);
  fprintf(stderr, " -I <n>\tchange the length increment [%d]\n", INCREMENT);
  fprintf(stderr, " -N <n>\tchange number of iterations [%d]\n", ITER);
  fprintf(stderr, " -W <n>\tchange number of warmup iterations [%d]\n", WARMUP);
  fprintf(stderr, " -P <n>\tpause (in milliseconds) between lengths [%d]\n", PAUSE_MS);
  fprintf(stderr, " -U\tswitch to undirectional mode (receiver sends 0-byte replies)\n");
  fprintf(stderr, " -Y\tswitch to synchronous communication mode\n");
}

struct param {
  uint32_t iter;
  uint32_t warmup;
  uint32_t min_low;
  uint32_t min_high;
  uint32_t max_low;
  uint32_t max_high;
  uint32_t multiplier_low;
  uint32_t multiplier_high;
  uint32_t increment_low;
  uint32_t increment_high;
  uint8_t align;
  uint8_t unidir;
  uint8_t sync;
};

#define HTON_DU32(dstlow, dsthigh, val) do { \
  dstlow = htonl((uint32_t)(val & 0xffffffff)); \
  dsthigh = htonl((uint32_t)(val >> 32)); \
} while (0)

#define NTOH_DU32(srclow, srchigh) ((uint64_t) ntohl(srclow)) + (((uint64_t) ntohl(srchigh)) << 32)

int main(int argc, char *argv[])
{
  omx_endpoint_t ep;
  omx_return_t ret;
  int c;

  unsigned bid = BID;
  unsigned eid = EID;
  int rid = RID;
  int iter = ITER;
  int warmup = WARMUP;
  unsigned long long min = MIN;
  unsigned long long max = MAX;
  unsigned long long multiplier = MULTIPLIER;
  unsigned long long increment = INCREMENT;
  int unidir = UNIDIR;
  int sync = SYNC;
  int yield = YIELD;
  int slave = 0;
  char my_hostname[OMX_HOSTNAMELEN_MAX];
  char my_ifacename[OMX_BOARD_ADDR_STRLEN];
  char *dest_hostname = NULL;
  uint64_t dest_addr;
  int sender = 0;
  int verbose = 0;
  void * recvbuffer;
  void * sendbuffer;
  int align = 0;
  int wait = 0;
  int pause_ms = PAUSE_MS;

  while ((c = getopt(argc, argv, "e:r:d:b:S:E:M:I:N:W:P:swUYyvah")) != -1)
    switch (c) {
    case 'b':
      bid = atoi(optarg);
      break;
    case 'e':
      eid = atoi(optarg);
      break;
    case 'd':
      dest_hostname = strdup(optarg);
      sender = 1;
      eid = OMX_ANY_ENDPOINT;
      break;
    case 'r':
      rid = atoi(optarg);
      break;
    case 'S':
      min = atoll(optarg);
      break;
    case 'E':
      max = atoll(optarg);
      break;
    case 'M':
      multiplier = atoll(optarg);
      break;
    case 'I':
      increment = atoll(optarg);
      break;
    case 'N':
      iter = atoi(optarg);
      break;
    case 'W':
      warmup = atoi(optarg);
      break;
    case 'P':
      pause_ms = atoi(optarg);
      break;
    case 's':
      slave = 1;
      break;
    case 'w':
      wait = 1;
      break;
    case 'v':
      verbose = 1;
      break;
    case 'a':
      align = 1;
      break;
    case 'U':
      unidir = 1;
      break;
    case 'Y':
      sync = 1;
      break;
    case 'y':
      yield = 1;
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  if (dest_hostname) {
    ret = omx_hostname_to_nic_id(dest_hostname, &dest_addr);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Cannot find peer name %s\n", dest_hostname);
      goto out;
    }
  }

  ret = omx_open_endpoint(bid, eid, 0x12345678, NULL, 0, &ep);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  ret = omx_get_info(ep, OMX_INFO_BOARD_HOSTNAME,
		     NULL, 0,
		     my_hostname, sizeof(my_hostname));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get endpoint hostname (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  ret = omx_get_info(ep, OMX_INFO_BOARD_IFACENAME,
		     NULL, 0,
		     my_ifacename, sizeof(my_ifacename));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to get endpoint iface name (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  if (eid == OMX_ANY_ENDPOINT)
    printf("Successfully open any endpoint for hostname '%s' iface '%s'\n",
	   my_hostname, my_ifacename);
  else
    printf("Successfully open endpoint %d for hostname '%s' iface '%s'\n",
	   eid, my_hostname, my_ifacename);

  if (sender) {
    /* sender */

    omx_request_t req;
    omx_status_t status;
    uint32_t result;
    omx_endpoint_addr_t addr;
    struct param param;
    struct timeval tv1, tv2;
    unsigned long long us;
    unsigned long long length;
    int i;

    printf("Starting sender to '%s'...\n", dest_hostname);

    ret = omx_connect(ep, dest_addr, rid, 0x12345678, OMX_TIMEOUT_INFINITE, &addr);
    if (ret != OMX_SUCCESS) {
	fprintf(stderr, "Failed to connect (%s)\n",
		omx_strerror(ret));
	goto out_with_ep;
    }

    /* send the param message */
    param.iter = htonl(iter);
    param.warmup = htonl(warmup);
    HTON_DU32(param.min_low, param.min_high, min);
    HTON_DU32(param.max_low, param.max_high, max);
    HTON_DU32(param.multiplier_low, param.multiplier_high, multiplier);
    HTON_DU32(param.increment_low, param.increment_high, increment);
    param.align = align;
    param.unidir = unidir;
    param.sync = sync;
    ret = omx_issend(ep, &param, sizeof(param),
		     addr, 0x1234567887654321ULL,
		     NULL, &req);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to isend param message (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to wait isend param message (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    if (status.code != OMX_SUCCESS) {
      fprintf(stderr, "isend param message failed with status (%s)\n",
	      omx_strerror(status.code));
      goto out_with_ep;
    }

    if (verbose)
      printf("Sent parameters (iter=%d, warmup=%d, min=%lld, max=%lld, mult=%lld, incr=%lld, unidir=%d) to peer %s\n",
	     iter, warmup, min, max, multiplier, increment, unidir, dest_hostname);

    /* wait for the ok message */
    ret = omx_irecv(ep, NULL, 0,
		    0, 0,
		    NULL, &req);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to irecv param ack message (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to wait param ack message (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    if (status.code != OMX_SUCCESS) {
      fprintf(stderr, "param ack message failed with status (%s)\n",
	      omx_strerror(status.code));
      goto out_with_ep;
    }

    for(length = min;
	length < max;
	length = next_length(length, multiplier, increment)) {

      if (align) {
	sendbuffer = memalign(BUFFER_ALIGN, length);
	recvbuffer = memalign(BUFFER_ALIGN, length);
      } else {
        sendbuffer = malloc(length);
        recvbuffer = malloc(length);
      }
      if (!sendbuffer || !recvbuffer) {
	perror("buffer malloc");
	goto out_with_ep;
      }

      for(i=0; i<iter+warmup; i++) {
	if (verbose)
	  printf("Iteration %d/%d\n", i-warmup, iter);

	if (i == warmup)
	  gettimeofday(&tv1, NULL);

	/* sending a message */
	ret = omx_isend_or_issend(sync,
				  ep, sendbuffer, length,
				  addr, 0x1234567887654321ULL,
				  NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to send (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_test_or_wait(wait, yield, ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	if (status.code != OMX_SUCCESS) {
	  fprintf(stderr, "send failed with status (%s)\n",
		  omx_strerror(status.code));
		  goto out_with_ep;
	}

	/* wait for an incoming message */
	ret = omx_irecv(ep, recvbuffer, unidir ? 0 : length,
			0, 0,
			NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to irecv (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_test_or_wait(wait, yield, ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	if (status.code != OMX_SUCCESS) {
	  fprintf(stderr, "irecv failed with status (%s)\n",
		  omx_strerror(status.code));
		  goto out_with_ep;
	}

      }
      if (verbose)
	printf("Iteration %d/%d\n", i-warmup, iter);

      gettimeofday(&tv2, NULL);
      us = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
      if (verbose)
	printf("Total Duration: %lld us\n", us);
      printf("length % 9lld:\t%.3f us\t%.2f MB/s\t %.2f MiB/s\n",
	     length, ((float) us)/(2.-unidir)/iter,
	     (2.-unidir)*iter*length/us, (2.-unidir)*iter*length/us/1.048576);

      free(sendbuffer);
      free(recvbuffer);

      usleep(pause_ms * 1000);
    }

  } else {
    /* receiver */

    omx_request_t req;
    omx_status_t status;
    uint32_t result;
    struct param param;
    omx_endpoint_addr_t addr;
    char src_hostname[OMX_HOSTNAMELEN_MAX];
    uint64_t board_addr;
    uint32_t endpoint_index;
    unsigned long long length;
    int i;

  slave_starts_again:
    printf("Starting receiver...\n");

    if (verbose)
      printf("Waiting for parameters...\n");

    /* wait for the param message */
    ret = omx_irecv(ep, &param, sizeof(param),
		    0, 0,
		    NULL, &req);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to irecv (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to wait (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    if (status.code != OMX_SUCCESS) {
      fprintf(stderr, "irecv param message failed with status (%s)\n",
	      omx_strerror(status.code));
      goto out_with_ep;
    }

    /* retrieve parameters */
    iter = ntohl(param.iter);
    warmup = ntohl(param.warmup);
    min = NTOH_DU32(param.min_low, param.min_high);
    max = NTOH_DU32(param.max_low, param.max_high);
    multiplier = NTOH_DU32(param.multiplier_low, param.multiplier_high);
    increment = NTOH_DU32(param.increment_low, param.increment_high);
    align = param.align;
    unidir = param.unidir;
    sync = param.sync;

    ret = omx_decompose_endpoint_addr(status.addr, &board_addr, &endpoint_index);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to decompose sender's address (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }

    ret = omx_nic_id_to_hostname(board_addr, src_hostname);
    if (ret != OMX_SUCCESS)
      strcpy(src_hostname, "<unknown peer>");

    if (verbose)
      printf("Got parameters (iter=%d, warmup=%d, min=%lld, max=%lld, mult=%lld, incr=%lld, unidir=%d) from peer %s\n",
	     iter, warmup, min, max, multiplier, increment, unidir, src_hostname);

    /* connect back, using iconnect for fun */
    ret = omx_iconnect(ep, board_addr, endpoint_index, 0x12345678,
		       0xabcddcbaabcddcbaULL, (void*) 0xdeadbeef, &req);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to connect back to client (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to wait iconnect (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    if (status.code != OMX_SUCCESS) {
      fprintf(stderr, "send param ack message failed with status (%s)\n",
	      omx_strerror(status.code));
      goto out_with_ep;
    }

    assert(status.match_info == 0xabcddcbaabcddcbaULL);
    assert(status.context == (void*) 0xdeadbeef);
    addr = status.addr;

    /* send param ack message */
    ret = omx_issend(ep, NULL, 0,
		     addr, 0, NULL, &req);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to isend param ack message (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    ret = omx_wait(ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to wait param ack message (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }

    for(length = min;
	length < max;
	length = next_length(length, multiplier, increment)) {

      if (align) {
	sendbuffer = memalign(BUFFER_ALIGN, length);
	recvbuffer = memalign(BUFFER_ALIGN, length);
      } else {
        sendbuffer = malloc(length);
        recvbuffer = malloc(length);
      }
      if (!sendbuffer || !recvbuffer) {
	perror("buffer malloc");
	goto out_with_ep;
      }

      for(i=0; i<iter+warmup; i++) {
	if (verbose)
	  printf("Iteration %d/%d\n", i-warmup, iter);

	/* wait for an incoming message */
	ret = omx_irecv(ep, sendbuffer, length,
			0, 0,
			NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to irecv (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_test_or_wait(wait, yield, ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	if (status.code != OMX_SUCCESS) {
	  fprintf(stderr, "irecv failed with status (%s)\n",
		  omx_strerror(status.code));
		  goto out_with_ep;
	}

	/* sending a message */
	ret = omx_isend_or_issend(sync,
				  ep, recvbuffer, unidir ? 0 : length,
				  addr, 0x1234567887654321ULL,
				  NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to send (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_test_or_wait(wait, yield, ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	if (status.code != OMX_SUCCESS) {
	  fprintf(stderr, "send failed with status (%s)\n",
		  omx_strerror(status.code));
		  goto out_with_ep;
	}
      }
      if (verbose)
	printf("Iteration %d/%d\n", i-warmup, iter);

      free(sendbuffer);
      free(recvbuffer);
    }

    if (slave) {
      usleep(500000);
      omx_progress(ep);
      omx_disconnect(ep, status.addr);
      goto slave_starts_again;
    }
  }

  omx_close_endpoint(ep);
  free(dest_hostname);
  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  free(dest_hostname);
  return -1;
}
