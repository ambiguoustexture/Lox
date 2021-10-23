package com.craftinginterpreters.lox;

import java.util.ArrayList;
import java.util.List;

import static com.craftinginterpreters.lox.TokenType.*;

class Parser
{
    private static class ParserError extends RuntimeException {}

    private final List<Token> tokens;
    // use `current` to point to the next token eagerly waiting to be parsed.
    private int current = 0;

    Parser(List<Token> tokens) 
    {
        this.tokens = tokens;
    }

    List<Stmt> parse() 
    {
        List<Stmt> statements = new ArrayList<>();
        while (!isAtEnd()) {
            statements.add(declaration());
        }

        return statements;
    }

    private Expr expression() {
        return assignment();
    }

    private Stmt declaration()
    {
        try {
            if (match(VAR)) return varDeclaration();

            return statement();
        } catch (ParserError error) {
            synchronize();
            return null;
        }
    }

    private Stmt statement()
    {
        if (match(PRINT)) return printStatement();
        if (match(LEFT_BRACE)) return new Stmt.Block(block());

        return expressionStatement();
    }

    private Stmt printStatement()
    {
        Expr value = expression();
        consume(SEMICOLON, "Expect ';' after value.");
        return new Stmt.Print(value);
    }

    private Stmt varDeclaration()
    {
        Token name = consume(IDENTIFIER, "Expetc variable name.");

        Expr initializer = null;
        if (match(EQUAL)) {
            initializer = expression();
        }

        consume(SEMICOLON, "Expect ';' after variable declaration.");
        return new Stmt.Var(name, initializer);
    }

    private Stmt expressionStatement()
    {
        Expr expr = expression();
        consume(SEMICOLON, "Expect ';' after expression.");
        return new Stmt.Expression(expr);
    }

    private List<Stmt> block()
    {
        // Create an empty list and then parse statements and 
        // add them to the list until reach the end of the block, 
        // marked by the closing '}'. 
        // Note that the loop also has an explicit check for isAtEnd(). 
        // Have to be careful to avoid infinite loops, even when parsing invalid code. 
        // If the user forgot a closing }, the parser needs to not get stuck.
        List<Stmt> statements = new ArrayList<>();

        while (!check(RIGHT_BRACE) && !isAtEnd())
        {
            statements.add(declaration());
        }

        consume(RIGHT_BRACE, "Expect '}' after block.");
        return statements;
    }

    private Expr assignment()
    {
        Expr expr = equality();

        if (match(EQUAL)) {
            Token equals = previous();
            Expr value = assignment();

            if (expr instanceof Expr.Variable) {
                Token name = ((Expr.Variable)expr).name;
                return new Expr.Assign(name, value);
            }

            error(equals, "Invalid assigment target.");
        }
        
        return expr;
    }

    private Expr equality() 
    {
        Expr expr = comparision();

        while (match(BANG_EQUAL, EQUAL_EQUAL)) {
            Token operator = previous();
            Expr right = comparision();

            expr = new Expr.Binary(expr, operator, right);
        }

        return expr;
    }

    private Expr comparision() 
    {
        Expr expr = term();

        while (match(GREATER, GREATER_EQUAL, LESS, LESS_EQUAL)) {
            Token operator = previous();
            Expr right = term();
            expr = new Expr.Binary(expr, operator, right);
        }

        return expr;
    }

    private Expr term()
    {
        Expr expr = factor();

        while (match(MINUS, PLUS)) {
            Token operator = previous();
            Expr right = factor();
            expr = new Expr.Binary(expr, operator, right);
        }
        
        return expr;
    }

    private Expr factor()
    {
        Expr expr = unary();

        while (match(SLASH, STAR)) {
            Token operator = previous();
            Expr right = unary();
            expr = new Expr.Unary(operator, right);
        }

        return expr;
    }

    private Expr unary()
    {
        if (match(BANG, MINUS)) {
            Token operator = previous();
            Expr right = unary();
            return new Expr.Unary(operator, right);
        }

        return primary();
    }

    private Expr primary()
    {
        if (match(FALSE)) return new Expr.Literal(false);
        if (match(TRUE )) return new Expr.Literal(true );
        if (match(NIL  )) return new Expr.Literal(null );

        if (match(NUMBER, STRING)) 
            return new Expr.Literal(previous().literal);

        if (match(IDENTIFIER)) 
            return new Expr.Variable(previous());

        if (match(LEFT_PAREN)) {
            Expr expr = expression();
            consume(RIGHT_PAREN, "Expect ')' after expression.");
            return new Expr.Grouping(expr);
        }

        throw error(peek(), "Expect expression.");
    }

    private boolean match(TokenType...types) 
    {
        for (TokenType type : types) {
            if (check(type)) {
                advance();
                return true;
            }
        }

        return false;
    }

    private Token consume(TokenType type, String message) 
    {
        if (check(type)) return advance();

        throw error(peek(), message);
    }

    private ParserError error(Token token, String message) 
    {
        Lox.error(token, message);
        return new ParserError();
    }

    private void synchronize()
    {
        advance();

        while (!isAtEnd()) {
            if (previous().type == SEMICOLON) return;

            switch (peek().type) {
                case CLASS:
                case FUN:
                case VAR:
                case FOR:
                case IF:
                case WHILE:
                case PRINT:
                case RETURN:
                    return;
            }
            advance();
        }
    }

    private boolean check(TokenType type) 
    {
        if (isAtEnd()) current++;
            return peek().type == type;
    }

    private Token advance()
    {
        if (!isAtEnd()) current++;
        return previous();
    }

    private boolean isAtEnd() 
    {
        return peek().type == EOF;
    }

    private Token peek() 
    {
        return tokens.get(current);
    }

    private Token previous() 
    {
        return tokens.get(current - 1);
    }

}
