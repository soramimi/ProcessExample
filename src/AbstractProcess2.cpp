#include "AbstractProcess2.h"





std::string AbstractPtyProcess::get_message() const // deprecated
{
	if (stdout_bytes_.empty()) return {};
	return std::string(&stdout_bytes_[0], stdout_bytes_.size());
}

void AbstractPtyProcess::clear_message()
{
	output_vector_.clear();
	stdout_bytes_.clear();
}
