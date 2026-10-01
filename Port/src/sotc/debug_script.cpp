#include "sotc/debug_script.h"
#include "sotc/function_hooks.h"
#include "sotc/log.h"

#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_save_state.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace sotc
{
    namespace
    {
        constexpr uint32_t kDefaultHook = 0x001C35B0u;

        enum class Op
        {
            Call,
            Poke,
            PokeFloat,
            PokeByte,
            Peek,
            Save,
            Quit,
        };

        struct Command
        {
            uint64_t field = 0;
            bool relative = false;
            Op op = Op::Call;
            uint32_t address = 0;
            uint32_t value = 0;
            float floatValue = 0.0f;
            std::vector<std::pair<int, uint32_t>> gprs;
            std::vector<int> previousResultGprs;
            std::vector<std::pair<int, float>> fprs;
            std::string path;
            std::string text;
        };

        struct Script
        {
            std::vector<Command> commands;
            size_t next = 0;
            uint32_t lastResult = 0;
            bool anchored = false;
            std::mutex mutex;
        };

        uint32_t parseNumber(const std::string &text)
        {
            return static_cast<uint32_t>(std::stoll(text, nullptr, 0));
        }

        uint32_t parseAddress(const std::string &text)
        {
            return static_cast<uint32_t>(std::stoull(text, nullptr, 16));
        }

        int gprIndex(const std::string &name)
        {
            static const char *const names[] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3",
                                                "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
                                                "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
            for (int i = 0; i < 32; ++i)
            {
                if (name == names[i])
                    return i;
            }
            return -1;
        }

        std::vector<std::string> split(const std::string &text, char separator)
        {
            std::vector<std::string> parts;
            std::stringstream stream(text);
            std::string part;
            while (std::getline(stream, part, separator))
                parts.push_back(part);
            return parts;
        }

        bool parseCommand(const std::string &spec, Command &command)
        {
            const std::vector<std::string> parts = split(spec, ':');
            if (parts.size() < 2)
                return false;
            command.text = spec;
            command.relative = !parts[0].empty() && parts[0][0] == '+';
            command.field = std::stoull(command.relative ? parts[0].substr(1) : parts[0]);
            const std::string &op = parts[1];
            if (op == "call" && parts.size() >= 3)
            {
                command.op = Op::Call;
                command.address = parseAddress(parts[2]);
                for (size_t i = 3; i < parts.size(); ++i)
                {
                    const size_t eq = parts[i].find('=');
                    if (eq == std::string::npos)
                        return false;
                    const std::string name = parts[i].substr(0, eq);
                    const std::string value = parts[i].substr(eq + 1);
                    if (name.size() > 1 && name[0] == 'f' && std::isdigit(static_cast<unsigned char>(name[1])))
                    {
                        command.fprs.emplace_back(std::stoi(name.substr(1)), std::stof(value));
                    }
                    else
                    {
                        const int index = gprIndex(name);
                        if (index <= 0)
                            return false;
                        if (value == "$v0")
                            command.previousResultGprs.push_back(index);
                        else
                            command.gprs.emplace_back(index, parseNumber(value));
                    }
                }
                return true;
            }
            if ((op == "poke" || op == "pokeb") && parts.size() == 4)
            {
                command.op = op == "poke" ? Op::Poke : Op::PokeByte;
                command.address = parseAddress(parts[2]);
                command.value = parseNumber(parts[3]);
                return true;
            }
            if (op == "pokef" && parts.size() == 4)
            {
                command.op = Op::PokeFloat;
                command.address = parseAddress(parts[2]);
                command.floatValue = std::stof(parts[3]);
                return true;
            }
            if (op == "peek" && parts.size() >= 3)
            {
                command.op = Op::Peek;
                command.address = parseAddress(parts[2]);
                command.value = parts.size() > 3 ? parseNumber(parts[3]) : 4u;
                return true;
            }
            if (op == "save" && parts.size() >= 3)
            {
                command.op = Op::Save;
                const size_t pathStart = spec.find(":save:") + 6;
                command.path = spec.substr(pathStart);
                return true;
            }
            if (op == "quit")
            {
                command.op = Op::Quit;
                return true;
            }
            return false;
        }

        void peek(uint8_t *rdram, const Command &command)
        {
            std::ostringstream line;
            line << "peek 0x" << std::hex << command.address << ":";
            const uint32_t count = std::min<uint32_t>(command.value, 0x100u);
            for (uint32_t offset = 0; offset < count; offset += 4)
            {
                uint32_t word = 0;
                std::memcpy(&word, rdram + ((command.address + offset) & PS2_RAM_MASK), sizeof(word));
                line << ' ' << std::setw(8) << std::setfill('0') << word;
            }
            SOTC_INFO(Hook, "[debug-script] " << line.str());
        }

        void runImmediate(uint8_t *rdram, PS2Runtime *runtime, const Command &command)
        {
            uint8_t *target = rdram + (command.address & PS2_RAM_MASK);
            switch (command.op)
            {
            case Op::Poke:
                std::memcpy(target, &command.value, sizeof(command.value));
                break;
            case Op::PokeByte:
                *target = static_cast<uint8_t>(command.value);
                break;
            case Op::PokeFloat:
                std::memcpy(target, &command.floatValue, sizeof(command.floatValue));
                break;
            case Op::Peek:
                peek(rdram, command);
                break;
            case Op::Save:
                ps2x::savestate::requestSave(command.path);
                break;
            case Op::Quit:
                runtime->requestStop();
                break;
            case Op::Call:
                break;
            }
        }

        GuestInvocation makeCall(const R5900Context &base, const Command &command, uint64_t field, const std::shared_ptr<Script> &script)
        {
            GuestInvocation invocation{};
            invocation.kind = GuestInvocationKind::HleCall;
            invocation.context = base;
            invocation.context.pc = command.address;
            for (const auto &[index, value] : command.gprs)
                SET_GPR_U32(&invocation.context, index, value);
            for (const int index : command.previousResultGprs)
                SET_GPR_U32(&invocation.context, index, script->lastResult);
            for (const auto &[index, value] : command.fprs)
                invocation.context.f[index & 31] = value;
            SET_GPR_U32(&invocation.context, 29, 0u);
            SET_GPR_U32(&invocation.context, 31, 0u);
            const std::string text = command.text;
            std::weak_ptr<Script> weak = script;
            invocation.onComplete = [text, field, weak](const R5900Context &completed, R5900Context &)
            {
                if (auto owner = weak.lock())
                    owner->lastResult = getRegU32(&completed, 2);
                SOTC_INFO(Hook, "[debug-script] field " << field << " done " << text << " -> v0=0x" << std::hex
                                                        << getRegU32(&completed, 2) << " f0=" << std::dec << completed.f[0]);
            };
            return invocation;
        }
    }

    void installDebugScriptFromEnvironment(PS2Runtime &runtime)
    {
        const char *value = std::getenv("SOTC_DEBUG_SCRIPT");
        if (!value || !*value)
            return;
        auto script = std::make_shared<Script>();
        for (const std::string &spec : split(value, ';'))
        {
            if (spec.empty())
                continue;
            Command command;
            bool ok = false;
            try
            {
                ok = parseCommand(spec, command);
            }
            catch (const std::exception &)
            {
                ok = false;
            }
            if (!ok)
            {
                SOTC_ERROR(Hook, "SOTC_DEBUG_SCRIPT: cannot parse '" << spec << "'");
                continue;
            }
            script->commands.push_back(std::move(command));
        }
        std::stable_sort(script->commands.begin(), script->commands.end(),
                         [](const Command &a, const Command &b) { return a.field < b.field; });
        const char *hookValue = std::getenv("SOTC_DEBUG_HOOK");
        const uint32_t hook = hookValue && *hookValue ? parseAddress(hookValue) : kDefaultHook;
        SOTC_INFO(Hook, "[debug-script] " << script->commands.size() << " commands, run at entries of 0x" << std::hex << hook);
        (void)runtime;
        FunctionHooks::instance().observeEntry(hook, "debug_script", [script](uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt) {
            const uint64_t field = rt->memory().gs().vsyncTick.load();
            std::vector<GuestInvocation> calls;
            {
                std::lock_guard<std::mutex> lock(script->mutex);
                if (!script->anchored)
                {
                    script->anchored = true;
                    for (Command &command : script->commands)
                    {
                        if (command.relative)
                            command.field += field;
                    }
                    std::stable_sort(script->commands.begin(), script->commands.end(),
                                     [](const Command &a, const Command &b) { return a.field < b.field; });
                    SOTC_INFO(Hook, "[debug-script] relative commands anchored at field " << field);
                }
                while (script->next < script->commands.size() && script->commands[script->next].field <= field)
                {
                    const Command &command = script->commands[script->next];
                    if (!calls.empty() && (command.op != Op::Call || !command.previousResultGprs.empty()))
                        break;
                    ++script->next;
                    SOTC_INFO(Hook, "[debug-script] field " << field << ": " << command.text);
                    if (command.op == Op::Call)
                        calls.push_back(makeCall(*ctx, command, field, script));
                    else
                        runImmediate(rdram, rt, command);
                }
            }
            if (!calls.empty())
                rt->eeScheduler().invokeCurrentSequence(std::move(calls));
        });
    }
}
