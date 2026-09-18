// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LOADGENERATION_H
#define LOADGENERATION_H

#include <atomic>
#include <cstdint>

/**
 * Monotonic generation token for async decode / soft install.
 *
 * Session Open, Gallery leave, and clearPendingLoads bump the token so
 * in-flight pool jobs reject themselves instead of painting a stale session.
 * Thread-safe: workers read current() without the GUI mutex.
 */
class LoadGeneration
{
public:
    using Value = std::uint64_t;

    Value current() const
    {
        return m_gen.load(std::memory_order_relaxed);
    }

    /** Bump and return the new generation (what new work should capture). */
    Value bump()
    {
        return ++m_gen;
    }

    /** True when @p captured still matches the live generation. */
    bool accepts(Value captured) const
    {
        return captured == current();
    }

private:
    std::atomic<Value> m_gen{0};
};

#endif // LOADGENERATION_H
