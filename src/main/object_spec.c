
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

#define DEFAULT_LIST_BUILDER_CAPACITY 8


#define BIN_SPEC_TYPE_INT    0x0
#define BIN_SPEC_TYPE_STR    0x1
#define BIN_SPEC_TYPE_BYTES  0x2
#define BIN_SPEC_TYPE_DOUBLE 0x3
#define BIN_SPEC_TYPE_LIST   0x4
#define BIN_SPEC_TYPE_MAP    0x5

#define BIN_SPEC_TYPE_MASK 0x7

/*
 * the maximum int range is "I8", or a range index of 7
 */
#define BIN_SPEC_MAX_INT_RANGE 7
/*
 * the default int range for an unspecified int value (just "I") is "I4"
 */
#define BIN_SPEC_DEFAULT_INT_RANGE 3

/*
 * the maximum length of a word in randomly generated strings
 */
#define BIN_SPEC_MAX_STR_LEN 9

/*
 * when generating random map values, the maximum number of times we'll try
 * regenerating a new key for keys that already exist in the map before giving
 * up and just inserting whatever key is made
 */
#define MAX_KEY_ENTRY_RETRIES 1024


struct bin_spec_s {

	union {

		/*
		 * one of the four main types of bins:
		 *	int: a random int, within certain bounds (described below)
		 *	string: a string of fixed length, consisting of [a-z]{1,9}
		 *			space-separated words
		 *	bytes array: array of random bytes of data
		 *	double: any 8-byte double floating point
		 *	list: a list of bin_specs
		 *	map: a map from some type of scalar bin_spec to another bin_spec
		 */
		uint8_t type;

		struct {
			uint64_t __unused;
			/*
			 * integer range is between 0-7, and are defined as follows:
			 * 	0: values from 0 - 255
			 * 	1: values from 256 - 65536
			 * 	2: values from 65536 - 2^24-1
			 * 	3: values from 2^24 - 2^32-1
			 * 	4: values from 2^32 - 2^40-1
			 * 	5: values from 2^40 - 2^48-1
			 * 	6: values from 2^48 - 2^56-1
			 * 	7: values from 2^56 - 2^64-1
			 */
			uint8_t range;
		} integer;

		struct {
			uint64_t __unused;
			/*
			 * length of strings to be generated (excluding the
			 * null-terminating bit)
			 */
			uint32_t length;
		} string;

		struct {
			uint64_t __unused;
			/*
			 * number of random bytes
			 */
			uint32_t length;
		} bytes;

		struct {
			uint32_t __unused;
			/*
			 * a list of the types of elements in this list (in the order they
			 * appear in the list)
			 *
			 * note that this is the length of the list accounting for multiples
			 * of elements, and the true length of the list (as in the number of
			 * struct bin_spec pointers) is <= this value
			 */
			uint32_t length;
			struct bin_spec_s* list;
		} list;

		struct {
			/*
			 * the number of entries in a map is specified by the key's
			 * n_repeats field, so we don't need to put the number of elements
			 * to be generated this level of the struct
			 */

			/*
			 * a pointer to the key type
			 *
			 * the key pointers must be aligned by 8 bytes, since the first 3
			 * bits of this pointer is aliased by the type of this bin_spec
			 */
			struct bin_spec_s* key;
			/*
			 * a pointer to the value type
			 */
			struct bin_spec_s* val;
		} map;

	};

	uint32_t n_repeats;
};


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

static void bin_spec_set_key_and_type(struct bin_spec_s* b, struct bin_spec_s* key);
static uint8_t bin_spec_get_type(const struct bin_spec_s* b);
static struct bin_spec_s* bin_spec_get_key(const struct bin_spec_s* b);
static uint32_t bin_spec_map_n_entries(const struct bin_spec_s* b);
static void _print_parse_error(const char* err_msg, const char* obj_spec_str,
		const char* err_loc);
static void bin_spec_free(struct bin_spec_s* bin_spec);
static void _destroy_consumer_states(struct consumer_state_s* state);
static int _parse_bin_types(as_vector* bin_specs, uint32_t* n_bins,
		const char* const obj_spec_str);
static void bin_spec_free(struct bin_spec_s* bin_spec);
static as_val* _gen_random_int(uint8_t range, as_random* random);
static uint64_t raw_to_alphanum(uint64_t n);
static as_val* _gen_random_str(uint32_t length, as_random* random);
static as_val* _gen_random_bytes(uint32_t length, as_random* random,
		float compression_ratio);
static as_val* _gen_random_double(as_random* random);
static as_val* _gen_random_list(const struct bin_spec_s* bin_spec,
		as_random* random, float compression_ratio);
static as_val* _gen_random_map(const struct bin_spec_s* bin_spec,
		as_random* random, float compression_ratio);
static as_val* bin_spec_random_val(const struct bin_spec_s* bin_spec,
		as_random* random, float compression_ratio);
static size_t _sprint_bin(const struct bin_spec_s* bin, char** out_str,
		size_t str_size);

#ifdef _TEST
static void _dbg_validate_int(uint8_t range, as_integer* as_val);
static void _dbg_validate_string(uint32_t length, as_string* as_val);
static void _dbg_validate_bytes(uint32_t length, as_bytes* as_val);
static void _dbg_validate_list(const struct bin_spec_s* bin_spec,
		const as_list* as_val);
static void _dbg_validate_map(const struct bin_spec_s* bin_spec,
		const as_map* val);
static void _dbg_validate_obj_spec(const struct bin_spec_s* bin_spec,
		const as_val* val);
#endif /* _TEST */


//==========================================================
// Inlines and macros.
//

/*
 * converts a bytevector of 8 values between 0-35 to a bytevector of 8
 * alphanumeric characters
 */
static inline uint64_t
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

int
obj_spec_populate_bins(const struct obj_spec_s* obj_spec, as_record* rec,
		as_random* random, const char* bin_name, float compression_ratio)
{
	uint32_t n_bin_specs = obj_spec->n_bin_specs;
	as_bins* bins = &rec->bins;

	if (n_bin_specs > bins->capacity) {
		fprintf(stderr, "Not enough bins allocated for obj_spec\n");
		return -1;
	}

	if (bin_name_too_large(strlen(bin_name), obj_spec->n_bin_specs)) {
		// if the key name is too long to fit every key we'll end up generating
		// in an as_bin_name, return an error
		fprintf(stderr, "Key name \"%s\" will exceed the maximum number of "
				"allowed characters in a single bin (%s_%d)\n",
				bin_name, bin_name, obj_spec->n_bin_specs);
		return -1;
	}

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
	return 0;
}


as_val*
obj_spec_gen_value(const struct obj_spec_s* obj_spec, as_random* random)
{
	return obj_spec_gen_compressible_value(obj_spec, random, 1.f);
}

as_val*
obj_spec_gen_compressible_value(const struct obj_spec_s* obj_spec,
		as_random* random, float compression_ratio)
{
	struct bin_spec_s tmp_list;
	tmp_list.type = BIN_SPEC_TYPE_LIST;
	tmp_list.list.length = obj_spec->n_bin_specs;
	tmp_list.list.list = obj_spec->bin_specs;

	return bin_spec_random_val(&tmp_list, random, compression_ratio);
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
		const as_record* rec, const char* bin_name)
{
	as_bin_name name;

	for (uint32_t i = 0, cnt = 0; cnt < obj_spec->n_bin_specs; i++) {
		const struct bin_spec_s* bin_spec = &obj_spec->bin_specs[i];

		for (uint32_t j = 0; j < bin_spec->n_repeats; j++, cnt++) {
			gen_bin_name(name, bin_name, cnt);

			as_val* val = (as_val*) as_record_get(rec, name);
			ck_assert_msg(val != NULL, "expected a record in bin \"%s\"", name);
			_dbg_validate_obj_spec(bin_spec, val);
		}
	}
}

#endif /* _TEST */


//==========================================================
// Local helpers.
//

static void
bin_spec_set_key_and_type(struct bin_spec_s* b, struct bin_spec_s* key)
{
	b->map.key = (struct bin_spec_s*) (((uint64_t) key) | BIN_SPEC_TYPE_MAP);
}

static uint8_t
bin_spec_get_type(const struct bin_spec_s* b)
{
	return b->type & BIN_SPEC_TYPE_MASK;
}

static struct bin_spec_s*
bin_spec_get_key(const struct bin_spec_s* b)
{
	return (struct bin_spec_s*) (((uint64_t) b->map.key) & ~BIN_SPEC_TYPE_MASK);
}

/*
 * gives the number of entries to put in the map
 */
static uint32_t
bin_spec_map_n_entries(const struct bin_spec_s* b)
{
	return bin_spec_get_key(b)->n_repeats;
}

static void
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
static void
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


static int
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
						if (state->list_builder->size == 0) {
							_print_parse_error("Cannot have empty list",
									obj_spec_str, str);
							_destroy_consumer_states(state);
							return -1;
						}
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
						if (map_state != MAP_DONE) {
							_print_parse_error("Unexpected token '}' before "
									"both key and value have been provided "
									"for a map",
									obj_spec_str, str);
							_destroy_consumer_states(state);
							return -1;
						}
						bin_spec_set_key_and_type(bin_spec, state->key);
						bin_spec->map.val = state->value;
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
			if (*str >= '1' && *str <= '9' && endptr != str) {

				if (type == CONSUMER_TYPE_MAP && map_state == MAP_VAL &&
						mult != 1) {
					_print_parse_error("Map value cannot have a multiplier",
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
				if (*str != '*') {
					_print_parse_error("Expect a '*' to follow a multiplier",
							obj_spec_str, str);
					goto _destroy_state;
				}
				str++;
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
					if (str_len == 0 || str_len != (uint32_t) str_len) {
						_print_parse_error("Invalid string length",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					bin_spec->type = BIN_SPEC_TYPE_STR;
					bin_spec->string.length = (uint32_t) str_len;;

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
					if (bytes_len == 0 || bytes_len != (uint32_t) bytes_len) {
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
				else if (*str != delim) {
					_print_parse_error("Expect ',' separating bin specifiers in a list",
							obj_spec_str, str);
					_destroy_consumer_states(state);
					return -1;
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

static void
bin_spec_free(struct bin_spec_s* bin_spec)
{
	struct bin_spec_s* key;
	switch (bin_spec_get_type(bin_spec)) {
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
			key = bin_spec_get_key(bin_spec);
			bin_spec_free(key);
			cf_free(key);
			bin_spec_free(bin_spec->map.val);
			cf_free(bin_spec->map.val);
			break;
	}
}

static as_val*
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
	min = (0x001LU << (range * 8)) & ~0x1LU;
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

static as_val*
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

static as_val*
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

static as_val*
_gen_random_double(as_random* random)
{
	as_double* val;
	// for now, just generate random uint64 and reinterpret as double
	uint64_t bytes = as_random_next_uint64(random);
	val = as_double_new(*(double*) &bytes);
	return (as_val*) val;
}

static as_val*
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

static as_val*
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
			key = bin_spec_random_val(bin_spec_get_key(bin_spec), random,
					compression_ratio);
		} while (as_hashmap_get(map, key) != NULL &&
				retry_count++ < MAX_KEY_ENTRY_RETRIES);

		as_val* val = bin_spec_random_val(bin_spec->map.val, random,
				compression_ratio);
		
		as_hashmap_set(map, key, val);
	}

	return (as_val*) map;
}


static as_val*
bin_spec_random_val(const struct bin_spec_s* bin_spec, as_random* random,
		float compression_ratio)
{
	as_val* val;
	switch (bin_spec_get_type(bin_spec)) {
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
			fprintf(stderr, "Unknown bin_spec type (%d)\n",
					bin_spec_get_type(bin_spec));
			val = NULL;
	}

	return val;
}

static size_t
_sprint_bin(const struct bin_spec_s* bin, char** out_str, size_t str_size)
{
	if (bin->n_repeats != 1) {
		sprint(out_str, str_size, "%d*", bin->n_repeats);
	}
	switch (bin_spec_get_type(bin)) {
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
			str_size = _sprint_bin(bin_spec_get_key(bin), out_str, str_size);
			sprint(out_str, str_size, ":");
			str_size = _sprint_bin(bin->map.val, out_str, str_size);
			sprint(out_str, str_size, "}");
			break;
	}
	return str_size;
}

#ifdef _TEST

static void
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

static void
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

static void
_dbg_validate_bytes(uint32_t length, as_bytes* as_val)
{
	ck_assert_msg(as_val != NULL, "Expected a bytes array, got something else");

	ck_assert_int_eq(length, as_bytes_size(as_val));
}

static void
_dbg_validate_double(as_double* as_val)
{
	ck_assert_msg(as_val != NULL, "Expected a double, got something else");
}

static void
_dbg_validate_list(const struct bin_spec_s* bin_spec, const as_list* as_val)
{
	ck_assert_msg(as_val != NULL, "Expected a list, got something else");
	size_t list_len = as_list_size(as_val);
	ck_assert_int_eq(list_len, bin_spec->list.length);

	for (uint32_t i = 0, cnt = 0; cnt < list_len; i++) {
		const struct bin_spec_s* ele_bin = &bin_spec->list.list[i];

		for (uint32_t j = 0; j < ele_bin->n_repeats; j++, cnt++) {
			_dbg_validate_obj_spec(ele_bin, as_list_get(as_val, cnt));
		}
	}
}


static void
_dbg_validate_map(const struct bin_spec_s* bin_spec, const as_map* val)
{
	as_hashmap_iterator iter;

	ck_assert_msg(val != NULL, "Expected a map, got something else");
	uint32_t map_size = as_map_size(val);
	ck_assert_int_eq(map_size, bin_spec_map_n_entries(bin_spec));

	const struct bin_spec_s* key_spec = bin_spec_get_key(bin_spec);
	const struct bin_spec_s* val_spec = bin_spec->map.val;

	for (as_hashmap_iterator_init(&iter, (as_hashmap*) val);
			as_hashmap_iterator_has_next(&iter);) {
		const as_val* kv_pair = as_hashmap_iterator_next(&iter);
		const as_val* key = as_pair_1((as_pair*) kv_pair);
		const as_val* val = as_pair_2((as_pair*) kv_pair);

		_dbg_validate_obj_spec(key_spec, key);
		_dbg_validate_obj_spec(val_spec, val);
	}
}

static void
_dbg_validate_obj_spec(const struct bin_spec_s* bin_spec, const as_val* val)
{
	switch (bin_spec_get_type(bin_spec)) {
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
			ck_assert_msg(0, "unknown bin_spec type (%d)",
					bin_spec_get_type(bin_spec));
	}
}

#endif /* _TEST */

