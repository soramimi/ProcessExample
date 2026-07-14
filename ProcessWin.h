#ifndef PROCESSWIN_H
#define PROCESSWIN_H

#include "AbstractProcess.h"
#include "BasicProcessWin.h"

class ProcessWin : public AbstractProcess {
private:
	BasicProcessWin proc_;
	bool started_ = false;
	bool running_ = false;
	int exit_code_ = -1;
	mutable std::vector<char> stdout_bytes_;
	mutable std::vector<char> stderr_bytes_;
	mutable std::mutex mutex_;
public:
	ProcessWin();
	~ProcessWin() override;
	void start(const std::string &command, bool use_input) override;
	int wait() override;
	void stop() override;
	bool is_running() const override;
	int get_exit_code() const override;
	void write_input(char const *ptr, int len) override;
	void close_input() override;
	std::vector<char> const &stdout_bytes() const override;
	std::vector<char> const &stderr_bytes() const override;
};

class ProcessWinConPty : public AbstractPtyProcess {
private:
	BasicProcessWinConPTY conpty_;
	bool started_ = false;
	bool running_ = false;
	int exit_code_ = -1;
	// mutable std::vector<char> stdout_bytes_;
	// mutable std::vector<char> stderr_bytes_;
	mutable std::mutex mutex_;
public:
	ProcessWinConPty();
	~ProcessWinConPty() override;
	void start(const std::string &command, const std::string &env, bool use_input) override;
	int wait() override;
	void stop() override;
	bool is_running() const override;
	int get_exit_code() const override;
	void close_input() override;
	void write_input(char const *ptr, int len) override;
	int read_output(char *ptr, int len);
	// std::vector<char> const &stdout_bytes() const;
	// std::vector<char> const &stderr_bytes() const;
};

class ProcessWinPty : public AbstractPtyProcess {
private:
	struct Private;
	Private *m;

	std::string exec_winpty(const std::string &cmd, const std::string &env, bool use_input);
public:
	ProcessWinPty();
	~ProcessWinPty();
	bool is_running() const;
	void write_input(const char *ptr, int len);
	int read_output(char *ptr, int len);
	void start(const std::string &cmd, std::string const &env, bool use_input);
	int wait();
	void stop();
	int get_exit_code() const;
	void close_input();

	void read_result(std::vector<char> *out);
};

#endif // PROCESSWIN_H
