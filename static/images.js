document.addEventListener('DOMContentLoaded', () => {
  const filterDefaults = { safe: 'moderate' };

  function applyFilter(key, value) {
    const params = new URLSearchParams(window.location.search);
    if (!key) return;
    if (value && value !== (filterDefaults[key] || '')) {
      params.set(key, value);
    } else {
      params.delete(key);
    }
    params.delete('p');
    const qs = params.toString();
    window.location = qs ? window.location.pathname + '?' + qs : window.location.pathname;
  }

  function closeFilterMenus(except) {
    document.querySelectorAll('.images-filter-menu').forEach((menu) => {
      if (menu === except) return;
      menu.hidden = true;
      const trigger = menu.previousElementSibling;
      if (trigger && trigger.classList.contains('images-filter-trigger')) {
        trigger.setAttribute('aria-expanded', 'false');
      }
    });
  }

  function enhanceFilterSelect(select) {
    const label = select.closest('.images-filter');
    const visibleLabel = label ? label.querySelector('.visually-hidden') : null;
    const wrap = document.createElement('div');
    wrap.className = 'images-filter-dropdown';

    const trigger = document.createElement('button');
    trigger.type = 'button';
    trigger.className = 'images-filter-trigger';
    trigger.setAttribute('aria-haspopup', 'listbox');
    trigger.setAttribute('aria-expanded', 'false');
    if (visibleLabel) {
      trigger.setAttribute('aria-label', visibleLabel.textContent.trim());
    }

    const menu = document.createElement('ul');
    menu.className = 'images-filter-menu';
    menu.setAttribute('role', 'listbox');
    menu.hidden = true;

    function syncTrigger() {
      const selected = select.options[select.selectedIndex];
      trigger.textContent = selected ? selected.textContent : '';
      menu.querySelectorAll('button').forEach((btn) => {
        btn.setAttribute('aria-selected', btn.dataset.value === select.value ? 'true' : 'false');
      });
    }

    [...select.options].forEach((opt) => {
      const li = document.createElement('li');
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.textContent = opt.textContent;
      btn.dataset.value = opt.value;
      btn.setAttribute('role', 'option');
      btn.setAttribute('aria-selected', opt.selected ? 'true' : 'false');
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        applyFilter(select.dataset.filter, opt.value);
      });
      li.appendChild(btn);
      menu.appendChild(li);
    });

    select.className = 'images-filter-select';
    select.tabIndex = -1;
    select.setAttribute('aria-hidden', 'true');

    const parent = select.parentNode;
    parent.insertBefore(wrap, select);
    wrap.appendChild(select);
    wrap.appendChild(trigger);
    wrap.appendChild(menu);
    syncTrigger();

    trigger.addEventListener('click', (e) => {
      e.stopPropagation();
      const willOpen = menu.hidden;
      closeFilterMenus(willOpen ? menu : null);
      menu.hidden = !willOpen;
      trigger.setAttribute('aria-expanded', willOpen ? 'true' : 'false');
    });

    trigger.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowDown' || e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        closeFilterMenus(menu);
        menu.hidden = false;
        trigger.setAttribute('aria-expanded', 'true');
        const selected = menu.querySelector('button[aria-selected="true"]') || menu.querySelector('button');
        selected?.focus();
      } else if (e.key === 'Escape') {
        menu.hidden = true;
        trigger.setAttribute('aria-expanded', 'false');
      }
    });

    menu.addEventListener('keydown', (e) => {
      const items = [...menu.querySelectorAll('button')];
      const idx = items.indexOf(document.activeElement);
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        items[Math.min(idx + 1, items.length - 1)]?.focus();
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        items[Math.max(idx - 1, 0)]?.focus();
      } else if (e.key === 'Escape') {
        e.preventDefault();
        menu.hidden = true;
        trigger.setAttribute('aria-expanded', 'false');
        trigger.focus();
      } else if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        document.activeElement?.click();
      }
    });
  }

  document.querySelectorAll('.images-filter-select').forEach(enhanceFilterSelect);

  const searchForm = document.getElementById('images-search-form');
  if (searchForm) {
    const params = new URLSearchParams(window.location.search);
    ['mkt', 'safe', 'when', 'size', 'color', 'type', 'layout', 'license'].forEach((key) => {
      const value = params.get(key);
      if (!value) return;
      if (document.querySelector(`input[name="${key}"]`)) return;
      const input = document.createElement('input');
      input.type = 'hidden';
      input.name = key;
      input.value = value;
      searchForm.appendChild(input);
    });
  }

  const cards = document.querySelectorAll('.image-card');

  cards.forEach((card) => {
    const img = card.querySelector('.image-thumb');
    const dimEl = card.querySelector('.image-dimensions');
    const domainEl = card.querySelector('.image-domain');
    const faviconEl = card.querySelector('.image-favicon');
    const sourceLink = card.querySelector('.image-source-link');
    const menuBtn = card.querySelector('.image-menu-btn');
    const menu = card.querySelector('.image-menu');

    if (img && dimEl) {
      const setDimensions = () => {
        if (img.naturalWidth > 0 && img.naturalHeight > 0) {
          dimEl.textContent = img.naturalWidth + ' x ' + img.naturalHeight;
        }
      };
      if (img.complete && img.naturalWidth > 0) {
        setDimensions();
        img.classList.add('is-loaded');
      } else {
        img.addEventListener('load', () => {
          setDimensions();
          img.classList.add('is-loaded');
        });
        img.addEventListener('error', () => {
          card.classList.add('is-broken');
        });
      }
    }

    if (sourceLink && domainEl) {
      const url = sourceLink.getAttribute('href') || '';
      try {
        const host = new URL(url).hostname.replace(/^www\./, '');
        domainEl.textContent = host;
        if (faviconEl && host) {
          faviconEl.src = '/favicon?host=' + encodeURIComponent(host);
          faviconEl.hidden = false;
          faviconEl.addEventListener('error', () => {
            faviconEl.onerror = null;
            faviconEl.src = '/static/icon-placeholder.svg';
          });
        }
      } catch {
        domainEl.textContent = url;
      }
    }

    if (menuBtn && menu) {
      menuBtn.addEventListener('click', (e) => {
        e.preventDefault();
        e.stopPropagation();
        closeFilterMenus();
        const open = menu.hidden;
        document.querySelectorAll('.image-menu').forEach((m) => {
          m.hidden = true;
        });
        menu.hidden = !open;
      });
    }
  });

  document.addEventListener('click', () => {
    closeFilterMenus();
    document.querySelectorAll('.image-menu').forEach((m) => {
      m.hidden = true;
    });
  });
});
