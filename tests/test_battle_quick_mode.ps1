$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$snapshotHeader = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/PreviewIntegration/EditorTreeSnapshot.h') -Raw -Encoding UTF8
$snapshotSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/PreviewIntegration/EditorTreeSnapshot.cpp') -Raw -Encoding UTF8
$pageHeader = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/BattlePreviewPage.h') -Raw -Encoding UTF8
$pageSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/BattlePreviewPage.cpp') -Raw -Encoding UTF8
$writebackSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/PreviewIntegration/BattleWritebackAdapter.cpp') -Raw -Encoding UTF8
$viewSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/cczEditor2View.cpp') -Raw -Encoding UTF8
$documentSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/cczEditor2Doc.cpp') -Raw -Encoding UTF8
$hostSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/ScenePreviewHost.cpp') -Raw -Encoding UTF8
$resolverSource = Get-Content -LiteralPath (Join-Path $root 'cczEditor2/PreviewIntegration/BattleQuickModeResolver.cpp') -Raw -Encoding UTF8

function Assert-Contains([string]$text, [string]$pattern, [string]$label) {
    if ($text -notmatch $pattern) {
        throw "BATTLE_QUICK_MODE_FAIL: $label"
    }
    Write-Output "[PASS] $label"
}

function Get-FunctionBlock([string]$text, [string]$signature) {
    $start = $text.IndexOf($signature, [System.StringComparison]::Ordinal)
    if ($start -lt 0) { throw "BATTLE_QUICK_MODE_FAIL: missing $signature" }
    $open = $text.IndexOf('{', $start)
    if ($open -lt 0) { throw "BATTLE_QUICK_MODE_FAIL: missing body for $signature" }
    $depth = 0
    for ($i = $open; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq '{') { $depth++ }
        elseif ($text[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $text.Substring($start, $i - $start + 1) }
        }
    }
    throw "BATTLE_QUICK_MODE_FAIL: unbalanced body for $signature"
}

function Assert-FunctionContains([string]$text, [string]$signature, [string]$pattern, [string]$label) {
    $block = Get-FunctionBlock $text $signature
    Assert-Contains $block $pattern $label
}

Assert-Contains $snapshotHeader 'CaptureEditorDocumentSnapshot\s*\(' 'full-document capture API is declared for auto-resolution without a selected Section'
$documentCapture = Get-FunctionBlock $snapshotSource 'bool CaptureEditorDocumentSnapshot('
Assert-Contains $documentCapture 'tree\.GetRootItem\(\)' 'document-only capture starts from the script root'
Assert-Contains $documentCapture 'CaptureDocumentRoot|CaptureNode' 'document-only capture copies the complete TreeView into owned nodes'
Assert-Contains $documentCapture 'document_roots\.push_back|CaptureDocumentRoot' 'document-only capture retains the full document snapshot'
Assert-Contains $documentCapture 'snapshot\.document_path' 'document-only capture preserves source document identity'
Assert-Contains $documentCapture 'snapshot\.selected_handle\s*=\s*reinterpret_cast<std::uintptr_t>\(\s*tree\.GetSelectedItem\(\)\)' 'document capture retains the current TreeView item as a fallback insertion anchor'
if ($documentCapture -match 'if\s*\(\s*snapshot\.selected_handle') {
    throw 'BATTLE_QUICK_MODE_FAIL: document resolution is gated on a selection instead of treating it as an optional insertion anchor'
}
Write-Output '[PASS] document resolution remains independent of the optional selected-node anchor'

Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestPreviewRefresh(' 'ActiveSession\(\)\.ui\.quick_mode_enabled' 'S preview refresh routes through the document session quick-mode state'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestPreviewRefresh(' 'RequestQuickModeRefresh' 'S preview refresh delegates selection-independent quick-mode routing'
Assert-FunctionContains $pageSource 'void CSBattlePreviewPage::LoadCapturedBattle(' 'ActiveSession\(\)\.baseline_capture = std::move\(capture\)' 'loading transfers the single capture into the immutable writeback baseline'
Assert-FunctionContains $pageSource 'void CSBattlePreviewPage::LoadCapturedBattle(' 'BuildBattleConditionTree\(\s*ActiveSession\(\)\.baseline_capture,' 'condition routing reuses the retained capture instead of copying the full document again'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh(' 'CaptureEditorPinnedSectionSnapshot' 'same-source quick mode validates only its pinned Scene1 Section'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh(' 'ComputeBattleSectionRevision' 'same-source quick mode hashes only the pinned Section content'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh(' 'ResolveScene1BattleSection' 'quick mode resolves the pinned Scene1 battle Section'
Assert-FunctionContains $pageSource 'void CSBattlePreviewPage::RestoreActiveSessionUiState(' 'GetProfileInt[\s\S]*_T\("QuickMode"\), 1' 'new document sessions default to quick mode enabled from the application profile'
Assert-FunctionContains $pageSource 'void CSBattlePreviewPage::CreateEditorControls(' 'BS_AUTOCHECKBOX' 'quick mode is exposed as a user toggle'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh(' 'quick_mode_section_revision' 'quick mode follows content changes inside its pinned Scene1 Section'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh(' 'CaptureEditorDocumentSnapshot' 'a changed pinned Section triggers a fresh source resolution'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh(' 'ResolveScene1BattleSection' 'changed source content is reclassified against all current Sections'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh(' 'LoadQuickModeBaselinePreservingDraft\(\s*std::move\(resolution\.capture\), requestedTree' 'the resolved Scene1 capture refreshes the baseline while rebasing actual preview drafts'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh(' 'can_rebase_obstacle_drafts\(ActiveSession\(\)\.baseline_capture,' 'staged obstacles receive a map-cell conflict check before Scene1 refresh instead of freezing every source edit'
$freshDraftState = Get-FunctionBlock $pageSource 'bool build_fresh_editor_state_for_scope('
Assert-Contains $freshDraftState 'in_scope\(key, capture\.input\.scope_key\)' 'draft source rows must still belong to the pinned Scene1 section'
Assert-Contains $freshDraftState 'is_current_terminal_anchor' 'draft insertion anchors must still be the same terminal child of the same parent'
Assert-Contains $freshDraftState 'parsedMap\.map_id\s*!=\s*oldMap\.map_id' 'a genuinely changed map ID must still block draft merging'
Assert-Contains $freshDraftState 'parsedMap\.width\s*>\s*0\s*&&\s*oldMap\.width\s*>\s*0\s*&&\s*parsedMap\.width\s*!=\s*oldMap\.width' 'unknown parser map width must not be treated as a real dimension change'
Assert-Contains $freshDraftState 'parsedMap\.height\s*>\s*0\s*&&\s*oldMap\.height\s*>\s*0\s*&&\s*parsedMap\.height\s*!=\s*oldMap\.height' 'unknown parser map height must not be treated as a real dimension change'
Assert-Contains $freshDraftState 'fresh_state\.battle\.map\.width\s*=\s*oldMap\.width' 'same-map draft rebases must retain the JPG-resolved map width'
Assert-Contains $freshDraftState 'fresh_state\.battle\.map\.height\s*=\s*oldMap\.height' 'same-map draft rebases must retain the JPG-resolved map height'
Assert-Contains $freshDraftState 'fresh_state\.battle\.map\.projection_valid\s*=\s*oldMap\.projection_valid' 'same-map draft rebases must retain the proven JPG grid projection'
Assert-Contains $freshDraftState 'fresh_state\.battle\.terrain\s*=\s*old_state\.battle\.terrain' 'same-map draft rebases must retain the already-decoded terrain grid'
$writebackPlan = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::BuildPendingWritebackPlan('
if ($writebackPlan -match 'SelectCapturedBattleSection|CaptureEditorDocumentSnapshot|BuildBattlePreviewInput') {
    throw 'BATTLE_QUICK_MODE_FAIL: writeback planning reselects and reconverts the already captured pinned Section'
}
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::BuildPendingWritebackPlan(' 'BuildBattleWritebackSectionSnapshot\(\s*ActiveSession\(\)\.baseline_capture,\s*fresh_capture,' 'quick mode writeback validates the pinned baseline against its fresh capture'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::BuildPendingWritebackPlan(' 'BuildBattleWritebackNativeBatch\(\s*plan,\s*fresh_capture,' 'quick mode native writes use the same pinned capture that passed preflight'
Write-Output '[PASS] quick-mode writeback consumes its single already-resolved capture'
$quickRefresh = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::RequestQuickModeRefresh('
$sameSourceBlock = Get-FunctionBlock $quickRefresh 'if (sameSource)'
$changedRevisionIndex = $sameSourceBlock.IndexOf('if (freshSectionRevision == ActiveSession().quick_mode_section_revision)', [System.StringComparison]::Ordinal)
if ($changedRevisionIndex -lt 0) {
    throw 'BATTLE_QUICK_MODE_FAIL: same-source route has no pinned-section change gate'
}
$unchangedRevisionPath = $sameSourceBlock.Substring(0, $changedRevisionIndex)
if ($unchangedRevisionPath -match 'BuildBattlePreviewInput|CaptureEditorDocumentSnapshot|SelectCapturedBattleSection') {
    throw 'BATTLE_QUICK_MODE_FAIL: selecting another instruction in the pinned Section performs a full-document conversion or capture'
}
Write-Output '[PASS] same-Section selection checks its pin without converting or recapturing the full document'
if ($quickRefresh -match 'document_revision') {
    throw 'BATTLE_QUICK_MODE_FAIL: navigation compares the whole-script revision instead of the pinned Scene1 Section'
}
if ($quickRefresh.IndexOf('CaptureEditorDocumentSnapshot', [System.StringComparison]::Ordinal) -lt
    $quickRefresh.IndexOf('if (freshSectionRevision == ActiveSession().quick_mode_section_revision)', [System.StringComparison]::Ordinal)) {
    throw 'BATTLE_QUICK_MODE_FAIL: same-source quick refresh captures the full script before checking the pinned Section revision'
}
Write-Output '[PASS] unrelated Section content changes do not invalidate the quick-mode source baseline'
Assert-FunctionContains $resolverSource 'BattleQuickModeResolution ResolveScene1BattleSection(' 'BuildBattlePreviewInput\(std::move\(document_snapshot\)\)' 'Scene1 resolution converts and classifies the moved complete script once'
Assert-FunctionContains $resolverSource 'BattleQuickModeResolution ResolveScene1BattleSection(' 'raw_section_at_path\(\s*capture\.input\.nodes,\s*candidate\.section_path\s*\)' 'each candidate Section is resolved by its path inside the shared conversion'
Assert-FunctionContains $resolverSource 'bool has_valid_battle_map(' 'section_nodes\{raw_section\}' 'candidate map validation parses only that Section subtree'
Assert-FunctionContains $resolverSource 'BattleQuickModeResolution ResolveScene1BattleSection(' 'has_valid_battle_map\(capture,\s*\*raw_section,\s*candidate\.scope_key' 'each Scene1 Section is parsed against the shared conversion using its own scope'
Assert-FunctionContains $resolverSource 'bool SelectCapturedBattleSection(' 'CapturedEditorTree document_snapshot' 'Section rebinding accepts transferable document ownership'
Assert-FunctionContains $resolverSource 'bool SelectCapturedBattleSection(' 'selected_snapshot = std::move\(document_snapshot\)' 'Section rebinding avoids a second full-document copy'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::LoadQuickModeBaselinePreservingDraft(' 'LoadCapturedBattle\(std::move\(capture\), sourceTreeWindow\)' 'resolved quick-mode capture is reused without a second full conversion'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::LoadQuickModeBaselinePreservingDraft(' 'RebaseBattleEditorDraft' 'ordinary source edits preserve compatible uncommitted battle layouts'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::LoadQuickModeBaselinePreservingDraft(' 'operations->CanUndo\(\)' 'only workspaces with real temporary edit history are rebased'
Assert-Contains $pageSource 'LoadCapturedBattle\(std::move\(freshCapture\), tree\.GetSafeHwnd\(\)\)' 'writeback and undo derive the quick-mode Section revision from the capture loaded as the new baseline'
Assert-Contains $pageSource 'quick_mode_section_revision = capture.input.section_revision' 'loading a committed capture refreshes the pinned quick-mode Section revision'
Assert-FunctionContains $writebackSource 'bool BuildBattleWritebackSectionSnapshot(' 'const bool revision_mismatch =\s*revision_scope == BattleWritebackRevisionScope::WholeDocument &&\s*\(baseline\.input\.document_revision != current\.input\.document_revision \|\|\s*baseline\.input\.section_revision != current\.input\.section_revision\);' 'only whole-document writes require an unchanged document and Section revision; pinned Section writes can reconcile live edits'
Assert-FunctionContains $viewSource 'void CcczEditor2View::OnInitialUpdate(' 'RefreshScenePreview\(\)' 'opening a script triggers its first preview load without a selection change'
Assert-FunctionContains $viewSource 'void CcczEditor2View::OnEditModify()' 'RefreshScenePreview\(\)' 'editing a script instruction refreshes the bound Scene1 preview'
Assert-FunctionContains $pageSource 'void CSBattlePreviewPage::OnQuickModeClicked(' 'SetBattleQuickModeEnabled' 'quick-mode toggle routes through the preview host'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::ConfirmDiscardSessionChanges(' 'MB_DEFBUTTON2' 'quick-mode transitions default to keeping current edits'
Assert-FunctionContains $pageSource 'bool CSBattlePreviewPage::SetQuickModeEnabled(' 'm_quickModeButton\.SetCheck' 'declining a mode transition restores the prior checkbox state'
$modeToggle = Get-FunctionBlock $pageSource 'bool CSBattlePreviewPage::SetQuickModeEnabled('
if ($modeToggle.IndexOf('WriteProfileInt', [System.StringComparison]::Ordinal) -lt
    $modeToggle.IndexOf('if (!accepted)', [System.StringComparison]::Ordinal)) {
    throw 'BATTLE_QUICK_MODE_FAIL: a rejected mode transition persists the requested setting'
}
Write-Output '[PASS] rejected mode transitions do not persist the new setting'
Assert-FunctionContains $hostSource 'bool CScenePreviewWnd::CanCloseDocument(' 'm_sBattleSessions\.Find\(documentId\)' 'close checks the requested S document session, including inactive documents'
Assert-FunctionContains $documentSource 'BOOL CcczEditor2Doc::SaveModified()' 'CanCloseDocument' 'single-document close routes preview confirmation through SaveModified'
Assert-FunctionContains $documentSource 'void CcczEditor2Doc::OnCloseDocument()' 'CloseSBattleDocument' 'actual document close evicts only its S session'
Assert-FunctionContains $viewSource 'void CcczEditor2View::OnDestroy(' 'EditorTreeDestroyed' 'destroying an unrelated view does not clear the pinned S preview'
Assert-FunctionContains $viewSource 'void CcczEditor2View::OnActivateView(' 'ActivateScriptPreview\(' 'activating an existing script revalidates or restores its document-owned preview session'

Write-Output 'BATTLE_QUICK_MODE_PASS'
