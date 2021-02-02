
#include <transaction.h>

#include <aerospike/as_atomic.h>
#include <aerospike/aerospike_key.h>
#include <citrusleaf/cf_clock.h>

#include <benchmark.h>
#include <common.h>
#include <coordinator.h>
#include <workload.h>


/******************************************************************************
 * Latency recording helpers
 *****************************************************************************/

static void _record_read(clientdata* cdata, uint64_t dt_us)
{
	if (cdata->latency || cdata->histogram_output != NULL ||
			cdata->hdr_comp_write_output != NULL) {
		if (cdata->latency) {
			latency_add(&cdata->read_latency, dt_us / 1000);
			hdr_record_value_atomic(cdata->read_hdr, dt_us);
		}
		if (cdata->histogram_output != NULL) {
			histogram_add(&cdata->read_histogram, dt_us);
		}
		if (cdata->hdr_comp_write_output != NULL) {
			hdr_record_value_atomic(cdata->summary_read_hdr, dt_us);
		}
	}
	as_incr_uint32(&cdata->read_count);
}

static void _record_write(clientdata* cdata, uint64_t dt_us)
{
	if (cdata->latency || cdata->histogram_output != NULL ||
			cdata->hdr_comp_write_output != NULL) {
		if (cdata->latency) {
			latency_add(&cdata->write_latency, dt_us / 1000);
			hdr_record_value_atomic(cdata->write_hdr, dt_us);
		}
		if (cdata->histogram_output != NULL) {
			histogram_add(&cdata->write_histogram, dt_us);
		}
		if (cdata->hdr_comp_write_output != NULL) {
			hdr_record_value_atomic(cdata->summary_write_hdr, dt_us);
		}
	}
	as_incr_uint32(&cdata->write_count);
}


/******************************************************************************
 * Read/Write singular/batch synchronous/asynchronous operations
 *****************************************************************************/

static int
_write_record_sync(as_key* key, as_record* rec, clientdata* cdata)
{
	as_status status;
	as_error err;

	uint64_t start = cf_getus();
	status = aerospike_key_put(&cdata->client, &err, 0, key, rec);
	uint64_t end = cf_getus();

	if (status == AEROSPIKE_OK) {
		_record_write(cdata, end - start);
		return 0;
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

int
_read_record_sync(as_key* key, clientdata* cdata)
{
	as_record* rec = NULL;
	as_status status;
	as_error err;

	uint64_t start = cf_getus();
	status = aerospike_key_get(&cdata->client, &err, 0, key, &rec);
	uint64_t end = cf_getus();

	if (status == AEROSPIKE_OK || status == AEROSPIKE_ERR_RECORD_NOT_FOUND) {
		_record_read(cdata, end - start);
		as_record_destroy(rec);
		return status;
	}

	// Handle error conditions.
	if (status == AEROSPIKE_ERR_TIMEOUT) {
		as_incr_uint32(&cdata->read_timeout_count);
	}
	else {
		as_incr_uint32(&cdata->read_error_count);

		if (cdata->debug) {
			blog_error("Read error: ns=%s set=%s key=%d bin=%s code=%d "
					"message=%s",
					cdata->namespace, cdata->set, key->value.integer.value,
					cdata->bin_name, status, err.message);
		}
	}

	as_record_destroy(rec);
	return status;
}


/******************************************************************************
 * Thread worker helper methods
 *****************************************************************************/

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
	*t_start = key_start + ((n_keys * t_idx) / n_threads);
	*t_end   = key_start + ((n_keys * (t_idx + 1)) / n_threads);
}


static void
_gen_key(uint64_t key_val, as_key* key, const clientdata* cdata)
{
	as_key_init_int64(key, cdata->namespace, cdata->set, key_val);
}


/*
 * generates a record with given key following the obj_spec in cdata
 */
static void
_gen_record(as_record* rec, as_random* random, const clientdata* cdata)
{
	if (cdata->random) {
		uint32_t n_objs = obj_spec_n_bins(&cdata->obj_spec);
		as_record_init(rec, n_objs);

		obj_spec_populate_bins(&cdata->obj_spec, rec, random,
				cdata->bin_name);
	}
	else {
		as_list* list = as_list_fromval(cdata->fixed_value);
		uint32_t n_objs = as_list_size(list);
		as_record_init(rec, n_objs);

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


/*
 * generates a record with all nil bins (used to remove records)
 */
static void
_gen_nil_record(as_record* rec, const clientdata* cdata)
{
	uint32_t n_objs = obj_spec_n_bins(&cdata->obj_spec);
	as_record_init(rec, n_objs);

	for (uint32_t i = 0; i < n_objs; i++) {
		as_bin* bin = &rec->bins.entries[i];
		gen_bin_name(bin->name, cdata->bin_name, i + 1);
		as_record_set_nil(rec, bin->name);
	}
}


/******************************************************************************
 * Synchronous query methods
 *****************************************************************************/

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

	// each worker thread takes a subrange of the total set of keys being
	// inserted, all approximately equal in size
	_calculate_subrange(stage->key_start, stage->key_end, t_idx,
			cdata->transaction_worker_threads, &start_key, &end_key);

	key_val = start_key;
	while (as_load_uint8((uint8_t*) &tdata->do_work) &&
			key_val < end_key) {

		// create a record with given key
		_gen_key(key_val, &key, cdata);
		_gen_record(&rec, tdata->random, cdata);

		// write this record to the database
		_write_record_sync(&key, &rec, cdata);

		as_record_destroy(&rec);
		as_key_destroy(&key);

		key_val++;
	}
	printf("thread %d wrote keys (%lu - %lu)\n", t_idx,
			start_key, end_key - 1);

	// once we've written everything, there's nothing left to do, so tell
	// coord we're done and exit
	thr_coordinator_complete(coord);
}


static void random_read_write(struct threaddata* tdata,
		clientdata* cdata, struct thr_coordinator* coord,
		struct stage* stage)
{
	as_key key;
	as_record rec;

	// multiply pct by 2**24 before dividing by 100 and casting to an int,
	// since floats have 24 bits of precision including the leading 1,
	// so that read_pct is pct% between 0 and 2**24
	uint32_t read_pct = (uint32_t) ((0x01000000 * stage->workload.pct) / 100);

	// since there is no specific target number of transactions required before
	// the stage is finished, only a timeout, tell the coordinator we are ready
	// to finish as soon as the timer runs out
	thr_coordinator_complete(coord);

	while (as_load_uint8((uint8_t*) &tdata->do_work)) {
		// roll the die
		uint32_t die = as_random_next_uint32(tdata->random);
		// floats have 24 bits of precision (including implicit leading 1)
		die &= 0x00ffffff;

		// generate a random key
		uint64_t key_val = stage_gen_random_key(stage, tdata->random);
		_gen_key(key_val, &key, cdata);

		if (die < read_pct) {
			_read_record_sync(&key, cdata);
		}
		else {
			// create a record
			_gen_record(&rec, tdata->random, cdata);

			// write this record to the database
			_write_record_sync(&key, &rec, cdata);

			as_record_destroy(&rec);
		}
		as_key_destroy(&key);
	}
}


static void linear_deletes(struct threaddata* tdata,
		clientdata* cdata, struct thr_coordinator* coord,
		struct stage* stage)
{
	uint32_t t_idx = tdata->t_idx;
	uint64_t start_key, end_key;
	uint64_t key_val;

	as_key key;
	as_record rec;

	// each worker thread takes a subrange of the total set of keys being
	// inserted, all approximately equal in size
	_calculate_subrange(stage->key_start, stage->key_end, t_idx,
			cdata->transaction_worker_threads, &start_key, &end_key);

	key_val = start_key;
	while (as_load_uint8((uint8_t*) &tdata->do_work) &&
			key_val < end_key) {

		// create a record with given key
		_gen_key(key_val, &key, cdata);
		_gen_nil_record(&rec, cdata);

		// delete this record from the database
		_write_record_sync(&key, &rec, cdata);

		as_record_destroy(&rec);
		as_key_destroy(&key);

		key_val++;
	}

	// once we've written everything, there's nothing left to do, so tell
	// coord we're done and exit
	thr_coordinator_complete(coord);
}


/******************************************************************************
 * Asynchronous query methods
 *****************************************************************************/

struct async_listener_args;

typedef void (*async_op_t)(struct async_listener_args*, clientdata*,
		struct threaddata*);

struct async_listener_args {
	clientdata* cdata;
	struct threaddata* tdata;
	// the time at which the async call was made
	uint64_t start_time;

	// the key to be used in the async calls
	as_key key;

	// the callback to be made after each async_call_listener return
	async_op_t next_op_cb;

	// what type of operation is being performed
	enum {
		read,
		write,
		delete
	} op;

	// the next key to be written for linear writers/deleters
	uint64_t next_key;
	// the key to stop on for linear writers/deleters
	uint64_t end_key;
};


static void
_async_call_listener(as_error* err, void* udata, as_event_loop* event_loop)
{
	struct async_listener_args* args = (struct async_listener_args*) udata;

	clientdata* cdata = args->cdata;
	struct threaddata* tdata = args->tdata;

	if (!err) {
		uint64_t end = cf_getus();
		if (args->op == read) {
			_record_read(cdata, end - args->start_time);
		}
		else {
			_record_write(cdata, end - args->start_time);
		}
		//tdata->key_count++;
	}
	else {
		if (err->code == AEROSPIKE_ERR_TIMEOUT) {
			if (args->op == read) {
				as_incr_uint32(&cdata->read_timeout_count);
			}
			else {
				as_incr_uint32(&cdata->write_timeout_count);
			}
		}
		else {
			if (args->op == read) {
				as_incr_uint32(&cdata->read_error_count);
			}
			else {
				as_incr_uint32(&cdata->write_error_count);
			}

			if (cdata->debug) {
				const static char* op_strs[] = {
					"Read",
					"Write",
					"Delete"
				};
				/*blog_error("%s error: ns=%s set=%s key=%d bin=%s code=%d "
						   "message=%s",
						   op_strs[args->op], cdata->namespace, cdata->set,
						   tdata->key.value.integer.value, cdata->bin_name,
						   err->code, err->message);*/
			}
		}
	}

	// Reuse tdata structures.
	// FIXME
	/*if (tdata->key_count == tdata->n_keys) {
		// We have reached max number of records for this command.
		uint64_t total = as_faa_uint64(&cdata->key_count, tdata->n_keys) + tdata->n_keys;
		destroy_threaddata(tdata);

		if (total >= cdata->n_keys) {
			// All commands have been written.
			as_monitor_notify(&monitor);
		}
		return;
	}*/

	/*tdata->key.value.integer.value = tdata->key_start + tdata->key_count;
	tdata->key.digest.init = false;
	linear_write_async(cdata, tdata, event_loop);*/
	args->next_op_cb(args, cdata, tdata);
}


static void init_async_chain(struct threaddata* tdata,
		clientdata* cdata, struct thr_coordinator* coord,
		async_op_t init_cb)
{
	uint32_t t_idx = tdata->t_idx;
	uint64_t start_idx, end_idx;
	uint64_t idx;

	// each worker thread takes a subrange of the total set of simultaneous
	// async calls being made
	_calculate_subrange(0, cdata->async_max_commands, t_idx,
			cdata->transaction_worker_threads, &start_idx, &end_idx);

	for (idx = start_idx; idx < end_idx; idx++) {
		struct async_listener_args* args = (struct async_listener_args*)
			cf_malloc(sizeof(struct async_listener_args));
		args->cdata = cdata;
		args->tdata = tdata;

		init_cb(args, cdata, tdata);
	}

	// once we've written everything, there's nothing left to do, so tell
	// coord we're done and exit
	thr_coordinator_complete(coord);
}


static void _linear_writes_cb(struct async_listener_args* args,
		clientdata* cdata, struct threaddata* tdata)
{
	args->start_time;
	args->key;
	args->op;
}

static void _init_async_linear_writes(struct async_listener_args* args,
		clientdata* cdata, struct threaddata* tdata)
{
	uint64_t start_key, end_key;
	args->next_op_cb = _linear_writes_cb;
	struct stage* stage = &cdata->stages.stages[tdata->stage_idx];

	_calculate_subrange(stage->key_start, stage->key_end, tdata->t_idx,
			cdata->tdata_count, &args->next_key, &args->end_key);

	_linear_writes_cb(args, cdata, tdata);
}


static void _random_read_write_cb(struct async_listener_args* args,
		clientdata* cdata, struct threaddata* tdata)
{

}

static void _init_async_rand_read_write(struct async_listener_args* args,
		clientdata* cdata, struct threaddata* tdata)
{
	args->next_op_cb = _random_read_write_cb;

	_random_read_write_cb(args, cdata, tdata);
}


static void _linear_deletes_cb(struct async_listener_args* args,
		clientdata* cdata, struct threaddata* tdata)
{

}

static void _init_async_linear_deletes(struct async_listener_args* args,
		clientdata* cdata, struct threaddata* tdata)
{
	args->next_op_cb = _linear_deletes_cb;

	_linear_deletes_cb(args, cdata, tdata);
}


/******************************************************************************
 * Main worker thread loop
 *****************************************************************************/

void* transaction_worker(void* udata)
{
	struct threaddata* tdata = (struct threaddata*) udata;
	clientdata* cdata = tdata->cdata;
	struct thr_coordinator* coord = tdata->coord;

	while (!as_load_uint8((uint8_t*) &tdata->finished)) {
		uint32_t stage_idx = as_load_uint32(&tdata->stage_idx);
		struct stage* stage = &cdata->stages.stages[stage_idx];

//		if (stage->async) {
		if (cdata->async) {
			switch (stage->workload.type) {
				case WORKLOAD_TYPE_LINEAR:
					init_async_chain(tdata, cdata, coord, _linear_writes_cb);
					break;
				case WORKLOAD_TYPE_RANDOM:
					init_async_chain(tdata, cdata, coord, _random_read_write_cb);
					break;
				case WORKLOAD_TYPE_DELETE:
					init_async_chain(tdata, cdata, coord, _linear_deletes_cb);
					break;
			}
		}
		else {
			switch (stage->workload.type) {
				case WORKLOAD_TYPE_LINEAR:
					linear_writes(tdata, cdata, coord, stage);
					break;
				case WORKLOAD_TYPE_RANDOM:
					random_read_write(tdata, cdata, coord, stage);
					break;
				case WORKLOAD_TYPE_DELETE:
					linear_deletes(tdata, cdata, coord, stage);
					break;
			}
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

