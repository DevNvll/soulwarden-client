#include "SwcUi/Model.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace swc::ui {
namespace {

constexpr float kPi = 3.14159265358979323846F;

Edges edges_of(const json::Value& value, Edges fallback) {
    if (!value.is_array() || value.size() < 4) {
        return fallback;
    }
    return {value[0].real(), value[1].real(), value[2].real(), value[3].real()};
}

Vec2 vec_of(const json::Value& value, Vec2 fallback) {
    if (!value.is_array() || value.size() < 2) {
        return fallback;
    }
    return {value[0].real(), value[1].real()};
}

Color color_of(const json::Value& value) {
    if (!value.is_array() || value.size() < 3) {
        return {};
    }
    return {value[0].real(), value[1].real(), value[2].real(), value.size() > 3 ? value[3].real(1.0F) : 1.0F};
}

/** @return The game path without its ending and in lower case, with "/" as the separator. */
std::string stem_of(const std::string& path) {
    std::string out = lower(path);
    std::replace(out.begin(), out.end(), '\\', '/');
    while (!out.empty() && out.front() == '/') {
        out.erase(out.begin());
    }
    for (const char* ending : {".dynamicuicanvas", ".uicanvas", ".dynamicslice", ".slice", ".dds", ".png", ".tif", ".tga", ".sprite"}) {
        const std::string e(ending);
        if (out.size() > e.size() && out.compare(out.size() - e.size(), e.size(), e) == 0) {
            out.resize(out.size() - e.size());
            break;
        }
    }
    return out;
}

/** @return The size of a PNG file from its IHDR chunk. */
bool png_size(const std::string& path, float& width, float& height) {
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 24> head{};
    if (!file.read(reinterpret_cast<char*>(head.data()), head.size())) {
        return false;
    }
    if (head[0] != 0x89 || head[1] != 'P' || head[2] != 'N' || head[3] != 'G') {
        return false;
    }
    const auto be32 = [&head](std::size_t at) {
        return (static_cast<std::uint32_t>(head[at]) << 24) | (static_cast<std::uint32_t>(head[at + 1]) << 16) |
               (static_cast<std::uint32_t>(head[at + 2]) << 8) | static_cast<std::uint32_t>(head[at + 3]);
    };
    width = static_cast<float>(be32(16));
    height = static_cast<float>(be32(20));
    return true;
}

} // namespace

// ------------------------------------------------------------------------------------------------------------- helpers

Affine Affine::operator*(const Affine& o) const {
    // (this * o)(p) = this(o(p))
    return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.tx + c * o.ty + tx,
            b * o.tx + d * o.ty + ty};
}

Affine Affine::inverse() const {
    const float det = a * d - b * c;
    if (std::fabs(det) < 1e-12F) {
        return {};
    }
    const float inv = 1.0F / det;
    Affine out{d * inv, -b * inv, -c * inv, a * inv, 0.0F, 0.0F};
    out.tx = -(out.a * tx + out.c * ty);
    out.ty = -(out.b * tx + out.d * ty);
    return out;
}

Affine Affine::rotation_degrees(float degrees) {
    const float radians = degrees * kPi / 180.0F;
    const float cs = std::cos(radians);
    const float sn = std::sin(radians);
    return {cs, sn, -sn, cs, 0.0F, 0.0F};
}

std::uint32_t crc32(std::string_view text) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> out{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            out[i] = c;
        }
        return out;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const char ch : text) {
        crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

Id parse_id(const std::string& text) {
    if (text.empty()) {
        return kNoId;
    }
    Id value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return kNoId;
        }
        value = value * 10 + static_cast<Id>(c - '0');
    }
    return value;
}

// --------------------------------------------------------------------------------------------------------------- model

Model::Model(std::string contentDir) : m_contentDir(std::move(contentDir)) {}

void Model::log(std::string line) {
    m_log.push_back(std::move(line));
}

std::vector<std::string> Model::take_log() {
    std::vector<std::string> out;
    out.swap(m_log);
    return out;
}

void Model::set_viewport(float width, float height) {
    m_viewport = {std::max(width, 1.0F), std::max(height, 1.0F)};
    m_viewScale = m_viewport.y / 1080.0F;
}

Vec2 Model::target_canvas_size(const Canvas& canvas) const {
    const float scale = m_viewport.y / std::max(canvas.authoredSize.y, 1.0F);
    return {m_viewport.x / scale, canvas.authoredSize.y};
}

Id Model::new_id() {
    while (m_elements.count(m_nextId) != 0 || m_canvases.count(m_nextId) != 0) {
        ++m_nextId;
    }
    return m_nextId++;
}

const Model::Template* Model::load_template(const std::string& path, std::string* error, bool slice) {
    const std::string stem = stem_of(path);
    const std::string key = slice ? stem + "#slice" : stem;
    const auto found = m_templates.find(key);
    if (found != m_templates.end()) {
        return &found->second;
    }
    Template entry;
    std::string why;
    const std::string file = m_contentDir + "/canvases/" + stem + ".json";
    if ((!slice || !json::parse_file(m_contentDir + "/canvases/" + stem + ".slice.json", entry.doc, nullptr)) &&
        !json::parse_file(file, entry.doc, &why)) {
        if (error != nullptr) {
            *error = "no converted canvas " + stem + " (" + why + "; tools/convert-ui.sh canvases)";
        }
        return nullptr;
    }
    entry.isCanvas = entry.doc["kind"].text() == "canvas";
    return &m_templates.emplace(key, std::move(entry)).first->second;
}

void Model::read_element(const json::Value& data, Element& out) const {
    out.name = data["name"].text();
    out.enabled = data["enabled"].boolean(true);
    out.renderPriority = data["renderPriority"].integer(0);
    out.childrenSortable = data["childrenSortable"].boolean(false);
    for (const json::Value& child : data["children"].elements()) {
        out.children.push_back(parse_id(child.text()));
    }
    const json::Value& t = data["transform"];
    out.transform.anchors = edges_of(t["anchors"], out.transform.anchors);
    out.transform.offsets = edges_of(t["offsets"], {});
    out.transform.pivot = vec_of(t["pivot"], {0.5F, 0.5F});
    out.transform.rotation = t["rotation"].real();
    out.transform.scale = vec_of(t["scale"], {1.0F, 1.0F});
    out.transform.scaleToDevice = t["scaleToDevice"].boolean();
    if (const json::Value& i = data["image"]; i.is_object()) {
        ImageData image;
        image.sprite = i["sprite"].is_string() ? lower(i["sprite"].text()) : std::string();
        image.spriteType = i["spriteType"].integer();
        image.index = i["index"].integer();
        image.color = color_of(i["color"]);
        image.alpha = i["alpha"].real(1.0F);
        image.type = i["type"].integer();
        image.fillCenter = i["fillCenter"].boolean(true);
        image.stretchSliced = i["stretchSliced"].boolean(true);
        image.blend = i["blend"].integer();
        image.fillType = i["fillType"].integer();
        image.fillAmount = i["fillAmount"].real(1.0F);
        image.fillStartAngle = i["fillStartAngle"].real();
        image.fillCornerOrigin = i["fillCornerOrigin"].integer();
        image.fillEdgeOrigin = i["fillEdgeOrigin"].integer();
        image.fillClockwise = i["fillClockwise"].boolean(true);
        out.image = image;
    }
    if (const json::Value& x = data["text"]; x.is_object()) {
        TextData text;
        text.text = localize(x["text"].text());
        text.markup = x["markup"].boolean();
        text.color = color_of(x["color"]);
        text.alpha = x["alpha"].real(1.0F);
        text.font = lower(x["font"].text());
        text.size = x["size"].real(32.0F);
        text.effect = x["effect"].integer();
        text.hAlign = x["hAlign"].integer();
        text.vAlign = x["vAlign"].integer();
        text.charSpacing = x["charSpacing"].real();
        text.lineSpacing = x["lineSpacing"].real();
        text.overflow = x["overflow"].integer();
        text.wrap = x["wrap"].integer();
        text.shrink = x["shrink"].integer();
        text.minShrink = x["minShrink"].real();
        text.textCase = x["case"].integer();
        out.text = text;
    }
    if (const json::Value& f = data["fader"]; f.is_object()) {
        out.fade = f["fade"].real(1.0F);
    }
    if (const json::Value& m = data["mask"]; m.is_object()) {
        MaskData mask;
        mask.enabled = m["enabled"].boolean(true);
        mask.interaction = m["interaction"].boolean(true);
        mask.childMask = m["childMask"].is_string() ? parse_id(m["childMask"].text()) : kNoId;
        mask.rtt = m["rtt"].boolean();
        mask.drawBehind = m["drawBehind"].boolean();
        mask.drawInFront = m["drawInFront"].boolean();
        mask.alphaTest = m["alphaTest"].boolean();
        out.mask = mask;
    }
    if (const json::Value& f = data["flipbook"]; f.is_object()) {
        FlipbookData book;
        book.start = f["start"].integer();
        book.end = f["end"].integer();
        book.loopStart = f["loopStart"].integer();
        book.loopType = f["loopType"].integer();
        book.rateUnit = f["rateUnit"].integer();
        book.rate = f["rate"].real(30.0F);
        book.startDelay = f["startDelay"].real();
        book.loopDelay = f["loopDelay"].real();
        book.reverseDelay = f["reverseDelay"].real();
        book.autoPlay = f["autoPlay"].boolean();
        book.frame = book.start;
        out.flipbook = book;
    }
    if (const json::Value& s = data["imageSequence"]; s.is_object()) {
        SequenceData sequence;
        sequence.type = s["type"].integer();
        for (const json::Value& image : s["images"].elements()) {
            sequence.images.push_back(lower(image.text()));
        }
        sequence.folder = lower(s["folder"].text());
        sequence.index = s["index"].integer();
        sequence.color = color_of(s["color"]);
        sequence.alpha = s["alpha"].real(1.0F);
        sequence.blend = s["blend"].integer();
        out.sequence = sequence;
    }
    if (const json::Value& s = data["spawner"]; s.is_object()) {
        out.spawnerSlice = lower(s["slice"].text());
    }
    if (const json::Value& d = data["desaturator"]; d.is_object()) {
        out.saturation = d["saturation"].real(1.0F);
    }
    if (const json::Value& r = data["imageRepeater"]; r.is_object()) {
        out.repeats = r["repeats"].integer();
    }
    if (const json::Value& b = data["button"]; b.is_object()) {
        out.button = b;
    }
    for (const char* key : {"interactable", "checkbox", "radio", "radioGroup", "slider", "scrollBar", "wheel", "scrollBox",
                            "textInput", "dropdown", "draggable", "dropTarget", "markupButton", "exitHover", "dynamicScrollBox",
                            "dynamicLayout"}) {
        if (const json::Value& component = data[key]; component.is_object()) {
            out.components[key] = component;
        }
    }
    if (const auto found = out.components.find("interactable"); found != out.components.end()) {
        out.handling = found->second["handling"].boolean(true);
    }
    for (const json::Value& script : data["scripts"].elements()) {
        ScriptData entry;
        entry.script = lower(script["script"].text());
        entry.properties = script["properties"];
        out.scripts.push_back(std::move(entry));
    }
    // The layouts: row and column (cells), the fixed forms of the fork, and the grid.
    for (const char* key : {"layoutRow", "layoutColumn", "layoutRowFixed", "layoutColumnFixed", "layoutGrid"}) {
        const json::Value& l = data[key];
        if (!l.is_object()) {
            continue;
        }
        const std::string kind = key;
        LayoutData layout;
        layout.kind = kind == "layoutGrid" ? LayoutData::Kind::grid
                      : (kind == "layoutRow" || kind == "layoutRowFixed") ? LayoutData::Kind::row : LayoutData::Kind::column;
        layout.fixed = kind.find("Fixed") != std::string::npos;
        const json::Value& p = l["padding"];
        if (p.size() == 4) {
            layout.padding = {p[0].real(), p[1].real(), p[2].real(), p[3].real()};
        }
        if (layout.kind == LayoutData::Kind::grid) {
            layout.spacing = vec_of(l["spacing"], {});
            layout.cellSize = vec_of(l["cellSize"], {100.0F, 100.0F});
            layout.horizontalOrder = l["hOrder"].integer();
            layout.order = l["vOrder"].integer();
            layout.startingWith = l["startingWith"].integer();
        } else {
            layout.spacing = {l["spacing"].real(), 0.0F};
            layout.order = l["order"].integer();
        }
        layout.hAlign = l["hAlign"].integer();
        layout.vAlign = l["vAlign"].integer();
        layout.ignoreDefaultCells = l["ignoreDefaultCells"].boolean(true);
        layout.visibleOnly = l["visibleOnly"].boolean(false);
        out.layout = layout;
    }
    if (const json::Value& cell = data["layoutCell"]; cell.is_object()) {
        CellData c;
        auto read = [&cell](const char* key, std::optional<float>& value) {
            if (cell[key].is_number()) {
                value = cell[key].real();
            }
        };
        read("minWidth", c.minWidth);
        read("minHeight", c.minHeight);
        read("targetWidth", c.targetWidth);
        read("targetHeight", c.targetHeight);
        read("extraWidthRatio", c.extraWidthRatio);
        read("extraHeightRatio", c.extraHeightRatio);
        out.cell = c;
    }
    if (const json::Value& f = data["layoutFitter"]; f.is_object()) {
        out.fitter = FitterData{f["horizontal"].boolean(), f["vertical"].boolean()};
    }
    for (const char* key : {"progressBar"}) {
        if (data[key].is_object()) {
            out.other.emplace_back(key);
        }
    }
    for (const json::Value& name : data["other"].elements()) {
        out.other.push_back(name.text());
    }
}

void Model::remap_entities(json::Value& value, const std::unordered_map<Id, Id>& map) const {
    // The data of an interactive component: a member whose key ends with "Entity" names an element.
    if (value.is_object()) {
        for (json::Value::Member& member : value.members_mut()) {
            const std::string& key = member.first;
            if (key.size() > 6 && key.compare(key.size() - 6, 6, "Entity") == 0 && member.second.is_string()) {
                const auto found = map.find(parse_id(member.second.text()));
                if (found != map.end()) {
                    member.second.set_text(std::to_string(found->second));
                }
            } else {
                remap_entities(member.second, map);
            }
        }
    } else if (value.is_array()) {
        for (json::Value& element : value.elements_mut()) {
            remap_entities(element, map);
        }
    }
}

void Model::remap_properties(json::Value& value, const std::unordered_map<Id, Id>& map) const {
    if (value.is_object()) {
        const json::Value& type = value["t"];
        if (type.text() == "id") {
            for (json::Value::Member& member : value.members_mut()) {
                if (member.first == "v" && member.second.is_string()) {
                    const auto found = map.find(parse_id(member.second.text()));
                    if (found != map.end()) {
                        member.second.set_text(std::to_string(found->second));
                    }
                }
            }
            return;
        }
        for (json::Value::Member& member : value.members_mut()) {
            remap_properties(member.second, map);
        }
    } else if (value.is_array()) {
        for (json::Value& element : value.elements_mut()) {
            remap_properties(element, map);
        }
    }
}

Id Model::load_canvas(const std::string& path, std::string* error) {
    const std::string stem = stem_of(path);
    if (const Id loaded = find_canvas_by_path(stem); loaded != kNoId) {
        return loaded;
    }
    const Template* entry = load_template(stem, error);
    if (entry == nullptr) {
        return kNoId;
    }
    if (!entry->isCanvas) {
        if (error != nullptr) {
            *error = stem + " is a UI slice, not a canvas";
        }
        return kNoId;
    }
    const json::Value& doc = entry->doc;
    const json::Value& info = doc["canvas"];
    Canvas canvas;
    canvas.path = stem;
    canvas.name = stem.substr(stem.rfind('/') + 1);
    canvas.authoredSize = vec_of(info["size"], {1920.0F, 1080.0F});
    canvas.drawOrder = info["drawOrder"].integer();
    canvas.pixelAligned = info["pixelAligned"].boolean(true);
    canvas.posInput = info["posInput"].boolean(true);
    canvas.loadSequence = ++m_loadSequence;
    if (const json::Value& j = info["javelin"]; j.is_object()) {
        canvas.javelin.present = true;
        canvas.javelin.requiresMouse = j["requiresMouse"].boolean();
        canvas.javelin.escapable = j["escapable"].boolean();
        canvas.javelin.replaceable = j["replaceable"].boolean();
        canvas.javelin.drawLayer = j["drawLayer"].text();
        for (const json::Value& map : j["actionMapOverrides"].elements()) {
            canvas.javelin.actionMapOverrides.push_back(map.text());
        }
    }
    for (const json::Value& script : info["scripts"].elements()) {
        ScriptData entry;
        entry.script = lower(script["script"].text());
        entry.properties = script["properties"];
        canvas.scripts.push_back(std::move(entry));
    }
    canvas.entity = parse_id(info["id"].text());
    if (canvas.entity == kNoId || m_canvases.count(canvas.entity) != 0 || m_elements.count(canvas.entity) != 0) {
        canvas.entity = new_id();
    }
    // An id that a loaded element has already (two canvases with copies of the same entities) gets a new id.
    std::unordered_map<Id, Id> remap;
    for (const json::Value& data : doc["elements"].elements()) {
        const Id id = parse_id(data["id"].text());
        if (m_elements.count(id) != 0 || m_canvases.count(id) != 0) {
            remap[id] = new_id();
        }
    }
    const auto mapped = [&remap](Id id) {
        const auto found = remap.find(id);
        return found != remap.end() ? found->second : id;
    };
    std::vector<Id> ids;
    for (const json::Value& data : doc["elements"].elements()) {
        Element element;
        element.id = mapped(parse_id(data["id"].text()));
        read_element(data, element);
        for (Id& child : element.children) {
            child = mapped(child);
        }
        if (element.mask && element.mask->childMask != kNoId) {
            element.mask->childMask = mapped(element.mask->childMask);
        }
        if (!remap.empty()) {
            for (ScriptData& script : element.scripts) {
                remap_properties(script.properties, remap);
            }
            for (auto& [key, component] : element.components) {
                remap_entities(component, remap);
            }
        }
        element.canvas = canvas.entity;
        ids.push_back(element.id);
        if (element.layout) {
            m_layoutElements.insert(element.id);
        }
        m_elements.emplace(element.id, std::move(element));
    }
    for (const Id id : ids) {
        Element& element = m_elements[id];
        std::vector<Id> kept;
        for (const Id child : element.children) {
            const auto found = m_elements.find(child);
            if (found != m_elements.end()) {
                found->second.parent = id;
                kept.push_back(child);
            }
        }
        element.children.swap(kept);
    }
    canvas.root = mapped(parse_id(info["root"].text()));
    if (m_elements.count(canvas.root) == 0) {
        // No root in the data: the first element with no parent.
        for (const Id id : ids) {
            if (m_elements[id].parent == kNoId) {
                canvas.root = id;
                break;
            }
        }
    }
    const Id canvasId = canvas.entity;
    const Id rootId = canvas.root;
    m_canvases.emplace(canvasId, std::move(canvas));
    // The layouts of the canvas, before the scripts activate (LyShine InitializeLayouts).
    compute_layout(rootId);
    return canvasId;
}

void Model::unload_canvas(Id canvasId, std::vector<Id>* destroyed) {
    const auto found = m_canvases.find(canvasId);
    if (found == m_canvases.end()) {
        return;
    }
    const Id root = found->second.root;
    // The elements of the canvas that are not under the root (a parent that the scripts removed) go too.
    std::vector<Id> loose;
    for (const auto& [id, element] : m_elements) {
        if (element.canvas == canvasId && element.parent == kNoId && id != root) {
            loose.push_back(id);
        }
    }
    destroy_element(root, destroyed);
    for (const Id id : loose) {
        destroy_element(id, destroyed);
    }
    m_canvases.erase(found);
}

Canvas* Model::canvas(Id id) {
    const auto found = m_canvases.find(id);
    return found != m_canvases.end() ? &found->second : nullptr;
}

Id Model::find_canvas_by_path(const std::string& path) const {
    const std::string stem = stem_of(path);
    for (const auto& [id, canvas] : m_canvases) {
        if (canvas.path == stem) {
            return id;
        }
    }
    return kNoId;
}

std::vector<Canvas*> Model::canvases_in_order() {
    std::vector<Canvas*> out;
    for (auto& [id, canvas] : m_canvases) {
        out.push_back(&canvas);
    }
    std::stable_sort(out.begin(), out.end(), [](const Canvas* a, const Canvas* b) {
        return a->drawOrder != b->drawOrder ? a->drawOrder < b->drawOrder : a->loadSequence < b->loadSequence;
    });
    return out;
}

std::vector<Id> Model::subtree(Id root) const {
    std::vector<Id> out;
    std::vector<Id> stack{root};
    while (!stack.empty()) {
        const Id id = stack.back();
        stack.pop_back();
        const Element* e = element(id);
        if (e == nullptr) {
            continue;
        }
        out.push_back(id);
        for (auto it = e->children.rbegin(); it != e->children.rend(); ++it) {
            stack.push_back(*it);
        }
    }
    return out;
}

Element* Model::element(Id id) {
    const auto found = m_elements.find(id);
    return found != m_elements.end() ? &found->second : nullptr;
}

const Element* Model::element(Id id) const {
    const auto found = m_elements.find(id);
    return found != m_elements.end() ? &found->second : nullptr;
}

namespace {

void insert_child(Element& parent, Id child, Id before) {
    auto at = std::find(parent.children.begin(), parent.children.end(), before);
    parent.children.insert(at, child);
}

} // namespace

Id Model::clone_element(Id source, Id parent, Id before, std::vector<Id>* created) {
    if (element(source) == nullptr) {
        return kNoId;
    }
    const std::vector<Id> ids = subtree(source);
    std::unordered_map<Id, Id> map;
    for (const Id id : ids) {
        map[id] = new_id();
    }
    const auto mapped = [&map](Id id) {
        const auto found = map.find(id);
        return found != map.end() ? found->second : id;
    };
    Element* target = element(parent);
    const Id canvasId = target != nullptr ? target->canvas : element(source)->canvas;
    for (const Id id : ids) {
        Element copy = *element(id);
        copy.id = map[id];
        copy.parent = id == source ? parent : mapped(copy.parent);
        copy.canvas = canvasId;
        for (Id& child : copy.children) {
            child = mapped(child);
        }
        if (copy.mask && copy.mask->childMask != kNoId) {
            copy.mask->childMask = mapped(copy.mask->childMask);
        }
        for (ScriptData& script : copy.scripts) {
            remap_properties(script.properties, map);
        }
        for (auto& [key, component] : copy.components) {
            remap_entities(component, map);
        }
        if (copy.flipbook) {
            copy.flipbook->playing = false;
        }
        if (copy.layout) {
            m_layoutElements.insert(copy.id);
        }
        m_elements.emplace(copy.id, std::move(copy));
        if (created != nullptr) {
            created->push_back(map[id]);
        }
    }
    if (target != nullptr) {
        insert_child(*target, map[source], before);
        child_list_changed(parent);
    }
    return map[source];
}

Id Model::spawn_slice(const std::string& slicePath, Id parent, std::vector<Id>* created, std::string* error,
                      std::unordered_map<Id, Id>* idMap) {
    const Template* entry = load_template(slicePath, error, true);
    if (entry == nullptr) {
        return kNoId;
    }
    const json::Value& doc = entry->doc;
    std::unordered_map<Id, Id> map;
    for (const json::Value& data : doc["elements"].elements()) {
        map[parse_id(data["id"].text())] = new_id();
    }
    const auto mapped = [&map](Id id) {
        const auto found = map.find(id);
        return found != map.end() ? found->second : id;
    };
    Element* target = element(parent);
    const Id canvasId = target != nullptr ? target->canvas : kNoId;
    std::vector<Id> ids;
    for (const json::Value& data : doc["elements"].elements()) {
        Element element;
        element.id = mapped(parse_id(data["id"].text()));
        read_element(data, element);
        for (Id& child : element.children) {
            child = mapped(child);
        }
        if (element.mask && element.mask->childMask != kNoId) {
            element.mask->childMask = mapped(element.mask->childMask);
        }
        for (ScriptData& script : element.scripts) {
            remap_properties(script.properties, map);
        }
        for (auto& [key, component] : element.components) {
            remap_entities(component, map);
        }
        element.canvas = canvasId;
        ids.push_back(element.id);
        if (element.layout) {
            m_layoutElements.insert(element.id);
        }
        m_elements.emplace(element.id, std::move(element));
    }
    for (const Id id : ids) {
        for (const Id child : m_elements[id].children) {
            if (Element* c = this->element(child)) {
                c->parent = id;
            }
        }
    }
    Id root = kNoId;
    if (doc["roots"].size() > 0) {
        root = mapped(parse_id(doc["roots"][0].text()));
    }
    if (root == kNoId && !ids.empty()) {
        root = ids.front();
    }
    if (Element* r = element(root); r != nullptr && target != nullptr) {
        r->parent = parent;
        insert_child(*target, root, kNoId);
        child_list_changed(parent);
    }
    if (created != nullptr) {
        for (const Id id : subtree(root)) {
            created->push_back(id);
        }
    }
    if (idMap != nullptr) {
        *idMap = map;
    }
    return root;
}

Id Model::create_element(const std::string& name, Id parent, Id before) {
    Element e;
    e.id = new_id();
    e.name = name;
    e.parent = parent;
    Element* target = element(parent);
    e.canvas = target != nullptr ? target->canvas : kNoId;
    const Id id = e.id;
    m_elements.emplace(id, std::move(e));
    if (target != nullptr) {
        insert_child(*target, id, before);
        child_list_changed(parent);
    }
    return id;
}

void Model::destroy_recursive(Id id, std::vector<Id>* destroyed) {
    Element* e = element(id);
    if (e == nullptr) {
        return;
    }
    const std::vector<Id> children = e->children;
    for (const Id child : children) {
        destroy_recursive(child, destroyed);
    }
    if (destroyed != nullptr) {
        destroyed->push_back(id);
    }
    m_layoutElements.erase(id);
    m_layoutSizes.erase(id);
    m_layoutMarks.erase(std::remove(m_layoutMarks.begin(), m_layoutMarks.end(), id), m_layoutMarks.end());
    m_elements.erase(id);
}

void Model::destroy_element(Id id, std::vector<Id>* destroyed) {
    Element* e = element(id);
    if (e == nullptr) {
        return;
    }
    const Id parentId = e->parent;
    if (Element* parent = element(parentId)) {
        parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), id), parent->children.end());
    }
    destroy_recursive(id, destroyed);
    child_list_changed(parentId);
}

bool Model::reparent(Id id, Id parent, Id before) {
    Element* e = element(id);
    Element* target = element(parent);
    if (e == nullptr || target == nullptr || id == parent) {
        return false;
    }
    // No cycle: the new parent is not in the subtree of the element.
    for (Id at = parent; at != kNoId;) {
        if (at == id) {
            return false;
        }
        const Element* p = element(at);
        at = p != nullptr ? p->parent : kNoId;
    }
    const Id oldParent = e->parent;
    if (Element* old = element(oldParent)) {
        old->children.erase(std::remove(old->children.begin(), old->children.end(), id), old->children.end());
    }
    e->parent = parent;
    insert_child(*target, id, before);
    child_list_changed(oldParent);
    child_list_changed(parent);
    if (e->canvas != target->canvas) {
        for (const Id sub : subtree(id)) {
            element(sub)->canvas = target->canvas;
        }
    }
    return true;
}

Id Model::find_child(Id parent, const std::string& name) const {
    const Element* e = element(parent);
    if (e == nullptr) {
        return kNoId;
    }
    for (const Id child : e->children) {
        const Element* c = element(child);
        if (c != nullptr && c->name == name) {
            return child;
        }
    }
    return kNoId;
}

Id Model::find_descendant(Id parent, const std::string& name) const {
    // Breadth first, as LyShine: the nearest match.
    const Element* e = element(parent);
    if (e == nullptr) {
        return kNoId;
    }
    std::vector<Id> level = e->children;
    while (!level.empty()) {
        std::vector<Id> next;
        for (const Id id : level) {
            const Element* c = element(id);
            if (c == nullptr) {
                continue;
            }
            if (c->name == name) {
                return id;
            }
            next.insert(next.end(), c->children.begin(), c->children.end());
        }
        level.swap(next);
    }
    return kNoId;
}

bool Model::enabled_in_hierarchy(Id id) const {
    for (Id at = id; at != kNoId;) {
        const Element* e = element(at);
        if (e == nullptr) {
            return false;
        }
        if (!e->enabled) {
            return false;
        }
        at = e->parent;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------------- transforms

Edges Model::rect(Id id) const {
    const Element* e = element(id);
    if (e == nullptr) {
        return {};
    }
    if (e->parent == kNoId || element(e->parent) == nullptr) {
        const auto found = m_canvases.find(e->canvas);
        const Vec2 size = found != m_canvases.end() ? target_canvas_size(found->second) : Vec2{1920.0F, 1080.0F};
        return {0.0F, 0.0F, size.x, size.y};
    }
    const Edges p = rect(e->parent);
    const float pw = p.right - p.left;
    const float ph = p.bottom - p.top;
    const TransformData& t = e->transform;
    Edges r{p.left + pw * t.anchors.left + t.offsets.left, p.top + ph * t.anchors.top + t.offsets.top,
            p.left + pw * t.anchors.right + t.offsets.right, p.top + ph * t.anchors.bottom + t.offsets.bottom};
    if (r.left > r.right) {
        r.left = r.right = (r.left + r.right) * 0.5F;
    }
    if (r.top > r.bottom) {
        r.top = r.bottom = (r.top + r.bottom) * 0.5F;
    }
    return r;
}

Vec2 Model::device_scale(Id id) const {
    const Element* e = element(id);
    if (e == nullptr) {
        return {1.0F, 1.0F};
    }
    Vec2 scale = e->transform.scale;
    if (e->transform.scaleToDevice) {
        const auto found = m_canvases.find(e->canvas);
        if (found != m_canvases.end()) {
            const Vec2 target = target_canvas_size(found->second);
            const float uniform = std::min(target.x / found->second.authoredSize.x, target.y / found->second.authoredSize.y);
            scale.x *= uniform;
            scale.y *= uniform;
        }
    }
    return scale;
}

Vec2 Model::pivot_point(Id id) const {
    const Element* e = element(id);
    const Edges r = rect(id);
    if (e == nullptr) {
        return {};
    }
    return {r.left + (r.right - r.left) * e->transform.pivot.x, r.top + (r.bottom - r.top) * e->transform.pivot.y};
}

Affine Model::local_transform(Id id) const {
    const Element* e = element(id);
    if (e == nullptr || e->parent == kNoId) {
        return {};
    }
    const Vec2 scale = device_scale(id);
    if (e->transform.rotation == 0.0F && scale.x == 1.0F && scale.y == 1.0F) {
        return {};
    }
    const Vec2 pivot = pivot_point(id);
    return Affine::translation(pivot.x, pivot.y) * Affine::rotation_degrees(e->transform.rotation) * Affine::scaling(scale.x, scale.y) *
           Affine::translation(-pivot.x, -pivot.y);
}

Affine Model::to_canvas(Id id) const {
    const Element* e = element(id);
    if (e == nullptr || e->parent == kNoId) {
        return {};
    }
    return to_canvas(e->parent) * local_transform(id);
}

Affine Model::to_viewport(Id id) const {
    const Element* e = element(id);
    float scale = m_viewScale;
    if (e != nullptr) {
        const auto found = m_canvases.find(e->canvas);
        if (found != m_canvases.end()) {
            scale = m_viewport.y / std::max(found->second.authoredSize.y, 1.0F);
        }
    }
    return Affine::scaling(scale, scale) * to_canvas(id);
}

Vec2 Model::local_position(Id id) const {
    const Element* e = element(id);
    if (e == nullptr || e->parent == kNoId) {
        return {};
    }
    const Edges p = rect(e->parent);
    const float pw = p.right - p.left;
    const float ph = p.bottom - p.top;
    const TransformData& t = e->transform;
    const Vec2 center{p.left + pw * (t.anchors.left + t.anchors.right) * 0.5F, p.top + ph * (t.anchors.top + t.anchors.bottom) * 0.5F};
    const Vec2 pivot = pivot_point(id);
    return {pivot.x - center.x, pivot.y - center.y};
}

void Model::set_local_position(Id id, Vec2 position) {
    Element* e = element(id);
    if (e == nullptr || e->parent == kNoId) {
        return;
    }
    const Vec2 current = local_position(id);
    const float dx = position.x - current.x;
    const float dy = position.y - current.y;
    e->transform.offsets.left += dx;
    e->transform.offsets.right += dx;
    e->transform.offsets.top += dy;
    e->transform.offsets.bottom += dy;
}

float Model::local_width(Id id) const {
    const Element* e = element(id);
    if (e == nullptr) {
        return 0.0F;
    }
    if (e->transform.anchors.left == e->transform.anchors.right) {
        return e->transform.offsets.right - e->transform.offsets.left;
    }
    const Edges r = rect(id);
    return r.right - r.left;
}

float Model::local_height(Id id) const {
    const Element* e = element(id);
    if (e == nullptr) {
        return 0.0F;
    }
    if (e->transform.anchors.top == e->transform.anchors.bottom) {
        return e->transform.offsets.bottom - e->transform.offsets.top;
    }
    const Edges r = rect(id);
    return r.bottom - r.top;
}

void Model::set_local_width(Id id, float width) {
    Element* e = element(id);
    if (e == nullptr || e->transform.anchors.left != e->transform.anchors.right) {
        return;
    }
    const float diff = width - (e->transform.offsets.right - e->transform.offsets.left);
    e->transform.offsets.left -= diff * e->transform.pivot.x;
    e->transform.offsets.right += diff * (1.0F - e->transform.pivot.x);
}

void Model::set_local_height(Id id, float height) {
    Element* e = element(id);
    if (e == nullptr || e->transform.anchors.top != e->transform.anchors.bottom) {
        return;
    }
    const float diff = height - (e->transform.offsets.bottom - e->transform.offsets.top);
    e->transform.offsets.top -= diff * e->transform.pivot.y;
    e->transform.offsets.bottom += diff * (1.0F - e->transform.pivot.y);
}

Vec2 Model::viewport_position(Id id) const {
    const Element* e = element(id);
    if (e == nullptr) {
        return {};
    }
    // The pivot through the transforms of the parents (the own rotation and scale act around the pivot).
    const Affine parent = e->parent != kNoId ? to_viewport(e->parent) : to_viewport(id);
    return parent.apply(pivot_point(id));
}

void Model::set_viewport_position(Id id, Vec2 position) {
    Element* e = element(id);
    if (e == nullptr || e->parent == kNoId) {
        return;
    }
    const Vec2 current = pivot_point(id);
    const Vec2 target = to_viewport(e->parent).inverse().apply(position);
    e->transform.offsets.left += target.x - current.x;
    e->transform.offsets.right += target.x - current.x;
    e->transform.offsets.top += target.y - current.y;
    e->transform.offsets.bottom += target.y - current.y;
}

Vec2 Model::canvas_position(Id id) const {
    const Element* e = element(id);
    if (e == nullptr) {
        return {};
    }
    const Affine parent = e->parent != kNoId ? to_canvas(e->parent) : Affine{};
    return parent.apply(pivot_point(id));
}

void Model::set_canvas_position(Id id, Vec2 position) {
    Element* e = element(id);
    if (e == nullptr || e->parent == kNoId) {
        return;
    }
    const Vec2 current = pivot_point(id);
    const Vec2 target = to_canvas(e->parent).inverse().apply(position);
    e->transform.offsets.left += target.x - current.x;
    e->transform.offsets.right += target.x - current.x;
    e->transform.offsets.top += target.y - current.y;
    e->transform.offsets.bottom += target.y - current.y;
}

void Model::viewport_points(Id id, Vec2 out[4]) const {
    const Edges r = rect(id);
    const Affine m = to_viewport(id);
    out[0] = m.apply({r.left, r.top});
    out[1] = m.apply({r.right, r.top});
    out[2] = m.apply({r.right, r.bottom});
    out[3] = m.apply({r.left, r.bottom});
}

// ------------------------------------------------------------------------------------------------------------ content

const Sprite& Model::sprite(const std::string& texturePath) {
    const std::string stem = stem_of(texturePath);
    const auto found = m_sprites.find(stem);
    if (found != m_sprites.end()) {
        return found->second;
    }
    Sprite sprite;
    sprite.file = m_contentDir + "/textures/" + stem + ".png";
    const auto read_side = [&sprite](const json::Value& side) {
        sprite.borders = edges_of(side["borders"], sprite.borders);
        for (const json::Value& cell : side["cells"].elements()) {
            Sprite::Cell c;
            c.uv = edges_of(cell["uv"], c.uv);
            c.borders = edges_of(cell["borders"], c.borders);
            c.alias = cell["alias"].text();
            sprite.cells.push_back(c);
        }
    };
    if (png_size(sprite.file, sprite.width, sprite.height)) {
        sprite.found = true;
        sprite.converted = true;
        json::Value side;
        if (json::parse_file(m_contentDir + "/textures/" + stem + ".sprite.json", side)) {
            read_side(side);
        }
    } else {
        m_missingTextures.insert(stem + ".dds");
        log("texture " + stem + ".dds is not in the converted content (tools/convert-ui.sh wanted)");
        // The size, borders, and cells of the index (the layout cells of images need them).
        if (!m_textureIndexLoaded) {
            m_textureIndexLoaded = true;
            json::parse_file(m_contentDir + "/textures/index.json", m_textureIndex);
        }
        const json::Value& entry = m_textureIndex[stem];
        if (entry.is_object() && entry["size"].size() == 2) {
            sprite.found = true;
            sprite.width = entry["size"][0].real();
            sprite.height = entry["size"][1].real();
            read_side(entry);
        }
    }
    return m_sprites.emplace(stem, std::move(sprite)).first->second;
}

void Model::load_fonts() {
    if (m_fontsLoaded) {
        return;
    }
    m_fontsLoaded = true;
    json::Value doc;
    std::string why;
    if (!json::parse_file(m_contentDir + "/fonts/fonts.json", doc, &why)) {
        log("no font table: " + why + " (tools/convert-ui.sh fonts)");
    }
    for (const json::Value::Member& member : doc.members()) {
        FontInfo info;
        info.file = m_contentDir + "/fonts/files/" + member.second["file"].text();
        info.size = member.second["size"].real(32.0F);
        m_fonts[lower(member.first)] = info;
    }
    json::Value effects;
    if (json::parse_file(m_contentDir + "/fonts/effects.json", effects, &why)) {
        for (const json::Value& effect : effects.elements()) {
            FontEffect out;
            out.name = effect["name"].text();
            for (const json::Value& pass : effect["passes"].elements()) {
                FontEffect::Pass p;
                if (pass["color"].is_array()) {
                    p.color = color_of(pass["color"]);
                }
                p.offset = vec_of(pass["offset"], {});
                out.passes.push_back(p);
            }
            m_effects.push_back(std::move(out));
        }
    }
}

const FontInfo* Model::font(const std::string& fontPath) {
    load_fonts();
    std::string key = lower(fontPath);
    std::replace(key.begin(), key.end(), '\\', '/');
    if (key.size() < 5 || key.compare(key.size() - 5, 5, ".font") != 0) {
        key += ".font";
    }
    const auto found = m_fonts.find(key);
    return found != m_fonts.end() ? &found->second : nullptr;
}

const FontEffect* Model::font_effect(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= m_effects.size()) {
        return nullptr;
    }
    return &m_effects[static_cast<std::size_t>(index)];
}

int Model::font_effect_index(const std::string& name) const {
    for (std::size_t i = 0; i < m_effects.size(); ++i) {
        if (lower(m_effects[i].name) == lower(name)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Model::load_texts() {
    if (m_textsLoaded) {
        return;
    }
    m_textsLoaded = true;
    json::Value doc;
    std::string why;
    if (!json::parse_file(m_contentDir + "/localization/en-us.json", doc, &why)) {
        const_cast<Model*>(this)->log("no texts: " + why + " (tools/convert-ui.sh text)");
        return;
    }
    m_texts.reserve(doc.size());
    for (json::Value::Member& member : doc.members_mut()) {
        m_texts.emplace(member.first, member.second.text());
    }
}

std::string Model::localize(const std::string& text) const {
    if (text.size() >= 2 && text[0] != '@' && text.find('@') != std::string::npos) {
        // A label inside a longer text ("30@ui_seconds_short" of TimeHelperFunctions ConvertToShorthandString, the duration of a
        // status in the tray): the localization of the engine replaces each @ label (letters, digits, and _) that it knows.
        std::string out;
        std::size_t i = 0;
        while (i < text.size()) {
            const std::size_t at = text.find('@', i);
            if (at == std::string::npos) {
                out += text.substr(i);
                break;
            }
            out += text.substr(i, at - i);
            std::size_t end = at + 1;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) != 0 || text[end] == '_')) {
                ++end;
            }
            const std::string label = text.substr(at, end - at);
            const std::string local = label.size() > 1 ? localize(label) : label;
            out += local;
            i = end;
        }
        return out;
    }
    if (text.size() < 2 || text[0] != '@') {
        return text;
    }
    const_cast<Model*>(this)->load_texts();
    const auto found = m_texts.find(lower(text.substr(1)));
    if (found == m_texts.end()) {
        // A label with more text after a space ("@ui_monitor_name 1"): the label is localized, the rest stays (the
        // localization of the engine replaces the @ label of a longer text).
        const std::size_t space = text.find(' ');
        if (space != std::string::npos && space > 1) {
            const std::string label = localize(text.substr(0, space));
            if (label != text.substr(0, space)) {
                return label + text.substr(space);
            }
        }
        return text;
    }
    // The text files have "\n" (a backslash and n) for a new line.
    std::string out;
    out.reserve(found->second.size());
    for (std::size_t i = 0; i < found->second.size(); ++i) {
        if (found->second[i] == '\\' && i + 1 < found->second.size() && found->second[i + 1] == 'n') {
            out += '\n';
            ++i;
        } else {
            out += found->second[i];
        }
    }
    return out;
}

bool Model::contains(Id id, Vec2 point) const {
    // The point in the canvas space of the element (no scale, no rotation), against its rect.
    const Vec2 local = to_viewport(id).inverse().apply(point);
    const Edges r = rect(id);
    return local.x >= r.left && local.x <= r.right && local.y >= r.top && local.y <= r.bottom;
}

Id Model::interactable_in(Id id, Vec2 point, Vec2 canvasPoint) {
    const Element* e = element(id);
    if (e == nullptr || !e->enabled) {
        return kNoId;
    }
    // A mask with "mask interaction" hides the points outside it from its children.
    const bool masked = e->mask && e->mask->enabled && e->mask->interaction && !contains(id, point);
    if (!masked) {
        // The last child first (LyShine FindInteractableToHandleEvent: the child order, not the render priority).
        const std::vector<Id> order = e->children;
        for (auto child = order.rbegin(); child != order.rend(); ++child) {
            if (const Id hit = interactable_in(*child, point, canvasPoint); hit != kNoId) {
                return hit;
            }
        }
    }
    if (e->handling && e->components.count("interactable") != 0 && contains(id, point)) {
        return id;
    }
    return kNoId;
}

Id Model::interactable_at(Vec2 point) {
    std::vector<Canvas*> canvases = canvases_in_order();
    for (auto it = canvases.rbegin(); it != canvases.rend(); ++it) {
        if (!(*it)->enabled || !(*it)->posInput) {
            continue;
        }
        if (const Id hit = interactable_in((*it)->root, point, point); hit != kNoId) {
            return hit;
        }
    }
    return kNoId;
}

} // namespace swc::ui
