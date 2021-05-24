/*******************************************************************************
 * Copyright 2020 by Aerospike.
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
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aerospike/as_record.h>
#include <aerospike/as_random.h>


//==========================================================
// Typedefs & constants.
//

#define DEFAULT_LIST_BUILDER_CAPACITY 8


#define BIN_SPEC_TYPE_BOOL   0x0
#define BIN_SPEC_TYPE_INT    0x1
#define BIN_SPEC_TYPE_STR    0x2
#define BIN_SPEC_TYPE_BYTES  0x3
#define BIN_SPEC_TYPE_DOUBLE 0x4
#define BIN_SPEC_TYPE_LIST   0x5
#define BIN_SPEC_TYPE_MAP    0x6

#define BIN_SPEC_TYPE_MASK 0x7

// indicates this field is a const
// note that only scalar types (aside from blobs) can be const
#define BIN_SPEC_TYPE_CONST 0x8

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


/*
 * defines a loop that goes over each of the write bins
 *
 * example usage:
 *     FOR_EACH_WRITE_BIN(<write_bins>, <n_write_bins>, <obj_spec>, iter, idx, bin_spec) {
 *         printf("Bin index %u is in slot %u in the record\n", idx, iter);
 *     }
 *     END_FOR_EACH_WRITE_BIN(iter, idx);
 *
 * iter_var: the variable which will hold the iteration index of the
 *         iteration of the loop we're on
 * idx_var: the variable which will hold the bin-index of the current bin
 */
#define FOR_EACH_WRITE_BIN(write_bins, n_write_bins, obj_spec, iter_var, idx_var, bin_spec_var) \
	uint32_t iter_var = 0; \
	uint32_t __tot = 0; \
	uint32_t idx_var = (write_bins)[iter_var]; \
	for (uint32_t __i = 0; __tot < (obj_spec)->n_bin_specs; __i++) { \
		const struct bin_spec_s* bin_spec_var = &(obj_spec)->bin_specs[__i]; \
		for (;;) { \
			if (__tot + bin_spec_var->n_repeats <= idx_var) { \
				__tot += bin_spec_var->n_repeats; \
				break; \
			} \
			else {

#define END_FOR_EACH_WRITE_BIN(write_bins, n_write_bins, iter_var, idx_var) \
				iter_var++; \
				if (iter_var >= (n_write_bins)) { \
					goto __exit_loop; \
				} \
				idx_var = (write_bins)[iter_var]; \
			} \
		} \
	} \
	__builtin_unreachable(); \
__exit_loop:


struct bin_spec_s {

	/*
	 * one of the five main types of bins:
	 * 	bool: a random boolean
	 *	int: a random int, within certain bounds (described below)
	 *	string: a string of fixed length, consisting of [a-z]{1,9}
	 *			space-separated words
	 *	bytes array: array of random bytes of data
	 *	double: any 8-byte double floating point
	 *	list: a list of bin_specs
	 *	map: a map from some type of scalar bin_spec to another bin_spec
	 *
	 * may also include the const bit, which applies to scalar bins to indicate
	 * that they have a fixed value
	 */
	uint8_t type;

	uint32_t n_repeats;

	union {

		struct {
			as_boolean val;
		} const_bool;

		struct {
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
			as_integer val;
		} const_integer;

		struct {
			/*
			 * length of strings to be generated (excluding the
			 * null-terminating bit)
			 */
			uint32_t length;
		} string;

		struct {
			as_string val;
		} const_string;

		struct {
			/*
			 * number of random bytes
			 */
			uint32_t length;
		} bytes;

		struct {
			as_double val;
		} const_double;

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
			 */
			struct bin_spec_s* key;
			/*
			 * a pointer to the value type
			 */
			struct bin_spec_s* val;
		} map;

	};
};

typedef struct obj_spec_s {
	struct bin_spec_s* bin_specs;
	uint32_t n_bin_specs;
	/*
     * when set to true, this is a valid obj_spec, when set to false, this
	 * obj_spec has already been freed/is owned by another obj_spec
	 * note, obviously, this value will be undefined before the object spec
	 * has first been initialized
	 */
	bool valid;
} obj_spec_t;


/*
 * Initialize the given obj_spec struct according to the following format:
 *
 *    <bin-type>[,<bin-type>...]
 *
 * where the possible bin types are:
 *
 * Scalar bins:
 *	   b | I<bytes> | B<size> | S<length> | D # Default: I
 *
 *    b) Generate a boolean bin
 *    I) Generate an integer bin or value in a specific byte range (treat I as I4)
 *        I1 for 0 - 255
 *        I2 for 256 - 65535
 *        I3 for 65536 - 2^24-1
 *        I4 for 2^24 - 2^32-1
 *        I5 for 2^32 - 2^40-1
 *        I6 for 2^40 - 2^48-1
 *        I7 for 2^48 - 2^56-1
 *        I8 for 2^56 - 2^64-1
 *    B) Generate a bytes bin or value with an bytearray of random bytes
 *        B12 - generates a bytearray of 12 random bytes
 *    S) Generate a string bin or value made of space-separated a-z{1,9} words
 *        S16 - a string with a 16 character length. ex: "uir a mskd poiur"
 *    D) Generate a Double bin or value (8 byte)
 *
 * Collection bins:
 *     [] - a list
 *         [3*I2] - ex: [312, 1651, 756]
 *         [I2, S4, I2] - ex: [892, "sf b", 8712]
 *         [2*S12, 3*I1] - ex: ["be lkr sqp s", "ndvi qd r fr", 18, 109, 212]
 *         [3*[I1, I1]] - ex: [[1,11],[123,321],[78,241]]
 *
 *     {} - a map
 *         {5*S1:I1} - ex {"a":1, "b":2, "d":4, "z":26, "e":5}
 *         {2*S1:[3*I:1]} - ex {"a": [1,2,3], "b": [6,7,8]}
 * 
 * Example:
 *     I2,S12,[3*I1] => b1: 478; b2: "dfoiu weop g"; b3: [12, 45, 209]
 */
int obj_spec_parse(obj_spec_t* base_obj, const char* obj_spec_str);

void obj_spec_free(obj_spec_t*);

/*
 * transfers ownership of the obj_spec from src to dst, so if free is called
 * on the previous owner it does not free the obj_spec while the new owner is
 * still using it
 */
void obj_spec_move(obj_spec_t* dst, obj_spec_t* src);

/*
 * copies the obj_spec src into dst without transferring ownership (meaning dst
 * may become invalid once src is freed)
 */
void obj_spec_shallow_copy(obj_spec_t* dst, const obj_spec_t* src);

/*
 * returns the number of bins required to fit all the objects in the obj_spec
 */
uint32_t obj_spec_n_bins(const obj_spec_t*);

/*
 * returns true if the given bin name base is compatible with the obj_spec, i.e.
 * if no bin names will be too long given the number of bins in the obj_spec
 * and the length of the name base
 */
bool obj_spec_bin_name_compatible(const obj_spec_t*, const char* bin_name);

/*
 * generates random values for each of the bings based on the object spec
 *
 * the bins are named as follows:
 * 	<bin_name_template>
 * 	<bin_name_template>_2
 * 	<bin_name_template>_3
 * 	...
 */
int obj_spec_populate_bins(const obj_spec_t*, as_record*, as_random*,
		const char* bin_name_template, uint32_t* write_bins,
		uint32_t n_write_bins, float compression_ratio);

/*
 * instead of populating a record's bins, returns an as_list of the objects
 * that would have been placed in the record
 */
as_val* obj_spec_gen_value(const obj_spec_t*, as_random*, uint32_t* write_bins,
		uint32_t n_write_bins);

/*
 * same as obj_spec_gen_value, but each bytes object is made to be compressible
 * by the given compression ratio
 */
as_val* obj_spec_gen_compressible_value(const obj_spec_t*, as_random*,
		uint32_t* write_bins, uint32_t n_write_bins, float compression_ratio);

void snprint_obj_spec(const obj_spec_t* obj_spec, char* out_str,
		size_t str_size);

// define bin printing methods only for testing
#ifdef _TEST

#define _dbg_sprint_obj_spec snprint_obj_spec

/*
 * assures that the as_record generated by this obj_spec conforms to the
 * obj_spec and is fully initialized
 */
void _dbg_obj_spec_assert_valid(const obj_spec_t*, const as_record*,
		uint32_t* write_bins, uint32_t n_write_bins, const char* bin_name);

void _dbg_validate_bin_spec(const struct bin_spec_s* bin_spec,
		const as_val* val);

#endif /* _TEST */

