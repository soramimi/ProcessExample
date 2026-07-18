#ifndef BASICPROCESSWIN_H
#define BASICPROCESSWIN_H

#include "AbstractProcess2.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#endif

namespace misc {
std::string get_error_message(uint32_t error_code);
std::string build_command_line(std::vector<std::string> const &args);

}

class _AbstractBasicProcess {
public:
	struct ExecResult {
		bool started = false;
#ifdef _WIN32
		DWORD exit_code = static_cast<DWORD>(-1);
		DWORD error_code = ERROR_SUCCESS;
#else
		uint32_t exit_code = static_cast<uint32_t>(-1);
		uint32_t error_code = 0; //ERROR_SUCCESS;
#endif
		std::string error_message;
	};
	virtual ~_AbstractBasicProcess() {}
	virtual bool start(std::string const &cmd) = 0;
	virtual ExecResult wait() = 0;
	virtual void close_input() = 0;
	virtual int write_input(char const *ptr, int n) = 0;
	virtual int read_output(char *ptr, int n) = 0;
	virtual bool is_running() const = 0;
	virtual std::vector<char> const &stdout_bytes() const = 0;
	virtual int get_exit_code() const = 0;
};

class BasicProcessWin : public _AbstractBasicProcess {
private:
	struct Private;
	Private *m;
	process::helper::dir_string_t change_dir_;
	std::shared_ptr<void> user_data_;
	std::function<void (bool, std::shared_ptr<void>)> completed_fn_;
public:
	struct Options {
		bool output_stdout = false;
		bool output_vector = false;
		bool output_queue = false;
	};
	BasicProcessWin(Options const &options = Options());
	~BasicProcessWin();
	void set_change_dir(process::helper::dir_string_t const &dir)
	{
		change_dir_ = dir;
	}
	void set_options(Options const &options);
	bool start(std::string const &cmd);
	ExecResult wait();
	void close_input();
	int write_input(char const *ptr, int n);
	int read_output(char *ptr, int n);
	bool is_running() const;
	std::vector<char> const &stdout_bytes() const;
	int get_exit_code() const;

	bool wait_for_output(std::string const &text);

	void set_completion_callback(std::function<void (bool, std::shared_ptr<void>)> const &fn, std::shared_ptr<void> user_data)
	{
		completed_fn_ = fn;
		user_data_ = user_data;
	}

	void notify_completed()
	{
		if (completed_fn_) {
			completed_fn_(true, user_data_);
		}
	}
};

#endif // BASICPROCESSWIN_H
