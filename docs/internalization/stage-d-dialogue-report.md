# Stage D：对白资产与 Overlay 验收记录

日期：2026-09-19

## 自动化结果

- `tests/test_dialogue_overlay_integration.ps1`：`DIALOGUE_OVERLAY_INTEGRATION_PASS`。
- Fix round 1 后 fixture 扩展为 10 项，并额外验证 Session diagnostic、identical-input repaint decision、invalid-state clear decision、窄/矮 geometry；同时只对 `ScenePreviewWnd` 做源接线检查，未声称无 MFC OnPaint 端到端。
- `Debug/cczPreviewCoreTests.exe dialogue`：全部 dialogue profile/model/replay 用例通过。
- `Debug/cczPreviewCoreTests.exe portrait`：全部 portrait resolver/overlay 用例通过。
- `tests/test_scene_background_preview.ps1`：`SCENE_BACKGROUND_TEST_PASS`。
- Release x86 solution build：0 errors；产物已同步到 `../../ccz_preview_ext/staging-test/ccz-editor-internalization-working/s.exe`，SHA-256：`3596B05BEB42EEFB236240CA9E6256F947AA5FD8FCF2F3E0741FC9CF104FE17E`。

集成 fixture 覆盖了 Mod root 切换清空旧资产、缺少 `Data.e5` 时保留对白并报告诊断、缺少 `Face.e5` 时仅去除头像、相同 model 不触发重绘、隐藏 model 清理旧头像并留下不透明面板，以及 layout 不越出 scene canvas。

## 手测状态

标准对白、`&*.XXX`、改名角色、0x16 信息、0x69 旁白、0x2C 地图文本的真实窗口手测统一延期到 Task12，遵从 Task7 的验收安排；本记录不虚报手测结果。

## 边界

旧 `ccz-editor-three-review-fixes-20260918` staging 未修改。Git identity 未配置，因此未创建 commit。
