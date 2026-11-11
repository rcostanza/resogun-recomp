// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <fstream>
#include <unordered_map>
#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/scope_exit.h"
#include "common/types.h"

#ifdef __APPLE__
#include <CoreFoundation/CFBundle.h>
#include <dlfcn.h>
#include <sys/param.h>
#endif

#ifndef MAX_PATH
#ifdef _WIN32
// This is the maximum number of UTF-16 code units permissible in Windows file paths
#define MAX_PATH 260
#include <Shlobj.h>
#include <windows.h>
#else
// This is the maximum number of UTF-8 code units permissible in all other OSes' file paths
#define MAX_PATH 1024
#endif
#endif

namespace Common::FS {

namespace fs = std::filesystem;

#ifdef __APPLE__
using IsTranslocatedURLFunc = Boolean (*)(CFURLRef path, bool* isTranslocated,
                                          CFErrorRef* __nullable error);
using CreateOriginalPathForURLFunc = CFURLRef __nullable (*)(CFURLRef translocatedPath,
                                                             CFErrorRef* __nullable error);

static CFURLRef UntranslocateBundlePath(const CFURLRef bundle_path) {
    if (void* security_handle =
            dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY)) {
        SCOPE_EXIT {
            dlclose(security_handle);
        };

        const auto IsTranslocatedURL = reinterpret_cast<IsTranslocatedURLFunc>(
            dlsym(security_handle, "SecTranslocateIsTranslocatedURL"));
        const auto CreateOriginalPathForURL = reinterpret_cast<CreateOriginalPathForURLFunc>(
            dlsym(security_handle, "SecTranslocateCreateOriginalPathForURL"));

        bool is_translocated = false;
        if (IsTranslocatedURL && CreateOriginalPathForURL &&
            IsTranslocatedURL(bundle_path, &is_translocated, nullptr) && is_translocated) {
            return CreateOriginalPathForURL(bundle_path, nullptr);
        }
    }
    return nullptr;
}

static std::optional<std::filesystem::path> GetBundleParentDirectory() {
    if (CFBundleRef bundle_ref = CFBundleGetMainBundle()) {
        if (CFURLRef bundle_url_ref = CFBundleCopyBundleURL(bundle_ref)) {
            SCOPE_EXIT {
                CFRelease(bundle_url_ref);
            };

            CFURLRef untranslocated_url_ref = UntranslocateBundlePath(bundle_url_ref);
            SCOPE_EXIT {
                if (untranslocated_url_ref) {
                    CFRelease(untranslocated_url_ref);
                }
            };

            char app_bundle_path[MAXPATHLEN];
            if (CFURLGetFileSystemRepresentation(
                    untranslocated_url_ref ? untranslocated_url_ref : bundle_url_ref, true,
                    reinterpret_cast<u8*>(app_bundle_path), sizeof(app_bundle_path))) {
                std::filesystem::path bundle_path{app_bundle_path};
                return bundle_path.parent_path();
            }
        }
    }
    return std::nullopt;
}
#endif

// resogun: built on first use, not before main, so a launch that reads its save folder from
// paths.ini (src/launcher.c) can still set RESOGUN_GPU_USER_DIR first.
static std::unordered_map<PathType, fs::path>& UserPaths() {
    static auto user_paths = [] {
    // resogun: the port keeps GPU caches where RESOGUN_GPU_USER_DIR points.
    const char* port_dir = std::getenv("RESOGUN_GPU_USER_DIR");
    auto user_dir = port_dir ? std::filesystem::path(port_dir) : std::filesystem::current_path() / PORTABLE_DIR;
    // resogun: a folder RESOGUN_GPU_USER_DIR names is created below, never swapped for
    // shadPS4's own (Linux took ~/.local/share/shadPS4 until the folder existed).
    if (!port_dir && !std::filesystem::exists(user_dir)) {
        // If it doesn't exist, use the standard path for the platform instead.
        // NOTE: On Windows we currently just create the portable directory instead.
#ifdef __APPLE__
        user_dir =
            std::filesystem::path(getenv("HOME")) / "Library" / "Application Support" / "shadPS4";
#elif defined(__linux__)
        const char* xdg_data_home = getenv("XDG_DATA_HOME");
        if (xdg_data_home != nullptr && strlen(xdg_data_home) > 0) {
            user_dir = std::filesystem::path(xdg_data_home) / "shadPS4";
        } else {
            user_dir = std::filesystem::path(getenv("HOME")) / ".local" / "share" / "shadPS4";
        }
#elif _WIN32
        // resogun: never shadPS4's %APPDATA% folder (an installed shadPS4 owns it); this runs
        // on first use, so the portable folder is created here (run.py and src/launcher.c set
        // RESOGUN_GPU_USER_DIR).
        std::filesystem::create_directories(user_dir);
#endif
    }

    std::unordered_map<PathType, fs::path> paths;

    const auto create_path = [&](PathType shad_path, const fs::path& new_path) {
        // resogun: with parents, so a Linux home without ~/.local/share still starts.
        std::filesystem::create_directories(new_path);
        paths.insert_or_assign(shad_path, new_path);
    };

    // resogun: only the folders the video core uses (the runtime makes its own: savedata,
    // temp0, download0); not shadPS4's cheats, patches, custom trophy and module folders.
    create_path(PathType::UserDir, user_dir);
    create_path(PathType::LogDir, user_dir / LOG_DIR);
    create_path(PathType::ScreenshotsDir, user_dir / SCREENSHOTS_DIR);
    create_path(PathType::ShaderDir, user_dir / SHADER_DIR);
    create_path(PathType::CacheDir, user_dir / CACHE_DIR);

    return paths;
    }();
    return user_paths;
}

bool ValidatePath(const fs::path& path) {
    if (path.empty()) {
        LOG_ERROR(Common_Filesystem, "Input path is empty, path={}", PathToUTF8String(path));
        return false;
    }

#ifdef _WIN32
    if (path.u16string().size() >= MAX_PATH) {
        LOG_ERROR(Common_Filesystem, "Input path is too long, path={}", PathToUTF8String(path));
        return false;
    }
#else
    if (path.u8string().size() >= MAX_PATH) {
        LOG_ERROR(Common_Filesystem, "Input path is too long, path={}", PathToUTF8String(path));
        return false;
    }
#endif

    return true;
}

std::string PathToUTF8String(const std::filesystem::path& path) {
    const auto u8_string = path.u8string();
    return std::string{u8_string.begin(), u8_string.end()};
}

const fs::path& GetUserPath(PathType shad_path) {
    return UserPaths().at(shad_path);
}

std::string GetUserPathString(PathType shad_path) {
    return PathToUTF8String(GetUserPath(shad_path));
}

// resogun: on the first start with a shaders folder, the cache earlier releases kept elsewhere is
// moved into it: <RESOGUN_GPU_USER_DIR>/cache/<serial> (%LOCALAPPDATA%\Resogun, ~/.cache/resogun
// or paths.ini's cache=), and on Linux shadPS4's folder, which the port used while the first
// one did not exist. Only a folder with the port's disk_shaders.bin marker is taken, so an
// installed shadPS4's own cache stays where it is.
static void MoveOldShaderCache(const fs::path& to, std::string_view serial) {
    std::error_code ec;
    if (fs::exists(to / "profile.bin", ec)) {
        return;
    }
    std::vector<fs::path> old_dirs{GetUserPath(PathType::CacheDir) / serial};
#ifdef __linux__
    const char* data_home = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    if (data_home && data_home[0]) {
        old_dirs.push_back(fs::path(data_home) / "shadPS4" / CACHE_DIR / serial);
    } else if (home && home[0]) {
        old_dirs.push_back(fs::path(home) / ".local" / "share" / "shadPS4" / CACHE_DIR / serial);
    }
#endif
    for (const auto& from : old_dirs) {
        if (!fs::is_regular_file(from / "disk_shaders.bin", ec) || fs::equivalent(from, to, ec)) {
            continue;
        }
        u32 moved = 0, failed = 0;
        for (const auto& entry : fs::directory_iterator{from, ec}) {
            const auto target = to / entry.path().filename();
            std::error_code move_ec;
            fs::remove_all(target, move_ec);
            fs::rename(entry.path(), target, move_ec);
            if (move_ec) {
                // Another drive: copied, then removed.
                move_ec.clear();
                fs::copy(entry.path(), target,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                         move_ec);
                if (!move_ec) {
                    fs::remove_all(entry.path(), move_ec);
                }
            }
            move_ec ? ++failed : ++moved;
        }
        fs::remove(from, ec);
        std::printf("Shader cache: moved %u entries from %s to %s%s\n", moved,
                    PathToUTF8String(from).c_str(), PathToUTF8String(to).c_str(),
                    failed ? fmt::format(" ({} could not be moved)", failed).c_str() : "");
        return;
    }
}

const fs::path& GetShaderCachePath(std::string_view serial) {
    static const fs::path path = [serial] {
        const char* dir = std::getenv("RESOGUN_SHADER_DIR");
        if (!dir || !dir[0]) {
            return GetUserPath(PathType::CacheDir) / serial;
        }
        fs::path shaders{dir};
        std::error_code ec;
        fs::create_directories(shaders, ec);
        MoveOldShaderCache(shaders, serial);
        return shaders;
    }();
    return path;
}

void SetUserPath(PathType shad_path, const fs::path& new_path) {
    if (!std::filesystem::is_directory(new_path)) {
        LOG_ERROR(Common_Filesystem, "Filesystem object at new_path={} is not a directory",
                  PathToUTF8String(new_path));
        return;
    }

    UserPaths().insert_or_assign(shad_path, new_path);
}

std::optional<fs::path> FindGameByID(const fs::path& dir, const std::string& game_id,
                                     int max_depth) {
    if (max_depth < 0) {
        return std::nullopt;
    }

    const auto boot_path_for = [](const fs::path& root) -> std::optional<fs::path> {
        std::error_code ec;
        if (fs::is_directory(root, ec) && !ec) {
            if (!fs::exists(root / "sce_sys" / "param.sfo")) {
                return std::nullopt;
            }
            if (auto eboot_path = root / "eboot.bin"; fs::exists(eboot_path)) {
                return eboot_path;
            }
            return std::nullopt;
        }
        // resogun: packed (ZArchive) game images are not supported by the port.
        return std::nullopt;
    };

    // Check if this is the game we're looking for
    if (dir.filename() == game_id) {
        if (auto found = boot_path_for(dir)) {
            return found;
        }
    }

    if (auto found = boot_path_for(dir / game_id)) {
        return found;
    }
    if (auto found = boot_path_for(dir / (game_id + ".zar"))) {
        return found;
    }

    // Recursively search subdirectories
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        std::error_code entry_ec;
        if (!entry.is_directory(entry_ec) || entry_ec) {
            continue;
        }
        if (auto found = FindGameByID(entry.path(), game_id, max_depth - 1)) {
            return found;
        }
    }

    return std::nullopt;
}

} // namespace Common::FS
