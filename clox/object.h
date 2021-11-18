#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

/* Macro that extracts the object type tag from a given Value. */
#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)     isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION)

#define IS_INSTANCE(value)  isObjType(value, OBJ_INSTANCE)
/* A macro to see if a value is a native function. */
#define IS_NATIVE(value)    isObjType(value, OBJ_NATIVE)

/* Macro that detects a cast is safe. */
#define IS_STRING(value)    isObjType(value, OBJ_STRING)

#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)              ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)          ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)        ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)        ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value)           (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)            ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)          (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
} ObjType;

struct Obj {
    /* The name “Obj” itself refers to a struct 
     * that contains the state shared across all object types. 
     * It’s sort of like the “base class” for objects. */
    ObjType type;
    
    /* The new field lives in the Obj header struct all objects share.*/
    bool isMarked;

    /* An intrusive list 
     * — the Obj struct itself will be the linked list node. 
     * Each Obj gets a pointer to the next Obj in the chain. */
    struct Obj* next;
};

typedef struct {
    Obj        obj;
    int        arity;
    int        upvalueCount;
    Chunk      chunk;
    ObjString* name;
} ObjFunction;

/* A programming language implementation 
 * reaches out and touches the material world through native functions.
 * If want to be able to write programs
 * that check the time, read user input, or access the file system,
 * need to add native functions
 * — callable from Lox but implemented in C
 * — that expose those capabilities. */
typedef Value (*NativeFn)(int argCount, Value* args);

/* The representation is simpler than ObjFunction
 * — merely an Obj header and a pointer to the C function
 * that implements the native behavior.
 * The native function takes the argument count
 * and a pointer to the first argument on the stack.
 * It accesses the arguments through that pointer.
 * Once it’s done, it returns the result value. */
typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

struct ObjString {
    /* Because ObjString is an Obj, 
     * it also needs the state all Objs share. 
     * It accomplishes that by having its first field be an Obj. */
    Obj obj;

    int length;
    char* chars;

    /* Each ObjString stores the hash code for that string. 
     * Since strings are immutable in Lox, 
     * can calculate it once up front 
     * and be certain that it will never get invalidated. */
    uint32_t hash;
};

typedef struct ObjUpvalue {
    /* Runtime representation for upvalues. */
    Obj    obj;
    /* The location field that points to the closed-over variable. 
     * Note that this is a pointer to a Value, not a Value itself. 
     * It’s a reference to a variable, not a value. 
     * This is important because it means that 
     * when assign to the variable the upvalue captures, 
     * are assigning to the actual variable, not a copy. */
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

/* Wrap the ObjFunction in a new ObjClosure structure. 
 * The latter has a reference to the underlying bare function 
 * along with runtime state for the variables the function closes over. */
typedef struct {
    Obj obj;
    ObjFunction* function;
    /* The upvalues themselves are dynamically allocated, 
     * so end up with a double pointer 
     * — a pointer to a dynamically allocated array of pointers to upvalues. */
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

typedef struct {
    Obj obj;
    ObjString* name;

    /* Each class stores a hash table of methods. 
     * Keys are method names and 
     * each value is an ObjClosure for the body of the method. */
    Table methods;
} ObjClass;

typedef struct {
    Obj obj;
    ObjClass* klass;
    Table fields;
} ObjInstance;

typedef struct {
    /* When the user executes a method access, 
     * we’ll find the closure for that method and wrap it 
     * in a new “bound method” object 
     * that tracks the instance that the method was accessed from. 
     * This bound object can be called later like a function. 
     * When invoked, the VM will do some shenanigans 
     * to wire up this to point to the receiver inside the method’s body. */
    Obj obj;
    /* The receiver’s type is Value 
     * even though methods can only be called on ObjInstances. 
     * Since the VM doesn’t care what kind of receiver it has anyway, 
     * using Value means don’t have to 
     * keep converting the pointer back to a Value 
     * when it gets passed to more general functions. */
    Value receiver;
    ObjClosure* method;
} ObjBoundMethod;

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);
ObjClass*    newClass(ObjString* name);
ObjClosure*  newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjInstance* newInstance(ObjClass* klass);
ObjNative*   newNative(NativeFn function);
ObjString*   takeString(char* chars, int length);
ObjString*   copyString(const char* chars, int length);
ObjUpvalue*  newUpvalue(Value* slot);

void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) 
{
    /* If a macro uses a parameter more than once, 
     * that expression gets evaluated multiple times. */
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
