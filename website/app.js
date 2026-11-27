/* KT Game Booster — site público 1.0 (vanilla, sem dependências) */
(function () {
  'use strict';

  var FINE = window.matchMedia('(pointer: fine)').matches;

  /* ---------- ano ---------- */
  var yearOut = document.getElementById('yr');
  if (yearOut) yearOut.textContent = String(new Date().getFullYear());

  /* ---------- menu mobile ---------- */
  var burger = document.getElementById('burger');
  var mobileNav = document.getElementById('navlinks-mobile');
  if (burger && mobileNav) {
    burger.addEventListener('click', function () {
      var open = mobileNav.classList.toggle('open');
      burger.setAttribute('aria-expanded', open ? 'true' : 'false');
    });
    mobileNav.addEventListener('click', function (e) {
      if (e.target.closest('a')) {
        mobileNav.classList.remove('open');
        burger.setAttribute('aria-expanded', 'false');
      }
    });
  }

  /* ---------- scroll: progresso + header + spy + to-top + parallax (rAF) ---------- */
  var topbar = document.querySelector('.topbar');
  var progress = document.getElementById('topbar-progress');
  var toTop = document.getElementById('to-top');
  var heroVisual = document.querySelector('.hero-visual');
  var navLinks = Array.prototype.slice.call(document.querySelectorAll('.nav a'));
  var spyed = navLinks.map(function (a) {
    var id = (a.getAttribute('href') || '').slice(1);
    var el = document.getElementById(id);
    return el ? { link: a, el: el } : null;
  }).filter(Boolean);

  var ticking = false;
  function onScroll() {
    if (ticking) return;
    ticking = true;
    requestAnimationFrame(function () {
      ticking = false;
      var doc = document.documentElement;
      var max = doc.scrollHeight - window.innerHeight;
      if (progress) progress.style.width = (max > 0 ? (window.scrollY / max) * 100 : 0) + '%';
      if (topbar) topbar.classList.toggle('scrolled', window.scrollY > 8);
      if (toTop) toTop.classList.toggle('show', window.scrollY > 700);
      if (heroVisual) {
        if (window.scrollY > 40 && window.scrollY < window.innerHeight) {
          heroVisual.style.transform = 'translateY(' + (window.scrollY * -0.05) + 'px)';
        } else if (window.scrollY <= 40) {
          heroVisual.style.transform = '';
        }
      }
      var y = window.scrollY + 130;
      var current = null;
      for (var i = 0; i < spyed.length; i++) {
        if (spyed[i].el.offsetTop <= y) current = spyed[i];
      }
      spyed.forEach(function (s) { s.link.classList.toggle('is-active', s === current); });
    });
  }
  window.addEventListener('scroll', onScroll, { passive: true });
  window.addEventListener('resize', onScroll);
  onScroll();

  if (toTop) {
    toTop.addEventListener('click', function () {
      window.scrollTo({ top: 0, behavior: 'smooth' });
    });
  }

  /* ---------- glow que segue o cursor (lerp) ---------- */
  var cgx = -999, cgy = -999, tgx = -999, tgy = -999, cgRaf = null;
  function cgStep() {
    cgx += (tgx - cgx) * 0.12;
    cgy += (tgy - cgy) * 0.12;
    document.documentElement.style.setProperty('--cgx', cgx.toFixed(1) + 'px');
    document.documentElement.style.setProperty('--cgy', cgy.toFixed(1) + 'px');
    if (Math.abs(tgx - cgx) > 0.5 || Math.abs(tgy - cgy) > 0.5) {
      cgRaf = requestAnimationFrame(cgStep);
    } else {
      cgRaf = null;
    }
  }
  document.addEventListener('mousemove', function (e) {
    tgx = e.clientX;
    tgy = e.clientY;
    if (!cgRaf) cgRaf = requestAnimationFrame(cgStep);
  });

  /* ---------- reveals com stagger ---------- */
  var revealables = document.querySelectorAll('[data-re]');
  var io = new IntersectionObserver(function (entries) {
    entries.forEach(function (en) {
      if (en.isIntersecting) {
        en.target.classList.add('in');
        io.unobserve(en.target);
      }
    });
  }, { threshold: 0.12, rootMargin: '0px 0px -6% 0px' });
  revealables.forEach(function (el) { io.observe(el); });

  /* ---------- desenho da curva do bench ---------- */
  var curve = document.querySelector('.curve');
  if (curve) {
    var cio = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        if (en.isIntersecting) {
          curve.classList.add('in');
          cio.disconnect();
        }
      });
    }, { threshold: 0.3 });
    cio.observe(curve);
  }

  /* ---------- contadores genéricos ---------- */
  var counters = document.querySelectorAll('[data-count]:not([data-skip-count])');
  var nio = new IntersectionObserver(function (entries) {
    entries.forEach(function (en) {
      if (!en.isIntersecting) return;
      var el = en.target;
      nio.unobserve(el);
      var target = parseInt(el.getAttribute('data-count'), 10);
      if (isNaN(target)) return;
      var plain = el.getAttribute('data-plain');
      var t0 = null;
      var dur = 1200;
      var pad = target < 10 ? '0' : '';
      function frame(ts) {
        if (!t0) t0 = ts;
        var p = Math.min((ts - t0) / dur, 1);
        var eased = 1 - Math.pow(1 - p, 3);
        el.textContent = pad + Math.round(target * eased);
        if (p < 1) requestAnimationFrame(frame);
        else {
          el.textContent = plain || (pad + target);
          el.classList.add('bump');
        }
      }
      requestAnimationFrame(frame);
    });
  }, { threshold: 0.5 });
  counters.forEach(function (el) { nio.observe(el); });

  /* ---------- bench: score + gauge + barra juntos ---------- */
  var benchBar = document.getElementById('bench-bar');
  var benchNum = document.querySelector('.bench-gauge .gg-num');
  var gauge = document.getElementById('gauge-fg');
  if (benchBar && benchNum && gauge) {
    var bbox = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        if (!en.isIntersecting) return;
        bbox.disconnect();
        var C = 2 * Math.PI * 52;
        var t0 = null;
        function frame(ts) {
          if (!t0) t0 = ts;
          var p = Math.min((ts - t0) / 1400, 1);
          var eased = 1 - Math.pow(1 - p, 3);
          var v = Math.round(86 * eased);
          benchNum.textContent = String(v);
          benchBar.style.width = (86 * eased) + '%';
          gauge.style.strokeDashoffset = String(C * (1 - (86 * eased) / 100));
          if (p < 1) requestAnimationFrame(frame);
          else benchNum.classList.add('bump');
        }
        requestAnimationFrame(frame);
      });
    }, { threshold: 0.35 });
    bbox.observe(benchBar);
  }

  /* ---------- painel do hero: score + barra ---------- */
  var appBar = document.getElementById('score-bar-el');
  var appScore = document.querySelector('.app-window [data-count]');
  if (appScore && appBar) {
    var aiox = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        if (!en.isIntersecting) return;
        aiox.disconnect();
        var t0 = null;
        function frame(ts) {
          if (!t0) t0 = ts;
          var p = Math.min((ts - t0) / 1400, 1);
          var eased = 1 - Math.pow(1 - p, 3);
          var v = Math.round(86 * eased);
          appScore.textContent = String(v);
          appBar.style.width = (86 * eased) + '%';
          if (p < 1) requestAnimationFrame(frame);
        }
        requestAnimationFrame(frame);
      });
    }, { threshold: 0.4 });
    aiox.observe(appScore);
  }

  /* ---------- tilt com lerp (rAF) ---------- */
  var tiltCard = document.getElementById('tilt-card');
  if (tiltCard && FINE) {
    var tx = 0, ty = 0, cx = 0, cy = 0, raf = null;
    function loop() {
      cx += (tx - cx) * 0.14;
      cy += (ty - cy) * 0.14;
      tiltCard.style.setProperty('--ry', (cx * 7).toFixed(2) + 'deg');
      tiltCard.style.setProperty('--rx', (-cy * 5).toFixed(2) + 'deg');
      tiltCard.style.setProperty('--ty', (-Math.abs(cx) * 2).toFixed(2) + 'px');
      if (Math.abs(tx - cx) > 0.001 || Math.abs(ty - cy) > 0.001) {
        raf = requestAnimationFrame(loop);
      } else {
        raf = null;
      }
    }
    tiltCard.addEventListener('mousemove', function (e) {
      var r = tiltCard.getBoundingClientRect();
      tx = (e.clientX - r.left) / r.width - 0.5;
      ty = (e.clientY - r.top) / r.height - 0.5;
      if (!raf) raf = requestAnimationFrame(loop);
    });
    tiltCard.addEventListener('mouseleave', function () {
      tx = 0; ty = 0;
      if (!raf) raf = requestAnimationFrame(loop);
    });
  }

  /* ---------- tilt 3D sutil nos cards do bento ---------- */
  if (FINE) {
    document.querySelectorAll('.bc').forEach(function (card) {
      card.addEventListener('pointermove', function (e) {
        var r = card.getBoundingClientRect();
        var px = (e.clientX - r.left) / r.width - 0.5;
        var py = (e.clientY - r.top) / r.height - 0.5;
        card.style.setProperty('--bry', (px * 3).toFixed(2) + 'deg');
        card.style.setProperty('--brx', (-py * 3).toFixed(2) + 'deg');
      });
      card.addEventListener('pointerleave', function () {
        card.style.setProperty('--bry', '0deg');
        card.style.setProperty('--brx', '0deg');
      });
    });
  }

  /* ---------- spotlight nos cards + botões magnéticos ---------- */
  if (FINE) {
    document.querySelectorAll('.spot').forEach(function (card) {
      card.addEventListener('mousemove', function (e) {
        var r = card.getBoundingClientRect();
        card.style.setProperty('--mx', (e.clientX - r.left) + 'px');
        card.style.setProperty('--my', (e.clientY - r.top) + 'px');
      });
    });

    document.querySelectorAll('.btn-mag').forEach(function (btn) {
      btn.addEventListener('mousemove', function (e) {
        var r = btn.getBoundingClientRect();
        var dx = (e.clientX - r.left) / r.width - 0.5;
        var dy = (e.clientY - r.top) / r.height - 0.5;
        btn.style.transform = 'translate(' + (dx * 6) + 'px,' + (dy * 4) + 'px)';
      });
      btn.addEventListener('mouseleave', function () {
        btn.style.transform = '';
      });
    });
  }

  /* ---------- terminal do hero (frases com typewriter) ---------- */
  var heroStatus = document.getElementById('app-status-txt');
  var heroTerm = document.getElementById('term-line');
  var heroPhrases = [
    'aguardando jogo…',
    'valorant-win64-shipping.exe detectado',
    'preset tournament aplicado · AC-safe on',
    'timer 1.00 ms · prio HIGH · sets P-cores',
    'governor ativo · tick 1 s',
    'journal pronto para rollback'
  ];
  if (heroStatus && heroTerm) {
    var hIdx = 0;
    var heroTimer = null;
    function heroType() {
      hIdx = (hIdx + 1) % heroPhrases.length;
      var line = heroPhrases[hIdx];
      heroStatus.textContent = (hIdx >= 1 && hIdx <= 4) ? 'otimizando' : 'em pé';
      var c = 0;
      if (heroTimer) clearInterval(heroTimer);
      heroTimer = setInterval(function () {
        c++;
        heroTerm.textContent = line.slice(0, c);
        if (c >= line.length) {
          clearInterval(heroTimer);
          heroTimer = null;
        }
      }, 22);
    }
    setInterval(heroType, 3100);
  } else if (heroTerm) {
    heroTerm.textContent = heroPhrases[3];
  }

  /* ---------- console de fluxo ---------- */
  var flowSteps = Array.prototype.slice.call(document.querySelectorAll('.flow-step'));
  var flowRail = document.getElementById('flow-rail-fill');
  var flowStatus = document.getElementById('flow-status');
  var flowHeadline = document.getElementById('flow-headline');
  var flowDesc = document.getElementById('flow-desc');
  var flowTerms = [0, 1, 2].map(function (n) { return document.getElementById('cterm' + n); });
  var flowLines = [
    {
      status: 'ETAPA 01 / DETECÇÃO',
      head: 'Nenhuma mudança começa sem contexto.',
      desc: 'Nome, caminho de loja, cadeia de launcher, confiança e manifest do EGS classificam o processo antes de qualquer mudança.',
      lines: [
        'detect: valorant-win64-shipping.exe (confidence 95)',
        'contexto: riot client chain ok · caminho de loja verificado',
        'preset: regra tournament encontrada para o exe'
      ]
    },
    {
      status: 'ETAPA 02 / APLICAÇÃO',
      head: 'O preset carrega antes da trava de anticheat.',
      desc: 'Em títulos com AC de kernel, a sequência segura derruba suspensões, serviços e limitadores — e mantém prioridade HIGH e levers user-mode.',
      lines: [
        'apply: perfil tournament.toml (AC-safe mode on)',
        'timer: 1.00 ms · prioridade HIGH · cpu sets P-cores',
        'fundo: ecoQoS on · limiter on (exceções respeitadas)'
      ]
    },
    {
      status: 'ETAPA 03 / MONITORAMENTO',
      head: 'Um tick de 1 segundo gerencia tudo.',
      desc: 'O governor segue o jogo, o FPS monitor ETW marca o quadro, e o log do painel mostra exatamente o que aconteceu.',
      lines: [
        'tick: session alive · 0.00 ms sched p95',
        'fps: 238 (present etw) · frame time 4.2 ms',
        'painel: modo jogo (20 fps, sem sombras)'
      ]
    },
    {
      status: 'ETAPA 04 / RESTAURAÇÃO',
      head: 'Tudo volta na ordem inversa.',
      desc: 'O journal registra cada mudança. Encerramento normal, Ctrl+C ou até crash — o rollback roda imediatamente na próxima sessão do motor.',
      lines: [
        'restaurar: prioridade, afinidade, ecoQoS, serviços',
        'journal: 11 entradas desfeitas em 0.012 s',
        'próximo: sistema volta ao estado original'
      ]
    }
  ];

  var flowIdx = 0;
  var flowTimer = null;
  var typeTimers = [];

  function clearType() {
    typeTimers.forEach(function (t) { clearInterval(t); });
    typeTimers = [];
  }

  function fitFlow(n) {
    flowSteps.forEach(function (s, i) {
      var active = i === n;
      s.classList.toggle('is-active', active);
      s.setAttribute('aria-selected', active ? 'true' : 'false');
    });
    if (flowRail) flowRail.style.height = ((n + 1) / flowSteps.length) * 100 + '%';
    var d = flowLines[n];
    if (flowStatus) flowStatus.textContent = d.status;
    if (flowHeadline) flowHeadline.textContent = d.head;
    if (flowDesc) flowDesc.textContent = d.desc;
    clearType();
    flowTerms.forEach(function (el, i) {
      if (!el) return;
      var line = d.lines[i] || '';
      var c = 0;
      el.textContent = '';
      var t = setInterval(function () {
        c++;
        el.textContent = line.slice(0, c);
        if (c >= line.length) clearInterval(t);
      }, 16 + i * 90);
      typeTimers.push(t);
    });
  }

  function startFlowAuto() {
    if (flowTimer) return;
    flowTimer = setInterval(function () {
      flowIdx = (flowIdx + 1) % flowSteps.length;
      fitFlow(flowIdx);
    }, 6400);
  }

  flowSteps.forEach(function (btn, i) {
    btn.addEventListener('click', function () {
      flowIdx = i;
      fitFlow(i);
      if (flowTimer) { clearInterval(flowTimer); flowTimer = null; }
      startFlowAuto();
    });
  });
  fitFlow(0);
  var fio = new IntersectionObserver(function (entries) {
    entries.forEach(function (en) {
      if (en.isIntersecting) {
        fio.disconnect();
        startFlowAuto();
      }
    });
  }, { threshold: 0.35 });
  var consoleEl = document.querySelector('.flow-console');
  if (consoleEl) fio.observe(consoleEl);

  /* ---------- marquee (todos os títulos, laço contínuo) ---------- */
  var marquee = document.getElementById('marquee-track');
  function buildMarquee() {
    if (!marquee) return;
    marquee.textContent = '';
    presetNames.forEach(function (item) {
      var s = document.createElement('span');
      s.textContent = item[0].toUpperCase();
      marquee.appendChild(s);
    });
    Array.prototype.slice.call(marquee.children).forEach(function (c) {
      marquee.appendChild(c.cloneNode(true));
    });
  }

  /* ---------- fundo de partículas interativas ---------- */
  (function () {
    var canvas = document.getElementById('bg-canvas');
    if (!canvas || !canvas.getContext) return;
    var ctx = canvas.getContext('2d');
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    var W = 0, H = 0, parts = [], rings = [];
    var mouse = { x: -9999, y: -9999 };
    var LINK = 110, CURSOR = 150, REPEL = 90;

    function resize() {
      dpr = Math.min(window.devicePixelRatio || 1, 2);
      W = window.innerWidth;
      H = window.innerHeight;
      canvas.width = Math.floor(W * dpr);
      canvas.height = Math.floor(H * dpr);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      var target = Math.min(120, Math.max(36, Math.floor((W * H) / 17000)));
      while (parts.length < target) parts.push(make());
      if (parts.length > target) parts.length = target;
    }
    function make() {
      return {
        x: Math.random() * W,
        y: Math.random() * H,
        vx: (Math.random() - 0.5) * 0.4,
        vy: (Math.random() - 0.5) * 0.4,
        r: 0.7 + Math.random() * 1.1
      };
    }

    window.addEventListener('resize', resize);
    document.addEventListener('mousemove', function (e) {
      mouse.x = e.clientX;
      mouse.y = e.clientY;
    });
    document.addEventListener('mouseleave', function () {
      mouse.x = -9999;
      mouse.y = -9999;
    });
    document.addEventListener('click', function (e) {
      if (rings.length < 6) rings.push({ x: e.clientX, y: e.clientY, r: 2, a: 0.5 });
    });
    window.addEventListener('blur', function () {
      mouse.x = -9999;
      mouse.y = -9999;
    });

    resize();

    function step() {
      ctx.clearRect(0, 0, W, H);
      var i, j, p, q, dx, dy, d, f;

      for (i = 0; i < parts.length; i++) {
        p = parts[i];
        dx = p.x - mouse.x; dy = p.y - mouse.y;
        d = Math.sqrt(dx * dx + dy * dy);
        if (d < REPEL && d > 0.01) {
          f = ((REPEL - d) / REPEL) * 0.09;
          p.vx += (dx / d) * f;
          p.vy += (dy / d) * f;
        }
        p.vx *= 0.985; p.vy *= 0.985;
        if (Math.abs(p.vx) < 0.04) p.vx += (Math.random() - 0.5) * 0.02;
        if (Math.abs(p.vy) < 0.04) p.vy += (Math.random() - 0.5) * 0.02;
        p.x += p.vx; p.y += p.vy;
        if (p.x < -12) p.x = W + 12; else if (p.x > W + 12) p.x = -12;
        if (p.y < -12) p.y = H + 12; else if (p.y > H + 12) p.y = -12;
      }

      ctx.lineWidth = 1;
      for (i = 0; i < parts.length; i++) {
        p = parts[i];
        for (j = i + 1; j < parts.length; j++) {
          q = parts[j];
          dx = p.x - q.x; dy = p.y - q.y;
          d = Math.sqrt(dx * dx + dy * dy);
          if (d < LINK) {
            ctx.strokeStyle = 'rgba(155,124,255,' + (0.15 * (1 - d / LINK)).toFixed(3) + ')';
            ctx.beginPath();
            ctx.moveTo(p.x, p.y);
            ctx.lineTo(q.x, q.y);
            ctx.stroke();
          }
        }
        dx = p.x - mouse.x; dy = p.y - mouse.y;
        d = Math.sqrt(dx * dx + dy * dy);
        if (d < CURSOR) {
          ctx.strokeStyle = 'rgba(155,124,255,' + (0.4 * (1 - d / CURSOR)).toFixed(3) + ')';
          ctx.beginPath();
          ctx.moveTo(p.x, p.y);
          ctx.lineTo(mouse.x, mouse.y);
          ctx.stroke();
        }
      }

      ctx.fillStyle = 'rgba(236,234,246,.55)';
      for (i = 0; i < parts.length; i++) {
        p = parts[i];
        ctx.beginPath();
        ctx.arc(p.x, p.y, p.r, 0, 6.2832);
        ctx.fill();
      }

      for (i = rings.length - 1; i >= 0; i--) {
        var ring = rings[i];
        ring.r += 3.4;
        ring.a *= 0.93;
        if (ring.a < 0.02) { rings.splice(i, 1); continue; }
        ctx.strokeStyle = 'rgba(155,124,255,' + ring.a.toFixed(3) + ')';
        ctx.beginPath();
        ctx.arc(ring.x, ring.y, ring.r, 0, 6.2832);
        ctx.stroke();
      }

      requestAnimationFrame(step);
    }
    requestAnimationFrame(step);
  })();

  /* ---------- explorer de presets ---------- */
  var presetNames = [
    ['valorant', 'torneio'], ['cs2', 'torneio'], ['counter-strike global offensive', 'torneio'],
    ['apex legends', 'torneio'], ['fortnite', 'torneio'], ['escape from tarkov', 'torneio'],
    ['rainbow six siege', 'torneio'], ['overwatch 2', 'torneio'], ['pubg', 'torneio'],
    ['warzone', 'torneio'], ['call of duty', 'torneio'], ['league of legends', 'torneio'],
    ['destiny 2', 'torneio'], ['rust', 'torneio'], ['rocket league', 'torneio'],
    ['deadlock', 'torneio'], ['the finals', 'torneio'], ['marvel rivals', 'torneio'],
    ['delta force', 'torneio'], ['hunt: showdown', 'torneio'], ['insurgency sandstorm', 'torneio'],
    ['ready or not', 'torneio'], ['squad', 'torneio'], ['hell let loose', 'torneio'],
    ['black ops 6', 'torneio'],
    ['elden ring', 'bg'], ['elden ring nightreign', 'bg'], ['sekiro', 'bg'],
    ['dark souls iii', 'bg'], ['red dead redemption 2', 'bg'], ['starfield', 'bg'],
    ['skyrim se', 'bg'], ['fallout 4', 'bg'], ['baldur\'s gate 3', 'bg'],
    ['valheim', 'bg'], ['stardew valley', 'bg'], ['monster hunter world', 'bg'],
    ['monster hunter wilds', 'bg'], ['stalker 2', 'bg'], ['the witcher 3', 'bg'],
    ['cyberpunk 2077', 'bg'], ['path of exile', 'bg'], ['path of exile 2', 'bg'],
    ['diablo iv', 'bg'], ['final fantasy xiv', 'bg'], ['minecraft', 'bg'],
    ['euro truck simulator 2', 'bg'], ['american truck simulator', 'bg'],
    ['microsoft flight simulator', 'bg'], ['black myth wukong', 'bg'],
    ['space marine 2', 'bg'], ['kingdom come deliverance ii', 'bg'],
    ['ghost of tsushima', 'bg'], ['the last of us part i', 'bg'], ['hogwarts legacy', 'bg'],
    ['palworld', 'bg'], ['sons of the forest', 'bg'], ['ark ascended', 'bg'],
    ['ark survival evolved', 'bg'], ['helldivers 2', 'bg'], ['once human', 'bg']
  ];

  buildMarquee();

  var grid = document.getElementById('preset-grid');
  var searchIn = document.getElementById('preset-search');
  var filterBtns = Array.prototype.slice.call(document.querySelectorAll('.preset-filters .filter'));
  var countOut = document.getElementById('preset-count');
  var noRes = document.getElementById('preset-nores');
  var filterCat = 'all';

  if (grid) {
    presetNames.forEach(function (item) {
      var chip = document.createElement('div');
      chip.className = 'preset-chip';
      chip.setAttribute('data-cat', item[1]);
      chip.setAttribute('data-search', item[0].toLowerCase());
      var name = document.createElement('span');
      name.textContent = item[0];
      var tag = document.createElement('b');
      tag.textContent = item[1] === 'torneio' ? 'TORNEIO' : 'BG-ONLY';
      chip.appendChild(name);
      chip.appendChild(tag);
      grid.appendChild(chip);
    });
  }

  function renderPresets() {
    if (!grid) return;
    var q = (searchIn && searchIn.value ? searchIn.value : '').trim().toLowerCase();
    var visible = [];
    Array.prototype.forEach.call(grid.children, function (chip) {
      var okCat = filterCat === 'all' || chip.getAttribute('data-cat') === filterCat;
      var okText = !q || chip.getAttribute('data-search').indexOf(q) !== -1;
      var show = okCat && okText;
      chip.classList.toggle('is-hidden', !show);
      if (show) visible.push(chip);
    });
    if (noRes) noRes.hidden = visible.length > 0;
    if (countOut) countOut.textContent = visible.length + (visible.length === 1 ? ' título visível' : ' títulos visíveis');
    visible.forEach(function (chip, i) {
      chip.style.animation = 'none';
      void chip.offsetWidth;
      chip.style.animation = 'chipIn .4s cubic-bezier(.22,.75,.25,1) both';
      chip.style.animationDelay = Math.min(i * 12, 300) + 'ms';
    });
  }

  if (searchIn) {
    searchIn.addEventListener('input', renderPresets);
    searchIn.addEventListener('keydown', function (e) {
      if (e.key === 'Escape' && searchIn.value) { searchIn.value = ''; renderPresets(); }
    });
  }
  filterBtns.forEach(function (b) {
    b.addEventListener('click', function () {
      filterCat = b.getAttribute('data-filter');
      filterBtns.forEach(function (x) {
        var active = x === b;
        x.classList.toggle('active', active);
        x.setAttribute('aria-pressed', active ? 'true' : 'false');
      });
      renderPresets();
    });
  });
  renderPresets();

  /* ---------- atalho "/" foca a busca de jogos ---------- */
  document.addEventListener('keydown', function (e) {
    if (e.key !== '/') return;
    var ae = document.activeElement;
    if (ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA')) return;
    if (!searchIn) return;
    e.preventDefault();
    searchIn.focus();
    searchIn.scrollIntoView({ behavior: 'smooth', block: 'center' });
  });

  /* ---------- FAQ (accordion CSS grid + classe) ---------- */
  document.querySelectorAll('.faq').forEach(function (item) {
    var btn = item.querySelector('.faq-q');
    if (!btn) return;
    btn.addEventListener('click', function () {
      var open = item.classList.contains('open');
      document.querySelectorAll('.faq.open').forEach(function (o) {
        o.classList.remove('open');
        var obtn = o.querySelector('.faq-q');
        if (obtn) obtn.setAttribute('aria-expanded', 'false');
      });
      if (!open) {
        item.classList.add('open');
        btn.setAttribute('aria-expanded', 'true');
      }
    });
  });
})();
