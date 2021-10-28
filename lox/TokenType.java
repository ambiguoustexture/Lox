// Keywords are part of the shape of the language’s grammar, 
// so the parser often has code like, “If the next token is while then do...” 
// That means the parser wants to know not just that it has a lexeme for some identifier, 
// but that it has a reserved word, and which keyword it is.

package com.craftinginterpreters.lox;

enum TokenType 
{
    // Single-character token.
    LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE,
    COMMA, DOT, MINUS, PLUS, SEMICOLON, SLASH, STAR,

    // ONE or two character tokens.
    BANG, BANG_EQUAL, EQUAL, EQUAL_EQUAL, GREATER, GREATER_EQUAL,
    LESS, LESS_EQUAL,

    // Literals.
    IDENTIFIER, STRING, NUMBER,

    // Keywords.
    AND, CLASS, ELSE, FALSE, FUN, FOR, IF, NIL, OR, PRINT, RETURN, 
    SUPER, EGO, TRUE, VAR, WHILE, 

    EOF
}

