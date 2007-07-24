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

#include "omx__lib.h"

#define BID 0
#define EID 0
#define RID 0
#define ITER 1000
#define WARMUP 10
#define MIN 0
#define MAX 129
#define MULTIPLIER 2
#define INCREMENT 0

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
  fprintf(stderr, " -S <n>\tchange the start length [%d]\n", MIN);
  fprintf(stderr, " -E <n>\tchange the end length [%d]\n", MAX);
  fprintf(stderr, " -M <n>\tchange the length multiplier [%d]\n", MULTIPLIER);
  fprintf(stderr, " -I <n>\tchange the length increment [%d]\n", INCREMENT);
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
  struct omx_endpoint * ep;
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
  uint64_t dest;
  int sender = 0;
  int verbose = 0;
  char * buffer;

  dest = 0; /* compiler warning */

  while ((c = getopt(argc, argv, "e:r:d:b:S:E:M:I:N:W:v")) != EOF)
    switch (c) {
    case 'b':
      bid = atoi(optarg);
      break;
    case 'e':
      eid = atoi(optarg);
      break;
    case 'd':
      omx_board_addr_sscanf(optarg, &dest);
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
    case 'v':
      verbose = 1;
      break;
    default:
      fprintf(stderr, "Unknown option -%c\n", c);
      usage();
      exit(-1);
      break;
    }

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
            omx_strerror(ret));
    goto out;
  }

  ret = omx_open_endpoint(bid, eid, &ep);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  if (sender) {
    /* sender */

    union omx_request * req;
    struct omx_status status;
    uint32_t result;
    struct param param;
    char dest_str[OMX_BOARD_ADDR_STRLEN];
    int length;
    int i;

    omx_board_addr_sprintf(dest_str, dest);
    printf("Starting sender to %s...\n", dest_str);

    for(length = min;
	length < max;
	length = next_length(length, multiplier, increment)) {

      /* send the param message */
      param.iter = iter;
      param.warmup = warmup;
      param.length = length;
      ret = omx_isend(ep, &param, sizeof(param),
		      0x1234567887654321ULL, dest, rid,
		      NULL, &req);
      if (ret != OMX_SUCCESS) {
	fprintf(stderr, "Failed to isend (%s)\n",
		omx_strerror(ret));
	goto out_with_ep;
      }
      ret = omx_wait(ep, &req, &status, &result);
      if (ret != OMX_SUCCESS || !result) {
	fprintf(stderr, "Failed to wait (%s)\n",
		omx_strerror(ret));
	goto out_with_ep;
      }

      if (verbose)
	printf("Sent parameters (iter=%d, warmup=%d, length=%d)\n", iter, warmup, length);

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
	ret = omx_wait(ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}

	/* sending a message */
	ret = omx_isend(ep, buffer, length,
			0x1234567887654321ULL, dest, rid,
			NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to isend (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_wait(ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
      }
      if (verbose)
	printf("Iteration %d/%d\n", i-warmup, iter);

      free(buffer);
    }

    /* send a param message with iter = 0 to stop the receiver */
    param.iter = 0;
    ret = omx_isend(ep, &param, sizeof(param),
		    0x1234567887654321ULL, dest, rid,
		    NULL, &req);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to isend (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }
    ret = omx_wait(ep, &req, &status, &result);
    if (ret != OMX_SUCCESS || !result) {
      fprintf(stderr, "Failed to wait (%s)\n",
	      omx_strerror(ret));
      goto out_with_ep;
    }

  } else {
    /* receiver */

    union omx_request * req;
    struct omx_status status;
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
      ret = omx_irecv(ep, &param, sizeof(param),
		      0, 0,
		      NULL, &req);
      if (ret != OMX_SUCCESS) {
	fprintf(stderr, "Failed to irecv (%s)\n",
		omx_strerror(ret));
	goto out_with_ep;
      }
      ret = omx_wait(ep, &req, &status, &result);
      if (ret != OMX_SUCCESS || !result) {
	fprintf(stderr, "Failed to wait (%s)\n",
		omx_strerror(ret));
	goto out_with_ep;
      }

      /* retrieve parameters */
      iter = param.iter;
      warmup = param.warmup;
      length = param.length;

      if (verbose)
	printf("Got parameters (iter=%d, warmup=%d, length=%d)\n", iter, warmup, length);

      if (!iter)
	/* the sender wants us to stop */
	goto out_receiver;

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
			0x1234567887654321ULL, status.board_addr, status.ep,
			NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to isend (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_wait(ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}

	/* wait for an incoming message */
	ret = omx_irecv(ep, buffer, length,
			0, 0,
			NULL, &req);
	if (ret != OMX_SUCCESS) {
	  fprintf(stderr, "Failed to irecv (%s)\n",
		  omx_strerror(ret));
	  goto out_with_ep;
	}
	ret = omx_wait(ep, &req, &status, &result);
	if (ret != OMX_SUCCESS || !result) {
	  fprintf(stderr, "Failed to wait (%s)\n",
		  omx_strerror(ret));
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
	     length, ((float) us)/2./iter, 2.*iter*length/us, 2.*iter*length/us/1.048576);

      free(buffer);
    }

  }
 out_receiver:

  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  return -1;
}
