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
      'button[aria-label*="submit" i]',
      'button[title*="submit" i]',
      '[role="button"][aria-label*="send" i]',
      'button[type="submit"]'
    ],
    stop: [
      'button[aria-label*="stop" i]',
      'button[title*="stop" i]',
      'button[data-testid*="stop" i]'
    ],
    // Where replies live. Code blocks are read from inside these; if none
    // match, the whole document is used.
    // Where replies live. Empty: the whole page (old blocks are told apart
    // by comparing with what was there before sending). Pages often carry
    // an empty role="log" live region, so no guessing here.
    messages: [],
    // Code containers, most specific first. Microsoft 365 renders fenced
    // code as its own "code card" (header, line numbers) rather than <pre>.
    codeBlocks: [
      'pre',
      '.monaco-editor',
      '.cm-editor',
      '[role="code"]',
      '[data-testid*="code" i]',
      '[class*="codeblock" i]',
      '[class*="code-block" i]',
      '[class*="codeBlock"]'
    ]
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

  // Text of a code container, one source line per line.
  function plainText(el) {
    var lines = el.querySelectorAll('.view-line, .cm-line');      // Monaco / CodeMirror
    if (lines.length)
      return Array.prototype.map.call(lines, function (l) { return l.textContent; }).join('\n');
    var code = el.tagName === 'PRE' ? el.querySelector('code') : null;
    return ((code || el).innerText || (code || el).textContent || '').replace(/ /g, ' ');
  }

  // Line numbers glued to the start of lines ("1{", "2  ...") counting up.
  function withoutLineNumbers(text) {
    var expect = 1;
    return text.split('\n').map(function (row) {
      var m = /^\s*(\d+)/.exec(row);
      if (m && +m[1] === expect) { expect++; return row.slice(m[0].length); }
      return row;
    }).join('\n');
  }

  // A gutter made of its own elements: the first element of each line that
  // holds nothing but the next line number. Removed from a copy.
  function withoutGutter(el) {
    var copy = el.cloneNode(true);
    var leaves = Array.prototype.filter.call(copy.querySelectorAll('*'), function (n) {
      return n.children.length === 0;
    });
    var expect = 1;
    leaves.forEach(function (leaf) {
      var t = (leaf.textContent || '').trim();
      if (t !== String(expect)) return;
      var first = leaf.parentNode && leaf.parentNode.firstElementChild === leaf;
      if (!first) return;
      leaf.parentNode.removeChild(leaf);
      expect++;
    });
    return copy.textContent || '';
  }

  function parses(objs) {
    if (!objs.length) return false;
    try { objs.forEach(function (o) { JSON.parse(o); }); return true; } catch (e) { return false; }
  }

  // The blocks a code area contributes: its action objects, each on its
  // own, if any reading of it yields valid ones; otherwise its plain text
  // (ordinary code, or a broken action the parser will report).
  function codeBlocksOf(el) {
    var text = plainText(el);
    var readings = [text, withoutLineNumbers(text), withoutGutter(el)];
    for (var i = 0; i < readings.length; i++) {
      var objs = actionObjects(readings[i]);
      if (parses(objs)) return objs;
    }
    return [text];
  }

  // Top-level {...} objects in text that mention "action", honouring
  // JSON strings. Last resort when no code container holds them.
  function actionObjects(text) {
    var out = [], depth = 0, start = -1, inStr = false, esc = false;
    for (var i = 0; i < text.length; i++) {
      var c = text[i];
      if (inStr) {
        if (esc) esc = false; else if (c === '\\') esc = true; else if (c === '"') inStr = false;
        continue;
      }
      if (c === '"' && depth > 0) inStr = true;
      else if (c === '{') { if (depth++ === 0) start = i; }
      else if (c === '}' && depth > 0) {
        if (--depth === 0) {
          var obj = text.slice(start, i + 1);
          if (/"(action|tool)"\s*:/.test(obj)) out.push(obj);
        }
      }
    }
    return out;
  }

  function squash(t) { return t.replace(/\s+/g, ''); }

  function blocksIn(root, selectors) {
    var out = [];
    var taken = [];
    selectors.forEach(function (sel) {
      var list;
      try { list = root.querySelectorAll(sel); } catch (e) { return; }
      list.forEach(function (el) {
        // One block per code area: skip anything inside, or around, one we
        // already took.
        for (var i = 0; i < taken.length; i++)
          if (taken[i] === el || taken[i].contains(el) || el.contains(taken[i])) return;
        taken.push(el);
        codeBlocksOf(el).forEach(function (b) { out.push(b); });
      });
    });
    // Actions that are not inside any recognised code area (an unknown
    // renderer): pick them out of the page text. Only valid JSON counts
    // here (page text around a code area can garble it), and only actions
    // not already found, compared by content.
    var known = {};
    out.forEach(function (b) {
      try { known[JSON.stringify(JSON.parse(b))] = true; } catch (e) { known['raw:' + squash(b)] = true; }
    });
    actionObjects(root.innerText || '').forEach(function (obj) {
      var canon;
      try { canon = JSON.stringify(JSON.parse(obj)); } catch (e) { return; }
      if (known[canon]) return;
      for (var k in known) if (k.indexOf('raw:') === 0 && k.indexOf(squash(obj)) >= 0) return;
      known[canon] = true;
      out.push(obj);
    });
    return out;
  }

  function messageRoot(overrides) {
    var sels = (overrides && overrides.messages) || defaults.messages;
    if (typeof sels === 'string') sels = [sels];
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
      var input = findLast(cfg('input', overrides));
      var inputText = input ? currentText(input) : '';
      var rootText = root.innerText || '';
      return {
        blocks: blocksIn(root, cfg('codeBlocks', overrides)),
        // What is typed in the input box is not a reply: leave it out, or a
        // prompt that was never sent looks like page activity.
        textLength: rootText.length - (input && root.contains(input) ? (input.innerText || inputText).length : 0),
        busy: !!stopBtn,
        hasInput: !!input,
        // Lets the app see whether the prompt actually left the input box.
        inputHead: inputText.replace(/\s+/g, ' ').trim().slice(0, 200),
        url: location.href
      };
    },

    // For "Copy page details": how the page wraps anything that looks like
    // an action or code, so a missed reply can be diagnosed from a paste.
    describe: function (overrides) {
      function name(el) {
        var s = el.tagName.toLowerCase();
        if (el.id) s += '#' + el.id;
        var cls = (typeof el.className === 'string' ? el.className : '').trim().split(/\s+/).slice(0, 3);
        if (cls[0]) s += '.' + cls.join('.');
        ['role', 'data-testid', 'aria-label'].forEach(function (a) {
          var v = el.getAttribute && el.getAttribute(a);
          if (v) s += '[' + a + '="' + v.slice(0, 40) + '"]';
        });
        return s;
      }
      function path(el) {
        var parts = [];
        for (var p = el; p && p !== document.body && parts.length < 10; p = p.parentElement) parts.unshift(name(p));
        return parts.join(' > ');
      }
      var lines = ['url: ' + location.href, 'ua: ' + navigator.userAgent];
      var input = findLast(cfg('input', overrides));
      lines.push('input: ' + (input ? path(input) : 'NOT FOUND'));
      var send = findLast(cfg('send', overrides));
      lines.push('send button: ' + (send ? path(send) : 'not found'));
      var stop = findLast(cfg('stop', overrides));
      lines.push('stop button: ' + (stop ? path(stop) : 'not found (fine when idle)'));
      var root = messageRoot(overrides);
      lines.push('blocks found: ' + blocksIn(root, cfg('codeBlocks', overrides)).length);
      // The deepest elements whose text mentions "action": the last 5.
      var hits = [];
      var all = document.body.querySelectorAll('*');
      for (var i = 0; i < all.length; i++) {
        var el = all[i];
        if (el.children.length > 40) continue;
        var t = el.textContent || '';
        if (t.indexOf('"action"') < 0) continue;
        var deeper = false;
        for (var j = 0; j < el.children.length; j++)
          if ((el.children[j].textContent || '').indexOf('"action"') >= 0) { deeper = true; break; }
        if (!deeper) hits.push(el);
      }
      hits.slice(-5).forEach(function (el, i) {
        var box = el;
        for (var up = 0; up < 4 && box.parentElement; up++) box = box.parentElement;
        lines.push('', '--- match ' + (i + 1) + ': ' + path(el));
        lines.push(box.outerHTML.replace(/\s+/g, ' ').slice(0, 1500));
      });
      return { ok: true, text: lines.join('\n') };
    },

    // Focus the input with the caret at the end, ready for a real key press
    // from the app (pages ignore synthetic key events from scripts).
    focusInput: function (overrides) {
      var el = findLast(cfg('input', overrides));
      if (!el) return { ok: false, error: 'input box not found' };
      el.focus();
      if (!isTextField(el)) {
        var range = document.createRange();
        range.selectNodeContents(el);
        range.collapse(false);
        var sel = window.getSelection();
        sel.removeAllRanges();
        sel.addRange(range);
      } else {
        el.selectionStart = el.selectionEnd = el.value.length;
      }
      return { ok: true };
    },

    // Empty the input box and focus it, so the app can type the prompt in
    // with real (trusted) text input.
    prepareInput: function (overrides) {
      var el = findLast(cfg('input', overrides));
      if (!el) return { ok: false, error: 'chat input box not found (are you signed in?)' };
      el.scrollIntoView({ block: 'center' });
      if (isTextField(el)) setTextField(el, ''); else clearEditable(el);
      el.focus();
      return { ok: true };
    },

    // Last resort when the app cannot send a real key press.
    syntheticEnter: function (overrides) {
      var el = findLast(cfg('input', overrides));
      if (!el) return { ok: false, error: 'input box gone before submit' };
      el.focus();
      var opts = { key: 'Enter', code: 'Enter', keyCode: 13, which: 13, bubbles: true, cancelable: true };
      el.dispatchEvent(new KeyboardEvent('keydown', opts));
      el.dispatchEvent(new KeyboardEvent('keypress', opts));
      el.dispatchEvent(new KeyboardEvent('keyup', opts));
      return { ok: true, method: 'synthetic-enter' };
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

    // Clicks the send button if one can be found. Whether that worked is
    // checked afterwards by looking at the input box.
    submit: function (overrides) {
      var btn = findLast(cfg('send', overrides));
      if (btn && !btn.disabled && btn.getAttribute('aria-disabled') !== 'true') {
        btn.click();
        return { ok: true, method: 'button' };
      }
      return { ok: true, method: 'none' };
    }
  };
})();
