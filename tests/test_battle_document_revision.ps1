$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$docHeader = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2Doc.h') -Raw -Encoding UTF8
$doc = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2Doc.cpp') -Raw -Encoding UTF8
$view = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/cczEditor2View.cpp') -Raw -Encoding UTF8
$page = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$scenePreview = Get-Content -LiteralPath (Join-Path $repo 'cczEditor2/ScenePreviewWnd.cpp') -Raw -Encoding UTF8

function Assert-Text([string]$text, [string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { throw "BATTLE_DOCUMENT_REVISION_FAIL: $message" }
    Write-Output "[PASS] $message"
}

function Get-Block([string]$text, [string]$start, [string]$end) {
    $startAt = $text.IndexOf($start)
    if ($startAt -lt 0) { return '' }
    if ($end -eq '') { return $text.Substring($startAt) }
    $endAt = $text.IndexOf($end, $startAt + $start.Length)
    if ($endAt -lt 0) { return $text.Substring($startAt) }
    return $text.Substring($startAt, $endAt - $startAt)
}

Assert-Text $docHeader 'GetSPreviewDocumentId\(\) const' `
    'each document exposes its process-local identity'
Assert-Text $docHeader 'GetPreviewContentEpoch\(\) const' `
    'each document exposes its semantic content epoch'
Assert-Text $docHeader 'GetPreviewTreeGeneration\(\) const' `
    'each document exposes its tree lifetime generation'
Assert-Text $docHeader 'MarkEditorContentChanged\(\)' `
    'semantic editor mutations have an explicit revision notification'
Assert-Text $docHeader 'MarkEditorTreeRebuilt\(\)' `
    'completed tree replacement has an explicit lifecycle notification'
Assert-Text $doc 'std::atomic<std::uint64_t>\s+s_nextSPreviewDocumentId' `
    'document identities are allocated from a process-wide monotonic counter'
Assert-Text $doc 's_nextSPreviewDocumentId\.fetch_add\(\s*1' `
    'every document instance receives a distinct nonzero identity'
Assert-Text $doc 'm_sPreviewDocumentId\(AllocateSPreviewDocumentId\(\)\)' `
    'document identity is assigned once when the MFC document instance is created'

$onModified = Get-Block $doc 'void CcczEditor2Doc::OnModified(BOOL m)' 'BEGIN_MESSAGE_MAP(Dialog_Exit'
Assert-Text $onModified 'if \(m\s*==\s*TRUE\)\s*\{[\s\S]*MarkEditorContentChanged\(\)' `
    'ordinary successful editor mutations advance the semantic content epoch'
Assert-Text $onModified 'if \(m\s*==\s*TRUE\)' `
    'clearing the modified state does not advance content revisions'

$saveBlock = Get-Block $doc 'BOOL CcczEditor2Doc::OnSaveDocument(' 'void CcczEditor2Doc::recur_close('
Assert-Text $saveBlock 'if \(updatedTarget != data->int_data\[0\]\)[\s\S]*jumpTargetsChanged = true;[\s\S]*if \(jumpTargetsChanged\)\s*MarkEditorContentChanged\(\)' `
    'save-time jump-target normalization advances the content epoch only when data changes'
Assert-Text $saveBlock 'previousPath\.CompareNoCase\(lpszPathName\)\s*!=\s*0\)[\s\S]*MarkEditorContentChanged\(\)' `
    'saving the same document instance to a new path advances its content epoch'

$markContent = Get-Block $doc 'void CcczEditor2Doc::MarkEditorContentChanged()' 'void CcczEditor2Doc::MarkEditorTreeRebuilt()'
$markTree = Get-Block $doc 'void CcczEditor2Doc::MarkEditorTreeRebuilt()' ''
Assert-Text $markContent '\+\+m_previewContentEpoch' `
    'content epoch advances monotonically'
Assert-Text $markTree '\+\+m_previewTreeGeneration[\s\S]*\+\+m_previewContentEpoch' `
    'tree rebuild invalidates both old node handles and prior content snapshots'

$newTree = Get-Block $view 'void CcczEditor2View::CreateNewTree()' 'void CcczEditor2View::CreateFileTree()'
$fileTree = Get-Block $view 'void CcczEditor2View::CreateFileTree()' 'int CcczEditor2View::EditData('
Assert-Text $newTree 'MarkEditorTreeRebuilt\(\)' `
    'new-tree construction reports completion exactly at its boundary'
Assert-Text $fileTree 'MarkEditorTreeRebuilt\(\)' `
    'file-tree loading reports completion after jump targets are normalized'
if ($newTree -match 'MarkEditorContentChanged\(\)' -or
    $fileTree -match 'MarkEditorContentChanged\(\)') {
    throw 'BATTLE_DOCUMENT_REVISION_FAIL: tree loading must not count each constructed node as an edit'
}
Write-Output '[PASS] tree construction uses one completion notification instead of per-node edit notifications'

$insert = Get-Block $view 'HTREEITEM CcczEditor2View::InsertInstructionAt(' 'HTREEITEM CcczEditor2View::InsertBattleInstructionAt('
$dialog89 = Get-Block $view 'BOOL Dialog_89::OnInitDialog()' 'void Dialog_89::OnBnClickedOk()'
$quickKeys = Get-Block $view 'BOOL CcczEditor2View::PreTranslateMessage(' 'BOOL CcczEditor2View::OnEraseBkgnd('
Assert-Text $insert 'MarkEditorContentChanged\(\)' `
    'position-preview instruction insertion advances the document content epoch'
Assert-Text $dialog89 'normalizedInstructionValues[\s\S]*MarkEditorContentChanged\(\)' `
    'instruction values normalized during dialog initialization are tracked even if the dialog is canceled'
Assert-Text $quickKeys 'data->int_data\[0\]\s*=\s*fast_total;[\s\S]*MarkEditorContentChanged\(\)' `
    'quick numeric editing advances the content epoch'
Assert-Text $scenePreview 'MarkEditorContentChanged\(\)' `
    'direct coordinate writes and their undo path advance the document content epoch'
$nativeUndo = Get-Block $page 'bool CSBattlePreviewPage::UndoNativeWriteback(' 'void CSBattlePreviewPage::ClearDocument()'
Assert-Text $nativeUndo 'previousContentEpoch[\s\S]*ActiveSession\(\)\.native_writeback_transaction\.Undo\(host, diagnostic\)[\s\S]*GetPreviewContentEpoch\(\) == previousContentEpoch[\s\S]*MarkEditorContentChanged\(\)' `
    'successful S writeback undo advances the content epoch when dirty-state restoration did not do so'

$updateShow = Get-Block $view 'void CcczEditor2View::UpdateShow(HTREEITEM me)' 'HTREEITEM CcczEditor2View::CreateItem('
if ($updateShow -match 'MarkEditorContentChanged|OnModified\(TRUE\)') {
    throw 'BATTLE_DOCUMENT_REVISION_FAIL: display-only UpdateShow must not be treated as a semantic edit'
}
Write-Output '[PASS] display refresh does not advance semantic content revisions'

Write-Output 'BATTLE_DOCUMENT_REVISION_PASS'
