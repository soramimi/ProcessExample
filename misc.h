#ifndef MISC_H
#define MISC_H

#include <string>

namespace misc {

std::wstring convert_str_to_wstr(std::string const &str);
std::string convert_wstr_to_str(std::wstring const &wstr);

}

#endif // MISC_H
