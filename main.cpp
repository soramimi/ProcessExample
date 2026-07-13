
#ifndef _WIN32
#error This code is for Windows only.
#endif

#include "ProcessWin.h"
#include "WinProcess.h"
#include "base64.h"
#include "misc.h"
#include <string>
#include <windows.h>

//

//

std::string find_windows_openssh()
{
	wchar_t system_dir[MAX_PATH];
	UINT len = GetSystemDirectoryW(system_dir, ARRAYSIZE(system_dir));
	if (len == 0 || len >= ARRAYSIZE(system_dir)) return {};

	std::wstring path(system_dir, len);
	path += L"\\OpenSSH\\ssh.exe";
	DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};

	std::string result = misc::convert_wstr_to_str(path);
	for (char &c : result) {
		if (c == '\\') c = '/';
	}
	return result;
}


constexpr std::string_view subprocess_tag = "--conpty-subprocess--";

class ProcessConPtyWithWorker /*: public AbstractProcess*/ {
public:
	int start(int argc, char **argv)
	{
	#if 0
		{
			std::string cmd = "git --version";
			{
				WinConPTY conpty;
				conpty.exec(cmd);
				conpty.close_input();
				WinConPTY::ExecResult result = conpty.wait();
			}
			return 0;
		}
	#endif

		if (!BasicProcessWinConPTY::is_conpty_available()) {
			fprintf(stderr, "ConPTY is not available on this system.\n");
			return 1;
		}

		if (argc == 3) {
			std::string_view arg = argv[1];
			if (arg == subprocess_tag) {
				// ワーカーモード: 親から渡されたコマンドを復元し、ConPTY内で実行する。
				std::string cmd = base64_decode(argv[2]);
				BasicProcessWinConPTY conpty;
				conpty.exec(cmd);
				BasicProcessWinConPTY::ExecResult result = conpty.wait();
				if (!result.started) {
					fprintf(stderr, "Failed to start process (error %lu).\n", result.error_code);
					return 128;
				}
				return 0;
			}
		}

		// 監督モード: 同じ実行ファイルをワーカーとして再起動する。
		char tmp[_MAX_PATH];
		memset(tmp, 0, sizeof(tmp));
		GetModuleFileNameA(NULL, tmp, _countof(tmp));
		std::string cmd = "\"" + std::string(tmp) + "\"";

		std::string gitcmd = "--version";
		// std::string gitcmd = "fetch";

		// Git同梱のMSYS版sshは、Gitが標準入出力をパイプ化するとConPTYを
		// 確認入力用TTYとして再取得できないため、Win32 OpenSSHを明示する。
		std::string ssh = find_windows_openssh();
		if (ssh.empty()) {
			fprintf(stderr, "Windows OpenSSH client was not found.\n");
			return 1;
		}
		gitcmd = "git -c core.sshCommand=\"" + ssh + "\" " + gitcmd;

		// 引用符や空白を含むGitコマンドを自己再実行の引数として安全に渡す。
		cmd += ' ' + std::string(subprocess_tag) + ' ' + base64_encode(gitcmd);

		BasicProcessWin proc;
		if (!proc.exec(cmd)) {
			fprintf(stderr, "Failed to start ConPTY worker.\n");
			return 128;
		}

		// SSHが入力待ちになったことを出力から確認してから、親側で決めた回答を送る。
		// 先に送ると、SSHがまだ入力を受け付けておらず回答を失う可能性がある。
		std::string prompt = "Are you sure you want to continue connecting";
		if (proc.wait_for_output(prompt)) {
			std::string send = "no\n";
			if (!proc.write_input(send.data(), send.size())) {
				fprintf(stderr, "Failed to write to ConPTY worker.\n");
			}
		}
		proc.close_input();
		proc.wait();

		return 0;
	}
};

int main_win_conpty_with_worker(int argc, char **argv)
{
	ProcessConPtyWithWorker proc;
	return proc.start(argc, argv);
}

int main_win(int argc, char **argv)
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

int main_winpty(int argc, char **argv)
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

int main_basic_win(int argc, char **argv)
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

int main_basic_win_conpty(int argc, char **argv)
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


int main_win_conpty(int argc, char **argv)
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