/**
 * (C) Copyright 2016-2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_rebuild.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include <daos/pool.h>
#include <daos/mgmt.h>

#define KEY_NR		1000
#define OBJ_NR		10
#define OBJ_CLS		DAOS_OC_R3S_RW
#define OBJ_REPLICAS	3

static void
rebuild_test_exclude_tgt(test_arg_t **args, int arg_cnt, d_rank_t rank,
			 bool kill)
{
	if (args[0]->myrank == 0) {
		int i;

		if (kill) {
			daos_kill_server(args[0], args[0]->pool.pool_uuid,
					 args[0]->group, &args[0]->pool.svc,
					 rank);
			sleep(5);
		}

		for (i = 0; i < arg_cnt; i++) {
			daos_exclude_server(args[i]->pool.pool_uuid,
				args[i]->group, &args[i]->pool.svc, rank);
			sleep(2);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
rebuild_test_add_tgt(test_arg_t **args, int args_cnt, d_rank_t rank)
{
	/** exclude the target from the pool */
	if (args[0]->myrank == 0) {
		int i;

		for (i = 0; i < args_cnt; i++) {
			if (!args[i]->pool.destroyed)
				daos_add_server(args[i]->pool.pool_uuid,
					args[i]->group, &args[i]->pool.svc,
					rank);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static int
rebuild_io_obj_internal(struct ioreq *req, bool validate, daos_epoch_t eph,
			daos_epoch_t validate_eph)
{
#define BULK_SIZE	5000
#define REC_SIZE	64
#define LARGE_KEY_SIZE	(512 * 1024)
	char	dkey[32];
	char	akey[32];
	char	data[REC_SIZE];
	char	data_verify[REC_SIZE];
	char	*large_key;
	int	akey_punch_idx = 1;
	int	dkey_punch_idx = 1;
	int	j;
	int	k;
	int	l;

	D_ALLOC(large_key, LARGE_KEY_SIZE);
	if (large_key == NULL)
		return -DER_NOMEM;
	memset(large_key, 'L', LARGE_KEY_SIZE - 1);

	for (j = 0; j < 5; j++) {
		req->iod_type = DAOS_IOD_ARRAY;
		/* small records */
		sprintf(dkey, "dkey_%d", j);
		sprintf(data, "%s_"DF_U64, "data", eph);
		sprintf(data_verify, "%s_"DF_U64, "data", validate_eph);
		for (k = 0; k < 3; k++) {
			sprintf(akey, "akey_%d", k);
			for (l = 0; l < 10; l++) {
				if (validate) {
					/* How to verify punch? XXX */
					if (k == akey_punch_idx ||
					    j == dkey_punch_idx)
						continue;
					memset(data, 0, REC_SIZE);
					if (l == 7)
						lookup_single(large_key, akey,
							l, data, REC_SIZE, eph,
							req);
					else
						lookup_single(dkey, akey, l,
							data, REC_SIZE, eph,
							req);
					assert_memory_equal(data, data_verify,
							  strlen(data_verify));
				} else {
					if (l == 7)
						insert_single(large_key, akey,
							l, data,
							strlen(data) + 1,
							eph, req);
					else
						insert_single(dkey, akey, l,
							data, strlen(data) + 1,
							eph, req);
				}
			}

			/* Punch akey */
			if (k == akey_punch_idx && !validate)
				punch_akey(dkey, akey, eph, req);
		}

		/* large records */
		for (k = 0; k < 2; k++) {
			char bulk[BULK_SIZE+10];
			char compare[BULK_SIZE];

			sprintf(akey, "akey_bulk_%d", k);
			memset(compare, 'a', BULK_SIZE);
			for (l = 0; l < 5; l++) {
				if (validate) {
					/* How to verify punch? XXX */
					if (k == akey_punch_idx ||
					    j == dkey_punch_idx)
						continue;
					memset(bulk, 0, BULK_SIZE);
					lookup_single(dkey, akey, l,
						      bulk, BULK_SIZE + 10,
						      eph, req);
					assert_memory_equal(bulk, compare,
							    BULK_SIZE);
				} else {
					memset(bulk, 'a', BULK_SIZE);
					insert_single(dkey, akey, l,
						      bulk, BULK_SIZE, eph,
						      req);
				}
			}

			/* Punch akey */
			if (k == akey_punch_idx && !validate)
				punch_akey(dkey, akey, eph, req);
		}

		/* Punch dkey */
		if (j == dkey_punch_idx && !validate)
			punch_dkey(dkey, eph, req);

		/* single record */
		sprintf(data, "%s_"DF_U64, "single_data", eph);
		sprintf(data_verify, "%s_"DF_U64, "single_data",
			validate_eph);
		req->iod_type = DAOS_IOD_SINGLE;
		sprintf(dkey, "dkey_single_%d", j);
		if (validate) {
			memset(data, 0, REC_SIZE);
			lookup_single(dkey, "akey_single", 0, data, REC_SIZE,
				      eph, req);
			assert_memory_equal(data, data_verify,
					    strlen(data_verify));
		} else {
			insert_single(dkey, "akey_single", 0, data,
				      strlen(data) + 1, eph, req);
		}
	}

	D_FREE(large_key);
	return 0;
}

static void
rebuild_io(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr)
{
	struct ioreq	req;
	daos_epoch_t	eph = arg->hce + arg->index * 2 + 1;
	daos_epoch_t	commit_eph = eph + 1;
	int		rc;
	int		i;
	int		punch_idx = 1;

	print_message("update obj %d eph "DF_U64" before rebuild\n", oids_nr,
		      eph);

	/** Disable holding epoch until we totally support epoch discard,
	 *  see DAOS-1199.
	 */
	rc = daos_epoch_hold(arg->coh, &commit_eph, NULL, NULL);
	assert_int_equal(rc, 0);

	for (i = 0; i < oids_nr; i++) {
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		if (i == punch_idx) {
			punch_obj(eph, &req);
		} else {
			rebuild_io_obj_internal((&req), false, eph, -1);
			/* eph + 1 is discarded, so it should read the data
			 * of eph
			 **/
			rebuild_io_obj_internal((&req), false, eph + 1, -1);
		}

		ioreq_fini(&req);
	}

	/* We may only commit eph, when it support discard */
	rc = daos_epoch_commit(arg->coh, commit_eph, NULL, NULL);
	if (rc)
		print_message("container epoch commit failed "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);
}

static void
rebuild_io_validate(test_arg_t *arg, daos_obj_id_t *oids, int oids_nr,
		    bool discard)
{
	struct ioreq	req;
	daos_epoch_t	eph = arg->hce + arg->index * 2 + 1;
	int		i;
	int		punch_idx = 1;

	/* XXX Disable discard until we support ARRAY record discard */
	discard = false;

	if (discard) {
		int rc;

		print_message("discard eph "DF_U64"\n", eph + 1);
		rc = daos_epoch_discard(arg->coh, eph + 1, NULL, NULL);
		assert_int_equal(rc, 0);
	}

	arg->fail_loc = DAOS_OBJ_SPECIAL_SHARD | DAOS_FAIL_VALUE;
	/* Validate data for each shard */
	for (i = 0; i < OBJ_REPLICAS; i++) {
		int j;

		arg->fail_value = i;
		for (j = 0; j < oids_nr; j++) {
			ioreq_init(&req, arg->coh, oids[j], DAOS_IOD_ARRAY,
				   arg);

			/* how to validate punch object */
			if (j != punch_idx)
				/* Validate eph data */
				rebuild_io_obj_internal((&req), true, eph, eph);

			ioreq_fini(&req);
		}
	}

	arg->fail_loc = 0;
	arg->fail_value = 0;
}

static void
rebuild_targets(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		int rank_nr, bool kill)
{
	int	i;

	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_pre_cb)
			args[i]->rebuild_pre_cb(args[i]);

	/** exclude the target from the pool */
	for (i = 0; i < rank_nr; i++) {
		rebuild_test_exclude_tgt(args, args_cnt, failed_ranks[i], kill);
		/* Sleep 5 seconds to make sure the rebuild start */
		sleep(5);
	}

	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_cb)
			args[i]->rebuild_cb(args[i]);

	if (args[0]->myrank == 0)
		test_rebuild_wait(args, args_cnt);

	if (!kill) {
		/* Add back the target if it is not being killed */
		for (i = 0; i < rank_nr; i++)
			rebuild_test_add_tgt(args, args_cnt, failed_ranks[i]);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	for (i = 0; i < args_cnt; i++)
		if (args[i]->rebuild_post_cb)
			args[i]->rebuild_post_cb(args[i]);
}

static void
rebuild_single_pool_target(test_arg_t *arg, d_rank_t failed_rank)
{
	rebuild_targets(&arg, 1, &failed_rank, 1, false);
}

static void
rebuild_pools_targets(test_arg_t **args, int args_cnt, d_rank_t *failed_ranks,
		      int ranks_nr)
{
	rebuild_targets(args, args_cnt, failed_ranks, ranks_nr, false);
}

static void
rebuild_dkeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	key[16];

		sprintf(key, "%d", i);
		insert_single(key, "a_key", 0, "data",
			      strlen("data") + 1, 0, &req);
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);
}

static void
rebuild_akeys(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	for (i = 0; i < KEY_NR; i++) {
		char	akey[16];

		sprintf(akey, "%d", i);
		insert_single("d_key", akey, 0, "data",
			      strlen("data") + 1, 0, &req);
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);
}

static void
rebuild_indexes(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			j;

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 2000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      2000, DP_OID(oid));
	for (i = 0; i < 100; i++) {
		char	key[16];

		sprintf(key, "%d", i);
		for (j = 0; j < 20; j++)
			insert_single(key, "a_key", j, "data",
				      strlen("data") + 1, 0, &req);
	}
	ioreq_fini(&req);

	/* Rebuild rank 1 */
	rebuild_single_pool_target(arg, ranks_to_kill[0]);
}

static void
rebuild_multiple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		i;
	int		j;
	int		k;

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      1000, DP_OID(oid));
	for (i = 0; i < 10; i++) {
		char	dkey[16];

		sprintf(dkey, "dkey_%d", i);
		for (j = 0; j < 10; j++) {
			char	akey[16];

			sprintf(akey, "akey_%d", j);
			for (k = 0; k < 10; k++)
				insert_single(dkey, akey, k, "data",
					      strlen("data") + 1, 0,
					      &req);
		}
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);
}

static void
rebuild_large_rec(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	char			buffer[5000];

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert 1000 records */
	print_message("Insert %d kv record in object "DF_OID"\n",
		      KEY_NR, DP_OID(oid));
	memset(buffer, 'a', 5000);
	for (i = 0; i < KEY_NR; i++) {
		char	key[16];

		sprintf(key, "%d", i);
		insert_single(key, "a_key", 0, buffer, 5000, 0, &req);
	}
	ioreq_fini(&req);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);
}

static void
rebuild_objects(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR, false);
}

static void
rebuild_drop_scan(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set drop scan fail_loc on server 0 */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, 0, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
				     NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR, true);
}

static void
rebuild_retry_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	if (arg->myrank == 0)	
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_NO_HDL | DAOS_FAIL_ONCE,
				     NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR, true);
}

static void
rebuild_retry_for_stale_pool(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_STALE_POOL | DAOS_FAIL_ONCE,
				     NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR, true);
}

static void
rebuild_drop_obj(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set drop REBUILD_OBJECTS reply on all servers */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, 0, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_DROP_OBJ | DAOS_FAIL_ONCE,
				     NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR, true);
}

static void
rebuild_update_failed(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set drop scan reply on all servers */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, 0, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_UPDATE_FAIL | DAOS_FAIL_ONCE,
				     NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);
}

static void
rebuild_multiple_pools(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*args[2] = { 0 };
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	args[0] = arg;
	/* create/connect another pool */
	rc = test_setup((void **)&args[1], SETUP_CONT_CONNECT, arg->multi_rank,
			DEFAULT_POOL_SIZE, NULL);
	if (rc) {
		print_message("open/connect another pool failed: rc %d\n", rc);
		return;
	}

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(args[0], oids, OBJ_NR);
	rebuild_io(args[1], oids, OBJ_NR);

	rebuild_pools_targets(args, 2, ranks_to_kill, 1);

	rebuild_io_validate(args[0], oids, OBJ_NR, true);
	rebuild_io_validate(args[1], oids, OBJ_NR, true);

	test_teardown((void **)&args[1]);
}

static int
rebuild_close_container_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;
	int		rc_reduce = 0;

	if (daos_handle_is_inval(arg->coh))
		return 0;

	rc = daos_cont_close(arg->coh, NULL);
	if (arg->multi_rank) {
		MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
			      MPI_COMM_WORLD);
		rc = rc_reduce;
	}
	print_message("container close "DF_UUIDF"\n",
		      DP_UUID(arg->co_uuid));
	if (rc) {
		print_message("failed to close container "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);
		return rc;
	}
	arg->coh = DAOS_HDL_INVAL;

	return 0;
}

static int
rebuild_destroy_container_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;

	if (uuid_is_null(arg->co_uuid))
		return 0;

	rc = rebuild_close_container_cb(data);
	if (rc)
		return rc;

	while (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, arg->co_uuid, 1, NULL);
		if (rc == -DER_BUSY || rc == -DER_IO) {
			print_message("Container is busy, wait\n");
			sleep(1);
			continue;
		}
		break;
	}
	print_message("container "DF_UUIDF"/"DF_UUIDF" destroyed\n",
		      DP_UUID(arg->pool.pool_uuid), DP_UUID(arg->co_uuid));
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		print_message("failed to destroy container "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);
	uuid_clear(arg->co_uuid);

	return rc;
}

static void
rebuild_destroy_container(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*args[2] = { 0 };
	daos_obj_id_t	oids[OBJ_NR * 100];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	args[0] = arg;
	/* create/connect another pool */
	rc = test_setup((void **)&args[1], SETUP_CONT_CONNECT, arg->multi_rank,
			DEFAULT_POOL_SIZE, NULL);
	if (rc) {
		print_message("open/connect another pool failed: rc %d\n", rc);
		return;
	}

	for (i = 0; i < OBJ_NR * 10; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(args[1], oids, OBJ_NR * 10);

	args[1]->rebuild_cb = rebuild_destroy_container_cb;

	rebuild_pools_targets(args, 2, ranks_to_kill, 1);

	test_teardown((void **)&args[1]);
}

static void
rebuild_close_container(void **state)
{
	test_arg_t	*arg = *state;
	test_arg_t	*args[2] = { 0 };
	daos_obj_id_t	oids[OBJ_NR];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	args[0] = arg;
	/* create/connect another pool */
	rc = test_setup((void **)&args[1], SETUP_CONT_CONNECT, arg->multi_rank,
			DEFAULT_POOL_SIZE, NULL);
	if (rc) {
		print_message("open/connect another pool failed: rc %d\n", rc);
		return;
	}

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(args[1], oids, OBJ_NR);

	args[1]->rebuild_pre_cb = rebuild_close_container_cb;

	rebuild_pools_targets(args, 2, ranks_to_kill, 1);

	test_teardown((void **)&args[1]);
}

static int
rebuild_pool_disconnect_internal(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;
	int		rc_reduce = 0;

	/* Close cont and disconnect pool */
	rc = daos_cont_close(arg->coh, NULL);
	if (arg->multi_rank) {
		MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
			      MPI_COMM_WORLD);
		rc = rc_reduce;
	}
	print_message("container close "DF_UUIDF"\n",
		      DP_UUID(arg->co_uuid));
	if (rc) {
		print_message("failed to close container "DF_UUIDF
			      ": %d\n", DP_UUID(arg->co_uuid), rc);
		return rc;
	}

	arg->coh = DAOS_HDL_INVAL;
	rc = daos_pool_disconnect(arg->pool.poh, NULL /* ev */);
	if (rc)
		print_message("failed to disconnect pool "DF_UUIDF
			      ": %d\n", DP_UUID(arg->pool.pool_uuid), rc);

	print_message("pool disconnect "DF_UUIDF"\n",
		      DP_UUID(arg->pool.pool_uuid));

	arg->pool.poh = DAOS_HDL_INVAL;
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}

static int
rebuild_destroy_pool_cb(void *data)
{
	test_arg_t	*arg = data;
	int		rc = 0;

	rebuild_pool_disconnect_internal(data);

	if (arg->myrank == 0) {
		rc = daos_pool_destroy(arg->pool.pool_uuid, NULL, true, NULL);
		if (rc)
			print_message("failed to destroy pool"DF_UUIDF" %d\n",
				      DP_UUID(arg->pool.pool_uuid), rc);
	}

	arg->pool.destroyed = true;
	print_message("pool destroyed "DF_UUIDF"\n",
		      DP_UUID(arg->pool.pool_uuid));
	/* Disable fail_loc and start rebuild */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     0, NULL);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}

static void
rebuild_destroy_pool_internal(void **state, uint64_t fail_loc)
{
	test_arg_t	*arg = *state;
	test_arg_t	*args[2] = { 0 };
	daos_obj_id_t	oids[OBJ_NR * 10];
	int		i;
	int		rc;

	if (!test_runable(arg, 6))
		return;

	args[0] = arg;
	/* create/connect another pool */
	rc = test_setup((void **)&args[1], SETUP_CONT_CONNECT, arg->multi_rank,
			DEFAULT_POOL_SIZE, NULL);
	if (rc) {
		print_message("open/connect another pool failed: rc %d\n", rc);
		return;
	}

	for (i = 0; i < OBJ_NR * 10; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(args[1], oids, OBJ_NR * 10);

	/* hang the rebuild */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC, fail_loc,
				     NULL);

	args[1]->rebuild_cb = rebuild_destroy_pool_cb;

	rebuild_pools_targets(args, 2, ranks_to_kill, 1);
}

static void
rebuild_destroy_pool_during_scan(void ** state)
{
	return rebuild_destroy_pool_internal(state, DAOS_REBUILD_TGT_SCAN_HANG |
						    DAOS_FAIL_VALUE);
}

static void
rebuild_destroy_pool_during_rebuild(void ** state)
{
	return rebuild_destroy_pool_internal(state,
					     DAOS_REBUILD_TGT_REBUILD_HANG |
					     DAOS_FAIL_VALUE);
}

static void
rebuild_iv_tgt_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Set no hdl fail_loc on all servers */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_IV_UPDATE_FAIL |
				     DAOS_FAIL_ONCE, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR, true);
}

static void
rebuild_tgt_start_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* failed to start rebuild on rank 0 */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, 0, DSS_KEY_FAIL_LOC,
				  DAOS_REBUILD_TGT_START_FAIL | DAOS_FAIL_ONCE,
				  NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	rebuild_io_validate(arg, oids, OBJ_NR, true);
}

static void
rebuild_send_objects_fail(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* Skip object send on all of the targets */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_SEND_OBJS_FAIL |
				     DAOS_FAIL_VALUE, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	/* Even do not sending the objects, the rebuild should still be
	 * able to finish.
	 */
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	/* failed to start rebuild on rank 0 */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC, 0,
				     NULL);
	MPI_Barrier(MPI_COMM_WORLD);
}

static int
rebuild_pool_connect_internal(void *data)
{
	test_arg_t	*arg = data;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &arg->pool.poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc)
			print_message("daos_pool_connect failed, rc: %d\n", rc);

		print_message("pool connect "DF_UUIDF"\n",
			       DP_UUID(arg->pool.pool_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast pool info */
	if (arg->multi_rank) {
		MPI_Bcast(&arg->pool.pool_info, sizeof(arg->pool.pool_info),
			  MPI_CHAR, 0, MPI_COMM_WORLD);
		handle_share(&arg->pool.poh, HANDLE_POOL, arg->myrank,
			     arg->pool.poh, 0);
	}

	/** open container */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);

		print_message("container open "DF_UUIDF"\n",
			       DP_UUID(arg->co_uuid));
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast container info */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->pool.poh,
			     0);
	}

	return 0;
}

static int
rebuild_pool_disconnect_cb(void *data)
{
	test_arg_t	*arg = data;

	rebuild_pool_disconnect_internal(data);

	/* Disable fail_loc and start rebuild */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	return 0;
}

static void
rebuild_tgt_pool_disconnect_internal(void **state, unsigned int fail_loc)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* hang the rebuild during scan */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC, fail_loc,
				     NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	/* NB: During the test, one target will be excluded from the pool map,
	 * then container/pool will be closed/disconnectd during the rebuild,
	 * i.e. before the target is added back. so the container hdl cache
	 * will be left on the excluded target after the target is added back.
	 * So the container might not be able to destroyed because of the left
	 * over container hdl. Once the container is able to evict the container
	 * hdl, then this issue can be fixed. XXX
	 */
	arg->rebuild_cb = rebuild_pool_disconnect_cb;
	arg->rebuild_post_cb = rebuild_pool_connect_internal;

	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	arg->rebuild_cb = NULL;
	arg->rebuild_post_cb = NULL;
}

static void
rebuild_tgt_pool_disconnect_in_scan(void **state)
{
	rebuild_tgt_pool_disconnect_internal(state,
					     DAOS_REBUILD_TGT_SCAN_HANG |
					     DAOS_FAIL_VALUE);
}

static void
rebuild_tgt_pool_disconnect_in_rebuild(void **state)
{
	rebuild_tgt_pool_disconnect_internal(state,
					     DAOS_REBUILD_TGT_REBUILD_HANG |
					     DAOS_FAIL_VALUE);
}

static int
rebuild_pool_connect_cb(void *data)
{
	test_arg_t	*arg = data;

	rebuild_pool_connect_internal(data);
	/* Disable fail_loc and start rebuild */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	return 0;
}

static void
rebuild_offline_pool_connect_internal(void **state, unsigned int fail_loc)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* hang the rebuild */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC, fail_loc,
				     NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	arg->rebuild_pre_cb = rebuild_pool_disconnect_internal;
	arg->rebuild_cb = rebuild_pool_connect_cb;

	rebuild_targets(&arg, 1, ranks_to_kill, 1, true);

	arg->rebuild_pre_cb = NULL;
	arg->rebuild_cb = NULL;

	rebuild_io_validate(arg, oids, OBJ_NR, false);
}

static void
rebuild_offline_pool_connect_in_scan(void **state)
{
	rebuild_offline_pool_connect_internal(state,
					      DAOS_REBUILD_TGT_SCAN_HANG |
					      DAOS_FAIL_VALUE);
}

static void
rebuild_offline_pool_connect_in_rebuild(void **state)
{
	rebuild_offline_pool_connect_internal(state,
					      DAOS_REBUILD_TGT_REBUILD_HANG |
					      DAOS_FAIL_VALUE);
}

static void
rebuild_offline(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}
	rebuild_io(arg, oids, OBJ_NR);

	arg->rebuild_pre_cb = rebuild_pool_disconnect_internal;
	arg->rebuild_post_cb = rebuild_pool_connect_internal;

	rebuild_targets(&arg, 1, ranks_to_kill, 1, true);

	arg->rebuild_pre_cb = NULL;
	arg->rebuild_post_cb = NULL;

	rebuild_io_validate(arg, oids, OBJ_NR, false);
}

static int
rebuild_change_leader_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	d_rank_t	leader;

	test_get_leader(test_arg, &leader);

	/* Skip appendentry to re-elect the leader */
	if (test_arg->myrank == 0) {
		daos_mgmt_params_set(test_arg->group, leader, DSS_KEY_FAIL_LOC,
				     DAOS_RDB_SKIP_APPENDENTRIES_FAIL |
				     DAOS_FAIL_VALUE, NULL);
		print_message("sleep 15 seconds for re-election leader\n");
		/* Sleep 15 seconds to make sure the leader is changed */
		sleep(15);
		/* Continue the rebuild */
		daos_mgmt_params_set(test_arg->group, -1, DSS_KEY_FAIL_LOC,
				     0, NULL);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	return 0;
}

static void
rebuild_master_change_during_scan(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6) || arg->pool.svc.rl_nr == 1)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* All ranks should wait before rebuild */
	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				 DAOS_REBUILD_TGT_SCAN_HANG | DAOS_FAIL_VALUE,
				     NULL);
	MPI_Barrier(MPI_COMM_WORLD);
	arg->rebuild_cb = rebuild_change_leader_cb;

	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	arg->rebuild_cb = NULL;

	/* Verify the data */
	rebuild_io_validate(arg, oids, OBJ_NR, true);
}

static void
rebuild_master_change_during_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6) || arg->pool.svc.rl_nr == 1)
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	/* All ranks should wait before rebuild */
	daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_TGT_REBUILD_HANG | DAOS_FAIL_VALUE,
			     NULL);

	arg->rebuild_cb = rebuild_change_leader_cb;

	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	arg->rebuild_cb = NULL;

	/* Verify the data */
	rebuild_io_validate(arg, oids, OBJ_NR, true);
}

static int
rebuild_nospace_cb(void *data)
{
	test_arg_t	*arg = data;

	/* Wait for space is claimed */
	sleep(60);

	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     0, NULL);

	print_message("re-enable recovery\n");
	if (arg->myrank == 0)
		/* Resume the rebuild. FIXME: fix this once we have better
		 * way to resume rebuild through mgmt cmd.
		 */
		daos_mgmt_params_set(arg->group, -1, DSS_REBUILD_RES_PERCENTAGE,
				     30, NULL);

	MPI_Barrier(MPI_COMM_WORLD);

	return 0;
}

static void
rebuild_nospace(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		skip();

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	rebuild_io(arg, oids, OBJ_NR);

	if (arg->myrank == 0)
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_TGT_NOSPACE | DAOS_FAIL_VALUE,
				     NULL);

	MPI_Barrier(MPI_COMM_WORLD);

	arg->rebuild_cb = rebuild_nospace_cb;
	rebuild_single_pool_target(arg, ranks_to_kill[0]);

	arg->rebuild_cb = NULL;
	rebuild_io_validate(arg, oids, OBJ_NR, true);
}

static void
rebuild_multiple_tgts(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct daos_obj_layout *layout;
	d_rank_t	leader;
	d_rank_t	exclude_ranks[2];
	int		i;

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);

	rebuild_io(arg, &oid, 1);

	test_get_leader(arg, &leader);
	daos_obj_layout_get(arg->coh, oid, &layout);

	if (arg->myrank == 0) {
		int fail_cnt = 0;

		/* All ranks should wait before rebuild */
		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
				     DAOS_REBUILD_HANG | DAOS_FAIL_VALUE,
				     NULL);
		/* kill 2 ranks at the same time */
		D_ASSERT(layout->ol_shards[0]->os_replica_nr > 2);
		for (i = 0; i < 3; i++) {
			d_rank_t rank = layout->ol_shards[0]->os_ranks[i];

			if (rank != leader) {
				exclude_ranks[fail_cnt] = rank;
				daos_exclude_server(arg->pool.pool_uuid,
						    arg->group, &arg->pool.svc,
						    rank);
				if (++fail_cnt >= 2)
					break;
			}
		}

		daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC, 0, NULL);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	/* Rebuild 2 ranks at the same time */
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	/* Verify the data */
	rebuild_io_validate(arg, &oid, 1, true);

	daos_obj_layout_free(layout);

	/* Add back the target if it is not being killed */
	if (arg->myrank == 0) {
		for (i = 0; i < 2; i++)
			rebuild_test_add_tgt(&arg, 1, exclude_ranks[i]);
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

static int
rebuild_io_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	daos_obj_id_t	*oids = test_arg->rebuild_cb_arg;

	if (!daos_handle_is_inval(test_arg->coh))
		rebuild_io(test_arg, oids, OBJ_NR);

	return 0;
}

static int
rebuild_io_post_cb(void *arg)
{
	test_arg_t	*test_arg = arg;
	daos_obj_id_t	*oids = test_arg->rebuild_post_cb_arg;

	if (!daos_handle_is_inval(test_arg->coh))
		rebuild_io_validate(test_arg, oids, OBJ_NR, true);

	return 0;
}

static void
rebuild_master_failure(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oids[OBJ_NR];
	daos_obj_id_t		cb_arg_oids[OBJ_NR];
	daos_pool_info_t	pinfo = { 0 };
	daos_pool_info_t	pinfo_new = { 0 };
	int			i;
	int			rc;

	/* need 5 svc replicas, as will kill the leader 2 times */
	if (!test_runable(arg, 6) || arg->pool.svc.rl_nr < 5) {
		print_message("testing skipped ...\n");
		return;
	}

	test_get_leader(arg, &ranks_to_kill[0]);
	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		cb_arg_oids[i] = dts_oid_gen(OBJ_CLS, 0, arg->myrank);
	}

	/* prepare the data */
	rebuild_io(arg, oids, OBJ_NR);

	arg->rebuild_cb = rebuild_io_cb;
	arg->rebuild_cb_arg = cb_arg_oids;
	arg->rebuild_post_cb = rebuild_io_post_cb;
	arg->rebuild_post_cb_arg = cb_arg_oids;

	rebuild_targets(&arg, 1, ranks_to_kill, 1, true);

	arg->rebuild_cb = NULL;
	arg->rebuild_post_cb = NULL;

	/* Verify the data */
	rebuild_io_validate(arg, oids, OBJ_NR, true);

	/* Verify the POOL_QUERY get same rebuild status after leader change */
	rc = test_pool_get_info(arg, &pinfo);
	assert_int_equal(rc, 0);
	assert_int_equal(pinfo.pi_rebuild_st.rs_done, 1);
	rc = rebuild_change_leader_cb(arg);
	assert_int_equal(rc, 0);
	rc = test_pool_get_info(arg, &pinfo_new);
	assert_int_equal(rc, 0);
	assert_int_equal(pinfo_new.pi_rebuild_st.rs_done, 1);
	rc = memcmp(&pinfo.pi_rebuild_st, &pinfo_new.pi_rebuild_st,
		    sizeof(pinfo.pi_rebuild_st));
	print_message("svc leader changed from %d to %d, should get same "
		      "rebuild status (memcmp result %d).\n", pinfo.pi_leader,
		      pinfo_new.pi_leader, rc);
	assert_int_equal(rc, 0);
}

static void
rebuild_multiple_failures(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	daos_obj_id_t	cb_arg_oids[OBJ_NR];
	int		i;

	if (!test_runable(arg, 6))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
		cb_arg_oids[i] = dts_oid_gen(OBJ_CLS, 0, arg->myrank);
	}

	/* prepare the data */
	rebuild_io(arg, oids, OBJ_NR);

	arg->rebuild_cb = rebuild_io_cb;
	arg->rebuild_cb_arg = cb_arg_oids;
	arg->rebuild_post_cb = rebuild_io_post_cb;
	arg->rebuild_post_cb_arg = cb_arg_oids;

	rebuild_targets(&arg, 1, ranks_to_kill, MAX_KILLS, true);

	arg->rebuild_cb = NULL;
	arg->rebuild_post_cb = NULL;
}

static void
rebuild_fail_all_replicas_before_rebuild(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct daos_obj_layout *layout;
	struct daos_obj_shard *shard;

	if (!test_runable(arg, 6))
		return;

	oid = dts_oid_gen(DAOS_OC_R2S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);

	rebuild_io(arg, &oid, 1);

	daos_obj_layout_get(arg->coh, oid, &layout);

	/* HOLD rebuild ULT */
	daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC,
			     DAOS_REBUILD_HANG | DAOS_FAIL_VALUE,
			     NULL);

	/* Kill one replica and start rebuild */
	shard = layout->ol_shards[0];
	daos_kill_server(arg, arg->pool.pool_uuid, arg->group, &arg->pool.svc,
			 shard->os_ranks[0]);
	daos_exclude_server(arg->pool.pool_uuid, arg->group, &arg->pool.svc,
			    shard->os_ranks[0]);

	/* Sleep 10 seconds after it scan finish and hang before rebuild */
	print_message("sleep 10 seconds to wait scan to be finished \n");
	sleep(10);

	/* Then kill rank 1 */
	daos_kill_server(arg, arg->pool.pool_uuid, arg->group, &arg->pool.svc,
			 shard->os_ranks[1]);
	daos_exclude_server(arg->pool.pool_uuid, arg->group, &arg->pool.svc,
			    shard->os_ranks[1]);

	/* Continue rebuild */
	daos_mgmt_params_set(arg->group, -1, DSS_KEY_FAIL_LOC, 0, NULL);

	sleep(5);
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_obj_layout_free(layout);
}

static void
rebuild_fail_all_replicas(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct daos_obj_layout *layout;
	int		i;

	/* This test will kill 3 replicas, which might include the ranks
	 * in svcs, so make sure there are at least 6 ranks in svc, so
	 * the new leader can be chosen.
	 */
	if (!test_runable(arg, 6) || arg->pool.svc.rl_nr < 6) {
		print_message("need at least 6 svcs, -s5\n");
		return;
	}

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, ranks_to_kill[0]);

	rebuild_io(arg, &oid, 1);

	daos_obj_layout_get(arg->coh, oid, &layout);

	for (i = 0; i < layout->ol_nr; i++) {
		int j;

		for (j = 0; j < layout->ol_shards[i]->os_replica_nr; j++) {
			d_rank_t rank = layout->ol_shards[i]->os_ranks[j];

			daos_kill_server(arg, arg->pool.pool_uuid,
					 arg->group, &arg->pool.svc, rank);
		}

		for (j = 0; j < layout->ol_shards[i]->os_replica_nr; j++) {
			d_rank_t rank = layout->ol_shards[i]->os_ranks[j];

			daos_exclude_server(arg->pool.pool_uuid, arg->group,
					    &arg->pool.svc, rank);
		}
	}

	sleep(5);
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);

	MPI_Barrier(MPI_COMM_WORLD);
	daos_obj_layout_free(layout);
}

static void
multi_pools_rebuild_concurrently(void **state)
{
#define POOL_NUM		6
#define CONT_PER_POOL		4
#define OBJ_PER_CONT		256
	test_arg_t		*arg = *state;
	test_arg_t		*args[POOL_NUM * CONT_PER_POOL];
	daos_obj_id_t		oids[OBJ_PER_CONT];
	struct test_pool	*pool;
	int			i;
	int			rc;

	if (!test_runable(arg, 6))
		return;

	memset(args, 0, sizeof(args[0]) * POOL_NUM * CONT_PER_POOL);
	for (i = 0; i < POOL_NUM * CONT_PER_POOL; i++) {
		pool = (i % CONT_PER_POOL == 0) ? NULL :
				&args[(i/CONT_PER_POOL) * CONT_PER_POOL]->pool;
		rc = test_setup((void **)&args[i], SETUP_CONT_CONNECT,
				arg->multi_rank, DEFAULT_POOL_SIZE, pool);
		if (rc) {
			print_message("open/connect another pool failed: "
				      "rc %d\n", rc);
			return;
		}
		if (i % CONT_PER_POOL == 0)
			assert_int_equal(args[i]->pool.slave, 0);
		else
			assert_int_equal(args[i]->pool.slave, 1);
	}

	for (i = 0; i < OBJ_PER_CONT; i++) {
		oids[i] = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
		oids[i] = dts_oid_set_rank(oids[i], ranks_to_kill[0]);
	}

	for (i = 0; i < POOL_NUM * CONT_PER_POOL; i++)
		rebuild_io(args[i], oids, OBJ_PER_CONT);

	rebuild_pools_targets(args, POOL_NUM * CONT_PER_POOL, ranks_to_kill, 1);

	for (i = POOL_NUM * CONT_PER_POOL - 1; i >= 0; i--) {
		rebuild_io_validate(args[i], oids, OBJ_PER_CONT, true);
		test_teardown((void **)&args[i]);
	}
}

/** create a new pool/container for each test */
static const struct CMUnitTest rebuild_tests[] = {
	{"REBUILD1: rebuild small rec mulitple dkeys",
	 rebuild_dkeys, NULL, test_case_teardown},
	{"REBUILD2: rebuild small rec multiple akeys",
	 rebuild_akeys, NULL, test_case_teardown},
	{"REBUILD3: rebuild small rec multiple indexes",
	 rebuild_indexes, NULL, test_case_teardown},
	{"REBUILD4: rebuild small rec multiple keys/indexes",
	 rebuild_multiple, NULL, test_case_teardown},
	{"REBUILD5: rebuild large rec single index",
	 rebuild_large_rec, NULL, test_case_teardown},
	{"REBUILD6: rebuild multiple objects",
	 rebuild_objects, NULL, test_case_teardown},
	{"REBUILD7: drop rebuild scan reply",
	rebuild_drop_scan, NULL, test_case_teardown},
	{"REBUILD8: retry rebuild for not ready",
	rebuild_retry_rebuild, NULL, test_case_teardown},
	{"REBUILD9: drop rebuild obj reply",
	rebuild_drop_obj, NULL, test_case_teardown},
	{"REBUILD10: rebuild multiple pools",
	rebuild_multiple_pools, NULL, test_case_teardown},
	{"REBUILD11: rebuild update failed",
	rebuild_update_failed, NULL, test_case_teardown},
	{"REBUILD12: retry rebuild for pool stale",
	rebuild_retry_for_stale_pool, NULL, test_case_teardown},
	{"REBUILD13: rebuild with container destroy",
	rebuild_destroy_container, NULL, test_case_teardown},
	{"REBUILD14: rebuild with container close",
	rebuild_close_container, NULL, test_case_teardown},
	{"REBUILD15: rebuild with pool destroy during scan",
	rebuild_destroy_pool_during_scan, NULL, test_case_teardown},
	{"REBUILD16: rebuild with pool destroy during rebuild",
	rebuild_destroy_pool_during_rebuild, NULL, test_case_teardown},
	{"REBUILD17: rebuild iv tgt fail",
	rebuild_iv_tgt_fail, NULL, test_case_teardown},
	{"REBUILD18: rebuild tgt start fail",
	rebuild_tgt_start_fail, NULL, test_case_teardown},
	{"REBUILD19: rebuild send objects failed",
	 rebuild_send_objects_fail, NULL, test_case_teardown},
	{"REBUILD20: rebuild with master change during scan",
	rebuild_master_change_during_scan, NULL, test_case_teardown},
	{"REBUILD21: rebuild with master change during rebuild",
	rebuild_master_change_during_rebuild, NULL, test_case_teardown},
	{"REBUILD22: rebuild no space failure",
	rebuild_nospace, NULL, test_case_teardown},
	{"REBUILD23: rebuild multiple tgts",
	rebuild_multiple_tgts, NULL, test_case_teardown},
	{"REBUILD24: disconnect pool during scan",
	 rebuild_tgt_pool_disconnect_in_scan, NULL, test_case_teardown},
	{"REBUILD25: disconnect pool during rebuild",
	 rebuild_tgt_pool_disconnect_in_rebuild, NULL, test_case_teardown},
	{"REBUILD26: connect pool during scan for offline rebuild",
	 rebuild_offline_pool_connect_in_scan, NULL, test_case_teardown},
	{"REBUILD27: connect pool during rebuild for offline rebuild",
	 rebuild_offline_pool_connect_in_rebuild, NULL, test_case_teardown},
	{"REBUILD28: offline rebuild",
	rebuild_offline, NULL, test_case_teardown},
	{"REBUILD29: rebuild with master failure",
	 rebuild_master_failure, NULL, test_case_teardown},
	{"REBUILD30: rebuild with two failures",
	 rebuild_multiple_failures, NULL, test_case_teardown},
	{"REBUILD31: rebuild fail all replicas before rebuild",
	 rebuild_fail_all_replicas_before_rebuild, NULL, test_case_teardown},
	{"REBUILD32: rebuild fail all replicas",
	 rebuild_fail_all_replicas, NULL, test_case_teardown},
	{"REBUILD33: multi-pools rebuild concurrently",
	 multi_pools_rebuild_concurrently, NULL, test_case_teardown},
};

#define REBUILD_POOL_SIZE	(10ULL << 30)
int
rebuild_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, REBUILD_POOL_SIZE,
			  NULL);
}

int
run_daos_rebuild_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(rebuild_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests(rebuild_tests, ARRAY_SIZE(rebuild_tests),
				REBUILD_POOL_SIZE, sub_tests, sub_tests_size,
				NULL, NULL);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
