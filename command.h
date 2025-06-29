#pragma once

#include <optional>
#include <string>

/**
 * @file command.h
 * @brief Cross-platform command execution utility
 * 
 * This header provides a function to execute system commands and capture their output
 * in a cross-platform manner, supporting both Windows and Unix-like systems.
 */

/**
 * @brief Execute a system command and return its output
 * 
 * This function executes the given command string and captures its standard output.
 * The implementation varies based on the target platform:
 * - Windows: Uses CreateProcess with pipes
 * - Unix/Linux: Uses fork/execvp with pipes
 * 
 * @param cmd The command string to execute (null-terminated C string)
 * @return std::optional<std::string> The command output if successful, std::nullopt if failed
 * 
 * @note The command string is parsed differently on each platform:
 *       - Windows: Passed directly to CreateProcess
 *       - Unix: Parsed to handle quotes and spaces properly
 * 
 * @example
 * ```cpp
 * auto result = command("ls -l");
 * if (result) {
 *     printf("Output: %s\n", result->c_str());
 * } else {
 *     printf("Command failed\n");
 * }
 * ```
 */
std::optional<std::string> command(char const *cmd);