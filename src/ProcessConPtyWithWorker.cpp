#include "BasicProcessWinConPTY.h"
#include "ProcessConPtyWithWorker.h"
#include <base64.h>
#include <misc.h>
#include <windows.h>
#include <algorithm>

#include "ProcessWinHelper.h"

ProcessConPtyWithWorker::ProcessConPtyWithWorker()
{
	BasicProcessWin::Options opts;
	opts.output_stdout = true;
	opts.output_vector = true; // output monitoring
	proc_.set_options(opts);
}

ProcessConPtyWithWorker::~ProcessConPtyWithWorker()
{

}

int ProcessConPtyWithWorker::run_worker(int argc, char **argv)
{
	bool as_worker = false;

	BasicProcessWinConPTY::Options opts;
	std::string_view encoded;

	int argi = 1;
	while (argi < argc) {
		std::string_view arg = argv[argi++];
		if (arg == subprocess_tag) {
			if (argi < argc) {
				encoded = argv[argi++];
				as_worker = true;
			} else {
				return 128;
			}
		} else if (arg == "--no-window") {
			opts.no_window = true;
		} else {
			return 128;
		}
	}

	if (as_worker && !encoded.empty()) {
		std::vector<char> decoded;
		Base64::decode(encoded.data(), encoded.size(), &decoded);
		std::string cmd(decoded.data(), decoded.size());
		opts.output_stdout = true;
		BasicProcessWinConPTY conpty(opts);
		conpty.start(cmd);
		auto result = conpty.wait();
		if (!result.started) {
			return 128;
		}
		return static_cast<int>(result.exit_code);
	}

	return 128; // not worker mode
}

void ProcessConPtyWithWorker::start(const std::string &command, const std::string &env, bool /*use_input*/)
{
	(void)env;
	stop();
	std::lock_guard<std::mutex> lock(mutex_);
	started_ = false;
	running_ = false;
	exit_code_ = -1;
	if (command.empty()) {
		return;
	}
	if (!BasicProcessWinConPTY::is_conpty_available()) {
		return;
	}

	std::wstring agent_path;
	{
		// Use dynamic buffer to avoid large stack array
		std::vector<wchar_t> tmp(32768);
		DWORD length = GetModuleFileNameW(nullptr, tmp.data(), static_cast<DWORD>(tmp.size()));
		if (length == 0 || length >= tmp.size()) {
			return;
		} else {
			std::wstring_view view(tmp.data(), length);
			auto i = view.find_last_of(L"/\\");
			agent_path = view.substr(0, i + 1);
			agent_path += L"conpty-worker.exe";
		}
	}
	DWORD attributes = GetFileAttributesW(agent_path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
		return;
	}

	std::string executable = convert_wstr_to_str(std::wstring_view(agent_path));
	if (executable.empty()) {
		return;
	}

	std::string cmd = misc::build_command_line({
		executable,
		std::string(subprocess_tag),
		base64_encode(command)
	});

	proc_.set_completion_callback([this](bool started, std::shared_ptr<void> user_data) {
		(void)started;
		(void)user_data;
		running_ = false;
		cv_.notify_all();
		this->notify_completed();
	}, this->user_data_);

	proc_.set_change_dir(change_dir_);
	started_ = proc_.start(cmd);
	running_ = started_;
	if (started_) {
		stdout_bytes_.clear();
		stderr_bytes_.clear();
	}
	exit_code_ = -1;
}

bool ProcessConPtyWithWorker::wait(unsigned long time)
{
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait_for(lock, std::chrono::milliseconds(time), [this]() { return !running_; });
	return true;
}

int ProcessConPtyWithWorker::wait()
{
#if 1
	proc_.wait();
#else

	std::lock_guard<std::mutex> lock(mutex_);
	if (!running_) {
		return exit_code_;
	}
	proc_.wait();
#endif
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<char> const &out = proc_.stdout_bytes();
	stdout_bytes_.assign(out.begin(), out.end());
	stderr_bytes_.clear();
	exit_code_ = proc_.get_exit_code();
	running_ = false;
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
	if (!ptr || len <= 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		proc_.write_input(ptr, len);
	}
}

int ProcessConPtyWithWorker::read_output(char *ptr, int len)
{
	if (!ptr || len <= 0) {
		return 0;
	}
	// std::lock_guard<std::mutex> lock(mutex_);
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
