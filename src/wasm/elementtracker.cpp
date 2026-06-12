/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/elementtracker.cpp
// Purpose:     Element tracking for WASM E2E tests. Owner-drawn widgets
//              report per-item geometry (grid cells, list rows, AUI parts,
//              tabs...) into the JS registry (wxRenderedElementRegister in
//              wx.js) so the Playwright harness can find and click them.
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/wasm/elementtracker.h"

#if wxUSE_GRID
#include "wx/grid.h"
#endif
#if wxUSE_LISTCTRL
#include "wx/listctrl.h"
#endif

#include <emscripten.h>

// Register a rendered element (not a wxWindow, but drawn by a parent control)
void WasmRegisterRenderedElement(
    wxWindow* parent,
    const char* elementType,  // "tool", "menuitem", "sash", "auipart"
    const char* subType,      // e.g., "button", "separator", "caption"
    int index,
    const wxString& label,
    const wxString& tooltip,
    int screenX, int screenY,
    int width, int height,
    bool enabled)
{
    if (!parent) return;

    uintptr_t parentId = reinterpret_cast<uintptr_t>(parent);

    // Create unique ID: parentId:elementType:index
    EM_ASM({
        var id = $0.toString() + ':' + UTF8ToString($1) + ':' + $2;
        wxRenderedElementRegister(
            id,
            $0.toString(),
            UTF8ToString($1),
            UTF8ToString($3),
            UTF8ToString($4),
            UTF8ToString($5),
            $6, $7, $8, $9,
            $10 ? true : false,
            $2
        );
    },
    parentId,
    elementType,
    index,
    subType,
    label.utf8_str().data(),
    tooltip.utf8_str().data(),
    screenX, screenY,
    width, height,
    enabled ? 1 : 0);
}

// Unregister all rendered elements for a parent
void WasmUnregisterRenderedElementsByParent(wxWindow* parent)
{
    if (!parent) return;

    uintptr_t parentId = reinterpret_cast<uintptr_t>(parent);

    EM_ASM({
        wxRenderedElementUnregisterByParent($0.toString());
    }, parentId);
}

void wxWasmTrackElement(wxWindow* parent, const char* elementType,
                        const char* subType, int index,
                        const wxString& label, const wxString& tooltip,
                        const wxRect& rect, bool enabled)
{
    if (!parent) return;

    const wxPoint screenPos = parent->GetScreenPosition();

    WasmRegisterRenderedElement(parent, elementType, subType, index,
                                label, tooltip,
                                screenPos.x + rect.x, screenPos.y + rect.y,
                                rect.width, rect.height, enabled);
}

#if wxUSE_GRID

void wxWasmTrackGridCell(wxGrid* grid, int row, int col,
                         const wxRect& cellRect)
{
    // Cell rects are relative to the inner grid window, not the wxGrid.
    const wxPoint screenPos = grid->GetGridWindow()->GetScreenPosition();

    wxString cellValue = grid->GetCellValue(row, col);
    if (cellValue.IsEmpty()) {
        cellValue = wxString::Format("%d,%d", row, col);
    }

    WasmRegisterRenderedElement(
        grid,
        "gridcell",
        "cell",
        row * grid->GetNumberCols() + col,
        cellValue,
        wxString::Format("Row %d, Col %d", row, col),
        screenPos.x + cellRect.x, screenPos.y + cellRect.y,
        cellRect.width, cellRect.height,
        grid->IsEditable());
}

#endif // wxUSE_GRID

#if wxUSE_LISTCTRL

void wxWasmTrackListRow(wxGenericListCtrl* list, size_t line,
                        const wxRect& rectLine, int devX, int devY)
{
    const wxPoint screenPos = list->GetScreenPosition();

    // Item text from the first column as label
    wxString itemText = list->GetItemText(line, 0);
    if (itemText.IsEmpty())
        itemText = wxString::Format("Item %zu", line);

    WasmRegisterRenderedElement(
        list,
        "listitem",
        "row",
        static_cast<int>(line),
        itemText,
        wxString::Format("Row %zu", line),
        screenPos.x + rectLine.x - devX, screenPos.y + rectLine.y - devY,
        rectLine.width, rectLine.height,
        true);  // List items are always enabled
}

#endif // wxUSE_LISTCTRL
