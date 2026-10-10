#include "DialogueOverlayRenderer.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cstring>
#include "../PreviewPageFont.h"
#include <ccz/preview/text_color.h>

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
               const Gdiplus::RectF& bounds, const std::vector<std::uint32_t>& palette) {
    if (text.empty()) {
        return;
    }
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    format.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    const auto runs=ccz::preview::parse_color_text(text);
    float x=bounds.X,y=bounds.Y,line=font.GetHeight(&graphics)+2;
    for(const auto& run:runs) {
        std::uint32_t rgb=run.color<0?0:(palette.size()==256?palette:ccz::preview::standard_text_palette()).at(run.color);
        Gdiplus::SolidBrush colored(Gdiplus::Color(255,(rgb>>16)&255,(rgb>>8)&255,rgb&255));
        for(std::size_t index=0;index<run.text.size();++index) {
            wchar_t c=run.text[index];if(c==L'\r') continue;
            if(c==L'\n') {x=bounds.X;y+=line;continue;}
            std::wstring glyph(1,c);
            if(c>=0xD800 && c<=0xDBFF && index+1<run.text.size()) glyph+=run.text[++index];
            Gdiplus::RectF measured;
            graphics.MeasureString(glyph.c_str(),(INT)glyph.size(),&font,Gdiplus::PointF(0,0),&format,&measured);
            if(x>bounds.X && x+measured.Width>bounds.GetRight()) {x=bounds.X;y+=line;}
            if(y+line>bounds.GetBottom()+2) return;
            graphics.DrawString(glyph.c_str(),(INT)glyph.size(),&font,Gdiplus::PointF(x,y),&format,run.color<0?&brush:&colored);
            x+=measured.Width;
        }
    }
}

void draw_portrait(Gdiplus::Graphics& graphics,
                   const std::vector<std::uint8_t>& encoded_image,
                   const Gdiplus::Rect& bounds, bool dim=false, bool preserveAspect=false) {
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
        auto area=bounds;
        if(preserveAspect && image.GetWidth() && image.GetHeight()) {
            double scale=(std::min)((double)bounds.Width/image.GetWidth(),(double)bounds.Height/image.GetHeight());
            area.Width=(int)(image.GetWidth()*scale);area.Height=(int)(image.GetHeight()*scale);
            area.X=bounds.X+(bounds.Width-area.Width)/2;area.Y=bounds.Y+bounds.Height-area.Height;
        }
        if(dim) {
            Gdiplus::ColorMatrix matrix={0.35f,0,0,0,0,0,0.35f,0,0,0,0,0,0.35f,0,0,0,0,0,1,0,0,0,0,0,1};
            Gdiplus::ImageAttributes attributes;attributes.SetColorMatrix(&matrix);
            graphics.DrawImage(&image,area,0,0,(INT)image.GetWidth(),(INT)image.GetHeight(),Gdiplus::UnitPixel,&attributes);
        } else graphics.DrawImage(&image, area);
    }
    stream->Release();
}

}  // namespace

bool CalculateDialogueOverlayLayout(const CRect& client,
                                    OverlayLayout& result) {
    result = {};
    const int width=client.Width(),height=client.Height();
    const float scale=(std::min)(1.0f,(std::min)(width/640.0f,height/600.0f));
    if(width<80 || height<80) return false;
    result.text_scale=scale;
    const auto scaled=[scale](int value){return (std::max)(1,static_cast<int>(value*scale+0.5f));};
    const int margin=scaled(kOuterMargin),padding=scaled(kInnerPadding),portrait=scaled(kPortraitSize);
    const int panelHeight=scaled(kMinimumPanelHeight+42);
    result.panel=CRect(client.left+margin,client.bottom-margin-panelHeight,client.right-margin,client.bottom-margin);
    result.portrait=CRect(result.panel.left+padding,result.panel.top+padding,result.panel.left+padding+portrait,result.panel.top+padding+portrait);
    const int textLeft=result.portrait.right+padding,textRight=result.panel.right-padding;
    result.speaker=CRect(textLeft,result.panel.top+padding,textRight,result.panel.top+padding+scaled(26));
    result.body=CRect(textLeft,result.panel.top+padding+scaled(28),textRight,result.panel.bottom-padding-scaled(20));
    result.diagnostic=CRect(textLeft,result.panel.bottom-scaled(22),textRight,result.panel.bottom-padding);
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

RSceneViewport CalculateRSceneViewport(const CRect& client, int player_height) {
    RSceneViewport result;
    if (client.Width() <= 0 || client.Height() <= 0) return result;
    OverlayLayout overlay;
    int height = client.Height();
    if (CalculateDialogueOverlayLayout(client, overlay))
        height = (std::max)(1, static_cast<int>(overlay.panel.top - client.top) - player_height);
    result.scale = (std::min)(2.0f, (std::min)(client.Width()/640.0f, height/400.0f));
    const int width = static_cast<int>(640*result.scale);
    const int scene_height = static_cast<int>(400*result.scale);
    const int x = client.left+(client.Width()-width)/2;
    const int y = client.top+(height-scene_height)/2;
    result.bounds = CRect(x,y,x+width,y+scene_height);
    return result;
}

bool CalculateHalfBodyOverlayLayout(const CRect& scene, OverlayLayout& result) {
    result = {};
    if(scene.Width() <= 0 || scene.Height() <= 0) return false;
    auto rect = [&scene](int l, int t, int r, int b) {
        return CRect(scene.left+l*scene.Width()/640, scene.top+t*scene.Height()/400,
                     scene.left+r*scene.Width()/640, scene.top+b*scene.Height()/400);
    };
    result.panel=rect(20,316,620,400);
    result.speaker=rect(30,320,610,342);
    result.body=rect(30,342,610,398);
    result.diagnostic=rect(30,382,610,398);
    for(int i=0;i<3;++i) result.half_bodies[i]=rect(32+i*192,44,224+i*192,316);
    result.game_half_body=true;
    result.text_scale=static_cast<float>(scene.Width())/640.0f;
    return !result.panel.IsRectEmpty();
}

bool CalculateHalfBodyDialogueLayout(const CRect& scene,const CRect& client,OverlayLayout& result) {
    const bool hasDialogue=CalculateDialogueOverlayLayout(client,result);
    OverlayLayout figures;
    if(!CalculateHalfBodyOverlayLayout(scene,figures)) return false;
    result.half_bodies=figures.half_bodies;result.game_half_body=true;
    if(hasDialogue) result.speaker.left=result.body.left=result.diagnostic.left=result.panel.left+static_cast<int>(kInnerPadding*result.text_scale+0.5f);
    return true;
}

void PaintDialogueOverlay(
    CDC& dc, const OverlayLayout& layout,
    const ccz::preview::DialogueOverlayModel& model) {
    if (dc.GetSafeHdc() == nullptr) {
        return;
    }
    Gdiplus::Graphics graphics(dc.m_hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    if(model.visible && model.half_body && layout.game_half_body) {
        for(int slot=0;slot<3;++slot) {
            const auto& bounds=layout.half_bodies[slot];
            Gdiplus::Rect area(bounds.left,bounds.top,bounds.Width(),bounds.Height());
            if(slot<(int)model.half_body_images.size() && !model.half_body_images[slot].empty())
                draw_portrait(graphics,model.half_body_images[slot],area,!(model.half_body_mask&(4u>>slot)),true);
        }
    }
    if(layout.panel.IsRectEmpty()) return;
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

    Gdiplus::Font speaker_font(L"Microsoft YaHei", 15.0f*layout.text_scale,
                               Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::Font body_font(L"Microsoft YaHei", 13.0f*layout.text_scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font diagnostic_font(L"Microsoft YaHei", (float)PreviewUiPointSize()*layout.text_scale);
    Gdiplus::SolidBrush speaker_brush(Gdiplus::Color(255, 255, 225, 170));
    auto palette=model.text_palette.size()==256?model.text_palette:ccz::preview::standard_text_palette();
    auto bodyColor=model.base_text_color>=0 && model.base_text_color<256?palette[model.base_text_color]:0xf5f5f5;
    Gdiplus::SolidBrush body_brush(Gdiplus::Color(255,(bodyColor>>16)&255,(bodyColor>>8)&255,bodyColor&255));
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
    const auto scaled=[&layout](int value){return (std::max)(1,static_cast<int>(value*layout.text_scale+0.5f));};
    const int plain_left=layout.panel.left+scaled(kInnerPadding),plain_right=layout.panel.right-scaled(kInnerPadding);
    const CRect plain_body(plain_left,layout.panel.top+scaled(kInnerPadding),plain_right,layout.panel.bottom-scaled(kInnerPadding)-scaled(20));
    const CRect plain_diagnostic(plain_left,layout.panel.bottom-scaled(22),plain_right,layout.panel.bottom-scaled(kInnerPadding));
    if (model.visible && model.show_speaker) {
        draw_text(graphics, model.speaker_label, speaker_font, speaker_brush,
                  to_rectf(layout.speaker),model.text_palette);
    }
    if (model.visible) {
        draw_text(graphics, model.body, body_font, body_brush,
                  to_rectf((model.show_portrait || layout.game_half_body) ? layout.body : plain_body),model.text_palette);
    }
    const Gdiplus::Brush& diagnostic_brush = model.inactive_sub_event
        ? static_cast<const Gdiplus::Brush&>(inactive_diagnostic_brush)
        : static_cast<const Gdiplus::Brush&>(normal_diagnostic_brush);
    draw_text(graphics, model.diagnostic, diagnostic_font, diagnostic_brush,
              to_rectf((model.show_portrait || layout.game_half_body) ? layout.diagnostic
                                            : plain_diagnostic),model.text_palette);
}

}  // namespace ccz::editor_preview
