
#include <stdio.h>
#include "main.h"
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <winpty.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <pty.h>
#endif

#ifndef _WIN32
std::string exec_git_posix()
{
	std::string ret;

	int pipefd[2];
	if (pipe(pipefd) == -1) {
		return ret;
	}

	pid_t pid = fork();
	if (pid == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		return ret;
	}

	if (pid == 0) {
		// child
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);

		char *args[] = { (char *)"git", (char *)"--version", nullptr };
		execvp("git", args);
		_exit(1);
	}

	// parent
	close(pipefd[1]);

	char buf[256];
	ssize_t n;
	while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
		ret.append(buf, n);
	}
	close(pipefd[0]);

	waitpid(pid, nullptr, 0);

	while (!ret.empty() && (ret.back() == '\n' || ret.back() == '\r')) {
		ret.pop_back();
	}

	return ret;
}

std::string exec_git_posixpty()
{
	std::string ret;

	int master_fd;
	pid_t pid = forkpty(&master_fd, nullptr, nullptr, nullptr);
	if (pid == -1) {
		return ret;
	}

	if (pid == 0) {
		// child
		char *args[] = { (char *)"git", (char *)"--version", nullptr };
		execvp("git", args);
		_exit(1);
	}

	// parent
	char buf[256];
	ssize_t n;
	while ((n = read(master_fd, buf, sizeof(buf))) > 0) {
		ret.append(buf, n);
	}
	close(master_fd);

	waitpid(pid, nullptr, 0);

	while (!ret.empty() && (ret.back() == '\n' || ret.back() == '\r')) {
		ret.pop_back();
	}

	return ret;
}

#else

std::wstring convert_str_to_wstr(std::string const &str)
{
	std::wstring wstr;
	if (str.empty()) return wstr;
	int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
	if (len > 0) {
		wstr.resize(len);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], len);
	}
	return wstr;
}

std::string exec_git_win()
{
	std::string ret;

	HANDLE hReadPipe = nullptr;
	HANDLE hWritePipe = nullptr;
	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
		return ret;
	}
	SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = hWritePipe;
	si.hStdError = hWritePipe;

	PROCESS_INFORMATION pi = {};
	wchar_t cmd[] = L"git --version";
	BOOL ok = CreateProcessW(
		nullptr, cmd,
		nullptr, nullptr,
		TRUE, 0,
		nullptr, nullptr,
		&si, &pi
	);
	CloseHandle(hWritePipe);

	if (!ok) {
		CloseHandle(hReadPipe);
		return ret;
	}

	char buf[256];
	DWORD n;
	while (ReadFile(hReadPipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
		ret.append(buf, n);
	}
	CloseHandle(hReadPipe);

	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	while (!ret.empty() && (ret.back() == '\n' || ret.back() == '\r')) {
		ret.pop_back();
	}

	return ret;
}

std::string exec_git_winpty()
{
	std::string ret;
	winpty_error_ptr_t err = nullptr;

	winpty_config_t *cfg = winpty_config_new(WINPTY_FLAG_PLAIN_OUTPUT, &err);
	if (!cfg) {
		winpty_error_free(err);
		return ret;
	}

	winpty_t *wp = winpty_open(cfg, &err);
	winpty_config_free(cfg);
	if (!wp) {
		winpty_error_free(err);
		return ret;
	}

	winpty_spawn_config_t *scfg = winpty_spawn_config_new(
		WINPTY_SPAWN_FLAG_AUTO_SHUTDOWN,
		nullptr,
		L"git --version",
		nullptr,
		nullptr,
		&err
	);
	if (!scfg) {
		winpty_error_free(err);
		winpty_free(wp);
		return ret;
	}

	HANDLE hProcess = nullptr;
	DWORD createError = 0;
	BOOL ok = winpty_spawn(wp, scfg, &hProcess, nullptr, &createError, &err);
	winpty_spawn_config_free(scfg);
	if (!ok) {
		winpty_error_free(err);
		winpty_free(wp);
		return ret;
	}

	HANDLE hConout = CreateFileW(
		winpty_conout_name(wp),
		GENERIC_READ, 0, nullptr,
		OPEN_EXISTING, 0, nullptr
	);

	if (hConout != INVALID_HANDLE_VALUE) {
		char buf[256];
		DWORD n;
		while (ReadFile(hConout, buf, sizeof(buf), &n, nullptr) && n > 0) {
			ret.append(buf, n);
		}
		CloseHandle(hConout);
	}

	WaitForSingleObject(hProcess, INFINITE);
	CloseHandle(hProcess);
	winpty_free(wp);

	while (!ret.empty() && (ret.back() == '\n' || ret.back() == '\r')) {
		ret.pop_back();
	}
	return ret;
}

static std::string strip_vt(const std::string &s)
{
	std::string out;
	size_t i = 0;
	while (i < s.size()) {
		if ((unsigned char)s[i] == 0x1b) {
			i++;
			if (i < s.size() && s[i] == '[') {
				// CSI sequence: ESC [ {param bytes} {final byte}
				i++;
				while (i < s.size() && (unsigned char)s[i] >= 0x20 && (unsigned char)s[i] <= 0x3f) i++;
				if (i < s.size() && (unsigned char)s[i] >= 0x40 && (unsigned char)s[i] <= 0x7e) i++;
			} else if (i < s.size() && s[i] == ']') {
				// OSC sequence: ESC ] {string} BEL  or  ESC ] {string} ESC '\'
				i++;
				while (i < s.size()) {
					if ((unsigned char)s[i] == 0x07) { i++; break; } // BEL
					if ((unsigned char)s[i] == 0x1b && i + 1 < s.size() && s[i + 1] == '\\') { i += 2; break; } // ST
					i++;
				}
			} else if (i < s.size()) {
				i++; // 2-char ESC sequence
			}
		} else {
			out += s[i++];
		}
	}
	return out;
}

std::string exec_git_conpty()
{
	std::string ret;

	HANDLE hPipeInRead = nullptr, hPipeInWrite = nullptr;
	HANDLE hPipeOutRead = nullptr, hPipeOutWrite = nullptr;

	if (!CreatePipe(&hPipeInRead, &hPipeInWrite, nullptr, 0)) {
		return ret;
	}
	if (!CreatePipe(&hPipeOutRead, &hPipeOutWrite, nullptr, 0)) {
		CloseHandle(hPipeInRead);
		CloseHandle(hPipeInWrite);
		return ret;
	}

	HPCON hPC = nullptr;
	COORD size = {80, 25};
	HRESULT hr = CreatePseudoConsole(size, hPipeInRead, hPipeOutWrite, 0, &hPC);
	// ConPTY が両端を引き継ぐので呼び出し側のコピーを閉じる
	CloseHandle(hPipeInRead);
	CloseHandle(hPipeOutWrite);
	if (FAILED(hr)) {
		CloseHandle(hPipeInWrite);
		CloseHandle(hPipeOutRead);
		return ret;
	}

	SIZE_T attrSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);

	STARTUPINFOEXW siEx = {};
	siEx.StartupInfo.cb = sizeof(siEx);
	siEx.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
	if (!siEx.lpAttributeList
			|| !InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &attrSize)
			|| !UpdateProcThreadAttribute(siEx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(hPC), nullptr, nullptr)) {
		if (siEx.lpAttributeList) HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
		CloseHandle(hPipeInWrite);
		CloseHandle(hPipeOutRead);
		ClosePseudoConsole(hPC);
		return ret;
	}

	wchar_t wcmd[] = L"git --version";
	PROCESS_INFORMATION pi = {};
	BOOL ok = CreateProcessW(nullptr, wcmd, nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &siEx.StartupInfo, &pi);

	DeleteProcThreadAttributeList(siEx.lpAttributeList);
	HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
	CloseHandle(hPipeInWrite);

	if (!ok) {
		CloseHandle(hPipeOutRead);
		ClosePseudoConsole(hPC);
		return ret;
	}

	// プロセス終了後に ClosePseudoConsole することで hPipeOutRead が EOF になる
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	ClosePseudoConsole(hPC);

	char buf[256];
	DWORD n;
	while (ReadFile(hPipeOutRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
		ret.append(buf, n);
	}
	CloseHandle(hPipeOutRead);

	ret = strip_vt(ret);

	while (!ret.empty() && (ret.back() == '\n' || ret.back() == '\r')) {
		ret.pop_back();
	}

	return ret;
}

bool is_conpty_available()
{
	HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!hKernel32) return false;
	return GetProcAddress(hKernel32, "CreatePseudoConsole") != nullptr;
}

#endif

int main(int argc, char **argv)
{
	std::string s = exec_git_conpty();
	printf("%s\n", s.c_str());
	return 0;
}

