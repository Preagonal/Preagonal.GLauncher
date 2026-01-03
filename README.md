# GLauncher

Hooks Graal's network stuff to connect to custom servers. Based on [Joey's glauncher](https://github.com/xtjoeytx/glauncher).

Just grab the patched clients from the repo if you don't feel like building.

By default connects to `listserver.graal.in:14911` if you don't have a `license.graal` file.

Windows version hosts the connector script internally for 6.117/Worlds/Era/Steam.

Press **Ctrl+Shift+L** in-game to toggle server selection at startup. When enabled, you'll get a dialog to choose which server to connect to.

Make a `license.graal` file in your game folder:

```
listserver.graal.in
14911
listserver.moreno.land
14911
listserver.graalonline.com
14900
```

Two lines per server - hostname/IP then port.

## Building

### Linux
```bash
mkdir build && cd build
cmake ..
make
```

### Windows
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## Installation

### Linux

**Install SDL:**
```bash
sudo apt install libsdl1.2debian
```
Or whatever your distro's package manager uses.

1. Copy `glauncher.so` to your Graal folder
2. Either patch it permanently:
   ```bash
   patchelf --add-needed ./glauncher.so --set-rpath '$ORIGIN' Graal
   ./Graal
   ```
   Or use LD_PRELOAD each time:
   ```bash
   LD_PRELOAD=./glauncher.so ./Graal
   ```

### Windows

**For v6 client (Graal.exe):**
1. Copy `version.dll` to your Graal folder
2. Launch Graal

**For Worlds/Era/Steam:**
1. Copy `dxgi.dll` to your game folder (don't use version.dll for these)
2. Launch the game