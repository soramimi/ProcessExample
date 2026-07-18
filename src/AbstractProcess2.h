#ifndef ABSTRACTPROCESS2_H
#define ABSTRACTPROCESS2_H

#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "ProcessHelper.h"

#ifdef APP_GUITAR
#include <QObject>
#include <QVariant>
#endif


class QString;

class AbstractProcess {
public:
	virtual ~AbstractProcess() {}

	virtual void start(const std::string &command, bool use_input) = 0;
	virtual int wait() = 0;
	virtual void stop() = 0;
	virtual bool is_running() const = 0;
	virtual int get_exit_code() const = 0;
	virtual void write_input(char const *ptr, int len) = 0;

	virtual void close_input() = 0;

	virtual std::vector<char> const &stdout_bytes() const = 0;
	virtual std::vector<char> const &stderr_bytes() const = 0;

};


class AbstractPtyProcess {
protected:
	std::mutex mutex_;
	std::condition_variable cond_;

	process::helper::dir_string_t change_dir_;

	std::shared_ptr<void> user_data_;
	std::function<void (bool, std::shared_ptr<void>)> completed_fn_;

	std::deque<char> output_queue_; // for log
	std::vector<char> output_vector_; // for result
	std::vector<char> stdout_bytes_;
	std::vector<char> stderr_bytes_;
	void write_output(char const *buf, size_t len)
	{
		output_queue_.insert(output_queue_.end(), buf, buf + len);
		output_vector_.insert(output_vector_.end(), buf, buf + len);
	}
public:
	virtual ~AbstractPtyProcess() {}

	void set_change_dir(process::helper::dir_string_t const &dir)
	{
		change_dir_ = dir;
	}

	void set_completion_callback(std::function<void (bool, std::shared_ptr<void>)> fn, std::shared_ptr<void> userdata)
	{
		completed_fn_ = fn;
		user_data_ = userdata;
	}

	void notify_completed()
	{
		if (completed_fn_) {
			completed_fn_(true, user_data_);
		}
	}

	std::string get_message() const; // deprecated
	void clear_message();

	std::vector<char> const &stdout_bytes() const
	{
		return stdout_bytes_;
	}
	std::vector<char> const &stderr_bytes() const
	{
		return stderr_bytes_;
	}

	virtual void start(std::string const &cmd, std::string const &env, bool use_input) = 0;
	virtual int wait() = 0;
	virtual void stop() = 0;
	virtual bool is_running() const = 0;
	virtual int get_exit_code() const = 0;
	virtual void write_input(char const *ptr, int len) = 0;
	virtual int read_output(char *ptr, int len) = 0;
	virtual void close_input() = 0;

};

#endif // ABSTRACTPROCESS2_H
