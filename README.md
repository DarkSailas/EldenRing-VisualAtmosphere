# 🌌 EldenRing-VisualAtmosphere: Native DirectX 12 Atmosphere Engine for Elden Ring

![Version](https://img.shields.io/badge/version-1.0.0-purple?style=for-the-badge)
![Platform](https://img.shields.io/badge/platform-Windows%20x64-blue?style=for-the-badge)
![DirectX](https://img.shields.io/badge/DirectX-12-orange?style=for-the-badge)
![License](https://img.shields.io/badge/license-MIT-green?style=for-the-badge)

[English](#english) | [Русский](#russian)

---

<a name="english"></a>
## 🌌 English

A zero-performance-cost, native DirectX 12 atmosphere and color-grading mod for **Elden Ring** (supporting **v1.12+ / Shadow of the Erdtree**, **The Convergence**, and **Seamless Co-op**).

Unlike ReShade or external screen-space injectors, **VisualAtmosphere** hooks directly into Elden Ring's internal tone mapping matrix (`ComputeColorMatrix`) and vignette rendering pass (`ComputeVignette`) via MinHook. It applies color grading and atmosphere transformations directly inside the engine's native pipeline.

---

### ✨ Key Features

1. ⚡ **Zero Performance Impact**:
   - Computes modifications directly inside the engine's internal tone map matrix.
   - Frame time overhead is effectively **< 0.01 ms** (zero extra draw calls or full-screen shader passes).
   - 100% compatible with **PureDark DLSS 3 Frame Generation**, FSR 3, and HDR displays without hook collisions or frame-pacing degradation.

2. 🌑 **Gothic Dark Fantasy Atmosphere**:
   - **Cold Slate Spectral Filter**: Cuts through the excessive yellow/greenish ambient haze of the Lands Between to deliver a gritty, atmospheric visual style reminiscent of *Dark Souls III* and *Bloodborne*.
   - **Custom RGB Tints**: Fine-tune tone balance across Red, Green, and Blue spectra.

3. 🎬 **Cinematic Vignette & Shadow Depth**:
   - Native vignette intensity and radius controls.
   - Controlled shadow deepening for rich contrast without crushing dark details.

4. ⌨️ **Live Hotkeys**:
   - `F5` (`0x74`): Instantly toggle atmosphere effects ON/OFF during gameplay.
   - `F6` (`0x75`): Hot-reload `VisualAtmosphere.ini` from disk without restarting the game.

---

### 📦 Installation

#### Using Mod Engine 2 / Mod Engine 3 (Recommended)
1. Copy `VisualAtmosphere.dll` and `VisualAtmosphere.ini` into your mod DLL directory:
   - For **The Convergence**: place in `ConvergenceER\mod\dll\`.
   - For **Mod Engine 2 / 3**: place in `mod\dll\`.
2. In your `.me3` profile or `config_eldenring.toml`, register the native library:
   ```toml
   [[natives]]
   path = './../mod/dll/VisualAtmosphere.dll'
   ```
3. Launch the game in offline mode with EAC disabled.

#### Using Elden Mod Loader
1. Install [Elden Mod Loader](https://www.nexusmods.com/eldenring/mods/117).
2. Place `VisualAtmosphere.dll` and `VisualAtmosphere.ini` directly into `ELDEN RING\Game\mods\`.

---

### ⚙️ Configuration (`VisualAtmosphere.ini`)

```ini
[Atmosphere]
; Enable atmosphere enhancement (true / false)
EnableAtmosphere = true

; Cold spectral filter (removes warm/yellowish atmospheric haze)
; Range: 0.00 (off) to 0.50 (heavy). Recommended: 0.10 - 0.14
ColdTintIntensity = 0.12
ColdTintR = 8
ColdTintG = 20
ColdTintB = 36

; Cinematic vignette intensity around screen edges
; Range: 0.00 (off) to 0.80 (pronounced). Recommended: 0.20 - 0.25
VignetteIntensity = 0.22

; Vignette coverage radius from screen perimeter (0.15 - 0.35)
VignetteSize = 0.25

; Deepens shadows and enhances midtone contrast
; Range: 0.00 (off) to 0.20 (deep black). Recommended: 0.04 - 0.07
ShadowDepth = 0.05

[Hotkeys]
; Hotkey to toggle effects on/off on the fly (0x74 = F5)
ToggleKey = 0x74

; Hotkey to hot-reload configuration from disk (0x75 = F6)
ReloadKey = 0x75
```

---

### 🛠️ Building from Source

Requires **MinGW-w64** GCC / G++ (v13 or newer) with C++20 support:
```bash
python build.py
```
Outputs `VisualAtmosphere.dll` (stripped, static runtime, ~1.1 MB).

---

<a name="russian"></a>
## 🌌 Русский

Высокопроизводительный нативный мод цветокоррекции и атмосферы на базе DirectX 12 для **Elden Ring** (полная поддержка **v1.12+ / Shadow of the Erdtree**, **The Convergence** и **Seamless Co-op**).

В отличие от ReShade и внешних инжекторов шейдеров, **VisualAtmosphere** перехватывает функции движка `ComputeColorMatrix` и `ComputeVignette` через библиотеку MinHook и модифицирует матрицу тональной компрессии непосредственно внутри конвейера игры.

---

### ✨ Основные возможности

1. ⚡ **Нулевое влияние на FPS**:
   - Расчет выполняется прямо внутри матрицы цветности движка.
   - Задержка кадра составляет менее **0.01 мс** (нет дополнительных полноэкранных проходов постобработки).
   - Полная совместимость с генерацией кадров **DLSS 3 Frame Generation (PureDark)** и HDR мониторами.

2. 🌑 **Атмосфера Dark Fantasy**:
   - **Холодный спектральный фильтр**: срезает излишнюю желтизну и теплый туман Междуземья, придавая игре мрачную эстетику в духе *Dark Souls III* и *Bloodborne*.
   - Точная настройка каналов RGB.

3. 🎬 **Кинематографическая виньетка и глубина теней**:
   - Мягкое затемнение по краям экрана с настраиваемым радиусом.
   - Увеличение глубины теней без завала деталей в абсолютно черный цвет.

4. ⌨️ **Управление на лету**:
   - Клавиша `F5` (`0x74`): мгновенное включение и отключение мода во время игры.
   - Клавиша `F6` (`0x75`): горячая перезагрузка параметров из `VisualAtmosphere.ini` без перезапуска игры.

---

### 📦 Установка

1. Скопируйте `VisualAtmosphere.dll` и `VisualAtmosphere.ini` в папку с DLL модами:
   - Для **ConvergenceER**: `ConvergenceER\mod\dll\`
   - Для **Mod Engine 2 / 3**: `mod\dll\`
2. В файле конфигурации профиля (`.me3` или `config_eldenring.toml`) добавьте:
   ```toml
   [[natives]]
   path = './../mod/dll/VisualAtmosphere.dll'
   ```
3. Запустите игру без античита.
