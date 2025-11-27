/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/region.h
// Purpose:     
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_REGION_H__
#define _WX_WASM_REGION_H__

#include "wx/generic/region.h"

class WXDLLIMPEXP_CORE wxRegion : public wxRegionGeneric
{
public:
    wxRegion(wxCoord x, wxCoord y, wxCoord w, wxCoord h)
    :   wxRegionGeneric(x,y,w,h)
    {}
    wxRegion(const wxPoint& topLeft, const wxPoint& bottomRight)
    :   wxRegionGeneric(topLeft, bottomRight)
    {}
    wxRegion(const wxRect& rect)
    :   wxRegionGeneric(rect)
    {}
    wxRegion(size_t n, const wxPoint *points, wxPolygonFillMode fillStyle = wxODDEVEN_RULE)
    :   wxRegionGeneric(n, points, fillStyle)
    {}
    wxRegion(const wxBitmap& bmp)
    :   wxRegionGeneric()
        { Union(bmp); }
    wxRegion(const wxBitmap& bmp,
             const wxColour& transColour, int tolerance = 0)
    :   wxRegionGeneric()
        { Union(bmp, transColour, tolerance); }
    virtual ~wxRegion() {}
    wxRegion(const wxRegion& r)
    :   wxRegionGeneric(r)
    {}
    wxRegion() {}
    wxRegion& operator= (const wxRegion& r)
    {   return *(wxRegion*)&(this->wxRegionGeneric::operator=(r)); }

private:
    wxDECLARE_DYNAMIC_CLASS(wxRegion);
};

class WXDLLIMPEXP_CORE wxRegionIterator : public wxRegionIteratorGeneric
{
public:
    wxRegionIterator() {}
    wxRegionIterator(const wxRegion& region)
    :   wxRegionIteratorGeneric(region)
    {}
    wxRegionIterator(const wxRegionIterator& iterator)
    :   wxRegionIteratorGeneric(iterator)
    {}
    virtual ~wxRegionIterator() {}

    wxRegionIterator& operator=(const wxRegionIterator& iter)
    {   return *(wxRegionIterator*)&(this->wxRegionIteratorGeneric::operator=(iter)); }
};

#endif // _WX_WASM_REGION_H__
