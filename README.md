# EldenRing-VisualAtmosphere

![Version](https://img.shields.io/badge/version-2.4-blue)
![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)

[English](#english) | [Русский](#русский)

## English

A DLL mod for Elden Ring that changes the game's own color grading: channel tint, saturation, contrast, shadow depth and vignette strength. It ships four cold, desaturated presets in the spirit of Bloodborne, Dark Souls 3 and Demon's Souls, plus one custom slot.

The mod hooks two engine functions with MinHook, the one that builds the color matrix and the one that computes the vignette, and adjusts their output. It does not hook DirectX, draw anything or add a post-processing pass, so it does not conflict with overlays, frame generation mods or HDR.

Both functions are found by byte signature. Tested on `eldenring.exe` 2.7.1 with me3 0.13.0, The Convergence and Seamless Co-op. If a signature is not found after a game update, the mod writes that to `VisualAtmosphere.log` and leaves the picture unchanged.

> [!WARNING]
> DLL mods do not load with Easy Anti-Cheat running. Play offline with EAC disabled (Mod Engine and me3 do this for you), or use Seamless Co-op.

### Presets

| `Preset` | Name | R / G / B | Saturation | Contrast | Shadow depth | Vignette |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Bloodborne (Yharnam Noir) | 0.96 / 0.95 / 1.00 | 0.82 | 1.05 | 0.01 | 0.15 |
| 1 | Dark Souls 3 (Lothric Ashen) | 0.94 / 0.94 / 0.97 | 0.75 | 1.04 | 0.01 | 0.12 |
| 2 | Demon's Souls (Boletaria Deep Cold) | 0.92 / 0.93 / 1.03 | 0.78 | 1.07 | 0.02 | 0.18 |
| 3 | Subtle Cold Slate | 0.98 / 0.98 / 1.01 | 0.88 | 1.02 | 0.00 | 0.10 |
| 4 | Custom | from `[CustomPreset]` | | | | |

### Installation

Take `VisualAtmosphere.dll` and `VisualAtmosphere.ini` from the [`release`](release) folder.

**me3**: put both files into the `dll` folder of your mod and add the DLL to the profile:

```toml
[[natives]]
path = './../mod/dll/VisualAtmosphere.dll'
```

**Mod Engine 2**: add the DLL path to `external_dlls` in `config_eldenring.toml`.

**[Elden Mod Loader](https://www.nexusmods.com/eldenring/mods/117)**: put both files into `ELDEN RING\Game\mods`.

### Settings menu

The DLL has no overlay of its own. The in-game menu is drawn by [EldenRing-PostureBarMod](https://github.com/DarkSailas/EldenRing-PostureBarMod), which loads this DLL and calls its exports `VA_GetConfig`, `VA_SaveConfig` and `VA_ApplyPreset`. With both mods installed, F5 opens a window with the preset list and sliders; changes are visible at once and Save writes them to the ini. The menu key is fixed in PostureBarMod, so `MenuKey` in the ini does not move it.

Without PostureBarMod the mod works from the ini alone: edit the file, restart the game, and use `ToggleKey` (F6 in the shipped ini) to switch the effect on and off while playing.

### Configuration

`VisualAtmosphere.ini` lives next to the DLL.

```ini
[Atmosphere]
EnableAtmosphere = true
; 0-3 built-in presets, 4 custom
Preset = 2
; The values below are read only when Preset = 4
ScaleR = 0.920000
ScaleG = 0.930000
ScaleB = 1.030000
Saturation = 0.780000
Contrast = 1.070000
ShadowDepth = 0.020000
VignetteIntensity = 0.180000

[CustomPreset]
; Values stored for preset 4
ScaleR = 0.920000
ScaleG = 0.920000
ScaleB = 1.060000
Saturation = 0.720000
Contrast = 1.100000
ShadowDepth = 0.025000
VignetteIntensity = 0.250000

[Hotkeys]
; Virtual-key codes. 0x74 = F5, 0x75 = F6, 0 = off
MenuKey = 0x74
ToggleKey = 0x75
```

`ScaleR/G/B` multiply the color channels, `Saturation` and `Contrast` are 1.0 for the unmodified picture, `ShadowDepth` lowers the black level, `VignetteIntensity` darkens the corners. If the ini is missing, preset 0 is used and the toggle key is off.

### Building

MinGW-w64 with g++ that supports C++20 (WinLibs works). With Python:

```
python build.py
```

Without Python:

```
mkdir build
gcc -O3 -c Minhook/buffer.c     -o build/buffer.o     -I. -IMinhook
gcc -O3 -c Minhook/hde64.c      -o build/hde64.o      -I. -IMinhook
gcc -O3 -c Minhook/hook.c       -o build/hook.o       -I. -IMinhook
gcc -O3 -c Minhook/trampoline.c -o build/trampoline.o -I. -IMinhook
g++ -O3 -std=c++20 -c VisualAtmosphere.cpp -o build/VisualAtmosphere.o -I. -IMinhook -IIni
g++ -O3 -std=c++20 -c dllmain.cpp          -o build/dllmain.o          -I. -IMinhook -IIni
g++ -O3 -shared -o VisualAtmosphere.dll build/*.o -static -static-libgcc -static-libstdc++ -s -lkernel32 -luser32 -lcomctl32 -ldwmapi -lgdi32
```

The result is a single DLL of about 1.1 MB with no runtime dependencies.

### Credits and license

[MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, [mINI](https://github.com/metayeti/mINI) by Danijel Durakovic.

MIT, see [LICENSE](LICENSE). Not affiliated with FromSoftware or Bandai Namco.

## Русский

DLL-мод для Elden Ring, который меняет цветокоррекцию самой игры: оттенок по каналам, насыщенность, контраст, глубину теней и силу виньетки. В комплекте четыре холодных приглушённых пресета в духе Bloodborne, Dark Souls 3 и Demon's Souls и один пользовательский.

Мод через MinHook перехватывает две функции движка — ту, что собирает цветовую матрицу, и ту, что считает виньетку, — и правит их результат. DirectX он не трогает, ничего не рисует и не добавляет проход постобработки, поэтому не конфликтует с оверлеями, модами генерации кадров и HDR.

Обе функции ищутся по сигнатуре. Проверено на `eldenring.exe` 2.7.1 с me3 0.13.0, The Convergence и Seamless Co-op. Если после обновления игры сигнатура не найдётся, мод напишет об этом в `VisualAtmosphere.log` и оставит картинку как есть.

> [!WARNING]
> С включённым Easy Anti-Cheat DLL-моды не загружаются. Играйте офлайн с отключённым EAC (Mod Engine и me3 делают это сами) или через Seamless Co-op.

### Пресеты

| `Preset` | Название | R / G / B | Насыщенность | Контраст | Глубина теней | Виньетка |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Bloodborne (Yharnam Noir) | 0.96 / 0.95 / 1.00 | 0.82 | 1.05 | 0.01 | 0.15 |
| 1 | Dark Souls 3 (Lothric Ashen) | 0.94 / 0.94 / 0.97 | 0.75 | 1.04 | 0.01 | 0.12 |
| 2 | Demon's Souls (Boletaria Deep Cold) | 0.92 / 0.93 / 1.03 | 0.78 | 1.07 | 0.02 | 0.18 |
| 3 | Subtle Cold Slate | 0.98 / 0.98 / 1.01 | 0.88 | 1.02 | 0.00 | 0.10 |
| 4 | Пользовательский | из `[CustomPreset]` | | | | |

### Установка

Возьмите `VisualAtmosphere.dll` и `VisualAtmosphere.ini` из папки [`release`](release).

**me3**: положите оба файла в папку `dll` мода и добавьте DLL в профиль:

```toml
[[natives]]
path = './../mod/dll/VisualAtmosphere.dll'
```

**Mod Engine 2**: добавьте путь к DLL в `external_dlls` в `config_eldenring.toml`.

**[Elden Mod Loader](https://www.nexusmods.com/eldenring/mods/117)**: положите оба файла в `ELDEN RING\Game\mods`.

### Меню настроек

Своего оверлея у DLL нет. Меню в игре рисует [EldenRing-PostureBarMod](https://github.com/DarkSailas/EldenRing-PostureBarMod): он подгружает эту DLL и вызывает её экспорты `VA_GetConfig`, `VA_SaveConfig` и `VA_ApplyPreset`. Когда стоят оба мода, F5 открывает окно со списком пресетов и ползунками; изменения видны сразу, кнопка Save записывает их в ini. Клавиша меню задана в PostureBarMod, параметр `MenuKey` в ini её не меняет.

Без PostureBarMod мод работает только от ini: поправьте файл, перезапустите игру, а эффект в игре включайте и выключайте клавишей `ToggleKey` (в поставляемом ini это F6).

### Настройка

`VisualAtmosphere.ini` лежит рядом с DLL. Пример файла — в английском разделе выше.

| Раздел | Ключ | Описание |
| --- | --- | --- |
| `Atmosphere` | `EnableAtmosphere` | Включён ли эффект при старте. |
| `Atmosphere` | `Preset` | 0–3 — встроенные пресеты, 4 — пользовательский. |
| `Atmosphere` | `ScaleR`, `ScaleG`, `ScaleB` | Множители цветовых каналов. Читаются только при `Preset = 4`. |
| `Atmosphere` | `Saturation`, `Contrast` | 1.0 — картинка без изменений. Читаются только при `Preset = 4`. |
| `Atmosphere` | `ShadowDepth` | Опускает уровень чёрного. Читается только при `Preset = 4`. |
| `Atmosphere` | `VignetteIntensity` | Затемнение углов. Читается только при `Preset = 4`. |
| `CustomPreset` | те же семь ключей | Значения, сохранённые для пресета 4. |
| `Hotkeys` | `ToggleKey` | Код виртуальной клавиши для включения и выключения эффекта. `0x75` — F6, `0` — выключено. |
| `Hotkeys` | `MenuKey` | Сохраняется в файле; клавишу меню PostureBarMod не меняет. |

Если ini нет, берётся пресет 0, клавиша переключения выключена.

### Сборка

Нужен MinGW-w64 с g++ и поддержкой C++20 (подойдёт WinLibs). С Python:

```
python build.py
```

Без Python — команды из английского раздела «Building». Получается одна DLL около 1,1 МБ без внешних зависимостей.

### Благодарности и лицензия

[MinHook](https://github.com/TsudaKageyu/minhook) — Tsuda Kageyu, [mINI](https://github.com/metayeti/mINI) — Danijel Durakovic.

MIT, см. [LICENSE](LICENSE). Проект не связан с FromSoftware и Bandai Namco.
