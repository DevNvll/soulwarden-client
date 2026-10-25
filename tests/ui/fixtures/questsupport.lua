-- The quest support fixture (stage Q9, Linear AST-244): the availability rules of the client (swc/conversation.lua unavailable) along the
-- new-character history of the backend support manifest (stage 1.8e/1.8f: the Soldier plays 15 rows in this order; Soulwarden
-- docs/systems/quests.md "The support manifest"). The achievements grow as the history plays: the archetype bit of the Soldier
-- (Archetype_Mercenary), then for each earlier row its completion achievement and the achievements of its unlock tasks
-- (taskunlockachievement ItemName). Each row of the history must be offered when its turn comes, and the boarfeast variants of the other
-- archetypes must not be offered. A row whose offer needs a bit that only a lock task, a scene, or a later round sets can fail here; the
-- note names its conditional.
Swc.PlayerSpawned()
LyShineManagerBus.Broadcast.SetState(2702338936)
DynamicBus.UiLoader.Broadcast.OnUiLoadingComplete()
Swc.HostProgression(70, 0)
frames(0.3)

local C = Swc.conversation
local D = Swc.quests.D()
local HISTORY = {"06_getoffbeach", "06_skinningknife", "9932_0104_RelicReclamation", "FactionIntro_Marauders_01", "FactionIntro_Marauders_02",
                 "FactionIntro_Marauders_Vector", "06_boarfeast_soldier", "06_crewmates_soldier", "06_survivors_soldier", "06_pillageship_soldier",
                 "06_recoveritems_soldier", "06_demonstorm", "06_demonstorm2_soldier", "06_raisealarm", "0625_0101_truffles"}
local known = {}
local function set_bits()
  local out = {}
  for id in pairs(known) do
    local index = Swc.AchievementIndex(id)
    if index then out[#out + 1] = tostring(index) end
  end
  table.sort(out)
  Swc.HostOwnership(table.concat(out, ","), "", true)
end
local function row_of(id)
  return D.objectives[string.lower(id)]
end
local function unlocks(row)
  local out = {}
  for _, entry in ipairs(row.tree or {}) do
    local task = D.tasks[entry[2]]
    if task and task.type == "taskunlockachievement" and task.cells and task.cells.ItemName then
      out[#out + 1] = task.cells.ItemName
    end
  end
  return out
end

known["Archetype_Mercenary"] = true
local offered, failed = 0, {}
for _, id in ipairs(HISTORY) do
  local row = row_of(id)
  check(row ~= nil, id .. ": a row of quests.json")
  if row then
    set_bits()
    local reason = C.unavailable(row)
    if reason == nil then
      offered = offered + 1
    else
      failed[#failed + 1] = id
      note(string.format("%s: not offered (%s); RequiredAchievementId %s", id, reason, tostring(row.cells.RequiredAchievementId)))
    end
    -- The history plays the row: its completion achievement and its unlocks.
    if row.cells.AchievementId and row.cells.AchievementId ~= "" then known[row.cells.AchievementId] = true end
    for _, achievement in ipairs(unlocks(row)) do known[achievement] = true end
    set_bits()
    equal(C.unavailable(row), "completedBefore", id .. ": after its hand-in, no second offer (completedBefore)")
  end
end
note(string.format("the Soldier history: %d of %d rows offered at their turn", offered, #HISTORY))
equal(#failed, 0, "each row of the history is offered at its turn (" .. table.concat(failed, ", ") .. ")")

-- The boarfeast variants of the other archetypes stay hidden for the Soldier.
for _, other in ipairs({"06_boarfeast", "06_boarfeast_destroyer", "06_boarfeast_musketeer", "06_boarfeast_mystic", "06_boarfeast_occultist",
                        "06_boarfeast_ranger", "06_boarfeast_swordsman"}) do
  local row = row_of(other)
  check(row ~= nil and C.unavailable(row) ~= nil, other .. ": not offered to the Soldier")
end
