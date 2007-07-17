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
#define ITER 1000
#define WARMUP 10
#define MIN 0
#define MAX 129
#define MULTIPLIER 2
#define INCREMENT 0

char buffer[MAX];

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
  fprintf(stderr, " -W <n>\tchange number of warmup iterations [%d]\n", WARMUP);
}

struct param {
  uint32_t iter;
  uint32_t warmup;
  uint32_t length;
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
  int warmup = WARMUP;
  int min = MIN;
  int max = MAX;
  int multiplier = MULTIPLIER;
  int increment = INCREMENT;
  struct mpoe_mac_addr dest;
  int sender = 0;
  int verbose = 0;

  while ((c = getopt(argc, argv, "e:r:d:b:N:W:v")) != EOF)
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
    case 'W':
      warmup = atoi(optarg);
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
    uint32_t result;
    struct param param;
    char dest_str[MPOE_MAC_ADDR_STRLEN];
    int length;
    int i;

    mpoe_mac_addr_sprintf(dest_str, &dest);
    printf("Starting sender to %s...\n", dest_str);

    for(length = min;
	length < max;
	length = next_length(length, multiplier, increment)) {

      /* send the param message */
      param.iter = iter;
      param.warmup = warmup;
      param.length = length;
      ret = mpoe_isend(ep, &param, sizeof(param),
		       0x1234567887654321ULL, &dest, rid,
		       NULL, &req);
      if (ret != MPOE_SUCCESS) {
	fprintf(stderr, "Failed to isend (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }
      ret = mpoe_wait(ep, &req, &status, &result);
      if (ret != MPOE_SUCCESS || !result) {
	fprintf(stderr, "Failed to wait (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }

      if (verbose)
	printf("Sent parameters (iter=%d, warmup=%d, length=%d)\n", iter, warmup, length);

      for(i=0; i<iter+warmup; i++) {
	if (verbose)
	  printf("Iteration %d/%d\n", i-warmup, iter);

	/* wait for an incoming message */
	ret = mpoe_irecv(ep, buffer, length,
			 0, 0,
			 NULL, &req);
	if (ret != MPOE_SUCCESS) {
	  fprintf(stderr, "Failed to irecv (%s)\n",
		  mpoe_strerror(ret));
	  goto out_with_ep;
	}
	ret = mpoe_wait(ep, &req, &status, &result);
	if (ret != MPOE_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  mpoe_strerror(ret));
	  goto out_with_ep;
	}

	/* sending a message */
	ret = mpoe_isend(ep, buffer, length,
			 0x1234567887654321ULL, &dest, rid,
			 NULL, &req);
	if (ret != MPOE_SUCCESS) {
	  fprintf(stderr, "Failed to isend (%s)\n",
		  mpoe_strerror(ret));
	  goto out_with_ep;
	}
	ret = mpoe_wait(ep, &req, &status, &result);
	if (ret != MPOE_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  mpoe_strerror(ret));
	  goto out_with_ep;
	}
      }
      if (verbose)
	printf("Iteration %d/%d\n", i-warmup, iter);
    }

  } else {
    /* receiver */

    union mpoe_request * req;
    struct mpoe_status status;
    uint32_t result;
    struct param param;
    struct timeval tv1, tv2;
    unsigned long long us;
    int length;
    int i;

    printf("Starting receiver...\n");

    while (1) {
      if (verbose)
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
      ret = mpoe_wait(ep, &req, &status, &result);
      if (ret != MPOE_SUCCESS || !result) {
	fprintf(stderr, "Failed to wait (%s)\n",
		mpoe_strerror(ret));
	goto out_with_ep;
      }

      /* retrieve parameters */
      iter = param.iter;
      warmup = param.warmup;
      length = param.length;

      if (verbose)
	printf("Got parameters (iter=%d, warmup=%d, length=%d)\n", iter, warmup, length);

      for(i=0; i<iter+warmup; i++) {
	if (verbose)
	  printf("Iteration %d/%d\n", i-warmup, iter);

	if (i == warmup)
	  gettimeofday(&tv1, NULL);

	/* sending a message */
	ret = mpoe_isend(ep, buffer, length,
			 0x1234567887654321ULL, &status.mac, status.ep,
			 NULL, &req);
	if (ret != MPOE_SUCCESS) {
	  fprintf(stderr, "Failed to isend (%s)\n",
		  mpoe_strerror(ret));
	  goto out_with_ep;
	}
	ret = mpoe_wait(ep, &req, &status, &result);
	if (ret != MPOE_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  mpoe_strerror(ret));
	  goto out_with_ep;
	}

	/* wait for an incoming message */
	ret = mpoe_irecv(ep, buffer, length,
			 0, 0,
		       NULL, &req);
	if (ret != MPOE_SUCCESS) {
	  fprintf(stderr, "Failed to irecv (%s)\n",
		  mpoe_strerror(ret));
	  goto out_with_ep;
	}
	ret = mpoe_wait(ep, &req, &status, &result);
	if (ret != MPOE_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  mpoe_strerror(ret));
	  goto out_with_ep;
	}

      }
      if (verbose)
	printf("Iteration %d/%d\n", i-warmup, iter);

      gettimeofday(&tv2, NULL);
      us = (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec);
      if (verbose)
	printf("Total Duration: %lld us\n", us);
      printf("length % 9d: %f us\n", length, ((float) us)/2./iter);
    }
  }

  return 0;

 out_with_ep:
  mpoe_close_endpoint(ep);
 out:
  return -1;
}
