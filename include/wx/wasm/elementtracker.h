/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/elementtracker.h
// Purpose:     RAII helper for element tracking in WASM E2E tests
// Author:      Claude Code
// Created:     2025-12-30
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_ELEMENTTRACKER_H_
#define _WX_WASM_ELEMENTTRACKER_H_

#ifdef __EMSCRIPTEN__

#include "wx/window.h"
#include "wx/string.h"

// Forward declarations (defined in src/wasm/window.cpp)
void WasmRegisterRenderedElement(wxWindow*, const char*, const char*, int,
    const wxString&, const wxString&, int, int, int, int, bool);
void WasmUnregisterRenderedElementsByParent(wxWindow*);

// RAII helper for element tracking - auto-clears on construct
class WasmElementTracker
{
public:
    WasmElementTracker(wxWindow* parent) : m_parent(parent) {
        WasmUnregisterRenderedElementsByParent(parent);
    }

    void Add(const char* type, const char* subType, int index,
             const wxString& label, const wxString& tooltip,
             int screenX, int screenY, int width, int height,
             bool enabled = true) {
        WasmRegisterRenderedElement(m_parent, type, subType, index,
            label, tooltip, screenX, screenY, width, height, enabled);
    }

    // Convenience overload without tooltip
    void Add(const char* type, const char* subType, int index,
             const wxString& label,
             int screenX, int screenY, int width, int height,
             bool enabled = true) {
        Add(type, subType, index, label, wxEmptyString,
            screenX, screenY, width, height, enabled);
    }

    wxWindow* GetParent() const { return m_parent; }
    wxPoint GetScreenPosition() const { return m_parent->GetScreenPosition(); }

private:
    wxWindow* m_parent;
};

#endif // __EMSCRIPTEN__
#endif // _WX_WASM_ELEMENTTRACKER_H_
