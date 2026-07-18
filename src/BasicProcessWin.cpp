#include "BasicProcessWin.h"
#include <misc.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

#include "ProcessHelper.h"
#include "ProcessWinHelper.h"

std::string misc::get_error_message(uint32_t error_code)
{
	wchar_t *buf = nullptr;
	DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
	DWORD len = FormatMessageW(flags, nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
	if (len == 0 || !buf) {
		return "unknown error";
	}
	std::wstring wmsg(buf, len);
	LocalFree(buf);
	// 末尾の改行や空白を取り除く
	while (!wmsg.empty() && (wmsg.back() == L'\r' || wmsg.back() == L'\n' || wmsg.back() == L' ')) {
		wmsg.pop_back();
	}
	return convert_wstr_to_str(wmsg);
}

// 引数配列から、CreateProcessW へ安全に渡せるコマンドライン文字列を構築する。
// 各引数は必要に応じてダブルクォートで囲み、バックスラッシュ・ダブルクォートを適切にエスケープする。
std::string misc::build_command_line(std::vector<std::string> const &args)
{
	std::string cmd;
	for (size_t i = 0; i < args.size(); ++i) {
		if (i > 0) cmd += ' ';
		std::string const &arg = args[i];
		// 空白、タブ、ダブルクォート、バックスラッシュを含まない場合はそのまま
		bool needs_quote = false;
		for (char c : arg) {
			if (c == ' ' || c == '\t' || c == '"' || c == '\\') {
				needs_quote = true;
				break;
			}
		}
		if (!needs_quote) {
			cmd += arg;
			continue;
		}
		cmd += '"';
		size_t backslash_count = 0;
		for (char c : arg) {
			if (c == '\\') {
				backslash_count++;
			} else if (c == '"') {
				// 連続するバックスラッシュを2倍にする
				cmd.append(backslash_count * 2, '\\');
				backslash_count = 0;
				cmd += '\\';
				cmd += '"';
			} else {
				cmd.append(backslash_count, '\\');
				backslash_count = 0;
				cmd += c;
			}
		}
		// 閉じるダブルクォートの前の連続するバックスラッシュを2倍にする
		cmd.append(backslash_count * 2, '\\');
		cmd += '"';
	}
	return cmd;
}


// BasicProcessWin

struct BasicProcessWin::Private {
	BasicProcessWin::Options options;
	process::helper::dir_string_t change_dir;
	std::shared_ptr<void> user_data;
	std::function<void (bool, std::shared_ptr<void>)> completed_fn;

	struct D {
		AutoHandle hInputWrite;
		AutoHandle hOutputRead;
		AutoProcessInformation pi;
		std::deque<char> output_queue;
		std::vector<char> output_vector;
		// std::string output_bytes;
		bool output_closed = false;
		DWORD exit_code = static_cast<DWORD>(-1);
		_AbstractBasicProcess::ExecResult result;
	} d;
	std::vector<char> output_bytes;
	std::thread output_reader;
	std::mutex output_mutex;
	std::condition_variable output_changed;
	DWORD last_exit_code = static_cast<DWORD>(-1);
	PROCESS_INFORMATION &pi()
	{
		return *&d.pi;
	}
};

BasicProcessWin::BasicProcessWin(BasicProcessWin::Options const &options)
	: m(new Private)
{
	set_options(options);
}

BasicProcessWin::~BasicProcessWin()
{
	close_input();
	wait();
	delete m;
}

void BasicProcessWin::set_change_dir(const process::helper::dir_string_t &dir)
{
	m->change_dir = dir;
}

void BasicProcessWin::set_options(Options const &options)
{
	m->options = options;
}

void BasicProcessWin::set_completion_callback(const std::function<void (bool, std::shared_ptr<void>)> &fn, std::shared_ptr<void> user_data)
{
	m->completed_fn = fn;
	m->user_data = user_data;
}

void BasicProcessWin::notify_completed()
{
	if (m->completed_fn) {
		m->completed_fn(true, m->user_data);
	}
}

bool BasicProcessWin::start(const std::string &cmd)
{
	wait();

	if (cmd.empty()) return false;
	if (IS_VALID_HANDLE(m->pi().hProcess) || IS_VALID_HANDLE(m->pi().hThread)) {
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(m->output_mutex);
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
		DWORD error_code = GetLastError();
		m->d = {};
		m->d.result.error_code = error_code;
		m->d.result.error_message = misc::get_error_message(error_code);
		return false;
	}
	if (!CreatePipe(&m->d.hOutputRead, &hOutputWrite, &sa, 0)) {
		DWORD error_code = GetLastError();
		m->d = {};
		m->d.result.error_code = error_code;
		m->d.result.error_message = misc::get_error_message(error_code);
		return false;
	}
	if (!SetHandleInformation(m->d.hInputWrite, HANDLE_FLAG_INHERIT, 0)
		|| !SetHandleInformation(m->d.hOutputRead, HANDLE_FLAG_INHERIT, 0)) {
		DWORD error_code = GetLastError();
		m->d = {};
		m->d.result.error_code = error_code;
		m->d.result.error_message = misc::get_error_message(error_code);
		return false;
	}

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = hInputRead;
	si.hStdOutput = hOutputWrite;
	si.hStdError = hOutputWrite;

	std::wstring wcmd = convert_str_to_wstr(cmd);
	BOOL ok = CreateProcessW(
				  nullptr, wcmd.data(),
				  nullptr, nullptr,
				  TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
				  nullptr, nullptr,
				  &si, &m->d.pi
				  );

	hInputRead.close();
	hOutputWrite.close();

	if (!ok) {
		DWORD error_code = GetLastError();
		m->d = {};
		m->d.result.error_code = error_code;
		m->d.result.error_message = misc::get_error_message(error_code);
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
				if (m->options.output_vector) {
					m->d.output_vector.insert(m->d.output_vector.end(), buf, buf + n);
				}
				if (m->options.output_queue) {
					m->d.output_queue.insert(m->d.output_queue.end(), buf, buf + n);
				}
			}
			m->output_changed.notify_all();
			if (m->options.output_stdout) {
				DWORD written = 0;
				WriteFile(hStdOutput, buf, n, &written, nullptr);
			}
		}
		{
			std::lock_guard<std::mutex> lock(m->output_mutex);
			m->d.output_closed = true;
		}
		m->output_changed.notify_all();

		this->notify_completed();
	});

	return true;
}

_AbstractBasicProcess::ExecResult BasicProcessWin::wait()
{
	close_input();

	m->d.result.started = IS_VALID_HANDLE(m->pi().hProcess);
	if (m->d.result.started) {
		WaitForSingleObject(m->pi().hProcess, INFINITE);
		DWORD ec = static_cast<DWORD>(-1);
		if (GetExitCodeProcess(m->pi().hProcess, &ec)) {
			m->d.result.exit_code = ec;
		}
	}
	m->d.pi.close();

	if (m->output_reader.joinable()) {
		m->output_reader.join();
	}
	if (IS_VALID_HANDLE(m->d.hOutputRead)) {
		m->d.hOutputRead.close();
	}

	m->output_bytes = std::move(m->d.output_vector);

	auto ret = std::move(m->d.result);
	m->last_exit_code = ret.exit_code;
	m->d = {};

	return ret;
}

bool BasicProcessWin::wait_for_output(const std::string &text)
{
	// プロンプトが複数回のReadFileに分割されても、連結済みoutput_から検索できる。
	// ワーカーが先に終了した場合はoutput_closed_で待機を解除する。
	std::unique_lock<std::mutex> lock(m->output_mutex);
	std::string s;
	m->output_changed.wait(lock, [&]{
		s = std::string(m->d.output_vector.begin(), m->d.output_vector.end());
		return s.find(text) != std::string::npos || m->d.output_closed;
	});
	return s.find(text) != std::string::npos;
}

void BasicProcessWin::close_input()
{
	if (IS_VALID_HANDLE(m->d.hInputWrite)) {
		m->d.hInputWrite.close();
	}
}

int BasicProcessWin::write_input(const char *ptr, int n)
{
	if (!ptr || n <= 0) {
		return 0;
	}
	if (IS_VALID_HANDLE(m->d.hInputWrite)) {
		if (write_all(m->d.hInputWrite, ptr, static_cast<size_t>(n))) {
			return n;
		}
		return 0;
	}
	return -1;
}

int BasicProcessWin::read_output(char *ptr, int n)
{
	if (!ptr || n <= 0) {
		return 0;
	}
	std::lock_guard<std::mutex> lock(m->output_mutex);
	if (!m->d.output_queue.empty()) {
		int count = std::min(n, (int)m->d.output_queue.size());
		for (int i = 0; i < count; i++) {
			ptr[i] = m->d.output_queue.front();
			m->d.output_queue.pop_front();
		}
		return static_cast<int>(count);
	}
	return 0;
}

bool BasicProcessWin::is_running() const
{
	return IS_VALID_HANDLE(m->pi().hProcess) || IS_VALID_HANDLE(m->pi().hThread);
}

std::vector<char> const &BasicProcessWin::stdout_bytes() const
{
	return m->output_bytes;
}

int BasicProcessWin::get_exit_code() const
{
	return static_cast<int>(m->last_exit_code);
}

