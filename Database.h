
#pragma once

#include <string>
#include <vector>

#include "Parser.h"
#include "sqlite3.h"

namespace AbsoluteParser
{
    namespace Database
    {
        class DatabaseManager
        {
        public:
            DatabaseManager();
            ~DatabaseManager();

            // Открытие базы данных
            bool Open(const std::wstring& filePath);

            // Закрытие базы данных
            void Close();

            // Сохранение объявления
            bool SaveAnnouncement(
                const Parser::Announcement& announcement);

            // Получение всех объявлений
            std::vector<Parser::Announcement> GetAnnouncements() const;

            // Получение количества объявлений
            int GetAnnouncementCount() const;

            // Очистка всей истории
            bool ClearHistory();

            // Проверка подключения к базе данных
            bool IsOpen() const;

        private:
            sqlite3* database;
        };
    }
}
