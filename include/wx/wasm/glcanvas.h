///////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/glcanvas.h
// Purpose:     wxGLCanvas for WebAssembly using WebGL via Emscripten
// Author:      KiCad WASM Project
// Created:     2024
// Copyright:   (c) 2024
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_GLCANVAS_H_
#define _WX_WASM_GLCANVAS_H_

#include "wx/glcanvas.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#include <GLES2/gl2.h>
#endif

class wxGLContextAttrs;
class wxGLAttributes;

// ----------------------------------------------------------------------------
// wxGLContext
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_GL wxGLContext : public wxGLContextBase
{
public:
    wxGLContext(wxGLCanvas *win,
                const wxGLContext *other = NULL,
                const wxGLContextAttrs *ctxAttrs = NULL);
    virtual ~wxGLContext();

    virtual bool SetCurrent(const wxGLCanvas& win) const wxOVERRIDE;

private:
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_glContext;

    wxDECLARE_CLASS(wxGLContext);
};

// ----------------------------------------------------------------------------
// wxGLCanvas
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_GL wxGLCanvas : public wxGLCanvasBase
{
public:
    wxGLCanvas() { Init(); }

    wxGLCanvas(wxWindow *parent,
               const wxGLAttributes& dispAttrs,
               wxWindowID id = wxID_ANY,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = 0,
               const wxString& name = wxGLCanvasName,
               const wxPalette& palette = wxNullPalette);

    explicit
    wxGLCanvas(wxWindow *parent,
               wxWindowID id = wxID_ANY,
               const int *attribList = NULL,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = 0,
               const wxString& name = wxGLCanvasName,
               const wxPalette& palette = wxNullPalette);

    bool Create(wxWindow *parent,
                const wxGLAttributes& dispAttrs,
                wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxGLCanvasName,
                const wxPalette& palette = wxNullPalette);

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxGLCanvasName,
                const int *attribList = NULL,
                const wxPalette& palette = wxNullPalette);

    virtual ~wxGLCanvas();

    // implement wxGLCanvasBase methods
    virtual bool SwapBuffers() wxOVERRIDE;

    // check if the given attributes are supported
    static bool IsDisplaySupported(const wxGLAttributes& dispAttrs);
    static bool IsDisplaySupported(const int *attribList);

    // Get the canvas target for Emscripten WebGL context
    const char* GetCanvasTarget() const { return m_canvasTarget.c_str(); }

    // Get the WebGL context handle (for use by wxGLContext)
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE GetWebGLContext() const { return m_webglContext; }

    // Override size and visibility handling to update the canvas element
    virtual bool Show(bool show = true) wxOVERRIDE;

protected:
    void Init();

    // Create the WebGL context
    bool CreateWebGLContext(const wxGLAttributes& dispAttrs);

    // DoSetSize is protected in wxWindow, so we keep it protected here
    virtual void DoSetSize(int x, int y, int width, int height,
                           int sizeFlags = wxSIZE_AUTO) wxOVERRIDE;

    // Convert wxGL attributes to Emscripten WebGL context attributes
    static void ConvertWXAttrsToWebGL(const wxGLAttributes& dispAttrs,
                                      EmscriptenWebGLContextAttributes& attrs);

private:
    std::string m_canvasTarget;  // Canvas element selector (e.g., "#window-123 canvas")
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_webglContext;
    int m_cssId;  // CSS ID for the canvas element in DOM

    wxDECLARE_CLASS(wxGLCanvas);
};

// ----------------------------------------------------------------------------
// wxGLApp
// ----------------------------------------------------------------------------

// this is used in wx/glcanvas.h, prevent it from defining a generic wxGLApp
#define wxGL_APP_DEFINED

class WXDLLIMPEXP_GL wxGLApp : public wxGLAppBase
{
public:
    wxGLApp() : wxGLAppBase() { }

    // implement wxGLAppBase method
    virtual bool InitGLVisual(const int *attribList) wxOVERRIDE;

private:
    wxDECLARE_DYNAMIC_CLASS(wxGLApp);
};

#endif // _WX_WASM_GLCANVAS_H_
