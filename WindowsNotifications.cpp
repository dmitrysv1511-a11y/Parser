
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "WindowsNotifications.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>

#include <winrt/base.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <cwchar>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "Windowsapp.lib")

namespace AbsoluteParser
{
    namespace WindowsNotifications
    {
        namespace
        {
            constexpr wchar_t APP_USER_MODEL_ID[] =
                L"AbsoluteParser.DesktopApp";

            constexpr wchar_t SHORTCUT_NAME[] =
                L"Absolute Parser.lnk";

            std::mutex g_notificationMutex;

            bool g_initialized = false;
            bool g_apartmentInitialized = false;

            // ====================================================
            // ????????? ???? ? EXE
            // ====================================================

            bool GetExecutablePath(std::wstring& executablePath)
            {
                std::vector<wchar_t> buffer(32768);

                DWORD length = GetModuleFileNameW(
                    nullptr,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()));

                if (length == 0 || length >= buffer.size())
                    return false;

                executablePath.assign(buffer.data(), length);

                return true;
            }

            // ====================================================
            // ????????? ????? EXE
            // ====================================================

            bool GetExecutableDirectory(
                const std::wstring& executablePath,
                std::wstring& directory)
            {
                size_t position =
                    executablePath.find_last_of(L"\\/");

                if (position == std::wstring::npos)
                    return false;

                directory = executablePath.substr(0, position);

                return !directory.empty();
            }

            std::unordered_map<std::wstring, std::wstring> g_emojiCodes;

            bool LoadEmojiCodes()
            {
                if (!g_emojiCodes.empty())
                    return true;

                std::wstring executablePath;
                std::wstring directory;
                if (!GetExecutablePath(executablePath) ||
                    !GetExecutableDirectory(executablePath, directory))
                    return false;

                for (int level = 0; level < 5; ++level)
                {
                    std::ifstream file(directory + L"\\emoji\\emoji.json", std::ios::binary);
                    if (file)
                    {
                        std::string json(
                            (std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
                        size_t position = 0;
                        while ((position = json.find('"', position)) != std::string::npos)
                        {
                            size_t keyEnd = json.find('"', position + 1);
                            size_t colon = keyEnd == std::string::npos
                                ? std::string::npos : json.find(':', keyEnd + 1);
                            size_t valueStart = colon == std::string::npos
                                ? std::string::npos : json.find('"', colon + 1);
                            size_t valueEnd = valueStart == std::string::npos
                                ? std::string::npos : json.find('"', valueStart + 1);
                            if (valueEnd == std::string::npos)
                                break;

                            std::string key = json.substr(position + 1, keyEnd - position - 1);
                            std::string value = json.substr(valueStart + 1, valueEnd - valueStart - 1);
                            int keySize = MultiByteToWideChar(CP_UTF8, 0, key.c_str(), -1, nullptr, 0);
                            int valueSize = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
                            if (keySize > 1 && valueSize > 1)
                            {
                                std::wstring wideKey(keySize, L'\0');
                                std::wstring wideValue(valueSize, L'\0');
                                MultiByteToWideChar(CP_UTF8, 0, key.c_str(), -1, wideKey.data(), keySize);
                                MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wideValue.data(), valueSize);
                                wideKey.pop_back();
                                wideValue.pop_back();
                                g_emojiCodes[wideKey] = wideValue;
                                if (!wideKey.empty() && wideKey.front() == L':')
                                    g_emojiCodes[wideKey.substr(1)] = wideValue;
                            }
                            position = valueEnd + 1;
                        }
                        return !g_emojiCodes.empty();
                    }

                    size_t parent = directory.find_last_of(L"\\/");
                    if (parent == std::wstring::npos)
                        break;
                    directory.resize(parent);
                }
                return false;
            }

            std::wstring EmojiToUnicode(const std::wstring& code)
            {
                std::wstring result;
                size_t start = 0;
                while (start < code.size())
                {
                    size_t end = code.find(L'-', start);
                    std::wstring part = code.substr(start,
                        end == std::wstring::npos ? std::wstring::npos : end - start);
                    wchar_t* parseEnd = nullptr;
                    unsigned long value = wcstoul(part.c_str(), &parseEnd, 16);
                    if (!part.empty() && parseEnd && *parseEnd == L'\0' && value <= 0x10FFFF)
                    {
                        if (value <= 0xFFFF)
                            result.push_back(static_cast<wchar_t>(value));
                        else
                        {
                            value -= 0x10000;
                            result.push_back(static_cast<wchar_t>(0xD800 + (value >> 10)));
                            result.push_back(static_cast<wchar_t>(0xDC00 + (value & 0x3FF)));
                        }
                    }
                    if (end == std::wstring::npos)
                        break;
                    start = end + 1;
                }
                return result;
            }

            std::wstring ReplaceEmojiTags(const std::wstring& message)
            {
                if (!LoadEmojiCodes())
                    return message;

                std::wstring result;
                size_t position = 0;
                while (position < message.size())
                {
                    size_t open = message.find(L'{', position);
                    if (open == std::wstring::npos)
                    {
                        result.append(message, position, std::wstring::npos);
                        break;
                    }
                    result.append(message, position, open - position);
                    size_t close = message.find(L'}', open + 1);
                    if (close == std::wstring::npos)
                    {
                        result.append(message, open, std::wstring::npos);
                        break;
                    }
                    std::wstring alias = message.substr(open + 1, close - open - 1);
                    auto found = g_emojiCodes.find(alias);
                    if (found == g_emojiCodes.end())
                        result.append(message, open, close - open + 1);
                    else
                        result += EmojiToUnicode(found->second);
                    position = close + 1;
                }
                return result;
            }

            // ====================================================
            // ????????????? XML
            // ====================================================

            std::wstring EscapeXml(const std::wstring& text)
            {
                std::wstring result;

                for (wchar_t ch : text)
                {
                    switch (ch)
                    {
                    case L'&':
                        result += L"&amp;";
                        break;

                    case L'<':
                        result += L"&lt;";
                        break;

                    case L'>':
                        result += L"&gt;";
                        break;

                    case L'"':
                        result += L"&quot;";
                        break;

                    case L'\'':
                        result += L"&apos;";
                        break;

                    default:
                        if (ch >= 0x20 || ch == L'\t' ||
                            ch == L'\n' || ch == L'\r')
                        {
                            result += ch;
                        }
                        break;
                    }
                }

                return result;
            }

            // ====================================================
            // ????????? APP USER MODEL ID
            // ====================================================

            bool SetApplicationId()
            {
                HRESULT result =
                    SetCurrentProcessExplicitAppUserModelID(
                        APP_USER_MODEL_ID);

                return SUCCEEDED(result);
            }

            // ====================================================
            // ???????? ??? ?????????? ??????
            // ====================================================

            bool CreateOrUpdateStartMenuShortcut()
            {
                std::wstring executablePath;
                std::wstring executableDirectory;

                if (!GetExecutablePath(executablePath))
                    return false;

                if (!GetExecutableDirectory(
                    executablePath,
                    executableDirectory))
                {
                    return false;
                }

                PWSTR programsPath = nullptr;

                HRESULT result = SHGetKnownFolderPath(
                    FOLDERID_Programs,
                    KF_FLAG_DEFAULT,
                    nullptr,
                    &programsPath);

                if (FAILED(result) || programsPath == nullptr)
                    return false;

                std::wstring shortcutPath = programsPath;

                CoTaskMemFree(programsPath);

                if (!shortcutPath.empty() &&
                    shortcutPath.back() != L'\\')
                {
                    shortcutPath += L'\\';
                }

                shortcutPath += SHORTCUT_NAME;

                IShellLinkW* shellLink = nullptr;

                result = CoCreateInstance(
                    CLSID_ShellLink,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&shellLink));

                if (FAILED(result) || shellLink == nullptr)
                    return false;

                // ?????????, ?????????? ?? ?????.

                if (GetFileAttributesW(shortcutPath.c_str()) !=
                    INVALID_FILE_ATTRIBUTES)
                {
                    IPersistFile* existingFile = nullptr;

                    result = shellLink->QueryInterface(
                        IID_PPV_ARGS(&existingFile));

                    if (FAILED(result) || existingFile == nullptr)
                    {
                        shellLink->Release();
                        return false;
                    }

                    result = existingFile->Load(
                        shortcutPath.c_str(),
                        STGM_READWRITE);

                    existingFile->Release();

                    if (FAILED(result))
                    {
                        shellLink->Release();
                        return false;
                    }
                }

                // ????????????? ???? ? ?????????.

                result = shellLink->SetPath(
                    executablePath.c_str());

                if (SUCCEEDED(result))
                {
                    result = shellLink->SetWorkingDirectory(
                        executableDirectory.c_str());
                }

                if (SUCCEEDED(result))
                {
                    result = shellLink->SetDescription(
                        L"Absolute Parser");
                }

                if (FAILED(result))
                {
                    shellLink->Release();
                    return false;
                }

                // ????????? AppUserModelID ??????.

                IPropertyStore* propertyStore = nullptr;

                result = shellLink->QueryInterface(
                    IID_PPV_ARGS(&propertyStore));

                if (FAILED(result) || propertyStore == nullptr)
                {
                    shellLink->Release();
                    return false;
                }

                PROPVARIANT propertyValue{};
                PropVariantInit(&propertyValue);

                result = InitPropVariantFromString(
                    APP_USER_MODEL_ID,
                    &propertyValue);

                if (SUCCEEDED(result))
                {
                    result = propertyStore->SetValue(
                        PKEY_AppUserModel_ID,
                        propertyValue);
                }

                if (SUCCEEDED(result))
                {
                    result = propertyStore->Commit();
                }

                PropVariantClear(&propertyValue);

                propertyStore->Release();

                if (FAILED(result))
                {
                    shellLink->Release();
                    return false;
                }

                // ????????? ?????.

                IPersistFile* persistFile = nullptr;

                result = shellLink->QueryInterface(
                    IID_PPV_ARGS(&persistFile));

                if (FAILED(result) || persistFile == nullptr)
                {
                    shellLink->Release();
                    return false;
                }

                result = persistFile->Save(
                    shortcutPath.c_str(),
                    TRUE);

                persistFile->Release();
                shellLink->Release();

                return SUCCEEDED(result);
            }

            // ====================================================
            // ?????????????
            // ====================================================

            bool InitializeInternal()
            {
                if (g_initialized)
                    return true;

                if (!g_apartmentInitialized)
                {
                    try
                    {
                        winrt::init_apartment(
                            winrt::apartment_type::single_threaded);

                        g_apartmentInitialized = true;
                    }
                    catch (...)
                    {
                        return false;
                    }
                }

                if (!SetApplicationId())
                    return false;

                if (!CreateOrUpdateStartMenuShortcut())
                    return false;

                try
                {
                    auto notifier =
                        winrt::Windows::UI::Notifications::
                        ToastNotificationManager::
                        CreateToastNotifier(APP_USER_MODEL_ID);

                    if (!notifier)
                        return false;
                }
                catch (...)
                {
                    return false;
                }

                g_initialized = true;

                return true;
            }
        }

        // ========================================================
        // ?????????????
        // ========================================================

        bool Initialize()
        {
            std::lock_guard<std::mutex> lock(
                g_notificationMutex);

            return InitializeInternal();
        }

        // ========================================================
        // ???????? ???????????
        // ========================================================

        bool Show(
            const std::wstring& title,
            const std::wstring& message)
        {
            if (message.empty())
                return false;

            std::lock_guard<std::mutex> lock(
                g_notificationMutex);

            if (!InitializeInternal())
                return false;

            try
            {
                using namespace
                    winrt::Windows::Data::Xml::Dom;

                using namespace
                    winrt::Windows::UI::Notifications;

                std::wstring xml =
                    L"<toast>"
                    L"<visual>"
                    L"<binding template='ToastGeneric'>"
                    L"<text hint-style='title'>" +
                    EscapeXml(ReplaceEmojiTags(message)) +
                    L"</text>"
                    L"</binding>"
                    L"</visual>"
                    L"</toast>";

                XmlDocument document;
                document.LoadXml(xml);

                ToastNotification notification(document);

                auto notifier =
                    ToastNotificationManager::CreateToastNotifier(
                        APP_USER_MODEL_ID);

                notifier.Show(notification);

                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        // ========================================================
        // ???????? ???????????
        // ========================================================

        bool ShowTest()
        {
            return Show(
                L"Absolute Parser",
                L"\u0422\u0435\u0441\u0442\u043e\u0432\u043e\u0435 "
                L"\u0443\u0432\u0435\u0434\u043e\u043c\u043b\u0435\u043d\u0438\u0435. "
                L"\u041f\u0440\u043e\u0432\u0435\u0440\u043a\u0430 "
                L"\u0440\u0430\u0431\u043e\u0442\u044b "
                L"\u0443\u0432\u0435\u0434\u043e\u043c\u043b\u0435\u043d\u0438\u0439!");
        }

        // ========================================================
        // ?????????? ??????
        // ========================================================

        void Shutdown()
        {
            std::lock_guard<std::mutex> lock(
                g_notificationMutex);

            g_initialized = false;

            if (g_apartmentInitialized)
            {
                winrt::uninit_apartment();
                g_apartmentInitialized = false;
            }
        }
    }
}
