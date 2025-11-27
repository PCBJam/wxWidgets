/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dataobj.cpp
// Purpose:     wxDataObject implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_DATAOBJ

#include "wx/dataobj.h"

#ifndef WX_PRECOMP
#endif // WX_PRECOMP

#include "wx/mstream.h"
#include "wx/scopedarray.h"

#include <string.h>

// ----------------------------------------------------------------------------
// wxDataFormat
// ----------------------------------------------------------------------------

wxDataFormat::wxDataFormat()
    : m_type(wxDF_INVALID)
{
}

wxDataFormat::wxDataFormat(wxDataFormatId type)
    : m_type(type)
{
}

wxDataFormat::wxDataFormat(const wxDataFormat& format)
{
    *this = format;
}

wxDataFormat& wxDataFormat::operator=(const wxDataFormat& format)
{
    m_type = format.m_type;
    m_id = format.m_id;
    return *this;
}

bool wxDataFormat::operator==(wxDataFormat format) const
{
    return m_type == format.m_type && m_id == format.m_id;
}

bool wxDataFormat::operator!=(wxDataFormat format) const
{
    return m_type != format.m_type || m_id != format.m_id;
}

bool wxDataFormat::operator==(wxDataFormatId type) const
{
    return m_type == type;
}

bool wxDataFormat::operator!=(wxDataFormatId type) const
{
    return m_type != type;
}

wxString wxDataFormat::GetId() const
{
    wxCHECK_MSG(m_type == wxDF_PRIVATE, wxEmptyString,
                wxT("id invalid for standard format"));
    return m_id;
}

void wxDataFormat::SetId(const wxString& id)
{
    m_type = wxDF_PRIVATE;
    m_id = id;
}

wxDataFormatId wxDataFormat::GetType() const
{
    return m_type;
}

void wxDataFormat::SetType(wxDataFormatId type)
{
    m_type = type;
    if (m_type != wxDF_PRIVATE)
    {
        m_id = wxEmptyString;
    }
}

void wxDataFormat::InitFromString(const wxString& id)
{
    SetId(id);
}

// ----------------------------------------------------------------------------
// wxDataObject
// ----------------------------------------------------------------------------

wxDataFormat wxDataObject::GetPreferredFormatForObject(const wxDataObject& obj,
        Direction dir) const
{
    Direction otherDir;
    switch (dir)
    {
        case Get:
            otherDir = Set;
            break;
        case Set:
            otherDir = Get;
            break;
        case Both:
            otherDir = Both;
            break;
    }

    wxDataFormat preferredFormat = GetPreferredFormat(dir);
    if (obj.IsSupported(preferredFormat, otherDir))
    {
        return preferredFormat;
    }

    wxDataFormat otherPreferredFormat = obj.GetPreferredFormat(otherDir);
    if (IsSupported(otherPreferredFormat, dir))
    {
        return otherPreferredFormat;
    }

    size_t formatCount = GetFormatCount();
    wxDataFormat *formats = new wxDataFormat[formatCount];
    GetAllFormats(formats, dir);

    wxDataFormat format(wxDF_INVALID);
    for (size_t i = 0; i < formatCount; ++i)
    {
        if (obj.IsSupported(formats[i], otherDir))
        {
            format = formats[i];
            break;
        }
    }

    delete [] formats;

    return format;
}

wxDataFormat wxDataObject::GetSupportedFormatInSource(wxDataObject *source) const
{
    wxDataFormat format;
    size_t formatcount = source->GetFormatCount();
    wxScopedArray<wxDataFormat> array(formatcount);

    source->GetAllFormats( array.get() );
    for (size_t i = 0; i < formatcount; i++)
    {
        wxDataFormat testFormat = array[i];
        if ( IsSupported( testFormat, wxDataObject::Set ) )
        {
            format = testFormat;
            break;
        }
    }
    return format;
}

// ----------------------------------------------------------------------------
// wxBitmapDataObject
// ----------------------------------------------------------------------------

wxBitmapDataObject::wxBitmapDataObject()
{
    Init();
}

wxBitmapDataObject::wxBitmapDataObject(const wxBitmap& bitmap)
    : wxBitmapDataObjectBase(bitmap)
{
    Init();

    DoConvertToPng();
}

wxBitmapDataObject::~wxBitmapDataObject()
{
    Clear();
}

void wxBitmapDataObject::SetBitmap( const wxBitmap &bitmap )
{
    ClearAll();

    wxBitmapDataObjectBase::SetBitmap(bitmap);

    DoConvertToPng();
}

bool wxBitmapDataObject::GetDataHere(void *buf) const
{
    if ( !m_pngSize )
    {
        wxFAIL_MSG( wxT("attempt to copy empty bitmap failed") );

        return false;
    }

    memcpy(buf, m_pngData, m_pngSize);

    return true;
}

bool wxBitmapDataObject::SetData(size_t size, const void *buf)
{
    Clear();

    wxCHECK_MSG( wxImage::FindHandler(wxBITMAP_TYPE_PNG) != NULL,
                 false, wxT("You must call wxImage::AddHandler(new wxPNGHandler); to be able to use clipboard with bitmaps!") );

    m_pngSize = size;
    m_pngData = new char[m_pngSize];

    memcpy(m_pngData, buf, m_pngSize);

    wxMemoryInputStream mstream((char*) m_pngData, m_pngSize);
    wxImage image;
    if ( !image.LoadFile( mstream, wxBITMAP_TYPE_PNG ) )
    {
        return false;
    }

    m_bitmap = wxBitmap(image);

    return m_bitmap.IsOk();
}

void wxBitmapDataObject::DoConvertToPng()
{
    if ( !m_bitmap.IsOk() )
        return;

    wxCHECK_RET( wxImage::FindHandler(wxBITMAP_TYPE_PNG) != NULL,
                 wxT("You must call wxImage::AddHandler(new wxPNGHandler); to be able to use clipboard with bitmaps!") );

    wxImage image = m_bitmap.ConvertToImage();

    wxCountingOutputStream count;
    image.SaveFile(count, wxBITMAP_TYPE_PNG);

    m_pngSize = count.GetSize() + 100; // sometimes the size seems to vary ???
    m_pngData = new char[m_pngSize];

    wxMemoryOutputStream mstream((char*) m_pngData, m_pngSize);
    image.SaveFile(mstream, wxBITMAP_TYPE_PNG);
}

// ----------------------------------------------------------------------------
// wxFileDataObject
// ----------------------------------------------------------------------------

void wxFileDataObject::AddFile(const wxString &filename)
{
    m_filenames.Add(filename);
}

size_t wxFileDataObject::GetDataSize() const
{
    size_t count = m_filenames.GetCount();
    size_t size = sizeof(count);

    for (size_t i = 0; i < count; ++i)
    {
        const wxScopedCharBuffer filename(m_filenames[i].utf8_str());
        size_t length = filename.length();

        size += sizeof(length) + length;
    }

    return size;
}

bool wxFileDataObject::GetDataHere(void *buf) const
{
    if (!buf)
    {
        return false;
    }

    char* bufptr = static_cast<char*>(buf);

    size_t count = m_filenames.GetCount();
    memcpy(bufptr, &count, sizeof(count));
    bufptr += sizeof(count);

    for (size_t i = 0; i < count; ++i)
    {
        const wxScopedCharBuffer filename(m_filenames[i].utf8_str());
        if (!filename)
        {
            return false;
        }

        size_t length = filename.length();

        memcpy(bufptr, &length, sizeof(length));
        bufptr += sizeof(length);
        memcpy(bufptr, filename, length);
        bufptr += length;
    }

    return true;
}

bool wxFileDataObject::SetData(size_t len, const void *buf)
{
    m_filenames.Clear();

    if (!buf)
    {
        return false;
    }

    const char* bufptr = static_cast<const char*>(buf);
    int bytesLeft = len;

    size_t count;
    if (bytesLeft < sizeof(count))
    {
        return false;
    }
    memcpy(&count, bufptr, sizeof(count));
    bufptr += sizeof(count);
    bytesLeft -= sizeof(count);

    for (size_t i = 0; i < count; ++i)
    {
        size_t length;
        if (bytesLeft < sizeof(length))
        {
            return false;
        }
        memcpy(&length, bufptr, sizeof(length));
        bufptr += sizeof(length);
        bytesLeft -= sizeof(length);

        if (bytesLeft < length)
        {
            return false;
        }
        wxString filename = wxString::FromUTF8(bufptr, length);
        bufptr += length;
        bytesLeft -= length;

        m_filenames.Add(filename);
    }

    return true;
}

#endif // wxUSE_DATAOBJ
