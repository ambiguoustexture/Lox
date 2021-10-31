#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

VM vm;

static void resetStack() 
{
    /*
     * The only initialization needed 
     * is to set stackTop to point to the beginning of the array 
     * to indicate that the stack is empty.
     */
    vm.stackTop = vm.stack;
}

void initVM() {
    resetStack();
}

void freeVM() {}

void push(Value value) 
{
    /*
     * Stores value in the array element at the top of the stack.
     * This stores the value in that slot. 
     * Then increment the pointer itself 
     * to point to the next unused slot in the array 
     * now that the previous slot is occupied.
     */
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop()
{
    /*
     * First, move the stack pointer back 
     * to get to the most recent used slot in the array. 
     * Then look up the value at that index and return it. 
     * Don’t need to explicitly “remove” it from the array 
     * —— moving stackTop down is enough to mark that slot as no longer in use.
     */
    vm.stackTop--;
    return *vm.stackTop;
}

static InterpretResult run()
{
/*
 * To process an instruction, 
 * first figure out what kind of instruction dealing with. 
 *
 * The READ_BYTE() macro reads the byte currently pointed at 
 * by ip and then advances the instruction pointer. 
 *
 * The first byte of any instruction is the opcode. 
 * Given a numeric opcode, need to get to the right C code 
 * that implements that instruction’s semantics. 
 * This process is called decoding or dispatching the instruction.
 */
#define READ_BYTE() (*vm.ip++)

/* 
 * READ_CONSTANT() 
 * reads the next byte from the bytecode, 
 * treats the resulting number as an index, 
 * and looks up the corresponding Value in the chunk’s constant table.
 */
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

/*
 * A binary operator takes two operands, 
 * so it pops twice. 
 * It performs the operation on those two values and then pushes the result.
 */
#define BINARY_OP(op) \
    do {\
        double b = pop(); \
        double a = pop(); \
        push(a op b); \
    } while (false);

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION 
        {
        /*
         * Loop, printing each value in the array, 
         * starting at the first (bottom of the stack) and 
         * ending when reach the top. 
         * This lets us observe the effect of each instruction on the stack. 
         */
        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        }

        {
        /*
         * Since disassembleInstruction() takes an integer byte offset 
         * and store the current instruction reference as a direct pointer, 
         * First do a little pointer math to convert ip 
         * back to a relative offset from the beginning of the bytecode. 
         * Then disassemble the instruction that begins at that byte.
         */
        disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
        }
#endif
        /*
         * An outer loop that goes and goes. 
         * Each turn through that loop, 
         * we read and execute a single bytecode instruction.
         */
        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_ADD:      BINARY_OP(+); break;
            case OP_SUBTRACT: BINARY_OP(-); break;
            case OP_MULTIPLY: BINARY_OP(*); break;
            case OP_DIVIDE:   BINARY_OP(/); break;
            case OP_NEGATE: {
                /*
                 * The instruction needs a value to operate on 
                 * which it gets by popping from the stack. 
                 * It negates that, then pushes the result back on 
                 * for later instructions to use.
                 */
                push(-pop()); break;
            }
            case OP_RETURN: {
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;                    
            }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}   

InterpretResult interpret(const char* source)
{
    complie(source);
    return INTERPRET_OK;
}
