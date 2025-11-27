///////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dnd.h
// Purpose:     
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_DND_H_
#define _WX_WASM_DND_H_

#include "wx/event.h"
#include "wx/icon.h"

// ----------------------------------------------------------------------------
// macros
// ----------------------------------------------------------------------------

// this macro may be used instead for wxDropSource ctor arguments: it will use
// the cursor 'name' from an XPM file under Wasm, but will expand to something
// else under MSW. If you don't use it, you will have to use #ifdef in the
// application code.
#define wxDROP_ICON(name)   wxCursor(X##_xpm)

//-------------------------------------------------------------------------
// wxDropTarget
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// wxDropSource
//-------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxDropSource: public wxDropSourceBase
{
public:
    wxDropSource(wxWindow *win = NULL,
                 const wxCursor &copy = wxNullCursor,
                 const wxCursor &move = wxNullCursor,
                 const wxCursor &none = wxNullCursor);

    virtual ~wxDropSource();

    virtual wxDragResult DoDragDrop(int flags = wxDrag_CopyOnly);

    virtual void OnDragResult(wxDragResult WXUNUSED(result)) { }

    static bool IsDragInProgress();
    static void HandleMouseEvent(wxMouseEvent *event);

private:
    void StartDrag();
    void EndDrag(wxDragResult result);

    const wxCursor& GetCursor(wxDragResult res) const;

    bool UseAlternateResult();
    wxDragResult GetDefaultDragResult();
    void UpdateDesiredDragResult(wxDragResult desiredResult);

    bool HandleMouseEvent(wxMouseEvent* event, wxDragResult* result);

    int m_dropFlags;
    
    wxWindow *m_overWindow;        // Current mouse over window

    wxPoint m_startPosition;       // Mouse position when drag started
    wxCursor m_startCursor;        // Cursor when drag started

    bool m_lastMouseEventValid;
    wxMouseEvent m_lastMouseEvent; // Last mouse event received during drag

    wxDragResult m_desiredResult;
};

class WXDLLIMPEXP_CORE wxDropTarget: public wxDropTargetBase
{
public:
    wxDropTarget(wxDataObject *dataObject = NULL);

    virtual bool OnDrop(wxCoord x, wxCoord y);
    virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def);
    virtual bool GetData();

    virtual wxDataFormat GetMatchingPair();
};


#endif // _WX_WASM_DND_H_

