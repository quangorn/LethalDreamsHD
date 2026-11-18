# tools/ldhook — спайк Фазы 0-бис (PLAN_proxy_dll.md, §2A)

Проверка .pyd-пути: расширение Python 2.2.3 (`ldhook.pyd`) грузится из
патчимого игрового скрипта явным `imp.load_dynamic()` и исполняет `initldhook`
(маркер в каталоге EXE + OutputDebugString). Хуков нет — подтверждается только
механизм доставки кода.

Канва: `docs/PLAN_proxy_dll.md` (§2A, Фаза 0-бис). Правила: `AGENTS.md`.

## Файлы

| файл | роль |
|---|---|
| `ldhook.c` | исходник .pyd: маркер + `Py_InitModule` (без хуков) |
| `fetch_headers.py` | официальный Python 2.2.3 (python.org): `Include/*.h` + `PC/pyconfig.h` → `vendor/py223/include/` (untracked) |
| `build.py` | MSVC x86 (vswhere + vcvarsall) → `build/ldhook/ldhook.pyd` (gitignored) |
| `spike.py` | полная копия установки → `<scratch>/ldhook/tree`, ldhook.pyd в корень, `ui/ui.pyc` = `patches/ui/ui.py` + блок `imp.load_dynamic`, config для scratch |
| `probe_spike.ps1` | запуск игры из scratch, снимок ОКНА (DPI-aware), гашение, проверка маркера |

## Прогон

```
python3 tools/ldhook/fetch_headers.py
python3 tools/ldhook/build.py
python3 tools/ldhook/spike.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ldhook/probe_spike.ps1 -GameRoot "<scratch>/ldhook/tree"
```

## Гейт

- `<scratch>/ldhook/tree/ldhook_marker.txt` существует → `imp.load_dynamic`
  работает в движке, `initldhook` исполнен;
- снимок `<scratch>/ldhook/tree/shots/<tag>_menu.png` — меню отрисовалось;
- `probe_spike.ps1` rc=0 (rc=2 — маркера нет, путь не подтверждён).

Установка игры не меняется: всё в scratch-дереве (`game.install_dir` подменён
только в `config.toml` спайка).