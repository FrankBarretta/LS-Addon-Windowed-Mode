# LS Windowed Mode Addon

This project is an addon for **LosslessProxy** designed to improve the overlay management of Lossless Scaling. It implements an advanced windowed mode, split-screen functionality, and a virtual monitor to force the desired behavior of the graphics engine.

## Key Features

*   **Fake Monitor Injection**: Creates a virtual DXGI adapter and output to trick the game and force it to render in a specific mode, bypassing exclusive fullscreen restrictions.
*   **Split Mode**: Allows automatically resizing and positioning the game window in different areas of the screen (Left, Right, Top, Bottom). Ideal for multi-clienting.
*   **Position Mode**: Allows anchoring the window to specific positions.
*   **ImGui Interface**: Includes an in-game graphical overlay to configure settings in real-time.
*   **Logging**: Integrated logging system for debugging.

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

## Installation and Usage

1.  Ensure you have **LosslessProxy** installed and configured for the target game.
2.  Copy the generated `LS_Windowed.dll` file into the folder where LosslessProxy loads addons (i.e., the `addons` folder in the main Lossless Scaling directory).
3.  Start the game.
4.  The addon should be loaded automatically.
5.  The configuration interface (ImGui) should be accessible via the LosslessProxy overlay or configured hotkeys.

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



