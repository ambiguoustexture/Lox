## jlox

```Java
main() {
  run() {
    scanTokens();       // tokenize: convert source code to token,
                        // omit comments and spaces

    parse();            // precedence & associativity
                        // recursive descent parsing
                        //
                        // convert tokens to various statements:
                        // block, class, expressions, functions, control-flow & etc.

    // add a new class with methods (OOP)or
    // add methods for existed classes (FP)?
    //
    // Visitor Pattern: approximate the functional style within an OOP language

    interprete() {
      evaluate();
      execute();
    }
  }
}
```
