
#ifndef PROCESSWINCONPTY_H
#define PROCESSWINCONPTY_H

#include "AbstractProcess.h"
#include "BasicProcessWinConPTY.h"

class ProcessWinConPty : public AbstractPtyProcess {
private:
	BasicProcessWinConPTY conpty_;
	bool started_ = false;
	bool running_ = false;
	int exit_code_ = -1;
	mutable std::mutex mutex_;
public:
	ProcessWinConPty();
	~ProcessWinConPty() override = default;
	void set_change_dir(process::helper::dir_string_t const &dir)
	{
		change_dir_ = dir;
	}
	void start(const std::string &command, const std::string &env, bool use_input) override;
	int wait() override;
	void stop() override;
	bool is_running() const override;
	int get_exit_code() const override;
	void close_input() override;
	void write_input(char const *ptr, int len) override;
	int read_output(char *ptr, int len);
	void set_no_window(bool no_window)
	{
		conpty_.set_no_window(no_window);
	}
};


#endif // PROCESSWINCONPTY_H
