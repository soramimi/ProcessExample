
#include "BasicProcessPosix.h"
#include "misc.h"
#include <string>

#ifdef _WIN32
#include "ProcessConPtyWithWorker.h"
#include "ProcessWin.h"
#include "BasicProcessWin.h"
#include <windows.h>

int main_win_conpty_with_worker(int /*argc*/, char ** /*argv*/)
{
	std::string ssh = misc::find_windows_openssh();
	if (ssh.empty()) {
		fprintf(stderr, "Windows OpenSSH client was not found.\n");
		return 1;
	}

	// std::string gitcmd = "--version";
	std::string gitcmd = "fetch";
	std::string cmd = "git -c core.sshCommand=\"" + ssh + "\" " + gitcmd;

	ProcessConPtyWithWorker proc;
	proc.start(cmd, {}, true);

	std::string prompt = "Are you sure you want to continue connecting";
	if (proc.wait_for_output(prompt)) {
		proc.write_input("no\n", 3);
	}
	proc.close_input();
	proc.wait();
	return 0;
}

int main_win(int /*argc*/, char ** /*argv*/)
{
	std::string cmd = R"("C:\Program Files\Git\cmd\git.exe")";
	cmd += " --version";
	ProcessWin proc;
	proc.start(cmd, false);
	proc.wait();
	auto vec = proc.stdout_bytes();
	std::string_view view(vec.data(), vec.size());
	std::string str = std::string(view);
	puts(str.c_str());
	return 0;
}

int main_winpty(int /*argc*/, char ** /*argv*/)
{
	std::string cmd = R"("C:\Program Files\Git\cmd\git.exe")";
	cmd += " --version";
	ProcessWinPty proc;
	proc.start(cmd, {}, false);
	proc.wait();
	{
		char tmp[1024];
		int len = proc.read_output(tmp, sizeof(tmp) - 1);
		tmp[len] = 0;
		puts(tmp);
	}
	return 0;
}

int main_basic_win(int /*argc*/, char ** /*argv*/)
{
	std::string cmd = R"("C:\Program Files\Git\cmd\git.exe")";
	cmd += " --version";
	BasicProcessWin::Options opts;
	opts.output_vector = true;
	BasicProcessWin proc(opts);
	proc.exec(cmd);
	proc.wait();
	auto vec = proc.stdout_bytes();
	std::string_view view(vec.data(), vec.size());
	std::string str = std::string(view);
	puts(str.c_str());
	return 0;
}

int main_basic_win_conpty(int /*argc*/, char ** /*argv*/)
{
	std::string cmd = R"("C:\Program Files\Git\cmd\git.exe")";
	cmd += " --version";
	BasicProcessWinConPTY::Options opts;
	opts.output_vector = true;
	BasicProcessWinConPTY proc(opts);
	proc.exec(cmd);
	proc.wait();
	auto vec = proc.stdout_bytes();
	std::string_view view(vec.data(), vec.size());
	std::string str = std::string(view);
	puts(str.c_str());
	return 0;
}


int main_win_conpty(int /*argc*/, char ** /*argv*/)
{
	std::string cmd = R"("C:\Program Files\Git\cmd\git.exe")";
	cmd += " --version";
	ProcessWinConPty proc;
	proc.start(cmd, {}, false);
	proc.wait();
	auto vec = proc.stdout_bytes();
	std::string_view view(vec.data(), vec.size());
	std::string str = std::string(view);
	puts(str.c_str());
	return 0;
}
#else
#endif

#ifdef _WIN32
int main(int argc, char **argv)
{
	// worker モードなら即座に実行して終了
	int worker_ret = ProcessConPtyWithWorker::run_worker(argc, argv);
	if (worker_ret >= 0) return worker_ret;

	int select = 0;
	switch (select) {
	case 0:
		main_basic_win(argc, argv);
		break;
	case 1:
		main_basic_win_conpty(argc, argv);
		break;
	case 2:
		main_win(argc, argv);
		break;
	case 3:
		main_win_conpty(argc, argv);
		break;
	case 4:
		main_win_conpty_with_worker(argc, argv);
		break;
	case 5:
		main_winpty(argc, argv);
		break;
	default:
		break;
	}
	return 0;
}
#else

int main_basic_posix(int argc, char **argv)
{
	std::string cmd = R"("/usr/bin/git")";
	cmd += " --version";
	// BasicProcessWin::Options opts;
	// opts.output_vector = true;
	PosixProcess proc;//(opts);
	proc.start(cmd, false);
	proc.wait();
	auto vec = proc.stdout_bytes();
	std::string_view view(vec.data(), vec.size());
	std::string str = std::string(view);
	puts(str.c_str());
	return 0;
}

int main_basic_posix_pty(int argc, char **argv)
{
	std::string cmd = R"("/usr/bin/git")";
	cmd += " --version";
	// BasicProcessWin::Options opts;
	// opts.output_vector = true;
	PosixPtyProcess proc;//(opts);
	proc.start(cmd, {}, false);
	proc.wait();
	auto vec = proc.stdout_bytes();
	std::string_view view(vec.data(), vec.size());
	std::string str = std::string(view);
	puts(str.c_str());
	return 0;
}

int main(int argc, char **argv)
{
	int select = 0;
	switch (select) {
	case 0:
		main_basic_posix(argc, argv);
		break;
	case 1:
		main_basic_posix_pty(argc, argv);
		break;
	}
	return 0;	
}
#endif

