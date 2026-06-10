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
#include "wx/arrstr.h"

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

// Boolean state: checkbox/radio checked, toggle-button pressed.
inline void wxDomSetBoolValue(int domId, bool on)
{
    EM_ASM({ wxDomSetBoolValue($0, $1); }, domId, on);
}

inline bool wxDomGetBoolValue(int domId)
{
    return EM_ASM_INT({ return wxDomGetBoolValue($0); }, domId) != 0;
}

// Numeric state: gauge/slider value.
inline void wxDomSetIntValue(int domId, int value)
{
    EM_ASM({ wxDomSetIntValue($0, $1); }, domId, value);
}

inline int wxDomGetIntValue(int domId)
{
    return EM_ASM_INT({ return wxDomGetIntValue($0); }, domId);
}

inline void wxDomSetRange(int domId, int minVal, int maxVal)
{
    EM_ASM({ wxDomSetRange($0, $1, $2); }, domId, minVal, maxVal);
}

// HTML radio exclusivity: same group name = browser-exclusive group.
inline void wxDomSetGroupName(int domId, const wxString& name)
{
    EM_ASM({ wxDomSetGroupName($0, UTF8ToString($1)); },
           domId, (const char *)name.utf8_str());
}

// Item lists (select/radiobox): pass items joined with the \x1f unit
// separator (cannot occur in wx labels).
inline void wxDomSetItems(int domId, const wxArrayString& items)
{
    wxString joined;
    for ( size_t i = 0; i < items.size(); i++ )
    {
        if ( i > 0 )
            joined += wxT('\x1f');
        joined += items[i];
    }
    EM_ASM({ wxDomSetItems($0, UTF8ToString($1)); },
           domId, (const char *)joined.utf8_str());
}

inline void wxDomSetItemSelected(int domId, int index, bool selected)
{
    EM_ASM({ wxDomSetItemSelected($0, $1, $2); }, domId, index, selected);
}

// Selected indices of a multi-select listbox, comma-joined ("" = none).
inline wxString wxDomGetSelectedIndices(int domId)
{
    char *s = (char *)EM_ASM_PTR({
        return stringToNewUTF8(wxDomGetSelectedIndices($0));
    }, domId);
    wxString result = wxString::FromUTF8(s);
    free(s);
    return result;
}

// width/height are the wx bitmap dimensions: images load asynchronously,
// so explicit sizes are required for correct best-size measurement.
inline void wxDomSetImageDataURL(int domId, const wxString& dataUrl,
                                 int width, int height)
{
    EM_ASM({ wxDomSetImage($0, UTF8ToString($1), $2, $3); },
           domId, (const char *)dataUrl.utf8_str(), width, height);
}

class wxBitmap;

// PNG data URL for a bitmap (implemented in src/wasm/domevents.cpp);
// empty string if the bitmap is invalid or encoding fails.
wxString wxDomBitmapToDataURL(const wxBitmap& bitmap);

// Minimal JSON string escaping for the menu/toolbar structure payloads.
inline wxString wxDomJsonEscape(const wxString& s)
{
    wxString out;
    out.reserve(s.length() + 8);
    for ( wxString::const_iterator it = s.begin(); it != s.end(); ++it )
    {
        const wxUniChar c = *it;
        if ( c == wxT('"') || c == wxT('\\') )
        {
            out += wxT('\\');
            out += c;
        }
        else if ( c == wxT('\n') )
            out += wxT("\\n");
        else if ( c == wxT('\t') )
            out += wxT("\\t");
        else if ( c == wxT('\r') )
            out += wxT("\\r");
        else
            out += c;
    }
    return out;
}

// Menubar structure: JSON [{title, items:[{id,label,kind,checked,enabled,
// items}]}] — kind: "normal" | "separator" | "check" | "radio" | "submenu".
inline void wxDomMenuSetStructure(int domId, const wxString& json)
{
    EM_ASM({ wxDomMenuSetStructure($0, UTF8ToString($1)); },
           domId, (const char *)json.utf8_str());
}

// Toolbar tools: JSON [{id,label,tooltip,kind,toggled,enabled,img,imgW,
// imgH}] — kind: "button" | "toggle" | "separator".
inline void wxDomToolbarSetTools(int domId, const wxString& json)
{
    EM_ASM({ wxDomToolbarSetTools($0, UTF8ToString($1)); },
           domId, (const char *)json.utf8_str());
}

// Command id of the last activated menu item / tool.
inline int wxDomGetLastCommandId(int domId)
{
    return EM_ASM_INT({ return wxDomGetLastCommandId($0); }, domId);
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

// Browser-native tooltip (HTML title attribute).
inline void wxDomSetTooltip(int domId, const wxString& tip)
{
    EM_ASM({ wxDomSetTooltip($0, UTF8ToString($1)); }, domId, (const char *)tip.utf8_str());
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
