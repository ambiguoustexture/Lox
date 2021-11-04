#include <stdio.h>
#include <string.h>

#include "object.h"
#include "memory.h"
#include "value.h"

void initValueArray(ValueArray* array) {
    array->values   = NULL;
    array->capacity = 0;
    array->count    = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values   = GROW_ARRAY(Value, array->values, 
                                     oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void printValue(Value value) {
    /* Right before sending the value to printf(), 
     * unwrap it and extract the double value. */    
    switch (value.type) {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false"); break;
        case VAL_NIL: 
            printf("nil"); break;
        case VAL_NUMBER:
            printf("%g", AS_NUMBER(value)); break;
        case VAL_OBJ: 
            printObject(value); break;
    }
}

bool valuesEqual(Value a, Value b)
{
    /* First, check the types. 
     * If the values have different types, 
     * they are definitely not equal. 
     * Otherwise, unwrap the two values and compare them directly. */
    if (a.type != b.type) return false;

    switch(a.type) {
        case VAL_BOOL:   return AS_BOOL(a)   == AS_BOOL(b);
        case VAL_NIL:    return true;
        case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ: {
            /* If the two values are both strings, 
             * then they are equal if their character arrays 
             * contain the same characters, 
             * regardless of whether they are two separate objects 
             * or the exact same one. 
             * This does mean that string equality 
             * is slower than equality 
             * on other types since it has to walk the whole string. */
            ObjString* aString = AS_STRING(a);
            ObjString* bString = AS_STRING(b);
            return aString->length == bString->length && 
                memcmp(aString->chars, bString->chars, aString->length) == 0;
        }
        default:
            return false; // Unreachable.
    }
}
