
#pragma once

#include <string>

namespace AbsoluteParser
{
    namespace WindowsNotifications
    {
        // Инициализация системы Windows-уведомлений
        bool Initialize();

        // Отправка уведомления
        bool Show(
            const std::wstring& title,
            const std::wstring& message);

        // Отправка тестового уведомления
        bool ShowTest();

        // Освобождение ресурсов
        void Shutdown();
    }
}
