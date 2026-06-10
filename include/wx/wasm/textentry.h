/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/textentry.h
// Purpose:     wxTextEntry class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_TEXTENTRY_H__
#define __WX_WASM_TEXTENTRY_H__

// wxTextEntry is a mixin (not a window): the text contents, insertion point
// and selection are kept in a C++-side cache until the DOM element backing
// the entry exists.
class WXDLLIMPEXP_CORE wxTextEntry : public wxTextEntryBase
{
public:
    wxTextEntry();

    virtual void WriteText(const wxString& text) wxOVERRIDE;

    virtual void Remove(long from, long to) wxOVERRIDE;

    virtual void Copy() wxOVERRIDE;
    virtual void Cut() wxOVERRIDE;
    virtual void Paste() wxOVERRIDE;

    virtual void Undo() wxOVERRIDE;
    virtual void Redo() wxOVERRIDE;
    virtual bool CanUndo() const wxOVERRIDE;
    virtual bool CanRedo() const wxOVERRIDE;

    virtual void SetInsertionPoint(long pos) wxOVERRIDE;
    virtual long GetInsertionPoint() const wxOVERRIDE;
    virtual long GetLastPosition() const wxOVERRIDE;

    virtual void SetSelection(long from, long to) wxOVERRIDE;
    virtual void GetSelection(long *from, long *to) const wxOVERRIDE;

    virtual bool IsEditable() const wxOVERRIDE;
    virtual void SetEditable(bool editable) wxOVERRIDE;

protected:
    virtual wxString DoGetValue() const wxOVERRIDE;
    virtual void DoSetValue(const wxString& value, int flags = 0) wxOVERRIDE;

    virtual wxWindow *GetEditableWindow() wxOVERRIDE;

private:
    // send wxEVT_TEXT if events are allowed and we have a window to send
    // them from
    void NotifyTextChanged();

    // clamp the given position to the valid range [0, length], -1 meaning
    // "end of text"
    long NormalizePos(long pos) const;

    wxString m_value;           // cached contents

    long m_insertionPoint;

    // selection range, both -1 when there is no selection
    long m_selectionStart;
    long m_selectionEnd;

    bool m_editable;
};

#endif // __WX_WASM_TEXTENTRY_H__
