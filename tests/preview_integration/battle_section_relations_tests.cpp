#include "../../cczPreviewCoreTests/tests/test_framework.h"
#include "../../cczEditor2/PreviewIntegration/BattleSectionRelations.h"
#include <cassert>
#include <iostream>
CCZ_TEST(battle_section_relations_tests_regression) {
    using namespace ccz::editor_preview;
    BattleSectionRelations model;
    const BattleSectionPath a{0,0}, b{1,0}, c{2,0};
    std::wstring diagnostic;
    CCZ_REQUIRE(model.Add(L"one",a,b,diagnostic));
    CCZ_REQUIRE(model.Add(L"one",b,c,diagnostic));
    CCZ_REQUIRE((model.Chain(L"one",c)==std::vector<BattleSectionPath>{a,b}));
    CCZ_REQUIRE(!model.Add(L"one",c,a,diagnostic)); // Three-node cycle.
    CCZ_REQUIRE(!model.Add(L"one",a,a,diagnostic));
    CCZ_REQUIRE(!model.Add(L"one",a,b,diagnostic));
    CCZ_REQUIRE(!model.Add(L"one",a,c,diagnostic)); // One direct predecessor.
    CCZ_REQUIRE(model.All(L"two").empty());
    model.UsePredecessors(L"one",false);
    CCZ_REQUIRE(!model.UsesPredecessors(L"one"));
    CCZ_REQUIRE(model.UsesPredecessors(L"two"));
    CCZ_REQUIRE(model.Remove(L"one",b));
    CCZ_REQUIRE((model.Chain(L"one",c)==std::vector<BattleSectionPath>{b}));
    CCZ_REQUIRE(model.Add(L"two",c,a,diagnostic));
    CCZ_REQUIRE(model.All(L"one").size()==1);
    CCZ_REQUIRE(model.All(L"two").size()==1);
    std::cout << "BATTLE_SECTION_RELATIONS_PASS\n";
}
