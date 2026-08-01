# Design v3: Android audio via minimal FFmpeg + lazy archive streaming

Date: 2026-08-01
Target: Zero Hour Android arm64-v8a (`GeneralsMD/` primary, shared `Core/`)
Scope: **audio decoding only**. Video cutscenes explicitly out of scope.

Supersedes two rejected drafts. v1 and v2 were killed by three parallel
adversarial review passes (feasibility / gap / regression). v3 is the surviving
change set: four edits plus a build script, every decision backed by evidence
gathered from this repo and the real game assets.

---

## Evidence base (verified in-session, not assumed)

### The OOM root cause — and why it is a one-flag fix

The engine already ships a lazy archive-file class. The OpenAL port simply
stopped asking for it; the legacy Miles path did ask.

| Fact | Evidence |
|---|---|
| The flag exists and this is literally its documented purpose | `Core/GameEngine/Include/Common/file.h:107` — `STREAMING = 0x00000100, ///< Do not read this file into a ram file, read it as requested.` |
| `openFile` does NOT default to it | `Core/GameEngine/Include/Common/FileSystem.h:153` — `Int access = File::NONE` |
| The archive layer branches on that exact bit | `Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFile.cpp:71-74` — `BitIsSet(access, File::STREAMING)` ? `StreamingArchiveFile` : `RAMFile` |
| `RAMFile` is eager — the whole file, up front | `Core/GameEngine/Source/Common/System/RAMFile.cpp:221` — `m_data = MSGNEW("RAMFILE") Char[size];` then reads all of it |
| `StreamingArchiveFile` allocates nothing | `StreamingArchiveFile::openFromArchive` records `m_file`/`m_startingPos`/`m_size`/`m_curPos` and seeks — no buffer |
| The legacy audio path requested it | `Core/GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp:2934` — `openFile(fileName, File::READ \| File::STREAMING)` |
| Both OpenAL paths do not | `OpenALAudioManager.cpp:793` and `OpenALAudioCache.cpp:142` pass filename only |

Consequence: every audio open on Android eagerly `new[]`s the whole file — the
allocation the README fingers as the ~2.5 min OOM under DXVK's ~17.9 GB virtual
reservation. This is a regression from the Miles path, not a design constraint.

### Codec scope — derived from the shipped assets

Parsed the `.big` TOCs (BIGF magic; BE `num_files`/`hdr_size` at offsets 8/12;
entries of BE offset + BE size + NUL-terminated name), sliced sample entries out
of the header window, and ran host `ffprobe`. No full extractor required.

| Asset class | Archive | Files | Codec | sample_fmt | Rate | Ch |
|---|---|---|---|---|---|---|
| SFX | `Audio.big` | 1720 | `pcm_s16le` | `s16` | 22050 | 1 |
| Speech | `Speech*.big` | 2430 | `adpcm_ima_wav` | `s16p` | 44100 | 1 |
| Music | `Music.big` | 49 | `mp3` | `fltp` | 44100 | 2 |

Extensions are exactly `.wav` and `.mp3`. Nothing else.

### What that evidence rules OUT

- **`pcm_s24le`** — no 24-bit assets exist, so the "S24 → S32 container → handed
  to OpenAL as FLOAT32 = noise" hazard cannot occur. Not enabled.
- **`swresample`** — nothing in the engine calls `swr_*`. All three formats map
  directly onto `getALFormat`: mono/16 (SFX), mono/16 (speech), stereo/32-float
  (music). Differing sample rates need no resampling because OpenAL takes the
  rate per buffer from `frame->sample_rate`. Not linked.
- **A streaming interleave "fix"** — music is genuinely planar (stereo `fltp`),
  but `OpenALAudioManager.cpp:836-853` **already** interleaves it correctly
  (`av_sample_fmt_is_planar` → `av_malloc` → nested sample x channel `memcpy` →
  `av_freep`). v2 proposed replacing this with the cache's slower
  `std::vector::insert` version; that would be a regression. Not touched.
- **`dxvk.conf` VmSize tuning** — `dxvk.maxChunkSize` is an internal allocator
  field, not a config key (`references/fbraz3-dxvk/src/dxvk/dxvk_memory.h:103-116`).
  The premise was false. Dropped.
- **A decoded-size cap** — the cache builds its whole `std::vector` before
  `m_maxSize` is consulted, so a post-decode cap guards the wrong layer. With
  lazy opens and the measured sizes below, it is unnecessary. Dropped.

### Why the memory budget is now safe

Music is the only large asset (a 4.6 MB mp3 decodes to roughly 85 MB) and it
runs through the **streaming** path, which is bounded: a 64 KB `avio` buffer
(`FFmpegFile.cpp:74-82`) feeding at most 32 queued OpenAL buffers
(`OpenALAudioStream.h:29`). It is never cached. The **cache** path handles only
SFX (119 KB, already PCM) and speech (~380 KB ADPCM, ~1.5 MB decoded). With
`File::STREAMING` the eager compressed-file allocation drops to zero on both
paths.

### Define audit

`RTS_HAS_FFMPEG` has exactly one audio consumer and two video consumers:

- audio: `OpenALAudioCache.cpp:5` → retargeted to `RTS_HAS_FFMPEG_AUDIO`
- video: `GeneralsMD/.../W3DGameClient.h:49,122` → left alone, stays on Bink
- desktop: `Core/GameEngineDevice/CMakeLists.txt:327` → untouched, desktop keeps audio+video

Reusing `RTS_HAS_FFMPEG` for audio-only was v1's link break: it makes
`createVideoPlayer()` emit `NEW FFmpegVideoPlayer` while the audio-only build
omits `FFmpegVideoPlayer.cpp`, producing an unresolved symbol. A separate define
is required, not cosmetic.

---

## The change set

### 1. `scripts/build/android/build-ffmpeg-android.sh` (new)

Minimal **static** FFmpeg for `arm64-v8a`, NDK r27, API 24 (matching the
`android-vulkan` preset). Static because it adds nothing to the APK (links into
`libmain.so`) and keeps FFmpeg's C allocator self-contained; needs `--enable-pic`.

Derived codec set, nothing speculative:

```
--disable-everything
--enable-demuxer=wav,mp3
--enable-decoder=pcm_s16le,adpcm_ima_wav,mp3
--enable-parser=mpegaudio
--disable-swresample --disable-swscale --disable-avfilter --disable-avdevice
--disable-programs --disable-doc --disable-network --disable-zlib --disable-lzma
```

Produces `build/ffmpeg-android/{include,lib}` with exactly three archives:
`libavformat.a`, `libavcodec.a`, `libavutil.a`. Pinned + checksummed tarball,
idempotent, fails loudly rather than leaving a half-built prefix.

NEON stays enabled (better mp3 decode); `--disable-asm` is a known fallback only
if the static+PIC link actually fails.

### 2. `Core/GameEngineDevice/CMakeLists.txt:271-307` (rewrite)

Replace the current shared-`.so`, five-library, video-inclusive Android branch
with an audio-only static tier:

- Require **all three** archives *and* the headers before selecting the real
  path — a partial prefix must fall back to the stub, not fail at link.
- Link order `avformat → avcodec → avutil`, wrapped in a link group if the final
  `libmain.so` link reports archive cycles.
- Sources: `FFmpegFile.{h,cpp}` only. **Do not** compile `FFmpegVideoPlayer.cpp`;
  **do not** link `libswscale`/`libswresample` (both currently referenced at
  lines 284 and 293-294 and must be removed).
- Define `RTS_HAS_FFMPEG_AUDIO` on `corei_gameenginedevice_public` INTERFACE,
  mirroring the existing line-296 placement so it propagates to the OpenAL TUs.
- Preserve the enclosing `if(NOT RTS_BUILD_OPTION_FFMPEG)` / `if(ANDROID AND …)`
  scoping and the `else()` desktop fallback exactly.
- Default `SAGE_ANDROID_FFMPEG_DIR` in the `android-vulkan` preset to
  `${sourceDir}/build/ffmpeg-android`.

### 3. Lazy archive opens — the actual OOM fix

| File | Change |
|---|---|
| `OpenALAudioManager.cpp:793` | `openFile(fileToPlay.str(), File::READ \| File::STREAMING)` |
| `OpenALAudioCache.cpp:142` | `openFile(strToFind.str(), File::READ \| File::STREAMING)` |

Restores the access flags the Miles path used, so archive entries resolve to the
lazy `StreamingArchiveFile` instead of the eager `RAMFile`.

### 4. Guard edits

| File:line | Change |
|---|---|
| `OpenALAudioCache.cpp:5` | `!defined(RTS_HAS_FFMPEG)` → `!defined(RTS_HAS_FFMPEG_AUDIO)` |
| `OpenALAudioCache.cpp:94` | `#if defined(__ANDROID__)` → `+ && !defined(RTS_HAS_FFMPEG_AUDIO)` (keeps the `return 0` for stub builds) |
| `OpenALAudioManager.cpp:89` | `#if defined(__ANDROID__)` → `+ && !defined(RTS_HAS_FFMPEG_AUDIO)` (stub include) |

Making #94 conditional rather than deleting it means a build **without** FFmpeg
keeps today's safe OOM-avoiding behaviour; only an FFmpeg-enabled build takes
the real path. No regression for anyone who doesn't run the build script.

`av_register_all()` needs **no** edit — `FFmpegFile.cpp:60-62` already guards it
with `#if LIBAVFORMAT_VERSION_MAJOR < 58`, and the file already uses
`ch_layout.nb_channels` (5.1+ API), so it is FFmpeg 7-ready as-is.

### 5. Packaging

No change. Static linking means nothing new to stage;
`scripts/build/android/package-android-zh.sh` is untouched.

---

## Verification

1. Build script → assert exactly three `.a` files + headers exist.
2. Configure → assert `RTS_HAS_FFMPEG_AUDIO` is defined and `FFmpegFile.cpp`
   compiles; assert `RTS_HAS_FFMPEG` is **not** defined on Android (video stays Bink).
3. Link → inspect the final `libmain.so` link line; assert no `sws_*`/`swr_*`
   and no `FFmpegVideoPlayer` symbols are referenced.
4. Desktop regression → `RTS_BUILD_OPTION_FFMPEG=ON` still defines
   `RTS_HAS_FFMPEG` and still builds audio + video.
5. Device test, three sharp checks:
   - **SFX** audible (menu clicks) → exercises `pcm_s16le` via cache
   - **Speech** audible → exercises `adpcm_ima_wav` via cache
   - **Music** audible in stereo → exercises `mp3`/`fltp` via streaming + interleave
   - Sample `VmRSS`/`VmSize` and watch for a Scudo/mmap-fail signature across a
     5-minute session including in-game past 150 s.

Honest limit on the evidence standard: the device currently runs Mesa Turnip as
its system Vulkan HAL, and DXVK — not the Vulkan driver — owns the virtual
reservation, so Turnip results are indicative but not a stock-driver guarantee.
Treat a clean run as "no regression observed on this configuration," and record
the stock-driver check as an open follow-up rather than claiming it is closed.

I cannot hear the device. "Audible" must be confirmed by the user; what I can
verify programmatically is decode progress and the absence of an OOM signature.

---

## Risks

1. **Static link pulls unexpected transitive deps.** `--disable-everything` does
   not by itself pin the external dependency set; zlib/lzma are explicitly
   disabled above, and the final link line is inspected in step 3.
2. **A codec appears that the sample missed.** The histogram covered 100% of
   entries in three archives, but samples were drawn from the header window.
   Per-file decode failures already degrade gracefully (`FFmpegFile::open`
   returns false → no buffer → that one sound is silent, no crash).
3. **`--disable-asm` may prove necessary** if static+PIC NEON fails to link.
   Known fallback, costs mp3 decode speed only.

## Rejected alternatives

- **Remove the stub and measure (v1 "Approach A").** Re-triggers the OOM through
  a different allocator; the prior author's own commit message reached the same
  conclusion.
- **dxvk.conf VmSize reduction first (v2 "Phase 0").** Built on a config key
  that does not exist.
- **Whole-file size cap (v2 Phase 3 #5).** Guards after the fatal allocation and
  has no specified measurement mechanism.
