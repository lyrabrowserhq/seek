const THEMES = ['light', 'dark'];

function setTheme(theme) {
    if (THEMES.includes(theme)) {
        document.documentElement.setAttribute('data-theme', theme);
        localStorage.setItem('theme', theme);
    } else {
        document.documentElement.removeAttribute('data-theme');
        localStorage.removeItem('theme');
    }

    setTimeout(() => {
        const accent = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
        if (accent) {
            document.querySelector('meta[name="theme-color"]')?.setAttribute('content', accent);
        }
    }, 10);
}

function toggleTheme() {
    const currentTheme = localStorage.getItem('theme');
    const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    const effectiveTheme = currentTheme || (prefersDark ? 'dark' : 'light');
    setTheme(effectiveTheme === 'dark' ? 'light' : 'dark');
}

(function initTheme() {
    const savedTheme = localStorage.getItem('theme');
    if (savedTheme === 'light' || savedTheme === 'dark') {
        setTheme(savedTheme);
        return;
    }
    const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    setTheme(prefersDark ? 'dark' : 'light');
})();

/* delegated handlers so markup needs no inline event attributes */
document.addEventListener('click', (e) => {
    const toggle = e.target.closest('.theme-toggle-btn');
    if (toggle) {
        toggleTheme();
        return;
    }
    const dismiss = e.target.closest('.degradation-dismiss');
    if (dismiss && dismiss.parentElement) {
        dismiss.parentElement.remove();
    }
});

/* capture-phase image error fallback - replaces inline onerror */
document.addEventListener('error', (e) => {
    const img = e.target;
    if (!(img instanceof HTMLImageElement)) return;
    if (img.dataset.imgfbDone) return;
    img.dataset.imgfbDone = '1';
    if (img.hasAttribute('data-infobox-img')) {
        if (img.parentNode) img.parentNode.classList.add('is-broken');
        return;
    }
    img.src = '/static/icon-placeholder.svg';
}, true);
