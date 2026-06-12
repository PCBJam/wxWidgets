/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/elementtracker.h
// Purpose:     Element tracking for WASM E2E tests: owner-drawn widgets
//              (grid cells, list rows, AUI parts...) have no per-item DOM,
//              so paint code reports their geometry to the JS registry
//              (window.wxElementRegistry in wx.js) for the test harness.
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_ELEMENTTRACKER_H_
#define _WX_WASM_ELEMENTTRACKER_H_

#ifdef __EMSCRIPTEN__

#include "wx/window.h"
#include "wx/string.h"

class wxGrid;
class wxGenericListCtrl;

// Low-level registration (implemented in src/wasm/elementtracker.cpp).
// The registry id is "parentId:elementType:index"; coordinates are screen
// coordinates.
void WasmRegisterRenderedElement(wxWindow* parent, const char* elementType,
    const char* subType, int index, const wxString& label,
    const wxString& tooltip, int screenX, int screenY, int width, int height,
    bool enabled);
void WasmUnregisterRenderedElementsByParent(wxWindow* parent);

// Convenience wrapper for the common case: the element rect is relative to
// the parent's origin; the screen position is computed here so paint-site
// hooks stay one-liners.
void wxWasmTrackElement(wxWindow* parent, const char* elementType,
    const char* subType, int index, const wxString& label,
    const wxString& tooltip, const wxRect& rect, bool enabled = true);

// Typed helpers where the whole hook collapses to one call.
#if wxUSE_GRID
void wxWasmTrackGridCell(wxGrid* grid, int row, int col,
                         const wxRect& cellRect);
#endif
#if wxUSE_LISTCTRL
void wxWasmTrackListRow(wxGenericListCtrl* list, size_t line,
                        const wxRect& rectLine, int devX, int devY);
#endif

#endif // __EMSCRIPTEN__
#endif // _WX_WASM_ELEMENTTRACKER_H_
