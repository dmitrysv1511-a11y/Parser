
#include "AppConfig.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <cwchar>
#include <string>
#include <vector>

namespace AbsoluteParser
{
    namespace Config
    {
        // ====================================================
        // ПОЛУЧЕНИЕ ПУТИ К CONFIG.INI
        // ====================================================

        std::wstring GetConfigFilePath()
        {
            wchar_t modulePath[MAX_PATH]{};

            DWORD length = GetModuleFileNameW(
                nullptr,
                modulePath,
                MAX_PATH);

            if (length == 0 || length >= MAX_PATH)
                return L"config.ini";

            std::wstring path(modulePath, length);

            size_t position = path.find_last_of(L"\\/");

            if (position != std::wstring::npos)
                path.resize(position + 1);
            else
                path.clear();

            return path + L"config.ini";
        }

        // ====================================================
        // ЗАГРУЗКА ПУТИ К CHATLOG.TXT
        // ====================================================

        std::wstring LoadChatlogPath()
        {
            std::wstring configPath = GetConfigFilePath();

            // Буфер размещается в куче, а не на стеке.

            std::vector<wchar_t> pathBuffer(32768, L'\0');

            GetPrivateProfileStringW(
                L"General",
                L"ChatlogPath",
                L"",
                pathBuffer.data(),
                static_cast<DWORD>(pathBuffer.size()),
                configPath.c_str());

            return std::wstring(pathBuffer.data());
        }

        // ====================================================
        // СОХРАНЕНИЕ ПУТИ К CHATLOG.TXT
        // ====================================================

        bool SaveChatlogPath(
            const std::wstring& filePath)
        {
            std::wstring configPath = GetConfigFilePath();

            return WritePrivateProfileStringW(
                L"General",
                L"ChatlogPath",
                filePath.c_str(),
                configPath.c_str()) != FALSE;
        }

        // ====================================================
        // ЗАГРУЗКА КЛЮЧЕВЫХ СЛОВ
        // ====================================================

        std::vector<KeywordEntry> LoadKeywords()
        {
            std::vector<KeywordEntry> keywords;

            std::wstring configPath = GetConfigFilePath();

            wchar_t countBuffer[32]{};

            GetPrivateProfileStringW(
                L"Keywords",
                L"Count",
                L"",
                countBuffer,
                32,
                configPath.c_str());

            // Если конфигурация ещё не создана,
            // используем стандартные ключевые слова.

            if (countBuffer[0] == L'\0')
            {
                keywords =
                {
                    { L"Ferrari", true },
                    { L"бизнес", true },
                    { L"Оружейную", true }
                };

                SaveKeywords(keywords);

                return keywords;
            }

            wchar_t* end = nullptr;

            long count = std::wcstol(
                countBuffer,
                &end,
                10);

            if (end == countBuffer || count < 0)
                return keywords;

            // Защита от некорректного количества записей.

            if (count > 1000)
                count = 1000;

            for (long i = 0; i < count; ++i)
            {
                std::wstring textKey =
                    L"Text" + std::to_wstring(i);

                std::wstring enabledKey =
                    L"Enabled" + std::to_wstring(i);

                wchar_t textBuffer[1024]{};

                GetPrivateProfileStringW(
                    L"Keywords",
                    textKey.c_str(),
                    L"",
                    textBuffer,
                    1024,
                    configPath.c_str());

                if (textBuffer[0] == L'\0')
                    continue;

                int enabled = GetPrivateProfileIntW(
                    L"Keywords",
                    enabledKey.c_str(),
                    1,
                    configPath.c_str());

                KeywordEntry entry;

                entry.text = textBuffer;
                entry.enabled = (enabled != 0);

                keywords.push_back(entry);
            }

            return keywords;
        }

        // ====================================================
        // СОХРАНЕНИЕ КЛЮЧЕВЫХ СЛОВ
        // ====================================================

        bool SaveKeywords(
            const std::vector<KeywordEntry>& keywords)
        {
            if (keywords.size() > 1000)
                return false;

            std::wstring configPath = GetConfigFilePath();

            int oldCount = GetPrivateProfileIntW(
                L"Keywords",
                L"Count",
                0,
                configPath.c_str());

            if (oldCount < 0)
                oldCount = 0;

            if (oldCount > 1000)
                oldCount = 1000;

            // Сохраняем каждое ключевое слово.

            for (size_t i = 0; i < keywords.size(); ++i)
            {
                std::wstring textKey =
                    L"Text" + std::to_wstring(i);

                std::wstring enabledKey =
                    L"Enabled" + std::to_wstring(i);

                if (!WritePrivateProfileStringW(
                    L"Keywords",
                    textKey.c_str(),
                    keywords[i].text.c_str(),
                    configPath.c_str()))
                {
                    return false;
                }

                if (!WritePrivateProfileStringW(
                    L"Keywords",
                    enabledKey.c_str(),
                    keywords[i].enabled ? L"1" : L"0",
                    configPath.c_str()))
                {
                    return false;
                }
            }

            // Удаляем старые записи, если список уменьшился.

            for (int i = static_cast<int>(keywords.size());
                i < oldCount;
                ++i)
            {
                std::wstring textKey =
                    L"Text" + std::to_wstring(i);

                std::wstring enabledKey =
                    L"Enabled" + std::to_wstring(i);

                WritePrivateProfileStringW(
                    L"Keywords",
                    textKey.c_str(),
                    nullptr,
                    configPath.c_str());

                WritePrivateProfileStringW(
                    L"Keywords",
                    enabledKey.c_str(),
                    nullptr,
                    configPath.c_str());
            }

            // Количество записей сохраняем последним.

            std::wstring count =
                std::to_wstring(keywords.size());

            return WritePrivateProfileStringW(
                L"Keywords",
                L"Count",
                count.c_str(),
                configPath.c_str()) != FALSE;
        }

        // ====================================================
        // ЗАГРУЗКА НАСТРОЙКИ WINDOWS-УВЕДОМЛЕНИЙ
        // ====================================================

        bool LoadNotificationsEnabled()
        {
            std::wstring configPath = GetConfigFilePath();

            int enabled = GetPrivateProfileIntW(
                L"Notifications",
                L"Enabled",
                1,
                configPath.c_str());

            return enabled != 0;
        }

        // ====================================================
        // СОХРАНЕНИЕ НАСТРОЙКИ WINDOWS-УВЕДОМЛЕНИЙ
        // ====================================================

        bool SaveNotificationsEnabled(bool enabled)
        {
            std::wstring configPath = GetConfigFilePath();

            return WritePrivateProfileStringW(
                L"Notifications",
                L"Enabled",
                enabled ? L"1" : L"0",
                configPath.c_str()) != FALSE;
        }
    }
}
