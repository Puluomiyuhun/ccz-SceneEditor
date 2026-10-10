#include "../../cczEditor2/pch.h"
#include "../../cczEditor2/CMyComboBox.h"
#include <iostream>
CWinApp testApplication;
#define REQUIRE(x)                                                             \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
int main() {
  try {
    REQUIRE(
        AfxWinInit(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), 0));
    CWnd host;
    REQUIRE(host.CreateEx(0, AfxRegisterWndClass(0),
                          L"Instruction combo regression", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          CRect(-2000, -2000, -1400, -1600), nullptr, 0));
    CMyComboBox combo;
    REQUIRE(combo.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, CRect(10, 10, 350, 300),
                         &host, 101));
    for (int id = 0; id < 124; ++id) {
      CString label;
      label.Format(L"%x:指令", id);
      if (id == 0x55)
        label = L"55:战场复活";
      if (id == 0x58)
        label = L"58:战场障碍设定";
      combo.AddString(label);
    }
    combo.EnableInstructionSearch();
    combo.SetCurSel(0x14);
    COMBOBOXINFO scrollInfo{sizeof(scrollInfo)};
    REQUIRE(GetComboBoxInfo(combo.GetSafeHwnd(), &scrollInfo));
    REQUIRE((GetWindowLongW(scrollInfo.hwndList, GWL_STYLE) & WS_VSCROLL) != 0);
    auto edit = combo.GetDlgItem(1001);
    REQUIRE(edit);
    ::SetCursor(::LoadCursorW(nullptr,IDC_ARROW));
    edit->SendMessage(WM_SETCURSOR,reinterpret_cast<WPARAM>(edit->GetSafeHwnd()),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));
    REQUIRE(::GetCursor()!=nullptr);
    edit->SetFocus();
    edit->SetWindowTextW(L"");
    for(wchar_t character : std::wstring(L"0x58")) edit->SendMessage(WM_CHAR, character, 1);
    REQUIRE(combo.GetDroppedState());
    GUITHREADINFO caretInfo{sizeof(caretInfo)};
    REQUIRE(GetGUIThreadInfo(GetCurrentThreadId(), &caretInfo));
    REQUIRE(caretInfo.hwndCaret == edit->GetSafeHwnd());
    REQUIRE(caretInfo.hwndFocus == edit->GetSafeHwnd());
    // Reproduce the native edit's NULL cursor while the real popup owns
    // capture. Verify recovery through mouse movement, not WM_SETCURSOR.
    POINT originalPosition;
    REQUIRE(GetCursorPos(&originalPosition));
    struct RestorePointerPosition {
      POINT original;
      ~RestorePointerPosition() { SetCursorPos(original.x, original.y); }
    } restorePointer{originalPosition};
    host.SetWindowPos(&CWnd::wndTopMost, 100, 100, 600, 400,
                      SWP_SHOWWINDOW | SWP_NOACTIVATE);
    combo.ShowDropDown(FALSE);
    combo.ShowDropDown(TRUE);
    CRect editBounds;
    edit->GetWindowRect(editBounds);
    REQUIRE(SetCursorPos(editBounds.left + 10, editBounds.top + 8));
    COMBOBOXINFO captured{sizeof(captured)};
    REQUIRE(GetComboBoxInfo(combo.GetSafeHwnd(), &captured));
    REQUIRE(GetCapture() == captured.hwndList);
    SetCursor(nullptr);
    SendMessage(captured.hwndList, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
    REQUIRE(GetCursor() == LoadCursorW(nullptr, IDC_IBEAM));
    CRect listBounds;
    REQUIRE(GetWindowRect(captured.hwndList, &listBounds));
    REQUIRE(SetCursorPos(listBounds.left + 10, listBounds.top + 10));
    SetCursor(nullptr);
    SendMessage(captured.hwndList, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
    REQUIRE(GetCursor() == LoadCursorW(nullptr, IDC_ARROW));
    REQUIRE(SetCursorPos(0, 0));
    SendMessage(captured.hwndList, WM_MOUSEMOVE, 0, 0);
    REQUIRE(SetCursorPos(listBounds.left + 10, listBounds.top + 10));
    SetCursor(nullptr);
    SendMessage(captured.hwndList, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
    REQUIRE(GetCursor() == LoadCursorW(nullptr, IDC_ARROW));
    REQUIRE(GetCapture() == captured.hwndList);
    GetGUIThreadInfo(GetCurrentThreadId(), &caretInfo);
    REQUIRE(caretInfo.hwndFocus == edit->GetSafeHwnd());
    host.SetWindowPos(&CWnd::wndNoTopMost, -2000, -2000, 600, 400, SWP_NOACTIVATE);
    std::cout << "[PASS] Captured popup restores NULL cursor on edit/list and re-entry; scrollbar is retained.\n";
    combo.ShowDropDown(FALSE);
    edit->SetWindowTextW(L"0x58");
    for(HWND surface:{combo.GetSafeHwnd(),edit->GetSafeHwnd()}) {
      ::SetCursor(::LoadCursorW(nullptr,IDC_ARROW));::SendMessageW(surface,WM_SETCURSOR,reinterpret_cast<WPARAM>(surface),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));REQUIRE(::GetCursor()!=nullptr);
    }
    COMBOBOXINFO info{sizeof(info)};GetComboBoxInfo(combo.GetSafeHwnd(),&info);
    ::SetCursor(::LoadCursorW(nullptr,IDC_ARROW));::SendMessageW(info.hwndList,WM_SETCURSOR,reinterpret_cast<WPARAM>(info.hwndList),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));REQUIRE(::GetCursor()!=nullptr);

    REQUIRE(combo.CommitIndexedInput());
    REQUIRE(combo.GetCurSel() == 0x58);
    edit->SetWindowTextW(L"4D");
    REQUIRE(combo.CommitIndexedInput());
    REQUIRE(combo.GetCurSel() == 0x4d);
    edit->SetWindowTextW(L"复活");
    REQUIRE(combo.CommitIndexedInput());
    REQUIRE(combo.GetCurSel() == 0x55);
    std::cout << "[PASS] Production MFC combo resolves hexadecimal and "
                 "filtered name to stable opcode.\n";
    edit->SetWindowTextW(L"0xGG");
    REQUIRE(!combo.CommitIndexedInput());
    CString text;
    edit->GetWindowTextW(text);
    REQUIRE(text == L"0xGG");
    REQUIRE(combo.GetCurSel() == CB_ERR);
    REQUIRE(!combo.CommitIndexedInput());
    host.SendMessage(WM_COMMAND, MAKEWPARAM(101, CBN_SELENDCANCEL),
                     reinterpret_cast<LPARAM>(combo.GetSafeHwnd()));
    edit->GetWindowTextW(text);
    REQUIRE(text == L"0xGG");
    REQUIRE(!combo.CommitIndexedInput());
    std::cout << "[PASS] Invalid draft survives commit and popup dismissal; "
                 "prior opcode cannot be submitted.\n";
    edit->SetWindowTextW(L"");
    REQUIRE(!combo.CommitIndexedInput());
    combo.SetCurSel(0x58);
    REQUIRE(combo.CommitIndexedInput());
    REQUIRE(combo.GetCurSel() == 0x58);
    std::cout << "[PASS] Empty draft is rejected and explicit native selection "
                 "remains usable.\n";
    combo.DestroyWindow();
    CMyComboBox person;
    REQUIRE(person.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, CRect(10, 10, 350, 300),
                          &host, 102));
    person.AddString(L"14:人物甲");
    person.AddString(L"20:人物乙");
    person.EnablePersonSearch();
    person.SetCurSel(1);
    auto personEdit = person.GetDlgItem(1001);
    personEdit->SetWindowTextW(L"14");
    REQUIRE(person.CommitIndexedInput());
    REQUIRE(person.GetCurSel() == 0);
    std::cout << "[PASS] Existing person picker still interprets numeric input "
                 "as decimal.\n";
    person.DestroyWindow();
    host.DestroyWindow();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
