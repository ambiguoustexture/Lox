#include <stdio.h>
#include <stdlib.h>

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

typedef void (*ParseFn)();

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

/* With a single global variable of the Parser struct type 
 * so don’t need to pass the state around from function to function; */
Parser parser;

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

static void endCompiler() { 
    emitReturn(); 
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
#endif 
}

static void expression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence); 

static void binary() 
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

static void literal()
{
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL  : emitByte(OP_NIL);   break;
        case TOKEN_TRUE : emitByte(OP_TRUE);  break;
        default:
            return; // Unreachable
    }
}

static void grouping() 
{
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// Parsing Functions:
static void number()
{
    /* To compile number literals, 
     * store a pointer at the TOKEN_NUMBER index in the array. 
     *
     * Assume the token for the number literal 
     * has already been consumed and is stored in previous. 
     */
    emitConstant(NUMBER_VAL(strtod(parser.previous.start, NULL)));
}

static void unary()
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
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_STRING]        = {NULL,     NULL,   PREC_NONE},
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

    prefixRule();

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
        infixRule();
    }
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
static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

bool compile(const char* source, Chunk* chunk) {
    /* Initialize the module variable 'compilingChunk' 
     * before write any bytecode. */
    compilingChunk = chunk;

    /* Pass in the chunk where the compiler will write the code, 
     * and then compile() returns whether or not compilation succeeded. */
    initScanner(source);
    
    {
        parser.hadError  = false;
        parser.panicMode = false;
    }

    /* The call to advance() “primes the pump” on the scanner. */
    advance();
    /* Then parse a single expression. */
    expression();
    /* After compile the expression, 
     * should be at the end of the source code, 
     * so check for the sentinel EOF token. */
    consume(TOKEN_EOF, "Expect end of expression.");

    /* Then, at the very end, 
     * when finish compiling the chunk, wrap things up. */
    endCompiler();
    return !parser.hadError;
}

