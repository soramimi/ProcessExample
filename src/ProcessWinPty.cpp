#include "ProcessWinPty.h"
#include <windows.h>
#include <winpty.h>
#include "ProcessHelper.h"
#include "ProcessWinHelper.h"

namespace {

class OutputReaderThread2 {
	friend class ::ProcessWinPty;
private:
	std::thread thread_;
	std::atomic<bool> interrupted_{false};
	HANDLE handle_ = INVALID_HANDLE_VALUE;
	std::deque<char> *output_queue_ = nullptr;
	std::vector<char> *output_vector_ = nullptr;
public:
	~OutputReaderThread2()
	{
		wait();
	}
	void start(HANDLE hOutput, std::deque<char> *outq, std::vector<char> *outv)
	{
		handle_ = hOutput;
		output_queue_ = outq;
		output_vector_ = outv;
		thread_ = std::thread([this]() {
			char buf[1024];
			while (1) {

				if (interrupted_) break;
				DWORD amount = 0;
				BOOL ret = ReadFile(handle_, buf, sizeof(buf), &amount, nullptr);
				if (!ret || amount == 0) {
					break;
				}
				output_queue_->insert(output_queue_->end(), buf, buf + amount);
				output_vector_->insert(output_vector_->end(), buf, buf + amount);
			}
		});
	}
	void wait()
	{
		if (thread_.joinable()) {
			thread_.join();
		}
	}
	void closeHandle()
	{
		if (handle_ != INVALID_HANDLE_VALUE) {
			CloseHandle(handle_);
			handle_ = INVALID_HANDLE_VALUE;
		}
	}
	void terminate()
	{
		interrupted_ = true;
		wait();
	}
	void interrupt()
	{
		interrupted_ = true;
		closeHandle();
	}
};

} // namespace


// ProcessWinPty

struct ProcessWinPty::Private {
	std::atomic<bool> interrupted{false};
	std::thread thread;
	std::mutex mutex;
	std::condition_variable cv;
	std::string command;
	std::string env;
	OutputReaderThread2 th_output_reader;
	AutoHandle hProcess;
	AutoHandle hOutput;
	AutoHandle hInput;
	DWORD exit_code = 0;
};

ProcessWinPty::ProcessWinPty()
	: m(new Private)
{
}

ProcessWinPty::~ProcessWinPty()
{
	delete m;
}

bool ProcessWinPty::is_running() const
{
	return m->thread.joinable();
}

void ProcessWinPty::run()
{
	std::wstring program;
	wchar_t const *program_p = nullptr;
	if (1) {
		// コマンドから実行ファイル名を抜き取る。実際に実行されるプログラムのパス。
		program = convert_str_to_wstr(getProgram(m->command));
		if (!program.empty()) {
			program_p = program.c_str();
		}
	} else {
		// nop:
		// program_p が nullptr 空の時、PATHが通っているコマンドなら実行できる。
	}

	process::helper::PushDir chdir(change_dir_);

	winpty_config_t *agent_cfg = winpty_config_new(WINPTY_FLAG_PLAIN_OUTPUT, nullptr);
	if (!agent_cfg) {
		m->exit_code = 127;
		notify_completed();
		return;
	}
	winpty_t *pty = winpty_open(agent_cfg, nullptr);
	winpty_config_free(agent_cfg);
	if (!pty) {
		m->exit_code = 127;
		fprintf(stderr, "Failed to open winpty\n");
		notify_completed();
		return;
	}

	m->hInput = CreateFileW(winpty_conin_name(pty), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	m->hOutput = CreateFileW(winpty_conout_name(pty), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (!IS_VALID_HANDLE(m->hInput) || !IS_VALID_HANDLE(m->hOutput)) {
		m->exit_code = 127;
		winpty_free(pty);
		notify_completed();
		return;
	}
	m->th_output_reader.start(m->hOutput, &output_queue_, &output_vector_);

	std::vector<wchar_t> envbuf;
#ifdef APP_GUITAR
	std::wstring env = convert_str_to_wstr(m->env);
	if (!env.empty()) {
		envbuf.resize(env.size() + 1);
		memcpy(envbuf.data(), env.c_str(), sizeof(wchar_t) * (env.size() + 1));
	}
#endif

	std::wstring wcmd = convert_str_to_wstr(m->command);

	winpty_spawn_config_t *spawn_cfg = winpty_spawn_config_new(WINPTY_SPAWN_FLAG_AUTO_SHUTDOWN,
															   program_p,
															   (wchar_t const *)wcmd.data(),
															   change_dir_.empty() ? nullptr : change_dir_.c_str(),
															   envbuf.empty() ? nullptr : envbuf.data(),
															   nullptr);
	if (!spawn_cfg) {
		m->exit_code = 127;
		m->th_output_reader.interrupt();
		close_input();
		winpty_free(pty);
		notify_completed();
		return;
	}
	BOOL spawnSuccess = winpty_spawn(pty, spawn_cfg, &m->hProcess, nullptr, nullptr, nullptr);
	winpty_spawn_config_free(spawn_cfg);
	if (!spawnSuccess) {
		m->exit_code = 127;
		m->th_output_reader.interrupt();
		m->hOutput.close();
	}

	bool ok = false;

	if (spawnSuccess) {
		while (1) {
			if (m->interrupted) break;
			GetExitCodeProcess(m->hProcess, &m->exit_code);
			if (m->exit_code == STILL_ACTIVE) {
				std::unique_lock<std::mutex> lock(m->mutex);
				m->cv.wait_for(lock, std::chrono::milliseconds(1), [this] { return m->interrupted.load(); });
			} else {
				ok = true;
				(void)ok;
				break;
			}
		}
	}

	// プロセスの出力を確実に取得するため、ここで output reader スレッドの終了を待つ
	m->th_output_reader.wait();

	winpty_free(pty);

	close_input();
	m->hOutput.close();
	m->hProcess.close();

	notify_completed();
}

int ProcessWinPty::read_output(char *dstptr, int maxlen)
{
	if (!dstptr || maxlen <= 0) {
		return 0;
	}
	size_t len = output_queue_.size();
	if (len > maxlen) {
		len = maxlen;
	}
	if (len > 0) {
		auto begin = output_queue_.begin();
		std::copy(begin, begin + len, dstptr);
		output_queue_.erase(begin, begin + len);
	}
	return (int)len;
}

void ProcessWinPty::write_input(char const *ptr, int len)
{
	if (!ptr || len <= 0 || !IS_VALID_HANDLE(m->hInput)) {
		return;
	}
	char const *begin = ptr;
	char const *end = begin + len;
	char const *left = begin;
	char const *right = begin;
	while (1) {
		int c = -1;
		if (right < end) {
			c = *right & 0xff;
		}
		if (c == '\r' || c == '\n' || c < 0) {
			if (left < right) {
				write_all(m->hInput, left, static_cast<size_t>(right - left));
			}
			if (c < 0) break;
			right++;
			if (c == '\r') {
				if (*right == '\n') {
					right++;
				}
				c = '\r';
			} else if (c == '\n') {
				c = '\r';
			} else {
				c = -1;
			}
			if (c >= 0) {
				char ch = static_cast<char>(c);
				write_all(m->hInput, &ch, 1);
			}
			left = right;
		} else {
			right++;
		}
	}
}

void ProcessWinPty::start(std::string const &cmdline, std::string const &env, bool use_input)
{
	(void)use_input;
	if (is_running()) return;
	m->command = cmdline;
	m->env = env;
	m->thread = std::thread([&]() {
		run();
	});
}

int ProcessWinPty::wait()
{
	if (m->thread.joinable()) {
		m->thread.join();
		stdout_bytes_ = output_vector_;
		stderr_bytes_ = {};
		return static_cast<int>(m->exit_code);
	}
	return 127;
}

void ProcessWinPty::stop()
{
	// 標準出力読み出しスレッドを強制終了しないとwinptyプロセスが終了してくれない
	m->th_output_reader.terminate();
	// プロセススレッド停止
	m->interrupted = true;
	wait();
}

void ProcessWinPty::close_input()
{
	m->hInput.close();
}

int ProcessWinPty::get_exit_code() const
{
	return m->exit_code;
}


