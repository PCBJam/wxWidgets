/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/domevents.cpp
// Purpose:     Routes DOM element events (click/input/focus...) from
//              wx-dom.js into wxWindowWasm::OnDomEvent. DOM port only.
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/window.h"
#include "wx/app.h"
#include "wx/menu.h"
#include "wx/hashmap.h"
#include "wx/base64.h"
#include "wx/bitmap.h"
#include "wx/image.h"
#include "wx/log.h"
#include "wx/mstream.h"
#include "wx/wasm/private/dom.h"
#include "wx/wasm/private/mouse.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <string>

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

// domId of the element whose event is currently being dispatched (see the
// declaration in wx/wasm/private/dom.h). Lets a window owning auxiliary DOM
// elements (built-in scrollbar gutters) discriminate which one fired.
static int gs_currentEventDomId = 0;

int wxDomCurrentEventDomId()
{
    return gs_currentEventDomId;
}

wxString wxDomBitmapToDataURL(const wxBitmap& bitmap)
{
    if ( !bitmap.IsOk() )
        return wxString();

    const wxImage image = bitmap.ConvertToImage();
    if ( !image.IsOk() )
        return wxString();

    // Apps don't necessarily call wxInitAllImageHandlers(); register the
    // PNG handler on demand and keep encode failures out of wxLog dialogs.
    if ( !wxImage::FindHandler(wxBITMAP_TYPE_PNG) )
        wxImage::AddHandler(new wxPNGHandler);

    wxLogNull noLog;

    wxMemoryOutputStream stream;
    if ( !image.SaveFile(stream, wxBITMAP_TYPE_PNG) )
        return wxString();

    const size_t len = stream.GetSize();
    wxMemoryBuffer buf;
    stream.CopyTo(buf.GetWriteBuf(len), len);
    buf.UngetWriteBuf(len);

    return wxT("data:image/png;base64,") +
           wxBase64Encode(buf.GetData(), buf.GetDataLen());
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

    gs_currentEventDomId = domId;
    window->OnDomEvent(static_cast<wxDomEventKind>(kind));
    gs_currentEventDomId = 0;
}

// Mouse events forwarded from the DOM layer (wx-dom.js document-level
// listeners): DOM elements swallow browser events before the #canvas
// Emscripten callbacks see them, which starves the wx-side hit-testing
// pipeline (ENTER/LEAVE hover, wheel scrolling, right-click) whenever the
// pointer is over a DOM-backed control. This entry rebuilds the
// Emscripten event structs and reuses the exact same converter + handler
// path, so capture, double-click state and hover synthesis stay in one
// place (src/wasm/app.cpp).
//
// kind: 1=motion, 2=down, 3=up, 4=wheel.
// x/y: #canvas-relative CSS px (same space as Emscripten targetX/Y).
// button/buttons/detail: DOM MouseEvent semantics.
// modifiers: 1 ctrl | 2 shift | 4 alt | 8 meta.
// Returns 1 when wx consumed the event (JS uses it for preventDefault).
int EMSCRIPTEN_KEEPALIVE wx_dom_mouse(int kind, int x, int y,
                                      int button, int buttons, int detail,
                                      int modifiers, double deltaY)
{
    if ( !wxTheApp )
        return 0;

    EmscriptenMouseEvent mouse;
    memset(&mouse, 0, sizeof(mouse));
    mouse.timestamp = emscripten_get_now();
    mouse.targetX = x;
    mouse.targetY = y;
    mouse.button = static_cast<unsigned short>(button);
    mouse.buttons = static_cast<unsigned short>(buttons);
    wxUnusedVar(detail); // EmscriptenMouseEvent has no click-count field;
                         // dclick synthesis lives in the converter's state
    mouse.ctrlKey = (modifiers & 1) != 0;
    mouse.shiftKey = (modifiers & 2) != 0;
    mouse.altKey = (modifiers & 4) != 0;
    mouse.metaKey = (modifiers & 8) != 0;

    wxMouseEvent event;

    if ( kind == 4 )
    {
        EmscriptenWheelEvent wheel;
        memset(&wheel, 0, sizeof(wheel));
        wheel.mouse = mouse;
        wheel.deltaY = deltaY;

        if ( !EmscriptenWheelEventToWXEvent(wheel, wxVERTICAL, &event) )
            return 0;

        wxTheApp->HandleMouseWheelEvent(&event);
        return 1;
    }

    int emType;
    switch ( kind )
    {
        case 2:  emType = EMSCRIPTEN_EVENT_MOUSEDOWN; break;
        case 3:  emType = EMSCRIPTEN_EVENT_MOUSEUP; break;
        default: emType = EMSCRIPTEN_EVENT_MOUSEMOVE; break;
    }

    if ( !EmscriptenMouseEventToWXEvent(emType, mouse, &event) )
        return 0;

    wxTheApp->HandleMouseEvent(&event);
    return 1;
}

// Called from wx-dom.js when a menubar title is clicked, BEFORE the popup is
// painted. Fires wxEVT_MENU_OPEN + runs the menu's UpdateUI so app handlers
// (KiCad's ACTION_MENU::OnMenuEvent -> updateMenu) refresh item enable/check
// state, then returns the freshly-serialized items JSON for that menu. The
// returned pointer stays valid until the next call (function-static storage).
// Returns "[]" if the domId is not a live menubar. (parity H-7)
const char* EMSCRIPTEN_KEEPALIVE wx_dom_menu_open(int domId, int menuIndex)
{
    static std::string s_json;
    s_json = "[]";

#if wxUSE_MENUBAR
    wxDomWindowMap::iterator it = gs_domWindows.find(domId);
    if ( it != gs_domWindows.end() && it->second )
    {
        wxMenuBar *bar = static_cast<wxMenuBar *>(it->second);
        const wxString json = bar->WasmOnMenuOpen(static_cast<size_t>(menuIndex));
        s_json = (const char *)json.utf8_str();
    }
#else
    (void)domId;
    (void)menuIndex;
#endif

    return s_json.c_str();
}

} // extern "C"
