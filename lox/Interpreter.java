package com.craftinginterpreters.lox;

import java.util.ArrayList;
import java.util.List;

class Interpreter implements Expr.Visitor<Object>, 
                             Stmt.Visitor<Void>
{
    // The environment field in the interpreter changes 
    // as enter and exit local scopes. 
    // It tracks the current environment. 
    // This new globals field holds a fixed reference 
    // to the outermost global environment.
    final Environment globals = new Environment();
    private Environment environment = globals;

    Interpreter()
    {
        // This defines a variable named “clock”. 
        // Its value is a Java anonymous class that implements LoxCallable. 
        // The clock() function takes no arguments, so its arity is zero. 
        // The implementation of call() calls the corresponding Java function and 
        // converts the result to a double value in seconds.
        globals.define("clock", new LoxCallable() {
            @Override
            public int arity() { return 0; }

            @Override
            public Object call(Interpreter interpreter, List<Object> arguments) {
                return (double)System.currentTimeMillis() / 1000.0;
            }

            @Override
            public String toString() { return "<native fn>"; }
        });
    }

    void interpret(List<Stmt> statements)
    {
        try {
            for (Stmt statement : statements) {
                execute(statement);
            }
        } catch (RuntimeError error) {
            Lox.runtimeError(error);
        }
    }

    // Evaluating literals
    // Convert the literal tree node into a runtime value.
    @Override
    public Object visitLiteralExpr(Expr.Literal expr) 
    {
        return expr.value;
    }

    @Override
    public Object visitLogicalExpr(Expr.Logical expr) 
    {
        Object left = evaluate(expr.left);

        if (expr.operator.type == TokenType.OR) {
            if (isTruthy(left)) return left;
        } else {
            if (!isTruthy(left)) return left;
        }

        return evaluate(expr.right);
    }

    // Evaluating parentheses
    @Override 
    public Object visitGroupingExpr(Expr.Grouping expr) 
    {
        return evaluate(expr.expression);
    }

    private Object evaluate(Expr expr) 
    {
        return expr.accept(this);
    }

    private void execute(Stmt stmt)
    {
        stmt.accept(this);
    }

    void executeBlock(List<Stmt> statements,
                      Environment environment) 
    {
        // This new method executes a list of statements in the context of a given environment. 
        // Up until now, the environment field in Interpreter 
        // always pointed to the same environment—the global one. 
        // Now, that field represents the current environment. 
        // That’s the environment that corresponds to the innermost scope containing the code to be executed.
        Environment previous = this.environment;
        try {
            // To execute code within a given scope, 
            // this method updates the interpreter’s environment field, 
            // visits all of the statements, 
            // and then restores the previous value. 
            this.environment = environment;
            
            for (Stmt statement : statements) {
                execute(statement);
            }
        } finally {
            this.environment = previous;
        }
    }

    @Override
    public Void visitBlockStmt(Stmt.Block stmt)
    {
        // To execute a block, 
        // create a new environment for the block’s scope and 
        // pass it off to this other method.

        executeBlock(stmt.statements, new Environment(environment));
        return null;
    }

    @Override
    public Void visitExpressionStmt(Stmt.Expression stmt) 
    {
        evaluate(stmt.expression);
        return null;
    }

    @Override
    public Void visitFunctionStmt(Stmt.Function stmt) 
    {
        // Take a function syntax node — a compile time representation of the function, 
        // and convert it to its runtime representation. 
        // Here, that’s a LoxFunction that wraps the syntax node.
        
        // Function declarations are different from other literal nodes in that 
        // the declaration also binds the resulting object to a new variable. 
        // So, after creating the LoxFunction, create a new binding in the current environment 
        // and store a reference to it there.

        // When create a LoxFunction, capture the current environment.
        LoxFunction function = new LoxFunction(stmt, environment);
        // This is the environment that is active 
        // when the function is declared not when it’s called, 
        // which is what want. 
        // It represents the lexical scope 
        // surrounding the function declaration. 
        // Finally, when call the function, 
        // use that environment as the call’s parent 
        // instead of going straight to globals.
        environment.define(stmt.name.lexeme, function);
        return null;
    }

    @Override
    public Void visitIfStmt(Stmt.If stmt) 
    {
        if (isTruthy(evaluate(stmt.condition))) {
            execute(stmt.thenBranch);
        } else if (stmt.elseBranch != null) {
            execute(stmt.elseBranch);
        }

        return null;
    }

    @Override
    public Void visitPrintStmt(Stmt.Print stmt)
    {
        Object value = evaluate(stmt.expression);
        System.out.println(stringify(value));
        return null;
    }

    @Override
    public Void visitReturnStmt(Stmt.Return stmt)
    {
        // When the return is executed, 
        // the interpreter needs to jump all the way 
        // out of whatever context it’s currently in and 
        // cause the function call to complete, 
        // like some kind of jacked up control flow construct.
        
        // When execute a return statement, 
        // use an exception to unwind the interpreter 
        // past the visit methods of all of the containing statements 
        // back to the code that began executing the body.
        Object value = null;
        if (stmt.value != null) value = evaluate(stmt.value);

        throw new Return(value);
    }

    @Override
    public Void visitVarStmt(Stmt.Var stmt) 
    {
        Object value = null;
        if (stmt.initializer != null) {
            value = evaluate(stmt.initializer);
        }

        environment.define(stmt.name.lexeme, value);
        return null;
    }

    @Override
    public Void visitWhileStmt(Stmt.While stmt)
    {
        while (isTruthy(evaluate(stmt.condition))) {
            execute(stmt.body);
        }

        return null;
    }

    @Override 
    public Object visitAssignExpr(Expr.Assign expr)
    {
        Object value = evaluate(expr.value);

        environment.assign(expr.name, value);
        return value;
    }
    
    @Override
    public Object visitVariableExpr(Expr.Variable expr)
    {
        return environment.get(expr.name);
    }

    // Evaluating unary expressions
    @Override 
    public Object visitUnaryExpr(Expr.Unary expr) 
    {
        Object right = evaluate(expr.right);

        switch (expr.operator.type) {
            case BANG:
                return !isTruthy(right);
            case MINUS:
                checkNumberOperand(expr.operator, right);
                return -(double)right;
        }

        // Unreachable
        return null;
    }

    private void checkNumberOperand(Token operator, Object operand) 
    {
        if (operand instanceof Double) return;
            throw new RuntimeError(operator, "Operand must be a number.");
    }

    private void checkNumberOperands(Token operator, Object left, Object right)
    {
        if (left instanceof Double && right instanceof Double) return ;
        throw new RuntimeError(operator, "Operands must be numbers.");
    }

    // Truthiness and falsiness
    private boolean isTruthy(Object object) 
    {
        if (object == null) return false;
        if (object instanceof Boolean) return (boolean)object;
        return true;
    }

    private boolean isEqual(Object a, Object b) 
    {
        if (a == null && b == null) return true;
        if (a == null) return false;

        return a.equals(b);
    }

    private String stringify(Object object) 
    {
        if (object == null) return "nil";

        if (object instanceof Double) {
            String text = object.toString();
            if (text.endsWith(".0")) {
                text = text.substring(0, text.length() - 2);
            }
            return text;
        }

        return object.toString();
    }

    // Evaluating binary operators
    @Override 
    public Object visitBinaryExpr(Expr.Binary expr) 
    {
        Object left  = evaluate(expr.left );
        Object right = evaluate(expr.right);

        switch (expr.operator.type) {
            case GREATER:
                checkNumberOperands(expr.operator, left, right);
                return (double)left >  (double)right;
            case GREATER_EQUAL:
                checkNumberOperands(expr.operator, left, right);
                return (double)left >= (double)right;
            case LESS:
                checkNumberOperands(expr.operator, left, right);
                return (double)left <  (double)right;
            case LESS_EQUAL:
                checkNumberOperands(expr.operator, left, right);
                return (double)left <= (double)right;
            case MINUS:
                checkNumberOperands(expr.operator, left, right);
                return (double)left -  (double)right;
            case PLUS:
                if (left instanceof Double && right instanceof Double) {
                    return (double)left + (double)right;
                }

                if (left instanceof String && right instanceof String) {
                    return (String)left + (String)right;
                }

                throw new RuntimeError(expr.operator, "Operands must be two numbers or two strings.");
            case SLASH:
                checkNumberOperands(expr.operator, left, right);
                return (double)left / (double)right;
            case STAR:
                checkNumberOperands(expr.operator, left, right);
                return (double)left * (double)right;

            case BANG_EQUAL:  return !isEqual(left, right);
            case EQUAL_EQUAL: return  isEqual(left, right);
        }

        // Unreachable
        return null;
    }

    @Override
    public Object visitCallExpr(Expr.Call expr)
    {
        // First, evaluate the expression for the callee. 
        // Typically, this expression is just an identifier 
        // that looks up the function by its name, 
        // but it could be anything. 
        // Then evaluate each of the argument expressions in order 
        // and store the resulting values in a list.
        Object callee = evaluate(expr.callee);

        List<Object> arguments = new ArrayList<>();
        for (Expr argument : expr.arguments) { // FIXME
            arguments.add(evaluate(argument));
        }

        if (!(callee instanceof LoxCallable)) {
            throw new RuntimeError(expr.paren, 
                "Can only call functions and classes.");
        }

        LoxCallable function = (LoxCallable)callee;
        if (arguments.size() != function.arity()) {
            throw new RuntimeError(expr.paren, "Expected " + 
                function.arity() + " arguments but got " + 
                arguments.size() + ".");
        }
        return function.call(this, arguments);
    }
}
