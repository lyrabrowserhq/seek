// Search suggestions: fetch /suggest?q=... and show a dropdown under
// inputs with class .search-box. DDG-compatible response shape:
// ["query", ["suggestion 1", "suggestion 2", ...]]
(function () {
    'use strict';

    var inputs = document.querySelectorAll('input.search-box[name="q"]');

    inputs.forEach(function (input) {
        var wrapper = input.closest('.search-input-wrapper') || input.parentElement;
        if (!wrapper) return;
        wrapper.style.position = 'relative';

        var listId = 'suggest-list-' + Math.floor(Math.random() * 1e9);
        var list = document.createElement('ul');
        list.className = 'suggest-list';
        list.id = listId;
        list.setAttribute('role', 'listbox');
        list.hidden = true;
        wrapper.appendChild(list);

        input.setAttribute('role', 'combobox');
        input.setAttribute('aria-controls', listId);
        input.setAttribute('aria-expanded', 'false');
        input.setAttribute('aria-autocomplete', 'list');

        var items = [];
        var active = -1;
        var debounce = null;
        var lastQuery = '';

        function hide() {
            list.hidden = true;
            active = -1;
            input.setAttribute('aria-expanded', 'false');
            input.removeAttribute('aria-activedescendant');
        }

        function render() {
            list.innerHTML = '';
            items.forEach(function (s, i) {
                var li = document.createElement('li');
                li.textContent = s;
                li.setAttribute('role', 'option');
                li.id = listId + '-opt-' + i;
                if (i === active) {
                    li.classList.add('active');
                    li.setAttribute('aria-selected', 'true');
                    input.setAttribute('aria-activedescendant', li.id);
                }
                li.addEventListener('mousedown', function (e) {
                    e.preventDefault();
                    input.value = s;
                    hide();
                    input.form && input.form.submit();
                });
                list.appendChild(li);
            });
            list.hidden = items.length === 0;
            input.setAttribute('aria-expanded', items.length > 0 ? 'true' : 'false');
        }

        function fetchSuggest(q) {
            fetch('/suggest?q=' + encodeURIComponent(q))
                .then(function (r) { return r.ok ? r.json() : null; })
                .then(function (data) {
                    if (!data || lastQuery !== input.value.trim()) return;
                    items = Array.isArray(data[1]) ? data[1].slice(0, 8) : [];
                    active = -1;
                    render();
                })
                .catch(function () {});
        }

        input.addEventListener('input', function () {
            var q = input.value.trim();
            lastQuery = q;
            if (debounce) clearTimeout(debounce);
            if (q.length < 2) {
                items = [];
                hide();
                return;
            }
            debounce = setTimeout(function () { fetchSuggest(q); }, 150);
        });

        input.addEventListener('keydown', function (e) {
            if (list.hidden) return;
            if (e.key === 'ArrowDown') {
                e.preventDefault();
                active = Math.min(active + 1, items.length - 1);
                render();
            } else if (e.key === 'ArrowUp') {
                e.preventDefault();
                active = Math.max(active - 1, -1);
                render();
            } else if (e.key === 'Escape') {
                hide();
            } else if (e.key === 'Enter' && active >= 0 && items[active]) {
                e.preventDefault();
                input.value = items[active];
                hide();
                input.form && input.form.submit();
            }
        });

        input.addEventListener('blur', function () {
            setTimeout(hide, 150);
        });
    });
})();
