#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

/* As for the simplest, classic solution: a tagged union. 
 * A value contains two parts: 
 * a type “tag”, and 
 * a payload for the actual value. 
 * To store the value’s type, 
 * define an enum for each kind of value the VM supports. */
typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ
} ValueType;

/* As the name “tagged union” implies, 
 * the new value representation combines two parts into a single struct. */
typedef struct {
    /* There’s a field for the type tag, 
     * and then a second field 
     * containing the union of all of the underlying values. */
    ValueType type;
    union {
        bool   boolean;
        double number;

        /* When a Value’s type is VAL_OBJ, 
         * the payload is a pointer to the heap memory, 
         * so add another case to the union for that. */
        Obj* obj;
    } as;
} Value;

/* It’s not safe to use any of the AS_ macros 
 * unless known the value contains the appropriate type. 
 * These macros return true if the value has that type. 
 * Any time call one of the AS_ macros, 
 * need to guard it behind a call to one of the IS_ macros first. */
#define IS_BOOL(value)   ((value).type == VAL_BOOL)
#define IS_NIL(value)    ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value)    ((value).type == VAL_OBJ)

/* Unpack Value and get the C value back out. */
#define AS_OBJ(value)    ((value).as.obj)
#define AS_BOOL(value)   ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

/* Each one of these macro takes a C value of the appropriate type and 
 * produces a Value 
 * that has the correct type tag and contains the underlying value. 
 * This hoists statically-typed values up 
 * into clox’s dynamically-typed universe. */
#define BOOL_VAL(value)   ((Value) {VAL_BOOL,   {.boolean = value}})
#define NIL_VAL           ((Value) {VAL_NIL,    {.number = 0}})
#define NUMBER_VAL(value) ((Value) {VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object)   ((Value) {VAL_OBJ,    {.obj = (Obj*)object}})

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

bool valuesEqual(Value a, Value b);

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
