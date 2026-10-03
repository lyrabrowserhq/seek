# Seek

Metasearch UI for seek.lyrabrowser.com. Queries go to the Lyra index at
index.lyrabrowser.com and to optional upstream engines. No ads, no
sponsored slots, no telemetry, no query logs in this process.

Images on results pages are fetched through `/proxy`. News is RSS, with a
search box over the merged items.

## Build

```
make
./bin/seeker
```

Config is `config.ini`. See `example-config.ini`. Default engines are
`lyra,ddg,brave,wiby,mojeek`.

`index_url` is the Lyra index origin. Default `https://index.lyrabrowser.com`.
Seek calls `/v1/search.rss?q=`.

## Ranking

Wikis and programming docs get a higher score. Reddit, X, Facebook, and
TikTok are almost dropped unless the query has `site:reddit` / `reddit.com`
or Settings turns on forums. Engine checkboxes and the default engine are
cookies, same as before.

## Browsers

Any browser can use HTTPS GET `/search?q=`. Lyra should POST `/search` with
`q` in the body so reverse-proxy access logs that record the request line
do not store the query. Terminate TLS at Seek or at a proxy you run. There
is no third-party analytics beacon.

## News

`/news` pulls the RSS list in `src/Routes/News.c`. The same items are
searchable. Add feeds to the Lyra index with `POST /v1/feeds` on the
indexer so they also show up in web results.

License: GPL-2.0.
