const test = require('node:test');
const assert = require('node:assert/strict');
const t = require('../src/pkjs/tandoor.js');

const { KIND, LIMIT, ERR } = t;
const TODAY = '2026-11-19'; // a Thursday

// Tandoor's JSON, trimmed to what a real answer carries around the fields the app reads.

const USER = { id: 1, username: 'cook', first_name: '', last_name: '', display_name: 'cook', is_staff: true, is_superuser: true, is_active: true };

function food(name, plural = null) {
  return {
    id: 1, name, plural_name: plural, description: '', recipe: null, url: '', properties: [], properties_food_amount: 100.0,
    properties_food_unit: null, fdc_id: null, food_onhand: false, supermarket_category: null, parent: null, numchild: 0,
    inherit_fields: [], full_name: name, ignore_shopping: false, substitute: [], substitute_siblings: false,
    substitute_children: false, substitute_onhand: false, child_inherit_fields: [], open_data_slug: null, shopping_lists: [],
  };
}

function unit(name, plural = null) {
  return { id: 1, name, plural_name: plural, description: null, base_unit: null, open_data_slug: null };
}

function ingredient(o = {}) {
  return {
    id: 6, food: null, unit: null, amount: 1.0, conversions: [], note: '', order: 0, is_header: false, no_amount: false,
    original_text: null, used_in_recipes: [{ id: 2, name: 'Air Fryer Frozen Burger Patties' }], checked: false, ...o,
  };
}

function step(o = {}) {
  const text = o.text === undefined ? '' : o.text;
  const { text: _text, ...rest } = o;
  return {
    id: 6, name: '', instruction: text, ingredients: [], instructions_markdown: text === '' ? '' : `<p>${text}</p>`, time: 0,
    order: 0, show_as_header: true, file: null, step_recipe: null, step_recipe_data: null, show_ingredients_table: true, ...rest,
  };
}

function recipe(o = {}) {
  return {
    id: 2, name: 'Air Fryer Frozen Burger Patties', description: '1/3 lb frozen burger patties cooked straight from frozen in an air fryer at 360 °F.',
    image: null, keywords: [], steps: [], working_time: 0, waiting_time: 12, created_by: USER, source_url: null, internal: false,
    show_ingredient_overview: true, nutrition: null, properties: [], food_properties: {}, servings: 1, file_path: '', servings_text: 'patty',
    diameter: 0, diameter_text: '', rating: null, last_cooked: null, private: false, shared: [], ...o,
  };
}

function overview(id, name, o = {}) {
  return {
    id, name, description: '', image: null, keywords: [], working_time: 0, waiting_time: 0, created_by: USER, internal: false,
    private: false, servings: 1, servings_text: '', rating: null, last_cooked: null, new: false, ...o,
  };
}

function page(results, o = {}) {
  return { count: results.length, next: null, previous: null, results, ...o };
}

// The recipe from a live Tandoor server: four steps, two ingredients.
function burgers() {
  return recipe({
    steps: [
      step({ id: 6, text: 'Preheat the air fryer to 360 °F.' }),
      step({
        id: 7, order: 1, time: 1, text: 'Put the frozen patties in the air fryer and cook for 1 minute.',
        ingredients: [ingredient({ id: 6, food: food('frozen burger patty'), amount: 1.0, note: '1/3 lb' })],
      }),
      step({
        id: 8, order: 2, time: 5, text: 'Season the tops of the patties, then cook for 5 more minutes.',
        ingredients: [ingredient({ id: 7, food: food('seasoning'), amount: 0.0, no_amount: true, note: 'to taste, for both sides' })],
      }),
      step({ id: 9, order: 3, time: 6, text: 'Flip the patties, season the other side, then cook for 6 more minutes.' }),
    ],
  });
}

// Server address -------------------------------------------------------------------------------------

test('normalizeServer adds https, drops slashes, /api and anything after it', () => {
  const cases = [
    ['tandoor.example.com', 'https://tandoor.example.com'],
    ['  https://Tandoor.Example.com/  ', 'https://tandoor.example.com'],
    ['https://tandoor.example.com/api', 'https://tandoor.example.com'],
    ['https://tandoor.example.com/api/', 'https://tandoor.example.com'],
    ['https://tandoor.example.com/api/recipe/?page=2', 'https://tandoor.example.com'],
    ['https://tandoor.example.com///', 'https://tandoor.example.com'],
    ['http://192.168.1.5:8080/', 'http://192.168.1.5:8080'],
    ['https://example.com/tandoor/api/', 'https://example.com/tandoor'],
    ['https://example.com/api-tools', 'https://example.com/api-tools'],
    ['https://example.com?x=1#top', 'https://example.com'],
    ['http://[::1]:8000', 'http://[::1]:8000'],
    ['localhost:8765', 'https://localhost:8765'],
  ];
  for (const [input, want] of cases) assert.equal(t.normalizeServer(input), want, input);
});

test('normalizeServer rejects what cannot be an address', () => {
  for (const input of ['', '   ', null, undefined, 'ftp://example.com', 'https://', 'a b.com', 'user:pw@example.com', 'https://exa_mple$.com', 'https://example.com:99999']) {
    assert.equal(t.normalizeServer(input), '', String(input));
  }
});

test('hostOf gives the host name, with the port when there is one', () => {
  assert.equal(t.hostOf('https://tandoor.example.com'), 'tandoor.example.com');
  assert.equal(t.hostOf('http://192.168.1.5:8080/tandoor'), '192.168.1.5:8080');
  assert.equal(t.hostOf(''), '');
  assert.equal(t.sameHost('https://tandoor.example.com', 'http://TANDOOR.example.com/x'), true);
  assert.equal(t.sameHost('https://tandoor.example.com', 'https://other.example'), false);
  assert.equal(t.sameHost('', ''), false);
});

// Days ------------------------------------------------------------------------------------------------

test('isoDate uses the local calendar date and addDays crosses months and years', () => {
  assert.equal(t.isoDate(new Date(2026, 10, 19, 23, 59)), '2026-11-19');
  assert.equal(t.isoDate(new Date(2026, 0, 5, 0, 1)), '2026-01-05');
  assert.equal(t.addDays('2026-11-19', 6), '2026-11-25');
  assert.equal(t.addDays('2026-11-28', 6), '2026-12-04');
  assert.equal(t.addDays('2026-12-30', 3), '2027-01-02');
  assert.equal(t.addDays('2028-02-27', 2), '2028-02-29');
  assert.equal(t.addDays('nonsense', 1), '');
});

test('dayLabel says Today, Tomorrow, or the weekday and date', () => {
  assert.equal(t.dayLabel('2026-11-19', TODAY), 'Today');
  assert.equal(t.dayLabel('2026-11-20', TODAY), 'Tomorrow');
  assert.equal(t.dayLabel('2026-11-21', TODAY), 'Sat 21');
  assert.equal(t.dayLabel('2026-11-25', TODAY), 'Wed 25');
  assert.equal(t.dayLabel('2026-11-18', TODAY), 'Wed 18');
  assert.equal(t.dayLabel('2026-08-21', '2026-08-17'), 'Fri 21');
});

// List requests ---------------------------------------------------------------------------------------

test('listPath builds the request of every list kind', () => {
  const o = { today: TODAY };
  assert.equal(t.listPath(KIND.PLAN, 0, 1, '', o), '/api/meal-plan/?from_date=2026-11-19&to_date=2026-11-25&page_size=200');
  assert.equal(t.listPath(KIND.RECENT, 0, 1, '', o), '/api/recipe/?sort_order=-lastviewed&page=1&page_size=20');
  assert.equal(t.listPath(KIND.ALL, 0, 3, '', o), '/api/recipe/?sort_order=name&page=3&page_size=20');
  assert.equal(t.listPath(KIND.BOOKS, 0, 1, '', o), '/api/recipe-book/?page=1&page_size=20');
  assert.equal(t.listPath(KIND.BOOK, 4, 2, '', o), '/api/recipe/?books=4&page=2&page_size=20');
  assert.equal(t.listPath(KIND.BOOK, 4, 1, '', { filterId: 7 }), '/api/recipe/?filter=7&page=1&page_size=20');
  assert.equal(t.listPath(KIND.KEYWORDS, 0, 1, '', o), '/api/keyword/?root=0&page=1&page_size=20');
  assert.equal(t.listPath(KIND.KEYWORDS, 9, 2, '', o), '/api/keyword/?root=9&page=2&page_size=20');
  assert.equal(t.listPath(KIND.KEYWORD, 17, 1, '', o), '/api/recipe/?keywords=17&page=1&page_size=20');
  assert.equal(t.listPath(KIND.SEARCH, 0, 1, 'crème brûlée & más', o), `/api/recipe/?query=${encodeURIComponent('crème brûlée & más')}&page=1&page_size=20`);
  assert.equal(t.listPath(KIND.SEARCH, 0, 1, '  ', o), '/api/recipe/?page=1&page_size=20');
  assert.equal(t.listPath(99, 0, 1, '', o), '');
});

test('listPath keeps the page a positive integer and the meal plan on the phone\'s week', () => {
  assert.equal(t.listPath(KIND.ALL, 0, 0, '', {}), '/api/recipe/?sort_order=name&page=1&page_size=20');
  assert.equal(t.listPath(KIND.ALL, 0, 'x', '', {}), '/api/recipe/?sort_order=name&page=1&page_size=20');
  assert.equal(t.listPath(KIND.PLAN, 0, 5, '', { today: '2026-12-29' }), '/api/meal-plan/?from_date=2026-12-29&to_date=2027-01-04&page_size=200');
});

// List records ----------------------------------------------------------------------------------------

test('recipe subtitles give the total time and the servings', () => {
  assert.equal(t.recipeSubtitle({ working_time: 10, waiting_time: 15, servings: 4, servings_text: '' }), '25 min · 4 servings');
  assert.equal(t.recipeSubtitle({ working_time: 20, waiting_time: 50, servings: 1, servings_text: '' }), '1 h 10 min · 1 serving');
  assert.equal(t.recipeSubtitle({ working_time: 60, waiting_time: 60, servings: 0 }), '2 h');
  assert.equal(t.recipeSubtitle({ working_time: 0, waiting_time: 12, servings: 1, servings_text: 'patty' }), '12 min · 1 patty');
  assert.equal(t.recipeSubtitle({ working_time: 0, waiting_time: 0, servings: 12, servings_text: 'cookies' }), '12 cookies');
  assert.equal(t.recipeSubtitle({ working_time: 5, waiting_time: 0, servings: 0, servings_text: '' }), '5 min');
  assert.equal(t.recipeSubtitle({}), '');
  assert.equal(t.formatMinutes(59), '59 min');
  assert.equal(t.formatMinutes(60), '1 h');
  assert.equal(t.formatMinutes(1450), '24 h 10 min');
});

test('parseList turns recipes into records and reads paging', () => {
  const answer = page(
    [
      overview(2, 'Air Fryer Frozen Burger Patties', { waiting_time: 12, servings_text: 'patty' }),
      overview(5, 'Crème &amp; Brûlée', { working_time: 10, waiting_time: 15, servings: 4 }),
    ],
    { count: 42, next: 'https://x/api/recipe/?page=2' },
  );
  const list = t.parseList(KIND.ALL, answer, { page: 1 });
  assert.deepEqual(list.records, [
    ['r', '2', 'Air Fryer Frozen Burger Patties', '12 min · 1 patty'],
    ['r', '5', 'Crème & Brûlée', '25 min · 4 servings'],
  ]);
  assert.equal(list.index, 0);
  assert.equal(list.more, true);
  assert.equal(list.total, 42);
  const last = t.parseList(KIND.ALL, page([overview(9, 'Z')], { count: 21 }), { page: 2 });
  assert.equal(last.index, 20);
  assert.equal(last.more, false);
});

test('parseList reads books and learns which ones are defined by a saved filter', () => {
  const answer = page([
    { id: 1, name: 'Favorites', description: '', shared: [], created_by: USER, filter: null, order: 0 },
    { id: 3, name: 'Quick', description: '', shared: [], created_by: USER, filter: { id: 7, name: 'f', search: '{}', shared: [], created_by: 1 }, order: 0 },
  ]);
  const list = t.parseList(KIND.BOOKS, answer, { page: 1 });
  assert.deepEqual(list.records, [['b', '1', 'Favorites', ''], ['b', '3', 'Quick', '']]);
  assert.deepEqual(list.learned.books, { 1: 0, 3: 7 });
  assert.equal(t.bookFilterId({ filter: { id: 7 } }), 7);
  assert.equal(t.bookFilterId({ filter: 9 }), 9);
  assert.equal(t.bookFilterId({ filter: null }), 0);
});

test('parseList marks keywords with children, and starts a parent\'s list with All <parent>', () => {
  const keywords = page(
    [
      { id: 4, name: 'Asian', label: 'Asian', parent: 1, numchild: 2 },
      { id: 17, name: 'Quick', label: 'Quick', parent: 1, numchild: 0 },
    ],
    { count: 25, next: 'next' },
  );
  const first = t.parseList(KIND.KEYWORDS, keywords, { page: 1, parentId: 1, parentName: 'Cuisine' });
  assert.deepEqual(first.records, [
    ['k', '1', 'All Cuisine', ''],
    ['f', '4', 'Asian', ''],
    ['k', '17', 'Quick', ''],
  ]);
  assert.equal(first.index, 0);
  assert.equal(first.total, 26);
  assert.deepEqual(first.learned.keywords, { 4: 'Asian', 17: 'Quick' });
  // The page after it starts one record later, because "All" took a place on page 1.
  const second = t.parseList(KIND.KEYWORDS, keywords, { page: 2, parentId: 1, parentName: 'Cuisine' });
  assert.equal(second.records.length, 2);
  assert.equal(second.index, 21);
  // The top level has no parent record.
  const top = t.parseList(KIND.KEYWORDS, keywords, { page: 1, parentId: 0 });
  assert.equal(top.records[0][2], 'Asian');
  assert.equal(top.total, 25);
});

test('a list stops at 200 records', () => {
  const twenty = page(Array.from({ length: 20 }, (_, i) => overview(i + 1, `R${i}`)), { count: 500, next: 'next' });
  const tenth = t.parseList(KIND.ALL, twenty, { page: 10 });
  assert.equal(tenth.index, 180);
  assert.equal(tenth.records.length, 20);
  assert.equal(tenth.more, false);
  assert.equal(tenth.total, 500);
  assert.equal(t.parseList(KIND.ALL, twenty, { page: 9 }).more, true);
  // "All <parent>" shifts the later pages by one, so the last page loses a record.
  const keywords = page(Array.from({ length: 20 }, (_, i) => ({ id: i + 1, name: `K${i}`, numchild: 0 })), { count: 500, next: 'next' });
  const last = t.parseList(KIND.KEYWORDS, keywords, { page: 10, parentId: 3, parentName: 'P' });
  assert.equal(last.index, 181);
  assert.equal(last.records.length, 19);
  assert.equal(last.more, false);
});

test('parseList survives answers that are not lists', () => {
  for (const answer of [null, undefined, 'x', 5, {}, { results: 'x' }, { results: [null, 5, {}, { id: 'abc' }] }]) {
    const list = t.parseList(KIND.ALL, answer, { page: 1 });
    assert.deepEqual(list.records, []);
    assert.equal(list.more, false);
    assert.equal(list.total, -1);
  }
});

// Meal plan -------------------------------------------------------------------------------------------

function meal(id, name, order, time = null) {
  return { id, name, order, time, color: null, created_by: 1 };
}

function planEntry(id, from, mealType, o = {}) {
  return {
    id, title: '', recipe: null, servings: 1, note: '', note_markdown: '', from_date: from, to_date: from, meal_type: mealType,
    created_by: 1, meal_type_name: mealType.name, shopping: false, ...o,
  };
}

const BREAKFAST = meal(1, 'Breakfast', 0, '08:00:00');
const LUNCH = meal(2, 'Lunch', 1, '12:30:00');
const DINNER = meal(3, 'Dinner', 2, '18:30:00');

test('the meal plan is sorted by day and meal, and labelled with the day', () => {
  const answer = page([
    planEntry(1, '2026-11-25T18:30:00+01:00', DINNER, { recipe: overview(8, 'Pad Thai'), recipe_name: 'Pad Thai' }),
    planEntry(2, '2026-11-19T18:30:00+01:00', DINNER, { recipe: overview(1, 'Tikka Masala'), recipe_name: 'Tikka Masala' }),
    planEntry(3, '2026-11-20T12:30:00+01:00', LUNCH, { recipe: overview(3, 'Greek Salad'), recipe_name: 'Greek Salad' }),
    planEntry(4, '2026-11-19T12:30:00+01:00', LUNCH, { recipe: overview(2, 'Soup'), recipe_name: 'Soup' }),
    planEntry(5, '2026-11-20T08:00:00+01:00', BREAKFAST, { recipe: overview(17, 'Pancakes'), recipe_name: 'Pancakes' }),
    planEntry(6, '2026-11-21T18:30:00+01:00', DINNER, { title: 'Dinner at Alex\'s', note: 'Bring dessert' }),
  ]);
  const records = t.parsePlan(answer, TODAY);
  assert.deepEqual(records, [
    ['r', '2', 'Soup', 'Today · Lunch'],
    ['r', '1', 'Tikka Masala', 'Today · Dinner'],
    ['r', '17', 'Pancakes', 'Tomorrow · Breakfast'],
    ['r', '3', 'Greek Salad', 'Tomorrow · Lunch'],
    ['n', '6', 'Dinner at Alex\'s', 'Sat 21 · Dinner'],
    ['r', '8', 'Pad Thai', 'Wed 25 · Dinner'],
  ]);
});

test('meals of one day follow the meal type order, then its time', () => {
  const noon = meal(5, 'Brunch', 1, '10:30:00');
  const answer = page([
    planEntry(1, '2026-11-19T12:30:00+01:00', LUNCH, { title: 'a' }),
    planEntry(2, '2026-11-19T10:30:00+01:00', noon, { title: 'b' }),
    planEntry(3, '2026-11-19T08:00:00+01:00', meal(9, 'Snack', 3, null), { title: 'c' }),
    planEntry(4, '2026-11-19T18:30:00+01:00', DINNER, { title: 'd' }),
  ]);
  assert.deepEqual(t.parsePlan(answer, TODAY).map((r) => r[2]), ['b', 'a', 'd', 'c']);
});

test('an entry without a recipe becomes a note record named by its title, note or meal', () => {
  const answer = page([
    planEntry(1, '2026-11-19', DINNER, { title: 'Out' }),
    planEntry(2, '2026-11-19', DINNER, { note: '<p>Leftovers</p>\n<p>second line</p>' }),
    planEntry(3, '2026-11-19', LUNCH),
    planEntry(4, 'garbage', LUNCH),
  ]);
  assert.deepEqual(t.parsePlan(answer, TODAY), [
    ['n', '3', 'Lunch', 'Today · Lunch'],
    ['n', '1', 'Out', 'Today · Dinner'],
    ['n', '2', 'Leftovers', 'Today · Dinner'],
  ]);
});

test('pageOf cuts a whole list into pages of 20', () => {
  const records = Array.from({ length: 45 }, (_, i) => ['n', String(i), `n${i}`, '']);
  const second = t.pageOf(records, 2);
  assert.equal(second.records.length, 20);
  assert.equal(second.records[0][1], '20');
  assert.equal(second.index, 20);
  assert.equal(second.more, true);
  assert.equal(second.total, 45);
  const third = t.pageOf(records, 3);
  assert.equal(third.records.length, 5);
  assert.equal(third.more, false);
  assert.deepEqual(t.pageOf([], 1), { records: [], index: 0, more: false, total: 0 });
});

// Text ------------------------------------------------------------------------------------------------

test('htmlToText turns paragraphs into blank lines and <br> into line breaks', () => {
  assert.equal(t.htmlToText('<p>First paragraph.</p>\n<p>Second one,<br>on two lines.</p>'), 'First paragraph.\n\nSecond one,\non two lines.');
  assert.equal(t.htmlToText('<p>One</p><p></p><p></p><p>Two</p>'), 'One\n\nTwo');
  assert.equal(t.htmlToText('a<br><br>b'), 'a\n\nb');
});

test('htmlToText writes list items as bullet lines, and numbers an ordered list', () => {
  assert.equal(
    t.htmlToText('<p>Mix:</p>\n<ul>\n<li>2 cups flour</li>\n<li>1 tsp <strong>salt</strong></li>\n</ul>\n<p>Then bake.</p>'),
    'Mix:\n\n• 2 cups flour\n• 1 tsp salt\n\nThen bake.',
  );
  assert.equal(t.htmlToText('<ol><li>Chop</li><li>Fry</li></ol>'), '1. Chop\n2. Fry');
  assert.equal(t.htmlToText('<ul><li>a<ul><li>b</li></ul></li><li>c</li></ul>'), '• a\n  • b\n• c');
  assert.equal(t.htmlToText('<ul><li><p>loose</p></li><li><p>items</p></li></ul>'), '• loose\n• items');
});

test('htmlToText strips tags and decodes named and numeric entities', () => {
  assert.equal(t.htmlToText('<p>Bake at 450&deg;F &amp; rest 5&nbsp;min &#8211; &#x2014; done&hellip;</p>'), 'Bake at 450°F & rest 5 min – — done…');
  assert.equal(t.htmlToText('<p>&lt;b&gt;not a tag&lt;/b&gt; &frac12; cup &eacute;&Eacute;</p>'), '<b>not a tag</b> ½ cup éÉ');
  assert.equal(t.htmlToText('<p>Use <a href="https://example.com/a?b=1&amp;c=2" title="x > y">the link</a> and <img src="a.png" alt="photo"> <code>code</code>.</p>'), 'Use the link and code.');
  assert.equal(t.htmlToText('<p>Keep &unknown; and &amp;lt; as typed</p>'), 'Keep &unknown; and &lt; as typed');
  assert.equal(t.htmlToText('<p>3 < 4 and 5 > 2</p>'), '3 < 4 and 5 > 2');
  assert.equal(t.htmlToText('<style>p { color: red }</style><script>alert(1)</script><p>Visible</p><!-- hidden -->'), 'Visible');
});

test('htmlToText collapses whitespace and trims', () => {
  assert.equal(t.htmlToText('  <p>  Lots   of\n\t space  </p>\n\n\n  <p>Next</p>  '), 'Lots of space\n\nNext');
  assert.equal(t.htmlToText('<b>bold</b> <i>and</i> <em>italic</em>'), 'bold and italic');
  assert.equal(t.htmlToText(''), '');
  assert.equal(t.htmlToText(null), '');
  assert.equal(t.htmlToText('<p></p>'), '');
});

test('htmlToText keeps the lines of text that is not HTML', () => {
  assert.equal(t.htmlToText('Line one\nLine   two\n\n\n\nPara &amp; more'), 'Line one\nLine two\n\nPara & more');
});

test('decodeEntities covers the Latin-1 table', () => {
  assert.equal(t.decodeEntities('&nbsp;&yuml;&eacute;&deg;&times;&divide;&szlig;'), '\u00a0ÿé°×÷ß');
  assert.equal(t.decodeEntities('&#65;&#x42;&#128512;&#0;&#xD800;'), 'AB😀');
  assert.equal(t.decodeEntities('R&D & AT&T; &constructor; &toString;'), 'R&D & AT&T; &constructor; &toString;');
});

// Durations -------------------------------------------------------------------------------------------

test('findDuration reads the first duration in a step', () => {
  const cases = [
    ['Simmer for 5 minutes.', 300],
    ['Roast 30 min, then rest.', 1800],
    ['Wait 2 mins', 120],
    ['Chill for 3 hours', 10800],
    ['Rest 1 hr.', 3600],
    ['Marinate 2 hrs or overnight', 7200],
    ['Proof for 2h', 7200],
    ['Blitz for 30 sec', 30],
    ['Blitz for 45 secs', 45],
    ['Wait 10 seconds', 10],
    ['Bake at 180°C for 12 min until golden', 720],
    ['Mix for 1 minute, then simmer for 20 minutes', 60],
  ];
  for (const [text, want] of cases) assert.equal(t.findDuration(text), want, text);
});

test('findDuration takes the lower end of a range', () => {
  const cases = [
    ['Simmer for 20-25 minutes', 1200],
    ['Simmer for 20 – 25 minutes', 1200],
    ['Rest for 1-2 hours', 3600],
    ['Cook 4 to 5 minutes per side', 240],
    ['Stir for two to three minutes', 120],
    ['Bake for 10 or 12 min', 600],
  ];
  for (const [text, want] of cases) assert.equal(t.findDuration(text), want, text);
});

test('findDuration reads fractions, number words and compound durations', () => {
  const cases = [
    ['Roast for 1½ hours', 5400],
    ['Roast for 1 1/2 hours', 5400],
    ['Roast for 1/2 hour', 1800],
    ['Cook for 0.5 hours', 1800],
    ['Cook for 1,5 hours', 5400],
    ['Rest ¼ hour', 900],
    ['Leave for half an hour', 1800],
    ['Leave for an hour', 3600],
    ['Leave for an hour and a half', 5400],
    ['Simmer for twenty-five minutes', 1500],
    ['Simmer for ten minutes', 600],
    ['Bake 1 hour 15 minutes', 4500],
    ['Bake 1 hour and 30 minutes until done', 5400],
    ['Bake 2 h 10 min 30 sec', 7830],
    ['Boil for a 5-minute rest', 300],
  ];
  for (const [text, want] of cases) assert.equal(t.findDuration(text), want, text);
});

test('findDuration ignores what is not a time', () => {
  for (const text of ['', null, 'Add 2 cups of flour', 'Add 2 hot peppers', 'Use 3 mint leaves', 'Cut 5 m of string', 'Preheat to 360 °F', 'Add the 1st minute', 'Cook 0 minutes']) {
    assert.equal(t.findDuration(text), 0, String(text));
  }
  assert.equal(t.findDuration('Marinate for 400 hours'), 359999);
});

// Recipes ---------------------------------------------------------------------------------------------

test('a recipe as a live Tandoor server sends it', () => {
  const r = t.parseRecipe(burgers());
  assert.equal(r.id, 2);
  assert.equal(r.name, 'Air Fryer Frozen Burger Patties');
  assert.equal(r.description, '1/3 lb frozen burger patties cooked straight from frozen in an air fryer at 360 °F.');
  assert.equal(r.servings, 100);
  assert.equal(r.servingsText, 'patty');
  assert.equal(r.workingTime, 0);
  assert.equal(r.waitingTime, 12);
  assert.equal(r.rating, 0);
  assert.equal(r.keywords, '');
  assert.deepEqual(r.steps, [
    { name: '', text: 'Preheat the air fryer to 360 °F.', seconds: 0, flags: 0 },
    { name: '', text: 'Put the frozen patties in the air fryer and cook for 1 minute.', seconds: 60, flags: 0 },
    { name: '', text: 'Season the tops of the patties, then cook for 5 more minutes.', seconds: 300, flags: 0 },
    { name: '', text: 'Flip the patties, season the other side, then cook for 6 more minutes.', seconds: 360, flags: 0 },
  ]);
  assert.deepEqual(r.ingredients, [
    ['1', '0', '1000', '', '', 'frozen burger patty', '', '1/3 lb'],
    ['2', '2', '0', '', '', 'seasoning', '', 'to taste, for both sides'],
  ]);
});

test('a step\'s timer is Tandoor\'s time, else the text\'s first duration, flagged', () => {
  const r = t.parseRecipe(recipe({
    steps: [
      step({ text: 'Roast for 12-15 minutes.', time: 15 }),
      step({ text: 'Simmer for 20-25 minutes, stirring.' }),
      step({ text: 'Stir in the cream.' }),
      step({ text: 'Rest 1 hour.', time: 0 }),
    ],
  }));
  assert.deepEqual(r.steps.map((s) => [s.seconds, s.flags]), [[900, 0], [1200, t.STEP_FROM_TEXT], [0, 0], [3600, t.STEP_FROM_TEXT]]);
});

test('ingredient records carry amounts x1000, plural names, headers and the missing amount', () => {
  const r = t.parseRecipe(recipe({
    steps: [
      step({
        text: 'Mix.',
        ingredients: [
          ingredient({ is_header: true, note: 'For the sauce', food: null, unit: null, amount: 0 }),
          ingredient({ food: food('egg', 'eggs'), unit: null, amount: 2 }),
          ingredient({ food: food('flour'), unit: unit('cup', 'cups'), amount: 0.333 }),
          ingredient({ food: food('milk'), unit: unit('cup', 'cups'), amount: 1.5, note: 'warm' }),
          ingredient({ food: food('sugar'), unit: unit('tablespoon', 'tablespoons'), amount: 2.25 }),
          ingredient({ food: food('butter'), unit: unit('stick', 'sticks'), amount: 0.5 }),
          ingredient({ food: food('salt'), amount: 0, no_amount: false }),
          ingredient({ food: food('pepper'), amount: 3, no_amount: true }),
          ingredient({ food: null, unit: null, note: '', amount: 1 }),
          ingredient({ food: food('cilantro'), amount: '1,5' }),
        ],
      }),
    ],
  }));
  assert.deepEqual(r.ingredients, [
    ['0', '1', '0', '', '', 'For the sauce', '', ''],
    ['0', '0', '2000', '', '', 'egg', 'eggs', ''],
    ['0', '0', '333', 'cup', 'cups', 'flour', '', ''],
    ['0', '0', '1500', 'cup', 'cups', 'milk', '', 'warm'],
    ['0', '0', '2250', 'tablespoon', 'tablespoons', 'sugar', '', ''],
    ['0', '0', '500', 'stick', 'sticks', 'butter', '', ''],
    ['0', '2', '0', '', '', 'salt', '', ''],
    ['0', '2', '0', '', '', 'pepper', '', ''],
    ['0', '0', '1500', '', '', 'cilantro', '', ''],
  ]);
});

test('a step that includes another recipe is replaced by its steps, named <recipe> · <step>', () => {
  const sub = {
    id: 21,
    name: 'Garam Masala',
    steps: [
      step({ id: 30, name: 'Toast', text: 'Toast the spices for about 2 minutes.', ingredients: [ingredient({ food: food('cumin'), unit: unit('tablespoon', 'tablespoons'), amount: 2 })] }),
      step({ id: 31, name: '', text: 'Grind them.' }),
    ],
  };
  const r = t.parseRecipe(recipe({
    steps: [
      step({ name: 'Marinate', text: 'Marinate for 30 minutes.', ingredients: [ingredient({ food: food('chicken'), amount: 1 })] }),
      step({ name: 'Spices', text: '', step_recipe: 21, step_recipe_data: sub, ingredients: [ingredient({ food: food('salt'), amount: 1 })] }),
      step({ name: 'Serve', text: 'Serve hot.' }),
    ],
  }));
  assert.deepEqual(r.steps.map((s) => s.name), ['Marinate', 'Garam Masala · Toast', 'Garam Masala', 'Serve']);
  assert.deepEqual(r.steps.map((s) => s.seconds), [1800, 120, 0, 0]);
  // The sub-recipe's ingredients follow their flattened steps; the including step's own stay, with no step.
  assert.deepEqual(r.ingredients.map((i) => [i[0], i[5]]), [['0', 'chicken'], ['', 'salt'], ['1', 'cumin']]);
});

test('an including step with text of its own keeps it as a step before the included ones', () => {
  const sub = { id: 21, name: 'Sauce', steps: [step({ name: 'Simmer', text: 'Simmer.' })] };
  const r = t.parseRecipe(recipe({
    steps: [step({ name: 'Make the sauce', text: 'Use a heavy pan.', step_recipe: 21, step_recipe_data: sub })],
  }));
  assert.deepEqual(r.steps.map((s) => [s.name, s.text]), [['Make the sauce', 'Use a heavy pan.'], ['Sauce · Simmer', 'Simmer.']]);
});

test('a step that names a recipe Tandoor did not expand stays a normal step', () => {
  const r = t.parseRecipe(recipe({ steps: [step({ name: 'Paste', text: 'Make the paste.', step_recipe: 21, step_recipe_data: null })] }));
  assert.deepEqual(r.steps.map((s) => s.name), ['Paste']);
});

test('steps and ingredients are cut at 40 and 80', () => {
  const manyIngredients = Array.from({ length: 30 }, (_, i) => ingredient({ food: food(`food ${i}`), amount: 1 }));
  const steps = Array.from({ length: 45 }, (_, i) => step({ text: `Step ${i}.`, ingredients: manyIngredients }));
  const r = t.parseRecipe(recipe({ steps }));
  assert.equal(r.steps.length, 40);
  assert.equal(r.ingredients.length, 80);
  // Ingredients of the first steps have their step; none is attached to a step that was cut.
  assert.equal(r.ingredients[0][0], '0');
  assert.ok(r.ingredients.every((i) => i[0] === '' || Number(i[0]) < 40));
});

test('empty steps are dropped and a recipe without steps still parses', () => {
  const r = t.parseRecipe(recipe({ steps: [step({}), step({ text: 'Only this.' }), null, 5] }));
  assert.deepEqual(r.steps.map((s) => s.text), ['Only this.']);
  for (const detail of [null, undefined, {}, [], 'x', recipe({ steps: null })]) {
    const empty = t.parseRecipe(detail);
    assert.deepEqual(empty.steps, []);
    assert.deepEqual(empty.ingredients, []);
  }
});

test('the recipe head reads rating, keywords and times', () => {
  const r = t.parseRecipe(recipe({
    rating: 3.6,
    servings: 6,
    servings_text: '',
    working_time: 25,
    waiting_time: 45,
    keywords: [{ id: 1, name: 'main course', label: 'main course' }, { id: 2, name: 'american', label: 'american' }, { id: 3, name: '✨ AI' }],
  }));
  assert.equal(r.rating, 4);
  assert.equal(r.servings, 600);
  assert.equal(r.workingTime, 25);
  assert.equal(r.waitingTime, 45);
  assert.equal(r.keywords, 'main course, american, ✨ AI');
  assert.equal(t.parseRecipe(recipe({ rating: 9 })).rating, 5);
  assert.equal(t.parseRecipe(recipe({ servings: 2.5 })).servings, 250);
});

test('keywords that do not fit are left out whole', () => {
  const keywords = ['a very long keyword name', 'another long keyword name', 'one more long keyword', 'short'].map((name, i) => ({ id: i, label: name }));
  const line = t.parseRecipe(recipe({ keywords })).keywords;
  assert.equal(line, 'a very long keyword name, another long keyword name');
  assert.ok(t.utf8Length(line) <= LIMIT.keywords);
});

test('every string of a recipe fits the watch\'s buffers, on a character boundary', () => {
  const long = (s) => s.repeat(400);
  const r = t.parseRecipe(recipe({
    name: long('Crème brûlée '),
    description: long('Ünïcödé 😀 '),
    servings_text: long('pieces'),
    keywords: [{ id: 1, label: long('日本語') }],
    steps: [
      step({ name: long('Étape'), text: long('Mélanger 😀 les œufs. ') }),
      step({
        text: 'Go.',
        ingredients: [ingredient({ food: food(long('œuf'), long('œufs')), unit: unit(long('cuillère'), long('cuillères')), note: long('très frais'), amount: 2 })],
      }),
    ],
  }));
  assert.ok(t.utf8Length(r.name) <= LIMIT.name);
  assert.ok(t.utf8Length(r.description) <= LIMIT.description);
  assert.ok(t.utf8Length(r.servingsText) <= LIMIT.servingsText);
  assert.ok(t.utf8Length(r.keywords) <= LIMIT.keywords);
  assert.ok(t.utf8Length(r.steps[0].name) <= LIMIT.stepName);
  assert.ok(t.utf8Length(r.steps[0].text) <= LIMIT.stepText);
  const [, , , unitName, unitPlural, foodName, foodPlural, note] = r.ingredients[0];
  assert.ok(t.utf8Length(unitName) <= LIMIT.unit);
  assert.ok(t.utf8Length(unitPlural) <= LIMIT.unit);
  assert.ok(t.utf8Length(foodName) <= LIMIT.food);
  assert.ok(t.utf8Length(foodPlural) <= LIMIT.food);
  assert.ok(t.utf8Length(note) <= LIMIT.note);
  // No string ends in half a character: re-encoding keeps every one of them.
  for (const s of [r.name, r.description, r.steps[0].text, foodName, note]) {
    assert.equal(Buffer.from(s, 'utf8').toString('utf8'), s);
    assert.ok(!s.includes('\ufffd'));
  }
});

// Truncation ------------------------------------------------------------------------------------------

test('truncateUtf8 cuts on a character boundary and marks the cut', () => {
  assert.equal(t.truncateUtf8('short', 63), 'short');
  assert.equal(t.truncateUtf8('exactly ten', 11), 'exactly ten');
  assert.equal(t.truncateUtf8('abcdefghij', 8), 'abcde…');
  // Two-byte characters: 5 × 2 = 10 bytes fit, so the cut keeps 3 of them and the 3-byte ellipsis.
  assert.equal(t.truncateUtf8('ééééé', 9), 'ééé…');
  assert.equal(t.truncateUtf8('ééééé', 10), 'ééééé');
  // Three-byte characters.
  assert.equal(t.truncateUtf8('日本語日本語', 10), '日本…');
  // Four-byte characters are never split.
  assert.equal(t.truncateUtf8('😀😀😀😀', 11), '😀😀…');
  assert.equal(t.truncateUtf8('😀😀😀😀', 10), '😀…');
  assert.equal(t.truncateUtf8('ab😀cd', 4), 'a…');
});

test('truncateUtf8 never exceeds the limit, whatever the characters', () => {
  const samples = ['é', '日', '😀', 'a', ' ', 'ß', '\u2019'];
  for (const limit of [3, 4, 5, 10, 23, 47, 63, 255]) {
    for (const sample of samples) {
      const out = t.truncateUtf8(sample.repeat(300), limit);
      assert.ok(t.utf8Length(out) <= limit, `${sample} × 300 at ${limit}`);
      assert.equal(Buffer.byteLength(out, 'utf8'), t.utf8Length(out));
    }
  }
});

test('truncateUtf8 does not leave a space before the ellipsis and replaces lone surrogates', () => {
  assert.equal(t.truncateUtf8('hello world', 9), 'hello…');
  assert.equal(t.truncateUtf8('a\ud83dz', 10), 'a\ufffdz');
  assert.equal(t.utf8Length('a\ud83dz'), 5);
  assert.equal(t.truncateUtf8(null, 5), '');
});

test('cleanLine makes one tidy line', () => {
  assert.equal(t.cleanLine('  Salt &amp;\n\tPepper\u00a0\u200b mix \u0007 ', 63), 'Salt & Pepper mix');
  assert.equal(t.cleanLine('a\x1fb\x1ec', 63), 'abc');
  assert.equal(t.cleanLine(null, 63), '');
});

// Packing ---------------------------------------------------------------------------------------------

function records(n, titleLength) {
  return Array.from({ length: n }, (_, i) => ['r', String(1000 + i), 'x'.repeat(titleLength), `${i} min · 4 servings`]);
}

function unpack(chunks) {
  return chunks.flatMap((chunk) => chunk.data.split('\x1e').filter(Boolean).map((r) => r.split('\x1f')));
}

test('packRecords joins fields with \\x1f, ends each record with \\x1e and keeps the order', () => {
  const chunks = t.packRecords([['r', '2', 'Burgers', '12 min'], ['b', '3', 'Books', '']]);
  assert.deepEqual(chunks, [{ start: 0, count: 2, data: 'r\x1f2\x1fBurgers\x1f12 min\x1eb\x1f3\x1fBooks\x1f\x1e' }]);
  assert.deepEqual(t.packRecords([]), []);
});

test('packRecords makes chunks of at most 1400 bytes and never splits a record', () => {
  const list = records(60, 63);
  const chunks = t.packRecords(list);
  assert.ok(chunks.length > 1);
  let next = 0;
  for (const chunk of chunks) {
    assert.ok(Buffer.byteLength(chunk.data, 'utf8') <= LIMIT.data, `chunk of ${Buffer.byteLength(chunk.data, 'utf8')} bytes`);
    assert.equal(chunk.start, next);
    assert.equal(chunk.data.split('\x1e').filter(Boolean).length, chunk.count);
    assert.ok(chunk.data.endsWith('\x1e'));
    next += chunk.count;
  }
  assert.equal(next, list.length);
  assert.deepEqual(unpack(chunks), list);
});

test('packRecords counts bytes, not characters', () => {
  // 20 records of 63 two-byte characters are 126 bytes of title each.
  const list = Array.from({ length: 20 }, (_, i) => ['r', String(i), 'é'.repeat(31), '日'.repeat(15)]);
  const chunks = t.packRecords(list);
  for (const chunk of chunks) assert.ok(Buffer.byteLength(chunk.data, 'utf8') <= LIMIT.data);
  assert.ok(chunks.length >= 2);
  assert.deepEqual(unpack(chunks), list);
  assert.throws(() => t.packRecords(list, 100), RangeError);
});

test('packRecords drops control characters from the fields', () => {
  const chunks = t.packRecords([['r', '1', 'Tab\there\nand\x00more\x1fsep\x1erec', 'line\r\nbreak']]);
  const [fields] = unpack(chunks);
  assert.equal(fields.length, 4);
  assert.equal(fields[2], 'Tab here andmoreseprec');
  assert.equal(fields[3], 'line break');
  for (const ch of fields.join('')) assert.ok(ch.charCodeAt(0) >= 0x20, 'a control character is left');
  assert.equal(chunks[0].data.split('\x1e').length, 2);
});

test('packRecords refuses a record that cannot fit a chunk', () => {
  assert.throws(() => t.packRecords([['r', '1', 'x'.repeat(2000), '']]), RangeError);
});

test('ingredient records of a big recipe pack into a few chunks that parse back', () => {
  const steps = [step({
    text: 'Mix.',
    ingredients: Array.from({ length: 80 }, (_, i) => ingredient({
      food: food(`ingredient number ${i} with a long name `.repeat(2), 'plurals of it'), unit: unit('tablespoon', 'tablespoons'), amount: 1.25, note: 'finely chopped and rinsed well in cold water',
    })),
  })];
  const r = t.parseRecipe(recipe({ steps }));
  assert.equal(r.ingredients.length, 80);
  const chunks = t.packRecords(r.ingredients);
  assert.ok(chunks.length >= 2);
  const back = unpack(chunks);
  assert.equal(back.length, 80);
  assert.ok(back.every((fields) => fields.length === 8));
  for (const chunk of chunks) assert.ok(Buffer.byteLength(chunk.data, 'utf8') <= LIMIT.data);
});

// Errors and settings ---------------------------------------------------------------------------------

test('errorFor maps HTTP statuses and network failures to the watch\'s error codes', () => {
  const cases = [
    [0, ERR.UNREACHABLE],
    [-1, ERR.SERVER],
    [401, ERR.TOKEN],
    [403, ERR.TOKEN],
    [404, ERR.NOT_FOUND],
    [400, ERR.SERVER],
    [429, ERR.SERVER],
    [500, ERR.SERVER],
    [502, ERR.SERVER],
    [503, ERR.SERVER],
  ];
  for (const [status, code] of cases) {
    const err = t.errorFor(status);
    assert.equal(err.code, code, String(status));
    assert.ok(err.text.length > 0 && t.utf8Length(err.text) <= 95, String(status));
    assert.ok(err.text.endsWith('.'), `one sentence: ${err.text}`);
    assert.equal(err.status, status);
  }
});

test('errorFor words a refused token, a read only token and a wrong address for their place', () => {
  assert.match(t.errorFor(401).text, /token/);
  assert.match(t.errorFor(403, 'cook').text, /read write/);
  assert.match(t.errorFor(401, 'cook').text, /refused/);
  assert.match(t.errorFor(401, 'save').text, /read write/);
  assert.match(t.errorFor(404, 'save').text, /address/);
  assert.match(t.errorFor(-1, 'save').text, /look like a Tandoor server/);
  assert.match(t.errorFor(0, 'save').text, /online/);
});

test('the messages the phone sends stay one sentence within the watch\'s buffer', () => {
  assert.equal(t.notSetUp().code, ERR.NOT_SET_UP);
  assert.equal(t.noRecipes().code, ERR.NOT_FOUND);
  for (const key of Object.keys(t.TEXT)) assert.ok(t.utf8Length(t.TEXT[key]) <= 95, key);
});

test('connectedMessage counts recipes', () => {
  assert.equal(t.connectedMessage(42), 'Connected · 42 recipes');
  assert.equal(t.connectedMessage(1), 'Connected · 1 recipe');
  assert.equal(t.connectedMessage(0), 'Connected · 0 recipes');
});

test('settings are two switches that default to on', () => {
  assert.deepEqual(t.cleanSettings(null), { backlight: 1, sound: 1 });
  assert.deepEqual(t.cleanSettings({}), { backlight: 1, sound: 1 });
  assert.deepEqual(t.cleanSettings({ backlight: false, sound: 0 }), { backlight: 0, sound: 0 });
  assert.deepEqual(t.cleanSettings({ backlight: true, sound: '0' }), { backlight: 1, sound: 0 });
  assert.deepEqual(t.cleanSettings({ backlight: 'maybe', sound: 7 }), { backlight: 1, sound: 1 });
  assert.deepEqual(t.cleanSettings('x'), { backlight: 1, sound: 1 });
});

test('the limits are the ones docs/design.md lists', () => {
  assert.deepEqual(
    [LIMIT.title, LIMIT.subtitle, LIMIT.name, LIMIT.description, LIMIT.servingsText, LIMIT.keywords, LIMIT.stepName, LIMIT.stepText, LIMIT.unit, LIMIT.food, LIMIT.note, LIMIT.records, LIMIT.steps, LIMIT.ingredients, LIMIT.data],
    [63, 47, 63, 255, 23, 63, 47, 1799, 23, 47, 47, 200, 40, 80, 1400],
  );
  assert.equal(t.PAGE_SIZE, 20);
});
