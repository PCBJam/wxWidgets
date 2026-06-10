/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/bmpbuttn.cpp
// Purpose:     wxBitmapButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_BMPBUTTON

#include "wx/bmpbuttn.h"

#include "wx/wasm/private/dom.h"

wxBitmapButton::wxBitmapButton()
{
}

wxBitmapButton::wxBitmapButton(wxWindow *parent,
                               wxWindowID id,
                               const wxBitmapBundle& bitmap,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style,
                               const wxValidator& validator,
                               const wxString& name)
{
    Create(parent, id, bitmap, pos, size, style, validator, name);
}

bool wxBitmapButton::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxBitmapBundle& bitmap,
                            const wxPoint& pos,
                            const wxSize& size,
                            long style,
                            const wxValidator& validator,
                            const wxString& name)
{
    // wxBitmapButtonBase::Create() goes through wxButton::Create() which
    // creates the "button" DOM node and sets its (empty) label.
    if (!wxBitmapButtonBase::Create(parent, id, pos, size, style,
                                    validator, name))
        return false;

    // Show the initial bitmap and resize accordingly:
    if (bitmap.IsOk())
    {
        // goes through wxAnyButton::DoSetBitmap() which pushes the bitmap
        // to the DOM <button>'s <img>
        wxBitmapButtonBase::SetBitmapLabel(bitmap);

        // we need to adjust the size after setting the bitmap as it may be
        // too big for the default button size
        SetInitialSize(size);
    }

    // Push the label bitmap once more now that the whole Create chain ran:
    // the image must always be set AFTER any SetLabel (wxDomSetText
    // replaces the element's children, dropping the <img>).
    if (WasmGetDomId())
    {
        const wxBitmap bmp = GetBitmapLabel();
        if (bmp.IsOk())
            wxDomSetImageDataURL(WasmGetDomId(), wxDomBitmapToDataURL(bmp),
                                 bmp.GetWidth(), bmp.GetHeight());
    }

    return true;
}

#endif // wxUSE_BMPBUTTON
