
/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/utils.cpp
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/app.h"
#endif // WX_PRECOMP

#include "wx/wasm/private.h"
#include "wx/private/launchbrowser.h"

#include <emscripten.h>
#include <emscripten/threading.h>

#define GET_JAVASCRIPT_STRING(varName, string) \
  { \
    int length = EM_ASM_INT({ \
      return lengthBytesUTF8(varName); \
    }); \
    char* buffer = new char[length + 1]; \
    EM_ASM({ \
      stringToUTF8(varName, $0, $1); \
    }, buffer, length + 1); \
    string = buffer; \
    delete [] buffer; \
  }

// TODO: return os version
wxOperatingSystemId wxGetOsVersion(int *WXUNUSED(verMaj),
                                   int *WXUNUSED(verMin),
                                   int *WXUNUSED(verMicro))
{
    wxString osName;
    GET_JAVASCRIPT_STRING(platformInfo.name, osName);

    wxOperatingSystemId systemId = wxOS_UNKNOWN;

    if (osName == "Windows NT" || osName == "Windows")
    {
        systemId = wxOS_WINDOWS_NT;
    }
    else if (osName == "Mac OS X" || osName == "Macintosh")
    {
        systemId = wxOS_MAC_OSX_DARWIN;
    }
    else if (osName == "Linux")
    {
        systemId = wxOS_UNIX_LINUX;
    }
    else if (osName == "CrOS")
    {
        systemId = wxOS_CHROME_OS;
    }

    return systemId;
}

bool wxCheckOsVersion(int majorVsn, int minorVsn, int microVsn)
{
    // TODO: implement
    return true;
}

wxString wxGetOsDescription()
{
    wxString browserName;
    wxString browserVersion;
    wxString osName;
    wxString osVersion;

    GET_JAVASCRIPT_STRING(browserInfo.name, browserName);
    GET_JAVASCRIPT_STRING(browserInfo.version, browserVersion);
    GET_JAVASCRIPT_STRING(platformInfo.name, osName);
    GET_JAVASCRIPT_STRING(platformInfo.version, osVersion);

    return browserName + " " + browserVersion + " (" + osName + " " + osVersion + ")";
}

bool wxIsPlatform64Bit()
{
    return false;
}

wxString wxGetCpuArchitectureName()
{
    return "unknown";
}

wxBrowserInfo wxGetBrowserInfo()
{
    wxBrowserId browserId = wxBROWSER_UNKNOWN;

    wxString userAgent;
    wxString browserName;
    wxString browserVersion;

    GET_JAVASCRIPT_STRING(navigator.userAgent, userAgent);
    GET_JAVASCRIPT_STRING(browserInfo.name, browserName);
    GET_JAVASCRIPT_STRING(browserInfo.version, browserVersion);

    if (browserName == "Firefox")
    {
        browserId = wxBROWSER_FIREFOX;
    }
    else if (browserName == "Chrome")
    {
        browserId = wxBROWSER_CHROME;
    }
    else if (browserName == "Safari")
    {
        browserId = wxBROWSER_SAFARI;
    }
    else if (browserName == "Edge")
    {
        browserId = wxBROWSER_EDGE;
    }
    else if (browserName == "MSIE")
    {
        browserId = wxBROWSER_MSIE;
    }
    else if (browserName == "Opera")
    {
        browserId = wxBROWSER_OPERA;
    }

    // TODO: populate version

    return wxBrowserInfo(browserId, browserName, userAgent, browserVersion);
}

void EmscriptenDoLaunchBrowser(const wxString& url)
{
    EM_ASM({
        openUrl(UTF8ToString($0))
    }, static_cast<const char *>(url.utf8_str()));
}

void EmscriptenDoLaunchBrowserAsync(void *arg)
{
    wxString *url = static_cast<wxString *>(arg);
    EmscriptenDoLaunchBrowser(*url);
    delete url;
}

bool wxDoLaunchDefaultBrowser(const wxLaunchBrowserParams& params)
{
    // TODO: handle wxBROWSER_NEW_WINDOW flag

    if (emscripten_is_main_runtime_thread())
    {
        EmscriptenDoLaunchBrowser(params.url);
    }
    else
    {
        emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI,
                &EmscriptenDoLaunchBrowserAsync,
                new wxString(params.url));
    }

    return true;
}

void wxBell()
{
}

