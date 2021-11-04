#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type)
{
    /* It allocates an object of the given size on the heap. 
     * Note that the size is not just the size of Obj itself. */
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;

    /* Every time allocate an Obj, insert it in the list. */
    object->next = vm.objects;
    vm.objects = object;
    return object;
}

static ObjString* allocateString(char* chars, int length) 
{
    /* It creates a new ObjString on the heap and 
     * then initializes its fields. 
     * It’s sort of like a constructor in an OOP language. 
     * As such, it first calls the “base class” constructor 
     * to initialize the Obj state. */
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars  = chars;

    return string;
}

ObjString* takeString(char* chars, int length)
{
    return allocateString(chars, length);
}

ObjString* copyString(const char* chars, int length) 
{
    /* First, allocate a new array on the heap, 
     * just big enough for the string’s characters 
     * and the trailing terminator. */
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length);
}

void printObject(Value value)
{
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING: 
            /* If the value is a heap-allocated object, 
             * it defers to a helper function over in the “object” module. */
            printf("%s", AS_CSTRING(value));
            break;
    }
}
