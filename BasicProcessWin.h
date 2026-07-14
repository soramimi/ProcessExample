#ifndef BASICPROCESSWIN_H
#define BASICPROCESSWIN_H

#include "AbstractProcess.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#endif


class _AbstractBasicProcess {
public:
	struct ExecResult {
		bool started = false;
		uint32_t exit_code = static_cast<uint32_t>(-1);
		uint32_t error_code = 0; //ERROR_SUCCESS;
	};
	virtual ~_AbstractBasicProcess() {}
	virtual bool exec(std::string const &cmd) = 0;
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
public:
	struct Options {
		bool output_stdout = false;
		bool output_vector = false;
		bool output_queue = false;
	};
	BasicProcessWin(Options const &options = Options());
	~BasicProcessWin();
	void set_options(Options const &options);
	bool exec(std::string const &cmd);
	ExecResult wait();
	void close_input();
	int write_input(char const *ptr, int n);
	int read_output(char *ptr, int n);
	bool is_running() const;
	std::vector<char> const &stdout_bytes() const;
	int get_exit_code() const;

	bool wait_for_output(std::string const &text);
};

class BasicProcessWinConPTY : public _AbstractBasicProcess {
private:
	struct Private;
	Private *m;
public:
	struct Options {
		bool output_stdout = false;
		bool output_vector = false;
		bool output_queue = false;
		bool vt_stripped = true;
	};
	BasicProcessWinConPTY(Options const &options = Options());
	~BasicProcessWinConPTY();
	void set_options(Options const &options);

	bool exec(std::string const &cmd);
	ExecResult wait();
	void close_input();
	int write_input(char const *ptr, int n);
	int read_output(char *ptr, int len);
	bool is_running() const;
	int get_exit_code() const;
	std::vector<char> const &stdout_bytes() const;

	static bool is_conpty_available();
};

#endif // BASICPROCESSWIN_H
