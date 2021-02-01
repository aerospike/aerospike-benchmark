
#include <transaction.h>

#include <aerospike/as_atomic.h>
#include <aerospike/aerospike_key.h>
#include <citrusleaf/cf_clock.h>

#include <benchmark.h>
#include <common.h>
#include <coordinator.h>
#include <workload.h>


/*
 * calculates the subrange of keys that the given thread should operate on,
 * which is done by evenly dividing the interval into n_threads intervals
 */
static void
_calculate_subrange(uint64_t key_start, uint64_t key_end,
		uint32_t t_idx, uint32_t n_threads,
		uint64_t* t_start, uint64_t* t_end)
{
	uint64_t n_keys = key_end - key_start;
	*t_start = n_keys + ((n_keys * t_idx) / n_threads);
	*t_end   = n_keys + ((n_keys * (t_idx + 1)) / n_threads);
}


/*
 * generates a record with given key following the obj_spec in cdata
 */
static void
_gen_record(uint64_t key_val, as_key* key, as_record* rec, as_random* random,
		const clientdata* cdata)
{
	as_key_init_int64(key, cdata->namespace, cdata->set, key_val);

	if (cdata->random) {
		obj_spec_populate_bins(&cdata->obj_spec, rec, random,
				cdata->bin_name);
	}
	else {
		as_list* list = as_list_fromval(cdata->fixed_value);
		uint32_t n_objs = as_list_size(list);
		for (uint32_t i = 0; i < n_objs; i++) {
			as_val* val = as_list_get(list, i);
			// TODO this will be highly contentious, put this in tdata
			as_val_reserve(val);

			as_bin* bin = &rec->bins.entries[i];
			gen_bin_name(bin->name, cdata->bin_name, i + 1);
			as_record_set(rec, bin->name, (as_bin_value*) val);
		}
	}
}

static int
_write_record_sync(as_key* key, as_record* rec, clientdata* cdata)
{
	as_status status;
	as_error err;
	
	if (cdata->latency || cdata->histogram_output != NULL ||
			cdata->hdr_comp_write_output != NULL) {
		uint64_t begin = cf_getus();
		status = aerospike_key_put(&cdata->client, &err, 0, key, rec);
		uint64_t end = cf_getus();
		
		if (status == AEROSPIKE_OK) {
			as_incr_uint32(&cdata->write_count);
			if (cdata->latency) {
				latency_add(&cdata->write_latency, (end - begin) / 1000);
				hdr_record_value_atomic(cdata->write_hdr, end - begin);
			}
			if (cdata->histogram_output != NULL) {
				histogram_add(&cdata->write_histogram, end - begin);
			}
			if (cdata->hdr_comp_write_output != NULL) {
				hdr_record_value_atomic(cdata->summary_write_hdr, end - begin);
			}
			return 0;
		}
	}
	else {
		status = aerospike_key_put(&cdata->client, &err, 0, key, rec);
		
		if (status == AEROSPIKE_OK) {
			as_incr_uint32(&cdata->write_count);
			return 0;
		}
	}
	
	// Handle error conditions.
	if (status == AEROSPIKE_ERR_TIMEOUT) {
		as_incr_uint32(&cdata->write_timeout_count);
	}
	else {
		as_incr_uint32(&cdata->write_error_count);
		
		if (cdata->debug) {
			blog_error("Write error: ns=%s set=%s key=%d bin=%s code=%d message=%s",
					   cdata->namespace, cdata->set, key, cdata->bin_name, status, err.message);
		}
	}
	return -1;
}


/*
 * TODO add work queue that threads can pull batches of keys from, rather than
 * having each thread take a predefined segment of the set of all keys
 */
static void linear_writes(struct threaddata* tdata,
		clientdata* cdata, struct thr_coordinator* coord,
		struct stage* stage)
{
	uint32_t t_idx = tdata->t_idx;
	uint64_t start_key, end_key;
	uint64_t key_val;

	as_key key;
	as_record rec;

	_calculate_subrange(stage->key_start, stage->key_end, t_idx,
			cdata->transaction_worker_threads, &start_key, &end_key);

	key_val = start_key;
	while (as_load_uint8((uint8_t*) &tdata->do_work) &&
			key_val < end_key) {

		// create a record with given key
		_gen_record(key_val, &key, &rec, tdata->random, cdata);

		_write_record_sync(&key, &rec, cdata);
	}
	thr_coordinator_complete(coord);
}


static void random_read_write(struct threaddata* tdata,
		clientdata* cdata, struct thr_coordinator* coord,
		struct stage* stage)
{

	thr_coordinator_complete(coord);
	while (as_load_uint8((uint8_t*) &tdata->do_work)) {
	}
}


void* transaction_worker(void* udata)
{
	struct threaddata* tdata = (struct threaddata*) udata;
	clientdata* cdata = tdata->cdata;
	struct thr_coordinator* coord = tdata->coord;

	while (!as_load_uint8((uint8_t*) &tdata->finished)) {
		uint32_t stage_idx = as_load_uint32(&tdata->stage_idx);
		struct stage* stage = &cdata->stages.stages[stage_idx];
		switch (stage->workload.type) {
			case WORKLOAD_TYPE_LINEAR:
				linear_writes(tdata, cdata, coord, stage);
				break;
			case WORKLOAD_TYPE_RANDOM:
				random_read_write(tdata, cdata, coord, stage);
				break;
			case WORKLOAD_TYPE_DELETE:
				break;
		}
		// check tdata->finished before locking
		if (as_load_uint8((uint8_t*) &tdata->finished)) {
			break;
		}
		thr_coordinator_wait(coord);
	}

	return NULL;
}

void* transaction_worker_async(void* udata)
{
	return NULL;
}

