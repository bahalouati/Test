// Injected into the Copilot page by CopilotBridge. Runs in an isolated JS
// world: it shares the DOM with the page but none of its scripts, so the page
// cannot see or tamper with it, and it does not depend on the page's globals.
//
// Microsoft changes this page's markup without notice, so every element is
// found through a list of selectors that the app's settings can override,
// tried in order, falling back to generic heuristics.
var __copilotAgent = (function () {
  'use strict';

  var defaults = {
    input: [
      '#m365-chat-editor-target-element',
      '[data-lexical-editor="true"][contenteditable="true"]',
      'textarea[aria-label*="message" i]',
      'textarea[placeholder*="message" i]',
      '[role="textbox"][contenteditable="true"]',
      'textarea',
      '[contenteditable="true"]'
    ],
    send: [
      'button[aria-label="Send"]',
      'button[aria-label*="send" i]',
      'button[title*="send" i]',
      'button[data-testid*="send" i]',
      'button[type="submit"]'
    ],
    stop: [
      'button[aria-label*="stop" i]',
      'button[title*="stop" i]',
      'button[data-testid*="stop" i]'
    ],
    // Where replies live. Code blocks are read from inside these; if none
    // match, the whole document is used.
    messages: [
      '[data-testid="chat-message-list"]',
      '[role="log"]',
      'main'
    ],
    codeBlocks: ['pre']
  };

  function cfg(key, overrides) {
    var o = overrides && overrides[key];
    if (typeof o === 'string') o = [o];
    return (o && o.length) ? o.concat(defaults[key]) : defaults[key];
  }

  function visible(el) {
    if (!el || !el.getBoundingClientRect) return false;
    var r = el.getBoundingClientRect();
    if (r.width <= 0 || r.height <= 0) return false;
    var s = window.getComputedStyle(el);
    return s.visibility !== 'hidden' && s.display !== 'none';
  }

  function findLast(selectors) {
    for (var i = 0; i < selectors.length; i++) {
      var list;
      try { list = document.querySelectorAll(selectors[i]); } catch (e) { continue; }
      for (var j = list.length - 1; j >= 0; j--)
        if (visible(list[j])) return list[j];
    }
    return null;
  }

  function isTextField(el) {
    return el.tagName === 'TEXTAREA' || el.tagName === 'INPUT';
  }

  function currentText(el) {
    return isTextField(el) ? el.value : (el.innerText || el.textContent || '');
  }

  function setTextField(el, text) {
    // Go through the native setter so React-style frameworks notice.
    var proto = el.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    var setter = Object.getOwnPropertyDescriptor(proto, 'value').set;
    setter.call(el, text);
    el.dispatchEvent(new Event('input', { bubbles: true }));
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }

  function clearEditable(el) {
    el.focus();
    var sel = window.getSelection();
    var range = document.createRange();
    range.selectNodeContents(el);
    sel.removeAllRanges();
    sel.addRange(range);
    document.execCommand('delete', false, null);
  }

  function setEditable(el, text) {
    clearEditable(el);
    // Rich editors (Lexical, Draft, ProseMirror) handle a paste of plain text
    // as one edit and keep newlines; typing newlines could submit early.
    try {
      var dt = new DataTransfer();
      dt.setData('text/plain', text);
      el.dispatchEvent(new ClipboardEvent('paste', { clipboardData: dt, bubbles: true, cancelable: true }));
    } catch (e) { /* fall through */ }
    if (currentText(el).trim().length < Math.min(20, text.trim().length)) {
      clearEditable(el);
      document.execCommand('insertText', false, text);
    }
    el.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'insertFromPaste' }));
  }

  function blocksIn(root, selectors) {
    var out = [];
    var seen = new Set();
    selectors.forEach(function (sel) {
      var list;
      try { list = root.querySelectorAll(sel); } catch (e) { return; }
      list.forEach(function (pre) {
        if (seen.has(pre)) return;
        // Skip blocks nested in an outer block we already took.
        for (var p = pre.parentElement; p; p = p.parentElement) if (seen.has(p)) return;
        seen.add(pre);
        var code = pre.querySelector('code');
        out.push(((code || pre).textContent || '').replace(/ /g, ' '));
      });
    });
    return out;
  }

  function messageRoot(overrides) {
    var sels = cfg('messages', overrides);
    for (var i = 0; i < sels.length; i++) {
      var el = null;
      try { el = document.querySelector(sels[i]); } catch (e) { }
      if (el) return el;
    }
    return document.body;
  }

  return {
    // What the page looks like now; polled while waiting for a reply.
    snapshot: function (overrides) {
      var root = messageRoot(overrides);
      var stopBtn = findLast(cfg('stop', overrides));
      return {
        blocks: blocksIn(root, cfg('codeBlocks', overrides)),
        textLength: (root.innerText || '').length,
        busy: !!stopBtn,
        hasInput: !!findLast(cfg('input', overrides)),
        url: location.href
      };
    },

    fill: function (text, overrides) {
      var el = findLast(cfg('input', overrides));
      if (!el) return { ok: false, error: 'chat input box not found (are you signed in?)' };
      el.scrollIntoView({ block: 'center' });
      el.focus();
      if (isTextField(el)) setTextField(el, text); else setEditable(el, text);
      var got = currentText(el).trim().length;
      if (got === 0) return { ok: false, error: 'the input box did not accept the text' };
      return { ok: true, length: got, tag: el.tagName };
    },

    submit: function (overrides) {
      var btn = findLast(cfg('send', overrides));
      if (btn && !btn.disabled && btn.getAttribute('aria-disabled') !== 'true') {
        btn.click();
        return { ok: true, method: 'button' };
      }
      var el = findLast(cfg('input', overrides));
      if (!el) return { ok: false, error: 'input box gone before submit' };
      el.focus();
      var opts = { key: 'Enter', code: 'Enter', keyCode: 13, which: 13, bubbles: true, cancelable: true };
      el.dispatchEvent(new KeyboardEvent('keydown', opts));
      el.dispatchEvent(new KeyboardEvent('keypress', opts));
      el.dispatchEvent(new KeyboardEvent('keyup', opts));
      return { ok: true, method: 'enter' };
    }
  };
})();
