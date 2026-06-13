# chek

A zero-copy, single-pass, no-dependency, no-IR native compiler written in C.

Supported targets: elf-x86_64 and win64-x86_64.

Supports only one data type `int` which is 32-bit signed integer. There are **no** string or char literals.

Currently, elf-x86_64 and win64-x86_64 targets both use System V AMD64 ABI internally. 
Since there is no support for FFI and binaries are self-contained, so ABi compatibility with native Win64 is not required.

**Usage**:
```bash
chek [-o <out>] [-O <opt_level>] [-t win64|elf64] <src>
```

**Optimization levels** (opt_level)
- level 0 -> no optimization
- level 1:
  - constants folding
  - algebraic optimization
- level 2 [default]
    - constants folding
    - constant variable resolution
    - constant branch evaluation
    - algebraic optimization
    - function inlining (only for functions that either don't have function calls or call inlinable functions)

## architecture

Frontend
- tokenizer
- recursive descend parser + Shunting Yard algorithm for expressions
- semantic analysis

Optimization
- constant folding
- constant propagation
- branch evaluation
- inlining

Backend
- x86-64 code generation
- ELF64 emitter
- Win64 emitter

## License

This project is licensed under the MIT License - see the `LICENSE` file for details.

## building from source (linux)

### debug build
```bash
# initialize cmake with debug
cmake . -DCMAKE_BUILD_TYPE=Debug

# build the compiler
cmake --build .
```

### release build
```bash
# initialize cmake in release
cmake . -DCMAKE_BUILD_TYPE=Release

# build the compiler
cmake --build .
```

## building from source (for windows)

```bash
mkdir cmake-windows-build
cd cmake-windows-build

cmake -DCMAKE_TOOLCHAIN_FILE=../cross-windows.cmake ..

cmake --build .
```

## syntax

### entry point
```
int main()
    ...
end
```

### creating variable
Variables are scoped in `if` and `while`. 
Variables can be declared only inside functions and are per-function valid. 
Every variable is mutable.
> `int <name>: <expr>;`
```
int a: 5*(1+var_b);
```

### changing value of a variable
> `set <name>: <expr>;`
```
set a: a * 2 + b;
```

### variable shadowing
Variables declared inside `if` or `while` overshadow variables declared in outer scope.
Variables declared inside function overshadow global variables.
```
global int var: 1;
int main()
    call print_int(var); print 10; # prints 1

    int var: 5;
    call print_int(var); print 10; # prints 5
    if 1 then
        int var: 10;
        call print_int(var); print 10; # prints 10
    end
    call print_int(var); print 10; # prints 5
end
```

### expressions
Rules of associativity apply here as follows: `*`, `/`, `%`, then `+`, '-', then `<`, `>`, `=`, `!`, then `&`, and then `|`.
Be aware the `!` means *not equal*.
> math operators: `+` `-` `*` `/` `%`
> comparison operators: `<` `>` `=`, `!`
> logical operator: `|` for `or` and `&` for `and`
```
int a: 1 ! 2;            # 1 is not eq 2 -> TRUE(1)
int b: 1 < 2 & 2 < 1;    # 1 is less than 2 AND 2 is less than 1 -> FALSE, second condition does not hold

int val: 5;
int val2: (val * 3) / (val + 1); # -> 2 (because / is integer division)
```

### comments
Comment starts with `#` and ends with the end of line at which `#` occurred.
```
call print_int(123); # this is comment
# and also this
```

### print
> print <expr or string> <expr or string> ...;
Prints ASCII codes or literal text strings. 
Expressions are evaluated and printed as their corresponding ASCII characters. 
String literals are printed directly as they are.
```
print 10;                # prints new line (\n in asci 10)
print 97 98;             # prints 'ab' (97 -> a, 98 -> b)
print "Hello World!" 64; # prints 'Hello World!@' (64 -> @)
print "Hi!";             # prints 'Hi!'
```

### if conditional
> `if <expr> then <stmts> <optional else <stmts>> end`
```
if a < 6 then
    ...
end

if b > 6 then
    ...
else
    ...
end
```

### while loop
> `while <expr> do <stmts> end`
```
int a: 0;
while a < 5 do
    print 97 32;
    set a: a + 1;
end
print 10;
```

### function call
When using `into` to capture returned value, variable needs to be declared beforehand.
If used without `into` for a function that returns value, warning will be generated.
> `call <name>(<expr>, <expr>, ...) <? or into <var_name>>;`
```
call print_int(97+a);
int b: 0;
call add(2,3) into b; # writes result into variable 'b'
call greet();
```

### function definition
Functions can have at most `6` arguments. More arguments are not supported. 
Void functions do not need return statement at all, but can be used.
> `<int/void> <name>(<arg_name>, <arg_name>, ...) <stmts> end`
```
void greet()
    print 97 98 99 10;
    return; # in case of void function return statements are optional
end

int add(a, b)
    return a + b;
end
```

### function pre-declaration
In some cases, you want to call a function before it is declared and cannot move the function before.
> `declare int add(a, b);`
```
declare int is_even(n);
declare int is_odd(n);

int is_even(n)
    if n = 0 then
        return 1;
    end
    
    int rs: 0;
    call is_odd(n - 1) into rs;
    return rs;
end

int is_odd(n)
    if n = 0 then
        return 0;
    end

    int rs: 0;
    call is_even(n - 1) into rs;
    return rs;
end
```

### recursion
```
int fac(n)
    if n = 1 then
        return 1;
    end
    if n = 0 then
        return 1;
    end
    int val: 0;
    call fac(n-1) into val;
    return n*val;
end
```

### global variables
> `global <type> <name>: <expr>;`
```
global int a: 10;
global int b: a + 5;

int main()
    set a: a - 5;
    return a + b;
end
```

### imports
Include copies over the contents and compiles it before the contents of importer. 
When the path is relative, it is resolved from the point of the importer's location. 
> `include "<path>";`
```
include "libs/print_int.ce";
```

## compilation errors

example:
```text
examples/errors/unknown_var.ce:4:12: error: undefined variable 'b'
      int a: b + 5;
             ^
```