/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/clipbrd.h
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////


#ifndef _WX_CLIPBRD_H_
#define _WX_CLIPBRD_H_

#if wxUSE_CLIPBOARD

//-----------------------------------------------------------------------------
// wxClipboard
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxClipboard : public wxClipboardBase
{
public:
    wxClipboard() {}
    virtual ~wxClipboard() {}

    // open the clipboard before SetData() and GetData()
    virtual bool Open() { return false; }

    // close the clipboard after SetData() and GetData()
    virtual void Close() {}

    // query whether the clipboard is opened
    virtual bool IsOpened() const { return false; }

    // set the clipboard data. all other formats will be deleted.
    virtual bool SetData(wxDataObject *WXUNUSED(data)) { return false; }

    // add to the clipboard data.
    virtual bool AddData(wxDataObject *WXUNUSED(data)) { return false; }

    // ask if data in correct format is available
    virtual bool IsSupported(const wxDataFormat& WXUNUSED(format)) { return false; }

    // fill data with data on the clipboard (if available)
    virtual bool GetData(wxDataObject& WXUNUSED(data))  { return false; }

    // clears wxTheClipboard and the system's clipboard if possible
    virtual void Clear() {}

    // flushes the clipboard: this means that the data which is currently on
    // clipboard will stay available even after the application exits (possibly
    // eating memory), otherwise the clipboard will be emptied on exit
    virtual bool Flush() { return false; }

private:
    wxDECLARE_DYNAMIC_CLASS(wxClipboard);
};

#endif // wxUSE_CLIPBOARD

#endif // _WX_CLIPBRD_H_
