#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
namespace ccz::preview {
using LogicalNodeKey = std::wstring;
struct BattleCoord { int x{}, y{}; };
struct BattleMapInfo { int map_id{-1}, width{}, height{}; };
struct RawReplayNode {
 LogicalNodeKey logical_key;
 std::uint16_t instruction_id{};
 struct { std::vector<std::int32_t> int_data; } command;
 std::vector<RawReplayNode> children;
};
struct BattleParseOptions { LogicalNodeKey scope_root_key; };
struct BattleParseResult { bool ok{}; struct { BattleMapInfo map; } state; };
BattleParseResult parse_battle_instructions(const std::wstring&,
 const std::vector<RawReplayNode>& nodes, const BattleParseOptions&) {
 BattleParseResult result{true};
 for (const auto& node : nodes) if (node.instruction_id == 0x27 &&
     !node.command.int_data.empty()) result.state.map.map_id = node.command.int_data[0];
 return result;
}
}
namespace ccz::editor_preview {
struct BattlePreviewCapture {
 struct { ccz::preview::LogicalNodeKey scope_key;
  std::vector<ccz::preview::RawReplayNode> nodes;
  std::wstring document_path; } input;
};
struct BattleObstacleDefaults {};
struct BattleObstacleDraft { int type{}; ccz::preview::BattleCoord coordinate;
 BattleObstacleDefaults parameters; };
struct BattleMapOperationInstruction {};
struct BattleObstacleDraftQueue {
 std::vector<BattleObstacleDraft> values;
 const std::vector<BattleObstacleDraft>& items() const { return values; }
};
bool BuildBattleObstacleInstruction(int type, const ccz::preview::BattleCoord& cell,
 int width, int height, BattleMapOperationInstruction&, std::wstring& diagnostic,
 const BattleObstacleDefaults&) {
 if (type < 0 || type > 2 || cell.x < 0 || cell.y < 0 ||
     cell.x >= width || cell.y >= height) { diagnostic = L"invalid cell"; return false; }
 return true;
}
}
bool in_scope(const std::wstring& key, const std::wstring& scope) {
 return key.compare(0, scope.size(), scope) == 0;
}
// PRODUCTION_OBSTACLE_REFRESH
int main() {
 try {
  using namespace ccz;
  editor_preview::BattlePreviewCapture old, fresh;
  old.input.scope_key = fresh.input.scope_key = L"scene1/section1";
  old.input.nodes.push_back({L"scene1/section1/map", 0x27, {{0}}, {}});
  fresh.input.nodes = old.input.nodes;
  editor_preview::BattleObstacleDraftQueue drafts;
  drafts.values.push_back({0, {7, 8}, {}});
  preview::BattleMapInfo map{0, 19, 18};
  std::wstring diagnostic;
  // A routine edit elsewhere in Scene1 must preserve the staged obstacle.
  fresh.input.nodes.push_back({L"scene1/section1/unit", 0x4b, {{1}}, {}});
  CHECK(can_rebase_obstacle_drafts(old, fresh, map, drafts, diagnostic));
  fresh.input.nodes.push_back({L"scene1/section1/other_obstacle", 0x58,
      {{0, 0, 26, 1, 2, 0, 0}}, {}});
  CHECK(can_rebase_obstacle_drafts(old, fresh, map, drafts, diagnostic));
  fresh.input.nodes.push_back({L"scene1/section1/draft_cell_obstacle", 0x58,
      {{0, 0, 26, 7, 8, 0, 0}}, {}});
  CHECK(!can_rebase_obstacle_drafts(old, fresh, map, drafts, diagnostic));
  fresh.input.nodes.pop_back();
  fresh.input.nodes[0].command.int_data[0] = 1;
  CHECK(!can_rebase_obstacle_drafts(old, fresh, map, drafts, diagnostic));
  fresh.input.nodes[0].command.int_data[0] = 0;
  drafts.values[0].coordinate = {20, 8};
  CHECK(!can_rebase_obstacle_drafts(old, fresh, map, drafts, diagnostic));
  drafts.values[0].coordinate = {7, 8};
  fresh.input.scope_key = L"scene1/section2";
  CHECK(!can_rebase_obstacle_drafts(old, fresh, map, drafts, diagnostic));
  std::cout << "BATTLE_OBSTACLE_REFRESH_PASS\n";
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
