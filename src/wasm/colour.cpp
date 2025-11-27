/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/colour.cpp
// Purpose:     wxColour implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/colour.h"

#ifndef WX_PRECOMP
#include "wx/gdicmn.h"
#endif

#include <emscripten.h>

// ----------------------------------------------------------------------------
// wxColour
// ----------------------------------------------------------------------------

wxColour::~wxColour()
{
}

void wxColour::Init()
{
    m_red = 0;
    m_green = 0;
    m_blue = 0;
    m_alpha = wxALPHA_OPAQUE;
    m_isInit = false;
}

void wxColour::InitRGBA(unsigned char r,
                        unsigned char g,
                        unsigned char b,
                        unsigned char a)
{
    m_red = r;
    m_green = g;
    m_blue = b;
    m_alpha = a;
    m_isInit = true;
}

wxColour& wxColour::operator=(const wxColour& col)
{
    m_red = col.m_red;
    m_green = col.m_green;
    m_blue = col.m_blue;
    m_alpha = col.m_alpha;
    m_isInit = col.m_isInit;

    return *this;
}
