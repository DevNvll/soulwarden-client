// The offline tests of the UI runtime (tools/test-ui.sh): the core rules with made-up data, then a run of the original
// HUD scripts on the converted content (SWC_CONTENT/ui) with no engine. The run writes the stub log, the error log,
// the missing textures, and the draw list (a JSON file for the preview: converter/swc_convert/uipreview.py).
//
// Usage: swc-ui-tests CONTENT_UI LUA_DIR REPORT_DIR [--seconds S] [--draw FILE] [--health H MAX] [--mana M MAX]
//                     [--stamina S MAX] [--only-core] [--all-screens] [--fixtures DIR] [--fixture NAME]... [--no-hud]
//   --fixtures DIR: after the HUD run, each DIR/*.lua (not prelude.lua) runs in a runtime of its own (fixtures of the
//   original scripts: tests/ui/fixtures); --fixture NAME runs only the named fixtures; --no-hud skips the HUD run.
//   --all-screens: the loader of the globals loads each screen of the game, not only the HUD screens of stage 1.10 (a
//   probe of the stub surface of the other screens; their textures are not converted).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>
#include <sstream>
#include <string>
#include <vector>

#include "SwcUi/Draw.h"
#include "SwcUi/Json.h"
#include "SwcUi/Model.h"
#include "SwcUi/Runtime.h"
#include "font_measure.h"

namespace {

FontMeasure g_measure;   // the font metrics of the converted font files (font_measure.cpp)
int g_checks = 0;
int g_failed = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s\n", what.c_str());
    }
}

bool close(float a, float b, float tolerance = 0.01F) {
    return std::fabs(a - b) <= tolerance;
}

/** A canvas in the converted form, written to a temporary content folder. */
void write_test_canvas(const std::string& dir) {
    std::system(("mkdir -p '" + dir + "/canvases/test'").c_str());
    std::ofstream out(dir + "/canvases/test/rules.json");
    // root (canvas rect) -> panel (centered 200x100, pivot 0.5) -> child (stretched with 10 px margins, rotated 90)
    out << R"({"format":1,"kind":"canvas","canvas":{"id":"1","root":"10","size":[1920,1080],"drawOrder":5},
"elements":[
{"id":"10","name":"_root","enabled":true,"children":["11"],"transform":{"anchors":[0.5,0.5,0.5,0.5],"offsets":[-50,-50,50,50],"pivot":[0.5,0.5],"rotation":0,"scale":[1,1]}},
{"id":"11","name":"Panel","enabled":true,"children":["12","13"],"transform":{"anchors":[0.5,0.5,0.5,0.5],"offsets":[-100,-50,100,50],"pivot":[0.5,0.5],"rotation":0,"scale":[1,1]},
 "scripts":[{"script":"x.luac","properties":{"5":{"t":"id","v":"12"},"6":{"t":"id","v":"99"}}}]},
{"id":"12","name":"Child","enabled":true,"children":[],"transform":{"anchors":[0,0,1,1],"offsets":[10,10,-10,-10],"pivot":[0.5,0.5],"rotation":90,"scale":[1,1]},
 "image":{"sprite":null,"color":[1,0,0,1],"alpha":1,"type":0,"fillType":1,"fillAmount":0.5,"fillEdgeOrigin":0}},
{"id":"13","name":"Off","enabled":false,"children":[],"transform":{"anchors":[0,0,0,0],"offsets":[0,0,10,10],"pivot":[0,0],"rotation":0,"scale":[1,1]}}
]})";
}

void core_tests(const std::string& tmp) {
    using namespace swc::ui;
    // JSON
    json::Value v;
    check(json::parse(R"({"a":[1,2,{"b":"xé"}],"c":true,"d":null})", v), "json parse");
    check(v["a"][2]["b"].text() == "x\xc3\xa9", "json unicode escape");
    check(v["c"].boolean() && v["d"].is_null() && v["a"].size() == 3, "json values");
    check(crc32("name") == 1579384326U, "crc32 of a property name (research ui.md 2.4)");
    check(crc32("hud") == 2702338936U, "crc32 of the state HUD");

    // Rect rules
    write_test_canvas(tmp);
    Model model(tmp);
    model.set_viewport(1920, 1080);
    std::string error;
    const Id canvas = model.load_canvas("test/rules.uicanvas", &error);
    check(canvas == 1, "load a canvas: " + error);
    const Edges root = model.rect(10);
    check(close(root.left, 0) && close(root.right, 1920) && close(root.bottom, 1080), "the root has the canvas rect");
    const Edges panel = model.rect(11);
    check(close(panel.left, 860) && close(panel.top, 490) && close(panel.right, 1060) && close(panel.bottom, 590), "anchors and offsets");
    const Edges child = model.rect(12);
    check(close(child.left, 870) && close(child.right, 1050) && close(child.top, 500) && close(child.bottom, 580), "stretched anchors");
    // Rotation 90 around the pivot (960, 540): the top left corner (870, 500) goes to (1000, 450).
    const Vec2 corner = model.to_canvas(12).apply({870, 500});
    check(close(corner.x, 1000) && close(corner.y, 450), "rotation around the pivot");
    const Vec2 local = model.local_position(11);
    check(close(local.x, 0) && close(local.y, 0), "local position of a centered element");
    model.set_local_position(11, {30, -20});
    check(close(model.rect(11).left, 890) && close(model.rect(11).top, 470), "set local position moves the offsets");
    model.set_local_width(11, 300);
    check(close(model.local_width(11), 300) && close(model.rect(11).left, 840), "set local width keeps the pivot");
    check(model.find_descendant(10, "Off") == 13 && !model.enabled_in_hierarchy(13), "find and enabled");

    // Clone: new ids, and a script property that names an element of the subtree names its copy.
    std::vector<Id> created;
    const Id copy = model.clone_element(11, 10, kNoId, &created);
    check(copy != 11 && created.size() == 3, "clone makes the subtree");
    const Element* e = model.element(copy);
    check(e != nullptr && e->scripts.size() == 1, "clone keeps the scripts");
    if (e != nullptr && !e->scripts.empty()) {
        const std::string inner = e->scripts[0].properties["5"]["v"].text();
        const std::string outer = e->scripts[0].properties["6"]["v"].text();
        check(inner == std::to_string(created[1]), "clone remaps an inner id property");
        check(outer == "99", "clone keeps an outer id property");
    }
    std::vector<Id> destroyed;
    model.destroy_element(copy, &destroyed);
    check(destroyed.size() == 3 && model.element(copy) == nullptr, "destroy removes the subtree");

    // Draw: the linear fill of the child gives one quad with half the width (left origin).
    struct Measure final : TextMeasure {
        float width(const std::string&, float size, float, std::string_view text) override { return static_cast<float>(text.size()) * size * 0.5F; }
        float line_height(const std::string&, float size) override { return size; }
    } measure;
    DrawList list;
    build_draw_list(model, measure, list);
    check(list.meshes == 1 && list.items.size() == 1, "one mesh for one image");
    if (!list.items.empty()) {
        check(list.items[0].vertices.size() == 4, "a linear fill is a quad");
    }
}


} // namespace

/** The layout conformance: each canvas of the converted content loads into a model (no scripts); the layouts compute
 *  as at a canvas load; each child of a layout must have the offsets that the editor saved in the canvas. Layouts that
 *  use the default cells of texts are not compared (the offline tests have no font metrics), nor canvases that are not
 *  1920 x 1080. @return The lines of the report. */
std::vector<std::string> layout_conformance(const std::string& content) {
    using namespace swc::ui;
    std::vector<std::string> report;
    int layouts = 0;
    int children = 0;
    int differ = 0;
    int skippedText = 0;
    int skippedStale = 0;
    int textChildren = 0;
    int textDiffer = 0;
    std::map<std::string, int> differByKind;
    const std::filesystem::path root = std::filesystem::path(content) / "canvases";
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& file : files) {
        json::Value doc;
        std::string error;
        if (!json::parse_file(file.string(), doc, &error)) {
            continue;
        }
        Model model(content);
        model.set_viewport(1920.0F, 1080.0F);
        model.set_text_measure(&g_measure);
        const std::string path = doc["source"].text();
        std::unordered_map<Id, Id> idMap;   // a slice: the ids of the data -> the ids of the spawn
        if (doc["canvas"].is_object()) {
            const json::Value& size = doc["canvas"]["size"];
            if (size.size() != 2 || size[0].real() != 1920.0F || size[1].real() != 1080.0F) {
                continue;
            }
            if (model.load_canvas(path, &error) == kNoId) {
                report.push_back("load " + path + ": " + error);
                continue;
            }
        } else {
            // A UI slice: spawned into the root of a canvas of 1920 x 1080 (the globals canvas), then its layouts.
            const Id host = model.load_canvas("lyshineui/globals.dynamicuicanvas", &error);
            const Canvas* c = host != kNoId ? model.canvas(host) : nullptr;
            const Id spawned = c != nullptr ? model.spawn_slice(path, c->root, nullptr, &error, &idMap) : kNoId;
            if (spawned == kNoId) {
                report.push_back("spawn " + path + ": " + error);
                continue;
            }
            model.compute_layout(spawned);
        }
        std::unordered_map<std::string, const json::Value*> saved;
        for (const json::Value& e : doc["elements"].elements()) saved[e["id"].text()] = &e;
        const auto has_text = [&](Id id) {
            for (const Id sub : model.subtree(id)) {
                const Element* e = model.element(sub);
                if (e != nullptr && e->text) return true;
            }
            return false;
        };
        const auto model_id = [&idMap](Id id) {
            const auto found = idMap.find(id);
            return found != idMap.end() ? found->second : id;
        };
        std::unordered_map<Id, Id> dataId;   // the id of the spawn -> the id of the data
        for (const auto& [from, to] : idMap) dataId[to] = from;
        for (const json::Value& data : doc["elements"].elements()) {
            const Id id = model_id(parse_id(data["id"].text()));
            const Element* e = model.element(id);
            if (e == nullptr || !e->layout) continue;
            bool textCells = false;
            if (!e->layout->ignoreDefaultCells || e->fitter) {
                for (const Id child : e->children) textCells = textCells || has_text(child);
            }
            if (textCells) {
                ++skippedText;
                // The layouts with text cells: compared with the font metrics of the tests (a report, no check).
                for (const Id child : e->children) {
                    const Element* c = model.element(child);
                    const auto original = dataId.find(child);
                    const auto found = saved.find(std::to_string(original != dataId.end() ? original->second : child));
                    if (c == nullptr || found == saved.end()) continue;
                    const json::Value& o = (*found->second)["transform"]["offsets"];
                    const Edges& got = c->transform.offsets;
                    const float d = std::max({std::fabs(got.left - o[0].real()), std::fabs(got.top - o[1].real()),
                                              std::fabs(got.right - o[2].real()), std::fabs(got.bottom - o[3].real())});
                    ++textChildren;
                    if (d > 1.0F) {
                        ++textDiffer;
                        if (textDiffer <= 8) {
                            char line[512];
                            std::snprintf(line, sizeof(line), "text cells differ %s %s/%s: got %.2f %.2f %.2f %.2f, saved %.2f %.2f %.2f %.2f",
                                          path.c_str(), e->name.c_str(), c->name.c_str(), got.left, got.top, got.right, got.bottom,
                                          o[0].real(), o[1].real(), o[2].real(), o[3].real());
                            report.emplace_back(line);
                        }
                    }
                }
                continue;
            }
            // Two layouts whose saved child offsets are older than the last edit of the canvas (the saved size of the
            // layout element does not give the saved child offsets; the game computes the layouts again at load).
            static const std::set<std::string> stale = {"lyshineui/guildmenu/guild.dynamicuicanvas PermissionList",
                                                        "lyshineui/main menu/landingscreenv2.dynamicuicanvas WorldInfoMessageHolder"};
            if (stale.count(path + " " + e->name) != 0) {
                ++skippedStale;
                continue;
            }
            ++layouts;
            const std::string kind = e->layout->kind == LayoutData::Kind::grid ? "grid"
                                     : std::string(e->layout->kind == LayoutData::Kind::row ? "row" : "column") + (e->layout->fixed ? " fixed" : "");
            for (const Id child : e->children) {
                const Element* c = model.element(child);
                const auto original = dataId.find(child);
                const auto found = saved.find(std::to_string(original != dataId.end() ? original->second : child));
                if (c == nullptr || found == saved.end()) continue;
                if (c->fitter && has_text(child)) {
                    ++skippedText;   // the fitter of the child takes the size of a text
                    continue;
                }
                const json::Value& o = (*found->second)["transform"]["offsets"];
                ++children;
                const Edges& got = c->transform.offsets;
                const float d = std::max({std::fabs(got.left - o[0].real()), std::fabs(got.top - o[1].real()),
                                          std::fabs(got.right - o[2].real()), std::fabs(got.bottom - o[3].real())});
                if (d > 0.05F) {
                    ++differ;
                    ++differByKind[kind];
                    if (differ <= 12) {
                        char line[512];
                        std::snprintf(line, sizeof(line), "differ %s %s/%s (%s): got %.2f %.2f %.2f %.2f, saved %.2f %.2f %.2f %.2f", path.c_str(),
                                      e->name.c_str(), c->name.c_str(), kind.c_str(), got.left, got.top, got.right, got.bottom, o[0].real(),
                                      o[1].real(), o[2].real(), o[3].real());
                        report.emplace_back(line);
                    }
                }
            }
        }
    }
    char line[256];
    std::snprintf(line, sizeof(line),
                  "layouts: %d layouts, %d children compared, %d differ; not compared: %d with text cells, %d with older saved offsets",
                  layouts, children, differ, skippedText, skippedStale);
    report.insert(report.begin(), line);
    std::snprintf(line, sizeof(line), "layouts with text cells (font metrics of the tests, no check): %d children, %d differ by more than 1 px",
                  textChildren, textDiffer);
    report.insert(report.begin() + 1, line);
    // A guard: the text metrics of the tests (stb) and of CryFont differ by the rounding of the size cache; at least 85%
    // of these children agree within 1 px (the result of the 0.8 glyph rule, docs/UI.md).
    check(textChildren == 0 || textDiffer * 100 <= textChildren * 15, "the layouts with text cells agree within 1 px (85% or more)");
    for (const auto& [kind, count] : differByKind) report.push_back("  differ by kind: " + kind + " " + std::to_string(count));
    check(differ == 0, "the layout children have the offsets that the editor saved");
    return report;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

/** Runs each fixture (DIR/NAME.lua) in a runtime of its own after the boot of the emulation; adds its checks. */
void run_fixtures(const std::string& content, const std::string& luaDir, const std::string& reports, const std::string& dir,
                  const std::vector<std::string>& only) {
    using swc::ui::Runtime;
    const std::string prelude = read_file(std::filesystem::path(dir) / "prelude.lua");
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().stem().string();
        if (entry.path().extension() == ".lua" && name != "prelude" &&
            (only.empty() || std::find(only.begin(), only.end(), name) != only.end())) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& file : files) {
        const std::string name = file.stem().string();
        Runtime::Options options;
        options.contentDir = content;
        options.luaDir = luaDir;
        options.reportDir = reports + "/fixtures/" + name;
        options.allowedCanvases = {"(no screen of the loader)"};
        std::filesystem::create_directories(options.reportDir);
        Runtime runtime(options);
        runtime.set_viewport(1920, 1080);
        runtime.set_text_measure(&g_measure);
        std::string error;
        if (!runtime.start(&error)) {
            check(false, "fixture " + name + ": the runtime did not start: " + error);
            continue;
        }
        runtime.call("Boot");
        std::string result;
        const bool ok = runtime.call("RunFixture", {Runtime::Arg::of(name), Runtime::Arg::of(prelude), Runtime::Arg::of(read_file(file))},
                                     &result);
        int checks = 0;
        int failed = 0;
        std::sscanf(result.c_str(), "%d checks, %d failed", &checks, &failed);
        g_checks += checks;
        g_failed += failed + (ok ? 0 : 1);
        std::printf("fixture %s: %s\n", name.c_str(), ok ? result.c_str() : "the runner raised an error");
        runtime.write_reports(&error);
        std::ofstream logFile(options.reportDir + "/ui-log.txt");
        for (const std::string& line : runtime.take_log()) {
            logFile << line << "\n";
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: swc-ui-tests CONTENT_UI LUA_DIR REPORT_DIR [--seconds S] [--draw FILE] [--health H MAX] [--only-core]\n");
        return 2;
    }
    const std::string content = argv[1];
    const std::string luaDir = argv[2];
    const std::string reports = argv[3];
    double seconds = 3.0;
    std::string drawFile;
    bool onlyCore = false;
    bool allScreens = false;
    bool hud = true;
    std::string fixtureDir;
    std::vector<std::string> fixtureNames;
    std::vector<std::string> dumpNames;
    std::vector<std::string> traceNames;
    std::vector<std::string> luaTexts;
    double health = 650, healthMax = 1000, stamina = 70, staminaMax = 100, mana = 40, manaMax = 100, heading = 30;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--seconds" && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (a == "--draw" && i + 1 < argc) drawFile = argv[++i];
        else if (a == "--only-core") onlyCore = true;
        else if (a == "--all-screens") allScreens = true;
        else if (a == "--no-hud") hud = false;
        else if (a == "--fixtures" && i + 1 < argc) fixtureDir = argv[++i];
        else if (a == "--fixture" && i + 1 < argc) fixtureNames.push_back(argv[++i]);
        else if (a == "--dump" && i + 1 < argc) dumpNames.push_back(argv[++i]);
        else if (a == "--trace" && i + 1 < argc) traceNames.push_back(argv[++i]);
        else if (a == "--lua" && i + 1 < argc) luaTexts.push_back(argv[++i]);
        else if (a == "--health" && i + 2 < argc) { health = std::atof(argv[++i]); healthMax = std::atof(argv[++i]); }
        else if (a == "--stamina" && i + 2 < argc) { stamina = std::atof(argv[++i]); staminaMax = std::atof(argv[++i]); }
        else if (a == "--mana" && i + 2 < argc) { mana = std::atof(argv[++i]); manaMax = std::atof(argv[++i]); }
        else if (a == "--heading" && i + 1 < argc) heading = std::atof(argv[++i]);
    }
    core_tests(reports + "/core-test-content");
    std::printf("core: %d checks, %d failed\n", g_checks, g_failed);
    for (const std::string& line : layout_conformance(content)) {
        std::printf("%s\n", line.c_str());
    }
    if (onlyCore) {
        return g_failed == 0 ? 0 : 1;
    }

    using swc::ui::Runtime;
    if (!fixtureDir.empty()) {
        run_fixtures(content, luaDir, reports, fixtureDir, fixtureNames);
    }
    if (!hud) {
        std::printf("all: %d checks, %d failed\n", g_checks, g_failed);
        return g_failed == 0 ? 0 : 1;
    }
    Runtime::Options options;
    options.contentDir = content;
    options.luaDir = luaDir;
    options.reportDir = reports;
    options.allowedCanvases = {"lyshineui/hud2024/vitals/vitals2024", "lyshineui/hud2024/weaponquickslots/weaponquickslots",
                               "lyshineui/compass/compass", "lyshineui/hud2024/editmode/hudeditmode"};
    if (allScreens) {
        options.allowedCanvases.clear();
    }
    Runtime runtime(options);
    runtime.set_viewport(1920, 1080);
    runtime.set_text_measure(&g_measure);
    std::string error;
    if (!runtime.start(&error)) {
        std::printf("the runtime did not start: %s\n", error.c_str());
        for (const std::string& line : runtime.take_log()) std::printf("  %s\n", line.c_str());
        return 1;
    }
    for (const std::string& name : traceNames) {
        runtime.call("Trace", {Runtime::Arg::of(name)});
    }
    runtime.call("Boot");
    runtime.call("EnterWorld", {Runtime::Arg::of(std::string("newworld_vitaeeterna"))});
    // A made-up player of the test: a sword in the first weapon slot, three sword abilities on its bar, one cooldown.
    runtime.call("PlayerSpawned");
    runtime.call("HostEquipment", {Runtime::Arg::of(24.0), Runtime::Arg::of(std::string("1hSwordT52"))});
    runtime.call("HostEquipment", {Runtime::Arg::of(45.0), Runtime::Arg::of(std::string("RuneBileBombT4A"))});
    runtime.call("HostEquipment", {Runtime::Arg::of(25.0), Runtime::Arg::of(std::string("artifact_set1_1hrapier"))});
    runtime.call("HostAbilityBar", {Runtime::Arg::of(2169986235.0), Runtime::Arg::of(617453373.0), Runtime::Arg::of(241886633.0), Runtime::Arg::of(2855651066.0)});
    runtime.call("HostActiveWeapon", {Runtime::Arg::of(24.0), Runtime::Arg::of(false)});
    runtime.call("HostAbilityBar", {Runtime::Arg::of(3907802902.0), Runtime::Arg::of(2977457251.0), Runtime::Arg::of(248953190.0),
                                    Runtime::Arg::of(3873560184.0)});
    runtime.call("HostCooldown", {Runtime::Arg::of(248953190.0), Runtime::Arg::of(12.0), Runtime::Arg::of(18.0)});
    const double dt = 1.0 / 60.0;
    const int frames = static_cast<int>(seconds / dt);
    for (int frame = 0; frame < frames; ++frame) {
        runtime.call("HostVitals", {Runtime::Arg::of(health), Runtime::Arg::of(healthMax), Runtime::Arg::of(stamina),
                                    Runtime::Arg::of(staminaMax), Runtime::Arg::of(mana), Runtime::Arg::of(manaMax),
                                    Runtime::Arg::of(false)});
        runtime.call("HostView", {Runtime::Arg::of(heading), Runtime::Arg::of(0.0), Runtime::Arg::of(0.0), Runtime::Arg::of(0.0)});
        runtime.call("HostCommit");
        runtime.tick(dt);
    }
    runtime.call("Report");
    for (const std::string& text : luaTexts) {
        runtime.call("Eval", {Runtime::Arg::of(text)});
    }
    // The state of some elements after the run (a check of the values that the scripts set).
    for (const std::string& wanted : dumpNames) {
        for (swc::ui::Canvas* canvas : runtime.model().canvases_in_order()) {
            for (const swc::ui::Id id : runtime.model().subtree(canvas->root)) {
                const swc::ui::Element* e = runtime.model().element(id);
                if (e == nullptr || e->name != wanted) continue;
                std::printf("  dump %s/%s enabled=%d fade=%.2f", canvas->name.c_str(), e->name.c_str(), runtime.model().enabled_in_hierarchy(id) ? 1 : 0,
                            e->fade ? *e->fade : -1.0F);
                if (e->image) std::printf(" color=(%.2f %.2f %.2f) alpha=%.2f fill=%.2f sprite=%s", e->image->color.r, e->image->color.g,
                                          e->image->color.b, e->image->alpha, e->image->fillAmount, e->image->sprite.c_str());
                if (e->text) std::printf(" text='%s'", e->text->text.c_str());
                std::printf("\n");
            }
        }
    }
    swc::ui::DrawList list;
    runtime.build_draw_list(list);
    if (!drawFile.empty()) {
        std::ofstream(drawFile) << swc::ui::draw_list_json(list);
    }
    if (!runtime.write_reports(&error)) {
        std::printf("reports: %s\n", error.c_str());
    }
    // The full log of the run in a file; the console gets the lines that are not stubs.
    std::ofstream logFile(reports + "/ui-log.txt");
    int shown = 0;
    for (const std::string& line : runtime.take_log()) {
        logFile << line << "\n";
        if (line.rfind("stub ", 0) != 0 && shown++ < 80) std::printf("  %s\n", line.c_str());
    }
    const Runtime::Counts& c = runtime.counts();
    std::printf("hud: %d frames, %llu Lua errors, %llu stub names, %llu stub calls, %zu draw items (%llu meshes, %llu texts)\n", frames,
                static_cast<unsigned long long>(c.luaErrors), static_cast<unsigned long long>(c.stubNames),
                static_cast<unsigned long long>(c.stubCalls), list.items.size(), static_cast<unsigned long long>(list.meshes),
                static_cast<unsigned long long>(list.texts));
    std::printf("all: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
