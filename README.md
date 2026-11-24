
# Bad Apple!! in The 3D Gamemaker
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Running **Bad Apple!!** in **The 3D Gamemaker** through a custom player object.

<p align="center">
  <a href="https://youtu.be/4S0xHYIzcGo">
    <img src="https://img.youtube.com/vi/4S0xHYIzcGo/maxresdefault.jpg" width="800" alt="Bad Apple!! in The 3D Gamemaker">
  </a>
  <br>
  <em>Bad Apple!! in The 3D Gamemaker (2001)</em>
</p>

<details>
<summary><b>Contents</b></summary>

- [Build](#build)
  - [Dependencies](#dependencies)
  - [Compilation](#compilation)
- [Usage](#usage)
  - [Input configuration](#input-configuration)
  - [Generate](#generate)
  - [Use generated asset in T3DGM](#use-generated-asset-in-t3dgm)
- [Rundown](#rundown)
  - [In-engine](#in-engine)
  - [Generator](#generator)
  - [Technical challenges](#technical-challenges)
    - [Hierarchy limitations](#hierarchy-limitations)
    - [Animation record ordering](#animation-record-ordering)
    - [Animation interpolation](#animation-interpolation)
    - [Numeric serialization](#numeric-serialization)
    - [Grouped binary vs. text .x models](#grouped-binary-vs-text-x-models)
    - [Animation keys](#animation-keys)
    - [Geometry processing](#geometry-processing)
    - [The animation speed is tied to the framerate](#the-animation-speed-is-tied-to-the-framerate)
- [AI disclosure](#ai-disclosure)
- [License](#license)
- [Credits](#credits)

</details>


## Build
### Dependencies
- Windows 10 or 11
- Python 3.11+
- CMake 3.20+
- Ninja
- A C++17 Windows toolchain with Media Foundation headers and libraries (tested with LLVM-MinGW)
- NumPy 2.3.4 and SciPy 1.16.3

Install the Python dependencies with:
```bash
python -m pip install -r requirements.txt
```
[Mapbox Earcut](https://github.com/mapbox/earcut.hpp) is included under `third_party/earcut`.

### Compilation
From the root of the repository with compiler, CMake and Ninja on PATH run:
```powershell
python -m pip install -r requirements.txt
cmake -S . -B ../badapple-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++
cmake --build ../badapple-build
```
## Usage
### Input configuration
This project was only designed for my source files which are **not included** and won't be adapted by me to perform well with other configurations.
- MP4: 960x720, 4:3, 60 fps, supplying at least 219.5 seconds of the sequence (the final picture can repeat at the tail), short videos are rejected. The MP4 audio is ignored
- WAV: stereo, 44100 Hz, signed 16-bit uncompressed PCM, exactly 9,666,383 sample frames
- The final sequence will have 8,780 pictures at 320x240 40 FPS
- RGB32 source samples use the original integer box average and threshold >=128
- Frame selection uses 1 ms timestamp tolerance

### Generate
Run:
```powershell
python generator/generate.py --video "PATH/TO/bad-apple.mp4" --audio "PATH/TO/bad-apple.wav" --tools ../badapple-build --work ../badapple-work --output ../badapple-output
```
Output must be new or empty. Working files are an optional local cache. The raw fitting passes run in parallel and may take substantial CPU time.

The output tree is like so:
```
Objects/09User/2Player/badapple/
    badapple.x
    model.dat
    apple.wav
    thumb.bmp
generation.json
```

### Use generated asset in T3DGM
Copy the ```badapple``` folder into an installation of T3DGM's Objects\09User\2Player\, open T3DGM, select it as your player, choose a bullet, optionally remove scene music and bullet audio, then test. If running on a modern computer, I recommend using [DDrawCompat](https://github.com/narzoul/DDrawCompat).

> [!WARNING]
> The generated asset will not work correctly in saved standalone games, but can be embedded in `.3gm` projects.

## Rundown
>[!IMPORTANT]
> My rundown references the source video and music I used when generating the object. I don't include these in this repository, so if you're looking to generate a project of your own, your mileage may vary; it's purpose-built for the established input configuration and isn't intended to be an all-purpose mp4-to-t3dgm encoder.

Basically, Bad Apple!! in T3DGM works like this.

### In-engine
Keep these rendering limits in mind:
- T3DGM runs at 800x600.
- T3DGM targets 40 FPS. Animation speed is dictated by the framerate; more on this later.
- T3DGM uses Direct3D 7 rendering with DirectDraw presentation, so it relies especially on accurate and fast graphics APIs.

The animation is an ordinary animated 3D binary `.x` model, assigned as an FPS player object. T3DGM and its DarkBASIC runtime are not responsible for any of the video encoding, nor does it decode the video. We don't use unintended behavior, nor do we use arbitrary code, but rather we push the existing behaviors and limits to their absolute breaking points in ways more elaborate than ever imagined for such a simple piece of software. I say "ordinary", but an FPS object being able to play a firing animation is actually itself an undocumented behavior of the engine, as while one official asset "Autocannon" *does* include animation data, it's completely invisible in gameplay and likely unintended for this reason. This is our entry point; we're basically using a "bad apple gun." Fire it, and the animation plays. While the longest animations of stock assets in T3DGM rarely exceed a second of playback, if there is any hard coded limit to how long an animation can last, the 3 minutes and 39 seconds of Bad Apple!! don't reach it.

As for the animation itself, it can be described as a 320x240 40 FPS animation encoded as polygons, giving it its polygonal and blocky look. It's like a flip-book with its pages hidden off screen - white silhouettes that are rapidly moved onto and off of a black backing every frame. This was the method I chose as the best in-between of performance and fidelity. Even if a slightly higher fidelity image could be achieved at 38 FPS, it's **VITAL** that the animation achieves a consistent 40, as a fluctuating framerate results in an unpredictable animation speed, which is no good for a multiple minute long animation intended to be a music video.

Idle and completion are also specific poses. Before firing, the geometry is hidden off-screen, and after the animation completes, it's hidden again. This means you can trigger the animation as many times as you like. T3DGM resets identical sounds from identical sources as well as object animation, so you can't overlap them.

Music is provided through an ordinary imported `.wav` which can be assigned to the model through a generated `model.dat` file, so the only changes in the editor that someone might want to make are to mute the scene's music and remove the player bullet audio.

Assuming default "Keyboard" movement in T3DGM (arrow keys to move, no vertical aim,) the projected screen is positioned relative to the camera to fill out the whole 4:3 viewport. T3DGM's HUD is drawn as a 3D object, and is still visible over the projected screen.

The result is an animated player model which acts as a 320x240 40 FPS video player, a video which is encoded into its animation data.

### Generator

As previously described, neither T3DGM nor DarkBASIC are responsible for encoding the video. T3DGM's 40 fps limit is pretty unusual but still noticeably smoother than 30, so we use a 60 FPS `.mp4` file and down-sample instead of settling for 30. To encode the video, we use a generator supported by Earcut and optimized vigorously by Codex. It works in these sequential processing stages:

1. Sample the supplied `.mp4` video into 320x240 monochrome pictures at 40 pictures per second.
2. Trace the white silhouettes of every picture including their holes. Black regions are considered the screen's backing, so there is no black-or-white polarity selection.
3. Simplify the contours of each silhouette, and then triangulate them with Earcut.
4. Check these simplifications against the reference mask. Pictures that are deemed risky to simplify for a loss of important detail fall back to the earlier, finer polygon approximation - combination of mathematical checks and human review. 3,630 pictures out of 8,780 use the fallback.
5. Remove redundant geometry.
6. Reuse pictures that are exact matches.
7. Pack the picture geometry into spatially separated mesh banks.
8. Generate transformational animation keys that bring the appropriate picture into view.
9. Serialize the model into a binary DirectX `.x` file and generate the `model.dat`.
10. Pack it as a T3DGM player object, including the generator's thumbnail and the supplied `.wav` music file.  

Every picture is made out of encoded polygons and the black backing, no textures or bitmaps. It's possible that a much higher fidelity picture could have been achieved using textures instead of model geometry (I didn't research this,) but that felt like it went against the spirit of the challenge.

Using this generator with the expected video and music file, the finished asset looked like this:
|Property|Result|
|--|--|
|Canvas|320x240|
|Framerate|40 per second|
|Pictures|8,780|
|Duration|219.5~ seconds|
|Vertices|560,507|
|Triangles|495,846|
|Mesh parts|21|
|Hierarchy nodes|23|
|Animation keys|17,295|
|Largest key channel|1,040 keys|
|Binary .x size|15,014,335 bytes|
|Audio|32 kHz, mono, 16-bit PCM|

### Technical challenges
The 3D Gamemaker isn't some entirely self-sufficient application, it's chock full of dependencies on both the runtime it uses (DarkBASIC Classic) and era-appropriate Windows graphics APIs. The goal was to run Bad Apple!! in the original, unmodified software, so adding graphics wrappers which might have improved performance in some cases was off the table. I used a hardware accelerated Dell Latitude E6420 with an SSD swap running Windows XP Professional 32bit SP3 as a baseline of hardware much more powerful than T3DGM was designed for, while still having access to the original graphics APIs.

The layers to T3DGM's runtime, relevant to running Bad Apple!!, are like so:
|Layer|Responsibilities|
|--|--|
|The 3D Gamemaker|Sound and animation from firing, choosing playback speed, hierarchy enumeration, 40 FPS gameplay.|
|DarkBASIC Runtime|Model/animation commands, checklist access, animation-record binding, animation key selection, stored animation increments, rendering operations.|
|Microsoft DirectX .x API|Parsing .x model data, exposing typed records, parsing memory.|
|Windows|Environmental. Proper driver support for its accurate graphics APIs likely contribute to rendering/scheduling/performance.|

#### Hierarchy limitations
The first challenge is a restriction in the model initialization path. T3DGM enumerates the model's hierarchy and reads its entries through DarkBASIC's checklist-string accessor. Inspecting the DarkBASIC runtime embedded in the original 2001 T3DGM executable shows that the accessor accepts indices 1 through 511, but rejects 512. This was exposed by an early model containing 694 hierarchy entries, which crashed when T3DGM attempted to read entry 512.

A hierarchy node, called a `Frame` in the `.x` format, is not an animation picture, but a transformable part of the model which can contain geometry or other nodes. Early generated assets gave individual rectangles their own nodes so their positions and sizes could be animated independently. In hindsight, this turned out to be a poor implementation, as although animated rectangles conserved geometry, each independently controlled rectangle required its own hierarchy entry, imposing a strict budget on how many the asset could use. That isn't a direct restriction on the number of pictures, however, as the same rectangle nodes could be reused throughout a long animation.

Instead, we pack complete picture geometry into 20 mesh banks. Each bank contains many pictures positioned far apart. Moving the bank brings the selected picture into view while its neighboring pictures remain outside the viewport, and the other banks are hidden. Therefore, individual pictures don't need individual hierarchy nodes.

Those 20 bank nodes, together with Root, Screen and black Backing nodes, give the model just 23 hierarchy nodes for the 8,780 picture animation. Additional pictures increase geometry and animation data costs, but don't require an equivalent increase in hierarchy node entries. The 511 entry restriction was confirmed in the model initialization path, and didn't establish any universal limit on animation length or limits within the `.x` format itself.

#### Animation record ordering
Secondly, animation records need to match hierarchy order. If an animation record is omitted for a static node, subsequent records will be shifted into incorrect slots, which creates image distortion. The modern renderer used to test the algorithm missed this limitation, and it was resolved by supplying a dense, correctly ordered record for every node, including static Root and Backing nodes. This correction only added 437 bytes.

#### Animation interpolation
Thirdly, animation keys are naturally interpolated by DarkBASIC. The image's polygons stretch or travel between positions. This was easy to resolve by giving them old/new values at repeated timestamps, forcing them to hold their correct pose. Conceptually:
```
time 10: pose A
time 20: pose A
time 20: pose B
time 30: pose B
time 30: pose C
```
The repeated timestamp results in a jump rather than a morphing interpolation, the expected behavior for frames in a video.

#### Numeric serialization
Fourthly, numeric serialization in the original parser is unforgiving. Integer and floating-point spellings are not safely interchangeable; a field intended as floating-point 1.0 could otherwise contain the integer word 0x00000001, rather than float-one’s 0x3F800000. This is a compatibility requirement of the legacy parser.

#### Grouped binary vs. text .x models
Fifthly, one of the most important optimizations, is that text-format `.x` assets have ENORMOUS overhead far beyond the file's apparent size. This ordinarily doesn't matter, but when we're trying to encode an entire video as an animation, it becomes incredibly expensive. T3DGM uses in its stock asset library exclusively text-format `.x` assets rather than the generator's grouped binary format, so I initially targeted that. But one runtime comparison of assets with equivalent animation data proved the ridiculous difference (sampled figures, not necessarily the absolute peaks):

|Representation|Memory|Timed parsing|
|--|--|--|
|Text|1,672 MiB|109.968 seconds|
|Grouped binary|59 MiB|0.188 seconds|

(Using equivalent large animation data in a separate reader using my machine's legacy DirectX `.x` API. This measured parsing and recursive data traversal and excluded initial enumeration/object creation/file mapping.)

Binary encoding preserves the numeric data exactly, so once it was discovered that T3DGM's DarkBASIC runtime supported this format, it was a no-brainer.

#### Animation keys
Having a lot of animation keys poses a problem for performance. DarkBASIC's key selector searches forward from the beginning of a channel. This means that an asset generated with dynamic rectangles that eventually accumulates a huge number of animation keys causes extreme slowdown, regardless of the image's complexity, even when idle. The generator uses a static geometry atlas, reducing its key count from what would have been millions to the final result of 17,295.

#### Geometry processing
This exposed the cost of geometry processing. Using a static geometry atlas in favor of dynamic rectangles resulted in a trade-off of using far more geometry. The important points are:
- Hiding geometry off-screen does not make it free.
- Large assets run slowly when they're loaded, regardless of if they're actively being animated.
- Removing rear faces from the asset improves filesize and reduces triangles, but doesn't cure the high vertex workload the software is tasked with.
- Using an algorithm that creates polygons fit to the contours of the extracted silhouettes was by far the method with the highest fidelity to acceptable performance.
- This algorithm is further optimized with simplification, with detail safeguarding, using a mixture of mathematical checks and human review. All pictures are optimized where possible, but small features receive tighter detailing. Simplifications that were deemed too risky to the overall fidelity of the image were discarded in favor of the fallback geometry.
The key idea behind the polygonal method is to represent shapes, not pixels or a pixelated decomposition. This had the added benefit of reducing jagged edges, though with the trade-off of arguably less detailed distant geometry.  

What these didn't necessarily prove is hard importer limits of the original engine. These are performance optimizations. Several early tests and assets ran at as low as single digit frame rates and wouldn't have been suitable for playing the animation, but they did technically work. I suspect modern, more optimized graphics wrappers like DDrawCompat could actually improve the performance of heavier assets, but again, this was outside the scope of the project.

#### The animation speed is tied to the framerate
Perhaps the single most annoying limitation of T3DGM is how it compensates for lag by changing the animation speed. When an animation is started on what it estimates to be a framerate different than 40, it uses a cached integer FPS estimate, which it measures over a 1+ second interval, to determine the animation's speed. This playback rate is absolute and doesn't correct itself; if the interval you fire the gun in just so happens to have an estimation of 39 FPS, the animation may play at the incorrect speed, even if every single subsequent estimation is correctly 40 FPS. Additionally, there's an approximately 3-second long startup gate after gameplay initialization where this compensation is deliberately disabled, meaning firing within this threshold may risk an incorrect playback rate. Something along the lines of:

```
When the FPS measurement window exceeds 1000 ms:
    cached_fps = floor(frame_count_delta * 1000 / elapsed_ms)

During the startup gate:
    compensation = 1
After the startup gate:
    compensation = 40 / clamp(cached_fps, 15, 60)

When firing:
    start shooting sound
    start complete model animation
    rate = truncate(clamp(50 * compensation, -100, 100))
    stored_step = float32(rate / 25)

On each eligible animation update:
    coordinate = float32(coordinate + stored_step)
```

This also means that the intended animation speed is unrecoverable at low update rates due to the ±100 rate wrapper. The maximum rate of 100 would give an increment of four coordinates per update - below 20 updates per second, we can't maintain the intended 80 coordinates per second.

This happens even when we're displaying a full 40 FPS - just because an overlay says 40, it doesn't dictate which cached value selected the animation speed. Normal T3DGM assets come literally nowhere near to this animation length, so it would be ordinarily imperceptible. There is, unfortunately, nothing I can do about this. While I've successfully thwarted all performance problems, this is the one thing I can't make consistent.

The best thing you can do is wait about five seconds after loading to fire the gun. My approximate observation is that nine times out of ten, Bad Apple!! will play at the correct speed.

Also, another animation speed oddity is that running T3DGM with DarkBASIC configured with `windowmode=1` for some reason produces different playback timing. No idea why, and I don't care to find out! `windowmode=0` was accepted as the default configuration.

## AI disclosure
The aggressively optimized asset generator is 100% generated by Codex, fit for my specific use, and therefore isn't particularly tidy or designed for adaptability. This was a huge effort dependent on a private project which has been ongoing for several years, but I'd be lying to take credit for the actual code.

## License
The original code in this repository is licensed under the [MIT License](LICENSE).

Third-party dependencies are subject to their respective licenses.

## Credits
- **Bad Apple!!** — Original composition by ZUN (Team Shanghai Alice), from Touhou 4: Lotus Land Story (1998)
- **Bad Apple!! feat. nomico** — Arrangement by Masayoshi Minoshima ([Alstroemeria Records](https://alst.net/)), lyrics by Haruka, vocals by nomico
- **Bad Apple!! Shadow Art PV** — Animation by Anira (あにら). [Original Niconico upload](https://www.nicovideo.jp/watch/sm8628149) (2009)
- **The 3D Gamemaker** by Dark Basic Software/The Game Creators.
- **[Mapbox Earcut](https://github.com/mapbox/earcut.hpp)** by Mapbox and contributors
