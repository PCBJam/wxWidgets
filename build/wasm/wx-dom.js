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
  var EVT = { CLICK: 1, INPUT: 2, CHANGE: 3, FOCUSIN: 4, FOCUSOUT: 5,
              ENTER: 6, SPIN_UP: 7, SPIN_DOWN: 8, MENU: 9, TOOL: 10 };

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
      case 'spinbutton': {
        // vertical up/down button pair; clicks dispatch SPIN_UP/SPIN_DOWN
        root = document.createElement('div');
        root.dataset.wxSpin = '1';
        var mk = function (txt, cls) {
          var b = document.createElement('button');
          b.textContent = txt;
          b.className = cls;
          b.style.cssText = 'flex:1;padding:0;margin:0;font-size:7px;' +
                            'line-height:1;min-height:0;overflow:hidden;';
          root.appendChild(b);
          return b;
        };
        root.style.display = 'flex';
        root.dataset.wxDisplay = 'flex';
        root.style.flexDirection = 'column';
        mk('▲', 'wx-spin-up');
        mk('▼', 'wx-spin-down');
        break;
      }
      case 'radiobox': {
        // owns its item rows (unlike statbox chrome): fieldset + legend +
        // one <label><input type=radio><span></span></label> per item,
        // filled by wxDomSetItems.
        root = document.createElement('fieldset');
        root.style.border = '1px solid #b5b2aa';
        root.style.borderRadius = '2px';
        label = document.createElement('legend');
        label.className = 'wx-label';
        label.style.padding = '0 3px';
        root.appendChild(label);
        root.dataset.wxRadioBox = '1';
        break;
      }
      case 'image': {
        root = document.createElement('img');
        root.dataset.wxChrome = '1'; // non-interactive like statbmp
        break;
      }
      case 'combobox': {
        // Editable combo: text input + datalist autocomplete (HTML has no
        // native editable select). wxDomSetItems fills the datalist.
        root = document.createElement('input');
        root.type = 'text';
        var dl = document.createElement('datalist');
        dl.id = 'wx-datalist-' + nextControlId; // == the domId assigned below
        root.setAttribute('list', dl.id);
        document.body.appendChild(dl);
        root.dataset.wxDatalist = dl.id;
        break;
      }
      case 'checklistbox': {
        // Scrollable list of checkbox rows; row checkbox toggles dispatch
        // CHANGE with the row index retrievable via wxDomGetIntValue.
        root = document.createElement('div');
        root.dataset.wxCheckList = '1';
        root.style.overflowY = 'auto';
        root.style.background = '#ffffff';
        root.style.border = '1px solid #b5b2aa';
        break;
      }
      case 'menubar': {
        // Horizontal strip of menu-title buttons; menus open as popup divs
        // (built by wxDomMenuSetStructure).
        root = document.createElement('div');
        root.dataset.wxMenuBar = '1';
        root.style.background = '#d4d0c8';
        flexCenter(root);
        break;
      }
      case 'toolbar': {
        // Horizontal strip of tool buttons (built by wxDomToolbarSetTools).
        root = document.createElement('div');
        root.dataset.wxToolBar = '1';
        root.style.background = '#d4d0c8';
        flexCenter(root);
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

    if (el.dataset.wxSpin) {
      el.querySelector('.wx-spin-up').addEventListener('click', function (ev) {
        dispatch(domId, EVT.SPIN_UP);
        ev.stopPropagation();
      });
      el.querySelector('.wx-spin-down').addEventListener('click', function (ev) {
        dispatch(domId, EVT.SPIN_DOWN);
        ev.stopPropagation();
      });
    } else if (!el.dataset.wxChrome) {
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
      if (el.dataset.wxDatalist) {
        var dl = document.getElementById(el.dataset.wxDatalist);
        if (dl) dl.remove();
      }
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

  // Numeric state: gauge/slider value, select/radiobox/combobox selection.
  window.wxDomSetIntValue = function (domId, value) {
    var el = inputs.get(domId) || controls.get(domId);
    if (!el) return;
    if (el.tagName === 'SELECT') {
      el.selectedIndex = value;
    } else if (el.dataset && el.dataset.wxRadioBox) {
      var radios = el.querySelectorAll('input[type=radio]');
      if (radios[value]) radios[value].checked = true;
    } else if (el.dataset && el.dataset.wxDatalist) {
      // combobox selection = the nth datalist option's text
      var dl = document.getElementById(el.dataset.wxDatalist);
      var opt = dl && dl.options[value];
      if (opt) el.value = opt.value;
    } else {
      el.value = value;
    }
  };

  window.wxDomGetIntValue = function (domId) {
    var el = inputs.get(domId) || controls.get(domId);
    if (!el) return 0;
    if (el.tagName === 'SELECT') return el.selectedIndex;
    if (el.dataset && el.dataset.wxRadioBox) {
      var radios = el.querySelectorAll('input[type=radio]');
      for (var i = 0; i < radios.length; i++) {
        if (radios[i].checked) return i;
      }
      return -1;
    }
    if (el.dataset && el.dataset.wxCheckList) {
      // index of the row whose checkbox last toggled (for wxEVT_CHECKLISTBOX)
      var t = parseInt(el.dataset.wxLastToggled, 10);
      return isNaN(t) ? -1 : t;
    }
    if (el.dataset && el.dataset.wxDatalist) {
      // combobox selection = index of the option matching the current text
      var dl = document.getElementById(el.dataset.wxDatalist);
      if (dl) {
        for (var j = 0; j < dl.options.length; j++) {
          if (dl.options[j].value === el.value) return j;
        }
      }
      return -1;
    }
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

  // Item lists for select-likes and radiobox; items arrive \x1f-joined
  // (the unit separator can't occur in wx labels).
  window.wxDomSetItems = function (domId, joined) {
    var el = controls.get(domId);
    if (!el) return;
    var items = joined === '' ? [] : joined.split('\x1f');
    if (el.dataset.wxDatalist) {
      var dl = document.getElementById(el.dataset.wxDatalist);
      if (dl) {
        dl.textContent = '';
        items.forEach(function (it) {
          var o = document.createElement('option');
          o.value = it;
          dl.appendChild(o);
        });
      }
    } else if (el.dataset.wxCheckList) {
      el.textContent = '';
      items.forEach(function (it, idx) {
        var row = document.createElement('label');
        row.style.cssText =
          'display:flex;align-items:center;padding:0 2px;white-space:pre;';
        var inp = document.createElement('input');
        inp.type = 'checkbox';
        inp.style.margin = '0 3px 0 0';
        inp.addEventListener('change', function () {
          el.dataset.wxLastToggled = String(idx);
          dispatch(domId, EVT.CHANGE);
        });
        var sp = document.createElement('span');
        sp.textContent = it;
        row.appendChild(inp);
        row.appendChild(sp);
        el.appendChild(row);
      });
    } else if (el.tagName === 'SELECT') {
      el.textContent = '';
      items.forEach(function (it) {
        var o = document.createElement('option');
        o.textContent = it;
        el.appendChild(o);
      });
    } else if (el.dataset.wxRadioBox) {
      Array.prototype.forEach.call(el.querySelectorAll('label'), function (r) {
        r.remove();
      });
      items.forEach(function (it) {
        var row = document.createElement('label');
        row.style.cssText =
          'display:flex;align-items:center;margin:1px 4px;white-space:pre;';
        var inp = document.createElement('input');
        inp.type = 'radio';
        inp.name = 'wxradiobox-' + domId;
        inp.style.margin = '0 3px 0 0';
        inp.addEventListener('change', function () {
          dispatch(domId, EVT.CHANGE);
        });
        var sp = document.createElement('span');
        sp.textContent = it;
        row.appendChild(inp);
        row.appendChild(sp);
        el.appendChild(row);
      });
    }
  };

  // Multi-selection (listbox) / per-item checked state (checklistbox):
  // per-index boolean; "selected" indices returned comma-joined.
  window.wxDomSetItemSelected = function (domId, index, on) {
    var el = controls.get(domId);
    if (!el) return;
    if (el.dataset.wxCheckList) {
      var boxes = el.querySelectorAll('input[type=checkbox]');
      if (boxes[index]) boxes[index].checked = !!on;
    } else if (el.tagName === 'SELECT' && el.options[index]) {
      el.options[index].selected = !!on;
    }
  };

  window.wxDomGetSelectedIndices = function (domId) {
    var el = controls.get(domId);
    if (!el) return '';
    var out = [];
    var i;
    if (el.dataset.wxCheckList) {
      var boxes = el.querySelectorAll('input[type=checkbox]');
      for (i = 0; i < boxes.length; i++) {
        if (boxes[i].checked) out.push(i);
      }
    } else if (el.tagName === 'SELECT') {
      for (i = 0; i < el.options.length; i++) {
        if (el.options[i].selected) out.push(i);
      }
    }
    return out.join(',');
  };

  // Bitmap content as a PNG data URL: <img> roots directly; buttons get a
  // leading <img> child (note: wxDomSetText replaces children — C++ must
  // set the image after the label). Explicit w/h from the wx bitmap size:
  // images load asynchronously, so without them the measurement clone (and
  // hence DoGetBestSize) would see a 0x0 image.
  window.wxDomSetImage = function (domId, dataUrl, w, h) {
    var el = controls.get(domId);
    if (!el) return;
    var img;
    if (el.tagName === 'IMG') {
      img = el;
    } else {
      img = el.querySelector('img.wx-btn-img');
      if (!img) {
        img = document.createElement('img');
        img.className = 'wx-btn-img';
        // keep the bitmap from being squashed inside the flex button
        img.style.flexShrink = '0';
        el.insertBefore(img, el.firstChild);
      }
    }
    if (w > 0) { img.width = w; img.style.width = w + 'px'; }
    if (h > 0) { img.height = h; img.style.height = h + 'px'; }
    img.src = dataUrl;
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

  window.wxDomSetTooltip = function (domId, tip) {
    var el = controls.get(domId);
    if (el) el.title = tip;
  };

  // Insets in CSS px relative to the element's own box; all <= 0 clears.
  // clip-path does not affect layout and clips hit-testing too, so rows
  // scrolled out of a pane neither paint nor catch clicks.
  window.wxDomSetClip = function (domId, top, right, bottom, left) {
    var el = controls.get(domId);
    if (!el) return;
    if (top <= 0 && right <= 0 && bottom <= 0 && left <= 0) {
      if (el.style.clipPath) el.style.clipPath = '';
    } else {
      el.style.clipPath = 'inset(' + Math.max(0, top) + 'px ' +
                          Math.max(0, right) + 'px ' +
                          Math.max(0, bottom) + 'px ' +
                          Math.max(0, left) + 'px)';
    }
  };

  // ========== Menus & toolbars ==========

  var openMenuPopup = null;

  function closeMenuPopup() {
    if (openMenuPopup) {
      openMenuPopup.remove();
      openMenuPopup = null;
    }
  }

  document.addEventListener('mousedown', function (ev) {
    // any click outside an open menu closes it (mousedown so the click on
    // another control still lands)
    if (openMenuPopup && !openMenuPopup.contains(ev.target)) {
      var inTitle = ev.target.closest && ev.target.closest('.wx-menu-title');
      if (!inTitle) closeMenuPopup();
    }
  });

  function registryRegister(id, info) {
    var reg = window.wxElementRegistry;
    if (reg && reg.registerRendered) reg.registerRendered(id, info);
  }

  function rectInfo(el) {
    var r = el.getBoundingClientRect();
    return { x: r.x, y: r.y, width: r.width, height: r.height,
             centerX: r.x + r.width / 2, centerY: r.y + r.height / 2 };
  }

  // Builds and shows the popup for one menu's items below `anchor`.
  // items: [{id,label,kind:'normal'|'separator'|'check'|'radio'|'submenu',
  //          checked,enabled,items}]
  function showMenuPopup(domId, anchor, items, registryParent) {
    closeMenuPopup();
    var pop = document.createElement('div');
    pop.className = 'wx-menu-popup';
    var a = anchor.getBoundingClientRect();
    pop.style.cssText =
      'position:absolute;z-index:10000;background:#d4d0c8;' +
      'border:1px solid #808080;box-shadow:2px 2px 4px rgba(0,0,0,.3);' +
      'padding:2px;white-space:pre;min-width:120px;' +
      'left:' + (a.left + window.scrollX) + 'px;' +
      'top:' + (a.bottom + window.scrollY) + 'px;';
    pop.style.font = anchor.style.font || getComputedStyle(anchor).font;

    items.forEach(function (it, idx) {
      if (it.kind === 'separator') {
        var sep = document.createElement('div');
        sep.style.cssText = 'border-top:1px solid #808080;margin:2px 4px;';
        pop.appendChild(sep);
        return;
      }
      var row = document.createElement('div');
      row.textContent = (it.checked ? '✓ ' : '   ') + it.label +
                        (it.kind === 'submenu' ? '  ▸' : '');
      row.style.cssText = 'padding:2px 14px 2px 6px;cursor:default;' +
                          (it.enabled ? '' : 'color:#808080;');
      if (it.enabled) {
        row.addEventListener('mouseenter', function () {
          row.style.background = '#000080';
          row.style.color = '#ffffff';
        });
        row.addEventListener('mouseleave', function () {
          row.style.background = '';
          row.style.color = '';
        });
        row.addEventListener('click', function (ev) {
          ev.stopPropagation();
          if (it.kind === 'submenu') {
            // simple inline expansion: replace popup with the submenu
            showMenuPopup(domId, row, it.items || [], registryParent);
            return;
          }
          var bar = controls.get(domId);
          if (bar) bar.dataset.wxLastCommand = String(it.id);
          closeMenuPopup();
          dispatch(domId, EVT.MENU);
        });
      }
      pop.appendChild(row);
      // register popup items for the e2e registry (canvas parity)
      requestAnimationFrame(function () {
        if (!pop.isConnected) return;
        registryRegister(registryParent + ':menuitem:' + idx, Object.assign({
          elementType: 'menuitem',
          subType: it.kind === 'check' || it.kind === 'radio' ? it.kind : 'normal',
          label: it.label, tooltip: '', enabled: !!it.enabled,
          parentId: registryParent, index: idx
        }, rectInfo(row)));
      });
    });

    document.body.appendChild(pop);
    openMenuPopup = pop;
  }

  // structureJson: [{title, items:[...]}, ...] (schema above)
  window.wxDomMenuSetStructure = function (domId, structureJson) {
    var el = controls.get(domId);
    if (!el || !el.dataset.wxMenuBar) return;
    var menus;
    try {
      menus = JSON.parse(structureJson);
    } catch (e) {
      console.error('wxDomMenuSetStructure: bad JSON: ' + e.message);
      return;
    }
    el.textContent = '';
    closeMenuPopup();
    menus.forEach(function (m, idx) {
      var btn = document.createElement('button');
      btn.className = 'wx-menu-title';
      btn.textContent = m.title;
      btn.style.cssText =
        'border:none;background:transparent;padding:2px 8px;margin:0;' +
        'font:inherit;white-space:pre;';
      btn.addEventListener('mousedown', function (ev) { ev.stopPropagation(); });
      btn.addEventListener('click', function (ev) {
        ev.stopPropagation();
        if (openMenuPopup) {
          closeMenuPopup();
        } else {
          showMenuPopup(domId, btn, m.items || [], domId + ':' + idx);
        }
      });
      el.appendChild(btn);
      requestAnimationFrame(function () {
        registryRegister(domId + ':menubartitle:' + idx, Object.assign({
          elementType: 'menuitem', subType: 'menubar',
          label: m.title, tooltip: '', enabled: true,
          parentId: String(domId), index: idx
        }, rectInfo(btn)));
      });
    });
  };

  // tools: [{id,label,tooltip,kind:'button'|'toggle'|'separator',
  //          toggled,enabled,img,imgW,imgH}]
  window.wxDomToolbarSetTools = function (domId, toolsJson) {
    var el = controls.get(domId);
    if (!el || !el.dataset.wxToolBar) return;
    var tools;
    try {
      tools = JSON.parse(toolsJson);
    } catch (e) {
      console.error('wxDomToolbarSetTools: bad JSON', e);
      return;
    }
    el.textContent = '';
    tools.forEach(function (t, idx) {
      if (t.kind === 'separator') {
        var sep = document.createElement('div');
        sep.style.cssText =
          'border-left:1px solid #808080;align-self:stretch;margin:1px 3px;';
        el.appendChild(sep);
        return;
      }
      var btn = document.createElement('button');
      btn.className = 'wx-tool';
      btn.title = t.tooltip || t.label || '';
      btn.style.cssText = 'padding:1px 3px;margin:1px;font:inherit;' +
                          'display:flex;align-items:center;';
      if (t.img) {
        var img = document.createElement('img');
        if (t.imgW > 0) { img.width = t.imgW; img.style.width = t.imgW + 'px'; }
        if (t.imgH > 0) { img.height = t.imgH; img.style.height = t.imgH + 'px'; }
        img.style.flexShrink = '0';
        img.src = t.img;
        btn.appendChild(img);
      } else {
        btn.textContent = t.label || '';
      }
      btn.disabled = !t.enabled;
      if (t.toggled) btn.style.background = '#b0c4de';
      btn.addEventListener('click', function (ev) {
        ev.stopPropagation();
        el.dataset.wxLastCommand = String(t.id);
        dispatch(domId, EVT.TOOL);
      });
      el.appendChild(btn);
      requestAnimationFrame(function () {
        if (!btn.isConnected) return;
        registryRegister(domId + ':tool:' + idx, Object.assign({
          elementType: 'tool', subType: t.kind === 'toggle' ? 'toggle' : 'button',
          label: t.label || '', tooltip: t.tooltip || '', enabled: !!t.enabled,
          parentId: String(domId), index: idx, toggled: !!t.toggled
        }, rectInfo(btn)));
      });
    });
  };

  // Command id of the last activated menu item / tool (set by the click
  // handlers above; read by wxMenuBar/wxToolBar OnDomEvent).
  window.wxDomGetLastCommandId = function (domId) {
    var el = controls.get(domId);
    var v = el ? parseInt(el.dataset.wxLastCommand, 10) : NaN;
    return isNaN(v) ? -1 : v;
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
