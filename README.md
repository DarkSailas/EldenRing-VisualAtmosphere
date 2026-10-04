# EldenRing-VisualAtmosphere

![Version](https://img.shields.io/badge/version-2.5-blue)
![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)

[English](#english) | [Русский](#русский)

## English

A DLL mod for Elden Ring that changes the game's own color grading: channel tint, saturation, contrast, shadow depth and vignette strength. It ships four cold, desaturated presets in the spirit of Bloodborne, Dark Souls 3 and Demon's Souls, plus one custom slot.

The mod hooks two engine functions with MinHook, the one that builds the color matrix and the one that computes the vignette, and adjusts their output. It adds no post-processing pass, so HDR and frame generation keep working. The only DirectX hook is the one that draws the settings window.

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

The mod draws its own settings window and needs no other mod. `MenuKey` (F5 in the shipped ini, `0` turns it off) opens a window with the preset list and sliders; changes are visible at once and Save writes them to the ini. Esc or the same key closes it.

While the window is open, keyboard and mouse do not reach the game: the character stays still and the camera does not turn. The mod sets the game's own input-pause flag every frame, returns an empty state from DirectInput `GetDeviceState` for the keyboard and mouse, and draws its own cursor. Gamepad input may still get through.

`ToggleKey` (F6 in the shipped ini) switches the effect on and off while playing. If the window cannot be created, the color hooks still work and the ini is the only way to change settings.

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
for s in VisualAtmosphere Overlay dllmain ImGui/imgui ImGui/imgui_draw ImGui/imgui_tables ImGui/imgui_widgets ImGui/imgui_impl_dx12; do
  g++ -O3 -std=c++20 -DNDEBUG -c $s.cpp -o build/$(basename $s).o -I. -IMinhook -IIni -IImGui
done
g++ -O3 -shared -o VisualAtmosphere.dll build/*.o -static -static-libgcc -static-libstdc++ -s -lkernel32 -luser32 -lcomctl32 -ldwmapi -lgdi32 -ld3d12 -ldxgi -ld3dcompiler -limm32 -ldinput8 -ldxguid
```

The result is a single DLL of about 1.1 MB with no runtime dependencies.

### Credits and license

[MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, [mINI](https://github.com/metayeti/mINI) by Danijel Durakovic.

MIT, see [LICENSE](LICENSE). Not affiliated with FromSoftware or Bandai Namco.

## Русский

DLL-мод для Elden Ring, который меняет цветокоррекцию самой игры: оттенок по каналам, насыщенность, контраст, глубину теней и силу виньетки. В комплекте четыре холодных приглушённых пресета в духе Bloodborne, Dark Souls 3 и Demon's Souls и один пользовательский.

Мод через MinHook перехватывает две функции движка — ту, что собирает цветовую матрицу, и ту, что считает виньетку, — и правит их результат. Прохода постобработки он не добавляет, поэтому HDR и генерация кадров продолжают работать. Единственный перехват DirectX нужен для отрисовки окна настроек.

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

Мод сам рисует окно настроек, другие моды для этого не нужны. Клавиша `MenuKey` (в поставляемом ini это F5, `0` — выключено) открывает окно со списком пресетов и ползунками; изменения видны сразу, кнопка Save записывает их в ini. Закрывается окно той же клавишей или Esc.

Пока окно открыто, клавиатура и мышь до игры не доходят: персонаж стоит, камера не вращается. Мод каждый кадр выставляет игровой флаг паузы ввода, отдаёт пустое состояние из `GetDeviceState` в DirectInput для клавиатуры и мыши и рисует свой курсор. Ввод с геймпада может проходить.

Клавиша `ToggleKey` (в поставляемом ini это F6) включает и выключает эффект в игре. Если окно создать не удалось, цветовые перехваты всё равно работают, а настройки меняются только через ini.

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
| `Hotkeys` | `MenuKey` | Код виртуальной клавиши окна настроек. `0x74` — F5, `0` — выключено. |

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
