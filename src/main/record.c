/*******************************************************************************
 * Copyright 2008-2020 by Aerospike.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ******************************************************************************/
#include "benchmark.h"
#include "common.h"
#include <aerospike/aerospike_batch.h>
#include <aerospike/aerospike_key.h>
#include <aerospike/as_arraylist.h>
#include <aerospike/as_atomic.h>
#include <aerospike/as_hashmap.h>
#include <aerospike/as_monitor.h>
#include <aerospike/as_random.h>
#include <aerospike/as_sleep.h>
#include <citrusleaf/cf_clock.h>
#include <assert.h>
#include <stdlib.h>

extern as_monitor monitor;

static const char alphanum[] =
	"0123456789"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz";

static int alphanum_len = sizeof(alphanum) - 1;

static int
calc_list_or_map_ele_count(char bintype, int binlen, len_type binlen_type, int expected_ele_size)
{
	int len = binlen;

	switch (binlen_type) {
	case LEN_TYPE_KBYTES:
		len *= 1024;
		/* no break */
	case LEN_TYPE_BYTES:
		len /= expected_ele_size;
		if (bintype == 'M') {
			len /= 2;
		}
		break;
	default:
		break;
	}

	return len;
}

// Expected msgpack size is 9 bytes.
static as_val *
random_element_9b(as_random *ran)
{
	int type = (int)(as_random_next_uint64(ran) % 2);

	if (type == 0) {
		return (as_val *)as_integer_new(as_random_next_uint64(ran));
	}

	// Len is 4 to 10, average 7 with 2 bytes of msgpack
	// string header results in an expected value of 9 bytes.
	int len = (int)(as_random_next_uint64(ran) % 6) + 4;
	uint8_t* buf = alloca(len + 1);
	as_random_next_bytes(ran, buf, len);

	for (int i = 0; i < len; i++) {
		buf[i] = alphanum[buf[i] % alphanum_len];
	}
	buf[len] = 0;

	return (as_val *)as_string_new_strdup((char *)buf);
}

int
gen_value(arguments* args, as_val** valpp)
{
	switch (args->bintype) {
		case 'I': {
			// Generate integer.
			uint32_t v = as_random_get_uint32();
			*valpp = (as_val *) as_integer_new(v);
			break;
		}
			
		case 'B': {
			// Generate byte array on heap.
			int len = args->binlen;
			uint8_t * buf;

			if (args->compression_ratio != 1.f) {
				// if compression is enabled & the desired compression ratio is not
				// 1, then only generate (compression_ratio * len) bytes of random
				// data and pad the rest with 0
				buf = cf_calloc(len, 1);
				int compressed_len = (int) (len * args->compression_ratio);
				assert(((unsigned) compressed_len) <= len);
				as_random_get_bytes(buf, compressed_len);
			}
			else {
				buf = cf_malloc(len);
				as_random_get_bytes(buf, len);
			}

			*valpp = (as_val *) as_bytes_new_wrap(buf, len, true);
			break;
		}
			
		case 'S': {
			// Generate random bytes on heap and convert to alphanumeric string.
			int len = args->binlen;
			uint8_t* buf = cf_malloc(len+1);
			as_random_get_bytes(buf, len);
			
			for (int i = 0; i < len; i++) {
				buf[i] = alphanum[buf[i] % alphanum_len];
			}
			buf[len] = 0;
			*valpp = (as_val *) as_string_new((char *)buf, true);
			break;
		}

		case 'L': {
			int len = calc_list_or_map_ele_count(args->bintype, args->binlen, args->binlen_type, 9);
			*valpp = (as_val *)as_arraylist_new(len, 0);

			for (size_t i = 0; i < len; i++) {
				as_list_append((as_list *)(*valpp), random_element_9b(as_random_instance()));
			}
			break;
		}

		case 'M': {
			int len = calc_list_or_map_ele_count(args->bintype, args->binlen, args->binlen_type, 9);
			*valpp = (as_val *)as_hashmap_new(len);

			for (size_t i = 0; i < len; i++) {
				as_map_set((as_map *)(*valpp), random_element_9b(as_random_instance()), random_element_9b(as_random_instance()));
			}
			break;
		}

		default: {
			blog_error("Unknown type %c", args->bintype);
			return -1;
		}
	}
	return 0;
}

threaddata*
create_threaddata(clientdata* cdata, uint64_t key_start, uint64_t n_keys)
{
	int len = 0;
	
	// Only random bin values need a thread local buffer.
	if (cdata->random) {
		switch (cdata->bintype)
		{
			case 'I': {
				// Integer does not use buffer.
				break;
			}
				
			case 'B': {
				// Create thread local byte buffer.
				len = cdata->binlen;
				break;
			}
				
			case 'S': {
				// Create thread local string buffer.
				len = cdata->binlen + 1;
				break;
			}

			case 'L':
			case 'M':
				// Does not need buffer.
				break;
				
			default: {
				blog_error("Unknown type %c", cdata->bintype);
				return 0;
			}
		}
	}

	threaddata* tdata = malloc(sizeof(threaddata));
	tdata->cdata = cdata;
	tdata->random = as_random_instance();
	tdata->buffer = len != 0 ? malloc(len) : NULL;
	tdata->begin = 0;
	tdata->key_start = key_start;
	tdata->key_count = 0;
	tdata->n_keys = n_keys;

	// Initialize a thread local key, record.
	as_key_init_int64(&tdata->key, cdata->namespace, cdata->set, key_start);
	as_record_init(&tdata->rec, cdata->numbins);
	for (int i = 0; i < cdata->numbins; i++) {
		tdata->rec.bins.entries[i].valuep = NULL;
		as_val_reserve(cdata->fixed_value);
	}
	tdata->rec.bins.size = cdata->numbins;
	return tdata;
}

void
destroy_threaddata(threaddata* tdata)
{
	as_key_destroy(&tdata->key);

	// Only decrement ref count on null bin entries.
	// Non-null bin entries references are decremented
	// in as_record_destroy().
	clientdata* cdata = tdata->cdata;
	for (int i = 0; i < cdata->numbins; i++) {
		if (tdata->rec.bins.entries[i].valuep == NULL) {
			as_val_destroy(cdata->fixed_value);
		}
	}
	as_record_destroy(&tdata->rec);
	free(tdata->buffer);
	free(tdata);
}

static void
init_write_record(clientdata* cdata, threaddata* tdata)
{
	if (cdata->del_bin) {
		for (int i = 0; i < cdata->numbins; i++) {
			as_bin* bin = &tdata->rec.bins.entries[i];
			if (i==0) {
				strcpy(bin->name, cdata->bin_name);
			} else {
				sprintf(bin->name, "%s_%d", cdata->bin_name, i);
			}
			as_record_set_nil(&tdata->rec, bin->name);
		}
		return;
	}
	
	for (int i = 0; i <cdata->numbins; i++) {
		as_bin* bin = &tdata->rec.bins.entries[i];
		if (i==0) {
			strcpy(bin->name, cdata->bin_name);
		} else {
			sprintf(bin->name, "%s_%d", cdata->bin_name, i);
		}

		if (cdata->random) {
			// Generate random value.
			switch (cdata->bintype)
			{
				case 'I': {
							  // Generate integer.
							  uint32_t i = as_random_next_uint32(tdata->random);
							  as_integer_init((as_integer*)&bin->value, i);
							  bin->valuep = &bin->value;
							  break;
						  }

				case 'B': {
							  // Generate byte array in thread local buffer.
							  uint8_t* buf = tdata->buffer;
							  int len = cdata->binlen;

							  if (cdata->compression_ratio != 1.f) {
								  // if compression is enabled & the desired compression ratio is not
								  // 1, then only generate (compression_ratio * len) bytes of random
								  // data and pad the rest with 0
								  int compressed_len = (int) (len * cdata->compression_ratio);
								  // sanity check, bad things would happen if this were false
								  assert(((unsigned) compressed_len) <= len);
								  as_random_next_bytes(tdata->random, buf, compressed_len);
								  memset(buf + compressed_len, 0, len - compressed_len);
							  }
							  else {
								  as_random_next_bytes(tdata->random, buf, len);
							  }

							  as_bytes_init_wrap((as_bytes*)&bin->value, buf, len, false);
							  bin->valuep = &bin->value;
							  break;
						  }

				case 'S': {
							  // Generate random bytes on stack and convert to alphanumeric string.
							  uint8_t* buf = tdata->buffer;
							  int len = cdata->binlen;
							  as_random_next_bytes(tdata->random, buf, len);

							  for (int i = 0; i < len; i++) {
								  buf[i] = alphanum[buf[i] % alphanum_len];
							  }
							  buf[len] = 0;
							  as_string_init((as_string *)&bin->value, (char*)buf, false);
							  bin->valuep = &bin->value;
							  break;
						  }

				case 'L': {
							  int len = calc_list_or_map_ele_count(cdata->bintype, cdata->binlen, cdata->binlen_type, 9);
							  as_list *list = (as_list *)as_arraylist_new((uint32_t)len, 0);

							  for (int i = 0; i < len; i++) {
								  as_list_append(list, random_element_9b(tdata->random));
							  }

							  as_record_set_list(&tdata->rec, cdata->bin_name, list);
							  break;
						  }

				case 'M': {
							  int len = calc_list_or_map_ele_count(cdata->bintype, cdata->binlen, cdata->binlen_type, 9);
							  as_map *map = (as_map *)as_hashmap_new((uint32_t)len);

							  for (int i = 0; i < len; i++) {
								  as_val *k = random_element_9b(tdata->random);
								  as_val *v = random_element_9b(tdata->random);
								  as_map_set(map, k, v);
							  }

							  as_record_set_map(&tdata->rec, cdata->bin_name, map);
							  break;
						  }

				default: {
							 blog_error("Unknown type %c", cdata->bintype);
							 break;
						 }
			}
		}
		else {
			// Use fixed value.
			((as_val*)&bin->value)->type = cdata->fixed_value->type;
			bin->valuep = (as_bin_value *)cdata->fixed_value;
		}
	}
}

bool
write_record_sync(clientdata* cdata, threaddata* tdata, uint64_t key)
{
	// Initialize key
	tdata->key.value.integer.value = key;
	tdata->key.digest.init = false;

	// Initialize record
	init_write_record(cdata, tdata);
	
	as_status status;
	as_error err;
	
	if (cdata->latency || cdata->histogram_output != NULL) {
		uint64_t begin = cf_getus();
		status = aerospike_key_put(&cdata->client, &err, 0, &tdata->key, &tdata->rec);
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
			return true;
		}
	}
	else {
		status = aerospike_key_put(&cdata->client, &err, 0, &tdata->key, &tdata->rec);
		
		if (status == AEROSPIKE_OK) {
			as_incr_uint32(&cdata->write_count);
			return true;
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
	return false;
}

int
read_record_sync(clientdata* cdata, threaddata* tdata)
{
	uint64_t keyval = as_random_next_uint64(tdata->random) % cdata->n_keys + cdata->key_start;
	as_key key;
	as_key_init_int64(&key, cdata->namespace, cdata->set, keyval);
	
	as_record* rec = 0;
	as_status status;
	as_error err;
	
	if (cdata->latency || cdata->histogram_output != NULL) {
		uint64_t begin = cf_getus();
		status = aerospike_key_get(&cdata->client, &err, 0, &key, &rec);
		uint64_t end = cf_getus();
		
		// Record may not have been initialized, so not found is ok.
		if (status == AEROSPIKE_OK || status == AEROSPIKE_ERR_RECORD_NOT_FOUND) {
			as_incr_uint32(&cdata->read_count);
			if (cdata->latency) {
				latency_add(&cdata->read_latency, (end - begin) / 1000);
				hdr_record_value_atomic(cdata->read_hdr, end - begin);
			}
			if (cdata->histogram_output != NULL) {
				histogram_add(&cdata->read_histogram, end - begin);
			}
			as_record_destroy(rec);
			return status;
		}
	}
	else {
		status = aerospike_key_get(&cdata->client, &err, 0, &key, &rec);
		
		// Record may not have been initialized, so not found is ok.
		if (status == AEROSPIKE_OK || status == AEROSPIKE_ERR_RECORD_NOT_FOUND) {
			as_incr_uint32(&cdata->read_count);
			as_record_destroy(rec);
			return status;
		}
	}
	
	// Handle error conditions.
	if (status == AEROSPIKE_ERR_TIMEOUT) {
		as_incr_uint32(&cdata->read_timeout_count);
	}
	else {
		as_incr_uint32(&cdata->read_error_count);
		
		if (cdata->debug) {
			blog_error("Read error: ns=%s set=%s key=%d bin=%s code=%d message=%s",
					   cdata->namespace, cdata->set, keyval, cdata->bin_name, status, err.message);
		}
	}
	
	as_record_destroy(rec);
	return status;
}

int
batch_record_sync(clientdata* cdata, threaddata* tdata)
{
	as_batch_read_records* records = as_batch_read_create(cdata->batch_size);

	for (int i = 0; i < cdata->batch_size; i++) {
		int64_t k = (int64_t)as_random_next_uint64(tdata->random) % cdata->n_keys + cdata->key_start;
		as_batch_read_record* record = as_batch_read_reserve(records);
		as_key_init_int64(&record->key, cdata->namespace, cdata->set, k);
		record->read_all_bins = true;
	}

	as_status status;
	as_error err;
	
	if (cdata->latency || cdata->histogram_output != NULL) {
		uint64_t begin = cf_getus();
		status = aerospike_batch_read(&cdata->client, &err, NULL, records);
		uint64_t end = cf_getus();
		
		if (status == AEROSPIKE_OK) {
			as_incr_uint32(&cdata->read_count);
			if (cdata->latency) {
				latency_add(&cdata->read_latency, (end - begin) / 1000);
				hdr_record_value_atomic(cdata->read_hdr, end - begin);
			}
			if (cdata->histogram_output != NULL) {
				histogram_add(&cdata->read_histogram, end - begin);
			}
			as_batch_read_destroy(records);
			return status;
		}
	}
	else {
		status = aerospike_batch_read(&cdata->client, &err, 0, records);
		
		// Record may not have been initialized, so not found is ok.
		if (status == AEROSPIKE_OK) {
			as_incr_uint32(&cdata->read_count);
			as_batch_read_destroy(records);
			return status;
		}
	}

	// Handle error conditions.
	if (status == AEROSPIKE_ERR_TIMEOUT) {
		as_incr_uint32(&cdata->read_timeout_count);
	}
	else {
		as_incr_uint32(&cdata->read_error_count);
		
		if (cdata->debug) {
			blog_error("Batch error: ns=%s set=%s bin=%s code=%d message=%s",
					   cdata->namespace, cdata->set, cdata->bin_name, status, err.message);
		}
	}
	
	as_batch_read_destroy(records);
	return status;
}

void
throttle(clientdata* cdata)
{
	if (cdata->throughput > 0) {
		int transactions = cdata->write_count + cdata->read_count;

		if (transactions >= cdata->throughput) {
			int64_t millis = (int64_t)cdata->period_begin + 1000L -
					(int64_t)cf_getus();

			if (millis > 0) {
				as_sleep((uint32_t)millis);
			}
		}
	}
}

static void linear_write_listener(as_error* err, void* udata, as_event_loop* event_loop);

void
linear_write_async(clientdata* cdata, threaddata* tdata, as_event_loop* event_loop)
{
	init_write_record(cdata, tdata);
	
	if (cdata->latency) {
		tdata->begin = cf_getus();
	}
	
	as_error err;
	
	if (aerospike_key_put_async(&cdata->client, &err, NULL, &tdata->key, &tdata->rec, linear_write_listener, tdata, event_loop, NULL) != AEROSPIKE_OK) {
		linear_write_listener(&err, tdata, event_loop);
	}
}

static void
linear_write_listener(as_error* err, void* udata, as_event_loop* event_loop)
{
	threaddata* tdata = udata;
	clientdata* cdata = tdata->cdata;

	if (!err) {
		if (cdata->latency || cdata->histogram_output != NULL) {
			uint64_t end = cf_getus();
			if (cdata->latency) {
				latency_add(&cdata->write_latency, (end - tdata->begin) / 1000);
				hdr_record_value_atomic(cdata->write_hdr, end - tdata->begin);
			}
			if (cdata->histogram_output != NULL) {
				histogram_add(&cdata->write_histogram, end - tdata->begin);
			}
		}
		as_incr_uint32(&cdata->write_count);
		tdata->key_count++;
	}
	else {
		if (err->code == AEROSPIKE_ERR_TIMEOUT) {
			as_incr_uint32(&cdata->write_timeout_count);
		}
		else {
			as_incr_uint32(&cdata->write_error_count);
			
			if (cdata->debug) {
				blog_error("Write error: ns=%s set=%s key=%d bin=%s code=%d message=%s",
						   cdata->namespace, cdata->set, tdata->key.value.integer.value,
						   cdata->bin_name, err->code, err->message);
			}
		}
	}
	
	// Reuse tdata structures.
	if (tdata->key_count == tdata->n_keys) {
		// We have reached max number of records for this command.
		uint64_t total = as_faa_uint64(&cdata->key_count, tdata->n_keys) + tdata->n_keys;
		destroy_threaddata(tdata);

		if (total >= cdata->n_keys) {
			// All commands have been written.
			as_monitor_notify(&monitor);
		}
		return;
	}

	tdata->key.value.integer.value = tdata->key_start + tdata->key_count;
	tdata->key.digest.init = false;
	linear_write_async(cdata, tdata, event_loop);
}

static void random_write_listener(as_error* err, void* udata, as_event_loop* event_loop);
static void random_read_listener(as_error* err, as_record* rec, void* udata, as_event_loop* event_loop);
static void random_batch_listener(as_error* err, as_batch_read_records* records, void* udata, as_event_loop* event_loop);

void
random_read_write_async(clientdata* cdata, threaddata* tdata, as_event_loop* event_loop)
{
	// Choose key at random.
	uint64_t key = as_random_next_uint64(tdata->random) % cdata->n_keys + cdata->key_start;
	tdata->key.value.integer.value = key;
	tdata->key.digest.init = false;
	
	int die = as_random_next_uint32(tdata->random) % 100;
	as_error err;
	
	if (die < cdata->read_pct) {
		if (cdata->latency) {
			tdata->begin = cf_getus();
		}

		if (cdata->batch_size <= 1) {
			if (aerospike_key_get_async(&cdata->client, &err, NULL, &tdata->key, random_read_listener, tdata, event_loop, NULL) != AEROSPIKE_OK) {
				random_read_listener(&err, NULL, tdata, event_loop);
			}
		}
		else {
			as_batch_read_records* records = as_batch_read_create(cdata->batch_size);

			for (int i = 0; i < cdata->batch_size; i++) {
				int64_t k = (int64_t)as_random_next_uint64(tdata->random) % cdata->n_keys + cdata->key_start;
				as_batch_read_record* record = as_batch_read_reserve(records);
				as_key_init_int64(&record->key, cdata->namespace, cdata->set, k);
				record->read_all_bins = true;
			}

			as_error err;
			if (aerospike_batch_read_async(&cdata->client, &err, NULL, records, random_batch_listener, tdata, event_loop) != AEROSPIKE_OK) {
				random_batch_listener(&err, records, NULL, event_loop);
			}
		}
	}
	else {
		init_write_record(cdata, tdata);
		
		if (cdata->latency) {
			tdata->begin = cf_getus();
		}
		
		if (aerospike_key_put_async(&cdata->client, &err, NULL, &tdata->key, &tdata->rec, random_write_listener, tdata, event_loop, NULL) != AEROSPIKE_OK) {
			random_write_listener(&err, tdata, event_loop);
		}
	}
}

static void
random_read_write_next(clientdata* cdata, threaddata* tdata, as_event_loop* event_loop)
{
	as_incr_uint64(&cdata->transactions_count);

	if (cdata->valid) {
		// Start a new command on same event loop to keep the queue full.
		random_read_write_async(cdata, tdata, event_loop);
	}
	else {
		destroy_threaddata(tdata);

		if (as_aaf_uint32(&cdata->tdata_count, -1) == 0) {
			// All tdata instances are complete.
			as_monitor_notify(&monitor);
		}
	}
}

static void
random_write_listener(as_error* err, void* udata, as_event_loop* event_loop)
{
	threaddata* tdata = udata;
	clientdata* cdata = tdata->cdata;
	
	if (!err) {
		if (cdata->latency || cdata->histogram_output != NULL) {
			uint64_t end = cf_getus();
			if (cdata->latency) {
				latency_add(&cdata->write_latency, (end - tdata->begin) / 1000);
				hdr_record_value_atomic(cdata->write_hdr, end - tdata->begin);
			}
			if (cdata->histogram_output != NULL) {
				histogram_add(&cdata->write_histogram, end - tdata->begin);
			}
		}
		as_incr_uint32(&cdata->write_count);
	}
	else {
		if (err->code == AEROSPIKE_ERR_TIMEOUT) {
			as_incr_uint32(&cdata->write_timeout_count);
		}
		else {
			as_incr_uint32(&cdata->write_error_count);
			
			if (cdata->debug) {
				blog_error("Write error: ns=%s set=%s key=%d bin=%s code=%d message=%s",
						   cdata->namespace, cdata->set, tdata->key.value.integer.value,
						   cdata->bin_name, err->code, err->message);
			}
		}
	}
	random_read_write_next(cdata, tdata, event_loop);
}

static void
random_read_listener(as_error* err, as_record* rec, void* udata, as_event_loop* event_loop)
{
	threaddata* tdata = udata;
	clientdata* cdata = tdata->cdata;
	
	if (!err || err->code == AEROSPIKE_ERR_RECORD_NOT_FOUND) {
		if (cdata->latency || cdata->histogram_output != NULL) {
			uint64_t end = cf_getus();
			if (cdata->latency) {
				latency_add(&cdata->read_latency, (end - tdata->begin) / 1000);
				hdr_record_value_atomic(cdata->read_hdr, end - tdata->begin);
			}
			if (cdata->histogram_output != NULL) {
				histogram_add(&cdata->read_histogram, end - tdata->begin);
			}
		}
		as_incr_uint32(&cdata->read_count);
	}
	else {
		if (err->code == AEROSPIKE_ERR_TIMEOUT) {
			as_incr_uint32(&cdata->read_timeout_count);
		}
		else {
			as_incr_uint32(&cdata->read_error_count);
			
			if (cdata->debug) {
				blog_error("Read error: ns=%s set=%s key=%d bin=%s code=%d message=%s",
						   cdata->namespace, cdata->set, tdata->key.value.integer.value,
						   cdata->bin_name, err->code, err->message);
			}
		}
	}
	random_read_write_next(cdata, tdata, event_loop);
}

static void
random_batch_listener(as_error* err, as_batch_read_records* records, void* udata, as_event_loop* event_loop)
{
	threaddata* tdata = udata;
	clientdata* cdata = tdata->cdata;
	
	if (!err) {
		if (cdata->latency || cdata->histogram_output != NULL) {
			uint64_t end = cf_getus();
			if (cdata->latency) {
				latency_add(&cdata->read_latency, (end - tdata->begin) / 1000);
				hdr_record_value_atomic(cdata->read_hdr, end - tdata->begin);
			}
			if (cdata->histogram_output != NULL) {
				histogram_add(&cdata->read_histogram, end - tdata->begin);
			}
		}
		as_incr_uint32(&cdata->read_count);
	}
	else {
		if (err->code == AEROSPIKE_ERR_TIMEOUT) {
			as_incr_uint32(&cdata->read_timeout_count);
		}
		else {
			as_incr_uint32(&cdata->read_error_count);
			
			if (cdata->debug) {
				blog_error("Batch error: ns=%s set=%s key=%d bin=%s code=%d message=%s",
						   cdata->namespace, cdata->set, tdata->key.value.integer.value,
						   cdata->bin_name, err->code, err->message);
			}
		}
	}
	as_batch_read_destroy(records);
	random_read_write_next(cdata, tdata, event_loop);
}
