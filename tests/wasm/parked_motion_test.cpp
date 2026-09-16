// parked_motion_test.cpp — repro + regression harness for mouse-MOTION delivery
// while a wx dispatch chain is parked in a RunSynchronousAction-style spin.
//
// KiCad's TOOL_MANAGER::RunSynchronousAction (paste-move, drag-move with a
// commit, wire drawing) waits for the tool with
//
//     while( state == STS_RUNNING ) { wxYield(); wxMilliSleep( 1 ); }
//
// on the stack of the key/button handler that started it. In the wasm port
// that handler holds the dispatch interlock (wxWasmDispatchGuard) and the
// nanosleep shim suspends the stack in place (JSPI), so the port stays
// "parked" for the whole interaction. wxApp::HandleMouseEvent's parked branch
// re-posted BUTTON events (the spin's wxYield drains them: a click still
// commits) but DROPPED every wxEVT_MOTION — a pasted symbol/footprint never
// followed the pointer. The fix queues one coalesced motion per drain.
//
// This harness rebuilds that shape without KiCad: a button whose handler
// spins exactly like RunSynchronousAction until the panel under the pointer
// reports a motion event (or a 3 s deadline passes).
//
// Console contract (tests/e2e/parked-motion.spec.ts asserts on these;
// counters, not FAIL-line absence — the J-6 discipline; ASCII only, no
// wxString::Format for log lines — %zu / em-dash vanish in this build):
//   [PARKED] READY
//   [PARKED] LAYOUT panel=(x,y,w,h)              screen (= canvas) coords
//   [PARKED] MOTION pos=(x,y) spinning=<0|1>     every motion the panel sees
//   [PARKED] CHECK unparked-motion PASS          first motion with no spin running
//   [PARKED] SPIN-START depth=<n>                n >= 1 proves the parked path
//   [PARKED] CHECK parked-motion PASS|FAIL waited=<ms>
//   [PARKED] CHECK parked-timer-oneshot PASS|FAIL   a wxTimer::StartOnce armed
//                                                   before the spin fired inside it
//   [PARKED] CHECK parked-timer-periodic PASS|FAIL ticks=<n>   a periodic wxTimer
//                                                   ticked >= 2 times inside it
//   [PARKED] SUITE-DONE failures=<n>
//
// The timer checks pin the second half of the paste bug: KiCad repaints the
// GAL canvas through a one-shot refresh timer (EDA_DRAW_PANEL_GAL::Refresh)
// and auto-pans through a periodic one; the port's mailbox refused to deliver
// any timer while a chain was parked, so the pasted item, although already
// moved to the cursor in the model, was not drawn until the first motion
// forced a synchronous repaint. The fix delivers due mailbox messages from
// the parked chain's own wxYield() (native wx semantics for wxYield).

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#include "wx/stopwatch.h"
#include "wx/timer.h"

#include <cstdarg>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// The interlock counter (wx/wasm/private/dispatch.h) — a plain global in the
// wx core library; declared here so the harness does not depend on the
// private header's include path.
extern int wxWasmDispatchDepth;

static void plog( const char* fmt, ... )
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

class ParkedMotionFrame : public wxFrame
{
public:
    ParkedMotionFrame();

private:
    wxPanel* m_panel;
    wxTimer  m_oneShot;
    wxTimer  m_periodic;
    bool     m_spinning;
    bool     m_motionSeen;
    bool     m_oneShotFired;
    int      m_periodicTicks;
    bool     m_unparkedReported;
    int      m_failures;

    void OnMotion( wxMouseEvent& evt );
    void OnStartSpin( wxCommandEvent& evt );
    void OnOneShot( wxTimerEvent& evt );
    void OnPeriodic( wxTimerEvent& evt );
};

ParkedMotionFrame::ParkedMotionFrame()
        : wxFrame( nullptr, wxID_ANY, "Parked-dispatch motion delivery test",
                   wxDefaultPosition, wxSize( 800, 600 ) ),
          m_panel( nullptr ),
          m_oneShot( this ),
          m_periodic( this ),
          m_spinning( false ),
          m_motionSeen( false ),
          m_oneShotFired( false ),
          m_periodicTicks( 0 ),
          m_unparkedReported( false ),
          m_failures( 0 )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    wxButton* start = new wxButton( this, wxID_ANY, "Start Spin" );
    sizer->Add( start, 0, wxALL, 8 );

    // Bare panel: no child may sit under the pointer, wxMouseEvent does not
    // propagate, so the panel itself must be the leaf that sees the motion.
    m_panel = new wxPanel( this, wxID_ANY );
    m_panel->SetBackgroundColour( wxColour( 220, 230, 240 ) );
    sizer->Add( m_panel, 1, wxEXPAND );

    SetSizer( sizer );

    m_panel->Bind( wxEVT_MOTION, &ParkedMotionFrame::OnMotion, this );
    start->Bind( wxEVT_BUTTON, &ParkedMotionFrame::OnStartSpin, this );
    Bind( wxEVT_TIMER, &ParkedMotionFrame::OnOneShot, this, m_oneShot.GetId() );
    Bind( wxEVT_TIMER, &ParkedMotionFrame::OnPeriodic, this, m_periodic.GetId() );

    CallAfter(
            [this]()
            {
                const wxRect r = m_panel->GetScreenRect();
                plog( "[PARKED] LAYOUT panel=(%d,%d,%d,%d)", r.x, r.y, r.width, r.height );
                plog( "[PARKED] READY" );
            } );
}

void ParkedMotionFrame::OnMotion( wxMouseEvent& evt )
{
    m_motionSeen = true;
    // (The normal delivery path also runs under a dispatch guard, so the
    // interlock depth is >= 1 in both phases; what distinguishes them is
    // whether the spin handler is on the stack.)
    plog( "[PARKED] MOTION pos=(%d,%d) spinning=%d", evt.GetX(), evt.GetY(),
          m_spinning ? 1 : 0 );

    // Positive control: motion reaches the panel when nothing is parked.
    if( !m_spinning && !m_unparkedReported )
    {
        m_unparkedReported = true;
        plog( "[PARKED] CHECK unparked-motion PASS" );
    }

    evt.Skip();
}

void ParkedMotionFrame::OnOneShot( wxTimerEvent& WXUNUSED( evt ) )
{
    m_oneShotFired = true;
    plog( "[PARKED] TIMER oneshot spinning=%d", m_spinning ? 1 : 0 );
}

void ParkedMotionFrame::OnPeriodic( wxTimerEvent& WXUNUSED( evt ) )
{
    m_periodicTicks++;
    plog( "[PARKED] TIMER periodic tick=%d spinning=%d", m_periodicTicks, m_spinning ? 1 : 0 );
}

void ParkedMotionFrame::OnStartSpin( wxCommandEvent& WXUNUSED( evt ) )
{
    m_spinning = true;
    m_motionSeen = false;
    m_oneShotFired = false;
    m_periodicTicks = 0;

    // Armed from inside the parked chain, like EDA_DRAW_PANEL_GAL::Refresh
    // arming its refresh timer right before RunSynchronousAction spins.
    m_oneShot.StartOnce( 20 );
    m_periodic.Start( 30 );

    // This handler runs inside a dispatch chain (depth >= 1). Everything
    // below happens with that chain parked in wxMilliSleep, exactly like
    // KiCad's RunSynchronousAction spin.
    plog( "[PARKED] SPIN-START depth=%d", wxWasmDispatchDepth );

    wxStopWatch sw;

    while( !( m_motionSeen && m_oneShotFired && m_periodicTicks >= 2 ) && sw.Time() < 3000 )
    {
        wxYield();          // drains posted events (the button path already relies on this)
        wxMilliSleep( 1 );  // nanosleep shim: a JSPI park of this very stack
    }

    m_periodic.Stop();

    const long waited = sw.Time();

    if( !m_motionSeen )
        m_failures++;

    plog( "[PARKED] CHECK parked-motion %s waited=%ld", m_motionSeen ? "PASS" : "FAIL", waited );

    if( !m_oneShotFired )
        m_failures++;

    plog( "[PARKED] CHECK parked-timer-oneshot %s", m_oneShotFired ? "PASS" : "FAIL" );

    if( m_periodicTicks < 2 )
        m_failures++;

    plog( "[PARKED] CHECK parked-timer-periodic %s ticks=%d", m_periodicTicks >= 2 ? "PASS" : "FAIL",
          m_periodicTicks );

    plog( "[PARKED] SUITE-DONE failures=%d", m_failures );

    m_spinning = false;
}

class ParkedMotionApp : public wxApp
{
public:
    bool OnInit() override
    {
        ParkedMotionFrame* frame = new ParkedMotionFrame();
        frame->Show( true );
        return true;
    }
};

wxIMPLEMENT_APP( ParkedMotionApp );
