/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/tooltip.h
// Purpose:     wxToolTip for the WASM DOM port: maps to the HTML title
//              attribute of the owner's DOM element.
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_TOOLTIP_H__
#define __WX_WASM_TOOLTIP_H__

#include "wx/object.h"
#include "wx/string.h"

class WXDLLIMPEXP_FWD_CORE wxWindow;

class WXDLLIMPEXP_CORE wxToolTip : public wxObject
{
public:
    wxToolTip(const wxString& tip);
    virtual ~wxToolTip();

    void SetTip(const wxString& tip);
    const wxString& GetTip() const { return m_text; }

    wxWindow *GetWindow() const { return m_window; }
    void SetWindow(wxWindow *win);

    // global settings: browser-native tooltips can't be configured
    static void Enable(bool WXUNUSED(flag)) { }
    static void SetDelay(long WXUNUSED(msecs)) { }
    static void SetAutoPop(long WXUNUSED(msecs)) { }
    static void SetReshow(long WXUNUSED(msecs)) { }
    static void SetMaxWidth(int WXUNUSED(width)) { }

private:
    void Push();

    wxString m_text;
    wxWindow *m_window;

    wxDECLARE_ABSTRACT_CLASS(wxToolTip);
};

#endif // __WX_WASM_TOOLTIP_H__
