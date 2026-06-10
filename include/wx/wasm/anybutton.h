/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/anybutton.h
// Purpose:     wxAnyButton class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_ANYBUTTON_H__
#define __WX_WASM_ANYBUTTON_H__

class WXDLLIMPEXP_CORE wxAnyButton : public wxAnyButtonBase
{
public:
    wxAnyButton() { }

protected:
    virtual wxBitmap DoGetBitmap(State state) const wxOVERRIDE;
    virtual void DoSetBitmap(const wxBitmapBundle& bitmap, State which) wxOVERRIDE;

    wxBitmapBundle m_bitmaps[State_Max];

private:
    wxDECLARE_NO_COPY_CLASS(wxAnyButton);
};

#endif // __WX_WASM_ANYBUTTON_H__
