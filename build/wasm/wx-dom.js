// wx-dom.js — shim additions for the WASM DOM port (non-universal build).
//
// Loaded as a second --pre-js AFTER wx.js, only in DOM-port bundles.
// wx.js keeps owning window management, the element registry, clipboard,
// file dialogs and the Canvas2D drawing used by owner-drawn widgets
// (canvas islands). This file owns native-control DOM elements: creation
// (type-based factory, composites where a wx control maps to more than one
// element), property sync, clone-based intrinsic measurement, and event
// wiring back into WASM via wx_dom_event (src/wasm/domevents.cpp).
//
// All functions are no-ops/guarded in Web Workers (-pthread evaluates
// pre-js there too) — see the wx.js worker guard precedent.

(function () {
  if (typeof window === 'undefined' || typeof document === 'undefined') {
    return;
  }

  var nextControlId = 1;
  var controls = new Map(); // domId -> root HTMLElement
  var inputs = new Map();   // domId -> value-bearing element (if != root)
  var labels = new Map();   // domId -> label text target (if != root)

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

  function isEditable(el) {
    return el && (el.tagName === 'TEXTAREA' ||
                  (el.tagName === 'INPUT' &&
                   ['text', 'password', 'number', 'search'].indexOf(el.type) >= 0));
  }

  function flexCenter(el) {
    el.dataset.wxDisplay = 'flex';
    el.style.display = 'flex';
    el.style.alignItems = 'center';
  }

  // Builds the element structure for a logical wx control type. Plain HTML
  // tags ("span", "button", "textarea", "input"+typeAttr) pass through for
  // the simple controls; composite types build wrappers.
  // Returns { root, input, label } (input/label optional).
  function buildControl(type, typeAttr) {
    var root, input, label;
    switch (type) {
      case 'checkbox':
      case 'radio': {
        root = document.createElement('label');
        input = document.createElement('input');
        input.type = type;
        input.style.margin = '0 3px 0 0';
        label = document.createElement('span');
        label.className = 'wx-label';
        root.appendChild(input);
        root.appendChild(label);
        flexCenter(root);
        break;
      }
      case 'toggle': {
        root = document.createElement('button');
        root.setAttribute('aria-pressed', 'false');
        flexCenter(root);
        root.style.justifyContent = 'center';
        root.style.padding = '1px 9px';
        break;
      }
      case 'statbox': {
        // Visual chrome only: the wx children of a wxStaticBox are SIBLING
        // DOM controls, so the fieldset must never intercept their input.
        root = document.createElement('fieldset');
        root.style.border = '1px solid #b5b2aa';
        root.style.borderRadius = '2px';
        label = document.createElement('legend');
        label.className = 'wx-label';
        label.style.padding = '0 3px';
        root.appendChild(label);
        root.dataset.wxChrome = '1';
        break;
      }
      case 'statline': {
        root = document.createElement('div');
        root.style.background = '#909090';
        root.dataset.wxChrome = '1';
        break;
      }
      case 'gauge': {
        root = document.createElement('progress');
        root.max = 100;
        root.value = 0;
        break;
      }
      case 'slider': {
        root = document.createElement('input');
        root.type = 'range';
        break;
      }
      case 'choice': {
        root = document.createElement('select');
        break;
      }
      case 'listbox': {
        root = document.createElement('select');
        root.multiple = true;
        break;
      }
      default: {
        root = document.createElement(type);
        if (typeAttr) root.setAttribute('type', typeAttr);
        if (type === 'span' || type === 'button' || type === 'label') {
          flexCenter(root);
        }
        if (type === 'button') {
          root.style.padding = '1px 9px';
          root.style.justifyContent = 'center';
        }
        break;
      }
    }
    return { root: root, input: input, label: label };
  }

  window.wxDomCreateControl = function (tlwCssId, type, typeAttr) {
    var container = window.__wxGetWindowElement(tlwCssId);
    if (!container) {
      console.error('wxDomCreateControl: no window element for css id ' + tlwCssId);
      return 0;
    }

    var built = buildControl(type, typeAttr);
    var el = built.root;

    var domId = nextControlId++;
    el.dataset.wxDomId = String(domId);
    el.classList.add('wx-dom-control');
    el.style.position = 'absolute';
    el.style.left = '0px';
    el.style.top = '0px';
    el.style.boxSizing = 'border-box';
    el.style.margin = '0';
    if (!el.style.padding) el.style.padding = '0';
    el.style.overflow = type === 'statbox' ? 'visible' : 'hidden';
    if (el.dataset.wxDisplay === undefined) el.dataset.wxDisplay = '';
    // wx labels never soft-wrap (the wx sizer sizes them exactly), but
    // multiline wxStaticText labels DO contain hard \n breaks — 'pre'
    // preserves those while preventing wrapping. <textarea> keeps native
    // wrapping.
    if (type !== 'textarea') {
      el.style.whiteSpace = 'pre';
    }
    // The TLW container has pointer-events:none so input funnels to the
    // canvas; real controls take their own events — except pure chrome
    // (statbox/statline), which must not block canvas hit-testing or
    // sibling controls.
    el.style.pointerEvents = el.dataset.wxChrome ? 'none' : 'auto';

    var valueEl = built.input || el;
    if (built.input) inputs.set(domId, built.input);
    if (built.label) labels.set(domId, built.label);

    if (!el.dataset.wxChrome) {
      el.addEventListener('click', function (ev) {
        dispatch(domId, EVT.CLICK);
        ev.stopPropagation();
      });
      el.addEventListener('focusin', function () {
        if (isEditable(valueEl)) {
          window.wxDomEditableFocused = 1;
        }
        dispatch(domId, EVT.FOCUSIN);
      });
      el.addEventListener('focusout', function () {
        if (isEditable(valueEl)) {
          window.wxDomEditableFocused = 0;
        }
        dispatch(domId, EVT.FOCUSOUT);
      });

      valueEl.addEventListener('input', function () {
        dispatch(domId, EVT.INPUT);
      });
      valueEl.addEventListener('change', function () {
        dispatch(domId, EVT.CHANGE);
      });
      if (isEditable(valueEl)) {
        valueEl.addEventListener('keydown', function (ev) {
          if (ev.key === 'Enter' && valueEl.tagName === 'INPUT') {
            dispatch(domId, EVT.ENTER);
          }
          // Typing belongs to the input; don't let the window-level
          // Emscripten keyboard handler see it (belt — the C++ callback
          // also checks wxDomEditableFocused as suspenders).
          ev.stopPropagation();
        });
        valueEl.addEventListener('keyup', function (ev) {
          ev.stopPropagation();
        });
      }
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
      inputs.delete(domId);
      labels.delete(domId);
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

  // Label text: routed to the inner label element for composites
  // (checkbox/radio span, statbox legend), the element itself otherwise.
  window.wxDomSetText = function (domId, text) {
    var target = labels.get(domId) || controls.get(domId);
    if (target) target.textContent = text;
  };

  window.wxDomSetValue = function (domId, value) {
    var el = inputs.get(domId) || controls.get(domId);
    if (el) el.value = value;
  };

  window.wxDomGetValue = function (domId) {
    var el = inputs.get(domId) || controls.get(domId);
    return el ? String(el.value) : '';
  };

  // Boolean state: checkbox/radio checked, toggle button pressed.
  window.wxDomSetBoolValue = function (domId, on) {
    var el = inputs.get(domId) || controls.get(domId);
    if (!el) return;
    if (el.tagName === 'INPUT') {
      el.checked = !!on;
    } else {
      el.setAttribute('aria-pressed', on ? 'true' : 'false');
      el.style.background = on ? '#b0c4de' : '';
    }
  };

  window.wxDomGetBoolValue = function (domId) {
    var el = inputs.get(domId) || controls.get(domId);
    if (!el) return 0;
    if (el.tagName === 'INPUT') return el.checked ? 1 : 0;
    return el.getAttribute('aria-pressed') === 'true' ? 1 : 0;
  };

  // Numeric state: gauge/slider value.
  window.wxDomSetIntValue = function (domId, value) {
    var el = inputs.get(domId) || controls.get(domId);
    if (el) el.value = value;
  };

  window.wxDomGetIntValue = function (domId) {
    var el = inputs.get(domId) || controls.get(domId);
    if (!el) return 0;
    var v = parseInt(el.value, 10);
    return isNaN(v) ? 0 : v;
  };

  window.wxDomSetRange = function (domId, minVal, maxVal) {
    var el = inputs.get(domId) || controls.get(domId);
    if (!el) return;
    if (el.tagName === 'PROGRESS') {
      el.max = maxVal;
    } else {
      el.min = minVal;
      el.max = maxVal;
    }
  };

  // HTML radio exclusivity groups via the name attribute (wx groups are
  // defined by wxRB_GROUP chains; C++ passes a stable per-group name).
  window.wxDomSetGroupName = function (domId, name) {
    var el = inputs.get(domId);
    if (el) el.name = name;
  };

  window.wxDomSetEnabled = function (domId, enabled) {
    var root = controls.get(domId);
    var el = inputs.get(domId) || root;
    if (!el) return;
    if ('disabled' in el) el.disabled = !enabled;
    if (root) root.style.opacity = enabled ? '' : '0.5';
  };

  window.wxDomSetReadOnly = function (domId, readOnly) {
    var el = inputs.get(domId) || controls.get(domId);
    if (el) el.readOnly = !!readOnly;
  };

  window.wxDomSetShown = function (domId, shown) {
    var el = controls.get(domId);
    if (el) el.style.display = shown ? (el.dataset.wxDisplay || '') : 'none';
  };

  window.wxDomFocus = function (domId) {
    var el = inputs.get(domId) || controls.get(domId);
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
