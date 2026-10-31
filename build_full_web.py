import gzip
import re

with open('web_clean.html', 'r', encoding='utf-8') as f:
    html = f.read()

# 1. Заміна картки логування на загальному огляді (Overview)
old_card = '''<!-- COMMON: CSV LOGGING CARD -->
<div class="card" style="margin-top:16px">
  <h2>📝 Запис CSV-лога (окремий файл для кожної АКБ)</h2>
  <div style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:12px">
    <div>Файлів у записі: <b id="logpath">—</b> · Записано рядків всього: <span id="logrows">0</span></div>
    <button id="logbtn" onclick="toggleLog()">Старт запису (усі АКБ)</button>
  </div>'''

new_card = '''<!-- COMMON: CSV LOGGING CARD -->
<div class="card" style="margin-top:16px">
  <h2>💾 Запис логів та керування сховищем</h2>
  <div style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:12px;margin-bottom:14px">
    <div>Стан MicroSD картки: <b id="sd_status"><span class="pill">перевірка...</span></b> · Файл: <b id="logpath" style="color:var(--acc)">—</b> · Рядків: <b id="logrows">0</b></div>
    <div style="display:flex;gap:8px;flex-wrap:wrap">
      <button id="logbtn" onclick="toggleLog()">Старт запису на MicroSD (усі)</button>
      <button id="web_rec_btn" onclick="toggleWebBankRecord()" style="background:#238636;color:#fff;font-weight:600">💻 Запис банку в браузер (CSV)</button>
      <button onclick="refreshSdFiles()" style="background:#30363d;color:#fff">🔄 Оновити файли SD</button>
    </div>
  </div>'''

assert old_card in html, "old_card not found in web_clean.html"
html = html.replace(old_card, new_card)

# 2. Оновлення картки логування для окремої батареї (Pack tab)
old_pack_log_card = '''    <div class="card">
      <h2>📝 CSV-лог цієї АКБ</h2>
      <table class="kv" style="width:100%">
        <tr><td style="width:110px">Файл</td><td id="p_logpath" style="font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:11px;color:var(--acc);word-break:break-all">—</td></tr>
        <tr><td>Записано рядків</td><td id="p_logrows">0</td></tr>
        <tr><td>Alarm / Work</td><td><span id="p_log_alarm">0</span> / <span id="p_log_work">0</span></td></tr>
        <tr><td>Останній запис</td><td id="p_logts" style="font-size:12px">—</td></tr>
      </table>
      <div style="margin-top:10px">
        <button id="p_logbtn" onclick="togglePackLog()">Старт запису</button>
      </div>
    </div>'''

new_pack_log_card = '''    <div class="card">
      <h2>📝 Логування цієї АКБ (MicroSD + Браузер)</h2>
      <table class="kv" style="width:100%">
        <tr><td style="width:110px">Файл MicroSD</td><td id="p_logpath" style="font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:11px;color:var(--acc);word-break:break-all">—</td></tr>
        <tr><td>Записано на SD</td><td><b id="p_logrows">0</b> рядків</td></tr>
      </table>
      <div style="margin-top:12px;display:flex;gap:8px;flex-wrap:wrap">
        <button id="p_logbtn" onclick="togglePackLog()">Старт запису на SD</button>
        <button id="p_web_rec_btn" onclick="toggleWebPackRecord()" style="background:#238636;color:#fff;font-weight:600">💻 Запис цієї АКБ в браузер</button>
      </div>
    </div>'''

assert old_pack_log_card in html, "old_pack_log_card not found in web_clean.html"
html = html.replace(old_pack_log_card, new_pack_log_card)

# 3. Оновлення коду в tick(): безпечні перевірки DOM + виклик таблиці SD
old_lf = '''    const lf = document.getElementById('logfiles');
    if (lf) {
      const files = d.log_files || {};
      const keys = Object.keys(files).sort();
      lf.innerHTML = keys.length ? keys.map(k => {
        const f = files[k];
        const state = f.on ? '<span class="pill ok">запис</span>' : '<span class="pill">стоп</span>';
        return `<tr><td>${escapeHtml(k)}</td><td>${escapeHtml(f.path || '—')}</td><td>${f.rows || 0} · ${escapeHtml(f.last_ts || '—')}</td><td>${state}</td><td style="color:var(--dim);font-size:12px">A ${f.alarm_rows || 0} / W ${f.work_rows || 0}</td></tr>`;
      }).join('') : '<tr><td>—</td><td>лог ще не створювався</td><td>—</td><td>—</td><td>—</td></tr>';
    }'''

new_lf = '''    const sdEl = document.getElementById('sd_status');
    if (sdEl) {
      sdEl.innerHTML = d.sd_card ? '<span class="pill ok">Вставлена (Ready)</span>' : '<span class="pill bad">Відсутня</span>';
    }
    const lp = document.getElementById('logpath');
    if (lp) lp.textContent = d.log_path || '—';
    const lr = document.getElementById('logrows');
    if (lr) lr.textContent = d.log_rows || 0;

    renderSdFilesTable();'''

assert old_lf in html, "old_lf not found in web_clean.html"
html = html.replace(old_lf, new_lf)

# 4. Заміна старого коду запису на перевірений повнофункціональний логер
extra_code = '''
// ==========================================
// MicroSD & Browser Robust Logging Engine
// ==========================================
window.sdFilesList = [];

async function refreshSdFiles() {
  try {
    const res = await fetch('/api/logs');
    if (res.ok) {
      const data = await res.json();
      window.sdFilesList = data.files || [];
      renderSdFilesTable();
    }
  } catch(e) {
    console.warn('Error fetching /api/logs:', e);
  }
}

function renderSdFilesTable() {
  const lf = document.getElementById('logfiles');
  if (!lf) return;
  if (!window.sdFilesList || window.sdFilesList.length === 0) {
    lf.innerHTML = '<tr><td colspan="4" style="text-align:center;color:var(--dim);padding:14px">На MicroSD карті модуля поки немає файлів CSV логів</td></tr>';
    return;
  }
  lf.innerHTML = window.sdFilesList.map(f => {
    const szKb = (f.size / 1024).toFixed(1) + ' KB';
    const encName = encodeURIComponent(f.name);
    return `<tr>
      <td>📄 <b>${escapeHtml(f.name)}</b></td>
      <td>${szKb}</td>
      <td>
        <a href="/download?file=${encName}" download class="tab-badge" style="background:#238636;color:#fff;text-decoration:none;padding:5px 12px;border-radius:6px;font-weight:600;display:inline-block">📥 Завантажити CSV</a>
        <button onclick="analyzeSdFile('${encName}')" style="background:#30363d;color:#58a6ff;border:1px solid #30363d;padding:4px 10px;border-radius:6px;cursor:pointer;margin-left:6px">📊 Аналіз</button>
      </td>
      <td>
        <button onclick="deleteSdFile('${encName}')" style="background:#da3633;color:#fff;border:none;padding:4px 10px;border-radius:6px;cursor:pointer">🗑️ Видалити</button>
      </td>
    </tr>`;
  }).join('');
}

async function deleteSdFile(filename) {
  const cleanName = decodeURIComponent(filename);
  if (!confirm('Видалити файл ' + cleanName + ' з MicroSD картки?')) return;
  try {
    const res = await fetch('/delete_log?file=' + filename, { method: 'POST' });
    const data = await res.json();
    if (data.status === 'ok') {
      alert('Файл ' + cleanName + ' успішно видалено з MicroSD!');
      await refreshSdFiles();
    } else {
      alert('Помилка видалення: ' + (data.msg || 'невідома помилка'));
    }
  } catch(e) {
    alert('Помилка мережі при видаленні: ' + e);
  }
}

async function analyzeSdFile(filename) {
  try {
    const res = await fetch('/download?file=' + filename);
    if (!res.ok) throw new Error('Помилка завантаження файлу');
    const text = await res.text();
    selectTab('logviewer');
    parseAndAnalyzeCSV(decodeURIComponent(filename), text);
  } catch(e) {
    alert('Не вдалося відкрити лог для аналізу: ' + e);
  }
}

// Download helper function that guarantees file download
function downloadCsv(filename, rowsArray) {
  const csvContent = rowsArray.join('\\r\\n');
  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.style.display = 'none';
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  setTimeout(() => {
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }, 200);
}

// --- Browser Bank Log Recorder ---
let isWebBankRecording = false;
let webBankRows = [];

function toggleWebBankRecord() {
  const btn = document.getElementById('web_rec_btn');
  if (!isWebBankRecording) {
    isWebBankRecording = true;
    webBankRows = [
      'Timestamp_ISO,Avg_Voltage_V,Total_Current_A,Total_Power_kW,Avg_SOC,Online_Packs,Bank_Delta_mV,Min_Cell_mV,Max_Cell_mV'
    ];
    if (btn) {
      btn.style.background = '#da3633';
      btn.textContent = '⏹ Зупинити та зберегти CSV (0)';
    }
  } else {
    isWebBankRecording = false;
    if (btn) {
      btn.style.background = '#238636';
      btn.textContent = '💻 Запис банку в браузер (CSV)';
    }
    if (webBankRows.length > 1) {
      const now = new Date();
      const pad = n => String(n).padStart(2, '0');
      const dtStr = now.getFullYear() + pad(now.getMonth()+1) + pad(now.getDate()) + '_' + pad(now.getHours()) + pad(now.getMinutes()) + pad(now.getSeconds());
      downloadCsv('bms_bank_web_' + dtStr + '.csv', webBankRows);
    } else {
      alert('Не було записано даних під час сесії.');
    }
  }
}

// --- Browser Individual Pack Log Recorder ---
let isWebPackRecording = false;
let webPackRows = [];
let recordingPackAddr = null;

function toggleWebPackRecord() {
  const btn = document.getElementById('p_web_rec_btn');
  if (!isWebPackRecording) {
    if (!currentPackAddr) {
      alert('Оберіть конкретну вкладку АКБ перед стартом запису!');
      return;
    }
    isWebPackRecording = true;
    recordingPackAddr = currentPackAddr;
    webPackRows = [
      'Timestamp_ISO,Pack,Voltage_V,Current_A,Power_W,SOC,Cell_Min_mV,Cell_Max_mV,Delta_mV,Temp_PCB,Temp_Avg,Temp_Max,Alarms,Cell_1..15'
    ];
    if (btn) {
      btn.style.background = '#da3633';
      btn.textContent = `⏹ Зупинити та зберегти ${recordingPackAddr} (0)`;
    }
  } else {
    isWebPackRecording = false;
    if (btn) {
      btn.style.background = '#238636';
      btn.textContent = '💻 Запис цієї АКБ в браузер';
    }
    if (webPackRows.length > 1) {
      const now = new Date();
      const pad = n => String(n).padStart(2, '0');
      const dtStr = now.getFullYear() + pad(now.getMonth()+1) + pad(now.getDate()) + '_' + pad(now.getHours()) + pad(now.getMinutes()) + pad(now.getSeconds());
      downloadCsv('bms_pack_' + (recordingPackAddr || '01') + '_' + dtStr + '.csv', webPackRows);
    } else {
      alert('Не було записано даних для цієї батареї.');
    }
    recordingPackAddr = null;
  }
}

// Hook into tick() to record data seamlessly on each second
const baseTick = tick;
tick = async function() {
  await baseTick();
  if (!latestData) return;

  // 1. Bank browser logging
  if (isWebBankRecording && latestData.summary) {
    const s = latestData.summary;
    const nowIso = new Date().toISOString();
    const row = `${nowIso},${s.avg_voltage},${s.total_current},${s.total_power_kw},${s.avg_soc},${s.online_packs},${s.cell_delta_mv},${s.min_cell_mv},${s.max_cell_mv}`;
    webBankRows.push(row);
    const btn = document.getElementById('web_rec_btn');
    if (btn) {
      btn.textContent = `⏹ Зупинити та зберегти CSV (${webBankRows.length - 1})`;
    }
  }

  // 2. Individual pack browser logging
  if (isWebPackRecording && recordingPackAddr && latestData.packs) {
    const p = latestData.packs[recordingPackAddr];
    if (p && p.parsed) {
      const d = p.parsed;
      const nowIso = new Date().toISOString();
      const cellsStr = (d.cells || []).join(';');
      const alarmsStr = `${d.warning}|${d.protection}|${d.error_code}`;
      const pW = (d.voltage * (d.current || 0.0)).toFixed(1);
      const row = `${nowIso},${recordingPackAddr},${d.voltage},${d.current},${pW},${d.soc},${d.cell_min},${d.cell_max},${d.cell_diff},${d.temp_pcb},${d.temp_avg},${d.temp_max},"${alarmsStr}","${cellsStr}"`;
      webPackRows.push(row);
      const pBtn = document.getElementById('p_web_rec_btn');
      if (pBtn) {
        pBtn.textContent = `⏹ Зупинити та зберегти ${recordingPackAddr} (${webPackRows.length - 1})`;
      }
    }
  }
};

setTimeout(refreshSdFiles, 1000);
setInterval(refreshSdFiles, 8000);
'''

# Заміна перед </script>
html = html.replace('initLogViewer();\n</script>', 'initLogViewer();\n' + extra_code + '\n</script>')

with open('web_extracted.html', 'w', encoding='utf-8') as f:
    f.write(html)

raw_bytes = html.encode('utf-8')
gz_bytes = gzip.compress(raw_bytes, 9)

with open('src/index_html_gz.h', 'w') as f:
    f.write('#ifndef INDEX_HTML_GZ_H\n#define INDEX_HTML_GZ_H\n\n')
    f.write(f'// Exact COMBOX Dashboard HTML with Verified Bank & Pack Loggers ({len(raw_bytes)} bytes -> {len(gz_bytes)} bytes)\n')
    f.write(f'const uint32_t INDEX_HTML_GZ_LEN = {len(gz_bytes)};\n')
    f.write('const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n')
    for i in range(0, len(gz_bytes), 16):
        chunk = gz_bytes[i:i+16]
        hex_str = ', '.join([f'0x{b:02x}' for b in chunk])
        f.write(f'    {hex_str},\n')
    f.write('};\n\n#endif // INDEX_HTML_GZ_H\n')

print(f"Generated clean HTML ({len(raw_bytes)} B) and compressed index_html_gz.h ({len(gz_bytes)} B)")
