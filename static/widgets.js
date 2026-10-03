(function () {
  'use strict';

  function evaluateExpression(raw) {
    if (!raw || !String(raw).trim()) return null;
    let expr = String(raw).trim();
    if (/[;'"]|__|constructor|prototype|import|export/.test(expr)) return NaN;
    let s = expr.replace(/\s+/g, '');
    s = s.replace(/\^/g, '**');
    s = s.replace(/\bsin\(/g, 'Math.sin(');
    s = s.replace(/\bcos\(/g, 'Math.cos(');
    s = s.replace(/\btan\(/g, 'Math.tan(');
    s = s.replace(/\bsqrt\(/g, 'Math.sqrt(');
    s = s.replace(/\bln\(/g, 'Math.log(');
    s = s.replace(/\blog\(/g, 'Math.log10(');
    s = s.replace(/\babs\(/g, 'Math.abs(');
    if (window.__seekerCalcDeg) {
      s = s.replace(/Math\.sin\(([^)(]+(?:\([^)(]*\))*[^)(]*)\)/g, (_, a) => 'Math.sin((' + a + ')*Math.PI/180)');
      s = s.replace(/Math\.cos\(([^)(]+(?:\([^)(]*\))*[^)(]*)\)/g, (_, a) => 'Math.cos((' + a + ')*Math.PI/180)');
      s = s.replace(/Math\.tan\(([^)(]+(?:\([^)(]*\))*[^)(]*)\)/g, (_, a) => 'Math.tan((' + a + ')*Math.PI/180)');
    }
    try {
      return Function('"use strict"; return (' + s + ')')();
    } catch (e) {
      return NaN;
    }
  }

  function el(tag, cls, text) {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text !== undefined && text !== null) n.textContent = text;
    return n;
  }

  function mountCalculator(host) {
    const root = host.querySelector('.widget-calc-root');
    if (!root) return;
    const initialExpr = host.dataset.initialExpr || '';
    const initialResult = host.dataset.initialResult || '';
    let expr = initialExpr;
    let advanced = false;
    window.__seekerCalcDeg = true;

    const wrap = el('div', 'widget-calc-app');
    const display = el('div', 'widget-calc-display mono', expr || '0');
    display.setAttribute('aria-live', 'polite');
    display.setAttribute('role', 'textbox');
    display.setAttribute('aria-label', 'Expression');

    const rowMode = el('div', 'widget-calc-toolbar');
    const btnAdv = el('button', 'widget-calc-mode-btn', 'Advanced');
    btnAdv.type = 'button';
    btnAdv.setAttribute('aria-pressed', 'false');
    const btnDeg = el('button', 'widget-calc-mode-btn', 'Deg');
    btnDeg.type = 'button';
    btnDeg.setAttribute('aria-pressed', 'true');
    rowMode.appendChild(btnAdv);
    rowMode.appendChild(btnDeg);

    const grid = el('div', 'widget-calc-keypad');

    function updateDisp() {
      display.textContent = expr || '0';
    }

    function append(ch) {
      expr += ch;
      updateDisp();
    }

    function back() {
      expr = expr.slice(0, -1);
      updateDisp();
    }

    function clearAll() {
      expr = '';
      updateDisp();
    }

    function equals() {
      const v = evaluateExpression(expr);
      if (v === null || (typeof v === 'number' && (isNaN(v) || !isFinite(v)))) {
        display.textContent = 'Error';
        return;
      }
      expr = String(v);
      const rounded = Math.abs(v) >= 1e15 || (Math.abs(v) < 1e-6 && v !== 0)
        ? v.toExponential(8)
        : String(Number(v.toPrecision(12)));
      expr = rounded;
      updateDisp();
    }

    function addRow(keys) {
      const row = el('div', 'widget-calc-row');
      for (const k of keys) {
        const b = el('button', 'widget-calc-btn', k === 'times' ? String.fromCharCode(215) : k === 'div' ? String.fromCharCode(247) : k);
        b.type = 'button';
        if (k === 'C') {
          b.classList.add('widget-calc-btn-wide');
          b.addEventListener('click', clearAll);
        } else if (k === 'CE') b.addEventListener('click', () => { expr = ''; updateDisp(); });
        else if (k === 'del') b.addEventListener('click', back);
        else if (k === '=') {
          b.classList.add('widget-calc-btn-eq');
          b.addEventListener('click', equals);
        } else if ('0123456789.'.includes(k)) b.addEventListener('click', () => append(k));
        else if (['+', '-', '*', '/', '^', '(', ')'].includes(k)) b.addEventListener('click', () => append(k));
        else if (k === 'times') b.addEventListener('click', () => append('*'));
        else if (k === 'div') b.addEventListener('click', () => append('/'));
        else if (k === 'Math.PI') {
          b.textContent = 'pi';
          b.addEventListener('click', () => append('Math.PI'));
        } else if (k === 'Math.E') {
          b.textContent = 'e';
          b.addEventListener('click', () => append('Math.E'));
        } else b.addEventListener('click', () => append(k));
        row.appendChild(b);
      }
      return row;
    }

    function rebuild() {
      grid.innerHTML = '';
      grid.appendChild(addRow(['C', 'CE', 'del', '/']));
      grid.appendChild(addRow(['7', '8', '9', 'times']));
      grid.appendChild(addRow(['4', '5', '6', '-']));
      grid.appendChild(addRow(['1', '2', '3', '+']));
      grid.appendChild(addRow(['0', '.', '(', ')']));
      grid.appendChild(addRow(['=']));
      if (advanced) {
        grid.appendChild(addRow(['sin(', 'cos(', 'tan(', 'sqrt(']));
        grid.appendChild(addRow(['ln(', 'log(', 'abs(', '^']));
        grid.appendChild(addRow(['Math.PI', 'Math.E']));
      }
    }

    btnAdv.addEventListener('click', () => {
      advanced = !advanced;
      btnAdv.setAttribute('aria-pressed', advanced ? 'true' : 'false');
      rebuild();
    });

    btnDeg.addEventListener('click', () => {
      window.__seekerCalcDeg = !window.__seekerCalcDeg;
      btnDeg.textContent = window.__seekerCalcDeg ? 'Deg' : 'Rad';
      btnDeg.setAttribute('aria-pressed', window.__seekerCalcDeg ? 'true' : 'false');
    });

    wrap.appendChild(rowMode);
    wrap.appendChild(display);
    wrap.appendChild(grid);
    root.appendChild(wrap);

    rebuild();

    if (initialExpr) expr = initialExpr;
    else if (initialResult) expr = initialResult;
    updateDisp();
  }

  const COMMON_TZ = [
    'UTC', 'Europe/London', 'Europe/Paris', 'Europe/Berlin', 'Asia/Tokyo',
    'Asia/Shanghai', 'Australia/Sydney', 'America/New_York', 'America/Chicago',
    'America/Denver', 'America/Los_Angeles', 'America/Sao_Paulo',
  ];

  function mountDateTime(host) {
    const root = host.querySelector('.widget-datetime-root');
    if (!root) return;
    const preset = host.dataset.preset || 'local';
    const iana = host.dataset.iana || '';
    const da = host.getAttribute('data-date-a') || '';
    const db = host.getAttribute('data-date-b') || '';

    const wrap = el('div', 'widget-datetime-app');

    const lbl = el('div', 'widget-datetime-label', 'Time zone');
    const sel = document.createElement('select');
    sel.className = 'widget-datetime-select';
    COMMON_TZ.forEach((z) => {
      const o = document.createElement('option');
      o.value = z;
      o.textContent = z;
      sel.appendChild(o);
    });
    if (iana && !COMMON_TZ.includes(iana)) {
      const o = document.createElement('option');
      o.value = iana;
      o.textContent = iana;
      sel.appendChild(o);
    }
    sel.value = iana && (COMMON_TZ.includes(iana) || iana) ? iana : 'UTC';

    if (preset === 'datecalc') {
      try {
        const localTz = Intl.DateTimeFormat().resolvedOptions().timeZone;
        if (localTz) {
          const has = [...sel.options].some((o) => o.value === localTz);
          if (!has) {
            const o = document.createElement('option');
            o.value = localTz;
            o.textContent = localTz;
            sel.appendChild(o);
          }
          sel.value = localTz;
        }
      } catch (e) {}
    }

    const out = el('div', 'widget-datetime-value mono', '');
    function tick() {
      try {
        const fmt = new Intl.DateTimeFormat('en-GB', {
          timeZone: sel.value,
          weekday: 'long',
          year: 'numeric',
          month: 'short',
          day: 'numeric',
          hour: '2-digit',
          minute: '2-digit',
          second: '2-digit',
          timeZoneName: 'short',
        });
        out.textContent = fmt.format(new Date());
      } catch (e) {
        out.textContent = 'Invalid zone';
      }
    }
    sel.addEventListener('change', tick);
    tick();
    setInterval(tick, 1000);

    const unixL = el('div', 'widget-datetime-label', 'Unix time');
    const unixV = el('div', 'widget-datetime-value mono', String(Math.floor(Date.now() / 1000)));
    setInterval(() => {
      unixV.textContent = String(Math.floor(Date.now() / 1000));
    }, 1000);

    const diffL = el('div', 'widget-datetime-label', 'Days between dates');
    const d1 = document.createElement('input');
    d1.type = 'date';
    d1.className = 'widget-datetime-date';
    const d2 = document.createElement('input');
    d2.type = 'date';
    d2.className = 'widget-datetime-date';
    if (da) d1.value = da.length >= 10 ? da.slice(0, 10) : da;
    if (db) d2.value = db.length >= 10 ? db.slice(0, 10) : db;
    const diffOut = el('div', 'widget-datetime-meta', '');
    function diff() {
      if (!d1.value || !d2.value) {
        diffOut.textContent = '';
        return;
      }
      const t1 = new Date(d1.value + 'T12:00:00');
      const t2 = new Date(d2.value + 'T12:00:00');
      const days = Math.round(Math.abs(t2 - t1) / 86400000);
      diffOut.textContent = days + ' days';
    }
    d1.addEventListener('change', diff);
    d2.addEventListener('change', diff);
    diff();

    wrap.appendChild(lbl);
    wrap.appendChild(sel);
    wrap.appendChild(out);
    wrap.appendChild(unixL);
    wrap.appendChild(unixV);
    wrap.appendChild(diffL);
    wrap.appendChild(d1);
    wrap.appendChild(d2);
    wrap.appendChild(diffOut);

    if (preset === 'unix') {
      lbl.style.display = 'none';
      sel.style.display = 'none';
      out.style.display = 'none';
    }
    root.appendChild(wrap);
  }

  const UNIT_TO_SI = {
    m: { t: 'L', f: 1 },
    km: { t: 'L', f: 1000 },
    cm: { t: 'L', f: 0.01 },
    mm: { t: 'L', f: 0.001 },
    mi: { t: 'L', f: 1609.344 },
    yd: { t: 'L', f: 0.9144 },
    ft: { t: 'L', f: 0.3048 },
    in: { t: 'L', f: 0.0254 },
    kg: { t: 'W', f: 1 },
    g: { t: 'W', f: 0.001 },
    mg: { t: 'W', f: 1e-6 },
    lb: { t: 'W', f: 0.453592 },
    oz: { t: 'W', f: 0.0283495 },
    l: { t: 'V', f: 1 },
    ml: { t: 'V', f: 0.001 },
    gal: { t: 'V', f: 3.78541 },
    c: { t: 'T', k: 'c' },
    f: { t: 'T', k: 'f' },
    k: { t: 'T', k: 'k' },
  };

  function convertTemp(val, from, to) {
    let c = val;
    if (from === 'f') c = (val - 32) * 5 / 9;
    else if (from === 'k') c = val - 273.15;
    else if (from === 'c') c = val;
    if (to === 'f') return c * 9 / 5 + 32;
    if (to === 'k') return c + 273.15;
    return c;
  }

  function convertUnit(val, from, to) {
    const a = UNIT_TO_SI[from];
    const b = UNIT_TO_SI[to];
    if (!a || !b) return null;
    if (a.t === 'T' && b.t === 'T') {
      return convertTemp(val, a.k, b.k);
    }
    if (a.t !== b.t) return null;
    const si = val * a.f;
    return si / b.f;
  }

  function mountUnit(host) {
    const root = host.querySelector('.widget-conversion-root');
    if (!root) return;
    const keys = Object.keys(UNIT_TO_SI).sort();
    const inp = document.createElement('input');
    inp.type = 'number';
    inp.className = 'widget-unit-input';
    inp.step = 'any';
    inp.value = host.dataset.value || '1';
    const sf = document.createElement('select');
    sf.className = 'widget-unit-select';
    const st = document.createElement('select');
    st.className = 'widget-unit-select';
    keys.forEach((k) => {
      sf.appendChild(new Option(k, k));
      st.appendChild(new Option(k, k));
    });
    sf.value = (host.dataset.from || 'km').toLowerCase();
    st.value = (host.dataset.to || 'mi').toLowerCase();
    const out = el('div', 'widget-conversion-line mono', '');
    function run() {
      const v = parseFloat(inp.value);
      if (isNaN(v)) {
        out.textContent = '';
        return;
      }
      const r = convertUnit(v, sf.value, st.value);
      if (r === null) {
        out.textContent = 'Incompatible units';
        return;
      }
      const fmt = Math.abs(r) >= 1e9 || (Math.abs(r) < 1e-6 && r !== 0)
        ? r.toExponential(6)
        : String(Number(r.toPrecision(10)));
      out.innerHTML = '<span class="widget-conversion-from"><b>' + v + '</b> ' + sf.value + '</span> <span class="widget-conversion-eq">=</span> <span class="widget-conversion-to"><b>' + fmt + '</b> ' + st.value + '</span>';
    }
    inp.addEventListener('input', run);
    sf.addEventListener('change', run);
    st.addEventListener('change', run);
    root.appendChild(inp);
    root.appendChild(sf);
    root.appendChild(st);
    root.appendChild(out);
    run();
  }

  const FIAT = ['USD', 'EUR', 'GBP', 'JPY', 'CHF', 'CAD', 'AUD', 'INR', 'CNY', 'BRL', 'MXN', 'KRW'];

  function mountCurrency(host) {
    const root = host.querySelector('.widget-currency-root');
    if (!root) return;
    const inp = document.createElement('input');
    inp.type = 'number';
    inp.className = 'widget-currency-input';
    inp.step = 'any';
    inp.value = host.dataset.value || '1';
    const sf = document.createElement('select');
    sf.className = 'widget-currency-select';
    const st = document.createElement('select');
    st.className = 'widget-currency-select';
    FIAT.forEach((c) => {
      sf.appendChild(new Option(c, c));
      st.appendChild(new Option(c, c));
    });
    sf.value = host.dataset.from || 'USD';
    st.value = host.dataset.to || 'EUR';
    const rate = parseFloat(host.dataset.rate || '0');
    const out = el('div', 'widget-currency-line', '');
    const meta = el('div', 'widget-currency-rate', '');

    async function run() {
      const v = parseFloat(inp.value);
      if (isNaN(v)) {
        out.textContent = '';
        meta.textContent = '';
        return;
      }
      if (sf.value === st.value) {
        out.textContent = v + ' ' + sf.value;
        meta.textContent = '';
        return;
      }
      try {
        const u = 'https://api.frankfurter.app/latest?amount=' + encodeURIComponent(v) +
          '&from=' + encodeURIComponent(sf.value) + '&to=' + encodeURIComponent(st.value);
        const r = await fetch(u);
        const j = await r.json();
        const res = j.rates && j.rates[st.value];
        if (res === undefined) throw new Error('bad');
        out.innerHTML = '<span class="widget-currency-from"><b>' + v + '</b> ' + sf.value + '</span> <span class="widget-currency-eq">=</span> <span class="widget-currency-to"><b>' + Number(res).toFixed(2) + '</b> ' + st.value + '</span>';
        const one = res / v;
        meta.textContent = '1 ' + sf.value + ' = ' + one.toFixed(4) + ' ' + st.value;
      } catch (e) {
        if (rate > 0 && sf.value === (host.dataset.from || '') && st.value === (host.dataset.to || '')) {
          const res = v * rate;
          out.innerHTML = '<span class="widget-currency-from"><b>' + v + '</b> ' + sf.value + '</span> <span class="widget-currency-eq">=</span> <span class="widget-currency-to"><b>' + res.toFixed(2) + '</b> ' + st.value + '</span> (cached rate)';
          meta.textContent = 'Live rate unavailable; using search result rate.';
        } else {
          out.textContent = 'Could not load rate. Try again.';
          meta.textContent = '';
        }
      }
    }
    inp.addEventListener('input', run);
    sf.addEventListener('change', run);
    st.addEventListener('change', run);
    root.appendChild(inp);
    root.appendChild(sf);
    root.appendChild(st);
    root.appendChild(out);
    root.appendChild(meta);
    run();
  }

  const WMO = {
    0: 'Clear sky', 1: 'Mainly clear', 2: 'Partly cloudy', 3: 'Overcast',
    45: 'Fog', 48: 'Fog', 51: 'Drizzle', 53: 'Drizzle', 55: 'Drizzle',
    56: 'Freezing drizzle', 57: 'Freezing drizzle', 61: 'Rain', 63: 'Rain',
    65: 'Rain', 66: 'Freezing rain', 67: 'Freezing rain', 71: 'Snow',
    73: 'Snow', 75: 'Snow', 77: 'Snow', 80: 'Showers', 81: 'Showers',
    82: 'Heavy showers', 85: 'Snow showers', 86: 'Snow showers',
    95: 'Thunderstorm', 96: 'Thunderstorm', 99: 'Thunderstorm',
  };

  function wmoLabel(code) {
    const c = Math.round(Number(code));
    return WMO[c] || 'Weather';
  }

  const UNIT_KEY = 'seekerWeatherTempUnit';

  function mountWeather(host) {
    const root = host.querySelector('.widget-weather-root');
    if (!root) return;
    const city = (host.dataset.city || '').trim();
    const wrap = el('div', 'widget-weather-app');
    const loading = el('p', 'widget-weather-loading', 'Loading weather…');
    wrap.appendChild(loading);
    root.appendChild(wrap);

    function getUnit() {
      return localStorage.getItem(UNIT_KEY) === 'fahrenheit' ? 'fahrenheit' : 'celsius';
    }

    function setUnit(u) {
      localStorage.setItem(UNIT_KEY, u);
    }

    async function run() {
      if (!city) {
        loading.textContent = 'Missing city.';
        return;
      }
      wrap.innerHTML = '';
      const ld = el('p', 'widget-weather-loading', 'Loading weather…');
      wrap.appendChild(ld);
      try {
        const geoUrl = 'https://geocoding-api.open-meteo.com/v1/search?name=' +
          encodeURIComponent(city) + '&count=1&language=en';
        const gr = await fetch(geoUrl);
        const gj = await gr.json();
        if (!gj.results || !gj.results.length) {
          ld.textContent = 'No location found for "' + city + '".';
          return;
        }
        const loc = gj.results[0];
        const lat = loc.latitude;
        const lon = loc.longitude;
        const label = loc.name + (loc.admin1 ? ', ' + loc.admin1 : '') +
          (loc.country ? ', ' + loc.country : '');

        const tu = getUnit();
        const fu = 'https://api.open-meteo.com/v1/forecast?latitude=' + encodeURIComponent(lat) +
          '&longitude=' + encodeURIComponent(lon) +
          '&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m' +
          '&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max' +
          '&timezone=auto&forecast_days=7&temperature_unit=' + tu +
          '&windspeed_unit=kmh';

        const wr = await fetch(fu);
        const wj = await wr.json();
        if (!wj.current) {
          ld.textContent = 'Forecast unavailable.';
          return;
        }

        wrap.innerHTML = '';
        const head = el('div', 'widget-weather-head');
        const place = el('div', 'widget-weather-place', label);
        const unitBar = el('div', 'widget-weather-units');
        const btnC = el('button', 'widget-weather-unit-btn');
        btnC.type = 'button';
        btnC.setAttribute('aria-label', 'Celsius');
        btnC.setAttribute('aria-pressed', tu === 'celsius' ? 'true' : 'false');
        btnC.innerHTML = '<span class="widget-weather-unit-icon" aria-hidden="true">C</span>';
        const btnF = el('button', 'widget-weather-unit-btn');
        btnF.type = 'button';
        btnF.setAttribute('aria-label', 'Fahrenheit');
        btnF.setAttribute('aria-pressed', tu === 'fahrenheit' ? 'true' : 'false');
        btnF.innerHTML = '<span class="widget-weather-unit-icon" aria-hidden="true">F</span>';
        if (tu === 'celsius') btnC.classList.add('is-active');
        else btnF.classList.add('is-active');
        btnC.addEventListener('click', () => {
          setUnit('celsius');
          run();
        });
        btnF.addEventListener('click', () => {
          setUnit('fahrenheit');
          run();
        });
        unitBar.appendChild(btnC);
        unitBar.appendChild(btnF);
        head.appendChild(place);
        head.appendChild(unitBar);
        wrap.appendChild(head);

        const cur = wj.current;
        const sym = tu === 'celsius' ? 'C' : 'F';
        const curBlock = el('div', 'widget-weather-current');
        const tempMain = el('div', 'widget-weather-temp-main',
          Math.round(cur.temperature_2m * 10) / 10 + String.fromCharCode(176) + sym);
        const curMeta = el('div', 'widget-weather-current-meta');
        curMeta.appendChild(el('span', 'widget-weather-meta-item',
          wmoLabel(cur.weather_code)));
        curMeta.appendChild(document.createTextNode(' '));
        curMeta.appendChild(el('span', 'widget-weather-meta-item',
          'Feels ' + Math.round(cur.apparent_temperature * 10) / 10 + String.fromCharCode(176) + sym));
        curMeta.appendChild(document.createTextNode(' '));
        curMeta.appendChild(el('span', 'widget-weather-meta-item',
          'Humidity ' + Math.round(cur.relative_humidity_2m) + '%'));
        curMeta.appendChild(document.createTextNode(' '));
        curMeta.appendChild(el('span', 'widget-weather-meta-item',
          'Wind ' + Math.round(cur.wind_speed_10m * 10) / 10 + ' km/h'));
        curBlock.appendChild(tempMain);
        curBlock.appendChild(curMeta);
        wrap.appendChild(curBlock);

        const daily = wj.daily;
        if (daily && daily.time && daily.time.length) {
          const days = el('div', 'widget-weather-daily');
          const dh = el('div', 'widget-weather-daily-title', 'Next 7 days');
          days.appendChild(dh);
          const n = Math.min(7, daily.time.length);
          for (let i = 0; i < n; i++) {
            const row = el('div', 'widget-weather-day');
            const d = new Date(daily.time[i] + 'T12:00:00');
            const dayName = d.toLocaleDateString(undefined, { weekday: 'short' });
            const md = d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' });
            const left = el('div', 'widget-weather-day-when');
            left.appendChild(el('span', 'widget-weather-day-name', dayName));
            left.appendChild(document.createTextNode(' '));
            left.appendChild(el('span', 'widget-weather-day-date', md));
            const wx = el('div', 'widget-weather-day-desc',
              wmoLabel(daily.weather_code[i]));
            const tm = el('div', 'widget-weather-day-temps',
              Math.round(daily.temperature_2m_max[i] * 10) / 10 + String.fromCharCode(176) +
              ' / ' + Math.round(daily.temperature_2m_min[i] * 10) / 10 + String.fromCharCode(176));
            const pr = daily.precipitation_probability_max &&
              daily.precipitation_probability_max[i] != null
              ? el('div', 'widget-weather-day-rain',
                'Rain ' + Math.round(daily.precipitation_probability_max[i]) + '%')
              : null;
            row.appendChild(left);
            row.appendChild(wx);
            row.appendChild(tm);
            if (pr) row.appendChild(pr);
            days.appendChild(row);
          }
          wrap.appendChild(days);
        }
      } catch (e) {
        wrap.innerHTML = '';
        wrap.appendChild(el('p', 'widget-weather-error', 'Could not load weather.'));
      }
    }

    run();
  }

  function utf8ToB64(s) {
    const bytes = new TextEncoder().encode(s);
    let bin = '';
    for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
    return btoa(bin);
  }

  function b64ToUtf8(s) {
    const bin = atob(s.replace(/\s/g, ''));
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return new TextDecoder().decode(bytes);
  }

  function b64uDecode(s) {
    s = s.replace(/-/g, '+').replace(/_/g, '/');
    while (s.length % 4) s += '=';
    const bin = atob(s);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return new TextDecoder().decode(bytes);
  }

  function rgbToHslJs(r, g, b) {
    r /= 255; g /= 255; b /= 255;
    const mx = Math.max(r, g, b);
    const mn = Math.min(r, g, b);
    const l = (mx + mn) / 2;
    let h = 0;
    let s = 0;
    const d = mx - mn;
    if (d > 1e-9) {
      s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
      if (mx === r) h = 60 * (g >= b ? (g - b) / d : 6 + (g - b) / d);
      else if (mx === g) h = 60 * ((b - r) / d + 2);
      else h = 60 * ((r - g) / d + 4);
    }
    return { h, s, l };
  }

  function mountDevTools(host) {
    const root = host.querySelector('.widget-devtools-root');
    if (!root) return;
    const mode = (host.dataset.mode || 'menu').toLowerCase();
    const initial = host.getAttribute('data-initial') || '';

    function ta(rows, val) {
      const x = document.createElement('textarea');
      x.className = 'widget-devtools-textarea';
      x.rows = rows;
      x.value = val;
      return x;
    }

    function out() {
      return el('pre', 'widget-devtools-out mono', '');
    }

    function btn(label) {
      const b = el('button', 'widget-devtools-btn', label);
      b.type = 'button';
      return b;
    }

    function sec(title, nodes) {
      const s = el('div', 'widget-devtools-section');
      s.appendChild(el('div', 'widget-devtools-section-title', title));
      for (let i = 0; i < nodes.length; i++) s.appendChild(nodes[i]);
      return s;
    }

    const wrap = el('div', 'widget-devtools-app');

    function addB64Enc(pre) {
      const i = ta(4, pre);
      const o = out();
      const b = btn('Encode');
      b.addEventListener('click', () => {
        try {
          o.textContent = utf8ToB64(i.value);
        } catch (e) {
          o.textContent = String(e.message || e);
        }
      });
      wrap.appendChild(sec('Base64 encode', [i, b, o]));
    }
    function addB64Dec(pre) {
      const i = ta(4, pre);
      const o = out();
      const b = btn('Decode');
      b.addEventListener('click', () => {
        try {
          o.textContent = b64ToUtf8(i.value);
        } catch (e) {
          o.textContent = String(e.message || e);
        }
      });
      wrap.appendChild(sec('Base64 decode', [i, b, o]));
    }
    function addUrlEnc(pre) {
      const i = ta(3, pre);
      const o = out();
      const b = btn('Encode');
      b.addEventListener('click', () => {
        try {
          o.textContent = encodeURIComponent(i.value);
        } catch (e) {
          o.textContent = String(e.message || e);
        }
      });
      wrap.appendChild(sec('URL encode (percent)', [i, b, o]));
    }
    function addUrlDec(pre) {
      const i = ta(3, pre);
      const o = out();
      const b = btn('Decode');
      b.addEventListener('click', () => {
        try {
          o.textContent = decodeURIComponent(i.value);
        } catch (e) {
          o.textContent = String(e.message || e);
        }
      });
      wrap.appendChild(sec('URL decode', [i, b, o]));
    }
    function addJwt(pre) {
      const i = ta(4, pre);
      const o = out();
      const b = btn('Decode header and payload');
      b.addEventListener('click', () => {
        try {
          const parts = i.value.trim().split('.');
          if (parts.length < 2) throw new Error('Need at least two JWT segments');
          const header = JSON.parse(b64uDecode(parts[0]));
          const payload = JSON.parse(b64uDecode(parts[1]));
          o.textContent = JSON.stringify({ header, payload }, null, 2);
        } catch (e) {
          o.textContent = String(e.message || e);
        }
      });
      wrap.appendChild(sec('JWT decode (no signature verify)', [i, b, o]));
    }
    function addSha256(pre) {
      const i = ta(4, pre);
      const o = out();
      const b = btn('SHA-256');
      b.addEventListener('click', async () => {
        try {
          if (!crypto.subtle) {
            o.textContent = 'SHA-256 not available in this context';
            return;
          }
          const buf = new TextEncoder().encode(i.value);
          const hash = await crypto.subtle.digest('SHA-256', buf);
          o.textContent = Array.from(new Uint8Array(hash))
            .map((x) => x.toString(16).padStart(2, '0'))
            .join('');
        } catch (e) {
          o.textContent = String(e.message || e);
        }
      });
      wrap.appendChild(sec('SHA-256', [i, b, o]));
    }
    function addUuid() {
      const o = out();
      const b = btn('New UUID');
      b.addEventListener('click', () => {
        o.textContent = crypto.randomUUID();
      });
      wrap.appendChild(sec('Random UUID', [b, o]));
    }

    if (mode === 'menu') {
      addB64Enc('');
      addB64Dec('');
      addUrlEnc('');
      addUrlDec('');
      addJwt('');
      addSha256('');
      addUuid();
    } else if (mode === 'base64encode') {
      addB64Enc(initial);
    } else if (mode === 'base64decode') {
      addB64Dec(initial);
    } else if (mode === 'urlencode') {
      addUrlEnc(initial);
    } else if (mode === 'urldecode') {
      addUrlDec(initial);
    } else if (mode === 'jwt') {
      addJwt(initial);
    } else if (mode === 'sha256') {
      addSha256(initial);
    } else if (mode === 'uuid') {
      addUuid();
    } else {
      addB64Enc('');
      addB64Dec('');
      addUrlEnc('');
      addUrlDec('');
      addJwt('');
      addSha256('');
      addUuid();
    }

    root.appendChild(wrap);
  }

  function mountColor(host) {
    const root = host.querySelector('.widget-color-root');
    if (!root) return;
    let r = parseInt(host.dataset.r || '0', 10);
    let g = parseInt(host.dataset.g || '0', 10);
    let b = parseInt(host.dataset.b || '0', 10);

    const wrap = el('div', 'widget-color-app');
    const sw = el('div', 'widget-color-swatch');
    const hexIn = el('input', 'widget-currency-input');
    hexIn.type = 'text';
    hexIn.setAttribute('aria-label', 'Hex color');
    const rIn = el('input', 'widget-currency-input');
    const gIn = el('input', 'widget-currency-input');
    const bIn = el('input', 'widget-currency-input');
    rIn.type = 'number';
    gIn.type = 'number';
    bIn.type = 'number';
    rIn.min = 0;
    rIn.max = 255;
    gIn.min = 0;
    gIn.max = 255;
    bIn.min = 0;
    bIn.max = 255;
    rIn.setAttribute('aria-label', 'Red');
    gIn.setAttribute('aria-label', 'Green');
    bIn.setAttribute('aria-label', 'Blue');
    const hslOut = el('div', 'widget-color-meta mono', '');

    function syncFromRgb() {
      r = Math.max(0, Math.min(255, r | 0));
      g = Math.max(0, Math.min(255, g | 0));
      b = Math.max(0, Math.min(255, b | 0));
      const hex = '#' + [r, g, b].map((x) => x.toString(16).padStart(2, '0')).join('');
      hexIn.value = hex;
      sw.style.backgroundColor = hex;
      rIn.value = String(r);
      gIn.value = String(g);
      bIn.value = String(b);
      const hsl = rgbToHslJs(r, g, b);
      hslOut.textContent = 'HSL ' + hsl.h.toFixed(0) + 'deg, ' +
        (hsl.s * 100).toFixed(1) + '%, ' + (hsl.l * 100).toFixed(1) + '%';
    }

    function parseHex(h) {
      const s = h.trim();
      if (!s.startsWith('#')) return null;
      const body = s.slice(1);
      if (!/^[0-9a-fA-F]+$/.test(body)) return null;
      if (body.length === 3) {
        const v = (c) => parseInt(c + c, 16);
        return { r: v(body[0]), g: v(body[1]), b: v(body[2]) };
      }
      if (body.length === 6) {
        return {
          r: parseInt(body.slice(0, 2), 16),
          g: parseInt(body.slice(2, 4), 16),
          b: parseInt(body.slice(4, 6), 16),
        };
      }
      return null;
    }

    hexIn.addEventListener('change', () => {
      const p = parseHex(hexIn.value);
      if (p) {
        r = p.r;
        g = p.g;
        b = p.b;
        syncFromRgb();
      }
    });
    rIn.addEventListener('input', () => {
      r = parseInt(rIn.value, 10) || 0;
      syncFromRgb();
    });
    gIn.addEventListener('input', () => {
      g = parseInt(gIn.value, 10) || 0;
      syncFromRgb();
    });
    bIn.addEventListener('input', () => {
      b = parseInt(bIn.value, 10) || 0;
      syncFromRgb();
    });

    const rgbRow = el('div', 'widget-color-rgb-row');
    rgbRow.appendChild(el('span', 'widget-color-rgb-lbl', 'R'));
    rgbRow.appendChild(rIn);
    rgbRow.appendChild(el('span', 'widget-color-rgb-lbl', 'G'));
    rgbRow.appendChild(gIn);
    rgbRow.appendChild(el('span', 'widget-color-rgb-lbl', 'B'));
    rgbRow.appendChild(bIn);

    wrap.appendChild(sw);
    wrap.appendChild(el('div', 'widget-datetime-label', 'Hex'));
    wrap.appendChild(hexIn);
    wrap.appendChild(el('div', 'widget-datetime-label', 'RGB'));
    wrap.appendChild(rgbRow);
    wrap.appendChild(hslOut);
    root.appendChild(wrap);
    syncFromRgb();
  }

  function utcMillisForWallTime(zone, y, mo, d, hh, mm) {
    let t = Date.UTC(y, mo - 1, d, 12, 0, 0);
    const fmt = new Intl.DateTimeFormat('en-GB', {
      timeZone: zone,
      hour: '2-digit',
      minute: '2-digit',
      hour12: false,
      year: 'numeric',
      month: 'numeric',
      day: 'numeric',
    });
    for (let i = 0; i < 48; i++) {
      const parts = Object.fromEntries(
        fmt.formatToParts(new Date(t)).map((p) => [p.type, p.value])
      );
      const py = +parts.year;
      const pm = +parts.month;
      const pd = +parts.day;
      const ph = +parts.hour;
      const pmi = +parts.minute;
      if (py === y && pm === mo && pd === d && ph === hh && pmi === mm) {
        return t;
      }
      const diff = ((hh - ph) * 60 + (mm - pmi)) * 60000;
      t += diff;
    }
    return t;
  }

  function mountMeeting(host) {
    const root = host.querySelector('.widget-meeting-root');
    if (!root) return;
    const COMMON_TZ = [
      'UTC', 'Europe/London', 'Europe/Paris', 'Europe/Berlin', 'Asia/Tokyo',
      'Asia/Shanghai', 'Australia/Sydney', 'America/New_York', 'America/Chicago',
      'America/Denver', 'America/Los_Angeles', 'America/Sao_Paulo',
    ];
    let fromZ = host.getAttribute('data-from-iana') || 'UTC';
    let toZ = host.getAttribute('data-to-iana') || 'UTC';
    const ds = host.getAttribute('data-date') || '';
    let hh = parseInt(host.dataset.hour || '12', 10);
    let mm = parseInt(host.dataset.minute || '0', 10);

    const wrap = el('div', 'widget-meeting-app');
    const dateIn = document.createElement('input');
    dateIn.type = 'date';
    dateIn.className = 'widget-datetime-date';
    if (ds) dateIn.value = ds;
    const selF = document.createElement('select');
    const selT = document.createElement('select');
    selF.className = 'widget-datetime-select';
    selT.className = 'widget-datetime-select';
    function fillSel(sel, selZ) {
      COMMON_TZ.forEach((z) => sel.appendChild(new Option(z, z)));
      if (!COMMON_TZ.includes(selZ)) {
        sel.appendChild(new Option(selZ, selZ));
      }
      sel.value = selZ;
    }
    fillSel(selF, fromZ);
    fillSel(selT, toZ);
    const timeIn = el('input', 'widget-currency-input');
    timeIn.type = 'time';
    timeIn.value = String(hh).padStart(2, '0') + ':' + String(mm).padStart(2, '0');

    const out = el('div', 'widget-meeting-result mono', '');
    const meta = el('div', 'widget-meeting-meta', '');

    function run() {
      fromZ = selF.value;
      toZ = selT.value;
      const parts = dateIn.value.split('-').map(Number);
      const y = parts[0];
      const mo = parts[1];
      const d = parts[2];
      const tm = timeIn.value.split(':');
      hh = parseInt(tm[0] || '0', 10);
      mm = parseInt(tm[1] || '0', 10);
      if (!y || !dateIn.value) {
        out.textContent = 'Pick a date';
        meta.textContent = '';
        return;
      }
      const utcMs = utcMillisForWallTime(fromZ, y, mo, d, hh, mm);
      const fmt = new Intl.DateTimeFormat('en-GB', {
        timeZone: toZ,
        weekday: 'long',
        year: 'numeric',
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        timeZoneName: 'short',
      });
      out.textContent = fmt.format(new Date(utcMs));
      meta.textContent = 'When ' + timeIn.value + ' on ' + dateIn.value + ' in ' + fromZ +
        ', the time in ' + toZ + ' is shown above.';
    }

    selF.addEventListener('change', run);
    selT.addEventListener('change', run);
    dateIn.addEventListener('change', run);
    timeIn.addEventListener('change', run);
    timeIn.addEventListener('input', run);

    wrap.appendChild(el('div', 'widget-datetime-label', 'Date'));
    wrap.appendChild(dateIn);
    wrap.appendChild(el('div', 'widget-datetime-label', 'Wall time in first zone'));
    wrap.appendChild(timeIn);
    wrap.appendChild(el('div', 'widget-datetime-label', 'First zone (reference)'));
    wrap.appendChild(selF);
    wrap.appendChild(el('div', 'widget-datetime-label', 'Second zone (converted)'));
    wrap.appendChild(selT);
    wrap.appendChild(el('div', 'widget-datetime-label', 'Same instant in second zone'));
    wrap.appendChild(out);
    wrap.appendChild(meta);
    root.appendChild(wrap);
    run();
  }

  function initWidgets(scope) {
    const root = scope || document;
    root.querySelectorAll('[data-widget="calc"]').forEach(mountCalculator);
    root.querySelectorAll('[data-widget="datetime"]').forEach(mountDateTime);
    root.querySelectorAll('[data-widget="conversion"]').forEach(mountUnit);
    root.querySelectorAll('[data-widget="currency"]').forEach(mountCurrency);
    root.querySelectorAll('[data-widget="weather"]').forEach(mountWeather);
    root.querySelectorAll('[data-widget="devtools"]').forEach(mountDevTools);
    root.querySelectorAll('[data-widget="color"]').forEach(mountColor);
    root.querySelectorAll('[data-widget="meeting"]').forEach(mountMeeting);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => initWidgets());
  } else {
    initWidgets();
  }
  window.seekerWidgets = { init: initWidgets };
})();
