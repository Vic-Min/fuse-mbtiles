#pragma once

#ifdef USE_LOGGER

#include <string>
#include <memory>
#ifdef USE_LOGGER_P7
#include <P7_Client.h>
#include <P7_Trace.h>
#else
#include <stdio.h>
#include <stdarg.h>
#endif


class Logger
{
public:
	enum Level
	{
		LEVEL_OFF,
		LEVEL_ERROR,
		LEVEL_WARNING,
		LEVEL_DEBUG,
		LEVEL_TRACE,

		LEVEL_MAX
	};

	Logger(Level level, const std::string&);
	~Logger();

#ifdef USE_LOGGER_P7
	bool on(Level level)const
	{
		return trace && level <= level_;
	}

	IP7_Trace* trace;

#else
	bool on(Level level)const
	{
		return stream != nullptr && level <= level_;
	}

	template<typename... Args>
	void write(Level level, const char *format, const Args&... args)
	{
		if (stream && level <= level_)
		{
			fprintf(stream, format, args...);
			fprintf(stream, "\n");
		}
	};
#endif

private:
	Level level_;
#ifdef USE_LOGGER_P7
	IP7_Client* client_;
#else
	FILE *stream;
#endif
};

extern std::unique_ptr <Logger> logger;


#ifdef USE_LOGGER_P7

#define LOG_DELIVER(level, p7Level, ...) if (logger && logger->on(level)) logger->trace->P7_DELIVER(0, (p7Level), nullptr, __VA_ARGS__)
#define LOG_ERROR(...)    LOG_DELIVER(Logger::LEVEL_ERROR,   EP7TRACE_LEVEL_ERROR   , __VA_ARGS__)
#define LOG_WARNING(...)  LOG_DELIVER(Logger::LEVEL_WARNING, EP7TRACE_LEVEL_WARNING , __VA_ARGS__)
#define LOG_DEBUG(...)    LOG_DELIVER(Logger::LEVEL_DEBUG,   EP7TRACE_LEVEL_DEBUG   , __VA_ARGS__)
#define LOG_TRACE(...)    LOG_DELIVER(Logger::LEVEL_TRACE,   EP7TRACE_LEVEL_TRACE   , __VA_ARGS__)

#else

template<typename... Args>
void LOG_DELIVER(Logger::Level level, const char *format, const Args&... args)
{
	if (logger && logger->on(level))
		logger->write(level, format, args...);
}

template<typename... Args>
void LOG_ERROR  (const char *format, const Args&... args)
{
	LOG_DELIVER(Logger::LEVEL_ERROR, (format), args...);
}

template<typename... Args>
void LOG_WARNING  (const char *format, const Args&... args)
{
	LOG_DELIVER(Logger::LEVEL_WARNING, (format), args...);
}

template<typename... Args>
void LOG_DEBUG  (const char *format, const Args&... args)
{
	LOG_DELIVER(Logger::LEVEL_DEBUG, (format), args...);
}

template<typename... Args>
void LOG_TRACE  (const char *format, const Args&... args)
{
	LOG_DELIVER(Logger::LEVEL_TRACE, (format), args...);
}

#endif

#else

#define LOG_ERROR(...)
#define LOG_WARNING(...)
#define LOG_DEBUG(...)
#define LOG_TRACE(...)

#endif //USE_LOGGER

