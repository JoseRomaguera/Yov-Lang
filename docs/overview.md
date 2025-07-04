# Overview

## Introduction

This section is not intended to teach general programming knowledge.

Yov is an interpreted language. It is not object-oriented and shares syntactic similarities with Odin and Jai, which are compiled systems programming languages.

To install the project, go to the [releases page](https://github.com/JoseRomaguera/Yov-Lang/releases) and download the latest version.
To build it yourself, download the project and run:
```
yov misc/build.yov
```

This uses a Yov script to compile the Yov language itself.
                                                  
## Hello World
                
Let's start with the classic hello world.

Create a file called "HelloWorld.yov":

```
println("Hello World!");
```

Then execute it with:
```
yov HelloWorld.yov
```

## Comments

Single and multi-line comments are supported. Multi-line comments can be nested:
```
// Single-line comment
/* Multi-line comment */
```

## Object Definition

Declaration:
```
value: Int = 10;
```

Type Inference:
```
value := "Hello"; // Implicitly inferred as String
```

Default Initialization: All values are zero-initialized unless explicitly set in struct members.
```
value: Foo;
```

Assignments:
```
value = 5;
value += 10;
```

Constants:
```
value0: Int : 10;
value1 :: 10;
```

## String Literals

Strings are a set of characters encoded as UTF8.
```
str := "Hello World";
```

String Interpolation
```
foo := 5;
println("foo = {foo}");
println("foo+5 = {foo + 5}");
```

Special Characters:
```
\n -> Next line
\r -> Carriage return
\t -> Tab
\\ -> Literal \
\{ -> Literal {
\} -> Literal }
\" -> Literal "
```

## Codepoints

Codepoints are integers encoding UTF32 characters.

```
codepoint: Int = 'A';
```

Extract from string: (WIP)
```
cursor := 0;
codepoint := str_get_cursor("Hello", &cursor);
```

Special Codepoints
```
\\ -> Literal \
\n -> Next line
\r -> Carriage return
\t -> Tab
\' -> Literal '
```

## Control Flow Statements

<b>if / else</b>:
```
if (x == 2) { ... }
else { ... }
```

<b>while loop</b>:
```
i := 0;
while (i < 10) {
    ...
    i += 1;
}
```
<b>for loop</b>:
```
for (i := 0; i < 10; i += 1) { ... }
```
<b>for each loop</b>:
```
values := { "A", "B", "C" };
for (value: values) { ... }
for (value, index: values) { ... }
```

<b>defer</b>: Executes code at the end of the current scope.
```
defer { println("End scope"); }
```

## References

By default, assignments create copies (even for structs).

To store a reference:
```
value0: Foo;
value1 := value0; // Copy
value2 := &value0; // Reference
value2: Foo = null; // Null Reference
```

## Arrays

Declaration:
```
value0 : Int[];
value1 : Int[][];
```

Initialization:
```
value0 := { 1, 2, 3 };
value1 := { { 1, 2, 3 }, { 1, 2, 3 } };
```

Empty Array Initialization:
```
value0 : Int[] = [10];
value1 := [10]->Int;

value2 : Int[][] = [2, 5];
value3 := [2, 5]->Int;
```

Manipulation:
```
words := { "This", "is" } + { "a" };
words += { "dynamic", "array" };
words += "!";
```

Access:
```
value := { 1, 2, 3 };
println("First = {value[0]}");
println("Count = {value.count}");
```

## Function Definition

```
empty_function :: () {}
params_function :: (param0: Int, param1: Bool) {}
return_function :: () -> String { return "Hello"; }
```

References as parameters:
```
function_name :: (param: String&) {
    param = "Hello";
}

value: String;
function_name(&value);
```

## Enums

```
Foo :: enum { A, B = 10, C }
```

If the expression expects an enum, type name can be omitted:
```
foo: Foo = .C;
```

Access:
```
foo: Foo = .B;
foo.index; // 1
foo.value; // 10
foo.name;  // "B"
Foo.name   // "Foo"
Foo.count  // 3
Foo.array  // { Foo.A, Foo.B, Foo.C }
```

## Structs

```
Foo :: struct {
    member0: Int;
    member1: String = "Hello";
}

foo: Foo;
```

Expression to create a default object:
```
foo := Foo();
```

## Script Arguments

Execute any script with this format:
```
yov [language flags] script_to_execute [script arguments]
```

Script arguments are defined in the script itself:
```
arg_identifier :: arg {}

println("{arg_identifier}"); // Argument definitions creates a constant.
```
Command:
```
yov script.yov -arg_identifier
```

Parameters:
```
arg_with_params :: arg -> String // Bool by default
{
    name = "-arg";                    // Name used by the argument, default would be "-arg_with_params"
    required = true;                  // Not required by default
    description = "arg description";  // Used to describe the argument
    default = "";
}
```
Command:
```
yov script.yov -arg=Value
```

## Script Help

Display help information for any Yov script by running:
```
yov script.yov -help
```

It will display the script description followed by a list of all arguments.

The script description is defined with a constant object called "script_description":
```
script_description :: "This is the description of the script!";
```

Argument descriptions are specified in his "description" parameter. (See Script Arguments)

## Imports

Import definitions from other scripts.
```
import "script.yov";
```

Only functions, enums, structs and global constants are imported.

## Language Flags

Specified in the -help command

```
yov -help
```

## Error Management TODO
## Environment Variables TODO