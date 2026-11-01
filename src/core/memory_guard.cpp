// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <thread>

#include "common/logging.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/memory_guard.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Core {

struct MemoryGuard::Impl {
#ifdef _WIN32
    struct Commit {
        u64 used;
        u64 limit;
    };

    static std::optional<Commit> Read() {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status)) {
            return std::nullopt;
        }
        return Commit{status.ullTotalPageFile - status.ullAvailPageFile, status.ullTotalPageFile};
    }

    static u64 ProcessPrivateBytes() {
        struct Counters {
            DWORD cb;
            DWORD PageFaultCount;
            SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage,
                QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage,
                PeakPagefileUsage, PrivateUsage;
        };
        using Fn = BOOL(WINAPI*)(HANDLE, Counters*, DWORD);
        static const auto get = reinterpret_cast<Fn>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
        Counters counters{};
        counters.cb = sizeof(counters);
        if (get == nullptr || !get(GetCurrentProcess(), &counters, sizeof(counters))) {
            return 0;
        }
        return counters.PrivateUsage;
    }

    void Log(const Commit& commit, const char* what) {
        auto& backing = system.DeviceMemory().buffer;
        const auto resident = backing.ResidentBackingBytes(0, backing.BackingSize());
        LOG_CRITICAL(Core, "Memory guard: {} (system commit {} of {} MiB; emulator private {} MiB, "
                           "guest memory resident {} MiB)",
                     what, commit.used >> 20, commit.limit >> 20, ProcessPrivateBytes() >> 20,
                     resident.value_or(0) >> 20);
    }

    void Run(std::stop_token stop) {
        Common::SetCurrentThreadName("MemoryGuard");
        std::mutex mutex;
        std::condition_variable_any cv;
        std::unique_lock lk{mutex};
        bool warned = false;
        bool paused = false;
        auto last_report = std::chrono::steady_clock::now();
        while (!cv.wait_for(lk, stop, std::chrono::milliseconds{250}, [] { return false; })) {
            const auto commit = Read();
            if (!commit || commit->limit == 0) {
                continue;
            }
            const u64 room = commit->limit - (std::min)(commit->used, commit->limit);
            const auto now = std::chrono::steady_clock::now();
            if (now - last_report >= std::chrono::seconds{30}) {
                last_report = now;
                LOG_INFO(Core, "Memory guard: system commit {} of {} MiB, emulator private {} MiB",
                         commit->used >> 20, commit->limit >> 20, ProcessPrivateBytes() >> 20);
            }
            if (!warned && room < WarnRoom) {
                warned = true;
                Log(*commit, "under 6 GiB of commit left");
            } else if (warned && room > WarnRoom + (u64{2} << 30)) {
                warned = false;
            }
            if (!paused && room < StopRoom) {
                paused = true;
                Log(*commit, "under 2.5 GiB of commit left, pausing emulation before the PC runs out");
                system.Pause();
            }
        }
    }

    static constexpr u64 WarnRoom = u64{6} << 30;
    static constexpr u64 StopRoom = u64{5} << 29;
#endif

    explicit Impl(System& system_) : system{system_} {
#ifdef _WIN32
        const char* value = std::getenv("EDEN_MEMORY_GUARD");
        if (value != nullptr && value[0] == '0') {
            return;
        }
        thread = std::jthread([this](std::stop_token stop) { Run(stop); });
#endif
    }

    System& system;
    std::jthread thread; ///< Last: stopped before anything it reads
};

MemoryGuard::MemoryGuard(System& system) : impl{std::make_unique<Impl>(system)} {}

MemoryGuard::~MemoryGuard() = default;

} // namespace Core
