// Keyboard shortcuts:
//   /       focus the search box
//   a/i/n   switch to All / Images / News (keeps current query)
//   j/k     move through results
//   Enter   open the focused result
//   Escape  leave search box / clear focus
(function () {
    'use strict';

    function typingInField() {
        var el = document.activeElement;
        return el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' ||
                      el.tagName === 'SELECT' || el.isContentEditable);
    }

    function currentQuery() {
        var box = document.querySelector('input.search-box[name="q"], input[name="q"]');
        return box ? box.value.trim() : '';
    }

    function results() {
        return Array.prototype.slice.call(document.querySelectorAll('.result'));
    }

    var focusIdx = -1;

    function clearFocus() {
        document.querySelectorAll('.result.kb-focus').forEach(function (el) {
            el.classList.remove('kb-focus');
        });
        focusIdx = -1;
    }

    function moveFocus(delta) {
        var items = results();
        if (!items.length) return;
        focusIdx += delta;
        if (focusIdx < 0) focusIdx = 0;
        if (focusIdx >= items.length) focusIdx = items.length - 1;
        items.forEach(function (el, i) {
            el.classList.toggle('kb-focus', i === focusIdx);
        });
        items[focusIdx].scrollIntoView({ block: 'nearest' });
    }

    document.addEventListener('keydown', function (e) {
        if (e.ctrlKey || e.metaKey || e.altKey) return;

        if (e.key === 'Escape') {
            var active = document.activeElement;
            if (typingInField()) {
                active.blur();
            } else {
                clearFocus();
            }
            return;
        }

        if (typingInField()) return;

        switch (e.key) {
        case '/': {
            e.preventDefault();
            var box = document.querySelector('input.search-box[name="q"], input[name="q"]');
            if (box) {
                box.focus();
                box.select();
            }
            break;
        }
        case 'a': {
            var q = currentQuery();
            window.location.href = '/search' + (q ? '?q=' + encodeURIComponent(q) : '');
            break;
        }
        case 'i': {
            var q2 = currentQuery();
            window.location.href = '/images' + (q2 ? '?q=' + encodeURIComponent(q2) : '');
            break;
        }
        case 'n': {
            var q3 = currentQuery();
            window.location.href = '/news' + (q3 ? '?q=' + encodeURIComponent(q3) : '');
            break;
        }
        case 'j':
            e.preventDefault();
            moveFocus(1);
            break;
        case 'k':
            e.preventDefault();
            moveFocus(-1);
            break;
        case 'Enter': {
            if (focusIdx >= 0) {
                var items = results();
                var link = items[focusIdx] &&
                    items[focusIdx].querySelector('a.result-title, h3 a, a[href]');
                if (link) {
                    e.preventDefault();
                    window.location.href = link.href;
                }
            }
            break;
        }
        default:
            break;
        }
    });
})();
