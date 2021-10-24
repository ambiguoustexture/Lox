package com.craftinginterpreters.lox;

import java.util.List;

class LoxFunction implements LoxCallable 
{
    private final Stmt.Function declaration;
    private final Environment closure;
    
    LoxFunction(Stmt.Function declaration, Environment closure) 
    {
        this.declaration = declaration;
        this.closure     = closure;
    }

    @Override
    public int arity() 
    {
        return declaration.params.size();
    }

    @Override 
    public String toString() 
    {
        return "<fn " + declaration.name.lexeme + ">";
    }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments) 
    {
        // Core to functions are the idea of parameters, and that 
        // a function encapsulates those parameters -- 
        // no other code outside of the function can see them. 
        // This means each function gets its own environment where it stores those variables.

        // Further, this environment must be created dynamically. 
        // Each function call gets its own environment. 
        // Otherwise, recursion would break. 
        // If there are multiple calls to the same function in play at the same time, 
        // each needs its own environment, 
        // even though they are all calls to the same function.
        
        // At the beginning of the call, it creates a new environment. 
        // Then it walks the parameter and argument lists in lockstep. 
        // For each pair, 
        // it creates a new variable with the parameter’s name and 
        // Binds it to the argument’s value.
        
        // Then call() tells the interpreter 
        // to execute the body of the function 
        // In the new function-local environment. 
        // Up until here, the current environment was the environment 
        // where the function was being called.

        // Once the body of the function has finished executing, 
        // executeBlock() discards that function-local environment and 
        // restores the previous one that was active back at the callsite. 
        // Finally, call() returns null, 
        // which returns nil to the caller. 
        
        // Environment environment = new Environment(interpreter.globals);
        
        // This creates an environment chain 
        // that goes from the function’s body out through the environments 
        // where the function is declared all the way out to the global scope. 
        // The runtime environment chain 
        // matches the textual nesting of the source code like want.       
        Environment environment = new Environment(closure);
        for (int i = 0; i < declaration.params.size(); i++) {
            environment.define(declaration.params.get(i).lexeme, 
                arguments.get(i));
        }

        // Wrap the call to executeBlock() in a try-catch block. 
        // When it catches a return exception, 
        // it pulls out the value and makes that the return value from call(). 
        // If it never catches one of these exceptions, 
        // it means the function reached the end of its body 
        // without hitting a return statement. 
        // In that case, it implicitly returns nil.
        try {
            interpreter.executeBlock(declaration.body, environment);
        } catch (Return returnValue) {
            return returnValue.value;
        }

        // interpreter.executeBlock(declaration.body, environment);
        return null;
    }


}
