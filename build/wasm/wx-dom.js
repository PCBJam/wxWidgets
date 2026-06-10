// wx-dom.js — shim additions for the WASM DOM port (non-universal build).
//
// Loaded as a second --pre-js AFTER wx.js, only in DOM-port bundles.
// wx.js keeps owning window management, the element registry, clipboard,
// file dialogs and the Canvas2D drawing used by owner-drawn widgets
// (canvas islands). This file owns native-control DOM elements: creation,
// property sync, intrinsic measurement, and event wiring back into WASM
// via wx_dom_event (src/wasm/domevents.cpp).
//
// All functions are no-ops/guarded in Web Workers (-pthread evaluates
// pre-js there too) — see the wx.js worker guard precedent.

(function () {
  if (typeof window === 'undefined' || typeof document === 'undefined') {
    return;
  }

  var nextControlId = 1;
  var controls = new Map(); // domId -> HTMLElement

  // Flag read by the C++ keyboard callback (src/wasm/app.cpp): while a DOM
  // editable owns browser focus, wx must not swallow/preventDefault keys.
  window.wxDomEditableFocused = 0;

  // Mirror of wxDomEventKind in include/wx/wasm/window.h.
  var EVT = { CLICK: 1, INPUT: 2, CHANGE: 3, FOCUSIN: 4, FOCUSOUT: 5, ENTER: 6 };

  // Marks the bundle as DOM-port for tests/boot code.
  window.wxDomPort = true;
  window.wxDomControls = controls;

  function dispatch(domId, kind) {
    try {
      Module['ccall']('wx_dom_event', null, ['number', 'number'], [domId, kind]);
    } catch (e) {
      // Surfaces in test logs; must never throw back into DOM event handlers.
      console.error('wx_dom_event(' + domId + ',' + kind + ') failed:', e);
    }
  }

  function isEditableTag(el) {
    return el.tagName === 'INPUT' || el.tagName === 'TEXTAREA';
  }

  window.wxDomCreateControl = function (tlwCssId, tag, typeAttr) {
    var container = window.__wxGetWindowElement(tlwCssId);
    if (!container) {
      console.error('wxDomCreateControl: no window element for css id ' + tlwCssId);
      return 0;
    }

    var el = document.createElement(tag);
    if (typeAttr) {
      el.setAttribute('type', typeAttr);
    }

    var domId = nextControlId++;
    el.dataset.wxDomId = String(domId);
    el.className = 'wx-dom-control';
    el.style.position = 'absolute';
    el.style.left = '0px';
    el.style.top = '0px';
    el.style.boxSizing = 'border-box';
    el.style.margin = '0';
    el.style.padding = '0';
    el.style.overflow = 'hidden';
    // wx labels never soft-wrap (the wx sizer sizes them exactly), but
    // multiline wxStaticText labels DO contain hard \n breaks — 'pre'
    // preserves those while preventing wrapping. <textarea> (multiline
    // editable) keeps its native wrapping behavior.
    if (tag !== 'textarea') {
      el.style.whiteSpace = 'pre';
    }
    // Vertically center label text like the native/univ controls do.
    // The preferred display is remembered so wxDomSetShown can restore it
    // (display:'' would lose the flex).
    if (tag === 'span' || tag === 'button' || tag === 'label') {
      el.dataset.wxDisplay = 'flex';
      el.style.display = 'flex';
      el.style.alignItems = 'center';
    } else {
      el.dataset.wxDisplay = '';
    }
    if (tag === 'button') {
      // approximate the univ button label margins (padding was zeroed above)
      el.style.padding = '1px 9px';
      el.style.justifyContent = 'center';
    }
    // The TLW container has pointer-events:none so input funnels to the
    // canvas; real controls take their own events.
    el.style.pointerEvents = 'auto';

    el.addEventListener('click', function (ev) {
      dispatch(domId, EVT.CLICK);
      ev.stopPropagation();
    });
    el.addEventListener('focusin', function () {
      if (isEditableTag(el)) {
        window.wxDomEditableFocused = 1;
      }
      dispatch(domId, EVT.FOCUSIN);
    });
    el.addEventListener('focusout', function () {
      if (isEditableTag(el)) {
        window.wxDomEditableFocused = 0;
      }
      dispatch(domId, EVT.FOCUSOUT);
    });

    if (isEditableTag(el)) {
      el.addEventListener('input', function () {
        dispatch(domId, EVT.INPUT);
      });
      el.addEventListener('change', function () {
        dispatch(domId, EVT.CHANGE);
      });
      el.addEventListener('keydown', function (ev) {
        if (ev.key === 'Enter' && el.tagName === 'INPUT') {
          dispatch(domId, EVT.ENTER);
        }
        // Typing belongs to the input; don't let the window-level
        // Emscripten keyboard handler see it (belt — the C++ callback
        // also checks wxDomEditableFocused as suspenders).
        ev.stopPropagation();
      });
      el.addEventListener('keyup', function (ev) {
        ev.stopPropagation();
      });
    }

    controls.set(domId, el);
    container.appendChild(el);
    return domId;
  };

  window.wxDomDestroyControl = function (domId) {
    var el = controls.get(domId);
    if (el) {
      el.remove();
      controls.delete(domId);
    }
  };

  window.wxDomSetRect = function (domId, x, y, w, h) {
    var el = controls.get(domId);
    if (!el) return;
    el.style.left = x + 'px';
    el.style.top = y + 'px';
    el.style.width = w + 'px';
    el.style.height = h + 'px';
  };

  window.wxDomSetText = function (domId, text) {
    var el = controls.get(domId);
    if (el) el.textContent = text;
  };

  window.wxDomSetValue = function (domId, value) {
    var el = controls.get(domId);
    if (el) el.value = value;
  };

  window.wxDomGetValue = function (domId) {
    var el = controls.get(domId);
    return el ? String(el.value) : '';
  };

  window.wxDomSetEnabled = function (domId, enabled) {
    var el = controls.get(domId);
    if (el) el.disabled = !enabled;
  };

  window.wxDomSetReadOnly = function (domId, readOnly) {
    var el = controls.get(domId);
    if (el) el.readOnly = !!readOnly;
  };

  window.wxDomSetShown = function (domId, shown) {
    var el = controls.get(domId);
    if (el) el.style.display = shown ? (el.dataset.wxDisplay || '') : 'none';
  };

  window.wxDomFocus = function (domId) {
    var el = controls.get(domId);
    if (el && document.activeElement !== el) el.focus();
  };

  window.wxDomSetFont = function (domId, cssFont) {
    var el = controls.get(domId);
    if (el && cssFont) el.style.font = cssFont;
  };

  window.wxDomSetAriaLabel = function (domId, label) {
    var el = controls.get(domId);
    if (el) el.setAttribute('aria-label', label);
  };

  // Intrinsic (content-driven) size, packed (w << 16) | h for EM_ASM_INT.
  // Measured on a CLONE inside an always-rendered offscreen host: sizers run
  // DoGetBestSize before the frame is shown, when the element (or any
  // ancestor TLW div) is display:none and would measure 0x0.
  var measureHost = null;
  window.wxDomIntrinsicSize = function (domId) {
    var el = controls.get(domId);
    if (!el) return 0;

    if (!measureHost) {
      measureHost = document.createElement('div');
      measureHost.style.cssText =
        'position:absolute;left:-100000px;top:0;visibility:hidden;';
      document.body.appendChild(measureHost);
    }

    var clone = el.cloneNode(true); // copies inline styles incl. font
    clone.style.display = el.dataset.wxDisplay || 'block';
    clone.style.position = 'static';
    clone.style.width = 'auto';
    clone.style.height = 'auto';
    // shrink-to-fit so the width reflects the content, not the host
    clone.style.inlineSize = 'fit-content';
    measureHost.appendChild(clone);
    var rect = clone.getBoundingClientRect();
    clone.remove();

    var w = Math.min(0xffff, Math.max(1, Math.ceil(rect.width)));
    var h = Math.min(0xffff, Math.max(1, Math.ceil(rect.height)));
    return (w << 16) | h;
  };
})();
