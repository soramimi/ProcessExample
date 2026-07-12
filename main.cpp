
#ifndef _WIN32
#error This code is for Windows only.
#endif

#include <windows.h>
#include <assert.h>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include "base64.h"

static inline bool IS_VALID_HANDLE(HANDLE h)
{
	return h && h != INVALID_HANDLE_VALUE;
}

namespace misc {

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

std::string convert_wstr_to_str(std::wstring const &wstr)
{
	std::string str;
	if (wstr.empty()) return str;
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	if (len > 0) {
		str.resize(len);
		WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &str[0], len, nullptr, nullptr);
	}
	return str;
}

std::string find_windows_openssh()
{
	wchar_t system_dir[MAX_PATH];
	UINT len = GetSystemDirectoryW(system_dir, ARRAYSIZE(system_dir));
	if (len == 0 || len >= ARRAYSIZE(system_dir)) return {};

	std::wstring path(system_dir, len);
	path += L"\\OpenSSH\\ssh.exe";
	DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};

	std::string result = convert_wstr_to_str(path);
	for (char &c : result) {
		if (c == '\\') c = '/';
	}
	return result;
}

class VtStripper {
public:
	std::string append(std::string_view input)
	{
		std::string output;
		for (unsigned char c : input) {
			switch (state_) {
			case State::Text:
				if (c == 0x1b) {
					state_ = State::Escape;
				} else {
					output.push_back(static_cast<char>(c));
				}
				break;

			case State::Escape:
				if (c == '[') {
					state_ = State::Csi;
				} else if (c == ']') {
					state_ = State::Osc;
				} else if (c == 'P' || c == 'X' || c == '^' || c == '_') {
					state_ = State::String;
				} else if (c >= 0x20 && c <= 0x2f) {
					state_ = State::EscapeIntermediate;
				} else if (c == 0x1b) {
					state_ = State::Escape;
				} else {
					state_ = State::Text;
				}
				break;

			case State::EscapeIntermediate:
				if (c >= 0x30 && c <= 0x7e) {
					state_ = State::Text;
				} else if (c == 0x1b) {
					state_ = State::Escape;
				}
				break;

			case State::Csi:
				if (c >= 0x40 && c <= 0x7e) {
					state_ = State::Text;
				} else if (c == 0x1b) {
					state_ = State::Escape;
				}
				break;

			case State::Osc:
				if (c == 0x07) {
					state_ = State::Text;
				} else if (c == 0x1b) {
					state_ = State::OscEscape;
				}
				break;

			case State::OscEscape:
				if (c == '\\') {
					state_ = State::Text;
				} else if (c != 0x1b) {
					state_ = State::Osc;
				}
				break;

			case State::String:
				if (c == 0x1b) {
					state_ = State::StringEscape;
				}
				break;

			case State::StringEscape:
				if (c == '\\') {
					state_ = State::Text;
				} else if (c != 0x1b) {
					state_ = State::String;
				}
				break;
			}
		}
		return output;
	}

private:
	enum class State {
		Text,
		Escape,
		EscapeIntermediate,
		Csi,
		Osc,
		OscEscape,
		String,
		StringEscape,
	};

	State state_ = State::Text;
};

}

//

bool write_all(HANDLE handle, char const *data, size_t size)
{
	while (size > 0) {
		DWORD written = 0;
		DWORD chunk = size > MAXDWORD ? MAXDWORD : static_cast<DWORD>(size);
		if (!WriteFile(handle, data, chunk, &written, nullptr) || written == 0) {
			return false;
		}
		data += written;
		size -= written;
	}
	return true;
}

//

class WinProcess {
private:
	bool use_input_ = false;
	HANDLE hWritePipe_ = nullptr;
public:
	bool exec(std::string const &cmd)
	{
		HANDLE hReadPipe = nullptr;
		SECURITY_ATTRIBUTES sa = {};
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		if (!CreatePipe(&hReadPipe, &hWritePipe_, &sa, 0)) {
			return false;
		}
		SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = hWritePipe_;
		si.hStdError = hWritePipe_;

		PROCESS_INFORMATION pi = {};
		std::wstring wcmd = misc::convert_str_to_wstr(cmd);
		BOOL ok = CreateProcessW(
					nullptr, wcmd.data(),
					nullptr, nullptr,
					TRUE, 0,
					nullptr, nullptr,
					&si, &pi
					);

		if (!use_input_) {
			close_input();
		}

		if (!ok) {
			CloseHandle(hReadPipe);
			return false;
		}

		HANDLE hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);

		char buf[256];
		DWORD n;
		while (ReadFile(hReadPipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
			WriteFile(hStdOutput, buf, n, &n, nullptr);
		}
		CloseHandle(hReadPipe);

		WaitForSingleObject(pi.hProcess, INFINITE);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		return true;
	}
	void close_input()
	{
		CloseHandle(hWritePipe_);
		hWritePipe_ = nullptr;
	}
	bool write_input(char const *ptr, size_t n)
	{
		if (hWritePipe_ != nullptr) {
			if (write_all(hWritePipe_, ptr, static_cast<DWORD>(n))) {
				return true;
			}
		}
		return false;
	}
};

//

struct ExecResult {
	bool started = false;
	DWORD exit_code = static_cast<DWORD>(-1);
	DWORD error_code = ERROR_SUCCESS;
	// std::string raw_output;
	// std::string text_output;
};

void wait_process_with_input(HANDLE process, HANDLE &conpty_input)
{
	HANDLE parent_input = GetStdHandle(STD_INPUT_HANDLE);
	if (!IS_VALID_HANDLE(parent_input)) {
		WaitForSingleObject(process, INFINITE);
		return;
	}

	DWORD console_mode = 0;
	bool const parent_is_console = GetConsoleMode(parent_input, &console_mode) != FALSE;
	HANDLE handles[] = {process, parent_input};

	for (;;) {
		DWORD wait_result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
		if (wait_result == WAIT_OBJECT_0) {
			return;
		}
		if (wait_result != WAIT_OBJECT_0 + 1) {
			break;
		}

		std::string input;
		if (parent_is_console) {
			INPUT_RECORD records[32];
			DWORD count = 0;
			if (!ReadConsoleInputW(parent_input, records, ARRAYSIZE(records), &count)) {
				break;
			}

			for (DWORD i = 0; i < count; i++) {
				if (records[i].EventType != KEY_EVENT) {
					continue;
				}
				KEY_EVENT_RECORD const &key = records[i].Event.KeyEvent;
				if (!key.bKeyDown || key.uChar.UnicodeChar == 0) {
					continue;
				}

				char utf8[4];
				int length = WideCharToMultiByte(CP_UTF8, 0, &key.uChar.UnicodeChar, 1, utf8, sizeof(utf8), nullptr, nullptr);
				for (WORD repeat = 0; repeat < key.wRepeatCount && length > 0; repeat++) {
					input.append(utf8, length);
				}
			}
		} else {
			char buf[256];
			DWORD count = 0;
			if (!ReadFile(parent_input, buf, sizeof(buf), &count, nullptr) || count == 0) {
				break;
			}
			input.assign(buf, count);
		}

		if (!input.empty() && !write_all(conpty_input, input.data(), input.size())) {
			break;
		}
	}

	// 親側の入力がEOFでも、ConPTYの入力パイプは子プロセス終了まで保持する。
	// ここで閉じると、入力を必要としないコマンドまで途中で終了してしまう。
	WaitForSingleObject(process, INFINITE);
}

class WinConPTY {
private:
	ExecResult result_;
	PROCESS_INFORMATION pi_ = {};
	BOOL running_ = FALSE;
	HPCON hPC_ = nullptr;
	HANDLE hPipeInRead_ = nullptr;
	HANDLE hPipeInWrite_ = nullptr;
	HANDLE hPipeOutRead_ = nullptr;
	HANDLE hPipeOutWrite_ = nullptr;
	std::thread input_writer_;
	std::thread output_reader_;
public:
	WinConPTY()
	{
	}
	~WinConPTY()
	{
		wait();
	}

	bool exec(std::string const &cmd)
	{
		misc::VtStripper vt_stripper;

		if (!CreatePipe(&hPipeInRead_, &hPipeInWrite_, nullptr, 0)) {
			result_.error_code = GetLastError();
			return false;
		}
		if (!CreatePipe(&hPipeOutRead_, &hPipeOutWrite_, nullptr, 0)) {
			result_.error_code = GetLastError();
			CloseHandle(hPipeInRead_);
			CloseHandle(hPipeInWrite_);
			return false;
		}

		COORD size = {80, 25};
		HRESULT hr = CreatePseudoConsole(size, hPipeInRead_, hPipeOutWrite_, 0, &hPC_);
		// ConPTY に接続する子プロセスを作成するまで、パイプ端は保持する。
		if (FAILED(hr)) {
			result_.error_code = static_cast<DWORD>(hr);
			CloseHandle(hPipeInRead_);
			CloseHandle(hPipeInWrite_);
			CloseHandle(hPipeOutRead_);
			CloseHandle(hPipeOutWrite_);
			return false;
		}

		SIZE_T attrSize = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);

		STARTUPINFOEXW siEx = {};
		siEx.StartupInfo.cb = sizeof(siEx);
		siEx.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
		if (!siEx.lpAttributeList
				|| !InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &attrSize)
				|| !UpdateProcThreadAttribute(siEx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC_, sizeof(hPC_), nullptr, nullptr)) {
			result_.error_code = siEx.lpAttributeList ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
			if (siEx.lpAttributeList) HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
			CloseHandle(hPipeInRead_);
			CloseHandle(hPipeInWrite_);
			CloseHandle(hPipeOutRead_);
			CloseHandle(hPipeOutWrite_);
			ClosePseudoConsole(hPC_);
			return false;
		}

		std::wstring wcmd = misc::convert_str_to_wstr(cmd);

		running_ = CreateProcessW(nullptr,
								 wcmd.data(),
								 nullptr,
								 nullptr,
								 FALSE,
								 CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
								 nullptr,
								 nullptr,
								 &siEx.StartupInfo,
								 &pi_);
		if (!running_) {
			result_.error_code = GetLastError();
		}

		DeleteProcThreadAttributeList(siEx.lpAttributeList);
		HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);

		// CreateProcessW が完了するまで、CreatePseudoConsole に渡したパイプ端を保持する。

		CloseHandle(hPipeInRead_);
		CloseHandle(hPipeOutWrite_);

		if (!running_) {
			CloseHandle(hPipeInWrite_);
			hPipeInWrite_ = nullptr;
			CloseHandle(hPipeOutRead_);
			ClosePseudoConsole(hPC_);
			return false;
		}
		result_.started = true;

		HANDLE hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		HANDLE hStdInput = GetStdHandle(STD_INPUT_HANDLE);

		input_writer_ = std::thread([&]{
			if (IS_VALID_HANDLE(hPipeInWrite_) && IS_VALID_HANDLE(hStdInput)) {
				char buf[256];
				DWORD n;
				while (ReadFile(hStdInput, buf, sizeof(buf), &n, nullptr) && n > 0) {
					if (!write_all(hPipeInWrite_, buf, n)) {
						break;
					}
				}
			}
		});

		// ConPTY の出力はプロセスの実行中から継続して排出する必要がある。
		output_reader_ = std::thread([&]{
			char buf[256];
			DWORD n;
			while (ReadFile(hPipeOutRead_, buf, sizeof(buf), &n, nullptr) && n > 0) {
				std::string text = vt_stripper.append({buf, n});
				WriteFile(hStdOutput, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
			}
		});

		ResumeThread(pi_.hThread);

		// debug input
		{
			if (IS_VALID_HANDLE(hPipeInWrite_)) {
				Sleep(1000);
				std::string send = "no\n";
				write_input(send.c_str(), send.size());
			}
		}

		return true;
	}
	ExecResult wait()
	{
		if (IS_VALID_HANDLE(pi_.hProcess) || IS_VALID_HANDLE(pi_.hThread)) {

			// プロセス終了後に ClosePseudoConsole することで hPipeOutRead が EOF になる
			wait_process_with_input(pi_.hProcess, hPipeInWrite_);
			GetExitCodeProcess(pi_.hProcess, &result_.exit_code);
			CloseHandle(pi_.hProcess);
			CloseHandle(pi_.hThread);

			// 入力しない場合でも、ここまでは書き込み端を保持する。
			// 先に閉じると ConPTY が入力チャネルの終了として扱い、子プロセスも終了する。
			if (input_writer_.joinable()) {
				input_writer_.join();
			}
			close_input();

			// ClosePseudoConsole が生成する最終出力も、読み取りスレッドで受け取る。
			ClosePseudoConsole(hPC_);

			if (output_reader_.joinable()) {
				output_reader_.join();
			}
			CloseHandle(hPipeOutRead_);
		}

		running_ = FALSE;
		pi_ = {};
		hPC_ = nullptr;
		hPipeInRead_ = nullptr;
		hPipeInWrite_ = nullptr;
		hPipeOutRead_ = nullptr;
		hPipeOutWrite_ = nullptr;
		return result_;
	}
	void close_input()
	{
		if (hPipeInWrite_) {
			CloseHandle(hPipeInWrite_);
			hPipeInWrite_ = nullptr;
		}
	}
	bool write_input(char const *ptr, size_t n)
	{
		if (hPipeInWrite_ != nullptr) {
			if (write_all(hPipeInWrite_, ptr, static_cast<DWORD>(n))) {
				return true;
			}
		}
		return false;
	}
};

bool is_conpty_available()
{
	HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!hKernel32) return false;
	return GetProcAddress(hKernel32, "CreatePseudoConsole") != nullptr;
}

constexpr std::string_view subprocess_tag = "--conpty-subprocess--";

int main(int argc, char **argv)
{
#if 0
	{
		std::string cmd = "git --version";
		{
			WinConPTY conpty;
			conpty.exec(cmd);
			conpty.close_input();
			ExecResult result = conpty.wait();
		}
		return 0;
	}
#endif

	if (!is_conpty_available()) {
		fprintf(stderr, "ConPTY is not available on this system.\n");
		return 1;
	}

	if (argc == 3) {
		std::string_view arg = argv[1];
		if (arg == subprocess_tag) {
			std::string cmd = base64_decode(argv[2]);
			WinConPTY conpty;
			conpty.exec(cmd);
			ExecResult result = conpty.wait();
			if (!result.started) {
				fprintf(stderr, "Failed to start process (error %lu).\n", result.error_code);
				return 128;
			}
			return 0;
		}
	}

	char tmp[_MAX_PATH];
	memset(tmp, 0, sizeof(tmp));
	GetModuleFileNameA(NULL, tmp, _countof(tmp));
	std::string cmd = "\"" + std::string(tmp) + "\"";

	// std::string gitcmd = "--version";
	std::string gitcmd = "fetch";

	// Git同梱のMSYS版sshは、Gitが標準入出力をパイプ化するとConPTYを
	// 確認入力用TTYとして再取得できないため、Win32 OpenSSHを明示する。
	std::string ssh = misc::find_windows_openssh();
	if (ssh.empty()) {
		fprintf(stderr, "Windows OpenSSH client was not found.\n");
		return 1;
	}
	gitcmd = "git -c core.sshCommand=\"" + ssh + "\" " + gitcmd;

	cmd += ' ' + std::string(subprocess_tag) + ' ' + base64_encode(gitcmd);

	WinProcess proc;
	proc.exec(cmd);

	return 0;
}

