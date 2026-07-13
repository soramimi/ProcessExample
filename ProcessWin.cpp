#include "ProcessWin.h"

ProcessWin::ProcessWin() = default;
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
	std::string out = proc_.getOutput();
	stdout_bytes_.assign(out.begin(), out.end());
	stderr_bytes_.clear();
	exit_code_ = proc_.getExitCode();
	return exit_code_;
}

void ProcessWin::stop()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		proc_.close_input();
		proc_.wait();
		running_ = false;
		std::string out = proc_.getOutput();
		stdout_bytes_.assign(out.begin(), out.end());
		stderr_bytes_.clear();
		exit_code_ = proc_.getExitCode();
	}
}

bool ProcessWin::isRunning() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return running_;
}

int ProcessWin::getExitCode() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return exit_code_;
}

void ProcessWin::writeInput(char const *ptr, int len)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		proc_.write_input(ptr, static_cast<size_t>(len));
	}
}

void ProcessWin::closeInput(bool justnow)
{
	std::lock_guard<std::mutex> lock(mutex_);
	proc_.close_input();
}

std::vector<char> const &ProcessWin::stdout_bytes() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		std::string out = proc_.getOutput();
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

void ProcessWinConPty::start(const std::string &command, bool use_input)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!WinConPTY::is_conpty_available()) {
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
	std::string out = conpty_.getOutput();
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
		std::string out = conpty_.getOutput();
		stdout_bytes_.assign(out.begin(), out.end());
		stderr_bytes_.clear();
		exit_code_ = conpty_.getExitCode();
	}
}

bool ProcessWinConPty::isRunning() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return running_;
}

int ProcessWinConPty::getExitCode() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return exit_code_;
}

void ProcessWinConPty::writeInput(char const *ptr, int len)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		conpty_.write_input(ptr, static_cast<size_t>(len));
	}
}

void ProcessWinConPty::closeInput(bool justnow)
{
	std::lock_guard<std::mutex> lock(mutex_);
	conpty_.close_input();
}

std::vector<char> const &ProcessWinConPty::stdout_bytes() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		std::string out = conpty_.getOutput();
		stdout_bytes_.assign(out.begin(), out.end());
	}
	return stdout_bytes_;
}

std::vector<char> const &ProcessWinConPty::stderr_bytes() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return stderr_bytes_;
}
