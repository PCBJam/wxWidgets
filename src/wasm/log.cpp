/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/log.cpp
// Purpose:     wxLog implementation for WASM - outputs to browser console
// Author:      Claude (for KiCad WASM project)
// Created:     2025-12-15
// Copyright:   (c) 2025
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/log.h"
#include "wx/string.h"

#include <emscripten.h>

// ----------------------------------------------------------------------------
// wxLogWasm - logs to browser console in addition to GUI dialogs
// ----------------------------------------------------------------------------

class wxLogWasm : public wxLogGui
{
public:
    wxLogWasm() = default;
    virtual ~wxLogWasm() = default;

protected:
    virtual void DoLogRecord(wxLogLevel level,
                             const wxString& msg,
                             const wxLogRecordInfo& info) override
    {
        // Determine log level string for console output
        const char* levelStr = "DEBUG";
        const char* consoleMethod = "log";

        switch (level)
        {
            case wxLOG_FatalError:
                levelStr = "FATAL";
                consoleMethod = "error";
                break;
            case wxLOG_Error:
                levelStr = "ERROR";
                consoleMethod = "error";
                break;
            case wxLOG_Warning:
                levelStr = "WARNING";
                consoleMethod = "warn";
                break;
            case wxLOG_Message:
                levelStr = "INFO";
                consoleMethod = "info";
                break;
            case wxLOG_Status:
                levelStr = "STATUS";
                consoleMethod = "log";
                break;
            case wxLOG_Info:
                levelStr = "INFO";
                consoleMethod = "info";
                break;
            case wxLOG_Debug:
                levelStr = "DEBUG";
                consoleMethod = "debug";
                break;
            case wxLOG_Trace:
                levelStr = "TRACE";
                consoleMethod = "debug";
                break;
            default:
                break;
        }

        // Log to browser console BEFORE showing dialog
        // Use appropriate console method based on level
        wxString fullMsg = wxString::Format("[wxLog][%s] %s", levelStr, msg);
        wxScopedCharBuffer utf8Buf = fullMsg.utf8_str();
        const char* msgUtf8 = utf8Buf.data();

        EM_ASM({
            var method = UTF8ToString($0);
            var message = UTF8ToString($1);
            if (method === "error") {
                console.error(message);
            } else if (method === "warn") {
                console.warn(message);
            } else if (method === "info") {
                console.info(message);
            } else if (method === "debug") {
                console.debug(message);
            } else {
                console.log(message);
            }
        }, consoleMethod, msgUtf8);

        // Call parent implementation to still collect for GUI dialog
        wxLogGui::DoLogRecord(level, msg, info);
    }
};

// ----------------------------------------------------------------------------
// wxLogWasm creation function - called from wxGUIAppTraits::CreateLogTarget()
// ----------------------------------------------------------------------------

wxLog* wxCreateLogWasm()
{
    return new wxLogWasm();
}
