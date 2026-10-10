#pragma once
#include "BattleSectionRelations.h"
#include "EditorTreeSnapshot.h"
#include <functional>

namespace ccz::editor_preview {
struct BattleSectionChoice {
    BattleSectionPath path;
    std::uintptr_t handle{};
    std::wstring name;
};
inline std::vector<BattleSectionChoice> BattleSectionChoices(const CapturedEditorTree& snapshot) {
    std::vector<BattleSectionChoice> result;
    if (snapshot.document_roots.empty()) return result;
    std::function<void(const CapturedEditorNode&, BattleSectionPath)> visit =
        [&](const auto& node, auto path) {
            if (node.instruction_id == -2) {
                std::wstring name;
                for(const auto& child:node.children)if(child.instruction_id==2){name=child.long_text.empty()?child.display_text:child.long_text;const auto at=name.find(L"内部信息");if(at!=std::wstring::npos)name=name.substr(at+4);while(!name.empty()&&(name.front()==L' '||name.front()==L':'||name.front()==L'：'))name.erase(name.begin());break;}
                if(name.empty())name=L"未命名";
                result.push_back({path,node.runtime_handle,name});
            }
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                auto childPath = path; childPath.push_back(static_cast<std::uint32_t>(i));
                visit(node.children[i], std::move(childPath));
            }
        };
    visit(snapshot.document_roots.front(), {});
    return result;
}
inline CString BattleSectionDescription(const BattleSectionChoice& section) {
    CString label;
    if(section.path.size()>=2)label.Format(L"%s（Scene %u / Section %u）",section.name.c_str(),section.path[0]+1,section.path[1]+1);
    else label=section.name.c_str();
    return label;
}
// Resource-free modal table: selection, relation overview, remove and locate.
class BattleSectionRelationDialog : public CDialog {
public:
    struct Row { CString first; CString second; BattleSectionPath current; std::uintptr_t handle{}; };
    std::vector<Row> rows;
    bool overview{};
    int selected{-1};
    std::function<void(const Row&)> remove;
    std::function<void(const Row&)> locate;
    INT_PTR Show(CWnd* owner) {
        struct Template { DLGTEMPLATE dlg; WORD menu; WORD cls; WORD title; };
        Template layout{};
        layout.dlg.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
        layout.dlg.cx = 440; layout.dlg.cy = 235;
        InitModalIndirect(&layout.dlg, owner);
        return DoModal();
    }
protected:
    BOOL OnInitDialog() override {
        CDialog::OnInitDialog();
        SetWindowText(overview ? L"动态预览前置关系总览" : L"选择当前 Section 的前置");
        font_.CreatePointFont(90,L"Microsoft YaHei");
        SetFont(&font_);
        CRect client; GetClientRect(&client);
        table_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            CRect(10, 10, client.right - 10, client.bottom - 48), this, 301);
        table_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        const int width = client.Width() - 24;
        table_.InsertColumn(0, overview ? L"前置 Section" : L"Section 名称及内部信息", LVCFMT_LEFT, overview ? width / 2 : width);
        if (overview) table_.InsertColumn(1, L"当前 Section", LVCFMT_LEFT, width / 2);
        Fill();
        const int y = client.bottom - 36;
        if (overview) {
            remove_.Create(L"移除关系", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10,y,110,y+26),this,302);
            locate_.Create(L"定位当前 Section", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(120,y,260,y+26),this,303);
        }
        ok_.Create(overview ? L"关闭" : L"设为前置",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,CRect(client.right-110,y,client.right-10,y+26),this,IDOK);
        table_.SetFont(&font_);ok_.SetFont(&font_);
        if(overview){remove_.SetFont(&font_);locate_.SetFont(&font_);}
        return TRUE;
    }
    BOOL OnCommand(WPARAM w, LPARAM l) override {
        const UINT id = LOWORD(w);
        const int index = table_.GetNextItem(-1, LVNI_SELECTED);
        if (id == 302 && index >= 0 && remove) { remove(rows[index]); rows.erase(rows.begin()+index); Fill(); return TRUE; }
        if (id == 303 && index >= 0 && locate) { locate(rows[index]); return TRUE; }
        if (id == IDOK) { if (!overview && index < 0) return TRUE; selected=index; EndDialog(IDOK); return TRUE; }
        return CDialog::OnCommand(w,l);
    }
private:
    void Fill() { table_.DeleteAllItems(); for (std::size_t i=0;i<rows.size();++i) { table_.InsertItem(static_cast<int>(i),rows[i].first); if(overview)table_.SetItemText(static_cast<int>(i),1,rows[i].second); } }
    CFont font_;
    CListCtrl table_;
    CButton remove_, locate_, ok_;
};
}
