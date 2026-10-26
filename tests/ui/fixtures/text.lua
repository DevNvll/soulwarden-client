-- The text fixture. The original InfoModal screen (infomodal.luac, modalrow_text.luac) shows one page of text rows
-- through its own scripts: each row sets a localized text, and the modal places the rows by their text heights. The
-- texts (keys only here) have markup: a <font> color and face, an inline image, a link with an escaped line end, and
-- the gesture tag of the fork; one long text wraps. Then the overflow modes and shrink to fit on one row, through the
-- text bus. Pictures: text-modal, text-overflow.
local canvas = screen("LyShineUI/InfoModal/InfoModal.uicanvas", 60)
local root = swcui.canvas_get(canvas, "root")
local rows = find(root, "RowContainer")
check(rows ~= nil, "the modal has its row container")

local keys = {
  "@01_lore_manifest_03",                  -- a long plain text: wrap
  "@ui_buy_order_fulfilled_desc",          -- <font color><font face>: pieces in other fonts and colors
  "@13_summer_02_maudlinbugs_description", -- <img src xPadding scale yOffset>: an inline image
  "@mm_loginservices_bancodeofconduct",    -- an escaped line end and <a data> in a <font color>
}
local pageRows = {}
for i, key in ipairs(keys) do
  pageRows[i] = {type = "Text", text = key}
end
DynamicBus.InfoModal.Broadcast.ShowModal({pagesData = {{title = "@ui_ok", rows = pageRows}}})
frames(0.5)
check(DynamicBus.InfoModal.Broadcast.GetIsModalShowing() == true, "the modal shows")
equal(LyShineManagerBus.Broadcast.GetCurrentState(), 3082229064, "the state of the modal (CRC-32 of infomodal)")
check(swcui.canvas_get(canvas, "enabled"), "the screen is on in its state")

-- The rows in their order on the page (the script puts them one under the other).
local shown = {}
for _, child in ipairs(swcui.children(rows)) do
  if swcui.enabled(child) and swcui.has(child, "text") then
    shown[#shown + 1] = child
  end
end
local function top_of(id)
  return select(2, swcui.rect(id))
end
table.sort(shown, function(a, b) return top_of(a) < top_of(b) end)
equal(#shown, #keys, "text rows on the page")

local layouts = {}
for i, id in ipairs(shown) do
  layouts[i] = swcui.text_layout(id)
end
local function pieces(layout)
  local out = {}
  for _, line in ipairs(layout.lines) do
    for _, piece in ipairs(line.pieces) do
      out[#out + 1] = piece
    end
  end
  return out
end

-- Wrap: the long text has several lines, each one in the width of its row.
if layouts[1] then
  check(#layouts[1].lines > 2, "the long text wraps (" .. #layouts[1].lines .. " lines)")
  for _, line in ipairs(layouts[1].lines) do
    check(line.width <= swcui.local_size(shown[1]) + 0.5, "a wrapped line fits in the row")
  end
end

-- <font color face>: a piece with the color #edc63e in another font than the row.
if layouts[2] then
  local colored, otherFont = false, false
  local first = pieces(layouts[2])[1]
  for _, piece in ipairs(pieces(layouts[2])) do
    if piece.color and math.abs(piece.color.r - 0xed / 255) < 0.01 and math.abs(piece.color.b - 0x3e / 255) < 0.01 then
      colored = true
      otherFont = otherFont or (first and piece.font ~= first.font)
    end
  end
  check(colored, "a <font color> piece has its color")
  check(otherFont, "a <font face> piece has its font")
end

-- <img>: an image piece with the texture of the tag (".dds" added), in a line with text.
if layouts[3] then
  local image
  for _, piece in ipairs(pieces(layouts[3])) do
    if piece.image ~= "" then
      image = piece
    end
  end
  check(image ~= nil, "the text has an inline image piece")
  if image then
    equal(image.image, "lyshineui/images/map/icon/pois/maudlinbug.dds", "the texture of the inline image")
    check(image.width > 0, "the inline image has a width")
  end
end

-- An escaped line end makes a new line; <a data> makes a link piece.
if layouts[4] then
  check(#layouts[4].lines >= 2, "the escaped line end makes a new line")
  local link
  for _, piece in ipairs(pieces(layouts[4])) do
    if piece.link then
      link = piece
    end
  end
  check(link ~= nil and link.data == "coc", "the <a data> piece is a link with its data")
end

for i, id in ipairs(shown) do
  local l, t, r, b = swcui.rect(id)
  note(string.format("row %d: rect %.1f %.1f %.1f %.1f, text height %.1f, local y %.1f", i, l, t, r, b, layouts[i].height,
    select(2, swcui.local_position(id))))
end
-- The rows do not overlap: each one starts under the text of the one before.
for i = 2, #shown do
  check(top_of(shown[i]) >= top_of(shown[i - 1]) + layouts[i - 1].height - 0.5, "row " .. i .. " is under the text of row " .. (i - 1))
end
picture("text-modal")

-- The fork gesture tag gives no text; markup that is not valid shows raw.
local gesture = shown[1]
UiTextBus.Event.SetTextWithFlags(gesture, "@01_aelstrom_default_00", eUiTextSet_SetLocalized)
check(not string.find(swcui.text_layout(gesture).lines[1].text, "<g", 1, true), "the gesture tag gives no text")
UiTextBus.Event.SetText(gesture, "a <unknown>tag</unknown> stays raw")
check(string.find(swcui.text_layout(gesture).lines[1].text, "<unknown>", 1, true) ~= nil, "a text that is not valid markup shows raw")

-- The overflow modes on one row with no wrap: ellipsis cuts the line and ends it with "..."; shrink to fit makes the
-- font smaller; clip keeps the line (the draw clips it at the rect).
local row = shown[2]
UiTextBus.Event.SetTextWithFlags(row, "@01_lore_manifest_03", eUiTextSet_SetLocalized)
UiTextBus.Event.SetWrapText(row, eUiTextWrapTextSetting_NoWrap)
UiTextBus.Event.SetOverflowMode(row, eUiTextOverflowMode_Ellipsis)
local width = swcui.local_size(row)
local cut = swcui.text_layout(row)
check(cut.lines[1].width <= width + 0.5, "the ellipsis line fits in the row")
check(string.sub(cut.lines[1].text, -3) == "...", "the ellipsis line ends with ...")
UiTextBus.Event.SetOverflowMode(row, eUiTextOverflowMode_OverflowText)
UiTextBus.Event.SetShrinkToFit(row, eUiTextShrinkToFit_Uniform)
UiTextBus.Event.SetMinimumShrinkScale(row, 0.2)
local shrunk = swcui.text_layout(row)
check(shrunk.fontSize < UiTextBus.Event.GetFontSize(row), "shrink to fit makes the font smaller")
UiTextBus.Event.SetShrinkToFit(row, eUiTextShrinkToFit_None)
UiTextBus.Event.SetOverflowMode(row, eUiTextOverflowMode_ClipText)
check(swcui.text_layout(row).lines[1].width > width, "clip keeps the whole line (the draw clips it)")
frames(0.1)
picture("text-overflow")

-- A label with more text after it (the localization of the engine replaces the @ label and keeps the rest; the user test of
-- 2026-10-26: the BIO tab showed "@ui_world_select_world: Soulwarden").
equal(swcui.localize("@ui_world_select_world:"), "World:", "a label and a colon: the label is localized")
equal(swcui.localize("@ui_world_select_world"), "World", "a whole label")
equal(swcui.localize("@ui_ok 1"), swcui.localize("@ui_ok") .. " 1", "a label and a space: the label is localized, the rest stays")
equal(swcui.localize("@no_such_label_xyz:"), "@no_such_label_xyz:", "an unknown label stays as it is")
