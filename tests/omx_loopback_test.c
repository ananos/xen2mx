#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "openmx.h"

#define IFNAME "lo"
#define EP 3
#define ITER 10

static omx_return_t
send_tiny(omx_endpoint_t ep, uint64_t dest_addr,
	  int i)
{
  omx_request_t request, request2;
  omx_status_t status;
  char buffer[12], buffer2[12];
  unsigned long length;
  omx_return_t ret;
  uint32_t result;

  sprintf(buffer, "message %d", i);
  length = strlen(buffer) + 1;

  ret = omx_isend(ep, buffer, length,
		  0x1234567887654321ULL, dest_addr, EP,
		  NULL, &request);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to send a tiny message (%s)\n",
	    omx_strerror(ret));
    return ret;
  }
  fprintf(stderr, "Successfully sent tiny \"%s\"\n", (char*) buffer);

  ret = omx_wait(ep, &request, &status, &result);
  if (ret != OMX_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    omx_strerror(ret));
    return ret;
  }

  ret = omx_irecv(ep, buffer2, length,
		  0, 0,
		  NULL, &request);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to post a recv for a tiny message (%s)\n",
	    omx_strerror(ret));
    return ret;
  }

  ret = omx_peek(ep, &request2, &result);
  if (ret != OMX_SUCCESS || !result) {
    fprintf(stderr, "Failed to peek (%s)\n",
	    omx_strerror(ret));
    return ret;
  }
  if (request != request2) {
    fprintf(stderr, "Peek got request %p instead of %p\n",
	    request2, request);
    return OMX_BAD_ERROR;
  }

  ret = omx_test(ep, &request, &status, &result);
  if (ret != OMX_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    omx_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully received tiny with omx_test loop \"%s\"\n", (char*) buffer2);

  return OMX_SUCCESS;
}

static int
send_small(omx_endpoint_t ep, uint64_t dest_addr,
	   int i)
{
  omx_request_t request;
  omx_status_t status;
  char buffer[4096];
  char buffer2[4096];
  unsigned long length;
  omx_return_t ret;
  uint32_t result;

  sprintf(buffer, "message %d is much longer than in a tiny buffer !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", i);
  length = strlen(buffer) + 1;

  ret = omx_isend(ep, buffer, length,
		  0x1234567887654321ULL, dest_addr, EP,
		  NULL, &request);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to send a small message (%s)\n",
	    omx_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully sent small \"%s\"\n", (char*) buffer);

  ret = omx_wait(ep, &request, &status, &result);
  if (ret != OMX_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    omx_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully waited for send completion\n");

  ret = omx_irecv(ep, buffer2, length,
		  0, 0,
		  NULL, &request);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to post a recv for a small message (%s)\n",
	    omx_strerror(ret));
    return ret;
  }

  do {
    ret = omx_test(ep, &request, &status, &result);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to wait for completion (%s)\n",
	      omx_strerror(ret));
      return ret;
    }
  } while (!result);

  fprintf(stderr, "Successfully received small with omx_test loop \"%s\"\n", (char*) buffer2);

  return OMX_SUCCESS;
}

static int
send_medium(omx_endpoint_t ep, uint64_t dest_addr,
	    int i)
{
  omx_request_t request, request2;
  omx_status_t status;
  char buffer[8192], buffer2[8192];
  omx_return_t ret;
  uint32_t result;
  unsigned long length;
  int j;

  sprintf(buffer, "message %d is much longer than in a tiny buffer !", i);
  length = strlen(buffer);
  for(j=0;j<4096;j++)
    buffer[length+j] = '!';
  buffer[length+4096] = '\0';
  length = strlen(buffer) + 1;

  ret = omx_irecv(ep, buffer2, length,
		  0, 0,
		  NULL, &request2);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to post a recv for a medium message (%s)\n",
	    omx_strerror(ret));
    return ret;
  }

  ret = omx_isend(ep, buffer, length,
		  0x1234567887654321ULL, dest_addr, EP,
		  NULL, &request);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to send a medium message (%s)\n",
	    omx_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully sent medium \"%s\"\n", (char*) buffer);

  ret = omx_wait(ep, &request, &status, &result);
  if (ret != OMX_SUCCESS || !result) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    omx_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully waited for send completion\n");

  do {
    ret = omx_test(ep, &request2, &status, &result);
    if (ret != OMX_SUCCESS) {
      fprintf(stderr, "Failed to wait for completion (%s)\n",
	      omx_strerror(ret));
      return ret;
    }
  } while (!result);

  fprintf(stderr, "Successfully received medium with omx_test loop \"%s\"\n", (char*) buffer2);

  return OMX_SUCCESS;
}

int main(void)
{
  omx_endpoint_t ep;
  uint64_t dest_addr;
  struct timeval tv1, tv2;
  uint8_t board_index;
  int i;
  omx_return_t ret;

  ret = omx_init();
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to initialize (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  ret = omx_get_info(NULL, OMX_INFO_BOARD_INDEX_BY_NAME,
		     IFNAME, strlen(IFNAME)+1,
		     &board_index, sizeof(board_index));
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to find iface %s (%s)\n",
	    IFNAME, omx_strerror(ret));
    goto out;
  }

  ret = omx_open_endpoint(board_index, EP, &ep);
  if (ret != OMX_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    omx_strerror(ret));
    goto out;
  }

  dest_addr = -1; /* broadcast */

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a tiny message */
    ret = send_tiny(ep, dest_addr, i);
    if (ret != OMX_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("tiny latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a small message */
    ret = send_small(ep, dest_addr, i);
    if (ret != OMX_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("small latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a medium message */
    ret = send_medium(ep, dest_addr, i);
    if (ret != OMX_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("medium latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  return 0;

 out_with_ep:
  omx_close_endpoint(ep);
 out:
  return -1;
}
