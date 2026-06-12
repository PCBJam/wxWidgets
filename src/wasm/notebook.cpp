/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/notebook.cpp
// Purpose:     wxNotebook implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_NOTEBOOK

#include "wx/notebook.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
#endif

#include "wx/wasm/private/dom.h"

// NB: the wxNotebook RTTI is implemented by src/common/nbkbase.cpp
// (wxIMPLEMENT_DYNAMIC_CLASS_XTI) — do not re-implement it here.

// ----------------------------------------------------------------------------
// construction
// ----------------------------------------------------------------------------

void wxNotebook::Init()
{
    m_stripHeight = -1;
}

wxNotebook::wxNotebook()
{
    Init();
}

wxNotebook::wxNotebook(wxWindow *parent,
                       wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name)
{
    Init();

    Create(parent, id, pos, size, style, name);
}

bool wxNotebook::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name)
{
    if ( (style & wxBK_ALIGN_MASK) == wxBK_DEFAULT )
        style |= wxBK_TOP;

    if ( !wxBookCtrlBase::Create(parent, id, pos, size, style, name) )
        return false;

    WasmCreateDomNode("notebook");
    WasmRebuildTabs();

    return true;
}

// ----------------------------------------------------------------------------
// the DOM tab strip
// ----------------------------------------------------------------------------

int wxNotebook::StripHeight() const
{
    if ( m_stripHeight <= 0 )
    {
        if ( WasmGetDomId() )
            m_stripHeight = wxDomNotebookStripHeight(WasmGetDomId());

        if ( m_stripHeight <= 0 )
            m_stripHeight = 28; // sane fallback before realization
    }

    return m_stripHeight;
}

void wxNotebook::WasmRebuildTabs()
{
    if ( !WasmGetDomId() )
        return;

    wxString json(wxT("["));

    for ( size_t n = 0; n < m_titles.GetCount(); n++ )
    {
        if ( n > 0 )
            json += wxT(",");

        json += wxT("{\"label\":\"");
        json += wxDomJsonEscape(m_titles[n]);
        json += wxString::Format(wxT("\",\"selected\":%s}"),
                                 (int)n == m_selection ? "true" : "false");
    }

    json += wxT("]");

    wxDomNotebookSetTabs(WasmGetDomId(), json);

    // The strip content changed — re-measure NOW. The page area's origin
    // depends on the strip height, but the pages' wx-side (client)
    // positions do not change with it, so no DoMoveWindow fires anywhere:
    // if the height differs from what earlier layout used, the DOM
    // projection and the element registry's screen rects go stale (pages
    // rendered into / clipped by the strip area). Re-project explicitly.
    const int oldStrip = m_stripHeight;
    m_stripHeight = -1;
    const int newStrip = StripHeight();
    if ( newStrip != oldStrip )
    {
        InvalidateBestSize();
        DoSize();                       // page sizes track the client area
        UpdateDomGeometry();            // re-project positions + clips
        UpdateChildrenDOMVisibility();  // refresh registry screen rects
    }
}

// ----------------------------------------------------------------------------
// page titles and images
// ----------------------------------------------------------------------------

bool wxNotebook::SetPageText(size_t n, const wxString& strText)
{
    wxCHECK_MSG( n < m_titles.GetCount(), false, wxT("invalid notebook page") );

    m_titles[n] = strText;
    WasmRebuildTabs();

    return true;
}

wxString wxNotebook::GetPageText(size_t n) const
{
    wxCHECK_MSG( n < m_titles.GetCount(), wxString(),
                 wxT("invalid notebook page") );

    return m_titles[n];
}

int wxNotebook::GetPageImage(size_t n) const
{
    wxCHECK_MSG( n < m_images.size(), NO_IMAGE, wxT("invalid notebook page") );

    return m_images[n];
}

bool wxNotebook::SetPageImage(size_t n, int imageId)
{
    wxCHECK_MSG( n < m_images.size(), false, wxT("invalid notebook page") );

    // stored but not rendered (no tab images in the DOM strip yet)
    m_images[n] = imageId;

    return true;
}

// ----------------------------------------------------------------------------
// adding/removing pages
// ----------------------------------------------------------------------------

bool wxNotebook::InsertPage(size_t n,
                            wxWindow *page,
                            const wxString& text,
                            bool bSelect,
                            int imageId)
{
    wxCHECK_MSG( page, false, wxT("NULL page in wxNotebook::InsertPage") );

    // pages start hidden; DoSetSelection shows the selected one
    page->Show(false);

    if ( !wxBookCtrlBase::InsertPage(n, page, text, bSelect, imageId) )
        return false;

    m_titles.Insert(text, n);
    m_images.insert(m_images.begin() + n, imageId);

    // keep m_selection pointing at the same page (no events)
    if ( m_selection != wxNOT_FOUND && n <= (size_t)m_selection )
        m_selection++;

    if ( !DoSetSelectionAfterInsertion(n, bSelect) )
        page->Hide();

    WasmRebuildTabs();

    // lay the new page out into the page area
    DoSize();

    return true;
}

wxWindow *wxNotebook::DoRemovePage(size_t page)
{
    wxWindow *win = wxBookCtrlBase::DoRemovePage(page);
    if ( !win )
        return NULL;

    m_titles.RemoveAt(page);
    m_images.erase(m_images.begin() + page);

    DoSetSelectionAfterRemoval(page);

    WasmRebuildTabs();

    return win;
}

bool wxNotebook::DeleteAllPages()
{
    if ( !wxBookCtrlBase::DeleteAllPages() )
        return false;

    m_titles.Clear();
    m_images.clear();

    WasmRebuildTabs();

    return true;
}

// ----------------------------------------------------------------------------
// selection
// ----------------------------------------------------------------------------

void wxNotebook::UpdateSelectedPage(size_t newsel)
{
    m_selection = (int)newsel;

    // refresh the strip's selected styling (and the registry mirror)
    WasmRebuildTabs();
}

wxBookCtrlEvent* wxNotebook::CreatePageChangingEvent() const
{
    return new wxBookCtrlEvent(wxEVT_NOTEBOOK_PAGE_CHANGING,
                               m_windowId);
}

void wxNotebook::MakeChangedEvent(wxBookCtrlEvent& event)
{
    event.SetEventType(wxEVT_NOTEBOOK_PAGE_CHANGED);
}

void wxNotebook::OnDomEvent(wxDomEventKind kind)
{
    if ( kind == wxDOM_EVENT_TAB )
    {
        const int idx = wxDomGetLastCommandId(WasmGetDomId());

        if ( idx >= 0 && (size_t)idx < GetPageCount() && idx != m_selection )
            SetSelection(idx);

        SetFocus();
        return;
    }

    wxBookCtrlBase::OnDomEvent(kind);
}

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

wxPoint wxNotebook::GetClientAreaOrigin() const
{
    // only wxBK_TOP is rendered; the strip occupies the top of the window
    return wxPoint(0, StripHeight());
}

void wxNotebook::DoGetClientSize(int *width, int *height) const
{
    wxBookCtrlBase::DoGetClientSize(width, height);

    if ( height )
    {
        *height -= StripHeight();
        if ( *height < 0 )
            *height = 0;
    }
}

void wxNotebook::DoSetClientSize(int width, int height)
{
    wxBookCtrlBase::DoSetClientSize(width, height + StripHeight());
}

wxSize wxNotebook::CalcSizeFromPage(const wxSize& sizePage) const
{
    return wxSize(sizePage.x, sizePage.y + StripHeight());
}

void wxNotebook::DoSize()
{
    // base DoSize() early-returns when there is no controller window
    // (m_bookctrl); size every page to fill the page area ourselves
    const wxSize size = GetClientSize(); // already excludes the strip

    for ( size_t n = 0; n < m_pages.size(); n++ )
    {
        wxWindow *page = m_pages[n];
        if ( page )
            page->SetSize(0, 0, size.x, size.y);
    }
}

#endif // wxUSE_NOTEBOOK
