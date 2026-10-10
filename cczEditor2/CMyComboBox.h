#pragma once
#include <afxwin.h>
#include <vector>
#include "IndexedComboSearch.h"

class CMyComboBox;

class CMyComboSearchEdit :
    public CEdit
{
public:
    void SetOwner(CMyComboBox* owner) { owner_ = owner; }

protected:
    LRESULT WindowProc(UINT message, WPARAM wparam, LPARAM lparam) override;
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint point);
    DECLARE_MESSAGE_MAP()

private:
    CMyComboBox* owner_ = nullptr;
};

class CMyComboSearchList : public CWnd
{
public:
    void SetOwner(CMyComboBox* owner) { owner_ = owner; }
protected:
    LRESULT WindowProc(UINT message, WPARAM wparam, LPARAM lparam) override;
private:
    CMyComboBox* owner_ = nullptr;
};

class CMyComboBox :
    public CComboBox
{
public:
    enum class IndexedMode {
        None,
        Person,
        Item,
        ItemSegment,
        Instruction,
    };

    CMyComboBox() = default;

    // Enable the editable/searchable behavior after the dialog has populated
    // the combo.  Existing list indexes remain the values written by the
    // dialogs; filtering only changes the visible rows.
    void EnablePersonSearch();
    void RestoreIndexedOptions();
    bool RemoveFinalIndexedOption();
    void EnableInstructionSearch();
    void EnableItemSearch();
    void EnableItemSegmentSearch(int first_global_code);
    bool CommitIndexedInput();
    bool HasValidIndexedSelection() const;
    BOOL ForwardMouseWheel(UINT nFlags, short zDelta, CPoint point);
    void RestoreCapturedSearchCursor();

    int AddString(LPCTSTR lpszString);
    void ResetContent();
    int GetCurSel() const;
    int SetCurSel(int nSelect);
    virtual BOOL PreTranslateMessage(MSG* pMsg);

protected:
    LRESULT WindowProc(UINT message, WPARAM wparam, LPARAM lparam) override;

private:
    void ConfigureIndexedMode(IndexedMode mode, int first_global_code = 0);
    void RebuildVisibleOptions(const CString& query);
    void RestoreFullOptionList();
    void BringSearchMatchIntoView(const CString& query);
    void RestoreLastSelectionText();
    CString GetComboText() const;
    void CreateSearchEdit();
    void SetSearchEditText(const CString& text);
    int FindVisibleRowForSelection(int selection_value) const;
    int SelectFirstVisibleRow();
    void SetCanonicalSelection(int selection_value);
    bool CommitNativeDropDownSelection();
    void AppendTypeaheadChar(wchar_t value);
    void OnEditChange();
    BOOL OnSelectionChangeReflect();
    BOOL OnSelectionEndOkReflect();
    BOOL OnSelectionEndCancelReflect();
    void OnSelectionChange();
    void OnDropDown();
    void OnCloseUp();
    void OnSearchEditChange();
    void OnSearchEditSetFocus();
    void OnSearchEditKillFocus();
    void RestoreSearchEditFocus();
    void NotifySelectionChange();

    IndexedMode indexed_mode_ = IndexedMode::None;
    int first_global_code_ = 0;
    bool rebuilding_ = false;
    bool native_list_complete_ = false;
    bool indexed_selection_valid_ = false;
    bool dropdown_open_ = false;
    bool dropdown_selection_accepted_ = false;
    bool dropdown_cancelled_ = false;
    bool dropdown_open_selection_valid_ = false;
    int dropdown_open_native_row_ = -1;
    int dropdown_open_selection_ = -1;
    bool dropdown_highlight_valid_ = false;
    int dropdown_highlight_selection_ = -1;
    bool filter_active_ = false;
    bool auto_filter_dropdown_ = false;
    bool select_all_on_next_focus_ = true;
    std::wstring last_filter_query_;
    std::vector<size_t> filter_candidate_indexes_;
    int indexed_selection_ = -1;
    int last_notified_selection_ = -1;
    CString last_committed_text_;
    std::vector<ccz::editor::IndexedOption> indexed_options_;
    std::vector<size_t> visible_option_indexes_;
    CMyComboSearchEdit search_edit_;
    CMyComboSearchList search_list_;

    wchar_t inputs[10];
    int cnt = 0;
    int cur_selected = -1;

    DECLARE_MESSAGE_MAP()
};

class CMyEdit :
    public CEdit
{
public:
    virtual BOOL PreTranslateMessage(MSG* pMsg);
};

class CMyList :
    public CListBox
{
};
