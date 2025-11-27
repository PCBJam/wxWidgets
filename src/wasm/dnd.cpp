/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dnd.cpp
// Purpose:     wxDropTaraget implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_DRAG_AND_DROP

#include "wx/app.h"
#include "wx/dnd.h"
#include "wx/window.h"

#include "wx/evtloop.h"

#ifndef WX_PRECOMP
#endif // WX_PRECOMP

// ----------------------------------------------------------------------------
// wxDropSource
// ----------------------------------------------------------------------------

namespace
{

wxDropSource *g_dropSource = NULL;
wxDataObject *g_dataObject = NULL;

wxCursor defaultDragNoneCursor(26);
wxCursor defaultDragCopyCursor(27);
wxCursor defaultDragMoveCursor(27);

} // anonymous namespace

wxDropSource::wxDropSource(wxWindow *WXUNUSED(win),
                           const wxCursor &copy,
                           const wxCursor &move,
                           const wxCursor &none)
    : wxDropSourceBase(copy, move, none),
      m_dropFlags(0),
      m_overWindow(NULL),
      m_lastMouseEventValid(false)
{
}

wxDropSource::~wxDropSource()
{
}

void wxDropSource::StartDrag()
{
    wxASSERT(!IsDragInProgress());

    g_dropSource = this;
    g_dataObject = GetDataObject();

    m_lastMouseEventValid = false;

    m_startCursor = wxGetCursor();

    m_startPosition = wxGetMousePosition();
    m_overWindow = wxTheApp->GetMouseWindow(m_startPosition);

    wxDragResult desiredResult = wxDragNone;
    if (m_overWindow != NULL && m_overWindow->GetDropTarget() != NULL)
    {
        desiredResult = m_overWindow->GetDropTarget()->OnEnter(
                            m_startPosition.x,
                            m_startPosition.y,
                            GetDefaultDragResult());
    }

    UpdateDesiredDragResult(desiredResult);
}

void wxDropSource::EndDrag(wxDragResult result)
{
    wxASSERT(IsDragInProgress());

    g_dropSource = NULL;
    g_dataObject = NULL;

    wxSetCursor(m_startCursor);

    if (m_lastMouseEventValid &&
        m_lastMouseEvent.GetPosition() != m_startPosition)
    {
        //printf("drop sending mouse update event\n");
        // Generate a motion event to bring app up to date with mouse position.
        wxMouseEvent mouseEvent(m_lastMouseEvent);
        mouseEvent.SetEventType(wxEVT_MOTION);
        mouseEvent.m_clickCount = 0;
        wxTheApp->HandleMouseEvent(&mouseEvent);
    }

    m_lastMouseEventValid = false;
    m_overWindow = NULL;

    OnDragResult(result);
}

const wxCursor& wxDropSource::GetCursor(wxDragResult res) const
{
    const wxCursor& cursor = wxDropSourceBase::GetCursor(res);

    // If cursor not set, use default.
    if (cursor.IsSameAs(wxNullCursor))
    {
        switch (res)
        {
            case wxDragCopy:
                return defaultDragCopyCursor;
                break;
            case wxDragMove:
                return defaultDragMoveCursor;
                break;
            default:
                return defaultDragNoneCursor;
                break;
        }
    }
    return cursor;
}

bool wxDropSource::UseAlternateResult()
{
    return wxKeyboardState().CmdDown();
}

wxDragResult wxDropSource::GetDefaultDragResult()
{
    if (m_dropFlags & wxDrag_CopyOnly)
    {
        return wxDragCopy;
    }
    else if (m_dropFlags & wxDrag_DefaultMove)
    {
        return UseAlternateResult() ? wxDragCopy : wxDragMove;
    }
    else
    {
        return UseAlternateResult() ? wxDragMove : wxDragCopy;
    }
}

void wxDropSource::UpdateDesiredDragResult(wxDragResult desiredResult)
{
    GiveFeedback(desiredResult);

    m_desiredResult = desiredResult;
    wxCursor cursor = GetCursor(desiredResult);
    wxSetCursor(cursor);
}

bool wxDropSource::HandleMouseEvent(wxMouseEvent *event, wxDragResult *result)
{
    //printf("drop mouse event: %d\n", event->GetEventType());

    m_lastMouseEvent = *event;
    m_lastMouseEventValid = true;

    wxPoint mousePosition = event->GetPosition();
    wxWindow *window = wxTheApp->GetMouseWindow(mousePosition);
    wxPoint clientMousePosition =
        window != NULL ? window->ScreenToClient(mousePosition) : mousePosition;

    wxTheApp->UpdateMouseState(*event);

    wxMouseState mouseState;
    wxTheApp->GetMouseState(&mouseState);

    if (window != m_overWindow)
    {
        //printf("drop window changed\n");
        if (m_overWindow != NULL && m_overWindow->GetDropTarget() != NULL)
        {
            m_overWindow->GetDropTarget()->OnLeave();
        }
        m_overWindow = window;

        wxDragResult desiredResult = wxDragNone;

        if (m_overWindow != NULL && m_overWindow->GetDropTarget() != NULL)
        {
            desiredResult = m_overWindow->GetDropTarget()->OnEnter(
                                clientMousePosition.x,
                                clientMousePosition.y,
                                GetDefaultDragResult());
        }

        if (desiredResult == wxDragCancel || desiredResult == wxDragError)
        {
            *result = desiredResult;
            return true;
        }
        if (desiredResult != m_desiredResult)
        {
            //printf("drop target result: %d\n", desiredResult);
            UpdateDesiredDragResult(desiredResult);
        }
    }

    if (event->GetEventType() == wxEVT_MOTION)
    {
        if (m_overWindow != NULL && m_overWindow->GetDropTarget() != NULL)
        {
            wxDragResult desiredResult = m_overWindow->GetDropTarget()->OnDragOver(
                                             clientMousePosition.x,
                                             clientMousePosition.y,
                                             GetDefaultDragResult());

            if (desiredResult == wxDragCancel || desiredResult == wxDragError)
            {
                *result = desiredResult;
                return true;
            }
            if (desiredResult != m_desiredResult)
            {
                //printf("drop target result: %d\n", desiredResult);
                UpdateDesiredDragResult(desiredResult);
            }
        }
    }

    if (!event->LeftIsDown())
    {
        if (m_overWindow != NULL &&
            m_overWindow->GetDropTarget() != NULL &&
            m_desiredResult != wxDragNone)
        {
            bool accept = m_overWindow->GetDropTarget()->OnDrop(
                              clientMousePosition.x,
                              clientMousePosition.y);
            //printf("drop target accept: %d\n", accept);
            if (accept)
            {
                *result = m_overWindow->GetDropTarget()->OnData(
                              clientMousePosition.x,
                              clientMousePosition.y,
                              GetDefaultDragResult());
            }
            else
            {
                *result = wxDragNone;
            }
        }
        else
        {
            *result = wxDragNone;
        }
        //printf("drop ending\n");
        return true;
    }

    return false;
}

wxDragResult wxDropSource::DoDragDrop(int flags)
{
    //printf("DoDragDrop enter\n");

    m_dropFlags = flags;

    if (IsDragInProgress() || !wxTheApp)
    {
        return wxDragNone;
    }

    wxMouseState mouseState;
    wxTheApp->GetMouseState(&mouseState);

    if (!mouseState.LeftIsDown())
    {
        return wxDragNone;
    }

    StartDrag();

    //printf("DoDragDrop exit\n");
    return wxDragNone;
}

/* static */
bool wxDropSource::IsDragInProgress()
{
    return g_dropSource != NULL;
}

/* static */
void wxDropSource::HandleMouseEvent(wxMouseEvent* event)
{
    wxASSERT(IsDragInProgress());

    wxDragResult result = wxDragNone;

    if (g_dropSource->HandleMouseEvent(event, &result))
    {
        g_dropSource->EndDrag(result);
    }
}


// ----------------------------------------------------------------------------
// wxDropTarget
// ----------------------------------------------------------------------------

wxDropTarget::wxDropTarget(wxDataObject *WXUNUSED(dataObject))
{
}

wxDataFormat wxDropTarget::GetMatchingPair()
{
    wxDataFormat supported(wxDF_INVALID);
    if (m_dataObject != NULL)
    {
        if (g_dropSource)
        {
            wxDataObject* data = g_dropSource->GetDataObject();

            if ( data )
            {
                supported = m_dataObject->GetSupportedFormatInSource(data);
            }
        }
    }

    return supported;
}

bool wxDropTarget::OnDrop(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y))
{
    if (g_dataObject == NULL)
    {
        return false;
    }
    return true;
}

wxDragResult wxDropTarget::OnData(wxCoord WXUNUSED(x),
                                  wxCoord WXUNUSED(y),
                                  wxDragResult def)
{
    return GetData() ? def : wxDragNone;
}

bool wxDropTarget::GetData()
{
    if (m_dataObject == NULL || g_dataObject == NULL)
    {
        return false;
    }

    wxDataFormat format = m_dataObject->GetPreferredFormatForObject(
                              *g_dataObject,
                              wxDataObjectBase::Set);

    if (format.GetType() == wxDF_INVALID)
    {
        return false;
    }
    size_t size = g_dataObject->GetDataSize(format);
    wxScopedCharBuffer buffer = wxScopedCharBuffer::CreateOwned(static_cast<char*>(malloc(size)), size);

    if (!g_dataObject->GetDataHere(format, buffer.data()))
    {
        return false;
    }

    return m_dataObject->SetData(format, size, buffer.data());
}

#endif // wxUSE_DRAG_AND_DROP
