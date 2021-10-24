package com.craftinginterpreters.lox;

class Return extends RuntimeException 
{
    // This class wraps the return value 
    // with the accoutrements Java requires for a runtime exception class. 
    // The super constructor call with those nulls and falses 
    // disables some JVM machinery that don’t need. 
    // Since using the exception class for control flow and 
    // not actual error handling, 
    // don’t need overhead like stack traces.
    
    final Object value;
    
    Return(Object value) 
    {
      super(null, null, false, false);
      this.value = value;
    }
}

