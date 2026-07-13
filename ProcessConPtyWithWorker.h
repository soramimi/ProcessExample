#ifndef PROCESSCONPTYWITHWORKER_H
#define PROCESSCONPTYWITHWORKER_H

#include "AbstractProcess.h"
#include "WinProcess.h"

#include <string>
#include <vector>
#include <mutex>

class ProcessConPtyWithWorker : public AbstractProcess {
public:
	static int run_worker(int argc, char **argv);
	bool wait_for_output(std::string const &text);

private:
	BasicProcessWin proc_;
	bool started_ = false;
	bool running_ = false;
	int exit_code_ = -1;
	mutable std::vector<char> stdout_bytes_;
	mutable std::vector<char> stderr_bytes_;
	mutable std::mutex mutex_;

public:
	ProcessConPtyWithWorker();
	~ProcessConPtyWithWorker() override;

	void start(const std::string &command, bool use_input) override;
	int wait() override;
	void stop() override;
	bool isRunning() const override;
	int getExitCode() const override;
	void writeInput(char const *ptr, int len) override;
	void closeInput(bool justnow) override;
	std::vector<char> const &stdout_bytes() const override;
	std::vector<char> const &stderr_bytes() const override;
};

#endif // PROCESSCONPTYWITHWORKER_H
