#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ccz::editor_preview {
// Value identities are document-local source paths, never TreeView pointers.
using BattleSectionPath = std::vector<std::uint32_t>;
struct BattleSectionRelation {
    BattleSectionPath predecessor; BattleSectionPath current;
    std::uintptr_t predecessor_handle{}; std::uintptr_t current_handle{};
};
class BattleSectionRelations {
public:
    const std::vector<BattleSectionRelation>& All(const std::wstring& document) const {
        const auto it = documents_.find(document);
        return it == documents_.end() ? empty_ : it->second;
    }
    bool UsesPredecessors(const std::wstring& document) const {
        const auto it = enabled_.find(document);
        return it == enabled_.end() || it->second;
    }
    void UsePredecessors(const std::wstring& document, bool enabled) { enabled_[document] = enabled; }
    void Forget(const std::wstring& document) { documents_.erase(document); enabled_.erase(document); }
    std::uintptr_t ExpectedHandle(const std::wstring& document, const BattleSectionPath& path) const {
        for (const auto& row : All(document)) {
            if (row.current == path) return row.current_handle;
            if (row.predecessor == path) return row.predecessor_handle;
        }
        return 0;
    }
    std::vector<BattleSectionPath> Chain(const std::wstring& document, BattleSectionPath current) const {
        std::vector<BattleSectionPath> result;
        const auto& rows = All(document);
        for (std::size_t step = 0; step < rows.size(); ++step) {
            const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.current == current; });
            if (it == rows.end()) break;
            result.push_back(it->predecessor);
            current = it->predecessor;
        }
        std::reverse(result.begin(), result.end());
        return result;
    }
    bool Add(const std::wstring& document, const BattleSectionPath& predecessor,
        const BattleSectionPath& current, std::wstring& diagnostic,
        std::uintptr_t predecessor_handle = 0, std::uintptr_t current_handle = 0) {
        diagnostic.clear();
        if (document.empty() || predecessor.empty() || current.empty() || predecessor == current) {
            diagnostic = L"不能将 Section 自身设为前序"; return false;
        }
        const auto& rows = All(document);
        if (std::any_of(rows.begin(), rows.end(), [&](const auto& r) { return r.current == current; })) {
            diagnostic = L"当前 Section 已有直接前序，请先移除旧关系"; return false;
        }
        const auto ancestors = Chain(document, predecessor);
        if (std::find(ancestors.begin(), ancestors.end(), current) != ancestors.end()) {
            diagnostic = L"该关系会形成 Section 循环"; return false;
        }
        documents_[document].push_back({predecessor, current, predecessor_handle, current_handle}); return true;
    }
    bool Remove(const std::wstring& document, const BattleSectionPath& current) {
        auto it = documents_.find(document);
        if (it == documents_.end()) return false;
        auto& rows = it->second;
        const auto old = rows.size();
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto& r) { return r.current == current; }), rows.end());
        return rows.size() != old;
    }
private:
    std::map<std::wstring, std::vector<BattleSectionRelation>> documents_;
    std::map<std::wstring, bool> enabled_;
    const std::vector<BattleSectionRelation> empty_;
};
}
