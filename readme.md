# Project Overview

This repository contains a simple text editor and assembly examples for learning low-level programming concepts.

## Contents

### 1. **mini_nano** (Terminal Text Editor)
- **Location**: `assembly/mini_nano.c`
- **Description**: A minimal, POSIX-compliant terminal text editor written in C. It mimics the basic functionality of `nano`.
- **Features**:
  - Open and Save files.
  - Basic cursor navigation (Arrow keys).
  - Insert and Delete characters.
- **Controls**:
  - `Ctrl-S`: Save
  - `Ctrl-O`: Open
  - `Ctrl-X`: Exit
- **Building**:
  ```bash
  gcc assembly/mini_nano.c -o mini_nano
  ```

### 2. **Assembly "Hello World"**
- **Location**: `assembly/hello.asm`
- **Description**: A simple "Hello World" program written in NASM assembly for Windows x64.
- **Highlights**:
  - Demonstrates Windows x64 calling conventions (shadow space allocation).
  - Uses `puts` from the C standard library.

## Getting Started

To explore the editor, navigate to the `assembly` directory and compile `mini_nano.c`. Note that `mini_nano` relies on `<termios.h>`, so it requires a POSIX environment (like Linux, WSL, or Cygwin) to build and run correctly.