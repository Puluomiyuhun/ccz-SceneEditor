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
                          L"Instruction combo regression", WS_OVERLAPPEDWINDOW,
                          CRect(0, 0, 600, 400), nullptr, 0));
    CMyComboBox combo;
    REQUIRE(combo.Create(WS_CHILD | CBS_DROPDOWNLIST, CRect(10, 10, 350, 300),
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
    auto edit = combo.GetDlgItem(0x7F42);
    REQUIRE(edit);
    edit->SetWindowTextW(L"0x58");
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
    REQUIRE(person.Create(WS_CHILD | CBS_DROPDOWNLIST, CRect(10, 10, 350, 300),
                          &host, 102));
    person.AddString(L"14:人物甲");
    person.AddString(L"20:人物乙");
    person.EnablePersonSearch();
    person.SetCurSel(1);
    auto personEdit = person.GetDlgItem(0x7F42);
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
