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

#define OMX_NUM_REQS   1024
#define OMX_FILTER_KEY 0x12345678
#define OMX_BID 0
#define OMX_SEND_EID 0
#define OMX_RECV_EID 1

static uint64_t            dest_addr;
static omx_endpoint_addr_t addr;
static omx_endpoint_t      eps  = NULL;
static omx_endpoint_t      epr  = NULL;
static bool                loop = true;
static hwloc_topology_t    topology;
static hwloc_cpuset_t      s_cpuset; 
static hwloc_cpuset_t      r_cpuset;

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
	omx_request_t sreq[OMX_NUM_REQS];
	omx_return_t  ret;
	omx_status_t  status;
	uint32_t      result;
	int           i;

	ret = omx_connect(eps, dest_addr, OMX_RECV_EID, OMX_FILTER_KEY, OMX_TIMEOUT_INFINITE, &addr);
	
	if (ret != OMX_SUCCESS) {
		fprintf(stderr, "Failed to connect to eps(%s)\n",
			omx_strerror(ret));
		exit(1);
	}

	omx__cpubind(s_cpuset, 1);
	while (loop) {
		for (i = 0; i < OMX_NUM_REQS; i++)
			omx_isend(eps, NULL, 0, addr, 0, NULL, sreq + i);
		for (i = 0; i < OMX_NUM_REQS; i++)
			omx_wait(eps, sreq + i, &status, &result, OMX_TIMEOUT_INFINITE);
	}
	return NULL;
}

static void *
omx__gen_receiver (void *arg)
{
	omx_return_t   ret;
	omx_status_t   status;
	omx_request_t  rreq[OMX_NUM_REQS];
	struct timeval tv;
	time_t   time;
	int      counter = 0;
        uint32_t result;
	int      i;

	omx__cpubind(r_cpuset, 2);

	gettimeofday(&tv, NULL);
	time = tv.tv_sec;
	while (loop) {
		for (i = 0; i < OMX_NUM_REQS; i++) {
			omx_irecv(epr, NULL, 0, 0, 0, NULL, rreq + i);
			ret = omx_wait(epr, rreq + i, &status, &result, OMX_TIMEOUT_INFINITE);
			
			if (likely(ret == OMX_SUCCESS && status.code == OMX_SUCCESS))
				counter++;
		}
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

	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);

	nb_cpus =  hwloc_get_nbobjs_by_type (topology, HWLOC_OBJ_CORE);

	printf("Found %d CPU(s) on the remote machine\n", nb_cpus);

	obj = hwloc_get_next_obj_by_type (topology, HWLOC_OBJ_CORE, NULL);

	if (! obj) {
		fprintf(stderr, "%s: Failed to get back obj for the first core\n", argv[0]);
		goto out_with_topo;
	}

	s_cpuset = hwloc_cpuset_dup(obj->cpuset);

	obj = hwloc_get_next_obj_by_type (topology, HWLOC_OBJ_CORE, obj);
	
	r_cpuset = obj ? hwloc_cpuset_dup(obj->cpuset) : s_cpuset;

	hwloc_cpuset_singlify(s_cpuset);
	hwloc_cpuset_singlify(r_cpuset);

	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT | SIGTERM, &sa, NULL)) {
		perror("sigaction");
		goto out_with_hwloc;
	}

	ret = omx_init ();
	
	if (ret != OMX_SUCCESS) {
		fprintf (stderr, "%s: Failed to initialize (%s)\n", argv[0],
			 omx_strerror (ret));
		goto out_with_hwloc;
	}


	ret = omx_open_endpoint (OMX_BID, OMX_RECV_EID, OMX_FILTER_KEY, NULL, 0, &epr);

	if (ret != OMX_SUCCESS) {
		fprintf (stderr, "%s: Failed to open endpoint for receiver(%s)\n", argv[0],
			 omx_strerror (ret));
		goto out_with_hwloc;
	}

	
	ret = omx_hostname_to_nic_id("localhost", &dest_addr);
	
	if (ret != OMX_SUCCESS) {
		fprintf(stderr, "Cannot find peer name localhost\n");
		goto out_with_epr;
	}
	
	
	ret = omx_open_endpoint (OMX_BID, OMX_SEND_EID, OMX_FILTER_KEY, NULL, 0, &eps);
	    
	if (ret != OMX_SUCCESS) {
		fprintf (stderr, "%s: Failed to open endpoint for sender(%s)\n", argv[0],
			 omx_strerror (ret));
		goto out_with_epr;
	}
	
	ret = omx__get_board_info (eps, 0, &board_info);
	
	if (ret != OMX_SUCCESS) {
		fprintf (stderr, "%s: Failed to read board #0, %s\n", argv[0],
			 omx_strerror (ret));
		goto out_with_ep;
	}
	
	omx__board_addr_sprintf (board_addr_str, board_info.addr);
	
	printf ("%s (board #0 name %s addr %s)\n",
		board_info.hostname, board_info.ifacename, board_addr_str);
	
	pthread_create (&sender, NULL, omx__gen_sender, NULL);
	pthread_create (&receiver, NULL, omx__gen_receiver, NULL);
	
	pthread_join (sender, NULL);
	pthread_join (receiver, NULL);
	
 out_with_ep:
	omx_close_endpoint (eps);
 out_with_epr:
	omx_close_endpoint (epr);
 out_with_hwloc:
        hwloc_cpuset_free(s_cpuset);
	hwloc_cpuset_free(r_cpuset);
 out_with_topo:
	hwloc_topology_destroy(topology);

	return ret;
}

/*
 * Local variables:
 *  tab-width: 8
 *  c-basic-offset: 8
 *  c-indent-level: 8
 * End:
 */
