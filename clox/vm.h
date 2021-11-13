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
    ObjFunction* function;
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

    /* Global Variables
     *
     * Need a place to store these globals.
     * Since want them to persist as long as clox is running,
     * store them right in the VM. */
    Table globals;

    /* The VM stores a pointer to the head of the intrusive list. */
    Obj* objects;
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
