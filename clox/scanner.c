#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    /*
     * The start pointer 
     * marks the beginning of the current lexeme being scanned, and 
     * current points to 
     * the current character being looked at.
     * And a line field to track 
     * what line the current lexeme is on for error reporting. 
     */
    const char* start;
    const char* current;
    int line;
} Scanner;

/*
 * As the scanner chews through the user’s source code, 
 * it tracks how far it’s gone. 
 * Like did with the VM, 
 * wrap that state in a struct and 
 * then create a single top-level module variable of that type 
 * so don’t have to pass it around all of the various functions.
 */
Scanner scanner;

void initScanner(const char* source) 
{
    scanner.start   = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool isDigit(char c) 
{
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || 
           (c >= 'A' && c <= 'Z') || 
           (c == '_');
}

static bool isAtEnd()
{
    /*
     * Rrequire the source string to be a good null-terminated C string. 
     * If the current character is the null byte, 
     * then reached the end.
     */
    return *scanner.current == '\0';
}

static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

static char peek() 
{
    return *scanner.current;
}

static char peekNext()
{
    /* 
     * This is like peek() but for one character past the current one
     */
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

static bool match(char expected) 
{
    /*
     * If the current character is the desired one, 
     * advance and return true. 
     * Otherwise, return false to indicate it wasn’t matched.
     */
    if (isAtEnd())                    return false;
    if (*scanner.current != expected) return false;

    scanner.current++;
    return true;
}

static Token makeToken(TokenType type)
{
    Token token; {
        /*
         * Uses the scanner’s start and current pointers
         * to capture the token’s lexeme.
         * Sets a couple of other obvious fields then returns the token.
         */
        token.type   = type;

        token.start  = scanner.start;
        token.length = (int)(scanner.current - scanner.start);
        token.line   = scanner.line;
    }

    return token;
}

static Token errorToken(const char* message) 
{
    Token token; {
        /*
         * The only difference is that 
         * the “lexeme” points to the error message string 
         * instead of pointing into the user’s source code.
         */
        token.type   = TOKEN_ERROR;
        token.start  = message;
        token.length = (int)strlen(message);
        token.line   = scanner.line;
    }

    return token;
}

static void skipWhitespace() 
{
    /*
     * This advances the scanner past any leading whitespace. 
     * After this call returns, 
     * know the very next character is a meaningful one 
     * (or we’re at the end of the source code).
     */
    for (;;) {
        /*
         * Need to be careful that 
         * it does not consume any non-whitespace characters.
         */
        char c = peek();
        switch(c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            
            case '\n':
                scanner.line++;
                advance();
                break;

            case '/':
                /*
                 * With !=, still wanted to consume the ! 
                 * even if the = wasn’t found. 
                 * Comments are different, 
                 * if don’t find a second /, 
                 * then skipWhitespace() needs to 
                 * not consume the first slash either.
                 */
                if (peekNext() == '/') {
                    // A comment goes until the end of the line.
                     while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return ;
                } break;

            default:
                return ;
        }
    }
}

static Token number()
{
    while (isDigit(peek())) advance();

    // Look for a fractional part.
    if (peek() == '.' && isDigit(peekNext())) {
        // Consume the '.'
        advance();

        while (isDigit(peek())) advance();
    }
        
    return makeToken(TOKEN_NUMBER);
}

static TokenType checkKeyword(int start, int length, 
        const char* rest, TokenType type) 
{
    /*
     * Once found a prefix 
     * that could only be one possible reserved word, 
     * need to verify two things. 
     * The lexeme must be exactly as long as the keyword. 
     * And the remaining characters 
     * must match exactly—“supar” isn’t good enough.
     *
     * If do have the right number of characters, 
     * and they’re the ones wanted, 
     * then it’s a keyword, 
     * and return the associated token type. 
     * Otherwise, it must be a normal identifier.
     */
    if (scanner.current - scanner.start == start + length && 
            memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static TokenType identifierType() 
{
    /*
     * Instead of using `Lex`,
     * just need a tiny trie for recognizing keywords. 
     * The absolute simplest solution is 
     * to use a switch statement 
     * for each node with cases for each branch. 
     */

    switch (scanner.start[0]) {
        /*
         * These are the initial letters 
         * that correspond to a single keyword. 
         */
        case 'a': return checkKeyword(1, 2, "nd",    TOKEN_AND);
        case 'c': return checkKeyword(1, 4, "lass",  TOKEN_CLASS);
        case 'e': 
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'l': return checkKeyword(2, 2, "se", TOKEN_ELSE);
                    case 'g': return checkKeyword(2, 1, "o", TOKEN_EGO);
                }
            }
        case 'f':
            if (scanner.current - scanner.start > 1) { 
                switch (scanner.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
                } 
            } 
            break;
        case 'i': return checkKeyword(1, 1, "f",     TOKEN_IF);
        case 'n': return checkKeyword(1, 2, "il",    TOKEN_NIL);
        case 'o': return checkKeyword(1, 1, "r",     TOKEN_OR);
        case 'p': return checkKeyword(1, 4, "rint",  TOKEN_PRINT);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's': return checkKeyword(1, 4, "uper",  TOKEN_SUPER);
        case 't': return checkKeyword(1, 3, "rue",   TOKEN_TRUE);
        case 'v': return checkKeyword(1, 2, "ar",    TOKEN_VAR);
        case 'w': return checkKeyword(1, 4, "hile",  TOKEN_WHILE);
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier()
{
    /*
     * After the first letter, 
     * allow digits too, 
     * and keep consuming alphanumerics 
     * until run out of them.
     */
    while (isAlpha(peek()) || isDigit(peek())) advance();

    return makeToken(identifierType());
}

static Token string()
{
    /*
     * Consume characters until reach the closing quote. 
     * Also track newlines inside the string literal. 
     * (Lox supports multi-line strings.) 
     * And, as ever, gracefully handle running out of source code 
     * before find the end quote.
     */
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    // The closing quote of c-string.
    advance();
    return makeToken(TOKEN_STRING);
}

Token scanToken() 
{
    /*
     * The scanner needs to handle spaces, tabs, and newlines, 
     * but those characters don’t become part of any token’s lexeme.
     */
    skipWhitespace();

    /*
     * Since each call to this function scans a complete token, 
     * we know we are at the beginning of a new token 
     * when we enter the function. 
     * Thus, set scanner.start to point to the current character 
     * so to remember where the lexeme we’re about to scan starts.
     */
    scanner.start = scanner.current;

    /*
     * Then check to see if reached the end of the source code. 
     * If so, return an EOF token and stop. 
     * This is a sentinel value 
     * that signals to the compiler to stop asking for more tokens.
     */
    if (isAtEnd()) return makeToken(TOKEN_EOF);

    /*
     * If not at the end, do some... stuff... to scan the next token.
     * If that code doesn’t successfully scan and return a token, 
     * then reach the end of the function. 
     * That must mean we’re at a character that the scanner can’t recognize, 
     * so return an error token for that.
     */

    /* A Lexical Grammar for Lox
     */

    /* Read the next character from the source code, 
     * and then do a straightforward switch 
     * to see if it matches any of Lox’s one-character lexemes.
     */
    char c = advance();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();
    switch (c) {
        /* Single-character Punction Tokens */
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(TOKEN_MINUS);
        case '+': return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);

        /*  Two-character Punctuation Tokens */
        case '!': 
            return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

        /* Literal tokens */
        case '"':
            return string();
    }

    return errorToken("Unexpected character.");
}
