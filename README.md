# Yov Programming Language

Yov is an interpreted programming language designed for batch scripting.
It offers the robustness of a general-purpose language with the simplicity of a lightweight executable for running scripts.<br>
The syntax is inspired by Jai and Odin.

```yov
say_hello_world := ask_yesno("Say hello world?");

if (say_hello_world) {
    println("Hello world!");
}
else {
    println("Understood :(");
    exit(); // The program exits😃
}

root := "example";
/* The operator '/' appends two paths */
absolute_root := context.cd / root;
println("Creating files into {absolute_root}");

create_folder(root, recursive=false);
set_cd(root);

files := { "foo", "test", "test2" };
exists : Bool[];
for (file : files) exists += file_exists(file);

for (file, index : files) {
    println("File[{index}] = \"{file}\"\n\tExists = {exists[index]}");
    create_file(file);
}

if (os.kind == .Windows) call("whatever");
else if (os.kind == .Linux) call("whatever2");
else println("OS is not supported😃");

```

## Features

- Robust programming language.
- Interpreted with Syntax Checking: Performs static analysis before execution.
- Strictly Typed.
- Lightweight: The only dependency is a ~60KB executable per platform.
- Cross-Platform:
    - Write once, run on Windows, Linux, and macOS with no changes.
    - Provides platform-independent intrinsics for common operations.
    - Easily detect the OS at runtime and make manual system calls if needed.
- Out-of-order function and struct definitions.
- Garbage Collection.
- Array Programming.
- Usefull debugging and error management features.

## Why using Yov?

- Far more robust than any other batch scripting language.
- Enhanced Reliability for Interpreted Languages: Since batch scripts tend to be shorter than full programs, 
Yov takes advantage of this by analyzing the entire codebase before execution without relying on caches.
- More expressive and readable than compiled systems languages.
- Minimal Dependencies: The only requirement is a lightweight ~60KB executable.
- Portable: The Yov executable can be easily included in any repository, ensuring that scripts run seamlessly across all machines, anytime.
- Safety Measures: Includes helpful safeguards to prevent accidental file mishandling during script creation, making development more secure.

## Documentation

TODO

- Basics
    - Context
- Intrinsics
- Interpreter arguments
    - Debugging

## What will not be in Yov

- No multithreading.
- No package managers.
- No operator overloading.
- No OOP features. (classes, inheritance, interfaces, etc)
- No constructors or destructors.
- No exceptions.

## Current State

TODO