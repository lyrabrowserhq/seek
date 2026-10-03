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
