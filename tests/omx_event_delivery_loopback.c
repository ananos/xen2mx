#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <hwloc.h>
#include <unistd.h>

#include "open-mx.h"
#include "omx_io.h"
#include "omx_lib.h"

#define OMX_NUM_REQS 1000000
#define OMX_FILTER_KEY 0x12345678
#define OMX_BID 0


#define TIME_DIFF(tv1, tv2)			\
	((tv2.tv_sec - tv1.tv_sec) * 1000000ULL + (tv2.tv_usec - tv1.tv_usec))

static uint64_t dest_addr;
static hwloc_topology_t topology;

struct data
{
	omx_endpoint_t ep;
	uint32_t recv_id;
	hwloc_cpuset_t cpuset;
};

static void
omx__cpubind(hwloc_const_cpuset_t cpuset)
{
	char *str;

	if (hwloc_set_cpubind(topology, cpuset, HWLOC_CPUBIND_THREAD)) {
		hwloc_cpuset_asprintf(&str, cpuset);
		fprintf(stderr, "Couldn't bind to cpuset %s\n", str);
		free(str);
		exit(1);
	}
}

static void
omx__gen_sender (const struct data *data)
{
	omx_endpoint_addr_t addr;
	omx_return_t ret;
	omx_status_t status;
	omx_request_t req;
	uint32_t result;
	int i;

	omx__cpubind(data->cpuset);
 	ret  = omx_connect(data->ep, dest_addr, data->recv_id, OMX_FILTER_KEY, OMX_TIMEOUT_INFINITE, &addr);

	if (unlikely(ret != OMX_SUCCESS)) {
		fprintf(stderr, "Failed to connect to ep #%d (%s)\n", i,
			omx_strerror(ret));
		exit(1);
	}

	/* synchronize with receiver */
	omx_isend(data->ep, NULL, 0, addr, 0, NULL, &req);
	omx_wait(data->ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
	
	for (i = 0; i < OMX_NUM_REQS - 1; i++)
		omx_isend(data->ep, NULL, 0, addr, 0, NULL, NULL);
	omx_isend(data->ep, NULL, 0, addr, 0, NULL, &req);
	omx_wait(data->ep, &req, &status, &result, OMX_TIMEOUT_INFINITE);
}

static void
omx__gen_receiver (const struct data *data)
{
	omx_return_t ret;
	omx_status_t status;
	omx_request_t rreq;
	struct timeval tv1, tv2;
        uint32_t result;
	int i;

	omx__cpubind(data->cpuset);

	/* synchronize with sender (required for the timer) */
	omx_irecv(data->ep, NULL, 0, 0, 0, NULL, &rreq);
	omx_wait(data->ep, &rreq, &status, &result, OMX_TIMEOUT_INFINITE);

	gettimeofday(&tv1, NULL);

	for (i = 0; i < OMX_NUM_REQS - 1; i++)
		omx_irecv(data->ep, NULL, 0, 0, 0, NULL, NULL);
	omx_irecv(data->ep, NULL, 0, 0, 0, NULL, &rreq);
	omx_wait(data->ep, &rreq, &status, &result, OMX_TIMEOUT_INFINITE);

	gettimeofday(&tv2, NULL);
	printf("%.3lf ms\n", (double)TIME_DIFF(tv1, tv2) / 1000);
}

int
main (int argc, char *argv[])
{
	struct omx_board_info board_info;
	struct data *data;
	char board_addr_str[OMX_BOARD_ADDR_STRLEN];
	omx_return_t ret;
	hwloc_obj_t obj;
	int i, nb_socket, nb_core, sender = 0, begin, end, core_per_sock;
	hwloc_cpuset_t *cpuset;
	void (*dispatcher)(const struct data *);

	if (argc >= 2 && !strcmp(argv[1], "-s"))
		sender = 1;
	
	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);

	nb_socket =  hwloc_get_nbobjs_by_type (topology, HWLOC_OBJ_SOCKET);

	if (nb_socket < 2) {
		fprintf(stderr, "%s: Not sockets enough, at least 2 are required\n", argv[0]);
		goto out_with_topo;
	}

	nb_core =  hwloc_get_nbobjs_by_type (topology, HWLOC_OBJ_CORE);
	core_per_sock = nb_core / nb_socket;

	cpuset = malloc(core_per_sock * 2 * sizeof(*cpuset));
	data = malloc(core_per_sock * 2 * sizeof(*data));

	assert (cpuset && data);

	printf("Found %d socket(s) and %d core(s) on the remote machine\n", nb_socket, nb_core);

	/* Distribute senders on the first socket */
	obj = hwloc_get_next_obj_by_type (topology, HWLOC_OBJ_SOCKET, NULL);

	hwloc_distribute(topology, obj, cpuset, core_per_sock);
	
	/* Then distribute receivers on the second socket */
	obj = hwloc_get_next_obj_by_type (topology, HWLOC_OBJ_SOCKET, obj);

	hwloc_distribute(topology, obj, cpuset + core_per_sock, core_per_sock);

	ret = omx_init ();
	
	if (ret != OMX_SUCCESS) {
		fprintf (stderr, "%s: Failed to initialize (%s)\n", argv[0],
			 omx_strerror (ret));
		goto out_with_free;
	}

	begin = sender ? 0 : core_per_sock;
	end   = sender ? core_per_sock : core_per_sock * 2;
	
	for (i = begin; i < end; i++) {
		ret = omx_open_endpoint (OMX_BID, i, OMX_FILTER_KEY, NULL, 0, &data[i].ep);
		
		if (unlikely(ret != OMX_SUCCESS)) {
			fprintf (stderr, "%s: Failed to open endpoint #%d (%s)\n", argv[0], i,
				 omx_strerror (ret));
			goto out_with_free;
		}

		data[i].cpuset = cpuset[i];
		hwloc_cpuset_singlify(cpuset[i]);
	}
	
	ret = omx_hostname_to_nic_id("localhost", &dest_addr);
	
	if (ret != OMX_SUCCESS) {
		fprintf(stderr, "Cannot find peer name localhost\n");
		goto out_with_ep;
	}
	
	/* FIXME: Don't use it (private) ! */
	ret = omx__get_board_info (data[sender ? 0 : core_per_sock].ep, sender ? 0 : core_per_sock, &board_info);
	
	if (ret != OMX_SUCCESS) {
		fprintf (stderr, "%s: Failed to read board #0, %s\n", argv[0],
			 omx_strerror (ret));
		goto out_with_ep;
	}
	
	/* FIXME: Don't use it (private) ! */
	omx__board_addr_sprintf (board_addr_str, board_info.addr);
	
	printf ("%s (board #0 name %s addr %s)\n",
		board_info.hostname, board_info.ifacename, board_addr_str);

	printf("Starting %s...\n", sender ? "senders" : "receivers");
	dispatcher = sender ? omx__gen_sender : omx__gen_receiver;

	for (i = begin; i < end; i++) {
		data[i].recv_id = sender ? i + core_per_sock : 0;
		if (fork() == 0) {
			dispatcher(data + i);
			exit(0);
		}
	}

	for (i = 0; i < core_per_sock; i++)
		wait(NULL);

 out_with_ep:
	for (i = begin; i < end; i++)
		omx_close_endpoint (data[i].ep);
 out_with_free:
	for (i = 0; i < 8; i++)
		hwloc_cpuset_free(cpuset[i]);
	free(cpuset);
	free(data);
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
