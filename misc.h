#ifndef MISC_H
#define MISC_H

#include <string>

namespace misc {

std::wstring convert_str_to_wstr(const std::string_view &str);
std::string convert_wstr_to_str(std::wstring const &wstr);

std::string_view getProgram(std::string_view cmdline);

}

#endif // MISC_H
