#include <stdio.h>
#include <optional>
#include <string>
#include <string_view>
#include <cstring>

#ifdef _MSC_VER

#include <windows.h>

/**
 * @brief Execute a command and return its output (Windows implementation)
 * @param cmd Command string to execute
 * @return std::optional<std::string> Command output if successful, std::nullopt if failed
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
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	// Create pipe
	if (!CreatePipe(&hRead, &hWrite, &saAttr, 0)) {
		fprintf(stderr, "CreatePipe failed.\n");
		return std::nullopt;
	}

	// Disable read handle inheritance (used only by parent process)
	SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

	// Initialize STARTUPINFO
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdOutput = hWrite;
	siStartInfo.hStdError  = hWrite;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	// Initialize PROCESS_INFORMATION
	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

	// Create child process
	char *cmd2 = strdup(cmd);
	if (!CreateProcessA(
			NULL,
			cmd2,            // Command line
			NULL,
			NULL,
			TRUE,           // Handle inheritance
			0,
			NULL,
			NULL,
			&siStartInfo,
			&piProcInfo)) {
		fprintf(stderr, "CreateProcess failed.\n");
		CloseHandle(hWrite);
		CloseHandle(hRead);
		return std::nullopt;
	}
	free(cmd2);

	// Write side is not needed in parent process
	CloseHandle(hWrite);

	std::string ret;

	// Read and display output
	while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &nbytes, NULL) && nbytes > 0) {
		std::string_view v(buffer, nbytes);
		ret.append(v);
	}

	// Cleanup
	CloseHandle(hRead);
	WaitForSingleObject(piProcInfo.hProcess, INFINITE);
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
 * @brief Execute a command using execvp and return its output
 * @param argv Vector of command arguments
 * @return std::optional<std::string> Command output if successful, std::nullopt if failed
 */
static std::optional<std::string> _command(std::vector<char *> const &argv)
{
	int pipefd[2];
	pid_t pid;
	char buffer[1024];
	ssize_t nbytes;

	std::string ret;

	if (pipe(pipefd) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}

	pid = fork();
	if (pid == -1) {
		perror("fork");
		exit(EXIT_FAILURE);
	}
	if (pid == 0) {
		puts(argv[0]);
		// Child process
		close(pipefd[0]); // Don't use read side
		dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
		close(pipefd[1]); // Close original write side

		// Execute external command
		execvp(argv[0], argv.data());

		// If execlp fails
		perror("execlp");
		exit(EXIT_FAILURE);
	} else {
		// Parent process
		close(pipefd[1]); // Don't use write side

		// Read from pipe and display
		while ((nbytes = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
			std::string_view v(buffer, nbytes);
			ret.append(v);
		}

		close(pipefd[0]); // Close read side
		wait(NULL); // Wait for child process to terminate
	}

	return ret;
}

/**
 * @brief Parse command string and execute it (Unix implementation)
 * @param cmd Command string to execute
 * @return std::optional<std::string> Command output if successful, std::nullopt if failed
 */
std::optional<std::string> command(char const *cmd)
{
	std::vector<std::string> vec;
	char const *begin = cmd;
	char const *end = begin + strlen(begin);
	char const *ptr = begin;
	char const *left = ptr;
	char quote = 0;
	while (1) {
		int c = 0;
		if (ptr < end) {
			c = (unsigned char)*ptr;
		}
		if (c == 0 || (quote == 0 && isspace(c))) {
			if (left < ptr) {
				if (left + 1 < ptr && *left == '"' && ptr[-1] == '"') {
					left++;
					ptr--;
				}
				vec.emplace_back(left, ptr - left);
			}
			if (c == 0) break;
			ptr++;
			left = ptr;
		} else if (c == '"') {
			quote = c;
			ptr++;
		} else if (c == quote) {
			quote = 0;
			ptr++;
		} else {
			ptr++;
		}
	}
	if (vec.size() > 0) {
		std::vector<char *> argv;
		for (std::string &v : vec) {
			argv.push_back(v.data());
		}
		argv.push_back(nullptr);
		return _command(argv);
	}
	return std::nullopt;
}

#endif

