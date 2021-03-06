#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm;

static Value clockNative(int argCount, Value* args)
{
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack()
{
    /*
     * The only initialization needed
     * is to set stackTop to point to the beginning of the array
     * to indicate that the stack is empty.
     */
    vm.stackTop = vm.stack;
    /* When the VM starts up, the CallFrame stack is empty. */
    vm.frameCount = 0;

    /* The list starts out empty. */
    vm.openUpvales = NULL;
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

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        // -1 because the IP is sitting on the next instruction to be executed.
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ",
                function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack();
}

static void defineNative(const char* name, NativeFn function)
{
    /* It takes a pointer to a C function
     * and the name it will be known as in Lox.
     * Wrap the function in an ObjNative
     * and then store that in a global variable with the given name. */

    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void initVM()
{
    resetStack();

    vm.objects = NULL;

    /* The starting threshold here is arbitrary. 
     * It???s similar to the initial capacity picked for various dynamic arrays. 
     * The goal is to not trigger the first few GCs too quickly 
     * but also to not wait too long. 
     * If had some real-world Lox programs, 
     * could profile those to tune this. */ 
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;

    /* The worklist for gray objects starts out empty: */
    vm.grayCount    = 0;
    vm.grayCapacity = 0;
    vm.grayStack    = NULL;


    /* Initialize the hash table to a valid state when the VM boots up. */
    initTable(&vm.globals);

    /* Function itself allocates memory, 
     * which can trigger a GC. 
     * If the collector ran at just the wrong time, 
     * it would read `vm.initString` before it had been initialized. 
     * So, first zero the field out */
    vm.initString = NULL;
    /* Create and intern the string when the VM boots up. */
    vm.initString = copyString("init", 4);

    /* When spin up a new VM, the string table is empty. */
    initTable(&vm.strings);

    defineNative("clock", clockNative);
}

void freeVM()
{
    freeTable(&vm.globals);

    /* When shut down the VM, clean up any resources used by the table. */
    freeTable(&vm.strings);

    vm.initString = NULL;

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
     * Don???t need to explicitly ???remove??? it from the array
     * ?????? moving stackTop down is enough
     * to mark that slot as no longer in use. */
    vm.stackTop--;
    return *vm.stackTop;
}

static Value peek(int distance)
{
    /* It returns a value from the stack but doesn???t pop it.
     * The distance argument is how far
     * down from the top of the stack to look:
     * zero is the top, one is one slot down, etc. */
    return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure* closure, int argCount)
{
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.",
                closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    /* This simply initializes the next CallFrame on the stack.
     * It stores a pointer to the function being called
     * and points the frame???s ip to the beginning of the function???s bytecode.
     * Finally, it sets up the slots pointer
     * to give the frame its window into the stack.
     * The arithmetic there ensures
     * that the arguments already on the stack line up
     * with the function???s parameters. */
    CallFrame* frame = &vm.frames[vm.frameCount++]; {
        frame->closure = closure;
        frame->ip       = closure->function->chunk.code;

        /* The funny little - 1 is to skip over local slot zero,
         * which contains the function being called. */
        frame->slots = vm.stackTop - argCount - 1;
    }

    return true;
}

static bool callValue(Value callee, int argCount)
{
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                /* Pull the raw closure back out of the ObjBoundMethod 
                 * and use the existing call() helper 
                 * to begin an invocation of that closure 
                 * by pushing a CallFrame for it onto the call stack. */
                ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                
                /* At runtime, the receiver isn???t actually in slot zero. 
                 * The interpreter isn???t holding up its end of the bargain yet. 
                 * 
                 * When a method is called, 
                 * the top of the stack contains all of the arguments 
                 * and then just under those is the closure of the called method. 
                 * That???s where slot zero in the new CallFrame will be. */ 
                /* The `-argCount` skips past the arguments 
                 * and the `- 1` adjusts for the fact 
                 * that stackTop points just past the last used stack slot. */
                vm.stackTop[-argCount - 1] = bound->receiver;
                
                return call(bound->method, argCount);
            }

            case OBJ_CLASS: {
                /* If the value being called ??? the object that results 
                 * when evaluating the expression 
                 * to the left of the opening parenthesis ??? is a class, 
                 * then treat it as a constructor call. 
                 * Create a new instance of the called class 
                 * and store the result in the stack. */
                ObjClass* klass = AS_CLASS(callee);
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass)); 

                /* In Lox, the runtime allocates new raw instances, 
                 * and a class may declare an initializer to set up any fields. 
                 * And first, automatically calling init() on new instances. */
                Value initializer;
                if (tableGet(&klass->methods, vm.initString, &initializer)) {
                    /* After the runtime allocates the new instance, 
                     * look for an init() method on the class. 
                     * If find one, initiate a call to it. 
                     * This pushes a new CallFrame for the initializer???s closure. */
                    return call(AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    /* If there is no init() method, 
                     * then it doesn???t make any sense 
                     * to pass arguments to the class when creating the instance. */
                    runtimeError("Expected 0 arguments but got %d.", 
                            argCount);
                    return false;
                }

                return true;
            }
            case OBJ_CLOSURE: 
                return call(AS_CLOSURE(callee), argCount);

            case OBJ_NATIVE: {
                /* If the object being called is a native function,
                 * invoke the C function right then and there.
                 * There???s no need to muck with CallFrames or anything.
                 * Iust hand off to C,
                 * get the result and stuff it back in the stack.
                 * This makes native functions as fast as we can get. */
                NativeFn native = AS_NATIVE(callee);
                Value result = native(argCount, vm.stackTop - argCount);
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }

            default:
                // Non-callable object type.
                break;
        }
    }

    runtimeError("Can only call functions and classes.");
    return false;
}

static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) 
{
    /* Combines the logic of 
     * how the VM implements OP_GET_PROPERTY and OP_CALL instructions, 
     * in that order. 
     * First look up the method by name in the class???s method table. 
     * If don???t find one, report that runtime error and exit. */
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    /* Otherwise, take the method???s closure and push a call to it 
     * onto the CallFrame stack. 
     * Don???t need to heap allocate and initialize an ObjBoundMethod. 
     * In fact, don???t even need to juggle anything on the stack. 
     * The receiver and method arguments are already right where they need to be. */
    return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount) 
{
    /* First grab the receiver off the stack. 
     * The arguments passed to the method are above it on the stack, 
     * so peek that many slots down. 
     * Then cast the object to an instance and invoke the method on it. */
    Value receiver = peek(argCount);

    /* As with OP_GET_PROPERTY instructions, 
     * also need to handle the case where 
     * a user incorrectly tries to call a method on a value of the wrong type. */
    if (!IS_INSTANCE(receiver)) {
        runtimeError("Only instances have methods.");
        return false;
    }
    ObjInstance* instance = AS_INSTANCE(receiver);

    /* Before looking up a method on the instance???s class, 
     * look for a field with the same name. 
     * If find a field, then store it on the stack in place of the receiver, 
     * under the argument list. 
     * This is how OP_GET_PROPERTY behaves since the latter instruction executes 
     * before a subsequent parenthesized list of arguments has been evaluated. 
     * Then try to call that field???s value like the callable that it hopefully is. 
     * The callValue() helper will check the value???s type and 
     * call it as appropriate or report a runtime error 
     * if the field???s value isn???t a callable type like a closure. */
    Value value;
    if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-argCount - 1] = value;
        return callValue(value, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

static bool bindMethod(ObjClass* klass, ObjString* name)
{
    /* First look for a method with the given name in the class???s method table. 
     * If don???t find one, report a runtime error and bail out. 
     * Otherwise, take the method and wrap it in a new ObjBoundMethod. 
     * Grab the receiver from its home on top of the stack. 
     * Finally, pop the instance and replace the top of the stack 
     * with the bound method. */
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

static ObjUpvalue* captureUpvalue(Value* local)
{
    /* Whenever close over a local variable, 
     * before creating a new upvalue, 
     * look for an existing one in the list. 
     *
     * Start at the head of the list, 
     * which is the upvalue closest to the top of the stack. 
     * Walk through the list, 
     * using a little pointer comparison to iterate past.
     *
     * Every upvalue pointing to slots above the one looking for. 
     * While do that, keep track of the preceding upvalue on the list. 
     * Need to update that node???s next pointer 
     * if end up inserting a node after it.*/
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvales;

    while (upvalue != NULL && upvalue->location > local) {
        /* Three ways to exit the loop:
         * 1. The local slot stopped at is the slot looking for.
         * 2. Ran out of upvalues to search.
         * 3. Found an upvalue whose local slot is below the one looking for.
         */
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
    
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvales = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }
    
    return createdUpvalue;
}

static void closeUpvalues(Value* last) 
{
    /* Takes a pointer to a stack slot. 
     * It closes every open upvalue it can find 
     * that points to that slot or any slot above it on the stack.
     * Walk the VM???s list of open upvalues, again from top to bottom. 
     * If an upvalue???s location points into the range of slots we???re closing, 
     * close the upvalue. 
     * Otherwise, once reach an upvalue outside of the range, 
     * know the rest will be too so stop iterating. */
    while (vm.openUpvales != NULL && 
           vm.openUpvales->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvales;
        upvalue->closed   = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvales = upvalue->next;
    }
}

static void defineMethod(ObjString* name)
{
    /* The method closure is on top of the stack, 
     * above the class it will be bound to. 
     * Read those two stack slots and 
     * store the closure in the class???s method table. 
     * Then pop the closure since we???re done with it. */
    Value method = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
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
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

static InterpretResult run()
{
    /* First, store the current topmost CallFrame in a local variable
     * inside the main bytecode execution function.
     * Then replace the bytecode access macros with versions
     * that access ip through that variable. */
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, \
     (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())

/*  First, check that the two operands are both numbers.
 *  If either isn???t, report a runtime error and yank the ejection seat lever.
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
        /* Instead of passing in the VM???s chunk and ip fields,
         * now read from the current CallFrame. */
        disassembleInstruction(&frame->closure->function->chunk,
                (int)(frame->ip - frame->closure->function->chunk.code));
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
                /* Accesses the current frame???s slots array,
                 * which means it accesses the given numbered slot
                 * relative to the beginning of that frame. */
                push(frame->slots[slot]);
                break;
            }

            case OP_SET_LOCAL: {
                /* It takes the assigned value from the top of the stack and
                 * stores it in the stack slot corresponding
                 * to the local variable.
                 * Note that it doesn???t pop the value from the stack.
                 * Remember, assignment is an expression,
                 * and every expression produces a value.
                 * The value of an assignment expression
                 * is the assigned value itself,
                 * so the VM just leaves the value on the stack. */
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }

            case OP_GET_GLOBAL: {
                /* Pull the constant table index
                 * from the instruction???s operand and
                 * get the variable name.
                 * Then use that as a key to look up the variable???s value
                 * in the globals hash table.
                 *
                 * If the key isn???t present in the hash table,
                 * it means that global variable has never been defined.
                 * That???s a runtime error in Lox,
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
                 * This code doesn???t check to see
                 * if the key is already in the table.
                 * Lox is pretty lax with global variables and
                 * lets you redefine them without error.
                 * That???s useful in a REPL session,
                 * so the VM supports that
                 * by simply overwriting the value
                 * if the key happens to already be in the hash table. */
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }

            case OP_SET_GLOBAL: {
                /*  If the variable hasn???t been defined yet,
                 *  it???s a runtime error to try to assign to it.
                 *  Lox doesn???t do implicit variable declaration. */
                ObjString* name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_GET_UPVALUE: {
                /* The operand is the index 
                 * into the current function???s upvalue array. 
                 * So, simply look up the corresponding upvalue and 
                 * dereference its location pointer 
                 * to read the value in that slot. */
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);             
                break;
            }

            case OP_SET_UPVALUE: {
                /* Take the value on top of the stack 
                 * and store it into the slot pointed to by the chosen upvalue. 
                 * Just as with the instructions for local variables, */ 
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }

            case OP_GET_PROPERTY: {
                /* Only instances are allowed to have fields. 
                 * Can???t stuff a field onto a string or number. 
                 * So need to check that the value is an instance 
                 * before accessing any fields on it. */
                if (!IS_INSTANCE(peek(0))) {
                    runtimeError("Obly instance have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                /* When the interpreter reaches this instruction, 
                 * the expression to the left of the . has already 
                 * been executed and the resulting instance 
                 * is on top of the stack. 
                 * Read the field name from the constant pool 
                 * and look it up in the instance???s field table. 
                 * If the hash table contains an entry with that name, 
                 * pop the instance and push the entry???s value as the result. */
                ObjInstance* instance = AS_INSTANCE(peek(0));
                ObjString* name = READ_STRING();

                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop(); // Instance.
                    push(value);
                    break;
                }

                /* Take the instance???s class and pass it to bindMethod() helper. 
                 * If the function finds a method, 
                 * it places the method on the stack and returns true. 
                 * Otherwise it returns false 
                 * to indicate a method with that name couldn???t be found. 
                 * Since the name also wasn???t a field, 
                 * that means we have a runtime error, 
                 * which aborts the interpreter.*/
                if (!bindMethod(instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_SET_PROPERTY: {
                /* Unlike when reading a field, 
                 * don???t need to worry about the hash table 
                 * not containing the field. 
                 * A setter implicitly creates the field if needed. 
                 * Do need to handle the user incorrectly trying 
                 * to store a field on a value that isn???t an instance. 
                 *
                 * Check the value???s type and report a runtime error 
                 * if it???s invalid. 
                 * And, with that, the stateful side of Lox???s support 
                 * for object-oriented programming is in place. */ 
                if (!IS_INSTANCE(peek(1))) {
                    runtimeError("Only instance have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                /* On top of the stack, 
                 * first have the instance whose field is being set and 
                 * then above that is the value to be stored 
                 * in the instance???s field. 
                 * Read the instruction???s operand and 
                 * find the field name string. 
                 * Using that, store the value on top of the stack 
                 * into the instance???s field table. */
                ObjInstance* instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, READ_STRING(), peek(0));

                /* Pop the stored value off, then pop the instance, 
                 * and finally push the value back on. 
                 * In other words, remove the second element from the stack 
                 * while leaving the top alone. 
                 * A setter is itself an expression 
                 * whose result is the assigned value, 
                 * so need to leave that value on the stack. */ 
                Value value = pop();
                pop();
                push(value);
                break;
            }
            
            case OP_GET_SUPER: {
                /* Read the method name from the constant table. 
                 * Then pass that to bindMethod() 
                 * which looks up the method in the given class???s method table 
                 * and creates an ObjBoundMethod 
                 * to bundle the resulting closure to the current instance. */
                ObjString* name = READ_STRING();
                /* For a super call, don???t use the instance???s class. 
                 * Instead, use the statically resolved superclass 
                 * of the containing class, 
                 * which the compiler has conveniently ensured 
                 * is sitting on top of the stack. */
                ObjClass*  superclass = AS_CLASS(pop());
                /* Pop that superclass and pass it to bindMethod(), 
                 * which correctly skips over any overriding methods 
                 * in any of the subclasses 
                 * between that superclass and the instance???s own class. 
                 * It also correctly includes any methods 
                 * inherited by the superclass from any of its superclasses. */
                if (!bindMethod(superclass, name)) {
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
                 * If they???re both numbers, it adds them.
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
                        "Operands must be two numbers or two strings.");
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
                 * If it???s not, report the runtime error and
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

            case OP_INVOKE: {
                /* Look up the method name from the first operand and 
                 * then read the argument count operand. 
                 * Then hand off to invoke() to do the heavy lifting. 
                 * That function returns true if the invocation succeeds. 
                 * As usual, a false return means a runtime error occurred. 
                 * Check for that here and abort the interpreter 
                 * if disaster struck. */
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                if (!invoke(method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                /* Finally, assuming the invocation succeeded, 
                 * then there is a new CallFrame on the stack, 
                 * so refresh our cached copy of the current frame in frame. */
                frame = &vm.frames[vm.frameCount - 1];
                break;                
            }
        
            case OP_SUPER_INVOKE: {
                /* Basically the implementation of OP_INVOKE 
                 * mixed together with a dash of OP_GET_SUPER. */

                /* First, pull out the method name and argument count operands. 
                 * Then pop the superclass off the top of the stack 
                 * so that can look up the method in its method table. 
                 * This conveniently leaves the stack set up 
                 * just right for a method call. */
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();                
                ObjClass* superclass = AS_CLASS(pop());
                /* Pass the superclass, method name, and argument count 
                 * to the existing invokeFromClass() function. 
                 * That function looks up the given method on the given class 
                 * and attempts to create a call to it with the given arity. 
                 * If a method could not be found, 
                 * it returns false and bail out of the interpreter. 
                 * Otherwise, invokeFromClass() pushes a new CallFrame 
                 * onto the call stack for the method???s closure. 
                 * That invalidates the interpreter???s cached CallFrame pointer, 
                 * so refresh frame. */
                if (!invokeFromClass(superclass, method, argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            
            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure*  closure  = newClosure(function);
                push(OBJ_VAL(closure));
                
                /* Fill the upvalue array over in the interpreter 
                 * when it creates a closure. 
                 * This is where walk through all of the operands 
                 * after OP_CLOSURE 
                 * to see what kind of upvalue each slot captures. */
                for (int i = 0; i < closure->upvalueCount; i++) {
                    /* Iterate over each upvalue the closure expects. 
                     * For each one, read a pair of operand bytes. 
                     * If the upvalue closes over a local variable 
                     * in the enclosing function. */
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index   = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = 
                            captureUpvalue(frame->slots + index);
                    } else {
                        /* Otherwise, 
                         * capture an upvalue from the surrounding function. 
                         * An OP_CLOSURE instruction is emitted 
                         * at the end of a function declaration. 
                         * At the moment that are executing that declaration, 
                         * the current function is the surrounding one. */
                        closure->upvalues[i] = 
                            frame->closure->upvalues[index];
                    }
                }

                break;      
            }

            case OP_CLOSE_UPVALUE: {
                /* The compiler helpfully emits an OP_CLOSE_UPVALUE instruction 
                 * to tell the VM 
                 * exactly when a local variable 
                 * should be hoisted onto the heap. 
                 *
                 * When reach that instruction, 
                 * the variable hoisted is right on top of the stack. 
                 * Call a helper function, 
                 * passing the address of that stack slot. 
                 * That function is responsible for closing the upvalue and 
                 * moving the local from the stack to the heap. 
                 * After that, the VM is free to discard the stack slot, 
                 * which it does by calling pop(). */
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            }

            case OP_RETURN: {
                /* When a function returns a value,
                 * that value will be on top of the stack.
                 * About to discard the called function???s entire stack window,
                 * so pop that return value off and hang on to it.
                 * Then discard the CallFrame for the returning function.
                 * If that was the very last CallFrame,
                 * it means have finished executing the top level code.
                 * The entire program is done,
                 * so pop the main script function from the stack
                 * and then exit the interpreter.
                 *
                 * Otherwise, discard all of the slots the callee was using
                 * for its parameters and local variables.
                 * That includes the same slots
                 * the caller used to pass the arguments.
                 * Now that the call is done,
                 * the caller doesn???t need them anymore.
                 * This means the top of the stack ends up
                 * right at the beginning
                 * of the returning function???s stack window.
                 *
                 * Push the return value back onto the stack
                 * at that new lower location.
                 * Then update the run() function???s cached pointer
                 * to the current frame.
                 * Just like when began a call,
                 * on the next iteration of the bytecode dispatch loop,
                 * the VM will read ip from that frame
                 * and execution will jump back to the caller,
                 * right where it left off immediately
                 * after the OP_CALL instruction. */
                Value result = pop();

                /* By passing the first slot in the function???s stack window, 
                 * close every remaining open upvalue owned 
                 * by the returning function. */
                closeUpvalues(frame->slots);
                
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }

                vm.stackTop = frame->slots;
                push(result);

                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            case OP_CLASS: {
                /* Load the string for the class???s name 
                 * from the constant table and pass that to newClass(). 
                 * That creates a new class object with the given name. 
                 * Push that onto the stack. 
                 * If the class is bound to a global variable, 
                 * then the compiler???s call to defineVariable() 
                 * will emit code to store that object 
                 * from the stack into the global variable table. 
                 * Otherwise, it???s right where it needs to be 
                 * on the stack for a new local variable. */
                push(OBJ_VAL(newClass(READ_STRING())));
                break;
            }

            case OP_JUMP: {
                /* The jump instructions used to modify the VM???s ip field.
                 * Now, they do the same for the current frame???s ip. */
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }

            case OP_JUMP_IF_FALSE: {
                /*  */
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }

            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }

            case OP_CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            case OP_INHERIT: {
                /* The top of the stack contains the superclass 
                 * with the subclass on top. 
                 * Grab both of those and then do the inherit-y bit. 
                 * This is where clox takes a different path than jlox. 
                 * In the first jlox interpreter, 
                 * each subclass stored a reference to its superclass. 
                 * When a method was accessed, 
                 * if didn???t find it in the subclass???s method table, 
                 * recursed through the inheritance chain 
                 * looking at each ancestor???s method table until found it. */
                Value superclass = peek(1);
                /* Runtime check for invalid superclasses. */
                if (!IS_CLASS(superclass)) {
                    runtimeError("Superclass must be a class.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjClass* subclass = AS_CLASS(peek(0));
                tableAddAll(&AS_CLASS(superclass)->methods, 
                            &subclass->methods);
                pop(); // Subclass
                break;
            } 

            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char* source)
{
    /* First, pass the source code to the compiler.
     * It returns a new ObjFunction containing the compiled top-level code.
     * If get NULL back, it means there was some compile-time error
     * which the compiler has already reported.
     * In that case, bail out since can???t run anything.
     *
     * Otherwise, store the function on the stack
     * and prepare an initial CallFrame to execute its code.
     * Now can see why the compiler sets aside stack slot zero
     * ??? that stores the function being called.
     * In the new CallFrame, point to the function,
     * initialize its ip to point to the beginning of the function???s bytecode,
     * and set up its stack window to start at the very bottom
     * of the VM???s value stack. */
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    /* Now that have a handy function for initiating a CallFrame,
     * may as well use it to set up the first frame
     * for executing the top level code. */
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    callValue(OBJ_VAL(closure), 0);

    /* After finishing, the VM used to free the hardcoded chunk.
     * Now that the ObjFunction owns that code,
     * don???t need to do that anymore,
     * so the end of interpret() is simply. */
    return run();
}
