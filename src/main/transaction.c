
//==========================================================
// Includes.
//

#include <transaction.h>

#include <assert.h>
#ifndef __aarch64__
#include <xmmintrin.h>
#endif

#include <aerospike/aerospike_batch.h>
#include <aerospike/aerospike_key.h>
#include <citrusleaf/cf_clock.h>

#include <benchmark.h>
#include <common.h>
#include <coordinator.h>
#include <queue.h>
#include <workload.h>


//==========================================================
// Typedefs & constants.
//

struct async_data_s {
	cdata_t* cdata;
	stage_t* stage;
	// queue to place this item back on once the callback has finished
	queue_t* adata_q;

	// keep each async_data in the same event loop to prevent the possibility
	// of overflowing an event loop due to bad scheduling
	as_event_loop* ev_loop;

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
};


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
		struct async_data_s* adata, tdata_t* tdata, cdata_t* cdata);
LOCAL_HELPER int _read_record_async(as_key* key, struct async_data_s* adata,
		tdata_t* tdata, cdata_t* cdata, const stage_t* stage);
LOCAL_HELPER int _batch_read_record_async(as_batch_read_records* keys,
		struct async_data_s* adata, tdata_t* tdata, cdata_t* cdata);
LOCAL_HELPER int _apply_udf_async(as_key* key, struct async_data_s* adata,
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
		thr_coord_t* coord, const stage_t* stage, struct async_data_s* adata);
LOCAL_HELPER void random_write_async(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage, struct async_data_s* adata);
LOCAL_HELPER void random_udf_async(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage, struct async_data_s* adata);
LOCAL_HELPER void random_delete_async(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, const stage_t* stage, struct async_data_s* adata);

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
LOCAL_HELPER struct async_data_s* queue_pop_wait(queue_t* adata_q);
LOCAL_HELPER void linear_writes_async(tdata_t* tdata, cdata_t* cdata,
	   thr_coord_t* coord, const stage_t* stage, queue_t* adata_q);
LOCAL_HELPER void random_read_write_async(tdata_t* tdata, cdata_t* cdata,
	   thr_coord_t* coord, const stage_t* stage, queue_t* adata_q);
LOCAL_HELPER void random_read_write_udf_async(tdata_t* tdata, cdata_t* cdata,
	   thr_coord_t* coord, const stage_t* stage, queue_t* adata_q);
LOCAL_HELPER void linear_deletes_async(tdata_t* tdata, cdata_t* cdata,
	   thr_coord_t* coord, const stage_t* stage, queue_t* adata_q);
LOCAL_HELPER void random_read_write_delete_async(tdata_t* tdata, cdata_t* cdata,
	   thr_coord_t* coord, const stage_t* stage, queue_t* adata_q);

// Main worker thread loop
LOCAL_HELPER void do_sync_workload(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, stage_t* stage);
LOCAL_HELPER void do_async_workload(tdata_t* tdata, cdata_t* cdata,
		thr_coord_t* coord, stage_t* stage);
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
_write_record_async(as_key* key, as_record* rec, struct async_data_s* adata,
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
_read_record_async(as_key* key, struct async_data_s* adata, tdata_t* tdata,
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
_batch_read_record_async(as_batch_read_records* keys, struct async_data_s* adata,
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
_apply_udf_async(as_key* key, struct async_data_s* adata, tdata_t* tdata,
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

LOCAL_HELPER void random_read_write_delete(tdata_t* tdata, cdata_t* cdata,
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
		const stage_t* stage, struct async_data_s* adata)
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
		const stage_t* stage, struct async_data_s* adata)
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
		const stage_t* stage, struct async_data_s* adata)
{
	// generate a random key
	uint64_t key_val = stage_gen_random_key(stage, tdata->random);
	_gen_key(key_val, &adata->key, cdata);
	adata->op = udf_op;

	_apply_udf_async(&adata->key, adata, tdata, cdata, stage);
}

LOCAL_HELPER void
random_delete_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, struct async_data_s* adata)
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
	struct async_data_s* adata = (struct async_data_s*) udata;

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

		if (err->code == AEROSPIKE_ERR_NO_MORE_CONNECTIONS) {
			// this event loop is full, try another
			adata->ev_loop = NULL;
		}
	}

	// put this adata object back on the queue
	queue_push(adata->adata_q, adata);
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

LOCAL_HELPER struct async_data_s*
queue_pop_wait(queue_t* adata_q)
{
	struct async_data_s* adata;

	while (1) {
		adata = queue_pop(adata_q);
		if (adata == NULL) {
			#ifdef __aarch64__
			__asm__ __volatile__("yield");
			#else
			_mm_pause();
			#endif

			continue;
		}
		break;
	}
	return adata;
}

LOCAL_HELPER void
linear_writes_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, queue_t* adata_q)
{
	uint64_t key_val, end_key;
	struct async_data_s* adata;

	struct timespec wake_time;
	uint64_t start_time;

	key_val = stage->key_start;
	end_key = stage->key_end;
	while (tdata->do_work &&
			key_val < end_key) {

		adata = queue_pop_wait(adata_q);

		clock_gettime(COORD_CLOCK, &wake_time);
		start_time = timespec_to_us(&wake_time);
		adata->start_time = start_time;

		as_record* rec;
		_gen_key(key_val, &adata->key, cdata);
		rec = _gen_record(tdata->random, cdata, tdata, stage);
		adata->op = write_op;

		_write_record_async(&adata->key, rec, adata, tdata, cdata);

		_destroy_record(rec, stage);

		uint64_t pause_for =
			dyn_throttle_pause_for(&tdata->dyn_throttle, start_time);
		timespec_add_us(&wake_time, pause_for);
		thr_coordinator_sleep(coord, &wake_time);

		key_val++;
	}

	// once we've written everything, there's nothing left to do, so tell
	// coord we're done and exit
	thr_coordinator_complete(coord);
}

LOCAL_HELPER void
random_read_write_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, queue_t* adata_q)
{
	struct async_data_s* adata;

	struct timespec wake_time;
	uint64_t start_time;

	uint32_t read_pct = _pct_to_fp(stage->workload.read_pct);

	// since this workload has no target number of transactions to be made, we
	// are always ready to be reaped, and so we notify the coordinator that we
	// are finished with our required tasks and can be stopped whenever
	thr_coordinator_complete(coord);

	while (tdata->do_work) {

		adata = queue_pop_wait(adata_q);

		clock_gettime(COORD_CLOCK, &wake_time);
		start_time = timespec_to_us(&wake_time);
		adata->start_time = start_time;

		// roll the die
		uint32_t die = _random_fp(tdata->random);

		if (die < read_pct) {
			random_read_async(tdata, cdata, coord, stage, adata);
		}
		else {
			random_write_async(tdata, cdata, coord, stage, adata);
		}

		uint64_t pause_for =
			dyn_throttle_pause_for(&tdata->dyn_throttle, start_time);
		timespec_add_us(&wake_time, pause_for);
		thr_coordinator_sleep(coord, &wake_time);
	}
}

LOCAL_HELPER void
random_read_write_udf_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, queue_t* adata_q)
{
	struct async_data_s* adata;

	struct timespec wake_time;
	uint64_t start_time;

	uint32_t read_pct = _pct_to_fp(stage->workload.read_pct);
	uint32_t write_pct = _pct_to_fp(stage->workload.write_pct);

	// store the cumulative probability in write_pct
	write_pct = read_pct + write_pct;

	// since this workload has no target number of transactions to be made, we
	// are always ready to be reaped, and so we notify the coordinator that we
	// are finished with our required tasks and can be stopped whenever
	thr_coordinator_complete(coord);

	while (tdata->do_work) {

		adata = queue_pop_wait(adata_q);

		clock_gettime(COORD_CLOCK, &wake_time);
		start_time = timespec_to_us(&wake_time);
		adata->start_time = start_time;

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

		uint64_t pause_for =
			dyn_throttle_pause_for(&tdata->dyn_throttle, start_time);
		timespec_add_us(&wake_time, pause_for);
		thr_coordinator_sleep(coord, &wake_time);
	}
}

LOCAL_HELPER void
linear_deletes_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, queue_t* adata_q)
{
	uint64_t key_val, end_key;
	struct async_data_s* adata;

	struct timespec wake_time;
	uint64_t start_time;

	key_val = stage->key_start;
	end_key = stage->key_end;
	while (tdata->do_work &&
			key_val < end_key) {

		adata = queue_pop_wait(adata_q);

		clock_gettime(COORD_CLOCK, &wake_time);
		start_time = timespec_to_us(&wake_time);
		adata->start_time = start_time;

		as_record* rec;
		_gen_key(key_val, &adata->key, cdata);
		rec = _gen_nil_record(tdata);
		adata->op = delete_op;

		_write_record_async(&adata->key, rec, adata, tdata, cdata);

		_destroy_record(rec, stage);

		uint64_t pause_for =
			dyn_throttle_pause_for(&tdata->dyn_throttle, start_time);
		timespec_add_us(&wake_time, pause_for);
		thr_coordinator_sleep(coord, &wake_time);

		key_val++;
	}

	// once we've written everything, there's nothing left to do, so tell
	// coord we're done and exit
	thr_coordinator_complete(coord);
}

LOCAL_HELPER void
random_read_write_delete_async(tdata_t* tdata, cdata_t* cdata, thr_coord_t* coord,
		const stage_t* stage, queue_t* adata_q)
{
	struct async_data_s* adata;

	struct timespec wake_time;
	uint64_t start_time;

	uint32_t read_pct = _pct_to_fp(stage->workload.read_pct);
	uint32_t write_pct = _pct_to_fp(stage->workload.write_pct);

	// store the cumulative probability in write_pct
	write_pct = read_pct + write_pct;

	// since this workload has no target number of transactions to be made, we
	// are always ready to be reaped, and so we notify the coordinator that we
	// are finished with our required tasks and can be stopped whenever
	thr_coordinator_complete(coord);

	while (tdata->do_work) {

		adata = queue_pop_wait(adata_q);

		clock_gettime(COORD_CLOCK, &wake_time);
		start_time = timespec_to_us(&wake_time);
		adata->start_time = start_time;

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

		uint64_t pause_for =
			dyn_throttle_pause_for(&tdata->dyn_throttle, start_time);
		timespec_add_us(&wake_time, pause_for);
		thr_coordinator_sleep(coord, &wake_time);
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
	struct async_data_s* adatas;
	uint32_t t_idx = tdata->t_idx;
	uint64_t n_adatas;
	queue_t adata_q;

	// thread 0 is designated to handle async calls, the rest can immediately
	// terminate
	if (t_idx != 0) {
		thr_coordinator_complete(coord);
		return;
	}

	n_adatas = cdata->async_max_commands;
	adatas =
		(struct async_data_s*) cf_malloc(n_adatas * sizeof(struct async_data_s));

	queue_init(&adata_q, n_adatas);
	for (uint32_t i = 0; i < n_adatas; i++) {
		struct async_data_s* adata = &adatas[i];

		adata->cdata = cdata;
		adata->stage = stage;
		adata->adata_q = &adata_q;
		adata->ev_loop = NULL;

		queue_push(&adata_q, adata);
	}

	switch (stage->workload.type) {
		case WORKLOAD_TYPE_I:
			linear_writes_async(tdata, cdata, coord, stage, &adata_q);
			break;
		case WORKLOAD_TYPE_RU:
		case WORKLOAD_TYPE_RR:
			random_read_write_async(tdata, cdata, coord, stage, &adata_q);
			break;
		case WORKLOAD_TYPE_RUF:
			random_read_write_udf_async(tdata, cdata, coord, stage, &adata_q);
			break;
		case WORKLOAD_TYPE_D:
			linear_deletes_async(tdata, cdata, coord, stage, &adata_q);
			break;
		case WORKLOAD_TYPE_RUD:
			random_read_write_delete_async(tdata, cdata, coord, stage, &adata_q);
			break;
	}

	// wait for all the async calls to finish
	for (uint32_t i = 0; i < n_adatas; i++) {
		queue_pop_wait(&adata_q);
	}
	queue_free(&adata_q);

	// free the async_data structs
	cf_free(adatas);
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

