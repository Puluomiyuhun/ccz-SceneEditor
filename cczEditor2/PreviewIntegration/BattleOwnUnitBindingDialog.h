#pragma once
#include <afxcmn.h>
#include "BattleOwnUnitBindings.h"
#include "../CMyComboBox.h"
class CBattleOwnUnitBindingDialog : public CDialog {
public:
    ccz::preview::BattlePreviewState baseline;
    ccz::editor_preview::BattleOwnUnitBindings bindings;
    std::vector<std::wstring> names;
    int initial_slot=-1;
    INT_PTR Show(CWnd* owner){struct Template{DLGTEMPLATE dlg;WORD menu,cls,title;};Template t{};t.dlg.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;t.dlg.cx=320;t.dlg.cy=initial_slot>=0?112:225;InitModalIndirect(&t.dlg,owner);return DoModal();}
protected:
    BOOL OnInitDialog() override {
        CDialog::OnInitDialog();SetWindowText(L"我军预览绑定");CRect r;GetClientRect(&r);
        font_.CreatePointFont(90,L"Microsoft YaHei");
        if(initial_slot>=0){
            CString title;title.Format(L"绑定我军编号 %d",initial_slot);SetWindowText(title);
            label_.Create(L"选择 DATA 武将（支持直输/检索）",WS_CHILD|WS_VISIBLE,CRect(12,10,r.right-12,34),this,305);label_.SetFont(&font_);
            person_.Create(WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,CRect(12,38,r.right-12,r.bottom+180),this,302);
            for(int i=0;i<1024;++i){CString name;name.Format(L"%d: %s",i,i<(int)names.size()?names[i].c_str():L"Data");person_.AddString(name);}
            person_.EnablePersonSearch();person_.SetFont(&font_);
            auto found=bindings.find(initial_slot);person_.SetCurSel(found!=bindings.end()?found->second:0);
            clear_.Create(L"取消绑定",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,CRect(12,r.bottom-39,110,r.bottom-12),this,304);
            ok_.Create(L"确定",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,CRect(r.right-204,r.bottom-39,r.right-112,r.bottom-12),this,IDOK);
            cancel_.Create(L"取消",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,CRect(r.right-104,r.bottom-39,r.right-12,r.bottom-12),this,IDCANCEL);
            for(CWnd* c:{(CWnd*)&clear_,(CWnd*)&ok_,(CWnd*)&cancel_})c->SetFont(&font_);
            return TRUE;
        }
        table_.Create(WS_CHILD|WS_VISIBLE|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,CRect(12,12,r.right-12,r.bottom-126),this,301);
        table_.SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);table_.InsertColumn(0,L"我军槽位",LVCFMT_LEFT,110);table_.InsertColumn(1,L"预览武将",LVCFMT_LEFT,r.Width()-142);table_.SetFont(&font_);
        person_.Create(WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,CRect(12,r.bottom-114,r.right-12,r.bottom+180),this,302);
        for(int i=0;i<1024;++i){CString name;name.Format(L"%d: %s",i,i<(int)names.size()?names[i].c_str():L"Data");person_.AddString(name);}person_.EnablePersonSearch();person_.SetFont(&font_);
        bind_.Create(L"绑定到选中槽位",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,CRect(12,r.bottom-80,166,r.bottom-53),this,303);
        clear_.Create(L"取消绑定",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,CRect(178,r.bottom-80,280,r.bottom-53),this,304);
        ok_.Create(L"应用并重新推演",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,CRect(r.right-172,r.bottom-39,r.right-12,r.bottom-12),this,IDOK);
        cancel_.Create(L"取消",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,CRect(12,r.bottom-39,100,r.bottom-12),this,IDCANCEL);
        for(CWnd* c:{(CWnd*)&bind_,(CWnd*)&clear_,(CWnd*)&ok_,(CWnd*)&cancel_})c->SetFont(&font_);
        person_.SetCurSel(0);Fill(initial_slot);return TRUE;
    }
    BOOL OnCommand(WPARAM w,LPARAM l) override {
        UINT id=LOWORD(w);
        if(initial_slot>=0&&(id==IDOK||id==304)){
            auto candidate=bindings;
            if(id==304)candidate.erase(initial_slot);
            else{if(!person_.CommitIndexedInput()||person_.GetCurSel()<0){AfxMessageBox(L"请选择有效武将");return TRUE;}candidate[initial_slot]=person_.GetCurSel();}
            auto projected=baseline;std::wstring diagnostic;
            if(!ccz::editor_preview::ApplyBattleOwnUnitBindings(projected,candidate,diagnostic)){AfxMessageBox(diagnostic.c_str());return TRUE;}
            bindings=std::move(candidate);EndDialog(IDOK);return TRUE;
        }
        if(id==303||id==304){int row=table_.GetNextItem(-1,LVNI_SELECTED);if(row<0){AfxMessageBox(L"请先选择我军槽位");return TRUE;}int slot=(int)table_.GetItemData(row);auto candidate=bindings;
            if(id==304)candidate.erase(slot);else {if(!person_.CommitIndexedInput()||person_.GetCurSel()<0){AfxMessageBox(L"请选择有效武将");return TRUE;}candidate[slot]=person_.GetCurSel();}
            auto projected=baseline;std::wstring diagnostic;if(id==303&&!ccz::editor_preview::ApplyBattleOwnUnitBindings(projected,candidate,diagnostic)){AfxMessageBox(diagnostic.c_str());return TRUE;}bindings=std::move(candidate);Fill(slot);return TRUE;
        }
        if(id==IDOK){auto projected=baseline;std::wstring diagnostic;if(!ccz::editor_preview::ApplyBattleOwnUnitBindings(projected,bindings,diagnostic)){AfxMessageBox(diagnostic.c_str());return TRUE;}EndDialog(IDOK);return TRUE;}
        return CDialog::OnCommand(w,l);
    }
private:
    void Fill(int selected) {
        table_.DeleteAllItems();std::set<int> slots;
        for(const auto& unit:baseline.units)
            if(ccz::preview::is_battle_deployment_slot_marker(unit)&&unit.fields_decoded&&!unit.editor_pending_delete)
                slots.insert(unit.deployment_index);
        for(const auto& binding:bindings)slots.insert(binding.first);
        int selection=-1;
        for(const int slotIndex:slots){
            CString slot;slot.Format(L"槽位 %d",slotIndex);
            const int row=table_.InsertItem(table_.GetItemCount(),slot);table_.SetItemData(row,slotIndex);
            const auto found=bindings.find(slotIndex);CString label=L"未绑定";
            if(found!=bindings.end())label.Format(L"Data %d · %s",found->second,
                found->second>=0&&found->second<(int)names.size()?names[found->second].c_str():L"");
            table_.SetItemText(row,1,label);if(slotIndex==selected)selection=row;
        }
        if(selection<0&&table_.GetItemCount())selection=0;
        if(selection>=0)table_.SetItemState(selection,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
    CStatic label_;CListCtrl table_;CMyComboBox person_;CButton bind_,clear_,ok_,cancel_;CFont font_;
};
