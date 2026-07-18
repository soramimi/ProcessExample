#include "ProcessWinConPty.h"

struct ProcessWinConPty::Private {
	mutable std::mutex mutex;
	BasicProcessWinConPTY conpty;
	bool started = false;
	bool running = false;
	int exit_code = -1;
};

ProcessWinConPty::ProcessWinConPty()
	: m(new Private())
{
	BasicProcessWinConPTY::Options opts;
	opts.output_vector = true;
	set_options(opts);
}

ProcessWinConPty::~ProcessWinConPty()
{
	stop();
	delete m;
}

void ProcessWinConPty::set_options(const BasicProcessWinConPTY::Options &options)
{
	m->conpty.set_options(options);
}

void ProcessWinConPty::start(const std::string &command, const std::string &env, bool use_input)
{
	(void)env;
	(void)use_input;
	std::lock_guard<std::mutex> lock(m->mutex);
	if (m->running) {
		m->conpty.wait();
		m->running = false;
	}
	m->exit_code = -1;
	if (command.empty()) {
		m->started = false;
		return;
	}
	if (!BasicProcessWinConPTY::is_conpty_available()) {
		m->started = false;
		m->running = false;
		return;
	}
	m->conpty.set_change_dir(change_dir_);
	m->started = m->conpty.start(command);
	m->running = m->started;
	if (m->started) {
		stdout_bytes_.clear();
		stderr_bytes_.clear();
	}
	m->exit_code = -1;
}

int ProcessWinConPty::wait()
{
	std::lock_guard<std::mutex> lock(m->mutex);
	if (!m->running) {
		return m->exit_code;
	}
	auto result = m->conpty.wait();
	m->running = false;
	std::vector<char> const &out = m->conpty.stdout_bytes();
	stdout_bytes_.assign(out.begin(), out.end());
	stderr_bytes_.clear();
	m->exit_code = result.exit_code;
	return m->exit_code;
}

void ProcessWinConPty::stop()
{
	std::lock_guard<std::mutex> lock(m->mutex);
	if (m->running) {
		m->conpty.close_input();
		m->conpty.wait();
		m->running = false;
		std::vector<char> const &out = m->conpty.stdout_bytes();
		stdout_bytes_.assign(out.begin(), out.end());
		stderr_bytes_.clear();
		m->exit_code = m->conpty.get_exit_code();
	}
}

bool ProcessWinConPty::is_running() const
{
	std::lock_guard<std::mutex> lock(m->mutex);
	return m->running;
}

int ProcessWinConPty::get_exit_code() const
{
	std::lock_guard<std::mutex> lock(m->mutex);
	return m->exit_code;
}

void ProcessWinConPty::write_input(char const *ptr, int len)
{
	if (!ptr || len <= 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(m->mutex);
	if (m->running) {
		m->conpty.write_input(ptr, len);
	}
}

int ProcessWinConPty::read_output(char *ptr, int len)
{
	if (!ptr || len <= 0) {
		return 0;
	}
	std::lock_guard<std::mutex> lock(m->mutex);
	if (m->running) {
		return m->conpty.read_output(ptr, len);
	}
	return 0;
}

void ProcessWinConPty::set_no_window(bool no_window)
{
	m->conpty.set_no_window(no_window);
}

void ProcessWinConPty::close_input()
{
	std::lock_guard<std::mutex> lock(m->mutex);
	m->conpty.close_input();
}
