///////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dataobj.h
// Purpose:     wxDataObject class
// Author:      Adam Hilss
// Copyright:   (c) Adam Hilss
// Licence:     LGPL v2
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_DATAOBJ_H_
#define _WX_WASM_DATAOBJ_H_

// ----------------------------------------------------------------------------
// wxDataObject is the same as wxDataObjectBase under wxWebAssembly
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxDataObject : public wxDataObjectBase
{
public:
    wxDataObject() {}
    virtual ~wxDataObject() {}

    virtual wxDataFormat GetPreferredFormatForObject(const wxDataObject& obj, Direction dir) const;

    wxDataFormat GetSupportedFormatInSource(wxDataObject *source) const;
};

#endif // _WX_WASM_DATAOBJ_H_

