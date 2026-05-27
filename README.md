# Simple compiler In C (attempt.2)

Simple zero-copy single pass full compiler written in C, currently supporting only elf-x86_64 target.

Supports only one data type `int` which is 32-bit signed integer.

## syntax

### entry point
```
int main()
    ...
end
```

### creating variable
> int <name>: <expr>;
```
int a: 5*(1+var_b);
```

### changing value of a variable
> set <name>: <expr>;
```
set a: a * 2 + b;
```

### expressions
Rules of associativity apply here as follows: `*`, `/`, `%`, then `+`, '-', then `<`, `>`, `=`.
> math operators: `+` `-` `*` `/` `%`
> comparison operators: `<` `>` `=`

### print
> print <expr> <expr> ...;
```
print 10; // prints new line (\n in asci 10)
print 97 98; // prints 'ab'
```

### if conditional
> if <expr> then <stmts> <optional else <stmts>> end
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
> while <expr> do <stmts> end
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
> call <name>(<expr>, <expr>, ...) <? or into <var_name>>;
```
call print_int(97+a);
int b: 0;
call add(2,3) into b; // writes result into variable 'b'
call greet();
```

### function definition
> <int/void> <name>(<arg_name>, <arg_name>, ...) <stmts> end
```
void greet()
    print 97 98 99 10;
    return; // in case of void function return statements are optional
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
> include "<path>";
```
include "libs/print_int.cm";
```
