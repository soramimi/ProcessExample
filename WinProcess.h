#ifndef WINPROCESS_H
#define WINPROCESS_H

#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

// 監督プロセスからConPTYワーカーを起動する。
// hInputWrite_ -> ワーカーstdin、ワーカーstdout/stderr -> hOutputRead_ の双方向構成。
class WinProcess {
private:
	struct Private;
	Private *m;
public:
	WinProcess();
	~WinProcess();
	bool exec(std::string const &cmd);
	bool wait();
	bool wait_for_output(std::string const &text);
	void close_input();
	bool write_input(char const *ptr, size_t n);
};

//


// ワーカープロセス内でConPTYを所有し、標準入出力とConPTYのパイプを中継する。
class WinConPTY {
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
	WinConPTY();
	~WinConPTY();

	bool exec(std::string const &cmd);
	ExecResult wait();
	void close_input();
	bool write_input(char const *ptr, size_t n);

	static bool is_conpty_available();
};


//
class ProcessWinPty {
private:
	struct Private;
	Private *m;

	std::string exec_winpty(const std::string &cmd, const std::string &env, bool use_input);
public:
	ProcessWinPty();
	~ProcessWinPty();
	bool isRunning() const;
	void writeInput(const char *ptr, int len);
	void exec(const std::string &cmd, std::string const &env, bool use_input);
	bool wait(unsigned long time = ULONG_MAX);
	void stop();
	int get_exit_code() const;
	void readResult(std::vector<char> *out);
	void close_input();

	int read_output(char *ptr, int len);
};

#endif // WINPROCESS_H
