document.addEventListener('DOMContentLoaded', function () {
  const searchInput = document.querySelector('.search-box');
  const results = document.querySelectorAll('.result, .image-card');
  const prevBtn = document.querySelector('.pagination-btn.prev');
  const nextBtn = document.querySelector('.pagination-btn.next');

  document.addEventListener('keydown', function (e) {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') {
      if (e.key === 'Escape') {
        e.target.blur();
      }
      return;
    }

    if (e.key === '/') {
      e.preventDefault();
      if (searchInput) {
        searchInput.focus();
      }
      return;
    }

    if (e.key === 'j' || e.key === 'J') {
      if (e.ctrlKey || e.metaKey) return;
      e.preventDefault();
      const current = document.activeElement;
      const focusable = [...results].filter((r) => {
        const link = r.querySelector('a[href]');
        return link && link.getAttribute('href') && !link.getAttribute('href').startsWith('#');
      });
      const idx = focusable.findIndex((el) => el.contains(current));
      const next = focusable[idx + 1] || focusable[0];
      if (next) {
        const link = next.querySelector('a.result-title, a.image-card-link');
        if (link) link.focus();
      }
      return;
    }

    if (e.key === 'k' || e.key === 'K') {
      if (e.ctrlKey || e.metaKey) return;
      e.preventDefault();
      const current = document.activeElement;
      const focusable = [...results].filter((r) => {
        const link = r.querySelector('a[href]');
        return link && link.getAttribute('href') && !link.getAttribute('href').startsWith('#');
      });
      const idx = focusable.findIndex((el) => el.contains(current));
      const prev = focusable[idx - 1] || focusable[focusable.length - 1];
      if (prev) {
        const link = prev.querySelector('a.result-title, a.image-card-link');
        if (link) link.focus();
      }
      return;
    }
  });

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js', { scope: '/' }).catch(function () {});
  }
});
