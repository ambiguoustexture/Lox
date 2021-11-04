#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
    Chunk* chunk;
    /*
     * As the VM works its way through the bytecode, 
     * it keeps track of where it is —— 
     * the location of the instruction currently being executed. 
     * Don’t use a local variable inside run() for this 
     * because eventually other functions will need to access it. 
     * Instead, store it as a field in VM.
     * 
     * Its type is a byte pointer. 
     * Use an actual real C pointer right into the middle 
     * of the bytecode array instead of something like an integer index 
     * because it’s faster to dereference a pointer 
     * than look up an element in an array by index.
     *
     * The name “IP” is traditional, 
     * and —— unlike many traditional names in CS —— actually makes sense: 
     * it’s an instruction pointer. 
     * Almost every instruction set in the world, real and virtual, 
     * has a register or variable like this. 
     * The other common name for it is “PC” for “program counter”.
     * Initialize ip by pointing it
     * at the first byte of code in the chunk.
     *
     * This will be true during the entire time the VM is running:
     * the IP always points to the next instruction,
     * not the one currently being handled.
     */
    uint8_t* ip;

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
