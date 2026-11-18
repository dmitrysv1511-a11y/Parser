
#pragma once

#include <string>
#include <vector>

namespace AbsoluteParser
{
    namespace Config
    {
        inline constexpr const wchar_t* APP_NAME = L"Absolute Parser";
        inline constexpr const wchar_t* APP_VERSION = L"2.1";

        inline constexpr int WINDOW_WIDTH = 1200;
        inline constexpr int WINDOW_HEIGHT = 750;

        inline constexpr int CHATLOG_CHECK_INTERVAL_MS = 1000;

        inline constexpr bool START_MINIMIZED = false;
        inline constexpr bool ENABLE_SOUND = true;

        inline constexpr bool ENABLE_TELEGRAM = false;
        inline constexpr bool ENABLE_VK = false;

        // ====================================================
        // ????????? ????????? ?????
        // ====================================================

        struct KeywordEntry
        {
            std::wstring text;
            bool enabled = true;
        };

        // ====================================================
        // ?????? ? ?????????????
        // ====================================================

        std::wstring GetConfigFilePath();

        // ???? ? chatlog.txt

        std::wstring LoadChatlogPath();

        bool SaveChatlogPath(
            const std::wstring& filePath);

        // ???????? ?????

        std::vector<KeywordEntry> LoadKeywords();

        bool SaveKeywords(
            const std::vector<KeywordEntry>& keywords);

        // Windows-???????????

        bool LoadNotificationsEnabled();

        bool SaveNotificationsEnabled(bool enabled);
    }
}
