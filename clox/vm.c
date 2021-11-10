#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
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

static void runtimeError(const char* format, ...)
{
    /* Using variadic functions: 
     * ones that take a varying number of arguments.
     * The ... and va_list stuff let us 
     * pass an arbitrary number of arguments to runtimeError(). 
     * It forwards those on to vfprintf(), 
     * which is the flavor of printf() that takes an explicit va_list. */
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    /* Look into the chunk’s debug line array 
     * using the current bytecode instruction index minus one. 
     * That’s because the interpreter advances 
     * past each instruction before executing it. */
    size_t instruction = vm.ip - vm.chunk->code - 1;
    int line = vm.chunk->lines[instruction];
    fprintf(stderr, "[line %d] in script\n", line);

    resetStack();
}

void initVM() 
{
    resetStack();

    vm.objects = NULL;

    /* Initialize the hash table to a valid state when the VM boots up. */
    initTable(&vm.globals);

    /* When spin up a new VM, the string table is empty. */
    initTable(&vm.strings);
}

void freeVM() 
{
    freeTable(&vm.globals);
    
    /* When shut down the VM, clean up any resources used by the table. */
    freeTable(&vm.strings);
    
    /* Once the program is done, free every object. */
    freeObjects();   
}

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
    /* First, move the stack pointer back 
     * to get to the most recent used slot in the array. 
     * Then look up the value at that index and return it. 
     * Don’t need to explicitly “remove” it from the array 
     * —— moving stackTop down is enough 
     * to mark that slot as no longer in use. */
    vm.stackTop--;
    return *vm.stackTop;
}

static Value peek(int distance) 
{
    /* It returns a value from the stack but doesn’t pop it. 
     * The distance argument is how far 
     * down from the top of the stack to look: 
     * zero is the top, one is one slot down, etc. */
    return vm.stackTop[-1 - distance];
}

static bool isFalsey(Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() 
{
    /* First, calculate the length of the result string 
     * based on the lengths of the operands. 
     * Allocate a character array for the result 
     * and then copy the two halves in. 
     * As always, carefully ensure the string is terminated.
     */
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(chars, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
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

/* Reads a one-byte operand from the bytecode chunk. 
 * It treats that as an index into the chunk’s constant table 
 * and returns the string at that index. 
 * It doesn’t check that the value is a string 
 * — it just indiscriminately casts it. 
 * That’s safe because the compiler never emits an instruction 
 * that refers to a non-string constant. */
#define READ_STRING() AS_STRING(READ_CONSTANT())

/*  First, check that the two operands are both numbers. 
 *  If either isn’t, report a runtime error and yank the ejection seat lever. 
 *
 *  If the operands are fine, 
 *  pop them both and unwrap them. 
 *  Then apply the given operator, wrap the result, 
 *  and push it back on the stack.
 */
#define BINARY_OP(valueType, op) \
    do {\
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
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
            
            /* Since parsePrecedence() has already consumed the keyword token,
             * all need to do is output the proper instruction.
             * Figure that out based on the type of token we parsed. */
            case OP_NIL:      push(NIL_VAL);            break;
            case OP_TRUE:     push(BOOL_VAL(true));     break;
            case OP_FALSE:    push(BOOL_VAL(false));    break;
            /* As the name implies, 
             * that `OP_POP` instruction pops the top value off the stack 
             * and forgets it */
            case OP_POP:      pop();                    break;

            case OP_GET_LOCAL: {
                /* It takes a single byte operand for the stack slot 
                 * where the local lives. 
                 * It loads the value from that index and 
                 * then pushes it on top of the stack 
                 * where later instructions can find it. */
                uint8_t slot = READ_BYTE();
                push(vm.stack[slot]);
                break;                
            }

            case OP_SET_LOCAL: {
                /* It takes the assigned value from the top of the stack and 
                 * stores it in the stack slot corresponding 
                 * to the local variable. 
                 * Note that it doesn’t pop the value from the stack. 
                 * Remember, assignment is an expression, 
                 * and every expression produces a value. 
                 * The value of an assignment expression 
                 * is the assigned value itself, 
                 * so the VM just leaves the value on the stack. */
                uint8_t slot = READ_BYTE();
                vm.stack[slot] = peek(0);
                break;
            }
    
            case OP_GET_GLOBAL: {
                /* Pull the constant table index 
                 * from the instruction’s operand and 
                 * get the variable name. 
                 * Then use that as a key to look up the variable’s value 
                 * in the globals hash table. 
                 *
                 * If the key isn’t present in the hash table, 
                 * it means that global variable has never been defined. 
                 * That’s a runtime error in Lox, 
                 * so report it and exit the interpreter loop if that happens. 
                 * Otherwise, take the value and push it onto the stack. */
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;                     
            }
            
            case OP_DEFINE_GLOBAL: {
                /* Get the name of the variable from the constant table. 
                 * Then take the value from the top of the stack and 
                 * store it in a hash table with that name as the key. 
                 *
                 * This code doesn’t check to see 
                 * if the key is already in the table. 
                 * Lox is pretty lax with global variables and 
                 * lets you redefine them without error. 
                 * That’s useful in a REPL session, 
                 * so the VM supports that 
                 * by simply overwriting the value 
                 * if the key happens to already be in the hash table. */
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }

            case OP_SET_GLOBAL: {
                /*  If the variable hasn’t been defined yet, 
                 *  it’s a runtime error to try to assign to it. 
                 *  Lox doesn’t do implicit variable declaration. */
                ObjString* name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }

            case OP_GREATER:  BINARY_OP(BOOL_VAL,   >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL,   <); break;
            
            case OP_ADD: {
                /* If both operands are strings, it concatenates. 
                 * If they’re both numbers, it adds them. 
                 * Any other combination of operand types 
                 * is a runtime error. */
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError(
                        "Opreands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            } 
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:      push(BOOL_VAL(isFalsey(pop()))); break;
            case OP_NEGATE: {
                /* First, check to see if the value on top of the stack 
                 * is a number. 
                 * If it’s not, report the runtime error and 
                 * stop the interpreter. 
                 * Otherwise, keep going. 
                 * Only after this validation do unwrap the operand, 
                 * negate it, wrap the result and push it.
                 */
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            }
            
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }

            case OP_RETURN: {
                // Exit interpreter.
                return INTERPRET_OK;                    
            }
        }
    }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}   

InterpretResult interpret(const char* source)
{
    /*
     * Create a new empty chunk and pass it over to the compiler. 
     * The compiler will take the user’s program and 
     * fill up the chunk with bytecode. 
     * At least, that’s what it will do 
     * if the program doesn’t have any compile errors. 
     * If it does encounter an error, 
     * compile() returns false and discard the unusable chunk.
     */
    Chunk chunk;
    initChunk(&chunk);

    if (!compile(source, &chunk)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;

    InterpretResult result = run();

    freeChunk(&chunk);
    return result;
}
