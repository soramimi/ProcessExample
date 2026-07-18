
#include <windows.h>
#include "ProcessWin.h"
#include "ProcessWinHelper.h"

namespace {

class OutputReaderThread {
private:
	HANDLE hRead_;
	std::thread thread_;
	std::mutex *mutex_;
	std::deque<char> *buffer_;
public:
	OutputReaderThread(HANDLE hRead, std::mutex *mutex, std::deque<char> *buffer)
		: hRead_(hRead)
		, mutex_(mutex)
		, buffer_(buffer)
	{
	}
	~OutputReaderThread()
	{
		stop();
	}
	void start()
	{
		thread_ = std::thread([this](){
			char buf[4096];
			while (1) {
				DWORD len = 0;
				if (!ReadFile(hRead_, buf, sizeof(buf), &len, nullptr)) break;
				if (len < 1) break;
				if (buffer_) {
					std::lock_guard lock(*mutex_);
					buffer_->insert(buffer_->end(), buf, buf + len);
				}
			}
		});
	}
	void stop()
	{
		if (thread_.joinable()) {
			thread_.join();
		}
	}
	void wait()
	{
		stop();
	}
};

class ProcessWinThread {
	friend class ProcessWin2;
public:
	std::thread thread_;
	std::mutex *mutex_ = nullptr;
	std::string command_;
	DWORD exit_code_ = -1;
	std::vector<char> input_;
	std::deque<char> outq_;
	std::deque<char> errq_;
	bool use_input_ = false;
	AutoHandle hInputWrite_;
	bool close_input_later_ = false;

	// 環境変数をキャッシュして再利用
	static std::vector<wchar_t> cached_env_;
	static std::mutex env_mutex_;

	void reset()
	{
		mutex_ = nullptr;
		command_.clear();
		exit_code_ = -1;
		input_.clear();
		outq_.clear();
		errq_.clear();
		use_input_ = false;
		hInputWrite_.close();
		close_input_later_ = false;
	}

public:
	ProcessWinThread()
	{
	}
	~ProcessWinThread()
	{
		stop();
	}
	void closeInput()
	{
		hInputWrite_.close();
	}
	void writeInput(char const *ptr, int len)
	{
		if (!ptr || len <= 0 || !mutex_) return;
		std::lock_guard lock(*mutex_);
		input_.insert(input_.end(), ptr, ptr + len);
	}
	void start()
	{
		thread_ = std::thread([this](){
			hInputWrite_.close();

			AutoHandle hOutputRead;
			AutoHandle hOutputWrite;
			AutoHandle hInputRead;
			AutoHandle hInputWrite;
			AutoHandle hErrorRead;
			AutoHandle hErrorWrite;
			AutoProcessInformation pi;

			SECURITY_ATTRIBUTES sa;
			sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			sa.lpSecurityDescriptor = nullptr;
			sa.bInheritHandle = TRUE;

			std::vector<wchar_t> *env_ptr = nullptr;

			std::wstring wcmd(convert_str_to_wstr(command_));
			if (wcmd.empty()) {
				return;
			}

			// パイプ作成の最適化
			static constexpr DWORD PIPE_BUFFER_SIZE = 65536;

			if (!CreatePipe(&hInputRead, &hInputWrite, &sa, PIPE_BUFFER_SIZE))
				return;

			if (!CreatePipe(&hOutputRead, &hOutputWrite, &sa, PIPE_BUFFER_SIZE))
				return;

			if (!CreatePipe(&hErrorRead, &hErrorWrite, &sa, PIPE_BUFFER_SIZE))
				return;

			// ハンドルの継承可能性を最適化
			if (!SetHandleInformation(hInputWrite, HANDLE_FLAG_INHERIT, 0)
				|| !SetHandleInformation(hOutputRead, HANDLE_FLAG_INHERIT, 0)
				|| !SetHandleInformation(hErrorRead, HANDLE_FLAG_INHERIT, 0)) {
				return;
			}

			// プロセス起動
			STARTUPINFOW si;

			ZeroMemory(&si, sizeof(STARTUPINFOW));
			si.cb = sizeof(STARTUPINFOW);
			si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE;
			si.hStdInput = hInputRead;
			si.hStdOutput = hOutputWrite;
			si.hStdError = hErrorWrite;

			{
				std::lock_guard<std::mutex> lock(env_mutex_);
				if (cached_env_.empty()) {
					wchar_t *p = GetEnvironmentStringsW();
					if (p) {
						int i = 0;
						while (p[i] || p[i + 1]) {
							i++;
						}
						cached_env_.assign(p, p + i + 1);
						FreeEnvironmentStringsW(p);

						// LANG=en_US.UTF8を追加
						wchar_t const *e = L"LANG=en_US.UTF8";
						cached_env_.insert(cached_env_.end() - 1, e, e + wcslen(e) + 1);
					}
				}
				env_ptr = cached_env_.empty() ? nullptr : &cached_env_;
			}

			// CreateProcessの最適化フラグ
			DWORD creation_flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;

			if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE, creation_flags, env_ptr ? env_ptr->data() : nullptr, nullptr, &si, &pi)) {
				return;
			}

			// 子プロセス側のハンドルをすぐに閉じる
			hInputRead.close();
			hOutputWrite.close();
			hErrorWrite.close();

			if (!use_input_) {
				closeInput();
			}


			{
				OutputReaderThread t1(hOutputRead, mutex_, &outq_);
				OutputReaderThread t2(hErrorRead, mutex_, &errq_);
				t1.start();
				t2.start();

				HANDLE handles[] = {pi->hProcess};
				while (1) {
					DWORD wait_result = WaitForMultipleObjects(1, handles, FALSE, 10);
					if (wait_result == WAIT_OBJECT_0) break;
					if (wait_result == WAIT_FAILED) break;

					{
						std::lock_guard lock(*mutex_);
						if (!input_.empty()) {
							if (IS_VALID_HANDLE(hInputWrite)) {
								if (!write_all(hInputWrite, input_.data(), input_.size())) {
									closeInput();
								}
								input_.clear();
							}
						} else if (close_input_later_) {
							closeInput();
						}
					}
				}

				t1.wait();
				t2.wait();

				hOutputRead.close();
				hErrorRead.close();

				GetExitCodeProcess(pi->hProcess, &exit_code_);
				pi.close();
			}

			hInputWrite_ = hInputWrite.detach();
		});
	}
	void stop()
	{
		if (thread_.joinable()) {
			thread_.join();
		}
	}
	void wait()
	{
		stop();
	}
};

// 静的メンバーの定義
std::vector<wchar_t> ProcessWinThread::cached_env_;
std::mutex ProcessWinThread::env_mutex_;

std::string toQString(const std::vector<char> &vec)
{
	if (vec.empty()) return {};
	return std::string(&vec[0], vec.size());
}

} // namespace

struct ProcessWin::Private {
	std::mutex mutex;
	ProcessWinThread th;
	std::vector<char> stdout_bytes;
	std::vector<char> stderr_bytes;
	int exit_code = -1;
};

ProcessWin::ProcessWin()
	: m(new Private)
{
}

ProcessWin::~ProcessWin()
{
	delete m;
}

void ProcessWin::start(std::string const &command, bool use_input)
{
	if (is_running()) {
		wait();
	}
	m->exit_code = -1;
	if (command.empty()) {
		return;
	}
	m->th.mutex_ = &m->mutex;
	m->th.use_input_ = use_input;
	m->th.command_ = command;
	m->th.start();
}

int ProcessWin::wait()
{
	m->th.wait();

	m->stdout_bytes.clear();
	m->stderr_bytes.clear();
	m->stdout_bytes.insert(m->stdout_bytes.end(), m->th.outq_.begin(), m->th.outq_.end());
	m->stderr_bytes.insert(m->stderr_bytes.end(), m->th.errq_.begin(), m->th.errq_.end());
	m->exit_code = m->th.exit_code_;
	m->th.reset();
	return m->exit_code;
}

bool ProcessWin::is_running() const
{
	return m->th.thread_.joinable();
}

void ProcessWin::write_input(char const *ptr, int len)
{
	m->th.writeInput(ptr, len);
}

void ProcessWin::close_input()
{
	_close_input(true);
}

void ProcessWin::_close_input(bool justnow)
{
	if (justnow) {
		m->th.closeInput();
	} else {
		m->th.close_input_later_ = true;
	}
}

void ProcessWin::stop()
{
	wait();
}

int ProcessWin::get_exit_code() const
{
	return m->exit_code;
}

const std::vector<char> &ProcessWin::stdout_bytes() const
{
	return m->stdout_bytes;
}

const std::vector<char> &ProcessWin::stderr_bytes() const
{
	return m->stderr_bytes;
}
