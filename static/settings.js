document.addEventListener('DOMContentLoaded', () => {
  const form = document.querySelector('.settings-form');
  if (!form) return;
  const status = document.getElementById('settings-save-status');
  const savedLabel = (status && status.dataset.saved) || 'Saved';
  let timer = 0;
  let inflight = 0;

  function save() {
    const params = new URLSearchParams(new FormData(form));
    params.set('format', 'json');
    inflight += 1;
    if (status) status.textContent = '';
    fetch('/save_settings?' + params.toString(), { credentials: 'same-origin' })
      .then((r) => r.json())
      .then((data) => {
        inflight -= 1;
        if (status && data && data.ok && inflight === 0)
          status.textContent = savedLabel;
      })
      .catch(() => {
        inflight -= 1;
      });
  }

  form.addEventListener('change', () => {
    clearTimeout(timer);
    timer = setTimeout(save, 250);
  });
  form.addEventListener('submit', (e) => {
    e.preventDefault();
    clearTimeout(timer);
    save();
  });
});
