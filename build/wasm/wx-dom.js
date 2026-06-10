// wx-dom.js — shim additions for the WASM DOM port (non-universal build).
//
// Loaded as a second --pre-js AFTER wx.js, only in DOM-port bundles.
// wx.js keeps owning window management, the element registry, clipboard,
// file dialogs and the Canvas2D drawing used by owner-drawn widgets
// (canvas islands). This file will own native-control DOM elements:
// createControl / property setters / measurement / event wiring.
//
// Phase 1: marker + scaffolding only; controls are still logical stubs.

if (typeof window !== 'undefined') {
    // Lets tests and boot code detect which port a bundle was built for.
    window.wxDomPort = true;

    // domId -> HTMLElement for native controls (populated from Phase 2 on).
    window.wxDomControls = new Map();
}
