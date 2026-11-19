# Mock Tandoor

A small stand-in for a Tandoor 2 server, so the watch app can be developed and tested without a real server. It uses only the Python 3 standard library and answers with the JSON shapes of Tandoor 2, copied from a live server and its OpenAPI schema.

## Run it

```sh
python3 tools/mock-tandoor/server.py
python3 tools/mock-tandoor/server.py --port 9000 --delay 800
python3 tools/mock-tandoor/server.py --host 0.0.0.0
```

- `--host` is the address to listen on. The default is `127.0.0.1`. Use `0.0.0.0` so a real phone on the same network can reach it.
- `--port` defaults to `8765`.
- `--delay MS` waits that long before every answer. Use it to look at the loading screens and the timeouts. The phone gives up on a list or a recipe after 22 seconds and the watch after 25.

The server prints one line per request. It never prints the `Authorization` header.

## Connect the app

Open the settings page of the app and enter:

- Server address: `http://127.0.0.1:8765` for the emulator, or `http://<address of your computer>:8765` for a real phone.
- API token: `mock-token`

Any other token gets a 401, which is a way to try the "token refused" messages. Stop the server to try the "can't reach the server" ones.

## What it serves

| Request | Notes |
| --- | --- |
| `GET /api/recipe/` | `page`, `page_size` (default 25, at most 100), `query`, `keywords`, `keywords_or`, `books`, `books_or`, `filter`, `include_children`, `random=true`, `sort_order=-lastviewed`. |
| `GET /api/recipe/<id>/` | The full recipe with steps, ingredients, nutrition, and a sub-recipe inside a step. |
| `GET /api/recipe-book/`, `GET /api/recipe-book/<id>/` | `page`, `page_size`, `query`. |
| `GET /api/keyword/`, `GET /api/keyword/<id>/` | `root=0` for the top level, `root=<id>` for the children of one keyword. An unknown root gives a 500, as Tandoor does. |
| `GET /api/meal-plan/` | `from_date`, `to_date`. Filters on the entry's end date, ordered by id like Tandoor. |
| `POST /api/cook-log/` | Needs `recipe` and optionally `servings`. Answers 201 and prints a `COOKED` line. Nothing is stored. |

Lists use Tandoor's page number pagination: `count`, `next`, `previous` and `results`. A page past the end gives a 404 with `{"detail": "Invalid page."}`.

## The data

`fixtures.json` holds 30 recipes, 3 books, 19 keywords and a meal plan. The recipes cover what the app has to handle:

- Recipe 1 (Chicken Tikka Masala) has eight steps, one of them an included recipe (21, Garam Masala Blend), ingredient group headers, HTML entities, a step with a Tandoor `time` and steps whose only time is in the text.
- Recipe 2 (No-Knead Crusty Bread) has long waits: a rise of 12 to 18 hours and a rest of an hour.
- Recipe 3 has no times at all, and recipe 4 is a short one with two steps.
- Recipes 5, 17 and 22 have very long names, and recipes 11 to 14 have accents in their names.
- The keywords go three levels deep (Cuisine, Asian, Thai), and one is named with an emoji.
- Book 3 is a saved filter (id 7, keyword Quick) with no recipes of its own, so it checks the `filter` parameter.

The meal plan is built when you ask for it. It has nine entries from today to a week ahead, one of them without a recipe, plus one entry from yesterday and one from eight days ahead that the app must leave out.

Edit `fixtures.json` to try other shapes. The server reads it at start, so restart it after a change.
