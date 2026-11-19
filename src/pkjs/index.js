// Tandoor's phone side: the bridge between the watch (AppMessage) and a Tandoor server (REST),
// and the answers to the settings page. The server address, the API token and the settings live
// in localStorage. tandoor.js holds the logic that needs no Pebble or XHR.
//
// The SDK bundles this file with webpack 1, whose parser takes ES2015 only: no async/await, no `**`.

var tandoor = require('./tandoor');

// Watch -> phone
var CMD_HELLO = 1;
var CMD_LIST = 2;
var CMD_RECIPE = 3;
var CMD_COOKED = 4;
// Phone -> watch
var CMD_READY = 10;
var CMD_STATUS = 11;
var CMD_SETTINGS = 12;
var CMD_ITEMS = 13;
var CMD_RECIPE_HEAD = 14;
var CMD_INGREDIENTS = 15;
var CMD_STEP = 16;
var CMD_DONE = 17;
var CMD_ERROR = 18;
var CMD_RESULT = 19;

var STATE_READY = 0;
var STATE_NOT_SET_UP = 1;

var KIND = tandoor.KIND;

var REQUEST_TIMEOUT_MS = 15000;
// The watch gives up on a request after 25 s, so PKJS reports its own failure first, however many
// requests the answer takes.
var JOB_BUDGET_MS = 22000;
var MIN_REQUEST_MS = 1500;
var NACK_RETRIES = 2;
var NACK_RETRY_MS = 400;
var PLAN_CACHE_MS = 10 * 60 * 1000;

// Storage. The keys are in docs/design.md.

function readItem(key) {
  try {
    var value = localStorage.getItem(key);
    return value === null || value === undefined ? '' : String(value);
  } catch (_e) {
    return '';
  }
}

function writeItem(key, value) {
  try {
    localStorage.setItem(key, value);
    return true;
  } catch (e) {
    console.log('localStorage write failed for ' + key + ': ' + e);
    return false;
  }
}

function removeItem(key) {
  try {
    localStorage.removeItem(key);
  } catch (e) {
    console.log('localStorage remove failed for ' + key + ': ' + e);
  }
}

function loadSettings() {
  var raw = null;
  try {
    raw = JSON.parse(readItem('settings') || 'null');
  } catch (_e) {
    raw = null;
  }
  return tandoor.cleanSettings(raw);
}

function savedServer() {
  return tandoor.normalizeServer(readItem('server'));
}

// The server and token, or null until both are saved.
function account() {
  var server = savedServer();
  var token = readItem('token').trim();
  return server !== '' && token !== '' ? { server: server, token: token } : null;
}

// Serial AppMessage queue: one message in flight, NACKs retried after a short delay. A group is a
// set of messages delivered whole or abandoned together; an owner is the request they answer.

var queue = [];
var inFlight = null;

function enqueue(messages, owner) {
  var group = {};
  for (var i = 0; i < messages.length; i++) {
    queue.push({ msg: messages[i], group: group, owner: owner || null, tries: 0, dead: false });
  }
  pump();
}

function removeEntry(entry) {
  var at = queue.indexOf(entry);
  if (at !== -1) queue.splice(at, 1);
}

function abandonGroup(group) {
  queue = queue.filter(function (entry) {
    return entry.group !== group;
  });
}

// Drops what a request has not sent yet. A message already on its way can't be recalled; it is
// marked so a NACK doesn't resend it.
function dropQueued(owner) {
  queue = queue.filter(function (entry) {
    if (entry.owner !== owner) return true;
    if (entry === inFlight) {
      entry.dead = true;
      return true;
    }
    return false;
  });
}

function pump() {
  while (!inFlight && queue.length > 0 && queue[0].dead) queue.shift();
  if (inFlight || queue.length === 0) return;
  var entry = queue[0];
  var settled = false;
  inFlight = entry;

  function ack() {
    if (settled) return;
    settled = true;
    inFlight = null;
    removeEntry(entry);
    pump();
  }

  function nack() {
    if (settled) return;
    settled = true;
    entry.tries++;
    if (!entry.dead && entry.tries <= NACK_RETRIES) {
      setTimeout(function () {
        inFlight = null;
        pump();
      }, entry.tries * NACK_RETRY_MS);
      return;
    }
    inFlight = null;
    if (entry.dead) {
      removeEntry(entry);
    } else {
      console.log('The watch did not take message ' + entry.msg.Cmd + '; dropping its answer.');
      abandonGroup(entry.group);
    }
    pump();
  }

  try {
    Pebble.sendAppMessage(entry.msg, ack, nack);
  } catch (e) {
    console.log('sendAppMessage threw: ' + e);
    nack();
  }
}

// HTTP. One request at a time per call; callback(err, data) runs once. err is null or
// { code, text, status } as tandoor.errorFor gives it, and data is the parsed JSON.
//   options: method, server, token, path, body (POST), timeout (ms), context ('save', 'cook')
// The handle's abort() stops the callback from ever running.

function http(options, callback) {
  var context = options.context;
  var finished = false;
  var xhr = null;
  var watchdog = null;

  function finish(err, data) {
    if (finished) return;
    finished = true;
    clearTimeout(watchdog);
    callback(err, data);
  }

  var handle = {
    abort: function () {
      if (finished) return;
      finished = true;
      clearTimeout(watchdog);
      try {
        if (xhr) xhr.abort();
      } catch (_e) {
        // Nothing to stop: the request had already ended.
      }
    }
  };

  try {
    xhr = new XMLHttpRequest();
    xhr.open(options.method, options.server + options.path, true);
    xhr.setRequestHeader('Authorization', 'Bearer ' + options.token);
    xhr.setRequestHeader('Accept', 'application/json');
    if (options.body) xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.timeout = options.timeout;
    xhr.onload = function () {
      var status = xhr.status;
      if (status < 200 || status >= 300) {
        finish(tandoor.errorFor(status, context), null);
        return;
      }
      var text = xhr.responseText;
      var data = null;
      if (text) {
        try {
          data = JSON.parse(text);
        } catch (_e) {
          finish(tandoor.errorFor(-1, context), null);
          return;
        }
      } else if (options.method === 'GET') {
        finish(tandoor.errorFor(-1, context), null);
        return;
      }
      finish(null, data);
    };
    xhr.onerror = function () {
      finish(tandoor.errorFor(0, context), null);
    };
    xhr.ontimeout = xhr.onerror;
    // For a runtime that ignores xhr.timeout.
    watchdog = setTimeout(function () {
      try {
        xhr.abort();
      } catch (_e) {
        // Already ended.
      }
      finish(tandoor.errorFor(0, context), null);
    }, options.timeout + 1000);
    xhr.send(options.body ? JSON.stringify(options.body) : null);
  } catch (e) {
    console.log('Request failed to start: ' + e);
    finish(tandoor.errorFor(0, context), null);
  }
  return handle;
}

// Jobs. A list or a recipe is a job. A newer job supersedes the older one: its requests are
// aborted and its answers it hasn't sent yet are dropped, so a screen the user left doesn't
// hold up the one they opened. Cooks being logged are not jobs: they must not be cut short.

var currentJob = null;

function cancelJob(job) {
  if (!job || job.cancelled) return;
  job.cancelled = true;
  job.requests.forEach(function (handle) {
    handle.abort();
  });
  job.requests = [];
  dropQueued(job);
}

function startJob(req) {
  cancelJob(currentJob);
  currentJob = { req: req, cancelled: false, requests: [], deadline: Date.now() + JOB_BUDGET_MS };
  return currentJob;
}

function reply(job, messages) {
  if (!job.cancelled) enqueue(messages, job);
}

function replyError(job, err) {
  reply(job, [{ Cmd: CMD_ERROR, Req: job.req, State: err.code, Text: err.text }]);
}

// Runs a step of a job. When it throws, as a surprising answer from the server can make it, the
// watch gets an error instead of silence.
function attempt(job, fn) {
  try {
    fn();
  } catch (e) {
    console.log('Request ' + job.req + ' failed: ' + e);
    replyError(job, tandoor.errorFor(-1));
  }
}

function jobGet(job, acct, path, callback) {
  var left = job.deadline - Date.now();
  if (left < MIN_REQUEST_MS) {
    callback(tandoor.errorFor(0), null);
    return;
  }
  var handle = http({
    method: 'GET',
    server: acct.server,
    token: acct.token,
    path: path,
    timeout: Math.min(REQUEST_TIMEOUT_MS, left)
  }, function (err, data) {
    job.requests = job.requests.filter(function (other) {
      return other !== handle;
    });
    if (!job.cancelled) {
      attempt(job, function () {
        callback(err, data);
      });
    }
  });
  job.requests.push(handle);
}

// What the lists teach about books and keywords, so the next list needn't ask.
var bookFilters = {}; // book id -> saved filter id, 0 for none
var keywordNames = {}; // keyword id -> name
var plan = null; // { day, at, records }

function resetCaches() {
  bookFilters = {};
  keywordNames = {};
  plan = null;
}

function learn(learned) {
  Object.keys(learned.books).forEach(function (id) {
    bookFilters[id] = learned.books[id];
  });
  Object.keys(learned.keywords).forEach(function (id) {
    keywordNames[id] = learned.keywords[id];
  });
}

// Messages to the watch

function statusMessages() {
  var settings = loadSettings();
  return [
    { Cmd: CMD_STATUS, State: account() ? STATE_READY : STATE_NOT_SET_UP, Text: tandoor.hostOf(savedServer()) },
    { Cmd: CMD_SETTINGS, Backlight: settings.backlight, Sound: settings.sound }
  ];
}

function itemsMessages(req, result) {
  var chunks = tandoor.packRecords(result.records);
  if (chunks.length === 0) chunks = [{ start: 0, count: 0, data: '' }];
  return chunks.map(function (chunk, i) {
    return {
      Cmd: CMD_ITEMS,
      Req: req,
      Index: result.index + chunk.start,
      Data: chunk.data,
      Done: i === chunks.length - 1 ? 1 : 0,
      More: result.more ? 1 : 0,
      Total: result.total
    };
  });
}

function recipeMessages(req, recipe, requestedId) {
  var messages = [{
    Cmd: CMD_RECIPE_HEAD,
    Req: req,
    Id: recipe.id || requestedId,
    Name: recipe.name,
    Desc: recipe.description,
    Servings: recipe.servings,
    ServText: recipe.servingsText,
    Time1: recipe.workingTime,
    Time2: recipe.waitingTime,
    Rating: recipe.rating,
    Count: recipe.steps.length,
    Total: recipe.ingredients.length,
    Sub: recipe.keywords
  }];
  tandoor.packRecords(recipe.ingredients).forEach(function (chunk) {
    messages.push({ Cmd: CMD_INGREDIENTS, Req: req, Index: chunk.start, Data: chunk.data });
  });
  recipe.steps.forEach(function (step, index) {
    messages.push({
      Cmd: CMD_STEP,
      Req: req,
      Index: index,
      Name: step.name,
      Text: step.text,
      Time1: step.seconds,
      Flags: step.flags
    });
  });
  messages.push({ Cmd: CMD_DONE, Req: req });
  return messages;
}

// Lists

// The whole meal plan, sorted. Page 1 always asks again; later pages come from the copy.
function loadPlan(job, acct, page, today, callback) {
  if (page > 1 && plan && plan.day === today && Date.now() - plan.at < PLAN_CACHE_MS) {
    callback(null, tandoor.pageOf(plan.records, page));
    return;
  }
  jobGet(job, acct, tandoor.listPath(KIND.PLAN, 0, 1, '', { today: today }), function (err, answer) {
    if (err) {
      callback(err, null);
      return;
    }
    plan = { day: today, at: Date.now(), records: tandoor.parsePlan(answer, today) };
    callback(null, tandoor.pageOf(plan.records, page));
  });
}

// A book defined by a saved filter lists its recipes through that filter, not through the book.
function loadFilterId(job, acct, bookId, callback) {
  if (bookFilters[bookId] !== undefined) {
    callback(null, bookFilters[bookId]);
    return;
  }
  jobGet(job, acct, '/api/recipe-book/' + bookId + '/', function (err, book) {
    if (err) {
      callback(err, null);
      return;
    }
    bookFilters[bookId] = tandoor.bookFilterId(book);
    callback(null, bookFilters[bookId]);
  });
}

function loadKeywordName(job, acct, keywordId, callback) {
  if (keywordNames[keywordId] !== undefined) {
    callback(null, keywordNames[keywordId]);
    return;
  }
  jobGet(job, acct, '/api/keyword/' + keywordId + '/', function (err, keyword) {
    if (err) {
      callback(err, null);
      return;
    }
    keywordNames[keywordId] = tandoor.isObject(keyword) ? tandoor.cleanLine(keyword.name || keyword.label, tandoor.LIMIT.title) : '';
    callback(null, keywordNames[keywordId]);
  });
}

function onList(payload) {
  var req = tandoor.toId(payload.Req);
  var kind = tandoor.toId(payload.Kind);
  var id = tandoor.toId(payload.Id);
  var page = Math.max(1, tandoor.toId(payload.Page));
  var query = typeof payload.Query === 'string' ? payload.Query : '';
  var job = startJob(req);
  var acct = account();
  if (!acct) {
    replyError(job, tandoor.notSetUp());
    return;
  }
  var today = tandoor.isoDate(new Date());

  function deliver(err, result) {
    var shown = result;
    if (err) {
      // Past the last page Tandoor answers 404; the list is complete.
      if (err.status !== 404 || page === 1 || kind === KIND.PLAN) {
        replyError(job, err);
        return;
      }
      shown = { records: [], index: (page - 1) * tandoor.PAGE_SIZE, more: false, total: -1 };
    }
    reply(job, itemsMessages(req, shown));
  }

  // One page of recipes, books or keywords from Tandoor, given what the path needs to know.
  function fetchPage(extra) {
    var path = tandoor.listPath(kind, id, page, query, { today: today, filterId: extra.filterId });
    jobGet(job, acct, path, function (err, answer) {
      if (err) {
        deliver(err, null);
        return;
      }
      var result = tandoor.parseList(kind, answer, { page: page, parentId: id, parentName: extra.parentName });
      learn(result.learned);
      deliver(null, result);
    });
  }

  attempt(job, function () {
    switch (kind) {
      case KIND.PLAN:
        loadPlan(job, acct, page, today, deliver);
        break;
      case KIND.RECENT:
      case KIND.ALL:
      case KIND.BOOKS:
      case KIND.KEYWORD:
      case KIND.SEARCH:
        fetchPage({});
        break;
      case KIND.BOOK:
        loadFilterId(job, acct, id, function (err, filterId) {
          if (err) {
            replyError(job, err);
            return;
          }
          fetchPage({ filterId: filterId });
        });
        break;
      case KIND.KEYWORDS:
        if (id === 0 || page > 1) {
          fetchPage({});
          break;
        }
        loadKeywordName(job, acct, id, function (err, name) {
          if (err) {
            replyError(job, err);
            return;
          }
          fetchPage({ parentName: name });
        });
        break;
      default:
        replyError(job, tandoor.errorFor(400));
    }
  });
}

// Recipes

function onRecipe(payload) {
  var req = tandoor.toId(payload.Req);
  var id = tandoor.toId(payload.Id);
  var job = startJob(req);
  var acct = account();
  if (!acct) {
    replyError(job, tandoor.notSetUp());
    return;
  }

  function show(recipeId) {
    jobGet(job, acct, '/api/recipe/' + recipeId + '/', function (err, detail) {
      if (err) {
        replyError(job, err);
        return;
      }
      reply(job, recipeMessages(req, tandoor.parseRecipe(detail), recipeId));
    });
  }

  if (id > 0) {
    attempt(job, function () {
      show(id);
    });
    return;
  }
  // Surprise me: one random recipe, then its detail.
  attempt(job, function () {
    jobGet(job, acct, '/api/recipe/?random=true&page_size=1', function (err, answer) {
      if (err) {
        replyError(job, err);
        return;
      }
      var first = tandoor.isObject(answer) && Array.isArray(answer.results) ? answer.results[0] : null;
      var randomId = tandoor.isObject(first) ? tandoor.toId(first.id) : 0;
      if (randomId === 0) {
        replyError(job, tandoor.noRecipes());
        return;
      }
      show(randomId);
    });
  });
}

// Logging a cook
function onCooked(payload) {
  var req = tandoor.toId(payload.Req);
  var id = tandoor.toId(payload.Id);
  var servings = Number(payload.Servings) / 100;
  var acct = account();

  function answer(err) {
    enqueue([{
      Cmd: CMD_RESULT,
      Req: req,
      State: err ? err.code : 0,
      Text: err ? err.text : tandoor.TEXT.logged
    }]);
  }

  if (!acct) {
    answer(tandoor.notSetUp());
    return;
  }
  if (id === 0) {
    answer(tandoor.errorFor(404));
    return;
  }
  var body = { recipe: id };
  if (servings > 0) body.servings = Math.round(servings * 100) / 100;
  http({
    method: 'POST',
    server: acct.server,
    token: acct.token,
    path: '/api/cook-log/',
    body: body,
    timeout: REQUEST_TIMEOUT_MS,
    context: 'cook'
  }, answer);
}

// Settings page. Requests from src/config.html, answered through e.respond.

function pageStatus() {
  return { ok: true, server: savedServer(), hasToken: readItem('token').trim() !== '', settings: loadSettings() };
}

// Whatever changed what the watch may do: new state for the watch, and nothing stale in flight.
function accountChanged() {
  resetCaches();
  cancelJob(currentJob);
  enqueue(statusMessages());
}

var PAGE = Object.create(null);

PAGE.status = function (_message, respond) {
  respond(pageStatus());
};

// Tests the server with the token, then saves server, token and settings.
PAGE.save = function (message, respond) {
  var server = tandoor.normalizeServer(message.server);
  if (server === '') {
    respond({ ok: false, message: 'Enter the address you open Tandoor at, like https://tandoor.example.com.' });
    return;
  }
  var token = String(message.token === null || message.token === undefined ? '' : message.token)
    .trim()
    .replace(/^(?:bearer|token)\s+/i, '');
  if (token === '') {
    // An empty field keeps the saved token, but only for the server it belongs to: it must not
    // go to a different host the user just typed.
    if (readItem('token').trim() === '') {
      respond({ ok: false, message: 'Paste an API token. The help above says where to create one.' });
      return;
    }
    if (!tandoor.sameHost(server, savedServer())) {
      respond({ ok: false, message: 'Paste the API token for this server. The saved token belongs to ' + tandoor.hostOf(savedServer()) + '.' });
      return;
    }
    token = readItem('token').trim();
  }
  var settings = message.settings === undefined ? loadSettings() : tandoor.cleanSettings(message.settings);
  http({
    method: 'GET',
    server: server,
    token: token,
    path: '/api/recipe/?page_size=1',
    timeout: REQUEST_TIMEOUT_MS,
    context: 'save'
  }, function (err, data) {
    if (err) {
      respond({ ok: false, message: err.text });
      return;
    }
    if (!tandoor.isObject(data) || typeof data.count !== 'number') {
      respond({ ok: false, message: tandoor.errorFor(-1, 'save').text });
      return;
    }
    var stored = writeItem('server', server) && writeItem('token', token) && writeItem('settings', JSON.stringify(settings));
    if (!stored) {
      respond({ ok: false, message: 'The phone could not save these settings. Try again.' });
      return;
    }
    accountChanged();
    respond({ ok: true, message: tandoor.connectedMessage(data.count), count: data.count, server: server });
  });
};

// Forgets the token; the server address stays so the page can fill it in again.
PAGE.signout = function (_message, respond) {
  removeItem('token');
  accountChanged();
  respond({ ok: true });
};

Pebble.addEventListener('configmessage', function (e) {
  var message = tandoor.isObject(e.data) ? e.data : {};
  var handler = typeof message.type === 'string' ? PAGE[message.type] : null;
  if (!handler) {
    e.respond({ ok: false, message: 'Unknown request.' });
    return;
  }
  try {
    handler(message, function (result) {
      e.respond(result);
    });
  } catch (err) {
    console.log('Settings request failed: ' + err);
    e.respond({ ok: false, message: 'That didn\u2019t work. Try again.' });
  }
});

// The watch holds its messages until this arrives, or for 6 seconds (Pebble's "waiting for PebbleKit JS" rule).
Pebble.addEventListener('ready', function () {
  enqueue([{ Cmd: CMD_READY }]);
});

Pebble.addEventListener('appmessage', function (e) {
  var payload = e.payload || {};
  try {
    switch (payload.Cmd) {
      case CMD_HELLO:
        enqueue(statusMessages());
        break;
      case CMD_LIST:
        onList(payload);
        break;
      case CMD_RECIPE:
        onRecipe(payload);
        break;
      case CMD_COOKED:
        onCooked(payload);
        break;
      default:
        console.log('Unknown command from the watch: ' + payload.Cmd);
    }
  } catch (err) {
    console.log('Command ' + payload.Cmd + ' failed: ' + err);
  }
});
