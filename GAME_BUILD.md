# Build and run

Run `build.bat` from this project folder for the optimized Release build.
`build.bat Release game` is equivalent. Use `build.bat Debug` for a debug build.

The only published build output is `Game/Game.exe`. It launches as a Windows
GUI application without opening a console. Command-line tools retain redirected
output and can use an existing parent terminal.

The build uses the installed private x64 GCC 17 toolchain at
`C:/msys64/ucrt64/bin/g++.exe`, C++29 and reflection. Release retains `-O3`,
`-flto=12`, `-fno-semantic-interposition`, section garbage collection, static
linking, RTTI and the embedded icon. Intermediate files stay in `.build/`.

Run `Game/Game.exe` to play. The `Game` directory also contains the installed
`Data`, `save`, `My Postcards` and `SndReverb.map` files. Saves and graphics
settings were copied independently from the existing installation.

The source is in `src/` and `include/simcoaster/`, with build resources in
`assets/`, `third_party/` and `cmake/`. No original executable, tests, compiler
reproducers or IDA database is required to build. The three movie decoder tables
were extracted byte-for-byte into `assets/` from the verified original image.

The local `Game/` installation, `assets/` and build outputs are ignored by Git.
Source and build scripts are tracked; the build uses the local assets directory.
