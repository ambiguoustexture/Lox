package com.craftinginterpreters.lox;

import java.util.List;


class AstPrinter implements Expr.Visitor<String>
{
    String print(Expr expr) 
    {
        return expr.accept(this);
    }

    @Override
    public String visitBinaryExpr(Expr.Binary expr) 
    {
        return parenthesize(expr.operator.lexeme,
            expr.left, expr.right);
    }

    @Override
    public String visitCallExpr(Expr.Call expr)
    {
        return parenthesize2("call", expr.callee, expr.arguments);
    }
    
    @Override
    public String visitGroupingExpr(Expr.Grouping expr) 
    {
        return parenthesize("group", expr.expression);
    }
    
    @Override
    public String visitLiteralExpr(Expr.Literal expr) 
    {
        if (expr.value == null) return "nil";
        return expr.value.toString();
    }

    @Override
    public String visitLogicalExpr(Expr.Logical expr)
    {
        return parenthesize(expr.operator.lexeme, expr.left, expr.right);
    }

    @Override
    public String visitSetExpr(Expr.Set expr)
    {
        return parenthesize2("=", 
            expr.object, expr.name.lexeme, expr.value);
    }

    @Override
    public String visitSuperExpr(Expr.Super expr) 
    {   
        return parenthesize2("super", expr.method);
    }  

    @Override
    public String visitGetExpr(Expr.Get expr) 
    {
        return parenthesize2(".", 
            expr.object, expr.name.lexeme);
    }

    @Override
    public String visitEgoExpr(Expr.Ego expr)
    {
        return "ego";
    }
    
    @Override
    public String visitUnaryExpr(Expr.Unary expr) {
        return parenthesize(expr.operator.lexeme, expr.right);
    }

    @Override
    public String visitVariableExpr(Expr.Variable expr)
    {
        return expr.name.lexeme;
    }

    @Override
    public String visitAssignExpr(Expr.Assign expr)
    {
        return parenthesize2("=", expr.name.lexeme, expr.value);
    }

    private String parenthesize(String name, Expr... exprs) 
    {
        StringBuilder builder = new StringBuilder();
    
        builder.append("(").append(name);
        for (Expr expr : exprs) {
            builder.append(" ");
            builder.append(expr.accept(this));
        }
        builder.append(")");
    
        return builder.toString();
    }

    private String parenthesize2(String name, Object... parts) 
    {
        StringBuilder builder = new StringBuilder();
        
        builder.append("(").append(name);
        transform(builder, parts);
        builder.append(")");

        return builder.toString();
    }

    private void transform(StringBuilder builder, Object... parts) 
    {
        for (Object part : parts) {
            builder.append(" ");
            if (     part instanceof Expr) {
                builder.append(((Expr)part).accept(this));
            } 
            // else if (part instanceof Stmt) {
            //     // commmented for error: 
            //     // method accept in class Stmt cannot be applied to given types;
            //     builder.append(((Stmt)part).accept(this)); // FIXME
            //     //                         ^
            //     // required: Visitor<R>
            //     // found:    AstPrinter
            //     // reason: cannot infer type-variable(s) R
            //     //   (argument mismatch; AstPrinter cannot be converted to Visitor<R>)
            //     // where R is a type-variable:
            //     // R extends Object declared in method <R>accept(Visitor<R>)
            // } 
            else if (part instanceof Token) {
                builder.append(((Token)part).lexeme);
            } 
            else if (part instanceof List) { 
                transform(builder, ((List) part).toArray()); 
            } 
            else {
                builder.append(part);
            }
        }
    }

    // public static void main(String[] args) 
    // {
    //     Expr expression = new Expr.Binary(
    //         new Expr.Unary(
    //             new Token(TokenType.MINUS, "-", null, 1), 
    //             new Expr.Literal(123)), 
    //         new Token(TokenType.STAR, "*", null, 1), 
    //         new Expr.Grouping( 
    //             new Expr.Literal(45.67)));
    //     
    //     System.out.println(new AstPrinter().print(expression));
    // }
}