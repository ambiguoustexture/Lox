#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64

/* Redefine the value stack’s size in terms of that
 * to make sure have plenty of stack slots even in very deep call trees. */
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
    /* A CallFrame represents a single ongoing function call.
     * The slots field points into the VM’s value stack
     * at the first slot that this function can use. */
    ObjClosure* closure;
    uint8_t* ip;
    Value*   slots;
} CallFrame;

typedef struct {
    /* Function calls have stack semantics.
     *
     * In the VM,
     * create an array of these CallFrame structs up front and
     * treat it as a stack.
     * This array replaces the chunk and ip fields
     * used to have directly in the VM.
     * Now each CallFrame has its own ip and its own pointer
     * to the ObjFunction that it’s executing.
     * From there, can get to the function’s chunk. */
    CallFrame frames[FRAMES_MAX];
    /* The new frameCount field in the VM
     * stores the current height of the stack
     * — the number of ongoing function calls. */
    int frameCount;

    /*
     * Implement the stack semantics on top of a raw C array.
     * The bottom of the stack —— the first value pushed
     * and the last to be popped —— is at element zero in the array,
     * and later pushed values follow it.
     */
    Value stack[STACK_MAX];
    /*
     * Since the stack grows and shrinks as values are pushed and popped,
     * need to track where the top of the stack is in the array.
     * As with ip, use a direct pointer instead of an integer index
     * since it’s faster to dereference the pointer
     * than calculate the offset from the index each time need it.
     *
     * The pointer points at the array element
     * just past the element containing the top value on the stack.
     * That seems a little odd, but almost every implementation does this.
     */
    Value* stackTop;

    /* String Interning */
    Table strings;

    /* To call the initializer, 
     * the runtime looks up the init() method by name. 
     * It would be good to take advantage of the string interning. 
     * To do that, the VM creates an ObjString for “init” and reuses it. */ 
    ObjString* initString;

    /* Global Variables
     *
     * Need a place to store these globals.
     * Since want them to persist as long as clox is running,
     * store them right in the VM. */
    Table globals;

    /* The VM owns the list of open upvalues, 
     * so the head pointer goes right inside the main VM struct. 
     *
     * Starting with the first upvalue pointed to by the VM, 
     * each open upvalue points to the next open upvalue that 
     * references a local variable farther down the stack. */
    ObjUpvalue* openUpvales;

    /* The idea is that the collector frequency automatically adjusts 
     * based on the live size of the heap. 
     * Track the total number of bytes of managed memory 
     * that the VM has allocated. 
     * When it goes above some threshold, trigger a GC. 
     * After that, note 
     * how many bytes of memory remain 
     * — how many were not freed. 
     * Then adjust the threshold to some value larger than that. 
     * The result is that as the amount of live memory increases, 
     * collect less frequently in order to avoid sacrificing throughput 
     * by re-traversing the growing pile of live objects. 
     * As the amount of live memory goes down, 
     * collect more frequently 
     * so that don’t lose too much latency by waiting too long. */
    
    /* The `bytesAllocated` is a running total 
     * of the number of bytes of managed memory the VM has allocated. 
     * The `nextGC` is the threshold that triggers the next collection. 
     * Initialize them when the VM starts up: */
    size_t bytesAllocated;
    size_t nextGC;

    /* The VM stores a pointer to the head of the intrusive list. */
    Obj* objects;

    /* Fields for the tri-color abstraction. */
    int grayCount;
    int grayCapacity;
    Obj** grayStack;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

/* The “object” module is directly using the global vm variable
 * from the “vm” module, so need to expose that externally. */
extern VM vm;

void initVM();
void freeVM();

InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif
