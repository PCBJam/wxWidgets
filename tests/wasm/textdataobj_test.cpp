// wxTextDataObject encoding test (WASM port).
//
// Regression guard for the clipboard truncation bug: without
// wxNEEDS_UTF8_FOR_TEXT_DATAOBJ the port fell into wxTextDataObject's default
// branch (dobjcmn.cpp, "only used under Windows"), which serves raw wxChar =
// 4-byte UTF-32 for wxDF_UNICODETEXT. src/wasm/clipbrd.cpp reads that buffer
// as UTF-8 — ASCII-in-UTF-32 is "valid" UTF-8 with a NUL after every char —
// and the JS write helper's UTF8ToString stops at the first NUL, so
// navigator.clipboard ended up holding just the first character of any copy
// (KiCad pasted the "(" of its s-expression as a stray SCH_TEXT).
//
// The contract asserted here: on this port wxDF_UNICODETEXT data IS UTF-8,
// both directions, plain and through wxDataObjectComposite (the exact shape
// eeschema's doCopy builds). The data-object checks run at startup; the
// wxClipboard round-trip (which suspends via JSPI) runs from a button
// handler, the port's supported suspension context.
//
// Built by pcbjam tests/apps/Makefile.wasm (target `textdataobj`), driven by
// tests/e2e/textdataobj.spec.ts, which asserts zero "[WXTEST] FAIL" console
// lines and that navigator.clipboard.readText() returns the full string.

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#include "wx/clipbrd.h"
#include "wx/dataobj.h"

#include <cstring>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// Multi-line, s-expression-shaped, with 2- and 3-byte UTF-8 sequences
// (µ Ω ¶ →) so a wide/UTF-8 mixup cannot pass by accident.
static const char TEST_UTF8[] =
    "(kicad_sch (version 20250114)\n"
    "  (symbol (lib_id \"Device:R\") (value \"10kµΩ¶→\"))\n"
    ")";

static int g_failures = 0;

static void LogConsole(const wxString& msg)
{
#ifdef __EMSCRIPTEN__
    const wxScopedCharBuffer utf8 = msg.utf8_str();
    EM_ASM({ console.log(UTF8ToString($0)); }, utf8.data());
#else
    wxLogMessage("%s", msg);
#endif
}

static void Check(bool ok, const char* name, const wxString& detail)
{
    if (ok)
    {
        LogConsole(wxString::Format("[WXTEST] PASS %s", name));
    }
    else
    {
        g_failures++;
        LogConsole(wxString::Format("[WXTEST] FAIL %s - %s", name, detail));
    }
}

// Extract wxDF_UNICODETEXT data from any wxDataObject the way the wasm
// wxClipboard::AddData does: GetDataSize, then GetDataHere into a raw buffer.
static void CheckUnicodeTextIsUtf8(wxDataObject& obj, const char* sizeName, const char* dataName)
{
    const size_t expected = strlen(TEST_UTF8);
    const size_t sz = obj.GetDataSize(wxDF_UNICODETEXT);

    Check(sz == expected, sizeName,
          wxString::Format("GetDataSize(wxDF_UNICODETEXT) = %lu, want %lu (UTF-8 bytes)",
                           (unsigned long)sz, (unsigned long)expected));

    std::vector<char> buf(wxMax(sz, expected) + 8, '\0');
    const bool got = obj.GetDataHere(wxDF_UNICODETEXT, buf.data());
    const bool match = got && sz == expected && memcmp(buf.data(), TEST_UTF8, expected) == 0;

    Check(match, dataName,
          got ? wxString::Format("data is not the UTF-8 bytes (first bytes: %02x %02x %02x %02x)",
                                 (unsigned char)buf[0], (unsigned char)buf[1],
                                 (unsigned char)buf[2], (unsigned char)buf[3])
              : wxString("GetDataHere returned false"));
}

class TextDataObjTestFrame : public wxFrame
{
public:
    TextDataObjTestFrame();

private:
    wxTextCtrl* m_log;

    void Log(const wxString& msg)
    {
        m_log->AppendText(msg + "\n");
        LogConsole(msg);
    }

    void RunStartupChecks();
    void OnClipboardRoundTrip(wxCommandEvent& evt);
    void OnCacheFallback(wxCommandEvent& evt);

    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_CLIPBOARD_ROUNDTRIP = wxID_HIGHEST + 1,
    ID_CACHE_FALLBACK
};

wxBEGIN_EVENT_TABLE(TextDataObjTestFrame, wxFrame)
    EVT_BUTTON(ID_CLIPBOARD_ROUNDTRIP, TextDataObjTestFrame::OnClipboardRoundTrip)
    EVT_BUTTON(ID_CACHE_FALLBACK, TextDataObjTestFrame::OnCacheFallback)
wxEND_EVENT_TABLE()

class TextDataObjTestApp : public wxApp
{
public:
    virtual bool OnInit() override
    {
        if (!wxApp::OnInit())
            return false;

        (new TextDataObjTestFrame())->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(TextDataObjTestApp);

TextDataObjTestFrame::TextDataObjTestFrame()
    : wxFrame(nullptr, wxID_ANY, "wxTextDataObject WASM Test",
              wxDefaultPosition, wxSize(600, 400))
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    mainSizer->Add(new wxStaticText(this, wxID_ANY,
        "wxTextDataObject encoding test\n"
        "Data-object checks run at startup; the button runs the wxClipboard round-trip."),
        0, wxALL, 10);

    mainSizer->Add(new wxButton(this, ID_CLIPBOARD_ROUNDTRIP, "Run Clipboard RoundTrip"),
        0, wxALIGN_CENTER | wxALL, 10);
    mainSizer->Add(new wxButton(this, ID_CACHE_FALLBACK, "Run Cache Fallback"),
        0, wxALIGN_CENTER | wxALL, 10);

    m_log = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 200),
                           wxTE_MULTILINE | wxTE_READONLY);
    mainSizer->Add(m_log, 1, wxEXPAND | wxALL, 10);

    SetSizer(mainSizer);

    LogConsole("[WXTEST] textdataobj test app started");
    RunStartupChecks();
}

void TextDataObjTestFrame::RunStartupChecks()
{
    const wxString testStr = wxString::FromUTF8(TEST_UTF8);

    // 1+2: plain wxTextDataObject serves UTF-8 for wxDF_UNICODETEXT.
    {
        wxTextDataObject obj(testStr);
        CheckUnicodeTextIsUtf8(obj, "unicodetext_size_utf8", "unicodetext_data_utf8");
    }

    // 3: through wxDataObjectComposite in eeschema's doCopy shape — the
    // custom "application/kicad" object added first, the text object last.
    {
        wxDataObjectComposite comp;
        wxCustomDataObject* kicadObj = new wxCustomDataObject(wxDataFormat("application/kicad"));
        kicadObj->SetData(strlen(TEST_UTF8), TEST_UTF8);
        comp.Add(kicadObj);
        comp.Add(new wxTextDataObject(testStr));
        CheckUnicodeTextIsUtf8(comp, "composite_size_utf8", "composite_data_utf8");
    }

    // 4: the paste direction — SetData(wxDF_UNICODETEXT, ...) parses UTF-8.
    {
        wxTextDataObject in;
        in.SetData(wxDF_UNICODETEXT, strlen(TEST_UTF8), TEST_UTF8);
        Check(in.GetText() == testStr, "setdata_parses_utf8",
              wxString::Format("GetText() length %lu, want %lu",
                               (unsigned long)in.GetText().length(), (unsigned long)testStr.length()));
    }

    Log(wxString::Format("[WXTEST] SUITE-DONE startup failures=%d", g_failures));
}

void TextDataObjTestFrame::OnClipboardRoundTrip(wxCommandEvent& WXUNUSED(evt))
{
    const wxString testStr = wxString::FromUTF8(TEST_UTF8);

    bool wrote = false;
    if (wxTheClipboard->Open())
    {
        wrote = wxTheClipboard->SetData(new wxTextDataObject(testStr));
        wxTheClipboard->Close();
    }
    Check(wrote, "clipboard_setdata", "wxClipboard::SetData failed");

    wxTextDataObject back;
    bool read = false;
    if (wxTheClipboard->Open())
    {
        read = wxTheClipboard->GetData(back);
        wxTheClipboard->Close();
    }
    Check(read && back.GetText() == testStr, "clipboard_roundtrip",
          wxString::Format("read=%d, got %lu chars, want %lu",
                           read ? 1 : 0,
                           (unsigned long)back.GetText().length(), (unsigned long)testStr.length()));

    Log(wxString::Format("[WXTEST] SUITE-DONE clipboard failures=%d", g_failures));
}

// Driven by the spec AFTER it has emptied navigator.clipboard: a paste must
// fall back to wxClipboard's local text cache (still holding the round-trip
// button's copy) rather than reporting the empty browser read as the result.
// Guards the GetData() empty-read fallback in src/wasm/clipbrd.cpp.
void TextDataObjTestFrame::OnCacheFallback(wxCommandEvent& WXUNUSED(evt))
{
    const wxString testStr = wxString::FromUTF8(TEST_UTF8);

    wxTextDataObject back;
    bool read = false;
    if (wxTheClipboard->Open())
    {
        read = wxTheClipboard->GetData(back);
        wxTheClipboard->Close();
    }
    Check(read && back.GetText() == testStr, "cache_fallback",
          wxString::Format("read=%d, got %lu chars, want %lu",
                           read ? 1 : 0,
                           (unsigned long)back.GetText().length(), (unsigned long)testStr.length()));

    Log(wxString::Format("[WXTEST] SUITE-DONE fallback failures=%d", g_failures));
}
