-- The quest persons fixture (stage Q4, Linear AST-239; docs/PERSONS.md "The conversation state"; backend persons.md, stage 1.8f): the
-- state selection of swc/conversation.lua with the validity of a record (IsInitialState XOR a requirement) and the offers of a state
-- (gamedata/conversations.json "objectives"). Marcel Dupont: with no bit the initial state 06_00; with S_introtosettlement the state
-- 06_01 (the offer 06A_MSQ_01_Quartermaster), not 06_01b (no requirement and not initial: never selected). Aelstrom of the beach: with no
-- bit 01_Aelstrom_Default; with S_getoffbeach_ww 01_AelstromWW_p1_01 (the offer 06_skinningknife).
-- Needs gamedata/conversations.json with "initial" and "objectives" (tools/convert-persons.sh conversations, AST-239).
local HUD = 2702338936
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(HUD)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(20, 0)
frames(0.3)

local C = Swc.conversation
local function bits(...)
  local out = {}
  for _, id in ipairs({...}) do
    local index = Swc.AchievementIndex(id)
    check(index ~= nil, "the achievement " .. id .. " has an index")
    out[#out + 1] = tostring(index or "")
  end
  Swc.HostOwnership(table.concat(out, ","), "", true)
end
local function select(npc)
  C.stateId = nil
  C.select_state(npc)
  return C.stateId
end

check(C.record_valid ~= nil, "conversation.lua has the validity rule of a state record")
local data = swcui.load_json("gamedata/conversations.json")
local p1 = data and data.states["01_AelstromWW_p1_01"]
check(p1 ~= nil and p1.initial == false and p1.objectives and p1.objectives[1] == "06_skinningknife",
  "conversations.json has initial and objectives (01_AelstromWW_p1_01 offers 06_skinningknife)")

-- Marcel Dupont.
bits()
equal(select("06_MarcelDupont01"), "06_00_MarcelDupont", "Marcel Dupont with no bit: the initial state 06_00")
bits("S_introtosettlement")
equal(select("06_MarcelDupont01"), "06_01_MarcelDupont", "with S_introtosettlement: 06_01, not 06_01b (no requirement, not initial)")
check(data and not C.record_valid(data.states["06_01b_MarcelDupont"]), "06_01b_MarcelDupont is not a valid record")
check(data and C.record_valid(data.states["06_00_MarcelDupont"]), "06_00_MarcelDupont (initial, no requirement) is valid")

-- Aelstrom.
bits()
equal(select("01_AelstromWW_01"), "01_Aelstrom_Default", "Aelstrom with no bit: 01_Aelstrom_Default")
bits("S_getoffbeach_ww")
equal(select("01_AelstromWW_01"), "01_AelstromWW_p1_01", "Aelstrom with S_getoffbeach_ww: 01_AelstromWW_p1_01 (the knife offer)")
