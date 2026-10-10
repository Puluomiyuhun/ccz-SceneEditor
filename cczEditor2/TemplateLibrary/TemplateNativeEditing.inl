// Included by cczEditor2View.cpp; ownership stays with the native tree.
#include <functional>
namespace {
void TemplateCollectChecked(CTreeCtrl &tree, HTREEITEM item,
                            std::vector<HTREEITEM> &result) {
  for (; item; item = tree.GetNextSiblingItem(item)) {
    auto data = reinterpret_cast<ItemData *>(tree.GetItemData(item));
    if (tree.GetCheck(item) && data && data->id != 0 && data->id != 1)
      result.push_back(item);
    else
      TemplateCollectChecked(tree, tree.GetChildItem(item), result);
  }
}
ccz::templates::Node TemplateCaptureNode(CTreeCtrl &tree, HTREEITEM item,
                                         int &key, std::map<int, int> &ordToKey,
                                         std::size_t depth) {
  if (depth > 64 || key >= static_cast<int>(ccz::templates::MaxNodes))
    throw std::runtime_error("large template");
  auto data = reinterpret_cast<ItemData *>(tree.GetItemData(item));
  if (!data)
    throw std::runtime_error("missing data");
  ccz::templates::Node node;
  node.id = data->id;
  node.key = key++;
  if (!ordToKey.emplace(data->ord, node.key).second)
    throw std::runtime_error("ambiguous ordinals");
  node.params = data->int_data;
  // Preview writeback stores compact semantic vectors; templates use the
  // editor's full storage layout so they can also be saved/reused as EEX.
  const std::size_t nativeFields = node.id == 0x46 || node.id == 0x47 ? 1000u
      : node.id == 0x05 ? 60u : 20u;
  if (node.params.size() < nativeFields) node.params.resize(nativeFields, 0);
  node.has_text = data->long_char_data != nullptr;
  if (node.has_text) {
    auto length =
        wcsnlen_s(data->long_char_data, data->long_char_data_capacity);
    if (length == data->long_char_data_capacity)
      throw std::runtime_error("invalid text");
    node.text.assign(data->long_char_data, length);
  }
  node.label = (LPCWSTR)tree.GetItemText(item);
  for (auto child = tree.GetChildItem(item); child;
       child = tree.GetNextSiblingItem(child))
    node.children.push_back(
        TemplateCaptureNode(tree, child, key, ordToKey, depth + 1));
  return node;
}
void TemplateRemapCapturedJumps(std::vector<ccz::templates::Node> &nodes,
                                const std::map<int, int> &ords) {
  for (auto &node : nodes) {
    if (node.id == 0x76) {
      auto target = ords.find(node.params.at(0));
      node.jump_target = target == ords.end() ? -1 : target->second;
    }
    TemplateRemapCapturedJumps(node.children, ords);
  }
}
void TemplateDeleteRoot(CTreeCtrl &tree, HTREEITEM root) {
  for (auto child = tree.GetChildItem(root); child;) {
    auto next = tree.GetNextSiblingItem(child);
    TemplateDeleteRoot(tree, child);
    child = next;
  }
  auto data = reinterpret_cast<ItemData *>(tree.GetItemData(root));
  tree.SetItemData(root, 0);
  if (data) {
    ccz::editor::ReleaseLongCharData(data->long_char_data,
                                     data->long_char_data_capacity);
    delete data;
  }
  tree.DeleteItem(root);
}
struct TemplatePreparedNode {
  std::unique_ptr<ItemData> data;
  std::wstring label;
  std::vector<TemplatePreparedNode> children;
  TemplatePreparedNode() = default;
  TemplatePreparedNode(TemplatePreparedNode &&) = default;
  TemplatePreparedNode &operator=(TemplatePreparedNode &&) = default;
  ~TemplatePreparedNode() {
    if (data)
      ccz::editor::ReleaseLongCharData(data->long_char_data,
                                       data->long_char_data_capacity);
  }
};
TemplatePreparedNode TemplatePrepare(const ccz::templates::Node &node,
                                     int &ordinal, std::map<int, int> &keys) {
  if (ordinal >= 200000)
    throw std::runtime_error("ordinal limit");
  TemplatePreparedNode p;
  p.data = std::make_unique<ItemData>();
  p.data->id = node.id;
  p.data->ord = ordinal++;
  p.data->int_data = node.params;
  keys.emplace(node.key, p.data->ord);
  p.label = node.label;
  if (node.has_text &&
      !ccz::editor::ReplaceLongCharData(
          p.data->long_char_data, p.data->long_char_data_capacity,
          node.text.c_str(),
          (std::max)(ccz::editor::kLongCharDataCapacity, node.text.size() + 1)))
    throw std::bad_alloc();
  for (auto &child : node.children)
    p.children.push_back(TemplatePrepare(child, ordinal, keys));
  return p;
}
void TemplatePatchJumps(TemplatePreparedNode &p,
                        const ccz::templates::Node &node,
                        const std::map<int, int> &keys, const std::map<int,int>& external) {
  if (node.id == 0x76) {
    p.data->int_data[0] = node.jump_target >= 0 ? keys.at(node.jump_target) : external.at(node.params.at(0));
    p.label = L"76:无条件跳转 " + std::to_wstring(p.data->int_data[0]);
  }
  for (std::size_t i = 0; i < p.children.size(); ++i)
    TemplatePatchJumps(p.children[i], node.children[i], keys, external);
}
HTREEITEM TemplateInsertPrepared(CTreeCtrl &tree, TemplatePreparedNode &p,
                                 HTREEITEM parent, HTREEITEM after) {
  auto item = tree.InsertItem(p.label.c_str(), 0, 1, parent, after);
  if (!item)
    throw std::bad_alloc();
  tree.SetItemData(item, reinterpret_cast<DWORD_PTR>(p.data.release()));
  try {
    for (auto &child : p.children)
      TemplateInsertPrepared(tree, child, item, TVI_LAST);
  } catch (...) {
    TemplateDeleteRoot(tree, item);
    throw;
  }
  return item;
}
} // namespace
bool CcczEditor2View::CaptureTemplateSelection(ccz::templates::Entry &output,
                                               CString &error) {
  auto &tree = GetTreeCtrl();
  std::vector<HTREEITEM> selection;
  if (checkbox)
    TemplateCollectChecked(tree, tree.GetRootItem(), selection);
  else if (tree.GetSelectedItem())
    selection.push_back(tree.GetSelectedItem());
  if (selection.empty()) {
    error = L"请先选择需要存入模板库的指令";
    return false;
  }
  auto parent = tree.GetParentItem(selection.front());
  auto first =
      reinterpret_cast<ItemData *>(tree.GetItemData(selection.front()));
  if (!first || first->id == 0 || first->id == 1) {
    error = L"请选择指令、完整子事件、Section 或 Scene";
    return false;
  }
  ccz::templates::Entry entry;
  entry.uid = L"capture";
  entry.name = L"capture";
  entry.level = first->id == -1   ? ccz::templates::Level::Scene
                : first->id == -2 ? ccz::templates::Level::Section
                                  : ccz::templates::Level::Instructions;
  for (auto item : selection) {
    auto d = reinterpret_cast<ItemData *>(tree.GetItemData(item));
    if (tree.GetParentItem(item) != parent || !d ||
        (d->id < 0 && d->id != first->id) || (d->id >= 0 && first->id < 0)) {
      error = L"请选择同一层级、同一父节点下的项目";
      return false;
    }
  }
  try {
    // Include the preceding 01 for a complete nested event without making
    // the user select structural marker nodes that copy normally excludes.
    std::set<HTREEITEM> selected(selection.begin(), selection.end());
    for (auto item : selection) {
      auto data = reinterpret_cast<ItemData *>(tree.GetItemData(item));
      auto prev = tree.GetPrevSiblingItem(item);
      auto prevData =
          prev ? reinterpret_cast<ItemData *>(tree.GetItemData(prev)) : nullptr;
      if (data->id > 0 && tree.GetChildItem(item)) {
        if (!prevData || prevData->id != 1) {
          error = L"子事件缺少配套的子事件设定指令";
          return false;
        }
        selected.insert(prev);
      }
    }
    std::map<int, int> ords;
    int key = 0;
    for (auto item = parent ? tree.GetChildItem(parent) : tree.GetRootItem();
         item; item = tree.GetNextSiblingItem(item))
      if (selected.count(item))
        entry.roots.push_back(TemplateCaptureNode(tree, item, key, ords, 0));
    TemplateRemapCapturedJumps(entry.roots, ords);
    ccz::templates::Validate(entry);
    output = std::move(entry);
    return true;
  } catch (...) {
    error = L"无法保存模板：指令结构不完整或数据无效";
    return false;
  }
}
bool CcczEditor2View::InsertTemplate(const ccz::templates::Entry &entry,
                                     CString &error) {
  auto &tree = GetTreeCtrl();
  auto anchor = tree.GetSelectedItem();
  if (!anchor) {
    error = L"请先选择需要插入新指令的位置";
    return false;
  }
  auto data = reinterpret_cast<ItemData *>(tree.GetItemData(anchor));
  auto parent = tree.GetParentItem(anchor);
  auto after = anchor;
  try {
    ccz::templates::Validate(entry);
  } catch (...) {
    error = L"模板格式或内部跳转无效";
    return false;
  }
  std::function<bool(const std::vector<ccz::templates::Node> &)> compatible =
      [&](const auto &nodes) {
        for (const auto &node : nodes) {
          if (node.id >= code_sum ||
              (node.id > 0 && !node.children.empty() &&
               code_test[node.id] == 0) ||
              !compatible(node.children))
            return false;
        }
        return true;
      };
  if (!compatible(entry.roots)) {
    error = L"模板中的指令或子事件结构与当前编辑器不匹配";
    return false;
  }
  if (entry.level == ccz::templates::Level::Scene) {
    if (anchor == tree.GetRootItem()) {
      parent = anchor;
      after = TVI_LAST;
    } else if (!data || data->id != -1) {
      error = L"Scene 模板需要选择 Scene 或剧本根节点";
      return false;
    }
  } else if (entry.level == ccz::templates::Level::Section) {
    if (data && data->id == -1) {
      parent = anchor;
      after = TVI_LAST;
    } else if (!data || data->id != -2) {
      error = L"Section 模板需要选择 Section 或 Scene";
      return false;
    }
  } else {
    if (!data || data->id < 0) {
      error = L"请先选择需要插入新指令的位置";
      return false;
    }
    if (ccz::editor_preview::
            IsPreviewInsertionImmediatelyAfterEventEndOrSubeventSetting(
                data->id)) {
      error = L"不允许在事件结束或子事件设定后面添加指令";
      return false;
    }
    auto pd = parent ? reinterpret_cast<ItemData *>(tree.GetItemData(parent))
                     : nullptr;
    if (!pd) {
      error = L"插入层级不匹配";
      return false;
    }
    for (auto &n : entry.roots) {
      if (n.id >= code_sum || n.id < 0) {
        error = L"模板包含当前编辑器不支持的指令";
        return false;
      }
      if (pd->id == -2 && (code_test[n.id] < 2 && n.id != 0x77 &&
                           n.id != 0x78 && n.id != 0x72)) {
        error = L"Section 头部不能插入这组指令，请选择事件内的位置";
        return false;
      }
      if (pd->id == -1) {
        error = L"不能在 Scene 中直接插入指令";
        return false;
      }
    }
  }
  std::set<int> externalIds;
  std::function<void(const std::vector<ccz::templates::Node>&)> collectExternal = [&](const auto& nodes) {
    for (const auto& node : nodes) {
      if (node.id == 0x76 && node.jump_target == -1) externalIds.insert(node.params.at(0));
      collectExternal(node.children);
    }
  };
  collectExternal(entry.roots);
  std::map<int,int> externalTargets;
  if (!ResolveTransferExternal(*this,externalIds,true,externalTargets)) {
    error = L"已取消模板插入"; return false;
  }
  if (!CanUndoTemplateInsertion())
    m_templateUndo.clear();
  TemplateInsertionUndo undo;
  undo.anchor = anchor;
  undo.ordinal_before = cur_code_ord;
  undo.modified_before = GetDocument()->modified;
  undo.generation = GetDocument()->GetPreviewTreeGeneration();
  int nextOrdinal = cur_code_ord;
  std::map<int, int> keys;
  std::vector<TemplatePreparedNode> prepared;
  try {
    for (auto &n : entry.roots)
      prepared.push_back(TemplatePrepare(n, nextOrdinal, keys));
    for (std::size_t i = 0; i < prepared.size(); ++i)
      TemplatePatchJumps(prepared[i], entry.roots[i], keys, externalTargets);
    undo.roots.reserve(prepared.size());
    m_templateUndo.reserve(m_templateUndo.size() + 1);
  } catch (...) {
    error = L"模板过大或内存不足，未写入任何指令";
    return false;
  }
  const bool previousSuppression = m_suppressBattleWritebackPreview;
  m_suppressBattleWritebackPreview = true;
  tree.SetRedraw(FALSE);
  try {
    for (auto &p : prepared) {
      after = TemplateInsertPrepared(tree, p, parent, after);
      undo.roots.push_back(after);
    }
  } catch (...) {
    for (auto root : undo.roots)
      TemplateDeleteRoot(tree, root);
    m_suppressBattleWritebackPreview = previousSuppression;
    tree.SetRedraw(TRUE);
    tree.Invalidate();
    error = L"模板插入失败，已回滚整组指令";
    return false;
  }
  cur_code_ord = nextOrdinal;
  GetDocument()->OnModified(TRUE);
  GetDocument()->SetModifiedFlag(TRUE);
  undo.epoch = GetDocument()->GetPreviewContentEpoch();
  m_templateUndo.push_back(std::move(undo));
  if (checkbox)
    refreshCheckbox(tree.GetRootItem(), -1);
  cur_item = after;
  tree.Expand(parent, TVE_EXPAND);
  tree.SelectItem(after);
  tree.EnsureVisible(after);
  m_suppressBattleWritebackPreview = previousSuppression;
  tree.SetRedraw(TRUE);
  tree.Invalidate();
  RefreshScenePreview();
  return true;
}
bool CcczEditor2View::CanUndoTemplateInsertion() const {
  if (m_templateUndo.empty() || !GetDocument())
    return false;
  const auto &u = m_templateUndo.back();
  return u.epoch == GetDocument()->GetPreviewContentEpoch() &&
         u.generation == GetDocument()->GetPreviewTreeGeneration();
}
bool CcczEditor2View::UndoTemplateInsertion(CString &error) {
  if (!CanUndoTemplateInsertion()) {
    m_templateUndo.clear();
    error = L"没有可撤回的模板插入；其他编辑或重新加载后旧记录不再适用";
    return false;
  }
  auto undo = std::move(m_templateUndo.back());
  m_templateUndo.pop_back();
  auto &tree = GetTreeCtrl();
  const bool suppression = m_suppressBattleWritebackPreview;
  m_suppressBattleWritebackPreview = true;
  tree.SetRedraw(FALSE);
  tree.SelectItem(undo.anchor);
  for (auto root : undo.roots)
    TemplateDeleteRoot(tree, root);
  cur_code_ord = undo.ordinal_before;
  GetDocument()->OnModified(undo.modified_before ? TRUE : FALSE);
  if (!undo.modified_before)
    GetDocument()->MarkEditorContentChanged();
  GetDocument()->SetModifiedFlag(undo.modified_before ? TRUE : FALSE);
  if (!m_templateUndo.empty())
    m_templateUndo.back().epoch = GetDocument()->GetPreviewContentEpoch();
  cur_item = undo.anchor;
  tree.EnsureVisible(cur_item);
  m_suppressBattleWritebackPreview = suppression;
  tree.SetRedraw(TRUE);
  tree.Invalidate();
  RefreshScenePreview();
  return true;
}
void CcczEditor2View::OnSaveTemplate() {
  auto frame = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
  if (frame)
    frame->m_wndTemplateLibrary.SaveSelection(*this);
}
