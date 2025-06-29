/**
 * @file command.cpp
 * @brief Cross-platform command execution implementation
 * 
 * This file contains the implementation of command execution functionality
 * for both Windows and Unix-like systems using platform-specific APIs.
 */

#include <stdio.h>
#include <optional>
#include <string>
#include <string_view>
#include <cstring>

#ifdef _MSC_VER

#include <windows.h>

/**
 * @brief Execute a command and return its output (Windows implementation)
 * 
 * This function uses Windows API to create a child process and capture its output
 * through pipes. It handles process creation, pipe management, and output reading.
 * 
 * @param cmd Command string to execute (passed to cmd.exe)
 * @return std::optional<std::string> Command output if successful, std::nullopt if failed
 * 
 * @details The implementation:
 *          1. Creates a pipe for capturing stdout/stderr
 *          2. Sets up STARTUPINFO to redirect output to the pipe
 *          3. Creates the child process using CreateProcess
 *          4. Reads output from the pipe until the process terminates
 *          5. Cleans up handles and waits for process completion
 */
std::optional<std::string> command(char const *cmd)
{
	SECURITY_ATTRIBUTES saAttr;
	HANDLE hRead, hWrite;
	PROCESS_INFORMATION piProcInfo;
	STARTUPINFOA siStartInfo;
	CHAR buffer[4096];
	DWORD nbytes;

	// Set pipe security attributes (inherit to child process)
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;  // Allow child process to inherit handles
	saAttr.lpSecurityDescriptor = NULL;

	// Create pipe for communication between parent and child process
	if (!CreatePipe(&hRead, &hWrite, &saAttr, 0)) {
		fprintf(stderr, "CreatePipe failed.\n");
		return std::nullopt;
	}

	// Disable read handle inheritance (used only by parent process)
	// This prevents the child from inheriting the read end of the pipe
	SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

	// Initialize STARTUPINFO structure
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdOutput = hWrite;  // Redirect stdout to pipe write end
	siStartInfo.hStdError  = hWrite;  // Redirect stderr to pipe write end
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;  // Use the specified handles

	// Initialize PROCESS_INFORMATION structure
	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

	// Create child process
	// Note: CreateProcess may modify the command line, so we need a writable copy
	char *cmd2 = strdup(cmd);
	if (!CreateProcessA(
			NULL,           // Application name (use command line instead)
			cmd2,           // Command line (modifiable copy)
			NULL,           // Process security attributes
			NULL,           // Thread security attributes
			TRUE,           // Handle inheritance flag
			0,              // Creation flags
			NULL,           // Environment block
			NULL,           // Current directory
			&siStartInfo,   // Startup info
			&piProcInfo)) { // Process info
		fprintf(stderr, "CreateProcess failed.\n");
		CloseHandle(hWrite);
		CloseHandle(hRead);
		free(cmd2);
		return std::nullopt;
	}
	free(cmd2);

	// Close write side of pipe in parent process
	// The child process will close it when it terminates
	CloseHandle(hWrite);

	std::string ret;

	// Read output from child process through the pipe
	// Continue reading until no more data is available
	while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &nbytes, NULL) && nbytes > 0) {
		std::string_view v(buffer, nbytes);
		ret.append(v);
	}

	// Cleanup: close handles and wait for child process to complete
	CloseHandle(hRead);
	WaitForSingleObject(piProcInfo.hProcess, INFINITE);  // Wait for process to finish
	CloseHandle(piProcInfo.hProcess);
	CloseHandle(piProcInfo.hThread);

	return ret;
}

#else

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>

/**
 * @brief Execute a command using execvp and return its output (Unix implementation)
 * 
 * This function uses Unix system calls to create a child process and capture its output.
 * It creates a pipe, forks a child process, and uses execvp to run the command.
 * 
 * @param argv Vector of command arguments (null-terminated array of C strings)
 * @return std::optional<std::string> Command output if successful, std::nullopt if failed
 * 
 * @details The implementation:
 *          1. Creates a pipe for inter-process communication
 *          2. Forks a child process
 *          3. In child: redirects stdout to pipe and executes command
 *          4. In parent: reads output from pipe until child terminates
 *          5. Waits for child process completion
 */
static std::optional<std::string> _command(std::vector<char *> const &argv)
{
	int pipefd[2];
	pid_t pid;
	char buffer[1024];
	ssize_t nbytes;

	std::string ret;

	// Create pipe for communication between parent and child
	if (pipe(pipefd) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}

	// Fork a child process
	pid = fork();
	if (pid == -1) {
		perror("fork");
		exit(EXIT_FAILURE);
	}
	
	if (pid == 0) {
		// Child process execution path
		puts(argv[0]);  // Debug: print command being executed
		
		close(pipefd[0]); // Close read end of pipe (not needed in child)
		
		// Redirect stdout to write end of pipe
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]); // Close original write end after duplication

		// Execute the external command
		// execvp will replace the current process image
		execvp(argv[0], argv.data());

		// If execvp returns, it means execution failed
		perror("execvp");
		exit(EXIT_FAILURE);
	} else {
		// Parent process execution path
		close(pipefd[1]); // Close write end of pipe (not needed in parent)

		// Read output from child process through the pipe
		while ((nbytes = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
			std::string_view v(buffer, nbytes);
			ret.append(v);
		}

		close(pipefd[0]); // Close read end of pipe
		wait(NULL); // Wait for child process to terminate
	}

	return ret;
}

/**
 * @brief Parse command string and execute it (Unix implementation)
 * 
 * This function parses a command string into individual arguments, handling
 * quotes and spaces properly, then executes the command using the internal
 * _command function.
 * 
 * @param cmd Command string to execute (space-separated arguments, quotes supported)
 * @return std::optional<std::string> Command output if successful, std::nullopt if failed
 * 
 * @details Parsing rules:
 *          - Arguments are separated by whitespace
 *          - Double quotes can be used to include spaces in arguments
 *          - Quoted arguments have their outer quotes removed
 *          - Empty command strings return std::nullopt
 * 
 * @example Input: 'ls -l "my file.txt"' becomes ["ls", "-l", "my file.txt"]
 */
std::optional<std::string> command(char const *cmd)
{
	std::vector<std::string> vec;
	char const *begin = cmd;
	char const *end = begin + strlen(begin);
	char const *ptr = begin;
	char const *left = ptr;
	char quote = 0;
	
	// Parse command string into individual arguments
	while (1) {
		int c = 0;
		if (ptr < end) {
			c = (unsigned char)*ptr;
		}
		
		// Check for end of string or whitespace (when not in quotes)
		if (c == 0 || (quote == 0 && isspace(c))) {
			if (left < ptr) {
				// Remove outer quotes if present
				if (left + 1 < ptr && *left == '"' && ptr[-1] == '"') {
					left++;
					ptr--;
				}
				vec.emplace_back(left, ptr - left);
			}
			if (c == 0) break;  // End of string
			ptr++;
			left = ptr;  // Start of next argument
		} else if (c == '"') {
			// Start of quoted section
			quote = c;
			ptr++;
		} else if (c == quote) {
			// End of quoted section
			quote = 0;
			ptr++;
		} else {
			ptr++;
		}
	}
	
	// Execute command if we have arguments
	if (vec.size() > 0) {
		std::vector<char *> argv;
		for (std::string &v : vec) {
			argv.push_back(v.data());
		}
		argv.push_back(nullptr);  // Null-terminate the argument list
		return _command(argv);
	}
	return std::nullopt;  // Empty command
}

#endif

