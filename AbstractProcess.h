#ifndef ABSTRACTPROCESS_H
#define ABSTRACTPROCESS_H

// #include <QObject>
// #include <QVariant>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>

class QString;

class AbstractProcess {
public:
	virtual ~AbstractProcess() {}

	virtual void start(const std::string &command, bool use_input) = 0;
	virtual int wait() = 0;
	virtual void stop() = 0;
	virtual bool isRunning() const = 0;
	virtual int getExitCode() const = 0;
	virtual void writeInput(char const *ptr, int len) = 0;

	virtual void closeInput(bool justnow) = 0;

	virtual std::vector<char> const &stdout_bytes() const = 0;
	virtual std::vector<char> const &stderr_bytes() const = 0;
};


class AbstractPtyProcess {
protected:
	std::mutex mutex_;
	std::condition_variable cond_;
#ifdef QT_VERSION
	QString change_dir_;
	QVariant user_data_;
	std::function<void (bool, const QVariant &)> completed_fn_;
#endif
	std::deque<char> output_queue_; // for log
	std::vector<char> output_vector_; // for result
	std::vector<char> stdout_bytes_;
	std::vector<char> stderr_bytes_;
	void writeOutput(char const *buf, size_t len)
	{
		output_queue_.insert(output_queue_.end(), buf, buf + len);
		output_vector_.insert(output_vector_.end(), buf, buf + len);
	}
public:
	virtual ~AbstractPtyProcess() {}

	void setChangeDir(QString const &dir);
#ifdef QT_VERSION
	// void setCompletedHandler(std::function<void (bool, const QVariant &)> fn, QVariant const &userdata)
	// {
	// 	completed_fn_ = fn;
	// 	user_data_ = userdata;
	// }
#endif

	void notifyCompleted()
	{
#ifdef QT_VERSION
		if (completed_fn_) {
			completed_fn_(true, user_data_);
		}
#endif
	}

	std::string getMessage() const; // deprecated
	void clearMessage();

	std::vector<char> const &stdout_bytes() const
	{
		return stdout_bytes_;
	}
	std::vector<char> const &stderr_bytes() const
	{
		return stderr_bytes_;
	}

	virtual void start(std::string const &cmd, std::string const &env, bool use_input) = 0;
	virtual bool wait(unsigned long time = ULONG_MAX) = 0;
	virtual void stop() = 0;
	virtual bool isRunning() const = 0;
	virtual int getExitCode() const = 0;
	virtual void writeInput(char const *ptr, int len) = 0;
	virtual int readOutput(char *ptr, int len) = 0;
};

#endif // ABSTRACTPROCESS_H
