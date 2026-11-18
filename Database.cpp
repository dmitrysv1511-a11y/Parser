
#include "Database.h"

namespace AbsoluteParser
{
    namespace Database
    {
        DatabaseManager::DatabaseManager()
            : database(nullptr)
        {
        }

        DatabaseManager::~DatabaseManager()
        {
            Close();
        }

        // ====================================================
        // Œ“ –€“»≈ ¡¿«€ ƒ¿ÕÕ€’
        // ====================================================

        bool DatabaseManager::Open(const std::wstring& filePath)
        {
            Close();

            int result = sqlite3_open16(
                filePath.c_str(),
                &database);

            if (result != SQLITE_OK)
            {
                Close();
                return false;
            }

            const char* createTable =
                "CREATE TABLE IF NOT EXISTS announcements ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "text TEXT NOT NULL,"
                "keyword TEXT NOT NULL,"
                "timestamp TEXT NOT NULL "
                "DEFAULT (datetime('now','localtime'))"
                ");";

            char* errorMessage = nullptr;

            result = sqlite3_exec(
                database,
                createTable,
                nullptr,
                nullptr,
                &errorMessage);

            if (errorMessage)
                sqlite3_free(errorMessage);

            if (result != SQLITE_OK)
            {
                Close();
                return false;
            }

            return true;
        }

        // ====================================================
        // «¿ –€“»≈ ¡¿«€ ƒ¿ÕÕ€’
        // ====================================================

        void DatabaseManager::Close()
        {
            if (database != nullptr)
            {
                sqlite3_close(database);
                database = nullptr;
            }
        }

        // ====================================================
        // —Œ’–¿Õ≈Õ»≈ Œ¡⁄ﬂ¬À≈Õ»ﬂ
        // ====================================================

        bool DatabaseManager::SaveAnnouncement(
            const Parser::Announcement& announcement)
        {
            if (database == nullptr)
                return false;

            const char* sql =
                "INSERT INTO announcements (text, keyword) "
                "VALUES (?, ?);";

            sqlite3_stmt* statement = nullptr;

            int result = sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr);

            if (result != SQLITE_OK)
                return false;

            result = sqlite3_bind_text16(
                statement,
                1,
                announcement.text.c_str(),
                -1,
                SQLITE_TRANSIENT);

            if (result == SQLITE_OK)
            {
                result = sqlite3_bind_text16(
                    statement,
                    2,
                    announcement.keyword.c_str(),
                    -1,
                    SQLITE_TRANSIENT);
            }

            if (result == SQLITE_OK)
                result = sqlite3_step(statement);

            bool success = (result == SQLITE_DONE);

            sqlite3_finalize(statement);

            return success;
        }

        // ====================================================
        // œŒÀ”◊≈Õ»≈ ¬—≈’ Œ¡⁄ﬂ¬À≈Õ»…
        // ====================================================

        std::vector<Parser::Announcement>
            DatabaseManager::GetAnnouncements() const
        {
            std::vector<Parser::Announcement> announcements;

            if (database == nullptr)
                return announcements;

            const char* sql =
                "SELECT text, keyword, timestamp "
                "FROM announcements "
                "ORDER BY id DESC;";

            sqlite3_stmt* statement = nullptr;

            int result = sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr);

            if (result != SQLITE_OK)
                return announcements;

            while (sqlite3_step(statement) == SQLITE_ROW)
            {
                Parser::Announcement announcement;

                const void* text = sqlite3_column_text16(
                    statement, 0);

                const void* keyword = sqlite3_column_text16(
                    statement, 1);

                const void* timestamp = sqlite3_column_text16(
                    statement, 2);

                if (text)
                {
                    announcement.text =
                        static_cast<const wchar_t*>(text);
                }

                if (keyword)
                {
                    announcement.keyword =
                        static_cast<const wchar_t*>(keyword);
                }

                if (timestamp)
                {
                    announcement.timestamp =
                        static_cast<const wchar_t*>(timestamp);
                }

                announcements.push_back(announcement);
            }

            sqlite3_finalize(statement);

            return announcements;
        }

        // ====================================================
        // œŒÀ”◊≈Õ»≈  ŒÀ»◊≈—“¬¿ Œ¡⁄ﬂ¬À≈Õ»…
        // ====================================================

        int DatabaseManager::GetAnnouncementCount() const
        {
            if (database == nullptr)
                return -1;

            const char* sql =
                "SELECT COUNT(*) FROM announcements;";

            sqlite3_stmt* statement = nullptr;

            int result = sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr);

            if (result != SQLITE_OK)
                return -1;

            int count = -1;

            if (sqlite3_step(statement) == SQLITE_ROW)
            {
                count = sqlite3_column_int(statement, 0);
            }

            sqlite3_finalize(statement);

            return count;
        }

        // ====================================================
        // Œ◊»—“ ¿ ¬—≈… »—“Œ–»»
        // ====================================================

        bool DatabaseManager::ClearHistory()
        {
            if (database == nullptr)
                return false;

            const char* sql =
                "DELETE FROM announcements;";

            char* errorMessage = nullptr;

            int result = sqlite3_exec(
                database,
                sql,
                nullptr,
                nullptr,
                &errorMessage);

            if (errorMessage)
                sqlite3_free(errorMessage);

            return result == SQLITE_OK;
        }

        // ====================================================
        // œ–Œ¬≈– ¿ —Œ—“ŒﬂÕ»ﬂ ¡¿«€ ƒ¿ÕÕ€’
        // ====================================================

        bool DatabaseManager::IsOpen() const
        {
            return database != nullptr;
        }
    }
}
