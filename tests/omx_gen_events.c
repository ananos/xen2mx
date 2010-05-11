#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>
#include <sys/time.h>
#include <signal.h>

#include "open-mx.h"
#include "omx_lib.h"

#define OMX_EVT_NUM 1024

static omx_endpoint_t ep = NULL;
static cpu_set_t      s_mask;
static cpu_set_t      r_mask;
static bool           loop = true;

static void
omx__sa_handler(int signum)
{
	loop = false;
}

static void *
omx__gen_sender (void *arg)
{
	if (sched_setaffinity(0, sizeof(cpu_set_t), &s_mask)) {
		perror("sched_setaffinity");
		exit(1);
	}

	while (loop) {
		if (unlikely(ep->desc->status & OMX_ENDPOINT_DESC_STATUS_UNEXP_EVENTQ_FULL))
			continue;
		omx_generate_events (ep, OMX_EVT_NUM);
	}
	return NULL;
}

static void *
omx__gen_receiver (void *arg)
{
	struct timeval tv;
	time_t time;
	int    counter = 0;

	if (sched_setaffinity(0, sizeof(cpu_set_t), &r_mask)) {
		perror("sched_setaffinity");
		exit(2);
	}

	gettimeofday(&tv, NULL);
	time = tv.tv_sec;
	while (loop) {

		volatile union omx_evt * evt = ep->next_unexp_event;

		if (unlikely(evt->generic.type == OMX_EVT_NONE))
			continue;
		omx_progress_counter (ep, &counter);
		gettimeofday(&tv, NULL);

		if (unlikely(tv.tv_sec - time)) {
			printf("%d events/s\n", counter);
			time    = tv.tv_sec;
			counter = 0;
		}
	}
	return NULL;
}

int
main (int argc, char *argv[])
{
	omx_return_t ret;
	int cpu_0, cpu_1;
	struct omx_board_info board_info;
	struct sigaction sa = { .sa_handler = omx__sa_handler };
	char board_addr_str[OMX_BOARD_ADDR_STRLEN];
	pthread_t sender, receiver;
	
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <cpu #0> <cpu #1>\n", argv[0]);
		goto out;
	}
	
	ret   = omx_init ();
	cpu_0 = atoi(argv[1]);
	cpu_1 = atoi(argv[2]);

	CPU_ZERO(&s_mask);
	CPU_ZERO(&r_mask);

	printf("sender on cpu%d/receiver on cpu%d\n", cpu_0, cpu_1);
	CPU_SET(cpu_0, &s_mask);
	CPU_SET(cpu_1, &r_mask);

	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT | SIGTERM, &sa, NULL)) {
		perror("sigaction");
		goto out;
	}
	
	if (ret != OMX_SUCCESS) {
		fprintf (stderr, "%s: Failed to initialize (%s)\n", argv[0],
			 omx_strerror (ret));
		goto out;
	}

	ret = omx_open_endpoint (0, 0, 0x12345678, NULL, 0, &ep);
	
	if (ret != OMX_SUCCESS) {
		fprintf (stderr, "%s: Failed to open endpoint (%s)\n", argv[0],
			 omx_strerror (ret));
		goto out;
	}

	ret = omx__get_board_info (NULL, 0, &board_info);
	
	if (ret != OMX_SUCCESS) {
	    fprintf (stderr, "%s: Failed to read board 0 id, %s\n", argv[0],
		     omx_strerror (ret));
	    goto out;
	}

	omx__board_addr_sprintf (board_addr_str, board_info.addr);
	
	printf ("%s (board #0 name %s addr %s)\n",
		board_info.hostname, board_info.ifacename, board_addr_str);
	
	pthread_create (&sender, NULL, omx__gen_sender, NULL);
	pthread_create (&receiver, NULL, omx__gen_receiver, NULL);
	
	pthread_join (sender, NULL);
	pthread_join (receiver, NULL);
	
	omx_close_endpoint (ep);
	
	return 0;
 out:
	return -1;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
