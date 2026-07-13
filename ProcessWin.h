#ifndef PROCESSWIN_H
#define PROCESSWIN_H

#include "AbstractProcess.h"
#include "WinProcess.h"

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
	bool isRunning() const override;
	int getExitCode() const override;
	void writeInput(char const *ptr, int len) override;
	void closeInput(bool justnow) override;
	std::vector<char> const &stdout_bytes() const override;
	std::vector<char> const &stderr_bytes() const override;
};

class ProcessWinConPty : public AbstractProcess {
private:
	BasicProcessWinConPTY conpty_;
    bool started_ = false;
	bool running_ = false;
	int exit_code_ = -1;
	mutable std::vector<char> stdout_bytes_;
	mutable std::vector<char> stderr_bytes_;
	mutable std::mutex mutex_;
public:
	ProcessWinConPty();
	~ProcessWinConPty() override;
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

#endif // PROCESSWIN_H
