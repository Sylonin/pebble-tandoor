#!/usr/bin/env python3
"""A stand-in for a Tandoor 2 server, for developing the Pebble app without a real one.

    python3 tools/mock-tandoor/server.py [--host 127.0.0.1] [--port 8765] [--delay MS]

It answers the endpoints the app uses, with the JSON shapes of Tandoor 2 (copied from a live
server and its OpenAPI schema), and only to `Authorization: Bearer mock-token`. The recipes,
books, keywords and the meal plan come from fixtures.json next to this file. The meal plan is
computed at request time, so it always covers today through the next week.

Endpoints: GET /api/recipe/, /api/recipe/<id>/, /api/recipe-book/, /api/recipe-book/<id>/,
/api/keyword/, /api/keyword/<id>/, /api/meal-plan/ and POST /api/cook-log/.
"""

import argparse
import datetime
import json
import math
import random
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlparse

TOKEN = 'mock-token'
FIXTURES = Path(__file__).with_name('fixtures.json')
CREATED = '2026-11-01T10:00:00.000000Z'

USER = {
    'id': 1,
    'username': 'cook',
    'first_name': '',
    'last_name': '',
    'display_name': 'cook',
    'is_staff': True,
    'is_superuser': True,
    'is_active': True,
}


class HttpError(Exception):
    def __init__(self, status, body):
        super().__init__(status)
        self.status = status
        self.body = body


def not_found():
    return HttpError(404, {'detail': 'Not found.'})


# Markdown ----------------------------------------------------------------------------------------


def escape_html(text):
    """Escapes < > and a bare &, and leaves entities such as &deg; as they are."""
    text = re.sub(r'&(?!#?\w+;)', '&amp;', text)
    return text.replace('<', '&lt;').replace('>', '&gt;')


def inline(text):
    return re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', escape_html(text))


def render_markdown(text):
    """The little Markdown Tandoor's instructions use: paragraphs, lists and bold."""
    blocks = [b for b in re.split(r'\n\s*\n', text.strip()) if b.strip()]
    out = []
    for block in blocks:
        lines = block.split('\n')
        if all(re.match(r'^[-*] ', line) for line in lines):
            items = ''.join('<li>%s</li>\n' % inline(line[2:]) for line in lines)
            out.append('<ul>\n%s</ul>' % items)
        elif all(re.match(r'^\d+\. ', line) for line in lines):
            items = ''.join('<li>%s</li>\n' % inline(re.sub(r'^\d+\. ', '', line)) for line in lines)
            out.append('<ol>\n%s</ol>' % items)
        else:
            out.append('<p>%s</p>' % inline('\n'.join(lines)))
    return '\n'.join(out)


# Data --------------------------------------------------------------------------------------------


def parse_time(value):
    hour, minute, second = (int(part) for part in value.split(':'))
    return datetime.time(hour, minute, second)


class Store:
    """The fixtures, expanded into Tandoor's JSON on demand."""

    def __init__(self, raw):
        self.keywords = {k['id']: k for k in raw['keywords']}
        self.meal_types = {m['id']: m for m in raw['meal_types']}
        self.books = {b['id']: b for b in raw['books']}
        self.plan = raw['meal_plan']
        self.recipes = {r['id']: r for r in raw['recipes']}
        self.filters = {b['filter']['id']: b['filter'] for b in raw['books'] if b.get('filter')}
        self.foods = {}
        self.units = {}
        for recipe in raw['recipes']:
            for step in recipe['steps']:
                for ingredient in step.get('ingredients', []):
                    if 'food' in ingredient:
                        self.food(ingredient['food'])
                    if 'unit' in ingredient:
                        self.unit(ingredient['unit'])

    # Foods and units get ids in the order they first appear.
    def food(self, spec):
        name, _, plural = spec.partition('|')
        if name not in self.foods:
            self.foods[name] = {
                'id': 100 + len(self.foods),
                'name': name,
                'plural_name': plural or None,
                'description': '',
                'recipe': None,
                'url': '',
                'properties': [],
                'properties_food_amount': 100.0,
                'properties_food_unit': None,
                'fdc_id': None,
                'food_onhand': False,
                'supermarket_category': None,
                'parent': None,
                'numchild': 0,
                'inherit_fields': [],
                'full_name': name,
                'ignore_shopping': False,
                'substitute': [],
                'substitute_siblings': False,
                'substitute_children': False,
                'substitute_onhand': False,
                'child_inherit_fields': [],
                'open_data_slug': None,
                'shopping_lists': [],
            }
        return self.foods[name]

    def unit(self, spec):
        name, _, plural = spec.partition('|')
        if name not in self.units:
            self.units[name] = {
                'id': 50 + len(self.units),
                'name': name,
                'plural_name': plural or None,
                'description': None,
                'base_unit': None,
                'open_data_slug': None,
            }
        return self.units[name]

    # Keywords

    def children(self, keyword_id):
        ids = [k['id'] for k in self.keywords.values() if k['parent'] == keyword_id]
        return sorted(ids, key=lambda i: self.keywords[i]['name'].lower())

    def descendants(self, keyword_id):
        found = {keyword_id}
        for child in self.children(keyword_id):
            found |= self.descendants(child)
        return found

    def full_name(self, keyword):
        names = [keyword['name']]
        while keyword['parent']:
            keyword = self.keywords[keyword['parent']]
            names.insert(0, keyword['name'])
        return ' > '.join(names)

    def keyword_json(self, keyword):
        return {
            'id': keyword['id'],
            'name': keyword['name'],
            'label': keyword['name'],
            'description': '',
            'parent': keyword['parent'],
            'numchild': len(self.children(keyword['id'])),
            'created_at': CREATED,
            'updated_at': CREATED,
            'full_name': self.full_name(keyword),
        }

    # Recipes

    def overview(self, recipe):
        return {
            'id': recipe['id'],
            'name': recipe['name'],
            'description': recipe['description'],
            'image': None,
            'keywords': [{'id': k, 'label': self.keywords[k]['name']} for k in recipe['keywords']],
            'working_time': recipe['working_time'],
            'waiting_time': recipe['waiting_time'],
            'created_by': USER,
            'created_at': CREATED,
            'updated_at': CREATED,
            'internal': False,
            'private': False,
            'servings': recipe['servings'],
            'servings_text': recipe['servings_text'],
            'rating': recipe['rating'],
            'last_cooked': None,
            'new': False,
        }

    def ingredient_json(self, recipe, step_index, position, spec):
        header = 'header' in spec
        food = None if header or 'food' not in spec else self.food(spec['food'])
        unit = None if header or 'unit' not in spec else self.unit(spec['unit'])
        no_amount = bool(spec.get('no_amount'))
        return {
            'id': (recipe['id'] * 100 + step_index) * 100 + position,
            'food': food,
            'unit': unit,
            'amount': 0.0 if header or no_amount else float(spec.get('amount', 0)),
            'conversions': [],
            'note': spec['header'] if header else spec.get('note', ''),
            'order': position,
            'is_header': header,
            'no_amount': no_amount,
            'original_text': None,
            'used_in_recipes': [{'id': recipe['id'], 'name': recipe['name']}],
            'checked': False,
        }

    def step_json(self, recipe, step_index, nested):
        step = recipe['steps'][step_index]
        sub = step.get('recipe')
        data = None
        # Like Tandoor, a step recipe's own steps are not expanded a second time.
        if sub and nested:
            other = self.recipes[sub]
            data = {
                'id': other['id'],
                'name': other['name'],
                'steps': [self.step_json(other, i, False) for i in range(len(other['steps']))],
            }
        text = step.get('text', '')
        return {
            'id': recipe['id'] * 100 + step_index,
            'name': step.get('name', ''),
            'instruction': text,
            'ingredients': [self.ingredient_json(recipe, step_index, n, spec) for n, spec in enumerate(step.get('ingredients', []))],
            'instructions_markdown': step['html'] if 'html' in step else render_markdown(text),
            'time': step.get('time', 0),
            'order': step_index,
            'show_as_header': True,
            'file': None,
            'step_recipe': sub,
            'step_recipe_data': data,
            'show_ingredients_table': True,
        }

    def detail(self, recipe):
        return {
            'id': recipe['id'],
            'name': recipe['name'],
            'description': recipe['description'],
            'image': None,
            'keywords': [self.keyword_json(self.keywords[k]) for k in recipe['keywords']],
            'steps': [self.step_json(recipe, i, True) for i in range(len(recipe['steps']))],
            'working_time': recipe['working_time'],
            'waiting_time': recipe['waiting_time'],
            'created_by': USER,
            'created_at': CREATED,
            'updated_at': CREATED,
            'source_url': None,
            'internal': False,
            'show_ingredient_overview': True,
            'nutrition': None,
            'properties': [],
            'food_properties': {},
            'servings': recipe['servings'],
            'file_path': '',
            'servings_text': recipe['servings_text'],
            'diameter': 0,
            'diameter_text': '',
            'rating': recipe['rating'],
            'last_cooked': None,
            'private': False,
            'shared': [],
        }

    # Books

    def book_json(self, book):
        flt = book.get('filter')
        return {
            'id': book['id'],
            'name': book['name'],
            'description': book['description'],
            'shared': [],
            'created_by': USER,
            'filter': self.filter_json(flt) if flt else None,
            'order': 0,
        }

    def filter_json(self, flt):
        return {
            'id': flt['id'],
            'name': flt['name'],
            'search': json.dumps(flt['search']),
            'shared': [],
            'created_by': 1,
        }

    # Meal plan

    def meal_type_json(self, meal_type):
        return {
            'id': meal_type['id'],
            'name': meal_type['name'],
            'order': meal_type['order'],
            'time': meal_type['time'],
            'color': None,
            'created_by': 1,
        }

    def plan_json(self, today):
        """The fixture entries, dated relative to today, in the order of their ids."""
        entries = []
        for number, spec in enumerate(self.plan, start=1):
            meal_type = self.meal_types[spec['meal']]
            day = today + datetime.timedelta(days=spec['offset'])
            at = parse_time(meal_type['time']) if meal_type['time'] else datetime.time(12, 0)
            moment = datetime.datetime.combine(day, at).astimezone().isoformat()
            recipe = self.recipes.get(spec['recipe']) if spec['recipe'] else None
            note = spec.get('note', '')
            entry = {
                'id': number,
                'title': spec.get('title', ''),
                'recipe': self.overview(recipe) if recipe else None,
                'servings': spec.get('servings', 1),
                'note': note,
                'note_markdown': render_markdown(note),
                'from_date': moment,
                'to_date': moment,
                'meal_type': self.meal_type_json(meal_type),
                'created_by': 1,
                'meal_type_name': meal_type['name'],
                'shopping': False,
            }
            # Tandoor leaves recipe_name out when there is no recipe.
            if recipe:
                entry['recipe_name'] = recipe['name']
            entries.append((day, entry))
        return entries


# Requests ----------------------------------------------------------------------------------------


def first(query, name, default=None):
    values = query.get(name)
    return values[0] if values else default


def int_list(query, *names):
    found = []
    for name in names:
        for value in query.get(name, []):
            for part in value.split(','):
                if part.strip().isdigit():
                    found.append(int(part))
    return found


def truthy(value):
    return str(value).lower() in ('1', 'true', 'yes')


def paginate(handler, items, query, default_size, max_size, timestamp):
    """DRF's page number pagination, with the page size limits of Tandoor's views."""
    size = default_size
    raw_size = first(query, 'page_size')
    if raw_size and raw_size.isdigit() and int(raw_size) > 0:
        size = min(int(raw_size), max_size)
    raw_page = first(query, 'page', '1')
    pages = max(1, math.ceil(len(items) / size))
    if not raw_page.isdigit() or not 1 <= int(raw_page) <= pages:
        raise HttpError(404, {'detail': 'Invalid page.'})
    page = int(raw_page)
    base = 'http://%s%s' % (handler.headers.get('Host', 'localhost'), urlparse(handler.path).path)

    def link(number):
        params = {k: v for k, v in query.items() if k != 'page'}
        params['page'] = [str(number)]
        return '%s?%s' % (base, urlencode(params, doseq=True))

    body = {
        'count': len(items),
        'next': link(page + 1) if page < pages else None,
        'previous': link(page - 1) if page > 1 else None,
    }
    if timestamp:
        body['timestamp'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    body['results'] = items[(page - 1) * size : page * size]
    return body


def search_recipes(store, query):
    """The recipe list: a subset of Tandoor's search (query, keywords, books, filter, sort, random)."""
    if first(query, 'filter'):
        flt = store.filters.get(int(first(query, 'filter')) if first(query, 'filter').isdigit() else -1)
        if flt:
            query = {k: [str(x) for x in (v if isinstance(v, list) else [v])] for k, v in flt['search'].items()}
    recipes = list(store.recipes.values())

    keywords = int_list(query, 'keywords', 'keywords_or')
    if keywords:
        wanted = set()
        for keyword_id in keywords:
            if truthy(first(query, 'include_children', 'true')):
                wanted |= store.descendants(keyword_id)
            else:
                wanted.add(keyword_id)
        recipes = [r for r in recipes if wanted & set(r['keywords'])]

    books = int_list(query, 'books', 'books_or')
    if books:
        wanted = set()
        for book_id in books:
            wanted |= set(store.books.get(book_id, {}).get('recipes', []))
        recipes = [r for r in recipes if r['id'] in wanted]

    text = (first(query, 'query') or '').strip().lower()
    if text:
        recipes = [r for r in recipes if text in r['name'].lower()]

    recipes.sort(key=lambda r: r['name'].lower())
    if text:
        recipes.sort(key=lambda r: not r['name'].lower().startswith(text))
    if first(query, 'sort_order') == '-lastviewed':
        def viewed(recipe):
            stamp = recipe.get('viewed')
            moment = datetime.datetime.fromisoformat(stamp.replace('Z', '+00:00')) if stamp else None
            return (moment is None, -moment.timestamp() if moment else 0, recipe['name'].lower())
        recipes.sort(key=viewed)
    if truthy(first(query, 'random', 'false')):
        random.shuffle(recipes)
    return recipes


def list_recipes(handler, store, query):
    items = [store.overview(r) for r in search_recipes(store, query)]
    return 200, paginate(handler, items, query, 25, 100, False)


def get_recipe(handler, store, query, recipe_id):
    recipe = store.recipes.get(int(recipe_id))
    if not recipe:
        raise not_found()
    return 200, store.detail(recipe)


def list_books(handler, store, query):
    text = (first(query, 'query') or '').lower()
    books = [b for b in sorted(store.books.values(), key=lambda b: b['id']) if text in b['name'].lower()]
    return 200, paginate(handler, [store.book_json(b) for b in books], query, 50, 200, True)


def get_book(handler, store, query, book_id):
    book = store.books.get(int(book_id))
    if not book:
        raise not_found()
    return 200, store.book_json(book)


def list_keywords(handler, store, query):
    keywords = list(store.keywords.values())
    root = first(query, 'root')
    if root is not None and root.isdigit():
        if int(root) == 0:
            keywords = [k for k in keywords if k['parent'] is None]
        elif int(root) in store.keywords:
            keywords = [store.keywords[i] for i in store.children(int(root))]
        else:
            # Tandoor's TreeMixin does not catch DoesNotExist: a plain 500.
            raise HttpError(500, None)
    text = (first(query, 'query') or '').lower()
    keywords = [k for k in keywords if text in k['name'].lower()]
    keywords.sort(key=lambda k: k['name'].lower())
    return 200, paginate(handler, [store.keyword_json(k) for k in keywords], query, 50, 200, True)


def get_keyword(handler, store, query, keyword_id):
    keyword = store.keywords.get(int(keyword_id))
    if not keyword:
        raise not_found()
    return 200, store.keyword_json(keyword)


def list_meal_plan(handler, store, query):
    today = datetime.date.today()
    low = first(query, 'from_date', (today - datetime.timedelta(days=90)).isoformat())[:10]
    high = first(query, 'to_date', (today + datetime.timedelta(days=360)).isoformat())[:10]
    # Like Tandoor, the dates filter on the entry's end date, in server time; the order is by id.
    entries = [e for day, e in store.plan_json(today) if low <= day.isoformat() <= high]
    return 200, paginate(handler, entries, query, 50, 200, True)


def read_json(handler):
    try:
        return json.loads(handler.body.decode('utf-8')) if handler.body else {}
    except ValueError:
        raise HttpError(400, {'detail': 'JSON parse error'})


def create_cook_log(handler, store, query):
    body = read_json(handler)
    recipe = store.recipes.get(body.get('recipe')) if isinstance(body.get('recipe'), int) else None
    if not recipe:
        raise HttpError(400, {'recipe': ['Invalid pk "%s" - object does not exist.' % body.get('recipe')]})
    servings = body.get('servings')
    print('COOKED recipe=%d (%s) servings=%s' % (recipe['id'], recipe['name'], servings), flush=True)
    now = datetime.datetime.now(datetime.timezone.utc).isoformat()
    return 201, {
        'id': 1,
        'recipe': recipe['id'],
        'servings': servings,
        'rating': None,
        'comment': None,
        'created_by': USER,
        'created_at': now,
        'updated_at': now,
    }


ROUTES = [
    ('GET', re.compile(r'^/api/recipe/$'), list_recipes),
    ('GET', re.compile(r'^/api/recipe/(\d+)/$'), get_recipe),
    ('GET', re.compile(r'^/api/recipe-book/$'), list_books),
    ('GET', re.compile(r'^/api/recipe-book/(\d+)/$'), get_book),
    ('GET', re.compile(r'^/api/keyword/$'), list_keywords),
    ('GET', re.compile(r'^/api/keyword/(\d+)/$'), get_keyword),
    ('GET', re.compile(r'^/api/meal-plan/$'), list_meal_plan),
    ('POST', re.compile(r'^/api/cook-log/$'), create_cook_log),
]


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    server_version = 'MockTandoor/1.0'

    def send(self, status, body, content_type='application/json', headers=None):
        if body is None:
            payload = b'<h1>Server Error (500)</h1>'
            content_type = 'text/html; charset=utf-8'
        else:
            payload = json.dumps(body, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(payload)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(payload)

    def handle_request(self, method):
        try:
            # Read the body first, so a kept-alive connection stays usable whatever the answer.
            length = int(self.headers.get('Content-Length') or 0)
            self.body = self.rfile.read(length) if length else b''
            url = urlparse(self.path)
            if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
                self.send(401, {'detail': 'Authentication credentials were not provided.'},
                          headers={'WWW-Authenticate': 'Bearer realm="api"'})
                return
            if self.server.delay:
                time.sleep(self.server.delay)
            query = parse_qs(url.query, keep_blank_values=True)
            for verb, pattern, view in ROUTES:
                match = pattern.match(url.path)
                if match and verb == method:
                    status, body = view(self, self.server.store, query, *match.groups())
                    self.send(status, body)
                    return
            raise not_found()
        except HttpError as error:
            self.send(error.status, error.body)
        except Exception as error:  # a bug in the mock should show up as a 500, not a hang
            print('error: %r' % (error,), file=sys.stderr, flush=True)
            self.send(500, None)

    def do_GET(self):
        self.handle_request('GET')

    def do_POST(self):
        self.handle_request('POST')

    def log_message(self, fmt, *args):
        # The request line only: the Authorization header is never printed.
        print('%s - %s' % (self.address_string(), fmt % args), flush=True)


def main():
    parser = argparse.ArgumentParser(description='A stand-in for a Tandoor 2 server.')
    parser.add_argument('--host', default='127.0.0.1', help='address to listen on (0.0.0.0 for a real phone)')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--delay', type=int, default=0, metavar='MS', help='wait this long before every answer')
    args = parser.parse_args()

    store = Store(json.loads(FIXTURES.read_text(encoding='utf-8')))
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.store = store
    server.delay = args.delay / 1000
    print('Mock Tandoor on http://%s:%d  token: %s  delay: %d ms' % (args.host, args.port, TOKEN, args.delay), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
