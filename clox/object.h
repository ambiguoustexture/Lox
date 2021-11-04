#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

/* Macro that extracts the object type tag from a given Value. */
#define OBJ_TYPE(value) (AS_OBJ(value)->type)

/* Macro that detects a cast is safe. */
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_STRING(value)   ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    OBJ_STRING,
} ObjType;

struct Obj {
    /* The name “Obj” itself refers to a struct 
     * that contains the state shared across all object types. 
     * It’s sort of like the “base class” for objects. */
    ObjType type;
    /* An intrusive list 
     * — the Obj struct itself will be the linked list node. 
     * Each Obj gets a pointer to the next Obj in the chain. */
    struct Obj* next;
};

struct ObjString {
    /* Because ObjString is an Obj, 
     * it also needs the state all Objs share. 
     * It accomplishes that by having its first field be an Obj. */
    Obj obj;

    int length;
    char* chars;
};

ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);

void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) 
{
    /* If a macro uses a parameter more than once, 
     * that expression gets evaluated multiple times. */
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
