/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/config.h
// Purpose:     wxLocalStorageConfig class
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////


#ifndef _WX_WASM_CONFIG_H_
#define _WX_WASM_CONFIG_H_

#include "wx/defs.h"

#if wxUSE_CONFIG

#include "wx/string.h"
#include "wx/confbase.h"

// ----------------------------------------------------------------------------
// wxLocalStorageConfig
// ----------------------------------------------------------------------------

/*
*/

class WXDLLIMPEXP_BASE wxLocalStorageConfig : public wxConfigBase
{
public:
  // ctor & dtor
  wxLocalStorageConfig(const wxString& appName = wxEmptyString,
                       const wxString& vendorName = wxEmptyString,
                       const wxString& localFilename = wxEmptyString,
                       const wxString& globalFilename = wxEmptyString,
                       long style = 0);

  virtual ~wxLocalStorageConfig();

  // implement inherited pure virtual functions
  // ------------------------------------------

  // path management
  virtual void SetPath(const wxString& strPath) wxOVERRIDE;
  virtual const wxString& GetPath() const wxOVERRIDE { return m_strPath; }

  // entry/subgroup info
  // enumerate all of them
  virtual bool GetFirstGroup(wxString& str, long& lIndex) const wxOVERRIDE;
  virtual bool GetNextGroup (wxString& str, long& lIndex) const wxOVERRIDE;
  virtual bool GetFirstEntry(wxString& str, long& lIndex) const wxOVERRIDE;
  virtual bool GetNextEntry (wxString& str, long& lIndex) const wxOVERRIDE;

  // tests for existence
  virtual bool HasGroup(const wxString& strName) const wxOVERRIDE;
  virtual bool HasEntry(const wxString& strName) const wxOVERRIDE;

  // get number of entries/subgroups in the current group, with or without
  // it's subgroups
  virtual size_t GetNumberOfEntries(bool bRecursive = false) const wxOVERRIDE;
  virtual size_t GetNumberOfGroups(bool bRecursive = false) const wxOVERRIDE;

  virtual bool Flush(bool WXUNUSED(bCurrentOnly) = false) wxOVERRIDE { return true; }

  // rename
  virtual bool RenameEntry(const wxString& oldName, const wxString& newName) wxOVERRIDE;
  virtual bool RenameGroup(const wxString& oldName, const wxString& newName) wxOVERRIDE;

  // delete
  virtual bool DeleteEntry(const wxString& key, bool bGroupIfEmptyAlso = true) wxOVERRIDE;
  virtual bool DeleteGroup(const wxString& key) wxOVERRIDE;
  virtual bool DeleteAll() wxOVERRIDE;

protected:
  virtual bool DoReadString(const wxString& key, wxString *pstr) const wxOVERRIDE;
  virtual bool DoReadLong(const wxString& key, long *pl) const wxOVERRIDE;
  virtual bool DoReadBool(const wxString& key, bool *pb) const wxOVERRIDE;
#if wxUSE_BASE64
  virtual bool DoReadBinary(const wxString& key, wxMemoryBuffer* buf) const wxOVERRIDE;
#endif // wxUSE_BASE64

  virtual bool DoWriteString(const wxString& key, const wxString& str) wxOVERRIDE;
  virtual bool DoWriteLong(const wxString& key, long l) wxOVERRIDE;
  virtual bool DoWriteBool(const wxString& key, bool b) wxOVERRIDE;
#if wxUSE_BASE64
  virtual bool DoWriteBinary(const wxString& key, const wxMemoryBuffer& buf) wxOVERRIDE;
#endif // wxUSE_BASE64

  wxString MakeEntryKey(const wxString& key) const;
  wxString MakeGroupKey(const wxString& key) const;

private:
  // member variables
  // ----------------
  wxString  m_strPath;

  wxDECLARE_NO_COPY_CLASS(wxLocalStorageConfig);
  wxDECLARE_ABSTRACT_CLASS(wxLocalStorageConfig);
};

#endif // wxUSE_CONFIG

#endif // _WX_WASM_CONFIG_H_
