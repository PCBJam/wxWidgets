/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/textentry.cpp
// Purpose:     wxTextEntry implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_TEXTCTRL || wxUSE_COMBOBOX

#include "wx/textentry.h"

#include "wx/wasm/private/dom.h"

#ifndef WX_PRECOMP
    #include "wx/window.h"
    #include "wx/utils.h"           // for wxSwap()
#endif

wxTextEntry::wxTextEntry()
{
    m_insertionPoint = 0;
    m_selectionStart =
    m_selectionEnd = -1;
    m_editable = true;
}

void wxTextEntry::NotifyTextChanged()
{
    // a bare wxTextEntry has no associated window (GetEditableWindow()
    // returns NULL until a derived class overrides it), in which case there
    // is nobody to notify
    if ( GetEditableWindow() )
        SendTextUpdatedEventIfAllowed();
}

long wxTextEntry::NormalizePos(long pos) const
{
    const long last = static_cast<long>(m_value.length());

    if ( pos == -1 || pos > last )
        return last;

    return pos < 0 ? 0 : pos;
}

int wxTextEntry::WasmDomId() const
{
    // GetEditableWindow() is conceptually const but wx declares it non-const
    wxWindow * const win = const_cast<wxTextEntry *>(this)->GetEditableWindow();
    return win ? win->WasmGetDomId() : 0;
}

void wxTextEntry::WriteText(const wxString& text)
{
    // writing text replaces the current selection, if any — use the live
    // DOM selection/caret (GetSelection reads it when the element exists),
    // not the stale cache
    long selStart = 0, selEnd = 0;
    GetSelection(&selStart, &selEnd);

    long pos;
    if ( selStart < selEnd )
    {
        pos = NormalizePos(selStart);
        m_value.erase(pos, NormalizePos(selEnd) - pos);
    }
    else
    {
        pos = NormalizePos(selStart);
    }

    m_value.insert(pos, text);

    m_insertionPoint = pos + static_cast<long>(text.length());
    m_selectionStart =
    m_selectionEnd = -1;

    // Mirror into the DOM: the value first (setSelectionRange clamps against
    // the element's current value), then the caret after the inserted text.
    if ( const int domId = WasmDomId() )
    {
        wxDomSetValue(domId, m_value);
        wxDomSetSelection(domId, (int)m_insertionPoint, (int)m_insertionPoint);
    }

    NotifyTextChanged();
}

void wxTextEntry::Remove(long from, long to)
{
    from = NormalizePos(from);
    to = NormalizePos(to);

    if ( from > to )
        wxSwap(from, to);

    if ( from == to )
        return;

    m_value.erase(from, to - from);

    m_insertionPoint = from;
    m_selectionStart =
    m_selectionEnd = -1;

    // TODO(dom-phase-2): mirror the new value into the DOM element.

    NotifyTextChanged();
}

void wxTextEntry::Copy()
{
    // TODO(dom-phase-2): use the (async) browser clipboard API.
}

void wxTextEntry::Cut()
{
    // TODO(dom-phase-2): use the (async) browser clipboard API.
}

void wxTextEntry::Paste()
{
    // TODO(dom-phase-2): use the (async) browser clipboard API.
}

void wxTextEntry::Undo()
{
    // TODO(dom-phase-2): hook into the DOM element's undo support, if any.
}

void wxTextEntry::Redo()
{
    // TODO(dom-phase-2): hook into the DOM element's redo support, if any.
}

bool wxTextEntry::CanUndo() const
{
    return false;
}

bool wxTextEntry::CanRedo() const
{
    return false;
}

void wxTextEntry::SetInsertionPoint(long pos)
{
    m_insertionPoint = NormalizePos(pos);

    // moving the insertion point removes any current selection
    m_selectionStart =
    m_selectionEnd = -1;

    if ( const int domId = WasmDomId() )
        wxDomSetSelection(domId, (int)m_insertionPoint, (int)m_insertionPoint);
}

long wxTextEntry::GetInsertionPoint() const
{
    // read the live caret from the DOM element when it exists
    if ( const int domId = WasmDomId() )
    {
        const int start = wxDomGetSelectionStart(domId);
        if ( start >= 0 )
            return start;
    }

    // the cached value may be stale if the text shrank since it was set
    return NormalizePos(m_insertionPoint);
}

long wxTextEntry::GetLastPosition() const
{
    return static_cast<long>(m_value.length());
}

void wxTextEntry::SetSelection(long from, long to)
{
    // (-1, -1) means "select all"
    if ( from == -1 && to == -1 )
    {
        from = 0;
        to = GetLastPosition();
    }

    from = NormalizePos(from);
    to = NormalizePos(to);

    if ( from > to )
        wxSwap(from, to);

    if ( from == to )
    {
        // empty selection: just move the insertion point
        m_selectionStart =
        m_selectionEnd = -1;
    }
    else
    {
        m_selectionStart = from;
        m_selectionEnd = to;
    }

    m_insertionPoint = to;

    if ( const int domId = WasmDomId() )
        wxDomSetSelection(domId, (int)from, (int)to);
}

void wxTextEntry::GetSelection(long *from, long *to) const
{
    long start, end;

    // read the live selection from the DOM element when it exists; a
    // collapsed DOM selection (start == end) is exactly wx's "no selection,
    // both values are the caret position" convention
    int domStart = -1, domEnd = -1;
    if ( const int domId = WasmDomId() )
    {
        domStart = wxDomGetSelectionStart(domId);
        domEnd = wxDomGetSelectionEnd(domId);
    }

    if ( domStart >= 0 && domEnd >= 0 )
    {
        start = domStart;
        end = domEnd;
    }
    else if ( m_selectionStart != -1 )
    {
        start = m_selectionStart;
        end = m_selectionEnd;
    }
    else
    {
        // no selection: both values are the insertion point position
        start =
        end = GetInsertionPoint();
    }

    if ( from )
        *from = start;
    if ( to )
        *to = end;
}

bool wxTextEntry::IsEditable() const
{
    return m_editable;
}

void wxTextEntry::SetEditable(bool editable)
{
    m_editable = editable;

    wxWindow * const win = GetEditableWindow();
    if (win && win->WasmGetDomId())
        wxDomSetReadOnly(win->WasmGetDomId(), !editable);
}

wxString wxTextEntry::DoGetValue() const
{
    return m_value;
}

void wxTextEntry::DoSetValue(const wxString& value, int flags)
{
    m_value = value;

    m_insertionPoint = 0;
    m_selectionStart =
    m_selectionEnd = -1;

    // TODO(dom-phase-2): mirror the new value into the DOM element.

    if ( flags & SetValue_SendEvent )
        NotifyTextChanged();
}

wxWindow *wxTextEntry::GetEditableWindow()
{
    // overridden by the classes really using this mixin (e.g. wxTextCtrl)
    return NULL;
}

#endif // wxUSE_TEXTCTRL || wxUSE_COMBOBOX
