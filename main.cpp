
#ifndef _WIN32
#error This code is for Windows only.
#endif

#include <windows.h>
#include <assert.h>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
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

// ReadFileの境界をまたぐVTシーケンスも除去できるよう、解析状態を保持する。
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
	// 匿名パイプへのWriteFileは要求サイズ未満で成功する場合があるため、全量を書き切る。
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

// 監督プロセスからConPTYワーカーを起動する。
// hInputWrite_ -> ワーカーstdin、ワーカーstdout/stderr -> hOutputRead_ の双方向構成。
class WinProcess {
private:
	HANDLE hInputWrite_ = nullptr;
	HANDLE hOutputRead_ = nullptr;
	PROCESS_INFORMATION pi_ = {};
	std::thread output_reader_;
	std::mutex output_mutex_;
	std::condition_variable output_changed_;
	std::string output_;
	bool output_closed_ = false;
public:
	~WinProcess()
	{
		close_input();
		wait();
	}

	bool exec(std::string const &cmd)
	{
		if (IS_VALID_HANDLE(pi_.hProcess) || IS_VALID_HANDLE(pi_.hThread)) {
			return false;
		}
		{
			std::lock_guard<std::mutex> lock(output_mutex_);
			output_.clear();
			output_closed_ = false;
		}

		// 子へ渡す端だけを継承可能にし、親が保持する端は継承させない。
		// 不要な継承端が残ると、反対側でEOFを検出できなくなる。
		HANDLE hInputRead = nullptr;
		HANDLE hOutputWrite = nullptr;
		SECURITY_ATTRIBUTES sa = {};
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;

		if (!CreatePipe(&hInputRead, &hInputWrite_, &sa, 0)) {
			return false;
		}
		if (!CreatePipe(&hOutputRead_, &hOutputWrite, &sa, 0)) {
			CloseHandle(hInputRead);
			close_input();
			return false;
		}
		SetHandleInformation(hInputWrite_, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(hOutputRead_, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = hInputRead;
		si.hStdOutput = hOutputWrite;
		si.hStdError = hOutputWrite;

		std::wstring wcmd = misc::convert_str_to_wstr(cmd);
		BOOL ok = CreateProcessW(
					nullptr, wcmd.data(),
					nullptr, nullptr,
					TRUE, 0,
					nullptr, nullptr,
					&si, &pi_
					);

		CloseHandle(hInputRead);
		CloseHandle(hOutputWrite);

		if (!ok) {
			close_input();
			CloseHandle(hOutputRead_);
			hOutputRead_ = nullptr;
			pi_ = {};
			return false;
		}

		HANDLE hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		// ワーカー出力を実行中から排出する。蓄積した文字列はプロンプト検出にも使い、
		// 同じデータを監督プロセスのstdoutへ逐次中継する。
		output_reader_ = std::thread([this, hStdOutput]{
			char buf[256];
			DWORD n;
			while (ReadFile(hOutputRead_, buf, sizeof(buf), &n, nullptr) && n > 0) {
				{
					std::lock_guard<std::mutex> lock(output_mutex_);
					output_.append(buf, n);
				}
				output_changed_.notify_all();
				DWORD written = 0;
				WriteFile(hStdOutput, buf, n, &written, nullptr);
			}
			{
				std::lock_guard<std::mutex> lock(output_mutex_);
				output_closed_ = true;
			}
			output_changed_.notify_all();
		});

		return true;
	}

	bool wait()
	{
		close_input();

		bool started = IS_VALID_HANDLE(pi_.hProcess);
		if (IS_VALID_HANDLE(pi_.hProcess)) {
			WaitForSingleObject(pi_.hProcess, INFINITE);
			CloseHandle(pi_.hProcess);
		}
		if (IS_VALID_HANDLE(pi_.hThread)) {
			CloseHandle(pi_.hThread);
		}
		pi_ = {};

		if (output_reader_.joinable()) {
			output_reader_.join();
		}
		if (IS_VALID_HANDLE(hOutputRead_)) {
			CloseHandle(hOutputRead_);
			hOutputRead_ = nullptr;
		}
		return started;
	}

	bool wait_for_output(std::string const &text)
	{
		// プロンプトが複数回のReadFileに分割されても、連結済みoutput_から検索できる。
		// ワーカーが先に終了した場合はoutput_closed_で待機を解除する。
		std::unique_lock<std::mutex> lock(output_mutex_);
		output_changed_.wait(lock, [&]{
			return output_.find(text) != std::string::npos || output_closed_;
		});
		return output_.find(text) != std::string::npos;
	}

	void close_input()
	{
		if (IS_VALID_HANDLE(hInputWrite_)) {
			CloseHandle(hInputWrite_);
			hInputWrite_ = nullptr;
		}
	}

	bool write_input(char const *ptr, size_t n)
	{
		if (IS_VALID_HANDLE(hInputWrite_)) {
			if (write_all(hInputWrite_, ptr, n)) {
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
};

// ワーカープロセス内でConPTYを所有し、標準入出力とConPTYのパイプを中継する。
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
	std::atomic<bool> stop_input_{false};
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
		// ConPTYから見た入力用と出力用の2本の匿名パイプを用意する。
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

		// ワーカーstdinは監督プロセスが作った匿名パイプである。
		// PeekNamedPipeでデータの有無を確認してから読むことでReadFileの永久待機を避け、
		// Git終了後にstop_input_でこのスレッドを確実に停止できるようにする。
		stop_input_ = false;
		input_writer_ = std::thread([this, hStdInput]{
			if (IS_VALID_HANDLE(hPipeInWrite_) && IS_VALID_HANDLE(hStdInput)) {
				char buf[256];
				while (!stop_input_) {
					DWORD available = 0;
					if (!PeekNamedPipe(hStdInput, nullptr, 0, nullptr, &available, nullptr)) {
						break;
					}
					if (available == 0) {
						Sleep(10);
						continue;
					}

					DWORD n = 0;
					DWORD size = available < sizeof(buf) ? available : sizeof(buf);
					if (!ReadFile(hStdInput, buf, size, &n, nullptr) || n == 0) {
						break;
					}
					if (!write_all(hPipeInWrite_, buf, n)) {
						break;
					}
				}
			}
		});

		// ConPTYの出力はプロセスの実行中から継続して排出する必要がある。
		// VtStripperはスレッド内に値で保持し、ReadFile間で解析状態を維持する。
		output_reader_ = std::thread([this, hStdOutput, vt_stripper = misc::VtStripper{}]() mutable {
			char buf[256];
			DWORD n;
			while (ReadFile(hPipeOutRead_, buf, sizeof(buf), &n, nullptr) && n > 0) {
				std::string text = vt_stripper.append({buf, n});
				WriteFile(hStdOutput, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
			}
		});

		ResumeThread(pi_.hThread);

		return true;
	}
	ExecResult wait()
	{
		if (IS_VALID_HANDLE(pi_.hProcess) || IS_VALID_HANDLE(pi_.hThread)) {

			// Git終了後にClosePseudoConsoleすることでhPipeOutReadがEOFになる。
			WaitForSingleObject(pi_.hProcess, INFINITE);
			GetExitCodeProcess(pi_.hProcess, &result_.exit_code);
			CloseHandle(pi_.hProcess);
			CloseHandle(pi_.hThread);

			// 入力転送スレッドを先に止めてから、ConPTY入力の書き込み端を閉じる。
			// Git実行中にこの端を閉じると、ConPTYが入力終了として扱う場合がある。
			stop_input_ = true;
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
			// ワーカーモード: 親から渡されたコマンドを復元し、ConPTY内で実行する。
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

	// 監督モード: 同じ実行ファイルをワーカーとして再起動する。
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

	// 引用符や空白を含むGitコマンドを自己再実行の引数として安全に渡す。
	cmd += ' ' + std::string(subprocess_tag) + ' ' + base64_encode(gitcmd);

	WinProcess proc;
	if (!proc.exec(cmd)) {
		fprintf(stderr, "Failed to start ConPTY worker.\n");
		return 128;
	}

	// SSHが入力待ちになったことを出力から確認してから、親側で決めた回答を送る。
	// 先に送ると、SSHがまだ入力を受け付けておらず回答を失う可能性がある。
	std::string prompt = "Are you sure you want to continue connecting";
	if (proc.wait_for_output(prompt)) {
		std::string send = "no\n";
		if (!proc.write_input(send.data(), send.size())) {
			fprintf(stderr, "Failed to write to ConPTY worker.\n");
		}
	}
	proc.close_input();
	proc.wait();

	return 0;
}

