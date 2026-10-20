// Tests of the menu document reader of the client (stage F1 of docs/FRONTEND.md) with documents that the backend makes: the one
// writer of the backend (state/creation_data.cpp make_menu_document) and its readers. The names and ids are made up. No capture
// of the user. Build and run: tools/test-menu.sh. The Soulwarden sources are read only.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "Menu/SwcMenuDocument.h"
#include "core/encoding/base64.h"
#include "state/creation_data.h"

namespace {

namespace backend = soulwarden::backend;
namespace menu = swc::menu;

int g_checks = 0;
int g_failures = 0;

void expect(bool condition, const std::string& text) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL %s\n", text.c_str());
    }
}

backend::state::AppearanceNames male_names() {
    return {"Male", "Human_Base", "skin04", "hair_m_01", "None", "hair_color_03", "hair_color_03", "eye_color_02",
            "None", "None", "None", "None"};
}

backend::state::AppearanceNames female_names() {
    return {"Female", "Human_Base", "skin09", "hair_f_12", "None", "hair_color_07", "hair_color_07", "eye_color_05",
            "facemark_02", "scar_01", "tattoo_03", "tattoo_color_04"};
}

/** The document text of the backend writer for the values. */
std::string make(const backend::state::MenuDocument& values) {
    return backend::state::make_menu_document(values);
}

/** The inflated JSON of a text of the backend writer (its zlib has stored blocks). */
std::string inflate_text(const std::string& text) {
    std::vector<std::uint8_t> compressed;
    std::vector<std::uint8_t> json;
    bool too_large = false;
    if (!menu::DecodeBase64(text, compressed) || !menu::Inflate(compressed, 1 << 20, json, too_large)) {
        return {};
    }
    return std::string(json.begin(), json.end());
}

/** A text with real deflate blocks (the zlib of the game), from a JSON document. */
std::string deflate_text(const std::string& json, int level = 9) {
    uLongf size = compressBound(static_cast<uLong>(json.size()));
    std::string compressed(size, '\0');
    compress2(reinterpret_cast<Bytef*>(compressed.data()), &size, reinterpret_cast<const Bytef*>(json.data()),
              static_cast<uLong>(json.size()), level);
    compressed.resize(size);
    return backend::encoding::base64(compressed);
}

void test_round_trip() {
    std::printf("the documents of the backend writer\n");
    // A level 1 male with no clothing.
    backend::state::MenuDocument plain;
    plain.name = "Testwarden";
    plain.appearance = male_names();
    plain.worldId = "00000000-0000-4000-8000-00000000aaaa";
    plain.level = 1;
    menu::FDocument out;
    expect(menu::Decode(make(plain), out) == menu::Status::Ok, "a plain document reads");
    expect(out.PlayerName == "Testwarden" && out.CompanyName == "Testwarden's Company", "the player name and the company name");
    expect(out.EntityClassName == "PlayerMale" && !out.IsFemale(), "a male body: PlayerMale");
    expect(out.CurrentLevel == 0 && out.DisplayLevel() == 1, "the level 1: currentLevel 0, the screen level 1");
    expect(out.WorldId == plain.worldId, "the world id of the document");
    expect(out.Equipment.empty(), "no clothing: an empty map");
    expect(out.Backstory.empty(), "a seeded character has no backstory");
    for (std::size_t field = 0; field < menu::kAppearanceFieldCount; ++field) {
        expect(out.Appearance[field] == plain.appearance[field], std::string("the appearance name ") + std::string(menu::kAppearanceFields[field]));
    }

    // A level 60 female with the five armor slots. The appearance ids have zero bytes and high bytes (open point FE4).
    backend::state::MenuDocument dressed;
    dressed.name = "Zoë \"Quote\" \\Back";
    dressed.appearance = female_names();
    dressed.backstory = "Archetype_Ranger";
    dressed.worldId = "00000000-0000-4000-8000-00000000bbbb";
    dressed.level = 60;
    const std::uint32_t ids[5] = {0x00FF8000, 0x80000001, 0x0000000A, 0xFFFFFFFF, 0x7F0D0A22};
    for (std::uint32_t slot = 0; slot < 5; ++slot) {
        dressed.equipment.push_back({slot, ids[slot]});
    }
    // An entry above slot 4 (a weapon): the writer leaves it out, as the original server (no weapon in the menu).
    dressed.equipment.push_back({7, 0x12345678});
    const std::string text = make(dressed);
    expect(menu::Decode(text, out) == menu::Status::Ok, "a dressed document reads");
    expect(out.PlayerName == dressed.name, "a name with high bytes, a quote, and a backslash keeps its bytes");
    expect(out.EntityClassName == "PlayerFemale" && out.IsFemale(), "a female body: PlayerFemale");
    expect(out.DisplayLevel() == 60, "the level 60");
    expect(out.Backstory == "Archetype_Ranger", "the backstory");
    for (std::size_t field = 0; field < menu::kAppearanceFieldCount; ++field) {
        expect(out.Appearance[field] == dressed.appearance[field], std::string("the female appearance name ") + std::string(menu::kAppearanceFields[field]));
    }
    expect(out.Equipment.size() == 5, "five entries: no weapon entry");
    const char* keys[5] = {"hat", "shirt", "gloves", "pants", "boots"};
    for (std::size_t slot = 0; slot < out.Equipment.size() && slot < 5; ++slot) {
        const menu::FEquipment& entry = out.Equipment[slot];
        expect(entry.Slot == keys[slot], std::string("the key ") + keys[slot]);
        expect(entry.AppearanceId == ids[slot], std::string("the appearance id of ") + keys[slot] + " (zero and high bytes)");
        expect(entry.ItemName.empty() && entry.bHasDyes && entry.Dyes == std::array<std::uint8_t, 4>{0, 0, 0, 0},
               std::string("the empty item name and the dye 0 of ") + keys[slot]);
    }
    expect(out.VisualsBytes > 200, "the visualsBlob has its bytes");

    // The same document with real deflate blocks (the zlib of the game).
    const std::string json = inflate_text(text);
    expect(!json.empty(), "the backend text inflates");
    menu::FDocument deflated;
    expect(menu::Decode(deflate_text(json), deflated) == menu::Status::Ok && deflated.Equipment.size() == 5
               && deflated.PlayerName == dressed.name,
           "a deflate document reads the same");
    // A JSON document with spaces and an other member order, as a different JSON writer can make.
    const std::size_t blob = json.find("\"visualsBlob\":");
    const std::size_t blob_end = json.find(",\"position\"");
    if (blob != std::string::npos && blob_end != std::string::npos) {
        const std::string member = json.substr(blob, blob_end - blob);
        const std::string reordered = "{ \"worldId\" : \"w\\u00e9\" ,\n \"extra\": [1, {\"a\": null}, true],\n " + member
            + ", \"position\": {\"y\": 2.5, \"x\": -1, \"z\": 3e1}, \"currentLevel\": 41, \"currency\": 12.5 }";
        menu::FDocument other;
        expect(menu::Decode(deflate_text(reordered), other) == menu::Status::Ok, "an other member order with spaces reads");
        expect(other.WorldId == "w\xC3\xA9" && other.DisplayLevel() == 42 && other.Currency == 12.5, "the members of the other order");
        expect(other.Position[0] == -1.0 && other.Position[1] == 2.5 && other.Position[2] == 30.0, "the position x, y, z");
    }

    // The backend reader of the creation data reads the same text (the two readers agree on the appearance).
    const auto creation = backend::state::read_creation_data(text);
    expect(creation.has_value() && creation->appearance == dressed.appearance && creation->archetype == dressed.backstory,
           "the backend reader agrees");
}

void test_faults() {
    std::printf("the faults and the limits\n");
    menu::FDocument out;
    expect(menu::Decode("", out) == menu::Status::Empty, "no text");
    expect(menu::Decode(std::string(menu::kMaxTextBytes + 4, 'A'), out) == menu::Status::TextTooLarge, "a text over the limit");
    expect(menu::Decode("abc", out) == menu::Status::BadBase64, "a base64 text of a bad length");
    expect(menu::Decode("ab$=", out) == menu::Status::BadBase64, "a character out of the alphabet");
    expect(menu::Decode("a=bc", out) == menu::Status::BadBase64, "padding in the middle");
    expect(menu::Decode(backend::encoding::base64("not zlib at all"), out) == menu::Status::BadZlib, "a text that is not zlib");

    backend::state::MenuDocument values;
    values.name = "Cutwarden";
    values.appearance = male_names();
    values.worldId = "w";
    values.equipment.push_back({0, 0x01020304});
    const std::string json = inflate_text(make(values));
    // A cut zlib stream.
    {
        std::string compressed;
        const bool decoded = backend::encoding::base64_decode(deflate_text(json), compressed);
        expect(decoded, "the deflate text decodes");
        for (std::size_t keep : {std::size_t(0), std::size_t(2), compressed.size() / 2, compressed.size() - 1}) {
            const menu::Status status = menu::Decode(backend::encoding::base64(compressed.substr(0, keep)), out);
            expect(status == menu::Status::BadZlib || status == menu::Status::BadBase64 || status == menu::Status::Empty,
                   "a cut zlib stream of " + std::to_string(keep) + " bytes");
        }
    }
    // An inflated document over the limit (a zlib bomb: 300 KiB of spaces).
    expect(menu::Decode(deflate_text(std::string(300 * 1024, ' ')), out) == menu::Status::DocumentTooLarge,
           "an inflated document over the limit");
    expect(menu::Decode(deflate_text("{\"currency\":0"), out) == menu::Status::BadJson, "a cut JSON document");
    expect(menu::Decode(deflate_text("[1,2]"), out) == menu::Status::BadJson, "a JSON array, not an object");
    expect(menu::Decode(deflate_text("{\"currency\":0,\"worldId\":\"w\"}"), out) == menu::Status::NoVisuals, "no visualsBlob");
    expect(menu::Decode(deflate_text("{\"visualsBlob\":\"\\u0100\"}"), out) == menu::Status::BadJson,
           "an escape above 0xFF in the visualsBlob");
    expect(menu::Decode(deflate_text("{\"visualsBlob\":\"\"}"), out) == menu::Status::BadVisuals, "an empty visualsBlob");

    // Each cut of the object stream fails, and no cut reads out of the bytes.
    std::vector<std::uint8_t> visuals;
    bool has_visuals = false;
    menu::FDocument read;
    const std::vector<std::uint8_t> json_bytes(json.begin(), json.end());
    expect(menu::ReadJson(json_bytes, read, visuals, has_visuals) == menu::Status::Ok && has_visuals, "the visualsBlob of the cut test");
    expect(menu::ReadSaveData(visuals, read), "the whole stream reads");
    int cut_failures = 0;
    for (std::size_t keep = 0; keep < visuals.size(); ++keep) {
        menu::FDocument cut;
        if (!menu::ReadSaveData(std::span(visuals.data(), keep), cut)) {
            ++cut_failures;
        }
    }
    expect(cut_failures == static_cast<int>(visuals.size()), "each of the " + std::to_string(visuals.size()) + " cuts of the stream fails");
    // A stream with a byte after its end mark.
    std::vector<std::uint8_t> extra = visuals;
    extra.push_back(0x08);
    expect(!menu::ReadSaveData(extra, read), "a byte after the end of the stream");
    // A size field of four bytes with a large size.
    std::vector<std::uint8_t> large{0, 0, 0, 0, 3, 0x08 | 0x10 | 0x20 | 4};
    large.insert(large.end(), 16, 0x11);
    large.insert(large.end(), {0x7F, 0xFF, 0xFF, 0xFF});
    expect(!menu::ReadSaveData(large, read), "a value size over the limit");
    // Elements deeper than the limit.
    std::vector<std::uint8_t> deep{0, 0, 0, 0, 3};
    for (int depth = 0; depth < 40; ++depth) {
        deep.push_back(0x08);
        deep.insert(deep.end(), 16, 0x22);
    }
    deep.insert(deep.end(), 41, 0x00);
    expect(!menu::ReadSaveData(deep, read), "elements deeper than the limit");
    // An element with no mark.
    std::vector<std::uint8_t> unmarked{0, 0, 0, 0, 3, 0x01};
    expect(!menu::ReadSaveData(unmarked, read), "an element with no mark");
}

void test_keys() {
    std::printf("an entry with an other key and a nonempty item name\n");
    // The reader keeps an unknown key and a nonempty item name: the look decides (open point FE5). The stream is the backend
    // stream with the key "gloves" changed to "ring01" (the same length) and the empty item name kept.
    backend::state::MenuDocument values;
    values.name = "Keywarden";
    values.appearance = male_names();
    values.worldId = "w";
    values.equipment.push_back({2, 0xCAFEBABE});
    std::string json = inflate_text(make(values));
    const std::size_t key = json.find("gloves");
    expect(key != std::string::npos, "the key gloves is in the document");
    if (key != std::string::npos) {
        json.replace(key, 6, "ring01");
    }
    menu::FDocument out;
    expect(menu::Decode(deflate_text(json), out) == menu::Status::Ok && out.Equipment.size() == 1
               && out.Equipment[0].Slot == "ring01" && out.Equipment[0].AppearanceId == 0xCAFEBABE,
           "an other key stays in the result");
}

} // namespace

int main() {
    test_round_trip();
    test_faults();
    test_keys();
    std::printf("menu tests: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
