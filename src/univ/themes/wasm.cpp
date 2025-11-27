///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/themes/wasm.cpp
// Purpose:     wxUniversal theme for WASM
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
///////////////////////////////////////////////////////////////////////////////

// ===========================================================================
// declarations
// ===========================================================================

// ---------------------------------------------------------------------------
// headers
// ---------------------------------------------------------------------------

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#include "wx/univ/theme.h"

#if wxUSE_THEME_WASM

#ifndef WX_PRECOMP
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/dcmemory.h"
    #include "wx/dcclient.h"
    #include "wx/window.h"

    #include "wx/menu.h"

    #include "wx/bmpbuttn.h"
    #include "wx/button.h"
    #include "wx/checkbox.h"
    #include "wx/listbox.h"
    #include "wx/checklst.h"
    #include "wx/combobox.h"
    #include "wx/scrolbar.h"
    #include "wx/slider.h"
    #include "wx/textctrl.h"
    #include "wx/toolbar.h"
    #include "wx/statusbr.h"

    #include "wx/settings.h"
    #include "wx/toplevel.h"
    #include "wx/image.h"
#endif // WX_PRECOMP

#include "wx/notebook.h"
#include "wx/spinbutt.h"
#include "wx/artprov.h"
#include "wx/tglbtn.h"

#include "wx/univ/stdrend.h"
#include "wx/univ/inpcons.h"
#include "wx/univ/inphand.h"
#include "wx/univ/colschem.h"

class wxWasmMenuGeometryInfo;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// standard border size
static const int BORDER_THICKNESS = 2;

static const int CHECK_WIDTH = 8;
static const int CHECK_HEIGHT = 8;

// ----------------------------------------------------------------------------
// wxWasmRenderer: draw the GUI elements in Wasm style
// ----------------------------------------------------------------------------

class wxWasmRenderer : public wxStdRenderer
{
public:
    wxWasmRenderer(const wxColourScheme *scheme);

    // wxRenderer methods
    virtual void DrawFocusRect(wxWindow* win,
                               wxDC& dc,
                               const wxRect& rect,
                               int flags = 0) wxOVERRIDE;
    virtual void DrawTextBorder(wxDC& dc,
                                wxBorder border,
                                const wxRect& rect,
                                int flags = 0,
                                wxRect *rectIn = NULL) wxOVERRIDE;
    virtual void DrawButtonSurface(wxDC& dc,
                                   const wxColour& col,
                                   const wxRect& rect,
                                   int flags) wxOVERRIDE;
    virtual void DrawButtonLabel(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& image,
                                 const wxRect& rect,
                                 int flags,
                                 int alignment,
                                 int indexAccel,
                                 wxRect *rectBounds) wxOVERRIDE;
    virtual void DrawButtonBorder(wxDC& dc,
                                  const wxRect& rect,
                                  int flags = 0,
                                  wxRect *rectIn = NULL) wxOVERRIDE;
    virtual void DrawArrow(wxDC& dc,
                           wxDirection dir,
                           const wxRect& rect,
                           int flags = 0) wxOVERRIDE;
    virtual void DrawMenuArrow(wxDC& dc,
                              const wxRect& rect,
                              int flags = 0);
    virtual void DrawScrollbarArrow(wxDC& dc,
                                    wxDirection dir,
                                    const wxRect& rect,
                                    int flags = 0) wxOVERRIDE;
    virtual void DrawScrollbarThumb(wxDC& dc,
                                    wxOrientation orient,
                                    const wxRect& rect,
                                    int flags = 0) wxOVERRIDE;
    virtual void DrawScrollbarShaft(wxDC& dc,
                                    wxOrientation orient,
                                    const wxRect& rect,
                                    int flags = 0) wxOVERRIDE;

#if wxUSE_TOOLBAR
    virtual void DrawToolBarButton(wxDC& dc,
                                   const wxString& label,
                                   const wxBitmap& bitmap,
                                   const wxRect& rect,
                                   int flags = 0,
                                   long style = 0,
                                   int tbarStyle = 0) wxOVERRIDE;
#endif // wxUSE_TOOLBAR

#if wxUSE_TEXTCTRL
    virtual void DrawLineWrapMark(wxDC& dc, const wxRect& rect) wxOVERRIDE;
#endif // wxUSE_TEXTCTRL

#if wxUSE_NOTEBOOK
    virtual void DrawTab(wxDC& dc,
                         const wxRect& rect,
                         wxDirection dir,
                         const wxString& label,
                         const wxBitmap& bitmap = wxNullBitmap,
                         int flags = 0,
                         int indexAccel = -1) wxOVERRIDE;
#endif // wxUSE_NOTEBOOK

#if wxUSE_SLIDER
    virtual void DrawSliderShaft(wxDC& dc,
                                 const wxRect& rect,
                                 double fracValue,
                                 int lenThumb,
                                 wxOrientation orient,
                                 int flags = 0,
                                 long style = 0,
                                 wxRect *rectShaft = NULL) wxOVERRIDE;
    virtual void DrawSliderThumb(wxDC& dc,
                                 const wxRect& rect,
                                 wxOrientation orient,
                                 int flags = 0,
                                 long style = 0) wxOVERRIDE;
    virtual void DrawSliderTicks(wxDC& WXUNUSED(dc),
                                 const wxRect& WXUNUSED(rect),
                                 int WXUNUSED(lenThumb),
                                 wxOrientation WXUNUSED(orient),
                                 int WXUNUSED(start),
                                 int WXUNUSED(end),
                                 int WXUNUSED(step) = 1,
                                 int WXUNUSED(flags) = 0,
                                 long WXUNUSED(style) = 0) wxOVERRIDE
    {
        // we don't have the ticks in Wasm version
    }
#endif // wxUSE_SLIDER

#if wxUSE_MENUS
    virtual void DrawMenuBarItem(wxDC& dc,
                                 const wxRect& rect,
                                 const wxString& label,
                                 int flags = 0,
                                 int indexAccel = -1) wxOVERRIDE;
    virtual void DrawMenuItem(wxDC& dc,
                              wxCoord y,
                              const wxMenuGeometryInfo& geometryInfo,
                              const wxString& label,
                              const wxString& accel,
                              const wxBitmap& bitmap = wxNullBitmap,
                              int flags = 0,
                              int indexAccel = -1) wxOVERRIDE;
    virtual void DrawMenuSeparator(wxDC& dc,
                                   wxCoord y,
                                   const wxMenuGeometryInfo& geomInfo) wxOVERRIDE;
    virtual void DrawMenuOverflowArrow(wxDC& dc,
                                   const wxRect& rect,
                                   wxDirection direction) wxOVERRIDE;
#endif // wxUSE_MENUS

    virtual void GetComboBitmaps(wxBitmap *bmpNormal,
                                 wxBitmap *bmpFocus,
                                 wxBitmap *bmpPressed,
                                 wxBitmap *bmpDisabled) wxOVERRIDE;

    virtual void AdjustSize(wxSize *size, const wxWindow *window) wxOVERRIDE;

    // geometry and hit testing
#if wxUSE_SCROLLBAR
    virtual wxSize GetScrollbarArrowSize(wxOrientation orientation) const wxOVERRIDE
        { return orientation == wxVERTICAL ? wxSize(15, 0) : wxSize(0, 15); }
#endif // wxUSE_SCROLLBAR

    virtual wxSize GetCheckBitmapSize() const wxOVERRIDE
        { return wxSize(14, 14); }
    virtual wxSize GetRadioBitmapSize() const wxOVERRIDE
        { return wxSize(15, 15); }
    virtual wxCoord GetCheckItemMargin() const wxOVERRIDE
        { return 2; }

#if wxUSE_TOOLBAR
    virtual wxSize GetToolBarButtonSize(wxCoord *separator) const wxOVERRIDE
        { if ( separator ) *separator = 5; return wxSize(16, 15); }
    virtual wxSize GetToolBarMargin() const wxOVERRIDE
        { return wxSize(6, 6); }
#endif // wxUSE_TOOLBAR

#if wxUSE_TEXTCTRL
    virtual wxRect GetTextClientArea(const wxTextCtrl *text,
                                     const wxRect& rect,
                                     wxCoord *extraSpaceBeyond) const wxOVERRIDE;
#endif // wxUSE_TEXTCTRL

#if wxUSE_NOTEBOOK
    virtual wxSize GetTabIndent() const wxOVERRIDE { return wxSize(2, 2); }
    virtual wxSize GetTabPadding() const wxOVERRIDE { return wxSize(6, 6); }
#endif // wxUSE_NOTEBOOK

#if wxUSE_SLIDER
    virtual wxCoord GetSliderDim() const wxOVERRIDE { return 21; }
    virtual wxCoord GetSliderTickLen() const wxOVERRIDE { return 0; }
    virtual wxRect GetSliderShaftRect(const wxRect& rect,
                                      int lenThumb,
                                      wxOrientation orient,
                                      long style = 0) const wxOVERRIDE;
    virtual wxSize GetSliderThumbSize(const wxRect& rect,
                                      int lenThumb,
                                      wxOrientation orient) const wxOVERRIDE;
#endif // wxUSE_SLIDER

    virtual wxSize GetProgressBarStep() const wxOVERRIDE { return wxSize(16, 32); }

#if wxUSE_MENUS
    virtual wxSize GetMenuBarItemSize(const wxSize& sizeText) const wxOVERRIDE;
    virtual wxMenuGeometryInfo *GetMenuGeometry(wxWindow *win,
                                                const wxMenu& menu) const wxOVERRIDE;
#endif // wxUSE_MENUS

    // helpers for "wxBitmap wxColourScheme::Get()"
    void DrawCheckBitmap(wxDC& dc, const wxRect& rect);
    void DrawUncheckBitmap(wxDC& dc, const wxRect& rect, bool isPressed);
    void DrawUndeterminedBitmap(wxDC& dc, const wxRect& rect, bool isPressed);

#if wxUSE_TEXTCTRL
    // return the width of the border around the text area in the text control
    virtual int GetTextBorderWidth(const wxTextCtrl *text) const wxOVERRIDE;
#endif // wxUSE_TEXTCTRL

protected:
    wxString RenderAccelString(const wxString& accel) const;

    // overridden wxStdRenderer methods
    virtual void DrawSunkenBorder(wxDC& dc, wxRect *rect) wxOVERRIDE;
    virtual void DrawStaticBorder(wxDC& dc, wxRect *rect) wxOVERRIDE;

    virtual void DrawHorizontalLine(wxDC& dc, wxCoord y, wxCoord x1, wxCoord x2) wxOVERRIDE;
    virtual void DrawVerticalLine(wxDC& dc, wxCoord x, wxCoord y1, wxCoord y2) wxOVERRIDE;

    virtual void DrawFrameWithoutLabel(wxDC& dc,
                                       const wxRect& rectFrame,
                                       const wxRect& rectLabel);

    virtual void DrawFrameWithLabel(wxDC& dc,
                                    const wxString& label,
                                    const wxRect& rectFrame,
                                    const wxRect& rectText,
                                    int flags,
                                    int alignment,
                                    int indexAccel) wxOVERRIDE;

    virtual void DrawFrame(wxDC& dc,
                           const wxString& label,
                           const wxRect& rect,
                           int flags,
                           int alignment,
                           int indexAccel) wxOVERRIDE;

    virtual void DrawCheckItemBitmap(wxDC& dc,
                                     const wxBitmap& bitmap,
                                     const wxRect& rect,
                                     int flags) wxOVERRIDE;

    // get the colour to use for background
    wxColour GetBackgroundColour(int flags) const
    {
        if ( flags & wxCONTROL_PRESSED )
            return wxSCHEME_COLOUR(m_scheme, CONTROL_PRESSED);
        else if ( flags & wxCONTROL_CURRENT )
            return wxSCHEME_COLOUR(m_scheme, CONTROL_CURRENT);
        else
            return wxSCHEME_COLOUR(m_scheme, CONTROL);
    }

    // as DrawShadedRect() but the pixels in the bottom left and upper right
    // border are drawn with the pen1, not pen2
    void DrawAntiShadedRect(wxDC& dc, wxRect *rect,
                            const wxPen& pen1, const wxPen& pen2);

    // used for drawing opened rectangles - draws only one side of it at once
    // (and doesn't adjust the rect)
    void DrawAntiShadedRectSide(wxDC& dc,
                                const wxRect& rect,
                                const wxPen& pen1,
                                const wxPen& pen2,
                                wxDirection dir);

    void DrawShadedRect(wxDC& dc, wxRect *rect,
                        const wxPen& pen1, const wxPen& pen2);

    // draw an opened rect for the arrow in given direction
    void DrawArrowBorder(wxDC& dc,
                         wxRect *rect,
                         wxDirection dir);

    // draw two sides of the rectangle
    void DrawThumbBorder(wxDC& dc,
                         wxRect *rect,
                         wxOrientation orient);

    // just as DrawRaisedBorder() except that the bottom left and up right
    // pixels of the interior rect are drawn in another colour (i.e. the inner
    // rect is drawn with DrawAntiShadedRect() and not DrawShadedRect())
    void DrawAntiRaisedBorder(wxDC& dc, wxRect *rect);

    // draw inner Wasm shadow
    void DrawInnerShadedRect(wxDC& dc, wxRect *rect);

    // get the line wrap indicator bitmap
    wxBitmap GetLineWrapBitmap() const;

    virtual wxBitmap GetCheckBitmap(int flags) wxOVERRIDE;
    virtual wxBitmap GetRadioBitmap(int flags) wxOVERRIDE;

    // draw a /\ or \/ line from (x1, y1) to (x2, y1) passing by the point
    // ((x1 + x2)/2, y2)
    void DrawUpZag(wxDC& dc,
                   wxCoord x1, wxCoord x2,
                   wxCoord y1, wxCoord y2);
    void DrawDownZag(wxDC& dc,
                     wxCoord x1, wxCoord x2,
                     wxCoord y1, wxCoord y2);

    void DrawCheck(wxDC& dc, const wxRect& rect);

    // draw the radio button bitmap for the given state
    void DrawRadioButtonBitmap(wxDC& dc, const wxRect& rect, int flags);

    // common part of DrawMenuItem() and DrawMenuBarItem()
    void DoDrawMenuItem(wxDC& dc,
                        const wxRect& rect,
                        const wxString& label,
                        int flags,
                        int indexAccel,
                        const wxString& accel = wxEmptyString,
                        const wxBitmap& bitmap = wxNullBitmap,
                        const wxWasmMenuGeometryInfo *geometryInfo = NULL);

    // initialize the combo bitmaps
    void InitComboBitmaps();

    virtual wxBitmap GetFrameButtonBitmap(FrameButtonType WXUNUSED(type)) wxOVERRIDE
    {
        return wxNullBitmap;
    }

private:
    // data
    wxSize m_sizeScrollbarArrow;

    // GDI objects
    wxPen m_penGrey;
    wxPen m_penMediumGrey;

    // the checkbox and radio button bitmaps: first row is for the normal,
    // second for the pressed state and the columns are for checked, unchecked
    // and undeterminated respectively
    wxBitmap m_bitmapsCheckbox[IndicatorState_MaxCtrl][IndicatorStatus_Max],
             m_bitmapsRadiobtn[IndicatorState_MaxCtrl][IndicatorStatus_Max];

    // the line wrap bitmap (drawn at the end of wrapped lines)
    wxBitmap m_bmpLineWrap;

    // the combobox bitmaps
    enum
    {
        ComboState_Normal,
        ComboState_Focus,
        ComboState_Pressed,
        ComboState_Disabled,
        ComboState_Max
    };

    wxBitmap m_bitmapsCombo[ComboState_Max];
};

// ----------------------------------------------------------------------------
// wxWasmInputHandler and derived classes: process the keyboard and mouse
// messages according to Wasm standards
// ----------------------------------------------------------------------------

class wxWasmInputHandler : public wxInputHandler
{
public:
    wxWasmInputHandler() { }

    virtual bool HandleKey(wxInputConsumer *control,
                           const wxKeyEvent& event,
                           bool pressed);
    virtual bool HandleMouse(wxInputConsumer *control,
                             const wxMouseEvent& event);
    virtual bool HandleMouseMove(wxInputConsumer *control,
                                 const wxMouseEvent& event);
};

#if wxUSE_SCROLLBAR

class wxWasmScrollBarInputHandler : public wxStdScrollBarInputHandler
{
public:
    wxWasmScrollBarInputHandler(wxRenderer *renderer, wxInputHandler *handler)
        : wxStdScrollBarInputHandler(renderer, handler) { }

protected:
    virtual void Highlight(wxScrollBar *scrollbar, bool doIt)
    {
        // only arrows and the thumb can be highlighted
        if ( !IsArrow() && m_htLast != wxHT_SCROLLBAR_THUMB )
            return;

        wxStdScrollBarInputHandler::Highlight(scrollbar, doIt);
    }

    virtual void Press(wxScrollBar *scrollbar, bool doIt)
    {
        // only arrows can be pressed
        if ( !IsArrow() )
            return;

        wxStdScrollBarInputHandler::Press(scrollbar, doIt);
    }

    // any button can be used to drag the scrollbar under Wasm+
    virtual bool IsAllowedButton(int WXUNUSED(button)) const { return true; }

    bool IsArrow() const
    {
        return m_htLast == wxHT_SCROLLBAR_ARROW_LINE_1 ||
                m_htLast == wxHT_SCROLLBAR_ARROW_LINE_2;
    }
};

#endif // wxUSE_SCROLLBAR

#if wxUSE_CHECKBOX

class wxWasmCheckboxInputHandler : public wxStdInputHandler
{
public:
    wxWasmCheckboxInputHandler(wxInputHandler *handler)
        : wxStdInputHandler(handler) { }

    virtual bool HandleKey(wxInputConsumer *control,
                           const wxKeyEvent& event,
                           bool pressed);
};

#endif // wxUSE_CHECKBOX

#if wxUSE_TEXTCTRL

class wxWasmTextCtrlInputHandler : public wxStdInputHandler
{
public:
    wxWasmTextCtrlInputHandler(wxInputHandler *handler)
        : wxStdInputHandler(handler) { }

    virtual bool HandleKey(wxInputConsumer *control,
                           const wxKeyEvent& event,
                           bool pressed);
};

#endif // wxUSE_TEXTCTRL

// ----------------------------------------------------------------------------
// wxWasmColourScheme: uses the standard Wasm colours
// ----------------------------------------------------------------------------

class wxWasmColourScheme : public wxColourScheme
{
public:
    virtual wxColour Get(StdColour col) const;
    virtual wxColour GetBackground(wxWindow *win) const;
};

// ----------------------------------------------------------------------------
// wxWasmArtProvider
// ----------------------------------------------------------------------------

class wxWasmArtProvider : public wxArtProvider
{
protected:
    virtual wxBitmap CreateBitmap(const wxArtID& id,
                                  const wxArtClient& client,
                                  const wxSize& size);
};

// ----------------------------------------------------------------------------
// wxWasmTheme
// ----------------------------------------------------------------------------

WX_DEFINE_ARRAY_PTR(wxInputHandler *, wxArrayHandlers);

class wxWasmTheme : public wxTheme
{
public:
    wxWasmTheme();
    virtual ~wxWasmTheme();

    virtual wxRenderer *GetRenderer();
    virtual wxArtProvider *GetArtProvider();
    virtual wxInputHandler *GetInputHandler(const wxString& control,
                                            wxInputConsumer *consumer);
    virtual wxColourScheme *GetColourScheme();

private:
    wxWasmRenderer *m_renderer;

    wxWasmArtProvider *m_artProvider;

    // the names of the already created handlers and the handlers themselves
    // (these arrays are synchronized)
    wxSortedArrayString m_handlerNames;
    wxArrayHandlers m_handlers;

    wxWasmColourScheme *m_scheme;

    WX_DECLARE_THEME(wasm)
};

// ============================================================================
// implementation
// ============================================================================

WX_IMPLEMENT_THEME(wxWasmTheme, wasm, wxTRANSLATE("WASM theme"));

// ----------------------------------------------------------------------------
// wxWasmTheme
// ----------------------------------------------------------------------------

wxWasmTheme::wxWasmTheme()
{
    m_scheme = NULL;
    m_renderer = NULL;
    m_artProvider = NULL;
}

wxWasmTheme::~wxWasmTheme()
{
    delete m_renderer;
    delete m_scheme;
    delete m_artProvider;
}

wxRenderer *wxWasmTheme::GetRenderer()
{
    if ( !m_renderer )
    {
        m_renderer = new wxWasmRenderer(GetColourScheme());
    }

    return m_renderer;
}

wxArtProvider *wxWasmTheme::GetArtProvider()
{
    if ( !m_artProvider )
    {
        m_artProvider = new wxWasmArtProvider;
    }

    return m_artProvider;
}

wxColourScheme *wxWasmTheme::GetColourScheme()
{
    if ( !m_scheme )
    {
        m_scheme = new wxWasmColourScheme;
    }
    return m_scheme;
}

wxInputHandler *wxWasmTheme::GetInputHandler(const wxString& control,
                                            wxInputConsumer *consumer)
{
    wxInputHandler *handler = NULL;
    int n = m_handlerNames.Index(control);
    if ( n == wxNOT_FOUND )
    {
        static wxWasmInputHandler s_handlerDef;

        wxInputHandler * const
          handlerStd = consumer->DoGetStdInputHandler(&s_handlerDef);

        // create a new handler
#if wxUSE_CHECKBOX
        if ( control == wxINP_HANDLER_CHECKBOX )
        {
            static wxWasmCheckboxInputHandler s_handler(handlerStd);

            handler = &s_handler;
        }
        else
#endif // wxUSE_CHECKBOX
#if wxUSE_SCROLLBAR
        if ( control == wxINP_HANDLER_SCROLLBAR )
        {
            static wxWasmScrollBarInputHandler s_handler(m_renderer, handlerStd);

            handler = &s_handler;
        }
        else
#endif // wxUSE_SCROLLBAR
#if wxUSE_TEXTCTRL
        if ( control == wxINP_HANDLER_TEXTCTRL )
        {
            static wxWasmTextCtrlInputHandler s_handler(handlerStd);

            handler = &s_handler;
        }
        else
#endif // wxUSE_TEXTCTRL
        {
            // no special handler for this control
            handler = handlerStd;
        }

        n = m_handlerNames.Add(control);
        m_handlers.Insert(handler, n);
    }
    else // we already have it
    {
        handler = m_handlers[n];
    }

    return handler;
}

// ============================================================================
// wxWasmColourScheme
// ============================================================================

wxColour wxWasmColourScheme::GetBackground(wxWindow *win) const
{
    wxColour col;
    if ( win->UseBgCol() )
    {
        // use the user specified colour
        col = win->GetBackgroundColour();
    }

    if ( !win->ShouldInheritColours() )
    {
        // doesn't depend on the state
        if ( !col.IsOk() )
        {
            col = Get(WINDOW);
        }
    }
    else
    {
        int flags = win->GetStateFlags();

        // the colour set by the user should be used for the normal state
        // and for the states for which we don't have any specific colours
        if ( !col.IsOk() || (flags != 0) )
        {
#if wxUSE_SCROLLBAR
            if ( wxDynamicCast(win, wxScrollBar) )
                col = Get(SCROLLBAR);
            else
#endif //wxUSE_SCROLLBAR
                 if ( (flags & wxCONTROL_CURRENT) && win->CanBeHighlighted() )
                col = Get(CONTROL_CURRENT);
            else if ( flags & wxCONTROL_PRESSED )
                col = Get(CONTROL_PRESSED);
            else
                col = Get(CONTROL);
        }
    }

    return col;
}

wxColour wxWasmColourScheme::Get(wxWasmColourScheme::StdColour col) const
{
    switch ( col )
    {
        case FRAME:
        case WINDOW:            return wxColour(0xe6e6e6);

        case SHADOW_DARK:       return wxColour(0x606060);
        case SHADOW_HIGHLIGHT:  return *wxWHITE;
        case SHADOW_IN:         return wxColour(0xd6d6d6);
        case SHADOW_OUT:        return wxColour(0x969696);

        case CONTROL:           return wxColour(0xe6e6e6);
        case CONTROL_PRESSED:   return wxColour(0xc3c3c3);
        case CONTROL_CURRENT:   return wxColour(0xeaeaea);

        case CONTROL_TEXT:      return *wxBLACK;
        case CONTROL_TEXT_DISABLED:
                                return wxColour(0x757575);
        case CONTROL_TEXT_DISABLED_SHADOW:
                                return *wxWHITE;

        case SCROLLBAR:
        case SCROLLBAR_PRESSED: return wxColour(0xa0a0a0);

        case HIGHLIGHT:         return wxColour(0xd56464);
        case HIGHLIGHT_TEXT:    return wxColour(0xffffff);

        case GAUGE:             return Get(CONTROL_CURRENT);

        case TITLEBAR:          return wxColour(0xaeaaae);
        case TITLEBAR_ACTIVE:   return wxColour(0x820300);
        case TITLEBAR_TEXT:     return wxColour(0xc0c0c0);
        case TITLEBAR_ACTIVE_TEXT:
                                return *wxWHITE;

        case DESKTOP:           return *wxBLACK;

        case MAX:
        default:
            wxFAIL_MSG(wxT("invalid standard colour"));
            return *wxBLACK;
    }
}

// ============================================================================
// wxWasmRenderer
// ============================================================================

// ----------------------------------------------------------------------------
// construction
// ----------------------------------------------------------------------------

wxWasmRenderer::wxWasmRenderer(const wxColourScheme *scheme)
             : wxStdRenderer(scheme)
{
    m_penGrey = wxPen(wxSCHEME_COLOUR(scheme, SCROLLBAR));
    m_penMediumGrey = wxPen(wxColour(182, 182, 182));
}

// ----------------------------------------------------------------------------
// border stuff
// ----------------------------------------------------------------------------

void wxWasmRenderer::DrawAntiShadedRectSide(wxDC& dc,
                                           const wxRect& rect,
                                           const wxPen& pen1,
                                           const wxPen& pen2,
                                           wxDirection dir)
{
    dc.SetPen(dir == wxLEFT || dir == wxUP ? pen1 : pen2);

    switch ( dir )
    {
        case wxLEFT:
            dc.DrawLine(rect.GetLeft(), rect.GetTop(),
                        rect.GetLeft(), rect.GetBottom() + 1);
            break;

        case wxUP:
            dc.DrawLine(rect.GetLeft(), rect.GetTop(),
                        rect.GetRight() + 1, rect.GetTop());
            break;

        case wxRIGHT:
            dc.DrawLine(rect.GetRight(), rect.GetTop(),
                        rect.GetRight(), rect.GetBottom() + 1);
            break;

        case wxDOWN:
            dc.DrawLine(rect.GetLeft(), rect.GetBottom(),
                        rect.GetRight() + 1, rect.GetBottom());
            break;

        default:
            wxFAIL_MSG(wxT("unknown rectangle side"));
    }
}

void wxWasmRenderer::DrawAntiShadedRect(wxDC& dc, wxRect *rect,
                                       const wxPen& pen1, const wxPen& pen2)
{
    // draw the rectangle
    dc.SetPen(pen1);
    dc.DrawLine(rect->GetLeft(), rect->GetTop(),
                rect->GetLeft(), rect->GetBottom());
    dc.DrawLine(rect->GetLeft() + 1, rect->GetTop(),
                rect->GetRight(), rect->GetTop());
    dc.SetPen(pen2);
    dc.DrawLine(rect->GetRight(), rect->GetTop() + 1,
                rect->GetRight(), rect->GetBottom());
    dc.DrawLine(rect->GetLeft() + 1, rect->GetBottom(),
                rect->GetRight(), rect->GetBottom());

    // adjust the rect
    rect->Inflate(-1);
}

void wxWasmRenderer::DrawShadedRect(wxDC& dc, wxRect *rect,
                                    const wxPen& pen1, const wxPen& pen2)
{
    // draw the rectangle
    dc.SetPen(pen1);
    dc.DrawLine(rect->GetLeft(), rect->GetTop(),
                rect->GetLeft(), rect->GetBottom());
    dc.DrawLine(rect->GetLeft(), rect->GetTop(),
                rect->GetRight(), rect->GetTop());
    dc.SetPen(pen2);
    dc.DrawLine(rect->GetRight(), rect->GetTop(),
                rect->GetRight(), rect->GetBottom());
    dc.DrawLine(rect->GetLeft(), rect->GetBottom(),
                rect->GetRight(), rect->GetBottom());

    // adjust the rect
    rect->Inflate(-1);
}

// ----------------------------------------------------------------------------
void wxWasmRenderer::DrawInnerShadedRect(wxDC& dc, wxRect *rect)
{
    DrawAntiShadedRect(dc, rect, m_penDarkGrey, m_penHighlight);
    DrawAntiShadedRect(dc, rect, m_penBlack, m_penHighlight);
}

void wxWasmRenderer::DrawAntiRaisedBorder(wxDC& dc, wxRect *rect)
{
    DrawShadedRect(dc, rect, m_penHighlight, m_penBlack);
    DrawAntiShadedRect(dc, rect, m_penLightGrey, m_penDarkGrey);
}

void wxWasmRenderer::DrawSunkenBorder(wxDC& dc, wxRect *rect)
{
    DrawAntiShadedRect(dc, rect, m_penDarkGrey, m_penHighlight);
    DrawShadedRect(dc, rect, m_penBlack, m_penLightGrey);
}

void wxWasmRenderer::DrawStaticBorder(wxDC& dc, wxRect *rect)
{
    DrawRect(dc, rect, m_penDarkGrey);
}

void
wxWasmRenderer::DrawFocusRect(wxWindow* WXUNUSED(win),
                              wxDC& WXUNUSED(dc),
                              const wxRect& WXUNUSED(rect),
                              int WXUNUSED(flags))
{
}

void wxWasmRenderer::DrawTextBorder(wxDC& dc,
                                   wxBorder border,
                                   const wxRect& rectOrig,
                                   int flags,
                                   wxRect *rectIn)
{
    DrawBorder(dc, border, rectOrig, flags, rectIn);
/*
    wxRect rect = rectOrig;

    DrawRect(dc, &rect, m_penDarkGrey);

    if ( rectIn )
        *rectIn = rect;
*/
}

void wxWasmRenderer::DrawButtonSurface(wxDC& dc,
                                       const wxColour& WXUNUSED(col),
                                       const wxRect& rect,
                                       int flags)
{
    if (flags & wxCONTROL_PRESSED)
    {
        DrawBackground(dc, wxColour(184, 184, 184), rect, flags);
    } else {
        DrawBackground(dc, wxColour(214, 214, 214), rect, flags);
    }
}

void wxWasmRenderer::DrawButtonLabel(wxDC& dc,
                                    const wxString& text,
                                    const wxBitmap& image,
                                    const wxRect& rect,
                                    int flags,
                                    int alignment,
                                    int WXUNUSED(indexAccel),
                                    wxRect *rectBounds)
{
    wxString label;
    if (!image.IsOk())
        label = text;

    wxDCTextColourChanger clrChanger(dc);

    wxRect rectLabel = rect;
    if ( !label.empty() && (flags & wxCONTROL_DISABLED) )
    {
        if ( flags & wxCONTROL_PRESSED )
        {
            // shift the label if a button is pressed
            rectLabel.Offset(1, 1);
        }


        // make the main label text grey
        clrChanger.Set(m_penDarkGrey.GetColour());

        if ( flags & wxCONTROL_FOCUSED )
        {
            // leave enough space for the focus rect
            rectLabel.Inflate(-2);
        }
    }

    dc.DrawLabel(label, image, rectLabel, alignment, -1, rectBounds);
}

void wxWasmRenderer::DrawButtonBorder(wxDC& WXUNUSED(dc),
                                     const wxRect& rectTotal,
                                     int WXUNUSED(flags),
                                     wxRect *rectIn)
{
    wxRect rect = rectTotal;

    //DrawRect(dc, &rect, m_penDarkGrey);

    if ( rectIn )
        *rectIn = rect;
}

int wxWasmRenderer::GetTextBorderWidth(const wxTextCtrl * WXUNUSED(text)) const
{
    return 4;
}

// ----------------------------------------------------------------------------
// lines and frames
// ----------------------------------------------------------------------------

void wxWasmRenderer::DrawHorizontalLine(wxDC& dc, wxCoord y, wxCoord x1, wxCoord x2)
{
    dc.SetPen(m_penMediumGrey);
    dc.DrawLine(x1, y, x2 + 1, y);
}

void wxWasmRenderer::DrawVerticalLine(wxDC& dc, wxCoord x, wxCoord y1, wxCoord y2)
{
    dc.SetPen(m_penMediumGrey);
    dc.DrawLine(x, y1, x, y2 + 1);
}

void wxWasmRenderer::DrawFrameWithoutLabel(wxDC& dc,
                                           const wxRect& rectFrame,
                                           const wxRect& rectLabel)
{
    // draw left, bottom and right lines entirely
    DrawVerticalLine(dc, rectFrame.GetLeft(),
                     rectFrame.GetTop(), rectFrame.GetBottom() - 2);
    DrawHorizontalLine(dc, rectFrame.GetBottom() - 1,
                       rectFrame.GetLeft(), rectFrame.GetRight() - 2);
    DrawHorizontalLine(dc, rectFrame.GetTop(),
                       rectLabel.GetRight(), rectFrame.GetRight() - 2);
    DrawVerticalLine(dc, rectFrame.GetRight() - 1,
                     rectFrame.GetTop(), rectFrame.GetBottom() - 2);
    DrawHorizontalLine(dc, rectFrame.GetTop(),
                       rectFrame.GetLeft() + 1, rectLabel.GetLeft());
}

void wxWasmRenderer::DrawFrameWithLabel(wxDC& dc,
                                       const wxString& label,
                                       const wxRect& rectFrameOrig,
                                       const wxRect& rectTextOrig,
                                       int flags,
                                       int alignment,
                                       int indexAccel)
{
    wxRect rectText(rectTextOrig);
    rectText.Inflate(1, 0);

    wxRect rectLabel;
    DrawLabel(dc, label, rectText, flags, alignment, indexAccel, &rectLabel);
    rectLabel.x -= 3;
    rectLabel.width += 6;

    wxRect rectFrame(rectFrameOrig);
    rectFrame.x += 5;
    rectFrame.width -= 5;

    DrawFrameWithoutLabel(dc, rectFrame, rectLabel);
}

void wxWasmRenderer::DrawFrame(wxDC& dc,
                               const wxString& label,
                               const wxRect& rect,
                               int flags,
                               int alignment,
                               int indexAccel)
{
    wxCoord height = 0; // of the label
    wxRect rectFrame = rect;
    if ( !label.empty() )
    {
        // the text should touch the top border of the rect, so the frame
        // itself should be lower
        dc.GetTextExtent(label, NULL, &height);
        rectFrame.y += height / 2;
        rectFrame.height -= height / 2;

        // we have to draw each part of the frame individually as we can't
        // erase the background beyond the label as it might contain some
        // pixmap already, so drawing everything and then overwriting part of
        // the frame with label doesn't work

        // TODO: the +5 shouldn't be hard coded
        wxRect rectText;
        rectText.x = rectFrame.x + 12;
        rectText.y = rect.y;
        rectText.width = rectFrame.width - 14; // +2 border width
        rectText.height = height;

        DrawFrameWithLabel(dc, label, rectFrame, rectText, flags,
                           alignment, indexAccel);
    }
    else // no label
    {
        wxRect rectFrame(rect);

        DrawVerticalLine(dc, rectFrame.GetLeft(),
                         rectFrame.GetTop(), rectFrame.GetBottom() - 2);
        DrawHorizontalLine(dc, rectFrame.GetTop(),
                           rectFrame.GetLeft() + 1, rectFrame.GetRight() - 2);
        DrawHorizontalLine(dc, rectFrame.GetBottom() - 1,
                           rectFrame.GetLeft(), rectFrame.GetRight() - 2);
        //DrawHorizontalLine(dc, rectFrame.GetTop(),
        //                   rectLabel.GetRight(), rectFrame.GetRight() - 2);
        DrawVerticalLine(dc, rectFrame.GetRight() - 1,
                         rectFrame.GetTop(), rectFrame.GetBottom() - 2);
    }
}

// ----------------------------------------------------------------------------
// check/radio buttons
// ----------------------------------------------------------------------------

void wxWasmRenderer::DrawCheckItemBitmap(wxDC& dc,
                                        const wxBitmap& bitmap,
                                        const wxRect& rect,
                                        int flags)
{
    // never draw the focus rect around the check indicators here
    DrawCheckButton(dc, wxEmptyString, bitmap, rect, flags & ~wxCONTROL_FOCUSED);
}

void wxWasmRenderer::DrawUndeterminedBitmap(wxDC& dc,
                                           const wxRect& rectTotal,
                                           bool isPressed)
{
    // FIXME: For sure it is not Wasm look but it is better than nothing.
    // Show me correct look and I will immediatelly make it better (ABX)
    wxRect rect = rectTotal;

    wxColour col1, col2;

    if ( isPressed )
    {
        col1 = wxSCHEME_COLOUR(m_scheme, SHADOW_DARK);
        col2 = wxSCHEME_COLOUR(m_scheme, CONTROL_PRESSED);
    }
    else
    {
        col1 = wxSCHEME_COLOUR(m_scheme, SHADOW_DARK);
        col2 = *wxWHITE;
    }

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(col1);
    dc.DrawRectangle(rect);
    rect.Deflate(1);
    dc.SetBrush(col2);
    dc.DrawRectangle(rect);
}

void wxWasmRenderer::DrawUncheckBitmap(wxDC& dc,
                                      const wxRect& rectTotal,
                                      bool isPressed)
{
    wxRect rect = rectTotal;

    wxColour col = *wxWHITE;

    if ( isPressed )
        col = wxSCHEME_COLOUR(m_scheme, CONTROL_PRESSED);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxSCHEME_COLOUR(m_scheme, SHADOW_OUT));
    dc.DrawRectangle(rect);

    rect.Inflate(-1);
    dc.SetBrush(col);
    dc.DrawRectangle(rect);
}

void wxWasmRenderer::DrawCheckBitmap(wxDC& dc, const wxRect& rectTotal)
{
    wxRect rect = rectTotal;

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxSCHEME_COLOUR(m_scheme, SHADOW_OUT));
    dc.DrawRectangle(rect);

    rect.Inflate(-1);
    dc.SetBrush(*wxWHITE);
    dc.DrawRectangle(rect);

    DrawCheck(dc, rect);
}

void wxWasmRenderer::DrawRadioButtonBitmap(wxDC& dc,
                                          const wxRect& rect,
                                          int flags)
{
    wxCoord xRight = rect.GetRight(),
            yBottom = rect.GetBottom();

    wxCoord radius = rect.height / 2 - 1;;

    DrawBackground(dc, wxSCHEME_COLOUR(m_scheme, CONTROL_CURRENT), rect);

    dc.SetPen(m_penDarkGrey);
    dc.SetBrush(wxSCHEME_COLOUR(m_scheme, CONTROL_CURRENT)); 
    // draw the normal border
    dc.DrawCircle(xRight/2,yBottom/2,radius);

    wxColor checkedCol, uncheckedCol;
    checkedCol = wxSCHEME_COLOUR(m_scheme, HIGHLIGHT);
    uncheckedCol = wxSCHEME_COLOUR(m_scheme, SHADOW_HIGHLIGHT);
    dc.SetBrush(flags & wxCONTROL_CHECKED ? checkedCol : uncheckedCol);

    // inner dot
    dc.DrawCircle(xRight/2,yBottom/2,radius/2);

    bool drawIt = true;

    if ( flags & wxCONTROL_PRESSED )
        dc.SetBrush(wxSCHEME_COLOUR(m_scheme, CONTROL_PRESSED));
    else // unchecked and unpressed
        drawIt = false;

    if ( drawIt )
        dc.DrawCircle(xRight/2, yBottom/2, radius/2);

    if ( flags & wxCONTROL_PRESSED )
    {
        dc.SetBrush(wxSCHEME_COLOUR(m_scheme, CONTROL_PRESSED));
        drawIt = true;
    }
    else // checked and unpressed
        drawIt = false;

    if ( drawIt )
        dc.DrawCircle(xRight/2, yBottom/2, radius/2);
}

void wxWasmRenderer::DrawUpZag(wxDC& dc,
                              wxCoord x1,
                              wxCoord x2,
                              wxCoord y1,
                              wxCoord y2)
{
    wxCoord xMid = (x1 + x2) / 2;
    dc.DrawLine(x1, y1, xMid, y2);
    dc.DrawLine(xMid, y2, x2 + 1, y1 + 1);
}

void wxWasmRenderer::DrawDownZag(wxDC& dc,
                                wxCoord x1,
                                wxCoord x2,
                                wxCoord y1,
                                wxCoord y2)
{
    wxCoord xMid = (x1 + x2) / 2;
    dc.DrawLine(x1 + 1, y1 + 1, xMid, y2);
    dc.DrawLine(xMid, y2, x2, y1);
}

void wxWasmRenderer::DrawCheck(wxDC& dc, const wxRect& rect)
{
    dc.SetPen(wxPen(dc.GetTextForeground(), 2));

    int x = rect.x + (rect.width - CHECK_WIDTH) / 2;
    int y = rect.y + (rect.height - CHECK_HEIGHT) / 2;

    int x1 = x;
    int y1 = y + 5;
    int x2 = x + 3;
    int y2 = y + 8;
    int x3 = x + 8;
    int y3 = y;

    dc.DrawLine(x1, y1, x2, y2);
    dc.DrawLine(x2, y2, x3, y3);
}

wxBitmap wxWasmRenderer::GetCheckBitmap(int flags)
{
    if ( !m_bitmapsCheckbox[0][0].IsOk() )
    {
        // init the bitmaps once only
        wxRect rect;
        wxSize size = GetCheckBitmapSize();
        rect.width = size.x;
        rect.height = size.y;
        double scaleFactor = wxContentScaleFactor();

        for ( int i = 0; i < 2; i++ )
        {
            for ( int j = 0; j < 3; j++ )
                m_bitmapsCheckbox[i][j].CreateScaled(rect.width, rect.height, wxBITMAP_SCREEN_DEPTH, scaleFactor);
        }

        wxMemoryDC dc;

        // normal checked
        dc.SelectObject(m_bitmapsCheckbox[0][0]);
        DrawCheckBitmap(dc, rect);

        // normal unchecked
        dc.SelectObject(m_bitmapsCheckbox[0][1]);
        DrawUncheckBitmap(dc, rect, false);

        // normal undeterminated
        dc.SelectObject(m_bitmapsCheckbox[0][2]);
        DrawUndeterminedBitmap(dc, rect, false);

        // pressed checked
        m_bitmapsCheckbox[1][0] = m_bitmapsCheckbox[0][0];

        // pressed unchecked
        dc.SelectObject(m_bitmapsCheckbox[1][1]);
        DrawUncheckBitmap(dc, rect, true);

        // pressed undeterminated
        dc.SelectObject(m_bitmapsCheckbox[1][2]);
        DrawUndeterminedBitmap(dc, rect, true);
    }

    IndicatorState state;
    IndicatorStatus status;
    GetIndicatorsFromFlags(flags, state, status);

    // disabled looks the same as normal
    if ( state == IndicatorState_Disabled )
        state = IndicatorState_Normal;

    return m_bitmapsCheckbox[state][status];
}

wxBitmap wxWasmRenderer::GetRadioBitmap(int flags)
{
    IndicatorState state;
    IndicatorStatus status;
    GetIndicatorsFromFlags(flags, state, status);

    wxBitmap& bmp = m_bitmapsRadiobtn[state][status];
    if ( !bmp.IsOk() )
    {
        const wxSize size = GetRadioBitmapSize();
        double scaleFactor = wxContentScaleFactor();

        wxMemoryDC dc;
        bmp.CreateScaled(size.x, size.y, wxBITMAP_SCREEN_DEPTH, scaleFactor);
        dc.SelectObject(bmp);

        DrawRadioButtonBitmap(dc, size, flags);
    }

    return bmp;
}

wxBitmap wxWasmRenderer::GetLineWrapBitmap() const
{
    if ( !m_bmpLineWrap.IsOk() )
    {
        #define line_wrap_width 6
        #define line_wrap_height 9
        static const char line_wrap_bits[] =
        {
          0x1e, 0x3e, 0x30, 0x30, 0x39, 0x1f, 0x0f, 0x0f, 0x1f,
        };

        wxBitmap bmpLineWrap(line_wrap_bits, line_wrap_width, line_wrap_height, wxBITMAP_SCREEN_DEPTH);
        if ( !bmpLineWrap.IsOk() )
        {
            wxFAIL_MSG( wxT("Failed to create line wrap XBM") );
        }
        else
        {
            wxConstCast(this, wxWasmRenderer)->m_bmpLineWrap = bmpLineWrap;
        }
    }

    return m_bmpLineWrap;
}

#if wxUSE_TOOLBAR
void wxWasmRenderer::DrawToolBarButton(wxDC& dc,
                                      const wxString& label,
                                      const wxBitmap& bitmap,
                                      const wxRect& rectOrig,
                                      int flags,
                                      long WXUNUSED(style),
                                      int tbarStyle)
{
    // we don't draw the separators at all
    if ( !label.empty() || bitmap.IsOk() )
    {
        wxRect rect = rectOrig;
        rect.Deflate(BORDER_THICKNESS);

        if ( flags & wxCONTROL_PRESSED )
        {
            DrawBorder(dc, wxBORDER_SUNKEN, rect, flags, &rect);

            DrawBackground(dc, wxSCHEME_COLOUR(m_scheme, CONTROL_PRESSED), rect);
        }
        else if ( flags & wxCONTROL_CURRENT )
        {
            DrawBorder(dc, wxBORDER_RAISED, rect, flags, &rect);

            DrawBackground(dc, wxSCHEME_COLOUR(m_scheme, CONTROL_CURRENT), rect);
        }

        if(tbarStyle & wxTB_TEXT)
        {
            if(tbarStyle & wxTB_HORIZONTAL)
            {
                dc.DrawLabel(label, bitmap, rect, wxALIGN_CENTRE);
            }
            else
            {
                dc.DrawLabel(label, bitmap, rect, wxALIGN_LEFT|wxALIGN_CENTER_VERTICAL);
            }
        }
        else
        {
            int xpoint = (rect.GetLeft() + rect.GetRight() + 1 - bitmap.GetWidth()) / 2;
            int ypoint = (rect.GetTop() + rect.GetBottom() + 1 - bitmap.GetHeight()) / 2;
            dc.DrawBitmap(bitmap, xpoint, ypoint);
        }
    }
}
#endif // wxUSE_TOOLBAR

// ----------------------------------------------------------------------------
// text control
// ----------------------------------------------------------------------------

#if wxUSE_TEXTCTRL

wxRect wxWasmRenderer::GetTextClientArea(const wxTextCtrl *text,
                                        const wxRect& rect,
                                        wxCoord *extraSpaceBeyond) const
{
    wxRect
      rectText = wxStdRenderer::GetTextClientArea(text, rect, extraSpaceBeyond);

    if ( text->WrapLines() )
    {
        // leave enough for the line wrap bitmap indicator
        wxCoord widthMark = GetLineWrapBitmap().GetWidth() + 2;

        rectText.width -= widthMark;

        if ( extraSpaceBeyond )
            *extraSpaceBeyond = widthMark;
    }

    return rectText;
}

void wxWasmRenderer::DrawLineWrapMark(wxDC& dc, const wxRect& rect)
{
    wxBitmap bmpLineWrap = GetLineWrapBitmap();

    // for a mono bitmap he colours it appears in depends on the current text
    // colours, so set them correctly
    wxColour colFgOld;
    if ( bmpLineWrap.GetDepth() == 1 )
    {
        colFgOld = dc.GetTextForeground();

        // FIXME: I wonder what should we do if the background is black too?
        dc.SetTextForeground(*wxBLACK);
    }

    dc.DrawBitmap(bmpLineWrap,
                  rect.x, rect.y + (rect.height - bmpLineWrap.GetHeight())/2);

    if ( colFgOld.IsOk() )
    {
        // restore old colour
        dc.SetTextForeground(colFgOld);
    }
}

#endif // wxUSE_TEXTCTRL

// ----------------------------------------------------------------------------
// notebook
// ----------------------------------------------------------------------------

#if wxUSE_NOTEBOOK

void wxWasmRenderer::DrawTab(wxDC& dc,
                            const wxRect& rectOrig,
                            wxDirection dir,
                            const wxString& label,
                            const wxBitmap& bitmap,
                            int flags,
                            int indexAccel)
{
    #define SELECT_FOR_VERTICAL(X,Y) ( isVertical ? Y : X )
    #define REVERSE_FOR_VERTICAL(X,Y) \
        SELECT_FOR_VERTICAL(X,Y)      \
        ,                             \
        SELECT_FOR_VERTICAL(Y,X)

    wxRect rect = rectOrig;

    bool isVertical = ( dir == wxLEFT ) || ( dir == wxRIGHT );

    // the current tab is drawn indented (to the top for default case) and
    // bigger than the other ones
    const wxSize indent = GetTabIndent();
    if ( flags & wxCONTROL_SELECTED )
    {
        rect.Inflate( SELECT_FOR_VERTICAL( indent.x , 0),
                      SELECT_FOR_VERTICAL( 0, indent.y ));
        switch ( dir )
        {
            default:
                wxFAIL_MSG(wxT("invaild notebook tab orientation"));
                // fall through

            case wxTOP:
                rect.y -= indent.y;
                // fall through
            case wxBOTTOM:
                rect.height += indent.y;
                break;

            case wxLEFT:
                rect.x -= indent.x;
                // fall through
            case wxRIGHT:
                rect.width += indent.x;
                break;
        }
    }

    // selected tab has different colour
    wxColour col = flags & wxCONTROL_SELECTED
                        ? wxSCHEME_COLOUR(m_scheme, SHADOW_IN)
                        : wxSCHEME_COLOUR(m_scheme, SCROLLBAR);
    DrawSolidRect(dc, col, rect);

    if ( flags & wxCONTROL_FOCUSED )
    {
        // draw the focus rect
        wxRect rectBorder = rect;
        rectBorder.Deflate(4, 3);
        if ( dir == wxBOTTOM )
            rectBorder.Offset(0, -1);
        if ( dir == wxRIGHT )
            rectBorder.Offset(-1, 0);

        //DrawRect(dc, &rectBorder, m_penBlack);
    }

    // draw the text, image and the focus around them (if necessary)
    wxRect rectLabel( REVERSE_FOR_VERTICAL(rect.x,rect.y),
                      REVERSE_FOR_VERTICAL(rect.width,rect.height)
                    );
    rectLabel.Deflate(1, 1);
    if ( isVertical )
    {
        // draw it horizontally into memory and rotate for screen
        wxMemoryDC dcMem;
        wxBitmap bitmapRotated,
                 bitmapMem( rectLabel.x + rectLabel.width,
                            rectLabel.y + rectLabel.height );
        dcMem.SelectObject(bitmapMem);
        dcMem.SetBackground(dc.GetBackground());
        dcMem.SetFont(dc.GetFont());
        dcMem.SetTextForeground(dc.GetTextForeground());
        dcMem.Clear();
        bitmapRotated =
#if wxUSE_IMAGE
                        wxBitmap( wxImage( bitmap.ConvertToImage() ).Rotate90(dir==wxLEFT) )
#else
                        bitmap
#endif // wxUSE_IMAGE
                        ;
        dcMem.DrawLabel(label, bitmapRotated, rectLabel, wxALIGN_CENTRE, indexAccel);
        dcMem.SelectObject(wxNullBitmap);
        bitmapMem = bitmapMem.GetSubBitmap(rectLabel);
#if wxUSE_IMAGE
        bitmapMem = wxBitmap(wxImage(bitmapMem.ConvertToImage()).Rotate90(dir==wxRIGHT))
#endif
                    ;

        dc.DrawBitmap(bitmapMem, rectLabel.y, rectLabel.x, false);
    }
    else
    {
        dc.DrawLabel(label, bitmap, rectLabel, wxALIGN_CENTRE, indexAccel);
    }

    // now draw the tab itself
    wxCoord x = SELECT_FOR_VERTICAL(rect.x,rect.y),
            y = SELECT_FOR_VERTICAL(rect.y,rect.x),
            x2 = SELECT_FOR_VERTICAL(rect.GetRight(),rect.GetBottom()),
            y2 = SELECT_FOR_VERTICAL(rect.GetBottom(),rect.GetRight());
    switch ( dir )
    {
        default:
            // default is top
        case wxLEFT:
            // left orientation looks like top but IsVertical makes x and y reversed
        case wxTOP:
            // top is not vertical so use coordinates in written order
            dc.SetPen(m_penHighlight);
            dc.DrawLine(REVERSE_FOR_VERTICAL(x, y2),
                        REVERSE_FOR_VERTICAL(x, y));
            dc.DrawLine(REVERSE_FOR_VERTICAL(x + 1, y),
                        REVERSE_FOR_VERTICAL(x2, y));

            dc.SetPen(m_penBlack);
            dc.DrawLine(REVERSE_FOR_VERTICAL(x2, y2),
                        REVERSE_FOR_VERTICAL(x2, y));

            dc.SetPen(m_penDarkGrey);
            dc.DrawLine(REVERSE_FOR_VERTICAL(x2 - 1, y2),
                        REVERSE_FOR_VERTICAL(x2 - 1, y + 1));

            if ( flags & wxCONTROL_SELECTED )
            {
                dc.SetPen(m_penLightGrey);

                // overwrite the part of the border below this tab
                dc.DrawLine(REVERSE_FOR_VERTICAL(x + 1, y2 + 1),
                            REVERSE_FOR_VERTICAL(x2 - 1, y2 + 1));

                // and the shadow of the tab to the left of us
                dc.DrawLine(REVERSE_FOR_VERTICAL(x + 1, y + 2),
                            REVERSE_FOR_VERTICAL(x + 1, y2 + 1));
            }
            break;

        case wxRIGHT:
            // right orientation looks like bottom but IsVertical makes x and y reversed
        case wxBOTTOM:
            // bottom is not vertical so use coordinates in written order
            dc.SetPen(m_penHighlight);

            // we need to continue one pixel further to overwrite the corner of
            // the border for the selected tab
            dc.DrawLine(REVERSE_FOR_VERTICAL(x, y - (flags & wxCONTROL_SELECTED ? 1 : 0)),
                        REVERSE_FOR_VERTICAL(x, y2));

            // it doesn't work like this (TODO: implement it properly)
#if 0
            // erase the corner of the tab to the right
            dc.SetPen(m_penLightGrey);
            dc.DrawPoint(REVERSE_FOR_VERTICAL(x2 - 1, y - 2));
            dc.DrawPoint(REVERSE_FOR_VERTICAL(x2 - 2, y - 2));
            dc.DrawPoint(REVERSE_FOR_VERTICAL(x2 - 2, y - 1));
#endif // 0

            dc.SetPen(m_penBlack);
            dc.DrawLine(REVERSE_FOR_VERTICAL(x + 1, y2),
                        REVERSE_FOR_VERTICAL(x2, y2));
            dc.DrawLine(REVERSE_FOR_VERTICAL(x2, y),
                        REVERSE_FOR_VERTICAL(x2, y2));

            dc.SetPen(m_penDarkGrey);
            dc.DrawLine(REVERSE_FOR_VERTICAL(x + 2, y2 - 1),
                        REVERSE_FOR_VERTICAL(x2 - 1, y2 - 1));
            dc.DrawLine(REVERSE_FOR_VERTICAL(x2 - 1, y),
                        REVERSE_FOR_VERTICAL(x2 - 1, y2));

            if ( flags & wxCONTROL_SELECTED )
            {
                dc.SetPen(m_penLightGrey);

                // overwrite the part of the (double!) border above this tab
                dc.DrawLine(REVERSE_FOR_VERTICAL(x + 1, y - 1),
                            REVERSE_FOR_VERTICAL(x2 - 1, y - 1));
                dc.DrawLine(REVERSE_FOR_VERTICAL(x + 1, y - 2),
                            REVERSE_FOR_VERTICAL(x2 - 1, y - 2));

                // and the shadow of the tab to the left of us
                dc.DrawLine(REVERSE_FOR_VERTICAL(x + 1, y2 - 1),
                            REVERSE_FOR_VERTICAL(x + 1, y - 1));
            }
            break;
    }
}

#endif // wxUSE_NOTEBOOK

// ----------------------------------------------------------------------------
// slider
// ----------------------------------------------------------------------------

#if wxUSE_SLIDER

wxSize wxWasmRenderer::GetSliderThumbSize(const wxRect& WXUNUSED(rect),
                                         int WXUNUSED(lenThumb),
                                         wxOrientation WXUNUSED(orient)) const
{
    static const wxCoord SLIDER_THUMB_LENGTH = 17;
    return wxSize(SLIDER_THUMB_LENGTH, SLIDER_THUMB_LENGTH);
}

wxRect wxWasmRenderer::GetSliderShaftRect(const wxRect& rect,
                                         int WXUNUSED(lenThumb),
                                         wxOrientation WXUNUSED(orient),
                                         long WXUNUSED(style)) const
{
    return rect.Deflate(2 * BORDER_THICKNESS, 2 * BORDER_THICKNESS);
}

void wxWasmRenderer::DrawSliderShaft(wxDC& dc,
                                    const wxRect& rectOrig,
                                    double fracValue,
                                    int lenThumb,
                                    wxOrientation orient,
                                    int flags,
                                    long WXUNUSED(style),
                                    wxRect *rectShaft)
{
    static const wxCoord SHAFT_WIDTH = 5;
    static const double SHAFT_RADIUS = SHAFT_WIDTH / 2.0;

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxSCHEME_COLOUR(m_scheme, WINDOW));
    dc.DrawRectangle(rectOrig);

    wxRect rect = rectOrig;
    rect.Deflate(2 * BORDER_THICKNESS);

    wxRect rectOn;
    wxRect rectOff;

    if ( orient == wxHORIZONTAL )
    {
        int offset = (rect.height - SHAFT_WIDTH) / 2;
        int thumbCenter = fracValue * (rect.width - lenThumb) + lenThumb / 2.0;
        rectOn = wxRect(rect.x, rect.y + offset, thumbCenter, SHAFT_WIDTH);
        rectOff = wxRect(rect.x + thumbCenter, rect.y + offset, rect.width - thumbCenter, SHAFT_WIDTH);
    }
    else
    {
        int offset = (rect.width - SHAFT_WIDTH) / 2;
        int thumbCenter = fracValue * (rect.height - lenThumb) + lenThumb / 2.0;
        rectOff = wxRect(rect.x + offset, rect.y, SHAFT_WIDTH, thumbCenter);
        rectOn = wxRect(rect.x + offset, rect.y + thumbCenter, SHAFT_WIDTH, rect.height - thumbCenter);
    }

    if (flags & wxSL_INVERSE)
    {
       wxRect tmpRect = rectOff;
       rectOff = rectOn;
       rectOn = tmpRect;
    }

    dc.SetBrush(wxSCHEME_COLOUR(m_scheme, HIGHLIGHT));
    dc.DrawRoundedRectangle(rectOn.x, rectOn.y, rectOn.width, rectOn.height, SHAFT_RADIUS);

    dc.SetBrush(wxSCHEME_COLOUR(m_scheme, SCROLLBAR));
    dc.DrawRoundedRectangle(rectOff.x, rectOff.y, rectOff.width, rectOff.height, SHAFT_RADIUS);

    if ( rectShaft )
        *rectShaft = rect;
}

void wxWasmRenderer::DrawSliderThumb(wxDC& dc,
                                    const wxRect& rectOrig,
                                    wxOrientation WXUNUSED(orient),
                                    int WXUNUSED(flags),
                                    long WXUNUSED(style))
{
    wxRect rect = rectOrig;

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxSCHEME_COLOUR(m_scheme, HIGHLIGHT));

    dc.DrawEllipse(rect);
}

#endif // wxUSE_SLIDER

#if wxUSE_MENUS

// ----------------------------------------------------------------------------
// menu and menubar
// ----------------------------------------------------------------------------

// FIXME: all constants are hardcoded but shouldn't be
static const wxCoord MENU_LEFT_MARGIN = 12;
static const wxCoord MENU_RIGHT_MARGIN = 9;

static const wxCoord MENU_HORZ_MARGIN = 12;
static const wxCoord MENU_VERT_MARGIN = 4;

// the margin around bitmap/check marks (on each side)
static const wxCoord MENU_BMP_MARGIN = 2;

static const wxCoord MENU_CHECK_MARGIN = 2;

static const wxCoord MENU_ARROW_WIDTH = 5;
static const wxCoord MENU_ARROW_HEIGHT = 9;

// the margin between the labels and accel strings
static const wxCoord MENU_ACCEL_MARGIN = 8;

// the separator height in pixels: in fact, strangely enough, the real height
// is 2 but Windows adds one extra pixel in the bottom margin, so take it into
// account here
static const wxCoord MENU_SEPARATOR_HEIGHT = 3;

static const wxCoord MENU_OVERFLOW_HEIGHT = 24;
static const wxCoord MENU_OVERFLOW_ARROW_WIDTH = MENU_ARROW_HEIGHT;
static const wxCoord MENU_OVERFLOW_ARROW_HEIGHT = MENU_ARROW_WIDTH;


// wxWasmMenuGeometryInfo: the wxMenuGeometryInfo used by wxWasmRenderer
class wxWasmMenuGeometryInfo : public wxMenuGeometryInfo
{
public:
    virtual wxSize GetSize() const wxOVERRIDE { return m_size; }

    virtual wxCoord GetOverflowHeight() const wxOVERRIDE { return MENU_OVERFLOW_HEIGHT; }

    wxCoord GetLabelOffset() const { return m_ofsLabel; }
    wxCoord GetAccelOffset() const { return m_ofsAccel; }

    wxCoord GetItemHeight() const { return m_heightItem; }

private:
    // the total size of the menu
    wxSize m_size;

    // the offset of the start of the menu item label
    wxCoord m_ofsLabel;

    // the offset of the start of the accel label
    wxCoord m_ofsAccel;

    // the height of a normal (not separator) item
    wxCoord m_heightItem;

    friend wxMenuGeometryInfo *
        wxWasmRenderer::GetMenuGeometry(wxWindow *, const wxMenu&) const;
};

void wxWasmRenderer::DrawMenuBarItem(wxDC& dc,
                                    const wxRect& rect,
                                    const wxString& label,
                                    int flags,
                                    int indexAccel)
{
    DoDrawMenuItem(dc, rect, label, flags, indexAccel);
}

void wxWasmRenderer::DrawMenuItem(wxDC& dc,
                                 wxCoord y,
                                 const wxMenuGeometryInfo& gi,
                                 const wxString& label,
                                 const wxString& accel,
                                 const wxBitmap& bitmap,
                                 int flags,
                                 int indexAccel)
{
    const wxWasmMenuGeometryInfo& geomInfo = (const wxWasmMenuGeometryInfo&)gi;

    wxRect rect;
    rect.x = 0;
    rect.y = y;
    rect.width = geomInfo.GetSize().x;
    rect.height = geomInfo.GetItemHeight();

    DoDrawMenuItem(dc, rect, label, flags, indexAccel, accel, bitmap, &geomInfo);
}

void wxWasmRenderer::DoDrawMenuItem(wxDC& dc,
                                   const wxRect& rectOrig,
                                   const wxString& label,
                                   int flags,
                                   int WXUNUSED(indexAccel),
                                   const wxString& accel,
                                   const wxBitmap& bitmap,
                                   const wxWasmMenuGeometryInfo *geometryInfo)
{
    wxRect rect = rectOrig;

    // draw the selected item specially
    if ( flags & wxCONTROL_SELECTED && !(flags & wxCONTROL_DISABLED) )
    {
        DrawBackground(dc, wxSCHEME_COLOUR(m_scheme, HIGHLIGHT), rect);
        dc.SetTextForeground(wxSCHEME_COLOUR(m_scheme, HIGHLIGHT_TEXT));
    }

    rect.Deflate(MENU_HORZ_MARGIN, MENU_VERT_MARGIN);

    // draw the bitmap: use the bitmap provided or the standard checkmark for
    // the checkable items
    if ( geometryInfo )
    {
        wxBitmap bmp = bitmap;

        if ( flags & wxCONTROL_CHECKED )
        {
            wxRect checkRect(rect.x, rect.y, CHECK_WIDTH, rect.height);
            DrawCheck(dc, checkRect);
        }

        if ( bmp.IsOk() )
        {
            rect.SetRight(geometryInfo->GetLabelOffset());
            wxControlRenderer::DrawBitmap(dc, bmp, rect);
        }
    }
    //else: menubar items don't have bitmaps

    // draw the label
    if ( geometryInfo )
    {
        rect.x = geometryInfo->GetLabelOffset();
        rect.SetRight(geometryInfo->GetAccelOffset());
    }

    DrawLabel(dc, label, rect, flags, wxALIGN_CENTRE_VERTICAL, -1);

    // draw the accel string
    if ( !accel.empty() )
    {
        // menubar items shouldn't have them
        wxCHECK_RET( geometryInfo, wxT("accel strings only valid for menus") );

        rect.x = geometryInfo->GetAccelOffset();
        rect.SetRight(geometryInfo->GetSize().x - MENU_RIGHT_MARGIN);

        wxString accelString = RenderAccelString(accel);
        DrawLabel(dc, accelString, rect, flags, wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL);
    }

    // draw the submenu indicator
    if ( flags & wxCONTROL_ISSUBMENU )
    {
        wxCHECK_RET( geometryInfo, wxT("wxCONTROL_ISSUBMENU only valid for menus") );

        rect.x = geometryInfo->GetSize().x - MENU_RIGHT_MARGIN - MENU_ARROW_WIDTH;
        rect.y = rect.y + (rect.height - MENU_ARROW_HEIGHT) / 2;
        rect.height = MENU_ARROW_HEIGHT;
        rect.width = MENU_ARROW_WIDTH;

        DrawMenuArrow(dc, rect, flags);
    }

    if ( flags & wxCONTROL_SELECTED && !(flags & wxCONTROL_DISABLED) )
    {
        dc.SetTextForeground(wxSCHEME_COLOUR(m_scheme, CONTROL_TEXT));
    }
}

void wxWasmRenderer::DrawMenuSeparator(wxDC& dc,
                                      wxCoord y,
                                      const wxMenuGeometryInfo& geomInfo)
{
    y += MENU_VERT_MARGIN;
    dc.SetPen(m_penMediumGrey);
    dc.DrawLine(0, y, geomInfo.GetSize().x + 1, y);
}

void wxWasmRenderer::DrawMenuOverflowArrow(wxDC& dc,
                                           const wxRect& rect,
                                           wxDirection direction)
{
    dc.SetBrush(wxSCHEME_COLOUR(m_scheme, CONTROL));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(rect);

    wxCoord x = rect.x + (rect.GetWidth() - MENU_OVERFLOW_ARROW_WIDTH) / 2;
    wxCoord y = rect.y + (rect.GetHeight() - MENU_OVERFLOW_ARROW_HEIGHT) / 2;

    wxRect arrowRect(x, y, MENU_OVERFLOW_ARROW_WIDTH, MENU_OVERFLOW_ARROW_HEIGHT);
    dc.SetBrush(dc.GetTextForeground());

    DrawArrow(dc, direction, arrowRect, 0);
}

wxSize wxWasmRenderer::GetMenuBarItemSize(const wxSize& sizeText) const
{
    wxSize size = sizeText;

    // TODO: make this configurable
    size.x += 2*MENU_HORZ_MARGIN;
    size.y += 2*MENU_VERT_MARGIN;

    return size;
}

static inline bool CompareAccelString(const wxString& str, const char *accel)
{
    return str.CmpNoCase(accel) == 0
#if wxUSE_INTL
            || str.CmpNoCase(wxGetTranslation(accel)) == 0
#endif
            ;
}

wxString wxWasmRenderer::RenderAccelString(const wxString& accel) const
{
    if ((wxGetOsVersion() & wxOS_MAC) != 0)
    {
        wxString label = accel;
        label.Trim(true);

        wxString modifiers;
        wxString current;

        for ( size_t n = 0; n < label.length(); n++ )
        {
            bool skip = false;
            if ( !skip && ( (label[n] == '+') || (label[n] == '-') ) )
            {
                if ( CompareAccelString(current, wxTRANSLATE("ctrl")) )
                    modifiers += wxString::FromUTF8("\xE2\x8C\x98"); 
                else if ( CompareAccelString(current, wxTRANSLATE("alt")) )
                    modifiers += wxString::FromUTF8("\xE2\x8C\xA5"); 
                else if ( CompareAccelString(current, wxTRANSLATE("shift")) )
                    modifiers += wxString::FromUTF8("\xE2\x87\xA7"); 
                else if ( CompareAccelString(current, wxTRANSLATE("rawctrl")) )
                    modifiers += wxString::FromUTF8("\xE2\x8C\x83"); 
                else if ( CompareAccelString(current, wxTRANSLATE("num ")) )
                {
                    // This isn't really a modifier, but is part of the name of keys
                    // that have a =/- in them (e.g. num + and num -)
                    // So we want to skip the processing if we see it
                    skip = true;
                    current += label[n];

                    continue;
                }
                else // not a recognized modifier name
                {
                    // we may have "Ctrl-+", for example, but we still want to
                    // catch typos like "Crtl-A" so only give the warning if we
                    // have something before the current '+' or '-', else take
                    // it as a literal symbol
                    if ( current.empty() )
                    {
                        current += label[n];

                        // skip clearing it below
                        continue;
                    }
                    else
                    {
                        wxLogDebug(wxT("Unknown accel modifier: '%s'"),
                                   current.c_str());
                    }
                }

                current.clear();
            }
            else // not special character
            {
                // Preserve case of the key (see comment below)
                current += label[n];
            }
        }

        return modifiers + current;
    }
    else
    {
        return accel;
    }
}

wxMenuGeometryInfo *wxWasmRenderer::GetMenuGeometry(wxWindow *win,
                                                   const wxMenu& menu) const
{
    // prepare the dc: for now we draw all the items with the system font
    wxClientDC dc(win);
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    // the height of a normal item
    wxCoord heightText = dc.GetCharHeight();

    // the total height
    wxCoord height = 0;

    // the max length of label and accel strings: the menu width is the sum of
    // them, even if they're for different items (as the accels should be
    // aligned)
    //
    // the max length of the bitmap is never 0 as Windows always leaves enough
    // space for a check mark indicator
    wxCoord widthLabelMax = 0,
            widthAccelMax = 0,
            widthCheck = 0,
            widthBmpMax = MENU_LEFT_MARGIN;

    bool hasCheck = false;

    for ( wxMenuItemList::compatibility_iterator node = menu.GetMenuItems().GetFirst();
          node;
          node = node->GetNext() )
    {
        // height of this item
        wxCoord h;

        wxMenuItem *item = node->GetData();
        if ( item->IsSeparator() )
        {
            h = MENU_SEPARATOR_HEIGHT;
        }
        else // not separator
        {
            h = heightText;

            wxCoord widthLabel;
            dc.GetTextExtent(item->GetItemLabelText(), &widthLabel, NULL);
            if ( widthLabel > widthLabelMax )
            {
                widthLabelMax = widthLabel;
            }

            wxCoord widthAccel;
            wxString accelString = RenderAccelString(item->GetAccelString());
            dc.GetTextExtent(accelString, &widthAccel, NULL);
            if ( widthAccel > widthAccelMax )
            {
                widthAccelMax = widthAccel;
            }
    
            if ( item->GetSubMenu() && MENU_ARROW_WIDTH > widthAccelMax ) {
                widthAccelMax = MENU_ARROW_WIDTH;
            }

            const wxBitmap& bmp = item->GetBitmap();
            if ( bmp.IsOk() )
            {
                wxCoord widthBmp = bmp.GetWidth();
                if ( widthBmp > widthBmpMax )
                    widthBmpMax = widthBmp;
            }
            else if ( item->IsChecked() )
            {
                hasCheck = true;
            }
        }

        h += 2*MENU_VERT_MARGIN;

        // remember the item position and height
        item->SetGeometry(height, h);

        height += h;
    }

    // bundle the metrics into a struct and return it
    wxWasmMenuGeometryInfo *gi = new wxWasmMenuGeometryInfo;

    if (hasCheck)
    {
        widthCheck = CHECK_WIDTH + MENU_CHECK_MARGIN;
    }

    gi->m_ofsLabel = widthCheck + widthBmpMax + 2*MENU_BMP_MARGIN;
    gi->m_ofsAccel = gi->m_ofsLabel + widthLabelMax;
    if ( widthAccelMax > 0 )
    {
        // if we actually have any accesl, add a margin
        gi->m_ofsAccel += MENU_ACCEL_MARGIN;
    }

    gi->m_heightItem = heightText + 2*MENU_VERT_MARGIN;

    gi->m_size.x = gi->m_ofsAccel + widthAccelMax + MENU_RIGHT_MARGIN;
    gi->m_size.y = height;

    return gi;
}

#endif // wxUSE_MENUS

// ----------------------------------------------------------------------------
// combobox
// ----------------------------------------------------------------------------

void wxWasmRenderer::InitComboBitmaps()
{
    wxSize sizeArrow = m_sizeScrollbarArrow;
    sizeArrow.x -= 2;
    sizeArrow.y -= 2;
    double scaleFactor = wxContentScaleFactor();

    size_t n;

    for ( n = ComboState_Normal; n < ComboState_Max; n++ )
    {
        m_bitmapsCombo[n].CreateScaled(sizeArrow.x, sizeArrow.y, wxBITMAP_SCREEN_DEPTH, scaleFactor);
    }

    static const int comboButtonFlags[ComboState_Max] =
    {
        0,
        wxCONTROL_CURRENT,
        wxCONTROL_PRESSED,
        wxCONTROL_DISABLED,
    };

    wxRect rect(sizeArrow);

    wxMemoryDC dc;
    for ( n = ComboState_Normal; n < ComboState_Max; n++ )
    {
        int flags = comboButtonFlags[n];

        dc.SelectObject(m_bitmapsCombo[n]);
        DrawSolidRect(dc, GetBackgroundColour(flags), rect);
        DrawArrow(dc, wxDOWN, rect, flags);
    }
}

void wxWasmRenderer::GetComboBitmaps(wxBitmap *bmpNormal,
                                    wxBitmap *bmpFocus,
                                    wxBitmap *bmpPressed,
                                    wxBitmap *bmpDisabled)
{
    if ( !m_bitmapsCombo[ComboState_Normal].IsOk() )
    {
        InitComboBitmaps();
    }

    if ( bmpNormal )
        *bmpNormal = m_bitmapsCombo[ComboState_Normal];
    if ( bmpFocus )
        *bmpFocus = m_bitmapsCombo[ComboState_Focus];
    if ( bmpPressed )
        *bmpPressed = m_bitmapsCombo[ComboState_Pressed];
    if ( bmpDisabled )
        *bmpDisabled = m_bitmapsCombo[ComboState_Disabled];
}

// ----------------------------------------------------------------------------
// scrollbar
// ----------------------------------------------------------------------------

void wxWasmRenderer::DrawArrowBorder(wxDC& dc,
                                    wxRect *rect,
                                    wxDirection dir)
{
    static const wxDirection sides[] =
    {
        wxUP, wxLEFT, wxRIGHT, wxDOWN
    };

    wxRect rect1, rect2, rectInner;
    rect1 =
    rect2 =
    rectInner = *rect;

    rect2.Inflate(-1);
    rectInner.Inflate(-2);

    DrawSolidRect(dc, wxSCHEME_COLOUR(m_scheme, SCROLLBAR), *rect);

    // find the side not to draw and also adjust the rectangles to compensate
    // for it
    wxDirection sideToOmit;
    switch ( dir )
    {
        case wxUP:
            sideToOmit = wxDOWN;
            rect2.height += 1;
            rectInner.height += 1;
            break;

        case wxDOWN:
            sideToOmit = wxUP;
            rect2.y -= 1;
            rect2.height += 1;
            rectInner.y -= 2;
            rectInner.height += 1;
            break;

        case wxLEFT:
            sideToOmit = wxRIGHT;
            rect2.width += 1;
            rectInner.width += 1;
            break;

        case wxRIGHT:
            sideToOmit = wxLEFT;
            rect2.x -= 1;
            rect2.width += 1;
            rectInner.x -= 2;
            rectInner.width += 1;
            break;

        default:
            wxFAIL_MSG(wxT("unknown arrow direction"));
            return;
    }

    // the outer rect first
    size_t n;
    for ( n = 0; n < WXSIZEOF(sides); n++ )
    {
        wxDirection side = sides[n];
        if ( side == sideToOmit )
            continue;

        DrawAntiShadedRectSide(dc, rect1, m_penDarkGrey, m_penHighlight, side);
    }

    // and then the inner one
    for ( n = 0; n < WXSIZEOF(sides); n++ )
    {
        wxDirection side = sides[n];
        if ( side == sideToOmit )
            continue;

        DrawAntiShadedRectSide(dc, rect2, m_penBlack, m_penGrey, side);
    }

    *rect = rectInner;
}

void wxWasmRenderer::DrawScrollbarArrow(wxDC& dc,
                                       wxDirection dir,
                                       const wxRect& rectArrow,
                                       int flags)
{
    // first of all, draw the border around it - but we don't want the border
    // on the side opposite to the arrow point
    wxRect rect = rectArrow;
    DrawArrowBorder(dc, &rect, dir);

    // then the arrow itself
    DrawArrow(dc, dir, rect, flags);
}

void wxWasmRenderer::DrawMenuArrow(wxDC& dc,
                                  const wxRect& rect,
                                  int flags)
{
    wxCoord middle = (rect.GetTop() + rect.GetBottom() + 1) / 2;

    wxPoint ptArrow[3];

    ptArrow[0] = rect.GetPosition();
    ptArrow[1].x = rect.GetRight();
    ptArrow[1].y = middle;
    ptArrow[2].x = rect.GetLeft();
    ptArrow[2].y = rect.GetBottom();

    wxColour colInside = GetBackgroundColour(flags);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(dc.GetTextForeground());

    dc.DrawPolygon(WXSIZEOF(ptArrow), ptArrow);
}

void wxWasmRenderer::DrawArrow(wxDC& dc,
                              wxDirection dir,
                              const wxRect& rect,
                              int flags)
{
    enum
    {
        Point_First,
        Point_Second,
        Point_Third,
        Point_Max
    };

    wxPoint ptArrow[Point_Max];

    wxColour colInside;
    if ( flags & wxCONTROL_PRESSED )
    {
        colInside = wxSCHEME_COLOUR(m_scheme, CONTROL_TEXT_DISABLED);
    }
    else
    {
        colInside = wxSCHEME_COLOUR(m_scheme, CONTROL_TEXT);
    }

    wxCoord middle;
    if ( dir == wxUP || dir == wxDOWN )
    {
        // horz middle
        middle = (rect.GetRight() + rect.GetLeft() + 1) / 2;
    }
    else // horz arrow
    {
        middle = (rect.GetTop() + rect.GetBottom() + 1) / 2;
    }

    // draw the arrow interior
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(colInside);

    switch ( dir )
    {
        case wxUP:
            ptArrow[Point_First].x = rect.GetLeft();
            ptArrow[Point_First].y = rect.GetBottom();
            ptArrow[Point_Second].x = middle;
            ptArrow[Point_Second].y = rect.GetTop();
            ptArrow[Point_Third].x = rect.GetRight();
            ptArrow[Point_Third].y = rect.GetBottom();
            break;

        case wxDOWN:
            ptArrow[Point_First] = rect.GetPosition();
            ptArrow[Point_Second].x = middle;
            ptArrow[Point_Second].y = rect.GetBottom();
            ptArrow[Point_Third].x = rect.GetRight();
            ptArrow[Point_Third].y = rect.GetTop();
            break;

        case wxLEFT:
            ptArrow[Point_First].x = rect.GetRight();
            ptArrow[Point_First].y = rect.GetTop();
            ptArrow[Point_Second].x = rect.GetLeft();
            ptArrow[Point_Second].y = middle;
            ptArrow[Point_Third].x = rect.GetRight();
            ptArrow[Point_Third].y = rect.GetBottom();
            break;

        case wxRIGHT:
            ptArrow[Point_First] = rect.GetPosition();
            ptArrow[Point_Second].x = rect.GetRight();
            ptArrow[Point_Second].y = middle;
            ptArrow[Point_Third].x = rect.GetLeft();
            ptArrow[Point_Third].y = rect.GetBottom();
            break;

        default:
            wxFAIL_MSG(wxT("unknown arrow direction"));
    }

    dc.DrawPolygon(WXSIZEOF(ptArrow), ptArrow);
}

void wxWasmRenderer::DrawThumbBorder(wxDC& dc,
                                    wxRect *rect,
                                    wxOrientation orient)
{
    if ( orient == wxVERTICAL )
    {
        DrawAntiShadedRectSide(dc, *rect, m_penDarkGrey, m_penDarkGrey,
                               wxLEFT);
        DrawAntiShadedRectSide(dc, *rect, m_penDarkGrey, m_penDarkGrey,
                               wxRIGHT);
        rect->Inflate(-1, 0);
    }
    else
    {
        DrawAntiShadedRectSide(dc, *rect, m_penDarkGrey, m_penDarkGrey,
                               wxUP);
        DrawAntiShadedRectSide(dc, *rect, m_penDarkGrey, m_penDarkGrey,
                               wxDOWN);
        rect->Inflate(0, -1);
    }
}

void wxWasmRenderer::DrawScrollbarThumb(wxDC& dc,
                                       wxOrientation orient,
                                       const wxRect& rect,
                                       int WXUNUSED(flags))
{
    // we don't want the border in the direction of the scrollbar movement
    wxRect rectThumb = rect;
    DrawThumbBorder(dc, &rectThumb, orient);

    double radius = (orient == wxVERTICAL ? rectThumb.width : rectThumb.height) / 2.0;

    wxColour col = wxSCHEME_COLOUR(m_scheme, CONTROL);

    dc.SetBrush(col);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRoundedRectangle(rectThumb.x, rectThumb.y, rectThumb.width, rectThumb.height, radius);
}

void wxWasmRenderer::DrawScrollbarShaft(wxDC& dc,
                                       wxOrientation orient,
                                       const wxRect& rect,
                                       int WXUNUSED(flags))
{
    wxRect rectBar = rect;
    DrawThumbBorder(dc, &rectBar, orient);
    DrawSolidRect(dc, wxSCHEME_COLOUR(m_scheme, SCROLLBAR), rectBar);
}

// ----------------------------------------------------------------------------
// size adjustments
// ----------------------------------------------------------------------------

void wxWasmRenderer::AdjustSize(wxSize *size, const wxWindow *window)
{
#if wxUSE_BMPBUTTON
    if ( wxDynamicCast(window, wxBitmapButton) )
    {
        size->x += 4;
        size->y += 4;
    } else
#endif // wxUSE_BMPBUTTON
#if wxUSE_BUTTON || wxUSE_TOGGLEBTN
    if ( 0
#  if wxUSE_BUTTON
         || wxDynamicCast(window, wxButton)
#  endif // wxUSE_BUTTON
#  if wxUSE_TOGGLEBTN
         || wxDynamicCast(window, wxToggleButton)
#  endif // wxUSE_TOGGLEBTN
        )
    {
        if ( !(window->GetWindowStyle() & wxBU_EXACTFIT) )
        {
            // TODO: this is ad hoc...
            size->x += 3*window->GetCharWidth();
            wxCoord minBtnHeight = 18;
            if ( size->y < minBtnHeight )
                size->y = minBtnHeight;

            // button border width
            size->y += 4;
        }
    } else
#endif // wxUSE_BUTTON || wxUSE_TOGGLEBTN
#if wxUSE_SCROLLBAR
    if ( wxDynamicCast(window, wxScrollBar) )
    {
        /*
        Don't adjust the size for a scrollbar as its DoGetBestClientSize
        already has the correct size set. Any size changes here would get
        added to the best size, making the scrollbar larger.
        Also skip border width adjustments, they don't make sense for us.
        */
        return;
    }
    else
#endif // wxUSE_SCROLLBAR
    {
        // take into account the border width
        wxStdRenderer::AdjustSize(size, window);
    }
}

// ----------------------------------------------------------------------------
// standard icons
// ----------------------------------------------------------------------------

/* Copyright (c) Julian Smart */
static const char *error_xpm[] = {
/* columns rows colors chars-per-pixel */
"48 48 4 1",
"  c None",
"X c #242424",
"o c #DCDF00",
". c #C00000",
/* pixels */
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                    .....                       ",
"                .............                   ",
"              .................                 ",
"             ...................                ",
"           .......................              ",
"          .........................             ",
"         ...........................            ",
"         ...........................X           ",
"        .............................X          ",
"       ...............................          ",
"       ...............................X         ",
"      .................................X        ",
"      .................................X        ",
"      .................................XX       ",
"      ...ooooooooooooooooooooooooooo...XX       ",
"     ....ooooooooooooooooooooooooooo....X       ",
"     ....ooooooooooooooooooooooooooo....X       ",
"     ....ooooooooooooooooooooooooooo....XX      ",
"     ....ooooooooooooooooooooooooooo....XX      ",
"     ....ooooooooooooooooooooooooooo....XX      ",
"      ...ooooooooooooooooooooooooooo...XXX      ",
"      ...ooooooooooooooooooooooooooo...XXX      ",
"      .................................XX       ",
"      .................................XX       ",
"       ...............................XXX       ",
"       ...............................XXX       ",
"        .............................XXX        ",
"         ...........................XXXX        ",
"         ...........................XXX         ",
"          .........................XXX          ",
"           .......................XXXX          ",
"            X...................XXXXX           ",
"             X.................XXXXX            ",
"               X.............XXXXX              ",
"                XXXX.....XXXXXXXX               ",
"                  XXXXXXXXXXXXX                 ",
"                      XXXXX                     ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                "
};

/* Copyright (c) Julian Smart */
static const char *info_xpm[] = {
/* columns rows colors chars-per-pixel */
"48 48 9 1",
"$ c Black",
"O c #FFFFFF",
"@ c #808080",
"+ c #000080",
"o c #E8EB01",
"  c None",
"X c #FFFF40",
"# c #C0C0C0",
". c #ABAD01",
/* pixels */
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                     .....                      ",
"                   ..XXXXX..                    ",
"                 ..XXXXXXXXo..                  ",
"                .XXXOXXXXXXXoo.                 ",
"                .XOOXXX+XXXXXo.                 ",
"               .XOOOXX+++XXXXoo.                ",
"               .XOOXXX+++XXXXXo.                ",
"              .XOOOXXX+++XXXXXXo.               ",
"              .XOOXXXX+++XXXXXXo.               ",
"              .XXXXXXX+++XXXXXXX.               ",
"              .XXXXXXX+++XXXXXXo.               ",
"              .XXXXXXX+++XXXXXoo.               ",
"               .XXXXXX+++XXXXXo.                ",
"               .XXXXXXX+XXXXXXo.                ",
"                .XXXXXXXXXXXXo.                 ",
"                .XXXXX+++XXXoo.                 ",
"                 .XXXX+++XXoo.                  ",
"                  .XXXXXXXXo.                   ",
"                  ..XXXXXXo..                   ",
"                   .XXXXXo..                    ",
"                   @#######@                    ",
"                   @@@@@@@@@                    ",
"                   @#######@                    ",
"                   @@@@@@@@@                    ",
"                   @#######@                    ",
"                    @@@@@@@                     ",
"                      ###                       ",
"                      $$$                       ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                "
};

/* Copyright (c) Julian Smart */
static const char *warning_xpm[] = {
/* columns rows colors chars-per-pixel */
"48 48 9 1",
"@ c Black",
"o c #A6A800",
"+ c #8A8C00",
"$ c #B8BA00",
"  c None",
"O c #6E7000",
"X c #DCDF00",
". c #C00000",
"# c #373800",
/* pixels */
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                      .                         ",
"                     ...                        ",
"                     ...                        ",
"                    .....                       ",
"                   ...X..                       ",
"                   ..XXX..                      ",
"                  ...XXX...                     ",
"                  ..XXXXX..                     ",
"                 ..XXXXXX...                    ",
"                ...XXoO+XX..                    ",
"                ..XXXO@#XXX..                   ",
"               ..XXXXO@#XXX...                  ",
"              ...XXXXO@#XXXX..                  ",
"              ..XXXXXO@#XXXX...                 ",
"             ...XXXXXo@OXXXXX..                 ",
"            ...XXXXXXo@OXXXXXX..                ",
"            ..XXXXXXX$@OXXXXXX...               ",
"           ...XXXXXXXX@XXXXXXXX..               ",
"          ...XXXXXXXXXXXXXXXXXX...              ",
"          ..XXXXXXXXXXOXXXXXXXXX..              ",
"         ...XXXXXXXXXO@#XXXXXXXXX..             ",
"         ..XXXXXXXXXXX#XXXXXXXXXX...            ",
"        ...XXXXXXXXXXXXXXXXXXXXXXX..            ",
"       ...XXXXXXXXXXXXXXXXXXXXXXXX...           ",
"       ..............................           ",
"       ..............................           ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                "
};

/* Copyright (c) Julian Smart */
static const char *question_xpm[] = {
/* columns rows colors chars-per-pixel */
"48 48 21 1",
". c Black",
"> c #696969",
"O c #1F1F00",
"+ c #181818",
"o c #F6F900",
"; c #3F3F00",
"$ c #111111",
"  c None",
"& c #202020",
"X c #AAAA00",
"@ c #949400",
": c #303030",
"1 c #383838",
"% c #2A2A00",
", c #404040",
"= c #B4B400",
"- c #484848",
"# c #151500",
"< c #9F9F00",
"2 c #6A6A00",
"* c #353500",
/* pixels */
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                   .........                    ",
"                 ...XXXXXXX..                   ",
"               ..XXXXoooooXXXO+                 ",
"             ..XXooooooooooooX@..               ",
"            ..XoooooooooooooooXX#.              ",
"           $%XoooooooooooooooooXX#.             ",
"          &.XoooooooXXXXXXooooooXX..            ",
"          .XooooooXX.$...$XXoooooX*.            ",
"         $.XoooooX%.$     .*oooooo=..           ",
"         .XooooooX..      -.XoooooX..           ",
"         .XoooooX..+       .XoooooX;.           ",
"         ...XXXX..:        .XoooooX;.           ",
"          ........        >.XoooooX;.           ",
"                          +.XoooooX..           ",
"                         ,.Xoooooo<..           ",
"                        1#XooooooXO..           ",
"                       &#XooooooX2..            ",
"                      $%XooooooXX..             ",
"                     $%XooooooXX..              ",
"                    $%XooooooXX..               ",
"                   &.XooooooXX..                ",
"                   .XooooooXX..                 ",
"                  &.XoooooXX..                  ",
"                  ..XooooXX..                   ",
"                  ..XooooX...                   ",
"                  ..XXooXX..&                   ",
"                  ...XXXXX..                    ",
"                   ........                     ",
"                                                ",
"                                                ",
"                   .......                      ",
"                  ..XXXXX..                     ",
"                 ..XXoooXX..                    ",
"                 ..XoooooX..                    ",
"                 ..XoooooX..                    ",
"                 ..XXoooXX..                    ",
"                  ..XXXXX..                     ",
"                   .......                      ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                ",
"                                                "
};

wxBitmap wxWasmArtProvider::CreateBitmap(const wxArtID& id,
                                        const wxArtClient& WXUNUSED(client),
                                        const wxSize& WXUNUSED(size))
{
    if ( id == wxART_INFORMATION )
        return wxBitmap(info_xpm);
    if ( id == wxART_ERROR )
        return wxBitmap(error_xpm);
    if ( id == wxART_WARNING )
        return wxBitmap(warning_xpm);
    if ( id == wxART_QUESTION )
        return wxBitmap(question_xpm);
    return wxNullBitmap;
}


// ============================================================================
// wxInputHandler
// ============================================================================

// ----------------------------------------------------------------------------
// wxWasmInputHandler
// ----------------------------------------------------------------------------

bool wxWasmInputHandler::HandleKey(wxInputConsumer * WXUNUSED(control),
                                  const wxKeyEvent& WXUNUSED(event),
                                  bool WXUNUSED(pressed))
{
    return false;
}

bool wxWasmInputHandler::HandleMouse(wxInputConsumer *control,
                                    const wxMouseEvent& event)
{
    // clicking on the control gives it focus
    if ( event.ButtonDown() && wxWindow::FindFocus() != control->GetInputWindow() )
    {
        control->GetInputWindow()->SetFocus();

        return true;
    }

    return false;
}

bool wxWasmInputHandler::HandleMouseMove(wxInputConsumer *control,
                                        const wxMouseEvent& event)
{
    if ( event.Entering() )
    {
        control->GetInputWindow()->SetCurrent(true);
    }
    else if ( event.Leaving() )
    {
        control->GetInputWindow()->SetCurrent(false);
    }
    else
    {
        return false;
    }

    return true;
}

#if wxUSE_CHECKBOX

// ----------------------------------------------------------------------------
// wxWasmCheckboxInputHandler
// ----------------------------------------------------------------------------

bool wxWasmCheckboxInputHandler::HandleKey(wxInputConsumer *control,
                                          const wxKeyEvent& event,
                                          bool pressed)
{
    if ( pressed )
    {
        int keycode = event.GetKeyCode();
        if ( keycode == WXK_SPACE || keycode == WXK_RETURN )
        {
            control->PerformAction(wxACTION_CHECKBOX_TOGGLE);

            return true;
        }
    }

    return false;
}

#endif // wxUSE_CHECKBOX

#if wxUSE_TEXTCTRL

// ----------------------------------------------------------------------------
// wxWasmTextCtrlInputHandler
// ----------------------------------------------------------------------------

bool wxWasmTextCtrlInputHandler::HandleKey(wxInputConsumer *control,
                                          const wxKeyEvent& event,
                                          bool pressed)
{
    // handle only Wasm-specific text bindings here, the others are handled in
    // the base class
    if ( pressed )
    {
        wxControlAction action;
        int keycode = event.GetKeyCode();
        if ( event.ControlDown() )
        {
            switch ( keycode )
            {
                case 'A':
                    action = wxACTION_TEXT_HOME;
                    break;

                case 'B':
                    action = wxACTION_TEXT_LEFT;
                    break;

                case 'D':
                    action << wxACTION_TEXT_PREFIX_DEL << wxACTION_TEXT_RIGHT;
                    break;

                case 'E':
                    action = wxACTION_TEXT_END;
                    break;

                case 'F':
                    action = wxACTION_TEXT_RIGHT;
                    break;

                case 'H':
                    action << wxACTION_TEXT_PREFIX_DEL << wxACTION_TEXT_LEFT;
                    break;

                case 'K':
                    action << wxACTION_TEXT_PREFIX_DEL << wxACTION_TEXT_END;
                    break;

                case 'N':
                    action = wxACTION_TEXT_DOWN;
                    break;

                case 'P':
                    action = wxACTION_TEXT_UP;
                    break;

                case 'U':
                    //delete the entire line
                    control->PerformAction(wxACTION_TEXT_HOME);
                    action << wxACTION_TEXT_PREFIX_DEL << wxACTION_TEXT_END;
                    break;

                case 'W':
                    action << wxACTION_TEXT_PREFIX_DEL << wxACTION_TEXT_WORD_LEFT;
                    break;
            }
        }
        else if ( event.AltDown() )
        {
            switch ( keycode )
            {
                case 'B':
                    action = wxACTION_TEXT_WORD_LEFT;
                    break;

                case 'D':
                    action << wxACTION_TEXT_PREFIX_DEL << wxACTION_TEXT_WORD_RIGHT;
                    break;

                case 'F':
                    action = wxACTION_TEXT_WORD_RIGHT;
                    break;
            }
        }

        if ( action != wxACTION_NONE )
        {
            control->PerformAction(action);

            return true;
        }
    }

    return wxStdInputHandler::HandleKey(control, event, pressed);
}

#endif // wxUSE_TEXTCTRL

#endif // wxUSE_THEME_WASM
