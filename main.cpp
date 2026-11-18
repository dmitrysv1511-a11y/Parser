
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>

#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <wincodec.h>

#include "AppConfig.h"
#include "Parser.h"
#include "Database.h"
#include "EventLog.h"
#include "TextUtils.h"
#include "WindowsNotifications.h"
#include "resource.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam);

#include "backends/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")

// ============================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ============================================================

static AbsoluteParser::Parser::ChatParser g_parser;
static AbsoluteParser::Database::DatabaseManager g_database;
static AbsoluteParser::EventLog::EventLogger g_eventLogger;

static std::vector<AbsoluteParser::Parser::Announcement> g_history;
static std::vector<AbsoluteParser::Config::KeywordEntry> g_keywords;

static UINT_PTR g_timerId = 1;
static int g_totalAnnouncements = 0;

static int g_currentPage = 0;
static bool g_parserRunning = false;

static HWND g_hwnd = nullptr;
static constexpr UINT WM_APP_TRAY = WM_APP + 1;
static constexpr UINT WM_APP_EXIT = WM_APP + 2;
static constexpr UINT TRAY_ICON_ID = 1;
static bool g_trayIconAdded = false;
static bool g_exitRequested = false;

static std::wstring g_chatlogPath;
static std::wstring g_chatlogStatus;

// Настройки Windows-уведомлений

static bool g_notificationsEnabled = true;
static bool g_notificationsAvailable = false;
static std::wstring g_notificationsStatus;

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static std::unordered_map<std::string, std::string> g_emojiCodes;
static std::unordered_map<std::string, ID3D11ShaderResourceView*> g_emojiTextures;
static std::wstring g_emojiRoot;
static IWICImagingFactory* g_wicFactory = nullptr;
static ImFont* g_fontRegular = nullptr;
static ImFont* g_fontSemibold = nullptr;
static int g_theme = 0; // 0 — текущая синяя, 1 — светлая, 2 — чёрная

static UINT g_resizeWidth = 0;
static UINT g_resizeHeight = 0;

// Поля страницы ключевых слов

static char g_newKeywordBuffer[512]{};
static char g_keywordSearchBuffer[512]{};

static std::wstring g_keywordStatus;

// Поля страницы истории

static char g_historySearchBuffer[1024]{};
static std::wstring g_historyStatus;
static std::wstring g_selectedAnnouncementText;
static std::wstring g_copyStatus;

// ============================================================
// ПРОВЕРКА ДУБЛИКАТА
// ============================================================

static bool KeywordExists(const std::wstring& text)
{
    for (const auto& keyword : g_keywords)
    {
        if (AbsoluteParser::TextUtils::EqualIgnoreCase(
            keyword.text,
            text))
        {
            return true;
        }
    }

    return false;
}

static bool AnnouncementMatchesHistorySearch(
    const AbsoluteParser::Parser::Announcement& announcement,
    const std::wstring& searchText)
{
    size_t position = 0;

    while (position < searchText.size())
    {
        while (position < searchText.size() &&
            iswspace(searchText[position]))
        {
            ++position;
        }

        size_t tokenStart = position;

        while (position < searchText.size() &&
            !iswspace(searchText[position]))
        {
            ++position;
        }

        if (tokenStart == position)
            continue;

        std::wstring token = searchText.substr(
            tokenStart,
            position - tokenStart);

        bool tokenFound =
            AbsoluteParser::TextUtils::ContainsIgnoreCase(
                announcement.text,
                token) ||
            AbsoluteParser::TextUtils::ContainsIgnoreCase(
                announcement.keyword,
                token);

        if (!tokenFound)
            return false;
    }

    return true;
}

// ============================================================
// ФУНКЦИИ ВЫВОДА ТЕКСТА IMGUI
// ============================================================

static void UiText(const wchar_t* text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(
            text ? text : L"");

    ImGui::SetWindowFontScale(1.08f);
    ImGui::TextUnformatted(utf8.c_str());
    ImGui::SetWindowFontScale(1.0f);
}

static void UiText(const std::wstring& text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(text);

    ImGui::SetWindowFontScale(1.03f);
    ImGui::TextUnformatted(utf8.c_str());
    ImGui::SetWindowFontScale(1.0f);
}

static void UiTextEmphasis(const wchar_t* text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(
            text ? text : L"");

    if (g_fontSemibold)
        ImGui::PushFont(g_fontSemibold);

    ImGui::TextUnformatted(utf8.c_str());

    if (g_fontSemibold)
        ImGui::PopFont();
}

static void UiTextSection(const wchar_t* text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(
            text ? text : L"");

    if (g_fontSemibold)
        ImGui::PushFont(g_fontSemibold);

    ImGui::TextUnformatted(utf8.c_str());

    if (g_fontSemibold)
        ImGui::PopFont();
}

static void UiTextDisabled(const wchar_t* text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(
            text ? text : L"");

    ImGui::TextDisabled("%s", utf8.c_str());
}

static void UiTextWrapped(const wchar_t* text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(
            text ? text : L"");

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(utf8.c_str());
    ImGui::PopTextWrapPos();
}

static void UiBulletText(const wchar_t* text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(
            text ? text : L"");

    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextUnformatted(utf8.c_str());
}

static void UiTextColored(
    const ImVec4& color,
    const wchar_t* text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(
            text ? text : L"");

    ImGui::TextColored(
        color,
        "%s",
        utf8.c_str());
}

static void UiTextColored(
    const ImVec4& color,
    const std::wstring& text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(text);

    ImGui::TextColored(
        color,
        "%s",
        utf8.c_str());
}

// Кнопка с русским текстом

static bool UiButton(
    const wchar_t* text,
    const ImVec2& size = ImVec2(0.0f, 0.0f))
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(
            text ? text : L"");

    return ImGui::Button(utf8.c_str(), size);
}

// Маленькая кнопка с русским текстом

static bool UiSmallButton(const wchar_t* text)
{
    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(
            text ? text : L"");

    return ImGui::SmallButton(utf8.c_str());
}

static bool InitializeEmojiSupport()
{
    wchar_t executablePath[MAX_PATH]{};
    DWORD pathLength = GetModuleFileNameW(
        nullptr,
        executablePath,
        ARRAYSIZE(executablePath));

    if (pathLength == 0 || pathLength >= ARRAYSIZE(executablePath))
        return false;

    std::wstring directory(executablePath, pathLength);
    size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return false;

    directory.resize(slash + 1);

    // Visual Studio places the executable under x64\\Debug or x64\\Release,
    // while the emoji folder normally stays beside the project file.
    std::wstring searchDirectory = directory;
    std::wstring jsonPath;

    for (int level = 0; level < 5; ++level)
    {
        std::wstring candidate = searchDirectory + L"emoji\\emoji.json";
        DWORD attributes = GetFileAttributesW(candidate.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            g_emojiRoot = searchDirectory + L"emoji\\";
            jsonPath = candidate;
            break;
        }

        size_t parentSlash = searchDirectory.find_last_of(
            L"\\/",
            searchDirectory.length() > 1
            ? searchDirectory.length() - 2
            : 0);
        if (parentSlash == std::wstring::npos)
            break;
        searchDirectory.resize(parentSlash + 1);
    }

    if (jsonPath.empty())
        return false;

    std::ifstream file(jsonPath, std::ios::binary);
    if (!file)
        return false;

    std::string json(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    size_t position = 0;
    while ((position = json.find('"', position)) != std::string::npos)
    {
        size_t keyEnd = json.find('"', position + 1);
        if (keyEnd == std::string::npos)
            break;

        std::string key = json.substr(position + 1, keyEnd - position - 1);
        size_t colon = json.find(':', keyEnd + 1);
        if (colon == std::string::npos)
            break;

        size_t valueStart = json.find('"', colon + 1);
        if (valueStart == std::string::npos)
            break;

        size_t valueEnd = json.find('"', valueStart + 1);
        if (valueEnd == std::string::npos)
            break;

        std::string code = json.substr(
            valueStart + 1,
            valueEnd - valueStart - 1);

        if (!key.empty() && !code.empty())
        {
            g_emojiCodes[key] = code;
            if (key.front() == ':')
                g_emojiCodes[key.substr(1)] = code;
        }

        position = valueEnd + 1;
    }

    return !g_emojiCodes.empty();
}

static ID3D11ShaderResourceView* LoadEmojiTexture(
    const std::string& code)
{
    auto cached = g_emojiTextures.find(code);
    if (cached != g_emojiTextures.end())
        return cached->second;

    if (!g_wicFactory && FAILED(CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&g_wicFactory))))
    {
        return nullptr;
    }

    std::wstring path = g_emojiRoot + L"36\\";
    for (char ch : code)
        path.push_back(static_cast<wchar_t>(ch));
    path += L".png";

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(g_wicFactory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder)))
    {
        g_emojiTextures.emplace(code, nullptr);
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11ShaderResourceView* texture = nullptr;

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(frame->GetSize(&width, &height)) &&
        width > 0 && height > 0 && width <= 256 && height <= 256 &&
        SUCCEEDED(g_wicFactory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom)))
    {
        std::vector<unsigned char> pixels(
            static_cast<size_t>(width) * height * 4);

        if (SUCCEEDED(converter->CopyPixels(
            nullptr,
            width * 4,
            static_cast<UINT>(pixels.size()),
            pixels.data())))
        {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = width;
            description.Height = height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_IMMUTABLE;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = pixels.data();
            data.SysMemPitch = width * 4;

            ID3D11Texture2D* bitmap = nullptr;
            if (SUCCEEDED(g_pd3dDevice->CreateTexture2D(
                &description,
                &data,
                &bitmap)))
            {
                g_pd3dDevice->CreateShaderResourceView(
                    bitmap,
                    nullptr,
                    &texture);
                bitmap->Release();
            }
        }
    }

    if (converter)
        converter->Release();
    if (frame)
        frame->Release();
    decoder->Release();

    g_emojiTextures.emplace(code, texture);
    return texture;
}

static void DrawTextWithEmoji(const std::string& text)
{
    size_t position = 0;

    while (position < text.size())
    {
        size_t open = text.find('{', position);
        if (open == std::string::npos)
        {
            ImGui::TextUnformatted(text.c_str() + position);
            break;
        }

        if (open > position)
        {
            std::string segment = text.substr(position, open - position);
            ImGui::TextUnformatted(segment.c_str());
            ImGui::SameLine(0.0f, 0.0f);
        }

        size_t close = text.find('}', open + 1);
        if (close == std::string::npos)
        {
            ImGui::TextUnformatted(text.c_str() + open);
            break;
        }

        std::string alias = text.substr(open + 1, close - open - 1);
        std::string normalized = alias;
        std::transform(
            normalized.begin(), normalized.end(),
            normalized.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        auto mapped = g_emojiCodes.find(normalized);
        ID3D11ShaderResourceView* texture = mapped == g_emojiCodes.end()
            ? nullptr
            : LoadEmojiTexture(mapped->second);

        if (texture)
        {
            float size = ImGui::GetTextLineHeight();
            ImGui::Image(texture, ImVec2(size, size));
            ImGui::SameLine(0.0f, 0.0f);
        }
        else
        {
            std::string literal = text.substr(open, close - open + 1);
            ImGui::TextUnformatted(literal.c_str());
            ImGui::SameLine(0.0f, 0.0f);
        }

        position = close + 1;
    }
}

static bool IsAnnouncementWordCharacter(wchar_t ch)
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

static void DrawHistoryAnnouncementText(const std::string& utf8Text)
{
    const std::wstring text =
        AbsoluteParser::TextUtils::Utf8ToWide(utf8Text);

    const std::wstring sellWord = L"\u041f\u0440\u043e\u0434\u0430\u043c";
    const std::wstring buyWord = L"\u041a\u0443\u043f\u043b\u044e";

    size_t position = 0;
    size_t plainStart = 0;

    while (position < text.size())
    {
        const std::wstring* matchedWord = nullptr;
        const ImVec4* matchedColor = nullptr;

        for (size_t candidate = position; candidate < text.size(); ++candidate)
        {
            bool leftBoundary =
                candidate == 0 ||
                !IsAnnouncementWordCharacter(text[candidate - 1]);

            if (!leftBoundary)
                continue;

            if (candidate + sellWord.size() <= text.size() &&
                CompareStringOrdinal(
                    text.data() + candidate,
                    static_cast<int>(sellWord.size()),
                    sellWord.data(),
                    static_cast<int>(sellWord.size()),
                    TRUE) == CSTR_EQUAL &&
                (candidate + sellWord.size() == text.size() ||
                    !IsAnnouncementWordCharacter(
                        text[candidate + sellWord.size()])))
            {
                position = candidate;
                matchedWord = &sellWord;
                static const ImVec4 mutedRed(0.78f, 0.34f, 0.33f, 1.0f);
                matchedColor = &mutedRed;
                break;
            }

            if (candidate + buyWord.size() <= text.size() &&
                CompareStringOrdinal(
                    text.data() + candidate,
                    static_cast<int>(buyWord.size()),
                    buyWord.data(),
                    static_cast<int>(buyWord.size()),
                    TRUE) == CSTR_EQUAL &&
                (candidate + buyWord.size() == text.size() ||
                    !IsAnnouncementWordCharacter(
                        text[candidate + buyWord.size()])))
            {
                position = candidate;
                matchedWord = &buyWord;
                static const ImVec4 mutedGreen(0.30f, 0.70f, 0.48f, 1.0f);
                matchedColor = &mutedGreen;
                break;
            }
        }

        if (!matchedWord)
            break;

        if (position > plainStart)
        {
            DrawTextWithEmoji(
                AbsoluteParser::TextUtils::WideToUtf8(
                    text.substr(plainStart, position - plainStart)));
            ImGui::SameLine(0.0f, 0.0f);
        }

        const std::string coloredWord =
            AbsoluteParser::TextUtils::WideToUtf8(*matchedWord);
        ImGui::TextColored(*matchedColor, "%s", coloredWord.c_str());
        ImGui::SameLine(0.0f, 0.0f);

        position += matchedWord->size();
        plainStart = position;
    }

    if (plainStart < text.size())
    {
        DrawTextWithEmoji(
            AbsoluteParser::TextUtils::WideToUtf8(
                text.substr(plainStart)));
    }
}

static void CleanupEmojiSupport()
{
    for (auto& entry : g_emojiTextures)
    {
        if (entry.second)
            entry.second->Release();
    }
    g_emojiTextures.clear();

    if (g_wicFactory)
    {
        g_wicFactory->Release();
        g_wicFactory = nullptr;
    }
}

static bool AddTrayIcon(HWND hwnd)
{
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = hwnd;
    icon.uID = TRAY_ICON_ID;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = WM_APP_TRAY;
    icon.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!icon.hIcon)
        icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(icon.szTip, L"Absolute Parser");

    g_trayIconAdded = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
    if (g_trayIconAdded)
    {
        icon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &icon);
    }

    return g_trayIconAdded;
}

static void RemoveTrayIcon()
{
    if (!g_trayIconAdded)
        return;

    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = g_hwnd;
    icon.uID = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    g_trayIconAdded = false;
}

static void RestoreMainWindow()
{
    if (!g_hwnd)
        return;

    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

static void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;

    AppendMenuW(menu, MF_STRING, 1, L"Развернуть");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, L"Закрыть");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);

    UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        cursor.x,
        cursor.y,
        0,
        hwnd,
        nullptr);

    DestroyMenu(menu);

    if (command == 1)
        RestoreMainWindow();
    else if (command == 2)
    {
        g_exitRequested = true;
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

// ============================================================
// ПРИМЕНЕНИЕ КЛЮЧЕВЫХ СЛОВ К ПАРСЕРУ
// ============================================================

static void ApplyKeywords()
{
    std::vector<std::wstring> activeKeywords;

    for (const auto& keyword : g_keywords)
    {
        if (keyword.enabled && !keyword.text.empty())
        {
            activeKeywords.push_back(keyword.text);
        }
    }

    g_parser.SetKeywords(activeKeywords);
}

// ============================================================
// СОХРАНЕНИЕ НАСТРОЕК КЛЮЧЕВЫХ СЛОВ
// ============================================================

static bool SaveKeywordSettings()
{
    ApplyKeywords();

    bool result =
        AbsoluteParser::Config::SaveKeywords(g_keywords);

    if (result)
    {
        g_keywordStatus = L"Изменения успешно сохранены.";
    }
    else
    {
        g_keywordStatus =
            L"Ошибка сохранения config.ini!";
    }

    return result;
}

// ============================================================
// СОХРАНЕНИЕ НАСТРОЙКИ WINDOWS-УВЕДОМЛЕНИЙ
// ============================================================

static void SaveNotificationSettings()
{
    if (!AbsoluteParser::Config::SaveNotificationsEnabled(
        g_notificationsEnabled))
    {
        g_notificationsStatus =
            L"Не удалось сохранить настройки уведомлений.";

        g_eventLogger.Error(
            L"Не удалось сохранить настройку Windows-уведомлений.");

        return;
    }

    if (g_notificationsEnabled)
    {
        if (g_notificationsAvailable)
        {
            g_notificationsStatus =
                L"Windows-уведомления включены.";
        }
        else
        {
            g_notificationsStatus =
                L"Настройка сохранена, но уведомления недоступны.";
        }

        g_eventLogger.Info(
            L"Windows-уведомления включены.");
    }
    else
    {
        g_notificationsStatus =
            L"Windows-уведомления отключены.";

        g_eventLogger.Info(
            L"Windows-уведомления отключены.");
    }
}

// ============================================================
// ДОБАВЛЕНИЕ КЛЮЧЕВОГО СЛОВА
// ============================================================

static void AddKeyword()
{
    std::string input = g_newKeywordBuffer;

    std::wstring text =
        AbsoluteParser::TextUtils::Trim(
            AbsoluteParser::TextUtils::Utf8ToWide(input));

    if (text.empty())
    {
        g_keywordStatus =
            L"Введите ключевое слово.";

        return;
    }

    if (KeywordExists(text))
    {
        g_keywordStatus =
            L"Такое ключевое слово уже существует.";

        return;
    }

    if (g_keywords.size() >= 1000)
    {
        g_keywordStatus =
            L"Достигнут лимит в 1000 ключевых слов.";

        return;
    }

    AbsoluteParser::Config::KeywordEntry entry;

    entry.text = text;
    entry.enabled = true;

    g_keywords.push_back(entry);

    g_newKeywordBuffer[0] = '\0';

    SaveKeywordSettings();
}

// ============================================================
// УДАЛЕНИЕ ОДНОГО КЛЮЧЕВОГО СЛОВА
// ============================================================

static void DeleteKeyword(size_t index)
{
    if (index >= g_keywords.size())
        return;

    g_keywords.erase(
        g_keywords.begin() + index);

    SaveKeywordSettings();
}

// ============================================================
// ПОЛНАЯ ОЧИСТКА СПИСКА
// ============================================================

static void ClearAllKeywords()
{
    g_keywords.clear();

    g_newKeywordBuffer[0] = '\0';
    g_keywordSearchBuffer[0] = '\0';

    SaveKeywordSettings();
}

// ============================================================
// ВЫБОР CHATLOG.TXT
// ============================================================


static bool SelectChatlogFile()
{
    wchar_t* selectedPath = new wchar_t[32768] {};

    OPENFILENAMEW openFile{};

    openFile.lStructSize = sizeof(openFile);
    openFile.hwndOwner = g_hwnd;
    openFile.lpstrFile = selectedPath;
    openFile.nMaxFile = 32768;

    openFile.lpstrFilter =
        L"Файл журнала GTA (chatlog.txt)\0chatlog.txt\0"
        L"Все файлы (*.*)\0*.*\0";

    openFile.nFilterIndex = 1;

    openFile.lpstrTitle =
        L"Выберите файл chatlog.txt";

    openFile.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&openFile))
    {
        delete[] selectedPath;
        return false;
    }

    std::wstring newPath = selectedPath;

    delete[] selectedPath;

    if (!g_parser.StartFromEnd(newPath))
    {
        g_parserRunning = false;

        KillTimer(g_hwnd, g_timerId);

        g_chatlogStatus =
            L"Не удалось запустить наблюдение за выбранным файлом.";

        g_eventLogger.Error(
            L"Не удалось запустить парсер для выбранного chatlog.txt.");

        return false;
    }

    if (SetTimer(
        g_hwnd,
        g_timerId,
        AbsoluteParser::Config::CHATLOG_CHECK_INTERVAL_MS,
        nullptr) == 0)
    {
        g_parser.StopWatching();

        g_parserRunning = false;

        g_chatlogStatus =
            L"Не удалось запустить таймер проверки файла.";

        g_eventLogger.Error(
            L"Не удалось запустить таймер проверки chatlog.txt.");

        return false;
    }

    g_parserRunning = true;

    if (!AbsoluteParser::Config::SaveChatlogPath(newPath))
    {
        g_chatlogStatus =
            L"Файл выбран, но не удалось сохранить путь в config.ini.";

        g_eventLogger.Error(
            L"Не удалось сохранить путь к chatlog.txt в config.ini.");
    }
    else
    {
        g_chatlogStatus =
            L"Файл успешно выбран. Отслеживание запущено.";

        g_eventLogger.Info(
            L"Выбран файл chatlog.txt: " + newPath);
    }

    g_chatlogPath = newPath;

    return true;
}

// ============================================================
// DIRECTX 11
// ============================================================

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;

    if (SUCCEEDED(g_pSwapChain->GetBuffer(
        0,
        IID_PPV_ARGS(&backBuffer))))
    {
        g_pd3dDevice->CreateRenderTargetView(
            backBuffer,
            nullptr,
            &g_mainRenderTargetView);

        backBuffer->Release();
    }
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();

    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }

    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }

    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

static bool CreateDeviceD3D(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};

    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;

    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;

    const D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext);

    if (FAILED(result))
        return false;

    CreateRenderTarget();

    return g_mainRenderTargetView != nullptr;
}

// ============================================================
// СТИЛЬ ИНТЕРФЕЙСА
// ============================================================

static std::wstring GetThemeFilePath()
{
    wchar_t path[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (length == 0 || length >= ARRAYSIZE(path))
        return L"theme.ini";

    std::wstring result(path, length);
    size_t slash = result.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        result.resize(slash + 1);
    return result + L"theme.ini";
}

static void LoadTheme()
{
    std::wifstream file(GetThemeFilePath());
    int savedTheme = 0;
    if (file >> savedTheme && savedTheme >= 0 && savedTheme <= 2)
        g_theme = savedTheme;
}

static void SaveTheme()
{
    std::wofstream file(GetThemeFilePath(), std::ios::trunc);
    if (file)
        file << g_theme;
}

static void ApplyTitleBarTheme(HWND hwnd)
{
    if (!hwnd)
        return;

    // 20 is supported on current Windows 10/11; 19 is the older attribute ID.
    constexpr DWORD darkModeAttribute = 20;
    constexpr DWORD legacyDarkModeAttribute = 19;
    // Keep the native title bar dark for every in-app theme.
    BOOL useDarkTitleBar = TRUE;
    HRESULT result = DwmSetWindowAttribute(
        hwnd,
        darkModeAttribute,
        &useDarkTitleBar,
        sizeof(useDarkTitleBar));

    if (FAILED(result))
    {
        DwmSetWindowAttribute(
            hwnd,
            legacyDarkModeAttribute,
            &useDarkTitleBar,
            sizeof(useDarkTitleBar));
    }

    // Force Windows to recalculate and repaint the non-client frame now.
    SetWindowPos(
        hwnd,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
        SWP_NOACTIVATE | SWP_FRAMECHANGED);
    DwmFlush();
    SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
    SendMessageW(hwnd, WM_NCACTIVATE, TRUE, 0);
    RedrawWindow(
        hwnd,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_UPDATENOW);
}

static void SetupImGuiStyle()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);

    // Синий контур внутренних панелей и полей ввода.
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Border] =
        ImVec4(0.09f, 0.20f, 0.34f, 1.0f);

    colors[ImGuiCol_WindowBg] =
        ImVec4(0.055f, 0.065f, 0.085f, 1.0f);

    colors[ImGuiCol_ChildBg] =
        ImVec4(0.075f, 0.090f, 0.115f, 1.0f);

    colors[ImGuiCol_FrameBg] =
        ImVec4(0.12f, 0.14f, 0.18f, 1.0f);

    colors[ImGuiCol_Button] =
        ImVec4(0.12f, 0.17f, 0.24f, 1.0f);

    colors[ImGuiCol_ButtonHovered] =
        ImVec4(0.18f, 0.29f, 0.42f, 1.0f);

    colors[ImGuiCol_ButtonActive] =
        ImVec4(0.20f, 0.38f, 0.57f, 1.0f);

    colors[ImGuiCol_Header] =
        ImVec4(0.13f, 0.25f, 0.37f, 1.0f);

    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.17f, 0.31f, 0.46f, 1.0f);

    if (g_theme == 1)
    {
        ImGui::StyleColorsLight();
        style.WindowRounding = 8.0f;
        style.ChildRounding = 8.0f;
        style.FrameRounding = 6.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 6.0f;
        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.FramePadding = ImVec2(12.0f, 8.0f);
        style.ItemSpacing = ImVec2(10.0f, 10.0f);
        style.ChildBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        colors[ImGuiCol_Border] = ImVec4(0.48f, 0.59f, 0.72f, 1.0f);
        colors[ImGuiCol_Button] = ImVec4(0.78f, 0.85f, 0.94f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.68f, 0.79f, 0.92f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.70f, 0.88f, 1.0f);
        colors[ImGuiCol_Header] = ImVec4(0.72f, 0.82f, 0.94f, 1.0f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.62f, 0.76f, 0.92f, 1.0f);
    }
    else if (g_theme == 2)
    {
        colors[ImGuiCol_WindowBg] = ImVec4(0.015f, 0.015f, 0.020f, 1.0f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.035f, 0.035f, 0.045f, 1.0f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.075f, 0.090f, 1.0f);
        colors[ImGuiCol_Button] = ImVec4(0.12f, 0.12f, 0.15f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.22f, 0.27f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.30f, 0.36f, 1.0f);
        colors[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.24f, 0.30f, 1.0f);
        colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.25f, 1.0f);
    }
}

// ============================================================
// БОКОВОЕ МЕНЮ
// ============================================================

static void DrawMenuButton(const wchar_t* label, int page)
{
    bool selected = (g_currentPage == page);

    if (selected)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.13f, 0.31f, 0.48f, 1.0f));

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.17f, 0.37f, 0.55f, 1.0f));
    }

    std::string utf8 =
        AbsoluteParser::TextUtils::WideToUtf8(label);

    if (ImGui::Button(
        utf8.c_str(),
        ImVec2(-1.0f, 42.0f)))
    {
        g_currentPage = page;
    }

    if (selected)
        ImGui::PopStyleColor(2);
}

// ============================================================
// ГЛАВНАЯ СТРАНИЦА
// ============================================================

static void DrawHomePage()
{
    UiTextEmphasis(L"Добро пожаловать в Absolute Parser");

    ImGui::Spacing();

    UiTextDisabled(L"Панель управления программой");

    ImGui::Separator();

    ImGui::Spacing();

    if (ImGui::BeginTable(
        "Statistics",
        2,
        ImGuiTableFlags_SizingStretchSame))
    {
        std::wstring parserStatus;
        ImVec4 parserStatusColor;
        if (g_parserRunning)
        {
            parserStatus = L"АКТИВЕН";
            parserStatusColor = ImVec4(0.25f, 0.85f, 0.50f, 1.0f);
        }
        else if (g_chatlogPath.empty())
        {
            parserStatus = L"ФАЙЛ НЕ ВЫБРАН";
            parserStatusColor = ImVec4(0.90f, 0.65f, 0.30f, 1.0f);
        }
        else
        {
            parserStatus = L"ОСТАНОВЛЕН";
            parserStatusColor = ImVec4(0.90f, 0.35f, 0.35f, 1.0f);
        }

        ImGui::TableNextColumn();

        ImGui::BeginChild(
            "StatusCard",
            ImVec2(0, 110),
            true);

        ImGui::SetCursorPosY(22.0f);
        std::string statusLabel =
            AbsoluteParser::TextUtils::WideToUtf8(L"СТАТУС ПАРСЕРА");
        float labelX = (ImGui::GetWindowWidth() -
            ImGui::CalcTextSize(statusLabel.c_str()).x) * 0.5f;
        ImGui::SetCursorPosX(labelX);
        if (g_fontSemibold)
            ImGui::PushFont(g_fontSemibold, 18.0f);
        ImGui::TextUnformatted(statusLabel.c_str());
        if (g_fontSemibold)
            ImGui::PopFont();

        std::string statusText =
            AbsoluteParser::TextUtils::WideToUtf8(parserStatus);
        float statusX = (ImGui::GetWindowWidth() -
            ImGui::CalcTextSize(statusText.c_str()).x) * 0.5f;
        ImGui::SetCursorPosX(statusX);
        if (g_fontSemibold)
            ImGui::PushFont(g_fontSemibold, 21.0f);
        ImGui::TextColored(parserStatusColor, "%s", statusText.c_str());
        if (g_fontSemibold)
            ImGui::PopFont();

        ImGui::EndChild();

        ImGui::TableNextColumn();

        ImGui::BeginChild(
            "AnnouncementCard",
            ImVec2(0, 110),
            true);

        ImGui::SetCursorPosY(22.0f);
        std::string foundLabel =
            AbsoluteParser::TextUtils::WideToUtf8(L"НАЙДЕНО ОБЪЯВЛЕНИЙ");
        float foundLabelX = (ImGui::GetWindowWidth() -
            ImGui::CalcTextSize(foundLabel.c_str()).x) * 0.5f;
        ImGui::SetCursorPosX(foundLabelX);
        if (g_fontSemibold)
            ImGui::PushFont(g_fontSemibold, 18.0f);
        ImGui::TextUnformatted(foundLabel.c_str());
        if (g_fontSemibold)
            ImGui::PopFont();

        std::string totalText = std::to_string(g_totalAnnouncements);
        float totalX = (ImGui::GetWindowWidth() -
            ImGui::CalcTextSize(totalText.c_str()).x) * 0.5f;
        ImGui::SetCursorPosX(totalX);
        if (g_fontSemibold)
            ImGui::PushFont(g_fontSemibold, 22.0f);
        ImGui::TextUnformatted(totalText.c_str());
        if (g_fontSemibold)
            ImGui::PopFont();

        ImGui::EndChild();

        ImGui::EndTable();
    }

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::Spacing();

    UiTextSection(L"Файл игрового чата");

    ImGui::Spacing();

    if (g_chatlogPath.empty())
    {
        UiTextDisabled(
            L"Файл chatlog.txt ещё не выбран.");
    }
    else
    {
        UiTextDisabled(L"Текущий путь:");

        UiTextWrapped(g_chatlogPath.c_str());
    }

    ImGui::Spacing();

    if (UiButton(
        L"Выбрать файл chatlog.txt",
        ImVec2(240.0f, 38.0f)))
    {
        SelectChatlogFile();
    }

    ImGui::Spacing();

    UiTextDisabled(
        L"Обычно файл находится в папке:");

    UiTextWrapped(
        L"C:\\Users\\пользователь\\Documents\\GTA San Andreas User Files\\SAMP");

    ImGui::Spacing();

    UiTextDisabled(
        L"Выберите именно файл chatlog.txt. "
        L"После выбора программа начнёт отслеживать новые сообщения.");

    if (!g_chatlogStatus.empty())
    {
        ImGui::Spacing();

        if (g_parserRunning)
        {
            UiTextColored(
                ImVec4(0.25f, 0.85f, 0.50f, 1.0f),
                g_chatlogStatus);
        }
        else
        {
            UiTextColored(
                ImVec4(0.95f, 0.55f, 0.30f, 1.0f),
                g_chatlogStatus);
        }
    }
}

// ============================================================
// СТРАНИЦА КЛЮЧЕВЫХ СЛОВ
// ============================================================

static void DrawKeywordsPage()
{
    UiTextEmphasis(L"Управление ключевыми словами");

    ImGui::Separator();

    ImGui::Spacing();

    int activeCount = 0;
    int inactiveCount = 0;

    for (const auto& keyword : g_keywords)
    {
        if (keyword.enabled)
            ++activeCount;
        else
            ++inactiveCount;
    }

    if (ImGui::BeginTable(
        "KeywordStatistics",
        3,
        ImGuiTableFlags_SizingStretchSame))
    {
        const std::wstring stats[] =
        {
            L"Всего: " + std::to_wstring(g_keywords.size()),
            L"Активных: " + std::to_wstring(activeCount),
            L"Отключённых: " + std::to_wstring(inactiveCount)
        };

        const ImVec4 statColors[] =
        {
            ImGui::GetStyleColorVec4(ImGuiCol_Text),
            ImVec4(0.25f, 0.85f, 0.50f, 1.0f),
            ImVec4(0.90f, 0.65f, 0.30f, 1.0f)
        };

        for (int i = 0; i < 3; ++i)
        {
            ImGui::TableNextColumn();

            if (g_fontSemibold)
                ImGui::PushFont(g_fontSemibold, 19.0f);

            std::string text =
                AbsoluteParser::TextUtils::WideToUtf8(stats[i]);
            float textWidth = ImGui::CalcTextSize(text.c_str()).x;
            float availableWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (availableWidth - textWidth) * 0.5f);
            UiTextColored(statColors[i], stats[i]);

            if (g_fontSemibold)
                ImGui::PopFont();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();

    ImGui::Separator();

    UiTextSection(L"Добавление нового слова");

    ImGui::SetNextItemWidth(-125.0f);

    bool enterPressed = ImGui::InputText(
        "##NewKeyword",
        g_newKeywordBuffer,
        sizeof(g_newKeywordBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();

    if (UiButton(L"Добавить", ImVec2(115.0f, 0.0f)) ||
        enterPressed)
    {
        AddKeyword();
    }

    ImGui::Spacing();

    ImGui::Separator();

    UiTextSection(L"Поиск по списку");

    ImGui::SetNextItemWidth(-1.0f);

    ImGui::InputText(
        "##KeywordSearch",
        g_keywordSearchBuffer,
        sizeof(g_keywordSearchBuffer));

    ImGui::Spacing();

    UiTextSection(L"Список ключевых слов");

    if (g_keywords.empty())
    {
        UiTextDisabled(
            L"Список пуст. Добавьте ключевые слова.");
    }
    else
    {
        std::wstring searchText =
            AbsoluteParser::TextUtils::Utf8ToWide(
                g_keywordSearchBuffer);

        int visibleCount = 0;

        ImGui::BeginChild(
            "KeywordList",
            ImVec2(0.0f, 300.0f),
            true);

        for (size_t i = 0; i < g_keywords.size(); ++i)
        {
            auto& keyword = g_keywords[i];

            if (!AbsoluteParser::TextUtils::ContainsIgnoreCase(
                keyword.text,
                searchText))
            {
                continue;
            }

            ++visibleCount;

            ImGui::PushID(static_cast<int>(i));

            std::string keywordText =
                AbsoluteParser::TextUtils::WideToUtf8(
                    keyword.text);

            bool enabled = keyword.enabled;

            if (ImGui::Checkbox(
                "##Enabled",
                &enabled))
            {
                keyword.enabled = enabled;

                SaveKeywordSettings();
            }

            ImGui::SameLine();

            if (keyword.enabled)
            {
                ImGui::TextUnformatted(
                    keywordText.c_str());
            }
            else
            {
                ImGui::TextDisabled(
                    "%s",
                    keywordText.c_str());
            }

            ImGui::SameLine();

            ImGui::SetCursorPosX(
                ImGui::GetWindowWidth() - 105.0f);

            if (UiSmallButton(L"Удалить"))
            {
                DeleteKeyword(i);

                ImGui::PopID();

                break;
            }

            ImGui::Separator();

            ImGui::PopID();
        }

        if (visibleCount == 0)
        {
            UiTextDisabled(
                L"По вашему запросу ничего не найдено.");
        }

        ImGui::EndChild();
    }

    ImGui::Spacing();

    if (!g_keywords.empty())
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.48f, 0.13f, 0.15f, 1.0f));

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(0.65f, 0.17f, 0.20f, 1.0f));

        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(0.75f, 0.20f, 0.23f, 1.0f));

        if (UiButton(
            L"Очистить весь список",
            ImVec2(220.0f, 0.0f)))
        {
            ImGui::OpenPopup("ClearKeywordsPopup");
        }

        ImGui::PopStyleColor(3);
    }

    if (ImGui::BeginPopupModal(
        "ClearKeywordsPopup",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiTextWrapped(
            L"Вы действительно хотите удалить "
            L"все ключевые слова?");

        UiTextDisabled(
            L"Это действие нельзя отменить.");

        ImGui::Spacing();

        if (UiButton(
            L"Да, очистить",
            ImVec2(140.0f, 0.0f)))
        {
            ClearAllKeywords();

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (UiButton(
            L"Отмена",
            ImVec2(100.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::Spacing();

    if (!g_keywordStatus.empty())
    {
        ImGui::Separator();

        if (g_keywordStatus ==
            L"Ошибка сохранения config.ini!")
        {
            UiTextColored(
                ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                g_keywordStatus);
        }
        else
        {
            UiTextColored(
                ImVec4(0.30f, 0.80f, 0.50f, 1.0f),
                g_keywordStatus);
        }
    }
}

// ============================================================
// ОЧИСТКА ИСТОРИИ
// ============================================================

static bool ClearHistory()
{
    if (!g_database.ClearHistory())
    {
        g_historyStatus =
            L"Не удалось очистить историю в базе данных.";

        g_eventLogger.Error(
            L"Ошибка очистки истории объявлений в SQLite.");

        return false;
    }

    g_history.clear();

    g_totalAnnouncements = 0;

    g_selectedAnnouncementText.clear();

    g_historySearchBuffer[0] = '\0';

    g_historyStatus =
        L"История успешно очищена.";

    g_eventLogger.Info(
        L"История объявлений успешно очищена.");

    return true;
}

static bool CopyTextToClipboard(const std::wstring& text)
{
    if (!OpenClipboard(g_hwnd))
        return false;

    EmptyClipboard();

    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
    {
        CloseClipboard();
        return false;
    }

    void* destination = GlobalLock(memory);
    if (!destination)
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    memcpy(destination, text.c_str(), bytes);
    GlobalUnlock(memory);

    if (!SetClipboardData(CF_UNICODETEXT, memory))
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

static std::string AnnouncementTextForSelection(
    const std::wstring& source)
{
    std::wstring display;
    size_t position = 0;

    while (position < source.size())
    {
        size_t open = source.find(L'{', position);
        if (open == std::wstring::npos)
        {
            display.append(source, position, std::wstring::npos);
            break;
        }

        display.append(source, position, open - position);
        size_t close = source.find(L'}', open + 1);
        if (close == std::wstring::npos)
        {
            display.append(source, open, std::wstring::npos);
            break;
        }

        std::wstring alias = source.substr(open + 1, close - open - 1);
        std::string utf8Alias =
            AbsoluteParser::TextUtils::WideToUtf8(alias);
        std::transform(
            utf8Alias.begin(), utf8Alias.end(), utf8Alias.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        auto found = g_emojiCodes.find(utf8Alias);
        if (found == g_emojiCodes.end())
        {
            display.append(source, open, close - open + 1);
        }
        else
        {
            unsigned long codepoint = std::stoul(found->second, nullptr, 16);
            if (codepoint <= 0xFFFF)
                display.push_back(static_cast<wchar_t>(codepoint));
            else
            {
                codepoint -= 0x10000;
                display.push_back(static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
                display.push_back(static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
            }
        }
        position = close + 1;
    }

    return AbsoluteParser::TextUtils::WideToUtf8(display);
}

// ============================================================
// СТРАНИЦА ИСТОРИИ
// ============================================================

static void DrawHistoryPage()
{
    UiTextEmphasis(L"История объявлений");

    ImGui::Separator();
    ImGui::Spacing();

    UiText(
        L"Всего объявлений: " +
        std::to_wstring(g_totalAnnouncements));

    ImGui::Spacing();

    UiTextSection(L"Поиск по истории");

    float buttonWidth = 200.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;

    float searchWidth =
        ImGui::GetContentRegionAvail().x -
        buttonWidth -
        spacing;

    if (searchWidth < 100.0f)
        searchWidth = 100.0f;

    ImGui::SetNextItemWidth(searchWidth);

    ImGui::InputText(
        "##HistorySearch",
        g_historySearchBuffer,
        sizeof(g_historySearchBuffer));

    ImGui::SameLine();

    ImVec4 clearButton = g_theme == 1
        ? ImVec4(0.78f, 0.30f, 0.34f, 1.0f)
        : ImVec4(0.48f, 0.13f, 0.15f, 1.0f);
    ImVec4 clearButtonHover = g_theme == 1
        ? ImVec4(0.86f, 0.38f, 0.42f, 1.0f)
        : ImVec4(0.65f, 0.17f, 0.20f, 1.0f);
    ImVec4 clearButtonActive = g_theme == 1
        ? ImVec4(0.72f, 0.24f, 0.29f, 1.0f)
        : ImVec4(0.75f, 0.20f, 0.23f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, clearButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, clearButtonHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, clearButtonActive);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    if (UiButton(
        L"Очистить историю",
        ImVec2(buttonWidth, 0.0f)))
    {
        ImGui::OpenPopup("ClearHistoryPopup");
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    if (ImGui::BeginPopupModal(
        "ClearHistoryPopup",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        UiTextWrapped(
            L"Вы действительно хотите удалить "
            L"всю историю объявлений?");

        UiTextDisabled(
            L"Все сохранённые записи будут удалены.");

        ImGui::Spacing();

        if (UiButton(
            L"Да, очистить",
            ImVec2(140.0f, 0.0f)))
        {
            ClearHistory();

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (UiButton(
            L"Отмена",
            ImVec2(100.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (!g_historyStatus.empty())
    {
        ImGui::Spacing();

        if (g_historyStatus == L"История успешно очищена." ||
            g_historyStatus == L"Текст объявления скопирован.")
        {
            UiTextColored(
                ImVec4(0.25f, 0.85f, 0.50f, 1.0f),
                g_historyStatus);
        }
        else
        {
            UiTextColored(
                ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                g_historyStatus);
        }
    }

    std::wstring searchText =
        AbsoluteParser::TextUtils::Utf8ToWide(
            g_historySearchBuffer);

    int visibleCount = 0;

    for (const auto& announcement : g_history)
    {
        if (AnnouncementMatchesHistorySearch(
            announcement,
            searchText))
            ++visibleCount;
    }

    ImGui::Spacing();

    UiText(
        L"Найдено записей: " +
        std::to_wstring(visibleCount));

    // Фиксированная панель действий отделена от прокручиваемого списка.
    ImGui::BeginChild(
        "HistoryCopyToolbar",
        ImVec2(0.0f, 62.0f),
        false,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    if (UiButton(L"Копировать текст", ImVec2(180.0f, 34.0f)))
    {
        if (g_selectedAnnouncementText.empty())
            g_copyStatus = L"Сначала выберите объявление в списке.";
        else if (CopyTextToClipboard(g_selectedAnnouncementText))
            g_copyStatus = L"Текст объявления скопирован.";
        else
            g_copyStatus = L"Не удалось скопировать текст объявления.";
    }

    ImGui::SameLine();
    if (!g_copyStatus.empty())
    {
        ImVec4 statusColor =
            g_copyStatus == L"Текст объявления скопирован."
            ? ImVec4(0.25f, 0.85f, 0.50f, 1.0f)
            : ImVec4(0.95f, 0.55f, 0.30f, 1.0f);
        UiTextColored(statusColor, g_copyStatus);
    }
    else
    {
        UiTextDisabled(L"Выберите объявление, затем нажмите эту кнопку.");
    }

    ImGui::EndChild();

    if (g_history.empty())
    {
        UiTextDisabled(L"История пока пуста.");
        return;
    }

    if (visibleCount == 0)
    {
        UiTextDisabled(
            L"По вашему запросу ничего не найдено.");
        return;
    }

    ImGui::BeginChild(
        "HistoryCardsList",
        ImVec2(0.0f, 0.0f),
        false);

    for (auto it = g_history.rbegin();
        it != g_history.rend();
        ++it)
    {
        const auto& announcement = *it;

        if (!AnnouncementMatchesHistorySearch(
            announcement,
            searchText))
            continue;

        std::string text =
            AbsoluteParser::TextUtils::WideToUtf8(
                announcement.text);

        ImGui::PushID(&announcement);

        float paddingX =
            ImGui::GetStyle().WindowPadding.x;

        float paddingY =
            ImGui::GetStyle().WindowPadding.y;

        float cardWidth =
            ImGui::GetContentRegionAvail().x;

        float contentWidth =
            cardWidth - paddingX * 2.0f;

        if (contentWidth < 100.0f)
            contentWidth = 100.0f;

        float lineHeight =
            ImGui::GetTextLineHeight();

        std::vector<std::string> lines;

        std::string currentLine;

        size_t position = 0;

        while (position < text.size())
        {
            size_t start =
                text.find_first_not_of(
                    " \t\r\n",
                    position);

            if (start == std::string::npos)
                break;

            if (text[start] == '\n')
            {
                if (!currentLine.empty())
                {
                    lines.push_back(currentLine);
                    currentLine.clear();
                }

                position = start + 1;
                continue;
            }

            size_t end =
                text.find_first_of(
                    " \t\r\n",
                    start);

            std::string word =
                text.substr(
                    start,
                    end == std::string::npos
                    ? std::string::npos
                    : end - start);

            std::string candidate =
                currentLine.empty()
                ? word
                : currentLine + " " + word;

            float candidateWidth =
                ImGui::CalcTextSize(
                    candidate.c_str()).x;

            if (!currentLine.empty() &&
                candidateWidth > contentWidth)
            {
                lines.push_back(currentLine);
                currentLine = word;
            }
            else
            {
                currentLine = candidate;
            }

            position =
                end == std::string::npos
                ? text.size()
                : end + 1;
        }

        if (!currentLine.empty())
            lines.push_back(currentLine);

        if (lines.empty())
            lines.push_back(text);

        float textHeight =
            static_cast<float>(lines.size()) *
            lineHeight;

        float cardHeight =
            textHeight +
            paddingY * 2.0f;

        bool isSelected =
            g_selectedAnnouncementText == announcement.text;

        if (isSelected)
        {
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg,
                ImVec4(0.10f, 0.23f, 0.38f, 1.0f));
        }

        ImGui::BeginChild(
            "HistoryCard",
            ImVec2(0.0f, cardHeight),
            true,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::SetCursorPosY(paddingY);

        for (const auto& line : lines)
        {
            float lineWidth =
                ImGui::CalcTextSize(line.c_str()).x;
            float startX =
                (ImGui::GetWindowWidth() - lineWidth) * 0.5f;

            ImGui::SetCursorPosX(startX);
            DrawHistoryAnnouncementText(line);
        }

        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            g_selectedAnnouncementText = announcement.text;
        }

        ImGui::EndChild();

        if (isSelected)
            ImGui::PopStyleColor();

        ImGui::PopID();

        ImGui::Spacing();
    }

    ImGui::EndChild();
}

// ============================================================
// СТРАНИЦА НАСТРОЕК
// ============================================================

static void DrawSettingsPage()
{
    UiTextEmphasis(L"Настройки");

    ImGui::Separator();

    ImGui::Spacing();

    UiTextSection(L"Внешний вид");
    UiTextDisabled(L"Цветовая тема приложения");

    static const std::string themeNames[] =
    {
        AbsoluteParser::TextUtils::WideToUtf8(L"Тёмно-синяя"),
        AbsoluteParser::TextUtils::WideToUtf8(L"Светлая"),
        AbsoluteParser::TextUtils::WideToUtf8(L"Чёрная")
    };

    const char* themeItems[] =
    {
        themeNames[0].c_str(),
        themeNames[1].c_str(),
        themeNames[2].c_str()
    };

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo(
        "##AppTheme",
        &g_theme,
        themeItems,
        IM_ARRAYSIZE(themeItems)))
    {
        SaveTheme();
        SetupImGuiStyle();
        ApplyTitleBarTheme(g_hwnd);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    UiTextSection(L"Уведомления");

    ImGui::Spacing();

    UiTextDisabled(
        L"Управление уведомлениями Windows");

    bool previousValue = g_notificationsEnabled;

    std::string notificationLabel =
        AbsoluteParser::TextUtils::WideToUtf8(
            L"Показывать уведомления Windows");

    if (ImGui::Checkbox(
        notificationLabel.c_str(),
        &g_notificationsEnabled))
    {
        SaveNotificationSettings();
    }

    ImGui::Spacing();

    if (g_notificationsAvailable)
    {
        UiTextColored(
            ImVec4(0.25f, 0.85f, 0.50f, 1.0f),
            L"Система уведомлений инициализирована.");
    }
    else
    {
        UiTextColored(
            ImVec4(0.95f, 0.55f, 0.30f, 1.0f),
            L"Система уведомлений недоступна.");
    }

    if (!g_notificationsStatus.empty())
    {
        ImGui::Spacing();
        UiTextWrapped(g_notificationsStatus.c_str());
    }

    ImGui::Spacing();

    if (UiButton(
        L"Отправить тестовое уведомление",
        ImVec2(280.0f, 36.0f)))
    {
        if (!g_notificationsAvailable)
        {
            g_notificationsStatus =
                L"Невозможно отправить уведомление: система не инициализирована.";
        }
        else if (!g_notificationsEnabled)
        {
            g_notificationsStatus =
                L"Сначала включите Windows-уведомления.";
        }
        else if (AbsoluteParser::WindowsNotifications::ShowTest())
        {
            g_notificationsStatus =
                L"Тестовое уведомление отправлено.";

            g_eventLogger.Info(
                L"Отправлено тестовое Windows-уведомление.");
        }
        else
        {
            g_notificationsStatus =
                L"Не удалось отправить тестовое уведомление.";

            g_eventLogger.Error(
                L"Ошибка отправки тестового Windows-уведомления.");
        }
    }

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::Spacing();

    UiTextSection(L"Проверка игрового чата");

    ImGui::Spacing();

    UiTextDisabled(
        L"Интервал проверки файла chatlog.txt:");

    UiTextColored(
        ImVec4(0.25f, 0.75f, 0.95f, 1.0f),
        L"1 секунда");

    UiTextDisabled(
        L"Этот параметр фиксированный и не изменяется пользователем.");

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::Spacing();

    UiTextSection(L"Параметры запуска");

    UiTextDisabled(
        L"Дополнительные параметры запуска в текущей версии не предусмотрены.");
}

// ============================================================
// СТРАНИЦА О ПРОГРАММЕ
// ============================================================

static void DrawAboutPage()
{
    UiTextEmphasis(L"О программе");

    ImGui::Separator();

    ImGui::Spacing();

    UiTextEmphasis(L"Absolute Parser");

    UiText(L"Версия:");

    ImGui::SameLine();

    UiText(AbsoluteParser::Config::APP_VERSION);

    ImGui::Spacing();

    UiTextWrapped(
        L"Программа предназначена для автоматического "
        L"поиска объявлений в игровом чате SAMP Absolute RP "
        );

    ImGui::Spacing();

    UiTextSection(L"Разработчик");
    UiText(L"Дмитрий Омельянчук");

    ImGui::Spacing();

    if (UiButton(
        L"Связь с разработчиком VK",
        ImVec2(280.0f, 38.0f)))
    {
        ShellExecuteW(
            nullptr,
            L"open",
            L"https://vk.ru/omelyan70",
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
    }

    ImGui::Spacing();

    UiTextDisabled(
        L"Разработано на C++ с использованием Dear ImGui, "
        L"DirectX 11 и SQLite.");
}

static void DrawFaqItem(const wchar_t* question, const wchar_t* answer)
{
    std::string questionUtf8 =
        AbsoluteParser::TextUtils::WideToUtf8(question);

    if (ImGui::CollapsingHeader(questionUtf8.c_str()))
    {
        ImGui::Indent(12.0f);
        UiTextWrapped(answer);
        ImGui::Unindent(12.0f);
        ImGui::Spacing();
    }
}

static void DrawFaqPage()
{
    UiTextEmphasis(L"Вопросы и ответы");
    ImGui::Separator();
    ImGui::Spacing();

    DrawFaqItem(
        L"Где найти chatlog.txt?",
        L"Обычно файл находится в папке GTA San Andreas User Files\\SAMP. "
        L"Укажите полный путь к файлу на главной странице приложения.");

    DrawFaqItem(
        L"Где включить или отключить уведомления Absolute Parser?",
        L"В приложении откройте «Настройки» и включите или отключите Windows-уведомления. Также их можно разрешить или запретить в Windows: Параметры → Система → Уведомления → Absolute Parser.");

    DrawFaqItem(
        L"Где включить или отключить звук уведомлений?",
        L"Откройте Параметры Windows → Система → Уведомления → Absolute Parser и измените параметр воспроизведения звука при получении уведомления.");

    DrawFaqItem(
        L"Где изменить, как долго показывается уведомление?",
        L"Откройте Параметры Windows → Специальные возможности → Визуальные эффекты и выберите время в пункте «Отклонять уведомления после указанного времени».");

    DrawFaqItem(
        L"Почему уведомление не показывается во время игры?",
        L"Windows может включать режим «Не беспокоить» автоматически. Откройте Параметры → Система → Уведомления → Автоматически включать режим «Не беспокоить» и отключите правило «Во время игры». В Windows 10 это правило находится в разделе «Фокусировка внимания».");

    DrawFaqItem(
        L"Что делает крестик окна?",
        L"Окно сворачивается в трей, а парсер продолжает работать. Чтобы закрыть программу, нажмите правой кнопкой по значку в трее и выберите «Закрыть».");

}

// ============================================================
// ОТРИСОВКА ИНТЕРФЕЙСА
// ============================================================

static void DrawInterface()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));

    ImGui::SetNextWindowSize(
        ImGui::GetIO().DisplaySize);

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        0.0f);

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(12.0f, 12.0f));

    ImGui::Begin(
        "##AbsoluteParserRoot",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::BeginChild(
        "Sidebar",
        ImVec2(225.0f, 0.0f),
        true);

    ImGui::Spacing();

    if (g_fontSemibold)
        ImGui::PushFont(g_fontSemibold, 21.0f);

    const char* brandPartOne = "ABSOLUTE";
    const char* brandPartTwo = "PARSER";
    float brandWidth =
        ImGui::CalcTextSize(brandPartOne).x +
        ImGui::CalcTextSize(brandPartTwo).x +
        ImGui::GetStyle().ItemSpacing.x * 0.5f;

    ImGui::SetCursorPosX(
        (ImGui::GetWindowWidth() - brandWidth) * 0.5f);

    ImGui::TextUnformatted(brandPartOne);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 0.5f);
    ImGui::TextColored(
        ImVec4(0.25f, 0.60f, 0.95f, 1.0f),
        "%s",
        brandPartTwo);

    if (g_fontSemibold)
        ImGui::PopFont();

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::Spacing();

    DrawMenuButton(L"Главная", 0);
    DrawMenuButton(L"Ключевые слова", 1);
    DrawMenuButton(L"История", 2);
    DrawMenuButton(L"Настройки", 3);
    DrawMenuButton(L"О программе", 4);
    DrawMenuButton(L"Вопросы и ответы", 5);

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild(
        "MainContent",
        ImVec2(0.0f, 0.0f),
        true,
        ImGuiWindowFlags_NoScrollbar);

    ImGui::Spacing();

    switch (g_currentPage)
    {
    case 0:
        DrawHomePage();
        break;

    case 1:
        DrawKeywordsPage();
        break;

    case 2:
        DrawHistoryPage();
        break;

    case 3:
        DrawSettingsPage();
        break;

    case 4:
        DrawAboutPage();
        break;

    case 5:
        DrawFaqPage();
        break;
    }

    ImGui::EndChild();

    ImGui::End();

    ImGui::PopStyleVar(2);
}

// ============================================================
// ОБРАБОТКА ОКНА
// ============================================================

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    if (ImGui::GetCurrentContext() != nullptr)
    {
        if (ImGui_ImplWin32_WndProcHandler(
            hwnd,
            msg,
            wParam,
            lParam))
        {
            return TRUE;
        }
    }

    switch (msg)
    {
    case WM_SIZE:
    {
        if (wParam != SIZE_MINIMIZED)
        {
            g_resizeWidth = LOWORD(lParam);
            g_resizeHeight = HIWORD(lParam);
        }

        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == g_timerId && g_parserRunning)
        {
            const auto newAnnouncements =
                g_parser.ReadNewLines();

            for (const auto& announcement : newAnnouncements)
            {
                if (!g_database.SaveAnnouncement(announcement))
                {
                    OutputDebugStringW(
                        L"[Absolute Parser] Ошибка сохранения объявления в базу данных.\n");

                    g_eventLogger.Error(
                        L"Не удалось сохранить объявление в базу данных.");

                    continue;
                }

                ++g_totalAnnouncements;

                g_history.push_back(announcement);

                g_eventLogger.Info(
                    L"Найдено и сохранено объявление. Ключевое слово: "
                    + announcement.keyword);

                // Отправляем уведомление только после успешного
                // сохранения объявления в SQLite.

                if (g_notificationsEnabled &&
                    g_notificationsAvailable)
                {
                    std::wstring notificationTitle =
                        L"Absolute Parser — найдено объявление";

                    std::wstring notificationMessage =
                        announcement.text;

                    if (!AbsoluteParser::WindowsNotifications::Show(
                        notificationTitle,
                        notificationMessage))
                    {
                        g_eventLogger.Error(
                            L"Не удалось отправить Windows-уведомление.");
                    }
                }

                std::wstring debugText =
                    L"\n[Absolute Parser] Найдено объявление!\n";

                debugText += L"Ключевое слово: ";
                debugText += announcement.keyword;
                debugText += L"\n";

                debugText += announcement.text;
                debugText += L"\n";

                OutputDebugStringW(debugText.c_str());
            }

            if (!newAnnouncements.empty())
            {
                std::wstring title =
                    L"Absolute Parser | Найдено объявлений: ";

                title += std::to_wstring(g_totalAnnouncements);

                SetWindowTextW(hwnd, title.c_str());
            }
        }

        return 0;
    }

    case WM_SYSCOMMAND:
    {
        if ((wParam & 0xFFF0) == SC_KEYMENU)
            return 0;

        break;
    }

    case WM_APP_TRAY:
    {
        switch (LOWORD(lParam))
        {
        case WM_LBUTTONDBLCLK:
            RestoreMainWindow();
            return 0;

        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hwnd);
            return 0;
        }

        break;
    }

    case WM_CLOSE:
    {
        if (!g_exitRequested)
        {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }

        g_eventLogger.Info(
            L"Программа завершает работу.");

        KillTimer(hwnd, g_timerId);

        g_parserRunning = false;

        g_parser.StopWatching();

        g_database.Close();

        // Освобождаем WIC COM-объекты до завершения WinRT/COM apartment.
        CleanupEmojiSupport();

        AbsoluteParser::WindowsNotifications::Shutdown();

        RemoveTrayIcon();

        DestroyWindow(hwnd);

        return 0;
    }

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam);
}

// ============================================================
// ТОЧКА ВХОДА
// ============================================================

int WINAPI wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE,
    _In_ PWSTR,
    _In_ int show)
{
    // ЗАГРУЗКА КЛЮЧЕВЫХ СЛОВ

    g_keywords = AbsoluteParser::Config::LoadKeywords();

    ApplyKeywords();

    // ЗАГРУЗКА НАСТРОЙКИ WINDOWS-УВЕДОМЛЕНИЙ

    g_notificationsEnabled =
        AbsoluteParser::Config::LoadNotificationsEnabled();

    // ЗАГРУЗКА СОХРАНЁННОГО ПУТИ К CHATLOG.TXT

    g_chatlogPath =
        AbsoluteParser::Config::LoadChatlogPath();

    // СОЗДАНИЕ ОКНА WINDOWS

    WNDCLASSEXW wc{};

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"AbsoluteParserWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc))
        return 1;

    HWND hwnd = CreateWindowExW(
        0,
        L"AbsoluteParserWindow",
        AbsoluteParser::Config::APP_NAME,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        AbsoluteParser::Config::WINDOW_WIDTH,
        AbsoluteParser::Config::WINDOW_HEIGHT,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd)
        return 1;

    g_hwnd = hwnd;

    // ИНИЦИАЛИЗАЦИЯ DIRECTX 11

    if (!CreateDeviceD3D(hwnd))
    {
        MessageBoxW(
            hwnd,
            L"Не удалось инициализировать DirectX 11.",
            L"Absolute Parser",
            MB_OK | MB_ICONERROR);

        CleanupDeviceD3D();

        DestroyWindow(hwnd);

        return 1;
    }

    // ИНИЦИАЛИЗАЦИЯ ЖУРНАЛА СОБЫТИЙ

    if (!g_eventLogger.Initialize(L"AbsoluteParser.log"))
    {
        MessageBoxW(
            hwnd,
            L"Не удалось создать или открыть журнал событий.",
            L"Absolute Parser",
            MB_OK | MB_ICONERROR);

        CleanupDeviceD3D();

        DestroyWindow(hwnd);

        return 1;
    }

    g_eventLogger.Info(L"Программа запущена.");

    // ИНИЦИАЛИЗАЦИЯ WINDOWS-УВЕДОМЛЕНИЙ

    g_notificationsAvailable =
        AbsoluteParser::WindowsNotifications::Initialize();

    if (g_notificationsAvailable)
    {
        g_notificationsStatus =
            L"Система Windows-уведомлений успешно инициализирована.";

        g_eventLogger.Info(
            L"Система Windows-уведомлений успешно инициализирована.");
    }
    else
    {
        g_notificationsStatus =
            L"Не удалось инициализировать Windows-уведомления.";

        g_eventLogger.Error(
            L"Не удалось инициализировать Windows-уведомления. Парсер продолжит работу.");
    }

    // ПОДКЛЮЧЕНИЕ ПАРСЕРА К ЖУРНАЛУ

    g_parser.SetErrorCallback(
        [](const std::wstring& message)
        {
            g_eventLogger.Error(message);
        });

    // ОТКРЫТИЕ БАЗЫ ДАННЫХ

    if (!g_database.Open(L"AbsoluteParser.db"))
    {
        g_eventLogger.Error(
            L"Не удалось открыть базу данных SQLite.");

        MessageBoxW(
            hwnd,
            L"Не удалось открыть базу данных SQLite.",
            L"Absolute Parser",
            MB_OK | MB_ICONERROR);

        AbsoluteParser::WindowsNotifications::Shutdown();

        CleanupDeviceD3D();

        DestroyWindow(hwnd);

        return 1;
    }

    // ЗАГРУЗКА ИСТОРИИ

    g_history = g_database.GetAnnouncements();

    std::reverse(
        g_history.begin(),
        g_history.end());

    g_totalAnnouncements =
        g_database.GetAnnouncementCount();

    if (g_totalAnnouncements < 0)
    {
        g_eventLogger.Error(
            L"Не удалось получить количество объявлений из базы данных.");

        g_database.Close();

        AbsoluteParser::WindowsNotifications::Shutdown();

        MessageBoxW(
            hwnd,
            L"Не удалось получить количество объявлений из базы данных.",
            L"Absolute Parser",
            MB_OK | MB_ICONERROR);

        CleanupDeviceD3D();

        DestroyWindow(hwnd);

        return 1;
    }

    // ЗАПУСК ПАРСЕРА ПО СОХРАНЁННОМУ ПУТИ

    if (!g_chatlogPath.empty())
    {
        if (g_parser.StartFromEnd(g_chatlogPath))
        {
            if (SetTimer(
                hwnd,
                g_timerId,
                AbsoluteParser::Config::CHATLOG_CHECK_INTERVAL_MS,
                nullptr) != 0)
            {
                g_parserRunning = true;

                g_chatlogStatus =
                    L"Отслеживание сохранённого файла запущено.";

                g_eventLogger.Info(
                    L"Отслеживание сохранённого chatlog.txt запущено.");
            }
            else
            {
                g_parser.StopWatching();

                g_parserRunning = false;

                g_chatlogStatus =
                    L"Не удалось запустить таймер проверки файла.";

                g_eventLogger.Error(
                    L"Не удалось запустить таймер проверки chatlog.txt.");
            }
        }
        else
        {
            g_parserRunning = false;

            g_chatlogStatus =
                L"Сохранённый файл не удалось открыть. Выберите файл заново.";

            g_eventLogger.Error(
                L"Не удалось открыть сохранённый chatlog.txt.");
        }
    }
    else
    {
        g_parserRunning = false;

        g_chatlogStatus =
            L"Выберите файл chatlog.txt для начала работы.";

        g_eventLogger.Info(
            L"Путь к chatlog.txt не задан. Ожидание выбора файла пользователем.");
    }

    // ИНИЦИАЛИЗАЦИЯ IMGUI

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    LoadTheme();
    SetupImGuiStyle();
    ApplyTitleBarTheme(hwnd);

    if (!InitializeEmojiSupport())
    {
        g_eventLogger.Warning(
            L"Не удалось загрузить словарь emoji.json. Эмодзи останутся текстом.");
    }

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    g_fontRegular = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeui.ttf",
        17.0f,
        nullptr,
        io.Fonts->GetGlyphRangesCyrillic());

    g_fontSemibold = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeuib.ttf",
        18.0f,
        nullptr,
        io.Fonts->GetGlyphRangesCyrillic());

    if (!g_fontRegular)
        g_fontRegular = io.Fonts->AddFontDefault();

    if (g_fontRegular)
        io.FontDefault = g_fontRegular;

    if (!ImGui_ImplWin32_Init(hwnd))
    {
        g_eventLogger.Error(
            L"Не удалось инициализировать ImGui Win32.");

        ImGui::DestroyContext();

        KillTimer(hwnd, g_timerId);
        g_parser.StopWatching();
        g_parserRunning = false;
        g_database.Close();

        AbsoluteParser::WindowsNotifications::Shutdown();

        CleanupDeviceD3D();
        DestroyWindow(hwnd);

        return 1;
    }

    if (!ImGui_ImplDX11_Init(
        g_pd3dDevice,
        g_pd3dDeviceContext))
    {
        g_eventLogger.Error(
            L"Не удалось инициализировать ImGui DirectX 11.");

        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        KillTimer(hwnd, g_timerId);
        g_parser.StopWatching();
        g_parserRunning = false;
        g_database.Close();

        AbsoluteParser::WindowsNotifications::Shutdown();

        CleanupDeviceD3D();
        DestroyWindow(hwnd);

        return 1;
    }

    // ПОКАЗ ОКНА

    ShowWindow(hwnd, show);

    UpdateWindow(hwnd);

    // Регистрируем значок после того, как окно показано и оболочка
    // Windows уже получила его оконный дескриптор.
    if (!AddTrayIcon(hwnd))
    {
        OutputDebugStringW(
            L"[Absolute Parser] Не удалось добавить значок в системный трей.\n");
    }

    // ГЛАВНЫЙ ЦИКЛ

    MSG msg{};

    while (true)
    {
        while (PeekMessageW(
            &msg,
            nullptr,
            0,
            0,
            PM_REMOVE))
        {
            TranslateMessage(&msg);

            DispatchMessageW(&msg);

            if (msg.message == WM_QUIT)
                goto ExitLoop;
        }

        // ОБРАБОТКА ИЗМЕНЕНИЯ РАЗМЕРА ОКНА

        if (g_resizeWidth != 0 &&
            g_resizeHeight != 0)
        {
            CleanupRenderTarget();

            HRESULT resizeResult =
                g_pSwapChain->ResizeBuffers(
                    0,
                    g_resizeWidth,
                    g_resizeHeight,
                    DXGI_FORMAT_UNKNOWN,
                    0);

            if (SUCCEEDED(resizeResult))
                CreateRenderTarget();

            g_resizeWidth = 0;
            g_resizeHeight = 0;
        }

        // НОВЫЙ КАДР IMGUI

        ImGui_ImplDX11_NewFrame();

        ImGui_ImplWin32_NewFrame();

        ImGui::NewFrame();

        DrawInterface();

        ImGui::Render();

        // ОЧИСТКА ЭКРАНА

        const float clearColor[4] =
        {
            0.055f,
            0.065f,
            0.085f,
            1.0f
        };

        g_pd3dDeviceContext->OMSetRenderTargets(
            1,
            &g_mainRenderTargetView,
            nullptr);

        g_pd3dDeviceContext->ClearRenderTargetView(
            g_mainRenderTargetView,
            clearColor);

        ImGui_ImplDX11_RenderDrawData(
            ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

ExitLoop:

    // ЗАВЕРШЕНИЕ РАБОТЫ IMGUI

    ImGui_ImplDX11_Shutdown();

    ImGui_ImplWin32_Shutdown();

    ImGui::DestroyContext();

    CleanupEmojiSupport();

    // ОСВОБОЖДЕНИЕ DIRECTX 11

    CleanupDeviceD3D();

    return static_cast<int>(msg.wParam);
}
