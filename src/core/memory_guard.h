// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

namespace Core {

class System;

/// Watches the host's commit charge while a game runs (Windows). Running out of it freezes the
/// whole PC, not just the emulator: near the limit this logs what the emulator holds and pauses
/// emulation instead. EDEN_MEMORY_GUARD=0 turns it off.
class MemoryGuard {
public:
    explicit MemoryGuard(System& system);
    ~MemoryGuard();

    MemoryGuard(const MemoryGuard&) = delete;
    MemoryGuard& operator=(const MemoryGuard&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Core
