
//==========================================================
// Includes.
//

#include <transaction.h>

#include <asm-generic/errno-base.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>
#ifndef __aarch64__
#include <xmmintrin.h>
#endif

#include <aerospike/aerospike_batch.h>
#include <aerospike/aerospike_key.h>
#include <citrusleaf/cf_clock.h>

#include <benchmark.h>
#include <common.h>
#include <coordinator.h>
#include <workload.h>


//==========================================================
// Typedefs & constants.
//

struct async_data_s;

typedef struct async_data_s {
	tdata_t* tdata;
	cdata_t* cdata;
	thr_coord_t* coord;
	stage_t* stage;

	// keep each async_data in the same event loop to prevent the possibility
	// of overflowing an event loop due to bad scheduling
	as_event_loop* ev_loop;

	void (*workload_cb)(struct async_data_s* adata);

	// the time at which the async call was made
	uint64_t start_time;

	// the key to be used in the async calls
	as_key key;

	// what type of operation is being performed
	enum {
		read_op,
		write_op,
		delete_op,
		udf_op
	} op;

	pthread_mutex_t done_lock;
} async_data_t;


//==========================================================
// Forward Declarations.
//

// Random number helper methods
LOCAL_HELPER uint32_t _pct_to_fp(float pct);
LOCAL_HELPER uint32_t _random_fp(as_random*);

// Latency recrding helpers
LOCAL_HELPER void _record_read(cdata_t* cdata, uint64_t dt_us);
LOCAL_HELPER void _record_write(cdata_t* cdata, uint64_t dt_us);
LOCAL_HELPER void _record_udf(cdata_t* cdata, uint64_t dt_us);

// Read/Write singular/batch synchronous operations
LOCAL_HELPER int _write_record_sync(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, as_key* key, as_record* rec);
LOCAL_HELPER int _read_record_sync(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, as_key* key);
LOCAL_HELPER int _batch_read_record_sync(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, as_batch_read_records* records);
LOCAL_HELPER int _apply_udf_sync(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, as_key* key);

// Read/Write singular/batch asynchronous operations
LOCAL_HELPER int _write_record_async(as_key* key, as_record* rec,
		async_data_t* adata, tdata_t* tdata, cdata_t* cdata);
LOCAL_HELPER int _read_record_async(as_key* key, async_data_t* adata,
		tdata_t* tdata, cdata_t* cdata, const stage_t* stage);
LOCAL_HELPER int _batch_read_record_async(as_batch_read_records* keys,
		async_data_t* adata, tdata_t* tdata, cdata_t* cdata);
LOCAL_HELPER int _apply_udf_async(as_key* key, async_data_t* adata,
		tdata_t* tdata, cdata_t* cdata, const stage_t* stage);

// Thread worker helper methods
LOCAL_HELPER void _calculate_subrange(uint64_t key_start, uint64_t key_end,
		uint32_t t_idx, uint32_t n_threads, uint64_t* t_start, uint64_t* t_end);
LOCAL_HELPER void _gen_key(uint64_t key_val, as_key* key, const cdata_t* cdata);
LOCAL_HELPER as_record* _gen_record(as_random* random, const cdata_t* cdata,
		tdata_t* tdata, const stage_t* stage);
LOCAL_HELPER as_record* _gen_nil_record(tdata_t* tdata);
LOCAL_HELPER void _destroy_record(as_record* rec, const stage_t* stage);
LOCAL_HELPER void throttle(tdata_t* tdata, thr_coord_t* coord);

// Synchronous workload helper methods
LOCAL_HELPER void random_read(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage);
LOCAL_HELPER void random_write(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage);
LOCAL_HELPER void random_udf(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage);
LOCAL_HELPER void random_delete(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage);

// Synchronous workload methods
LOCAL_HELPER void linear_writes(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage);
LOCAL_HELPER void random_read_write(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage);
LOCAL_HELPER void random_read_write_udf(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage);
LOCAL_HELPER void linear_deletes(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage);
LOCAL_HELPER void random_read_write_delete(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage);

// Asynchronous workload helper methods
LOCAL_HELPER void random_read_async(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage, async_data_t* adata);
LOCAL_HELPER void random_write_async(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage, async_data_t* adata);
LOCAL_HELPER void random_udf_async(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage, async_data_t* adata);
LOCAL_HELPER void random_delete_async(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage, async_data_t* adata);

// Asynchronous workload methods
LOCAL_HELPER void _async_listener(as_error* err, void* udata,
		as_event_loop* event_loop);
LOCAL_HELPER void _async_read_listener(as_error* err, as_record* rec, void* udata,
		as_event_loop* event_loop);
LOCAL_HELPER void _async_write_listener(as_error* err, void* udata,
		as_event_loop* event_loop);
LOCAL_HELPER void _async_batch_read_listener(as_error* err,
		as_batch_read_records* records, void* udata, as_event_loop* event_loop);
LOCAL_HELPER void _async_val_listener(as_error* err, as_val* val, void* udata,
		as_event_loop* event_loop);
LOCAL_HELPER void linear_writes_async(async_data_t* adata);
LOCAL_HELPER void random_read_write_async(async_data_t* adata);
LOCAL_HELPER void random_read_write_udf_async(async_data_t* adata);
LOCAL_HELPER void linear_deletes_async(async_data_t* adata);
LOCAL_HELPER void random_read_write_delete_async(async_data_t* adata);

// Main worker thread loop
LOCAL_HELPER void do_sync_workload(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, stage_t* stage);
LOCAL_HELPER void do_async_workload(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, stage_t* stage);
LOCAL_HELPER void async_workload_init_coord(thr_coord_t* coord,
		const stage_t* stage);
LOCAL_HELPER void async_workload_start_workers(async_data_t** worker_datas,
		tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord, stage_t* stage);
LOCAL_HELPER bool async_workload_is_done(async_data_t** worker_datas,
		uint32_t n_workers, uint64_t tps);
LOCAL_HELPER void async_workload_distribute(cf_clock delta_time,
		double* prior_contig_unworked, uint64_t tps, tdata_t* tdata);
LOCAL_HELPER void async_workload_notify_coord_done(thr_coord_t* coord,
		const stage_t* stage);
LOCAL_HELPER void init_stage(const cdata_t* cdata, tdata_t* tdata,
		stage_t* stage);
LOCAL_HELPER void terminate_stage(const cdata_t* cdata, tdata_t* tdata,
		stage_t* stage);


//==========================================================
// Public API.
//

void*
transaction_worker(void* udata)
{
	tdata_t* tdata = (tdata_t*) udata;
	cdata_t* cdata = tdata->cdata;
	thr_coord_t* coord = tdata->coord;

	while (!tdata->finished) {
		uint32_t stage_idx = tdata->stage_idx;
		stage_t* stage = &cdata->stages.stages[stage_idx];

		init_stage(cdata, tdata, stage);

		if (stage->async) {
			do_async_workload(tdata, cdata, coord, stage);
		}
		else {
			do_sync_workload(tdata, cdata, coord, stage);
		}
		// check tdata->finished before locking
		if (tdata->finished) {
			break;
		}
		terminate_stage(cdata, tdata, stage);
		thr_coordinator_wait(coord);
	}

	return NULL;
}


//==========================================================
// Local helpers.
//

/******************************************************************************
 * Random number helper methods
 *****************************************************************************/

LOCAL_HELPER uint32_t
_pct_to_fp(float pct)
{
	// multiply pct by 2**24 before dividing by 100 and casting to an int,
	// since floats have 24 bits of precision including the leading 1,
	// so that read_pct is pct% between 0 and 2**24
	return (uint32_t) ((0x01000000 * pct) / 100);
}

LOCAL_HELPER uint32_t
_random_fp(as_random* random)
{
	uint32_t die = as_random_next_uint32(random);
	// floats have 24 bits of precision (including implicit leading 1)
	return die & 0x00ffffff;
}

/******************************************************************************
 * Latency recording helpers
 *****************************************************************************/

LOCAL_HELPER void
_record_read(cdata_t* cdata, uint64_t dt_us)
{
	if (cdata->latency) {
		hdr_record_value_atomic(cdata->read_hdr, dt_us);
	}
	if (cdata->histogram_output != NULL || cdata->hdr_comp_read_output != NULL) {
		histogram_incr(&cdata->read_histogram, dt_us);
	}
	cdata->read_hit_count++;
}

LOCAL_HELPER void
_record_write(cdata_t* cdata, uint64_t dt_us)
{
	if (cdata->latency) {
		hdr_record_value_atomic(cdata->write_hdr, dt_us);
	}
	if (cdata->histogram_output != NULL || cdata->hdr_comp_write_output != NULL) {
		histogram_incr(&cdata->write_histogram, dt_us);
	}
	cdata->write_count++;
}

LOCAL_HELPER void
_record_udf(cdata_t* cdata, uint64_t dt_us)
{
	if (cdata->latency) {
		hdr_record_value_atomic(cdata->udf_hdr, dt_us);
	}
	if (cdata->histogram_output != NULL || cdata->hdr_comp_udf_output != NULL) {
		histogram_incr(&cdata->udf_histogram, dt_us);
	}
	cdata->udf_count++;
}


/******************************************************************************
 * Read/Write singular/batch synchronous operations
 *****************************************************************************/

LOCAL_HELPER int
_write_record_sync(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		as_key* key, as_record* rec)
{
	as_status status;
	as_error err;

	uint64_t start = cf_getus();
	status = aerospike_key_put(&cdata->client, &err, &tdata->policies.write, key, rec);
	uint64_t end = cf_getus();

	if (status == AEROSPIKE_OK) {
		_record_write(cdata, end - start);
		throttle(tdata, coord);
		return 0;
	}

	// Handle error conditions.
	if (status == AEROSPIKE_ERR_TIMEOUT) {
		cdata->write_timeout_count++;
	}
	else {
		cdata->write_error_count++;

		if (cdata->debug) {
			blog_error("Write error: ns=%s set=%s key=%d bin=%s code=%d "
					"message=%s",
					cdata->namespace, cdata->set, key, cdata->bin_name, status,
					err.message);
		}
	}
	throttle(tdata, coord);
	return -1;
}

LOCAL_HELPER int
_read_record_sync(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, as_key* key)
{
	as_record* rec = NULL;
	as_status status;
	as_error err;

	uint64_t start, end;
	if (stage->read_bins) {
		start = cf_getus();
		status = aerospike_key_select(&cdata->client, &err, &tdata->policies.read,
				key, (const char**) stage->read_bins, &rec);
		end = cf_getus();
	}
	else {
		start = cf_getus();
		status = aerospike_key_get(&cdata->client, &err, &tdata->policies.read,
				key, &rec);
		end = cf_getus();
	}

	if (status == AEROSPIKE_OK) {
		_record_read(cdata, end - start);
		as_record_destroy(rec);
		throttle(tdata, coord);
		return status;
	}

	// Handle error conditions.
	if (status == AEROSPIKE_ERR_RECORD_NOT_FOUND) {
		cdata->read_miss_count++;
	}
	else if (status == AEROSPIKE_ERR_TIMEOUT) {
		cdata->read_timeout_count++;
	}
	else {
		cdata->read_error_count++;

		if (cdata->debug) {
			blog_error("Read error: ns=%s set=%s key=%d bin=%s code=%d "
					"message=%s",
					cdata->namespace, cdata->set, key->value.integer.value,
					cdata->bin_name, status, err.message);
		}
	}

	as_record_destroy(rec);
	throttle(tdata, coord);
	return status;
}

LOCAL_HELPER int
_batch_read_record_sync(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, as_batch_read_records* records)
{
	as_status status;
	as_error err;

	uint64_t start = cf_getus();
	status = aerospike_batch_read(&cdata->client, &err, &tdata->policies.batch,
			records);
	uint64_t end = cf_getus();

	if (status == AEROSPIKE_OK) {
		_record_read(cdata, end - start);
		throttle(tdata, coord);
		return status;
	}

	// Handle error conditions.
	if (status == AEROSPIKE_ERR_TIMEOUT) {
		cdata->read_timeout_count++;
	}
	else {
		cdata->read_error_count++;

		if (cdata->debug) {
			blog_error("Batch read error: ns=%s set=%s bin=%s code=%d "
					"message=%s",
					cdata->namespace, cdata->set, cdata->bin_name, status,
					err.message);
		}
	}

	throttle(tdata, coord);
	return status;
}

LOCAL_HELPER int
_apply_udf_sync(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, as_key* key)
{
	as_status status;
	as_error err;
	uint64_t start, end;
	as_val* val;

	as_list* args;
	if (stage->random) {
		val = obj_spec_gen_value(&stage->udf_fn_args, tdata->random, NULL, 0);
		args = as_list_fromval(val);
		assert(args != NULL);
	}
	else {
		args = tdata->fixed_udf_fn_args;
	}

	start = cf_getus();
	status = aerospike_key_apply(&cdata->client, &err, &tdata->policies.apply, key,
			stage->udf_package_name, stage->udf_fn_name, args, &val);
	end = cf_getus();

	if (status == AEROSPIKE_OK || status == AEROSPIKE_ERR_RECORD_NOT_FOUND) {
		_record_udf(cdata, end - start);
		as_val_destroy(val);
		if (stage->random) {
			as_val_destroy((as_val*) args);
		}
		throttle(tdata, coord);
		return status;
	}

	// Handle error conditions.
	if (status == AEROSPIKE_ERR_TIMEOUT) {
		cdata->udf_timeout_count++;
	}
	else {
		cdata->udf_error_count++;

		if (cdata->debug) {
			blog_error("UDF error: ns=%s set=%s key=%d bin=%s code=%d "
					"message=%s",
					cdata->namespace, cdata->set, key->value.integer.value,
					cdata->bin_name, status, err.message);
		}
	}

	as_val_destroy(val);
	if (stage->random) {
		as_val_destroy((as_val*) args);
	}
	throttle(tdata, coord);
	return status;
}


/******************************************************************************
 * Read/Write singular/batch asynchronous operations
 *****************************************************************************/

LOCAL_HELPER int
_write_record_async(as_key* key, as_record* rec, async_data_t* adata,
		tdata_t* tdata, cdata_t* cdata)
{
	as_status status;
	as_error err;

	adata->start_time = cf_getus();
	status = aerospike_key_put_async(&cdata->client, &err, &tdata->policies.write,
			key, rec, _async_write_listener, adata, adata->ev_loop, NULL);

	if (status != AEROSPIKE_OK) {
		// if the async call failed for any reason, call the callback directly
		_async_write_listener(&err, adata, adata->ev_loop);
	}

	return status;
}

LOCAL_HELPER int
_read_record_async(as_key* key, async_data_t* adata, tdata_t* tdata,
		cdata_t* cdata, const stage_t* stage)
{
	as_status status;
	as_error err;

	if (stage->read_bins) {
		adata->start_time = cf_getus();
		status = aerospike_key_select_async(&cdata->client, &err,
				&tdata->policies.read, key, (const char**) stage->read_bins,
				_async_read_listener, adata, adata->ev_loop, NULL);
	}
	else {
		adata->start_time = cf_getus();
		status = aerospike_key_get_async(&cdata->client, &err,
				&tdata->policies.read, key, _async_read_listener, adata,
				adata->ev_loop, NULL);
	}

	if (status != AEROSPIKE_OK) {
		// if the async call failed for any reason, call the callback directly
		_async_read_listener(&err, NULL, adata, adata->ev_loop);
	}

	return status;
}

LOCAL_HELPER int
_batch_read_record_async(as_batch_read_records* keys, async_data_t* adata,
		tdata_t* tdata, cdata_t* cdata)
{
	as_status status;
	as_error err;

	adata->start_time = cf_getus();
	status = aerospike_batch_read_async(&cdata->client, &err,
			&tdata->policies.batch, keys, _async_batch_read_listener, adata,
			adata->ev_loop);

	if (status != AEROSPIKE_OK) {
		// if the async call failed for any reason, call the callback directly
		_async_batch_read_listener(&err, NULL, adata, adata->ev_loop);
	}

	return status;
}

LOCAL_HELPER int
_apply_udf_async(as_key* key, async_data_t* adata, tdata_t* tdata,
		cdata_t* cdata, const stage_t* stage)
{
	as_status status;
	as_error err;

	as_list* args;
	if (stage->random) {
		as_val* val = obj_spec_gen_value(&stage->udf_fn_args, tdata->random, NULL, 0);
		args = as_list_fromval(val);
		assert(args != NULL);
	}
	else {
		args = tdata->fixed_udf_fn_args;
	}

	adata->start_time = cf_getus();
	status = aerospike_key_apply_async(&cdata->client, &err, &tdata->policies.apply,
			key, stage->udf_package_name, stage->udf_fn_name, args,
			_async_val_listener, adata, adata->ev_loop, NULL);

	if (stage->random) {
		as_val_destroy((as_val*) args);
	}

	if (status != AEROSPIKE_OK) {
		// if the async call failed for any reason, call the callback directly
		_async_read_listener(&err, NULL, adata, adata->ev_loop);
	}

	return status;
}


/******************************************************************************
 * Thread worker helper methods
 *****************************************************************************/

/*
 * calculates the subrange of keys that the given thread should operate on,
 * which is done by evenly dividing the interval into n_threads intervals
 */
LOCAL_HELPER void
_calculate_subrange(uint64_t key_start, uint64_t key_end, uint32_t t_idx,
		uint32_t n_threads, uint64_t* t_start, uint64_t* t_end)
{
	uint64_t n_keys = key_end - key_start;
	*t_start = key_start + ((n_keys * t_idx) / n_threads);
	*t_end   = key_start + ((n_keys * (t_idx + 1)) / n_threads);
}

LOCAL_HELPER void
_gen_key(uint64_t key_val, as_key* key, const cdata_t* cdata)
{
	as_key_init_int64(key, cdata->namespace, cdata->set, key_val);
}

/*
 * generates a record with given key following the obj_spec in cdata
 */
LOCAL_HELPER as_record*
_gen_record(as_random* random, const cdata_t* cdata, tdata_t* tdata,
		const stage_t* stage)
{
	as_record* rec;
	uint32_t write_all_pct = _pct_to_fp(stage->workload.write_all_pct);
	uint32_t die = _random_fp(tdata->random);

	if (die < write_all_pct) {
		if (stage->random) {
			uint32_t n_objs = obj_spec_n_bins(&stage->obj_spec);
			rec = as_record_new(n_objs);

			obj_spec_populate_bins(&stage->obj_spec, rec, random,
					cdata->bin_name, NULL, 0, cdata->compression_ratio);
			rec->ttl = stage->ttl;
		}
		else {
			rec = &tdata->fixed_full_record;
		}
	}
	else {
		if (stage->random) {
			rec = as_record_new(stage->n_write_bins);

			obj_spec_populate_bins(&stage->obj_spec, rec, random,
					cdata->bin_name, stage->write_bins, stage->n_write_bins,
					cdata->compression_ratio);
			rec->ttl = stage->ttl;
		}
		else {
			rec = &tdata->fixed_partial_record;
		}
	}
	return rec;
}

/*
 * generates a record with all nil bins (used to remove records)
 */
LOCAL_HELPER as_record*
_gen_nil_record(tdata_t* tdata)
{
	return &tdata->fixed_delete_record;
}

LOCAL_HELPER void
_destroy_record(as_record* rec, const stage_t* stage)
{
	// don't destroy the records if the workload isn't randomized
	if (stage->random) {
		as_record_destroy(rec);
	}
}

/*
 * throttler to be called between every transaction
 */
LOCAL_HELPER void
throttle(tdata_t* tdata, thr_coord_t* coord)
{
	struct timespec wake_up;

	if (tdata->dyn_throttle.target_period != 0) {
		clock_gettime(COORD_CLOCK, &wake_up);

		uint64_t pause_for = dyn_throttle_pause_for(&tdata->dyn_throttle,
				timespec_to_us(&wake_up));
		timespec_add_us(&wake_up, pause_for);
		thr_coordinator_sleep(coord, &wake_up);
	}
}


/******************************************************************************
 * Synchronous workload helper methods
 *****************************************************************************/

LOCAL_HELPER void
random_read(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage)
{
	as_key key;
	uint32_t batch_size = stage->batch_size;

	if (batch_size <= 1) {
		// generate a random key
		uint64_t key_val = stage_gen_random_key(stage, tdata->random);
		_gen_key(key_val, &key, cdata);

		_read_record_sync(tdata, cdata, coord, stage, &key);
		as_key_destroy(&key);
	}
	else {
		// generate a batch of random keys
		as_batch_read_records* keys = as_batch_read_create(batch_size);
		for (uint32_t i = 0; i < batch_size; i++) {
			uint64_t key_val =
				stage_gen_random_key(stage, tdata->random);
			as_batch_read_record* key = as_batch_read_reserve(keys);
			_gen_key(key_val, &key->key, cdata);
			if (stage->read_bins) {
				key->read_all_bins = false;
				key->bin_names = stage->read_bins;
				key->n_bin_names = stage->n_read_bins;
			}
			else {
				key->read_all_bins = true;
			}
		}

		_batch_read_record_sync(tdata, cdata, coord, keys);
		as_batch_read_destroy(keys);
	}
}

LOCAL_HELPER void
random_write(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage)
{
	as_key key;
	as_record* rec;

	// generate a random key
	uint64_t key_val = stage_gen_random_key(stage, tdata->random);
	_gen_key(key_val, &key, cdata);

	// create a record
	rec = _gen_record(tdata->random, cdata, tdata, stage);

	// write this record to the database
	_write_record_sync(tdata, cdata, coord, &key, rec);

	_destroy_record(rec, stage);
	as_key_destroy(&key);
}

LOCAL_HELPER void
random_udf(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage)
{
	as_key key;

	// generate a random key
	uint64_t key_val = stage_gen_random_key(stage, tdata->random);
	_gen_key(key_val, &key, cdata);

	_apply_udf_sync(tdata, cdata, coord, stage, &key);
	as_key_destroy(&key);
}

LOCAL_HELPER void
random_delete(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage)
{
	as_key key;
	as_record* rec;

	// generate a random key
	uint64_t key_val = stage_gen_random_key(stage, tdata->random);
	_gen_key(key_val, &key, cdata);

	// create a record
	rec = _gen_nil_record(tdata);

	// write this record to the database
	_write_record_sync(tdata, cdata, coord, &key, rec);

	// don't destroy delete records
	as_key_destroy(&key);
}


/******************************************************************************
 * Synchronous workload methods
 *****************************************************************************/

/*
 * TODO add work queue that threads can pull batches of keys from, rather than
 * having each thread take a predefined segment of the set of all keys
 */
LOCAL_HELPER void
linear_writes(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage)
{
	uint32_t t_idx = tdata->t_idx;
	uint64_t start_key, end_key;
	uint64_t key_val;

	as_key key;
	as_record* rec;

	// each worker thread takes a subrange of the total set of keys being
	// inserted, all approximately equal in size
	_calculate_subrange(stage->key_start, stage->key_end, t_idx,
			cdata->transaction_worker_threads, &start_key, &end_key);

	key_val = start_key;
	while (tdata->do_work &&
			key_val < end_key) {

		// create a record with given key
		_gen_key(key_val, &key, cdata);
		rec = _gen_record(tdata->random, cdata, tdata, stage);

		// write this record to the database
		_write_record_sync(tdata, cdata, coord, &key, rec);

		_destroy_record(rec, stage);
		as_key_destroy(&key);

		key_val++;
	}

	// once we've written everything, there's nothing left to do, so tell
	// coord we're done and exit
	thr_coordinator_complete(coord);
}

LOCAL_HELPER void
random_read_write(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage)
{
	uint32_t read_pct = _pct_to_fp(stage->workload.read_pct);

	// since there is no specific target number of transactions required before
	// the stage is finished, only a timeout, tell the coordinator we are ready
	// to finish as soon as the timer runs out
	thr_coordinator_complete(coord);

	while (tdata->do_work) {
		// roll the die
		uint32_t die = _random_fp(tdata->random);

		if (die < read_pct) {
			random_read(tdata, cdata, coord, stage);
		}
		else {
			random_write(tdata, cdata, coord, stage);
		}
	}
}

LOCAL_HELPER void
random_read_write_udf(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage)
{
	uint32_t read_pct = _pct_to_fp(stage->workload.read_pct);
	uint32_t write_pct = _pct_to_fp(stage->workload.write_pct);

	// store the cumulative probability in write_pct
	write_pct = read_pct + write_pct;

	// since there is no specific target number of transactions required before
	// the stage is finished, only a timeout, tell the coordinator we are ready
	// to finish as soon as the timer runs out
	thr_coordinator_complete(coord);

	while (tdata->do_work) {
		// roll the die
		uint32_t die = _random_fp(tdata->random);

		if (die < read_pct) {
			random_read(tdata, cdata, coord, stage);
		}
		else if (die < write_pct) {
			random_write(tdata, cdata, coord, stage);
		}
		else {
			random_udf(tdata, cdata, coord, stage);
		}
	}
}

LOCAL_HELPER void
linear_deletes(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage)
{
	uint32_t t_idx = tdata->t_idx;
	uint64_t start_key, end_key;
	uint64_t key_val;

	as_key key;
	as_record* rec;

	// each worker thread takes a subrange of the total set of keys being
	// inserted, all approximately equal in size
	_calculate_subrange(stage->key_start, stage->key_end, t_idx,
			cdata->transaction_worker_threads, &start_key, &end_key);

	key_val = start_key;
	while (tdata->do_work &&
			key_val < end_key) {

		// create a record with given key
		_gen_key(key_val, &key, cdata);
		rec = _gen_nil_record(tdata);

		// delete this record from the database
		_write_record_sync(tdata, cdata, coord, &key, rec);

		_destroy_record(rec, stage);
		as_key_destroy(&key);

		key_val++;
	}

	// once we've written everything, there's nothing left to do, so tell
	// coord we're done and exit
	thr_coordinator_complete(coord);
}

LOCAL_HELPER void
random_read_write_delete(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage)
{
	uint32_t read_pct = _pct_to_fp(stage->workload.read_pct);
	uint32_t write_pct = _pct_to_fp(stage->workload.write_pct);

	// store the cumulative probability in write_pct
	write_pct = read_pct + write_pct;

	// since there is no specific target number of transactions required before
	// the stage is finished, only a timeout, tell the coordinator we are ready
	// to finish as soon as the timer runs out
	thr_coordinator_complete(coord);

	while (tdata->do_work) {
		// roll the die
		uint32_t die = _random_fp(tdata->random);

		if (die < read_pct) {
			random_read(tdata, cdata, coord, stage);
		}
		else if (die < write_pct) {
			random_write(tdata, cdata, coord, stage);
		}
		else {
			random_delete(tdata, cdata, coord, stage);
		}
	}
}

/******************************************************************************
 * Asynchronous workload helper methods
 *****************************************************************************/

LOCAL_HELPER void
random_read_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, async_data_t* adata)
{
	uint32_t batch_size = stage->batch_size;

	adata->op = read_op;

	if (batch_size <= 1) {
		// generate a random key
		uint64_t key_val = stage_gen_random_key(stage, tdata->random);

		_gen_key(key_val, &adata->key, cdata);
		_read_record_async(&adata->key, adata, tdata, cdata, stage);
	}
	else {
		// generate a batch of random keys
		as_batch_read_records* keys = as_batch_read_create(batch_size);
		for (uint32_t i = 0; i < batch_size; i++) {
			uint64_t key_val =
				stage_gen_random_key(stage, tdata->random);
			as_batch_read_record* key = as_batch_read_reserve(keys);
			_gen_key(key_val, &key->key, cdata);
			if (stage->read_bins) {
				key->read_all_bins = false;
				key->bin_names = stage->read_bins;
				key->n_bin_names = stage->n_read_bins;
			}
			else {
				key->read_all_bins = true;
			}
		}

		_batch_read_record_async(keys, adata, tdata, cdata);
	}
}

LOCAL_HELPER void
random_write_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, async_data_t* adata)
{
	as_record* rec;

	// generate a random key
	uint64_t key_val = stage_gen_random_key(stage, tdata->random);

	_gen_key(key_val, &adata->key, cdata);
	rec = _gen_record(tdata->random, cdata, tdata, stage);
	adata->op = write_op;

	_write_record_async(&adata->key, rec, adata, tdata, cdata);

	_destroy_record(rec, stage);
}

LOCAL_HELPER void
random_udf_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, async_data_t* adata)
{
	// generate a random key
	uint64_t key_val = stage_gen_random_key(stage, tdata->random);
	_gen_key(key_val, &adata->key, cdata);
	adata->op = udf_op;

	_apply_udf_async(&adata->key, adata, tdata, cdata, stage);
}

LOCAL_HELPER void
random_delete_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, async_data_t* adata)
{
	as_record* rec;

	// generate a random key
	uint64_t key_val = stage_gen_random_key(stage, tdata->random);

	_gen_key(key_val, &adata->key, cdata);
	rec = _gen_nil_record(tdata);
	adata->op = write_op;

	_write_record_async(&adata->key, rec, adata, tdata, cdata);

	_destroy_record(rec, stage);
}


/******************************************************************************
 * Asynchronous workload methods
 *****************************************************************************/

LOCAL_HELPER void
_async_listener(as_error* err, void* udata, as_event_loop* event_loop)
{
	async_data_t* adata = (async_data_t*) udata;
	tdata_t* tdata = adata->tdata;
	cdata_t* cdata = adata->cdata;

	if (!err) {
		uint64_t end = cf_getus();
		if (adata->op == read_op) {
			_record_read(cdata, end - adata->start_time);
		}
		else if (adata->op == udf_op) {
			_record_udf(cdata, end - adata->start_time);
		}
		else {
			_record_write(cdata, end - adata->start_time);
		}

		// set the event loop (only effective the first time around but let's
		// avoid conditional logic)
		adata->ev_loop = event_loop;
	}
	else {
		if (err->code == AEROSPIKE_ERR_TIMEOUT) {
			if (adata->op == read_op) {
				cdata->read_timeout_count++;
			}
			else if (adata->op == udf_op) {
				cdata->udf_timeout_count++;
			}
			else {
				cdata->write_timeout_count++;
			}
		}
		else {
			if (adata->op == read_op) {
				cdata->read_error_count++;
			}
			else if (adata->op == udf_op) {
				cdata->udf_error_count++;
			}
			else {
				cdata->write_error_count++;
			}

			if (cdata->debug) {
				const static char* op_strs[] = {
					"Read",
					"Write",
					"Delete",
					"UDF"
				};

				blog_error("%s error: ns=%s set=%s key=%d bin=%s code=%d "
					"message=%s",
					op_strs[adata->op], cdata->namespace, cdata->set,
					adata->key.value.integer.value, cdata->bin_name,
					err->code, err->message);
			}
		}
	}

	int64_t v;

	while ((v = atomic_fetch_sub(&tdata->async_req_quota, 1)) <= 0) {
		atomic_fetch_add(&tdata->async_req_quota, 1);

		// Not immediately looping back to sub to avoid cache invalidation.
		while ((v = atomic_load(&tdata->async_req_quota)) <= 0) {
			usleep(tdata->min_usleep / 4);
		}
	}

	adata->workload_cb(adata);
}

LOCAL_HELPER void
_async_read_listener(as_error* err, as_record* rec, void* udata,
		as_event_loop* event_loop)
{
	_async_listener(err, udata, event_loop);
}

LOCAL_HELPER void
_async_write_listener(as_error* err, void* udata, as_event_loop* event_loop)
{
	_async_listener(err, udata, event_loop);
}

LOCAL_HELPER void
_async_batch_read_listener(as_error* err, as_batch_read_records* records,
		void* udata, as_event_loop* event_loop)
{
	_async_listener(err, udata, event_loop);
	if (records != NULL) {
		as_batch_read_destroy(records);
	}
}

LOCAL_HELPER void
_async_val_listener(as_error* err, as_val* val, void* udata,
		as_event_loop* event_loop)
{
	_async_listener(err, udata, event_loop);
	if (val != NULL) {
		as_val_destroy(val);
	}
}

LOCAL_HELPER void
linear_writes_async(async_data_t* adata)
{
	tdata_t* tdata = adata->tdata;
	cdata_t* cdata = adata->cdata;
	const stage_t* stage = adata->stage;

	uint64_t key_val;
	uint64_t end_key;
	key_val = atomic_fetch_add(&tdata->current_key, 1);
	end_key = stage->key_end;

	if (tdata->do_work && key_val < end_key) {
		adata->start_time = cf_getus();

		as_record* rec;
		_gen_key(key_val, &adata->key, cdata);
		rec = _gen_record(tdata->random, cdata, tdata, stage);
		adata->op = write_op;

		_write_record_async(&adata->key, rec, adata, tdata, cdata);

		_destroy_record(rec, stage);

		key_val++;
	}
	else {
		int rv;

		if ((rv = pthread_mutex_unlock(&adata->done_lock)) != 0) {
			blog_error("failed to unlock mutex - %d\n", rv);
			exit(-1);
		}
	}
}

LOCAL_HELPER void
random_read_write_async(async_data_t* adata)
{
	tdata_t* tdata = adata->tdata;
	cdata_t* cdata = adata->cdata;
	thr_coord_t* coord = adata->coord;
	const stage_t* stage = adata->stage;

	uint32_t read_pct = _pct_to_fp(stage->workload.read_pct);

	if (tdata->do_work) {
		adata->start_time = cf_getus();

		// roll the die
		uint32_t die = _random_fp(tdata->random);

		if (die < read_pct) {
			random_read_async(tdata, cdata, coord, stage, adata);
		}
		else {
			random_write_async(tdata, cdata, coord, stage, adata);
		}
	}
	else {
		int rv;

		if ((rv = pthread_mutex_unlock(&adata->done_lock)) != 0) {
			blog_error("failed to unlock mutex - %d\n", rv);
			exit(-1);
		}
	}
}

LOCAL_HELPER void
random_read_write_udf_async(async_data_t* adata)
{
	tdata_t* tdata = adata->tdata;
	cdata_t* cdata = adata->cdata;
	thr_coord_t* coord = adata->coord;
	const stage_t* stage = adata->stage;

	uint32_t read_pct = _pct_to_fp(stage->workload.read_pct);
	uint32_t write_pct = _pct_to_fp(stage->workload.write_pct);

	// store the cumulative probability in write_pct
	write_pct = read_pct + write_pct;

	if (tdata->do_work) {
		adata->start_time = cf_getus();

		// roll the die
		uint32_t die = _random_fp(tdata->random);

		if (die < read_pct) {
			random_read_async(tdata, cdata, coord, stage, adata);
		}
		else if (die < write_pct) {
			random_write_async(tdata, cdata, coord, stage, adata);
		}
		else {
			random_udf_async(tdata, cdata, coord, stage, adata);
		}
	}
	else {
		int rv;

		if ((rv = pthread_mutex_unlock(&adata->done_lock)) != 0) {
			blog_error("failed to unlock mutex - %d\n", rv);
			exit(-1);
		}
	}
}

LOCAL_HELPER void
linear_deletes_async(async_data_t* adata)
{
	tdata_t* tdata = adata->tdata;
	cdata_t* cdata = adata->cdata;
	const stage_t* stage = adata->stage;

	uint64_t key_val, end_key;

	key_val = atomic_fetch_add(&tdata->current_key, 1);
	end_key = stage->key_end;

	if (tdata->do_work && key_val < end_key) {
		adata->start_time = cf_getus();

		as_record* rec;
		_gen_key(key_val, &adata->key, cdata);
		rec = _gen_nil_record(tdata);
		adata->op = delete_op;

		_write_record_async(&adata->key, rec, adata, tdata, cdata);

		_destroy_record(rec, stage);

		key_val++;
	}
	else {
		int rv;

		if ((rv = pthread_mutex_unlock(&adata->done_lock)) != 0) {
			blog_error("failed to unlock mutex - %d\n", rv);
			exit(-1);
		}
	}
}

LOCAL_HELPER void
random_read_write_delete_async(async_data_t* adata)
{
	tdata_t* tdata = adata->tdata;
	cdata_t* cdata = adata->cdata;
	thr_coord_t* coord = adata->coord;
	const stage_t* stage = adata->stage;

	uint32_t read_pct = _pct_to_fp(stage->workload.read_pct);
	uint32_t write_pct = _pct_to_fp(stage->workload.write_pct);

	// store the cumulative probability in write_pct
	write_pct = read_pct + write_pct;

	if (tdata->do_work) {
		adata->start_time = cf_getus();

		// roll the die
		uint32_t die = _random_fp(tdata->random);

		if (die < read_pct) {
			random_read_async(tdata, cdata, coord, stage, adata);
		}
		else if (die < write_pct) {
			random_write_async(tdata, cdata, coord, stage, adata);
		}
		else {
			random_delete_async(tdata, cdata, coord, stage, adata);
		}
	}
	else {
		int rv;

		if ((rv = pthread_mutex_unlock(&adata->done_lock)) != 0) {
			blog_error("failed to unlock mutex - %d\n", rv);
			exit(-1);
		}
	}
}


/******************************************************************************
 * Main worker thread loop
 *****************************************************************************/

LOCAL_HELPER void
do_sync_workload(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		stage_t* stage)
{
	switch (stage->workload.type) {
		case WORKLOAD_TYPE_I:
			linear_writes(tdata, cdata, coord, stage);
			break;
		case WORKLOAD_TYPE_RU:
		case WORKLOAD_TYPE_RR:
			random_read_write(tdata, cdata, coord, stage);
			break;
		case WORKLOAD_TYPE_RUF:
			random_read_write_udf(tdata, cdata, coord, stage);
			break;
		case WORKLOAD_TYPE_D:
			linear_deletes(tdata, cdata, coord, stage);
			break;
		case WORKLOAD_TYPE_RUD:
			random_read_write_delete(tdata, cdata, coord, stage);
			break;
	}
}

LOCAL_HELPER void
do_async_workload(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		stage_t* stage)
{
	uint32_t t_idx = tdata->t_idx;

	// Thread 0 is designated to handle async calls, the rest can immediately
	// terminate.
	if (t_idx != 0) {
		thr_coordinator_complete(coord);
		return;
	}

	async_workload_init_coord(coord, stage);

	uint32_t n_workers = cdata->async_max_commands;
	async_data_t* adatas =
		(async_data_t*)cf_calloc(sizeof(async_data_t), n_workers);
	async_data_t* worker_datas[n_workers];

	for (uint32_t i = 0; i < n_workers; i++) {
		worker_datas[i] = &adatas[i];
	}

	uint64_t tps = stage->tps;

	// Microseconds per txn.
	uint64_t min_usleep = ((double)1.0 / (double)tps) * 1000000;

	min_usleep = min_usleep > 0 ? 1 : min_usleep;
	tdata->min_usleep = min_usleep;

	atomic_store(&tdata->async_req_quota, tps == 0 ? INT64_MAX : 0);

	async_workload_start_workers(worker_datas, tdata, cdata, coord, stage);

	// Reduction causes the loading of one min_usleep worth of txns.
	cf_clock start_time = cf_getns() - (min_usleep * 1000);
	double prior_contig_unworked = 0.0;

	while (! async_workload_is_done(worker_datas, n_workers, tps)) {
		cf_clock now_time = cf_getns();
		cf_clock delta_time = now_time - start_time;

		start_time = now_time;

		async_workload_distribute(delta_time, &prior_contig_unworked, tps,
			tdata);
		usleep(min_usleep); // wait for more work
	}

	async_workload_notify_coord_done(coord, stage);

	// free the async_data structs
	cf_free(adatas);
}

LOCAL_HELPER void
async_workload_init_coord(thr_coord_t* coord, const stage_t* stage)
{
	switch (stage->workload.type) {
	case WORKLOAD_TYPE_RU:
	case WORKLOAD_TYPE_RR:
	case WORKLOAD_TYPE_RUF:
	case WORKLOAD_TYPE_RUD:
		// No target num txns - tell coord this is reapable.
		thr_coordinator_complete(coord);
		break;
	default:
		break;
	}
}

LOCAL_HELPER void
async_workload_start_workers(async_data_t** worker_datas, tdata_t* tdata,
		cdata_t* cdata, thr_coord_t* coord, stage_t* stage)
{
	uint64_t n_workers = cdata->async_max_commands;

	for (uint32_t i = 0; i < n_workers; i++) {
		async_data_t* adata = worker_datas[i];

		adata->tdata = tdata;
		adata->cdata = cdata;
		adata->coord = coord;
		adata->stage = stage;
		adata->ev_loop = NULL;

		int rv;

		if ((rv = pthread_mutex_init(&adata->done_lock, NULL)) != 0) {
			blog_error("failed to initialize mutex - %d\n", rv);
			exit(-1);
		}

		if ((rv = pthread_mutex_lock(&adata->done_lock)) != 0) {
			blog_error("failed to lock mutex - %d\n", rv);
			exit(-1);
		}

		switch (stage->workload.type) {
		case WORKLOAD_TYPE_I:
			adata->workload_cb = linear_writes_async;
			linear_writes_async(adata);
			break;
		case WORKLOAD_TYPE_RU:
		case WORKLOAD_TYPE_RR:
			adata->workload_cb = random_read_write_async;
			random_read_write_async(adata);
			break;
		case WORKLOAD_TYPE_RUF:
			adata->workload_cb = random_read_write_udf_async;
			random_read_write_udf_async(adata);
			break;
		case WORKLOAD_TYPE_D:
			adata->workload_cb = linear_deletes_async;
			linear_deletes_async(adata);
			break;
		case WORKLOAD_TYPE_RUD:
			adata->workload_cb = random_read_write_delete_async;
			random_read_write_delete_async(adata);
			break;
		}
	}
}

LOCAL_HELPER bool
async_workload_is_done(async_data_t** worker_datas, uint32_t n_workers,
		uint64_t tps)
{
	while (true) {
		uint32_t worker_datas_write_i = 0;

		for (uint32_t i = 0; i < n_workers; i++) {
			int rv;
			async_data_t* adata = worker_datas[i];

			if (tps != 0) {
				if ((rv = pthread_mutex_trylock(&adata->done_lock)) != 0) {
					if (rv == EBUSY) {
						worker_datas[worker_datas_write_i++] = adata;
						continue;
					}

					blog_error("failed to lock mutex - %d\n", rv);
					exit(-1);
				}
			}
			else {
				if ((rv = pthread_mutex_lock(&adata->done_lock)) != 0) {
					blog_error("failed to lock mutex - %d\n", rv);
					exit(-1);
				}
			}

			if ((rv = pthread_mutex_unlock(&adata->done_lock)) != 0) {
				blog_error("failed to unlock mutex - %d\n", rv);
				exit(-1);
			}

			if ((rv = pthread_mutex_destroy(&adata->done_lock)) != 0) {
				blog_error("failed to destroy mutex - %d\n", rv);
				exit(-1);
			}
		}

		// If any worker finished then we are done with transactions.
		if (worker_datas_write_i != n_workers) {
			if (worker_datas_write_i == 0) {
				return true;
			}
			// Else - done but need to wait for other workers to finish.

			n_workers = worker_datas_write_i;
			continue;
		}

		break; // not done
	}

	return false;
}

LOCAL_HELPER void
async_workload_distribute(cf_clock delta_time, double* prior_contig_unworked,
		uint64_t tps, tdata_t* tdata)
{
	double delta_sec = (double)delta_time / (1000000000) /*nsec per sec*/;
	double contig_work = (delta_sec * tps) + *prior_contig_unworked;
	uint64_t discreet_work = (uint64_t)contig_work;

	if (discreet_work > 0) {
		// Accumulate the fraction of work missed by discreet_work.
		*prior_contig_unworked = contig_work - discreet_work;

		atomic_fetch_add(&tdata->async_req_quota, discreet_work);
	}
	else {
		*prior_contig_unworked = contig_work;
	}
}

LOCAL_HELPER void
async_workload_notify_coord_done(thr_coord_t* coord, const stage_t* stage)
{
	switch (stage->workload.type) {
	case WORKLOAD_TYPE_I:
	case WORKLOAD_TYPE_D:
		// once we've written everything, there's nothing left to do, so tell
		// coord we're done and exit
		thr_coordinator_complete(coord);
		break;
	default:
		break;
	}
}

LOCAL_HELPER void
init_stage(const cdata_t* cdata, tdata_t* tdata, stage_t* stage)
{
	if (stage->workload.type == WORKLOAD_TYPE_RR) {
		tdata->policies.write.exists = AS_POLICY_EXISTS_REPLACE;
		tdata->policies.operate.exists = AS_POLICY_EXISTS_REPLACE;
	}
	else {
		tdata->policies.write.exists = AS_POLICY_EXISTS_IGNORE;
		tdata->policies.operate.exists = AS_POLICY_EXISTS_IGNORE;
	}
	tdata->policies.apply.ttl = stage->ttl;

	if (stage->tps == 0) {
		// tps = 0 means no throttling
		dyn_throttle_init(&tdata->dyn_throttle, 0);
	}
	else {
		// dyn_throttle uses a target delay between consecutive events, so
		// calculate the target delay given the requested transactions per
		// second and the number of concurrent transactions (i.e. num threads)
		uint32_t n_threads = stage->async ? 1 :
			cdata->transaction_worker_threads;
		dyn_throttle_init(&tdata->dyn_throttle,
				(1000000.f * n_threads) / stage->tps);
	}

	if (!stage->random) {

		if (workload_contains_writes(&stage->workload)) {
			if (stage->workload.write_all_pct != 0) {
				uint32_t n_bins = obj_spec_n_bins(&stage->obj_spec);
				as_record_init(&tdata->fixed_full_record, n_bins);
				obj_spec_populate_bins(&stage->obj_spec, &tdata->fixed_full_record,
						tdata->random, cdata->bin_name, NULL, 0,
						cdata->compression_ratio);

				tdata->fixed_full_record.ttl = stage->ttl;
			}
			if (stage->workload.write_all_pct != 100) {
				uint32_t n_bins = stage->n_write_bins;

				as_record_init(&tdata->fixed_partial_record, n_bins);
				obj_spec_populate_bins(&stage->obj_spec, &tdata->fixed_partial_record,
						tdata->random, cdata->bin_name, stage->write_bins, n_bins,
						cdata->compression_ratio);

				tdata->fixed_partial_record.ttl = stage->ttl;
			}
		}

		if (workload_contains_udfs(&stage->workload)) {
			as_val* val = obj_spec_gen_value(&stage->udf_fn_args,
					tdata->random, NULL, 0);
			tdata->fixed_udf_fn_args = as_list_fromval(val);
		}
	}

	if (workload_contains_deletes(&stage->workload)) {
		if (stage->workload.type != WORKLOAD_TYPE_D ||
				stage->workload.write_all_pct != 0) {
			uint32_t n_bins = obj_spec_n_bins(&stage->obj_spec);
			as_record_init(&tdata->fixed_delete_record, n_bins);

			for (uint32_t i = 0; i < n_bins; i++) {
				as_bin_name bin_name;
				gen_bin_name(bin_name, cdata->bin_name, i);
				as_record_set_nil(&tdata->fixed_delete_record, bin_name);
			}
		}
		if (stage->workload.type == WORKLOAD_TYPE_D &&
				stage->workload.write_all_pct != 100) {
			// write_bins must be set if write_all_pct != 100 in DB workload
			assert(stage->write_bins != NULL);
			as_record_init(&tdata->fixed_delete_record, stage->n_write_bins);

			FOR_EACH_WRITE_BIN(stage->write_bins, stage->n_write_bins,
					&stage->obj_spec, iter, idx, __bin_spec) {

				as_bin_name bin_name;
				gen_bin_name(bin_name, cdata->bin_name, idx);
				as_record_set_nil(&tdata->fixed_delete_record, bin_name);
			}
			END_FOR_EACH_WRITE_BIN(stage->write_bins, stage->n_write_bins,
					iter, idx);
		}
	}
}

LOCAL_HELPER void
terminate_stage(const cdata_t* cdata, tdata_t* tdata, stage_t* stage)
{
	dyn_throttle_free(&tdata->dyn_throttle);

	if (!stage->random) {
		if (stage->workload.write_all_pct != 0) {
			as_record_destroy(&tdata->fixed_full_record);
		}
		if (stage->workload.write_all_pct != 100) {
			as_record_destroy(&tdata->fixed_partial_record);
		}

		if (workload_contains_udfs(&stage->workload)) {
			as_val_destroy((as_val*) tdata->fixed_udf_fn_args);
		}
	}

	if (workload_contains_deletes(&stage->workload)) {
		as_record_destroy(&tdata->fixed_delete_record);
	}
}

