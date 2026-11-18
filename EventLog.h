
#pragma once

#include <string>

namespace AbsoluteParser
{
    namespace EventLog
    {
        enum class EventType
        {
            Info,
            Warning,
            Error
        };

        class EventLogger
        {
        public:
            EventLogger();

            // Настройка файла журнала
            bool Initialize(const std::wstring& filePath);

            // Запись информационного события
            void Info(const std::wstring& message);

            // Запись предупреждения
            void Warning(const std::wstring& message);

            // Запись ошибки
            void Error(const std::wstring& message);

            // Очистка журнала
            bool Clear();

            // Получение пути к журналу
            const std::wstring& GetFilePath() const;

        private:
            std::wstring logFilePath;

            // Запись события в файл
            void Write(
                EventType type,
                const std::wstring& message);
        };
    }
}
