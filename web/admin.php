<?php
// Admin portal: view, download and delete submitted drawings.
require __DIR__ . '/lib.php';

$https = !empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off';
session_name('povsword_admin');
session_set_cookie_params([
  'lifetime' => 0,
  'path'     => rtrim(dirname($_SERVER['SCRIPT_NAME']), '/\\') . '/',
  'secure'   => $https,
  'httponly' => true,
  'samesite' => 'Strict',
]);
session_start();
header('X-Frame-Options: DENY');
header('X-Content-Type-Options: nosniff');
header('Cache-Control: no-store');

$password   = (string)cfg('admin_password');
$configured = $password !== '' && $password !== 'CHANGE-ME';
$loggedIn   = $configured && !empty($_SESSION['admin']);
$error      = '';
$self       = basename(__FILE__);

function back($query = '') {
  global $self;
  header('Location: ' . $self . ($query !== '' ? '?' . $query : ''), true, 303);
  exit;
}

function csrf_ok() {
  return isset($_POST['csrf'], $_SESSION['csrf']) && hash_equals($_SESSION['csrf'], (string)$_POST['csrf']);
}

// ------------------------------------------------------------------ actions
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
  $action = isset($_POST['action']) ? $_POST['action'] : '';

  if ($action === 'login') {
    if ($configured && hash_equals($password, (string)(isset($_POST['password']) ? $_POST['password'] : ''))) {
      session_regenerate_id(true);
      $_SESSION['admin'] = true;
      $_SESSION['csrf']  = bin2hex(random_bytes(16));
      back();
    }
    sleep(1);
    $error = $configured ? 'Wrong password.' : '';
  } elseif ($loggedIn && csrf_ok()) {
    $page = max(1, (int)(isset($_POST['page']) ? $_POST['page'] : 1));
    if ($action === 'logout') {
      $_SESSION = [];
      session_destroy();
      back();
    }
    if ($action === 'delete') {
      db()->prepare('DELETE FROM drawings WHERE id = ?')->execute([(int)$_POST['id']]);
      back("page=$page");
    }
    // Bulk delete by browser, or by network (IP) for someone clearing cookies.
    if ($action === 'delete_browser') {
      db()->prepare("DELETE FROM drawings WHERE owner = ? AND owner != ''")->execute([(string)$_POST['owner']]);
      back("page=$page");
    }
    if ($action === 'delete_network') {
      db()->prepare("DELETE FROM drawings WHERE ip_hash = ? AND ip_hash != ''")->execute([(string)$_POST['ip_hash']]);
      back("page=$page");
    }
  }
}

// ---------------------------------------------------------------- downloads
if ($loggedIn && isset($_GET['dl'])) {
  $dl = $_GET['dl'];

  if ($dl === 'json') {
    $rows = db()->query('SELECT id, name, author, data, created_at, ip_hash, owner FROM drawings ORDER BY id')->fetchAll();
    foreach ($rows as &$r) {
      $r['id'] = (int)$r['id'];
      $r['data'] = decode_data($r['data']);
      $r['created_at'] = (int)$r['created_at'];
      $r['browser'] = substr($r['owner'], 0, 6);
      $r['network'] = substr($r['ip_hash'], 0, 6);
      unset($r['ip_hash'], $r['owner']);
    }
    unset($r);
    header('Content-Type: application/json; charset=utf-8');
    header('Content-Disposition: attachment; filename="povsword_drawings_' . date('Ymd') . '.json"');
    echo json_encode($rows, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
  }

  if ($dl === 'all') {
    $rows = db()->query('SELECT * FROM drawings ORDER BY id')->fetchAll();
    $out = "// POV sword gallery export, " . date('Y-m-d H:i') . ", " . count($rows) . " drawings\n"
         . "// Paste into the sketch; the playlist at the bottom drops into images[].\n\n";
    foreach ($rows as $row) $out .= export_c($row, export_name($row)) . "\n";
    $out .= "// Playlist entries:\n";
    foreach ($rows as $row) $out .= '//   IMG(' . export_name($row) . "),\n";
    header('Content-Type: text/plain; charset=utf-8');
    header('Content-Disposition: attachment; filename="povsword_drawings_' . date('Ymd') . '.h"');
    echo $out;
    exit;
  }

  $st = db()->prepare('SELECT * FROM drawings WHERE id = ?');
  $st->execute([(int)$dl]);
  $row = $st->fetch();
  if (!$row) { http_response_code(404); exit('Not found.'); }
  header('Content-Type: text/plain; charset=utf-8');
  header('Content-Disposition: attachment; filename="' . export_name($row) . '.h"');
  echo export_c($row);
  exit;
}

// ------------------------------------------------------------------- page
$perPage = 48;
$page = max(1, (int)(isset($_GET['page']) ? $_GET['page'] : 1));
$rows = []; $total = 0; $pages = 1; $perBrowser = []; $perNetwork = [];

if ($loggedIn) {
  purge_old();
  $total = (int)db()->query('SELECT COUNT(*) FROM drawings')->fetchColumn();
  $pages = max(1, (int)ceil($total / $perPage));
  $page  = min($page, $pages);
  $st = db()->prepare('SELECT * FROM drawings ORDER BY id DESC LIMIT ? OFFSET ?');
  $st->bindValue(1, $perPage, PDO::PARAM_INT);
  $st->bindValue(2, ($page - 1) * $perPage, PDO::PARAM_INT);
  $st->execute();
  $rows = $st->fetchAll();
  foreach (db()->query("SELECT owner, COUNT(*) AS n FROM drawings WHERE owner != '' GROUP BY owner") as $r) {
    $perBrowser[$r['owner']] = (int)$r['n'];
  }
  foreach (db()->query("SELECT ip_hash, COUNT(*) AS n FROM drawings WHERE ip_hash != '' GROUP BY ip_hash") as $r) {
    $perNetwork[$r['ip_hash']] = (int)$r['n'];
  }
}
$csrf = isset($_SESSION['csrf']) ? $_SESSION['csrf'] : '';
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>POV sword admin</title>
<style>
:root {
  /* "Sith" palette, same as the editor. */
  color-scheme: dark;
  --bg: #0b0707; --panel: #160d0d; --text: #f5e6e6; --muted: #a07f7f; --line: #3a1a1a;
  --copper: #e0242c; --btn: #211212; --btn-hover: #2e1919; --danger: #ff6b6b;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 16px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 1200px; margin: 0 auto; padding: 24px 16px 48px; }
header { display: flex; flex-wrap: wrap; gap: 10px 16px; align-items: center; justify-content: space-between; margin-bottom: 18px; }
h1 { font-size: 1.5rem; margin: 0; }
.muted { color: var(--muted); }
.actions { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
form { display: inline; margin: 0; }
button, .btn {
  font: inherit; font-size: 0.9rem; color: var(--text); background: var(--btn); border: 1px solid var(--line);
  border-radius: 6px; padding: 5px 10px; cursor: pointer; text-decoration: none; display: inline-block; white-space: nowrap;
}
button:hover, .btn:hover { background: var(--btn-hover); }
.primary { background: var(--copper); border-color: var(--copper); color: #fff; font-weight: 600; }
.primary:hover { background: var(--copper); filter: brightness(1.08); }
.danger { color: var(--danger); }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 14px; }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 10px; display: flex; flex-direction: column; gap: 8px; min-width: 0; }
.card svg { display: block; width: 100%; height: 90px; background: #050303; border-radius: 6px; }
.card .title { font-weight: 600; overflow-wrap: anywhere; }
.card .meta { font-size: 0.82rem; color: var(--muted); }
.card .actions { gap: 6px; }
.card button, .card .btn { font-size: 0.8rem; padding: 4px 8px; }
.login { max-width: 340px; margin: 12vh auto; background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 20px; }
.login input { font: inherit; width: 100%; padding: 8px; margin: 10px 0; border: 1px solid var(--line); border-radius: 6px; background: var(--bg); color: var(--text); }
.error { color: var(--danger); }
.pager { display: flex; gap: 8px; justify-content: center; align-items: center; margin-top: 20px; }
code { font-size: 0.85em; }
</style>
</head>
<body>
<main>
<?php if (!$configured): ?>
  <div class="login">
    <h1>POV sword admin</h1>
    <p>Set <code>admin_password</code> in <code>config.php</code> to unlock this page.</p>
  </div>
<?php elseif (!$loggedIn): ?>
  <form class="login" method="post" style="display:block">
    <h1>POV sword admin</h1>
    <input type="hidden" name="action" value="login">
    <input type="password" name="password" placeholder="Password" autocomplete="current-password" autofocus required>
    <?php if ($error): ?><p class="error"><?= h($error) ?></p><?php endif; ?>
    <button class="primary" type="submit">Log in</button>
  </form>
<?php else: ?>
  <header>
    <div>
      <h1>POV sword drawings</h1>
      <div class="muted"><?= $total ?> drawing<?= $total === 1 ? '' : 's' ?> from <?= count($perBrowser) ?> browser<?= count($perBrowser) === 1 ? '' : 's' ?> on <?= count($perNetwork) ?> network<?= count($perNetwork) === 1 ? '' : 's' ?></div>
    </div>
    <div class="actions">
      <button class="primary" type="button" data-copy="?dl=all">Copy all code</button>
      <a class="btn" href="?dl=json">Download backup (JSON)</a>
      <a class="btn" href="index.html">Open editor</a>
      <form method="post">
        <input type="hidden" name="action" value="logout">
        <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
        <button type="submit">Log out</button>
      </form>
    </div>
  </header>

  <?php if (!$rows): ?>
    <p class="muted">No drawings yet.</p>
  <?php endif; ?>

  <div class="grid">
  <?php foreach ($rows as $row):
    $data = decode_data($row['data']);
    $bro  = substr($row['owner'], 0, 6);
    $broN = isset($perBrowser[$row['owner']]) ? $perBrowser[$row['owner']] : 0;
    $net  = substr($row['ip_hash'], 0, 6);
    $netN = isset($perNetwork[$row['ip_hash']]) ? $perNetwork[$row['ip_hash']] : 0;
  ?>
    <div class="card">
      <?= thumb_svg($data) ?>
      <div>
        <div class="title">#<?= (int)$row['id'] ?> <?= h($row['name']) ?></div>
        <div class="meta">
          <?= $row['author'] !== '' ? 'by ' . h($row['author']) . ' · ' : '' ?>
          <?= h(date('Y-m-d H:i', $row['created_at'])) ?> · <?= count($data) ?> cols<br>
          <?php if ($net === ''): ?>submitter forgotten (older than <?= (int)cfg('retention_days') ?> days)<?php else: ?>
          <?php if ($bro !== ''): ?>browser <code><?= h($bro) ?></code> (<?= $broN ?>) · <?php endif; ?>network <code><?= h($net) ?></code> (<?= $netN ?>)<?php endif; ?>
        </div>
      </div>
      <div class="actions">
        <button type="button" data-copy="?dl=<?= (int)$row['id'] ?>">Copy code</button>
        <a class="btn" href="index.html#id=<?= (int)$row['id'] ?>" target="_blank" rel="noopener">Open</a>
        <form method="post" onsubmit="return confirm('Delete drawing #<?= (int)$row['id'] ?>?')">
          <input type="hidden" name="action" value="delete">
          <input type="hidden" name="id" value="<?= (int)$row['id'] ?>">
          <input type="hidden" name="page" value="<?= $page ?>">
          <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
          <button class="danger" type="submit">Delete</button>
        </form>
        <?php if ($broN > 1): ?>
        <form method="post" onsubmit="return confirm('Delete all <?= $broN ?> drawings from browser <?= h($bro) ?>?')">
          <input type="hidden" name="action" value="delete_browser">
          <input type="hidden" name="owner" value="<?= h($row['owner']) ?>">
          <input type="hidden" name="page" value="<?= $page ?>">
          <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
          <button class="danger" type="submit">Delete browser's <?= $broN ?></button>
        </form>
        <?php endif; ?>
        <?php if ($netN > max(1, $broN)): ?>
        <form method="post" onsubmit="return confirm('Delete all <?= $netN ?> drawings from network <?= h($net) ?>? On a shared wifi that can be a whole room.')">
          <input type="hidden" name="action" value="delete_network">
          <input type="hidden" name="ip_hash" value="<?= h($row['ip_hash']) ?>">
          <input type="hidden" name="page" value="<?= $page ?>">
          <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
          <button class="danger" type="submit">Delete network's <?= $netN ?></button>
        </form>
        <?php endif; ?>
      </div>
    </div>
  <?php endforeach; ?>
  </div>

  <?php if ($pages > 1): ?>
  <nav class="pager">
    <?php if ($page > 1): ?><a class="btn" href="?page=<?= $page - 1 ?>">← Newer</a><?php endif; ?>
    <span class="muted">Page <?= $page ?> of <?= $pages ?></span>
    <?php if ($page < $pages): ?><a class="btn" href="?page=<?= $page + 1 ?>">Older →</a><?php endif; ?>
  </nav>
  <?php endif; ?>
<?php endif; ?>
</main>
<?php if ($loggedIn): ?>
<script>
// Copy a drawing's (or every drawing's) PROGMEM code, ready to paste into the sketch.
// The ClipboardItem promise keeps the click's permission while the code downloads.
document.addEventListener("click", async e => {
  const btn = e.target.closest("[data-copy]");
  if (!btn) return;
  const label = btn.textContent;
  const fetchText = () => fetch(btn.dataset.copy, { cache: "no-store" }).then(r => {
    if (!r.ok) throw new Error(r.statusText);
    return r.text();
  });
  try {
    if (window.ClipboardItem && navigator.clipboard.write) {
      const blob = fetchText().then(t => new Blob([t], { type: "text/plain" }));
      await navigator.clipboard.write([new ClipboardItem({ "text/plain": blob })]);
    } else {
      await navigator.clipboard.writeText(await fetchText());
    }
    btn.textContent = "Copied!";
  } catch (err) {
    btn.textContent = "Copy failed";
  }
  setTimeout(() => { btn.textContent = label; }, 1500);
});
</script>
<?php endif; ?>
</body>
</html>
