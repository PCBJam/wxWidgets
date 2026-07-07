/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/stattext.cpp
// Purpose:     wxStaticText implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_STATTEXT

#include "wx/stattext.h"

#include "wx/wasm/private/dom.h"

wxStaticText::wxStaticText()
{
}

wxStaticText::wxStaticText(wxWindow *parent,
                           wxWindowID id,
                           const wxString& label,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxString& name)
{
    Create(parent, id, label, pos, size, style, name);
}

bool wxStaticText::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxString& label,
                          const wxPoint& pos,
                          const wxSize& size,
                          long style,
                          const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, wxDefaultValidator, name))
        return false;

    WasmCreateDomNode("span");

    // An ellipsized label (wxST_ELLIPSIZE_*) is first truncated by SetLabel()
    // against the control's initial — often near-zero — client size, before any
    // sizer has assigned it a real width. Re-ellipsize on every resize so the
    // text expands once the (e.g. growable-column) width is known. This mirrors
    // the native ports, which call UpdateLabel() on size changes.
    Bind(wxEVT_SIZE, &wxStaticText::OnSize, this);

    SetLabel(label);

    return true;
}

void wxStaticText::OnSize(wxSizeEvent& event)
{
    // UpdateLabel() recomputes GetEllipsizedLabel() against the current client
    // size and pushes it through WXSetVisibleLabel(). It is a no-op when the
    // control is not ellipsized or when the ellipsized text is unchanged, so it
    // neither touches plain labels nor feeds back into the layout (the best size
    // is fixed and only the DOM text changes).
    UpdateLabel();
    event.Skip();
}

void wxStaticText::SetLabel(const wxString& label)
{
    if (label == m_labelOrig)
        return;

    // stores m_labelOrig and invalidates the best size
    wxControl::SetLabel(label);

    WXSetVisibleLabel(GetEllipsizedLabel());

    AutoResizeIfNecessary();
}

wxString wxStaticText::WXGetVisibleLabel() const
{
    return m_visibleLabel;
}

void wxStaticText::WXSetVisibleLabel(const wxString& str)
{
    m_visibleLabel = str;

    // Consume the wx mnemonic marker before showing the text, like the native
    // ports (e.g. wxGTK's GTKConvertMnemonics + gtk_label_set_text_with_mnemonic):
    // a single '&' marks the (here unusable) mnemonic and is removed, "&&"
    // collapses to a literal '&'. Otherwise KiCad's '&'-bearing dialog labels
    // render with a stray ampersand.
    if (WasmGetDomId())
        wxDomSetText(WasmGetDomId(), RemoveMnemonics(m_visibleLabel));
}

#endif // wxUSE_STATTEXT
