#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef double Value;

/*
 * The constant pool is an array of values. 
 * The instruction to load a constant looks up the value 
 * by index in that array. 
 * As with bytecode array, 
 * the compiler doesn’t know how big the array needs to be ahead of time. 
 * So, again, we need a dynamic one.
 */
typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

/*
 * As with the bytecode array in Chunk, 
 * this struct wraps a pointer to an array 
 * along with its allocated capacity and the number of elements in use. 
 * Also need the same three functions to work with value arrays.
 */
void initValueArray( ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray( ValueArray* array);
void printValue(Value value);

#endif
