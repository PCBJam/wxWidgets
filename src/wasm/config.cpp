/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dc.cpp
// Purpose:     wxLocalStorageConfig implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////


// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#if wxUSE_CONFIG

#include "wx/config.h"

#ifndef WX_PRECOMP
#include "wx/string.h"
#include "wx/intl.h"
#include "wx/log.h"
#include "wx/event.h"
#include "wx/app.h"
#endif //WX_PRECOMP

#include "wx/wasm/config.h"

#include <emscripten.h>

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

#define ROOT_PREFIX wxT("config")

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ctor/dtor
// ----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(wxLocalStorageConfig, wxConfigBase);

wxLocalStorageConfig::wxLocalStorageConfig(const wxString& appName,
        const wxString& vendorName,
        const wxString& strLocal,
        const wxString& strGlobal,
        long style)
    : wxConfigBase(appName, vendorName, strLocal, strGlobal, style),
      m_strPath(wxCONFIG_PATH_SEPARATOR)
{
}

wxLocalStorageConfig::~wxLocalStorageConfig()
{
}

// ----------------------------------------------------------------------------
// path management
// ----------------------------------------------------------------------------

void wxLocalStorageConfig::SetPath(const wxString& strPath)
{
    if (strPath.empty())
    {
        m_strPath = wxCONFIG_PATH_SEPARATOR;
    }
    else // not root
    {
        // construct the full path
        wxString strFullPath;
        if (strPath[0u] == wxCONFIG_PATH_SEPARATOR)
        {
            // absolute path
            strFullPath = strPath;
        }
        else // relative path
        {
            strFullPath.reserve(m_strPath.length() + strPath.length() + 2);

            strFullPath << m_strPath;
            if (strFullPath.Len() == 0 ||
                strFullPath.Last() != wxCONFIG_PATH_SEPARATOR)
            {
                strFullPath << wxCONFIG_PATH_SEPARATOR;
            }
            strFullPath << strPath;
        }
        m_strPath = strFullPath;
    }

    if (strPath.Contains(wxT("..")))
    {
        wxArrayString parts;
        wxSplitPath(parts, m_strPath);

        m_strPath = wxCONFIG_PATH_SEPARATOR + wxJoin(parts, wxCONFIG_PATH_SEPARATOR, '\0');
    }
}

wxString wxLocalStorageConfig::MakeEntryKey(const wxString& key) const
{
    if (!key.empty() && key[0u] == wxCONFIG_PATH_SEPARATOR)
    {
        return ROOT_PREFIX + key;
    }
    else
    {
        return ROOT_PREFIX + GetPath() + key;
    }
}

wxString wxLocalStorageConfig::MakeGroupKey(const wxString& key) const
{
    wxString groupKey = MakeEntryKey(key);
    if (!groupKey.EndsWith(wxCONFIG_PATH_SEPARATOR))
    {
        groupKey << wxCONFIG_PATH_SEPARATOR;
    }
    return groupKey;
}

// ----------------------------------------------------------------------------
// enumeration (works only with current group)
// ----------------------------------------------------------------------------

bool wxLocalStorageConfig::GetFirstGroup(wxString& str, long& lIndex) const
{
    lIndex = 0;
    return GetNextGroup(str, lIndex);
}

bool wxLocalStorageConfig::GetNextGroup(wxString& str, long& lIndex) const
{
    const wxString prefix = MakeGroupKey("");
    const char *prefixCStr = static_cast<const char*>((prefix).mb_str(wxConvUTF8));

    int keyIndex = EM_ASM_INT({
        return getConfigGroupIndex(UTF8ToString($0), $1);
    }, prefixCStr, lIndex);

    if (keyIndex == -1)
    {
        return false;
    }

    lIndex++;

    int length = EM_ASM_INT({
        return getConfigKeyLength($0);
    }, keyIndex);

    if (length == 0)
    {
        str = "";
    }
    else
    {
        char *keyCStr = new char[length + 1];
        EM_ASM({
            getConfigKey($0, $1, $2);
        }, keyIndex, keyCStr, length + 1);

        char *start = keyCStr + strlen(prefixCStr);
        char *end = strchr(start, '/');
        if (end)
        {
            *end = '\0';
        }
        str = start;
        delete [] keyCStr;
    }

    return true;
}

bool wxLocalStorageConfig::GetFirstEntry(wxString& str, long& lIndex) const
{
    lIndex = 0;
    return GetNextEntry(str, lIndex);
}

bool wxLocalStorageConfig::GetNextEntry(wxString& str, long& lIndex) const
{
    const wxString prefix = MakeGroupKey("");
    const char *prefixCStr = static_cast<const char*>((prefix).mb_str(wxConvUTF8));

    int keyIndex = EM_ASM_INT({
        return getConfigEntryIndex(UTF8ToString($0), $1);
    }, prefixCStr, lIndex);

    if (keyIndex == -1)
    {
        return false;
    }

    lIndex++;

    int length = EM_ASM_INT({
        return getConfigKeyLength($0);
    }, keyIndex);

    if (length == 0)
    {
        str = "";
    }
    else
    {
        char *keyCStr = new char[length + 1];
        EM_ASM({
            getConfigKey($0, $1, $2);
        }, keyIndex, keyCStr, length + 1);

        str = keyCStr + strlen(prefixCStr);
        delete [] keyCStr;
    }

    return true;
}

size_t wxLocalStorageConfig::GetNumberOfEntries(bool bRecursive) const
{
    const wxString prefix = MakeGroupKey("");
    const char *prefixCStr = static_cast<const char*>((prefix).mb_str(wxConvUTF8));

    int numEntries = EM_ASM_INT({
        return getConfigEntryCount(UTF8ToString($0), $1);
    }, prefixCStr, bRecursive);

    return numEntries;
}

size_t wxLocalStorageConfig::GetNumberOfGroups(bool bRecursive) const
{
    const wxString prefix = MakeGroupKey("");
    const char *prefixCStr = static_cast<const char*>((prefix).mb_str(wxConvUTF8));

    int numGroups = EM_ASM_INT({
        return getConfigGroupCount(UTF8ToString($0));
    }, prefixCStr, bRecursive);

    return numGroups;
}

// ----------------------------------------------------------------------------
// tests for existence
// ----------------------------------------------------------------------------

bool wxLocalStorageConfig::HasGroup(const wxString& key) const
{
    const wxString groupKey = MakeGroupKey(key);
    const char *keyCStr = static_cast<const char*>((groupKey).mb_str(wxConvUTF8));

    bool hasGroup = EM_ASM_INT({
        return hasConfigGroup(UTF8ToString($0));
    }, keyCStr);

    return hasGroup;
}

bool wxLocalStorageConfig::HasEntry(const wxString& key) const
{
    const wxString entryKey = MakeEntryKey(key);
    const char *keyCStr = static_cast<const char*>((entryKey).mb_str(wxConvUTF8));

    bool hasEntry = EM_ASM_INT({
        return hasConfigEntry(UTF8ToString($0));
    }, keyCStr);

    return hasEntry;
}

// ----------------------------------------------------------------------------
// reading/writing
// ----------------------------------------------------------------------------

bool wxLocalStorageConfig::DoReadString(const wxString& key, wxString *pstr) const
{
    wxCHECK_MSG(pstr, false, wxT("wxLocalStorageConfig::Read(): NULL param"));

    const wxString entryKey = MakeEntryKey(key);
    const char *keyCStr = static_cast<const char*>((entryKey).mb_str(wxConvUTF8));

    //printf("GetItem: %s\n", keyCStr);

    int length = EM_ASM_INT({
        return getConfigEntryLength(UTF8ToString($0));
    }, keyCStr);

    if (length == -1)
    {
        return false;
    }
    else if (length == 0)
    {
        pstr->Clear();
        return true;
    }
    else
    {
        char *valueCStr = new char[length + 1];
        valueCStr[0] = '\0';

        EM_ASM({
            getConfigEntry(UTF8ToString($0), $1, $2);
        }, keyCStr, valueCStr, length + 1);

        *pstr = valueCStr;
        delete [] valueCStr;

        return true;
    }
}

bool wxLocalStorageConfig::DoReadLong(const wxString& key, long *pl) const
{
    wxCHECK_MSG(pl, false, wxT("wxLocalStorageConfig::Read(): NULL param"));

    wxString str;
    if (!Read(key, &str))
        return false;

    return str.ToLong(pl);
}

bool wxLocalStorageConfig::DoReadBool(const wxString& key, bool *pb) const
{
    wxCHECK_MSG(pb, false, wxT("wxLocalStorageConfig::Read(): NULL param"));

    wxString value;

    if (Read(key, &value))
    {
        if (value == "true")
        {
            *pb = true;
            return true;
        }
        else if (value == "false")
        {
            *pb = false;
            return true;
        }
        else
        {
            wxLogWarning(_("Invalid value \"%s\" for a boolean key \"%s\" in "
                           "config file."),
                         value, key);
            return false;
        }
    }
    else
    {
        return false;
    }
}

#if wxUSE_BASE64
bool wxLocalStorageConfig::DoReadBinary(const wxString& key, wxMemoryBuffer* buf) const
{
    wxCHECK_MSG(buf, false, wxT("NULL buffer"));

    wxString value;
    if (!Read(key, &value))
        return false;

    *buf = wxBase64Decode(value);

    return true;
}
#endif // wxUSE_BASE64

bool wxLocalStorageConfig::DoWriteString(const wxString& key, const wxString& str)
{
    const wxString entryKey = MakeEntryKey(key);
    const char *keyCStr = static_cast<const char*>((entryKey).mb_str(wxConvUTF8));
    const char *valueCStr = static_cast<const char*>((str).mb_str(wxConvUTF8));

    //printf("SetItem: %s=%s\n", keyCStr, valueCStr);

    EM_ASM({
        setConfigEntry(UTF8ToString($0), UTF8ToString($1));
    }, keyCStr, valueCStr);

    return true;
}

bool wxLocalStorageConfig::DoWriteLong(const wxString& key, long l)
{
    return Write(key, wxString::Format(wxT("%ld"), l));
}

bool wxLocalStorageConfig::DoWriteBool(const wxString& key, bool b)
{
    return Write(key, b ? "true" : "false");
}

#if wxUSE_BASE64
bool wxLocalStorageConfig::DoWriteBinary(const wxString& key, const wxMemoryBuffer& buf)
{
    return Write(key, wxBase64Encode(buf));
}
#endif // wxUSE_BASE64

// ----------------------------------------------------------------------------
// renaming
// ----------------------------------------------------------------------------

bool wxLocalStorageConfig::RenameEntry(const wxString& oldName, const wxString& newName)
{
    wxString value;
    if (!Read(oldName, &value))
    {
        return false;
    }
    else
    {
        if (Write(newName, value))
        {
            DeleteEntry(oldName);
            return true;
        }
        else
        {
            return false;
        }
    }

    return true;
}

bool wxLocalStorageConfig::RenameGroup(const wxString& oldName, const wxString& newName)
{
    const wxString oldGroupKey = MakeGroupKey(oldName);
    const char *oldGroupKeyCStr = static_cast<const char*>((oldGroupKey).mb_str(wxConvUTF8));

    const wxString newGroupKey = MakeGroupKey(newName);
    const char *newGroupKeyCStr = static_cast<const char*>((newGroupKey).mb_str(wxConvUTF8));

    bool retval = EM_ASM_INT({
        renameConfigGroup(UTF8ToString($0), UTF8ToString($1));
    }, oldGroupKeyCStr, newGroupKeyCStr);

    return retval;
}

// ----------------------------------------------------------------------------
// deleting
// ----------------------------------------------------------------------------

bool wxLocalStorageConfig::DeleteEntry(const wxString& key, bool WXUNUSED(bGroupIfEmptyAlso))
{
    if (HasEntry(key))
    {
        const wxString entryKey = MakeEntryKey(key);
        const char *keyCStr = static_cast<const char*>((entryKey).mb_str(wxConvUTF8));

        EM_ASM({
            removeConfigEntry(UTF8ToString($0));
        }, keyCStr);

        return true;
    }
    else
    {
        return false;
    }
}

bool wxLocalStorageConfig::DeleteGroup(const wxString& key)
{
    const wxString groupKey = MakeGroupKey(key);
    const char *keyCStr = static_cast<const char*>((groupKey).mb_str(wxConvUTF8));

    bool groupDeleted = EM_ASM_INT({
        return removeConfigGroup(UTF8ToString($0));
    }, keyCStr);

    wxString path = GetPath();
    while (!HasGroup(path) && !path.empty())
    {
        path = path.BeforeLast(wxCONFIG_PATH_SEPARATOR);
    }
    SetPath(path);

    return groupDeleted;
}

bool wxLocalStorageConfig::DeleteAll()
{
    EM_ASM({
        clearConfig();
    });
    return true;
}

#endif // wxUSE_CONFIG
