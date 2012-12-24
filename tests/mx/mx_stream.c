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

#include "myriexpress.h"

#if !MX_OS_WINNT

#include <unistd.h>
#include <sys/time.h>

#include <pthread.h>
#define MX_MUTEX_T pthread_mutex_t
#define MX_MUTEX_INIT(mutex_) pthread_mutex_init(mutex_, 0)
#define MX_MUTEX_LOCK(mutex_) pthread_mutex_lock(mutex_)
#define MX_MUTEX_UNLOCK(mutex_) pthread_mutex_unlock(mutex_)

#define MX_THREAD_T pthread_t
#define MX_THREAD_CREATE(thread_, start_routine_, arg_) \
pthread_create(thread_, 0, start_routine_, arg_)
#define MX_THREAD_JOIN(thread_) pthread_join(thread, 0)

#else

#include <windows.h>
#include <process.h>
#include "getopt.h"

#define MX_MUTEX_T HANDLE
#define MX_MUTEX_INIT(mutex_) *mutex_=CreateMutex(0, FALSE, 0)
#define MX_MUTEX_LOCK(mutex_) WaitForSingleObject(*mutex_, INFINITE)
#define MX_MUTEX_UNLOCK(mutex_) ReleaseMutex(*mutex_)

#define MX_THREAD_T HANDLE
#define MX_THREAD_CREATE(thread_, start_routine_, arg_) \
do { \
  unsigned thrdaddr; \
  *thread_=(HANDLE)_beginthreadex(0, 0, start_routine_, arg_, 0, &thrdaddr); \
} while(0)
#define MX_THREAD_JOIN(thread_) WaitForSingleObject(thread_, INFINITE)

int mx_gettimeofday(struct timeval *tv, void *tz);
#define gettimeofday(x,y) mx_gettimeofday(x,y)

#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mx_byteswap.h"
#include "test_common.h"

MX_MUTEX_T stream_mutex;

#define FILTER     0x12345
#define MATCH_VAL  0xabcdef
#define DFLT_EID   1
#define DFLT_LEN   8192
#define DFLT_END   128
#define MAX_LEN    (1024*1024*1024) 
#define DFLT_ITER  1000
#define NUM_RREQ   8  /* currently constrained by  MX_MCP_RDMA_HANDLES_CNT*/
#define NUM_SREQ   8  /* currently constrained by  MX_MCP_RDMA_HANDLES_CNT*/

#define DO_HANDSHAKE 1
#define MATCH_VAL_MAIN (1 << 31)
#define MATCH_VAL_THREAD 1

int Verify;
int do_verbose;
int num_threads;
volatile int threads_running;

static void receiver_blocking(mx_endpoint_t ep, uint32_t match_val, uint32_t filter);
static void receiver_polling(mx_endpoint_t ep, uint32_t match_val, uint32_t filter);
static void sender_blocking(mx_endpoint_t ep,mx_endpoint_addr_t dest, 
			    int iter, int len, int bothways, 
			    uint32_t match_val);
static void sender_polling(mx_endpoint_t ep,mx_endpoint_addr_t dest, 
			   int iter, int len, int bothways,
			   uint32_t match_val);


void
usage()
{
	fprintf(stderr, "Usage: mx_stream [args]\n");
	fprintf(stderr, "-n nic_id - local NIC ID [MX_ANY_NIC]\n");
	fprintf(stderr, "-b board_id - local Board ID [MX_ANY_NIC]\n");
	fprintf(stderr, "-e local_eid - local endpoint ID [%d]\n", DFLT_EID);
	fprintf(stderr, "-r remote_eid - remote endpoint ID [%d]\n", DFLT_EID);
	fprintf(stderr, "-d hostname - destination hostname, required for sender\n");
	fprintf(stderr, "-f filter - endpoint filter, default %x\n", FILTER);
	fprintf(stderr, "-l length - message length, default %d\n", DFLT_LEN);
	fprintf(stderr, "-N iter - iterations, default %d\n", DFLT_ITER);
	fprintf(stderr, "-v - verbose\n");
	fprintf(stderr, "-x - bothways\n");
	fprintf(stderr, "-w - wait\n");
	fprintf(stderr, "-V - verify msg content [OFF]\n");
	fprintf(stderr, "-h - help\n");
}

struct metadata {
	uint32_t len;
	uint32_t iter;
	uint32_t usec;
	uint32_t verify;
	uint32_t bothways;
};

struct mx_thread_arg {
	mx_endpoint_t ep;
	mx_endpoint_addr_t dest;
	int iter;
	int len;
	int blocking;
};

struct bwinfo {
	double bandwidth;
	double pkts_per_sec;
};

struct bwinfo global_bwinfo;

static inline void
mx_test_or_wait(int blocking, mx_endpoint_t ep, mx_request_t *req, 
		int timo, mx_status_t *stat, uint32_t *result)
{
	if (blocking)
		mx_wait(ep, req, timo, stat, result);
	else {
		do {
			mx_test(ep, req, stat, result);
		} while(!*result);
	}
}

static inline void
mx_check_buffer(char *buffer, int len)
{
	int i;
	uint32_t *val;
	static int num_calls = 0;
	static uint32_t cookie = 0;



	num_calls++;
	
	if (len < 4)
		return;
	val = (uint32_t *)buffer;
	for (i = 0; i < len/4; i++) {
		cookie++;
		if (ntohl(val[i]) != cookie) {
			fprintf(stderr, "Verification error at byte %d of message %d\n",
				i * 4, num_calls - 1);
			fprintf(stderr, "Expected 0x%x, got 0x%x\n",
				cookie, (unsigned int)ntohl(val[i]));
			abort();
		}
	}
}

static inline void
mx_fill_buffer(char *buffer, int len)
{
	int i;
	uint32_t *val;
	static uint32_t cookie = 0;

	if (len < 4)
		return;
	val = (uint32_t *)buffer;
	for (i = 0; i < len/4; i++) {
		cookie++;
		val[i] = htonl(cookie);
	}
}

#if !MX_OS_WINNT
void *
#else
unsigned __stdcall
#endif
start_send_thread(void *arg)
{
	struct mx_thread_arg *t;

	t = (struct mx_thread_arg *)arg;
	if (t->blocking)
		sender_blocking(t->ep, t->dest, t->iter, t->len, 0, 
				MATCH_VAL_THREAD);
	else
		sender_polling(t->ep, t->dest, t->iter, t->len, 0,
			       MATCH_VAL_THREAD);
	return 0;
}

#if !MX_OS_WINNT
void *
#else
unsigned __stdcall
#endif
start_recv_thread(void *arg)
{
	struct mx_thread_arg *t;

	t = (struct mx_thread_arg *)arg;
	if (t->blocking)
		receiver_blocking(t->ep, MATCH_VAL_THREAD, ~0);
	else
		receiver_polling(t->ep, MATCH_VAL_THREAD, ~0);
	return 0;
}


static inline void
receiver(mx_endpoint_t ep, int blocking, uint32_t match_val, uint32_t filter)
{
	int count, len, iter, cur_req, num_req;
	mx_status_t stat;	
	mx_request_t req[NUM_RREQ];
	mx_request_t sreq;
	mx_segment_t seg;
	uint32_t result, usec;
	struct timeval start_time, end_time;
	double bw, pkts_per_sec;
	char *buffer;
	struct metadata info;
	int bothways;
#if MX_THREAD_SAFE
	struct mx_thread_arg args;
	MX_THREAD_T thread;
#endif
	uint64_t nic;
	uint32_t eid;

	seg.segment_ptr = &info;
	seg.segment_length = sizeof(info);
	mx_irecv(ep, &seg, 1, match_val, MX_MATCH_MASK_NONE, 0, &req[0]);
	/* wait for the receive to complete */
	mx_test_or_wait(blocking, ep, &req[0], MX_INFINITE, &stat, &result);
	if (!result) {
		fprintf(stderr, "mx_wait failed\n");
		exit(1);
	}
	if (stat.code != MX_STATUS_SUCCESS) {
		fprintf(stderr, "irecv failed with status %s\n", mx_strstatus(stat.code));
		exit(1);
	}
	if (filter != ~0) {
		/* filter == ~0 means recv threads on master */
		mx_decompose_endpoint_addr(stat.source, &nic, &eid);
		mx_connect(ep, nic, eid, filter, MX_INFINITE, &stat.source);
	}
	len = ntohl(info.len);
	iter = ntohl(info.iter);
	Verify = ntohl(info.verify);
	bothways = ntohl(info.bothways);
	if (do_verbose)
		printf("Starting test: len = %d, iter = %d\n", len, iter);
	if (do_verbose && Verify) {
		printf("Verifying results\n");
	}
	buffer = malloc(len * NUM_RREQ);
	if (buffer == NULL) {
		fprintf(stderr, "Can't allocate buffers\n");
		exit(1);
	}

	if (bothways) {
#if MX_THREAD_SAFE
		args.ep = ep;
		args.dest = stat.source;
		args.iter = iter;
		args.len = len;
		args.blocking = blocking;
		num_threads++;
		MX_THREAD_CREATE(&thread, &start_send_thread, &args);
#else
		fprintf(stderr,"bothways not supported\n");
		exit(1);
#endif
	}


	/* pre-post our receives */
	num_req = NUM_RREQ;
	if (num_req > iter)
		num_req = iter;
	for (cur_req = 0; cur_req < num_req; cur_req++) {
		seg.segment_ptr = &buffer[cur_req * len];
		seg.segment_length = len;
		mx_irecv(ep, &seg, 1, match_val, MX_MATCH_MASK_NONE, 0, 
			 &req[cur_req]);
	}

	MX_MUTEX_LOCK(&stream_mutex);
	++threads_running;
	MX_MUTEX_UNLOCK(&stream_mutex);
	while(threads_running != num_threads)
		/* spin */;

#if DO_HANDSHAKE
	/* post a send to let the sender know we are ready */
	seg.segment_ptr = &info;
	seg.segment_length = sizeof(info);
	sreq = 0;
	mx_isend(ep, &seg, 1, stat.source, match_val, NULL, &sreq);
	mx_test_or_wait(blocking, ep, &sreq, MX_INFINITE, &stat, &result);
	if (!result) {
		fprintf(stderr, "mx_wait failed\n");
		exit(1);
	}
	if (stat.code != MX_STATUS_SUCCESS) {
		fprintf(stderr, "isend failed with status %s\n", mx_strstatus(stat.code));
		exit(1);
	}
#endif
	/* start the test */
	gettimeofday(&start_time, NULL);
	for (count = 0; count < iter; count++) {
		/* wait for the receive to complete */
		cur_req = count & (NUM_RREQ - 1);
		
		mx_test_or_wait(blocking, ep, &req[cur_req], 
				MX_INFINITE, &stat, &result);
		if (!result) {
			fprintf(stderr, "mx_wait failed\n");
			exit(1);
		}
		if (stat.code != MX_STATUS_SUCCESS) {
			fprintf(stderr, "irecv failed with status %s\n", mx_strstatus(stat.code));
			exit(1);
		}
		if (stat.xfer_length != len) {
			fprintf(stderr, "bad len %d != %d\n", stat.xfer_length, len);
			exit(1);
		}
		/* hack since mx_cancel does not work */
		if ((count + NUM_RREQ) > iter)
			continue;
		
		seg.segment_ptr = &buffer[cur_req * len];
		seg.segment_length = len;
		if (Verify)
			mx_check_buffer(seg.segment_ptr, len);
		mx_irecv(ep, &seg, 1, match_val, MX_MATCH_MASK_NONE, 0, 
			      &req[cur_req]);
	}
	gettimeofday(&end_time, NULL);
	usec = end_time.tv_usec - start_time.tv_usec;
	usec += (end_time.tv_sec - start_time.tv_sec) * 1000000;
	bw =  ((double)iter * (double)len) / (double) usec;
	pkts_per_sec = iter / ((double) usec / 1000000.0);
	global_bwinfo.bandwidth = bw;
	global_bwinfo.pkts_per_sec = pkts_per_sec;
	/* printf("%8d    %5.3f    %5.3f\n", len, bw, pkts_per_sec);*/
#if 0 /* mx_cancel assert(0)'s */
	for (cur_req = 0; cur_req < num_req; cur_req++) {
		mx_cancel(ep, &req[cur_req]);
	}
#endif

	info.usec = htonl(usec);
	seg.segment_ptr = &info;
	seg.segment_length = sizeof(info);
	sreq = 0;
	mx_isend(ep, &seg, 1, stat.source, match_val, NULL, &sreq);
	mx_test_or_wait(blocking, ep, &sreq, MX_INFINITE, &stat, &result);
	free(buffer);
#if MX_THREAD_SAFE
	if(bothways)
		MX_THREAD_JOIN(thread);
#endif
}

static inline void
sender(mx_endpoint_t ep,mx_endpoint_addr_t dest, int iter, int len, 
       int blocking, int bothways, uint32_t match_val)
{
	int count, cur_req, num_req;
	mx_status_t stat;	
	mx_request_t req[NUM_SREQ];
	mx_segment_t seg;
	uint32_t result;
	struct metadata info;
	char *buffer;
	uint32_t usec;
	double bw, pkts_per_sec;
#if MX_THREAD_SAFE
	MX_THREAD_T thread;
	struct mx_thread_arg args;
#endif
	struct bwinfo *bwinfo = NULL;

	buffer = malloc(len * NUM_SREQ);
	if (buffer == NULL) {
		fprintf(stderr, "Can't allocate buffers\n");
		exit(1);
	}

	seg.segment_ptr = &info;
	seg.segment_length = sizeof(info);
	info.len = htonl(len);
	info.iter = htonl(iter);
	info.verify = htonl(Verify);
	info.bothways = htonl(bothways);

	if (bothways) {
#if MX_THREAD_SAFE
		args.ep = ep;
		args.dest = dest;
		args.iter = iter;
		args.len = len;
		args.blocking = blocking;
		MX_THREAD_CREATE(&thread, &start_recv_thread, &args);
#else
		fprintf(stderr, "bothways not supported\n");
		exit(1);
#endif
	}

	mx_isend(ep, &seg, 1, dest, match_val, NULL, &req[0]);
	/* wait for the send to complete */
	mx_test_or_wait(blocking, ep, &req[0], MX_INFINITE, &stat, &result);
	if (!result) {
		fprintf(stderr, "mx_wait failed\n");
		exit(1);
	}
	if (stat.code != MX_STATUS_SUCCESS) {
		fprintf(stderr, "isend failed with status %s\n", mx_strstatus(stat.code));
		exit(1);
	}

	MX_MUTEX_LOCK(&stream_mutex);
	++threads_running;
	MX_MUTEX_UNLOCK(&stream_mutex);
	while(threads_running != num_threads)
		/* spin */;

#if DO_HANDSHAKE
	/* wait for the receiver to get ready */
	seg.segment_ptr = &info;
	seg.segment_length = sizeof(info);
	mx_irecv(ep, &seg, 1, match_val, MX_MATCH_MASK_NONE, 0, &req[0]);
	/* wait for the receive to complete */
	mx_test_or_wait(blocking, ep, &req[0], MX_INFINITE, &stat, &result);
	if (!result) {
		fprintf(stderr, "mx_wait failed\n");
		exit(1);
	}
	if (stat.code != MX_STATUS_SUCCESS) {
		fprintf(stderr, "irecv failed with status %s\n", mx_strstatus(stat.code));
		exit(1);
	}
#endif
	num_req = NUM_SREQ;
	if (num_req > iter)
		num_req = iter;
	for (cur_req = 0; cur_req < num_req; cur_req++) {
		seg.segment_ptr = &buffer[cur_req * len];
		seg.segment_length = len;
		if (Verify)
			mx_fill_buffer(seg.segment_ptr, len);
		mx_isend(ep, &seg, 1, dest, match_val, NULL, 
			      &req[cur_req]);
	}

	for (count = 0; count < iter; count++) {
		/* wait for the send to complete */
		cur_req = count & (NUM_SREQ - 1);
		mx_test_or_wait(blocking, ep, &req[cur_req], 
				MX_INFINITE, &stat, &result);
		if (!result) {
			fprintf(stderr, "mx_wait failed\n");
			exit(1);
		}
		if (stat.code != MX_STATUS_SUCCESS) {
			fprintf(stderr, "isend failed with status %s\n", mx_strstatus(stat.code));
			exit(1);
		}

		/* hack since mx_cancel does not work */
		if ((count + NUM_SREQ) >= iter)
			continue;

		seg.segment_ptr = &buffer[cur_req * len];
		seg.segment_length = len;
		if (Verify)
			mx_fill_buffer(seg.segment_ptr, len);
		mx_isend(ep, &seg, 1, dest, match_val, NULL, &req[cur_req]);
	}
	
	seg.segment_ptr = &info;
	seg.segment_length = sizeof(info);
	mx_irecv(ep, &seg, 1, match_val, MX_MATCH_MASK_NONE, 0, &req[0]);
	/* wait for the receive to complete */
	mx_test_or_wait(blocking, ep, &req[0], MX_INFINITE, &stat, &result);
	if (!result) {
		fprintf(stderr, "mx_wait failed\n");
		exit(1);
	}
	if (stat.code != MX_STATUS_SUCCESS) {
		fprintf(stderr, "irecv failed with status %s\n", mx_strstatus(stat.code));
		exit(1);
	}
	usec = ntohl(info.usec);
	bw =  ((double)iter * (double)len) / (double) usec;
	pkts_per_sec = iter / ((double) usec / 1000000.0);
	if (match_val == MATCH_VAL_THREAD)
		return;
	if (match_val == MATCH_VAL_MAIN && bothways)
#if MX_THREAD_SAFE
	if(bothways) {
		printf("Send:  %8d    %5.3f    %5.3f\n", 
		       len, bw, pkts_per_sec);
		MX_THREAD_JOIN(thread);
		bwinfo = &global_bwinfo;
	}
#endif
	if (bwinfo) {
		printf("Recv:  %8d    %5.3f    %5.3f\n", 
		       len, bwinfo->bandwidth, bwinfo->pkts_per_sec);
		bw += bwinfo->bandwidth;
		pkts_per_sec += bwinfo->pkts_per_sec;
	}
	printf("Total: %8d    %5.3f    %5.3f\n", len, bw, pkts_per_sec);
}

static void
receiver_blocking(mx_endpoint_t ep, uint32_t match_val, uint32_t filter)
{
	receiver(ep, 1, match_val, filter);
}
static void
receiver_polling(mx_endpoint_t ep, uint32_t match_val, uint32_t filter)
{
	receiver(ep, 0, match_val, filter);
}

static void
sender_blocking(mx_endpoint_t ep,mx_endpoint_addr_t dest, int iter, 
		int len, int bothways, uint32_t match_val)
{
	sender(ep, dest, iter, len, 1, bothways, match_val);
}

static void
sender_polling(mx_endpoint_t ep,mx_endpoint_addr_t dest, int iter, 
	       int len, int bothways, uint32_t match_val)
{
	sender(ep, dest, iter, len, 0, bothways, match_val);
}

int 
main(int argc, char **argv)
{
	mx_endpoint_t ep;
	uint64_t nic_id;
	uint16_t my_eid;
	uint64_t his_nic_id;
	uint32_t board_id;
	uint32_t filter;
	uint16_t his_eid;
	mx_endpoint_addr_t his_addr;
	char *rem_host;
	int len;
	int iter;
	int c;
	int do_wait;
	int do_bothways;
	extern char *optarg;
	mx_return_t ret;

#if DEBUG
	extern int mx_debug_mask;
	mx_debug_mask = 0xFFF;
#endif

	mx_init();
	MX_MUTEX_INIT(&stream_mutex);
	/* set up defaults */
	rem_host = NULL;
	filter = FILTER;
	my_eid = DFLT_EID;
	his_eid = DFLT_EID;
	board_id = MX_ANY_NIC;
	len = DFLT_LEN;
	iter = DFLT_ITER;
	do_wait = 0;
	do_bothways = 0;
	num_threads = 1;

	while ((c = getopt(argc, argv, "hd:e:f:n:b:r:l:N:Vvwx")) != EOF) switch(c) {
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
		mx_nic_id_to_board_number(nic_id, &board_id);
		break;
	case 'b':
		board_id = atoi(optarg);
		break;
	case 'r':
		his_eid = atoi(optarg);
		break;
	case 'l':
		len = atoi(optarg);
		if (len > MAX_LEN) {
			fprintf(stderr, "len too large, max is %d\n", MAX_LEN);
			exit(1);
		}
		break;
	case 'N':
		iter = atoi(optarg);
		break;
	case 'V':
		Verify = 1;
		break;
	case 'v':
		do_verbose = 1;
		break;
	case 'w':
		do_wait = 1;
		break;
	case 'x':
#if MX_THREAD_SAFE
		do_bothways = 1;
#else
		fprintf(stderr, "bi-directional mode only supported with threadsafe mx lib\n");
		exit(1);
#endif
		break;
	case 'h':
	default:
		usage();
		exit(1);
	}

	if (rem_host != NULL)
		num_threads += do_bothways;
	ret = mx_open_endpoint(board_id, my_eid, filter, NULL, 0, &ep);
	if (ret != MX_SUCCESS) {
		fprintf(stderr, "Failed to open endpoint %s\n", mx_strerror(ret));
		exit(1);
	}

	/* If no host, we are receiver */
	if (rem_host == NULL) {
		if (do_verbose)
			printf("Starting streaming receiver\n");
		if (Verify) {
			fprintf(stderr, "-V ignored.  Verify must be set by sender\n");
			Verify = 0;
		}

		if (do_wait)
			receiver_blocking(ep, MATCH_VAL_MAIN, filter);
		else
			receiver_polling(ep, MATCH_VAL_MAIN, filter);
		

	} else {
		/* get address of destination */
		mx_hostname_to_nic_id(rem_host, &his_nic_id);
		mx_connect(ep, his_nic_id, his_eid, filter, 
			   MX_INFINITE, &his_addr);
		if (do_verbose)
			printf("Starting streaming send to host %s\n", 
			       rem_host);
		if (Verify) printf("Verifying results\n");

		/* start up the sender */
		if (do_wait)
			sender_blocking(ep, his_addr, iter, len, 
					do_bothways,MATCH_VAL_MAIN);
		else
			sender_polling(ep, his_addr, iter, len, 
				       do_bothways, MATCH_VAL_MAIN);
	}		

  
	mx_close_endpoint(ep);
	mx_finalize();
	exit(0);
}
	








/*
  This file uses MX driver indentation.

  Local Variables:
  c-file-style:"linux"
  tab-width:8
  End:
*/
