/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/private.cpp
// Purpose      wxWasm private classes
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/app.h"
#endif // WX_PRECOMP

#include "wx/display.h"
#include "wx/private/display.h"
#include "wx/wasm/private/display.h"

#include <emscripten.h>
#include <emscripten/html5.h>

double GetDevicePixelRatio()
{
    return emscripten_get_device_pixel_ratio();
}

int GetScreenWidth()
{
    return EM_ASM_INT({
        return mainWindow.offsetWidth;
    });
}

int GetScreenHeight()
{
    return EM_ASM_INT({
        return mainWindow.offsetHeight;
    });
}

// ===========================================================================
// wxWasmDisplay
// ===========================================================================

wxWasmDisplay::wxWasmDisplay()
    : m_screenSize(GetScreenWidth(), GetScreenHeight()),
      m_deviceScaleFactor(GetDevicePixelRatio()),
      m_contentScaleFactor(m_deviceScaleFactor >= 1.5 ? 2.0 : 1.0)
{
}

void wxWasmDisplay::UpdateScaleFactor()
{
    m_deviceScaleFactor = GetDevicePixelRatio();
    m_contentScaleFactor = m_deviceScaleFactor >= 1.5 ? 2.0 : 1.0;
}

const int DEFAULT_DEPTH = 32;

// ----------------------------------------------------------------------------
// display characteristics
// ----------------------------------------------------------------------------

class wxDisplayImplSingleWasm : public wxDisplayImplSingle
{
public:
    virtual wxRect GetGeometry() const wxOVERRIDE
    {
        wxSize screenSize = wxTheApp->GetDisplay()->GetScreenSize();
        return wxRect(0, 0, screenSize.x, screenSize.y);
    }

    virtual int GetDepth() const wxOVERRIDE
    {
        return DEFAULT_DEPTH;
    }

    virtual wxSize GetPPI() const wxOVERRIDE
    {
        // CSS reference pixel size is 1/96 in
        // see http://www.w3.org/TR/css3-values/#reference-pixel
        const double ppi = 96.0;
        return wxSize(ppi, ppi);
    }
};

double wxDisplayScaleFactor()
{
    return wxTheApp->GetDisplay()->GetDeviceScaleFactor();
}

double wxContentScaleFactor()
{
    return wxTheApp->GetDisplay()->GetContentScaleFactor();
}

class wxDisplayFactorySingleWasm : public wxDisplayFactorySingle
{
protected:
    virtual wxDisplayImpl *CreateSingleDisplay()
    {
        return new wxDisplayImplSingleWasm();
    }
};

wxDisplayFactory *wxDisplay::CreateFactory()
{
    return new wxDisplayFactorySingleWasm();
}

