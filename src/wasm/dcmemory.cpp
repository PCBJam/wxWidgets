/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dcmemory.cpp
// Purpose:     wxMemoryDC implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/wasm/dcmemory.h"

#include <emscripten.h>

// ----------------------------------------------------------------------------
// wxMemoryDCImpl
// ----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxMemoryDCImpl, wxWasmDCImpl)

wxMemoryDCImpl::wxMemoryDCImpl(wxMemoryDC *owner)
    : wxWasmDCImpl(owner)
{
    Init();
}

wxMemoryDCImpl::wxMemoryDCImpl(wxMemoryDC *owner, wxBitmap& bitmap)
    : wxWasmDCImpl(owner)
{
    Init();
    DoSelect(bitmap);
}

wxMemoryDCImpl::wxMemoryDCImpl(wxMemoryDC *owner, wxDC *dc)
    : wxWasmDCImpl(owner)
{
    Init();
    m_contentScaleFactor = dc->GetContentScaleFactor();
}

wxMemoryDCImpl::~wxMemoryDCImpl(void)
{
    Deselect();
}

void wxMemoryDCImpl::Init()
{
    m_ok = false;
}

void wxMemoryDCImpl::DoGetSize(int *width, int *height) const
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    if (width)
    {
        *width = m_bitmap.GetWidth();
    }
    if (height)
    {
        *height = m_bitmap.GetHeight();
    }
}

void wxMemoryDCImpl::DoSelect(const wxBitmap& bitmap)
{
    Deselect();

    m_bitmap = bitmap;
    m_contentScaleFactor = bitmap.IsOk() ? bitmap.GetScaleFactor() : 1.0;

    if (m_bitmap.IsOk())
    {
        m_bitmap.SyncToJs();

        int jsId = EM_ASM_INT({
            return createMemoryContext($0, $1);
        }, m_bitmap.GetJavascriptId(), GetContentScaleFactor());

        SetJavascriptId(jsId);
        m_ok = true;
    }
}

void wxMemoryDCImpl::Deselect()
{
    if (m_bitmap.IsOk())
    {
        EM_ASM({
            destroyMemoryContext($0);
        }, GetJavascriptId());

        SetJavascriptId(-1);
        m_ok = false;
    }
}

// TODO: wrap drawing methods and SyncToJs the selected bitmap
