#include "ProcessWin.h"
#include "misc.h"
#include <winpty.h>

ProcessWin::ProcessWin()
{
	BasicProcessWin::Options opts;
	opts.output_vector = true;
	proc_.set_options(opts);

}

ProcessWin::~ProcessWin() = default;

void ProcessWin::start(const std::string &command, bool use_input)
{
	std::lock_guard<std::mutex> lock(mutex_);
	started_ = proc_.exec(command);
	running_ = started_;
	if (started_) {
		stdout_bytes_.clear();
		stderr_bytes_.clear();
	}
	exit_code_ = -1;
}

int ProcessWin::wait()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!running_) {
		return exit_code_;
	}
	proc_.wait();
	running_ = false;
	stdout_bytes_ = proc_.stdout_bytes();
	stderr_bytes_.clear();
	exit_code_ = proc_.get_exit_code();
	return exit_code_;
}

void ProcessWin::stop()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		proc_.close_input();
		proc_.wait();
		running_ = false;
		std::vector<char> const &out = proc_.stdout_bytes();
		stdout_bytes_.assign(out.begin(), out.end());
		stderr_bytes_.clear();
		exit_code_ = proc_.get_exit_code();
	}
}

bool ProcessWin::is_running() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return running_;
}

int ProcessWin::get_exit_code() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return exit_code_;
}

void ProcessWin::write_input(char const *ptr, int len)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		proc_.write_input(ptr, static_cast<size_t>(len));
	}
}

void ProcessWin::close_input()
{
	std::lock_guard<std::mutex> lock(mutex_);
	proc_.close_input();
}

std::vector<char> const &ProcessWin::stdout_bytes() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		std::vector<char> const &out = proc_.stdout_bytes();
		stdout_bytes_.assign(out.begin(), out.end());
	}
	return stdout_bytes_;
}

std::vector<char> const &ProcessWin::stderr_bytes() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return stderr_bytes_;
}

// ProcessWinConPty

ProcessWinConPty::ProcessWinConPty() = default;
ProcessWinConPty::~ProcessWinConPty() = default;

void ProcessWinConPty::start(const std::string &command, const std::string &env, bool use_input)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!BasicProcessWinConPTY::is_conpty_available()) {
		started_ = false;
		running_ = false;
		return;
	}
	started_ = conpty_.exec(command);
	running_ = started_;
	if (started_) {
		stdout_bytes_.clear();
		stderr_bytes_.clear();
	}
	exit_code_ = -1;
}

int ProcessWinConPty::wait()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!running_) {
		return exit_code_;
	}
	auto result = conpty_.wait();
	running_ = false;
	std::vector<char> const &out = conpty_.stdout_bytes();
	stdout_bytes_.assign(out.begin(), out.end());
	stderr_bytes_.clear();
	exit_code_ = result.exit_code;
	return exit_code_;
}

void ProcessWinConPty::stop()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		conpty_.close_input();
		conpty_.wait();
		running_ = false;
		std::vector<char> const &out = conpty_.stdout_bytes();
		stdout_bytes_.assign(out.begin(), out.end());
		stderr_bytes_.clear();
		exit_code_ = conpty_.get_exit_code();
	}
}

bool ProcessWinConPty::is_running() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return running_;
}

int ProcessWinConPty::get_exit_code() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return exit_code_;
}

void ProcessWinConPty::write_input(char const *ptr, int len)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		conpty_.write_input(ptr, static_cast<size_t>(len));
	}
}

int ProcessWinConPty::read_output(char *ptr, int len)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		return conpty_.read_output(ptr, static_cast<size_t>(len));
	}
	return 0;
}

void ProcessWinConPty::close_input()
{
	std::lock_guard<std::mutex> lock(mutex_);
	conpty_.close_input();
}

// std::vector<char> const &ProcessWinConPty::stdout_bytes() const
// {
// 	std::lock_guard<std::mutex> lock(mutex_);
// 	if (running_) {
// 		std::vector<char> const &out = conpty_.stdout_bytes();
// 		stdout_bytes_.assign(out.begin(), out.end());
// 	}
// 	return stdout_bytes_;
// }

// std::vector<char> const &ProcessWinConPty::stderr_bytes() const
// {
// 	std::lock_guard<std::mutex> lock(mutex_);
// 	return stderr_bytes_;
// }

// ProcessWinPty

struct ProcessWinPty::Private {
	std::thread thread;
	int input_fd = -1;
	HANDLE hConout = INVALID_HANDLE_VALUE;
	HANDLE hInput = INVALID_HANDLE_VALUE;
	int exit_code = 128;
};

ProcessWinPty::ProcessWinPty()
	: m(new Private)
{

}

ProcessWinPty::~ProcessWinPty()
{
	wait();
	delete m;
}

std::string ProcessWinPty::exec_winpty(const std::string &cmd, const std::string &env, bool use_input)
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

	std::wstring wcmd = misc::convert_str_to_wstr(cmd);

	std::wstring program = misc::convert_str_to_wstr(misc::getProgram(cmd));
	wchar_t const *program_p = nullptr;
	if (1) {
		// コマンドから実行ファイル名を抜き取る。実際に実行されるプログラムのパス。
		if (!program.empty()) {
			program_p = program.c_str();
		}
	} else {
		// nop:
		// program_p が nullptr 空の時、PATHが通っているコマンドなら実行できる。
	}

	std::wstring wenv = misc::convert_str_to_wstr(env);
	std::vector<wchar_t> envbuf;
	if (!env.empty()) {
		envbuf.resize(wenv.size() + 1);
		memcpy(envbuf.data(), env.c_str(), sizeof(wchar_t) * env.size());
	}

	winpty_spawn_config_t *scfg = winpty_spawn_config_new(WINPTY_SPAWN_FLAG_AUTO_SHUTDOWN,
														  program_p,
														  wcmd.data(),
														  nullptr,
														  envbuf.empty() ? nullptr : envbuf.data(),
														  &err);
	if (!scfg) {
		winpty_error_free(err);
		winpty_free(wp);
		return ret;
	}

	m->hConout = CreateFileW(
				winpty_conout_name(wp),
				GENERIC_READ, 0, nullptr,
				OPEN_EXISTING, 0, nullptr
				);

	if (use_input) {
		m->hInput = CreateFileW(
					winpty_conin_name(wp),
					GENERIC_WRITE, 0, nullptr,
					OPEN_EXISTING, 0, nullptr
					);
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

	if (m->hConout != INVALID_HANDLE_VALUE) {
		char buf[256];
		DWORD n;
		while (ReadFile(m->hConout, buf, sizeof(buf), &n, nullptr) && n > 0) {
			std::lock_guard<std::mutex> lock(mutex_);
			write_output(buf, n);
		}
		CloseHandle(m->hConout);
	}

	WaitForSingleObject(hProcess, INFINITE);
	CloseHandle(hProcess);
	winpty_free(wp);

	if (!ret.empty() && ret.back() == '\n') ret.pop_back();
	if (!ret.empty() && ret.back() == '\r') ret.pop_back();

	return ret;
}

bool ProcessWinPty::is_running() const
{
	return m->thread.joinable();
}

void ProcessWinPty::write_input(const char *ptr, int len)
{
	if (m->hInput != INVALID_HANDLE_VALUE) {
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
					DWORD written;
					WriteFile(m->hInput, left, right - left, &written, nullptr);
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
					DWORD written;
					WriteFile(m->hInput, &c, 1, &written, nullptr);
				}
				left = right;
			} else {
				right++;
			}
		}
	}
}

void ProcessWinPty::start(const std::string &cmd, const std::string &env, bool use_input)
{
	m->thread = std::thread([&](std::string const &cmd, std::string const &env, bool use_input){
			exec_winpty(cmd, env, use_input);
			}, cmd, env, use_input);
}

int ProcessWinPty::wait()
{
	close_input();
	if (m->thread.joinable()) {
		m->thread.join();
		stdout_bytes_ = output_vector_;
		stderr_bytes_ = stderr_bytes_;
		return true;
	}
	return false;
}

void ProcessWinPty::stop()
{
	if (m->hConout != INVALID_HANDLE_VALUE) {
		CloseHandle(m->hConout);
		m->hConout = INVALID_HANDLE_VALUE;
	}
	wait();
}

int ProcessWinPty::get_exit_code() const
{
	return m->exit_code;
}

void ProcessWinPty::read_result(std::vector<char> *out)
{
	*out = output_vector_;
	output_vector_.clear();
}

void ProcessWinPty::close_input()
{
	if (m->hInput != INVALID_HANDLE_VALUE) {
		CloseHandle(m->hInput);
		m->hInput = INVALID_HANDLE_VALUE;
	}
}

int ProcessWinPty::read_output(char *ptr, int len)
{
	std::lock_guard<std::mutex> lock(mutex_);
	int n = output_queue_.size();
	if (n > len) n = len;
	for (int i = 0; i < n; i++) {
		ptr[i] = output_queue_.front();
		output_queue_.pop_front();
	}
	return n;
}

