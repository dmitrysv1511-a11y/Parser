
#include "Parser.h"
#include "TextUtils.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <fstream>
#include <string>
#include <vector>
#include <iterator>

namespace AbsoluteParser
{
    namespace Parser
    {
        // ====================================================
        // Checks whether a character is part of a word.
        // ====================================================

        static bool IsWordCharacter(wchar_t ch)
        {
            return
                (ch >= L'a' && ch <= L'z') ||
                (ch >= L'A' && ch <= L'Z') ||
                (ch >= L'\u0430' && ch <= L'\u044f') ||
                (ch >= L'\u0410' && ch <= L'\u042f') ||
                ch == L'\u0451' || ch == L'\u0401' ||
                (ch >= L'0' && ch <= L'9') ||
                ch == L'_';
        }

        // ====================================================
        // Searches for a keyword with word boundaries.
        // ====================================================

        static bool ContainsKeyword(
            const std::wstring& text,
            const std::wstring& keyword)
        {
            if (keyword.empty() || keyword.size() > text.size())
                return false;

            for (size_t i = 0; i <= text.size() - keyword.size(); ++i)
            {
                if (CompareStringOrdinal(
                    text.data() + i,
                    static_cast<int>(keyword.size()),
                    keyword.data(),
                    static_cast<int>(keyword.size()),
                    TRUE) != CSTR_EQUAL)
                {
                    continue;
                }

                bool leftBoundary =
                    (i == 0 || !IsWordCharacter(text[i - 1]));

                size_t end = i + keyword.size();

                bool rightBoundary =
                    (end == text.size() ||
                        !IsWordCharacter(text[end]));

                if (leftBoundary && rightBoundary)
                    return true;
            }

            return false;
        }

        // ====================================================
        // Detects announcements awaiting review.
        // ====================================================

        static bool IsReviewAnnouncement(const std::wstring& line)
        {
            static const std::wstring excludedPhrases[] =
            {
                L"\u041e\u0431\u044a\u044f\u0432\u043b\u0435\u043d\u0438\u0435 \u043d\u0430 \u043f\u0440\u043e\u0432\u0435\u0440\u043a\u0443:",
                L"\u041e\u0431\u044c\u044f\u0432\u043b\u0435\u043d\u0438\u0435 \u043d\u0430 \u043f\u0440\u043e\u0432\u0435\u0440\u043a\u0443:"
            };

            for (const auto& phrase : excludedPhrases)
            {
                if (phrase.size() > line.size())
                    continue;

                for (size_t i = 0; i <= line.size() - phrase.size(); ++i)
                {
                    if (CompareStringOrdinal(
                        line.data() + i,
                        static_cast<int>(phrase.size()),
                        phrase.data(),
                        static_cast<int>(phrase.size()),
                        TRUE) == CSTR_EQUAL)
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        static void ProcessLine(
            const std::wstring& line,
            const std::vector<std::wstring>& keywords,
            std::vector<Announcement>& results)
        {
            if (line.empty() || IsReviewAnnouncement(line))
                return;

            for (const auto& keyword : keywords)
            {
                if (ContainsKeyword(line, keyword))
                {
                    Announcement announcement;

                    announcement.text = line;
                    announcement.timestamp = L"";
                    announcement.keyword = keyword;

                    results.push_back(announcement);
                }
            }
        }

        // ====================================================
        // Constructor.
        // ====================================================

        ChatParser::ChatParser()
            : announcements(),
            keywords(),
            watchedFilePath(),
            lastReadPosition(0),
            pendingBytes(),
            isWatching(false),
            errorCallback(),
            errorReported(false)
        {
        }

        // ====================================================
        // Error reporting.
        // ====================================================

        void ChatParser::ReportError(
            const std::wstring& message)
        {
            if (errorReported)
                return;

            errorReported = true;

            if (errorCallback)
                errorCallback(message);
        }

        // ====================================================
        // Detects announcements awaiting review.????????
        // ====================================================

        void ChatParser::SetErrorCallback(
            std::function<void(const std::wstring&)> callback)
        {
            errorCallback = callback;
            errorReported = false;
        }

        // ====================================================
        // Constructor.??? ?????? ?????
        // ====================================================

        bool ChatParser::LoadFile(
            const std::wstring& filePath)
        {
            announcements.clear();

            std::ifstream file(filePath, std::ios::binary);

            if (!file.is_open())
            {
                ReportError(
                    L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u043e\u0442\u043a\u0440\u044b\u0442\u044c \u0444\u0430\u0439\u043b \u0434\u043b\u044f \u043f\u0435\u0440\u0432\u043e\u043d\u0430\u0447\u0430\u043b\u044c\u043d\u043e\u0433\u043e \u0447\u0442\u0435\u043d\u0438\u044f.");

                return false;
            }

            std::string fileData(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());

            if (file.bad())
            {
                file.close();

                ReportError(
                    L"\u041e\u0448\u0438\u0431\u043a\u0430 \u0447\u0442\u0435\u043d\u0438\u044f \u0444\u0430\u0439\u043b\u0430 \u0434\u043b\u044f \u043f\u0435\u0440\u0432\u043e\u043d\u0430\u0447\u0430\u043b\u044c\u043d\u043e\u0433\u043e \u0447\u0442\u0435\u043d\u0438\u044f.");

                return false;
            }

            file.close();

            errorReported = false;

            std::wstring content =
                TextUtils::DecodeText(fileData, 1251);

            size_t position = 0;

            while (position < content.size())
            {
                size_t end = content.find(L'\n', position);

                if (end == std::wstring::npos)
                    end = content.size();

                std::wstring line = content.substr(
                    position,
                    end - position);

                if (!line.empty() && line.back() == L'\r')
                    line.pop_back();

                ProcessLine(line, keywords, announcements);

                position = end + 1;
            }

            return true;
        }

        // ====================================================
        // Detects announcements awaiting review.?? ????
        // ====================================================

        void ChatParser::SetKeywords(
            const std::vector<std::wstring>& newKeywords)
        {
            keywords = newKeywords;
        }

        // ====================================================
        // Detects announcements awaiting review.????
        // ====================================================

        std::vector<Announcement> ChatParser::GetAnnouncements() const
        {
            return announcements;
        }

        // ====================================================
        // Clears parsed announcements.
        // ====================================================

        void ChatParser::Clear()
        {
            announcements.clear();
        }

        // ====================================================
        // Starts watching the file.
        // ====================================================

        bool ChatParser::StartFromEnd(
            const std::wstring& filePath)
        {
            StopWatching();

            errorReported = false;

            std::ifstream file(filePath, std::ios::binary);

            if (!file.is_open())
            {
                ReportError(
                    L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u043e\u0442\u043a\u0440\u044b\u0442\u044c chatlog.txt \u0434\u043b\u044f \u043d\u0430\u0447\u0430\u043b\u0430 \u0441\u043b\u0435\u0436\u0435\u043d\u0438\u044f.");

                return false;
            }

            file.seekg(0, std::ios::end);

            std::streampos position = file.tellg();

            if (position < 0)
            {
                file.close();

                ReportError(
                    L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u043e\u043f\u0440\u0435\u0434\u0435\u043b\u0438\u0442\u044c \u0440\u0430\u0437\u043c\u0435\u0440 chatlog.txt.");

                return false;
            }

            lastReadPosition =
                static_cast<std::uint64_t>(position);

            file.close();

            watchedFilePath = filePath;
            pendingBytes.clear();
            isWatching = true;

            errorReported = false;

            return true;
        }

        // ====================================================
        // Reads newly appended lines.
        // ====================================================

        std::vector<Announcement> ChatParser::ReadNewLines()
        {
            std::vector<Announcement> newAnnouncements;

            if (!isWatching)
                return newAnnouncements;

            // Read only bytes appended since the previous check.

            std::ifstream file(
                watchedFilePath,
                std::ios::binary);

            if (!file.is_open())
            {
                ReportError(
                    L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u043e\u0442\u043a\u0440\u044b\u0442\u044c chatlog.txt \u0432\u043e \u0432\u0440\u0435\u043c\u044f \u043f\u0440\u043e\u0432\u0435\u0440\u043a\u0438.");

                return newAnnouncements;
            }

            file.seekg(0, std::ios::end);

            std::streampos endPosition = file.tellg();

            if (endPosition < 0)
            {
                file.close();

                ReportError(
                    L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u043e\u043f\u0440\u0435\u0434\u0435\u043b\u0438\u0442\u044c \u0442\u0435\u043a\u0443\u0449\u0438\u0439 \u0440\u0430\u0437\u043c\u0435\u0440 \u0444\u0430\u0439\u043b\u0430 chatlog.txt.");

                return newAnnouncements;
            }

            std::uint64_t currentSize =
                static_cast<std::uint64_t>(endPosition);

            // Reset the read position if the file was truncated.

            if (currentSize < lastReadPosition)
            {
                lastReadPosition = 0;
                pendingBytes.clear();
            }

            if (currentSize == lastReadPosition)
            {
                file.close();

                errorReported = false;

                return newAnnouncements;
            }

            file.seekg(
                static_cast<std::streamoff>(lastReadPosition),
                std::ios::beg);

            if (!file)
            {
                file.close();

                ReportError(
                    L"\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u043f\u0435\u0440\u0435\u0439\u0442\u0438 \u043a \u043f\u043e\u0441\u043b\u0435\u0434\u043d\u0435\u0439 \u043f\u043e\u0437\u0438\u0446\u0438\u0438 \u0447\u0442\u0435\u043d\u0438\u044f \u0444\u0430\u0439\u043b\u0430 chatlog.txt.");

                return newAnnouncements;
            }

            std::string newData(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());

            if (file.bad())
            {
                file.close();

                ReportError(
                    L"\u041f\u0440\u043e\u0438\u0437\u043e\u0448\u043b\u0430 \u043e\u0448\u0438\u0431\u043a\u0430 \u043f\u0440\u0438 \u0447\u0442\u0435\u043d\u0438\u0438 chatlog.txt.");

                return newAnnouncements;
            }

            lastReadPosition = currentSize;

            // Read the newly appended portion of the file.

            file.close();

            errorReported = false;

            pendingBytes += newData;

            size_t newlinePosition = 0;

            while ((newlinePosition =
                pendingBytes.find('\n')) != std::string::npos)
            {
                std::string rawLine =
                    pendingBytes.substr(0, newlinePosition);

                pendingBytes.erase(0, newlinePosition + 1);

                if (!rawLine.empty() && rawLine.back() == '\r')
                    rawLine.pop_back();

                std::wstring line =
                    TextUtils::DecodeText(rawLine, 1251);

                std::vector<Announcement> lineResults;

                ProcessLine(
                    line,
                    keywords,
                    lineResults);

                for (const auto& announcement : lineResults)
                {
                    newAnnouncements.push_back(announcement);
                    announcements.push_back(announcement);
                }
            }

            return newAnnouncements;
        }

        // ====================================================
        // Detects announcements awaiting review.????
        // ====================================================

        void ChatParser::StopWatching()
        {
            isWatching = false;

            watchedFilePath.clear();
            pendingBytes.clear();

            lastReadPosition = 0;
        }
    }
}
