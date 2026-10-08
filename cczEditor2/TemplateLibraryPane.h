#pragma once
#include <afxcmn.h>
#include <afxdockablepane.h>
#include "TemplateLibrary/TemplateStore.h"
class CcczEditor2View;
class CTemplateLibraryPane : public CDockablePane {
public:
  void SaveSelection(CcczEditor2View &view);

protected:
  void OnUpdateCmdUI(CFrameWnd *target, BOOL disableIfNoHandler) override;
  afx_msg int OnCreate(LPCREATESTRUCT cs);
  afx_msg void OnSize(UINT type, int cx, int cy);
  afx_msg void OnTimer(UINT_PTR id);
  afx_msg void OnDestroy();
  afx_msg void OnSearch();
  afx_msg void OnSelected(NMHDR *, LRESULT *result);
  afx_msg void OnInsert();
  afx_msg void OnEdit();
  afx_msg void OnDelete();
  afx_msg void OnUndo();
  afx_msg void OnContextMenu(CWnd*, CPoint);
  afx_msg void OnPaint();
  DECLARE_MESSAGE_MAP()
private:
  CcczEditor2View *ActiveView();
  void Reload(bool report);
  void Fill();
  const ccz::templates::Entry *Selected() const;
  ccz::templates::Store store_;
  ccz::templates::Library library_;
  std::wstring diagnostic_;
  CEdit search_;
  CListCtrl table_;
  CTreeCtrl preview_;
  CStatic status_;
  CButton insert_, edit_, remove_, undo_;
  CFont font_;
  std::vector<std::wstring> visible_ids_;
  bool filling_ = false;
  bool stamp_known_ = false;
  std::pair<std::uint64_t, std::uint64_t> file_stamp_{};
};
