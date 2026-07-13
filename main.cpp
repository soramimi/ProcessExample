
#ifndef _WIN32
#error This code is for Windows only.
#endif

#include "ProcessConPtyWithWorker.h"
#include "ProcessWin.h"
#include "WinProcess.h"
#include "misc.h"
#include <string>
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
	proc.start(cmd, false);

	std::string prompt = "Are you sure you want to continue connecting";
	if (proc.wait_for_output(prompt)) {
		proc.writeInput("no\n", 3);
	}
	proc.closeInput(false);
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
		int len = proc.readOutput(tmp, sizeof(tmp) - 1);
		tmp[len] = 0;
		puts(tmp);
	}
	return 0;
}

int main_basic_win(int /*argc*/, char ** /*argv*/)
{
	std::string cmd = R"("C:\Program Files\Git\cmd\git.exe")";
	cmd += " --version";
	BasicProcessWin proc;
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
	BasicProcessWinConPTY proc;
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
	proc.start(cmd, false);
	proc.wait();
	auto vec = proc.stdout_bytes();
	std::string_view view(vec.data(), vec.size());
	std::string str = std::string(view);
	puts(str.c_str());
	return 0;
}

int main(int argc, char **argv)
{
	// worker モードなら即座に実行して終了
	int worker_ret = ProcessConPtyWithWorker::run_worker(argc, argv);
	if (worker_ret >= 0) return worker_ret;

	int select = 4;
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
