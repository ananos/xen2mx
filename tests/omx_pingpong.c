/*
 * Open-MX
 * Copyright © INRIA 2007 (see AUTHORS file)
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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <getopt.h>
#include <assert.h>
#include <malloc.h>	/* memalign() */
#include <arpa/inet.h>

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

static int
next_length(int length, int multiplier, int increment)
{
  if (length)
    return length*multiplier+increment;
  else if (increment)
    return increment;
  else
    return 1;
}

omx_return_t
omx_test_or_wait(int wait,
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
    } while (!*result);
    return OMX_SUCCESS;
  }
}

static void
usage(void)
{
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, " -s\tswitch to slave receiver mode\n");
  fprintf(stderr, " -w\tsleep instead of busy polling\n");
  fprintf(stderr, " -v\tverbose\n");
  fprintf(stderr, "Sender options:\n");
  fprintf(stderr, " -a\tuse aligned buffers on both hosts\n");
  fprintf(stderr, " -d <hostname>\tset remote peer name and switch to sender mode\n");
  fprintf(stderr, " -r <n>\tchange remote endpoint id [%d]\n", RID);
  fprintf(stderr, " -S <n>\tchange the start length [%d]\n", MIN);
  fprintf(stderr, " -E <n>\tchange the end length [%d]\n", MAX);
  fprintf(stderr, " -M <n>\tchange the length multiplier [%d]\n", MULTIPLIER);
  fprintf(stderr, " -I <n>\tchange the length increment [%d]\n", INCREMENT);
  fprintf(stderr, " -N <n>\tchange number of iterations [%d]\n", ITER);
  fprintf(stderr, " -W <n>\tchange number of warmup iterations [%d]\n", WARMUP);
  fprintf(stderr, " -U\tswitch to undirectional mode (receiver sends 0-byte replies)\n");
}

struct param {
  uint32_t iter;
  uint32_t warmup;
  uint32_t min;
  uint32_t max;
  uint32_t multiplier;
  uint32_t increment;
  uint32_t align;
  uint8_t unidir;
};

int main(int argc, char *argv[])
{
  omx_endpoint_t ep;
  omx_return_t ret;
  char c;

  int bid = BID;
  int eid = EID;
  int rid = RID;
  int iter = ITER;
  int warmup = WARMUP;
  int min = MIN;
  int max = MAX;
  int multiplier = MULTIPLIER;
  int increment = INCREMENT;
  int unidir = UNIDIR;
  int slave = 0;
  char dest_name[OMX_HOSTNAMELEN_MAX];
  uint64_t dest_addr;
  int sender = 0;
  int verbose = 0;
  char * buffer;
  int align = 0;
  int wait = 0;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  while ((c = getopt(argc, argv, "e:r:d:b:S:E:M:I:N:W:swUva")) != EOF)
    switch (c) {
    case 'b':
      bid = atoi(optarg);
      break;
    case 'e':
      eid = atoi(optarg);
      break;
    case 'd':
      strncpy(dest_name, optarg, OMX_HOSTNAMELEN_MAX);
      dest_name[OMX_HOSTNAMELEN_MAX-1] = '\0';
      ret = omx_hostname_to_nic_id(dest_name, &dest_addr);
      if (ret != OMX_SUCCESS) {
	fprintf(stderr, "Cannot find peer name %s\n", dest_name);
	goto out;
      }
      sender = 1;
      break;
    case 'r':
      rid = atoi(optarg);
      break;
    case 'S':
      min = atoi(optarg);
      break;
    case 'E':
      max = atoi(optarg);
      break;
    case 'M':
      multiplier = atoi(optarg);
      break;
    case 'I':
      increment = atoi(optarg);
      break;
    case 'N':
      iter = atoi(optarg);
      break;
    case 'W':
      warmup = atoi(optarg);
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
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
      usage();
      exit(-1);
      break;
    }

  ret = omx_open_endpoint(bid, eid, 0x12345678, NULL, 0, &ep);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  if (sender) {
    /* sender */

    omx_request_t req;
    omx_status_t status;
    uint32_t result;
    omx_endpoint_addr_t addr;
    struct param param;
    struct timeval tv1, tv2;
    unsigned long long us;
    int length;
    int i;

    printf("Starting sender to %s...\n", dest_name);

    ret = omx_connect(ep, dest_addr, rid, 0x12345678, 0, &addr);
    if (ret != OMX_SUCCESS) {
	fprintf(stderr, "Failed to connect (%s)\n",
		omx_strerror(ret));
	goto out_with_ep;
      }

    /* send the param message */
    param.iter = htonl(iter);
    param.warmup = htonl(warmup);
    param.min = htonl(min);
    param.max = htonl(max);
    param.multiplier = htonl(multiplier);
    param.increment = htonl(increment);
    param.align = htonl(align);
    param.unidir = unidir;
    ret = omx_isend(ep, &param, sizeof(param),
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
    if (status.code != OMX_STATUS_SUCCESS) {
      fprintf(stderr, "isend param message failed with status (%s)\n",
	      omx_strstatus(status.code));
      goto out_with_ep;
    }

    if (verbose)
      printf("Sent parameters (iter=%d, warmup=%d, min=%d, max=%d, mult=%d, incr=%d, unidir=%d)\n",
	     iter, warmup, min, max, multiplier, unidir, increment);

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
    if (status.code != OMX_STATUS_SUCCESS) {
      fprintf(stderr, "param ack message failed with status (%s)\n",
	      omx_strstatus(status.code));
      goto out_with_ep;
    }

    for(length = min;
	length < max;
	length = next_length(length, multiplier, increment)) {

      if (align)
	buffer = memalign(BUFFER_ALIGN, length);
      else
        buffer = malloc(length);
      if (!buffer) {
	perror("buffer malloc");
	goto out_with_ep;
      }

      for(i=0; i<iter+warmup; i++) {
	if (verbose)
	  printf("Iteration %d/%d\n", i-warmup, iter);

	if (i == warmup)
	  gettimeofday(&tv1, NULL);

	/* sending a message */
	ret = omx_isend(ep, buffer, length,
			addr, 0x1234567887654321ULL,
			NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to isend (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_test_or_wait(wait, ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	if (status.code != OMX_STATUS_SUCCESS) {
	  fprintf(stderr, "isend failed with status (%s)\n",
		  omx_strstatus(status.code));
		  goto out_with_ep;
	}

	/* wait for an incoming message */
	ret = omx_irecv(ep, buffer, unidir ? 0 : length,
			0, 0,
			NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to irecv (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_test_or_wait(wait, ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	if (status.code != OMX_STATUS_SUCCESS) {
	  fprintf(stderr, "irecv failed with status (%s)\n",
		  omx_strstatus(status.code));
		  goto out_with_ep;
	}

      }
      if (verbose)
	printf("Iteration %d/%d\n", i-warmup, iter);

      gettimeofday(&tv2, NULL);
      us = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
      if (verbose)
	printf("Total Duration: %lld us\n", us);
      printf("length % 9d:\t%.3f us\t%.2f MB/s\t %.2f MiB/s\n",
	     length, ((float) us)/(2.-unidir)/iter,
	     (2.-unidir)*iter*length/us, (2.-unidir)*iter*length/us/1.048576);

      free(buffer);

      sleep(1);
    }

  } else {
    /* receiver */

    omx_request_t req;
    omx_status_t status;
    uint32_t result;
    struct param param;
    omx_endpoint_addr_t addr;
    uint64_t board_addr;
    uint32_t endpoint_index;
    int length;
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
    if (status.code != OMX_STATUS_SUCCESS) {
      fprintf(stderr, "irecv param message failed with status (%s)\n",
	      omx_strstatus(status.code));
      goto out_with_ep;
    }

    /* retrieve parameters */
    iter = ntohl(param.iter);
    warmup = ntohl(param.warmup);
    min = ntohl(param.min);
    max = ntohl(param.max);
    multiplier = ntohl(param.multiplier);
    increment = ntohl(param.increment);
    align = ntohl(param.align);
    unidir = param.unidir;

    ret = omx_decompose_endpoint_addr(status.addr, &board_addr, &endpoint_index);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to decompose sender's address (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }

    ret = omx_nic_id_to_hostname(board_addr, dest_name);
    if (ret != OMX_SUCCESS)
      strcpy(dest_name, "<unknown peer>");

    if (verbose)
      printf("Got parameters (iter=%d, warmup=%d, min=%d, max=%d, mult=%d, incr=%d, unidir=%d) from peer %s\n",
	     iter, warmup, min, max, multiplier, increment, unidir, dest_name);

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
    if (status.code != OMX_STATUS_SUCCESS) {
      fprintf(stderr, "send param ack message failed with status (%s)\n",
	      omx_strstatus(status.code));
      goto out_with_ep;
    }

    assert(status.match_info == 0xabcddcbaabcddcbaULL);
    assert(status.context == (void*) 0xdeadbeef);
    addr = status.addr;

    /* send param ack message */
    ret = omx_isend(ep, NULL, 0,
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

      if (align)
	buffer = memalign(BUFFER_ALIGN, length);
      else
        buffer = malloc(length);
      if (!buffer) {
	perror("buffer malloc");
	goto out_with_ep;
      }

      for(i=0; i<iter+warmup; i++) {
	if (verbose)
	  printf("Iteration %d/%d\n", i-warmup, iter);

	/* wait for an incoming message */
	ret = omx_irecv(ep, buffer, length,
			0, 0,
			NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to irecv (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_test_or_wait(wait, ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	if (status.code != OMX_STATUS_SUCCESS) {
	  fprintf(stderr, "irecv failed with status (%s)\n",
		  omx_strstatus(status.code));
		  goto out_with_ep;
	}

	/* sending a message */
	ret = omx_isend(ep, buffer, unidir ? 0 : length,
			addr, 0x1234567887654321ULL,
			NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to isend (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_test_or_wait(wait, ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	if (status.code != OMX_STATUS_SUCCESS) {
	  fprintf(stderr, "isend failed with status (%s)\n",
		  omx_strstatus(status.code));
		  goto out_with_ep;
	}
      }
      if (verbose)
	printf("Iteration %d/%d\n", i-warmup, iter);

      free(buffer);
    }

    if (slave)
      goto slave_starts_again;
  }

  omx_close_endpoint(ep);
  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  return -1;
}
