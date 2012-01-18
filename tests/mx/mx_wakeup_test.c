/*************************************************************************
 * The contents of this file are subject to the MYRICOM MYRINET          *
 * EXPRESS (MX) NETWORKING SOFTWARE AND DOCUMENTATION LICENSE (the       *
 * "License"); User may not use this file except in compliance with the  *
 * License.  The full text of the License can found in LICENSE.TXT       *
 *                                                                       *
 * Software distributed under the License is distributed on an "AS IS"   *
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See  *
 * the License for the specific language governing rights and            *
 * limitations under the License.                                        *
 *                                                                       *
 * Copyright 2003 - 2004 by Myricom, Inc.  All rights reserved.          *
 *************************************************************************/

/* This file was originally contributed by
 * Brice.Goglin@ens-lyon.org (LIP/inria/ENS-Lyon) */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include "mx_auto_config.h"
#include "myriexpress.h"
#include "mx_extensions.h"
#include "test_common.h"

#define DFLT_EID   1
#define FILTER     0x12345
mx_endpoint_t ep;
mx_endpoint_addr_t addr;
sem_t launched, terminated;

void * send1_func(void *dummy)
{
  mx_return_t ret;
  mx_status_t status;
  uint32_t result;
  mx_request_t req;
  mx_segment_t seg = {NULL, 0};
  
  ret = mx_issend(ep, &seg, 1, addr, 0, NULL, &req);
  insist(ret == MX_SUCCESS);

  sem_post(&launched);
  printf("send1 launched\n");

  ret = mx_wait(ep, &req, MX_INFINITE, &status, &result);
  insist(ret == MX_SUCCESS);
  insist(!result);
  printf("send1 woke up\n");

  sem_post(&terminated);
  printf("send1 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

void * send2_func(void *dummy)
{
  mx_return_t ret;
  mx_status_t status;
  uint32_t result;
  mx_request_t req;
  mx_segment_t seg = {NULL, 0};
  
  ret = mx_issend(ep, &seg, 1, addr, 0, NULL, &req);
  insist(ret == MX_SUCCESS);

  sem_post(&launched);
  printf("send2 launched\n");

  ret = mx_wait(ep, &req, MX_INFINITE, &status, &result);
  insist(ret == MX_SUCCESS);
  insist(!result);
  printf("send2 woke up\n");

  sem_post(&terminated);
  printf("send2 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

void * recv1_func(void *dummy)
{
  mx_return_t ret;
  mx_status_t status;
  uint32_t result;
  mx_request_t req;
  mx_segment_t seg = {NULL, 0};
  
  ret = mx_irecv(ep, &seg, 1, 1, MX_MATCH_MASK_NONE, NULL, &req);
  insist(ret == MX_SUCCESS);

  sem_post(&launched);
  printf("recv1 launched\n");

  mx_wait(ep, &req, MX_INFINITE, &status, &result);
  insist(ret == MX_SUCCESS);
  insist(!result);
  ret = printf("recv1 woke up\n");

  sem_post(&terminated);
  printf("recv1 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

void * recv2_func(void *dummy)
{
  mx_return_t ret;
  mx_status_t status;
  uint32_t result;
  mx_request_t req;
  mx_segment_t seg = {NULL, 0};
  
  ret = mx_irecv(ep, &seg, 1, 1, MX_MATCH_MASK_NONE, NULL, &req);
  insist(ret == MX_SUCCESS);

  sem_post(&launched);
  printf("recv2 launched\n");

  mx_wait(ep, &req, MX_INFINITE, &status, &result);
  insist(ret == MX_SUCCESS);
  insist(!result);
  ret = printf("recv2 woke up\n");

  sem_post(&terminated);
  printf("recv2 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

void * probe1_func(void *dummy)
{
  mx_return_t ret;
  mx_status_t status;
  uint32_t result;
  
  sem_post(&launched);
  printf("probe1 launched\n");

  ret = mx_probe(ep, MX_INFINITE, 2, 2, &status, &result);
  insist(ret == MX_SUCCESS);
  insist(!result);
  printf("probe1 woke up\n");

  sem_post(&terminated);
  printf("probe1 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

void * probe2_func(void *dummy)
{
  mx_return_t ret;
  mx_status_t status;
  uint32_t result;
  
  sem_post(&launched);
  printf("probe2 launched\n");

  ret = mx_probe(ep, MX_INFINITE, 2, 2, &status, &result);
  insist(ret == MX_SUCCESS);
  insist(!result);
  printf("probe2 woke up\n");

  sem_post(&terminated);
  printf("probe2 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

void * peek1_func(void *dummy)
{
  mx_return_t ret;
  uint32_t result;
  mx_request_t any;
    
  sem_post(&launched);
  printf("peek1 launched\n");

  ret = mx_peek(ep, MX_INFINITE, &any, &result);
  insist(ret == MX_SUCCESS);
  if (result)
    printf("peek1 got a iconnect ?\n");
  else
    printf("peek1 woke up\n");

  sem_post(&terminated);
  printf("peek1 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

void * peek2_func(void *dummy)
{
  mx_return_t ret;
  uint32_t result;
  mx_request_t any;
  
  sem_post(&launched);
  printf("peek2 launched\n");

  ret = mx_peek(ep, MX_INFINITE, &any, &result);
  insist(ret == MX_SUCCESS);
  if (result)
    printf("peek2 got a iconnect ?\n");
  else
    printf("peek2 woke up\n");

  sem_post(&terminated);
  printf("peek2 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

uint32_t filter;
uint16_t his_eid;
uint64_t his_nic_id;

void * iconnect1_func(void *dummy)
{
  mx_return_t ret;
  mx_status_t status;
  uint32_t result;
  mx_request_t req;
  
  ret = mx_iconnect(ep, his_nic_id, his_eid, filter, 0, NULL, &req);
  insist(ret == MX_SUCCESS);

  sem_post(&launched);
  printf("iconnect1 launched\n");

  ret = mx_wait(ep, &req, MX_INFINITE, &status, &result);
  insist(ret == MX_SUCCESS);
  if (result)
    printf("iconnect1 completed (%s)\n", mx_strstatus(status.code));
  else
    printf("iconnect1 woke up\n");

  sem_post(&terminated);
  printf("iconnect1 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

void * iconnect2_func(void *dummy)
{
  mx_return_t ret;
  mx_status_t status;
  uint32_t result;
  mx_request_t req;
  
  ret = mx_iconnect(ep, his_nic_id, his_eid, filter, 0, NULL, &req);
  insist(ret == MX_SUCCESS);

  sem_post(&launched);
  printf("iconnect2 launched\n");

  ret = mx_wait(ep, &req, MX_INFINITE, &status, &result);
  insist(ret == MX_SUCCESS);
  if (result)
    printf("iconnect2 completed (%s)\n", mx_strstatus(status.code));
  else
    printf("iconnect2 woke up\n");

  sem_post(&terminated);
  printf("iconnect2 terminated\n");
  
  pthread_exit(NULL);
  return NULL;
}

void * connect1_func(void *dummy)
{
  mx_return_t ret;
  mx_endpoint_addr_t his_addr;
  
  sem_post(&launched);
  printf("connect1 launched\n");

  ret = mx_connect(ep, his_nic_id, his_eid, filter, MX_INFINITE, &his_addr);
  insist(ret == MX_TIMEOUT || ret == MX_SUCCESS);

  sem_post(&terminated);
  printf("connect1 terminated (%s)\n", mx_strerror(ret));
  
  pthread_exit(NULL);
  return NULL;
}

void * connect2_func(void *dummy)
{
  mx_return_t ret;
  mx_endpoint_addr_t his_addr;
  
  sem_post(&launched);
  printf("connect2 launched\n");

  ret = mx_connect(ep, his_nic_id, his_eid, filter, MX_INFINITE, &his_addr);
  insist(ret == MX_TIMEOUT || ret == MX_SUCCESS);

  sem_post(&terminated);
  printf("connect2 terminated (%s)\n", mx_strerror(ret));
  
  pthread_exit(NULL);
  return NULL;
}

void
usage()
{
  fprintf(stderr, "Usage: mx_wakeup_test [args]\n");
  fprintf(stderr, "-n nic_id - local NIC ID [MX_ANY_NIC]\n");
  fprintf(stderr, "-b board_id - local Board ID [MX_ANY_NIC]\n");
  fprintf(stderr, "-e local_eid - local endpoint ID [%d]\n", DFLT_EID);
  fprintf(stderr, "-r remote_eid - remote endpoint ID [%d]\n", DFLT_EID);
  fprintf(stderr, "-d hostname - destination hostname, required for sender\n");
  fprintf(stderr, "-h - help\n");
}

int main(int argc, char * argv[]) {
  mx_return_t ret;
  pthread_t send1_th;
  pthread_t send2_th;
  pthread_t recv1_th;
  pthread_t recv2_th;
  pthread_t probe1_th;
  pthread_t probe2_th;
  pthread_t peek1_th;
  pthread_t peek2_th;
  pthread_t iconnect1_th;
  pthread_t iconnect2_th;
  pthread_t connect1_th;
  pthread_t connect2_th;
  int i;
  int n = 0;
  uint64_t nic_id;
  uint16_t my_eid;
  uint32_t board_id;
  char *rem_host;
  int c;
  extern char *optarg;

  /* set up defaults */
  rem_host = NULL;
  filter = FILTER;
  my_eid = DFLT_EID;
  his_eid = DFLT_EID;
  board_id = MX_ANY_NIC;

  sem_init(&launched, 0, 0);
  sem_init(&terminated, 0, 0);

  ret = mx_init();
  insist(ret == MX_SUCCESS);

  while ((c = getopt(argc, argv, "hd:e:n:b:r:f:w")) != EOF) switch(c) {
  case 'd':
    rem_host = optarg;
    break;
  case 'e':
    my_eid = atoi(optarg);
    break;
  case 'f':
    filter = atoi(optarg);
    break;
  case 'n':
    sscanf(optarg, "%"SCNx64, &nic_id);
    ret = mx_nic_id_to_board_number(nic_id, &board_id);
    if (ret != MX_SUCCESS) {
      fprintf(stderr, "nic_id %012"PRIx64" can't be found\n", nic_id);
      exit(1);
    }
  case 'b':
    board_id = atoi(optarg);
    break;
  case 'r':
    his_eid = atoi(optarg);
    break;
  case 'h':
  default:
    usage();
    exit(1);
  }

  ret = mx_open_endpoint(board_id, my_eid, filter, NULL, 0, &ep);
  insist(ret == MX_SUCCESS);
  mx_set_error_handler(MX_ERRORS_RETURN);
  
  /* If no host, we are receiver */
  if (rem_host == NULL) {

    printf("Starting mx_wakeup_test dummy receiver, please ^Z me to test connect on the sender's side\n");
    sleep(10000);
    exit(0);
  }

  /* get address of destination */
  ret = mx_get_endpoint_addr(ep, &addr);
  insist(ret == MX_SUCCESS);

  /* get address of destination */
  ret = mx_hostname_to_nic_id(rem_host, &his_nic_id);
  insist(ret == MX_SUCCESS);

  printf("Starting mx_wakeup_test sender to host %s\n", rem_host);

  printf("launching all\n");
  pthread_create(&send1_th, NULL, &send1_func, NULL); n++;
  pthread_create(&send2_th, NULL, &send2_func, NULL); n++;
  pthread_create(&recv1_th, NULL, &recv1_func, NULL); n++;
  pthread_create(&recv2_th, NULL, &recv2_func, NULL); n++;
  pthread_create(&probe1_th, NULL, &probe1_func, NULL); n++;
  pthread_create(&probe2_th, NULL, &probe2_func, NULL); n++;
  pthread_create(&peek1_th, NULL, &peek1_func, NULL); n++;
  pthread_create(&peek2_th, NULL, &peek2_func, NULL); n++;
  pthread_create(&iconnect1_th, NULL, &iconnect1_func, NULL); n++;
  pthread_create(&iconnect2_th, NULL, &iconnect2_func, NULL); n++;
  pthread_create(&connect1_th, NULL, &connect1_func, NULL); n++;
  pthread_create(&connect2_th, NULL, &connect2_func, NULL); n++;
  printf("launched all\n");

  for(i=0; i<n; i++)
    sem_wait(&launched);

  printf("sleeping 10 seconds...\n");
  sleep(10);

  printf("wake up!\n");
  mx_wakeup(ep);

  for(i=0; i<n; i++)
    sem_wait(&terminated);

  printf("joining\n");
  pthread_join(send1_th, NULL);
  pthread_join(send2_th, NULL);
  pthread_join(recv1_th, NULL);
  pthread_join(recv2_th, NULL);
  pthread_join(peek1_th, NULL);
  pthread_join(peek2_th, NULL);
  pthread_join(probe1_th, NULL);
  pthread_join(probe2_th, NULL);
  pthread_join(iconnect1_th, NULL);
  pthread_join(iconnect2_th, NULL);
  pthread_join(connect1_th, NULL);
  pthread_join(connect2_th, NULL);
  printf("joined all\n");

  ret = mx_close_endpoint(ep);
  insist(ret == MX_SUCCESS);
  ret = mx_finalize();
  insist(ret == MX_SUCCESS);

  return 0;
}
