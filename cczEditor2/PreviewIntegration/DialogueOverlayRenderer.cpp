#include "DialogueOverlayRenderer.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

namespace ccz::editor_preview {
namespace {

constexpr int kPortraitSize = 120;
constexpr int kOuterMargin = 8;
constexpr int kInnerPadding = 8;
constexpr int kMinimumPanelHeight = kPortraitSize + kInnerPadding * 2;
constexpr int kMinimumTextWidth = 1;
constexpr int kMinimumPanelWidth = kInnerPadding + kPortraitSize +
                                   kInnerPadding + kMinimumTextWidth +
                                   kInnerPadding;

void draw_text(Gdiplus::Graphics& graphics, const std::wstring& text,
               const Gdiplus::Font& font, const Gdiplus::Brush& brush,
               const Gdiplus::RectF& bounds) {
    if (text.empty()) {
        return;
    }
    Gdiplus::StringFormat format;
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font,
                        bounds, &format, &brush);
}

void draw_portrait(Gdiplus::Graphics& graphics,
                   const std::vector<std::uint8_t>& encoded_image,
                   const Gdiplus::Rect& bounds) {
    if (encoded_image.empty()) {
        return;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, encoded_image.size());
    if (memory == nullptr) {
        return;
    }
    void* destination = GlobalLock(memory);
    if (destination == nullptr) {
        GlobalFree(memory);
        return;
    }
    std::memcpy(destination, encoded_image.data(), encoded_image.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK) {
        GlobalFree(memory);
        return;
    }
    Gdiplus::Image image(stream);
    if (image.GetLastStatus() == Gdiplus::Ok) {
        graphics.DrawImage(&image, bounds);
    }
    stream->Release();
}

}  // namespace

bool CalculateDialogueOverlayLayout(const CRect& client,
                                    OverlayLayout& result) {
    result = {};
    const int width = client.Width();
    const int height = client.Height();
    if (width < kMinimumPanelWidth + kOuterMargin * 2 ||
        height < kMinimumPanelHeight + kOuterMargin * 2) {
        return false;
    }

    const int panel_height = (std::min)(kMinimumPanelHeight + 42,
                                        height - kOuterMargin * 2);
    result.panel = CRect(client.left + kOuterMargin,
                         client.bottom - kOuterMargin - panel_height,
                         client.right - kOuterMargin,
                         client.bottom - kOuterMargin);
    result.portrait = CRect(result.panel.left + kInnerPadding,
                            result.panel.top + kInnerPadding,
                            result.panel.left + kInnerPadding + kPortraitSize,
                            result.panel.top + kInnerPadding + kPortraitSize);
    const int text_left = result.portrait.right + kInnerPadding;
    const int text_right = result.panel.right - kInnerPadding;
    const int text_width = (std::max)(1, text_right - text_left);
    result.speaker = CRect(text_left, result.panel.top + kInnerPadding,
                           text_left + text_width,
                           result.panel.top + kInnerPadding + 26);
    result.body = CRect(text_left, result.panel.top + kInnerPadding + 28,
                        text_left + text_width,
                        result.panel.bottom - kInnerPadding - 20);
    result.diagnostic = CRect(text_left, result.panel.bottom - 22,
                              text_left + text_width,
                              result.panel.bottom - kInnerPadding);
    return result.panel.left >= client.left && result.panel.top >= client.top &&
           result.panel.right <= client.right && result.panel.bottom <= client.bottom &&
           result.portrait.left >= client.left &&
           result.portrait.top >= client.top &&
           result.portrait.right <= client.right &&
           result.portrait.bottom <= client.bottom &&
           result.speaker.left >= client.left &&
           result.speaker.top >= client.top &&
           result.speaker.right <= client.right &&
           result.speaker.bottom <= client.bottom &&
           result.body.left >= client.left &&
           result.body.top >= client.top &&
           result.body.right <= client.right &&
           result.body.bottom <= client.bottom &&
           result.diagnostic.left >= client.left &&
           result.diagnostic.top >= client.top &&
           result.diagnostic.right <= client.right &&
           result.diagnostic.bottom <= client.bottom;
}

void PaintDialogueOverlay(
    CDC& dc, const OverlayLayout& layout,
    const ccz::preview::DialogueOverlayModel& model) {
    if (dc.GetSafeHdc() == nullptr || layout.panel.IsRectEmpty()) {
        return;
    }
    Gdiplus::Graphics graphics(dc.m_hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    const Gdiplus::Rect panel(layout.panel.left, layout.panel.top,
                              layout.panel.Width(), layout.panel.Height());
    Gdiplus::SolidBrush panel_brush(Gdiplus::Color(255, 20, 20, 24));
    // Always fill the entire panel, including the hidden state, so a stale
    // portrait/body from the previous frame cannot remain after a branch clear.
    graphics.FillRectangle(&panel_brush, panel);
    if (!model.visible && model.diagnostic.empty()) {
        return;
    }

    if (model.visible) {
        Gdiplus::Pen panel_pen(Gdiplus::Color(245, 210, 185, 120), 1.0f);
        graphics.DrawRectangle(&panel_pen, panel);
    }
    const Gdiplus::Rect portrait(layout.portrait.left, layout.portrait.top,
                                 layout.portrait.Width(), layout.portrait.Height());
    if (model.visible && model.show_portrait) {
        Gdiplus::SolidBrush portrait_background(Gdiplus::Color(180, 8, 8, 8));
        graphics.FillRectangle(&portrait_background, portrait);
        draw_portrait(graphics, model.portrait_image, portrait);
    }

    Gdiplus::Font speaker_font(L"Microsoft YaHei", 15.0f,
                               Gdiplus::FontStyleBold);
    Gdiplus::Font body_font(L"Microsoft YaHei", 13.0f);
    Gdiplus::Font diagnostic_font(L"Microsoft YaHei", 9.0f);
    Gdiplus::SolidBrush speaker_brush(Gdiplus::Color(255, 255, 225, 170));
    Gdiplus::SolidBrush body_brush(Gdiplus::Color(255, 245, 245, 245));
    Gdiplus::SolidBrush normal_diagnostic_brush(
        Gdiplus::Color(255, 255, 150, 120));
    Gdiplus::SolidBrush inactive_diagnostic_brush(
        Gdiplus::Color(255, 235, 70, 70));

    const auto to_rectf = [](const CRect& rect) {
        return Gdiplus::RectF(static_cast<Gdiplus::REAL>(rect.left),
                              static_cast<Gdiplus::REAL>(rect.top),
                              static_cast<Gdiplus::REAL>(rect.Width()),
                              static_cast<Gdiplus::REAL>(rect.Height()));
    };
    const int plain_left = layout.panel.left + kInnerPadding;
    const int plain_right = layout.panel.right - kInnerPadding;
    const CRect plain_body(
        plain_left, layout.panel.top + kInnerPadding, plain_right,
        layout.panel.bottom - kInnerPadding - 20);
    const CRect plain_diagnostic(
        plain_left, layout.panel.bottom - 22, plain_right,
        layout.panel.bottom - kInnerPadding);
    if (model.visible && model.show_speaker) {
        draw_text(graphics, model.speaker_label, speaker_font, speaker_brush,
                  to_rectf(layout.speaker));
    }
    if (model.visible) {
        draw_text(graphics, model.body, body_font, body_brush,
                  to_rectf(model.show_portrait ? layout.body : plain_body));
    }
    const Gdiplus::Brush& diagnostic_brush = model.inactive_sub_event
        ? static_cast<const Gdiplus::Brush&>(inactive_diagnostic_brush)
        : static_cast<const Gdiplus::Brush&>(normal_diagnostic_brush);
    draw_text(graphics, model.diagnostic, diagnostic_font, diagnostic_brush,
              to_rectf(model.show_portrait ? layout.diagnostic
                                            : plain_diagnostic));
}

}  // namespace ccz::editor_preview
