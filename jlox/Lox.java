package com.craftinginterpreters.lox;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class Lox 
{
    private static final Interpreter interpreter = new Interpreter();
    static boolean hadError = false;
    static boolean hadRuntimeError = false;
    
    public static void main(String[] args) throws IOException 
    {
           if (args.length > 1) {
            System.out.println("Usage: jlox [script]");
            System.exit(64);
        } else if (args.length == 1) {
            runFile(args[0]);
        } else {
            runPrompt();
        }
    }

    // Start jlox from the command line and 
    // give it a path to a file, it reads the file and executes it.
    private static void runFile(String path) throws IOException
    {
        byte[] bytes = Files.readAllBytes(Paths.get(path));
        run(new String(bytes, Charset.defaultCharset()));

        // Indicate an error in the exit code.
        if (hadError) System.exit(65);
        if (hadRuntimeError) System.exit(70);
    }

    // Fire up jlox without any arguments, 
    // and it drops you into a prompt 
    // where you can enter and execute code one line at a time.
    private static void runPrompt() throws IOException 
    {
        BufferedReader reader = 
            new BufferedReader(new InputStreamReader(System.in));

        for (;;) {
            System.out.print("> ");
            String line = reader.readLine();
            if (line == null) break;
            run(line);

            // Need to reset this flag in the interactive loop. 
            // If the user makes a mistake, 
            // it shouldn’t kill their entire session.
            hadError = false;
        }
    }

    // Both the prompt and the file runner are thin wrappers 
    // around this core function below:
    private static void run(String source) 
    {
        Scanner scanner = new Scanner(source);
        List<Token> tokens = scanner.scanTokens();
        Parser parser = new Parser(tokens);
        List<Stmt> statements = parser.parse();

        // Stop if there was a syntax error.
        if (hadError) return ;

        // Don’t run the resolver if there are any parse errors. 
        // If the code has a syntax error, it’s never going to run, 
        // so there’s little value in resolving it. 
        // If the syntax is clean, tell the resolver to do its thing. 
        // The resolver has a reference to the interpreter and 
        // pokes the resolution data directly into it 
        // as it walks over variables. 
        // When the interpreter runs next, it has everything it needs.
        Resolver resolver = new Resolver(interpreter);

        // Stop if there was a resolution error.
        if (hadError) return;
        resolver.resolve(statements);

        interpreter.interpret(statements);
    }

    static void error(int line, String message) 
    {
        report(line, "", message);
    }

    private static void report(int line, String where, String message) 
    {
        System.err.println(
            "[line " + line + "] Error" + where + ": " + message);
        hadError = true;
    }

    static void error(Token token, String message) 
    {
        if (token.type == TokenType.EOF) {
            report(token.line, " at end", message);
        } else {
            report(token.line, "at '" + token.lexeme + "'", message);
        }
    }

    static void runtimeError(RuntimeError error) {
        System.err.println(error.getMessage() + "\n[line " + error.token.line + "]");
        hadRuntimeError = true;
    }
}
