#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
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

/* In order to take the “precedence” as a parameter, 
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
} Local;

typedef struct {
    /* A simple flat array of all locals 
     * that are in scope during each point in the compilation process. 
     * All ordered in the array in the order 
     * that their declarations appear in the code*/
    Local locals[UINT8_COUNT];

    int localCount;
    int scopeDepth;
} Compiler;

/* With a single global variable of the Parser struct type 
 * so don’t need to pass the state around from function to function; */
Parser parser;

Compiler* current = NULL;

/* The chunk pointer is stored in a module level variable 
 * like storing other global state. */
Chunk* compilingChunk;

static Chunk* currentChunk()
{
    return compilingChunk;
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

static void emitReturn() { emitByte(OP_RETURN); }

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

static void initCompiler(Compiler* compiler)
{
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    current = compiler;
}

static void endCompiler() 
{ 
    emitReturn(); 
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
#endif 
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
        emitByte(OP_POP);
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
    return memcmp(a->start, b->start, a->length);
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
        local->depth = 1; 
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
            error("Already variable with this name in this scope.");
        }
    }

    addLocal(*name);
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

static void variable(bool canAssign)
{
    namedVariable(parser.previous, canAssign);
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
    [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
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
    [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EGO]           = {NULL,     NULL,   PREC_NONE},
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
    if (match(TOKEN_VAR)) {
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

bool compile(const char* source, Chunk* chunk) {
    /* Initialize the module variable 'compilingChunk' 
     * before write any bytecode. */
    compilingChunk = chunk;

    /* Pass in the chunk where the compiler will write the code, 
     * and then compile() returns whether or not compilation succeeded. */
    initScanner(source);
    
    Compiler compiler;
    initCompiler(&compiler);

    {
        parser.hadError  = false;
        parser.panicMode = false;
    }

    /* The call to advance() “primes the pump” on the scanner. */
    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    endCompiler();
    return !parser.hadError;
}

