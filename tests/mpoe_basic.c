#include <stdio.h>
#include <sys/time.h>

#include "mpoe_lib.h"

#define IFNAME "lo"
#define EP 3
#define ITER 10

static mpoe_return_t
send_tiny(struct mpoe_endpoint * ep, struct mpoe_mac_addr * dest_addr,
	  int i)
{
  union mpoe_request * request, * request2;
  struct mpoe_status status;
  char buffer[12], buffer2[12];
  unsigned long length;
  mpoe_return_t ret;
  uint32_t result;

  sprintf(buffer, "message %d", i);
  length = strlen(buffer) + 1;

  ret = mpoe_isend(ep, buffer, length,
		   0x1234567887654321ULL, dest_addr, EP,
		   NULL, &request);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to send a tiny message (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }
  fprintf(stderr, "Successfully sent tiny \"%s\"\n", (char*) buffer);

  ret = mpoe_wait(ep, &request, &status, &result);
  if (ret != MPOE_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  ret = mpoe_irecv(ep, buffer2, length,
		   0, 0,
		   NULL, &request);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to post a recv for a tiny message (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  ret = mpoe_peek(ep, &request2, &result);
  if (ret != MPOE_SUCCESS || !result) {
    fprintf(stderr, "Failed to peek (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }
  if (request != request2) {
    fprintf(stderr, "Peek got request %p instead of %p\n",
	    request2, request);
    return MPOE_BAD_ERROR;
  }

  ret = mpoe_test(ep, &request, &status, &result);
  if (ret != MPOE_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully received tiny with mpoe_test loop \"%s\"\n", (char*) buffer2);

  return MPOE_SUCCESS;
}

static int
send_small(struct mpoe_endpoint * ep, struct mpoe_mac_addr * dest_addr,
	   int i)
{
  union mpoe_request * request;
  struct mpoe_status status;
  char buffer[4096];
  char buffer2[4096];
  unsigned long length;
  mpoe_return_t ret;
  uint32_t result;

  sprintf(buffer, "message %d is much longer than in a tiny buffer !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", i);
  length = strlen(buffer) + 1;

  ret = mpoe_isend(ep, buffer, length,
		   0x1234567887654321ULL, dest_addr, EP,
		   NULL, &request);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to send a small message (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully sent small \"%s\"\n", (char*) buffer);

  ret = mpoe_wait(ep, &request, &status, &result);
  if (ret != MPOE_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully waited for send completion\n");

  ret = mpoe_irecv(ep, buffer2, length,
		   0, 0,
		   NULL, &request);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to post a recv for a small message (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  do {
    ret = mpoe_test(ep, &request, &status, &result);
    if (ret != MPOE_SUCCESS) {
      fprintf(stderr, "Failed to wait for completion (%s)\n",
	      mpoe_strerror(ret));
      return ret;
    }
  } while (!result);

  fprintf(stderr, "Successfully received small with mpoe_test loop \"%s\"\n", (char*) buffer2);

  return MPOE_SUCCESS;
}

static int
send_medium(struct mpoe_endpoint * ep, struct mpoe_mac_addr * dest_addr,
	    int i)
{
  union mpoe_request * request, * request2;
  struct mpoe_status status;
  char buffer[8192], buffer2[8192];
  mpoe_return_t ret;
  uint32_t result;
  unsigned long length;
  int j;

  sprintf(buffer, "message %d is much longer than in a tiny buffer !", i);
  length = strlen(buffer);
  for(j=0;j<4096;j++)
    buffer[length+j] = '!';
  buffer[length+4096] = '\0';
  length = strlen(buffer) + 1;

  ret = mpoe_irecv(ep, buffer2, length,
		   0, 0,
		   NULL, &request2);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to post a recv for a medium message (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  ret = mpoe_isend(ep, buffer, length,
		   0x1234567887654321ULL, dest_addr, EP,
		   NULL, &request);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to send a medium message (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully sent medium \"%s\"\n", (char*) buffer);

  ret = mpoe_wait(ep, &request, &status, &result);
  if (ret != MPOE_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully waited for send completion\n");

  do {
    ret = mpoe_test(ep, &request2, &status, &result);
    if (ret != MPOE_SUCCESS) {
      fprintf(stderr, "Failed to wait for completion (%s)\n",
	      mpoe_strerror(ret));
      return ret;
    }
  } while (!result);

  fprintf(stderr, "Successfully received medium with mpoe_test loop \"%s\"\n", (char*) buffer2);

  return MPOE_SUCCESS;
}

int main(void)
{
  struct mpoe_endpoint * ep;
  struct mpoe_mac_addr dest_addr;
  struct timeval tv1, tv2;
  int i;
  mpoe_return_t ret;

  ret = mpoe_open_endpoint(0, EP, &ep);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    mpoe_strerror(ret));
    goto out;
  }

  fprintf(stderr, "Successfully open endpoint %d/%d\n", 0, EP);

  mpoe_mac_addr_set_bcast(&dest_addr);

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a tiny message */
    ret = send_tiny(ep, &dest_addr, i);
    if (ret != MPOE_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("tiny latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a small message */
    ret = send_small(ep, &dest_addr, i);
    if (ret != MPOE_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("small latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a medium message */
    ret = send_medium(ep, &dest_addr, i);
    if (ret != MPOE_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("medium latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  return 0;

 out_with_ep:
  mpoe_close_endpoint(ep);
 out:
  return -1;
}
