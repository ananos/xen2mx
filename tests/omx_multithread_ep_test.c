/*
 * Open-MX
 * Copyright © INRIA 2010-2011
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


#include <sys/types.h>
#include <sys/wait.h>

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "open-mx.h"

static void
usage(int argc, char *argv[])
{
    fprintf(stderr, "%s [options]\n", argv[0]);
}

#ifdef OMX_HAVE_HWLOC
#include <hwloc.h>

static hwloc_topology_t topology = NULL;

static void
topology_init(void)
{
  if (!topology) {
    hwloc_topology_init(&topology);
    hwloc_topology_load(topology);
  }
}

static unsigned
get_nbthreads(void)
{
  topology_init();
  return hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_PU);
}

static void
topology_exit(void)
{
  if (topology)
    hwloc_topology_destroy(topology);
}
#else
#define get_nbthreads() sysconf(_SC_NPROCESSORS_ONLN)
#define topology_exit() /* nothing to do */
#endif

static void *threadfunc(void *_barriers)
{
#define NREQ_LOOPS 10
#define NREQ_BASE 8
  omx_endpoint_t ep;
  omx_request_t req[NREQ_BASE << NREQ_LOOPS];
  omx_return_t ret;
  uint32_t result;
  pthread_barrier_t *barrier = _barriers;
  int i, j;

  ret = omx_open_endpoint(OMX_ANY_NIC, OMX_ANY_ENDPOINT, 0, NULL, 0, &ep);
  if (ret != OMX_SUCCESS)
    return NULL;

  /* wait for threads to be ready */
  pthread_barrier_wait(&barrier[0]);

  /* allocate many recvs, cancel them, again and again with even more recvs */
  for(j=0; j<NREQ_LOOPS; j++) {
    unsigned nreq = NREQ_BASE<<j;
    for(i=0; i<nreq; i++) {
      ret = omx_irecv(ep, NULL, 0, i, i, NULL, &req[i]);
      if (ret != OMX_SUCCESS)
        break;
    }
    i--;
    for( ; i>=0; i--)
      ret = omx_cancel(ep, &req[i], &result);
  }

  /* wait for threads to be ready */
  pthread_barrier_wait(&barrier[1]);

  /* close/reopen the endpoints many times */
#define NEP_LOOPS 16
  for(j=0; j<NEP_LOOPS; j++) {
    omx_close_endpoint (ep);
    ret = omx_open_endpoint(OMX_ANY_NIC, OMX_ANY_ENDPOINT, 0, NULL, 0, &ep);
    if (ret != OMX_SUCCESS)
      return NULL;
  }

  omx_close_endpoint (ep);
  return 0;
}

int main (int argc, char *argv[])
{
  pthread_t *th;
  pthread_barrier_t barrier[2];
  unsigned nbthreads = get_nbthreads();
  omx_return_t ret;
  int i, c;

  while ((c = getopt (argc, argv, "h")) != -1)
    switch (c) {
    default:
      fprintf (stderr, "Unknown option -%c\n", c);
    case 'h':
      usage (argc, argv);
      exit (-1);
  }

  ret = omx_init();
  if (ret != OMX_SUCCESS)
    return -1;

  th = malloc(nbthreads*sizeof(*th));

  pthread_barrier_init(&barrier[0], NULL, nbthreads);
  pthread_barrier_init(&barrier[1], NULL, nbthreads);

  for (i = 0; i < nbthreads; i++)
    pthread_create (&th[i], NULL, threadfunc, (void*) &barrier);
  for (i = 0; i < nbthreads; i++)
    pthread_join (th[i], NULL);

  pthread_barrier_destroy(&barrier[0]);
  pthread_barrier_destroy(&barrier[1]);

  omx_finalize ();
  topology_exit();
  return 0;
}
