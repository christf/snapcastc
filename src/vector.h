/*
  Copyright (c) 2012-2015, Matthias Schiffer <mschiffer@universe-factory.net>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
   \file

   Typesafe dynamically sized arrays
*/

#pragma once

#include <search.h>
#include <stdlib.h>

/** A vector descriptor */
typedef struct snap_vector_desc {
	size_t allocated; /**< The number of elements currently allocated */
	size_t length;    /**< The actual number of elements in the vector */
} snap_vector_desc_t;

/**
   A type for a vector of \e type elements

   \hideinitializer
*/
#define VECTOR(type)                     \
	struct {                         \
		snap_vector_desc_t desc; \
		type *data;              \
	}

void _snap_vector_resize(snap_vector_desc_t *desc, void **data, size_t n, size_t elemsize);
void *_snap_vector_insert(snap_vector_desc_t *desc, void **data, void *element, size_t pos, size_t elemsize);
void _snap_vector_delete(snap_vector_desc_t *desc, void **data, size_t pos, size_t elemsize);

/**
   Resizes the vector \e a to \e n elements

   \hideinitializer
*/
#define VECTOR_RESIZE(v, n)                                                                 \
	({                                                                                  \
		__typeof__(v) *_v = &(v);                                                   \
		_snap_vector_resize(&_v->desc, (void **)&_v->data, (n), sizeof(*_v->data)); \
	})

/**
   Frees all resources used by the vector \e v

   \hideinitializer
*/
#define VECTOR_FREE(v) free((v).data)

/**
   Returns the number of elements in the vector \e v

   \hideinitializer
*/
#define VECTOR_LEN(v) ((v).desc.length)

/**
   Returns the element with index \e i in the vector \e v

   \hideinitializer
*/
#define VECTOR_INDEX(v, i) ((v).data[i])

/**
   Given an element, return the index in the vector

   \hideinitializer
*/
#define VECTOR_GETINDEX(v, elem) ({ (elem - v.data) / sizeof(*elem); })

/**
   Returns a pointer to the vector elements of \e v

   \hideinitializer
*/
#define VECTOR_DATA(v) ((v).data)

/**
   Inserts the element \e elem at index \e pos of vector \e v

   \hideinitializer
*/
#define VECTOR_INSERT(v, elem, pos)                                                                \
	({                                                                                         \
		__typeof__(v) *_v = &(v);                                                          \
		__typeof__(*_v->data) _e = (elem);                                                 \
		return _snap_vector_insert(&_v->desc, (void **)&_v->data, &_e, (pos), sizeof(_e)); \
	})

/**
   Adds the element \e elem at the end of vector \e v

   \hideinitializer
*/
#define VECTOR_ADD(v, elem)                                                                           \
	({                                                                                            \
		__typeof__(v) *_v = &(v);                                                             \
		__typeof__(*_v->data) _e = (elem);                                                    \
		_snap_vector_insert(&_v->desc, (void **)&_v->data, &_e, _v->desc.length, sizeof(_e)); \
	})

/**
   Deletes the element at index \e pos of vector \e v

   \hideinitializer
*/
#define VECTOR_DELETE(v, pos)                                                                 \
	({                                                                                    \
		__typeof__(v) *_v = &(v);                                                     \
		_snap_vector_delete(&_v->desc, (void **)&_v->data, (pos), sizeof(*_v->data)); \
	})

/**
 Initializes the description of a vector v with 0
   \hideinitializer
   */
#define VECTOR_INIT(v)                \
	({                            \
		v.data = NULL;        \
		v.desc.length = 0;    \
		v.desc.allocated = 0; \
	})

/**
   Performs a binary search on the vector \e v, returning a pointer to a
   matching vector element

   \hideinitializer
*/
#define VECTOR_BSEARCH(key, v, cmp)                                                                                                            \
	({                                                                                                                                     \
		__typeof__(v) *_v = &(v);                                                                                                      \
		const __typeof__(*_v->data) *_key = (key);                                                                                     \
		int (*_cmp)(__typeof__(_key), __typeof__(_key)) = (cmp);                                                                       \
		(__typeof__(_v->data)) bsearch(_key, _v->data, _v->desc.length, sizeof(*_v->data), (int (*)(const void *, const void *))_cmp); \
	})

/**
   Performs a linear search on the vector \e v, returning a pointer to a
   matching vector element

   \hideinitializer
*/
#define VECTOR_LSEARCH(key, v, cmp)                                                                                                           \
	({                                                                                                                                    \
		__typeof__(v) *_v = &(v);                                                                                                     \
		const __typeof__(*_v->data) *_key = (key);                                                                                    \
		int (*_cmp)(__typeof__(_key), __typeof__(_key)) = (cmp);                                                                      \
		(__typeof__(_v->data)) lfind(_key, _v->data, &_v->desc.length, sizeof(*_v->data), (int (*)(const void *, const void *))_cmp); \
	})
