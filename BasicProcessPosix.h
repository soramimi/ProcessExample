#ifndef BASICPROCESSPOSIX_H
#define BASICPROCESSPOSIX_H

#include "AbstractProcess.h"
#include <optional>
#include <climits>

class PosixProcess : public AbstractProcess {
private:
	struct Private;
	Private *m;
	mutable std::vector<char> stdout_bytes_;
	mutable std::vector<char> stderr_bytes_;
	int exit_code_ = -1;
	static void parseArgs(std::string const &cmd, std::vector<std::string> *out);
public:
	
	PosixProcess();
	~PosixProcess();
	
	void start(std::string const &command, bool use_input);
	int wait();
	void stop();
	bool is_running() const;
	void write_input(char const *ptr, int len);
	void close_input();
	int get_exit_code() const;
	std::vector<char> const &stdout_bytes() const;
	std::vector<char> const &stderr_bytes() const;
	
	void close_input(bool justnow);
	
	static std::optional<std::string> run_and_wait(std::string const &command);
	
	
	// AbstractProcess interface
public:
};

class PosixPtyProcess : public AbstractPtyProcess {
private:
	struct Private;
	Private *m;
	bool wait_(unsigned long time = ULONG_MAX);
	void stop_();
protected:
	void run();
public:
	PosixPtyProcess();
	~PosixPtyProcess() override;
	bool is_running() const override;
	void write_input(char const *ptr, int len) override;
	int read_output(char *ptr, int len) override;
	void start(const std::string &cmd, const std::string &env, bool use_input) override;
	int wait() override;
	void stop() override;
	int get_exit_code() const override;
	// std::vector<char> const &readResult() override
	// {
	// 	return stdout_bytes_;
	// }
	
	// AbstractPtyProcess interface
public:
	void close_input();
};

#endif // BASICPROCESSPOSIX_H
