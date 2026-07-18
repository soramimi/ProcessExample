#include "AbstractProcess.h"

void AbstractPtyProcess::write_output(const char *buf, size_t len)
{
	std::lock_guard<std::mutex> lock(mutex_);
	output_queue_.insert(output_queue_.end(), buf, buf + len);
	output_vector_.insert(output_vector_.end(), buf, buf + len);
}

std::string AbstractPtyProcess::get_message() const // deprecated
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (stdout_bytes_.empty()) return {};
	return std::string(&stdout_bytes_[0], stdout_bytes_.size());
}

void AbstractPtyProcess::clear_message()
{
	std::lock_guard<std::mutex> lock(mutex_);
	output_vector_.clear();
	stdout_bytes_.clear();
}
