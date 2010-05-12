#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <hwloc.h>

#include "open-mx.h"
#include "omx_io.h"
#include "omx_lib.h"

#define OMX_EVT_NUM 512

static omx_endpoint_t   ep   = NULL;
static bool             loop = true;
static hwloc_topology_t topology;
static hwloc_cpuset_t   s_cpuset; 
static hwloc_cpuset_t   r_cpuset;

static void
omx__sa_handler(int signum)
{
	loop = false;
}

static void
omx__cpubind(hwloc_const_cpuset_t cpuset, int retval)
{
	char *str;

	if (hwloc_set_cpubind(topology, cpuset, HWLOC_CPUBIND_THREAD)) {
		hwloc_cpuset_asprintf(&str, cpuset);
		fprintf(stderr, "Couldn't bind to cpuset %s\n", str);
		free(str);
		exit(retval);
	}
}

static void *
omx__gen_sender (void *arg)
{
	omx__cpubind(s_cpuset, 1);
	while (loop) {

		union omx_evt *slot;

		slot = ep->unexp_eventq + (OMX_EVENTQ_ENTRY_SIZE * OMX_UNEXP_EVENTQ_ENTRY_NR);
		/* unexp eventq is full */
		if (unlikely(slot->generic.type != OMX_EVT_NONE)) 
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

	omx__cpubind(r_cpuset, 2);

	gettimeofday(&tv, NULL);
	time = tv.tv_sec;
	while (loop) {

		volatile union omx_evt * evt = ep->next_unexp_event;

		/* unexp eventq is empty */
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
	struct omx_board_info board_info;
	struct sigaction sa = { .sa_handler = omx__sa_handler };
	char board_addr_str[OMX_BOARD_ADDR_STRLEN];
	pthread_t sender, receiver;
	hwloc_obj_t obj;
	int         nb_cpus;

	ret = omx_init ();

	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);

	nb_cpus =  hwloc_get_nbobjs_by_type (topology, HWLOC_OBJ_CORE);

	printf("Found %d CPU(s) on the remote machine\n", nb_cpus);

	obj = hwloc_get_next_obj_by_type (topology, HWLOC_OBJ_CORE, NULL);

	if (! obj) {
		fprintf(stderr, "%s: Failed to get back obj for the first core\n", argv[0]);
		goto out;
	}

	s_cpuset = hwloc_cpuset_dup(obj->cpuset);

	obj = hwloc_get_next_obj_by_type (topology, HWLOC_OBJ_CORE, obj);
	
	r_cpuset = obj ? hwloc_cpuset_dup(obj->cpuset) : s_cpuset;

	hwloc_cpuset_singlify(s_cpuset);
	hwloc_cpuset_singlify(r_cpuset);

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
        hwloc_cpuset_free(s_cpuset);
	hwloc_cpuset_free(r_cpuset);
	hwloc_topology_destroy(topology);
	
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
