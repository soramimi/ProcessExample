#ifndef PROCESSCONPTYWITHWORKER_H
#define PROCESSCONPTYWITHWORKER_H

#include "AbstractProcess2.h"
#include "BasicProcessWin.h"
#include <condition_variable>
#include <string>
#include <vector>
#include <mutex>
#include "ProcessHelper.h"

class ProcessConPtyWithWorker : public AbstractPtyProcess {
public:
    static int run_worker(int argc, char **argv);
	bool wait_for_output(std::string const &text);

private:
	static constexpr std::string_view subprocess_tag = "--conpty-worker--";
	BasicProcessWin proc_;
	bool started_ = false;
	bool running_ = false;
	int exit_code_ = -1;
	mutable std::vector<char> stdout_bytes_;
	mutable std::vector<char> stderr_bytes_;
	mutable std::mutex mutex_;
	// process::helper::PushDir change_dir_;

	std::condition_variable cv_;
public:
	ProcessConPtyWithWorker();
	~ProcessConPtyWithWorker() override;

	void start(const std::string &command, std::string const &env, bool use_input) override;
	int wait() override;
	void stop() override;
	bool is_running() const override;
	int get_exit_code() const override;
	void write_input(char const *ptr, int len) override;
	int read_output(char *ptr, int len);
	void close_input() override;

	bool wait(unsigned long time);

	// std::vector<char> const &stdout_bytes() const;
	// std::vector<char> const &stderr_bytes() const;
};

#endif // PROCESSCONPTYWITHWORKER_H
