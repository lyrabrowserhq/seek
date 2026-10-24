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

HTML is parsed by vendored lexbor (`vendor/lexbor`, static); libxml2 is
still used for the XML/RSS paths (lyra results, news feeds, wiki infobox).

Config is `config.ini`. See `example-config.ini`. Default engines are
`lyra,ddg,brave,wiby,mojeek`.

`index_url` is the Lyra index origin. Default `https://index.lyrabrowser.com`.
Seek calls `/v1/search.rss?q=`.

## Ranking

Wikis and programming docs get a higher score. Reddit, X, Facebook, and
TikTok are almost dropped unless the query has `site:reddit` / `reddit.com`
or Settings turns on forums. Engine checkboxes and the default engine are
cookies, same as before.

The `...` menu under each result pins, raises, lowers, or blocks its
domain. Settings has a matching list editor: `++domain` pins,
`+domain` raises, `-domain` lowers, `!domain` blocks. Everything is a
cookie, no account.

## Lenses and operators

The lens row under the engine filters narrows results to Forums, Docs,
or Wikis. The index and the upstream engines understand `site:`,
`-site:`, `intitle:`, `inurl:`, `before:YYYY-MM-DD`, `after:`,
`-term` exclusion, and `"quoted phrases"`. At most two results per
domain are shown; the rest collapse into a `+N more from domain` link
that re-searches with `site:domain`.

## Browsers

Any browser can use HTTPS GET `/search?q=`. Lyra should POST `/search` with
`q` in the body so reverse-proxy access logs that record the request line
do not store the query. Terminate TLS at Seek or at a proxy you run. There
is no third-party analytics beacon.

## News

`/news` pulls the RSS list in `src/Routes/News.c`. The same items are
searchable. Add feeds to the Lyra index with `POST /v1/feeds` on the
indexer so they also show up in web results.

## Bangs

`!name query` redirects straight to a site. Defaults include `!w`, `!gh`,
`!r`, `!g`, `!ddg`, `!hn`, `!so`, `!yt`, `!maps`, `!mdn`, `!crates`,
`!npm`, `!pypi`, `!a`, `!ebay`, `!imdb`, `!t`, `!wa`, `!img`, `!news`
and more, all listed on `/bangs`. Override with `bangs_file` in config.

License: GPL-2.0 (see LICENSE). Vendored components keep their own
licenses: beaker/ is LGPL-2.1 and has been modified for Seek
(see beaker/FORK-NOTICE), lucide icons are ISC, and the Geist fonts
are SIL OFL-1.1. See NOTICE.
