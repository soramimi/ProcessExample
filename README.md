# ProcessExample

A C++ sample project that demonstrates various techniques for spawning a child process and capturing its output. The program runs `git --version` as a concrete example and prints the result to stdout.

## Motivation

Different platforms and use cases call for different IPC approaches. This project compares them side-by-side so you can understand the trade-offs:

| Function | Platform | Method |
|---|---|---|
| `exec_git_posix()` | Linux / macOS | `pipe()` + `fork()` + `execvp()` |
| `exec_git_posixpty()` | Linux / macOS | `forkpty()` — pseudo-terminal |
| `exec_git_win()` | Windows | `CreatePipe()` + `CreateProcessW()` |
| `exec_git_winpty()` | Windows | [winpty](https://github.com/rprichard/winpty) pseudo-terminal |
| `exec_git_conpty()` | Windows 10+ | `CreatePseudoConsole()` (ConPTY) |

## Why pseudo-terminals?

Plain pipes only capture stdout/stderr. Some programs (including Git in certain configurations) detect that their output is not a terminal and change their behavior — e.g., disabling color or progress output. Using a pseudo-terminal (PTY) makes the child process behave as if it is writing to a real terminal.

### VT / ANSI escape sequence stripping

When a PTY is used, the child process may emit VT100/ANSI escape sequences (color codes, cursor movement) or OSC (Operating System Command) sequences such as terminal title updates:

```
ESC ] 0 ; <title string> BEL
```

The helper function `strip_vt()` removes both CSI sequences (`ESC [`) and OSC sequences (`ESC ]`) from the output so that plain text is returned to the caller.

## Dependencies

### winpty (Windows)

Pre-built binaries and headers are bundled under `winpty/`:

```
winpty/
  include/   winpty.h, winpty_constants.h
  x64/lib/   winpty.lib
  x64/bin/   winpty.dll, winpty-agent.exe
  ia32/      (32-bit equivalents)
```

Source: https://github.com/rprichard/winpty

### ConPTY (Windows 10 version 1809+)

`CreatePseudoConsole` is part of `kernel32.dll` on Windows 10 (build 17763) and later. The function `is_conpty_available()` checks for its presence at runtime so the binary can fall back gracefully on older systems.

### Requirements

- C++11 or later
- Windows: MSVC or MinGW, Windows SDK with ConPTY support (`EXTENDED_STARTUPINFO_PRESENT`)
- Linux / macOS: `libutil` (for `forkpty`)

## Project Structure

```
main.cpp            — all implementations
main.h              — (reserved)
process-example.pro — qmake project file
winpty/             — bundled winpty library
_bin/               — build output
```
