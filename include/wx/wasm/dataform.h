///////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dataform.h
// Purpose:     wxDataFormat class
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_DATAFORM_H
#define _WX_WASM_DATAFORM_H

class WXDLLIMPEXP_CORE wxDataFormat
{
public:
    wxDataFormat();
    wxDataFormat(wxDataFormatId type);
    wxDataFormat(const wxDataFormat& format);

    // we have to provide all the overloads to allow using strings instead of
    // data formats (as a lot of existing code does)
    wxDataFormat(const wxString& id) { InitFromString(id); }
    wxDataFormat(const char *id) { InitFromString(id); }
    wxDataFormat(const wchar_t *id) { InitFromString(id); }
    wxDataFormat(const wxCStrData& id) { InitFromString(id); }

    wxDataFormat& operator=(const wxDataFormat& format);

    // comparison (must have both versions)
    bool operator==(wxDataFormat format) const;
    bool operator!=(wxDataFormat format) const;
    bool operator==(wxDataFormatId type) const;
    bool operator!=(wxDataFormatId type) const;

    // string ids are used for custom types - this SetId() must be used for
    // application-specific formats
    wxString GetId() const;
    void SetId(const wxString& id);

    wxDataFormatId GetType() const;
    void SetType(wxDataFormatId type);

private:
    void InitFromString(const wxString& id);

    wxDataFormatId m_type;
    wxString m_id;
};

#endif // _WX_WASM_DATAFORM_H
