
#include "TextUtils.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <cwctype>
#include <limits>

namespace AbsoluteParser
{
    namespace TextUtils
    {
        // ====================================================
        // ВНУТРЕННЯЯ ФУНКЦИЯ ДЕКОДИРОВАНИЯ
        // ====================================================

        static std::wstring DecodeWithCodePage(
            const char* data,
            int length,
            UINT codePage,
            DWORD flags)
        {
            if (data == nullptr || length <= 0)
                return {};

            int requiredSize = MultiByteToWideChar(
                codePage,
                flags,
                data,
                length,
                nullptr,
                0);

            if (requiredSize <= 0)
                return {};

            std::wstring result(
                static_cast<size_t>(requiredSize),
                L'\0');

            int convertedSize = MultiByteToWideChar(
                codePage,
                flags,
                data,
                length,
                result.data(),
                requiredSize);

            if (convertedSize != requiredSize)
                return {};

            return result;
        }

        // ====================================================
        // UTF-16 В UTF-8
        // ====================================================

        std::string WideToUtf8(const std::wstring& text)
        {
            if (text.empty())
                return {};

            if (text.size() >
                static_cast<size_t>(
                    (std::numeric_limits<int>::max)()))
            {
                return {};
            }

            int length = static_cast<int>(text.size());

            int requiredSize = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                text.data(),
                length,
                nullptr,
                0,
                nullptr,
                nullptr);

            if (requiredSize <= 0)
                return {};

            std::string result(
                static_cast<size_t>(requiredSize),
                '\0');

            int convertedSize = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                text.data(),
                length,
                result.data(),
                requiredSize,
                nullptr,
                nullptr);

            if (convertedSize != requiredSize)
                return {};

            return result;
        }

        // ====================================================
        // UTF-8 В UTF-16
        // ====================================================

        std::wstring Utf8ToWide(const std::string& text)
        {
            if (text.empty())
                return {};

            if (text.size() >
                static_cast<size_t>(
                    (std::numeric_limits<int>::max)()))
            {
                return {};
            }

            return DecodeWithCodePage(
                text.data(),
                static_cast<int>(text.size()),
                CP_UTF8,
                MB_ERR_INVALID_CHARS);
        }

        // ====================================================
        // ПРОВЕРКА КОРРЕКТНОСТИ UTF-8
        // ====================================================

        bool IsValidUtf8(const std::string& text)
        {
            if (text.empty())
                return true;

            if (text.size() >
                static_cast<size_t>(
                    (std::numeric_limits<int>::max)()))
            {
                return false;
            }

            int length = static_cast<int>(text.size());

            return MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                text.data(),
                length,
                nullptr,
                0) > 0;
        }

        // ====================================================
        // ДЕКОДИРОВАНИЕ ТЕКСТОВЫХ ДАННЫХ
        // ====================================================

        std::wstring DecodeText(
            const std::string& data,
            unsigned int fallbackCodePage)
        {
            if (data.empty())
                return {};

            size_t offset = 0;

            bool hasUtf8Bom =
                data.size() >= 3 &&
                static_cast<unsigned char>(data[0]) == 0xEF &&
                static_cast<unsigned char>(data[1]) == 0xBB &&
                static_cast<unsigned char>(data[2]) == 0xBF;

            if (hasUtf8Bom)
                offset = 3;

            if (offset >= data.size())
                return {};

            const char* source = data.data() + offset;

            size_t remainingSize = data.size() - offset;

            if (remainingSize >
                static_cast<size_t>(
                    (std::numeric_limits<int>::max)()))
            {
                return {};
            }

            int length = static_cast<int>(remainingSize);

            // Сначала пробуем строгое декодирование UTF-8.

            std::wstring result = DecodeWithCodePage(
                source,
                length,
                CP_UTF8,
                MB_ERR_INVALID_CHARS);

            if (!result.empty())
                return result;

            // При наличии BOM считаем файл UTF-8.
            // Если данные повреждены, не подменяем их CP1251.

            if (hasUtf8Bom)
                return {};

            // Если UTF-8 не подошёл, используем резервную кодировку.

            if (fallbackCodePage == CP_UTF8 ||
                fallbackCodePage == 0)
            {
                return {};
            }

            return DecodeWithCodePage(
                source,
                length,
                fallbackCodePage,
                0);
        }

        // ====================================================
        // УДАЛЕНИЕ ПРОБЕЛОВ ПО КРАЯМ СТРОКИ
        // ====================================================

        std::wstring Trim(const std::wstring& text)
        {
            size_t first = 0;
            size_t last = text.size();

            while (first < last &&
                std::iswspace(text[first]))
            {
                ++first;
            }

            while (last > first &&
                std::iswspace(text[last - 1]))
            {
                --last;
            }

            return text.substr(
                first,
                last - first);
        }

        // ====================================================
        // СРАВНЕНИЕ БЕЗ УЧЁТА РЕГИСТРА
        // ====================================================

        bool EqualIgnoreCase(
            const std::wstring& first,
            const std::wstring& second)
        {
            if (first.size() != second.size())
                return false;

            if (first.empty())
                return true;

            if (first.size() >
                static_cast<size_t>(
                    (std::numeric_limits<int>::max)()))
            {
                return false;
            }

            int length = static_cast<int>(first.size());

            return CompareStringOrdinal(
                first.data(),
                length,
                second.data(),
                length,
                TRUE) == CSTR_EQUAL;
        }

        // ====================================================
        // ПОИСК ПОДСТРОКИ БЕЗ УЧЁТА РЕГИСТРА
        // ====================================================

        bool ContainsIgnoreCase(
            const std::wstring& text,
            const std::wstring& search)
        {
            if (search.empty())
                return true;

            if (search.size() > text.size())
                return false;

            if (search.size() >
                static_cast<size_t>(
                    (std::numeric_limits<int>::max)()))
            {
                return false;
            }

            int searchLength =
                static_cast<int>(search.size());

            for (size_t i = 0;
                i <= text.size() - search.size();
                ++i)
            {
                if (CompareStringOrdinal(
                    text.data() + i,
                    searchLength,
                    search.data(),
                    searchLength,
                    TRUE) == CSTR_EQUAL)
                {
                    return true;
                }
            }

            return false;
        }
    }
}
