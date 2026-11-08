// SPDX-License-Identifier: GPL-2.0-or-later
// User settings, read once at start from settings.ini (RESOGUN_CONFIG overrides the path) and then
// from environment variables, which win. One "key=value" per line, '#' starts a comment.
#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "resogun_graphics.h"

namespace ResogunSettings {

/// Speaker layout the game's 7.1 audio is mixed down to (src/runtime_audio.c): the playback
/// device's own, stereo, 5.1 or 7.1. settings.ini and RESOGUN_AUDIO_CHANNELS name them as below.
enum AudioChannels : int { AudioChannelsAuto = 0, AudioStereo, Audio51, Audio71, AudioChannelsCount };
inline constexpr std::string_view AudioChannelKeys[AudioChannelsCount] = {"auto", "stereo", "5.1", "7.1"};
/// A layout key (also "2.0", "6" and "8"); anything else is automatic.
inline int ParseAudioChannels(std::string_view value) {
    for (int c = 0; c < AudioChannelsCount; ++c) {
        if (value == AudioChannelKeys[c]) return c;
    }
    if (value == "2.0" || value == "2") return AudioStereo;
    if (value == "6") return Audio51;
    if (value == "8") return Audio71;
    return AudioChannelsAuto;
}
/// Size the game renders at (key render_resolution): the PS4's 1080p, or the PS4 Pro's 2160p
/// (PS4 Pro mode and a 3840x2160 video out, loader.c mount_boot_config_4k).
enum RenderResolution : int { Render1080p = 0, Render4k, RenderResolutionCount };
inline constexpr std::string_view RenderResolutionKeys[RenderResolutionCount] = {"1080p", "4k"};

struct Values {
    /// ResogunGraphics::DisplayMode (key display_mode: windowed, borderless, fullscreen);
    /// borderless by default, so the picture is scaled to the desktop resolution.
    std::atomic<int> display_mode{ResogunGraphics::Borderless};
    std::atomic<bool> vsync{true};
    /// With VSync off: present at once, tearing allowed (Vulkan Immediate), instead of the newest
    /// frame at each refresh (Mailbox, which some drivers hold to twice the refresh rate).
    std::atomic<bool> tearing{false};
    /// SDL display index the window opens on, -1 the primary (key monitor, RESOGUN_MONITOR).
    std::atomic<int> monitor{-1};
    int startup_monitor = -1;
    std::vector<int> monitors; ///< SDL indices, populated before the guest starts
    std::vector<std::string> monitor_names; ///< SDL's name of each of `monitors`
    /// Resolution (key window, "1920x1080"): the window's size when windowed, the screen mode in
    /// exclusive fullscreen; borderless uses the desktop's. The window follows changes.
    std::atomic<int> window_width{1920}, window_height{1080};
    /// Sizes the start display offers (largest first), listed before the guest starts.
    std::vector<std::pair<int, int>> resolutions;
    /// Skip the start-up logos and notices: the game starts with its own -nosplash switch
    /// (src/loader.c); read once, before the game starts. On by default.
    bool skip_intro = true;
    /// Back (Circle) on the title screen asks whether to close the game (src/runtime_pad.c and
    /// the overlay's exit prompt); read at each press. On by default.
    std::atomic<bool> title_back_exits{true};
    /// The game's frame rate: 60, 90, 120 or 180 (ResogunGraphics::FrameRates). The PS4 vblank
    /// (videoout driver) and the engine's target frame rate (src/runtime_game.c, at each flip)
    /// follow it, so the game speed stays the same; the Display Settings panel changes it live.
    std::atomic<int> frame_rate{60};
    /// Speaker layout of the game's audio (AudioChannels), read when the game opens its ports.
    std::atomic<int> audio_channels{AudioChannelsAuto};
    /// RenderResolution, 4k by default; read once, before the game starts.
    int startup_render_resolution = Render4k;
    /// What the Display Settings pages save for the next start.
    std::atomic<int> render_resolution{Render4k};
    /// HDR output (key hdr, RESOGUN_HDR; off by default): the game is told the display takes
    /// BT.2020 PQ and switches its own output to HDR. Read once, before the game starts.
    bool startup_hdr = false;
    std::atomic<bool> hdr{false};
    /// Whether the window's display offers an HDR10 surface: -1 not known yet, set by the
    /// swapchain (vk_swapchain.cpp).
    std::atomic<int> hdr_display{-1};
};

Values& Get();

/// Reads the file, then the environment overrides. Called once at start. When the save folder has
/// no settings.ini yet (Path()), the default file is copied there so it always holds one.
void Load();

/// Writes the display keys (display_mode, monitor, window, vsync, tearing, frame_rate,
/// render_resolution, hdr) into Path() in place: their lines are replaced, missing ones
/// appended, the rest kept (from the default file until Path() exists).
void SaveDisplay();

/// SaveDisplay() when a display key differs from what Path() holds: called as the game closes, so
/// a change made on a page that was still open is not lost.
void SaveDisplayIfChanged();

/// Refresh rate of the window's display in Hz (0 unknown), looked up at most every 500 ms.
double DisplayRefresh();

/// The frame rate the game runs at: frame_rate, lowered to the highest of FrameRates the window's
/// display refreshes at, since a frame shown later than the engine's target slows the game (it
/// does not skip frames). VSync off with tearing is not bound to the refresh. Any thread.
int EffectiveFrameRate();

} // namespace ResogunSettings
