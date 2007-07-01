#include <stdio.h>
#include <sys/time.h>

#include "mpoe_lib.h"

#define IFNAME "lo"
#define EP 3
#define ITER 10

static int
send_tiny(struct mpoe_endpoint * ep, struct mpoe_mac_addr * dest_addr,
	  int i)
{
  union mpoe_request * request;
  struct mpoe_status status;
  char buffer[12];
  int ret;

  sprintf(buffer, "message %d", i);

  ret = mpoe_isend(ep, buffer, strlen(buffer) + 1,
		   0x1234567887654321ULL, dest_addr, EP,
		   NULL, &request);
  if (ret < 0)
    return ret;
  fprintf(stderr, "Successfully sent tiny \"%s\"\n", (char*) buffer);

  ret = mpoe_wait(ep, &request, &status);
  if (ret < 0)
    return ret;

  fprintf(stderr, "Successfully waited for send completion\n");

  return 0;
}

static int
send_medium(struct mpoe_endpoint * ep, struct mpoe_mac_addr * dest_addr,
	  int i)
{
  union mpoe_request * request;
  struct mpoe_status status;
  char buffer[4096];
  int ret;

  sprintf(buffer, "message %d is much longer than in a tiny buffer !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", i);

  ret = mpoe_isend(ep, buffer, strlen(buffer) + 1,
		   0x1234567887654321ULL, dest_addr, EP,
		   NULL, &request);
  if (ret < 0)
    return ret;
  fprintf(stderr, "Successfully sent medium \"%s\"\n", (char*) buffer);

  ret = mpoe_wait(ep, &request, &status);
  if (ret < 0)
    return ret;

  fprintf(stderr, "Successfully waited for send completion\n");

  return 0;
}

int main(void)
{
  struct mpoe_endpoint * ep;
  struct mpoe_mac_addr dest_addr;
  struct timeval tv1, tv2;
  int i;
  int ret;

  ret = mpoe_open_endpoint(0, EP, &ep);
  if (ret < 0) {
    perror("open endpoint");
    goto out;
  }
  fprintf(stderr, "Successfully open endpoint %d/%d\n", 0, EP);

  mpoe_mac_addr_set_bcast(&dest_addr);

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a tiny message */
    if (send_tiny(ep, &dest_addr, i) < 0)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("tiny latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a medium message */
    if (send_medium(ep, &dest_addr, i) < 0)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("medium latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  return 0;

 out_with_ep:
  /* FIXME: close endpoint */
 out:
  return -1;
}
