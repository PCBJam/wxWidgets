/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/textctrl.cpp
// Purpose:     wxTextCtrl implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_TEXTCTRL

#include "wx/textctrl.h"

#include "wx/wasm/private/dom.h"

wxTextCtrl::wxTextCtrl()
{
    m_modified = false;
}

wxTextCtrl::wxTextCtrl(wxWindow *parent, wxWindowID id,
                       const wxString& value,
                       const wxPoint& pos,
                       const wxSize& size, long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    m_modified = false;

    Create(parent, id, value, pos, size, style, validator, name);
}

bool wxTextCtrl::Create(wxWindow *parent, wxWindowID id,
                        const wxString& value,
                        const wxPoint& pos,
                        const wxSize& size, long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    if (style & wxTE_MULTILINE)
        WasmCreateDomNode("textarea");
    else if (style & wxTE_PASSWORD)
        WasmCreateDomNode("input", "password");
    else
        WasmCreateDomNode("input", "text");

    // set the initial contents without generating a wxEVT_TEXT event
    ChangeValue(value);

    if (WasmGetDomId() && (style & wxTE_READONLY))
        SetEditable(false);

    return true;
}

// ----------------------------------------------------------------------------
// dirty flag
// ----------------------------------------------------------------------------

bool wxTextCtrl::IsModified() const
{
    return m_modified;
}

void wxTextCtrl::MarkDirty()
{
    m_modified = true;
}

void wxTextCtrl::DiscardEdits()
{
    m_modified = false;
}

void wxTextCtrl::WriteText(const wxString& text)
{
    // The base mixin pushes both the new value and the caret position into
    // the DOM element (pushing the value again here would reset the DOM
    // caret to the end, undoing the caret push).
    wxTextEntry::WriteText(text);

    MarkDirty();
}

void wxTextCtrl::DoSetValue(const wxString& value, int flags)
{
    wxTextEntry::DoSetValue(value, flags);

    // Push the programmatic value into the DOM element. (The 'input' event path
    // goes through wxTextEntry::DoSetValue directly, bypassing this push, so we
    // never echo a value that just came FROM the element.)
    if (WasmGetDomId())
        wxDomSetValue(WasmGetDomId(), value);

    // setting the value programmatically resets the modified flag, as if the
    // contents had just been loaded
    m_modified = false;
}

void wxTextCtrl::OnDomEvent(wxDomEventKind kind)
{
    switch (kind)
    {
        case wxDOM_EVENT_INPUT:
        {
            // The element already holds the typed text, so update the wx cache
            // and fire wxEVT_TEXT WITHOUT echoing the value back into the DOM:
            // call the base DoSetValue directly (the wxTextCtrl override would
            // push). This keeps no in-dom-input flag, so a wxEVT_TEXT handler
            // that throws cannot wedge later programmatic SetValue/ChangeValue.
            const wxString value = wxDomGetValue(WasmGetDomId());
            wxTextEntry::DoSetValue(value, SetValue_SendEvent);
            m_modified = true;
            return;
        }

        case wxDOM_EVENT_ENTER:
            if (GetWindowStyle() & wxTE_PROCESS_ENTER)
            {
                wxCommandEvent event(wxEVT_TEXT_ENTER, GetId());
                event.SetEventObject(this);
                event.SetString(GetValue());
                HandleWindowEvent(event);
            }
            return;

        default:
            wxControl::OnDomEvent(kind);
            return;
    }
}

// ----------------------------------------------------------------------------
// multiline position arithmetic (computed from the cached value)
// ----------------------------------------------------------------------------

int wxTextCtrl::GetNumberOfLines() const
{
    // even an empty control has one (empty) line
    return static_cast<int>(GetValue().Freq(wxT('\n'))) + 1;
}

wxString wxTextCtrl::GetLineText(long lineNo) const
{
    if (lineNo < 0)
        return wxString();

    const wxString value = GetValue();

    // find the start of the line
    size_t start = 0;
    for (long line = 0; line < lineNo; line++)
    {
        start = value.find(wxT('\n'), start);
        if (start == wxString::npos)
            return wxString();

        start++; // skip the newline itself
    }

    size_t end = value.find(wxT('\n'), start);
    if (end == wxString::npos)
        end = value.length();

    return value.Mid(start, end - start);
}

int wxTextCtrl::GetLineLength(long lineNo) const
{
    if (lineNo < 0 || lineNo >= GetNumberOfLines())
        return -1;

    return static_cast<int>(GetLineText(lineNo).length());
}

long wxTextCtrl::XYToPosition(long x, long y) const
{
    if (x < 0 || y < 0)
        return -1;

    const wxString value = GetValue();

    // find the start of line y
    size_t start = 0;
    for (long line = 0; line < y; line++)
    {
        start = value.find(wxT('\n'), start);
        if (start == wxString::npos)
            return -1;

        start++; // skip the newline itself
    }

    size_t end = value.find(wxT('\n'), start);
    if (end == wxString::npos)
        end = value.length();

    if (static_cast<size_t>(x) > end - start)
        return -1;

    return static_cast<long>(start) + x;
}

bool wxTextCtrl::PositionToXY(long pos, long *x, long *y) const
{
    const wxString value = GetValue();

    if (pos < 0 || pos > static_cast<long>(value.length()))
        return false;

    long col = 0;
    long line = 0;
    for (long i = 0; i < pos; i++)
    {
        if (value[i] == wxT('\n'))
        {
            line++;
            col = 0;
        }
        else
        {
            col++;
        }
    }

    if (x)
        *x = col;
    if (y)
        *y = line;

    return true;
}

void wxTextCtrl::ShowPosition(long WXUNUSED(pos))
{
    // TODO(dom-phase-2): scroll the DOM element to make the position visible.
}

#endif // wxUSE_TEXTCTRL
