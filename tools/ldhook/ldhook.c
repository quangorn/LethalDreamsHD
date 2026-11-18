/* ldhook.pyd — спайк Фазы 0-бис (PLAN_proxy_dll.md, §2A).
 *
 * Тривиальное расширение Python 2.2.3: initldhook() пишет файл-маркер в
 * каталог EXE, дублирует в OutputDebugString и создаёт корректный модуль
 * (Py_InitModule), чтобы imp.load_dynamic() завершился успешно.
 * Хуков нет — задача спайка: подтвердить, что движок исполняет
 * imp.load_dynamic() из игрового скрипта.
 *
 * Сборка: python3 tools/ldhook/build.py (MSVC x86, python22.lib из vendor).
 * Загрузка:    imp.load_dynamic('ldhook', '<путь>/ldhook.pyd') из ui/ui.pyc.
 */
#include <Python.h>
#include <windows.h>
#include <string.h>

static void ldhook_marker(void)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    OutputDebugStringA("ldhook: initldhook called\n");
    if (n == 0 || n >= MAX_PATH) {
        /* путь к EXE не удался — пишем маркер в cwd */
        strcpy(path, "ldhook_marker.txt");
    } else {
        char *p = strrchr(path, '\\');
        if (p != NULL) {
            p[1] = '\0';
        } else {
            path[0] = '\0';
        }
        strncat(path, "ldhook_marker.txt", sizeof(path) - strlen(path) - 1);
    }
    {
        HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            const char *msg = "ldhook.pyd loaded (spike 0-bis)\r\n";
            WriteFile(h, msg, (DWORD)strlen(msg), &w, NULL);
            CloseHandle(h);
            OutputDebugStringA("ldhook: marker written\n");
        } else {
            OutputDebugStringA("ldhook: marker FAILED\n");
        }
    }
}

static PyMethodDef ldhook_methods[] = {
    {NULL, NULL, 0, NULL}
};

/* DL_EXPORT в 2.2.3 не экспортирует (USE_DL_EXPORT не определён для MSVC в
 * PC/pyconfig.h) — объявляем экспорт явно. methods не может быть NULL:
 * Py_InitModule4 в 2.2.3 разыменовывает methods без проверки. */
__declspec(dllexport) void initldhook(void)
{
    ldhook_marker();
    Py_InitModule("ldhook", ldhook_methods);
}