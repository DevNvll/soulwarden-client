#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "SwcUi/Draw.h"
#include "SwcUi/Export.h"
#include "SwcUi/Model.h"

struct lua_State;

// The UI runtime: the element model, the embedded Lua 5.1.5 that runs the original scripts of the game, and the
// emulation of the buses that the scripts call (docs/UI.md). The emulation is Lua code of this project
// (unreal/SoulwardenClient/Lua/swc); the natives of this file give it the element model. Each bus method that the
// emulation does not have is a stub: it returns nil and the stub log counts it (the run-time surface of a screen).
//
// The runtime runs on one thread (the game thread of the client). It has no engine header: tools/test-ui.sh builds it
// with the system compiler and runs the HUD offline.
namespace swc::ui {

class SWCUI_EXPORT Runtime {
public:
    struct Options {
        /** The converted UI content (SWC_CONTENT/ui). */
        std::string contentDir;
        /** The emulation scripts of the project (unreal/SoulwardenClient/Lua). */
        std::string luaDir;
        /** The stub log, the error log, and the list of missing textures go here. */
        std::string reportDir;
        /** The canvases that ScriptLoadCanvas loads (game paths with no ending, lower case); each other canvas is a
         *  load failure for the scripts (the screens of later stages). Empty: each canvas. */
        std::vector<std::string> allowedCanvases;
    };

    struct Counts {
        std::uint64_t luaErrors{0};
        std::uint64_t stubCalls{0};
        std::uint64_t stubNames{0};
        std::uint64_t ticks{0};
    };

    explicit Runtime(Options options);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /** Opens Lua, installs the loader and the natives, and runs the boot script of the emulation. */
    bool start(std::string* error);
    [[nodiscard]] bool started() const { return m_lua != nullptr; }

    void set_viewport(float width, float height);
    /** The text metrics for the layout of the texts (GetTextWidth, wrap, shrink to fit). It stays owned by the caller. */
    void set_text_measure(TextMeasure* measure) { m_measure = measure; }

    /** Calls a function of the emulation: Swc.<name>(arguments). The arguments are numbers, booleans, or texts.
     *  @return False when the call raised a Lua error (the log has it). */
    struct Arg {
        enum class Type { nil, number, boolean, text } type{Type::nil};
        double number{0.0};
        bool boolean{false};
        std::string text;
        static Arg of(double v) { Arg a; a.type = Type::number; a.number = v; return a; }
        static Arg of(bool v) { Arg a; a.type = Type::boolean; a.boolean = v; return a; }
        static Arg of(std::string v) { Arg a; a.type = Type::text; a.text = std::move(v); return a; }
        static Arg nil() { return {}; }
    };
    bool call(const std::string& name, const std::vector<Arg>& args = {});

    /** One frame: the tick buses, the tweens, the flipbooks of the emulation. */
    void tick(double deltaSeconds);

    /** Builds the draw list of the canvases (after the tick). */
    void build_draw_list(DrawList& out);

    [[nodiscard]] Model& model() { return *m_model; }
    [[nodiscard]] const Counts& counts() const { return m_counts; }
    /** The log lines since the last call: Lua errors, the first call of each stub, the runtime notes. */
    std::vector<std::string> take_log();
    /** Writes the stub log (ui-stubs.json, ui-stubs.txt), the error log (ui-errors.txt), and the missing textures
     *  (SWC_WORK/ui/wanted.json is the caller's choice: wanted.json in the report folder). */
    bool write_reports(std::string* error);

    // Used by the natives (LuaHost.cpp).
    struct Stub {
        std::string kind;
        std::string name;
        std::uint64_t count{0};
        std::string first;   // the script of the first call
    };
    void note_stub(const std::string& kind, const std::string& name, const std::string& where);
    void note_error(const std::string& text);
    void note(const std::string& text);
    [[nodiscard]] TextMeasure* measure() const { return m_measure; }
    [[nodiscard]] const Options& options() const { return m_options; }
    [[nodiscard]] double clock() const { return m_clock; }

private:
    bool run_file(const std::string& path, std::string* error);

    Options m_options;
    std::unique_ptr<Model> m_model;
    lua_State* m_lua{nullptr};
    TextMeasure* m_measure{nullptr};
    Counts m_counts;
    std::map<std::string, Stub> m_stubs;
    std::vector<std::string> m_errors;
    std::map<std::string, std::uint64_t> m_errorCounts;
    std::vector<std::string> m_log;
    double m_clock{0.0};
};

} // namespace swc::ui
