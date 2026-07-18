#ifndef PROCESSWIN_H
#define PROCESSWIN_H

#include "AbstractProcess.h"
#include "BasicProcessWin.h"

class ProcessWin : public AbstractProcess {
private:
	struct Private;
	Private *m;
	void _close_input(bool justnow);
public:
	ProcessWin();
	~ProcessWin();
	bool is_running() const;
	void write_input(char const *ptr, int len);
	void close_input();
	void start(const std::string &command, bool use_input);
	void stop();
	int wait();
	int get_exit_code() const;

	const std::vector<char> &stdout_bytes() const;
	const std::vector<char> &stderr_bytes() const;

};

class ProcessWinPty : public AbstractPtyProcess {
private:
	struct Private;
	Private *m;
protected:
	void run();
public:
	ProcessWinPty();
	~ProcessWinPty() override;
	bool is_running() const override;
	int read_output(char *dstptr, int maxlen) override;
	void write_input(char const *ptr, int len) override;
	void close_input();
	void start(std::string const &cmdline, std::string const &env, bool use_input) override;
	void stop() override;
	int wait();
	int get_exit_code() const override;

	bool wait(unsigned long /*time*/)
	{
		return wait() == 0;
	}
};

#endif // PROCESSWIN_H
