package com.craftinginterpreters.lox;

import java.util.List;
import java.util.Map;

class LoxClass implements LoxCallable {
    final String name;
    private final Map<String, LoxFunction> methods;

    LoxClass(String name, Map<String, LoxFunction> methods) 
    {
        this.name = name;
        this.methods = methods;
    }

    LoxFunction findMethod(String name) 
    {
        if (methods.containsKey(name)) {
            return methods.get(name);
        }

        return null;
    }

    @Override
    public String toString() {
        return name;
    }

    // Literally a wrapper around a name by far.

    // When “call” a class, 
    // it instantiates a new LoxInstance for the called class and returns it. 
    // The arity() method is how the interpreter validates 
    // that passed the right number of arguments to a callable. 
    @Override 
    public Object call(Interpreter interpreter, List<Object> arguments)
    {
        LoxInstance instance = new LoxInstance(this);
        // When a class is called, after the LoxInstance is created, 
        // look for an “init” method. 
        // If find one, immediately bind and invoke it 
        // just like a normal method call. 
        // The argument list is forwarded along.
        LoxFunction initializer = findMethod("ego");
        if (initializer != null) {
            initializer.bind(instance).call(interpreter, arguments);
        }

        return instance;
    }

    @Override 
    public int arity() 
    {
        // That argument list means also need to tweak 
        // how a class declares its arity.
        LoxFunction initializer = findMethod("init");
        if (initializer == null) return 0;
        return initializer.arity();
        // If there is an initializer, 
        // that method’s arity determines 
        // how many arguments must pass when call the class itself. 
        // Don’t require a class to define an initializer, 
        // though, as a convenience. 
        // If don’t have an initializer, the arity is still zero.
    }
}


