# LS Windowed Mode Addon

**LS Windowed Mode** is an experimental addon for **Lossless Scaling** that introduces a **secondary virtual display**, selectable directly from the Lossless Scaling menu.

When this virtual display is selected:

* the **Lossless Scaling overlay is no longer displayed in fullscreen**
* instead, the overlay is shown **in windowed mode**, allowing for more flexible use cases such as multitasking or specific desktop setups.

## Key Features

*   **Fake Monitor Injection**: Creates a virtual DXGI adapter and output to trick the game and force it to render in a specific mode, bypassing exclusive fullscreen restrictions.
*   **Split Mode**: Allows automatically resizing and positioning the game window in different areas of the screen (Left, Right, Top, Bottom). Ideal for multi-clienting.
*   **Position Mode**: Allows anchoring the window to specific positions.
*   **ImGui Interface**: Includes an in-game graphical overlay to configure settings in real-time.
*   **Logging**: Integrated logging system for debugging.

---

<img width="2269" height="1379" alt="Screenshot 2025-12-27 232140" src="https://github.com/user-attachments/assets/6124ed7b-8deb-4444-9c62-37de5ad1f272" />


---

## Installation

To use this addon, you must **first install the LS Addon Manager (LosslessProxy)**.

### 1. Install LS Addon Manager

Download the latest release of **LS Addon Manager (LosslessProxy)** from:
👉 [https://github.com/FrankBarretta/LS-Addons-Manager](https://github.com/FrankBarretta/LS-Addons-Manager)

Follow the instructions in that repository to complete the installation.

---

### 2. Install LS Windowed Mode

1. Download **`LS_Windowed.dll`** from the addon project’s release page.
2. Go to the **main Lossless Scaling installation folder**.
3. Create a folder named:

   ```
   addons
   ```
4. Inside the `addons` folder, create another folder named:

   ```
   LS_Windowed
   ```
5. Place the downloaded **`LS_Windowed.dll`** file inside the `LS_Windowed` folder.

The final folder structure should look like this:

```
Lossless Scaling/
└── addons/
    └── LS_Windowed/
        └── LS_Windowed.dll
```

---

✅ **Done!**
The addon is now installed and available in Lossless Scaling.

## Requirements

*   **CMake** (version 3.15 or higher)
*   **C++ Compiler** with C++17 support (e.g., Visual Studio 2019/2022)
*   **LosslessProxy**: This addon depends on LosslessProxy headers (`addon_api.hpp`).

## How to Build

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/FrankBarretta/LS-Addon-Windowed-Mode.git
    cd LS-Addon-Windowed-Mode/LS_Windowed
    ```

2.  **Create the build directory**:
    ```bash
    mkdir build
    cd build
    ```

3.  **Configure with CMake**:
    ```bash
    cmake ..
    ```
    *Note: If you are using Visual Studio, this will generate a `.sln` file.*

4.  **Build the project**:
    ```bash
    cmake --build . --config Release
    ```

5.  **Output**:
    The compiled file `LS_Windowed.dll` will be located in the `Release` folder (e.g., `build/Release/LS_Windowed.dll`).

## Technologies Used

*   [MinHook](https://github.com/TsudaKageyu/minhook): For hooking Windows and DirectX APIs.
*   [ImGui](https://github.com/ocornut/imgui): For the graphical user interface.
*   DirectX 11 (D3D11/DXGI): For graphics management and proxying.

## ⚠️ Disclaimer

This add-on is an unofficial extension for Lossless Scaling.

It is NOT affiliated with, endorsed by, or supported by
Lossless Scaling Developers in any way.

This add-on was developed through independent analysis and
reverse engineering of the software behavior. No proprietary
source code or assets are included.

Use at your own risk.

The author assumes no responsibility for any damage, data loss,
account bans, or other consequences resulting from the use of
this add-on.

This add-on may interact with the software at runtime in
non-documented ways.


## Legal Notice / Trademarks

All trademarks, product names, and company names are the property
of their respective owners and are used for identification purposes only.



