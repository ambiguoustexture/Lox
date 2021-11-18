#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

/* In order to take the “precedence” as a parameter.
 * define it numerically. */
typedef enum {
    /* These are all of Lox’s precedence levels 
     * in order from lowest to highest. 
     * Since C implicitly gives successively larger numbers for enums, 
     * this means that PREC_CALL is numerically larger than PREC_UNARY. */
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    /* - The function to compile a prefix expression 
     *   starting with a token of that type.
     * - The function to compile an infix expression 
     *   whose left operand is followed by a token of that type.
     * - The precedence of an infix expression 
     *   that uses that token as an operator.
     */
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    /* Store the name of the variable. 
     * When resolving an identifier, 
     * compare the identifier’s lexeme with each local’s name to find a match. 
     * The depth field records the scope depth of the block 
     * where the local variable was declared. */
    Token name;
    int depth;
    bool isCaptured;
} Local;

typedef struct {
    /* The index field stores which local slot the upvalue is capturing. 
     * The isLocal field deserves its own section, which we’ll get to next. */
    uint8_t index;
    bool    isLocal;
} Upvalue;

typedef enum {
    /* A little FunctionType enum.
     * This lets the compiler tell when it’s compiling top level code
     * versus the body of a function. */
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_MEHTOD,
    TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
    /* Unlike the Value and CallFrame stacks in the VM,
     * won’t use an array. Instead, use a linked list.
     * Each Compiler points back to the Compiler for the function
     * that encloses it, all the way back to the root Compiler
     * for the top level code. */
    struct Compiler* enclosing;
    /* To support that implicit top-level function.
     * Instead of pointing directly to a Chunk that the compiler writes to,
     * it will instead have a reference to the function object being built. */
    ObjFunction* function;
    FunctionType type;

    /* A simple flat array of all locals 
     * that are in scope during each point in the compilation process. 
     * All ordered in the array in the order 
     * that their declarations appear in the code*/
    Local locals[UINT8_COUNT];

    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;
} Compiler;

typedef struct ClassCompiler {
    /* Information about the nearest enclosing class. 
     * If had that, could use it to determine if inside a method.
     * 
     * Store only the class’s name. 
     * Also keep a pointer to the ClassCompiler for the enclosing class, if any. */ 
    struct ClassCompiler* enclosing;
    Token name;
    bool hasSuperslass;
} ClassCompiler;

/* With a single global variable of the Parser struct type 
 * so don’t need to pass the state around from function to function; */
Parser parser;

Compiler* current = NULL;

/* The chunk pointer is stored in a module level variable 
 * like storing other global state. */
Chunk* compilingChunk;

/* Information about the nearest enclosing class. 
 * If had that, could use it here to determine if inside a method. */
ClassCompiler* currentClass = NULL;

static Chunk* currentChunk()
{
    /* The current chunk is always the chunk owned by the function
     * we’re in the middle of compiling. */
    return &current->function->chunk;
}

static void errorAt(Token* token, const char* message)
{
    /* The trick is that while the panic mode flag is set, 
     * just simply suppress any other errors that get detected. */
    if (parser.panicMode) return ;
    parser.panicMode = true;

    /* First, print where the error occurred. 
     * Try to show the lexeme if it’s human-readable. 
     * Then print the error message itself. 
     * After that, set this hadError flag. 
     * That records whether any errors occurred during compilation. */
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // placeholder
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message)
{
    errorAt(&parser.previous, message);
}

/* If the scanner hands an error token, 
 * need to actually tell the user. */
static void errorAtCurrent(const char* message)
{
    /* Pull the location out of the current token 
     * in order to tell the user where the error occurred and 
     * forward it to errorAt(). */
    errorAt(&parser.current, message);
}

static void advance()
{
    /* Steps forward through the token stream. 
     * It asks the scanner for the next token and 
     * stores it for later use. 
     * Before doing that, 
     * it takes the old current token and stashes that in a previous field. 
     * That will come in handy later so that 
     * can get at the lexeme after matching a token. */
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        /* clox’s scanner doesn’t report lexical errors. 
         * Instead, it creates special “error tokens” and 
         * leaves it up to the parser to report them.
         *
         * Keep looping, reading tokens and reporting the errors, 
         * until hit a non-error one or reach the end. 
         * That way, the rest of the parser only sees valid tokens. */
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message)
{
    /* It’s similar to advance() in that it reads the next token. 
     * But it also validates that the token has an expected type. 
     * If not, it reports an error. */ 
    if (parser.current.type == type) {
        advance();
        return ;
    }

    errorAtCurrent(message);
}

static bool check(TokenType type) 
{
    return parser.current.type == type;
}

static bool match(TokenType type) 
{
    /* If the current token has the given type, 
     * consume the token and return true. 
     * Otherwise leave the token alone and return false. */
    if (!check(type)) return false;
    advance();
    return true;
}

static void emitByte(uint8_t byte)
{
    /* Writes the given byte, 
     * which may be an opcode or an operand to an instruction. 
     * It sends in the previous token’s line information 
     * so that runtime errors are associated with that line. */
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2)
{
    emitByte(byte1);
    emitByte(byte2);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) 
{
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void emitReturn()
{
    /* Whenever the compiler emits the implicit return 
     * at the end of a body, 
     * check the type to decide 
     * whether to insert the initializer-specific behavior. */
    if (current->type == TYPE_INITIALIZER) {
        /* In an initializer, 
         * instead of pushing nil onto the stack before returning, 
         * load slot zero, which contains the instance. */
        emitBytes(OP_GET_LOCAL, 0);
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) 
{
    /* Most of the work happens in addConstant(). 
     * That adds the given value to the end of the chunk’s constant table 
     * and returns its index. 
     * The makeConstant() function’s job is mostly 
     * to make sure don’t have too many constants. 
     * Since the OP_CONSTANT instruction uses a single byte 
     * for the index operand, 
     * can only store and load up to 256 constants in a chunk.
     */
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Value value)
{
    /*
     * First, add the value to the constant table, 
     * then emit an OP_CONSTANT instruction 
     * that pushes it onto the stack at runtime. */
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset)
{
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
      error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler* compiler, FunctionType type)
{
    /* When initializing a new Compiler,
     * capture the about-to-no-longer-be-current one in that pointer. */
    compiler->enclosing = current;

    /* A function object is the runtime representation of a function,
     * but here are creating it at compile time.
     * The way to think of it is that
     * a function is similar to string and number literals.
     * It forms a bridge between the compile time and runtime world.
     * When get to function declarations,
     * those really are literals — they are a notation that
     * produces values of a built-in type.
     * So the compiler creates function objects during compilation.
     * Then, at runtime, they are simply invoked. */

    /* First, clear out the new Compiler fields. */
    compiler->function = NULL;
    compiler->type     = type;

    compiler->localCount = 0;
    compiler->scopeDepth = 0;

    /* Then allocate a new function object to compile into. */
    compiler->function = newFunction();

    current = compiler;

    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start,
                parser.previous.length);
    }

    /* Remember that the compiler’s locals array keeps track of
     * which stack slots are associated
     * with which local variables or temporaries.
     * From now on, the compiler implicitly claims stack slot zero
     * for the VM’s own internal use.
     * Give it an empty name so that the user can’t write an identifier
     * that refers to it. */
    Local* local = &current->locals[current->localCount++]; {
        local->depth = 0;
        local->isCaptured = false;

        /* For function calls, the slot ends up holding the function being called. 
         * Since the slot has no name, the function body never accesses it. 
         * For method calls, repurpose that slot to store the receiver. 
         * Slot zero will store the instance that this is bound to. 
         * In order to compile this expressions, 
         * the compiler simply needs to give the right name 
         * to that local variable. */
        if (type != TYPE_FUNCTION) {
            local->name.start = "ego";
            local->name.length = 3;
        } else {
            local->name.start = "";
            local->name.length = 0;
        }
    }
}

static ObjFunction* endCompiler()
{ 
    emitReturn(); 
    /* Previously, when interpret() called into the compiler,
     * it passed in a Chunk to be written to.
     * Now that the compiler creates the function object itself,
     * return that function. Grab it from the current compiler */
    ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), function->name != NULL ?
                function->name->chars : "<script>");
    }
#endif 

    /* Then when a Compiler finishes,
     * it pops itself off the stack
     * by restoring the previous compiler as the current one. */
    current = current->enclosing;

    /* And then return it to the previous `compile()`. */
    return function;
}

static void beginScope() 
{
    /* In order to “create” a scope, 
     * all we do is increment the current depth. */ 
    current->scopeDepth++;
}

static void endScope()
{
    current->scopeDepth--;

    /* Like ghosts, they linger on beyond the scope where they are declared. 
     *
     * When pop a scope, 
     * walk backwards through the local array looking 
     * for any variables declared at the scope depth just left. 
     * Discard them by simply decrementing the length of the array.
     *
     * There is a runtime component to this too. 
     * Local variables occupy slots on the stack. 
     * When a local variable goes out of scope, 
     * that slot is no longer needed and should be freed. 
     * So, for each variable that discard, 
     * also emit an OP_POP instruction to pop it from the stack. */
    while (current->localCount > 0 && 
           current->locals[current->localCount - 1].depth 
           > current->scopeDepth) {
        /* Now, at the end of a block scope when the compiler emits code 
         * to free the stack slots for the locals, 
         * can tell which ones need to get hoisted onto the heap. 
         * Use a new instruction for that. */
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static uint8_t identifierConstant(Token* name) 
{
    /* Global variables are looked up by name at runtime.
     * That means the VM —
     * the bytecode interpreter loop—needs access to the name.
     * A whole string is too big
     * to stuff into the bytecode stream as an operand.
     * Instead, store the string in the constant table
     * and the instruction then refers to the name
     * by its index in the table. */
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) 
{
    /* Since know the lengths of both lexemes, check that first. 
     * That will fail quickly for many non-equal strings. 
     * If the lengths are the same, 
     * check the characters using memcmp(). */
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name)
{
    /* Walk the list of locals that are currently in scope. 
     * If one has the same name as the identifier token, 
     * the identifier must refer to that variable. 
     * Walk the array backwards so that find the last declared variable 
     * with the identifier. 
     * That ensures that inner local variables correctly 
     * shadow locals with the same name in surrounding scopes. 
     *
     * The locals array in the compiler has the exact same layout 
     * as the VM’s stack will have at runtime. 
     * The variable’s index in the locals array 
     * is the same as its stack slot.
     * 
     * If make it through the whole array 
     * without finding a variable with the given name, 
     * it must not be a local. 
     * In that case, return -1 to signal that it wasn’t found and 
     * should be assumed to be a global variable instead. */
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i]; 
        if (identifiersEqual(name, &local->name)) {
            /* If the variable has the sentinel depth, 
             * it must be a reference to a variable in its own initializer, 
             * and report that as an error.  */
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) 
{
    /* The compiler keeps an array of upvalue structures 
     * to track the closed-over identifiers 
     * that it has resolved in the body of each function. 
     * The indexes in the compiler’s array match the indexes 
     * where upvalues will live in the ObjClosure at runtime.
     *
     * This function adds a new upvalue to that array. 
     * It also keeps track of the number of upvalues the function uses. 
     * It stores that count directly in the ObjFunction itself 
     * because we’ll also need that number for use at runtime. */
    int upvalueCount = compiler->function->upvalueCount;

    /* A closure may reference the same variable 
     * in a surrounding function multiple times. 
     * In that case, don’t want to waste time and memory 
     * creating a separate upvalue for each identifier expression. 
     * To fix that, before adding a new upvalue, 
     * first check to see if the function already has an upvalue 
     * that closes over that variable: */
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal)
            return i;
    }

    /* There’s a restriction on how many upvalues a function can have 
     * — how many unique variables it can close over. 
     * Given that, can afford a static array that large.
     * Also need to make sure the compiler doesn’t overflow that limit. */
    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
            return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index   = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) 
{
    /* Call this after failing to resolve a local variable 
     * in the current function’s scope, 
     * so know the variable isn’t in the current compiler. 
     * Recall that Compiler stores a pointer to the Compiler 
     * for the enclosing function and these pointers form a linked chain 
     * that goes all the way to the root Compiler for the top-level code. 
     * Thus, if the enclosing Compiler is NULL, 
     * know we’ve reached the outermost function 
     * without finding a local variable. 
     * The variable must be global so return -1. */
    if (compiler->enclosing == NULL) return -1;

    /* Otherwise, try to resolve the identifier 
     * as a local variable in the enclosing compiler. 
     * In other words, look for it right outside the current function. */
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        /* When resolving an identifier, 
         * if end up creating an upvalue for a local variable, 
         * mark it as captured. */
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    /* A function only ever captures
     * — either a local or upvalue 
     * — from the immediately surrounding function, 
     * which is guaranteed to still be around at the point 
     * that the inner function declaration executes. 
     *
     * In order to implement this, resolveUpvalue() becomes recursive.
     *
     * First, look for a matching local variable in the enclosing function. 
     * If we find one, capture that local and return. That’s the base case.*/
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    /* Otherwise, look for a local variable 
     * beyond the immediately enclosing function. 
     * Do that by recursively calling resolveUpvalue() 
     * on the enclosing compiler, not the current one. 
     * This series of resolveUpvalue() calls works its way 
     * along the chain of nested compilers until it hits one of the base cases 
     * — either it finds an actual local variable to capture or 
     * it runs out of compilers. */
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void addLocal(Token name) 
{
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return ;
    }
    /* But for local variables, 
     * the compiler does need to remember that the variable exists. 
     * That’s what declaring it does 
     * — it adds it to the compiler’s list of variables 
     * in the current scope. */
    Local* local = &current->locals[current->localCount++]; {
        /* Creates a new Local and 
         * appends it to the compiler’s array of variables. 
         * It stores the variable’s name and the depth of the scope 
         * that owns the variable. */
        local->name = name;
        /* Splitting a variable’s declaration into two phases. */
        local->depth = -1;
        /* This field is true if the local is captured 
         * by any later nested function declaration. 
         * Initially, all locals are not captured. */
        local->isCaptured = false;
    }
}



static void declareVariable()
{
    /* This is the point where the compiler 
     * records the existence of the variable. 
     * Only do this for locals, so if in the top level global scope, 
     * just bail out. 
     * Because global variables are late bound, 
     * the compiler doesn’t keep track of 
     * which declarations for them it has seen. */
    // Global variables are implicitly declared.
    if (current->scopeDepth == 0) return ;

    /* Local variables are appended to the array when they’re declared, 
     * which means the current scope is always at the end of the array. 
     * When declare a new variable, start at the end and 
     * work backwards looking for an existing variable with the same name. 
     * If find one in the current scope, report the error. 
     * Otherwise, if reach the beginning of the array or a variable 
     * owned by another scope 
     * then know we’ve checked all of the existing variables in the scope. */
    Token* name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

static uint8_t argumentList()
{
    /* Each argument expression generates code 
     * which leaves its value on the stack in preparation for the call.  */
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}


static void binary(bool canAssign) 
{
    /* Compiles the right operand, 
     * much like how unary() compiles its own trailing operand. 
     * Finally, it emits the bytecode instruction 
     * that performs the binary operation.
     *
     * When run, the VM will execute the left and right operand code, 
     * in that order, leaving their values on the stack. 
     * Then it executes the instruction for the operator. 
     * That pops the two values, 
     * computes the operation, and pushes the result.
     */

    // Remember the operator.
    TokenType operatorType = parser.previous.type;

    // Compile the right operand.
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    // Emit the operator instruction.
    switch (operatorType) {
        case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT);   break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL);            break;
        case TOKEN_GREATER:       emitByte(OP_GREATER);          break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT);    break;
        case TOKEN_LESS:          emitByte(OP_LESS);             break;
        case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:          emitByte(OP_ADD);              break;
        case TOKEN_BANG:          emitByte(OP_NOT);              break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT);         break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY);         break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE);           break;
        default:
            return; // Unreachable.
    }

}

static void call(bool canAssign) 
{
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign)
{
    /* The parser expects to find a property name 
     * immediately after the dot. 
     * Load that token’s lexeme into the constant table as a string 
     * so that the name is available at runtime. */
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(&parser.previous);

    /* If see an equals sign after the field name, 
     * it must be a set expression that is assigning to a field. 
     * But don’t always allow an equals sign after the field to be compiled. 
     *
     * Only parse and compile the equals part when canAssign is true. 
     * If an equals token appears when canAssign is false, 
     * dot() leaves it alone and returns. 
     * In that case, the compiler will eventually 
     * unwind up to parsePrecedence() 
     * which stops at the unexpected = still 
     * sitting as the next token and reports an error.
     *
     * If found an = in a context where it is allowed, 
     * then compile the right - hand expression being stored in the field. 
     * After that, emit a new OP_SET_PROPERTY instruction. 
     * That takes a single operand for the index of the property name 
     * in the constant table. 
     * If didn’t compile a set expression, 
     * assume it’s a getter and emit an OP_GET_PROPERTY instruction, 
     * which also takes an operand for the property name. */
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } 
    /*  Since recognize the `access-call` pair of operations 
     *  at compile time, 
     *  there is the opportunity to emit a new special instruction 
     *  that performs an optimized method call. */ 
    else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        /* After the compiler has parsed the property name, 
         * look for a left parenthesis. 
         * If match one, switch to a new code path. 
         * There, compile the argument list exactly like 
         * when compiling a call expression. 
         * Then emit a single new OP_INVOKE instruction. 
         *
         * In other words, 
         * this single instruction combines the operands 
         * of the OP_GET_PROPERTY and OP_CALL instructions 
         * it replaces, in that order. */ 
        emitBytes(OP_INVOKE, name);
        emitByte(argCount);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
    }
}

static void literal(bool canAssign)
{
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL  : emitByte(OP_NIL);   break;
        case TOKEN_TRUE : emitByte(OP_TRUE);  break;
        default:
            return; // Unreachable
    }
}

static void grouping(bool canAssign) 
{
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// Parsing Functions:
static void number(bool canAssign)
{
    /* To compile number literals, 
     * store a pointer at the TOKEN_NUMBER index in the array. 
     *
     * Assume the token for the number literal 
     * has already been consumed and is stored in previous. 
     */
    emitConstant(NUMBER_VAL(strtod(parser.previous.start, NULL)));
}

static void string(bool canAssign)
{
    /* This takes the string’s characters 
     * directly from the lexeme. 
     * The + 1 and - 2 parts trim the leading and trailing quotation marks. 
     * It then creates a string object, 
     * wraps it in a Value, and stuffs it into the constant table. */
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, 
                                    parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign)
{
    /* First, try to find a local variable with the given name. 
     * If find one, use the instructions for working with locals. 
     * Otherwise, assume it’s a global variable 
     * and use the existing bytecode instructions for globals. */
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        /* The new resolveUpvalue() function looks for a local variable 
         * declared in any of the surrounding functions. 
         * If it finds one, it returns an “upvalue index” for that variable.
         * Otherwise, it returns -1 to indicate the variable wasn’t found. 
         * If it was found, use two new instructions 
         * for reading or writing to the variable through its upvalue. */
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        /* Calls the same identifierConstant() function 
         * from before to take the given identifier token 
         * and add its lexeme to the chunk’s constant table as a string. 
         * All that remains is to emit an instruction 
         * that loads the global variable with that name. */
        arg  = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    /* In the parse function for identifier expressions, 
     * look for a following equals sign. 
     * If find one, instead of emitting code for a variable access, 
     * compile the assigned value and then emit an assignment instruction.  */
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static void and_(bool canAssign)
{
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void or_(bool canAssign)
{
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);
    patchJump(elseJump);
    emitByte(OP_POP);
    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void variable(bool canAssign)
{
    namedVariable(parser.previous, canAssign);
}

static Token syntheticToken(const char* text)
{
    /* A little helper function to create a synthetic token 
     * for the given constant string. */
    Token token; {
        token.start  = text;
        token.length = (int)strlen(text);
    } 
    return token;
}

static void super_(bool canAssign)
{
    /* A super token is not a standalone expression. 
     * Instead, the dot and method name following it 
     * are inseparable parts of the syntax. */ 
    
    /* A super call is only meaningful 
     * inside the body of a method 
     * (or in a function nested inside a method), 
     * and only inside the method of a class that has a superclass. 
     * Detect both of these cases using the value of currentClass. 
     * If that’s NULL or points to a class with no superclass, 
     * report those errors. */
    if (currentClass == NULL) {
        error("Can't use 'super' outside of a class.");
    } else if (!currentClass->hasSuperslass) {
        error("Can;t use 'super' in a class with no superclass.");
    }

    /* In other words, Lox doesn’t really have `super call` expressions, 
     * it has `super access` expressions, 
     * which can choose to immediately invoke if want. 
     * So when the compiler hits a super token, 
     * consume the subsequent `.` token and then look for a method name. 
     * Methods are looked up dynamically, 
     * so use identifierConstant() 
     * to take the lexeme of the method name token 
     * and store it in the constant table just like 
     * do for property access expressions. */
    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint8_t name = identifierConstant(&parser.previous);

    /* In order to access a superclass method on the current instance, 
     * the runtime needs both the receiver 
     * and the superclass of the surrounding method’s class. 
     * The first namedVariable() call generates code 
     * to look up the current receiver stored in the hidden variable “ego” 
     * and push it onto the stack. 
     * The second namedVariable() call emits code 
     * to look up the superclass from its “super” variable 
     * and push that on top. */
    namedVariable(syntheticToken("ego"),   false);
    /* Before emit anything, look for a parenthesized argument list. 
     * If find one, compile that. Then load the superclass. 
     * After that, emit a new OP_SUPER_INVOKE instruction. 
     * This superinstruction 
     * combines the behavior of OP_GET_SUPER and OP_CALL, 
     * so it takes two operands: 
     * the constant table index of the method name to lookup and 
     * the number of arguments to pass to it. */
    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_SUPER_INVOKE, name);
        emitByte(argCount);
    } else {
        /* Otherwise, 
         * if don’t find a '(', 
         * continue to compile the expression as a super access 
         * like did before and emit an OP_GET_SUPER. */
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_GET_SUPER, name);
    }
}

static void ego(bool canAssign)
{
    /* The reason bound methods need to keep hold of the receiver is 
     * so that it can be accessed inside the body of the method. 
     * Treat `ego` as a lexically-scoped local variable 
     * whose value gets magically initialized. 
     * Compiling it like a local variable 
     * means get a lot of behavior for free. 
     * In particular, closures inside a method 
     * that reference `ego` will do the right thing and 
     * capture the receiver in an upvalue. */

    /* When an outermost class body ends, 
     * enclosing will be NULL, so this resets currentClass to NULL. 
     * Thus to see if inside a class — and 
     * thus inside a method — simply check that module variable. */ {
        if (currentClass == NULL) {
            error("Can't use 'this' outside of a class.");
            return;
        }
    }

    /* Call the existing variable() function 
     * which compiles identifier expressions as variable accesses. 
     * It takes a single Boolean parameter 
     * for whether the compiler should look for a following = operator 
     * and parse a setter. 
     * Can’t assign to `ego`, so pass false to disallow that. */
    variable(false);
}

static void unary(bool canAssign)
{
    TokenType operatorType = parser.previous.type;

    // Compile the operand.
    parsePrecedence(PREC_UNARY);

    // Emit the operatro instruction.
    switch (operatorType) {
        case TOKEN_BANG:  emitByte(OP_NOT);    break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default:
            return ; // Unreachable
    }
}

ParseRule rules[] = {
    /* The table that drives the whole parser 
     * is an array of ParseRules. */
    [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     dot,    PREC_CALL},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {super_,     NULL,   PREC_NONE},
    [TOKEN_EGO]           = {ego,      NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence)
{
    /* At the beginning of parsePrecedence(), 
     * look up a prefix parser for the current token. 
     * The first token is always going to 
     * belong to some kind of prefix expression, by definition. 
     * It may turn out to be nested as an operand 
     * inside one or more infix expressions, 
     * but as you read the code from left to right, 
     * the first token you hit always belongs to a prefix expression. */

    /* Read the next token and look up the corresponding ParseRule. 
     * If there is no prefix parser 
     * then the token must be a syntax error. 
     * Report that and return to the caller. */
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return ;
    }

    /* Since assignment is the lowest precedence expression, 
     * the only time we allow an assignment is 
     * when parsing an assignment expression 
     * or top-level expression like in an expression statement. */
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    /* Otherwise, call that prefix parse function 
     * and let it do its thing. 
     * That prefix parser compiles the rest of the prefix expression, 
     * consuming any other tokens it needs, and returns back here. 
     * Infix expressions are where it gets interesting, 
     * since precedence comes into play. 
     * The implementation is remarkably simple. */
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static uint8_t parseVariable(const char* errorMessage)
{
    /* Takes the given token and 
     * adds its lexeme to the chunk’s constant table as a string. 
     * It then returns the index of that constant in the constant table. */
    consume(TOKEN_IDENTIFIER, errorMessage);

    /* First, “declare” the variable. 
     * After that, exit the function if in a local scope. 
     * At runtime, locals aren’t looked up by name. 
     * There’s no need to stuff the variable’s name into the constant table 
     * so if the declaration is inside a local scope, 
     * return a dummy table index instead. */
    declareVariable();
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void markInitialized()
{
    /* A top level function declaration will also call this function.
     * When that happens, there is no local variable to mark initialized
     * — the function is bound to a global variable. */
    if (current->scopeDepth == 0) return ;
    /* So this is really what “declaring” and “defining” a variable 
     * means in the compiler. 
     * “Declaring” is when it’s added to the scope, 
     * and “defining” is when it becomes available for use. */
    current->locals[current->localCount - 1].depth =
        current->scopeDepth;
}

static void defineVariable(uint8_t global)
{
    /* There is no code to create a local variable at runtime. 
     * Think about what state the VM is in. 
     * It has already executed the code for the variable’s initializer 
     * (or the implicit nil if the user omitted an initializer), 
     * and that value is sitting right on top of the stack 
     * as the only remaining temporary. */
    if (current->scopeDepth > 0) {
        markInitialized();
        return ;
    } 

    /* This outputs the bytecode instruction 
     * that defines the new variable and stores its initial value. 
     * The index of the variable’s name in the constant table 
     * is the instruction’s operand. 
     * As usual in a stack-based VM, 
     * emit this instruction last. 
     * At runtime, execute the code for the variable’s initializer first. 
     * That leaves the value on the stack. 
     * Then this instruction takes that and stores it away for later. */
    emitBytes(OP_DEFINE_GLOBAL, global);
}

static ParseRule* getRule(TokenType type) 
{
    /* It simply returns the rule at the given index. 
     * It’s called by binary() 
     * to look up the precedence of the current operator. 
     * This function exists solely to handle a declaration cycle 
     * in the C code.
     * binary() is defined before the rules table
     * so that the table can store a pointer to it.
     * That means the body of binary() cannot access the table directly. */
    return &rules[type];
}

/* The missing piece in the middle that connects those together. */
static void expression()
{
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block()
{
    /* Keeps parsing declarations and statements
     * until it hits the closing brace.
     * As do with any loop in the parser,
     * also check for the end of the token stream. */
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type)
{
    /* The Compiler struct stores data
     * like which slots are owned by which local variables,
     * how many blocks of nesting currently in, etc.
     * All of that is specific to a single function.
     *
     * When start compiling a function declaration,
     * create a new Compiler on the C stack and initialize it. */
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    // Compile the parameter list.
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    /* Functions aren’t very useful if can’t pass arguments to them.
     * Semantically, a parameter is simply a local variable
     * declared in the outermost lexical scope of the function body.
     * Get to use the existing compiler support
     * for declaring named local variables to parse and compile parameters.
     * Unlike local variables which have initializers,
     * there’s no code here to initialize the parameter’s value. */
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }

            uint8_t paramConstant = parseVariable("Expect parameter name.");
            defineVariable(paramConstant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    // The body.
    consume(TOKEN_LEFT_BRACE,  "Expect '{' before function body.");
    block();
    // Create the function object.
    ObjFunction* function = endCompiler();

    /* Before, the final bytecode for a declaration 
     * was a single OP_CONSTANT instruction to load the compiled function 
     * from the surrounding function’s constant table and 
     * push it onto the stack. 
     * Now we have a new instruction `OP_CLOSURE`.
     * Like OP_CONSTANT, it takes a single operand 
     * that represents a constant table index for the function. */
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

    /* The OP_CLOSURE instruction is unique in that 
     * it has a variably-sized encoding. 
     * For each upvalue the closure captures, 
     * there are two single-byte operands. 
     * Each pair of operands specifies what that upvalue captures. 
     * If the first byte is one, 
     * it captures a local variable in the enclosing function. 
     * If zero, it captures one of the function’s upvalues. 
     * The next byte is the local slot or upvalue index to capture. */
    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void method()
{
    /* Like OP_GET_PROPERTY and other instructions 
     * that need names at runtime, 
     * the compiler adds the method name token’s lexeme 
     * to the constant table, getting back a table index. 
     * Then emit an OP_METHOD instruction 
     * with that index as the operand. That’s the name. */
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifierConstant(&parser.previous);

    FunctionType type = TYPE_MEHTOD;

    /* Whenever the front end compiles an initializer method, 
     * it will emit different bytecode at the end of the body 
     * to return `ego` from the method 
     * instead of the usual implicit nil most functions return. 
     * In order to do that, the compiler needs to actually know 
     * when it is compiling an initializer. 
     * Detect that by checking to see 
     * if the name of the method compiling is “init” */
    if (parser.previous.length == 4 && 
            memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    } 

    /* The `function()` utility function 
     * compiles the subsequent parameter list and function body. 
     * Then it emits the code to create an ObjClosure 
     * and leave it on top of the stack. 
     * At runtime, the VM will find the closure there. */
    function(type);
    emitBytes(OP_METHOD, constant);
}

static void classDeclaration()
{
    /* Class declarations are statements and the parser recognizes one 
     * by the leading class keyword. */
    consume(TOKEN_IDENTIFIER, "Expect class name.");

    /* Capture the namme of the class 
     * after consume its identifier token. */
    Token className = parser.previous;

    /* Immediately after the class keyword is the class’s name. 
     * Take that identifier and 
     * add it to the surrounding function’s constant table 
     * as a string. */ 
    uint8_t nameConstant = identifierConstant(&parser.previous);
    
    /* The class’s name is also used to bind the class object 
     * to a variable of the same name. 
     * So declare a variable with that identifier 
     * right after consuming its token. */
    declareVariable();

    /* Emit a new instruction 
     * to actually create the class object at runtime.
     * That instruction takes the constant table index 
     * of the class’s name as an operand. */
    emitBytes(OP_CLASS, nameConstant);
    /* Before compiling the body of the class, 
     * define the variable for the class’s name. 
     * Declaring the variable adds it to the scope. 
     * For classes, we define the variable before the body. 
     * That way, users can refer to the containing class 
     * inside the bodies of methods. 
     * That’s useful for things like factory methods. */
    defineVariable(nameConstant);

    /* If aren’t inside any class declaration at all, 
     * the module variable currentClass is NULL. 
     * When the compiler begins compiling a class, 
     * it pushes a new ClassCompiler onto that implict linked stack. */
    ClassCompiler classCompiler; {
        classCompiler.name          = parser.previous;
        classCompiler.hasSuperslass = false;
        classCompiler.enclosing     = currentClass; 
    }
    currentClass = &classCompiler;

    /* After compile the class name, 
     * if the next token is a < then found a superclass clause. 
     * Consume the superclass’s identifier token, 
     * then call variable(). 
     * That function takes the previously consumed token, 
     * treats it as a variable reference, 
     * and emits code to load the variable’s value. 
     * In other words, it looks up the superclass by name 
     * and pushes it onto the stack. */
    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclasses name.");
        variable(false);

        /* A class cannot be its own superclass. */
        if (identifiersEqual(&className, &parser.previous)) {
            error("A class can't inherit from itself.");
        }

        /* Over in the front end, 
         * compiling the superclass clause emits bytecode 
         * that loads the superclass onto the stack. 
         * Instead of leaving that slot as a temporary, 
         * create a new scope and make it a local variable. */
        beginScope();
        /* Creating a new lexical scope ensures 
         * that if declare two classes in the same scope, 
         * each has a different local slot to store its superclass. */ 
        addLocal(syntheticToken("super"));
        defineVariable(0);

        
        /* After that, call namedVariable() to load the subclass 
         * doing the inheriting onto the stack, 
         * followed by an OP_INHERIT instruction. 
         * That instruction wires up the superclass to the new subclass. */
        namedVariable(className, false);
        emitByte(OP_INHERIT);

        /* If see a superclass clause, know compiling a subclass. */
        classCompiler.hasSuperslass = true;
    }

    /* Before start binding methods, 
     * emit whatever code is necessary 
     * to load the class back on top of the stack. 
     *
     * Right before compiling the class body, 
     * call namedVariable(). 
     * That helper function generates code to load a variable 
     * with the given name onto the stack. 
     * Then compile the methods. */
    namedVariable(className, false);

    /* Compile the body. */ 
    consume(TOKEN_LEFT_BRACE,  "Expect '{' before class body.");

    /* Compile a series of method declarations between the braces. 
     * Stop compiling methods when hit the final curly 
     * or if reach the end of the file. */
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        method();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body." );

    /* When execute each OP_METHOD instruction, 
     * the top of the stack is the closure 
     * for the method with the class right under it. 
     * Once reached the end of the methods, 
     * no longer need the class and pop it off the stack. */
    emitByte(OP_POP);

    /* Pop the scope and discard the “super” variable 
     * after compiling the class body and its methods. 
     * That way, the variable is accessible 
     * in all of the methods of the subclass. 
     * It’s a somewhat pointless optimization, 
     * but only create the scope if there is a superclass clause. 
     * Thus need to only close the scope if there is one. */
    if (classCompiler.hasSuperslass) {
        endScope();
    }

    /* The memory for the ClassCompiler struct lives right on the C stack, 
     * a handy capability get by writing our compiler using recursive descent. 
     * At the end of the class body, 
     * pop that compiler off the stack and restore the enclosing one. */
    currentClass = currentClass->enclosing;
}

static void funDeclaration()
{
    /* Functions are first class values
     * and a function declaration simply creates and stores one
     * in a newly-declared variable.
     * So parse the name just like any other variable declaration.
     * A function declaration at the top level
     * will bind the function to a global variable.
     * Inside a block or other function,
     * a function declaration creates a local variable. */
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
    /* It’s safe for a function to refer to its own name inside its body.
     * One can’t call the function and execute the body
     * until after it’s fully defined,
     * so one will never see the variable in an uninitialized state.
     * Practically speaking, it’s useful to allow this
     * in order to support recursive local functions. */
}

static void varDeclaration()
{
    uint8_t global = parseVariable("Expect variable name.");

    /* The keyword is followed by the variable name.
     * That’s compiled by parseVariable(),
     * Then look for an `=` followed by an initializer expression.
     * If the user doesn’t initialize the variable,
     * the compiler implicitly initializes it to nil
     * by emitting an OP_NIL instruction.
     * Either way, expect the statement to be terminated with a semicolon. */
    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}

static void expressionStatement()
{
    /* An “expression statement” is simply an expression
     * followed by a semicolon.
     * They’re how you write an expression in a context
     * where a statement is expected.
     * Usually, it’s so that you can call a function
     * or evaluate an assignment for its side effect. */
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    /* Semantically, an expression statement evaluates the expression
     * and discards the result.
     * The compiler directly encodes that behavior.
     * It emits the code for the expression,
     * and then an OP_POP instruction. */
    emitByte(OP_POP);
}

static void printStatement()
{
    /* A print statement evaluates an expression and prints the result,
     * so first parse and compile that expression.
     * The grammar expects a semicolon after that,
     * so consume it.
     * Finally, emit a `OP_PRINT` instruction to print the result. */
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void returnStatement()
{
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }

    /* The return value expression is optional,
     * so the parser looks for a semicolon token
     * to tell if a value was provided. */
    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        /* Report an error 
         * if a return statement in an initializer has a value. 
         * Still go ahead and compile the value afterwards 
         * so that the compiler doesn’t get confused 
         * by the trailing expression and 
         * report a bunch of cascaded errors. */
        if (current->type == TYPE_INITIALIZER) {
            error("Can't return a value from an initializer.");
        }

        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void forStatement()
{
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(TOKEN_SEMICOLON)) {
      // No initializer.
    } else if (match(TOKEN_VAR)) {
      varDeclaration();
    } else {
      expressionStatement();
    }

    int loopStart = currentChunk()->count;
    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) {
      expression();
      consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

      // Jump out of the loop if the condition is false.
      exitJump = emitJump(OP_JUMP_IF_FALSE);
      emitByte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    endScope();
}

static void ifStatement()
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

static void whileStatement()
{
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OP_POP);
}

static void synchronize()
{
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;

        /* Skip tokens indiscriminately until reach something
         * that looks like a statement boundary.
         * Recognize the boundary by looking for a preceding token
         * that can end a statement, like a semicolon.
         * Or we’ll look for a subsequent token that begins a statement,
         * usually one of the control flow or declaration keywords. */
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;

            default:
                // Do nothing.
                ;
        }

        advance();
    }
}

/* Keep compiling declarations until we hit the end of the source file. */
static void declaration()
{   
    if (match(TOKEN_CLASS)) {
        classDeclaration();
    } else if (match(TOKEN_FUN)) {
        funDeclaration();
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }

    /* If hit a compile error while parsing the previous statement,
     * will enter panic mode.
     * In that case, after the statement, start synchronizing. */
    if (parser.panicMode) synchronize();
}

static void statement()
{
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_FOR)) {
       forStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        /* Blocks are a kind of statement,
         * so the rule for them goes in the statement production. */
        beginScope();
        block();
        endScope();
    } else {
        /* Wait until see the next statement.
         * If don’t see a print keyword,
         * then must be looking at an expression statement. */
        expressionStatement();
    }
}

ObjFunction* compile(const char* source) {
    /* Pass in the chunk where the compiler will write the code,
     * and then compile() returns whether or not compilation succeeded. */
    initScanner(source);

    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    {
        parser.hadError  = false;
        parser.panicMode = false;
    }

    /* The call to advance() “primes the pump” on the scanner. */
    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    /* Get the function object from the compiler.
     * If there were no compile errors, return it.
     * Otherwise, signal an error by returning NULL.
     * This way, the VM doesn’t try to execute a function
     * that may contain invalid bytecode. */
    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}

void markCompilerRoots() 
{
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
