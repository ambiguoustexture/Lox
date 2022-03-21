package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

class Environment 
{
    final Environment enclosing;

    // The no-argument constructor is for the global scope’s environment, which ends the chain.
    Environment() 
    {
        enclosing = null;
    }

    // The other constructor creates a new local scope nested inside the given outer one.
    Environment(Environment enclosing) {
        this.enclosing = enclosing;
    }

    private final Map<String, Object> values = new HashMap<>();

    Object get(Token name) 
    {
        if (values.containsKey(name.lexeme)) {
            return values.get(name.lexeme);
        }

        // If the variable isn’t found in this environment, 
        // we simply try the enclosing one. 
        // That in turn does the same thing recursively, 
        // so this will ultimately walk the entire chain. 
        // If we reach an environment with no enclosing one and still don’t find the variable, 
        // then we give up and report an error as before.
        if (enclosing != null) return enclosing.get(name);
        
        throw new RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");
    }

    void assign(Token name, Object value)
    {
        if (values.containsKey(name.lexeme)) {
            values.put(name.lexeme, value);
            return ;
        }

        if (enclosing != null) {
            enclosing.assign(name, value);
            return ;
        }

        throw new RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");
    }

    void define(String name, Object value) 
    {
        values.put(name, value);
    }

    Environment ancestor(int distance) 
    {
        // Walks a fixed number of hops up the parent chain and returns the environment there. 
        // Once have that, getAt() simply returns the value of the variable 
        // in that environment’s map. 
        // It doesn’t even have to check to see if the variable is there -— 
        // onw know it will be because the resolver already found it before.
        Environment environment = this;
        for (int i = 0; i < distance; i++) {
            environment = environment.enclosing;
        }

        return environment;
    }

    Object getAt(int distance, String name) 
    {
        // If did get a distance, have a local variable, 
        // and get to take advantage of the results of our static analysis. 
        return ancestor(distance).values.get(name);
    } 

    // 
    void assignAt(int distance, Token name, Object value) 
    {
        // As getAt() is to get(), assignAt() is to assign(). 
        // It walks a fixed number of environments, 
        // and then stuffs the new value in that map.
        ancestor(distance).values.put(name.lexeme, value);
    }
}

