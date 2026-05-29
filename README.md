# Z-Man-Game
A 2-D action game inspired by the Mega Man Zero and ZX games

# Build and Run
-To run this game, SDL2 is required
-To install SDl2 for your OS, follow the steps below:

# Windows:
-Install MSYS2
-In MSYS2 UCRT64 terminal, run this:

pacman -S mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-gcc

g++ -std=c++17 main.cpp -
o game $(sdl2-config --cflags --libs) -lSDL2_mixer

./game

# Linux (Debian/Ubuntu):
sudo apt install libsdl2-dev

g++ -std=c++17 main.cpp -
o game $(sdl2-config --cflags --libs) -lSDL2_mixer

./game

# macOS:
brew install sdl2

g++ -std=c++17 main.cpp -
o game $(sdl2-config --cflags --libs) -lSDL2_mixer

./game
