/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/display.h
// Purpose:     
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_DISPLAY_H_
#define _WX_WASM_PRIVATE_DISPLAY_H_

#include "wx/gdicmn.h"

// ----------------------------------------------------------------------------
// wxWasmDisplay
// ----------------------------------------------------------------------------

class wxWasmDisplay
{
public:
    wxWasmDisplay();
    virtual ~wxWasmDisplay() { }
    
    inline wxSize GetScreenSize() const { return m_screenSize; }
    void SetScreenSize(const wxSize& screenSize) { m_screenSize = screenSize; }

    inline double GetDeviceScaleFactor() const { return m_deviceScaleFactor; }
    inline double GetContentScaleFactor() const { return m_contentScaleFactor; }
    void UpdateScaleFactor();

private:
    wxSize m_screenSize;
    double m_deviceScaleFactor;
    double m_contentScaleFactor;
};

#endif // _WX_WASM_PRIVATE_DISPLAY_H_
