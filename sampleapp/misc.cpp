
#include "misc.h"
#include <string>

#ifdef _WIN32
#include <windows.h>
#include "ProcessHelper.h"
#include <misc.h>
#include "unicode_conversion.h"

/**
 * コマンドラインから実行ファイル名を抜き取る。
 * 例: "C:\Program Files\MyApp\app.exe" --option -> C:\Program Files\MyApp\app.exe
 */
std::string find_windows_openssh()
{
	wchar_t system_dir[MAX_PATH];
	UINT len = GetSystemDirectoryW(system_dir, ARRAYSIZE(system_dir));
	if (len == 0 || len >= ARRAYSIZE(system_dir)) return {};

	std::wstring path(system_dir, len);
	path += L"\\OpenSSH\\ssh.exe";
	DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};

	std::u16string_view view(reinterpret_cast<const char16_t *>(path.c_str()), path.size());
	std::string result = convert_utf16_to_utf8(view);
	for (char &c : result) {
		if (c == '\\') c = '/';
	}
	return result;
}

#else
#endif




