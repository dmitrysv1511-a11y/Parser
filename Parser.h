#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace AbsoluteParser
{
    namespace Parser
    {
        struct Announcement
        {
            std::wstring text;
            std::wstring timestamp;
            std::wstring keyword;
        };

        class ChatParser
        {
        public:
            ChatParser();

            // Первоначальное чтение файла
            bool LoadFile(const std::wstring& filePath);

            // Настройка ключевых слов
            void SetKeywords(
                const std::vector<std::wstring>& newKeywords);

            // Получение найденных объявлений
            std::vector<Announcement> GetAnnouncements() const;

            // Очистка результатов
            void Clear();

            // Запоминаем конец файла при запуске
            bool StartFromEnd(const std::wstring& filePath);

            // Открываем, читаем новые строки и закрываем файл
            std::vector<Announcement> ReadNewLines();

            // Остановка наблюдения
            void StopWatching();

            // Настройка передачи ошибок в журнал
            void SetErrorCallback(
                std::function<void(const std::wstring&)> callback);

        private:
            std::vector<Announcement> announcements;
            std::vector<std::wstring> keywords;

            std::wstring watchedFilePath;

            std::uint64_t lastReadPosition = 0;

            std::string pendingBytes;

            bool isWatching = false;

            // Функция передачи ошибки
            std::function<void(const std::wstring&)> errorCallback;

            // Защита от повторной записи одной ошибки
            bool errorReported = false;

            void ReportError(const std::wstring& message);
        };
    }
}