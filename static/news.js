document.addEventListener('DOMContentLoaded', () => {
  const wrap = document.querySelector('.source-overflow-wrap');
  const list = document.querySelector('.engine-filter-list');
  const column = document.querySelector('.results-main-column');
  const sourceInput = document.querySelector('#news-search-form input[name="source"]');
  const emptyTitle = (column && column.getAttribute('data-empty-title')) || 'No headlines';
  const emptyHint = (column && column.getAttribute('data-empty-hint')) || '';
  const allLabel = (column && column.getAttribute('data-all-label')) || 'All sources';
  const webLabel = (column && column.getAttribute('data-web-label')) || 'Search the web';

  function closeOverflow() {
    if (!wrap) return;
    const btn = wrap.querySelector('.source-overflow');
    const menu = wrap.querySelector('.source-overflow-menu');
    if (!btn || !menu) return;
    menu.hidden = true;
    wrap.classList.remove('is-open');
    btn.setAttribute('aria-expanded', 'false');
  }

  function openOverflow() {
    if (!wrap) return;
    const btn = wrap.querySelector('.source-overflow');
    const menu = wrap.querySelector('.source-overflow-menu');
    if (!btn || !menu) return;
    menu.hidden = false;
    wrap.classList.add('is-open');
    btn.setAttribute('aria-expanded', 'true');
  }

  if (wrap) {
    const btn = wrap.querySelector('.source-overflow');
    const menu = wrap.querySelector('.source-overflow-menu');
    if (btn && menu) {
      btn.addEventListener('click', (e) => {
        e.preventDefault();
        e.stopPropagation();
        if (menu.hidden)
          openOverflow();
        else
          closeOverflow();
      });
      document.addEventListener('click', () => closeOverflow());
      document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') closeOverflow();
      });
    }
  }

  function el(tag, className) {
    const n = document.createElement(tag);
    if (className)
      n.className = className;
    return n;
  }

  function renderEmpty(href) {
    const box = el('div', 'empty-state');
    box.setAttribute('role', 'status');
    const icon = el('span', 'ui-icon ui-icon-newspaper empty-state-icon');
    icon.setAttribute('aria-hidden', 'true');
    const title = el('h2', 'empty-state-title');
    title.textContent = emptyTitle;
    const copy = el('p', 'empty-state-copy');
    copy.textContent = emptyHint;
    const actions = el('div', 'empty-state-actions');
    const page = new URL(href || location.href, location.origin);
    const q = page.searchParams.get('q') || '';
    const src = page.searchParams.get('source') || '';
    if (src) {
      const all = el('a', 'empty-state-link');
      const allUrl = new URL('/news', location.origin);
      if (q)
        allUrl.searchParams.set('q', q);
      all.href = allUrl.pathname + allUrl.search;
      all.textContent = allLabel;
      actions.appendChild(all);
    }
    if (q) {
      const web = el('a', 'empty-state-link');
      web.href = '/search?q=' + encodeURIComponent(q);
      web.textContent = webLabel;
      actions.appendChild(web);
    }
    box.appendChild(icon);
    box.appendChild(title);
    if (emptyHint)
      box.appendChild(copy);
    if (actions.childNodes.length)
      box.appendChild(actions);
    column.appendChild(box);
  }

  function renderItems(items, href) {
    if (!column) return;
    column.replaceChildren();
    if (!items || !items.length) {
      renderEmpty(href);
      return;
    }
    const box = el('div', 'results-articles');
    items.forEach((item) => {
      const art = el('article', 'result');
      const header = el('div', 'result-header news-result-header');
      const source = el('span', 'news-source');
      if (item.icon) {
        const img = el('img', 'news-source-ico');
        img.src = item.icon;
        img.alt = '';
        img.width = 16;
        img.height = 16;
        img.decoding = 'async';
        img.addEventListener('error', () => {
          img.src = '/static/icon-placeholder.svg';
        }, { once: true });
        source.appendChild(img);
      }
      source.appendChild(document.createTextNode(item.source || ''));
      header.appendChild(source);
      if (item.display_date) {
        const sep = el('span', 'news-date-sep');
        sep.setAttribute('aria-hidden', 'true');
        sep.textContent = '\u00b7';
        const time = document.createElement('time');
        time.className = 'news-date';
        if (item.date)
          time.setAttribute('datetime', item.date);
        time.textContent = item.display_date;
        header.appendChild(sep);
        header.appendChild(time);
      }
      const title = el('a', 'result-title');
      title.href = item.url || '#';
      title.rel = 'noopener noreferrer';
      title.target = '_blank';
      title.textContent = item.title || '';
      art.appendChild(header);
      art.appendChild(title);
      if (item.snippet) {
        const desc = el('p', 'desc');
        desc.textContent = item.snippet;
        art.appendChild(desc);
      }
      box.appendChild(art);
    });
    column.appendChild(box);
  }

  function setActive(href) {
    if (!list) return;
    const next = new URL(href, location.origin);
    const src = next.searchParams.get('source') || '';
    list.querySelectorAll('a.engine-filter').forEach((a) => {
      const u = new URL(a.href, location.origin);
      const id = u.searchParams.get('source') || '';
      if (id === src)
        a.classList.add('active');
      else
        a.classList.remove('active');
    });
    if (sourceInput)
      sourceInput.value = src;
  }

  async function loadSource(href, push) {
    const next = new URL(href, location.origin);
    next.searchParams.set('format', 'json');
    const res = await fetch(next.toString(), { headers: { Accept: 'application/json' } });
    if (!res.ok)
      throw new Error('news');
    const data = await res.json();
    renderItems(data.items || [], href);
    setActive(href);
    closeOverflow();
    if (push)
      history.pushState({ news: true }, '', href);
  }

  if (list && column) {
    list.addEventListener('click', (e) => {
      const a = e.target.closest('a');
      if (!a || !list.contains(a))
        return;
      if (!a.href || a.target === '_blank')
        return;
      e.preventDefault();
      loadSource(a.href, true).catch(() => {
        location.href = a.href;
      });
    });
    window.addEventListener('popstate', () => {
      loadSource(location.href, false).catch(() => {
        location.reload();
      });
    });
  }
});
