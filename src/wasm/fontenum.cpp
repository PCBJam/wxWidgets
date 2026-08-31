
/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/fontenum.cpp
// Purpose:     wxFontEnumerator using browser Local Font Access API
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/fontenum.h"

#ifndef WX_PRECOMP
    #include "wx/arrstr.h"
#endif

#include <emscripten.h>

#include "wx/wasm/private/yieldwait.h"

//-----------------------------------------------------------------------------
// JavaScript helper functions using scheduler token waits for the
// Local Font Access API
//-----------------------------------------------------------------------------

// Check if the Local Font Access API is available
EM_JS(bool, js_isFontAccessAPIAvailable, (), {
    return typeof window !== 'undefined' &&
           typeof window.queryLocalFonts === 'function';
});

// W5 (docs/features/async/22 §5): the enumeration opens a wait token, starts
// the query, and suspends via wxWasmYieldUntil. All output writes happen in
// the resolve callback BEFORE resolveWait — the suspended caller reads them
// only after it resumes. Resolution always defers to at least a microtask
// (the early-resolve contract).
//
// Wait result: number of fonts found, -1 on error/permission denied.
// Font names are stored in the provided array (caller allocates pointers, we
// allocate strings).
EM_JS(void, js_enumerateFontsStart, (int token, char** fontNames, int maxFonts, bool fixedWidthOnly), {
    const finish = (v) => globalThis.__wxScheduler.resolveWait(token, v);

    if (typeof window === 'undefined' ||
        typeof window.queryLocalFonts !== 'function') {
        console.warn('[wxFontEnumerator] Local Font Access API not available');
        Promise.resolve().then(() => finish(-1));
        return;
    }

    // Add timeout to prevent hanging
    const timeoutMs = 5000;
    const timeoutPromise = new Promise((_, reject) => {
        setTimeout(() => reject(new Error('Font enumeration timed out')), timeoutMs);
    });

    Promise.race([
        window.queryLocalFonts(),
        timeoutPromise
    ]).then((fonts) => {
        // Get unique family names
        const familySet = new Set();
        for (const font of fonts) {
            familySet.add(font.family);
        }

        // TODO: Filter by fixedWidthOnly if needed
        // This would require checking font metrics which is complex

        const families = Array.from(familySet).sort();
        const count = Math.min(families.length, maxFonts);

        // Allocate and copy font names
        for (let i = 0; i < count; i++) {
            const name = families[i];
            const len = lengthBytesUTF8(name) + 1;
            const ptr = _malloc(len);
            if (ptr === 0) {
                console.error('[wxFontEnumerator] Failed to allocate memory for font name');
                // Clean up already allocated names
                for (let j = 0; j < i; j++) {
                    _free(HEAPU32[fontNames/4 + j]);
                }
                return finish(-1);
            }
            stringToUTF8(name, ptr, len);
            HEAPU32[fontNames/4 + i] = ptr;
        }

        finish(count);
    }).catch((err) => {
        if (err.name === 'NotAllowedError') {
            console.warn('[wxFontEnumerator] Font access permission denied');
        } else if (err.message && err.message.includes('timed out')) {
            console.warn('[wxFontEnumerator] Font enumeration timed out');
        } else {
            console.error('[wxFontEnumerator] Font enumeration error: ' + err.message);
        }
        finish(-1);
    });
});

//-----------------------------------------------------------------------------
// wxFontEnumerator
//-----------------------------------------------------------------------------

bool wxFontEnumerator::EnumerateFacenames(wxFontEncoding WXUNUSED(encoding),
        bool fixedWidthOnly)
{
    // Check if Local Font Access API is available
    if (!js_isFontAccessAPIAvailable())
    {
        // API not available - return false but don't fail
        return false;
    }

    // Maximum number of fonts we'll enumerate
    const int MAX_FONTS = 500;

    // Allocate array of pointers for font names
    char** fontNames = new char*[MAX_FONTS];
    for (int i = 0; i < MAX_FONTS; i++)
    {
        fontNames[i] = nullptr;
    }

    // Call JavaScript to enumerate fonts
    const int token = wxWasmBeginWait("font");

    // Token 0 = the scheduler refused the wait (dead or terminal instance):
    // never start a completion that writes the heap of a damaged module.
    if (token <= 0)
    {
        delete[] fontNames;
        return false;
    }

    js_enumerateFontsStart(token, fontNames, MAX_FONTS, fixedWidthOnly);
    int count = wxWasmYieldUntil(token);

    if (count < 0)
    {
        // Error or permission denied
        delete[] fontNames;
        return false;
    }

    // Add each font to the enumerator
    for (int i = 0; i < count; i++)
    {
        if (fontNames[i] != nullptr)
        {
            wxString fontName = wxString::FromUTF8(fontNames[i]);
            if (!OnFacename(fontName))
            {
                // Callback returned false - stop enumeration
                // Clean up remaining names
                for (int j = i; j < count; j++)
                {
                    if (fontNames[j] != nullptr)
                    {
                        free(fontNames[j]);
                    }
                }
                delete[] fontNames;
                return true;  // We did enumerate some fonts
            }
            free(fontNames[i]);
        }
    }

    delete[] fontNames;
    return count > 0;
}

bool wxFontEnumerator::EnumerateEncodings(const wxString& WXUNUSED(family))
{
    // In browser/WASM environment, we primarily use Unicode (UTF-8)
    // Return just Unicode encoding
    OnFontEncoding(wxEmptyString, wxT("UTF-8"));
    return true;
}

