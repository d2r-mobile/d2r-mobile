# Tables used by item annotations

| File | Contents | In the public repository? |
|---|---|---|
| `comments.tsv` | A one-line comment for each unique item, set item and runeword (rewritten entry by entry from the comments of a third-party mod) | **No.** It is an adaptation and the public repository does not ship it; without this table no comments are added, and the rest of the annotations work as usual |
| `oldnames.tsv` | Old-translation table: changes some names back to the older Chinese translations that long-time players know | **No.** It is a complete set of someone else's translated names and the public repository does not ship it; without this table the "change back to old names" step is skipped automatically, and the rest of the annotations work as usual |

## Supplying your own old-translation table

Put an `oldnames.tsv` in this directory to enable it. Tab-separated, UTF-8, first line is the header (the column names are in Chinese; keep them as shown):

```
文件	键	原文校验	旧译名
item-names	KhalimFlail	2717cc0f	(the name you want instead)
```

- `文件` (file): the name of the game string file, without `.json` (under `data/local/lng/strings/` in the game resources)
- `键` (key): the `Key` of that entry in the file
- `原文校验` (checksum of the original text): the first 8 hex digits of the SHA-1 of the entry's current Simplified Chinese text (UTF-8). Every entry is checked at build time, and a changed original (the game was updated) is reported as an error, so that nothing is replaced by mistake
- `旧译名` (old name): the name to use instead; write a line break as `\n`

## Supplying your own comment table

Put a `comments.tsv` in this directory to enable it. Tab-separated, UTF-8, first line is the header; the columns are the ones read by `comments()` in `annot_strings.py`:

```
文件	键	名字颜色	点评
item-names	(the Key of the string)	unique	(your one-line comment)
```

- `文件` (file): `item-names` (unique and set items) or `item-runes` (runewords)
- `键` (key): the `Key` of that string; a key in the table that cannot be found in the game's string tables is reported as an error
- `名字颜色` (name color): `set` = set item (green); anything else is treated as unique (gold)
- `点评` (comment): one line shown below the name. Skill names mentioned in it must be the names after the change back to old names (if you also supply the old-translation table)
