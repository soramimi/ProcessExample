#include "ProcessConPtyWithWorker.h"
#include "base64.h"
#include "misc.h"
#include <windows.h>

ProcessConPtyWithWorker::ProcessConPtyWithWorker()
{
	BasicProcessWin::Options opts;
	opts.output_stdout = true;
	opts.output_vector = true; // 出力を監視する用（TODO:あとでなんとかする）
	proc_.set_options(opts);
}

ProcessConPtyWithWorker::~ProcessConPtyWithWorker() = default;

int ProcessConPtyWithWorker::run_worker(int argc, char **argv)
{
	if (argc == 3) {
		std::string_view arg = argv[1];
		if (arg == subprocess_tag) {
			std::string cmd = base64_decode(argv[2]);
			BasicProcessWinConPTY::Options opts;
			opts.output_stdout = true;
			BasicProcessWinConPTY conpty(opts);
			conpty.exec(cmd);
			auto result = conpty.wait();
			if (!result.started) {
				return 128;
			}
			return static_cast<int>(result.exit_code);
		}
	}
	return -1; // not worker mode
}

void ProcessConPtyWithWorker::start(const std::string &command, const std::string &env, bool /*use_input*/)
{
	(void)env;
	std::lock_guard<std::mutex> lock(mutex_);
	if (!BasicProcessWinConPTY::is_conpty_available()) {
		started_ = false;
		running_ = false;
		return;
	}

	char tmp[_MAX_PATH];
	memset(tmp, 0, sizeof(tmp));
	GetModuleFileNameA(NULL, tmp, _countof(tmp));

	std::string cmd = "\"" + std::string(tmp) + "\"";
	cmd += ' ' + std::string(subprocess_tag) + ' ' + base64_encode(command);

	started_ = proc_.exec(cmd);
	running_ = started_;
	if (started_) {
		stdout_bytes_.clear();
		stderr_bytes_.clear();
	}
	exit_code_ = -1;
}

int ProcessConPtyWithWorker::wait()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!running_) {
		return exit_code_;
	}
	proc_.wait();
	running_ = false;
	std::vector<char> const &out = proc_.stdout_bytes();
	stdout_bytes_.assign(out.begin(), out.end());
	stderr_bytes_.clear();
	exit_code_ = proc_.get_exit_code();
	return exit_code_;
}

void ProcessConPtyWithWorker::stop()
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

bool ProcessConPtyWithWorker::is_running() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return running_;
}

int ProcessConPtyWithWorker::get_exit_code() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return exit_code_;
}

void ProcessConPtyWithWorker::write_input(char const *ptr, int len)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		proc_.write_input(ptr, static_cast<size_t>(len));
	}
}

int ProcessConPtyWithWorker::read_output(char *ptr, int len)
{
	std::lock_guard<std::mutex> lock(mutex_);
	// if (running_) {
		int n = proc_.read_output(ptr, len);
		return n;
	// }
	return 0;
}

void ProcessConPtyWithWorker::close_input()
{
	std::lock_guard<std::mutex> lock(mutex_);
	proc_.close_input();
}

// std::vector<char> const &ProcessConPtyWithWorker::stdout_bytes() const
// {
// 	std::lock_guard<std::mutex> lock(mutex_);
// 	if (running_) {
// 		std::string out = proc_.stdout_bytes();
// 		stdout_bytes_.assign(out.begin(), out.end());
// 	}
// 	return stdout_bytes_;
// }

// std::vector<char> const &ProcessConPtyWithWorker::stderr_bytes() const
// {
// 	std::lock_guard<std::mutex> lock(mutex_);
// 	return stderr_bytes_;
// }

bool ProcessConPtyWithWorker::wait_for_output(std::string const &text)
{
	return proc_.wait_for_output(text);
}
