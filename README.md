# Doom on Sokol

This is port of the Doom shareware version to the cross-platform [Sokol headers](https://github.com/floooh/sokol).

Web version: https://floooh.github.io/doom-sokol/

Forked from https://github.com/ozkl/doomgeneric

Additional dependencies:

- TinySoundFont by Bernhard Schelling: https://github.com/schellingb/TinySoundFont
- MUS support by Mattias Gustavsson: https://github.com/mattiasgustavsson/doom-crt/blob/main/libs_win32/mus.h

# How to build

Prerequisites:

- cmake 3.x
- python 2.7.x or 3.x
- on Windows: a somewhat recent Visual Studio version
- on macOS: a somewhat recent Xcode version and command line tools
- on Linux and for the WASM build: make
- on Linux: X11, OpenGL and ALSA development packages

On Windows, Linux or macOS:

```sh
mkdir workspace
cd workspace
git clone https://github.com/floooh/doom-sokol
cd doom-sokol
./fips build
./fips run doom
```

To build the web version (in the doom-sokol directory):

```sh
./fips setup emscripten
./fips set config wasm-make-release
./fips build
./fips run doom
```

# Porting Notes

The project has been forked from the [doomgeneric](https://github.com/ozkl/doomgeneric) project
which in turn is a fork of [fbDoom](https://github.com/maximevince/fbDOOM). Doomgeneric
adds callback functions for easier porting of the rendering-, input- and timing-code
to new platforms. This was very useful to get started but in the end didn't help
much because Doom (and Doomgeneric) depend on an "own the game loop" application model,
while sokol_app.h is built around a frame callback model which required some changes
in the "outer" Doom code itself. Eventually nearly all Doomgeneric callbacks ended
up as empty stubs, and it probably would have made more sense to start with
fbDoom, or even the original Doom source code.

The first step was to replace the main() function with the sokol_app.h
application entry and callback functions.

The original main() function is in [i_main.c](https://github.com/ozkl/doomgeneric/blob/master/doomgeneric/i_main.c), this source file has been removed completely. Instead the sokol_app.h entry function
is in [doomgeneric_sokol.c](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L684-L700), along with all other sokol-port-specific code.

After ```sokol_main()``` is called, execution continues at the
[init() callback](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L159). This first initialized all sokol libraries:

- [sokol_time.h](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L161) for measuring frame duration
- [sokol_gfx.h](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L162-L169) for rendering the Doom framebuffer via OpenGL, WebGL, D3D11 or Metal
- [sokol_debugtext.h](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L170-L173) for rendering a intro screen and data load progress  before the actual game starts
- [sokol_fetch.h](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L174-L178) for asynchronously loading data files (the Doom shareware WAD file, and a
soundfont needed for the sound track)
- [sokol_audio.h](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L179-L184) for streaming audio samples to WASAPI, ALSA, WebGL or CoreAudio

Next, [sokol-gfx resource objects are created](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L186-L267) which are needed for rendering the Doom framebuffer (more on that later).

Next, [two asynchronous load operations are started](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L269-L284) to load the required data files (a WAD file and a soundfont file) into memory.

It's important to note that Doom itself isn't initialized yet, this is delayed until
all data has finished loading.

Finally the 'application state' is set to 'loading', which concludes the sokol_app.h 
initialization function.

This is a good time to talk about the general application structure:

All sokol-port-specific data lives in a [single nested data structure](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L75-L129) which is only
accessible from within the doomgeneric_sokol.c source file.

The application goes through several states before running any actual 
Doom code:

- The first state is ```LOADING```, this is active as long as the asynchronous
  data loading isn't finished. During the ```LOADING``` state, [an intro screen](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L316-L351) will be displayed, and a message that loading is in progress.
- For the unlikely case that [loading fails](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L139-L142), the application will switch into
  the ```LOADING_FAILED``` state. During the ```LOADING_FAILED``` state the
  same into screen will be displayed, but with the loading message replaced
  with an error message.
- Once [loading has successfully finished](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L132-L138), the application will switch into the
  ```WAITING``` state. This shows the usual intro screen and the message
  'Press any key to start game'. When this happens.
- When [a key (or mouse button) is pressed](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L531-L538), the application will switch into the
  ```INIT``` state. This is where the actual [Doom initialization code](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L455-L460) runs, the application state switches to ```RUNNING```, and [this is finally](https://github.com/floooh/doom-sokol/blob/204ee61021e311695c038e4a7529531b98a58ebb/src/doomgeneric_sokol.c#L461-L477) where the actual game code runs frame after frame.

## Frame Slicing

(TODO)

## WAD Loading

(TODO)

## Rendering

(TODO)

## Sound

(TODO)