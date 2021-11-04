# Compile a debug build of clox.
make -f c.make NAME=cloxd  MODE=debug   SOURCE_DIR=.

# Compile the C interpreter.
make -f c.make NAME=clox   MODE=release SOURCE_DIR=.

# Compile the C interpreter as ANSI standard C++.
make -f c.make NAME=cpplox MODE=debug   CPP=true SOURCE_DIR=.
