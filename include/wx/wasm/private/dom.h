/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/private/dom.h
// Purpose:     C++ -> JS bridge for the WASM DOM port's native controls.
//              Wraps the window.wxDom* functions defined in wx-dom.js
//              (only loaded in DOM-port bundles; never in canvas builds).
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_PRIVATE_DOM_H__
#define __WX_WASM_PRIVATE_DOM_H__

#ifndef __WXUNIVERSAL__

#include <emscripten.h>

#include "wx/string.h"

class wxWindowWasm;

// domId -> window routing table, maintained by src/wasm/domevents.cpp.
void wxDomRegisterWindow(int domId, wxWindowWasm *window);
void wxDomUnregisterWindow(int domId);

// Creates an absolutely positioned element of the given tag inside the
// top-level window's container div. Returns a JS-side dom id (> 0) or 0.
inline int wxDomCreateControl(int tlwCssId, const char *tag, const char *typeAttr)
{
    return EM_ASM_INT({
        return wxDomCreateControl($0, UTF8ToString($1), UTF8ToString($2));
    }, tlwCssId, tag, typeAttr ? typeAttr : "");
}

inline void wxDomDestroyControl(int domId)
{
    EM_ASM({ wxDomDestroyControl($0); }, domId);
}

// x/y are relative to the top-level window's container div.
inline void wxDomSetRect(int domId, int x, int y, int w, int h)
{
    EM_ASM({ wxDomSetRect($0, $1, $2, $3, $4); }, domId, x, y, w, h);
}

// textContent — for <button>, <span>, <label> style elements.
inline void wxDomSetText(int domId, const wxString& text)
{
    EM_ASM({ wxDomSetText($0, UTF8ToString($1)); }, domId, (const char *)text.utf8_str());
}

// value property — for <input>/<textarea>.
inline void wxDomSetValue(int domId, const wxString& value)
{
    EM_ASM({ wxDomSetValue($0, UTF8ToString($1)); }, domId, (const char *)value.utf8_str());
}

// Returns the current value property (typed text) of an <input>/<textarea>.
inline wxString wxDomGetValue(int domId)
{
    char *s = (char *)EM_ASM_PTR({
        return stringToNewUTF8(wxDomGetValue($0));
    }, domId);
    wxString result = wxString::FromUTF8(s);
    free(s);
    return result;
}

inline void wxDomSetEnabled(int domId, bool enabled)
{
    EM_ASM({ wxDomSetEnabled($0, $1); }, domId, enabled);
}

inline void wxDomSetReadOnly(int domId, bool readOnly)
{
    EM_ASM({ wxDomSetReadOnly($0, $1); }, domId, readOnly);
}

inline void wxDomSetShown(int domId, bool shown)
{
    EM_ASM({ wxDomSetShown($0, $1); }, domId, shown);
}

inline void wxDomFocus(int domId)
{
    EM_ASM({ wxDomFocus($0); }, domId);
}

// CSS font string — the port's wxFont native info desc IS a CSS font.
inline void wxDomSetFont(int domId, const wxString& cssFont)
{
    EM_ASM({ wxDomSetFont($0, UTF8ToString($1)); }, domId, (const char *)cssFont.utf8_str());
}

inline void wxDomSetAriaLabel(int domId, const wxString& label)
{
    EM_ASM({ wxDomSetAriaLabel($0, UTF8ToString($1)); }, domId, (const char *)label.utf8_str());
}

// Intrinsic (content-driven) size of the live element: width/height packed
// as (w << 16) | h. Used by DoGetBestSize before sizer layout.
inline void wxDomGetIntrinsicSize(int domId, int *w, int *h)
{
    int packed = EM_ASM_INT({ return wxDomIntrinsicSize($0); }, domId);
    if ( w ) *w = (packed >> 16) & 0xffff;
    if ( h ) *h = packed & 0xffff;
}

#endif // !__WXUNIVERSAL__

#endif // __WX_WASM_PRIVATE_DOM_H__
