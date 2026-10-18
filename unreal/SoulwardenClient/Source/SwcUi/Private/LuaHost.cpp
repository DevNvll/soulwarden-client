#include "SwcUi/Runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "LuaApi.h"
#include "LuaSource.h"

// The natives of the emulation (the Lua table `swcui`) and the loader of the game scripts. A native reads its
// arguments with the luaL checks: a wrong argument is a Lua error of the calling script (a C++ exception in this build
// of Lua, thus the C++ objects of the native are destroyed).
namespace swc::ui {
namespace {

constexpr const char* kIdType = "swc.EntityId";
constexpr const char* kIdCache = "swc.EntityId.cache";
constexpr const char* kTraceback = "swc.traceback";

Runtime& runtime_of(lua_State* L) {
    return *static_cast<Runtime*>(lua_touserdata(L, lua_upvalueindex(1)));
}

Model& model_of(lua_State* L) {
    return runtime_of(L).model();
}

// ------------------------------------------------------------------------------------------------------- entity ids

/** Pushes the EntityId userdata of an id. Each id has one userdata (a weak cache): == and table keys work. */
void push_id(lua_State* L, Id id) {
    lua_getfield(L, LUA_REGISTRYINDEX, kIdCache);
    lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)));
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    auto* box = static_cast<Id*>(lua_newuserdata(L, sizeof(Id)));
    *box = id;
    luaL_getmetatable(L, kIdType);
    lua_setmetatable(L, -2);
    lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)));
    lua_pushvalue(L, -2);
    lua_rawset(L, -4);
    lua_remove(L, -2);
}

bool is_id(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TUSERDATA || !lua_getmetatable(L, index)) {
        return false;
    }
    luaL_getmetatable(L, kIdType);
    const bool same = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return same;
}

/** @return The id of an argument: an EntityId, a number, a decimal text; nil or none is kNoId. */
Id to_id(lua_State* L, int index) {
    if (is_id(L, index)) {
        return *static_cast<Id*>(lua_touserdata(L, index));
    }
    switch (lua_type(L, index)) {
    case LUA_TNUMBER:
        return static_cast<Id>(lua_tonumber(L, index));
    case LUA_TSTRING:
        return parse_id(lua_tostring(L, index));
    default:
        return kNoId;
    }
}

int id_new(lua_State* L) {
    // EntityId() is the invalid id; EntityId(number) or EntityId("decimal") an id.
    push_id(L, lua_gettop(L) >= 1 ? to_id(L, 1) : kNoId);
    return 1;
}

int id_tostring(lua_State* L) {
    const Id id = to_id(L, 1);
    const std::string text = "[" + std::to_string(id) + "]";
    lua_pushstring(L, text.c_str());
    return 1;
}

int id_eq(lua_State* L) {
    lua_pushboolean(L, to_id(L, 1) == to_id(L, 2));
    return 1;
}

int id_is_valid(lua_State* L) {
    const Id id = to_id(L, 1);
    lua_pushboolean(L, id != kNoId && id != 0);
    return 1;
}

void install_entity_id(lua_State* L) {
    lua_newtable(L);
    lua_newtable(L);
    lua_pushliteral(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, kIdCache);

    luaL_newmetatable(L, kIdType);
    lua_pushcfunction(L, id_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, id_eq);
    lua_setfield(L, -2, "__eq");
    lua_newtable(L);   // methods: the emulation adds more (swc/values.lua)
    lua_pushcfunction(L, id_is_valid);
    lua_setfield(L, -2, "IsValid");
    lua_pushcfunction(L, id_tostring);
    lua_setfield(L, -2, "ToString");
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_pushcfunction(L, id_new);
    lua_setglobal(L, "EntityId");
}

// ------------------------------------------------------------------------------------------------------- helpers

std::string arg_text(lua_State* L, int index) {
    size_t size = 0;
    const char* text = luaL_checklstring(L, index, &size);
    return std::string(text, size);
}

float arg_real(lua_State* L, int index) {
    return static_cast<float>(luaL_checknumber(L, index));
}

void push_ids(lua_State* L, const std::vector<Id>& ids) {
    lua_createtable(L, static_cast<int>(ids.size()), 0);
    int at = 1;
    for (const Id id : ids) {
        push_id(L, id);
        lua_rawseti(L, -2, at++);
    }
}

Element& checked_element(lua_State* L, int index) {
    Element* e = model_of(L).element(to_id(L, index));
    if (e == nullptr) {
        luaL_error(L, "no element %s", std::to_string(to_id(L, index)).c_str());
    }
    return *e;
}

/** Pushes a raw script property ({t = ..., v = ...}) as a Lua table; the emulation converts it. */
void push_json(lua_State* L, const json::Value& value) {
    switch (value.type()) {
    case json::Value::Type::null:
        lua_pushnil(L);
        break;
    case json::Value::Type::boolean:
        lua_pushboolean(L, value.boolean());
        break;
    case json::Value::Type::number:
        lua_pushnumber(L, value.number());
        break;
    case json::Value::Type::string:
        lua_pushlstring(L, value.text().data(), value.text().size());
        break;
    case json::Value::Type::array: {
        lua_createtable(L, static_cast<int>(value.size()), 0);
        int at = 1;
        for (const json::Value& element : value.elements()) {
            push_json(L, element);
            lua_rawseti(L, -2, at++);
        }
        break;
    }
    case json::Value::Type::object:
        lua_createtable(L, 0, static_cast<int>(value.size()));
        for (const json::Value::Member& member : value.members()) {
            push_json(L, member.second);
            lua_setfield(L, -2, member.first.c_str());
        }
        break;
    }
}

// --------------------------------------------------------------------------------------------------- element tree

int n_exists(lua_State* L) {
    lua_pushboolean(L, model_of(L).element(to_id(L, 1)) != nullptr);
    return 1;
}

int n_name(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    if (e == nullptr) {
        return 0;
    }
    lua_pushlstring(L, e->name.data(), e->name.size());
    return 1;
}

int n_set_name(lua_State* L) {
    checked_element(L, 1).name = arg_text(L, 2);
    return 0;
}

int n_parent(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    push_id(L, e != nullptr ? e->parent : kNoId);
    return 1;
}

int n_children(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    push_ids(L, e != nullptr ? e->children : std::vector<Id>{});
    return 1;
}

int n_child(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    const auto index = static_cast<long>(luaL_checknumber(L, 2));
    if (e == nullptr || index < 0 || static_cast<std::size_t>(index) >= e->children.size()) {
        push_id(L, kNoId);
    } else {
        push_id(L, e->children[static_cast<std::size_t>(index)]);
    }
    return 1;
}

int n_num_children(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    lua_pushnumber(L, e != nullptr ? static_cast<double>(e->children.size()) : 0.0);
    return 1;
}

int n_index_of_child(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    const Id child = to_id(L, 2);
    if (e != nullptr) {
        const auto found = std::find(e->children.begin(), e->children.end(), child);
        if (found != e->children.end()) {
            lua_pushnumber(L, static_cast<double>(found - e->children.begin()));
            return 1;
        }
    }
    lua_pushnumber(L, -1);
    return 1;
}

int n_find_child(lua_State* L) {
    push_id(L, model_of(L).find_child(to_id(L, 1), arg_text(L, 2)));
    return 1;
}

int n_find_descendant(lua_State* L) {
    push_id(L, model_of(L).find_descendant(to_id(L, 1), arg_text(L, 2)));
    return 1;
}

int n_canvas_of(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    push_id(L, e != nullptr ? e->canvas : kNoId);
    return 1;
}

int n_enabled(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    lua_pushboolean(L, e != nullptr && e->enabled);
    return 1;
}

int n_set_enabled(lua_State* L) {
    Element* e = model_of(L).element(to_id(L, 1));
    if (e != nullptr) {
        e->enabled = lua_toboolean(L, 2) != 0;
    }
    return 0;
}

int n_enabled_in_hierarchy(lua_State* L) {
    lua_pushboolean(L, model_of(L).enabled_in_hierarchy(to_id(L, 1)));
    return 1;
}

int n_reparent(lua_State* L) {
    lua_pushboolean(L, model_of(L).reparent(to_id(L, 1), to_id(L, 2), to_id(L, 3)));
    return 1;
}

int n_destroy(lua_State* L) {
    std::vector<Id> destroyed;
    model_of(L).destroy_element(to_id(L, 1), &destroyed);
    push_ids(L, destroyed);
    return 1;
}

int n_clone(lua_State* L) {
    std::vector<Id> created;
    const Id id = model_of(L).clone_element(to_id(L, 1), to_id(L, 2), to_id(L, 3), &created);
    push_id(L, id);
    push_ids(L, created);
    return 2;
}

int n_spawn(lua_State* L) {
    std::vector<Id> created;
    std::string error;
    const Id id = model_of(L).spawn_slice(arg_text(L, 1), to_id(L, 2), &created, &error);
    if (id == kNoId) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    push_id(L, id);
    push_ids(L, created);
    return 2;
}

int n_create(lua_State* L) {
    push_id(L, model_of(L).create_element(arg_text(L, 1), to_id(L, 2), to_id(L, 3)));
    return 1;
}

int n_has(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    const std::string what = arg_text(L, 2);
    bool has = false;
    if (e != nullptr) {
        if (what == "image") has = e->image.has_value();
        else if (what == "text") has = e->text.has_value();
        else if (what == "fader") has = e->fade.has_value();
        else if (what == "mask") has = e->mask.has_value();
        else if (what == "flipbook") has = e->flipbook.has_value();
        else if (what == "sequence") has = e->sequence.has_value();
        else if (what == "spawner") has = e->spawnerSlice.has_value();
        else if (what == "desaturator") has = e->saturation.has_value();
        else if (what == "repeater") has = e->repeats.has_value();
        else if (what == "button") has = e->button.has_value();
        else has = std::find(e->other.begin(), e->other.end(), what) != e->other.end();
    }
    lua_pushboolean(L, has);
    return 1;
}

int n_scripts(lua_State* L) {
    Model& model = model_of(L);
    const Element* e = model.element(to_id(L, 1));
    const Canvas* canvas = e == nullptr ? model.canvas(to_id(L, 1)) : nullptr;
    lua_newtable(L);
    if (e == nullptr && canvas == nullptr) {
        return 1;
    }
    int at = 1;
    for (const ScriptData& script : e != nullptr ? e->scripts : canvas->scripts) {
        lua_newtable(L);
        lua_pushstring(L, script.script.c_str());
        lua_setfield(L, -2, "script");
        push_json(L, script.properties);
        lua_setfield(L, -2, "properties");
        lua_rawseti(L, -2, at++);
    }
    return 1;
}

int n_other(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    lua_newtable(L);
    if (e != nullptr) {
        int at = 1;
        for (const std::string& name : e->other) {
            lua_pushstring(L, name.c_str());
            lua_rawseti(L, -2, at++);
        }
    }
    return 1;
}

int n_button(lua_State* L) {
    const Element* e = model_of(L).element(to_id(L, 1));
    if (e == nullptr || !e->button) {
        return 0;
    }
    push_json(L, *e->button);
    return 1;
}

// ------------------------------------------------------------------------------------------------------ transforms

int push_vec(lua_State* L, Vec2 v) {
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    return 2;
}

int push_edges(lua_State* L, Edges e) {
    lua_pushnumber(L, e.left);
    lua_pushnumber(L, e.top);
    lua_pushnumber(L, e.right);
    lua_pushnumber(L, e.bottom);
    return 4;
}

int n_local_position(lua_State* L) {
    return push_vec(L, model_of(L).local_position(to_id(L, 1)));
}

int n_set_local_position(lua_State* L) {
    model_of(L).set_local_position(to_id(L, 1), {arg_real(L, 2), arg_real(L, 3)});
    return 0;
}

int n_local_size(lua_State* L) {
    const Id id = to_id(L, 1);
    return push_vec(L, {model_of(L).local_width(id), model_of(L).local_height(id)});
}

int n_set_local_width(lua_State* L) {
    model_of(L).set_local_width(to_id(L, 1), arg_real(L, 2));
    return 0;
}

int n_set_local_height(lua_State* L) {
    model_of(L).set_local_height(to_id(L, 1), arg_real(L, 2));
    return 0;
}

int n_viewport_position(lua_State* L) {
    return push_vec(L, model_of(L).viewport_position(to_id(L, 1)));
}

int n_set_viewport_position(lua_State* L) {
    model_of(L).set_viewport_position(to_id(L, 1), {arg_real(L, 2), arg_real(L, 3)});
    return 0;
}

int n_canvas_position(lua_State* L) {
    return push_vec(L, model_of(L).canvas_position(to_id(L, 1)));
}

int n_set_canvas_position(lua_State* L) {
    model_of(L).set_canvas_position(to_id(L, 1), {arg_real(L, 2), arg_real(L, 3)});
    return 0;
}

int n_rect(lua_State* L) {
    return push_edges(L, model_of(L).rect(to_id(L, 1)));
}

int n_viewport_points(lua_State* L) {
    Vec2 points[4];
    model_of(L).viewport_points(to_id(L, 1), points);
    for (const Vec2& p : points) {
        lua_pushnumber(L, p.x);
        lua_pushnumber(L, p.y);
    }
    return 8;
}

/** Fields of components: get(id, "component.field") and set(id, "component.field", values...). */
int n_get(lua_State* L) {
    Model& model = model_of(L);
    Element* e = model.element(to_id(L, 1));
    const std::string key = arg_text(L, 2);
    if (e == nullptr) {
        return 0;
    }
    const TransformData& t = e->transform;
    if (key == "transform.anchors") return push_edges(L, t.anchors);
    if (key == "transform.offsets") return push_edges(L, t.offsets);
    if (key == "transform.pivot") return push_vec(L, t.pivot);
    if (key == "transform.scale") return push_vec(L, t.scale);
    if (key == "transform.rotation") { lua_pushnumber(L, t.rotation); return 1; }
    if (key == "transform.scaleToDevice") { lua_pushboolean(L, t.scaleToDevice); return 1; }
    if (key == "element.renderPriority") { lua_pushnumber(L, e->renderPriority); return 1; }
    if (key.rfind("image.", 0) == 0) {
        if (!e->image) {
            return 0;
        }
        const ImageData& i = *e->image;
        const std::string f = key.substr(6);
        if (f == "sprite") { lua_pushstring(L, i.sprite.c_str()); return 1; }
        if (f == "color") { lua_pushnumber(L, i.color.r); lua_pushnumber(L, i.color.g); lua_pushnumber(L, i.color.b); lua_pushnumber(L, i.alpha); return 4; }
        if (f == "alpha") { lua_pushnumber(L, i.alpha); return 1; }
        if (f == "type") { lua_pushnumber(L, i.type); return 1; }
        if (f == "fillType") { lua_pushnumber(L, i.fillType); return 1; }
        if (f == "fillAmount") { lua_pushnumber(L, i.fillAmount); return 1; }
        if (f == "fillStartAngle") { lua_pushnumber(L, i.fillStartAngle); return 1; }
        if (f == "fillEdgeOrigin") { lua_pushnumber(L, i.fillEdgeOrigin); return 1; }
        if (f == "fillCornerOrigin") { lua_pushnumber(L, i.fillCornerOrigin); return 1; }
        if (f == "fillClockwise") { lua_pushboolean(L, i.fillClockwise); return 1; }
        if (f == "fillCenter") { lua_pushboolean(L, i.fillCenter); return 1; }
        if (f == "index") { lua_pushnumber(L, i.index); return 1; }
        if (f == "blend") { lua_pushnumber(L, i.blend); return 1; }
        if (f == "spriteType") { lua_pushnumber(L, i.spriteType); return 1; }
        if (f == "cells") {
            const Sprite& s = model.sprite(i.sprite);
            lua_pushnumber(L, static_cast<double>(s.cells.size()));
            return 1;
        }
        if (f == "textureSize") {
            const Sprite& s = model.sprite(i.sprite);
            return push_vec(L, {s.width, s.height});
        }
    }
    if (key.rfind("text.", 0) == 0) {
        if (!e->text) {
            return 0;
        }
        const TextData& x = *e->text;
        const std::string f = key.substr(5);
        if (f == "text") { lua_pushlstring(L, x.text.data(), x.text.size()); return 1; }
        if (f == "color") { lua_pushnumber(L, x.color.r); lua_pushnumber(L, x.color.g); lua_pushnumber(L, x.color.b); lua_pushnumber(L, x.alpha); return 4; }
        if (f == "alpha") { lua_pushnumber(L, x.alpha); return 1; }
        if (f == "font") { lua_pushstring(L, x.font.c_str()); return 1; }
        if (f == "size") { lua_pushnumber(L, x.size); return 1; }
        if (f == "effect") { lua_pushnumber(L, x.effect); return 1; }
        if (f == "hAlign") { lua_pushnumber(L, x.hAlign); return 1; }
        if (f == "vAlign") { lua_pushnumber(L, x.vAlign); return 1; }
        if (f == "charSpacing") { lua_pushnumber(L, x.charSpacing); return 1; }
        if (f == "lineSpacing") { lua_pushnumber(L, x.lineSpacing); return 1; }
        if (f == "overflow") { lua_pushnumber(L, x.overflow); return 1; }
        if (f == "wrap") { lua_pushnumber(L, x.wrap); return 1; }
        if (f == "shrink") { lua_pushnumber(L, x.shrink); return 1; }
        if (f == "minShrink") { lua_pushnumber(L, x.minShrink); return 1; }
        if (f == "case") { lua_pushnumber(L, x.textCase); return 1; }
        if (f == "markup") { lua_pushboolean(L, x.markup); return 1; }
        if (f == "size2d") {
            TextMeasure* measure = runtime_of(L).measure();
            if (measure == nullptr) {
                return push_vec(L, {0.0F, 0.0F});
            }
            const TextLayout layout = layout_text(model, e->id, *measure);
            return push_vec(L, {layout.width, layout.height});
        }
    }
    if (key == "fader.fade") {
        if (!e->fade) return 0;
        lua_pushnumber(L, *e->fade);
        return 1;
    }
    if (key == "desaturator.saturation") {
        if (!e->saturation) return 0;
        lua_pushnumber(L, *e->saturation);
        return 1;
    }
    if (key.rfind("flipbook.", 0) == 0) {
        if (!e->flipbook) return 0;
        const FlipbookData& b = *e->flipbook;
        const std::string f = key.substr(9);
        if (f == "frame") { lua_pushnumber(L, b.frame); return 1; }
        if (f == "start") { lua_pushnumber(L, b.start); return 1; }
        if (f == "end") { lua_pushnumber(L, b.end); return 1; }
        if (f == "loopStart") { lua_pushnumber(L, b.loopStart); return 1; }
        if (f == "loopType") { lua_pushnumber(L, b.loopType); return 1; }
        if (f == "rateUnit") { lua_pushnumber(L, b.rateUnit); return 1; }
        if (f == "rate") { lua_pushnumber(L, b.rate); return 1; }
        if (f == "startDelay") { lua_pushnumber(L, b.startDelay); return 1; }
        if (f == "loopDelay") { lua_pushnumber(L, b.loopDelay); return 1; }
        if (f == "reverseDelay") { lua_pushnumber(L, b.reverseDelay); return 1; }
        if (f == "autoPlay") { lua_pushboolean(L, b.autoPlay); return 1; }
        if (f == "playing") { lua_pushboolean(L, b.playing); return 1; }
    }
    if (key == "sequence.index") {
        if (!e->sequence) return 0;
        lua_pushnumber(L, e->sequence->index);
        return 1;
    }
    if (key == "sequence.count") {
        if (!e->sequence) return 0;
        lua_pushnumber(L, static_cast<double>(e->sequence->images.size()));
        return 1;
    }
    if (key == "spawner.slice") {
        if (!e->spawnerSlice) return 0;
        lua_pushstring(L, e->spawnerSlice->c_str());
        return 1;
    }
    if (key == "repeater.repeats") {
        if (!e->repeats) return 0;
        lua_pushnumber(L, *e->repeats);
        return 1;
    }
    if (key.rfind("mask.", 0) == 0) {
        if (!e->mask) return 0;
        if (key == "mask.enabled") { lua_pushboolean(L, e->mask->enabled); return 1; }
        if (key == "mask.childMask") { push_id(L, e->mask->childMask); return 1; }
    }
    return luaL_error(L, "get: no field %s", key.c_str());
}

Color arg_color(lua_State* L, int first, Color fallback) {
    Color c = fallback;
    c.r = static_cast<float>(luaL_optnumber(L, first, fallback.r));
    c.g = static_cast<float>(luaL_optnumber(L, first + 1, fallback.g));
    c.b = static_cast<float>(luaL_optnumber(L, first + 2, fallback.b));
    c.a = static_cast<float>(luaL_optnumber(L, first + 3, fallback.a));
    return c;
}

int n_set(lua_State* L) {
    Model& model = model_of(L);
    Element* e = model.element(to_id(L, 1));
    const std::string key = arg_text(L, 2);
    if (e == nullptr) {
        return 0;
    }
    TransformData& t = e->transform;
    if (key == "transform.anchors") { t.anchors = {arg_real(L, 3), arg_real(L, 4), arg_real(L, 5), arg_real(L, 6)}; return 0; }
    if (key == "transform.offsets") { t.offsets = {arg_real(L, 3), arg_real(L, 4), arg_real(L, 5), arg_real(L, 6)}; return 0; }
    if (key == "transform.pivot") { t.pivot = {arg_real(L, 3), arg_real(L, 4)}; return 0; }
    if (key == "transform.scale") { t.scale = {arg_real(L, 3), arg_real(L, 4)}; return 0; }
    if (key == "transform.rotation") { t.rotation = arg_real(L, 3); return 0; }
    if (key == "transform.scaleToDevice") { t.scaleToDevice = lua_toboolean(L, 3) != 0; return 0; }
    if (key == "element.renderPriority") { e->renderPriority = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
    if (key.rfind("image.", 0) == 0) {
        if (!e->image) {
            return 0;
        }
        ImageData& i = *e->image;
        const std::string f = key.substr(6);
        if (f == "sprite") { i.sprite = lower(lua_isnil(L, 3) ? std::string() : arg_text(L, 3)); std::replace(i.sprite.begin(), i.sprite.end(), '\\', '/'); return 0; }
        if (f == "color") { const Color c = arg_color(L, 3, {i.color.r, i.color.g, i.color.b, i.alpha}); i.color = c; i.alpha = c.a; return 0; }
        if (f == "alpha") { i.alpha = arg_real(L, 3); return 0; }
        if (f == "type") { i.type = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "fillType") { i.fillType = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "fillAmount") { i.fillAmount = std::clamp(arg_real(L, 3), 0.0F, 1.0F); return 0; }
        if (f == "fillStartAngle") { i.fillStartAngle = arg_real(L, 3); return 0; }
        if (f == "fillEdgeOrigin") { i.fillEdgeOrigin = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "fillCornerOrigin") { i.fillCornerOrigin = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "fillClockwise") { i.fillClockwise = lua_toboolean(L, 3) != 0; return 0; }
        if (f == "fillCenter") { i.fillCenter = lua_toboolean(L, 3) != 0; return 0; }
        if (f == "index") { i.index = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "blend") { i.blend = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "spriteType") { i.spriteType = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "uv") {
            if (lua_isnil(L, 3)) {
                i.uvOverride.reset();
            } else {
                i.uvOverride = Edges{arg_real(L, 3), arg_real(L, 4), arg_real(L, 5), arg_real(L, 6)};
            }
            return 0;
        }
    }
    if (key.rfind("text.", 0) == 0) {
        if (!e->text) {
            return 0;
        }
        TextData& x = *e->text;
        const std::string f = key.substr(5);
        if (f == "text") { x.text = arg_text(L, 3); return 0; }
        if (f == "color") { const Color c = arg_color(L, 3, {x.color.r, x.color.g, x.color.b, x.alpha}); x.color = c; x.alpha = c.a; return 0; }
        if (f == "alpha") { x.alpha = arg_real(L, 3); return 0; }
        if (f == "font") { x.font = lower(arg_text(L, 3)); std::replace(x.font.begin(), x.font.end(), '\\', '/'); return 0; }
        if (f == "size") { x.size = arg_real(L, 3); return 0; }
        if (f == "effect") { x.effect = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "effectName") {
            const int index = model.font_effect_index(arg_text(L, 3));
            if (index >= 0) x.effect = index;
            lua_pushboolean(L, index >= 0);
            return 1;
        }
        if (f == "hAlign") { x.hAlign = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "vAlign") { x.vAlign = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "charSpacing") { x.charSpacing = arg_real(L, 3); return 0; }
        if (f == "lineSpacing") { x.lineSpacing = arg_real(L, 3); return 0; }
        if (f == "overflow") { x.overflow = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "wrap") { x.wrap = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "shrink") { x.shrink = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "minShrink") { x.minShrink = arg_real(L, 3); return 0; }
        if (f == "case") { x.textCase = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "markup") { x.markup = lua_toboolean(L, 3) != 0; return 0; }
    }
    if (key == "fader.fade") {
        if (e->fade) *e->fade = std::clamp(arg_real(L, 3), 0.0F, 1.0F);
        return 0;
    }
    if (key == "desaturator.saturation") {
        if (e->saturation) *e->saturation = arg_real(L, 3);
        return 0;
    }
    if (key.rfind("flipbook.", 0) == 0) {
        if (!e->flipbook) return 0;
        FlipbookData& b = *e->flipbook;
        const std::string f = key.substr(9);
        if (f == "frame") { b.frame = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "start") { b.start = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "end") { b.end = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "loopStart") { b.loopStart = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "loopType") { b.loopType = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
        if (f == "rate") { b.rate = arg_real(L, 3); return 0; }
        if (f == "playing") { b.playing = lua_toboolean(L, 3) != 0; return 0; }
    }
    if (key == "sequence.index") {
        if (e->sequence) e->sequence->index = static_cast<int>(luaL_checknumber(L, 3));
        return 0;
    }
    if (key == "repeater.repeats") {
        if (e->repeats) *e->repeats = static_cast<int>(luaL_checknumber(L, 3));
        return 0;
    }
    if (key == "mask.enabled") {
        if (e->mask) e->mask->enabled = lua_toboolean(L, 3) != 0;
        return 0;
    }
    return luaL_error(L, "set: no field %s", key.c_str());
}

// ----------------------------------------------------------------------------------------------------------- canvases

int n_load_canvas(lua_State* L) {
    Model& model = model_of(L);
    std::string error;
    const std::string path = arg_text(L, 1);
    const Id before = model.find_canvas_by_path(path);
    const Id id = model.load_canvas(path, &error);
    if (id == kNoId) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    push_id(L, id);
    // The new elements, parents before children; none for a canvas that was loaded already.
    Canvas* canvas = model.canvas(id);
    push_ids(L, before == kNoId && canvas != nullptr ? model.subtree(canvas->root) : std::vector<Id>{});
    return 2;
}

int n_unload_canvas(lua_State* L) {
    std::vector<Id> destroyed;
    model_of(L).unload_canvas(to_id(L, 1), &destroyed);
    push_ids(L, destroyed);
    return 1;
}

int n_find_canvas(lua_State* L) {
    push_id(L, model_of(L).find_canvas_by_path(arg_text(L, 1)));
    return 1;
}

int n_canvases(lua_State* L) {
    std::vector<Id> ids;
    for (Canvas* canvas : model_of(L).canvases_in_order()) {
        ids.push_back(canvas->entity);
    }
    push_ids(L, ids);
    return 1;
}

int n_canvas_get(lua_State* L) {
    Model& model = model_of(L);
    Canvas* canvas = model.canvas(to_id(L, 1));
    const std::string key = arg_text(L, 2);
    if (canvas == nullptr) {
        return 0;
    }
    if (key == "name") { lua_pushstring(L, canvas->name.c_str()); return 1; }
    if (key == "path") { lua_pushstring(L, canvas->path.c_str()); return 1; }
    if (key == "drawOrder") { lua_pushnumber(L, canvas->drawOrder); return 1; }
    if (key == "enabled") { lua_pushboolean(L, canvas->enabled); return 1; }
    if (key == "root") { push_id(L, canvas->root); return 1; }
    if (key == "size") { return push_vec(L, model.target_canvas_size(*canvas)); }
    if (key == "authoredSize") { return push_vec(L, canvas->authoredSize); }
    if (key == "drawLayer") { lua_pushstring(L, canvas->javelin.drawLayer.c_str()); return 1; }
    if (key == "requiresMouse") { lua_pushboolean(L, canvas->javelin.requiresMouse); return 1; }
    if (key == "escapable") { lua_pushboolean(L, canvas->javelin.escapable); return 1; }
    return luaL_error(L, "canvas_get: no field %s", key.c_str());
}

int n_canvas_set(lua_State* L) {
    Canvas* canvas = model_of(L).canvas(to_id(L, 1));
    const std::string key = arg_text(L, 2);
    if (canvas == nullptr) {
        return 0;
    }
    if (key == "drawOrder") { canvas->drawOrder = static_cast<int>(luaL_checknumber(L, 3)); return 0; }
    if (key == "enabled") { canvas->enabled = lua_toboolean(L, 3) != 0; return 0; }
    return luaL_error(L, "canvas_set: no field %s", key.c_str());
}

// --------------------------------------------------------------------------------------------------------------- misc

int n_localize(lua_State* L) {
    const std::string text = model_of(L).localize(arg_text(L, 1));
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int n_has_text(lua_State* L) {
    const std::string key = arg_text(L, 1);
    lua_pushboolean(L, model_of(L).localize("@" + key) != "@" + key);
    return 1;
}

int n_crc32(lua_State* L) {
    lua_pushnumber(L, crc32(arg_text(L, 1)));
    return 1;
}

int n_texture_exists(lua_State* L) {
    lua_pushboolean(L, model_of(L).sprite(arg_text(L, 1)).found);
    return 1;
}

int n_stub(lua_State* L) {
    runtime_of(L).note_stub(arg_text(L, 1), arg_text(L, 2), luaL_optstring(L, 3, ""));
    return 0;
}

int n_log(lua_State* L) {
    const std::string level = arg_text(L, 1);
    const std::string text = arg_text(L, 2);
    if (level == "error") {
        runtime_of(L).note_error(text);
    } else {
        runtime_of(L).note(text);
    }
    return 0;
}

int n_clock(lua_State* L) {
    lua_pushnumber(L, runtime_of(L).clock());
    return 1;
}

// The tests (tests/ui/fixtures): one frame of the runtime, and the draw list of the frame as a file for a picture.
int n_tick(lua_State* L) {
    runtime_of(L).tick(luaL_checknumber(L, 1));
    return 0;
}

int n_write_draw(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    if (name.find_first_of("/\\") != std::string::npos || name.empty()) {
        return luaL_error(L, "write_draw: a plain name only");
    }
    Runtime& runtime = runtime_of(L);
    DrawList list;
    runtime.build_draw_list(list);
    const std::string dir = runtime.options().reportDir + "/draw";
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    const std::string path = dir + "/" + name + ".json";
    std::ofstream(path) << draw_list_json(list);
    lua_pushstring(L, path.c_str());
    return 1;
}

int n_viewport(lua_State* L) {
    return push_vec(L, model_of(L).viewport());
}

int n_allowed(lua_State* L) {
    // Is a canvas one of the screens of this stage?
    const Runtime::Options& options = runtime_of(L).options();
    std::string path = lower(arg_text(L, 1));
    std::replace(path.begin(), path.end(), '\\', '/');
    for (const char* ending : {".uicanvas", ".dynamicuicanvas"}) {
        const std::string e(ending);
        if (path.size() > e.size() && path.compare(path.size() - e.size(), e.size(), e) == 0) {
            path.resize(path.size() - e.size());
        }
    }
    const bool allowed = options.allowedCanvases.empty() ||
                         std::find(options.allowedCanvases.begin(), options.allowedCanvases.end(), path) != options.allowedCanvases.end();
    lua_pushboolean(L, allowed);
    return 1;
}

/** Reads a JSON file of the converted content (a path below SWC_CONTENT/ui) as Lua tables. @return The table, or nil
 *  and the error. */
int n_load_json(lua_State* L) {
    const std::string relative = arg_text(L, 1);
    if (relative.find("..") != std::string::npos) {
        return luaL_error(L, "load_json: a path with '..'");
    }
    json::Value doc;
    std::string error;
    if (!json::parse_file(runtime_of(L).options().contentDir + "/" + relative, doc, &error)) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    push_json(L, doc);
    return 1;
}

/** Loads a script of the game: the content file "scripts/<path>.luac", 2 bytes, then Lua 5.1 bytecode. */
int n_load_game_script(lua_State* L) {
    std::string name = lower(arg_text(L, 1));
    std::replace(name.begin(), name.end(), '\\', '/');
    std::string path = name;
    if (path.size() < 5 || path.compare(path.size() - 5, 5, ".luac") != 0) {
        std::replace(path.begin(), path.end(), '.', '/');
        path += ".luac";
    }
    const std::string file = runtime_of(L).options().contentDir + "/scripts/" + path;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        lua_pushnil(L);
        lua_pushstring(L, ("no script " + path).c_str());
        return 2;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string data = buffer.str();
    // The 2-byte prefix of the game (04 00), then the stock header of Lua 5.1 bytecode.
    std::size_t start = 0;
    if (data.size() > 6 && data[0] != '\x1b' && data[2] == '\x1b' && data.compare(3, 3, "Lua") == 0) {
        start = 2;
    }
    const std::string chunk = "@" + path;
    if (luaL_loadbuffer(L, data.data() + start, data.size() - start, chunk.c_str()) != 0) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    // The game bytecode has no debug data: each function gets the script path as its source (the stub log and the
    // error log name the script; there are still no line numbers).
    set_chunk_source(L, -1, chunk.c_str());
    return 1;
}

/** Loads a source file of the emulation (luaDir/<name with / >.lua). */
int n_load_lua(lua_State* L) {
    std::string name = arg_text(L, 1);
    std::replace(name.begin(), name.end(), '.', '/');
    const std::string file = runtime_of(L).options().luaDir + "/" + name + ".lua";
    if (luaL_loadfile(L, file.c_str()) != 0) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

int traceback(lua_State* L) {
    // The message and the stack (the game scripts have no line data: the chunk names show the scripts).
    const char* message = lua_tostring(L, 1);
    lua_getglobal(L, "debug");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "traceback");
        if (lua_isfunction(L, -1)) {
            lua_pushstring(L, message != nullptr ? message : "(error with no text)");
            lua_pushinteger(L, 2);
            lua_call(L, 2, 1);
            return 1;
        }
    }
    lua_pushstring(L, message != nullptr ? message : "(error with no text)");
    return 1;
}

const luaL_Reg kNatives[] = {
    {"exists", n_exists}, {"name", n_name}, {"set_name", n_set_name}, {"parent", n_parent}, {"children", n_children},
    {"child", n_child}, {"num_children", n_num_children}, {"index_of_child", n_index_of_child}, {"find_child", n_find_child},
    {"find_descendant", n_find_descendant}, {"canvas_of", n_canvas_of}, {"enabled", n_enabled}, {"set_enabled", n_set_enabled},
    {"enabled_in_hierarchy", n_enabled_in_hierarchy}, {"reparent", n_reparent}, {"destroy", n_destroy}, {"clone", n_clone},
    {"spawn", n_spawn}, {"create", n_create}, {"has", n_has}, {"scripts", n_scripts}, {"other", n_other}, {"button", n_button},
    {"local_position", n_local_position}, {"set_local_position", n_set_local_position}, {"local_size", n_local_size},
    {"set_local_width", n_set_local_width}, {"set_local_height", n_set_local_height}, {"viewport_position", n_viewport_position},
    {"set_viewport_position", n_set_viewport_position}, {"canvas_position", n_canvas_position},
    {"set_canvas_position", n_set_canvas_position}, {"rect", n_rect}, {"viewport_points", n_viewport_points}, {"get", n_get},
    {"set", n_set}, {"load_canvas", n_load_canvas}, {"unload_canvas", n_unload_canvas}, {"find_canvas", n_find_canvas},
    {"canvases", n_canvases}, {"canvas_get", n_canvas_get}, {"canvas_set", n_canvas_set}, {"localize", n_localize},
    {"has_text", n_has_text}, {"crc32", n_crc32}, {"texture_exists", n_texture_exists}, {"stub", n_stub}, {"log", n_log},
    {"clock", n_clock}, {"tick", n_tick}, {"write_draw", n_write_draw}, {"viewport", n_viewport}, {"allowed", n_allowed}, {"load_game_script", n_load_game_script}, {"load_json", n_load_json},
    {"load_lua", n_load_lua}, {nullptr, nullptr}};

} // namespace

// ------------------------------------------------------------------------------------------------------------- runtime

Runtime::Runtime(Options options) : m_options(std::move(options)), m_model(std::make_unique<Model>(m_options.contentDir)) {}

Runtime::~Runtime() {
    if (m_lua != nullptr) {
        lua_close(m_lua);
        m_lua = nullptr;
    }
}

bool Runtime::start(std::string* error) {
    if (m_lua != nullptr) {
        return true;
    }
    m_lua = luaL_newstate();
    if (m_lua == nullptr) {
        if (error != nullptr) *error = "no memory for the Lua state";
        return false;
    }
    lua_State* L = m_lua;
    // The libraries of the client (research ui.md 3.1): base, package, table, string, math, os (time only), debug.
    // No io library: the scripts read no file.
    luaL_openlibs(L);
    lua_pushnil(L);
    lua_setglobal(L, "io");
    lua_getglobal(L, "os");
    for (const char* name : {"execute", "exit", "remove", "rename", "tmpname", "getenv", "setlocale"}) {
        lua_pushnil(L);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
    lua_getglobal(L, "package");
    lua_pushnil(L);
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);

    install_entity_id(L);
    lua_pushcfunction(L, traceback);
    lua_setfield(L, LUA_REGISTRYINDEX, kTraceback);

    lua_newtable(L);
    for (const luaL_Reg* reg = kNatives; reg->name != nullptr; ++reg) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, reg->func, 1);
        lua_setfield(L, -2, reg->name);
    }
    lua_setglobal(L, "swcui");
    return run_file(m_options.luaDir + "/swc/boot.lua", error);
}

bool Runtime::run_file(const std::string& path, std::string* error) {
    lua_State* L = m_lua;
    lua_getfield(L, LUA_REGISTRYINDEX, kTraceback);
    if (luaL_loadfile(L, path.c_str()) != 0) {
        const std::string why = lua_tostring(L, -1);
        lua_pop(L, 2);
        if (error != nullptr) *error = why;
        note_error(why);
        return false;
    }
    if (lua_pcall(L, 0, 0, -2) != 0) {
        const std::string why = lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1) : "(error)";
        lua_pop(L, 2);
        if (error != nullptr) *error = why;
        note_error(why);
        return false;
    }
    lua_pop(L, 1);
    return true;
}

bool Runtime::call(const std::string& name, const std::vector<Arg>& args) {
    return call(name, args, nullptr);
}

bool Runtime::call(const std::string& name, const std::vector<Arg>& args, std::string* result) {
    if (m_lua == nullptr) {
        return false;
    }
    lua_State* L = m_lua;
    const int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, kTraceback);
    lua_getglobal(L, "Swc");
    if (!lua_istable(L, -1)) {
        lua_settop(L, top);
        return false;
    }
    lua_getfield(L, -1, name.c_str());
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        note_error("no function Swc." + name);
        return false;
    }
    for (const Arg& arg : args) {
        switch (arg.type) {
        case Arg::Type::number: lua_pushnumber(L, arg.number); break;
        case Arg::Type::boolean: lua_pushboolean(L, arg.boolean); break;
        case Arg::Type::text: lua_pushlstring(L, arg.text.data(), arg.text.size()); break;
        default: lua_pushnil(L); break;
        }
    }
    const bool ok = lua_pcall(L, static_cast<int>(args.size()), result != nullptr ? 1 : 0, top + 1) == 0;
    if (!ok) {
        const char* why = lua_tostring(L, -1);
        note_error(std::string("Swc.") + name + ": " + (why != nullptr ? why : "(error)"));
    } else if (result != nullptr) {
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, -2);
        lua_call(L, 1, 1);
        const char* text = lua_tostring(L, -1);
        *result = text != nullptr ? text : "";
    }
    lua_settop(L, top);
    return ok;
}

void Runtime::set_viewport(float width, float height) {
    m_model->set_viewport(width, height);
}

void Runtime::tick(double deltaSeconds) {
    m_clock += deltaSeconds;
    ++m_counts.ticks;
    call("Tick", {Arg::of(deltaSeconds)});
    for (std::string& line : m_model->take_log()) {
        note(line);
    }
}

void Runtime::build_draw_list(DrawList& out) {
    struct Estimate final : TextMeasure {
        float width(const std::string&, float size, float spacing, std::string_view text) override {
            return static_cast<float>(text.size()) * size * (0.5F + spacing / 1000.0F);
        }
        float line_height(const std::string&, float size) override { return size; }
    };
    Estimate estimate;
    swc::ui::build_draw_list(*m_model, m_measure != nullptr ? *m_measure : estimate, out);
}

void Runtime::note_stub(const std::string& kind, const std::string& name, const std::string& where) {
    ++m_counts.stubCalls;
    Stub& stub = m_stubs[kind + " " + name];
    if (stub.count == 0) {
        stub.kind = kind;
        stub.name = name;
        stub.first = where;
        ++m_counts.stubNames;
        m_log.push_back("stub " + kind + " " + name + (where.empty() ? std::string() : " (" + where + ")"));
    }
    ++stub.count;
}

void Runtime::note_error(const std::string& text) {
    ++m_counts.luaErrors;
    // The same error each frame is one line of the log; the error file has the count.
    std::uint64_t& count = m_errorCounts[text];
    if (++count > 1) {
        return;
    }
    if (m_errors.size() < 2000) {
        m_errors.push_back(text);
    }
    m_log.push_back("error " + text);
}

void Runtime::note(const std::string& text) {
    m_log.push_back(text);
}

std::vector<std::string> Runtime::take_log() {
    for (std::string& line : m_model->take_log()) {
        m_log.push_back(std::move(line));
    }
    std::vector<std::string> out;
    out.swap(m_log);
    return out;
}

bool Runtime::write_reports(std::string* error) {
    const std::string& dir = m_options.reportDir;
    std::ofstream json(dir + "/ui-stubs.json");
    std::ofstream text(dir + "/ui-stubs.txt");
    std::ofstream errors(dir + "/ui-errors.txt");
    std::ofstream wanted(dir + "/wanted.json");
    if (!json || !text || !errors || !wanted) {
        if (error != nullptr) *error = "cannot write the reports in " + dir;
        return false;
    }
    std::vector<const Stub*> stubs;
    for (const auto& [key, stub] : m_stubs) {
        stubs.push_back(&stub);
    }
    std::sort(stubs.begin(), stubs.end(), [](const Stub* a, const Stub* b) {
        return a->kind != b->kind ? a->kind < b->kind : a->name < b->name;
    });
    json << "{\"format\":1,\"ticks\":" << m_counts.ticks << ",\"luaErrors\":" << m_counts.luaErrors << ",\"stubCalls\":"
         << m_counts.stubCalls << ",\"stubs\":[";
    std::map<std::string, std::uint64_t> kinds;
    for (std::size_t i = 0; i < stubs.size(); ++i) {
        const Stub& s = *stubs[i];
        json << (i == 0 ? "" : ",") << "{\"kind\":" << json::quote(s.kind) << ",\"name\":" << json::quote(s.name)
             << ",\"count\":" << s.count << ",\"first\":" << json::quote(s.first) << "}";
        ++kinds[s.kind];
    }
    json << "]}\n";
    text << "stub log of the UI runtime: " << stubs.size() << " names, " << m_counts.stubCalls << " calls, " << m_counts.ticks
         << " ticks, " << m_counts.luaErrors << " Lua errors\n";
    for (const auto& [kind, count] : kinds) {
        text << "  " << kind << ": " << count << "\n";
    }
    text << "\n";
    for (const Stub* s : stubs) {
        text << s->kind << "\t" << s->name << "\t" << s->count << "\t" << s->first << "\n";
    }
    for (const std::string& line : m_errors) {
        errors << "[" << m_errorCounts[line] << " times] " << line << "\n\n";
    }
    // The emulated methods that the scripts called (the stubs are the other part of the run-time surface).
    if (m_lua != nullptr) {
        lua_State* L = m_lua;
        const int top = lua_gettop(L);
        lua_getglobal(L, "Swc");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "SurfaceText");
            if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == 0 && lua_isstring(L, -1)) {
                std::ofstream(dir + "/ui-surface.txt") << lua_tostring(L, -1);
            }
        }
        lua_settop(L, top);
    }
    wanted << "{\"textures\":[";
    bool first = true;
    for (const std::string& texture : m_model->missing_textures()) {
        wanted << (first ? "" : ",") << json::quote(texture);
        first = false;
    }
    wanted << "]}\n";
    return true;
}

} // namespace swc::ui
