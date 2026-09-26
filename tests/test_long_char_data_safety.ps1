param(
    [string]$SourceRoot = (Join-Path (Join-Path $PSScriptRoot '..') 'cczEditor2')
)

$viewPath = Join-Path $SourceRoot 'cczEditor2View.cpp'
$docPath = Join-Path $SourceRoot 'cczEditor2Doc.cpp'
$docHeaderPath = Join-Path $SourceRoot 'cczEditor2Doc.h'
$helperPath = Join-Path $SourceRoot 'LongCharData.h'

foreach ($path in @($viewPath, $docPath, $docHeaderPath, $helperPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "LONG_CHAR_DATA_TEST_FAIL: missing $path"
    }
}

$view = Get-Content -LiteralPath $viewPath -Raw
$doc = Get-Content -LiteralPath $docPath -Raw
$docHeader = Get-Content -LiteralPath $docHeaderPath -Raw
$helper = Get-Content -LiteralPath $helperPath -Raw

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "LONG_CHAR_DATA_TEST_FAIL: $Message"
    }
}

function Assert-NotContains([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) {
        throw "LONG_CHAR_DATA_TEST_FAIL: $Message"
    }
}

$txtExport = [regex]::Match($doc, '(?s)void CcczEditor2Doc::recur_export\(.*?\n\}')
if (-not $txtExport.Success) {
    throw 'LONG_CHAR_DATA_TEST_FAIL: TXT export function is missing'
}
Assert-NotContains $txtExport.Value 'char\s+tmp\[3000\]' 'TXT export still uses the fixed 3000-byte stack buffer'
Assert-Contains $txtExport.Value 'std::vector<char>' 'TXT export does not allocate according to the encoded length'
Assert-Contains $txtExport.Value 'written\s*-\s*1' 'TXT export still writes the trailing NUL byte'

Assert-Contains $docHeader 'long_char_data_capacity' 'ItemData does not track the actual text-buffer capacity'
Assert-Contains $helper 'ReplaceLongCharData' 'replacement helper is missing'
Assert-Contains $helper 'ReleaseLongCharData' 'release helper is missing'
Assert-Contains $view 'std::vector<wchar_t> decoded' 'parser still writes conversion output directly into ItemData'
Assert-Contains $view 'decoded\.data\(\),\s*wlen' 'parser conversion does not use a bounded temporary buffer'
Assert-Contains $view 'me->long_char_data_capacity' 'parser does not record the actual replacement capacity'
Assert-Contains $doc 'newData->long_char_data_capacity' 'AddInstruction does not update capacity ownership'
Assert-Contains $doc 'data->long_char_data_capacity' 'HTTP modification does not update capacity ownership'

# The wrap-assist byte threshold is a per-line setting.  It must not be able
# to create a value that the editor later rejects because the UTF-16 text
# buffer has a smaller whole-value contract.  The dialog therefore needs both
# an input limit and a capacity-aware wrap path.
Assert-Contains $view 'ApplyAutoWrapForStorage' 'wrap dialogs do not route through the storage-capacity guard'
Assert-Contains $view 'wrapped\.GetLength\(\)[\s\S]{0,120}kLongCharDataMaxLength' 'wrap path does not guard the whole UTF-16 value length'
Assert-Contains $view 'SetLimitText\(\s*static_cast<UINT>\(ccz::editor::kLongCharDataMaxLength\)' 'wrap dialogs do not constrain input to the storage contract'
$limitCalls = ([regex]::Matches($view, 'SetLongTextInputLimit\(edit1\)')).Count
if ($limitCalls -lt 3) {
    throw "LONG_CHAR_DATA_TEST_FAIL: only $limitCalls of the three long-text dialogs constrain input length"
}
Assert-NotContains $view 'long_char_data\s*=\s*new\s+wchar_t\[' 'view still creates untracked text buffers'
Assert-NotContains $doc 'long_char_data\s*=\s*new\s+wchar_t\[' 'document code still creates untracked text buffers'
Assert-NotContains $view 'wcscpy_s\([^\r\n]*long_char_data' 'view still writes text with a hard-coded buffer size'
Assert-NotContains $doc 'wcscpy_s\([^\r\n]*long_char_data' 'document code still writes text with a hard-coded buffer size'
Assert-NotContains $view 'delete\s+\w*->long_char_data' 'view still uses scalar delete for text buffers'
Assert-NotContains $doc 'delete\s+\w*->long_char_data' 'document code still uses scalar delete for text buffers'

'LONG_CHAR_DATA_SAFETY_PASS'
