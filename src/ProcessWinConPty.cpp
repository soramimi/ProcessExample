#include "ProcessWinConPty.h"

ProcessWinConPty::ProcessWinConPty()
{
	// conpty_.set
}


void ProcessWinConPty::start(const std::string &command, const std::string &env, bool use_input)
{
	(void)env;
	(void)use_input;
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		conpty_.wait();
		running_ = false;
	}
	exit_code_ = -1;
	if (command.empty()) {
		started_ = false;
		return;
	}
	if (!BasicProcessWinConPTY::is_conpty_available()) {
		started_ = false;
		running_ = false;
		return;
	}
	conpty_.set_change_dir(change_dir_);
	started_ = conpty_.start(command);
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
	if (!ptr || len <= 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		conpty_.write_input(ptr, len);
	}
}

int ProcessWinConPty::read_output(char *ptr, int len)
{
	if (!ptr || len <= 0) {
		return 0;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		return conpty_.read_output(ptr, len);
	}
	return 0;
}

void ProcessWinConPty::close_input()
{
	std::lock_guard<std::mutex> lock(mutex_);
	conpty_.close_input();
}
