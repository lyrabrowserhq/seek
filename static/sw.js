const CACHE_NAME = 'seeker-v2';
const STATIC_ASSETS = [
  '/static/main.css',
  '/static/theme.js',
  '/static/a11y.js',
  '/static/icons/icon-192.svg',
  '/static/icons/icon-512.svg',
  '/static/wikipedia.ico'
];

self.addEventListener('install', (e) => {
  e.waitUntil(
    caches.open(CACHE_NAME).then((cache) => cache.addAll(STATIC_ASSETS))
  );
  self.skipWaiting();
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE_NAME).map((k) => caches.delete(k)))
    )
  );
  self.clients.claim();
});

self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  const sameOrigin = url.origin === self.location.origin;
  const isStatic = sameOrigin && (
    STATIC_ASSETS.includes(url.pathname) ||
    url.pathname.startsWith('/static/')
  );
  if (isStatic && e.request.method === 'GET') {
    e.respondWith(
      caches.match(e.request).then((cached) =>
        cached || fetch(e.request).then((r) => {
          const clone = r.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(e.request, clone));
          return r;
        })
      )
    );
  }
});
