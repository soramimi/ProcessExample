# ProcessExample

A cross-platform C++ utility for executing system commands and capturing their output.

## Overview

ProcessExample provides a simple and safe way to execute system commands and retrieve their standard output across different platforms. The implementation uses platform-specific APIs to ensure optimal performance and reliability on both Windows and Unix-like systems.

## Features

- **Cross-platform compatibility**: Supports Windows and Unix/Linux systems
- **Safe error handling**: Uses `std::optional` for robust error management
- **Output capture**: Captures and returns command standard output as a string
- **Platform-optimized**: Uses native APIs for best performance
  - Windows: Win32 API with `CreateProcess` and pipes
  - Unix/Linux: POSIX APIs with `fork`, `execvp`, and pipes
- **Command parsing**: Intelligent parsing of command strings with quote support (Unix)

## Build Requirements

- C++17 compatible compiler
- Standard C++ library with `<optional>` support

### Supported Compilers
- GCC 7.0 or later
- Clang 5.0 or later
- Microsoft Visual C++ 2017 or later

## Building

### Using Make (Linux/Unix)

```bash
make
```

To run the example:
```bash
make run
```

To clean build files:
```bash
make clean
```

### Using Qt Creator

1. Open `ProcessExample.pro` in Qt Creator
2. Build the project (Ctrl+B)
3. Run the project (Ctrl+R)

## Usage

### Basic Example

```cpp
#include "command.h"
#include <stdio.h>

int main() {
    // Execute a command and capture output
    auto result = command("ls -l");
    
    if (result) {
        printf("Command output:\n%s\n", result->c_str());
    } else {
        printf("Failed to execute command.\n");
    }
    
    return 0;
}
```

### Platform-specific Examples

#### Windows
```cpp
auto result = command("dir C:\\");
auto result2 = command("cmd.exe /c echo Hello World");
```

#### Unix/Linux
```cpp
auto result = command("ls -la /home");
auto result2 = command("ps aux");
auto result3 = command("grep -r \"pattern\" /path/to/search");
```

#### Command with Quotes (Unix)
```cpp
auto result = command("find /home -name \"*.txt\"");
auto result2 = command("echo \"Hello World with spaces\"");
```

## API Reference

### Functions

#### `command(const char* cmd)`

Executes a system command and returns its output.

**Parameters:**
- `cmd`: Null-terminated command string to execute

**Returns:**
- `std::optional<std::string>`: Command output if successful, `std::nullopt` if failed

**Platform Behavior:**
- **Windows**: Command is passed directly to `CreateProcess`
- **Unix/Linux**: Command string is parsed to handle quotes and spaces, then executed via `execvp`

## Implementation Details

### Windows Implementation
- Uses `CreateProcess` API for process creation
- Creates anonymous pipes for stdout/stderr redirection
- Handles process synchronization and cleanup
- Supports full Windows command-line syntax

### Unix/Linux Implementation
- Uses `fork()` to create child process
- Uses `execvp()` to replace child process image
- Creates pipes for inter-process communication
- Includes command-line parsing with quote support
- Handles process waiting and cleanup

### Error Handling
- Returns `std::nullopt` on any failure
- Platform-specific error messages printed to stderr
- Safe resource cleanup in all code paths

## File Structure

```
ProcessExample/
├── command.h          # Header file with function declarations
├── command.cpp        # Implementation file
├── main.cpp          # Example usage
├── Makefile          # Build configuration for Make
├── ProcessExample.pro # Qt Creator project file
└── README.md         # This file
```

## Examples in the Repository

The `main.cpp` file demonstrates basic usage:

- **Windows**: Executes `cmd.exe /c dir` to list directory contents
- **Unix/Linux**: Executes `ls -l` to list directory contents with details

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## License

This project is provided as-is for educational and demonstration purposes.

## Platform Notes

### Windows
- Requires Windows API headers
- Tested on Windows 10 and later
- Uses Unicode-aware APIs where possible

### Unix/Linux
- Requires POSIX-compliant system
- Tested on Ubuntu, CentOS, and macOS
- Uses standard POSIX system calls

## Troubleshooting

### Common Issues

1. **Compilation errors about `std::optional`**
   - Ensure you're using C++17 or later
   - Add `-std=c++17` to compiler flags

2. **Command not found errors**
   - Ensure the command exists in system PATH
   - Use absolute paths for executables when necessary

3. **Permission denied errors**
   - Check file permissions for executed commands
   - Ensure proper execution rights

### Debug Output

The Unix implementation includes debug output that prints the command being executed. This can be removed or modified in the `_command` function if needed.

## Performance Considerations

- Commands are executed synchronously
- Large output is buffered in memory
- Consider timeout mechanisms for long-running commands
- Child processes are properly cleaned up to prevent zombies

## Security Considerations

- Input validation should be performed by the caller
- Be cautious with user-provided command strings
- Consider using allowlists for permitted commands in production
- Avoid executing commands with elevated privileges when possible
