// dropfiles_test.cpp — repro + regression harness for finding G-14: browser
// file drops must reach the window that registered via DragAcceptFiles(),
// not the leaf child under the pointer.
//
// The wasm port synthesizes a wxDropFilesEvent in OnFileDropped
// (src/wasm/app.cpp) from the HTML5 drop. wxDropFilesEvent does NOT propagate
// upward, so delivering it to wxFindWindowAtPoint()'s leaf silently loses the
// drop whenever the DragAcceptFiles() owner is an ancestor — which is exactly
// KiCad's shape (frames call DragAcceptFiles(true); the pointer is over the
// draw panel). This harness rebuilds that shape:
//
//   frame (DragAcceptFiles(true) + EVT_DROP_FILES)
//   └── outer panel (fills the client area — the leaf for drop point A)
//       └── nested panel at (300,200) 300x200 (deeper leaf for drop point B)
//
// Console contract (tests/e2e/dropfiles.spec.ts asserts on these; counters,
// not FAIL-line absence — the J-6 discipline; ASCII only, no wxString::Format
// for log lines — %zu / em-dash vanish in this build):
//   [DROPFILES] READY
//   [DROPFILES] LAYOUT outer=(x,y,w,h) nested=(x,y,w,h)
//   [DROPFILES] delivered n=<count> pos=(x,y) file0=<path>
//   [DROPFILES] SUMMARY delivered=<cumulative deliveries>

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#include <cstdarg>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static void dlog( const char* fmt, ... )
{
    char buf[512];
    va_list ap;
    va_start( ap, fmt );
    vsnprintf( buf, sizeof( buf ), fmt, ap );
    va_end( ap );
#ifdef __EMSCRIPTEN__
    EM_ASM( { console.log( UTF8ToString( $0 ) ); }, buf );
#else
    printf( "%s\n", buf );
#endif
}

class DropFilesFrame : public wxFrame
{
public:
    DropFilesFrame();

private:
    wxPanel* m_outer;
    wxPanel* m_nested;
    int      m_delivered;

    void OnDropFiles( wxDropFilesEvent& evt );

    wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE( DropFilesFrame, wxFrame )
    EVT_DROP_FILES( DropFilesFrame::OnDropFiles )
wxEND_EVENT_TABLE()

DropFilesFrame::DropFilesFrame()
        : wxFrame( nullptr, wxID_ANY, "DropFiles delivery test (G-14)",
                   wxDefaultPosition, wxSize( 800, 600 ) ),
          m_delivered( 0 )
{
    // The G-14 shape: the frame registers for drops, but a child covers the
    // whole client area, so the leaf under any drop point is never the frame.
    DragAcceptFiles( true );

    m_outer = new wxPanel( this, wxID_ANY );
    m_outer->SetBackgroundColour( wxColour( 220, 230, 240 ) );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    sizer->Add( m_outer, 1, wxEXPAND );
    SetSizer( sizer );

    // A deeper leaf: nested panel with explicit geometry inside the outer one.
    m_nested = new wxPanel( m_outer, wxID_ANY, wxPoint( 300, 200 ),
                            wxSize( 300, 200 ) );
    m_nested->SetBackgroundColour( wxColour( 250, 220, 200 ) );

    new wxStaticText( m_outer, wxID_ANY,
                      "G-14 drop-delivery harness - see console.",
                      wxPoint( 10, 10 ) );

    // Sizes settle after the port's boot size event; report the leaf
    // geometry then, so the spec's fixed drop points can be sanity-checked
    // from the logs when debugging.
    CallAfter( [this]() {
        const wxRect o = m_outer->GetScreenRect();
        const wxRect n = m_nested->GetScreenRect();
        dlog( "[DROPFILES] LAYOUT outer=(%d,%d,%d,%d) nested=(%d,%d,%d,%d)",
              o.x, o.y, o.width, o.height, n.x, n.y, n.width, n.height );
    } );
}

void DropFilesFrame::OnDropFiles( wxDropFilesEvent& evt )
{
    // KiCad's real drop handlers SUSPEND under JSPI (append-board reads the
    // file, may raise dialogs). A handler dispatched synchronously from the
    // plain (non-promising) OnFileDropped export dies with SuspendError, so
    // delivery must arrive via the event queue — this sleep suspends and
    // pins that contract (red: SuspendError + no delivered log).
    wxMilliSleep( 5 );

    const int       n = evt.GetNumberOfFiles();
    const wxString* files = evt.GetFiles();

    m_delivered++;
    dlog( "[DROPFILES] delivered n=%d pos=(%d,%d) file0=%s", n,
          evt.GetPosition().x, evt.GetPosition().y,
          n > 0 ? (const char*) files[0].utf8_str() : "(none)" );
    dlog( "[DROPFILES] SUMMARY delivered=%d", m_delivered );
}

class DropFilesApp : public wxApp
{
public:
    bool OnInit() override
    {
        if( !wxApp::OnInit() )
            return false;

        ( new DropFilesFrame() )->Show( true );
        dlog( "[DROPFILES] READY" );
        return true;
    }
};

wxIMPLEMENT_APP( DropFilesApp );
