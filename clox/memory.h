#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"
#include "object.h"

/* Low-level macro that allocates an array 
 * with a given element type and count. */
#define ALLOCATE(type, count) \
    /* (type*)reallocate(NULL, 0, sizeof(type) * (count)) */ \
    reallocate(NULL, 0, sizeof(type) * (count))
/* FIXME? Commented for:
 * vm.c:112:19: error: expected expression
 *     char* chars = ALLOCATE(chars, length + 1);
 *                   ^
 * ./memory.h:10:11: note: expanded from macro 'ALLOCATE'
 *     (type*)reallocate(NULL, 0, sizeof(type) * (count)) 
 *           ^
 * 1 error generated. */

/* A tiny wrapper around reallocate() 
 * that “resizes” an allocation down to zero bytes. */
#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

/*
 * This macro calculates a new capacity 
 * based on a given current capacity.
 * It also handle the when the current capacity is zero. 
 * In that case, jump straight to eight elements 
 * instead of starting at one. 
 * That avoids a little extra memory churn 
 * when the array is very small, 
 * at the expense of wasting a few bytes on very small chunks.
 */
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

/*
 * This macro pretties up a function call to reallocate() 
 * where the real work happens. 
 * The macro itself takes care of 
 * getting the size of the array’s element type and 
 * casting the resulting void* back 
 * to a pointer of the right type.
 */
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
            (type*)reallocate(pointer, sizeof(type) * (oldCount), \
                    sizeof(type) * (newCount)) 

/* 
 * This macro frees the memory by passing in zero 
 * for the new size.
 */
#define FREE_ARRAY(type, pointer, oldCount) \
     reallocate(pointer, sizeof(type) * (oldCount), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);
void freeObjects();

#endif

