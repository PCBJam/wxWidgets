///////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/glcanvas.cpp
// Purpose:     wxGLCanvas for WebAssembly using WebGL via Emscripten
// Author:      KiCad WASM Project
// Created:     2024
// Copyright:   (c) 2024
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

#include "wx/wxprec.h"

#if wxUSE_GLCANVAS

#include "wx/glcanvas.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/app.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>
#endif

// ============================================================================
// wxGLContextAttrs implementation
// ============================================================================

wxGLContextAttrs& wxGLContextAttrs::CoreProfile()
{
    // WebGL doesn't support core profiles in the same way
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::MajorVersion(int val)
{
    if ( val > 0 )
    {
        AddAttribute(WX_GL_MAJOR_VERSION);
        AddAttribute(val);
    }
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::MinorVersion(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_MINOR_VERSION);
        AddAttribute(val);
    }
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::CompatibilityProfile()
{
    // WebGL doesn't support compatibility profiles
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ForwardCompatible()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ES2()
{
    AddAttribute(WX_GL_ES2);
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::DebugCtx()
{
    AddAttribute(WX_GL_DEBUG);
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::Robust()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::NoResetNotify()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::LoseOnReset()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ResetIsolation()
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::ReleaseFlush(int)
{
    return *this;
}

wxGLContextAttrs& wxGLContextAttrs::PlatformDefaults()
{
    return *this;
}

void wxGLContextAttrs::EndList()
{
    AddAttribute(0);
}

// ============================================================================
// wxGLAttributes implementation
// ============================================================================

wxGLAttributes& wxGLAttributes::RGBA()
{
    AddAttribute(WX_GL_RGBA);
    return *this;
}

wxGLAttributes& wxGLAttributes::BufferSize(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_BUFFER_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Level(int val)
{
    AddAttribute(WX_GL_LEVEL);
    AddAttribute(val);
    return *this;
}

wxGLAttributes& wxGLAttributes::DoubleBuffer()
{
    AddAttribute(WX_GL_DOUBLEBUFFER);
    return *this;
}

wxGLAttributes& wxGLAttributes::Stereo()
{
    // WebGL doesn't support stereo
    return *this;
}

wxGLAttributes& wxGLAttributes::AuxBuffers(int)
{
    // WebGL doesn't support aux buffers
    return *this;
}

wxGLAttributes& wxGLAttributes::MinRGBA(int mRed, int mGreen, int mBlue, int mAlpha)
{
    if ( mRed >= 0 )
    {
        AddAttribute(WX_GL_MIN_RED);
        AddAttribute(mRed);
    }
    if ( mGreen >= 0 )
    {
        AddAttribute(WX_GL_MIN_GREEN);
        AddAttribute(mGreen);
    }
    if ( mBlue >= 0 )
    {
        AddAttribute(WX_GL_MIN_BLUE);
        AddAttribute(mBlue);
    }
    if ( mAlpha >= 0 )
    {
        AddAttribute(WX_GL_MIN_ALPHA);
        AddAttribute(mAlpha);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Depth(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_DEPTH_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Stencil(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_STENCIL_SIZE);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::MinAcumRGBA(int mRed, int mGreen, int mBlue, int mAlpha)
{
    // WebGL doesn't support accumulation buffers
    wxUnusedVar(mRed);
    wxUnusedVar(mGreen);
    wxUnusedVar(mBlue);
    wxUnusedVar(mAlpha);
    return *this;
}

wxGLAttributes& wxGLAttributes::PlatformDefaults()
{
    // Use WebGL 2.0 by default (ES 3.0)
    return *this;
}

wxGLAttributes& wxGLAttributes::Defaults()
{
    RGBA().DoubleBuffer().Depth(16);
    return *this;
}

wxGLAttributes& wxGLAttributes::SampleBuffers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_SAMPLE_BUFFERS);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::Samplers(int val)
{
    if ( val >= 0 )
    {
        AddAttribute(WX_GL_SAMPLES);
        AddAttribute(val);
    }
    return *this;
}

wxGLAttributes& wxGLAttributes::FrameBuffersRGB()
{
    AddAttribute(WX_GL_FRAMEBUFFER_SRGB);
    return *this;
}

void wxGLAttributes::EndList()
{
    AddAttribute(0);
}

void wxGLAttributes::AddDefaultsForWXBefore31()
{
    Defaults();
    EndList();
}

// ============================================================================
// wxGLContext implementation
// ============================================================================

wxIMPLEMENT_CLASS(wxGLContext, wxObject);

wxGLContext::wxGLContext(wxGLCanvas *win,
                         const wxGLContext *other,
                         const wxGLContextAttrs *ctxAttrs)
    : m_glContext(0)
{
    wxUnusedVar(other);     // Context sharing not yet implemented
    wxUnusedVar(ctxAttrs);  // Context attributes handled in canvas

    m_isOk = false;

    if ( !win )
    {
        wxLogError("wxGLContext: NULL window specified");
        return;
    }

    // Get the context from the canvas (it's created there for WebGL)
    m_glContext = win->GetWebGLContext();
    m_isOk = (m_glContext > 0);

    if ( !m_isOk )
    {
        wxLogError("wxGLContext: Failed to get WebGL context from canvas");
    }
}

wxGLContext::~wxGLContext()
{
    // Context is owned by the canvas in WebGL, don't destroy here
    m_glContext = 0;
}

bool wxGLContext::SetCurrent(const wxGLCanvas& win) const
{
    if ( !m_isOk )
        return false;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = win.GetWebGLContext();
    if ( ctx <= 0 )
        return false;

    EMSCRIPTEN_RESULT result = emscripten_webgl_make_context_current(ctx);
    if ( result != EMSCRIPTEN_RESULT_SUCCESS )
        return false;

    // Initialize GLImmediate if not already done
    // This is needed because GLImmediate.init() is normally called only for the
    // main canvas context, but wxGLCanvas creates its own separate WebGL context.
    // Without this, GL emulation functions like glEnable crash with null texUnits.
    //
    // We need to set Browser.useWebGL=true first, because GLImmediate.init() checks
    // this flag and returns early without initializing TexEnvJIT if it's false.
    // The main wxWidgets canvas uses 2D context, so Browser.useWebGL would be false.
    EM_ASM({
        if (typeof GLImmediate !== 'undefined' && !GLImmediate.initted) {
            // Save old value and set useWebGL to true for init
            var oldUseWebGL = Browser.useWebGL;
            Browser.useWebGL = true;
            GLImmediate.init();
            // Restore original value
            Browser.useWebGL = oldUseWebGL;
        }
    });

    return true;
}

// ============================================================================
// wxGLCanvas implementation
// ============================================================================

wxIMPLEMENT_CLASS(wxGLCanvas, wxWindow);

void wxGLCanvas::Init()
{
    m_webglContext = 0;
    m_cssId = wxID_NONE;
    m_canvasTarget = "";  // Will be set dynamically in Create()
}

wxGLCanvas::wxGLCanvas(wxWindow *parent,
                       const wxGLAttributes& dispAttrs,
                       wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name,
                       const wxPalette& palette)
{
    Init();
    Create(parent, dispAttrs, id, pos, size, style, name, palette);
}

wxGLCanvas::wxGLCanvas(wxWindow *parent,
                       wxWindowID id,
                       const int *attribList,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxString& name,
                       const wxPalette& palette)
{
    Init();
    Create(parent, id, pos, size, style, name, attribList, palette);
}

bool wxGLCanvas::Create(wxWindow *parent,
                        const wxGLAttributes& dispAttrs,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name,
                        const wxPalette& palette)
{
    wxUnusedVar(palette);

    if ( !wxWindow::Create(parent, id, pos, size, style, name) )
        return false;

    // Create a dedicated GL canvas element in JavaScript
    // This canvas is separate from the 2D UI canvas to avoid context conflicts
    m_cssId = EM_ASM_INT({
        return createGLCanvas(true);
    });

    // Position the GL canvas element to match this window's screen position
    wxPoint screenPos = GetScreenPosition();
    wxSize clientSize = GetClientSize();
    EM_ASM({
        setGLCanvasRect($0, $1, $2, $3, $4);
    }, m_cssId, screenPos.x, screenPos.y, clientSize.GetWidth(), clientSize.GetHeight());

    // Set canvas selector to point to our dedicated GL canvas element
    char selectorBuf[64];
    snprintf(selectorBuf, sizeof(selectorBuf), "#glcanvas-%d", m_cssId);
    m_canvasTarget = selectorBuf;

    return CreateWebGLContext(dispAttrs);
}

bool wxGLCanvas::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name,
                        const int *attribList,
                        const wxPalette& palette)
{
    wxGLAttributes dispAttrs;
    if ( attribList )
    {
        ParseAttribList(attribList, dispAttrs, &m_GLCTXAttrs);
    }
    else
    {
        dispAttrs.Defaults().EndList();
    }

    return Create(parent, dispAttrs, id, pos, size, style, name, palette);
}

wxGLCanvas::~wxGLCanvas()
{
    if ( m_webglContext > 0 )
    {
        emscripten_webgl_destroy_context(m_webglContext);
        m_webglContext = 0;
    }

    // Destroy the GL canvas element we created
    if ( m_cssId != wxID_NONE )
    {
        EM_ASM({
            destroyGLCanvas($0);
        }, m_cssId);
        m_cssId = wxID_NONE;
    }
}

void wxGLCanvas::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);

    // Update the GL canvas element position to match the window
    if ( m_cssId != wxID_NONE )
    {
        wxPoint screenPos = GetScreenPosition();
        wxSize clientSize = GetClientSize();
        EM_ASM({
            setGLCanvasRect($0, $1, $2, $3, $4);
        }, m_cssId, screenPos.x, screenPos.y, clientSize.GetWidth(), clientSize.GetHeight());
    }
}

bool wxGLCanvas::Show(bool show)
{
    bool result = wxWindow::Show(show);

    // Update the GL canvas element visibility based on actual on-screen visibility.
    // This accounts for parent visibility - if any parent is hidden, the GL canvas
    // DOM element should be hidden too, even if this window's Show() is set to true.
    if ( m_cssId != wxID_NONE )
    {
        // Use IsShownOnScreen() to check both this window AND parent visibility
        bool actuallyVisible = IsShownOnScreen();
        EM_ASM({
            setGLCanvasVisibility($0, $1);
        }, m_cssId, actuallyVisible);
    }

    return result;
}

bool wxGLCanvas::CreateWebGLContext(const wxGLAttributes& dispAttrs)
{
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);

    // Convert wxGL attributes to WebGL attributes
    ConvertWXAttrsToWebGL(dispAttrs, attrs);

    // Create the WebGL context on the canvas
    m_webglContext = emscripten_webgl_create_context(m_canvasTarget.c_str(), &attrs);

    if ( m_webglContext <= 0 )
    {
        wxLogError("Failed to create WebGL context on canvas '%s' (error: %d)",
                   m_canvasTarget, m_webglContext);
        return false;
    }

    // Make it current immediately
    EMSCRIPTEN_RESULT result = emscripten_webgl_make_context_current(m_webglContext);
    if ( result != EMSCRIPTEN_RESULT_SUCCESS )
    {
        wxLogError("Failed to make WebGL context current (error: %d)", result);
        emscripten_webgl_destroy_context(m_webglContext);
        m_webglContext = 0;
        return false;
    }

    return true;
}

/* static */
void wxGLCanvas::ConvertWXAttrsToWebGL(const wxGLAttributes& dispAttrs,
                                        EmscriptenWebGLContextAttributes& attrs)
{
    // Set sensible defaults for WebGL
    attrs.alpha = true;
    attrs.depth = true;
    attrs.stencil = false;
    attrs.antialias = true;
    attrs.premultipliedAlpha = true;
    // Keep the drawing buffer after compositing. The 3D viewer raytraces a static
    // frame and then stops redrawing; without this the buffer is cleared once the
    // render settles, so screenshots/read-back (and an idle re-composite) capture an
    // empty canvas even though the frame rendered correctly.
    attrs.preserveDrawingBuffer = true;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_DEFAULT;
    attrs.failIfMajorPerformanceCaveat = false;
    attrs.majorVersion = 2;  // WebGL 2.0 by default
    attrs.minorVersion = 0;
    attrs.enableExtensionsByDefault = true;

    // Parse the attribute list
    const int* attrList = dispAttrs.GetGLAttrs();
    if ( !attrList )
        return;

    for ( int i = 0; attrList[i]; i++ )
    {
        switch ( attrList[i] )
        {
            case WX_GL_RGBA:
                attrs.alpha = true;
                break;

            case WX_GL_DOUBLEBUFFER:
                // WebGL always double buffers
                break;

            case WX_GL_DEPTH_SIZE:
                i++;
                attrs.depth = (attrList[i] > 0);
                break;

            case WX_GL_STENCIL_SIZE:
                i++;
                attrs.stencil = (attrList[i] > 0);
                break;

            case WX_GL_MIN_ALPHA:
                i++;
                attrs.alpha = (attrList[i] > 0);
                break;

            case WX_GL_SAMPLE_BUFFERS:
            case WX_GL_SAMPLES:
                i++;
                attrs.antialias = (attrList[i] > 0);
                break;

            case WX_GL_MAJOR_VERSION:
                i++;
                attrs.majorVersion = attrList[i];
                break;

            case WX_GL_MINOR_VERSION:
                i++;
                attrs.minorVersion = attrList[i];
                break;

            case WX_GL_ES2:
                // Force WebGL 1.0 (ES 2.0)
                attrs.majorVersion = 1;
                break;

            default:
                // Skip unknown attributes and their values
                if ( attrList[i] != WX_GL_RGBA &&
                     attrList[i] != WX_GL_DOUBLEBUFFER )
                {
                    // Most attributes have a value following them
                    i++;
                }
                break;
        }
    }
}

bool wxGLCanvas::SwapBuffers()
{
    // WebGL automatically swaps buffers at the end of each frame
    // when using requestAnimationFrame or when the JavaScript event
    // loop yields. This is effectively a no-op.
    return m_webglContext > 0;
}

/* static */
bool wxGLCanvas::IsDisplaySupported(const wxGLAttributes& dispAttrs)
{
    wxUnusedVar(dispAttrs);
    // WebGL is always available in Emscripten (it's the only option)
    return true;
}

/* static */
bool wxGLCanvas::IsDisplaySupported(const int *attribList)
{
    wxUnusedVar(attribList);
    return true;
}

// ============================================================================
// wxGLApp implementation
// Note: wxIMPLEMENT_CLASS(wxGLApp, wxApp) is in glcmn.cpp (common code)
// ============================================================================

bool wxGLApp::InitGLVisual(const int *attribList)
{
    wxUnusedVar(attribList);
    // WebGL is always available
    return true;
}

#endif // wxUSE_GLCANVAS
