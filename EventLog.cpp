
#include "EventLog.h"
#include "TextUtils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace AbsoluteParser
{
    namespace EventLog
    {
        EventLogger::EventLogger()
        {
        }

        bool EventLogger::Initialize(
            const std::wstring& filePath)
        {
            logFilePath = filePath;

            FILE* file = nullptr;

            if (_wfopen_s(
                &file,
                logFilePath.c_str(),
                L"ab") != 0 || file == nullptr)
            {
                return false;
            }

            fclose(file);

            Info(L"Журнал событий инициализирован.");

            return true;
        }

        void EventLogger::Info(const std::wstring& message)
        {
            Write(EventType::Info, message);
        }

        void EventLogger::Warning(const std::wstring& message)
        {
            Write(EventType::Warning, message);
        }

        void EventLogger::Error(const std::wstring& message)
        {
            Write(EventType::Error, message);
        }

        void EventLogger::Write(
            EventType type,
            const std::wstring& message)
        {
            if (logFilePath.empty())
                return;

            FILE* file = nullptr;

            if (_wfopen_s(
                &file,
                logFilePath.c_str(),
                L"ab") != 0 || file == nullptr)
            {
                return;
            }

            std::time_t currentTime = std::time(nullptr);

            std::tm localTime{};

            localtime_s(&localTime, &currentTime);

            std::ostringstream timestamp;

            timestamp
                << std::put_time(
                    &localTime,
                    "%d.%m.%Y %H:%M:%S");

            const char* typeName = "INFO";

            switch (type)
            {
            case EventType::Info:
                typeName = "INFO";
                break;

            case EventType::Warning:
                typeName = "WARNING";
                break;

            case EventType::Error:
                typeName = "ERROR";
                break;
            }

            std::string utf8Message =
                TextUtils::WideToUtf8(message);

            std::string logEntry =
                "[" + timestamp.str() + "] [" +
                typeName + "] " +
                utf8Message + "\r\n";

            fwrite(
                logEntry.data(),
                1,
                logEntry.size(),
                file);

            fclose(file);
        }

        bool EventLogger::Clear()
        {
            if (logFilePath.empty())
                return false;

            FILE* file = nullptr;

            if (_wfopen_s(
                &file,
                logFilePath.c_str(),
                L"wb") != 0 || file == nullptr)
            {
                return false;
            }

            fclose(file);

            Info(L"Журнал событий очищен.");

            return true;
        }

        const std::wstring& EventLogger::GetFilePath() const
        {
            return logFilePath;
        }
    }
}
