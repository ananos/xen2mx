/*
 * Open-MX
 * Copyright © INRIA, CNRS 2007-2009 (see AUTHORS file)
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
#include <string.h>
#include <sys/time.h>
#include <getopt.h>
#include <assert.h>

#include "open-mx.h"

#define ITER 10
#define MAX 16777216

struct rail {
  omx_endpoint_t ep;
  char local_name[OMX_HOSTNAMELEN_MAX];
  uint64_t local_nicid;
  uint32_t local_eid;
  omx_endpoint_addr_t local_addr;

  char remote_name[OMX_HOSTNAMELEN_MAX];
  uint64_t remote_nicid;
  uint32_t remote_eid;
  omx_endpoint_addr_t remote_addr;

  omx_request_t req;
} * rails;

static void
usage(int argc, char *argv[])
{
  fprintf(stderr, "%s [options]\n", argv[0]);
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, " -R\tnumber of rails\n");
  fprintf(stderr, "Sender options:\n");
  fprintf(stderr, " -d <hostname1>,...\tset remote peer names and switch to sender mode\n");
}

int main(int argc, char *argv[])
{
  struct rail * rails;
  omx_return_t ret;
  unsigned nbrails = 0;
  int c;
  int i;
  unsigned long length;
  int sender = 0;
  char *dest_hostname = NULL;
  char *buffer;

  while ((c = getopt(argc, argv, "R:d:vh")) != -1)
    switch (c) {
    case 'd':
      dest_hostname = strdup(optarg);
      sender = 1;
      break;
    case 'R':
      nbrails = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
    case 'h':
      usage(argc, argv);
      exit(-1);
      break;
    }

  if (!nbrails) {
    fprintf(stderr, "0 rails requested, nothing to do\n");
    goto out;
  }

  rails = malloc(nbrails*sizeof(*rails));
  if (!rails) {
    fprintf(stderr, "failed to allocate rails\n");
    goto out;
  }

  buffer = malloc(MAX*nbrails);
  if (!buffer) {
    fprintf(stderr, "failed to allocate buffer\n");
    goto out;
  }

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  for(i=0; i<nbrails; i++) {
    ret = omx_open_endpoint(OMX_ANY_NIC, OMX_ANY_ENDPOINT, 0x87654321+i, NULL, 0, &rails[i].ep);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to open endpoint #%d (%s)\n",
	      i, omx_strerror(ret));
      goto out;
    }
    ret = omx_get_info(rails[i].ep, OMX_INFO_BOARD_HOSTNAME,
		       NULL, 0,
		       rails[i].local_name, sizeof(rails[i].local_name));
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to get endpoint #%d hostname (%s)\n",
	      i, omx_strerror(ret));
      goto out;
    }
    ret = omx_get_endpoint_addr(rails[i].ep, &rails[i].local_addr);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to get endpoint #%d addr (%s)\n",
	      i, omx_strerror(ret));
      goto out;
    }
    ret = omx_decompose_endpoint_addr(rails[i].local_addr, &rails[i].local_nicid, &rails[i].local_eid);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to decompose endpoint #%d addr (%s)\n",
	      i, omx_strerror(ret));
      goto out;
    }
  }

  if (dest_hostname) {
    char *tmp = dest_hostname;
    i = 0;
    while (1) {
      char *colon, *next;

      next = strchr(tmp, ',');
      if (next)
        *next = '\0';

      colon = strrchr(tmp, ':');
      if (colon) {
        rails[i].remote_eid = atoi(colon+1);
        *colon = '\0';
      } else {
	rails[i].remote_eid = 0;
      }

      strncpy(rails[i].remote_name, tmp, OMX_HOSTNAMELEN_MAX);
      rails[i].remote_name[OMX_HOSTNAMELEN_MAX-1] = '\0';

      ret = omx_hostname_to_nic_id(rails[i].remote_name, &rails[i].remote_nicid);
      if (ret != OMX_SUCCESS) {
        fprintf(stderr, "Cannot find peer name #%d %s\n", i, rails[i].remote_name);
        goto out;
      }

      if (!next)
	break;
      tmp = next+1;
      i++;
      if (i == nbrails)
        break;
    }
    if (i != nbrails-1) {
      fprintf(stderr, "Found %d peer names instead of %d\n", i+1, nbrails);
      goto out;
    }
  }

  if (sender) {
    /* sender */

    printf("Starting sender to remote addresses ");
    for(i=0; i<nbrails; i++)
      printf("%s:%ld%s", rails[i].remote_name, (unsigned long) rails[i].remote_eid, i==nbrails-1?"":",");
    printf("\n");

    /* connection handshakes, one at a time so that the remote side is polling when the request arrives */
    for(i=0; i<nbrails; i++) {
      omx_status_t status;
      uint32_t result;
      ret = omx_connect(rails[i].ep, rails[i].remote_nicid, rails[i].remote_eid, 0x87654321+i, OMX_TIMEOUT_INFINITE, &rails[i].remote_addr);
      if (ret != OMX_SUCCESS) {
        fprintf(stderr, "Failed to connect to peer #%d name %s endpoint %ld\n",
		i, rails[i].remote_name, (unsigned long) rails[i].remote_eid);
        goto out;
      }
      ret = omx_isend(rails[i].ep, buffer, 0, rails[i].remote_addr, 0, NULL, &rails[i].req);
      assert(ret == OMX_SUCCESS);
      ret = omx_wait(rails[i].ep, &rails[i].req, &status, &result, OMX_TIMEOUT_INFINITE);
      assert(ret == OMX_SUCCESS);
      assert(result);
      ret = omx_irecv(rails[i].ep, buffer, 0, 0, 0, NULL, &rails[i].req);
      assert(ret == OMX_SUCCESS);
      ret = omx_wait(rails[i].ep, &rails[i].req, &status, &result, OMX_TIMEOUT_INFINITE);
      assert(ret == OMX_SUCCESS);
      assert(result);
    }

    /* actual loop */
    for(length=0; length<MAX; length?length*=2:length++) {
      struct timeval tv1,tv2;
      gettimeofday(&tv1, NULL);
      int j;
      for(j=0; j<ITER; j++) {
	for(i=0; i<nbrails; i++) {
	  ret = omx_isend(rails[i].ep, buffer+i*length, length, rails[i].remote_addr, 0, NULL, &rails[i].req);
	  assert(ret == OMX_SUCCESS);
	}
	for(i=0; i<nbrails; i++) {
	  omx_status_t status;
	  uint32_t result = 0;
	  ret = omx_wait(rails[i].ep, &rails[i].req, &status, &result, OMX_TIMEOUT_INFINITE);
	  assert(ret == OMX_SUCCESS);
	  assert(result);
	}
	for(i=0; i<nbrails; i++) {
	  ret = omx_irecv(rails[i].ep, buffer+i*length, length, 0, 0, NULL, &rails[i].req);
	  assert(ret == OMX_SUCCESS);
	}
	for(i=0; i<nbrails; i++) {
	  omx_status_t status;
	  uint32_t result = 0;
	  ret = omx_wait(rails[i].ep, &rails[i].req, &status, &result, OMX_TIMEOUT_INFINITE);
	  assert(ret == OMX_SUCCESS);
	  assert(result);
	}
      }
      gettimeofday(&tv2, NULL);
      printf("pingpong %d rails %d iters %ld bytes => %ld us\n",
	     nbrails, ITER, length,
	     (unsigned long) (tv2.tv_sec-tv1.tv_sec)*1000000+(tv2.tv_usec-tv1.tv_usec));
      usleep(100000);
    }

  } else {
    /* receiver */

    printf("Starting receiver with local addresses ");
    for(i=0; i<nbrails; i++)
      printf("%s:%d%s", rails[i].local_name, rails[i].local_eid, i==nbrails-1?"":",");
    printf("\n");

    /* connection handshakes, one at a time so that the remote side is polling when the request arrives */
    for(i=0; i<nbrails; i++) {
      omx_status_t status;
      uint32_t result;
      ret = omx_irecv(rails[i].ep, buffer, 0, 0, 0, NULL, &rails[i].req);
      assert(ret == OMX_SUCCESS);
      ret = omx_wait(rails[i].ep, &rails[i].req, &status, &result, OMX_TIMEOUT_INFINITE);
      assert(ret == OMX_SUCCESS);
      assert(result);
      ret = omx_decompose_endpoint_addr(status.addr, &rails[i].remote_nicid, &rails[i].remote_eid);
      assert(ret == OMX_SUCCESS);
      ret = omx_connect(rails[i].ep, rails[i].remote_nicid, rails[i].remote_eid, 0x87654321+i, OMX_TIMEOUT_INFINITE, &rails[i].remote_addr);
      if (ret != OMX_SUCCESS) {
	fprintf(stderr, "Failed to connect back to peer #%d name %s endpoint %ld\n",
		i, rails[i].remote_name, (unsigned long) rails[i].remote_eid);
	goto out;
      }
      ret = omx_isend(rails[i].ep, buffer, 0, rails[i].remote_addr, 0, NULL, &rails[i].req);
      assert(ret == OMX_SUCCESS);
      ret = omx_wait(rails[i].ep, &rails[i].req, &status, &result, OMX_TIMEOUT_INFINITE);
      assert(ret == OMX_SUCCESS);
      assert(result);
    }

    /* actual loop */
    for(length=0; length<MAX; length?length*=2:length++) {
      int j;
      for(j=0; j<ITER; j++) {
	for(i=0; i<nbrails; i++) {
	  ret = omx_irecv(rails[i].ep, buffer+i*length, length, 0, 0, NULL, &rails[i].req);
	  assert(ret == OMX_SUCCESS);
	}
	for(i=0; i<nbrails; i++) {
	  omx_status_t status;
	  uint32_t result = 0;
	  ret = omx_wait(rails[i].ep, &rails[i].req, &status, &result, OMX_TIMEOUT_INFINITE);
	  assert(ret == OMX_SUCCESS);
	  assert(result);
	}
	for(i=0; i<nbrails; i++) {
	  ret = omx_isend(rails[i].ep, buffer+i*length, length, rails[i].remote_addr, 0, NULL, &rails[i].req);
	  assert(ret == OMX_SUCCESS);
	}
	for(i=0; i<nbrails; i++) {
	  omx_status_t status;
	  uint32_t result = 0;
	  ret = omx_wait(rails[i].ep, &rails[i].req, &status, &result, OMX_TIMEOUT_INFINITE);
	  assert(ret == OMX_SUCCESS);
	  assert(result);
	}
      }
    }
  }

  free(dest_hostname);
  return 0;

 out:
  free(dest_hostname);
  return -1;
}
