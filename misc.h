#ifndef MISC_H
#define MISC_H

#include <string>

namespace misc {

#ifdef _WIN32
std::wstring convert_str_to_wstr(const std::string_view &str);
std::string convert_wstr_to_str(std::wstring const &wstr);
#else
std::u16string convert_str_to_wstr(const std::string_view &str);
std::string convert_wstr_to_str(const std::u16string &wstr);
#endif

std::string_view getProgram(std::string_view cmdline);

std::string find_windows_openssh();

}

#endif // MISC_H
