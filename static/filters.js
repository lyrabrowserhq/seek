// keep the engine filter bar to one row and fold the extra chips into an
// overflow dropdown, recalculated on resize
(function () {
    const nav = document.getElementById('engine-filters');
    if (!nav) return;

    const menu = document.createElement('div');
    menu.className = 'engine-overflow-menu';
    const details = document.createElement('details');
    details.className = 'engine-overflow';
    const summary = document.createElement('summary');
    summary.className = 'engine-filter engine-more';
    summary.setAttribute('aria-label', 'More engines');
    summary.innerHTML = '<span class="ui-icon ui-icon-ellipsis-vertical"></span>';
    details.appendChild(summary);
    details.appendChild(menu);
    nav.appendChild(details);

    const chips = () => nav.querySelectorAll(':scope > a.engine-filter');

    function reflow() {
        // put every chip back inline before measuring
        while (menu.firstElementChild) nav.insertBefore(menu.firstElementChild, details);
        details.hidden = false;

        const first = chips()[0];
        if (!first) return;
        const top = first.offsetTop;
        for (const chip of chips()) {
            if (chip.offsetTop > top + 4) menu.appendChild(chip);
        }
        // the more button needs room too: pull chips until it fits the first row
        let guard = 8;
        while (menu.childElementCount && details.offsetTop > top + 4 && guard-- > 0) {
            const vis = chips();
            const last = vis[vis.length - 1];
            if (!last) break;
            menu.prepend(last);
        }
        details.hidden = !menu.childElementCount;
    }

    reflow();
    new ResizeObserver(() => reflow()).observe(nav);
    document.addEventListener('click', (e) => {
        if (!details.contains(e.target)) details.removeAttribute('open');
    });
})();
