#ifndef WINPROCESS_H
#define WINPROCESS_H

#include "AbstractProcess.h"

#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

// 監督プロセスからConPTYワーカーを起動する。
// hInputWrite_ -> ワーカーstdin、ワーカーstdout/stderr -> hOutputRead_ の双方向構成。
class BasicProcessWin {
private:
	struct Private;
	Private *m;
public:
	BasicProcessWin();
	~BasicProcessWin();
	bool exec(std::string const &cmd);
	bool wait();
	bool wait_for_output(std::string const &text);
	void close_input();
	bool write_input(char const *ptr, size_t n);
	bool isRunning() const;
	std::string stdout_bytes() const;
	int getExitCode() const;
};

//


// ワーカープロセス内でConPTYを所有し、標準入出力とConPTYのパイプを中継する。
class BasicProcessWinConPTY {
public:
	struct ExecResult {
		bool started = false;
		DWORD exit_code = static_cast<DWORD>(-1);
		DWORD error_code = ERROR_SUCCESS;
	};
private:
	struct Private;
	Private *m;
public:
	BasicProcessWinConPTY();
	~BasicProcessWinConPTY();

	bool exec(std::string const &cmd);
	ExecResult wait();
	void close_input();
	bool write_input(char const *ptr, size_t n);
	bool isRunning() const;
	std::string stdout_bytes() const;
	int getExitCode() const;

	static bool is_conpty_available();
};


//
class ProcessWinPty : public AbstractPtyProcess {
private:
	struct Private;
	Private *m;

	std::string exec_winpty(const std::string &cmd, const std::string &env, bool use_input);
public:
	ProcessWinPty();
	~ProcessWinPty();
	bool isRunning() const;
	void writeInput(const char *ptr, int len);
	void start(const std::string &cmd, std::string const &env, bool use_input);
	bool wait(unsigned long time = ULONG_MAX);
	void stop();
	int getExitCode() const;
	int readOutput(char *ptr, int len);

	void readResult(std::vector<char> *out);
	void close_input();

};

#endif // WINPROCESS_H
