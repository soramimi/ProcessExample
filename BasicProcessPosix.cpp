#include "BasicProcessPosix.h"
#include <cstring>
#include <deque>
#include <mutex>

#ifdef _WIN32
#include <io.h>
#else
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>

namespace {
// 子プロセスが標準入力を閉じた（あるいは既に終了した）後にwrite()すると、
// デフォルトのSIGPIPEでアプリ全体が終了してしまうため、プロセス起動前に一度だけ無効化する。
bool const ignore_sigpipe_ = [](){ std::signal(SIGPIPE, SIG_IGN); return true; }();
}
#endif


class OutputReaderThread {
private:
	int fd;
	std::thread thread_;
	std::mutex *mutex_;
	std::deque<char> *buffer_;
protected:
	void run()
	{
		while (1) {
			char buf[1024];
			int n = read(fd, buf, sizeof(buf));
			if (n < 1) break;
			if (buffer_) {
				std::lock_guard<std::mutex> lock(*mutex_);
				buffer_->insert(buffer_->end(), buf, buf + n);
			}
		}
	}
public:
	OutputReaderThread(int fd, std::mutex *mutex, std::deque<char> *out)
		: fd(fd)
		, mutex_(mutex)
		, buffer_(out)
	{
	}
	~OutputReaderThread()
	{
		stop();
	}
	void start()
	{
		stop();
		thread_ = std::thread([this](){
			run();
		});
	}
	void stop()
	{
		if (thread_.joinable()) {
			thread_.join();
		}
	}
	void wait()
	{
		stop();
	}
};

class UnixProcessThread {
public:
	std::thread thread;
	std::mutex *mutex = nullptr;
	std::vector<std::string> argvec;
	std::vector<char *> args;
	std::deque<char> inq;
	std::deque<char> outq;
	std::deque<char> errq;
	bool use_input = false;
	int fd_in_read = -1;
	std::atomic<pid_t> pid{0};
	int exit_code = -1;
	bool close_input_later = false;
protected:
public:
	void init(std::mutex *mutex, bool use_input)
	{
		this->mutex = mutex;
		this->use_input = use_input;
	}
	void reset()
	{
		argvec.clear();
		args.clear();
		inq.clear();
		outq.clear();
		errq.clear();
		use_input = false;
		fd_in_read = -1;
		pid = 0;
		exit_code = -1;
		close_input_later = false;
	}
	
protected:
	void run()
	{
		exit_code = -1;
		const int R = 0;
		const int W = 1;
		const int E = 2;
		int stdin_pipe[3] = { -1, -1, -1 };
		int stdout_pipe[3] = { -1, -1, -1 };
		int stderr_pipe[3] = { -1, -1, -1 };
		char const *error_message;
		int fd_out_write;
		int fd_err_write;
		pid_t child_pid;

		if (pipe(stdin_pipe) < 0) {
			error_message = "failed: pipe";
			goto fail;
		}

		if (pipe(stdout_pipe) < 0) {
			error_message = "failed: pipe";
			goto fail;
		}

		if (pipe(stderr_pipe) < 0) {
			error_message = "failed: pipe";
			goto fail;
		}

		child_pid = fork();
		if (child_pid < 0) {
			error_message = "failed: fork";
			goto fail;
		}

		if (child_pid == 0) { // child
			setenv("LANG", "C", 1);
			close(stdin_pipe[W]);
			close(stdout_pipe[R]);
			close(stderr_pipe[R]);
			dup2(stdin_pipe[R], R);
			dup2(stdout_pipe[W], W);
			dup2(stderr_pipe[W], E);
			close(stdin_pipe[R]);
			close(stdout_pipe[W]);
			close(stderr_pipe[E]);
			if (execvp(args[0], &args[0]) < 0) {
				close(stdin_pipe[R]);
				close(stdout_pipe[W]);
				close(stderr_pipe[E]);
				fprintf(stderr, "failed: exec\n");
				// forkした子プロセス側なので、exit()（atexitハンドラやCライブラリの
				// バッファを親と共有した状態でフラッシュしてしまう）ではなく_exit()を使う。
				_exit(127);
			}
		}
		pid = child_pid;

		close(stdin_pipe[R]);
		close(stdout_pipe[W]);
		close(stderr_pipe[W]);
		fd_in_read = stdin_pipe[W];
		fd_out_write = stdout_pipe[R];
		fd_err_write = stderr_pipe[R];

		//

		if (!use_input) {
			closeInput();
		}

		{
			OutputReaderThread t1(fd_out_write, mutex, &outq);
			OutputReaderThread t2(fd_err_write, mutex, &errq);
			t1.start();
			t2.start();

			while (1) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				int status = 0;
				pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
				if (wait_result == child_pid) {
					if (WIFEXITED(status)) {
						exit_code = WEXITSTATUS(status);
						break;
					}
					if (WIFSIGNALED(status)) {
						exit_code = 128 + WTERMSIG(status);
						break;
					}
				} else if (wait_result < 0 && errno != EINTR) {
					break;
				}
				{
					std::lock_guard<std::mutex> lock(*mutex);
					int n = inq.size();
					if (n > 0) {
						while (n > 0) {
							char tmp[1024];
							int l = n;
							if (l > (int)sizeof(tmp)) {
								l = sizeof(tmp);
							}
							std::copy(inq.begin(), inq.begin() + l, tmp);
							if (fd_in_read != -1) {
								ssize_t r = write(fd_in_read, tmp, l);
								if (r < 0) {
									// 子プロセスが標準入力を閉じている（またはすでに終了した）。
									// これ以上書き込めないので入力側を閉じて諦める。
									closeInput();
									inq.clear();
									break;
								}
								inq.erase(inq.begin(), inq.begin() + r);
								n -= static_cast<int>(r);
								continue;
							}
							inq.clear();
							break;
						}
					} else if (close_input_later) {
						closeInput();
					}
				}
			}

			t1.wait();
			t2.wait();
		}

		close(fd_out_write);
		close(fd_err_write);
		pid = 0;
		return;

	fail:
		// ここに到達するのはpipe()/fork()がこのプロセス（親側）で失敗した場合のみ。
		// fdを使い果たした等の一時的な資源不足でホストアプリ全体を巻き込んで
		// 終了させるべきではないので、exit()は呼ばずに失敗として呼び出し元へ返す。
		if (stdin_pipe[R] >= 0) close(stdin_pipe[R]);
		if (stdin_pipe[W] >= 0) close(stdin_pipe[W]);
		if (stdout_pipe[R] >= 0) close(stdout_pipe[R]);
		if (stdout_pipe[W] >= 0) close(stdout_pipe[W]);
		if (stderr_pipe[R] >= 0) close(stderr_pipe[R]);
		if (stderr_pipe[W] >= 0) close(stderr_pipe[W]);
		fd_in_read = -1;
		pid = 0;
		exit_code = -1;
		fprintf(stderr, "%s\n", error_message);
	}
public:
	UnixProcessThread() = default;
	~UnixProcessThread()
	{
		terminate();
		stop();
	}
	void writeInput(char const *ptr, int len)
	{
		if (!mutex || !ptr || len <= 0) return;
		std::lock_guard<std::mutex> lock(*mutex);
		inq.insert(inq.end(), ptr, ptr + len);
	}
	
	void closeInput()
	{
		if (fd_in_read >= 0) {
			close(fd_in_read);
			fd_in_read = -1;
		}
	}
	void start()
	{
		stop();
		thread = std::thread([this](){
			run();
		});
	}
	void terminate()
	{
		pid_t child_pid = pid.load();
		if (child_pid > 0) kill(child_pid, SIGTERM);
		closeInput();
	}
	void stop()
	{
		if (thread.joinable()) {
			thread.join();
		}
	}
	void wait()
	{
		stop();
	}
};

struct PosixProcess::Private {
	std::mutex mutex;
	UnixProcessThread th;
};

PosixProcess::PosixProcess()
	: m(new Private)
{
}

PosixProcess::~PosixProcess()
{
	delete m;
}

void PosixProcess::parseArgs(std::string const &cmd, std::vector<std::string> *out)
{
	out->clear();
	char const *begin = cmd.c_str();
	char const *end = begin + cmd.size();
	std::vector<char> tmp;
	char const *ptr = begin;
	int quote = 0;
	while (1) {
		int c = 0;
		if (ptr < end) {
			c = *ptr & 0xff;
		}
		if (c == '\"' && ptr + 2 < end && ptr[1] == '\"' && ptr[2] == '\"') {
			tmp.push_back(c);
			ptr += 3;
		} else {
			if (quote != 0 && c != 0) {
				if (c == quote) {
					quote = 0;
				} else {
					tmp.push_back(c);
				}
			} else if (c == '\"') {
				quote = c;
			} else if (isspace(c) || c == 0) {
				if (!tmp.empty()) {
					std::string s(&tmp[0], tmp.size());
					out->push_back(s);
				}
				if (c == 0) break;
				tmp.clear();
			} else {
				tmp.push_back(c);
			}
			ptr++;
		}
	}
}

void PosixProcess::start(std::string const &command, bool use_input)
{
	if (is_running()) return;
	exit_code_ = -1;
	parseArgs(command, &m->th.argvec);
	if (!m->th.argvec.empty()) {
		for (std::string const &s : m->th.argvec) {
			m->th.args.push_back(const_cast<char *>(s.c_str()));
		}
		m->th.args.push_back(nullptr);
		
		m->th.init(&m->mutex, use_input);
		m->th.start();
	}
}

int PosixProcess::wait()
{
	m->th.wait();
	
	stdout_bytes_.clear();
	stderr_bytes_.clear();
	if (!m->th.outq.empty()) stdout_bytes_.insert(stdout_bytes_.end(), m->th.outq.begin(), m->th.outq.end());
	if (!m->th.errq.empty()) stderr_bytes_.insert(stderr_bytes_.end(), m->th.errq.begin(), m->th.errq.end());
	exit_code_ = m->th.exit_code;
	m->th.reset();
	return exit_code_;
}

void PosixProcess::write_input(char const *ptr, int len)
{
	m->th.writeInput(ptr, len);
}

void PosixProcess::close_input(bool justnow)
{
	if (justnow) {
		m->th.closeInput();
	} else {
		m->th.close_input_later = true;
	}
}

void PosixProcess::close_input()
{
	close_input(true);
}

std::vector<char> const &PosixProcess::stdout_bytes() const
{
	return stdout_bytes_;
}

std::vector<char> const &PosixProcess::stderr_bytes() const
{
	return stderr_bytes_;
}

std::optional<std::string> PosixProcess::run_and_wait(const std::string &command)
{
	PosixProcess proc;
	proc.start(command, false);
	proc.wait();
	std::vector<char> v = proc.stdout_bytes();
	if (v.empty()) return std::nullopt;
	return std::string(v.data(), v.size());
}

void PosixProcess::stop()
{
	m->th.terminate();
	wait();
}

bool PosixProcess::is_running() const
{
	return m->th.thread.joinable();
}

int PosixProcess::get_exit_code() const
{
	// wait()完了後はUnixProcessThread側がreset()で終了コードを失うため、
	// wait()がキャッシュした値を返す。
	return exit_code_;
}

//

namespace {

void make_argv(char *command, std::vector<char *> *out)
{
	char *dst = command;
	char *src = command;
	char *arg = command;
	bool quote = false;
	bool accept = false;
	while (1) {
		char c = *src;
		if (c == '\"') {
			if (src[1] == '\"' && src[2] == '\"') {
				*dst++ = c;
				src += 3;
			} else {
				quote = !quote;
				accept = true;
				src++;
			}
		} else if (quote && c != 0) {
			*dst++ = *src++;
		} else if (c == 0 || isspace(c & 0xff)) {
			*dst++ = 0;
			if (accept || *arg) {
				out->push_back(arg);
			}
			if (c == 0) break;
			accept = false;
			arg = dst;
			src++;
		} else {
			*dst++ = *src++;
		}
	}
}

} // namespace

// PosixPtyProcess

struct PosixPtyProcess::Private {
	std::atomic<bool> interrupted{false};
	std::mutex mutex;
	std::thread thread;
	std::string command;
	std::string env;
	int pty_master = -1;
	int exit_code = -1;
};

PosixPtyProcess::PosixPtyProcess()
	: m(new Private)
{
}

PosixPtyProcess::~PosixPtyProcess()
{
	stop_();
	delete m;
}

bool PosixPtyProcess::is_running() const
{
	// return QThread::isRunning();
	return m->thread.joinable();
}

void PosixPtyProcess::write_input(char const *ptr, int len)
{
	if (!ptr || len <= 0) return;
	std::lock_guard<std::mutex> lock(m->mutex);
	if (m->pty_master < 0) return;
	while (len > 0) {
		ssize_t written = write(m->pty_master, ptr, static_cast<size_t>(len));
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0) break;
		ptr += written;
		len -= static_cast<int>(written);
	}
}

int PosixPtyProcess::read_output(char *ptr, int len)
{
	// QMutexLocker lock(&m->mutex);
	std::lock_guard<std::mutex> lock(m->mutex);
	int n = output_queue_.size();
	if (n > len) {
		n = len;
	}
	if (n > 0) {
		auto it = output_queue_.begin();
		std::copy(it, it + n, ptr);
		output_queue_.erase(it, it + n);
	}
	return n;
}

void PosixPtyProcess::start(std::string const &cmd, std::string const &env, bool use_input)
{
	(void)use_input;
	if (is_running()) return;
	m->command = cmd;
	m->env = env;
	m->interrupted = false;
	m->exit_code = -1;
	if (cmd.empty()) return;
	// QThread::start();
	m->thread = std::thread([this](){
		run();
	});
}

bool PosixPtyProcess::wait_(unsigned long time)
{
	if (m->thread.joinable()) {
		m->thread.join();
		// QMutexLocker lock(&m->mutex);
		std::lock_guard<std::mutex> lock(m->mutex);
		stdout_bytes_ = output_vector_;
		// stderr_bytes_ =
		return true;
	}
	return false;
}

int PosixPtyProcess::wait()
{
	return wait_(LONG_MAX);
}

void PosixPtyProcess::run()
{
	struct termios orig_termios = {};
	struct winsize orig_winsize = {25, 80, 0, 0};
	
	// TraceLogger trace;
	// trace.begin("process", QString::fromStdString(m->command));
	
	tcgetattr(STDIN_FILENO, &orig_termios);
	ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&orig_winsize);
	
	m->pty_master = posix_openpt(O_RDWR);
	if (m->pty_master < 0 || grantpt(m->pty_master) < 0 || unlockpt(m->pty_master) < 0) {
		// PTYを確保できない場合はforkせずに失敗として終了する。
		// ここでforkに進むと、壊れたfdを子プロセスに渡してしまい原因不明な不具合になる。
		fprintf(stderr, "failed: posix_openpt/grantpt/unlockpt\n");
		if (m->pty_master >= 0) {
			close(m->pty_master);
			m->pty_master = -1;
		}
		m->exit_code = -1;
		notify_completed();
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "failed: fork\n");
		close(m->pty_master);
		m->pty_master = -1;
		m->exit_code = -1;
		notify_completed();
		return;
	}
	if (pid == 0) {
		setsid();
		setenv("LANG", "C", 1);
		if (!m->env.empty()) {
			char *env = (char *)alloca(m->env.size() + 1);
			strcpy(env, m->env.c_str());
			putenv(env);
		}

		char *pts_name = ptsname(m->pty_master);
		int pty_slave = pts_name ? open(pts_name, O_RDWR) : -1;
		close(m->pty_master);
		if (pty_slave < 0) {
			fprintf(stderr, "failed: open pty slave\n");
			_exit(127);
		}

		struct termios tio;
		memset(&tio, 0, sizeof(tio));
		cfmakeraw(&tio);
		tio.c_cc[VMIN]  = 1;
		tio.c_cc[VTIME] = 0;
		tio.c_lflag |= ECHO;
		tcsetattr(pty_slave, TCSANOW, &tio);
		ioctl(pty_slave, TIOCSWINSZ, &orig_winsize);
		
		dup2(pty_slave, STDIN_FILENO);
		dup2(pty_slave, STDOUT_FILENO);
		dup2(pty_slave, STDERR_FILENO);
		close(pty_slave);
		
#ifdef QT_VERSION
		QDir::setCurrent(change_dir_);
#endif
		
		char *command = (char *)alloca(m->command.size() + 1);
		strcpy(command, m->command.c_str());
		std::vector<char *> argv;
		make_argv(command, &argv);
		if (argv.empty()) _exit(127);
		argv.push_back(nullptr);
		execvp(argv[0], &argv[0]);

		// execvp()は成功すれば戻らない。ここに来るのは失敗した場合のみ。
		// 何もせず抜けると、フォークされたこの子プロセスがスレッド関数の
		// 残りを実行し続け（このプロセスにとっては唯一のスレッドなので）
		// 最終的に終了コード0で静かに終了してしまい、呼び出し元からは
		// コマンドが成功したように見えてしまう。
		fprintf(stderr, "failed: exec\n");
		_exit(127);

	} else {
		
		bool ok = false;
		bool child_reaped = false;
		
		while (1) {
			// if (isInterruptionRequested()) break;
			if (m->interrupted) break;
			int status = 0;
			int r = waitpid(pid, &status, WNOHANG);
			if (r < 0) break;
			if (r > 0) {
				child_reaped = true;
				if (WIFEXITED(status)) {
					m->exit_code = WEXITSTATUS(status);
					ok = true;
					break;
				}
				if (WIFSIGNALED(status)) {
					m->exit_code = 128 + WTERMSIG(status);
					break;
				}
			}
			
			{
				fd_set fds;
				FD_ZERO(&fds);
				FD_SET(m->pty_master, &fds);
				timeval tv;
				tv.tv_sec = 0;
				tv.tv_usec = 10000;
				int r = select(m->pty_master + 1, &fds, nullptr, nullptr, &tv);
				if (r < 0) break;
				if (r > 0) {
					char buf[1024];
					int len = read(m->pty_master, buf, sizeof(buf));
					if (len > 0) {
						// QMutexLocker lock(&m->mutex);
						std::lock_guard<std::mutex> lock(m->mutex);
						// output_queue_.insert(output_queue_.end(), buf, buf + len);
						// output_vector_.insert(output_vector_.end(), buf, buf + len);
						write_output(buf, len);
					}
				}
			}
		}
		
		if (!child_reaped) {
			kill(pid, SIGTERM);
			int status = 0;
			while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
			if (WIFSIGNALED(status)) m->exit_code = 128 + WTERMSIG(status);
		}
		close(m->pty_master);
		m->pty_master = -1;
		
		// trace.end();
		
		notify_completed();
		
		(void)ok;
	}
}

void PosixPtyProcess::stop_()
{
	// requestInterruption();
	m->interrupted = true;
	wait_();
}

void PosixPtyProcess::stop()
{
	stop_();
}

int PosixPtyProcess::get_exit_code() const
{
	return m->exit_code;
}

void PosixPtyProcess::close_input()
{
	
}







