package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Stack;

// Each time the resolver visits a variable, it tells the interpreter how many scopes there are 
// between the current scope and the scope where the variable is defined. 
// At runtime, this corresponds exactly to the number of environments 
// between the current one and the enclosing one 
// where the interpreter can find the variable’s value.
class Resolver implements Expr.Visitor<Void>, Stmt.Visitor<Void>
{
    private final Interpreter interpreter;

    // This field keeps track of the stack of scopes currently, in scope. 
    // Each element in the stack is a Map representing a single block scope. 
    // Keys, as in Environment, are variable names.

    // The scope stack is only used for local block scopes. 
    // Variables declared at the top level in the global scope 
    // are not tracked by the resolver 
    // since they are more dynamic in Lox. 
    // When resolving a variable, 
    // if can’t find it in the stack of local scopes, assume it must be global.
    private final Stack<Map<String, Boolean>> scopes = new Stack<>();
    private FunctionType currentFunction = FunctionType.NONE;

    Resolver(Interpreter interpreter)
    {
        this.interpreter = interpreter;
    }

    private enum FunctionType 
    {
        NONE,
        FUNCTION,
        INITIALIZER,
        METHOD
    }

    private enum CLassType 
    {
        NONE,
        CLASS,
        SUBCLASS
    }

    private CLassType currentClass = CLassType.NONE;

    private void resolve(Stmt stmt)
    {
        stmt.accept(this);
    }

    private void resolve(Expr expr)
    {
        expr.accept(this);
    }

    void resolve(List<Stmt> statements)
    {
        for (Stmt statement : statements) {
            resolve(statement);
        }
    }

    // Resolve the function's body:
    private void resolveFunction(Stmt.Function function, FunctionType type) 
    {
        // Take that parameter and store it in the field before resolving the body.
        // Stash the previous value of the field in a local variable first.
        // Use an explicit stack of FunctionType values for that, 
        // but instead one will piggyback on the JVM. 
        // Store the previous value in a local on the Java stack.
        FunctionType enclosingFunction = currentFunction;
        currentFunction = type;
        // Creates a new scope for the body and then 
        // binds variables for each of the function’s parameters.
        beginScope();
        for (Token param : function.params) {
            declare(param);
            define(param);
        }
        // Once that’s ready, resolves the function body in that scope. 
        // This is different from how the interpreter handles function declarations. 
        // At runtime declaring a function doesn’t do anything with the function’s body. 
        // The body doesn’t get touched until later when the function is called. 
        // In a static analysis, immediately traverse into the body right then and there.
        resolve(function.body);

        endScope();
        // When one are done resolving the function body, 
        // restore the field to that value.
        currentFunction = enclosingFunction;
    }

    private void beginScope()
    {
        // Lexical scopes nest in both the interpreter and the resolver. 
        // They behave like a stack. 
        // The interpreter implements that stack 
        // using a linked list — the chain of Environment objects.
        scopes.push(new HashMap<String, Boolean>());
    }

    private void endScope()
    {
        scopes.pop();
    }

    private void declare(Token name)
    {
        // Declaration adds the variable to the innermost scope 
        // so that it shadows any outer one and 
        // so that know the variable exists. 
        // Mark it as “not ready yet” by binding its name to false in the scope map. 
        // The value associated with a key in the scope map represents 
        // whether or not have finished resolving that variable’s initializer.
        if (scopes.isEmpty()) return ;

        Map<String, Boolean> scope = scopes.peek();
        // Lox Do allow declaring multiple variables 
        // with the same name in the global scope, 
        // but doing so in a local scope is probably a mistake. 
        // If they knew the variable already existed, 
        // they would assign to it instead of using var. 
        // And if they didn’t know it existed, 
        // they probably don’t intend to overwrite the previous one.

        // When declare a variable in a local scope, 
        // already know the names of every variable 
        // previously declared in that same scope. 
        // If see a collision, we report an error.
        if (scope.containsKey(name.lexeme)) {
            Lox.error(name, 
                "Already variable with this name in this scope.");
        }

        scope.put(name.lexeme, true);
    }

    private void define(Token name)
    {
        // After declaring the variable, 
        // resolve its initializer expression in that same scope 
        // where the new variable now exists but is unavailable. 
        // Once the initializer expression is done, 
        // the variable is ready for prime time. 
        // Do that by defining it.
        // Set the variable’s value in the scope map to true to mark it 
        // as fully initialized and available for use. 
        if (scopes.isEmpty()) return ;
        scopes.peek().put(name.lexeme, true);
    }

    @Override
    public Void visitBlockStmt(Stmt.Block stmt)
    {
        beginScope();
        resolve(stmt.statements);
        endScope();

        return null;
    }

    @Override 
    public Void  visitClassStmt(Stmt.Class stmt)
    {
        // Store the previous value of the field in a local variable. 
        // This lets us piggyback onto the JVM 
        // to keep a stack of currentClass values.
        CLassType enclosingClass = currentClass;
        currentClass = CLassType.CLASS;

        declare(stmt.name);
        define(stmt.name);

        if (stmt.superclass != null && 
                stmt.name.lexeme.equals(stmt.superclass.name.lexeme)) {
            Lox.error(stmt.superclass.name, 
                "A class can't inherit from itself.");
        }

        if (stmt.superclass != null) {
            currentClass = CLassType.SUBCLASS;
            resolve(stmt.superclass);
        } 

        if (stmt.superclass != null) {
            beginScope();
            scopes.peek().put("super", true);
        }

        // Resolve it exactly like any other local variable 
        // using “ego” as the name for the “variable”. 

        // Before step in and start resolving the method bodies, 
        // push a new scope and define “ego” in it as if it were a variable. 
        // Then, when done, discard that surrounding scope.
        beginScope();
        scopes.peek().put("ego", true);

        for (Stmt.Function method : stmt.methods) {
            FunctionType declaration = FunctionType.METHOD;
            if (method.name.lexeme.equals("init")) {
                declaration = FunctionType.INITIALIZER;
            }
        }

        endScope();

        if (stmt.superclass != null) endScope();

        // Once the methods have been resolved, 
        // “pop” that stack by restoring the old value.
        currentClass = enclosingClass;
        return null;
    }

    @Override
    public Void visitExpressionStmt(Stmt.Expression stmt)
    {
        resolve(stmt.expression);

        return null;
    }

    @Override 
    public Void visitFunctionStmt(Stmt.Function stmt)
    {
        // Functions both bind names and introduce a scope. 
        // The name of the function itself is bound in the surrounding scope 
        // where the function is declared. 
        // When step into the function’s body, 
        // also bind its parameters into that inner function scope.

        declare(stmt.name);
        define(stmt.name);

        resolveFunction(stmt, FunctionType.FUNCTION);
        return null;
        // Declare and define the name of the function in the current scope. 
        // Unlike variables, though, define the name eagerly, 
        // before resolving the function’s body. 
        // This lets a function recursively refer to itself inside its own body.
    }

    @Override
    public Void visitIfStmt(Stmt.If stmt)
    {
        // Here, one can see how resolution is different from interpretation. 
        // When one resolve an if statement, there is no control flow. 
        // One resolve the condition and both branches. 
        // Where a dynamic execution only steps into the branch that is run, 
        // a static analysis is conservative —- 
        // it analyzes any branch that could be run. 
        // Since either one could be reached at runtime, resolve both.
        resolve(stmt.condition);
        resolve(stmt.thenBranch);
        if (stmt.elseBranch != null) resolve(stmt.elseBranch);

        return null;
    }

    @Override
    public Void visitPrintStmt(Stmt.Print stmt)
    {
        resolve(stmt.expression);

        return null;
    }

    @Override
    public Void visitReturnStmt(Stmt.Return stmt)
    {
        // Check whether or not inside a function declaration 
        // when resolving a return statement.
        if (currentFunction == FunctionType.NONE) {
            Lox.error(stmt.keyword, "Can't return from top-level code.");
        }

        if (stmt.value != null) {
            if (currentFunction == FunctionType.INITIALIZER) {
                Lox.error(stmt.keyword,
                    "Can't return a value from an initializer.");
            }
            resolve(stmt.value);
        }

        return null;
    }

    @Override
    public Void visitWhileStmt(Stmt.While stmt)
    {
        resolve(stmt.condition);
        resolve(stmt.body);

        return null;
    }

    @Override
    public Void visitVarStmt(Stmt.Var stmt)
    {
        // Split binding into two steps, 
        // declaring then defining,
        declare(stmt.name);
        if (stmt.initializer != null) {
            resolve(stmt.initializer);
        }
        define(stmt.name);

        return null;
    }

    @Override
    public Void visitBinaryExpr(Expr.Binary expr)
    {
        resolve(expr.left);
        resolve(expr.right);
        
        return null;
    } 

    @Override
    public Void visitCallExpr(Expr.Call expr) 
    {
        // Walk the argument list and resolve them all. 
        // The thing being called is also an expression (usually a variable expression) 
        // so that gets resolved too.
        resolve(expr.callee);

        for (Expr argument : expr.arguments) {
            resolve(argument);
        }

        return null;
    }

    @Override
    public Void visitGetExpr(Expr.Get expr) 
    {
        resolve(expr.object);

        return null;
    }

    @Override
    public Void visitGroupingExpr(Expr.Grouping expr)
    {
        resolve(expr.expression);

        return null;
    }

    @Override
    public Void visitLiteralExpr(Expr.Literal expr)
    {
        return null;
    }

    @Override
    public Void visitLogicalExpr(Expr.Logical expr)
    {
        // Since a static analysis does no control flow or short-circuiting, 
        // logical expressions are exactly the same as other binary operators.
        resolve(expr.left);
        resolve(expr.right);

        return null;
    }

    @Override
    public Void visitSetExpr(Expr.Set expr)
    {
        resolve(expr.value);
        resolve(expr.object);

        return null;
    }

    @Override
    public Void visitSuperExpr(Expr.Super expr)
    {
        if (currentClass == CLassType.NONE) {
            Lox.error(expr.keyword, 
                "Can't use 'super' outside of a class.");
        } else if (currentClass != CLassType.SUBCLASS) {
            Lox.error(expr.keyword, 
                "Can't use 'super' ina class with no superclass.");
        }

        // Resolve the super token exactly as if it were a variable. 
        // The resolution stores the number of hops along the environment chain 
        // that the interpreter needs to walk to find the environment 
        // where the superclass is stored.
        resolveLocal(expr, expr.keyword);
        return null;
    }

    @Override
    public Void visitEgoExpr(Expr.Ego expr) 
    {
        // When resolve a "ego" expression, 
        // the currentClass field gives the bit of data 
        // need to report an error 
        // if the expression doesn’t occur nestled inside a method body.
        if (currentClass == CLassType.NONE) {
            Lox.error(expr.keyword,
                "Can't use 'echo' outside of a class.");
        }
        
        resolveLocal(expr, expr.keyword);
        
        return null;
    }

    @Override
    public Void visitUnaryExpr(Expr.Unary expr)
    {
        resolve(expr.right);
        
        return null;
    }

    @Override
    public Void visitAssignExpr(Expr.Assign expr) 
    {
        // First, resolve the expression for the assigned value 
        // in case it also contains references to other variables.
        resolve(expr.value);
        // Then use our existing resolveLocal() method to resolve the variable 
        // that’s being assigned to.
        resolveLocal(expr, expr.name);

        return null;
    }

    @Override
    public Void visitVariableExpr(Expr.Variable expr)
    {
        // First, check to see if the variable is being accessed inside its own initializer. 
        // This is where the values in the scope map come into play. 
        // If the variable exists in the current scope but its value is false, 
        // that means have declared it but not yet defined it. Report that error.
        if (!scopes.isEmpty() && 
                scopes.peek().get(expr.name.lexeme) == Boolean.FALSE) {
            Lox.error(expr.name, 
                "Can't read local variable in its own initializer.");    
        }

        resolveLocal(expr, expr.name);

        return null;
    }

    private void resolveLocal(Expr expr, Token name) 
    {
        // Start at the innermost scope and work outwards, 
        // looking in each map for a matching name. 
        // If find the variable, resolve it, passing in the number of scopes 
        // between the current innermost scope and the scope where the variable was found. 
        // So, if the variable was found in the current scope, pass in 0. 
        // If it’s in the immediately enclosing scope, 1.
        for (int i = scopes.size() - 1; i >= 0; i--) {
            if (scopes.get(i).containsKey(name.lexeme)) {
                interpreter.resolve(expr, scopes.size() - 1 - i);
                return ;
            }
        }
    }

}
