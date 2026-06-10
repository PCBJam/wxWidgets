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
function wxElementRegister(id, label, name, typeName, screenX, screenY, width, height, parentId, visible, enabled) {
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
      lastUpdated: Date.now()
    });
  }
}

function wxElementUpdate(id, label, name, typeName, screenX, screenY, width, height, parentId, visible, enabled) {
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
      context: null
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

      canvas.width = width * scaleFactor;
      canvas.height = height * scaleFactor;
      canvas.style.width = width + 'px';
      canvas.style.height = height + 'px';

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

  var createBitmap = function (x, y, width, height, data, scaleFactor) {
    //console.log('setWindowImageData: ' + id + ': ' + '(' + x + ', ' + y + ') ' + width + 'x' + height);

    var id = nextBitmapId++;    
    setBitmapData(id, x, y, width, height, data, scaleFactor);

    return id;
  };

  var destroyBitmap = function (id) {
    bitmapMap.delete(id);
  };

  var getBitmapData = function (id, data) {
    var bitmap = bitmapMap.get(id);

    var imageData;

    if (bitmap.context) {
      imageData = bitmap.context.getImageData(0, 0, bitmap.width, bitmap.height);
      bitmap.context = null;
    } else {
      imageData = bitmap.imageData;
    }

    bitmap.imageBitmap = null;

    Module.HEAPU8.set(imageData.data, data);
  };

  var setBitmapData = function (id, width, height, data, scaleFactor) {
    var size = 4 * width * height;
    var array = new Uint8ClampedArray(Module.HEAPU8.buffer, data, size);
    var imageData = new ImageData(width, height);  
    imageData.data.set(array);

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

    createImageBitmap(imageData, 0, 0, width, height).then(function (imageBitmap) {
      // TODO: fix race condition
      var bitmap = bitmapMap.get(id);
      if (bitmap && !bitmap.context) {
        bitmap.imageBitmap = imageBitmap;
      }
    })
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

  var createGLCanvas = function (isVisible) {
    var id = nextGLCanvasId++;
    var canvas = document.createElement('canvas');
    canvas.id = 'glcanvas-' + id;
    canvas.className = 'gl-canvas';
    canvas.style.position = 'absolute';
    canvas.style.display = 'none';  // Always start hidden until properly positioned
    canvas.style.zIndex = '100';  // Above 2D canvas
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
    canvas.width = width * scaleFactor;
    canvas.height = height * scaleFactor;

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

  // Patch Emscripten's GL.newRenderingFrameStarted to handle contexts without temp buffers
  // This is needed because wxGLCanvas creates additional WebGL contexts that don't have
  // the temp buffers initialized (those are only set up during GLImmediate.init for the main context)
  var patchGLNewRenderingFrameStarted = function () {
    if (typeof GL === 'undefined' || !GL.newRenderingFrameStarted) {
      return; // GL not initialized yet
    }
    if (GL._wxPatched) {
      return; // Already patched
    }
    var originalNewRenderingFrameStarted = GL.newRenderingFrameStarted;
    GL.newRenderingFrameStarted = function () {
      if (!GL.currentContext) {
        return;
      }
      // Skip temp buffer operations if they haven't been initialized for this context
      if (!GL.currentContext.tempVertexBuffers1 || !GL.currentContext.tempVertexBufferCounters1) {
        return;
      }
      return originalNewRenderingFrameStarted.call(this);
    };
    GL._wxPatched = true;
  };

  // Try to patch GL immediately and also set up a delayed check
  // (GL object is created after wx.js runs)
  if (typeof GL !== 'undefined') {
    patchGLNewRenderingFrameStarted();
  }
  // Check periodically until patched (GL is created during Module initialization)
  var glPatchInterval = setInterval(function () {
    if (typeof GL !== 'undefined') {
      patchGLNewRenderingFrameStarted();
      if (GL._wxPatched) {
        clearInterval(glPatchInterval);
      }
    }
  }, 10);
  // Clear interval after 5 seconds to avoid memory leak if GL never gets created
  setTimeout(function () {
    clearInterval(glPatchInterval);
  }, 5000);

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

    // FIX: If clip region is empty (0,0,0,0), use full context dimensions
    // This happens when wxWidgets hasn't properly initialized the clip region
    if (width <= 0 || height <= 0) {
      x = 0;
      y = 0;
      width = ctx.width;
      height = ctx.height;
    }

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

  var showFileDialog = function (multiple) {
    var input = document.createElement('input');
    if (multiple) {
      input.setAttribute('multiple', '');
    }
    input.type = 'file';
    input.onchange = function () {
      for (var i = 0; i < input.files.length; i++) {
        var file = input.files[i];
        console.log('file selected: ' + file.name);
        file.arrayBuffer().then(function (arrayBuffer) {
          var array = new Uint8Array(arrayBuffer);
          var path = '/tmp/' + file.name;

          var stream = FS.open(path, 'w+');
          var retCode = 0;

          if (stream) {
            FS.write(stream, array, 0, file.size);
            FS.close(stream);
          } else {
            retCode = 1;
          }

          ccall('OpenFileCallback', 'void', ['string', 'number'], [path, retCode]);
        });
      }
    };
    input.click();
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

  var endModal = null;
/*
  var startModal = async function () {
    Asyncify.handleAsync(async () => {
      console.log('startModal');
      const result = await new Promise((resolve, reject) =>  {
        endModal = resolve;
      });
      console.log('modal result: ' + result);
    });
  };
 */

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

  var pendingDropFiles = [];
  var pendingDropX = 0;
  var pendingDropY = 0;

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
      ccall('OnDragEnter', 'void', ['number', 'number'], [e.clientX, e.clientY]);
    });

    canvas.addEventListener('dragleave', function (e) {
      console.log('[DND] dragleave');
      ccall('OnDragLeave', 'void', [], []);
    });

    canvas.addEventListener('drop', function (e) {
      var files = e.dataTransfer.files;
      console.log('[DND] drop: ' + files.length + ' files');

      // Get canvas-relative coordinates
      var rect = canvas.getBoundingClientRect();
      var x = e.clientX - rect.left;
      var y = e.clientY - rect.top;

      pendingDropFiles = [];
      pendingDropX = x;
      pendingDropY = y;

      if (files.length === 0) {
        return;
      }

      // Process all files, then notify C++ when all are ready
      var processedCount = 0;

      for (var i = 0; i < files.length; i++) {
        (function (file) {
          file.arrayBuffer().then(function (arrayBuffer) {
            var array = new Uint8Array(arrayBuffer);
            var path = '/tmp/' + file.name;

            // Write to WASM filesystem
            var stream = FS.open(path, 'w+');
            if (stream) {
              FS.write(stream, array, 0, file.size);
              FS.close(stream);
              pendingDropFiles.push(path);
              console.log('[DND] Wrote file: ' + path + ' (' + file.size + ' bytes)');
            } else {
              console.error('[DND] Failed to write file: ' + path);
            }

            processedCount++;
            if (processedCount === files.length) {
              // All files processed, notify C++
              notifyDropComplete();
            }
          }).catch(function (error) {
            console.error('[DND] Error reading file: ' + error);
            processedCount++;
            if (processedCount === files.length) {
              notifyDropComplete();
            }
          });
        })(files[i]);
      }
    });

    console.log('[DND] Drag and drop handlers registered');
  };

  var notifyDropComplete = function () {
    if (pendingDropFiles.length === 0) {
      return;
    }

    // Notify C++ for each file
    for (var i = 0; i < pendingDropFiles.length; i++) {
      ccall('OnFileDropped', 'void',
            ['string', 'number', 'number'],
            [pendingDropFiles[i], pendingDropX, pendingDropY]);
    }

    // Clear pending files
    pendingDropFiles = [];
  };

