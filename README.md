# SMALLSH

### Introduction
My own shell written in C which implements a command line interface similar to well-known shells, such as bash. 

The program:
1. Prints an interactive input prompt
2. Parses command line input into semantic tokens
3. Implements parameter expansion
    - Shell special parameters `$$`, `$?`, and `$!`
    - Tilde (~) expansion
4. Implements two shell built-in commands: `exit` and `cd`
5. Executes non-built-in commands using the the appropriate `EXEC(3)` function.
6. Implements redirection operators ‘<’ and ‘>’
7. Implements the ‘&’ operator to run commands in the background
8. Implements custom behavior for `SIGINT` and `SIGTSTP` signals

### Program Functionality
1. Input
2. Word splitting
3. Expansion
4. Parsing
5. Execution
6. Waiting
