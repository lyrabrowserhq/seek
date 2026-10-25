/* mobile header: hide on scroll down, show on scroll up - only active
   while the <=900px layout matches. Escape closes open dropdowns. */
(function () {
    'use strict';

    var mq = window.matchMedia('(max-width: 900px)');
    var header = document.querySelector('.main-header, .results-header');
    if (!header) return;
    header.classList.add('hdr-auto');

    var lastY = window.scrollY;
    var hidden = false;
    var ticking = false;

    function headerLocked() {
        if (header.contains(document.activeElement) &&
            document.activeElement !== document.body)
            return true;
        if (header.querySelector('details.box-dropdown[open], ' +
            '.engine-overflow[open], ' +
            '.images-filter-trigger[aria-expanded="true"]'))
            return true;
        return false;
    }

    function setHidden(h) {
        if (hidden === h) return;
        hidden = h;
        header.classList.toggle('hdr-hidden', h);
    }

    function onScroll() {
        if (ticking) return;
        ticking = true;
        requestAnimationFrame(function () {
            ticking = false;
            if (!mq.matches) {
                setHidden(false);
                lastY = window.scrollY;
                return;
            }
            var y = window.scrollY;
            var dy = y - lastY;
            var hh = header.offsetHeight || 60;
            if (y <= hh || headerLocked()) {
                setHidden(false);
            } else if (dy > 8) {
                setHidden(true);
            } else if (dy < -8) {
                setHidden(false);
            }
            lastY = y;
        });
    }

    window.addEventListener('scroll', onScroll, { passive: true });
    header.addEventListener('focusin', function () { setHidden(false); });
    window.addEventListener('resize', function () { setHidden(false); });
    if (mq.addEventListener)
        mq.addEventListener('change', function () { setHidden(false); });
    else if (mq.addListener)
        mq.addListener(function () { setHidden(false); });

    /* bring the active tab into view inside the horizontal scroller
       without scrolling the page */
    function revealActiveTab() {
        var track = document.querySelector('.nav-tabs-track');
        if (!track) return;
        var active = track.querySelector('a.active');
        if (!active) return;
        var a = active.offsetLeft - (track.clientWidth - active.offsetWidth) / 2;
        track.scrollLeft = Math.max(0, a);
    }
    if (document.readyState === 'loading')
        document.addEventListener('DOMContentLoaded', revealActiveTab);
    else
        revealActiveTab();

    /* Escape closes any open header dropdown and returns focus to its
       summary or trigger */
    document.addEventListener('keydown', function (e) {
        if (e.key !== 'Escape') return;
        var openDetails = document.querySelectorAll(
            'details.box-dropdown[open], details.engine-overflow[open]');
        var handled = false;
        openDetails.forEach(function (d) {
            d.removeAttribute('open');
            var s = d.querySelector('summary');
            if (s) s.focus();
            handled = true;
        });
        var trig = document.querySelector(
            '.images-filter-trigger[aria-expanded="true"]');
        if (trig) {
            trig.click();
            trig.focus();
            handled = true;
        }
        if (handled) e.preventDefault();
    });
})();
