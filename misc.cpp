#include "misc.h"
#include <windows.h>



std::wstring misc::convert_str_to_wstr(const std::string_view &str)
{
	std::wstring wstr;
	if (str.empty()) return wstr;
	int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
	if (len > 0) {
		wstr.resize(len);
		MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], len);
	}
	return wstr;
}

std::string misc::convert_wstr_to_str(const std::wstring &wstr)
{
	std::string str;
	if (wstr.empty()) return str;
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	if (len > 0) {
		str.resize(len);
		WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &str[0], len, nullptr, nullptr);
	}
	return str;
}

/**
 * コマンドラインから実行ファイル名を抜き取る。
 * 例: "C:\Program Files\MyApp\app.exe" --option -> C:\Program Files\MyApp\app.exe
 */
std::string misc::find_windows_openssh()
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

std::string_view misc::getProgram(std::string_view cmdline)
{
	char const *begin = cmdline.data();
	char const *end = begin + cmdline.size();
	char const *ptr = begin;
	bool quote = 0;
	while (1) {
		char c = 0;
		if (ptr < end) {
			c = *ptr;
		}
		if (c == '\"') {
			if (quote) {
				quote = false;
			} else {
				quote = true;
			}
			ptr++;
		} else if (quote && c != 0) {
			ptr++;
		} else if (isspace((unsigned char)c) || c == 0) {
			break;
		} else {
			ptr++;
		}
	}
	char const *left = begin;
	char const *right = ptr;
	if (left + 1 < right) {
		if (left[0] == '\"' && right[-1] == '\"') {
			left++;
			right--;
		}
	}
	return std::string_view(left, right - left);
}


