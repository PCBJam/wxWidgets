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
#include "wx/hashmap.h"
#include "wx/base64.h"
#include "wx/bitmap.h"
#include "wx/image.h"
#include "wx/log.h"
#include "wx/mstream.h"
#include "wx/wasm/private/dom.h"
#include "wx/wasm/private/execution_owner.h"
#include "wx/wasm/private/mouse.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>

// wxDomGetValue() and wxDomGetSelectedIndices() allocate their UTF-8 return
// values with this Emscripten library helper. EM_ASM bodies do not declare
// indirect JS-library dependencies by themselves, so keep the dependency with
// the DOM bridge instead of requiring every wx application to add a link flag.
EM_JS_DEPS(wx_wasm_dom_string_results, "$stringToNewUTF8");

EM_JS(int, wxWasmDomEventSnapshotPushJs,
      (unsigned token, int domId, int kind), {
    return typeof window.wxDomEventSnapshotPush === "function"
            ? window.wxDomEventSnapshotPush(token, domId, kind) : 0;
});

EM_JS(int, wxWasmDomEventSnapshotPopJs, (unsigned token), {
    return typeof window.wxDomEventSnapshotPop === "function"
            ? window.wxDomEventSnapshotPop(token) : 0;
});

EM_JS(int, wxWasmDomEventSnapshotDiscardJs, (unsigned token), {
    return typeof window.wxDomEventSnapshotDiscard === "function"
            ? window.wxDomEventSnapshotDiscard(token) : 0;
});

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

namespace
{

struct wxWasmDomIngressJob
{
    virtual ~wxWasmDomIngressJob() = default;

    wx_wasm_execution::ScopeToken targetScope;
};

bool wxWasmDomScopeMatchesCandidate(
        wx_wasm_execution::ScopeToken candidate,
        wx_wasm_execution::ScopeToken actual)
{
    return !candidate || candidate == actual;
}

struct wxWasmControlEventJob : wxWasmDomIngressJob
{
    int domId = 0;
    int kind = 0;
    unsigned snapshotToken = 0;
};

class wxWasmDomEventSnapshotScope
{
public:
    explicit wxWasmDomEventSnapshotScope(wxWasmControlEventJob *job)
        : m_token(job->snapshotToken),
          m_active(wxWasmDomEventSnapshotPushJs(
                  job->snapshotToken, job->domId, job->kind) == 1)
    {
    }

    ~wxWasmDomEventSnapshotScope()
    {
        if (m_active && wxWasmDomEventSnapshotPopJs(m_token) != 1)
            wxWasmExecutionFailStop(
                    "DOM event snapshot refused exact release");
    }

    explicit operator bool() const { return m_active; }

private:
    unsigned m_token;
    bool m_active;
};

void wxWasmDiscardControlEventJob(void *arg)
{
    wxWasmControlEventJob *job =
            static_cast<wxWasmControlEventJob *>(arg);
    if (wxWasmDomEventSnapshotDiscardJs(job->snapshotToken) != 1)
        wxWasmExecutionFailStop(
                "DOM event snapshot refused queued discard");
    delete job;
}

void wxWasmRunControlEventJob(void *arg)
{
    wxWasmControlEventJob *job = static_cast<wxWasmControlEventJob *>(arg);
    const wxDomWindowMap::iterator it = gs_domWindows.find(job->domId);

    // Resolve the DOM ID only after admission. A queued callback must not keep
    // a raw wxWindow pointer alive after wxDomUnregisterWindow erased it.
    if (it != gs_domWindows.end() && it->second && it->second->IsEnabled()
        && wxWasmDomScopeMatchesCandidate(
                job->targetScope,
                wxWasmExecutionScopeForWindow(it->second)))
    {
        wxWasmDomEventSnapshotScope snapshot(job);
        if (snapshot)
        {
            const int previousDomId = gs_currentEventDomId;
            gs_currentEventDomId = job->domId;
            it->second->OnDomEvent(static_cast<wxDomEventKind>(job->kind));
            gs_currentEventDomId = previousDomId;
        }
        else
        {
            if (wxWasmDomEventSnapshotDiscardJs(job->snapshotToken) != 1)
                wxWasmExecutionFailStop(
                        "DOM event snapshot refused missing-snapshot discard");
            wxWasmExecutionFailStop(
                    "DOM event snapshot was missing at admission");
        }
    }
    else if (wxWasmDomEventSnapshotDiscardJs(job->snapshotToken) != 1)
    {
        wxWasmExecutionFailStop(
                "DOM event snapshot refused target discard");
    }

    delete job;
}

struct wxWasmForwardedMouseJob : wxWasmDomIngressJob
{
    wxApp *app = NULL;
    int kind = 0;
    EmscriptenMouseEvent browserEvent;
    double deltaY = 0.0;
};

void wxWasmRunForwardedMouseJob(void *arg)
{
    wxWasmForwardedMouseJob *job =
            static_cast<wxWasmForwardedMouseJob *>(arg);
    wxApp *app = wxTheApp == job->app ? job->app : NULL;

    if (app)
    {
        const wxPoint ingressPoint(job->browserEvent.targetX,
                                   job->browserEvent.targetY);
        const wx_wasm_execution::ScopeToken ingressScope =
                wxWasmExecutionScopeForWindow(
                        app->GetMouseWindow(ingressPoint));
        bool targetMatches = wxWasmDomScopeMatchesCandidate(
                job->targetScope, ingressScope);

        // HandleMouseWheelEvent intentionally resolves the current pointer
        // target. If capture moved while this message waited, do not let an
        // envelope admitted for one top-level mutate another.
        if (targetMatches && job->kind == 4)
        {
            targetMatches = wxWasmDomScopeMatchesCandidate(
                    job->targetScope,
                    wxWasmExecutionScopeForWindow(
                            app->GetMouseWindow(wxGetMousePosition())));
        }

        wxMouseEvent event;

        if (targetMatches && job->kind == 4)
        {
            EmscriptenWheelEvent wheel;
            memset(&wheel, 0, sizeof(wheel));
            wheel.mouse = job->browserEvent;
            wheel.deltaY = job->deltaY;

            if (EmscriptenWheelEventToWXEvent(
                    wheel, wxVERTICAL, &event))
                app->HandleMouseWheelEvent(&event);
        }
        else if (targetMatches)
        {
            int emType;

            switch (job->kind)
            {
                case 2:  emType = EMSCRIPTEN_EVENT_MOUSEDOWN; break;
                case 3:  emType = EMSCRIPTEN_EVENT_MOUSEUP; break;
                default: emType = EMSCRIPTEN_EVENT_MOUSEMOVE; break;
            }

            // Conversion belongs to the admitted transaction too. In
            // particular, mouse-down conversion updates the shared
            // double-click history used by canvas and DOM-backed controls.
            if (EmscriptenMouseEventToWXEvent(
                    emType, job->browserEvent, &event))
                app->HandleMouseEvent(&event);
        }
    }

    delete job;
}

void wxWasmDiscardForwardedMouseJob(void *arg)
{
    delete static_cast<wxWasmForwardedMouseJob *>(arg);
}

} // namespace

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

// Called from wx-dom.js event listeners. The browser entry is copied into a
// heap job and admitted on a dispatch context. It can therefore wait behind an
// unrelated owner, or enter through an active modal lease, without dispatching
// mutable wx state on the browser's shared main stack.
int EMSCRIPTEN_KEEPALIVE wx_dom_event_stage(
        int domId, int kind, unsigned snapshotToken,
        unsigned ingressReceiptToken)
{
    if (domId <= 0 || snapshotToken == 0)
        return 0;

    const wx_wasm_execution::BrowserIngressReceipt receipt =
            wxWasmExecutionTakeBrowserIngressReceipt(
                    ingressReceiptToken);
    wxWasmControlEventJob *job = new wxWasmControlEventJob();
    job->domId = domId;
    job->kind = kind;
    job->snapshotToken = snapshotToken;
    job->targetScope = receipt.lease
            ? receipt.lease.targetScope
            : wx_wasm_execution::ScopeToken{};

    if (!wxWasmExecutionStageBrowserIngress(
            &wxWasmRunControlEventJob, job,
            wx_wasm_execution::WorkClass::UserInput,
            job->targetScope, receipt, &wxWasmDiscardControlEventJob))
    {
        delete job;
        return 0;
    }

    return 1;
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
// x/y: wx screen CSS px. For #canvas this is its targetX/Y space; the DOM
// bridge reconstructs the same logical space from a control's TLW origin and
// local pointer offset when browser layout/scroll gives it a different origin.
// button/buttons/detail: DOM MouseEvent semantics.
// modifiers: 1 ctrl | 2 shift | 4 alt | 8 meta.
// Returns 1 when wx consumed the event (JS uses it for preventDefault).
int EMSCRIPTEN_KEEPALIVE wx_dom_mouse_stage(
        int kind, int x, int y, int button, int buttons, int detail,
        int modifiers, double deltaY, unsigned ingressReceiptToken)
{
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

    // The only wheel input exposed by this bridge is deltaY, so this preserves
    // the old synchronous "not consumed" result without running the converter
    // outside admission.
    if (kind == 4 && deltaY == 0.0)
        return 0;

    const wx_wasm_execution::BrowserIngressReceipt receipt =
            wxWasmExecutionTakeBrowserIngressReceipt(
                    ingressReceiptToken);
    wxWasmForwardedMouseJob *job = new wxWasmForwardedMouseJob();
    job->app = wxTheApp;
    job->kind = kind;
    job->browserEvent = mouse;
    job->deltaY = deltaY;
    job->targetScope = receipt.lease
            ? receipt.lease.targetScope
            : wx_wasm_execution::ScopeToken{};

    if (!wxWasmExecutionStageBrowserIngress(
            &wxWasmRunForwardedMouseJob, job,
            wx_wasm_execution::WorkClass::UserInput,
            job->targetScope, receipt, &wxWasmDiscardForwardedMouseJob,
            kind == 1 && buttons == 0
                ? wx_wasm_execution::CoalesceClass::PassiveMouseMove
                : wx_wasm_execution::CoalesceClass::None))
    {
        delete job;
        return 0;
    }

    return 1;
}

} // extern "C"
