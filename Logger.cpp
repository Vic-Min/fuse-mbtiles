#include "Logger.h"

#include <cassert>


#ifdef USE_LOGGER_P7

Logger::Logger(Level level, const std::string& str)
	: level_(level)
{
	P7_Set_Crash_Handler();
	
	client_ = P7_Create_Client(str.empty() ? "/P7.Sink=Auto" : str.c_str());

	trace = client_ ? P7_Create_Trace(client_, TM("T0")) : nullptr;
};

Logger::~Logger()
{
	if (trace)
	{
		// Для избежания гонки здесь надо бы использовать мьютекс
		// или, возможно, std::shared_mutex (C++17),
		// и закрыть этим мьютексом ВСЕ обращения к trace.
		// Но это существенно снизит производительность логгера.
		// Сам логгер многопоточный и опасность гонки есть только здесь - при его уничтожении.
		// Но это происходит только при перезапуске программы и вреда особого не принесёт.

		auto tmp = trace;
		trace = nullptr;
		tmp->Release();
	}

	if (client_)
		client_->Release();
};

#else

Logger::Logger(Level level, const std::string& str)
{
	level_ = level;

	stream = fopen(str.c_str(), "w+");
	if (stream == NULL)
	{
		fprintf(stderr, "can't open file %s\n", str.c_str());
	}
};

Logger::~Logger()
{
	if (stream)
		fclose(stream);
};

#endif

std::unique_ptr <Logger> logger;
