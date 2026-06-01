# Simple compiler In C (attempt.2)

Simple zero-copy single pass full compiler written in C.
Supported formats: elf-x86_64 and win64-x86_64.

Supports only one data type `int` which is 32-bit signed integer. There are **no** string or char literals for now.

Usage:
```bash
scic2 [-o <out>] [-f win64|elf64] <src>
```

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
```
int var: 5;
call print_int(var); print 10; # prints 5
if 1 then
    int var: 10;
    call print_int(var); print 10; # prints 10
end
call print_int(var); print 10; # prints 5
```

### expressions
Rules of associativity apply here as follows: `*`, `/`, `%`, then `+`, '-', then `<`, `>`, `=`, `!`, then `|`, `&`.
Be aware the `!` means *not equal*.
> math operators: `+` `-` `*` `/` `%`
> comparison operators: `<` `>` `=`, `!`
> logical operator: `|` for `or` and `&` for `and`
```
int a: 1 ! 2; # 1 is not eq 2 -> TRUE(1)
int b: 1 < 2 & 2 < 1; # 1 is less than 2 AND 2 is less than 1 -> FALSE, second condition does not hold

int val: 5;
int val2: (val * 3) / (val + 1); # = 2 (integer division)
```

### comments
Comment starts with `#` and ends with the end of line at which `#` occurred.
```
call print_int(123); # this is comment
# and also this
```

### print
> print <expr> <expr> ...;
```
print 10; # prints new line (\n in asci 10)
print 97 98; # prints 'ab'
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

### imports
Include copies over the contents and compiles it before the contents of importer. 
When the path is relative, it is resolved from the point of the importer's location. 
> `include "<path>";`
```
include "libs/print_int.cm";
```
