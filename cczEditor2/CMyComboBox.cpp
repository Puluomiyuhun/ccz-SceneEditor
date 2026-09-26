#include "pch.h"
#include "CMyComboBox.h"

namespace {

constexpr UINT kIndexedSearchEditId = 0x7F42;

std::wstring CStringToWString(const CString& value)
{
    return std::wstring(value.GetString());
}

bool IsNoItemLabel(const std::wstring& label)
{
    return label == L"无";
}

} // namespace

BEGIN_MESSAGE_MAP(CMyComboSearchEdit, CEdit)
    ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

BOOL CMyComboSearchEdit::OnMouseWheel(UINT nFlags, short zDelta, CPoint point)
{
    return owner_ ? owner_->ForwardMouseWheel(nFlags, zDelta, point) : FALSE;
}

BEGIN_MESSAGE_MAP(CMyComboBox, CComboBox)
    ON_CONTROL_REFLECT(CBN_EDITCHANGE, OnEditChange)
    ON_CONTROL_REFLECT_EX(CBN_SELCHANGE, OnSelectionChangeReflect)
    ON_CONTROL_REFLECT_EX(CBN_SELENDOK, OnSelectionEndOkReflect)
    ON_CONTROL_REFLECT_EX(CBN_SELENDCANCEL, OnSelectionEndCancelReflect)
    ON_CONTROL_REFLECT(CBN_DROPDOWN, OnDropDown)
    ON_CONTROL_REFLECT(CBN_CLOSEUP, OnCloseUp)
    ON_CONTROL(EN_CHANGE, kIndexedSearchEditId, OnSearchEditChange)
    ON_CONTROL(EN_SETFOCUS, kIndexedSearchEditId, OnSearchEditSetFocus)
    ON_CONTROL(EN_KILLFOCUS, kIndexedSearchEditId, OnSearchEditKillFocus)
END_MESSAGE_MAP()

int CMyComboBox::AddString(LPCTSTR lpszString)
{
    const int result = CComboBox::AddString(lpszString);
    if (result < 0) return result;

    ccz::editor::IndexedOption option;
    option.label = lpszString ? lpszString : L"";
    option.selection_value = result;
    if (result >= static_cast<int>(indexed_options_.size())) {
        indexed_options_.push_back(option);
    } else {
        indexed_options_.insert(indexed_options_.begin() + result, option);
        for (size_t i = static_cast<size_t>(result + 1); i < indexed_options_.size(); ++i) {
            // The visible list is rebuilt from the model, so only the stable
            // value needs to follow an insertion into a non-sorted combo.
            indexed_options_[i].selection_value = static_cast<int>(i);
        }
    }
    return result;
}

void CMyComboBox::ResetContent()
{
    CComboBox::ResetContent();
    indexed_options_.clear();
    visible_option_indexes_.clear();
    native_list_complete_ = false;
    indexed_selection_ = -1;
    last_notified_selection_ = -1;
    indexed_selection_valid_ = false;
    dropdown_open_ = false;
    dropdown_selection_accepted_ = false;
    dropdown_cancelled_ = false;
    dropdown_open_selection_valid_ = false;
    dropdown_open_native_row_ = -1;
    dropdown_open_selection_ = -1;
    dropdown_highlight_valid_ = false;
    dropdown_highlight_selection_ = -1;
    filter_active_ = false;
    auto_filter_dropdown_ = false;
    select_all_on_next_focus_ = true;
    last_filter_query_.clear();
    filter_candidate_indexes_.clear();
    last_committed_text_.Empty();
}

int CMyComboBox::GetCurSel() const
{
    if (indexed_mode_ == IndexedMode::None) {
        return CComboBox::GetCurSel();
    }
    if (indexed_selection_valid_) return indexed_selection_;

    const int visible_row = CComboBox::GetCurSel();
    if (visible_row >= 0 && visible_row < static_cast<int>(visible_option_indexes_.size())) {
        return indexed_options_[visible_option_indexes_[visible_row]].selection_value;
    }
    return CComboBox::GetCurSel();
}

int CMyComboBox::SetCurSel(int nSelect)
{
    if (indexed_mode_ == IndexedMode::None) {
        return CComboBox::SetCurSel(nSelect);
    }

    int visible_row = FindVisibleRowForSelection(nSelect);
    if (visible_row < 0) {
        // A caller may set a value while a previous search is still active.
        // Restore the complete model before applying the programmatic value.
        RestoreFullOptionList();
        visible_row = FindVisibleRowForSelection(nSelect);
    }

    if (visible_row < 0) {
        indexed_selection_valid_ = false;
        return CB_ERR;
    }

    const int result = CComboBox::SetCurSel(visible_row);
    if (result != CB_ERR) {
        indexed_selection_ = nSelect;
        indexed_selection_valid_ = true;
        last_notified_selection_ = nSelect;
        for (const auto& option : indexed_options_) {
            if (option.selection_value == nSelect) {
                last_committed_text_ = option.label.c_str();
                SetSearchEditText(last_committed_text_);
                break;
            }
        }
    }
    return result;
}

void CMyComboBox::EnablePersonSearch()
{
    ConfigureIndexedMode(IndexedMode::Person);
}

void CMyComboBox::EnableItemSearch()
{
    ConfigureIndexedMode(IndexedMode::Item);
}

void CMyComboBox::EnableItemSegmentSearch(int first_global_code)
{
    ConfigureIndexedMode(IndexedMode::ItemSegment, first_global_code);
}

void CMyComboBox::ConfigureIndexedMode(IndexedMode mode, int first_global_code)
{
    indexed_mode_ = mode;
    first_global_code_ = first_global_code;
    native_list_complete_ = true;
    indexed_selection_valid_ = false;
    indexed_selection_ = -1;
    last_notified_selection_ = -1;
    dropdown_open_ = false;
    dropdown_selection_accepted_ = false;
    dropdown_cancelled_ = false;
    dropdown_open_selection_valid_ = false;
    dropdown_open_native_row_ = -1;
    dropdown_open_selection_ = -1;
    dropdown_highlight_valid_ = false;
    dropdown_highlight_selection_ = -1;
    filter_active_ = false;
    auto_filter_dropdown_ = false;
    select_all_on_next_focus_ = true;
    last_filter_query_.clear();
    filter_candidate_indexes_.clear();
    visible_option_indexes_.clear();

    for (size_t i = 0; i < indexed_options_.size(); ++i) {
        auto& option = indexed_options_[i];
        option.numeric_code = -1;
        option.numeric_searchable = true;
        option.numeric_resolvable = true;

        int code = -1;
        if (mode == IndexedMode::Person) {
            if (ccz::editor::ExtractLeadingDecimalCode(option.label, code) && code >= 0 && code <= 1023) {
                option.numeric_code = code;
            }
        } else if (mode == IndexedMode::Item) {
            if (ccz::editor::ExtractLeadingDecimalCode(option.label, code)) {
                option.numeric_code = code;
            } else if (IsNoItemLabel(option.label)) {
                // item[255]/item[511] are represented by the text “无”.
                // Keep the stable value for list selection, but do not expose
                // the reserved value through typed numeric input.
                option.numeric_code = option.selection_value;
                option.numeric_searchable = false;
                option.numeric_resolvable = false;
            }
        } else if (mode == IndexedMode::ItemSegment && option.selection_value >= 2) {
            if (ccz::editor::ExtractLeadingDecimalCode(option.label, code)) {
                option.numeric_code = code;
            } else if (IsNoItemLabel(option.label)) {
                option.numeric_code = first_global_code + option.selection_value - 2;
            }
        }
        if ((mode == IndexedMode::Item || mode == IndexedMode::ItemSegment) &&
            option.numeric_code >= 0 && option.numeric_code % 256 == 255) {
            option.numeric_searchable = false;
            option.numeric_resolvable = false;
        }
        visible_option_indexes_.push_back(i);
    }

    const int current = CComboBox::GetCurSel();
    if (current >= 0 && current < static_cast<int>(indexed_options_.size())) {
        indexed_selection_ = indexed_options_[current].selection_value;
        indexed_selection_valid_ = true;
        last_notified_selection_ = indexed_selection_;
        last_committed_text_ = indexed_options_[current].label.c_str();
    }
    CreateSearchEdit();
}

bool CMyComboBox::HasValidIndexedSelection() const
{
    return indexed_mode_ == IndexedMode::None || indexed_selection_valid_;
}

CString CMyComboBox::GetComboText() const
{
    CString text;
    if (search_edit_.GetSafeHwnd()) {
        search_edit_.GetWindowTextW(text);
    } else {
        const_cast<CMyComboBox*>(this)->GetWindowTextW(text);
    }
    return text;
}

void CMyComboBox::CreateSearchEdit()
{
    if (search_edit_.GetSafeHwnd() || !GetSafeHwnd()) return;

    CRect client;
    GetClientRect(&client);
    // Leave the native drop-down button exposed.  The edit is an overlay for
    // list-only resources, which lets us add input without rewriting the
    // legacy UTF-16 .rc file or changing ordinary combo styles.
    const int button_width = GetSystemMetrics(SM_CXVSCROLL);
    CRect edit_rect(2, 2, max(2, client.Width() - button_width - 1), max(4, client.Height() - 2));
    if (!search_edit_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
        edit_rect, this, kIndexedSearchEditId)) {
        return;
    }
    search_edit_.SetOwner(this);
    search_edit_.SetFont(GetFont());
    search_edit_.SetLimitText(255);
    const bool previous_rebuilding = rebuilding_;
    rebuilding_ = true;
    search_edit_.SetWindowTextW(last_committed_text_);
    rebuilding_ = previous_rebuilding;
    select_all_on_next_focus_ = true;
    search_edit_.BringWindowToTop();
}

void CMyComboBox::SetSearchEditText(const CString& text)
{
    if (search_edit_.GetSafeHwnd()) {
        CString current;
        search_edit_.GetWindowTextW(current);
        if (current == text) return;

        const DWORD selection = search_edit_.GetSel();
        const int old_start = static_cast<int>(LOWORD(selection));
        const int old_end = static_cast<int>(HIWORD(selection));
        const bool previous_rebuilding = rebuilding_;
        rebuilding_ = true;
        search_edit_.SetWindowTextW(text);
        rebuilding_ = previous_rebuilding;

        const int length = text.GetLength();
        const int new_start = min(old_start, length);
        const int new_end = min(old_end, length);
        search_edit_.SetSel(new_start, new_end);
    } else {
        SetWindowTextW(text);
    }
}

int CMyComboBox::FindVisibleRowForSelection(int selection_value) const
{
    for (size_t row = 0; row < visible_option_indexes_.size(); ++row) {
        const auto& option = indexed_options_[visible_option_indexes_[row]];
        if (option.selection_value == selection_value) return static_cast<int>(row);
    }
    return -1;
}

int CMyComboBox::SelectFirstVisibleRow()
{
    if (visible_option_indexes_.empty()) return CB_ERR;
    indexed_selection_ = indexed_options_[visible_option_indexes_[0]].selection_value;
    indexed_selection_valid_ = true;
    return CComboBox::SetCurSel(0);
}

void CMyComboBox::RestoreLastSelectionText()
{
    if (!last_committed_text_.IsEmpty()) {
        SetSearchEditText(last_committed_text_);
    }
}

void CMyComboBox::RestoreFullOptionList()
{
    if (rebuilding_ || indexed_mode_ == IndexedMode::None) return;

    const bool complete =
        native_list_complete_ &&
        CComboBox::GetCount() == static_cast<int>(indexed_options_.size()) &&
        visible_option_indexes_.size() == indexed_options_.size();
    if (complete) {
        filter_active_ = false;
        last_filter_query_.clear();
        filter_candidate_indexes_.clear();
        return;
    }

    rebuilding_ = true;
    CComboBox::ResetContent();
    visible_option_indexes_.clear();
    for (size_t i = 0; i < indexed_options_.size(); ++i) {
        CComboBox::AddString(indexed_options_[i].label.c_str());
        visible_option_indexes_.push_back(i);
    }
    native_list_complete_ = true;
    filter_active_ = false;
    last_filter_query_.clear();
    filter_candidate_indexes_.clear();
    rebuilding_ = false;
}

void CMyComboBox::BringSearchMatchIntoView(const CString& query)
{
    if (indexed_mode_ == IndexedMode::None) return;

    CString normalized_query(query);
    normalized_query.Trim();
    const std::wstring query_text = CStringToWString(normalized_query);
    const bool query_extends_previous =
        filter_active_ &&
        ccz::editor::IsStrictQueryExtension(last_filter_query_, query_text);
    const std::vector<size_t> matches = query_extends_previous
        ? ccz::editor::FilterIndexedOptionsFromCandidates(
              indexed_options_, query_text, filter_candidate_indexes_)
        : ccz::editor::FilterIndexedOptions(indexed_options_, query_text);

    last_filter_query_ = query_text;
    filter_candidate_indexes_ = matches;
    int selection = -1;
    const bool numeric_match =
        ccz::editor::ResolveDecimalCode(indexed_options_, query_text, selection);
    if (!numeric_match) {
        if (matches.empty()) return;
        if (indexed_selection_valid_) {
            for (size_t index : matches) {
                if (indexed_options_[index].selection_value == indexed_selection_) {
                    selection = indexed_selection_;
                    break;
                }
            }
        }
        if (selection < 0) {
            selection = indexed_options_[matches.front()].selection_value;
        }
    }

    const int row = FindVisibleRowForSelection(selection);
    if (row < 0) return;

    // Scrolling the native list is deliberately separate from selecting an
    // item.  CB_SETCURSEL (and our indexed override) can normalize the edit
    // text; during typing that would replace the user's unfinished query.
    // CB_SETTOPINDEX only brings the match into view and leaves the current
    // value untouched until the user explicitly commits it.
    CComboBox::SetTopIndex(row);
}

void CMyComboBox::RebuildVisibleOptions(const CString& query)
{
    if (rebuilding_ || indexed_mode_ == IndexedMode::None) return;

    CString normalized_query(query);
    normalized_query.Trim();
    const std::wstring query_text = CStringToWString(normalized_query);

    auto restore_full_list = [this]() {
        // Keep the automatic-popup origin until CBN_CLOSEUP.  Clearing it
        // while the popup is still open would make its later close look like
        // a user-confirmed native selection.
        if (!dropdown_open_) auto_filter_dropdown_ = false;
        RestoreFullOptionList();
    };

    if (query_text.empty()) {
        restore_full_list();
        return;
    }

    const std::vector<size_t> matches =
        ccz::editor::FilterIndexedOptions(indexed_options_, query_text);

    // Keep the complete option model in indexed_options_, but make the
    // native popup show exactly the current search candidates.  This is the
    // same live-filter behavior used by the scene-preview character index.
    const bool popup_open_before_rebuild =
        dropdown_open_ || GetDroppedState();
    rebuilding_ = true;
    CComboBox::SetRedraw(FALSE);
    CComboBox::ResetContent();
    visible_option_indexes_.clear();
    for (size_t model_index : matches) {
        CComboBox::AddString(indexed_options_[model_index].label.c_str());
        visible_option_indexes_.push_back(model_index);
    }
    CComboBox::SetCurSel(-1);
    CComboBox::SetRedraw(TRUE);
    Invalidate();
    native_list_complete_ = false;
    rebuilding_ = false;
    filter_active_ = true;

    // A search result is useful without first clicking the arrow.  Do not
    // select or write back a result here; only a click/Enter commits it.
    if (!matches.empty() && !popup_open_before_rebuild && !GetDroppedState()) {
        auto_filter_dropdown_ = true;
        ShowDropDown(TRUE);
        // The popup is asynchronous.  Return focus to the overlay edit after
        // the native list has been shown so the caret and subsequent typing
        // remain owned by the search field.
        RestoreSearchEditFocus();
    }
}

void CMyComboBox::SetCanonicalSelection(int selection_value)
{
    int row = FindVisibleRowForSelection(selection_value);
    if (row < 0) {
        RestoreFullOptionList();
        row = FindVisibleRowForSelection(selection_value);
    }
    if (row < 0) return;

    const size_t model_index = visible_option_indexes_[row];
    rebuilding_ = true;
    const int native_result = CComboBox::SetCurSel(row);
    rebuilding_ = false;
    if (native_result == CB_ERR) return;
    SetSearchEditText(indexed_options_[model_index].label.c_str());
    indexed_selection_ = selection_value;
    indexed_selection_valid_ = true;
    last_committed_text_ = indexed_options_[model_index].label.c_str();
}

bool CMyComboBox::CommitIndexedInput()
{
    if (indexed_mode_ == IndexedMode::None) return true;

    auto notify_if_needed = [this]() {
        if (indexed_selection_valid_ && indexed_selection_ != last_notified_selection_) {
            NotifySelectionChange();
        }
    };

    CString text = GetComboText();
    text.Trim();
    const std::wstring query = CStringToWString(text);
    if (query.empty()) {
        if (indexed_selection_valid_) {
            SetCanonicalSelection(indexed_selection_);
            notify_if_needed();
            return true;
        }
        RestoreLastSelectionText();
        return indexed_selection_valid_;
    }

    int selection = -1;
    if (ccz::editor::ResolveDecimalCode(indexed_options_, query, selection)) {
        SetCanonicalSelection(selection);
        notify_if_needed();
        return true;
    }

    std::vector<size_t> matched_indexes;
    for (size_t index : visible_option_indexes_) {
        if (ccz::editor::MatchesIndexedOption(indexed_options_[index], query)) {
            matched_indexes.push_back(index);
        }
    }
    if (!matched_indexes.empty()) {
        int target_selection = -1;
        if (matched_indexes.size() == 1) {
            target_selection = indexed_options_[matched_indexes.front()].selection_value;
        } else if (!ccz::editor::ResolveFirstMatchingOption(
            indexed_options_, matched_indexes, query, target_selection)) {
            return false;
        }
        SetCanonicalSelection(target_selection);
        notify_if_needed();
        return indexed_selection_valid_;
    }

    RestoreFullOptionList();
    RestoreLastSelectionText();
    return false;
}

void CMyComboBox::OnEditChange()
{
    if (rebuilding_ || indexed_mode_ == IndexedMode::None) return;
    const int previous_selection = indexed_selection_valid_ ? indexed_selection_ : -1;
    RebuildVisibleOptions(GetComboText());
    if (indexed_selection_valid_ && indexed_selection_ != previous_selection) {
        NotifySelectionChange();
    }
}

BOOL CMyComboBox::OnSelectionChangeReflect()
{
    // Ordinary enum combos still rely on their parent dialog's
    // ON_CBN_SELCHANGE handler (for example Dialog_50's type switch).  Only
    // indexed/searchable combos should consume the reflected notification.
    if (indexed_mode_ == IndexedMode::None) return FALSE;
    OnSelectionChange();
    if (!dropdown_open_) NotifySelectionChange();
    return TRUE;
}

void CMyComboBox::OnSelectionChange()
{
    if (rebuilding_ || indexed_mode_ == IndexedMode::None) return;
    const int row = CComboBox::GetCurSel();
    if (row < 0 || row >= static_cast<int>(visible_option_indexes_.size())) return;
    const size_t model_index = visible_option_indexes_[row];
    if (dropdown_open_) {
        // CBN_SELCHANGE is only a native highlight change.  Do not replace
        // the user's draft text or mark the row accepted until CBN_SELENDOK.
        dropdown_highlight_selection_ = indexed_options_[model_index].selection_value;
        dropdown_highlight_valid_ = true;
        return;
    }
    indexed_selection_ = indexed_options_[model_index].selection_value;
    indexed_selection_valid_ = true;
    rebuilding_ = true;
    SetSearchEditText(indexed_options_[model_index].label.c_str());
    rebuilding_ = false;
    last_committed_text_ = indexed_options_[model_index].label.c_str();
}

BOOL CMyComboBox::OnSelectionEndOkReflect()
{
    if (indexed_mode_ == IndexedMode::None) return FALSE;
    dropdown_selection_accepted_ = true;
    OnSelectionChange();
    if (dropdown_highlight_valid_) {
        SetCanonicalSelection(dropdown_highlight_selection_);
    }
    NotifySelectionChange();
    return TRUE;
}

BOOL CMyComboBox::OnSelectionEndCancelReflect()
{
    if (indexed_mode_ == IndexedMode::None) return FALSE;
    if (!dropdown_open_) return TRUE;

    const bool had_selection = dropdown_open_selection_valid_;
    const int selection = dropdown_open_selection_;
    dropdown_open_ = false;
    dropdown_selection_accepted_ = false;
    dropdown_cancelled_ = true;
    dropdown_open_selection_valid_ = false;
    dropdown_open_native_row_ = -1;
    dropdown_open_selection_ = -1;
    dropdown_highlight_valid_ = false;
    dropdown_highlight_selection_ = -1;
    auto_filter_dropdown_ = false;

    // Esc must restore the logical value that was active before the list was
    // opened.  This also restores the overlay text after CBN_SELCHANGE has
    // moved the native highlight.
    if (had_selection) SetCanonicalSelection(selection);
    else RestoreLastSelectionText();
    return TRUE;
}

void CMyComboBox::OnDropDown()
{
    if (rebuilding_ || indexed_mode_ == IndexedMode::None) return;
    const bool keep_filtered_results = auto_filter_dropdown_;
    dropdown_open_ = true;
    dropdown_selection_accepted_ = false;
    dropdown_cancelled_ = false;
    dropdown_highlight_valid_ = false;
    dropdown_highlight_selection_ = -1;
    dropdown_open_native_row_ = CComboBox::GetCurSel();
    dropdown_open_selection_valid_ = indexed_selection_valid_;
    dropdown_open_selection_ = indexed_selection_;
    if (!dropdown_open_selection_valid_ &&
        dropdown_open_native_row_ >= 0 &&
        dropdown_open_native_row_ < static_cast<int>(visible_option_indexes_.size())) {
        dropdown_open_selection_ =
            indexed_options_[visible_option_indexes_[dropdown_open_native_row_]].selection_value;
        dropdown_open_selection_valid_ = true;
    }
    // An automatic popup belongs to the current query and must keep its
    // filtered rows.  A manual click on the arrow restores the complete
    // legacy list, preserving the old browse-and-scroll workflow.
    if (!keep_filtered_results) RestoreFullOptionList();
    if (dropdown_open_selection_valid_) {
        const int restored_row = FindVisibleRowForSelection(dropdown_open_selection_);
        if (restored_row >= 0) {
            rebuilding_ = true;
            CComboBox::SetCurSel(restored_row);
            rebuilding_ = false;
            dropdown_open_native_row_ = restored_row;
        }
    }
    BringSearchMatchIntoView(GetComboText());
}

bool CMyComboBox::CommitNativeDropDownSelection()
{
    if (!dropdown_open_) return false;

    const int row = CComboBox::GetCurSel();
    // An automatically opened result list is a preview of the current query,
    // not a commit.  Its CBN_SELCHANGE/CBN_CLOSEUP sequence may contain a
    // native highlight even when the user never clicked a row.  Only an
    // explicit CBN_SELENDOK is allowed to write that result back.  For a
    // manually opened legacy list, retaining the old row-difference fallback
    // keeps keyboard/mouse selection behavior unchanged.
    const bool was_auto_filter_popup = auto_filter_dropdown_;
    const bool accepted = dropdown_selection_accepted_ ||
        (!was_auto_filter_popup && row != dropdown_open_native_row_);
    const bool had_selection = dropdown_open_selection_valid_;
    const int selection = dropdown_open_selection_;
    const bool had_highlight = dropdown_highlight_valid_;
    const int highlighted_selection = dropdown_highlight_selection_;
    dropdown_open_ = false;
    dropdown_selection_accepted_ = false;
    dropdown_cancelled_ = false;
    dropdown_open_selection_valid_ = false;
    dropdown_open_native_row_ = -1;
    dropdown_open_selection_ = -1;
    dropdown_highlight_valid_ = false;
    dropdown_highlight_selection_ = -1;
    auto_filter_dropdown_ = false;
    if (!accepted || row < 0 || row >= static_cast<int>(visible_option_indexes_.size())) {
        if (had_selection) SetCanonicalSelection(selection);
        else RestoreLastSelectionText();
        return true;
    }

    const int target_selection = had_highlight
        ? highlighted_selection
        : indexed_options_[visible_option_indexes_[row]].selection_value;
    SetCanonicalSelection(target_selection);
    NotifySelectionChange();
    return true;
}

void CMyComboBox::OnCloseUp()
{
    if (rebuilding_ || indexed_mode_ == IndexedMode::None) return;
    if (dropdown_cancelled_) {
        dropdown_cancelled_ = false;
        auto_filter_dropdown_ = false;
        return;
    }
    if (CommitNativeDropDownSelection()) return;
    CommitIndexedInput();
}

void CMyComboBox::OnSearchEditChange()
{
    OnEditChange();
}

void CMyComboBox::OnSearchEditSetFocus()
{
    if (indexed_mode_ != IndexedMode::None && search_edit_.GetSafeHwnd() &&
        select_all_on_next_focus_) {
        // The common workflow is to replace the displayed label with either
        // a decimal code or a search term.  Selecting all on first focus
        // keeps that workflow one click + one paste/type, while subsequent
        // caret movement remains untouched.
        search_edit_.SetSel(0, -1);
        select_all_on_next_focus_ = false;
    }
}

void CMyComboBox::RestoreSearchEditFocus()
{
    if (indexed_mode_ == IndexedMode::None || !search_edit_.GetSafeHwnd()) return;

    const DWORD selection = search_edit_.GetSel();
    const int start = static_cast<int>(LOWORD(selection));
    const int end = static_cast<int>(HIWORD(selection));
    search_edit_.SetFocus();
    // SetFocus may synchronously send EN_SETFOCUS and select all.  Restore
    // the user's actual caret/selection after that notification so opening a
    // result list never moves the insertion point to an arbitrary location.
    search_edit_.SetSel(start, end);
}

void CMyComboBox::OnSearchEditKillFocus()
{
    if (indexed_mode_ == IndexedMode::None || !GetSafeHwnd()) return;

    // Moving focus from the overlay to the combo's arrow/list is still part
    // of the same control.  Keep the live filter in place in that case;
    // commit only when focus leaves the whole combo (for example to OK or to
    // another dialog field).
    const HWND focus = ::GetFocus();
    if (focus == m_hWnd || focus == search_edit_.GetSafeHwnd() ||
        (focus && ::IsChild(m_hWnd, focus))) {
        return;
    }
    COMBOBOXINFO combo_info = { sizeof(COMBOBOXINFO) };
    if (::GetComboBoxInfo(m_hWnd, &combo_info) &&
        (focus == combo_info.hwndList || focus == combo_info.hwndItem)) {
        return;
    }
    // A transient focus gap can occur while the native popup is being opened;
    // do not parse the draft during that gap.  If focus has already moved to
    // a real dialog control (for example OK), commit normally and let the
    // subsequent CBN_CLOSEUP finish the popup lifecycle.
    if (!focus && (dropdown_open_ || GetDroppedState())) {
        return;
    }
    select_all_on_next_focus_ = true;
    CommitIndexedInput();
}

void CMyComboBox::NotifySelectionChange()
{
    if (!indexed_selection_valid_ || indexed_selection_ == last_notified_selection_) return;
    CWnd* parent = GetParent();
    if (!parent || !GetSafeHwnd()) return;
    const WPARAM wParam = MAKEWPARAM(GetDlgCtrlID(), CBN_SELCHANGE);
    last_notified_selection_ = indexed_selection_;
    parent->SendMessage(WM_COMMAND, wParam, reinterpret_cast<LPARAM>(m_hWnd));
}

BOOL CMyComboBox::ForwardMouseWheel(UINT nFlags, short zDelta, CPoint point)
{
    if (!GetSafeHwnd()) return FALSE;

    COMBOBOXINFO combo_info = { sizeof(COMBOBOXINFO) };
    HWND target = m_hWnd;
    if (::GetComboBoxInfo(m_hWnd, &combo_info) &&
        combo_info.hwndList && ::IsWindowVisible(combo_info.hwndList)) {
        target = combo_info.hwndList;
    }
    ::SendMessage(target, WM_MOUSEWHEEL,
        MAKEWPARAM(nFlags, static_cast<WORD>(zDelta)),
        MAKELPARAM(point.x, point.y));
    return TRUE;
}

void CMyComboBox::AppendTypeaheadChar(wchar_t value)
{
    if (cnt < 0 || cnt >= static_cast<int>(_countof(inputs))) cnt = 0;
    inputs[cnt++] = value;
}


BOOL CMyComboBox::PreTranslateMessage(MSG* pMsg)
{
	if (indexed_mode_ != IndexedMode::None) {
		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE &&
			!dropdown_open_ && !GetDroppedState()) {
			// With no native popup, CBN_SELENDCANCEL is never emitted.  Restore
			// the last committed value directly instead of treating Esc as a
			// failed Enter or leaving the filtered list in place.
			auto_filter_dropdown_ = false;
			select_all_on_next_focus_ = true;
			RestoreFullOptionList();
			RestoreLastSelectionText();
			return TRUE;
		}
		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN) {
			if (!CommitIndexedInput()) {
				MessageBeep(MB_ICONWARNING);
				return TRUE;
			}
		}
		return CComboBox::PreTranslateMessage(pMsg);
	}

	// Preserve the original prefix/typeahead behavior for ordinary enum
	// combos.  Only explicitly enabled indexed combos use the editable model.
	if (pMsg->message == WM_KEYUP) {
		return TRUE;
	}
	if (pMsg->message == WM_CHAR) {
		wchar_t a = pMsg->wParam;
		AppendTypeaheadChar(a);
				CString txt;
				bool flag1 = false;
				for (int i = 0; i < this->GetCount(); i++)
		{
			this->GetLBText(i, txt);
			bool flag2 = true;
			if (cnt > txt.GetLength())continue;
			for (int j = 0; j < cnt; j++)
			{
				if (j >= txt.GetLength())continue;
				if (txt[j] != inputs[j]) flag2 = false;
			}
			if (flag2)
			{
				this->SetCurSel(i);
				cur_selected = i;
				flag1 = true;
				break;
			}
		}
		if (flag1 == false) {
			cnt = 0;
			AppendTypeaheadChar(a);
			CString txt;
			bool flag1 = false;
			for (int i = 0; i < this->GetCount(); i++)
			{
				this->GetLBText(i, txt);
				bool flag2 = true;
				for (int j = 0; j < cnt; j++)
				{
					if (j >= txt.GetLength() || txt[j] != inputs[j]) flag2 = false;
				}
				if (flag2)
				{
					this->SetCurSel(i);
					cur_selected = i;
					flag1 = true;
					break;
				}
			}
		}

		WPARAM wParam = MAKELPARAM(this->GetDlgCtrlID(), CBN_SELCHANGE);
		HWND hWnd = this->m_hWnd;
		this->GetParent()->SendMessage(WM_COMMAND, wParam, (LPARAM)hWnd);
		return TRUE;
	}
	if (pMsg->message == WM_KEYDOWN) {
		wchar_t a = pMsg->wParam;
		if (a == 13)return CComboBox::PreTranslateMessage(pMsg);
		if (a >= VK_NUMPAD0 && a <= VK_NUMPAD9)
			a = a - VK_NUMPAD0 + '0';
		else if (a == VK_UP || a == VK_DOWN)
			return CComboBox::PreTranslateMessage(pMsg);
		else
			return CComboBox::PreTranslateMessage(pMsg);
		AppendTypeaheadChar(a);
		CString txt;
		bool flag1 = false;
		for (int i = 0; i < this->GetCount(); i++)
		{
			this->GetLBText(i, txt);
			bool flag2 = true;
				for (int j = 0; j < cnt; j++)
				{
					if (j >= txt.GetLength() || txt[j] != inputs[j]) flag2 = false;
				}
			if (flag2)
			{
				this->SetCurSel(i);
				cur_selected = i;
				flag1 = true;
				break;
			}
		}
		if (flag1 == false) {
			cnt = 0;
			AppendTypeaheadChar(a);
			CString txt;
			bool flag1 = false;
			for (int i = 0; i < this->GetCount(); i++)
			{
				this->GetLBText(i, txt);
				bool flag2 = true;
				for (int j = 0; j < cnt; j++)
				{
					if (j >= txt.GetLength() || txt[j] != inputs[j]) flag2 = false;
				}
				if (flag2)
				{
					this->SetCurSel(i);
					cur_selected = i;
					flag1 = true;
					break;
				}
			}
		}

		WPARAM wParam = MAKELPARAM(this->GetDlgCtrlID(), CBN_SELCHANGE);
		HWND hWnd = this->m_hWnd;
		this->GetParent()->SendMessage(WM_COMMAND, wParam, (LPARAM)hWnd);

		return TRUE;
	}

	return CComboBox::PreTranslateMessage(pMsg);
}

BOOL CMyEdit::PreTranslateMessage(MSG* pMsg)
{
	DWORD dwStyle = ::GetWindowLong(this->GetSafeHwnd(), GWL_STYLE);
	DWORD dwStyle2 = dwStyle &= ES_MULTILINE;
	if (dwStyle2)return CEdit::PreTranslateMessage(pMsg);
	if (pMsg->message == WM_CHAR)
	{
		if ((pMsg->wParam <= '9' && pMsg->wParam >= '0') || (pMsg->wParam <= 'f' && pMsg->wParam >= 'a')
			|| pMsg->wParam == '-' || pMsg->wParam == 'x' || pMsg->wParam == VK_BACK || pMsg->wParam == 3 || pMsg->wParam == 22)
		{
			return CEdit::PreTranslateMessage(pMsg);
		}
		else
			return TRUE;
	}
	/*if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == VK_CONTROL)
			return CEdit::PreTranslateMessage(pMsg);
		if ((pMsg->wParam <= '9' && pMsg->wParam >= '0') || (pMsg->wParam <= VK_NUMPAD9 && pMsg->wParam >= VK_NUMPAD0)
			|| pMsg->wParam == '-' || pMsg->wParam == 'X' || pMsg->wParam == VK_BACK || pMsg->wParam == 'c' || pMsg->wParam == 'v')
		{
			return CEdit::PreTranslateMessage(pMsg);
		}
		else return TRUE;
	}*/
	return CEdit::PreTranslateMessage(pMsg);
}
