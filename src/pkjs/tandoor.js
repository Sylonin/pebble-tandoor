// Tandoor's phone side without Pebble or XHR: server addresses, list requests and records, recipe
// flattening, HTML to text, timers read from text, packing and error wording. Node loads it in the
// tests (test/tandoor.test.cjs); index.js does the networking and the messages.
//
// The SDK bundles PKJS with webpack 1, whose parser takes ES2015 only: no async/await, no `**`, no
// object spread, no `catch {}` without a binding, no trailing commas in calls.

// Shared with the watch (src/c/comm.h, src/c/model.h) and docs/design.md.
var KIND = { PLAN: 1, RECENT: 2, ALL: 3, BOOKS: 4, BOOK: 5, KEYWORDS: 6, KEYWORD: 7, SEARCH: 8 };
var ERR = { NOT_SET_UP: 1, UNREACHABLE: 2, TOKEN: 3, NOT_FOUND: 4, SERVER: 5 };
var INGREDIENT_HEADER = 1;
var INGREDIENT_NO_AMOUNT = 2;
var STEP_FROM_TEXT = 1;

var PAGE_SIZE = 20;
// Bytes of content. The watch's buffers hold one more for the terminator (docs/design.md, Limits).
var LIMIT = {
  title: 63,
  subtitle: 47,
  name: 63,
  description: 255,
  servingsText: 23,
  keywords: 63,
  stepName: 47,
  stepText: 1799,
  unit: 23,
  food: 47,
  note: 47,
  host: 47,
  message: 95,
  records: 200,
  steps: 40,
  ingredients: 80,
  data: 1400
};
// The longest countdown the watch shows is 99:59:59.
var MAX_TIMER_SECONDS = 359999;
// Amounts travel ×1000 in an int32 on the watch.
var MAX_AMOUNT = 2000000;
var PLAN_DAYS = 7;
var DEFAULT_SETTINGS = { backlight: 1, sound: 1 };

var DOT = ' \u00b7 ';
var ELLIPSIS = '\u2026';
var BULLET = '\u2022 ';
var DAY_NAMES = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];

// Values ------------------------------------------------------------------------------------------

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function asArray(value) {
  return Array.isArray(value) ? value : [];
}

function toStr(value) {
  return value === null || value === undefined ? '' : String(value);
}

// Tandoor sends numbers, but a decimal string or a comma decimal must not break a list.
function toNumber(value) {
  var n = typeof value === 'number' ? value : parseFloat(toStr(value).replace(',', '.'));
  return Number.isFinite(n) ? n : 0;
}

function toId(value) {
  var n = Math.floor(toNumber(value));
  return n > 0 ? n : 0;
}

function clamp(value, low, high) {
  return Math.max(low, Math.min(high, value));
}

function repeatString(s, count) {
  var out = '';
  for (var i = 0; i < count; i++) out += s;
  return out;
}

function formatNumber(value) {
  return String(Math.round(value * 100) / 100);
}

// Strings -----------------------------------------------------------------------------------------

// The longest prefix of s within budget UTF-8 bytes, on a character boundary. A lone surrogate
// goes out as U+FFFD, which is what the phone's UTF-8 encoder sends.
function takeUtf8(s, budget) {
  var out = '';
  var bytes = 0;
  var i = 0;
  while (i < s.length) {
    var c = s.charCodeAt(i);
    var units = 1;
    var size;
    var piece;
    if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length && (s.charCodeAt(i + 1) & 0xfc00) === 0xdc00) {
      units = 2;
      size = 4;
      piece = s.substr(i, 2);
    } else if (c >= 0xd800 && c <= 0xdfff) {
      size = 3;
      piece = '\ufffd';
    } else {
      size = c < 0x80 ? 1 : (c < 0x800 ? 2 : 3);
      piece = s.charAt(i);
    }
    if (bytes + size > budget) return { text: out, cut: true };
    out += piece;
    bytes += size;
    i += units;
  }
  return { text: out, cut: false };
}

function utf8Length(s) {
  var bytes = 0;
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    if (c < 0x80) {
      bytes += 1;
    } else if (c < 0x800) {
      bytes += 2;
    } else if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length && (s.charCodeAt(i + 1) & 0xfc00) === 0xdc00) {
      bytes += 4;
      i++;
    } else {
      bytes += 3;
    }
  }
  return bytes;
}

// At most maxBytes of UTF-8 on a character boundary. A cut string ends in an ellipsis, so it
// doesn't read as a typo.
function truncateUtf8(value, maxBytes) {
  var s = toStr(value);
  var whole = takeUtf8(s, maxBytes);
  if (!whole.cut) return whole.text;
  if (maxBytes < 4) return whole.text;
  return takeUtf8(s, maxBytes - 3).text.replace(/\s+$/, '') + ELLIPSIS;
}

// Characters that draw nothing, or that break layout: soft hyphen, zero width and direction marks,
// variation selectors, the byte order mark.
var INVISIBLE = /\u00ad|[\u200b-\u200f]|[\u202a-\u202e]|[\u2060-\u2064]|[\ufe00-\ufe0f]|\ufeff/g;

// Drops C0 and C1 control characters. A loop, because a regex over the control range trips linters.
function stripControls(s, keepNewline) {
  var out = '';
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    var control = c < 0x20 || (c >= 0x7f && c <= 0x9f);
    if (!control || (keepNewline && c === 0x0a)) out += s.charAt(i);
  }
  return out;
}

var LATIN1_NAMES = (
  'nbsp iexcl cent pound curren yen brvbar sect uml copy ordf laquo not shy reg macr deg plusmn sup2 sup3 acute ' +
  'micro para middot cedil sup1 ordm raquo frac14 frac12 frac34 iquest Agrave Aacute Acirc Atilde Auml Aring ' +
  'AElig Ccedil Egrave Eacute Ecirc Euml Igrave Iacute Icirc Iuml ETH Ntilde Ograve Oacute Ocirc Otilde Ouml ' +
  'times Oslash Ugrave Uacute Ucirc Uuml Yacute THORN szlig agrave aacute acirc atilde auml aring aelig ccedil ' +
  'egrave eacute ecirc euml igrave iacute icirc iuml eth ntilde ograve oacute ocirc otilde ouml divide oslash ' +
  'ugrave uacute ucirc uuml yacute thorn yuml'
).split(' ');

var OTHER_ENTITIES = {
  quot: 34, amp: 38, apos: 39, lt: 60, gt: 62, QUOT: 34, AMP: 38, LT: 60, GT: 62,
  OElig: 338, oelig: 339, Scaron: 352, scaron: 353, Yuml: 376, circ: 710, tilde: 732,
  ensp: 8194, emsp: 8195, thinsp: 8201, zwnj: 8204, zwj: 8205, lrm: 8206, rlm: 8207,
  ndash: 8211, mdash: 8212, lsquo: 8216, rsquo: 8217, sbquo: 8218, ldquo: 8220, rdquo: 8221, bdquo: 8222,
  dagger: 8224, Dagger: 8225, bull: 8226, hellip: 8230, permil: 8240, prime: 8242, Prime: 8243,
  lsaquo: 8249, rsaquo: 8250, euro: 8364, trade: 8482, larr: 8592, uarr: 8593, rarr: 8594, darr: 8595,
  frac13: 8531, frac23: 8532, frac18: 8539, frac38: 8540, frac58: 8541, frac78: 8542,
  minus: 8722, asymp: 8776, ne: 8800, le: 8804, ge: 8805
};

var ENTITIES = (function () {
  var map = Object.create(null);
  LATIN1_NAMES.forEach(function (name, i) {
    map[name] = String.fromCharCode(160 + i);
  });
  Object.keys(OTHER_ENTITIES).forEach(function (name) {
    map[name] = String.fromCharCode(OTHER_ENTITIES[name]);
  });
  return map;
})();

function fromCodePoint(cp) {
  if (!(cp > 0 && cp <= 0x10ffff) || (cp >= 0xd800 && cp <= 0xdfff)) return '';
  if (cp <= 0xffff) return String.fromCharCode(cp);
  var v = cp - 0x10000;
  return String.fromCharCode(0xd800 + (v >> 10), 0xdc00 + (v & 0x3ff));
}

// Named and numeric entities. Unknown names stay as they are.
function decodeEntities(value) {
  var s = toStr(value);
  if (s.indexOf('&') === -1) return s;
  return s.replace(/&(#[xX][0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]*);/g, function (whole, body) {
    if (body.charAt(0) === '#') {
      var hex = body.charAt(1) === 'x' || body.charAt(1) === 'X';
      return fromCodePoint(parseInt(body.slice(hex ? 2 : 1), hex ? 16 : 10));
    }
    return body in ENTITIES ? ENTITIES[body] : whole;
  });
}

// One line for a title or a record field: entities decoded, any whitespace or control run
// one space, cut to maxBytes.
function cleanLine(value, maxBytes) {
  var s = decodeEntities(toStr(value)).replace(INVISIBLE, '').replace(/\s+/g, ' ');
  return truncateUtf8(stripControls(s, false).replace(/ {2,}/g, ' ').replace(/^ | $/g, ''), maxBytes);
}

// Text from HTML ----------------------------------------------------------------------------------

function finishText(s) {
  return stripControls(s.replace(INVISIBLE, ''), true)
    .replace(/ +\n/g, '\n')
    .replace(/\n{3,}/g, '\n\n')
    .replace(/^\s+|\s+$/g, '');
}

function plainToText(src) {
  var lines = decodeEntities(src).split('\n').map(function (line) {
    return line.replace(/\s+/g, ' ').replace(/^ | $/g, '');
  });
  return finishText(lines.join('\n'));
}

// The index of the ">" that ends a tag, skipping quoted attribute values; -1 when there is none.
function tagEnd(src, from) {
  var quote = '';
  for (var j = from; j < src.length; j++) {
    var c = src.charAt(j);
    if (quote) {
      if (c === quote) quote = '';
    } else if (c === '"' || c === "'") {
      quote = c;
    } else if (c === '>') {
      return j;
    }
  }
  return -1;
}

var BLOCK_TAGS = {
  p: 2, h1: 2, h2: 2, h3: 2, h4: 2, h5: 2, h6: 2, blockquote: 2, pre: 2, table: 2, hr: 2,
  div: 1, section: 1, article: 1, header: 1, footer: 1, figure: 1, figcaption: 1, tr: 1,
  dl: 1, dt: 1, dd: 1, address: 1
};

// Tandoor renders a step's instructions to HTML. The watch gets plain text: blocks are separated by
// blank lines, <br> breaks the line, list items start with "• " (numbers in an <ol>), tags go and
// entities are decoded.
function htmlToText(html) {
  var src = toStr(html).replace(/\r\n?/g, '\n');
  if (src.indexOf('<') === -1) return plainToText(src);

  var out = '';
  var owed = 0; // line breaks to write before the next text
  var lists = []; // open lists, innermost last
  var skipping = ''; // script or style, whose content isn't text
  var atBullet = false; // nothing written since a list item's marker

  function owe(count) {
    if (count > owed) owed = count;
  }

  function write(s, raw) {
    if (owed > 0) {
      if (out !== '') out = out.replace(/ +$/, '') + repeatString('\n', owed);
      owed = 0;
    }
    if (!raw) {
      var last = out.charAt(out.length - 1);
      if (out === '' || last === '\n' || last === ' ') s = s.replace(/^ +/, '');
    }
    out += s;
  }

  function text(raw) {
    var s = decodeEntities(raw).replace(/\s+/g, ' ');
    if (s === '') return;
    if (s === ' ' && (owed > 0 || out === '' || /\s$/.test(out))) return;
    write(s, false);
    atBullet = false;
  }

  function tag(name, closing) {
    var block = BLOCK_TAGS[name];
    if (name === 'script' || name === 'style') {
      if (!closing) skipping = name;
    } else if (name === 'br') {
      owed = Math.min(2, owed + 1);
    } else if (name === 'ul' || name === 'ol') {
      if (closing) {
        lists.pop();
        owe(lists.length === 0 ? 2 : 1);
      } else {
        lists.push({ ordered: name === 'ol', count: 0 });
        owe(1);
      }
    } else if (name === 'li') {
      owe(1);
      if (!closing) {
        var top = lists[lists.length - 1];
        var mark = BULLET;
        if (top && top.ordered) {
          top.count++;
          mark = top.count + '. ';
        }
        write(repeatString('  ', Math.max(0, lists.length - 1)) + mark, true);
        atBullet = true;
      }
    } else if (block) {
      // A loose list wraps item text in <p>: keep it on the marker's line and the items tight.
      if (!closing && atBullet) return;
      owe(lists.length > 0 ? 1 : block);
    } else if ((name === 'td' || name === 'th') && !closing) {
      text(' ');
    }
  }

  var i = 0;
  while (i < src.length) {
    var lt = src.indexOf('<', i);
    if (lt === -1) {
      if (!skipping) text(src.slice(i));
      break;
    }
    if (lt > i && !skipping) text(src.slice(i, lt));
    if (src.substr(lt, 4) === '<!--') {
      var closeComment = src.indexOf('-->', lt + 4);
      i = closeComment === -1 ? src.length : closeComment + 3;
      continue;
    }
    var next = src.charAt(lt + 1);
    if (next === '!' || next === '?') {
      var closeDecl = src.indexOf('>', lt);
      i = closeDecl === -1 ? src.length : closeDecl + 1;
      continue;
    }
    var m = /^<(\/?)([a-zA-Z][a-zA-Z0-9]*)/.exec(src.substr(lt, 40));
    var end = m ? tagEnd(src, lt + m[0].length) : -1;
    if (end === -1) {
      // A lone "<" in the text.
      if (!skipping) text('<');
      i = lt + 1;
      continue;
    }
    var name = m[2].toLowerCase();
    i = end + 1;
    if (skipping) {
      if (m[1] === '/' && name === skipping) skipping = '';
    } else {
      tag(name, m[1] === '/');
    }
  }
  return finishText(out);
}

// Durations in text -------------------------------------------------------------------------------

var NUMBER_WORDS = {
  one: 1, two: 2, three: 3, four: 4, five: 5, six: 6, seven: 7, eight: 8, nine: 9, ten: 10, eleven: 11,
  twelve: 12, fifteen: 15, twenty: 20, thirty: 30, forty: 40, fifty: 50, sixty: 60, ninety: 90
};
var TENS_WORDS = { twenty: 20, thirty: 30, forty: 40, fifty: 50, sixty: 60 };
var VULGAR = {
  '\u00bc': 0.25, '\u00bd': 0.5, '\u00be': 0.75, '\u2153': 1 / 3, '\u2154': 2 / 3, '\u2155': 0.2,
  '\u2156': 0.4, '\u2157': 0.6, '\u2158': 0.8, '\u2159': 1 / 6, '\u215a': 5 / 6, '\u215b': 0.125,
  '\u215c': 0.375, '\u215d': 0.625, '\u215e': 0.875
};
var VULGAR_CLASS = '[\\u00bc-\\u00be\\u2150-\\u215e]';
var QUANTITY =
  '(?:(?:twenty|thirty|forty|fifty|sixty)[- ](?:one|two|three|four|five|six|seven|eight|nine)' +
  '|one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|fifteen|twenty|thirty|forty|fifty|sixty|ninety' +
  '|\\d+[ \\u00a0]+\\d+\\/\\d+' +
  '|\\d+\\s*\\/\\s*\\d+' +
  '|\\d+(?:[.,]\\d+)?' + VULGAR_CLASS + '?' +
  '|' + VULGAR_CLASS + ')';
var UNIT = '(hours?|hrs?|h|minutes?|mins?|seconds?|secs?)(?![a-z0-9])';
// "20 min", "1-2 hours", "5-minute"; the lower end of a range counts.
var DURATION = new RegExp(
  '(^|[^a-z0-9])(' + QUANTITY + ')(?:\\s*(?:-|\\u2013|\\u2014|to|or)\\s*(?:' + QUANTITY + '))?\\s*-?\\s*' + UNIT,
  'gi'
);
var FOLLOWING = new RegExp('^\\s*(?:,|and|&)?\\s*(' + QUANTITY + ')\\s*-?\\s*' + UNIT, 'i');
var PHRASES = [
  { pattern: /(^|[^a-z])(an hour and a half)(?![a-z])/i, seconds: 5400 },
  { pattern: /(^|[^a-z])(half an hour|half hour)(?![a-z])/i, seconds: 1800 },
  { pattern: /(^|[^a-z])(an hour)(?![a-z])/i, seconds: 3600 }
];

function unitSeconds(unit) {
  var c = unit.charAt(0).toLowerCase();
  if (c === 'h') return 3600;
  return unit.slice(0, 2).toLowerCase() === 'mi' ? 60 : 1;
}

// The number a quantity token spells: 20, 1.5, 1/2, 1 1/2, 1½, ½, "twenty-five", "two".
function quantity(token) {
  var t = token.toLowerCase().replace(/\u00a0/g, ' ').trim();
  var m;
  if (t in NUMBER_WORDS) return NUMBER_WORDS[t];
  m = /^(twenty|thirty|forty|fifty|sixty)[- ](\w+)$/.exec(t);
  if (m) return TENS_WORDS[m[1]] + (NUMBER_WORDS[m[2]] || 0);
  m = /^(\d+)\s+(\d+)\s*\/\s*(\d+)$/.exec(t);
  if (m) return +m[3] > 0 ? +m[1] + +m[2] / +m[3] : 0;
  m = /^(\d+)\s*\/\s*(\d+)$/.exec(t);
  if (m) return +m[2] > 0 ? +m[1] / +m[2] : 0;
  m = new RegExp('^(\\d+(?:[.,]\\d+)?)(' + VULGAR_CLASS + ')?$').exec(t);
  if (m) return parseFloat(m[1].replace(',', '.')) + (m[2] ? VULGAR[m[2]] : 0);
  return t in VULGAR ? VULGAR[t] : 0;
}

// The first duration in a step's text, in seconds, or 0. "1 hour 30 minutes" counts whole; of a range
// like "20-25 minutes" the lower end counts.
function findDuration(value) {
  var text = toStr(value);
  var best = null;
  DURATION.lastIndex = 0;
  for (var m = DURATION.exec(text); m !== null; m = DURATION.exec(text)) {
    var amount = quantity(m[2]);
    if (amount <= 0) continue;
    var unit = unitSeconds(m[3]);
    var seconds = amount * unit;
    var end = m.index + m[0].length;
    for (var i = 0; i < 2; i++) {
      var more = FOLLOWING.exec(text.slice(end));
      if (!more || unitSeconds(more[2]) >= unit) break;
      unit = unitSeconds(more[2]);
      seconds += quantity(more[1]) * unit;
      end += more[0].length;
    }
    best = { at: m.index + m[1].length, seconds: seconds };
    break;
  }
  for (var p = 0; p < PHRASES.length; p++) {
    var phrase = PHRASES[p].pattern.exec(text);
    if (phrase && (!best || phrase.index + phrase[1].length < best.at)) {
      best = { at: phrase.index + phrase[1].length, seconds: PHRASES[p].seconds };
    }
  }
  return best ? clamp(Math.round(best.seconds), 0, MAX_TIMER_SECONDS) : 0;
}

// Server address ----------------------------------------------------------------------------------

// What a user types or pastes becomes "scheme://host[:port][/prefix]" without a trailing slash, or
// '' when it can't be an address. "/api" and what follows it are dropped: people paste the API URL.
function normalizeServer(input) {
  var s = toStr(input).trim();
  if (s === '' || /\s/.test(s)) return '';
  if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(s)) s = 'https://' + s;
  var m = /^(https?):\/\/([^/?#]+)([^?#]*)/i.exec(s);
  if (!m) return '';
  var host = m[2].toLowerCase();
  if (!/^(\[[0-9a-f:.]+\]|[a-z0-9_]([a-z0-9._-]*[a-z0-9_])?)(:[0-9]{1,5})?$/.test(host)) return '';
  var port = /:([0-9]+)$/.exec(host);
  if (port && (+port[1] < 1 || +port[1] > 65535)) return '';
  var path = m[3].replace(/\/{2,}/g, '/').replace(/\/api(\/.*)?$/i, '').replace(/\/+$/, '');
  return m[1].toLowerCase() + '://' + host + path;
}

// "tandoor.example.com", or "192.168.1.5:8080" when the port isn't the default one.
function hostOf(server) {
  var m = /^https?:\/\/([^/]+)/i.exec(toStr(server));
  return m ? truncateUtf8(m[1].toLowerCase(), LIMIT.host) : '';
}

function sameHost(a, b) {
  var host = hostOf(a);
  return host !== '' && host === hostOf(b);
}

// Days ---------------------------------------------------------------------------------------------

function pad2(n) {
  return (n < 10 ? '0' : '') + n;
}

// The phone's local date as YYYY-MM-DD.
function isoDate(date) {
  return date.getFullYear() + '-' + pad2(date.getMonth() + 1) + '-' + pad2(date.getDate());
}

function parseDay(value) {
  var m = /^(\d{4})-(\d{2})-(\d{2})/.exec(toStr(value));
  if (!m || +m[2] < 1 || +m[2] > 12 || +m[3] < 1 || +m[3] > 31) return null;
  return { year: +m[1], month: +m[2], day: +m[3] };
}

function addDays(day, count) {
  var p = parseDay(day);
  if (!p) return '';
  var d = new Date(Date.UTC(p.year, p.month - 1, p.day + count));
  return d.getUTCFullYear() + '-' + pad2(d.getUTCMonth() + 1) + '-' + pad2(d.getUTCDate());
}

// "Today", "Tomorrow", "Fri 21".
function dayLabel(day, today) {
  var a = parseDay(day);
  if (!a) return '';
  var b = parseDay(today);
  var diff = b ? (Date.UTC(a.year, a.month - 1, a.day) - Date.UTC(b.year, b.month - 1, b.day)) / 86400000 : NaN;
  if (diff === 0) return 'Today';
  if (diff === 1) return 'Tomorrow';
  return DAY_NAMES[new Date(Date.UTC(a.year, a.month - 1, a.day)).getUTCDay()] + ' ' + a.day;
}

// Lists -------------------------------------------------------------------------------------------

// "25 min", "1 h 10 min", "2 h".
function formatMinutes(minutes) {
  var m = Math.max(0, Math.round(minutes));
  if (m < 60) return m + ' min';
  var h = Math.floor(m / 60);
  return m % 60 === 0 ? h + ' h' : h + ' h ' + (m % 60) + ' min';
}

// "25 min · 4 servings", "1 h 10 min", "1 patty" (servings_text replaces "servings").
function recipeSubtitle(recipe) {
  var total = Math.round(toNumber(recipe.working_time)) + Math.round(toNumber(recipe.waiting_time));
  var parts = [];
  if (total > 0) parts.push(formatMinutes(total));
  var servings = toNumber(recipe.servings);
  if (servings > 0) {
    var label = cleanLine(recipe.servings_text, LIMIT.servingsText);
    parts.push(formatNumber(servings) + ' ' + (label || (servings === 1 ? 'serving' : 'servings')));
  }
  return parts.join(DOT);
}

// The request path for one page of a list. A book defined by a saved filter takes `filterId`.
// The meal plan is fetched whole, since Tandoor orders it by id and index.js sorts it by day.
function listPath(kind, id, page, query, options) {
  var opts = options || {};
  var p = Math.max(1, Math.floor(toNumber(page)) || 1);
  var paging = 'page=' + p + '&page_size=' + PAGE_SIZE;
  var q = toStr(query).trim();
  switch (kind) {
    case KIND.PLAN: {
      var today = opts.today || isoDate(new Date());
      return '/api/meal-plan/?from_date=' + today + '&to_date=' + addDays(today, PLAN_DAYS - 1) +
        '&page_size=' + LIMIT.records;
    }
    case KIND.RECENT:
      return '/api/recipe/?sort_order=-lastviewed&' + paging;
    case KIND.ALL:
      return '/api/recipe/?sort_order=name&' + paging;
    case KIND.BOOKS:
      return '/api/recipe-book/?' + paging;
    case KIND.BOOK:
      return '/api/recipe/?' + (opts.filterId ? 'filter=' + toId(opts.filterId) : 'books=' + toId(id)) + '&' + paging;
    case KIND.KEYWORDS:
      return '/api/keyword/?root=' + toId(id) + '&' + paging;
    case KIND.KEYWORD:
      return '/api/recipe/?keywords=' + toId(id) + '&' + paging;
    case KIND.SEARCH:
      return '/api/recipe/?' + (q === '' ? '' : 'query=' + encodeURIComponent(q) + '&') + paging;
    default:
      return '';
  }
}

// The id of the saved filter that defines a book, or 0.
function bookFilterId(book) {
  var filter = isObject(book) ? book.filter : null;
  return toId(isObject(filter) ? filter.id : filter);
}

function recipeRecord(recipe) {
  var id = toId(recipe.id);
  return ['r', String(id), cleanLine(recipe.name, LIMIT.title) || 'Recipe ' + id, cleanLine(recipeSubtitle(recipe), LIMIT.subtitle)];
}

// One page of a list from a paginated Tandoor answer.
//   records  list records for the Data field
//   index    position of the first record in the whole list
//   more     another page exists and the list has room for it
//   total    items in the whole list (Tandoor's count), or -1
//   learned  what index.js caches: book id -> filter id, keyword id -> name
// For KEYWORDS under a parent (options.parentId), page 1 starts with "All <parent>", a keyword
// record with the parent's id, so every page after it begins one record later.
function parseList(kind, answer, options) {
  var opts = options || {};
  var page = Math.max(1, Math.floor(toNumber(opts.page)) || 1);
  var data = isObject(answer) ? answer : {};
  var results = asArray(data.results).filter(function (item) {
    return isObject(item) && toId(item.id) > 0;
  });
  var learned = { books: {}, keywords: {} };
  var records;
  if (kind === KIND.BOOKS) {
    records = results.map(function (book) {
      learned.books[toId(book.id)] = bookFilterId(book);
      return ['b', String(toId(book.id)), cleanLine(book.name, LIMIT.title) || 'Book ' + toId(book.id), ''];
    });
  } else if (kind === KIND.KEYWORDS) {
    records = results.map(function (keyword) {
      var name = cleanLine(keyword.name || keyword.label, LIMIT.title);
      learned.keywords[toId(keyword.id)] = name;
      return [toNumber(keyword.numchild) > 0 ? 'f' : 'k', String(toId(keyword.id)), name, ''];
    });
  } else {
    records = results.map(recipeRecord);
  }

  var parent = kind === KIND.KEYWORDS ? toId(opts.parentId) : 0;
  if (parent > 0 && page === 1) {
    records.unshift(['k', String(parent), cleanLine('All ' + toStr(opts.parentName), LIMIT.title), '']);
  }
  var index = (page - 1) * PAGE_SIZE + (parent > 0 && page > 1 ? 1 : 0);
  records = records.slice(0, Math.max(0, LIMIT.records - index));
  var count = typeof data.count === 'number' && data.count >= 0 ? data.count : -1;
  return {
    records: records,
    index: index,
    more: !!data.next && index + records.length < LIMIT.records,
    total: count < 0 ? -1 : count + (parent > 0 ? 1 : 0),
    learned: learned
  };
}

// The meal plan's entries from a paginated answer, as records in day, then meal order. An entry
// without a recipe becomes a note record ('n'). `today` is the phone's local date.
function parsePlan(answer, today) {
  var entries = [];
  asArray(isObject(answer) ? answer.results : null).forEach(function (entry, seq) {
    if (!isObject(entry)) return;
    var from = toStr(entry.from_date);
    var day = parseDay(from) ? from.slice(0, 10) : '';
    if (day === '') return;
    var mealType = isObject(entry.meal_type) ? entry.meal_type : {};
    var meal = cleanLine(entry.meal_type_name || mealType.name, LIMIT.subtitle);
    var recipe = isObject(entry.recipe) && toId(entry.recipe.id) > 0 ? entry.recipe : null;
    var subtitle = cleanLine([dayLabel(day, today), meal].filter(Boolean).join(DOT), LIMIT.subtitle);
    var record;
    if (recipe) {
      record = ['r', String(toId(recipe.id)), cleanLine(recipe.name || entry.recipe_name || entry.title, LIMIT.title) || 'Recipe', subtitle];
    } else {
      var title = cleanLine(entry.title, LIMIT.title) || cleanLine(htmlToText(entry.note).split('\n')[0], LIMIT.title) || meal || 'Meal plan';
      record = ['n', String(toId(entry.id)), title, subtitle];
    }
    entries.push({
      day: day,
      order: toNumber(mealType.order),
      mealTime: toStr(mealType.time),
      time: from.slice(11, 19),
      meal: meal.toLowerCase(),
      seq: seq,
      record: record
    });
  });
  entries.sort(function (a, b) {
    if (a.day !== b.day) return a.day < b.day ? -1 : 1;
    if (a.order !== b.order) return a.order - b.order;
    if (a.mealTime !== b.mealTime) return a.mealTime < b.mealTime ? -1 : 1;
    if (a.time !== b.time) return a.time < b.time ? -1 : 1;
    if (a.meal !== b.meal) return a.meal < b.meal ? -1 : 1;
    return a.seq - b.seq;
  });
  return entries.slice(0, LIMIT.records).map(function (entry) {
    return entry.record;
  });
}

// One page of a list kept whole, with the same fields as parseList.
function pageOf(records, page) {
  var p = Math.max(1, Math.floor(toNumber(page)) || 1);
  var start = (p - 1) * PAGE_SIZE;
  return {
    records: records.slice(start, start + PAGE_SIZE),
    index: start,
    more: start + PAGE_SIZE < records.length,
    total: records.length
  };
}

// Records ------------------------------------------------------------------------------------------

function encodeRecord(fields) {
  var parts = [];
  for (var i = 0; i < fields.length; i++) {
    parts.push(stripControls(toStr(fields[i]).replace(/\s+/g, ' '), false).replace(/^ +| +$/g, ''));
  }
  return parts.join('\x1f') + '\x1e';
}

// Records joined into Data chunks of at most maxBytes (UTF-8). A chunk says where it starts in
// `records`. A record never splits; the field limits keep each far below the chunk size.
function packRecords(records, maxBytes) {
  var limit = maxBytes || LIMIT.data;
  var chunks = [];
  var data = '';
  var bytes = 0;
  var start = 0;
  var count = 0;
  for (var i = 0; i < records.length; i++) {
    var record = encodeRecord(records[i]);
    var size = utf8Length(record);
    if (size > limit) throw new RangeError('A record is longer than ' + limit + ' bytes');
    if (bytes + size > limit) {
      chunks.push({ start: start, count: count, data: data });
      data = '';
      bytes = 0;
      start = i;
      count = 0;
    }
    data += record;
    bytes += size;
    count++;
  }
  if (count > 0) chunks.push({ start: start, count: count, data: data });
  return chunks;
}

// Recipes ------------------------------------------------------------------------------------------

// How long a step's timer runs: Tandoor's `time` (minutes), else the first duration in its text.
function stepTimer(raw, text) {
  var minutes = toNumber(raw.time);
  if (minutes > 0) return { seconds: clamp(Math.round(minutes * 60), 1, MAX_TIMER_SECONDS), flags: 0 };
  var found = findDuration(text);
  return { seconds: found, flags: found > 0 ? STEP_FROM_TEXT : 0 };
}

// One step of Tandoor's answer; `prefix` is the sub-recipe's name for a step it contributes.
function describeStep(raw, prefix) {
  var text = htmlToText(raw.instructions_markdown);
  if (text === '') text = htmlToText(raw.instruction);
  var own = cleanLine(raw.name, 200);
  var name = prefix === '' ? own : (own === '' ? prefix : prefix + DOT + own);
  var timer = stepTimer(raw, text);
  return {
    name: truncateUtf8(name, LIMIT.stepName),
    text: truncateUtf8(text, LIMIT.stepText),
    seconds: timer.seconds,
    flags: timer.flags,
    ingredients: asArray(raw.ingredients)
  };
}

// An ingredient record: step, flags, amount ×1000, unit, unit plural, food, food plural, note.
// A header keeps its text in `food`. In Tandoor the header's text is the ingredient's note.
function ingredientRecord(raw, stepIndex) {
  if (!isObject(raw)) return null;
  var food = isObject(raw.food) ? raw.food : {};
  var unit = isObject(raw.unit) ? raw.unit : {};
  var step = stepIndex === null ? '' : String(stepIndex);
  if (raw.is_header) {
    var heading = cleanLine(raw.note || food.name || raw.original_text, LIMIT.food);
    return heading === '' ? null : [step, String(INGREDIENT_HEADER), '0', '', '', heading, '', ''];
  }
  var name = cleanLine(food.name, LIMIT.food);
  var note = cleanLine(raw.note, LIMIT.note);
  var unitName = cleanLine(unit.name, LIMIT.unit);
  if (name === '' && note === '' && unitName === '') return null;
  var amount = Math.round(clamp(toNumber(raw.amount), 0, MAX_AMOUNT) * 1000);
  var flags = raw.no_amount || amount <= 0 ? INGREDIENT_NO_AMOUNT : 0;
  return [
    step,
    String(flags),
    String(flags ? 0 : amount),
    unitName,
    cleanLine(unit.plural_name, LIMIT.unit),
    name,
    cleanLine(food.plural_name, LIMIT.food),
    note
  ];
}

// Steps and ingredients as the watch gets them. A step that includes another recipe is replaced
// by that recipe's steps ("Curry paste · Toast the spices"). Its own text stays as a step of its
// own, and its own ingredients stay in the list, so nothing Tandoor shows is lost.
function flattenSteps(detail) {
  var steps = [];
  var ingredients = [];

  function addIngredients(list, stepIndex) {
    for (var i = 0; i < list.length && ingredients.length < LIMIT.ingredients; i++) {
      var record = ingredientRecord(list[i], stepIndex);
      if (record) ingredients.push(record);
    }
  }

  function addStep(step) {
    var blank = step.text === '' && step.name === '' && step.ingredients.length === 0 && step.seconds === 0;
    if (blank) return;
    if (steps.length >= LIMIT.steps) {
      addIngredients(step.ingredients, null);
      return;
    }
    addIngredients(step.ingredients, steps.length);
    steps.push({ name: step.name, text: step.text, seconds: step.seconds, flags: step.flags });
  }

  asArray(detail.steps).forEach(function (raw) {
    if (!isObject(raw)) return;
    var sub = isObject(raw.step_recipe_data) ? raw.step_recipe_data : null;
    var included = sub ? asArray(sub.steps).filter(isObject) : [];
    var own = describeStep(raw, '');
    if (included.length === 0) {
      addStep(own);
      return;
    }
    if (own.text === '') {
      addIngredients(own.ingredients, null);
    } else {
      addStep(own);
    }
    var prefix = cleanLine(sub.name, 200) || own.name || 'Recipe';
    included.forEach(function (step) {
      addStep(describeStep(step, prefix));
    });
  });
  return { steps: steps, ingredients: ingredients };
}

// Keyword names that fit maxBytes together, whole names only.
function keywordLine(keywords, maxBytes) {
  var line = '';
  var names = asArray(keywords).map(function (k) {
    return cleanLine(isObject(k) ? k.label || k.name : k, maxBytes);
  }).filter(Boolean);
  for (var i = 0; i < names.length; i++) {
    var next = line === '' ? names[i] : line + ', ' + names[i];
    if (utf8Length(next) > maxBytes) break;
    line = next;
  }
  return line === '' && names.length > 0 ? truncateUtf8(names[0], maxBytes) : line;
}

// A recipe from GET /api/recipe/<id>/ in the watch's terms.
function parseRecipe(detail) {
  var d = isObject(detail) ? detail : {};
  var flat = flattenSteps(d);
  return {
    id: toId(d.id),
    name: cleanLine(d.name, LIMIT.name),
    description: truncateUtf8(htmlToText(d.description), LIMIT.description),
    servings: clamp(Math.round(toNumber(d.servings) * 100), 0, 2000000000),
    servingsText: cleanLine(d.servings_text, LIMIT.servingsText),
    workingTime: clamp(Math.round(toNumber(d.working_time)), 0, 32767),
    waitingTime: clamp(Math.round(toNumber(d.waiting_time)), 0, 32767),
    rating: clamp(Math.round(toNumber(d.rating)), 0, 5),
    keywords: keywordLine(d.keywords, LIMIT.keywords),
    steps: flat.steps,
    ingredients: flat.ingredients
  };
}

// Errors and settings -----------------------------------------------------------------------------

var TEXT = {
  notSetUp: 'Open this app\u2019s settings in the Pebble app and add your server.',
  offline: 'Can\u2019t reach your Tandoor server. Check the address and network.',
  token: 'Tandoor refused the API token. Check it in the settings.',
  readOnly: 'The token can\u2019t save. Give it the read write scope.',
  notFound: 'That wasn\u2019t found. It may have been deleted.',
  busy: 'Tandoor is busy. Try again in a moment.',
  server: 'Tandoor had a problem. Try again in a moment.',
  refused: 'Tandoor didn\u2019t accept that request.',
  unreadable: 'Tandoor\u2019s answer couldn\u2019t be read.',
  noRecipes: 'You have no recipes yet.',
  logged: 'Logged in Tandoor.'
};

var PAGE_TEXT = {
  offline: 'Couldn\u2019t reach that address. Check it, and that your phone is online.',
  token: 'The server refused this token. Check that you copied all of it, and that its scope is read write.',
  notFound: 'No Tandoor API at that address. Check it, and any path after the host name.',
  unreadable: 'That doesn\u2019t look like a Tandoor server. Check the address.'
};

// The error code and the sentence for an HTTP status; 0 means the request never got an answer, and
// -1 an answer that isn't JSON. `context` is 'save' for the settings page, which has room for a
// longer sentence, and 'cook' for the cook log, where 403 means a read only token.
function errorFor(status, context) {
  var page = context === 'save';
  var code;
  var text;
  if (status === -1) {
    code = ERR.SERVER;
    text = page ? PAGE_TEXT.unreadable : TEXT.unreadable;
  } else if (status === 401 || status === 403) {
    code = ERR.TOKEN;
    text = page ? PAGE_TEXT.token : (context === 'cook' && status === 403 ? TEXT.readOnly : TEXT.token);
  } else if (status === 404) {
    code = ERR.NOT_FOUND;
    text = page ? PAGE_TEXT.notFound : TEXT.notFound;
  } else if (status === 429) {
    code = ERR.SERVER;
    text = TEXT.busy;
  } else if (status >= 500) {
    code = ERR.SERVER;
    text = TEXT.server;
  } else if (status >= 400) {
    code = ERR.SERVER;
    text = TEXT.refused;
  } else if (status >= 200 && status < 300) {
    code = 0;
    text = '';
  } else {
    code = ERR.UNREACHABLE;
    text = page ? PAGE_TEXT.offline : TEXT.offline;
  }
  return { code: code, text: page ? text : truncateUtf8(text, LIMIT.message), status: status };
}

function notSetUp() {
  return { code: ERR.NOT_SET_UP, text: TEXT.notSetUp, status: 0 };
}

function noRecipes() {
  return { code: ERR.NOT_FOUND, text: TEXT.noRecipes, status: 404 };
}

// "Connected · 42 recipes"
function connectedMessage(count) {
  var n = Math.max(0, Math.floor(toNumber(count)));
  return 'Connected' + DOT + n + (n === 1 ? ' recipe' : ' recipes');
}

function flag(value, fallback) {
  if (value === true || value === 1 || value === '1' || value === 'true') return 1;
  if (value === false || value === 0 || value === '0' || value === 'false') return 0;
  return fallback;
}

// Settings are two switches, 1 or 0.
function cleanSettings(raw) {
  var r = isObject(raw) ? raw : {};
  return {
    backlight: flag(r.backlight, DEFAULT_SETTINGS.backlight),
    sound: flag(r.sound, DEFAULT_SETTINGS.sound)
  };
}

module.exports = {
  KIND: KIND,
  ERR: ERR,
  LIMIT: LIMIT,
  PAGE_SIZE: PAGE_SIZE,
  TEXT: TEXT,
  DEFAULT_SETTINGS: DEFAULT_SETTINGS,
  INGREDIENT_HEADER: INGREDIENT_HEADER,
  INGREDIENT_NO_AMOUNT: INGREDIENT_NO_AMOUNT,
  STEP_FROM_TEXT: STEP_FROM_TEXT,
  isObject: isObject,
  toId: toId,
  truncateUtf8: truncateUtf8,
  utf8Length: utf8Length,
  decodeEntities: decodeEntities,
  cleanLine: cleanLine,
  htmlToText: htmlToText,
  findDuration: findDuration,
  normalizeServer: normalizeServer,
  hostOf: hostOf,
  sameHost: sameHost,
  isoDate: isoDate,
  addDays: addDays,
  dayLabel: dayLabel,
  formatMinutes: formatMinutes,
  recipeSubtitle: recipeSubtitle,
  listPath: listPath,
  bookFilterId: bookFilterId,
  parseList: parseList,
  parsePlan: parsePlan,
  pageOf: pageOf,
  packRecords: packRecords,
  parseRecipe: parseRecipe,
  errorFor: errorFor,
  notSetUp: notSetUp,
  noRecipes: noRecipes,
  connectedMessage: connectedMessage,
  cleanSettings: cleanSettings
};
