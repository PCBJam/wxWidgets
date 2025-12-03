/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/clipbrd.cpp
// Purpose:     wxClipboard implementation for WASM using browser Clipboard API
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_CLIPBOARD

#include "wx/clipbrd.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/utils.h"
    #include "wx/dataobj.h"
#endif // WX_PRECOMP

#include <emscripten.h>
#include <string.h>

//-----------------------------------------------------------------------------
// JavaScript helper functions using Asyncify
//-----------------------------------------------------------------------------

// Check if the browser Clipboard API is available
EM_JS(bool, js_isClipboardAPIAvailable, (), {
    return typeof navigator !== 'undefined' &&
           typeof navigator.clipboard !== 'undefined' &&
           typeof navigator.clipboard.writeText === 'function';
});

// Helper to create a timeout promise
// Write text to clipboard using Asyncify
// Returns: 0 = success, 1 = no API, 2 = permission denied, 3 = other error, 4 = timeout
EM_JS(int, js_writeTextToClipboard, (const char* text), {
    return Asyncify.handleAsync(async () => {
        if (typeof navigator === 'undefined' ||
            typeof navigator.clipboard === 'undefined') {
            console.warn('[wxClipboard] Clipboard API not available');
            return 1;
        }

        try {
            const textStr = UTF8ToString(text);

            // Add timeout to prevent hanging - clipboard should be fast
            const timeoutMs = 2000;
            const timeoutPromise = new Promise((_, reject) => {
                setTimeout(() => reject(new Error('Clipboard operation timed out')), timeoutMs);
            });

            await Promise.race([
                navigator.clipboard.writeText(textStr),
                timeoutPromise
            ]);

            return 0;
        } catch (err) {
            if (err.name === 'NotAllowedError') {
                console.warn('[wxClipboard] Clipboard write permission denied: ' + err.message);
                return 2;
            }
            if (err.message && err.message.includes('timed out')) {
                console.warn('[wxClipboard] Clipboard write timed out');
                return 4;
            }
            console.error('[wxClipboard] Clipboard write error: ' + err.message);
            return 3;
        }
    });
});

// Read text from clipboard using Asyncify
// Returns the text or NULL on failure. Caller must free with free().
EM_JS(char*, js_readTextFromClipboard, (), {
    return Asyncify.handleAsync(async () => {
        if (typeof navigator === 'undefined' ||
            typeof navigator.clipboard === 'undefined') {
            console.warn('[wxClipboard] Clipboard API not available');
            return 0;  // NULL
        }

        try {
            // Add timeout to prevent hanging
            const timeoutMs = 2000;
            const timeoutPromise = new Promise((_, reject) => {
                setTimeout(() => reject(new Error('Clipboard operation timed out')), timeoutMs);
            });

            const text = await Promise.race([
                navigator.clipboard.readText(),
                timeoutPromise
            ]);

            // Allocate memory for the string and copy it
            const len = lengthBytesUTF8(text) + 1;
            const ptr = _malloc(len);
            if (ptr === 0) {
                console.error('[wxClipboard] Failed to allocate memory for clipboard text');
                return 0;
            }
            stringToUTF8(text, ptr, len);
            return ptr;
        } catch (err) {
            if (err.name === 'NotAllowedError') {
                console.warn('[wxClipboard] Clipboard read permission denied: ' + err.message);
            } else if (err.message && err.message.includes('timed out')) {
                console.warn('[wxClipboard] Clipboard read timed out');
            } else {
                console.error('[wxClipboard] Clipboard read error: ' + err.message);
            }
            return 0;  // NULL
        }
    });
});

// Check if clipboard has text content using Asyncify
// Returns: 0 = no text, 1 = has text, -1 = error/unavailable
EM_JS(int, js_clipboardHasText, (), {
    return Asyncify.handleAsync(async () => {
        if (typeof navigator === 'undefined' ||
            typeof navigator.clipboard === 'undefined') {
            return -1;
        }

        try {
            // Add timeout to prevent hanging
            const timeoutMs = 2000;
            const timeoutPromise = new Promise((_, reject) => {
                setTimeout(() => reject(new Error('Clipboard operation timed out')), timeoutMs);
            });

            // Try to read to check availability
            const text = await Promise.race([
                navigator.clipboard.readText(),
                timeoutPromise
            ]);
            return (text && text.length > 0) ? 1 : 0;
        } catch (err) {
            // Permission denied or other error - we can't determine
            console.warn('[wxClipboard] Cannot check clipboard content: ' + err.message);
            return -1;
        }
    });
});

// Clear the clipboard by writing empty text
EM_JS(int, js_clearClipboard, (), {
    return Asyncify.handleAsync(async () => {
        if (typeof navigator === 'undefined' ||
            typeof navigator.clipboard === 'undefined') {
            return 1;
        }

        try {
            // Add timeout to prevent hanging
            const timeoutMs = 2000;
            const timeoutPromise = new Promise((_, reject) => {
                setTimeout(() => reject(new Error('Clipboard operation timed out')), timeoutMs);
            });

            await Promise.race([
                navigator.clipboard.writeText(''),
                timeoutPromise
            ]);
            return 0;
        } catch (err) {
            console.warn('[wxClipboard] Failed to clear clipboard: ' + err.message);
            return 1;
        }
    });
});

//-----------------------------------------------------------------------------
// wxClipboard implementation
//-----------------------------------------------------------------------------

IMPLEMENT_DYNAMIC_CLASS(wxClipboard, wxClipboardBase)

wxClipboard::wxClipboard()
    : m_isOpened(false)
{
}

wxClipboard::~wxClipboard()
{
    Close();
}

bool wxClipboard::Open()
{
    if (m_isOpened)
    {
        wxLogDebug(wxT("wxClipboard::Open() called when already open"));
        return true;
    }

    m_isOpened = true;
    return true;
}

void wxClipboard::Close()
{
    m_isOpened = false;
}

bool wxClipboard::IsOpened() const
{
    return m_isOpened;
}

bool wxClipboard::SetData(wxDataObject *data)
{
    // SetData clears existing data and sets new data
    Clear();
    return AddData(data);
}

bool wxClipboard::AddData(wxDataObject *data)
{
    wxCHECK_MSG(m_isOpened, false, wxT("clipboard not open"));
    wxCHECK_MSG(data, false, wxT("data is NULL"));

    // Check if this is text data
    bool hasText = data->IsSupported(wxDF_TEXT) ||
                   data->IsSupported(wxDF_UNICODETEXT);

    if (hasText)
    {
        // Get the text data
        wxDataFormat format = data->IsSupported(wxDF_UNICODETEXT) ?
                              wxDF_UNICODETEXT : wxDF_TEXT;

        size_t size = data->GetDataSize(format);
        if (size == 0)
        {
            // Empty text is valid
            m_textCache.Clear();
            delete data;
            return true;
        }

        // Allocate buffer for the data
        char* buf = new char[size + 1];
        buf[size] = '\0';

        if (!data->GetDataHere(format, buf))
        {
            delete[] buf;
            delete data;
            wxLogError(wxT("wxClipboard::AddData - failed to get data"));
            return false;
        }

        // Store in cache
        m_textCache = wxString::FromUTF8(buf, size);
        delete[] buf;
        delete data;

        // Write to browser clipboard
        if (js_isClipboardAPIAvailable())
        {
            const wxScopedCharBuffer utf8 = m_textCache.utf8_str();
            int result = js_writeTextToClipboard(utf8.data());

            if (result == 0)
            {
                return true;
            }
            else if (result == 2)
            {
                wxLogWarning(wxT("Clipboard access denied - requires user gesture"));
                // Data is still cached locally
                return true;
            }
            else
            {
                wxLogWarning(wxT("Failed to write to browser clipboard, using local cache"));
                return true;
            }
        }

        // Even if browser clipboard unavailable, we have local cache
        return true;
    }

    // For non-text formats, we only support local cache for now
    wxLogDebug(wxT("wxClipboard::AddData - non-text format not supported in WASM"));
    delete data;
    return false;
}

bool wxClipboard::IsSupported(const wxDataFormat& format)
{
    // For text formats, check browser clipboard or local cache
    if (format == wxDF_TEXT || format == wxDF_UNICODETEXT)
    {
        // First check local cache
        if (!m_textCache.IsEmpty())
        {
            return true;
        }

        // Try browser clipboard
        if (js_isClipboardAPIAvailable())
        {
            int result = js_clipboardHasText();
            if (result == 1)
            {
                return true;
            }
        }
    }

    return false;
}

bool wxClipboard::GetData(wxDataObject& data)
{
    wxCHECK_MSG(m_isOpened, false, wxT("clipboard not open"));

    // Check what formats the data object wants
    bool wantsText = data.IsSupported(wxDF_TEXT) ||
                     data.IsSupported(wxDF_UNICODETEXT);

    if (wantsText)
    {
        wxString text;
        bool gotFromBrowser = false;

        // Try to get from browser clipboard first
        if (js_isClipboardAPIAvailable())
        {
            char* browserText = js_readTextFromClipboard();
            if (browserText != nullptr)
            {
                text = wxString::FromUTF8(browserText);
                free(browserText);
                gotFromBrowser = true;

                // Update local cache
                m_textCache = text;
            }
        }

        // Fall back to local cache if browser read failed
        if (!gotFromBrowser && !m_textCache.IsEmpty())
        {
            text = m_textCache;
        }

        if (!text.IsEmpty())
        {
            // Set the data on the receiving object
            wxTextDataObject* textData = dynamic_cast<wxTextDataObject*>(&data);
            if (textData)
            {
                textData->SetText(text);
                return true;
            }

            // Alternative: use SetData with raw bytes
            wxDataFormat targetFormat = data.IsSupported(wxDF_UNICODETEXT) ?
                                        wxDF_UNICODETEXT : wxDF_TEXT;

            const wxScopedCharBuffer utf8 = text.utf8_str();
            size_t len = utf8.length();

            return data.SetData(targetFormat, len, utf8.data());
        }
    }

    return false;
}

void wxClipboard::Clear()
{
    // Clear local cache
    m_textCache.Clear();

    // Try to clear browser clipboard
    if (js_isClipboardAPIAvailable())
    {
        js_clearClipboard();
    }
}

bool wxClipboard::Flush()
{
    // In browser context, data is already "flushed" to the system clipboard
    // when we write it. However, browser clipboard doesn't persist after
    // the page closes.
    return !m_textCache.IsEmpty() || js_isClipboardAPIAvailable();
}

#endif // wxUSE_CLIPBOARD
