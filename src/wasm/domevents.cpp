/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/domevents.cpp
// Purpose:     Routes DOM element events (click/input/focus...) from
//              wx-dom.js into wxWindowWasm::OnDomEvent. DOM port only.
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef __WXUNIVERSAL__

#include "wx/window.h"
#include "wx/hashmap.h"
#include "wx/wasm/private/dom.h"

#include <emscripten.h>

WX_DECLARE_HASH_MAP(int, wxWindowWasm*, wxIntegerHash, wxIntegerEqual,
                    wxDomWindowMap);

static wxDomWindowMap gs_domWindows;

void wxDomRegisterWindow(int domId, wxWindowWasm *window)
{
    gs_domWindows[domId] = window;
}

void wxDomUnregisterWindow(int domId)
{
    gs_domWindows.erase(domId);
}

extern "C"
{

// Called from wx-dom.js event listeners. Dispatches directly into the wx
// event machinery — the same pattern the mouse/keyboard Emscripten callbacks
// use (each JS callback is a fresh WASM entry, so this works even while a
// modal dialog has the main C++ stack Asyncify-suspended).
void EMSCRIPTEN_KEEPALIVE wx_dom_event(int domId, int kind)
{
    wxDomWindowMap::iterator it = gs_domWindows.find(domId);
    if ( it == gs_domWindows.end() || !it->second )
        return;

    wxWindowWasm *window = it->second;
    if ( !window->IsEnabled() )
        return;

    window->OnDomEvent(static_cast<wxDomEventKind>(kind));
}

} // extern "C"

#endif // !__WXUNIVERSAL__
