#pragma once

#include "ps2_runtime.h"

#include <cstdint>
#include <functional>
#include <string>

namespace sotc
{
    using GuestFunction = PS2Runtime::RecompiledFunction;
    using EntryObserver = std::function<void(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)>;

    enum class ReplacementStatus
    {
        Recompiled,
        Observed,
        Native,
        TemporaryStub,
    };

    struct FunctionBinding
    {
        uint32_t address = 0;
        std::string name;
        ReplacementStatus status = ReplacementStatus::Recompiled;
        std::string reason;
    };

    class FunctionHooks
    {
    public:
        static FunctionHooks &instance();

        void attach(PS2Runtime &runtime);
        bool observeEntry(uint32_t address, std::string name, EntryObserver observer);
        bool replace(uint32_t address, std::string name, GuestFunction implementation, ReplacementStatus status, std::string reason);
        GuestFunction original(uint32_t address) const;
        void logBindings() const;

    private:
        FunctionHooks() = default;
        static void observedTrampoline(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

        PS2Runtime *m_runtime = nullptr;
    };
}
