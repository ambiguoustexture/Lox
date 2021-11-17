#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
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

    /* Every new object begins life unmarked 
     * because haven’t determined if it is reachable or not yet.*/
    object->isMarked = false;

    /* Every time allocate an Obj, insert it in the list. */
    object->next = vm.objects;
    vm.objects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif 

    return object;
}

ObjClass* newClass(ObjString* name) 
{
    /* It takes in the class’s name as a string and stores it. 
     * Every time the user declares a new class, 
     * the VM will create a new one of these ObjClass structs 
     * to represent it. */
    ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    return klass;
}

ObjClosure*  newClosure(ObjFunction* function)
{
    /* When create an ObjClosure, 
     * allocate an upvalue array of the proper size, 
     * which determined at compile time and stored in the ObjFunction. */
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i= 0; i < function->upvalueCount; i++) {
        /* Before creating the closure object itself, 
         * allocate the array of upvalues and initialize them all to NULL. 
         * This weird ceremony around memory is a careful dance 
         * to please the (forthcoming) garbage collection deities. 
         * It ensures the memory manager never sees uninitialized memory. */
        upvalues[i] = NULL;
    }

    /* It takes a pointer to the ObjFunction it wraps. */
    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjFunction* newFunction()
{
    /* Instead of passing in arguments to initialize the function,
     * set it up in a sort of “blank” state—zero arity,
     * no name, and no code. */
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION); {
        function->arity = 0;
        function->upvalueCount = 0;
        function->name = NULL;
        initChunk(&function->chunk);
    }

    return function;
}

ObjInstance* newInstance(ObjClass* klass)
{
    /* Store a reference to the instance’s class. 
     * Then initialize the field table to an empty hash table. */ 
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE); {
        instance->klass = klass;
        initTable(&instance->fields);
    }

    return instance;
}

ObjNative* newNative(NativeFn function)
{
    /* The constructor takes a C function pointer to wrap in an ObjNative.
     * It sets up the object header and stores the function. */
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) 
{
    /* It creates a new ObjString on the heap and 
     * then initializes its fields. 
     * It’s sort of like a constructor in an OOP language. 
     * As such, it first calls the “base class” constructor 
     * to initialize the Obj state. */
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars  = chars;

    /* Whenever call the internal function to allocate a string, 
     * pass in its hash code. */
    string->hash = hash;

    push(OBJ_VAL(string));
    /* Automatically intern every new string. 
     * That means whenever create a new unique string, add it to the table.
     *
     * Cause using the table more like a hash set than a hash table. 
     * The keys are the strings and those are all care about, 
     * so just use nil for the values. */
    tableSet(&vm.strings, string, NIL_VAL);
    pop();

    return string;
}

static uint32_t fnv1a32(const char* key, int length) 
{
    /* The actual bonafide “hash function” in clox. 
     * The algorithm is called “FNV-1a”,
     * which could be intorduced at:
     * https://youtu.be/IdVX4qHJXEY?t=681 
     * and more on:
     * http://www.isthe.com/chongo/tech/comp/fnv/#FNV-1a .
     *
     * The basic idea is pretty simple and 
     * many hash functions follow the same pattern. 
     * Start with some initial hash value, usually a constant 
     * with certain carefully chosen mathematical properties. 
     * Then you the data to be hashed. 
     * For each byte (or sometimes word), 
     * munge the bits into the hash value somehow and 
     * then mix the resulting bits around some. */
    uint32_t hash = 2166136261u;

    for (int i = 0; i < length; i++) {
        hash ^= key[i];
        hash *= 16777619; /* The number 16777619 is a prime!*/
    }
    
    return hash;
}

static uint32_t hashString(const char* key, int length) 
{
    return fnv1a32(key, length);
}

ObjString* takeString(char* chars, int length)
{
    uint32_t hash = hashString(chars, length);
    
    /* Look up the string in the string table first. 
     * If find it, before return it, 
     * free the memory for the string that was passed in. 
     * Since ownership is being passed to this function 
     * and no longer need the duplicate string, 
     * it’s up to us to free it.*/
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) 
{
    /* Calculate the hash code and then pass it along. */
    uint32_t hash = hashString(chars, length);

    /* When copying a string into a new LoxString, 
     * look it up in the string table first. 
     * If find it, instead of “copying”, 
     * just return a reference to that string. 
     * Otherwise, fall through, 
     * allocate a new string, 
     * and store it in the string table. */
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    /* First, allocate a new array on the heap, 
     * just big enough for the string’s characters 
     * and the trailing terminator. */
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, hash);
}

ObjUpvalue* newUpvalue(Value* slot) 
{
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    /* Zero the `closed` field out when create an ObjUpvalue 
     * so there’s no uninitialized memory floating around. */
    upvalue->closed = NIL_VAL;
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

static void printFunction(ObjFunction* function)
{
    if (function->name == NULL) {
        printf("<script>");
        return ;
    }

    printf("<fn %s>", function->name->chars);
}

void printObject(Value value)
{
    switch (OBJ_TYPE(value)) {
        case OBJ_CLASS:
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_INSTANCE:
            printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_STRING: 
            /* If the value is a heap-allocated object, 
             * it defers to a helper function over in the “object” module. */
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
    }
}
