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

#include "mpoe_lib.h"

#define BID 0
#define EID 0
#define RID 0
#define ITER 100

static void
usage(void)
{
  fprintf(stderr, "Common options:\n");
  fprintf(stderr, " -b <n>\tchange local board id [%d]\n", BID);
  fprintf(stderr, " -e <n>\tchange local endpoint id [%d]\n", EID);
  fprintf(stderr, " -v\tverbose\n");
  fprintf(stderr, "Sender options:\n");
  fprintf(stderr, " -d <mac>\tset remote board mac address and switch to sender mode\n");
  fprintf(stderr, " -r <n>\tchange remote endpoint id [%d]\n", RID);
  fprintf(stderr, " -N <n>\tchange number of iterations [%d]\n", ITER);
}

struct param {
  uint32_t iter;
};

int main(int argc, char *argv[])
{
  struct mpoe_endpoint * ep;
  mpoe_return_t ret;
  char c;

  int bid = BID;
  int eid = EID;
  int rid = RID;
  int iter = ITER;
  struct mpoe_mac_addr dest;
  int sender = 0;
  int verbose = 0;

  while ((c = getopt(argc, argv, "e:r:d:b:N:v")) != EOF)
    switch (c) {
    case 'b':
      bid = atoi(optarg);
      break;
    case 'e':
      eid = atoi(optarg);
      break;
    case 'd':
      mpoe_mac_addr_sscanf(optarg, &dest);
      sender = 1;
      break;
    case 'r':
      rid = atoi(optarg);
      break;
    case 'N':
      iter = atoi(optarg);
      break;
    case 'v':
      verbose = 1;
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
      usage();
      exit(-1);
      break;
    }

  ret = mpoe_open_endpoint(bid, eid, &ep);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    mpoe_strerror(ret));
    goto out;
  }

  if (sender) {
    /* sender */

    union mpoe_request * req;
    struct mpoe_status status;
    struct param param;
    char dest_str[MPOE_MAC_ADDR_STRLEN];
    int i;

    mpoe_mac_addr_sprintf(dest_str, &dest);
    printf("Starting sender to %s...\n", dest_str);

    /* sending the param message */
    param.iter = iter;
    ret = mpoe_isend(ep, &param, sizeof(param),
		     0x1234567887654321ULL, &dest, rid,
		     NULL, &req);
    if (ret != MPOE_SUCCESS) {
      fprintf(stderr, "Failed to isend (%s)\n",
	      mpoe_strerror(ret));
      goto out_with_ep;
    }
    ret = mpoe_wait(ep, &req, &status);
    if (ret != MPOE_SUCCESS) {
      fprintf(stderr, "Failed to wait (%s)\n",
	      mpoe_strerror(ret));
      goto out_with_ep;
    }

    printf("Sent parameters (iter=%d)\n", iter);

    for(i=0; i<iter; i++) {
      if (verbose)
	printf("Iteration %d/%d\n", i, iter);

      /* wait for an incoming message */
      ret = mpoe_irecv(ep, NULL, 0,
		       0, 0,
		       NULL, &req);
      if (ret != MPOE_SUCCESS) {
	fprintf(stderr, "Failed to irecv (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }
      ret = mpoe_wait(ep, &req, &status);
      if (ret != MPOE_SUCCESS) {
	fprintf(stderr, "Failed to wait (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }

      /* sending the param message */
      ret = mpoe_isend(ep, NULL, 0,
		       0x1234567887654321ULL, &dest, rid,
		       NULL, &req);
      if (ret != MPOE_SUCCESS) {
	fprintf(stderr, "Failed to isend (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }
      ret = mpoe_wait(ep, &req, &status);
      if (ret != MPOE_SUCCESS) {
	fprintf(stderr, "Failed to wait (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }
    }
    if (verbose)
      printf("Iteration %d/%d\n", i, iter);

  } else {
    /* receiver */

    union mpoe_request * req;
    struct mpoe_status status;
    struct param param;
    struct timeval tv1, tv2;
    unsigned long long us;
    int i;

    printf("Starting receiver...\n");

    printf("Waiting for parameters...\n");

    /* wait for theparam  message */
    ret = mpoe_irecv(ep, &param, sizeof(param),
		     0, 0,
		     NULL, &req);
    if (ret != MPOE_SUCCESS) {
      fprintf(stderr, "Failed to irecv (%s)\n",
	      mpoe_strerror(ret));
      goto out_with_ep;
    }
    ret = mpoe_wait(ep, &req, &status);
    if (ret != MPOE_SUCCESS) {
      fprintf(stderr, "Failed to wait (%s)\n",
	      mpoe_strerror(ret));
      goto out_with_ep;
    }

    /* retrieve parameters */
    iter = param.iter;

    printf("Got parameters (iter=%d)\n", iter);

    gettimeofday(&tv1, NULL);

    for(i=0; i<iter; i++) {
      if (verbose)
	printf("Iteration %d/%d\n", i, iter);

      /* sending the param message */
      ret = mpoe_isend(ep, NULL, 0,
		       0x1234567887654321ULL, &status.mac, status.ep,
		       NULL, &req);
      if (ret != MPOE_SUCCESS) {
	fprintf(stderr, "Failed to isend (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }
      ret = mpoe_wait(ep, &req, &status);
      if (ret != MPOE_SUCCESS) {
	fprintf(stderr, "Failed to wait (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }

      /* wait for an incoming message */
      ret = mpoe_irecv(ep, NULL, 0,
		       0, 0,
		       NULL, &req);
      if (ret != MPOE_SUCCESS) {
	fprintf(stderr, "Failed to irecv (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }
      ret = mpoe_wait(ep, &req, &status);
      if (ret != MPOE_SUCCESS) {
	fprintf(stderr, "Failed to wait (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }

    }
    if (verbose)
      printf("Iteration %d/%d\n", i, iter);

    gettimeofday(&tv2, NULL);
    us = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
    printf("Total Duration: %lld us\n", us);
    printf("Latency: %f us\n", ((float) us)/2./iter);
  }

  return 0;

 out_with_ep:
  mpoe_close_endpoint(ep);
 out:
  return -1;
}
