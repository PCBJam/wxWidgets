// Element Registry for E2E Tests
// Tracks all wxWindow instances with their positions for automated testing
(function() {
  if (typeof window !== 'undefined' && typeof window.wxElementRegistry === 'undefined') {
    window.wxElementRegistry = {
      elements: new Map(),
      version: 0,

      register: function(id, info) {
        this.elements.set(id, info);
        this.version++;
      },

      update: function(id, updates) {
        var elem = this.elements.get(id);
        if (elem) {
          Object.assign(elem, updates);
          elem.lastUpdated = Date.now();
          this.version++;
        }
      },

      unregister: function(id) {
        this.elements.delete(id);
        this.version++;
      },

      findByLabel: function(label, options) {
        options = options || {};
        var results = [];
        var exact = options.exact || false;
        var visibleOnly = options.visible !== false;

        this.elements.forEach(function(elem) {
          if (visibleOnly && !elem.visible) return;
          if (options.enabled && !elem.enabled) return;
          if (options.type && elem.typeName !== options.type) return;

          var matches = exact
            ? elem.label === label
            : elem.label.indexOf(label) !== -1;
          if (matches) results.push(elem);
        });

        return results;
      },

      findByName: function(name, options) {
        options = options || {};
        var results = [];
        var exact = options.exact || false;
        var visibleOnly = options.visible !== false;

        this.elements.forEach(function(elem) {
          if (visibleOnly && !elem.visible) return;
          if (options.enabled && !elem.enabled) return;
          if (options.type && elem.typeName !== options.type) return;

          var matches = exact
            ? elem.name === name
            : elem.name.indexOf(name) !== -1;
          if (matches) results.push(elem);
        });

        return results;
      },

      findByType: function(typeName, options) {
        options = options || {};
        var results = [];
        var visibleOnly = options.visible !== false;

        this.elements.forEach(function(elem) {
          if (visibleOnly && !elem.visible) return;
          if (options.enabled && !elem.enabled) return;
          if (elem.typeName === typeName) results.push(elem);
        });

        return results;
      },

      findAll: function(filter) {
        filter = filter || {};
        var results = [];
        var visibleOnly = filter.visible !== false;

        this.elements.forEach(function(elem) {
          if (visibleOnly && !elem.visible) return;
          if (filter.enabled && !elem.enabled) return;
          if (filter.type && elem.typeName !== filter.type) return;
          if (filter.label && elem.label.indexOf(filter.label) === -1) return;
          if (filter.name && elem.name.indexOf(filter.name) === -1) return;
          results.push(elem);
        });

        return results;
      },

      getElement: function(id) {
        return this.elements.get(id) || null;
      },

      dump: function() {
        console.log('[wxElementRegistry] Elements:', this.elements.size);
        this.elements.forEach(function(elem) {
          console.log('  ' + elem.id + ': ' + elem.typeName + ' "' + elem.label + '" at (' + elem.screenX + ',' + elem.screenY + ') ' + elem.width + 'x' + elem.height);
        });
      },

      getStats: function() {
        var stats = { total: 0, byType: {} };
        this.elements.forEach(function(elem) {
          stats.total++;
          stats.byType[elem.typeName] = (stats.byType[elem.typeName] || 0) + 1;
        });
        return stats;
      },

      // ========== Rendered Elements (toolbar tools, menu items, etc.) ==========
      renderedElements: new Map(),
      renderedVersion: 0,

      registerRendered: function(id, info) {
        this.renderedElements.set(id, info);
        this.renderedVersion++;
      },

      updateRendered: function(id, updates) {
        var elem = this.renderedElements.get(id);
        if (elem) {
          Object.assign(elem, updates);
          elem.lastUpdated = Date.now();
          this.renderedVersion++;
        }
      },

      unregisterRendered: function(id) {
        this.renderedElements.delete(id);
        this.renderedVersion++;
      },

      unregisterRenderedByParent: function(parentId) {
        var toDelete = [];
        var self = this;
        this.renderedElements.forEach(function(elem, key) {
          if (elem.parentId === parentId) {
            toDelete.push(key);
          }
        });
        toDelete.forEach(function(key) {
          self.renderedElements.delete(key);
        });
        if (toDelete.length > 0) this.renderedVersion++;
      },

      findRenderedByLabel: function(label, options) {
        options = options || {};
        var results = [];
        var exact = options.exact || false;

        this.renderedElements.forEach(function(elem) {
          if (options.enabled !== undefined && elem.enabled !== options.enabled) return;
          if (options.elementType && elem.elementType !== options.elementType) return;
          if (options.subType && elem.subType !== options.subType) return;
          if (options.parentId && elem.parentId !== options.parentId) return;

          var elemLabel = elem.label || elem.tooltip || '';
          var matches = exact
            ? elemLabel === label
            : elemLabel.indexOf(label) !== -1;
          if (matches) results.push(elem);
        });

        return results;
      },

      findRenderedByType: function(elementType, options) {
        options = options || {};
        var results = [];

        this.renderedElements.forEach(function(elem) {
          if (elem.elementType !== elementType) return;
          if (options.enabled !== undefined && elem.enabled !== options.enabled) return;
          if (options.subType && elem.subType !== options.subType) return;
          if (options.parentId && elem.parentId !== options.parentId) return;
          results.push(elem);
        });

        return results;
      },

      findRenderedByParent: function(parentId, options) {
        options = options || {};
        var results = [];

        this.renderedElements.forEach(function(elem) {
          if (elem.parentId !== parentId) return;
          if (options.enabled !== undefined && elem.enabled !== options.enabled) return;
          if (options.elementType && elem.elementType !== options.elementType) return;
          if (options.subType && elem.subType !== options.subType) return;
          results.push(elem);
        });

        return results;
      },

      findAllRendered: function(filter) {
        filter = filter || {};
        var results = [];

        this.renderedElements.forEach(function(elem) {
          if (filter.enabled !== undefined && elem.enabled !== filter.enabled) return;
          if (filter.elementType && elem.elementType !== filter.elementType) return;
          if (filter.subType && elem.subType !== filter.subType) return;
          if (filter.parentId && elem.parentId !== filter.parentId) return;
          if (filter.label) {
            var elemLabel = elem.label || elem.tooltip || '';
            if (elemLabel.indexOf(filter.label) === -1) return;
          }
          results.push(elem);
        });

        return results;
      },

      dumpRendered: function() {
        console.log('[wxElementRegistry] Rendered Elements:', this.renderedElements.size);
        this.renderedElements.forEach(function(elem) {
          console.log('  ' + elem.id + ': ' + elem.elementType + '/' + elem.subType + ' "' + (elem.label || elem.tooltip || '') + '" at (' + elem.screenX + ',' + elem.screenY + ') ' + elem.width + 'x' + elem.height);
        });
      },

      getRenderedStats: function() {
        var stats = { total: 0, byType: {} };
        this.renderedElements.forEach(function(elem) {
          stats.total++;
          var key = elem.elementType + '/' + elem.subType;
          stats.byType[key] = (stats.byType[key] || 0) + 1;
        });
        return stats;
      }
    };
  }
})();

// Helper functions called from C++ via EM_ASM
function wxElementRegister(id, label, name, typeName, screenX, screenY, width, height, parentId, visible, enabled, domId) {
  if (window.wxElementRegistry) {
    window.wxElementRegistry.register(id, {
      id: id,
      label: label,
      name: name,
      typeName: typeName,
      screenX: screenX,
      screenY: screenY,
      width: width,
      height: height,
      centerX: screenX + Math.floor(width / 2),
      centerY: screenY + Math.floor(height / 2),
      parentId: parentId,
      visible: visible,
      enabled: enabled,
      domId: domId,
      lastUpdated: Date.now()
    });
  }
}

function wxElementUpdate(id, label, name, typeName, screenX, screenY, width, height, parentId, visible, enabled, domId) {
  if (window.wxElementRegistry) {
    var elem = window.wxElementRegistry.elements.get(id);
    if (elem) {
      elem.label = label;
      elem.name = name;
      elem.typeName = typeName;
      elem.screenX = screenX;
      elem.screenY = screenY;
      elem.width = width;
      elem.height = height;
      elem.centerX = screenX + Math.floor(width / 2);
      elem.centerY = screenY + Math.floor(height / 2);
      elem.parentId = parentId;
      elem.visible = visible;
      elem.enabled = enabled;
      elem.domId = domId;
      elem.lastUpdated = Date.now();
      window.wxElementRegistry.version++;
    }
  }
}

function wxElementUnregister(id) {
  if (window.wxElementRegistry) {
    window.wxElementRegistry.unregister(id);
  }
}

// Helper functions for rendered elements (called from C++ via EM_ASM)
function wxRenderedElementRegister(id, parentId, elementType, subType, label, tooltip, screenX, screenY, width, height, enabled, index) {
  if (window.wxElementRegistry) {
    window.wxElementRegistry.registerRendered(id, {
      id: id,
      parentId: parentId,
      elementType: elementType,
      subType: subType,
      label: label,
      tooltip: tooltip,
      screenX: screenX,
      screenY: screenY,
      width: width,
      height: height,
      centerX: screenX + Math.floor(width / 2),
      centerY: screenY + Math.floor(height / 2),
      enabled: enabled,
      index: index,
      lastUpdated: Date.now()
    });
  }
}

function wxRenderedElementUpdate(id, screenX, screenY, width, height, enabled) {
  if (window.wxElementRegistry) {
    window.wxElementRegistry.updateRendered(id, {
      screenX: screenX,
      screenY: screenY,
      width: width,
      height: height,
      centerX: screenX + Math.floor(width / 2),
      centerY: screenY + Math.floor(height / 2),
      enabled: enabled
    });
  }
}

function wxRenderedElementUnregister(id) {
  if (window.wxElementRegistry) {
    window.wxElementRegistry.unregisterRendered(id);
  }
}

function wxRenderedElementUnregisterByParent(parentId) {
  if (window.wxElementRegistry) {
    window.wxElementRegistry.unregisterRenderedByParent(parentId);
  }
}

// Delayed browser callbacks can outlive the Wasm instance they were armed by.
// Once native integrity is unknown, never enter that instance again—not even
// for cleanup. The fallback keeps upstream wx builds without our scheduler
// working; both configurations honor the shared terminal integrity marker.
var wxWasmCanTouchNative = function () {
  if (globalThis.__wxNativeIntegrityUnknown) {
    return false;
  }
  var scheduler = globalThis.__wxScheduler;
  if (!scheduler) {
    return true;
  }
  return typeof scheduler.canTouchNative === 'function'
    ? scheduler.canTouchNative()
    : !scheduler.dead;
};

var wxWasmTouchNative = function (site, fn) {
  if (!wxWasmCanTouchNative()) {
    return undefined;
  }
  var scheduler = globalThis.__wxScheduler;
  if (scheduler && typeof scheduler.runNativeCompletion === 'function') {
    return scheduler.runNativeCompletion(site, fn);
  }
  return fn();
};

// Stateful browser ingress can swap to a managed dispatch context and unwind
// its JavaScript-to-Wasm export. Give that entry to the physical arbiter and
// call a plain numeric export there; ccall's synchronous wrapper treats the
// intentional unwind as an error.
var wxWasmEnqueueNativeEntry = function (site, fn) {
  if (!wxWasmCanTouchNative()) {
    return false;
  }

  var scheduler = globalThis.__wxScheduler;
  if (scheduler && typeof scheduler.enqueueNativeEntry === 'function') {
    return scheduler.enqueueNativeEntry(null, site, function () {
      wxWasmTouchNative(site, fn);
    });
  }

  wxWasmTouchNative(site, fn);
  return true;
};

// Receipt-time stateful ingress. fn may only call a native staging export:
// that strict leaf copies an owned scalar payload, captures exact modal-lease
// provenance, appends one typed record, and returns without admission or
// suspension. Refusal would lose a discrete browser event, so it is fatal.
var wxWasmStageNativeIngress = function (site, fn) {
  if (!wxWasmCanTouchNative()) {
    return false;
  }

  var scheduler = globalThis.__wxScheduler;
  if (!scheduler ||
      typeof scheduler.runNativeIngressReceipt !== 'function') {
    // The owner runtime is mandatory for scheduler builds. Calling the stage
    // with token 0 would silently restore ambient modal discovery and defeat
    // immutable receipt provenance.
    globalThis.__wxWasmFailed = true;
    globalThis.__wxNativeIntegrityUnknown = true;
    console.error('[wx-owner] ' + site + ' has no ingress receipt runtime');
    // Keep the same asynchronous throw shape as scheduler fail-stop. This
    // callback is already browser-owned; throwing now only reaches the page's
    // error boundary and cannot rewind a native frame.
    setTimeout(function () {
      throw new Error('[wx-owner] ' + site + ' has no ingress receipt runtime');
    }, 0);
    return false;
  }

  var accepted;
  try {
    accepted = scheduler.runNativeIngressReceipt(site, fn);
  } catch (error) {
    // A thrown staging call has already consumed this discrete receipt.  Even
    // a non-trap C++/allocation exception cannot be retried without changing
    // its modal-lease provenance, so make the scheduler terminal first and
    // then preserve the original exception for the browser error boundary.
    if (scheduler && !scheduler.dead &&
        typeof scheduler._failScheduler === 'function') {
      try {
        scheduler._failScheduler(site + ' staging threw: ' + String(error), false);
      } catch (failStopError) {
        console.error('[wx-owner] staging fail-stop failed:', failStopError);
      }
    } else {
      globalThis.__wxWasmFailed = true;
      globalThis.__wxNativeIntegrityUnknown = true;
    }
    throw error;
  }
  if ((accepted | 0) === 1) {
    return true;
  }

  if (scheduler && !scheduler.dead &&
      typeof scheduler._failScheduler === 'function') {
    scheduler._failScheduler(site + ' was refused', false);
  }
  return false;
};

if (typeof navigator !== 'undefined') {
  var browserInfo = (function () {
    var ua = navigator.userAgent;

    var match =
      /(Opera)(?:.*version|)[ \/]([\w.]+)/.exec(ua) ||
      /(OPR)[ \/]([\w.]+)/.exec(ua) ||
      /(Edge)[ \/]([\w.]+)/.exec(ua) ||
      /(MSIE) ([\w.]+)/.exec(ua) ||
      /(Chrome)[ \/]([\w.]+)/.exec(ua) ||
      /Version[ \/]([\w.]+) (Safari)/.exec(ua) ||
      /(Safari)[ \/]([\w.]+)/.exec(ua) ||
      /(Firefox)[ \/]([\w.]+)/.exec(ua) ||
      ua.indexOf('compatible') < 0 &&
      /(Mozilla)(?:.*? rv:([\w.]+)|)/.exec(ua) ||
      [];

    if (match[2] === 'Safari') {
      return {
        browser: match[2],
        name: match[2],
        version: match[1]
      };
    } else {
      return {
        browser: match[1] || '',
        name: match[1] || '',
        version: match[2] || '0'
      };
    }
  })();

  var isWebkit = function () {
    return browserInfo.name === 'Chrome' || browserInfo.name === 'Safari';
  }

  var platformInfo = (function () {
    var ua = navigator.userAgent;

    var match =
      /(Windows NT) ([\w.]+)/.exec(ua) ||
      /(Mac OS X) ([\w.]+)/.exec(ua) ||
      /(CrOS) \w+ ([\w.]+)/.exec(ua) ||
      /(iPhone); .* OS ([\d_]+)/.exec(ua) ||
      /(iPad); .* OS ([\d_]+)/.exec(ua);

    var name = 'unknown';
    var version = '';

    if (match) {
      name = match[1];
      version = match[2];
    } else {
      var PLATFORMS = ['Android', 'iPhone', 'iPad', 'Windows', 'Macintosh', 'Linux', 'CrOs', 'NetBSD', 'OpenBSD', 'FreeBSD'];

      for (var i = 0; i < PLATFORMS.length; i++) {
        if (ua.indexOf(PLATFORMS[i]) !== -1) {
          name = PLATFORMS[i];
        }
      }
    }

    return {
      name: name,
      version: version
    };
  })();
}

  var openUrl = function(url) {
    if (typeof window !== 'undefined') {
      window.open(url, '_blank');
    }
  };

  var setIcon = function(id) {
    var bitmap = bitmapMap.get(id);

    var canvas = document.createElement('canvas');
    var ctx = canvas.getContext('2d');
    canvas.width = bitmap.width;
    canvas.height = bitmap.height;

    drawImage(ctx, bitmap, 0, 0);

    var link = document.querySelector("link[rel*='icon']") || document.createElement('link');
    link.type = 'image/png';
    link.rel = 'shortcut icon';
    link.href = canvas.toDataURL('image/png');
    document.getElementsByTagName('head')[0].appendChild(link);
  };

  var displayScaleFactor = null;

  var getDisplayScaleFactor = function () {
    if (displayScaleFactor === null) {
      displayScaleFactor = window.devicePixelRatio >= 1.5 ? 2.0 : 1.0;
    }
    return displayScaleFactor;
  };

  /* wxNonOwnedWindow */

  // Ensure #window-container creates a stacking context so GL canvases
  // render above the 2D #canvas inside #main-window.
  // This runs at script eval, and with -pthread the same script also evaluates
  // inside Web Workers, where `document` doesn't exist — guard or the workers
  // die with "ReferenceError: document is not defined" before the app loads.
  var windowContainer = (typeof document !== 'undefined')
      ? document.getElementById('window-container') : null;
  if (windowContainer) {
    windowContainer.style.position = 'relative';
    windowContainer.style.zIndex = '1';

    // Window-chrome CSS for the divs createWindow() builds (.window /
    // .window.toplevel / .window-canvas). Injected here — the code that creates
    // these elements — so every host (the e2e test pages, the React standalone
    // shell, and the wx build template) gets identical styling from one source
    // instead of pasting it into each page's <style>. pcbjam #22.
    if (!document.getElementById('wx-window-chrome')) {
      var wxStyle = document.createElement('style');
      wxStyle.id = 'wx-window-chrome';
      wxStyle.textContent = [
        '.window {',
        '  position: absolute;',
        '  pointer-events: none;',
        '  z-index: 10;',
        '  background-color: black;',
        '  overflow: hidden;',
        '  width: 0;',
        '  height: 0;',
        '}',
        // Modal dialogs (top-level windows, not popup menus) get a border + drop
        // shadow so they read as raised surfaces. Popup menus style themselves
        // in wx-dom.js and the main frame is #canvas — neither matches
        // .window.toplevel, so neither is affected.
        '.window.toplevel {',
        '  border: 1px solid #808080;',
        '  box-shadow: 2px 2px 8px rgba(0, 0, 0, 0.35);',
        '}',
        // Input barrier for windows shadowed by a higher, overlapping top-level
        // window (see recomputeModalBarrier). Each dialog control is a real DOM
        // element with pointer-events:auto, so without this a click over an
        // upper modal's canvas-drawn area (which is pointer-events:none) still
        // hit-tests the live control of the dialog beneath it. Forcing the whole
        // subtree to pointer-events:none — !important to beat the inline
        // pointer-events:auto wx-dom.js sets on controls — drops the click
        // through to #canvas, where the C++ hit-test routes it to the true
        // topmost window. Native wx leans on the OS to block input to shadowed
        // windows; the browser has no such barrier, so we add one here.
        '.wx-inert, .wx-inert * {',
        '  pointer-events: none !important;',
        '}',
        '.window-canvas {',
        '  position: absolute;',
        '  top: 0;',
        '  left: 0;',
        '  pointer-events: none;',
        '}',
        // Real DOM title bar for non-main wxFrames (e.g. the 3D viewer). It lives
        // in the top strip of the window-N div with pointer-events:auto, so it
        // wins hit-testing over OTHER frames' DOM controls (it sits inside
        // #window-container (z-index:1), above #main-window's controls). It never
        // overlaps the GL canvas — the GL canvas is positioned the title-bar
        // height BELOW the frame top — so the GL canvas's huge z-index is
        // irrelevant. z-index:11 keeps the bar above the sibling .window-canvas
        // inside window-N's own stacking context (window-N has z-index:10), so it
        // never escapes that context or competes with the GL sentinel z.
        '.window-titlebar {',
        '  position: absolute;',
        '  top: 0;',
        '  left: 0;',
        '  right: 0;',
        '  pointer-events: auto;',
        '  cursor: move;',
        '  z-index: 11;',
        '  display: flex;',
        '  align-items: center;',
        '  box-sizing: border-box;',
        '  background-color: #c8c8c8;',
        '  color: #282828;',
        '  font: bold 12px sans-serif;',
        '  user-select: none;',
        '}',
        '.window-titlebar-text {',
        '  flex: 1;',
        '  padding: 0 6px;',
        '  overflow: hidden;',
        '  text-overflow: ellipsis;',
        '  white-space: nowrap;',
        '}',
        '.window-titlebar-close {',
        '  pointer-events: auto;',
        '  cursor: pointer;',
        '  width: 22px;',
        '  height: 100%;',
        '  border: 0;',
        '  padding: 0;',
        '  background: transparent;',
        '  color: #282828;',
        '  font: bold 15px sans-serif;',
        '  line-height: 1;',
        '}',
        '.window-titlebar-close:hover {',
        '  background-color: #e25a5a;',
        '  color: #ffffff;',
        '}',
        // Edge-resize handles for resizable (wxRESIZE_BORDER) non-main windows.
        // pointer-events:auto + z-index:12 so they win hit-testing inside window-N's
        // own stacking context (above the .window-canvas) AND over other frames'
        // controls (#window-container z-index:1 out-stacks #main-window). Box geometry
        // (top/left/right/bottom/size) is set inline by createWindowResizeHandles so
        // the left/right edges can start below the title bar; CSS carries only the
        // shared bits + per-direction cursor. No top edge/corners: the title bar owns
        // the top strip (move + close), so handles never overlap it.
        '.window-resize-handle {',
        '  position: absolute;',
        '  pointer-events: auto;',
        '  z-index: 12;',
        '}',
        '.window-resize-e { cursor: ew-resize; }',
        '.window-resize-w { cursor: ew-resize; }',
        '.window-resize-s { cursor: ns-resize; }',
        '.window-resize-se { cursor: nwse-resize; z-index: 13; }',
        '.window-resize-sw { cursor: nesw-resize; z-index: 13; }'
      ].join('\n');
      document.head.appendChild(wxStyle);
    }
  }

  var nextWindowId = 0;
  var windowMap = new Map();

  var createWindow = function (id, needsCanvas, isVisible, classList) {
    //console.log('createWindow: ' + id + ' ' + needsCanvas + ' ' + isVisible);

    if (id === -1) {
      id = nextWindowId++;
    }
    
    var window = null;
    var canvas = null;

    if (id === 0) {
      window = document.getElementById('main-window');
      canvas = document.getElementById('canvas');
    } else {
      window = document.createElement('div');
      window.className = classList;
      window.id = 'window-' + id;
      window.style.display = isVisible ? 'block' : 'none';

      // Popup/transient windows (toolbar palettes, color pickers, etc.) are
      // floating overlays. Position them relative to the viewport instead of
      // absolutely within #window-container — the container is not at the
      // viewport origin (it sits below other page content), so an absolutely
      // positioned popup lands far from its intended screen coordinates and is
      // effectively unreachable. `fixed` makes the screen coords passed to
      // setWindowRect map straight to viewport coords, independent of layout.
      if (classList && (' ' + classList + ' ').indexOf(' popup ') !== -1) {
        window.style.position = 'fixed';
      }

      if (needsCanvas) {
        canvas = document.createElement('canvas');
        canvas.className = 'window-canvas';
        window.appendChild(canvas);
      }

      document.getElementById('window-container').appendChild(window);
    }

    windowMap.set(id, {
      window: window,
      canvas: canvas,
      width: 0,
      height: 0,
      imageData: null,
      context: null,
      nativeEnabled: true
    });

    return id;
  };

  var destroyWindow = function (id) {
    var windowData = windowMap.get(id);

    // The window element isn't always a child of #window-container (it may have
    // been moved or never appended), so removeChild() on the container throws
    // NotFoundError — which unwinds out of native callers like OpenProjectFiles
    // and aborts the operation. Use Element.remove(): detaches from whatever
    // parent it has, and is a no-op when unparented.
    if (windowData && windowData.window) windowData.window.remove();
    windowMap.delete(id);
    recomputeModalBarrier();
  };

  // Read-only accessor for the DOM port's control layer (wx-dom.js): native
  // controls attach to their top-level window's container element. No
  // behavior change for the canvas port. (Guarded: with -pthread this script
  // also evaluates in Web Workers, where `window` doesn't exist.)
  if (typeof window !== 'undefined') {
    window.__wxGetWindowElement = function (id) {
      var windowData = windowMap.get(id);
      return windowData ? windowData.window : null;
    };
  }

  var setWindowVisibility = function (id, isVisible) {
    //console.log('setWindowVisibility: ' + id + ': ' + isVisible);

    var windowData = windowMap.get(id);
    windowData.window.style.display = isVisible ? 'block' : 'none';
    recomputeModalBarrier();
  };

  // Mirror wxWindow::Enable() for a top-level window. The C++ enabled flag is
  // authoritative; this browser state supplies the physical pointer, focus,
  // and keyboard barrier that an operating-system window gets natively.
  var setWindowEnabled = function (id, isEnabled) {
    var windowData = windowMap.get(id);
    if (!windowData) return;
    windowData.nativeEnabled = !!isEnabled;
    recomputeModalBarrier();
  };

  var setWindowRect = function (id, x, y, width, height) {
    //console.log('setWindowRect: ' + id + ' (' + x + ', ' + y + ', ' + width + ', ' + height + ')');

    var windowData = windowMap.get(id);

    var header = document.getElementsByClassName('header')[0];
    var headerHeight = header ? header.offsetHeight : 0;

    var window = windowData.window;
    window.style.left = x + 'px';
    window.style.top = y + headerHeight + 'px';
    window.style.width = width + 'px';
    window.style.height = height + 'px';

    var canvas = windowData.canvas;

    if (canvas) {
      var scaleFactor = getDisplayScaleFactor();
      var newWidth = width * scaleFactor;
      var newHeight = height * scaleFactor;

      // Only RE-ASSIGN canvas.width/height when the pixel dimensions actually
      // change. Assigning canvas.width/height clears the canvas to transparent
      // (true in both Chrome and Firefox, even when the value is unchanged). A
      // position-only move — e.g. dragging a dialog by its title bar — keeps the
      // same size, and a move does not schedule a repaint of the window's own
      // content (DoMoveWindow only refreshes the parent). Clearing here would
      // therefore leave the canvas transparent, exposing the black `.window`
      // div behind it until some later repaint. See pcbjam #22.
      //
      // Everything below the guard must run UNCONDITIONALLY: a freshly created
      // window's canvas defaults to 300x150, so when a modal's first
      // setWindowRect happens to match that size the guard is false — and if the
      // context/imageData init lived inside the guard, windowData.context would
      // stay null and the next paint (createWindowContext) would throw
      // "Cannot read properties of null (reading 'depth')", cancelling the modal.
      if (canvas.width !== newWidth || canvas.height !== newHeight) {
        canvas.width = newWidth;
        canvas.height = newHeight;
        canvas.style.width = width + 'px';
        canvas.style.height = height + 'px';
      }

      windowData.width = canvas.width;
      windowData.height = canvas.height;

      if (windowData.width > 0 && windowData.height > 0) {
        windowData.imageData = new ImageData(windowData.width, windowData.height);
      } else {
        windowData.imageData = null;
      }

      var ctx = canvas.getContext('2d');
      ctx.lineJoin = "round";
      ctx.lineCap = "round";
      ctx.imageSmoothingEnabled = false;
      ctx.textBaseline = 'alphabetic';
      ctx.depth = 0;
      ctx.stack = [];

      windowData.context = ctx;
    }

    // A move/resize changes which windows overlap, so re-derive the barrier
    // (e.g. a modal is centered via setWindowRect after it is first shown).
    recomputeModalBarrier();
  };

  // Build a real DOM title bar (drag handle + title text + close "X") for a
  // non-main wxFrame's window-N div (called from wxTopLevelWindowWasm::Create via
  // EM_ASM). Replaces the canvas-painted title bar so the bar wins DOM
  // hit-testing instead of relying on #canvas event routing — which an
  // overlapping pointer-events:auto control from another frame would steal (the
  // confirmed 3D-viewer bug). Drag funnels through wx_window_move_stage ->
  // wxWindow::Move (one reposition source of truth: children follow via the
  // size-event -> Layout path). Close funnels through a non-suspending
  // wx_window_close_stage ingress envelope; later modal work runs under its owner.
  // barHeight comes from the C++ TITLE_BAR_HEIGHT so the strip height is single-
  // sourced and never under/over-laps the client area reserved for it.
  var createWindowTitlebar = function (id, title, barHeight) {
    var windowData = windowMap.get(id);
    if (!windowData || !windowData.window) {
      return;
    }
    var win = windowData.window;

    var bar = document.createElement('div');
    bar.className = 'window-titlebar';
    bar.style.height = barHeight + 'px';

    var text = document.createElement('span');
    text.className = 'window-titlebar-text';
    text.textContent = title || '';

    var closeBtn = document.createElement('button');
    closeBtn.className = 'window-titlebar-close';
    closeBtn.setAttribute('type', 'button');
    closeBtn.setAttribute('aria-label', 'Close');
    closeBtn.textContent = '×';

    bar.appendChild(text);
    bar.appendChild(closeBtn);
    win.appendChild(bar);

    windowData.titlebar = bar;
    windowData.titlebarText = text;

    // --- Drag: titlebar pointer -> wx screen coords -> staged native move. ----
    var dragging = false;
    var grabDX = 0;
    var grabDY = 0;

    bar.addEventListener('pointerdown', function (ev) {
      if (ev.button !== 0) {
        return;
      }
      var r = win.getBoundingClientRect();
      grabDX = ev.clientX - r.left;
      grabDY = ev.clientY - r.top;
      dragging = true;
      try { bar.setPointerCapture(ev.pointerId); } catch (e) {}
      ev.preventDefault();
      ev.stopPropagation();
    });

    bar.addEventListener('pointermove', function (ev) {
      if (!dragging) {
        return;
      }
      // Place the window's top-left so the grab point stays under the cursor,
      // relative to #window-container, then convert to wx screen coords — the
      // inverse of setWindowRect (top = y + headerHeight).
      var container = document.getElementById('window-container');
      var crect = container ? container.getBoundingClientRect() : { left: 0, top: 0 };
      var header = document.getElementsByClassName('header')[0];
      var headerHeight = header ? header.offsetHeight : 0;
      var x = Math.round(ev.clientX - grabDX - crect.left);
      var y = Math.round(ev.clientY - grabDY - crect.top - headerHeight);
      ev.stopPropagation();
      if (typeof Module !== 'undefined') {
        wxWasmStageNativeIngress('titlebar move receipt', function (ingressReceiptToken) {
          var stage = Module['_wx_window_move_stage'];
          if (typeof stage !== 'function')
            throw new Error('wx_window_move_stage export is missing');
          return stage(id, x, y, ingressReceiptToken);
        });
      }
    });

    var endDrag = function (ev) {
      if (!dragging) {
        return;
      }
      dragging = false;
      try { bar.releasePointerCapture(ev.pointerId); } catch (e) {}
      ev.stopPropagation();
    };
    bar.addEventListener('pointerup', endDrag);
    bar.addEventListener('pointercancel', endDrag);

    // --- Close: X -> a typed native ingress envelope. --------------------------
    closeBtn.addEventListener('pointerdown', function (ev) {
      ev.stopPropagation(); // a press on the X must not start a window drag
    });
    closeBtn.addEventListener('click', function (ev) {
      ev.stopPropagation();
      if (typeof Module !== 'undefined') {
        wxWasmStageNativeIngress('titlebar close receipt', function (ingressReceiptToken) {
          var stage = Module['_wx_window_close_stage'];
          if (typeof stage !== 'function')
            throw new Error('wx_window_close_stage export is missing');
          return stage(id, ingressReceiptToken);
        });
      }
    });
  };

  // Edge-resize handles for a resizable (wxRESIZE_BORDER) non-main window. Mirrors
  // createWindowTitlebar's pointer plumbing but drives wx_window_resize_stage
  // (-> wxWindow::SetSize) with a FULL rect: the left/bottom edges and corners move
  // the window origin as well as its size. Five handles — right (e), left (w),
  // bottom (s) and the two bottom corners (se, sw). The top strip is the title bar
  // (move + close), so there are deliberately no top handles. barHeight comes from
  // the C++ TITLE_BAR_HEIGHT so the side handles start just below the bar.
  var createWindowResizeHandles = function (id, barHeight) {
    var windowData = windowMap.get(id);
    if (!windowData || !windowData.window) {
      return;
    }
    var win = windowData.window;

    var MIN_W = 120;    // minimum window size, px (flat floor)
    var MIN_H = 80;
    var EDGE = 6;       // edge-handle thickness, px
    var CORNER = 12;    // corner-handle size, px

    // Each handle: which window borders it moves (edges) + its inline box. e/w start
    // at barHeight so they never overlap the title bar; s/corners sit at the bottom.
    var defs = [
      { cls: 'window-resize-e', edges: { right: true },
        box: { top: barHeight + 'px', right: '0px', bottom: '0px', width: EDGE + 'px' } },
      { cls: 'window-resize-w', edges: { left: true },
        box: { top: barHeight + 'px', left: '0px', bottom: '0px', width: EDGE + 'px' } },
      { cls: 'window-resize-s', edges: { bottom: true },
        box: { left: CORNER + 'px', right: CORNER + 'px', bottom: '0px', height: EDGE + 'px' } },
      { cls: 'window-resize-se', edges: { right: true, bottom: true },
        box: { right: '0px', bottom: '0px', width: CORNER + 'px', height: CORNER + 'px' } },
      { cls: 'window-resize-sw', edges: { left: true, bottom: true },
        box: { left: '0px', bottom: '0px', width: CORNER + 'px', height: CORNER + 'px' } }
    ];

    var handles = [];
    var resizing = false;
    var activeEdges = null;
    var startRect = null;       // window rect (viewport coords) captured at grab
    var startX = 0, startY = 0; // pointerdown coords

    defs.forEach(function (def) {
      var handle = document.createElement('div');
      handle.className = 'window-resize-handle ' + def.cls;
      for (var k in def.box) {
        if (def.box.hasOwnProperty(k)) {
          handle.style[k] = def.box[k];
        }
      }
      win.appendChild(handle);
      handles.push(handle);

      handle.addEventListener('pointerdown', function (ev) {
        if (ev.button !== 0) {
          return;
        }
        resizing = true;
        activeEdges = def.edges;
        startRect = win.getBoundingClientRect();
        startX = ev.clientX;
        startY = ev.clientY;
        try { handle.setPointerCapture(ev.pointerId); } catch (e) {}
        ev.preventDefault();
        ev.stopPropagation();
      });

      handle.addEventListener('pointermove', function (ev) {
        if (!resizing) {
          return;
        }
        var dx = ev.clientX - startX;
        var dy = ev.clientY - startY;

        // New viewport rect: move only the active borders (top never moves here).
        var left = startRect.left;
        var top = startRect.top;
        var right = startRect.right;
        var bottom = startRect.bottom;
        if (activeEdges.left) { left = startRect.left + dx; }
        if (activeEdges.right) { right = startRect.right + dx; }
        if (activeEdges.bottom) { bottom = startRect.bottom + dy; }

        var w = right - left;
        var h = bottom - top;
        // Clamp to the minimum, anchored to the FIXED edge so the window doesn't jump.
        if (w < MIN_W) {
          if (activeEdges.left) { left = right - MIN_W; }
          w = MIN_W;
        }
        if (h < MIN_H) {
          bottom = top + MIN_H; // top is anchored; only the bottom moved
          h = MIN_H;
        }

        // Convert the top-left back to wx screen coords — the inverse of setWindowRect
        // (top = y + headerHeight), the same transform the title-bar drag uses.
        var container = document.getElementById('window-container');
        var crect = container ? container.getBoundingClientRect() : { left: 0, top: 0 };
        var header = document.getElementsByClassName('header')[0];
        var headerHeight = header ? header.offsetHeight : 0;
        var pending = {
          x: Math.round(left - crect.left),
          y: Math.round(top - crect.top - headerHeight),
          w: Math.round(w),
          h: Math.round(h)
        };
        ev.stopPropagation();
        if (typeof Module !== 'undefined') {
          wxWasmStageNativeIngress('window resize receipt', function (ingressReceiptToken) {
            var stage = Module['_wx_window_resize_stage'];
            if (typeof stage !== 'function')
              throw new Error('wx_window_resize_stage export is missing');
            return stage(
              id, pending.x, pending.y, pending.w, pending.h,
              ingressReceiptToken);
          });
        }
      });

      var endResize = function (ev) {
        if (!resizing) {
          return;
        }
        resizing = false;
        try { handle.releasePointerCapture(ev.pointerId); } catch (e) {}
        ev.stopPropagation();
      };
      handle.addEventListener('pointerup', endResize);
      handle.addEventListener('pointercancel', endResize);
    });

    windowData.resizeHandles = handles;
  };

  // Update an existing DOM title bar's text (no-op if the bar isn't built yet —
  // the Create-time SetTitle runs before createWindowTitlebar).
  var setWindowTitle = function (id, title) {
    var windowData = windowMap.get(id);
    if (windowData && windowData.titlebarText) {
      windowData.titlebarText.textContent = title || '';
    }
  };

  var setWindowZIndex = function (id, zIndex) {
    //console.log('setWindowZIndex: ' + id + ': ' + zIndex);

    // The main window (id=0) lives outside #window-container at the body level.
    // Setting its z-index would place it above #window-container's stacking context,
    // hiding GL canvases and popup windows inside it.
    if (id === 0) return;

    var windowData = windowMap.get(id);
    windowData.window.style.zIndex = zIndex;
  };

  var raiseWindow = function (id) {
    var maxZ = 0;

    // Check z-index of all windows
    for (const windowId of windowMap.keys()) {
      var windowData = windowMap.get(windowId);
      if (windowId !== id && windowData) {
        var style = document.defaultView.getComputedStyle(windowData.window);
        var zIndex = parseInt(style.getPropertyValue('z-index'), 10);
        if (!isNaN(zIndex)) {
          maxZ = Math.max(maxZ, zIndex);
        }
      }
    }

    // Also check z-index of GL canvases so popups can appear above them
    for (const [glId, canvas] of glCanvasMap.entries()) {
      if (canvas && canvas.style.display !== 'none') {
        var style = document.defaultView.getComputedStyle(canvas);
        var zIndex = parseInt(style.getPropertyValue('z-index'), 10);
        if (!isNaN(zIndex)) {
          maxZ = Math.max(maxZ, zIndex);
        }
      }
    }

    setWindowZIndex(id, maxZ + 1);
    recomputeModalBarrier();
  };

  var lowerWindow = function (id) {
    var minZ = 0;

    for (const windowId of windowMap.keys()) {
      var windowData = windowMap.get(windowId);
      if (windowId !== id && windowData) {
        var style = document.defaultView.getComputedStyle(windowData.window);
        var zIndex = parseInt(style.getPropertyValue('z-index'), 10);
        if (!isNaN(zIndex)) {
          minZ = Math.min(minZ, zIndex);
        }
      }
    }

    setWindowZIndex(id, minZ - 1);
    recomputeModalBarrier();
  };

  var rectsOverlap = function (a, b) {
    return a.left < b.right && a.right > b.left &&
           a.top < b.bottom && a.bottom > b.top;
  };

  // Re-derive the shadowed-window input barrier from the current z-order and
  // geometry. A top-level dialog/frame is "shadowed" — and gets the wx-inert
  // class so its DOM controls stop receiving pointer events — when some other
  // top-level window with a higher z-index overlaps it. This is what makes a
  // click land on the genuine topmost window (via #canvas + the C++ hit-test)
  // instead of leaking to a live control of a dialog stacked underneath it.
  //
  // Excluded from the barrier, by design:
  //   - the main window (id 0): its #canvas must stay live so shadowed windows'
  //     clicks can fall through to the C++ hit-test.
  //   - popups/tooltips (.popup): they must stay interactive (e.g. a combobox
  //     dropdown) and must never shadow the dialog beneath them, so they count
  //     neither as inert candidates nor as shadowing windows.
  // Non-overlapping windows (e.g. two side-by-side modeless dialogs) are left
  // interactive — only a genuine overlap blocks input.
  var recomputeModalBarrier = function () {
    if (typeof document === 'undefined') return; // worker context: no DOM
    var wins = [];
    windowMap.forEach(function (windowData, id) {
      if (id === 0 || !windowData) return;
      var el = windowData.window;
      if (!el || !el.classList ||
          !el.classList.contains('toplevel') || el.classList.contains('popup')) {
        return;
      }
      if (el.style.display === 'none') return;
      var z = parseInt(document.defaultView.getComputedStyle(el).zIndex, 10);
      if (isNaN(z)) z = 0;
      wins.push({ el: el, z: z, rect: el.getBoundingClientRect() });
    });

    var shadowedWindows = new Set();
    wins.forEach(function (w) {
      var shadowed = wins.some(function (o) {
        return o !== w && o.z > w.z && rectsOverlap(o.rect, w.rect);
      });
      if (shadowed) shadowedWindows.add(w.el);
    });

    // Apply one derived barrier to every registered top-level container,
    // including id 0. Overlap intentionally excludes the main window so its
    // canvas can route hits to the upper dialog, but native wx modality must
    // disable that main window completely.
    windowMap.forEach(function (windowData) {
      if (!windowData || !windowData.window) return;
      var blocked = windowData.nativeEnabled === false ||
                    shadowedWindows.has(windowData.window);
      windowData.window.classList.toggle('wx-inert', blocked);
      // Also block focus/keyboard where supported; CSS is the fallback for
      // engines without HTMLElement.inert.
      try { windowData.window.inert = blocked; }
      catch (e) { /* older engine: CSS suffices */ }
    });
  };

  /* wxColour */

  var formatHexString = function (n) {
    var hexString = n.toString(16);
    while (hexString.length < 8) {
      hexString = '0' + hexString;
    }
    return hexString;
  };

  var makeColorString = function (color) {
    var a = (color >> 24) & 0xff;
    var b = (color >> 16) & 0xff;
    var g = (color >> 8) & 0xff;
    var r = color & 0xff;
    return 'rgba(' + r + ',' + g + ',' + b + ',' + a / 255.0 + ')';
    //return '#' + formatHexString(color);
  };

  /* wxBitmap */

  var nextBitmapId = 0;
  var bitmapMap = new Map();

  var createBitmap = function (width, height, data, scaleFactor) {
    //console.log('setWindowImageData: ' + id + ': ' + '(' + x + ', ' + y + ') ' + width + 'x' + height);

    var id = nextBitmapId++;
    setBitmapData(id, width, height, data, scaleFactor);

    return id;
  };

  var closeImageBitmap = function (imageBitmap) {
    if (imageBitmap && typeof imageBitmap.close === 'function') {
      imageBitmap.close();
    }
  };

  var destroyBitmap = function (id) {
    var bitmap = bitmapMap.get(id);
    if (bitmap) closeImageBitmap(bitmap.imageBitmap);
    bitmapMap.delete(id);
  };

  // Capture this script evaluation's map. A later Wasm instance can evaluate
  // wx.js again in the same realm and reassign the global `bitmapMap` binding;
  // terminal cleanup for the old Module must never clear the new map.
  (function (module, ownedBitmapMap) {
    module['wxDiscardBitmapResources'] = function () {
      ownedBitmapMap.forEach(function (bitmap) {
        closeImageBitmap(bitmap.imageBitmap);
      });
      ownedBitmapMap.clear();
    };
  })(Module, bitmapMap);

  var getBitmapData = function (id, data) {
    var bitmap = bitmapMap.get(id);

    if (!bitmap) {
      return;
    }

    var imageData;

    if (bitmap.context) {
      imageData = bitmap.context.getImageData(0, 0, bitmap.width, bitmap.height);
      bitmap.context = null;
    } else {
      imageData = bitmap.imageData;
    }

    if (!imageData) {
      return;
    }

    // Cache the recovered pixels so a later SyncToCpp on this bitmap (after its
    // memory-DC context has been consumed) can't dereference a null imageData.
    bitmap.imageData = imageData;
    closeImageBitmap(bitmap.imageBitmap);
    bitmap.imageBitmap = null;

    Module.HEAPU8.set(imageData.data, data);
  };

  var setBitmapData = function (id, width, height, data, scaleFactor) {
    var size = 4 * width * height;
    var array = new Uint8ClampedArray(Module.HEAPU8.buffer, data, size);
    var imageData = new ImageData(width, height);  
    imageData.data.set(array);

    var previous = bitmapMap.get(id);
    if (previous) closeImageBitmap(previous.imageBitmap);

    // The record object is the generation token for this exact pixel update.
    // createImageBitmap() completions can arrive out of order. A completion
    // may publish only while this same record is still current for the id.
    var bitmap = {
      data: data,
      size: size,
      width: width,
      height: height,
      scaleFactor: scaleFactor,
      imageData: imageData,
      imageBitmap: null,
      context: null
    };

    bitmapMap.set(id, bitmap);

    // Retain the exact map as well as the record. Re-evaluating this global
    // pre-js file for a replacement module rebinds `bitmapMap`.
    var ownedBitmapMap = bitmapMap;
    createImageBitmap(imageData, 0, 0, width, height).then(function (imageBitmap) {
      if (ownedBitmapMap.get(id) !== bitmap || bitmap.context) {
        closeImageBitmap(imageBitmap);
        return;
      }

      closeImageBitmap(bitmap.imageBitmap);
      bitmap.imageBitmap = imageBitmap;
    }, function (error) {
      // imageData remains the synchronous rendering fallback. Observe every
      // rejection, but report it only if this generation still owns the id.
      if (ownedBitmapMap.get(id) === bitmap) {
        console.warn('[wxBitmap] createImageBitmap failed: ' + String(error));
      }
    });
  };

  /* wxDC */

  var nextContextId = 0;
  var contextMap = new Map();

  var createOffscreenContext = function (width, height) {
    var canvas = null;

    if (typeof OffscreenCanvas !== 'undefined') {
      canvas = new OffscreenCanvas(width, height);
    } else if (typeof document !== 'undefined' && 'createElement' in document) {
      canvas = document.createElement('canvas');
      canvas.width = width;
      canvas.height = height;
    }

    if (canvas !== null) {
        var ctx = canvas.getContext('2d');
        ctx.lineJoin = "round";
        ctx.lineCap = "round";
        ctx.imageSmoothingEnabled = false;
        ctx.textBaseline = 'alphabetic';
        return ctx;
    } else {
        return null;
    }
  };

  var offscreenContext = createOffscreenContext(1, 1);

  var pushContext = function (ctx) {
    var saveCtx = {
      x: ctx.x,
      y: ctx.y,
      width: ctx.width,
      height: ctx.height,
      scaleFactor: ctx.scaleFactor,
      isInitialized: ctx.isInitialized
    };

    if (ctx.isInitialized) {
      saveCtx.font = ctx.font,
      saveCtx.lineWidth = ctx.lineWidth,
      saveCtx.lineJoin = ctx.lineJoin,
      saveCtx.lineCap = ctx.lineCap,
      saveCtx.fillStyle = ctx.fillStyle,
      saveCtx.strokeStyle = ctx.strokeStyle
      saveCtx.dashCount = ctx.dashCount;

      if (saveCtx.dashCount > 0) {
        saveCtx.setLineDash(ctx.getLineDash());
      }

      ctx.restore();
      ctx.save();
    }

    ctx.stack.push(saveCtx);
  };

  var popContext = function (ctx) {
    var restoreCtx = ctx.stack.pop();

    ctx.x = restoreCtx.x;
    ctx.y = restoreCtx.y;
    ctx.width = restoreCtx.width;
    ctx.height = restoreCtx.height;
    ctx.scaleFactor = restoreCtx.scaleFactor;
    ctx.isInitialized = restoreCtx.isInitialized;

    if (ctx.isInitialized) {
      ctx.restore();
      ctx.save();

      ctx.font = restoreCtx.font;
      ctx.lineWidth = restoreCtx.lineWidth;
      ctx.lineJoin = restoreCtx.lineJoin;
      ctx.lineCap = restoreCtx.lineCap;
      ctx.fillStyle = restoreCtx.fillStyle;
      ctx.strokeStyle = restoreCtx.strokeStyle;
      ctx.dashCount = restoreCtx.dashCount;

      if (ctx.dashCount > 0) {
        ctx.setLineDash(restoreCtx.getLineDash());
      }

      // TODO: save/restore clip
      ctx.beginPath();
      ctx.rect(0, 0, ctx.width, ctx.height);
      ctx.clip();
    }
  };

  // GL canvas element management (for wxGLCanvas child windows)
  var glCanvasMap = new Map();
  var nextGLCanvasId = 1;

  var createGLCanvas = function (isMainFrame) {
    var id = nextGLCanvasId++;
    var canvas = document.createElement('canvas');
    canvas.id = 'glcanvas-' + id;
    canvas.className = 'gl-canvas';
    canvas.style.position = 'absolute';
    canvas.style.display = 'none';  // Always start hidden until properly positioned
    // The C++ window that owns this canvas supplies its semantic role.  Do not
    // infer the role from canvas creation or visibility order: GAL recovery
    // creates the replacement main-frame canvas before it destroys the failed
    // one, so both can exist briefly.  Main-frame GL stays below the shared 2D
    // chrome at z=100.  A secondary-frame GL surface (for example, the 3D
    // viewer) must be above that frame's opaque chrome.
    canvas.style.zIndex = isMainFrame ? '100' : '2147483647';
    canvas.style.pointerEvents = 'none';  // Don't intercept clicks - let main canvas handle events
    document.getElementById('window-container').appendChild(canvas);
    glCanvasMap.set(id, canvas);
    return id;
  };

  var setGLCanvasRect = function (id, x, y, width, height) {
    var canvas = glCanvasMap.get(id);
    if (!canvas) return;

    // Only position and show if we have valid dimensions
    if (width <= 0 || height <= 0) {
      canvas.style.display = 'none';
      return;
    }

    var header = document.getElementsByClassName('header')[0];
    var headerHeight = header ? header.offsetHeight : 0;

    canvas.style.left = x + 'px';
    canvas.style.top = (y + headerHeight) + 'px';
    canvas.style.width = width + 'px';
    canvas.style.height = height + 'px';

    var scaleFactor = getDisplayScaleFactor();
    var newW = width * scaleFactor;
    var newH = height * scaleFactor;
    // Only reassign the backing store when the pixel size actually changes:
    // assigning canvas.width/height clears the GL drawing buffer, which would
    // blank/flicker the 3D view on every pointermove during a title-bar drag (a
    // drag is a pure move — same size). Mirrors the guard in setWindowRect.
    if (canvas.width !== newW || canvas.height !== newH) {
      canvas.width = newW;
      canvas.height = newH;
    }

    // Show the canvas now that it's properly positioned
    // (visibility is also controlled by setGLCanvasVisibility for show/hide logic)
    if (canvas.dataset.shouldBeVisible !== 'false') {
      canvas.style.display = 'block';
    }
  };

  var setGLCanvasVisibility = function (id, isVisible) {
    var canvas = glCanvasMap.get(id);
    if (canvas) {
      canvas.dataset.shouldBeVisible = isVisible ? 'true' : 'false';
      canvas.style.display = isVisible ? 'block' : 'none';
    }
  };

  var destroyGLCanvas = function (id) {
    var canvas = glCanvasMap.get(id);
    if (canvas && canvas.parentNode) {
      canvas.parentNode.removeChild(canvas);
    }
    glCanvasMap.delete(id);
  };

  // One polling lifetime per wx.js evaluation. Keep every helper and record
  // inside this closure: global `var` bindings are replaced when a new Wasm
  // module evaluates this pre-js file in the same realm.
  (function (module, initialGL) {
    var lifetime = {
      active: true,
      module: module,
      gl: initialGL,
      interval: null,
      deadline: null
    };

    var stop = function () {
      if (!lifetime.active) return;
      lifetime.active = false;
      if (lifetime.interval !== null) {
        clearInterval(lifetime.interval);
        lifetime.interval = null;
      }
      if (lifetime.deadline !== null) {
        clearTimeout(lifetime.deadline);
        lifetime.deadline = null;
      }
    };

    var patch = function () {
      if (!lifetime.active || Module !== lifetime.module) {
        stop();
        return;
      }

      // GL can appear after pre-js evaluation. Capture it once for this
      // module; a later changed global GL belongs to a replacement runtime.
      if (lifetime.gl === null) {
        if (typeof GL === 'undefined') return;
        lifetime.gl = GL;
      } else if (typeof GL === 'undefined' || GL !== lifetime.gl) {
        stop();
        return;
      }

      var ownedGL = lifetime.gl;
      if (!ownedGL.newRenderingFrameStarted || ownedGL._wxPatched) {
        if (ownedGL._wxPatched) stop();
        return;
      }

      var originalNewRenderingFrameStarted = ownedGL.newRenderingFrameStarted;
      ownedGL.newRenderingFrameStarted = function () {
        if (!ownedGL.currentContext) return;
        if (!ownedGL.currentContext.tempVertexBuffers1 ||
            !ownedGL.currentContext.tempVertexBufferCounters1) return;
        return originalNewRenderingFrameStarted.call(ownedGL);
      };
      ownedGL._wxPatched = true;
      stop();
    };

    module['wxDiscardGLPatchTimer'] = stop;
    patch();
    if (lifetime.active) {
      lifetime.interval = setInterval(patch, 10);
      lifetime.deadline = setTimeout(stop, 5000);
    }
  })(Module, typeof GL !== 'undefined' ? GL : null);

  var createWindowContext = function (windowId, x, y, width, height, scaleFactor) {
    var id = nextContextId++;
    //console.log('createWindowContext: ' + windowId + ' ' + x + ' ' + y + ' ' + width + ' ' + height);

    var windowData = windowMap.get(windowId);
    var ctx = windowData.context;

    if (ctx.depth > 0) {
      pushContext(ctx);
    }

    ctx.x = x;
    ctx.y = y;
    ctx.width = width;
    ctx.height = height;
    ctx.scaleFactor = scaleFactor;
    ctx.isInitialized = false;
    ctx.depth++;

    contextMap.set(id, ctx);

    return id;
  };

  var destroyWindowContext = function (id) {
    var ctx = contextMap.get(id);

    if (ctx.isInitialized) {
      ctx.restore();
    }

    if (ctx.depth > 1) {
      popContext(ctx);
    }

    ctx.depth--;

    //console.log('destroyContext: ' + id + ' ' + ctx.width + ' ' + ctx.height);
    contextMap.delete(id);
  };

  var createMemoryContext = function (bitmapId, scaleFactor) {
    var contextId = nextContextId++;
    var bitmap = bitmapMap.get(bitmapId);

    var ctx = createOffscreenContext(bitmap.width, bitmap.height);

    ctx.x = 0;
    ctx.y = 0;
    ctx.width = bitmap.width / scaleFactor;
    ctx.height = bitmap.height / scaleFactor;
    ctx.scaleFactor = scaleFactor;
    ctx.dashCount = 0;
    ctx.isInitialized = true;
    ctx.depth = 0;
    ctx.stack = [];

    ctx.scale(scaleFactor, scaleFactor);

    contextMap.set(contextId, ctx);

    drawImage(ctx, bitmap, 0, 0);

    bitmap.imageData = null;
    closeImageBitmap(bitmap.imageBitmap);
    bitmap.imageBitmap = null;
    bitmap.context = ctx;

    return contextId;
  };

  var destroyMemoryContext = function (contextId) {
    //console.log('deselectBitmap: ' + contextId);
    contextMap.delete(contextId);
  };

  var getContext = function (id) {
    var ctx = contextMap.get(id);

    if (!ctx.isInitialized) {
      // scale and translate(x, y)
      var x = ctx.x;
      var y = ctx.y;
      var scaleFactor = ctx.scaleFactor;

      ctx.setTransform(scaleFactor, 0, 0, scaleFactor, scaleFactor * x, scaleFactor * y);

      ctx.save();

      ctx.beginPath();
      ctx.rect(0, 0, ctx.width, ctx.height);
      ctx.clip()

      ctx.dashCount = 0;
      ctx.isInitialized = true;
    }

    return ctx;
  };

  var setFont = function (id, font) {
    var ctx = getContext(id);
    ctx.font = font;
  };

  var createPattern = function (contextId, bitmapId) {
    var ctx = getContext(contextId);
    var bitmap = bitmapMap.get(bitmapId);
    var source;

    if (bitmap.imageBitmap) {
      source = bitmap.imageBitmap;
    } else if (bitmap.context) {
      source = bitmap.context.canvas;
    } else {
      offscreenContext.canvas.width = bitmap.width;
      offscreenContext.canvas.height = bitmap.height;
      offscreenContext.putImageData(bitmap.imageData, 0, 0);
      source = offscreenContext.canvas;
    }

    return ctx.createPattern(source, 'repeat');
  };

  var setBrush = function (contextId, color, bitmapId) {
    var ctx = getContext(contextId);

    if (bitmapId === -1 || typeof bitmapId === 'undefined') {
      ctx.fillStyle = makeColorString(color);
    } else {
      ctx.fillStyle = createPattern(contextId, bitmapId);
    }
  };

  var lineJoinMap = [
    'round',
    'bevel',
    'miter'
  ];

  var lineCapMap = [
    'butt',
    'round',
    'square'
  ];

  var setPen = function (contextId, color, lineWidth, lineJoin, lineCap, dashCount, dashPtr, bitmapId) {
    var ctx = getContext(contextId);

    ctx.lineWidth = lineWidth;
    ctx.lineJoin = lineJoinMap[lineJoin];
    ctx.lineCap = lineCapMap[lineCap];

    if (bitmapId === -1 || typeof bitmapId === 'undefined') {
      ctx.strokeStyle = makeColorString(color);
    } else {
      ctx.strokeStyle = createPattern(contextId, bitmapId);
    }

    ctx.dashCount = dashCount;
    var dashes = [];
    for (var i = 0; i < dashCount; i++) {
      dashes.push(Module.HEAP8[dashPtr + i]);
    }
    ctx.setLineDash(dashes);
  };

  var resetClip = function (ctx) {
    var font = ctx.font;
    var lineWidth = ctx.lineWidth;
    var lineJoin = ctx.lineJoin;
    var lineCap = ctx.lineCap;
    var fillStyle = ctx.fillStyle;
    var strokeStyle = ctx.strokeStyle;

    ctx.restore();
    ctx.save();

    ctx.font = font;
    ctx.lineWidth = lineWidth;
    ctx.lineJoin = lineJoin;
    ctx.lineCap = lineCap;
    ctx.fillStyle = fillStyle;
    ctx.strokeStyle = strokeStyle;
  };

  var clipRect = function (id, x, y, width, height) {
    //console.log('clipRect: ' + x + ' ' + y + ' ' + width + ' ' + height);
    var ctx = getContext(id);

    // An empty clip box means "everything is clipped out" — apply it as such.
    // (An earlier fallback expanded empty rects to the full context, but that
    // was masking the C++ side sending empty boxes for every clip.)
    if (width < 0) width = 0;
    if (height < 0) height = 0;

    resetClip(ctx);

    ctx.beginPath();
    ctx.rect(x, y, width, height);
    ctx.clip();
  };

  // Clip to a non-rectangular region composed of multiple rectangles
  var clipRegion = function (id, rectDataPtr, rectCount) {
    var ctx = getContext(id);
    resetClip(ctx);

    ctx.beginPath();

    // Read rectangle data from WASM memory (4 ints per rect: x, y, w, h)
    for (var i = 0; i < rectCount; i++) {
      var offset = rectDataPtr / 4 + i * 4;  // Convert byte offset to int offset
      var x = Module.HEAP32[offset];
      var y = Module.HEAP32[offset + 1];
      var w = Module.HEAP32[offset + 2];
      var h = Module.HEAP32[offset + 3];
      ctx.rect(x, y, w, h);
    }

    ctx.clip();
  };

  var destroyClip = function (id) {
    var ctx = getContext(id);

    resetClip(ctx);

    ctx.beginPath();
    ctx.rect(0, 0, ctx.width, ctx.height);
    ctx.clip();
  };

  var clearRect = function (id, width, height, color) {
    var ctx = getContext(id);

    var saveFillStyle = ctx.fillStyle;
    ctx.fillStyle = makeColorString(color);
    // TODO: save/restore clip

    ctx.fillRect(0, 0, width, height);
    ctx.fillStyle = saveFillStyle;
  };

  var drawRect = function (id, x, y, width, height, fill, stroke) {
    var ctx = getContext(id);

    if (fill) {
      ctx.fillRect(x, y, width, height);
    }

    if (stroke) {
      ctx.strokeRect(x, y, width, height);
    }
  };

  var drawRoundedRect = function (id, x, y, width, height, radius, fill, stroke) {
    var ctx = getContext(id);

    ctx.beginPath();
    ctx.moveTo(x + radius, y);
    ctx.lineTo(x + width - radius, y);
    ctx.arcTo(x + width, y, x + width, y + radius, radius);
    ctx.lineTo(x + width, y + height - radius);
    ctx.arcTo(x + width, y + height, x + width - radius, y + height, radius);
    ctx.lineTo(x + radius, y + height);
    ctx.arcTo(x, y + height, x, y + height - radius, radius);
    ctx.lineTo(x, y + radius);
    ctx.arcTo(x, y, x + radius, y, radius);
    ctx.closePath();

    if (fill) {
      ctx.fill();
    }

    if (stroke) {
      ctx.stroke();
    }
  };

  var drawEllipse = function (id, x, y, width, height, fill, stroke) {
    var ctx = getContext(id);

    var radiusX = width / 2.0; 
    var radiusY = height / 2.0;
    var cx = x + radiusX;
    var cy = y + radiusY 

    ctx.beginPath();
    ctx.ellipse(cx, cy, radiusX, radiusY, 0.0, 0.0, 2 * Math.PI);

    if (fill) {
      ctx.fill();
    }

    if (stroke) {
      ctx.stroke();
    }
  };

  var drawArc = function (id, x, y, radius, startAngle, endAngle, fill, stroke) {
    var ctx = getContext(id);

    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.arc(x, y, radius, startAngle, endAngle, true);

    if (fill) {
      ctx.fill();
    }

    if (stroke) {
      ctx.stroke();
    }
  };

  var drawEllipticArc = function (id, x, y, width, height, startDegrees, endDegrees, fill, stroke) {
    var ctx = getContext(id);

    var radiusX = width / 2.0;
    var radiusY = height / 2.0;
    var cx = x + radiusX;
    var cy = y + radiusY;
    var startRadians = -startDegrees * (Math.PI / 180.0);
    var endRadians = -endDegrees * (Math.PI / 180.0);

    if (fill) {
      ctx.beginPath();
      ctx.ellipse(cx, cy, radiusX, radiusY, 0.0, startRadians, endRadians, true);
      ctx.lineTo(cx, cy);
      ctx.fill();
    }

    if (stroke) {
      ctx.beginPath();
      ctx.ellipse(cx, cy, radiusX, radiusY, 0.0, startRadians, endRadians, true);
      ctx.stroke();
    }
  };

  var drawPoint = function (id, x, y) {
    var ctx = getContext(id);
    ctx.strokeRect(x, y, 1e-6, 1e-6);
  };

  var drawLine = function (id, x1, y1, x2, y2) {
    var ctx = getContext(id);

    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);

    ctx.stroke();
  };

  var drawLines = function (id, n, ptr) {
    var ctx = getContext(id);

    if (n > 0) {
      var index = ptr >> 2;
      var x = Module.HEAP32[index++];
      var y = Module.HEAP32[index++];

      ctx.beginPath();
      ctx.moveTo(x, y);

      for (var i = 1; i < n; i++) {
        x = Module.HEAP32[index++]; 
        y = Module.HEAP32[index++];
        ctx.lineTo(x, y);
      }

      ctx.stroke();
    } 
  };

  var drawPolygon = function (id, n, ptr, fillEvenOdd, fill, stroke) {
    var ctx = getContext(id);

    if (n > 0) {
      var index = ptr >> 2;
      var x = Module.HEAP32[index++];
      var y = Module.HEAP32[index++];

      ctx.beginPath();
      ctx.moveTo(x, y);

      for (var i = 1; i < n; i++) {
        x = Module.HEAP32[index++]; 
        y = Module.HEAP32[index++];
        ctx.lineTo(x, y);
      }

      ctx.closePath();

      if (fill) {
        ctx.fill(fillEvenOdd ? 'evenodd' : 'nonzero');
      }

      if (stroke) {
        ctx.stroke();
      }
    } 
  };

  var drawImage = function (ctx, bitmap, x, y) {
    var w = bitmap.width;
    var h = bitmap.height;
    var sf = bitmap.scaleFactor;
    var source;

    // console.log('drawImage: ' + bitmap.id + ' ' + x + ' ' + y + ' ' + w + ' ' + h + ' ' + sf);

    if (bitmap.imageBitmap) {
      source = bitmap.imageBitmap;
    } else if (bitmap.context) {
      source = bitmap.context.canvas;
    } else {
      offscreenContext.canvas.width = bitmap.width;
      offscreenContext.canvas.height = bitmap.height;
      offscreenContext.putImageData(bitmap.imageData, 0, 0);
      source = offscreenContext.canvas;
    }

    if (bitmap.scaleFactor == 1.0) {
      ctx.drawImage(source, x, y);
    } else {
      var sf = 1.0 / bitmap.scaleFactor;
      ctx.drawImage(source, 0, 0, w, h, x, y, w * sf, h * sf);
    }
  };

  var drawBitmap = function (contextId, bitmapId, x, y) {
    var ctx = getContext(contextId);
    var bitmap = bitmapMap.get(bitmapId);

    //console.log('drawBitmap: ' + contextId + ' ' + bitmapId + ' (' + x + ', ' + y + ')' + ' (' + bitmap.width + ', ' + bitmap.height + ')');

    drawImage(ctx, bitmap, x, y);
  };

  var blit = function (srcId, dstId, sx, sy, width, height, dx, dy) {
    var srcCtx = getContext(srcId);
    var dstCtx = getContext(dstId);

    //console.log('blit: ' + sx + ' ' + sy + ' ' + dx + ' ' + dy + ' ' + width + ' ' + height + ' ' + srcCtx.scaleFactor + ' ' + dstCtx.scaleFactor);

    var sf = srcCtx.scaleFactor
    dstCtx.drawImage(srcCtx.canvas, sx * sf, sy * sf, width * sf, height * sf, dx, dy, width, height);
  };

  var drawText = function (id, text, x, y, textColor, underline, strikethrough) {
    var ctx = getContext(id);
    //console.log('drawText: ' + text + ' ' + id + ' ' + ctx.width + ' ' + ctx.height);

    var fillStyle = ctx.fillStyle;

    ctx.fillStyle = makeColorString(textColor);
    ctx.fillText(text, x, y);

    // Draw text decorations (underline and/or strikethrough)
    if (underline || strikethrough) {
      var metrics = ctx.measureText(text);
      var textWidth = metrics.width;

      // Save current state
      var strokeStyle = ctx.strokeStyle;
      var lineWidth = ctx.lineWidth;

      ctx.strokeStyle = makeColorString(textColor);
      ctx.lineWidth = 1;

      if (underline) {
        // Draw underline below the baseline
        // Use fontBoundingBoxDescent if available, otherwise estimate
        var descent = metrics.fontBoundingBoxDescent || 3;
        var underlineY = y + descent;
        ctx.beginPath();
        ctx.moveTo(x, underlineY);
        ctx.lineTo(x + textWidth, underlineY);
        ctx.stroke();
      }

      if (strikethrough) {
        // Draw strikethrough at middle of text
        // Use fontBoundingBoxAscent if available, otherwise estimate
        var ascent = metrics.fontBoundingBoxAscent || 10;
        var strikeY = y - ascent * 0.35;  // ~35% up from baseline
        ctx.beginPath();
        ctx.moveTo(x, strikeY);
        ctx.lineTo(x + textWidth, strikeY);
        ctx.stroke();
      }

      // Restore state
      ctx.strokeStyle = strokeStyle;
      ctx.lineWidth = lineWidth;
    }

    ctx.fillStyle = fillStyle;
  };

  var measureText = function (text, font) {
    offscreenContext.font = font;

    var textMetrics = offscreenContext.measureText(text);
    return Math.round(textMetrics.width);
  };

  var rotateAtPoint = function (id, x, y, angle) {
    var ctx = getContext(id);

    ctx.save();
    ctx.translate(x, y);
    ctx.rotate(-angle * (Math.PI / 180.0));
  };

  var clearRotation = function (id) {
    var ctx = getContext(id);
    ctx.restore();
  };

  /* wxCursor */

  var cursorMap = [
    'default',
    'crosshair',
    'hand',
    'text',
    'wait',
    'help',
    'e-resize',
    'n-resize',
    'ne-resize',
    'nw-resize',
    's-resize',
    'se-resize',
    'sw-resize',
    'w-resize',
    'ns-resize',
    'ew-resize',
    'nesw-resize',
    'nwse-resize',
    'col-resize',
    'row-resize',
    'move',
    'vertical-text',
    'cell',
    'context-menu',
    'alias',
    'progress',
    'no-drop',
    'copy',
    'none',
    'not-allowed',
    'zoom-in',
    'zoom-out',
    'grab',
    'grabbing'
  ];

  var setCursor = function (cursorIndex, bitmapId, hotSpotX, hotSpotY) {
    if (cursorIndex >= 0 && cursorIndex < cursorMap.length) {
      var cursor = cursorMap[cursorIndex];
      if (cursor.startsWith('grab') && isWebkit()) {
        cursor = '-webkit-' + cursor;
      }
      Module.canvas.style.cursor = cursor;
    } else {
      var bitmap = bitmapMap.get(bitmapId);

      var canvas = document.createElement('canvas');
      var ctx = canvas.getContext('2d');
      canvas.width = bitmap.width;
      canvas.height = bitmap.height;

      drawImage(ctx, bitmap, 0, 0);
      var dataUrl = 'url(' + canvas.toDataURL('image/png') + ')';

      Module.canvas.style.cursor = dataUrl + ' ' + hotSpotX + ' ' + hotSpotY + ', auto';
    }
  };

  var showFullscreen = function (enable) {
    if (enable) {
      if (document.body.requestFullscreen) {
        document.body.requestFullscreen();
      } else if (document.body.webkitRequestFullscreen()) {
        document.body.webkitRequestFullscreen();
      }
    } else {
      if (document.exitFullscreen) {
        document.exitFullscreen();
      } else if (document.webkitExitFullscreen) {
        document.webkitExitFullscreen();
      }
    }
  };

  var downloadFile = function (filename, size, data) {
    var link = document.createElement('a');

    var sharedArray = new Uint8Array(Module.HEAPU8.buffer, data, size);
    // Blob fails when passed SharedArrayBuffer
    var array = new Uint8Array(sharedArray);
    var blob = new Blob([array], {type: 'application/octet-stream'});

    link.href = URL.createObjectURL(blob);
    link.download = filename;
    link.click();
  };

  /* wxLocalStorageConfig */

  var hasConfigEntry = function (key) {
    try {
      return localStorage.getItem(key) !== null;
    } catch (error) {
      console.error(error);
      return false;
    }
  };

  var hasConfigGroup = function (key) {
    try {
      for (var i = 0; i < localStorage.length; i++) {
        if (localStorage.key(i).startsWith(key)) {
          return true;
        }
      }
      return false;
    } catch (error) {
      console.error(error);
      return false;
    }
  };

  var getConfigEntryCount = function (prefix, recurse) {
    var entryCount = 0;

    try {
      for (var i = 0; i < localStorage.length; i++) {
        var key = localStorage.key(i);
        if (key.startsWith(prefix)) {
          var end = key.indexOf('/', prefix.length);
          if (end == -1 || recurse) {
            ++entryCount;
          }
        }
      }
    } catch (error) {
      console.error(error);
    }
    return entryCount;
  };

  var getConfigEntryIndex = function (prefix, index) {
    var entryCount = 0;

    try {
      for (var i = 0; i < localStorage.length; i++) {
        var key = localStorage.key(i);
        if (key.startsWith(prefix)) {
          var end = key.indexOf('/', prefix.length);
          if (end == -1) {
            if (entryCount >= index) {
              return i;
            } else {
              ++entryCount;
            }
          }
        }
      }
    } catch (error) {
      console.error(error);
    }
    return -1;
  };

  var getConfigGroupCount = function (prefix, recurse) {
    var children = new Set();

    try {
      for (var i = 0; i < localStorage.length; i++) {
        var key = localStorage.key(i);
        if (key.startsWith(prefix)) {
          var end = key.indexOf('/', prefix.length);
          if (end != -1) {
            if (recurse) {
              end = key.lastIndexOf('/');
            }
            var child = key.substring(prefix.length, end);
            if (!children.has(child)) {
              children.add(child);
            }
          }
        }
      }
    } catch (error) {
      console.error(error);
    }
    return children.size;
  };

  var getConfigGroupIndex = function (prefix, index) {
    var children = new Set();

    try {
      for (var i = 0; i < localStorage.length; i++) {
        var key = localStorage.key(i);
        if (key.startsWith(prefix)) {
          var end = key.indexOf('/', prefix.length);
          if (end != -1) {
            var child = key.substring(prefix.length, end);
            if (!children.has(child)) {
              if (children.size >= index) {
                return i;
              } else {
                children.add(child);
              }
            }
          }
        }
      }
    } catch (error) {
      console.error(error);
    }
    return -1;
  };

  var getConfigKeyLength = function (index) {
    try {
      return localStorage.key(index).length;
    } catch (error) {
      console.error(error);
      return 0;
    }
  };

  var getConfigKey = function (index, keyBuffer, length) {
    try {
      var key = localStorage.key(index);
      stringToUTF8(key, keyBuffer, length);
    } catch (error) {
      console.error(error);
    }
  };

  var getConfigEntryLength = function (key) {
    var value = null;
    try {
      value = localStorage.getItem(key);
    } catch (error) {
      //console.error(error);
    }

    if (value === null) {
      return -1;
    } else {
      return value.length
    }
  };

  var getConfigEntry = function (key, valueBuffer, length) {
    try {
      var value = localStorage.getItem(key);
      if (value !== null) {
        stringToUTF8(value, valueBuffer, length);
        return true;
      } else {
        return false;
      }
    } catch (error) {
      console.error(error);
      return false;
    }
  };

  var setConfigEntry = function (key, value) {
    try {
      localStorage.setItem(key, value);
    } catch (error) {
      console.error(error);
    }
  };

  var removeConfigEntry = function (key) {
      try {
        localStorage.removeItem(key);
      } catch (error) {
        console.error(error);
      }
  };

  var removeConfigGroup = function (group) {
    try {
      var keysToRemove = [];

      for (var i = 0; i < localStorage.length; i++) {
        var key = localStorage.key(i);
        if (key.startsWith(group)) {
          keysToRemove.push(key);
        }
      }
      for (var i = 0; i < keysToRemove.length; i++) {
        localStorage.removeItem(keysToRemove[i]);
      }
      return keysToRemove.length > 0;
    } catch (error) {
      console.error(error);
      return false;
    }
  };

  var clearConfig = function () {
    try {
      localStorage.clear();
    } catch (error) {
      console.error(error);
    }
  };

  var renameConfigGroup = function (oldGroup, newGroup) {
    try {
      var keysToRename = [];

      for (var i = 0; i < localStorage.length; i++) {
        var key = localStorage.key(i);
        if (key.startsWith(oldGroup)) {
          keysToRename.push(key);
        } else if (key.startsWith(newGroup)) {
          return false;
        }
      }

      if (keysToRename.length > 0) {
        for (var i = 0; i < keysToRename.length; i++) {
          var oldKey = keysToRename[i];
          var newKey = newGroup + oldKey.substring(oldGroup.length);

          var value = localStorage.getItem(oldKey);
          localStorage.setItem(newKey, value);
          localStorage.removeItem(oldKey);
        }
        return true;
      } else {
        return false;
      }
    } catch (error) {
      console.error(error);
      return false;
    }

  };

  /* HTML5 Drag and Drop Support */

  // Each browser drop owns one immutable transaction. File reads finish
  // asynchronously and different drops can overlap, so a process-wide
  // pending-files array would mix their paths and coordinates. Native owns a
  // token after wx_file_drop_stage accepts it and releases that exact token at
  // dispatch or discard.
  // One exact lifetime for one evaluation of this global pre-js file. A slow
  // File.arrayBuffer() reaction can outlive its Wasm module. Never let it use
  // a replacement evaluation's map or its restarted token sequence.
  var dropLifetime = {
    active: true,
    batches: new Map(),
    pendingBytes: 0,
    nextToken: 1
  };
  var MAX_PENDING_DROP_BATCHES = 64;
  var MAX_FILES_PER_DROP = 4096;
  var MAX_PENDING_DROP_BYTES = 256 * 1024 * 1024;

  // Capacity is checked before native accepts a drop. Refusing at this point
  // loses no native wake, owner tail, or model mutation, so it is an ordinary
  // user-input rejection rather than a scheduler failure.
  var rejectDropBatch = function (reason) {
    console.warn('[DND] Rejected: ' + reason);
  };

  var safeDropFileName = function (name, index) {
    var safe = String(name || '').replace(/[\\/\0]/g, '_');
    if (!safe || safe === '.' || safe === '..')
      safe = 'file-' + index;
    return safe;
  };

  // Reserve an immutable browser transaction before starting any File reads.
  // Reads from different accepted drops remain concurrent, but their retained
  // File objects and eventual ArrayBuffers share one count and byte budget.
  var reserveDropBatch = function (lifetime, files) {
    if (!lifetime.active ||
        lifetime.batches.size >= MAX_PENDING_DROP_BATCHES ||
        lifetime.nextToken > 0xffffffff) {
      rejectDropBatch('Too many pending drop transactions');
      return 0;
    }

    var totalBytes = 0;
    for (var i = 0; i < files.length; i++) {
      var fileBytes = Number(files[i].size);
      if (!Number.isSafeInteger(fileBytes) || fileBytes < 0 ||
          fileBytes > MAX_PENDING_DROP_BYTES - totalBytes) {
        rejectDropBatch('Drop transaction exceeds 256 MiB');
        return 0;
      }
      totalBytes += fileBytes;
    }
    if (totalBytes > MAX_PENDING_DROP_BYTES - lifetime.pendingBytes) {
      rejectDropBatch('Pending drop payloads exceed 256 MiB');
      return 0;
    }

    var token = lifetime.nextToken++;
    // JavaScript tasks cannot interleave this check-and-reserve sequence.
    // Publish the reservation before any asynchronous read starts.
    try {
      lifetime.pendingBytes += totalBytes;
      lifetime.batches.set(token, {
        byteLength: totalBytes,
        files: null
      });
    } catch (error) {
      lifetime.batches.delete(token);
      lifetime.pendingBytes -= totalBytes;
      rejectDropBatch('Failed to reserve a drop transaction');
      throw error;
    }
    return token;
  };

  var publishDropBatch = function (lifetime, token, files) {
    if (!lifetime.active) return false;
    var batch = lifetime.batches.get(token >>> 0);
    if (!batch || batch.files !== null) return false;

    var totalBytes = 0;
    for (var i = 0; i < files.length; i++) {
      totalBytes += files[i].bytes.byteLength;
      if (!Number.isSafeInteger(totalBytes) || totalBytes > batch.byteLength) {
        rejectDropBatch('Drop file size changed while it was being read');
        return false;
      }
    }
    if (totalBytes !== batch.byteLength) {
      rejectDropBatch('Drop file size changed while it was being read');
      return false;
    }

    var directory = '/tmp/wx-drop-' + token;
    batch.files = files.map(function (file, index) {
      return {
        bytes: file.bytes,
        path: directory + '/' + index + '-' +
              safeDropFileName(file.name, index),
        materialized: false
      };
    });
    return true;
  };

  globalThis.wxFileDropBatchCount = function (token) {
    var batch = dropLifetime.batches.get(token >>> 0);
    return batch && batch.files !== null ? batch.files.length : -1;
  };

  globalThis.wxFileDropBatchMaterialize = function (token, index) {
    var batch = dropLifetime.batches.get(token >>> 0);
    if (!batch || batch.files === null ||
        index < 0 || index >= batch.files.length) return null;

    var file = batch.files[index];
    if (file.materialized) return file.path;

    var stream = null;
    try {
      var slash = file.path.lastIndexOf('/');
      FS.mkdirTree(file.path.substring(0, slash));
      stream = FS.open(file.path, 'w+');
      FS.write(stream, file.bytes, 0, file.bytes.byteLength);
      file.materialized = true;
      console.log('[DND] Wrote file: ' + file.path +
                  ' (' + file.bytes.byteLength + ' bytes)');
      return file.path;
    } catch (error) {
      console.error('[DND] Failed to write file: ' + file.path + ': ' + error);
      return null;
    } finally {
      if (stream) FS.close(stream);
    }
  };

  var releaseDropBatch = function (lifetime, token) {
    token = token >>> 0;
    var batch = lifetime.batches.get(token);
    if (!batch) return 0;

    // Delete first: re-entrant or duplicate release observes no ownership and
    // cannot subtract this batch twice.
    lifetime.batches.delete(token);
    lifetime.pendingBytes -= batch.byteLength;
    return 1;
  };

  globalThis.wxFileDropBatchRelease = function (token) {
    return releaseDropBatch(dropLifetime, token);
  };

  // Bind Module cleanup and diagnostics to this lifetime. An old Module's
  // shutdown callback must not clear a replacement Module's batch map.
  (function (module, lifetime) {
    module['wxDiscardFileDropBatches'] = function () {
      lifetime.active = false;
      lifetime.batches.clear();
      lifetime.pendingBytes = 0;
    };

    module['wxFileDropPendingBytes'] = function () {
      return lifetime.pendingBytes;
    };
  })(Module, dropLifetime);

  var registerDragDropHandlers = function () {
    var canvas = Module.canvas;
    if (!canvas) {
      console.error('[DND] Module.canvas not available');
      return;
    }

    // Prevent default to enable drop
    ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(function (eventName) {
      canvas.addEventListener(eventName, function (e) {
        e.preventDefault();
        e.stopPropagation();
      }, false);
    });

    canvas.addEventListener('dragenter', function (e) {
      console.log('[DND] dragenter');
    });

    canvas.addEventListener('dragleave', function (e) {
      console.log('[DND] dragleave');
    });

    canvas.addEventListener('drop', function (e) {
      var receiptLifetime = dropLifetime;
      var receiptModule = Module;
      var files = Array.prototype.slice.call(e.dataTransfer.files || []);
      console.log('[DND] drop: ' + files.length + ' files');

      // Get canvas-relative coordinates
      var rect = canvas.getBoundingClientRect();
      var x = e.clientX - rect.left;
      var y = e.clientY - rect.top;

      if (files.length === 0) {
        return;
      }
      if (files.length > MAX_FILES_PER_DROP) {
        console.error('[DND] Drop transaction exceeds 4096 files');
        return;
      }

      // Reserve before reading. Independent accepted transactions still read
      // concurrently; the reservation only bounds retained browser payloads.
      var token = reserveDropBatch(receiptLifetime, files);
      if (!token) return;

      // Read the complete browser transaction without touching native state.
      // File reads cannot be cancelled. Keep the batch reservation until all
      // of them settle, even if an earlier read fails, so a fast rejection
      // cannot let a still-running sibling bypass the retained-byte cap.
      var reads = files.map(function (file) {
        // Start from an already-resolved Promise so a synchronous exception
        // from a non-conforming File-like object becomes a normal rejection.
        return Promise.resolve().then(function () {
          return file.arrayBuffer();
        }).then(function (arrayBuffer) {
          return { name: String(file.name), bytes: new Uint8Array(arrayBuffer) };
        });
      });
      Promise.allSettled(reads).then(function (results) {
        var records = [];
        var readFailed = false;
        var firstFailure;
        for (var i = 0; i < results.length; i++) {
          if (results[i].status === 'rejected') {
            if (!readFailed) firstFailure = results[i].reason;
            readFailed = true;
          } else {
            records.push(results[i].value);
          }
        }
        if (readFailed) throw firstFailure;

        if (!receiptLifetime.active || dropLifetime !== receiptLifetime ||
            Module !== receiptModule || !wxWasmCanTouchNative() ||
            !publishDropBatch(receiptLifetime, token, records)) {
          releaseDropBatch(receiptLifetime, token);
          return;
        }

        try {
          if (wxWasmStageNativeIngress(
            'file-drop transaction receipt', function (ingressReceiptToken) {
              var stage = receiptModule['_wx_file_drop_stage'];
              if (typeof stage !== 'function')
                throw new Error('wx_file_drop_stage export is missing');
              return stage(token, x, y, ingressReceiptToken) | 0;
            })) return;

          releaseDropBatch(receiptLifetime, token);
        } catch (error) {
          releaseDropBatch(receiptLifetime, token);
          console.error('[DND] Native drop staging failed: ' + error);
          throw error;
        }
      }).catch(function (error) {
        releaseDropBatch(receiptLifetime, token);
        console.error('[DND] Error processing drop transaction: ' + error);
      });
    });

    console.log('[DND] Drag and drop handlers registered');
  };
