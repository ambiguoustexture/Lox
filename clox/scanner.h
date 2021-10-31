#ifndef clox_scanner_h
#define clox_scanner_h

typedef enum {
    // Single-character tokens.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,
    
    // One or two character tokens.
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,

    // Literals.
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

    // Keywords.
    TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
    TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_EGO,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,
    
    /*
     * There are only a couple of errors that get detected during scanning: 
     * unterminated strings and 
     * unrecognized characters.
     */
    TOKEN_ERROR, TOKEN_EOF
  } TokenType;
/*
 * The novel part in clox’s Token type is how it represents the lexeme.
 *
 * Use the original source string as character store. 
 * Represent a lexeme by a pointer to its first character 
 * and the number of characters it contains. 
 * This means don’t need to worry about managing memory for lexemes at all 
 * and can freely copy tokens around. 
 * As long as the main source code string outlives all of the tokens, 
 * everything works fine.
 */

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

void initScanner(const char* source);
Token scanToken();

#endif 
