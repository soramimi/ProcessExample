#include "WinProcess.h"

#include "misc.h"

namespace {

inline bool IS_VALID_HANDLE(HANDLE h)
{
	return h && h != INVALID_HANDLE_VALUE;
}

inline bool IS_VALID_HANDLE(PROCESS_INFORMATION const &h)
{
	return IS_VALID_HANDLE(h.hProcess) || IS_VALID_HANDLE(h.hThread);
}

template <typename HANDLE> class AbstractAutoHandle {
private:
	HANDLE h_ = {};
protected:
	virtual void close_handle(HANDLE &h) = 0;
public:
	AbstractAutoHandle() = default;
	AbstractAutoHandle(HANDLE h)
		: h_(h)
	{
	}
	virtual ~AbstractAutoHandle()
	{
		close();
	}
	HANDLE detach()
	{
		HANDLE h = h_;
		h_ = {};
		return h;
	}
	void close()
	{
		HANDLE h = detach();
		if (IS_VALID_HANDLE(h)) {
			close_handle(h);
		}
	}
	HANDLE *operator -> ()
	{
		return &h_;
	}
	HANDLE *operator & ()
	{
		close();
		return &h_;
	}
	HANDLE &operator = (HANDLE h)
	{
		if (h_ == h) return h_;
		close();
		h_ = h;
		return h_;
	}
	operator HANDLE & ()
	{
		return h_;
	}
};

class AutoHandle : public AbstractAutoHandle<HANDLE> {
protected:
	void close_handle(HANDLE &h)
	{
		CloseHandle(h);
		h = nullptr;
	}
};

class AutoProcessInformation : public AbstractAutoHandle<PROCESS_INFORMATION> {
private:
	PROCESS_INFORMATION pi = {};
protected:
	void close_handle(PROCESS_INFORMATION &pi)
	{
		if (IS_VALID_HANDLE(pi.hProcess)) {
			CloseHandle(pi.hProcess);
		}
		if (IS_VALID_HANDLE(pi.hThread)) {
			CloseHandle(pi.hThread);
		}
		pi = {};
	}
};

class AutoPseudoConsole {
private:
	HPCON hPC = nullptr;
public:
	AutoPseudoConsole() = default;
	~AutoPseudoConsole()
	{
		close();
	}
	void close()
	{
		if (IS_VALID_HANDLE(hPC)) {
			ClosePseudoConsole(hPC);
			hPC = nullptr;
		}
	}
	operator HPCON & ()
	{
		return hPC;
	}
	HPCON *operator & ()
	{
		return &hPC;
	}
};

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

// WinProcess

struct WinProcess::Private {
	struct D {
		AutoHandle hInputWrite;
		AutoHandle hOutputRead;
		AutoProcessInformation pi;
		std::string output;
		bool output_closed = false;
	} d;
	std::thread output_reader;
	std::mutex output_mutex;
	std::condition_variable output_changed;
	PROCESS_INFORMATION &pi()
	{
		return *&d.pi;
	}
};

WinProcess::WinProcess()
	: m(new Private)
{
}

WinProcess::~WinProcess()
{
	close_input();
	wait();
	delete m;
}

bool WinProcess::exec(const std::string &cmd)
{
	if (IS_VALID_HANDLE(m->pi().hProcess) || IS_VALID_HANDLE(m->pi().hThread)) {
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(m->output_mutex);
		m->d.output.clear();
		m->d.output_closed = false;
	}

	// 子へ渡す端だけを継承可能にし、親が保持する端は継承させない。
	// 不要な継承端が残ると、反対側でEOFを検出できなくなる。
	// HANDLE _hInputRead = nullptr;
	AutoHandle hInputRead;
	AutoHandle hOutputWrite;
	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	if (!CreatePipe(&hInputRead, &m->d.hInputWrite, &sa, 0)) {
		m->d = {};
		return false;
	}
	if (!CreatePipe(&m->d.hOutputRead, &hOutputWrite, &sa, 0)) {
		m->d = {};
		return false;
	}
	SetHandleInformation(m->d.hInputWrite, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(m->d.hOutputRead, HANDLE_FLAG_INHERIT, 0);

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
				  &si, &m->d.pi
				  );

	hInputRead.close();
	hOutputWrite.close();

	if (!ok) {
		m->d = {};
		return false;
	}

	HANDLE hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	// ワーカー出力を実行中から排出する。蓄積した文字列はプロンプト検出にも使い、
	// 同じデータを監督プロセスのstdoutへ逐次中継する。
	m->output_reader = std::thread([this, hStdOutput]{
		char buf[256];
		DWORD n;
		while (ReadFile(m->d.hOutputRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
			{
				std::lock_guard<std::mutex> lock(m->output_mutex);
				m->d.output.append(buf, n);
			}
			m->output_changed.notify_all();
			DWORD written = 0;
			WriteFile(hStdOutput, buf, n, &written, nullptr);
		}
		{
			std::lock_guard<std::mutex> lock(m->output_mutex);
			m->d.output_closed = true;
		}
		m->output_changed.notify_all();
	});

	return true;
}

bool WinProcess::wait()
{
	close_input();

	bool started = IS_VALID_HANDLE(m->pi().hProcess);
	if (started) {
		WaitForSingleObject(m->pi().hProcess, INFINITE);
	}
	m->d.pi = {};

	if (m->output_reader.joinable()) {
		m->output_reader.join();
	}
	if (IS_VALID_HANDLE(m->d.hOutputRead)) {
		m->d.hOutputRead.close();
	}
	m->d = {};
	return started;
}

bool WinProcess::wait_for_output(const std::string &text)
{
	// プロンプトが複数回のReadFileに分割されても、連結済みoutput_から検索できる。
	// ワーカーが先に終了した場合はoutput_closed_で待機を解除する。
	std::unique_lock<std::mutex> lock(m->output_mutex);
	m->output_changed.wait(lock, [&]{
		return m->d.output.find(text) != std::string::npos || m->d.output_closed;
	});
	return m->d.output.find(text) != std::string::npos;
}

void WinProcess::close_input()
{
	if (IS_VALID_HANDLE(m->d.hInputWrite)) {
		m->d.hInputWrite.close();
	}
}

bool WinProcess::write_input(const char *ptr, size_t n)
{
	if (IS_VALID_HANDLE(m->d.hInputWrite)) {
		if (write_all(m->d.hInputWrite, ptr, n)) {
			return true;
		}
	}
	return false;
}

// WinConPTY

struct WinConPTY::Private {
	struct D {
		BOOL running = FALSE;
		AutoProcessInformation pi;
		AutoPseudoConsole hPC;
		AutoHandle hPipeInRead;
		AutoHandle hPipeInWrite;
		AutoHandle hPipeOutRead;
		AutoHandle hPipeOutWrite;
		WinConPTY::ExecResult result;
	} d;
	std::atomic<bool> stop_input{false};
	std::thread input_writer;
	std::thread output_reader;
};

WinConPTY::WinConPTY()
	: m(new Private)
{
}

WinConPTY::~WinConPTY()
{
	wait();
	delete m;
}

bool WinConPTY::exec(const std::string &cmd)
{
	m->d = {};

	// ConPTYから見た入力用と出力用の2本の匿名パイプを用意する。
	if (!CreatePipe(&m->d.hPipeInRead, &m->d.hPipeInWrite, nullptr, 0)) {
		m->d.result.error_code = GetLastError();
		return false;
	}
	if (!CreatePipe(&m->d.hPipeOutRead, &m->d.hPipeOutWrite, nullptr, 0)) {
		m->d = {};
		m->d.result.error_code = GetLastError();
		return false;
	}

	COORD size = {80, 25};
	HRESULT hr = CreatePseudoConsole(size, m->d.hPipeInRead, m->d.hPipeOutWrite, 0, &m->d.hPC);
	// ConPTY に接続する子プロセスを作成するまで、パイプ端は保持する。
	if (FAILED(hr)) {
		m->d = {};
		m->d.result.error_code = static_cast<DWORD>(hr);
		return false;
	}

	SIZE_T attrSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);

	STARTUPINFOEXW siEx = {};
	siEx.StartupInfo.cb = sizeof(siEx);
	siEx.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
	if (!siEx.lpAttributeList
		|| !InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &attrSize)
		|| !UpdateProcThreadAttribute(siEx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m->d.hPC, sizeof(m->d.hPC), nullptr, nullptr)) {
		m->d.result.error_code = siEx.lpAttributeList ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
		if (siEx.lpAttributeList) HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
		m->d = {};
		return false;
	}

	std::wstring wcmd = misc::convert_str_to_wstr(cmd);

	m->d.running = CreateProcessW(nullptr,
							  wcmd.data(),
							  nullptr,
							  nullptr,
							  FALSE,
							  CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
							  nullptr,
							  nullptr,
							  &siEx.StartupInfo,
							  &m->d.pi);
	if (!m->d.running) {
		m->d.result.error_code = GetLastError();
	}

	DeleteProcThreadAttributeList(siEx.lpAttributeList);
	HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);

	// CreateProcessW が完了するまで、CreatePseudoConsole に渡したパイプ端を保持する。

	m->d.hPipeInRead.close();
	m->d.hPipeOutWrite.close();

	if (!m->d.running) {
		m->d = {};
		return false;
	}
	m->d.result.started = true;

	HANDLE hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	// ワーカーstdinは監督プロセスが作った匿名パイプである。
	// PeekNamedPipeでデータの有無を確認してから読むことでReadFileの永久待機を避け、
	// Git終了後にstop_input_でこのスレッドを確実に停止できるようにする。
	m->stop_input = false;
	m->input_writer = std::thread([this, hStdInput]{
		if (IS_VALID_HANDLE(m->d.hPipeInWrite) && IS_VALID_HANDLE(hStdInput)) {
			char buf[256];
			while (!m->stop_input) {
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
				if (!write_all(m->d.hPipeInWrite, buf, n)) {
					break;
				}
			}
		}
	});

	// ConPTYの出力はプロセスの実行中から継続して排出する必要がある。
	// VtStripperはスレッド内に値で保持し、ReadFile間で解析状態を維持する。
	m->output_reader = std::thread([this, hStdOutput, vt_stripper = VtStripper{}]() mutable {
		char buf[256];
		DWORD n;
		while (ReadFile(m->d.hPipeOutRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
			std::string text = vt_stripper.append({buf, n});
			WriteFile(hStdOutput, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
		}
	});

	ResumeThread(m->d.pi->hThread);

	return true;
}

WinConPTY::ExecResult WinConPTY::wait()
{
	if (IS_VALID_HANDLE(m->d.pi->hProcess) || IS_VALID_HANDLE(m->d.pi->hThread)) {

		// Git終了後にClosePseudoConsoleすることでhPipeOutReadがEOFになる。
		WaitForSingleObject(m->d.pi->hProcess, INFINITE);
		GetExitCodeProcess(m->d.pi->hProcess, &m->d.result.exit_code);
		m->d.pi.close();

		// 入力転送スレッドを先に止めてから、ConPTY入力の書き込み端を閉じる。
		// Git実行中にこの端を閉じると、ConPTYが入力終了として扱う場合がある。
		m->stop_input = true;
		if (m->input_writer.joinable()) {
			m->input_writer.join();
		}
		close_input();

		// ClosePseudoConsole が生成する最終出力も、読み取りスレッドで受け取る。
		m->d.hPC.close();

		if (m->output_reader.joinable()) {
			m->output_reader.join();
		}
		m->d.hPipeOutRead.close();
	}

	auto ret = std::move(m->d.result);
	m->d = {};
	return ret;
}

void WinConPTY::close_input()
{
	if (m->d.hPipeInWrite) {
		m->d.hPipeInWrite.close();
	}
}

bool WinConPTY::write_input(const char *ptr, size_t n)
{
	if (m->d.hPipeInWrite != nullptr) {
		if (write_all(m->d.hPipeInWrite, ptr, static_cast<DWORD>(n))) {
			return true;
		}
	}
	return false;
}

bool WinConPTY::is_conpty_available()
{
	HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!hKernel32) return false;
	return GetProcAddress(hKernel32, "CreatePseudoConsole") != nullptr;
}
