
//==========================================================
// Includes.
//

#include <assert.h>
#include <stdio.h>

#include <aerospike/as_arraylist.h>
#include <aerospike/as_bytes.h>
#include <aerospike/as_double.h>
#include <aerospike/as_hashmap.h>
#include <aerospike/as_integer.h>
#include <aerospike/as_list.h>
#include <aerospike/as_hashmap_iterator.h>
#include <aerospike/as_pair.h>
#include <aerospike/as_string.h>
#include <aerospike/as_vector.h>
#include <citrusleaf/alloc.h>

#ifdef _TEST
// include libcheck so we can ck_assert in the validation methods below
#include <check.h>
#endif /* _TEST */

#include <common.h>
#include <object_spec.h>


//==========================================================
// Typedefs & constants.
//

// parsing a list currently
#define CONSUMER_TYPE_LIST 0x0
// parsing a map currently
#define CONSUMER_TYPE_MAP  0x1

// in a map and expecting the key next
#define MAP_KEY  0x0
// in a map and expecting the value next
#define MAP_VAL  0x1
// key and value already given, expecing the map to be closed with a '}'
#define MAP_DONE 0x2

struct consumer_state_s {
	// the character which denotes the end of this bin spec
	char delimiter;

	// one of CONSUMER_TYPE_LIST or CONSUMER_TYPE_MAP
	uint8_t type;

	// used only for the map, can be one of MAP_KEY, MAP_VAL, or MAP_DONE
	uint8_t state;

	// a pointer to the bin spec that is being built at this layer
	struct bin_spec_s* bin_spec;

	struct consumer_state_s* parent;

	union {
		struct {
			// the vector of bin_specs used to build a list
			as_vector* list_builder;

			// the length of the list, accounting for multipliers
			uint32_t list_len;
		};

		// the key/value bin_specs used to build a map
		struct {
			struct bin_spec_s* key;
			struct bin_spec_s* value;
		};
	};
};


//==========================================================
// Forward declarations.
//

LOCAL_HELPER uint32_t bin_spec_map_n_entries(const struct bin_spec_s* b);
LOCAL_HELPER void _print_parse_error(const char* err_msg, const char* obj_spec_str,
		const char* err_loc);
LOCAL_HELPER void bin_spec_free(struct bin_spec_s* bin_spec);
LOCAL_HELPER void _destroy_consumer_states(struct consumer_state_s* state);
LOCAL_HELPER int _parse_bin_types(as_vector* bin_specs, uint32_t* n_bins,
		const char* const obj_spec_str);
LOCAL_HELPER void bin_spec_free(struct bin_spec_s* bin_spec);
LOCAL_HELPER as_val* _gen_random_int(uint8_t range, as_random* random);
LOCAL_HELPER uint64_t raw_to_alphanum(uint64_t n);
LOCAL_HELPER as_val* _gen_random_str(uint32_t length, as_random* random);
LOCAL_HELPER as_val* _gen_random_bytes(uint32_t length, as_random* random,
		float compression_ratio);
LOCAL_HELPER as_val* _gen_random_double(as_random* random);
LOCAL_HELPER as_val* _gen_random_list(const struct bin_spec_s* bin_spec,
		as_random* random, float compression_ratio);
LOCAL_HELPER as_val* _gen_random_map(const struct bin_spec_s* bin_spec,
		as_random* random, float compression_ratio);
LOCAL_HELPER as_val* bin_spec_random_val(const struct bin_spec_s* bin_spec,
		as_random* random, float compression_ratio);
LOCAL_HELPER size_t _sprint_bin(const struct bin_spec_s* bin, char** out_str,
		size_t str_size);

#ifdef _TEST
LOCAL_HELPER void _dbg_validate_int(uint8_t range, as_integer* as_val);
LOCAL_HELPER void _dbg_validate_string(uint32_t length, as_string* as_val);
LOCAL_HELPER void _dbg_validate_bytes(uint32_t length, as_bytes* as_val);
LOCAL_HELPER void _dbg_validate_double(as_double* as_val);
LOCAL_HELPER void _dbg_validate_list(const struct bin_spec_s* bin_spec,
		const as_list* as_val);
LOCAL_HELPER void _dbg_validate_map(const struct bin_spec_s* bin_spec,
		const as_map* val);
#endif /* _TEST */


//==========================================================
// Inlines and macros.
//

/*
 * converts a bytevector of 8 values between 0-35 to a bytevector of 8
 * alphanumeric characters
 */
LOCAL_HELPER inline uint64_t
raw_to_alphanum(uint64_t n)
{
	uint64_t x, y;
	// offset each value so 0-9 don't have the 6'th bit set and the rest do
	n += 0x3636363636363636LU;
	// take each value with the 6'th bit set (designated to be alphabetical
	// characters) and make another bytevector with their first bit set
	x = (n >> 6) & 0x0101010101010101LU;
	// take each value whose first bit isn't set in x and turn it into 0x7a
	// (which will act like -6 when we add in the end)
	y = (x + 0x7f7f7f7f7f7f7f7fLU) & 0x7a7a7a7a7a7a7a7aLU;
	// turn each byte in x with value 0x01 into 0x31
	x |= x << 5;
	n += x + y;
	return n & 0x7f7f7f7f7f7f7f7fLU;
}

/*
 * safe printing to a fixed-size buffer, updating the size of the buffer
 */
#define sprint(out_str, str_size, ...) \
	do { \
		size_t __w = snprintf(*(out_str), str_size, __VA_ARGS__); \
		*(out_str) += (str_size > __w ? __w : str_size); \
		str_size = (str_size > __w ? str_size - __w : 0); \
	} while (0)


//==========================================================
// Public API.
//

int
obj_spec_parse(struct obj_spec_s* base_obj, const char* obj_spec_str)
{
	int err;
	// use as_vector to build the list of bin_specs, as it has dynamic sizing
	as_vector bin_specs;
	uint32_t n_bins;

	// begin with a capacity of 8
	as_vector_inita(&bin_specs, sizeof(struct bin_spec_s),
			DEFAULT_LIST_BUILDER_CAPACITY);

	err = _parse_bin_types(&bin_specs, &n_bins, obj_spec_str);
	if (!err) {
		// copy the vector into base_obj before cleaning up
		base_obj->bin_specs = as_vector_to_array(&bin_specs, &base_obj->n_bin_specs);

		// n_bins is initialized by _parse_bin_types
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
		base_obj->n_bin_specs = n_bins;
#pragma GCC diagnostic pop

		base_obj->valid = true;
	}
	else {
		base_obj->valid = false;
	}

	as_vector_destroy(&bin_specs);
	return err;
}

void
obj_spec_free(struct obj_spec_s* obj_spec)
{
	if (obj_spec->valid) {
		for (uint32_t i = 0, cnt = 0; cnt < obj_spec->n_bin_specs; i++) {
			cnt += obj_spec->bin_specs[i].n_repeats;
			bin_spec_free(&obj_spec->bin_specs[i]);
		}
		cf_free(obj_spec->bin_specs);
		obj_spec->valid = false;
	}
}


void
obj_spec_move(struct obj_spec_s* dst, struct obj_spec_s* src)
{
	// you can only move from a valid src
	assert(src->valid);
	__builtin_memcpy(dst, src, offsetof(struct obj_spec_s, valid));
	dst->valid = true;
	src->valid = false;
}


void
obj_spec_shallow_copy(struct obj_spec_s* dst, const struct obj_spec_s* src)
{
	__builtin_memcpy(dst, src, offsetof(struct obj_spec_s, valid));
	dst->valid = false;
}


uint32_t
obj_spec_n_bins(const struct obj_spec_s* obj_spec)
{
	return obj_spec->n_bin_specs;
}

bool
obj_spec_bin_name_compatible(const obj_spec_t* obj_spec, const char* bin_name)
{
	if (bin_name_too_large(strlen(bin_name), obj_spec->n_bin_specs)) {
		// if the key name is too long to fit every key we'll end up generating
		// in an as_bin_name, return an error
		if (obj_spec->n_bin_specs == 1) {
			fprintf(stderr, "Key name \"%s\" will exceed the maximum number of "
					"allowed characters in a single bin (%s)\n",
					bin_name, bin_name);
		}
		else {
			fprintf(stderr, "Key name \"%s\" will exceed the maximum number of "
					"allowed characters in a single bin (%s_%d)\n",
					bin_name, bin_name, obj_spec->n_bin_specs);
		}
		return false;
	}
	return true;
}

int
obj_spec_populate_bins(const struct obj_spec_s* obj_spec, as_record* rec,
		as_random* random, const char* bin_name, uint32_t* write_bins,
		uint32_t n_write_bins, float compression_ratio)
{
	uint32_t n_bin_specs =
		write_bins == NULL ? obj_spec->n_bin_specs : n_write_bins;
	as_bins* bins = &rec->bins;

	if (n_bin_specs > bins->capacity) {
		fprintf(stderr, "Not enough bins allocated for obj_spec\n");
		return -1;
	}

	if (write_bins == NULL) {
		for (uint32_t i = 0, cnt = 0; cnt < n_bin_specs; i++) {
			const struct bin_spec_s* bin_spec = &obj_spec->bin_specs[i];

			for (uint32_t j = 0; j < bin_spec->n_repeats; j++, cnt++) {
				as_val* val = bin_spec_random_val(bin_spec, random,
						compression_ratio);

				if (val == NULL) {
					return -1;
				}

				as_bin_name name;
				gen_bin_name(name, bin_name, cnt);
				if (!as_record_set(rec, name, (as_bin_value*) val)) {
					// failed to set a record, meaning we ran out of space
					fprintf(stderr, "Not enough free bin slots in record\n");
					return -1;
				}
			}
		}
	}
	else {
		uint32_t k = 0;
		uint32_t tot = 0;
		uint32_t next_idx = write_bins[k];

		for (uint32_t i = 0; tot < obj_spec->n_bin_specs; i++) {
			const struct bin_spec_s* bin_spec = &obj_spec->bin_specs[i];

			for (;;) {
				if (tot + bin_spec->n_repeats <= next_idx) {
					tot += bin_spec->n_repeats;
					break;
				}
				else {
					as_val* val = bin_spec_random_val(bin_spec, random,
							compression_ratio);

					if (val == NULL) {
						return -1;
					}

					as_bin_name name;
					gen_bin_name(name, bin_name, next_idx);
					if (!as_record_set(rec, name, (as_bin_value*) val)) {
						// failed to set a record, meaning we ran out of space
						fprintf(stderr, "Not enough free bin slots in record\n");
						return -1;
					}

					k++;
					if (k >= n_write_bins) {
						goto finished_generating;
					}

					next_idx = write_bins[k];
				}
			}
		}

		if (tot == n_bin_specs) {
			// this should never happen if write_bins are valid (which is
			// checked for during initialization)
			fprintf(stderr, "Failed to generate record with write-bins\n");
			return -1;
		}

finished_generating: ;
	}

	return 0;
}


as_val*
obj_spec_gen_value(const struct obj_spec_s* obj_spec, as_random* random,
		uint32_t* write_bins, uint32_t n_write_bins)
{
	return obj_spec_gen_compressible_value(obj_spec, random, write_bins,
			n_write_bins, 1.f);
}

as_val*
obj_spec_gen_compressible_value(const struct obj_spec_s* obj_spec,
		as_random* random, uint32_t* write_bins, uint32_t n_write_bins,
		float compression_ratio)
{

	if (write_bins == NULL) {
		struct bin_spec_s tmp_list;
		tmp_list.type = BIN_SPEC_TYPE_LIST;
		tmp_list.list.length = obj_spec->n_bin_specs;
		tmp_list.list.list = obj_spec->bin_specs;
		return bin_spec_random_val(&tmp_list, random, compression_ratio);
	}
	else {
		uint32_t k = 0;
		uint32_t tot = 0;
		uint32_t next_idx = write_bins[k];

		as_arraylist* list = as_arraylist_new(n_write_bins, 0);

		for (uint32_t i = 0; tot < obj_spec->n_bin_specs; i++) {
			const struct bin_spec_s* bin_spec = &obj_spec->bin_specs[i];

			for (;;) {
				if (tot + bin_spec->n_repeats <= next_idx) {
					tot += bin_spec->n_repeats;
					break;
				}
				else {
					as_val* val = bin_spec_random_val(bin_spec, random,
							compression_ratio);

					if (val == NULL) {
						as_list_destroy((as_list*) list);
						return NULL;
					}

					as_list_append((as_list*) list, val);

					k++;
					if (k >= n_write_bins) {
						goto finished_generating_val;
					}

					next_idx = write_bins[k];
				}
			}
		}

		if (tot == obj_spec->n_bin_specs) {
			// this should never happen if write_bins are valid (which is
			// checked for during initialization)
			fprintf(stderr, "Failed to generate value with write-bins\n");
			as_list_destroy((as_list*) list);
			return NULL;
		}
finished_generating_val:
		return (as_val*) list;
	}
}

void
snprint_obj_spec(const struct obj_spec_s* obj_spec, char* out_str,
		size_t str_size)
{
	for (uint32_t i = 0, cnt = 0; cnt < obj_spec->n_bin_specs; i++) {
		str_size = _sprint_bin(&obj_spec->bin_specs[i], &out_str, str_size);

		cnt += obj_spec->bin_specs[i].n_repeats;

		if (cnt != obj_spec->n_bin_specs && str_size > 0) {
			sprint(&out_str, str_size, ",");
		}
	}
}

#ifdef _TEST

void
_dbg_obj_spec_assert_valid(const struct obj_spec_s* obj_spec,
		const as_record* rec, uint32_t* write_bins, uint32_t n_write_bins,
		const char* bin_name)
{
	as_bin_name name;

	if (write_bins == NULL) {
		for (uint32_t i = 0, cnt = 0; cnt < obj_spec->n_bin_specs; i++) {
			const struct bin_spec_s* bin_spec = &obj_spec->bin_specs[i];

			for (uint32_t j = 0; j < bin_spec->n_repeats; j++, cnt++) {
				gen_bin_name(name, bin_name, cnt);

				as_val* val = (as_val*) as_record_get(rec, name);
				ck_assert_msg(val != NULL, "expected a record in bin \"%s\"",
						name);
				_dbg_validate_bin_spec(bin_spec, val);
			}
		}
	}
	else {
		uint32_t k = 0;
		uint32_t tot = 0;
		uint32_t next_idx = write_bins[k];

		for (uint32_t i = 0; tot < obj_spec->n_bin_specs; i++) {
			const struct bin_spec_s* bin_spec = &obj_spec->bin_specs[i];

			for (;;) {
				if (tot + bin_spec->n_repeats <= next_idx) {
					tot += bin_spec->n_repeats;
					break;
				}
				else {
					gen_bin_name(name, bin_name, next_idx);

					as_val* val = (as_val*) as_record_get(rec, name);
					ck_assert_msg(val != NULL, "expected a record in bin "
							"\"%s\"", name);
					_dbg_validate_bin_spec(bin_spec, val);

					if (k >= n_write_bins) {
						goto finished_validating;
					}

					next_idx = write_bins[k++];
				}
			}
		}

finished_validating:;
	}
}

void
_dbg_validate_bin_spec(const struct bin_spec_s* bin_spec, const as_val* val)
{
	switch (bin_spec->type) {
		case BIN_SPEC_TYPE_INT:
			_dbg_validate_int(bin_spec->integer.range, as_integer_fromval(val));
			break;
		case BIN_SPEC_TYPE_STR:
			_dbg_validate_string(bin_spec->string.length, as_string_fromval(val));
			break;
		case BIN_SPEC_TYPE_BYTES:
			_dbg_validate_bytes(bin_spec->string.length, as_bytes_fromval(val));
			break;
		case BIN_SPEC_TYPE_DOUBLE:
			_dbg_validate_double(as_double_fromval(val));
			break;
		case BIN_SPEC_TYPE_LIST:
			_dbg_validate_list(bin_spec, as_list_fromval((as_val*) val));
			break;
		case BIN_SPEC_TYPE_MAP:
			_dbg_validate_map(bin_spec, as_map_fromval(val));
			break;
		default:
			ck_assert_msg(0, "unknown bin_spec type (%d)", bin_spec->type);
	}
}

#endif /* _TEST */


//==========================================================
// Local helpers.
//

/*
 * gives the number of entries to put in the map
 */
LOCAL_HELPER uint32_t
bin_spec_map_n_entries(const struct bin_spec_s* b)
{
	return b->map.key->n_repeats;
}

LOCAL_HELPER void
_print_parse_error(const char* err_msg, const char* obj_spec_str,
		const char* err_loc)
{
	const char* last_newline;
	const char* next_newline;

	last_newline = memrchr(obj_spec_str, '\n',
			((uint64_t) err_loc) - ((uint64_t) obj_spec_str));
	next_newline = strchrnul(err_loc, '\n');

	if (last_newline == NULL) {
		last_newline = obj_spec_str - 1;
	}

	int32_t line_length = (int32_t)
		(((uint64_t) next_newline) - ((uint64_t) last_newline) - 1);
	int32_t err_offset = (int32_t)
		(((uint64_t) err_loc) - ((uint64_t) last_newline) - 1);

	fprintf(stderr,
			"Object Spec parse error: %s\n"
			"%*s\n"
			"%*s^\n",
			err_msg,
			line_length, last_newline + 1,
			err_offset, "");
}

/*
 * to be called when an error is encountered while parsing, and cleanup of the
 * consumer state managers and bin_specs is necessary
 */
LOCAL_HELPER void
_destroy_consumer_states(struct consumer_state_s* state)
{
	struct consumer_state_s* parent;
	as_vector* list_builder;

	while (state != NULL) {
		switch (state->type) {
			case CONSUMER_TYPE_LIST:
				// go through everything in list_builder that has, so far, been
				// fully initialized
				list_builder = state->list_builder;
				for (uint32_t i = 0;
						(int32_t) i < (int32_t) (list_builder->size - 1); i++) {
					// since all bin_spec objects but the last have been fully
					// initialized, we can call free on each
					bin_spec_free(
							(struct bin_spec_s*) as_vector_get(list_builder, i));
				}
				// the last bin_spec is guaranteed to not be initialized yet,
				// so ignore it
				as_vector_destroy(list_builder);
				break;
			case CONSUMER_TYPE_MAP:
				// free the key/value if they've been initialized
				switch (state->state) {
					case MAP_DONE:
						bin_spec_free(state->value);
						cf_free(state->value);
						// fall through to free key too
					case MAP_VAL:
						bin_spec_free(state->key);
						cf_free(state->key);
					case MAP_KEY:
						break;
					default:
						__builtin_unreachable();
				}
				break;
			default:
				__builtin_unreachable();
		}

		parent = state->parent;
		// since the root state is allocated on the stack, don't try freeing it
		if (parent != NULL) {
			cf_free(state);
		}
		state = parent;
	}
}


LOCAL_HELPER int
_parse_bin_types(as_vector* bin_specs, uint32_t* n_bins,
		const char* const obj_spec_str)
{
	struct consumer_state_s begin_state;
	struct consumer_state_s* state;
	struct bin_spec_s* bin_spec;
	const char* str;

	begin_state.delimiter = '\0';
	begin_state.type = CONSUMER_TYPE_LIST;
	begin_state.bin_spec = NULL;
	begin_state.parent = NULL;
	begin_state.list_builder = bin_specs;
	begin_state.list_len = 0;

	state = &begin_state;
	str = obj_spec_str;

	do {
		uint8_t delim;
		uint8_t type;
		uint8_t map_state;
		as_vector* list_builder;

		delim = state->delimiter;
		type = state->type;

		// this variable will be initialized when reading a map, but we always
		// start with a list so it will technically be uninitialized first
		// time around, which gcc will complain about
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
		map_state = state->state;
#pragma GCC diagnostic pop

		if (*str == delim) {
			// we are finished parsing this collection, commit results back to
			// the bin_spec object being built
			bin_spec = state->bin_spec;
			if (bin_spec != NULL) {
				switch (type) {
					case CONSUMER_TYPE_LIST:
						bin_spec->type = BIN_SPEC_TYPE_LIST;
						bin_spec->list.list =
							as_vector_to_array(state->list_builder,
									&bin_spec->list.length);
						// the length we'll be storing is the effective length,
						// so overwrite what was stored by as_vector_to_array
						bin_spec->list.length = state->list_len;
						as_vector_destroy(state->list_builder);
						break;
					case CONSUMER_TYPE_MAP:
						bin_spec->type = BIN_SPEC_TYPE_MAP;
						if (map_state != MAP_DONE) {
							// create an empty map (i.e. a map with n_keys == 0)
							bin_spec->map.key = (struct bin_spec_s*)
								cf_malloc(sizeof(struct bin_spec_s));
							bin_spec->map.key->n_repeats = 0;
							// also create a val since it will be freed
							bin_spec->map.val = (struct bin_spec_s*)
								cf_malloc(sizeof(struct bin_spec_s));

							// set the types of both key and val to be scalar so
							// they are not recursively freed
							bin_spec->map.key->type = BIN_SPEC_TYPE_INT;
							bin_spec->map.val->type = BIN_SPEC_TYPE_INT;
						}
						else {
							bin_spec->map.key = state->key;
							bin_spec->map.val = state->value;
						}
						break;
					default:
						// this is an impossible condition
						__builtin_unreachable();
				}
				str++;
			}
			struct consumer_state_s* new_state = state->parent;
			if (new_state != NULL) {
				cf_free(state);

				delim = new_state->delimiter;
				type = new_state->type;
				map_state = new_state->state;
			}
			else {
				// this is the root-level list, so commit the effective length
				// to the n_bins variable passed by the caller
				*n_bins = state->list_len;
			}
			state = new_state;
		}
		else {

			// consume the next bin_type
			switch (type) {
				case CONSUMER_TYPE_LIST:
					list_builder = state->list_builder;
					bin_spec = (struct bin_spec_s*) as_vector_reserve(list_builder);
					break;
				case CONSUMER_TYPE_MAP:
					bin_spec =
						(struct bin_spec_s*) cf_malloc(sizeof(struct bin_spec_s));
					assert((((uint64_t) bin_spec) & BIN_SPEC_TYPE_MASK) == 0);
					switch (map_state) {
						case MAP_KEY:
							state->key = bin_spec;
							break;
						case MAP_VAL:
							state->value = bin_spec;
							break;
						default:
							// not actually possible to reach this case, since
							// it's checked for in the previous iteration of the
							// loop
							__builtin_unreachable();
					}
					break;
				default:
					// this is an impossible condition
					__builtin_unreachable();
			}

			// first, check to see if a multiplier has been applied to this
			// bin_type
			uint64_t mult;
			char* endptr;
			mult = strtoul(str, &endptr, 10);
			if (*str >= '0' && *str <= '9' && endptr != str) {

				if (type == CONSUMER_TYPE_MAP && map_state == MAP_VAL &&
						mult != 1) {
					_print_parse_error("Map value cannot have a multiplier",
							obj_spec_str, str);
					goto _destroy_state;
				}

				if ((type != CONSUMER_TYPE_MAP || map_state != MAP_KEY) &&
						mult == 0) {
					_print_parse_error("Cannot have a multiplier of 0 except "
							"on map keys (to indicate an empty map)",
							obj_spec_str, str);
					goto _destroy_state;
				}

				if (((uint32_t) mult) != mult) {
					_print_parse_error("Multiplier exceeds maximum unsigned "
							"32-bit integer value",
							obj_spec_str, str);
					goto _destroy_state;
				}

				// a multiplier has been specified, expect a '*' next, followed by
				// the bin_spec
				str = endptr;
				if (*str == ' ') {
					str++;
				}
				if (*str != '*') {
					_print_parse_error("Expect a '*' to follow a multiplier",
							obj_spec_str, str);
					goto _destroy_state;
				}
				str++;
				if (*str == ' ') {
					str++;
				}
			}
			else {
				// no multiplier has been specified, default to 1
				mult = 1;
			}

			if (type == CONSUMER_TYPE_LIST) {
				uint32_t new_list_len = state->list_len + mult;
				if (new_list_len < state->list_len) {
					// we overflowed! That means too many elements were placed
					// in a single list (> 2^32)
					_print_parse_error("Too many elements in a list (>= 2^32)",
							obj_spec_str, str);
					goto _destroy_state;
				}
				state->list_len = new_list_len;
			}
			bin_spec->n_repeats = mult;
			switch (*str) {
				case 'I': {
					uint8_t next_char = *(str + 1) - '1';
					if (next_char > BIN_SPEC_MAX_INT_RANGE) {
						// default range is 4
						next_char = BIN_SPEC_DEFAULT_INT_RANGE;
					}
					else {
						str++;
					}
					bin_spec->type = BIN_SPEC_TYPE_INT;
					bin_spec->integer.range = next_char;

					str++;
					break;
				}
				case 'S': {
					uint64_t str_len;
					char* endptr;
					str_len = strtoul(str + 1, &endptr, 10);
					if (endptr == str + 1) {
						_print_parse_error("Expect a number following an 'S' specifier",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					if (str_len != (uint32_t) str_len) {
						_print_parse_error("Invalid string length",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					bin_spec->type = BIN_SPEC_TYPE_STR;
					bin_spec->string.length = (uint32_t) str_len;

					str = endptr;
					break;
				}
				case 'B': {
					uint64_t bytes_len;
					char* endptr;
					bytes_len = strtoul(str + 1, &endptr, 10);
					if (endptr == str + 1) {
						_print_parse_error("Expect a number following a 'B' specifier",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					if (bytes_len != (uint32_t) bytes_len) {
						_print_parse_error("Invalid bytes length",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					bin_spec->type = BIN_SPEC_TYPE_BYTES;
					bin_spec->bytes.length = (uint32_t) bytes_len;

					str = endptr;
					break;
				}
				case 'D':
					bin_spec->type = BIN_SPEC_TYPE_DOUBLE;
					str++;
					break;
				case '[': {
					if (type == CONSUMER_TYPE_MAP && map_state == MAP_KEY) {
						_print_parse_error("Map key must be scalar type, "
								"cannot be list",
								obj_spec_str, str);
						goto _destroy_state;
					}
					// begin list parse
					as_vector* list_builder = as_vector_create(sizeof(struct bin_spec_s),
							DEFAULT_LIST_BUILDER_CAPACITY);
					struct consumer_state_s* list_state =
						(struct consumer_state_s*) cf_malloc(sizeof(struct consumer_state_s));
					list_state->delimiter = ']';
					list_state->type = CONSUMER_TYPE_LIST;
					list_state->bin_spec = bin_spec;
					list_state->parent = state;
					list_state->list_builder = list_builder;
					list_state->list_len = 0;

					str++;
					state = list_state;

					// allow a space after the '['
					if (*str == ' ') {
						str++;
					}
					continue;
				}
				case '{': {
					if (type == CONSUMER_TYPE_MAP && map_state == MAP_KEY) {
						_print_parse_error("Map key must be scalar type, "
								"cannot be map",
								obj_spec_str, str);
						goto _destroy_state;
					}
					// begin map parse
					struct consumer_state_s* map_state =
						(struct consumer_state_s*) cf_malloc(sizeof(struct consumer_state_s));
					map_state->delimiter = '}';
					map_state->type = CONSUMER_TYPE_MAP;
					map_state->state = MAP_KEY;
					map_state->bin_spec = bin_spec;
					map_state->parent = state;

					str++;
					state = map_state;

					// allow a space after the '{'
					if (*str == ' ') {
						str++;
					}
					continue;
				}
				default:
					_print_parse_error("Expect 'I', 'S', 'B', or 'D' specifier, "
							"or a list/map",
							obj_spec_str, str);
					goto _destroy_state;
			}

			if (0) {
_destroy_state:
				if (type == CONSUMER_TYPE_MAP) {
					cf_free(bin_spec);
				}
				_destroy_consumer_states(state);
				return -1;
			}
		}

		switch (type) {
			case CONSUMER_TYPE_LIST:
				if (*str == ',') {
					str++;

					// allow a space after a comma
					if (*str == ' ') {
						str++;
					}
				}
				else {
					// allow a space before the ']'
					if (*str == ' ') {
						str++;
					}

					if (*str != delim) {
						_print_parse_error("Expect ',' separating bin specifiers in a list",
								obj_spec_str, str);
						_destroy_consumer_states(state);
						return -1;
					}
				}
				break;
			case CONSUMER_TYPE_MAP:
				// advance map to the next state since the key/value for this
				// state has now been fully initialized
				state->state++;

				switch (map_state) {
					case MAP_KEY:
						// allow a space before the ':'
						if (*str == ' ') {
							str++;
						}
						if (*str != ':') {
							_print_parse_error("Expect ':' separating key and "
									"value pair in a map",
									obj_spec_str, str);
							_destroy_consumer_states(state);
							return -1;
						}
						str++;
						// allow a space after the ':'
						if (*str == ' ') {
							str++;
						}
						break;
					case MAP_VAL:
						// allow a space before the '}'
						if (*str == ' ') {
							str++;
						}
						if (*str != delim) {
							_print_parse_error("Expect '}' after key/value "
									"pair specifier in a map",
									obj_spec_str, str);
							_destroy_consumer_states(state);
							return -1;
						}
						break;
				}
				break;
			default:
				// this is an impossible condition
				__builtin_unreachable();
		}

	} while (state != NULL);

	return 0;
}

LOCAL_HELPER void
bin_spec_free(struct bin_spec_s* bin_spec)
{
	switch (bin_spec->type) {
		case BIN_SPEC_TYPE_INT:
		case BIN_SPEC_TYPE_STR:
		case BIN_SPEC_TYPE_BYTES:
		case BIN_SPEC_TYPE_DOUBLE:
			// no-op, scalar types use no disjointed memory
			break;
		case BIN_SPEC_TYPE_LIST:
			for (uint32_t i = 0, cnt = 0; cnt < bin_spec->list.length; i++) {
				cnt += bin_spec->list.list[i].n_repeats;
				bin_spec_free(&bin_spec->list.list[i]);
			}
			cf_free(bin_spec->list.list);
			break;
		case BIN_SPEC_TYPE_MAP:
			bin_spec_free(bin_spec->map.key);
			cf_free(bin_spec->map.key);
			bin_spec_free(bin_spec->map.val);
			cf_free(bin_spec->map.val);
			break;
	}
}

LOCAL_HELPER as_val*
_gen_random_int(uint8_t range, as_random* random)
{
	as_integer* val;
	uint64_t r;
	uint64_t min;
	uint64_t range_size;
	if (range > 7) {
		fprintf(stderr, "bin_spec integer range must be between 0-7, got %u\n",
				range);
		return NULL;
	}

	/*
	 * min values for ranges are:
	 * 	0: 0
	 * 	1: 256
	 * 	2: 2^16
	 * 	3: 2^24
	 * 	...
	 * 	so they are just 2^(8*range), with the exception of 0
	 *
	 * 	and the sizes of each range is just 2^(8*range + 8) - 2^(8*range) =
	 * 	    0xff << (8*range), with the exception of range 0, which has one
	 * 	    extra possible value (0)
	 */
	min = (0x1LU << (range * 8)) & ~0x1LU;
	range_size = (0xffLU << (range * 8)) + !range;

	r = gen_rand_range_64(random, range_size) + min;

	val = as_integer_new(r);
	return (as_val*) val;
}

/*
 * the number of random alphanumeric digits we can pull from 64 bits of random
 * data
 */
#define ALPHANUM_PER_64_BITS 12LU
/*
 * the number of possible alphanumeric characters [a-z1-9]
 */
#define N_ALPHANUM 36LU

/*
 * the maximum allowable seed (to prevent modulo bias)
 * i.e. 36**12
 */
#define MAX_SEED 4738381338321616896LU

LOCAL_HELPER as_val*
_gen_random_str(uint32_t length, as_random* random)
{
	as_string* val;
	char* buf;
	uint32_t i = 0, j;

	buf = (char*) cf_malloc(length + 1);

	// take groups of 24 characters and batch-generate all random alphanumeric
	// values with just 2 random 64-bit values
	while (i + (2 * ALPHANUM_PER_64_BITS) <= length) {
		uint64_t s1 = gen_rand_range_64(random, MAX_SEED);
		uint64_t s2 = gen_rand_range_64(random, MAX_SEED);

		// c1-c3 will be 3 vectors of 8 byte-values, which range from 0-35
		uint64_t c1 = (s1 % N_ALPHANUM);
		for (uint32_t k = 1; k < 8; k++) {
			s1 /= N_ALPHANUM;
			c1 |= (s1 % N_ALPHANUM) << (k * 8);
		}

		s1 /= N_ALPHANUM;
		uint64_t c2 = (s1 % N_ALPHANUM);
		for (uint32_t k = 1; k < 4; k++) {
			s1 /= N_ALPHANUM;
			c2 |= (s1 % N_ALPHANUM) << (k * 8);
		}
		for (uint32_t k = 4; k < 8; k++) {
			c2 |= (s2 % N_ALPHANUM) << (k * 8);
			s2 /= N_ALPHANUM;
		}

		uint64_t c3 = (s2 % N_ALPHANUM);
		for (uint32_t k = 1; k < 8; k++) {
			s2 /= N_ALPHANUM;
			c3 |= (s2 % N_ALPHANUM) << (k * 8);
		}

		// turn each of these byte-vectors with values 0-35 into byte-vectors
		// of alphanumeric characters
		c1 = raw_to_alphanum(c1);
		c2 = raw_to_alphanum(c2);
		c3 = raw_to_alphanum(c3);

		// write them to memory
		*((uint64_t*) &buf[i + 0])  = c1;
		*((uint64_t*) &buf[i + 8])  = c2;
		*((uint64_t*) &buf[i + 16]) = c3;

		i += 2 * ALPHANUM_PER_64_BITS;
	}
	// for the remaining characters, use a single randomly generated value to
	// make sets of 12 characters
	while (i < length) {
		uint64_t r;
		uint32_t sz = MIN(length - i, ALPHANUM_PER_64_BITS);
		uint64_t s = gen_rand_range_64(random, MAX_SEED);

		r = s % N_ALPHANUM;
		buf[i] = r < 10 ? r + '0' : r + 'a' - 10;
		for (j = 1; j < sz; j++) {
			s /= N_ALPHANUM;
			r = s % N_ALPHANUM;
			buf[i + j] = r < 10 ? r + '0' : r + 'a' - 10;
		}
		i += sz;
	}

	// null-terminate the string
	buf[length] = '\0';

	val = as_string_new_wlen(buf, length, 1);
	return (as_val*) val;
}

LOCAL_HELPER as_val*
_gen_random_bytes(uint32_t length, as_random* random, float compression_ratio)
{
	as_bytes* val;
	uint8_t* buf;

	buf = (uint8_t*) cf_calloc(1, length);
	uint32_t c_len = (uint32_t) (compression_ratio * length);
	as_random_next_bytes(random, buf, c_len);

	val = as_bytes_new_wrap(buf, length, 1);
	return (as_val*) val;
}

LOCAL_HELPER as_val*
_gen_random_double(as_random* random)
{
	as_double* val;
	// for now, just generate random uint64 and reinterpret as double
	uint64_t bytes = as_random_next_uint64(random);
	val = as_double_new(*(double*) &bytes);
	return (as_val*) val;
}

LOCAL_HELPER as_val*
_gen_random_list(const struct bin_spec_s* bin_spec, as_random* random,
		float compression_ratio)
{
	as_arraylist* list;

	/*
	 * we'll build the list as an arraylist, which we can't do directly in the
	 * bin, since the bin is only large enough to hold as_list fields
	 */
	list = as_arraylist_new(bin_spec->list.length, 0);

	// iterate over the list elements and recursively generate their
	// values
	for (uint32_t i = 0, cnt = 0; cnt < bin_spec->list.length; i++) {
		const struct bin_spec_s* ele_bin = &bin_spec->list.list[i];

		for (uint32_t j = 0; j < ele_bin->n_repeats; j++, cnt++) {
			as_val* val = bin_spec_random_val(ele_bin, random,
					compression_ratio);

			if (val) {
				as_list_append((as_list*) list, val);
			}
			else {
				as_list_destroy((as_list*) list);
				return NULL;
			}
		}
	}

	return (as_val*) list;
}

LOCAL_HELPER as_val*
_gen_random_map(const struct bin_spec_s* bin_spec, as_random* random,
		float compression_ratio)
{
	as_hashmap* map;

	uint32_t n_entries = bin_spec_map_n_entries(bin_spec);

	map = as_hashmap_new(2 * n_entries);
	uint64_t retry_count = 0;

	for (uint32_t i = 0; i < n_entries; i++) {
		as_val* key;
		do {
			key = bin_spec_random_val(bin_spec->map.key, random,
					compression_ratio);
		} while (as_hashmap_get(map, key) != NULL &&
				retry_count++ < MAX_KEY_ENTRY_RETRIES);

		as_val* val = bin_spec_random_val(bin_spec->map.val, random,
				compression_ratio);

		as_hashmap_set(map, key, val);
	}

	return (as_val*) map;
}


LOCAL_HELPER as_val*
bin_spec_random_val(const struct bin_spec_s* bin_spec, as_random* random,
		float compression_ratio)
{
	as_val* val;
	switch (bin_spec->type) {
		case BIN_SPEC_TYPE_INT:
			val = _gen_random_int(bin_spec->integer.range, random);
			break;
		case BIN_SPEC_TYPE_STR:
			val = _gen_random_str(bin_spec->string.length, random);
			break;
		case BIN_SPEC_TYPE_BYTES:
			val = _gen_random_bytes(bin_spec->bytes.length, random,
					compression_ratio);
			break;
		case BIN_SPEC_TYPE_DOUBLE:
			val = _gen_random_double(random);
			break;
		case BIN_SPEC_TYPE_LIST:
			val = _gen_random_list(bin_spec, random, compression_ratio);
			break;
		case BIN_SPEC_TYPE_MAP:
			val = _gen_random_map(bin_spec, random, compression_ratio);
			break;
		default:
			fprintf(stderr, "Unknown bin_spec type (%d)\n", bin_spec->type);
			val = NULL;
	}

	return val;
}

LOCAL_HELPER size_t
_sprint_bin(const struct bin_spec_s* bin, char** out_str, size_t str_size)
{
	if (bin->n_repeats != 1) {
		sprint(out_str, str_size, "%d*", bin->n_repeats);
	}
	switch (bin->type) {
		case BIN_SPEC_TYPE_INT:
			sprint(out_str, str_size, "I%d", bin->integer.range + 1);
			break;
		case BIN_SPEC_TYPE_STR:
			sprint(out_str, str_size, "S%u", bin->string.length);
			break;
		case BIN_SPEC_TYPE_BYTES:
			sprint(out_str, str_size, "B%u", bin->bytes.length);
			break;
		case BIN_SPEC_TYPE_DOUBLE:
			sprint(out_str, str_size, "D");
			break;
		case BIN_SPEC_TYPE_LIST:
			sprint(out_str, str_size, "[");
			for (uint32_t i = 0, cnt = 0; cnt < bin->list.length; i++) {
				str_size = _sprint_bin(&bin->list.list[i], out_str, str_size);
				cnt += bin->list.list[i].n_repeats;
				if (cnt != bin->list.length) {
					sprint(out_str, str_size, ",");
				}
			}
			sprint(out_str, str_size, "]");
			break;
		case BIN_SPEC_TYPE_MAP:
			sprint(out_str, str_size, "{");
			if (bin_spec_map_n_entries(bin) != 0) {
				str_size = _sprint_bin(bin->map.key, out_str, str_size);
				sprint(out_str, str_size, ":");
				str_size = _sprint_bin(bin->map.val, out_str, str_size);
			}
			sprint(out_str, str_size, "}");
			break;
	}
	return str_size;
}

#ifdef _TEST

LOCAL_HELPER void
_dbg_validate_int(uint8_t range, as_integer* as_val)
{
	ck_assert_msg(as_val != NULL, "Expected an integer, got something else");
	ck_assert_msg(range <= 7, "invalid bin_spec integer range (%u)\n", range);

	uint64_t val = (uint64_t) as_integer_get(as_val);
	switch (range) {
		case 0:
			ck_assert_msg(0 <= val && val < 256, "Integer value (%lu) is out "
					"of range", val);
			break;
		case 1:
			ck_assert_msg(256 <= val && val < 65536, "Integer value (%lu) is "
					"out of range", val);
			break;
		case 2:
			ck_assert_msg(65536 <= val && val < 0x1000000, "Integer value "
					"(%lu) is out of range", val);
			break;
		case 3:
			ck_assert_msg(0x1000000 <= val && val < 0x100000000, "Integer "
					"value (%lu) is out of range", val);
			break;
		case 4:
			ck_assert_msg(0x100000000 <= val && val < 0x10000000000, "Integer "
					"value (%lu) is out of range", val);
			break;
		case 5:
			ck_assert_msg(0x10000000000 <= val && val < 0x1000000000000,
					"Integer value (%lu) is out of range", val);
			break;
		case 6:
			ck_assert_msg(0x1000000000000 <= val && val < 0x100000000000000,
					"Integer value (%lu) is out of range", val);
			break;
		case 7:
			ck_assert_msg(0x100000000000000 <= val,
					"Integer value (%lu) is out of range", val);
			break;
	}
}

LOCAL_HELPER void
_dbg_validate_string(uint32_t length, as_string* as_val)
{
	ck_assert_msg(as_val != NULL, "Expected a string, got something else");

	size_t str_len = as_string_len(as_val);
	ck_assert_int_eq(length, str_len);

	for (uint32_t i = 0; i < str_len; i++) {
		char c = as_string_get(as_val)[i];
		ck_assert(('a' <= c && c <= 'z') || ('0' <= c && c <= '9'));
	}
}

LOCAL_HELPER void
_dbg_validate_bytes(uint32_t length, as_bytes* as_val)
{
	ck_assert_msg(as_val != NULL, "Expected a bytes array, got something else");

	ck_assert_int_eq(length, as_bytes_size(as_val));
}

LOCAL_HELPER void
_dbg_validate_double(as_double* as_val)
{
	ck_assert_msg(as_val != NULL, "Expected a double, got something else");
}

LOCAL_HELPER void
_dbg_validate_list(const struct bin_spec_s* bin_spec, const as_list* as_val)
{
	ck_assert_msg(as_val != NULL, "Expected a list, got something else");
	size_t list_len = as_list_size(as_val);
	ck_assert_int_eq(list_len, bin_spec->list.length);

	for (uint32_t i = 0, cnt = 0; cnt < list_len; i++) {
		const struct bin_spec_s* ele_bin = &bin_spec->list.list[i];

		for (uint32_t j = 0; j < ele_bin->n_repeats; j++, cnt++) {
			_dbg_validate_bin_spec(ele_bin, as_list_get(as_val, cnt));
		}
	}
}


LOCAL_HELPER void
_dbg_validate_map(const struct bin_spec_s* bin_spec, const as_map* val)
{
	as_hashmap_iterator iter;

	ck_assert_msg(val != NULL, "Expected a map, got something else");
	uint32_t map_size = as_map_size(val);
	ck_assert_int_eq(map_size, bin_spec_map_n_entries(bin_spec));

	const struct bin_spec_s* key_spec = bin_spec->map.key;
	const struct bin_spec_s* val_spec = bin_spec->map.val;

	for (as_hashmap_iterator_init(&iter, (as_hashmap*) val);
			as_hashmap_iterator_has_next(&iter);) {
		const as_val* kv_pair = as_hashmap_iterator_next(&iter);
		const as_val* key = as_pair_1((as_pair*) kv_pair);
		const as_val* val = as_pair_2((as_pair*) kv_pair);

		_dbg_validate_bin_spec(key_spec, key);
		_dbg_validate_bin_spec(val_spec, val);
	}
}

#endif /* _TEST */

