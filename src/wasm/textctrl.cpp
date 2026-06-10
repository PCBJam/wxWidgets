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
    m_inDomInput = false;
}

wxTextCtrl::wxTextCtrl(wxWindow *parent, wxWindowID id,
                       const wxString& value,
                       const wxPoint& pos,
                       const wxSize& size, long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    m_modified = false;
    m_inDomInput = false;

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
    wxTextEntry::WriteText(text);

    // WriteText/AppendText mutate the cache directly (not via DoSetValue),
    // so push the result into the DOM element here.
    if (WasmGetDomId() && !m_inDomInput)
        wxDomSetValue(WasmGetDomId(), GetValue());

    MarkDirty();
}

void wxTextCtrl::DoSetValue(const wxString& value, int flags)
{
    wxTextEntry::DoSetValue(value, flags);

    // push into the DOM element — unless the new value just came FROM the
    // element ('input' event), where echoing it back would move the caret
    if (WasmGetDomId() && !m_inDomInput)
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
            // Pull the typed text into the wxTextEntry cache and fire
            // wxEVT_TEXT, like any port does for user edits.
            m_inDomInput = true;
            const wxString value = wxDomGetValue(WasmGetDomId());
            DoSetValue(value, SetValue_SendEvent);
            m_inDomInput = false;
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
