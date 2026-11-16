<?php
// Shared helpers for api.php and admin.php.
if (realpath($_SERVER['SCRIPT_FILENAME']) === __FILE__) { http_response_code(404); exit; }

const ROWS = 12;
const MASK = 0xFFF;
const MAX_COLS = 256;

// Defaults for settings an older config.php on the server may not have yet.
const CFG_DEFAULTS = [
  'submit_secret'          => '',
  'slots_per_browser'      => 3,
  'max_writes_per_browser' => 30,
  'max_writes_per_ip'      => 300,
  'max_total'              => 20000,
  'retention_days'         => 183,
  'privacy_name'           => '',
  'privacy_email'          => '',
];

const BROWSER_COOKIE = 'povsword_id';

function cfg($key) {
  static $c = null;
  if ($c === null) $c = (require __DIR__ . '/config.php') + CFG_DEFAULTS;
  return $c[$key];
}

function db() {
  static $pdo = null;
  if ($pdo) return $pdo;

  $path = cfg('db_path');
  $dir = dirname($path);
  if (!is_dir($dir)) mkdir($dir, 0750, true);

  $pdo = new PDO('sqlite:' . $path, null, null, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
  ]);
  $pdo->exec('PRAGMA busy_timeout = 5000');
  $pdo->exec("CREATE TABLE IF NOT EXISTS drawings (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT    NOT NULL,
    author     TEXT    NOT NULL DEFAULT '',
    data       TEXT    NOT NULL,   -- JSON array of 12-bit column values
    ip_hash    TEXT    NOT NULL,
    created_at INTEGER NOT NULL
  )");
  $pdo->exec('CREATE INDEX IF NOT EXISTS drawings_ip ON drawings (ip_hash)');
  // owner = hashed browser id. Added later, so older databases get the column here.
  $cols = array_column($pdo->query('PRAGMA table_info(drawings)')->fetchAll(), 'name');
  if (!in_array('owner', $cols, true)) {
    $pdo->exec("ALTER TABLE drawings ADD COLUMN owner TEXT NOT NULL DEFAULT ''");
  }
  $pdo->exec('CREATE INDEX IF NOT EXISTS drawings_owner ON drawings (owner)');
  // Uploads so far per browser ('b:<hash>') and per IP ('ip:<hash>'), kept
  // even when the drawings themselves are replaced or deleted.
  $pdo->exec('CREATE TABLE IF NOT EXISTS upload_counts (
    who        TEXT    PRIMARY KEY,
    n          INTEGER NOT NULL DEFAULT 0,
    updated_at INTEGER NOT NULL DEFAULT 0
  )');
  $cols = array_column($pdo->query('PRAGMA table_info(upload_counts)')->fetchAll(), 'name');
  if (!in_array('updated_at', $cols, true)) {
    $pdo->exec('ALTER TABLE upload_counts ADD COLUMN updated_at INTEGER NOT NULL DEFAULT 0');
  }
  return $pdo;
}

// Forget who made what after `retention_days` (see privacy.php): drawings keep
// their picture, title and nickname but lose the hashed browser and IP, and
// upload counters nobody has touched since are deleted.
function purge_old() {
  $cutoff = time() - (int)cfg('retention_days') * 86400;
  $pdo = db();
  $pdo->prepare("UPDATE drawings SET ip_hash = '', owner = ''
                 WHERE created_at < ? AND (ip_hash != '' OR owner != '')")->execute([$cutoff]);
  $pdo->prepare('DELETE FROM upload_counts WHERE updated_at < ?')->execute([$cutoff]);
}

// Salted hash of the client's address. IPv6 is cut to its /64, since one
// connection usually owns a whole /64 and could otherwise rotate freely.
function client_key() {
  $ip = isset($_SERVER['REMOTE_ADDR']) ? $_SERVER['REMOTE_ADDR'] : '';
  if (strpos($ip, ':') !== false) {
    $bin = @inet_pton($ip);
    if ($bin !== false) $ip = bin2hex(substr($bin, 0, 8)) . '/64';
  }
  return hash_hmac('sha256', $ip, cfg('ip_salt'));
}

// Salted hash of this browser's id, from a cookie. The cookie is only created
// when someone submits ($create), so people who just look get no cookie; that
// keeps it strictly necessary for the service they asked for, with no consent
// banner needed. Server-set and HttpOnly, so Safari doesn't expire it after a
// week the way it does browser storage. Returns '' when there is no cookie.
function browser_key($create = false) {
  static $key = null;
  if ($key !== null && ($key !== '' || !$create)) return $key;
  $id = isset($_COOKIE[BROWSER_COOKIE]) ? (string)$_COOKIE[BROWSER_COOKIE] : '';
  if (!preg_match('/^[0-9a-f]{32}$/', $id)) {
    if (!$create) return $key = '';
    $id = bin2hex(random_bytes(16));
    setcookie(BROWSER_COOKIE, $id, [
      'expires'  => time() + (int)cfg('retention_days') * 86400,
      'path'     => rtrim(dirname($_SERVER['SCRIPT_NAME']), '/\\') . '/',
      'secure'   => !empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off',
      'httponly' => true,
      'samesite' => 'Lax',
    ]);
  }
  return $key = hash_hmac('sha256', 'browser:' . $id, cfg('ip_salt'));
}

// Trim, drop control characters, cap length. Returns null for invalid UTF-8.
function clean_text($s, $max) {
  if (!is_string($s) || !preg_match('//u', $s)) return null;
  $s = trim(preg_replace('/[\x00-\x1F\x7F]+/u', ' ', $s));
  return function_exists('mb_substr') ? mb_substr($s, 0, $max, 'UTF-8') : substr($s, 0, $max);
}

// Validate a column array from the client. Returns ints or null.
function clean_data($data) {
  if (!is_array($data) || !$data || count($data) > MAX_COLS) return null;
  $out = [];
  foreach ($data as $v) {
    if (!is_int($v) || $v < 0 || $v > MASK) return null;
    $out[] = $v;
  }
  return $out;
}

function decode_data($json) {
  $d = json_decode($json, true);
  return is_array($d) ? $d : [];
}

// ---------------------------------------------------------------- C export
// Mirrors the editor's export so downloaded files paste straight into the sketch.

function c_name($name) {
  $n = preg_replace('/[^A-Za-z0-9_]/', '_', trim((string)$name));
  if ($n === '') $n = 'image';
  if (preg_match('/^[0-9]/', $n)) $n = '_' . $n;
  return $n;
}

function hex3($v) { return '0x' . strtoupper(str_pad(dechex($v), 3, '0', STR_PAD_LEFT)); }
function bin12($v) { return '0b' . str_pad(decbin($v), 12, '0', STR_PAD_LEFT); }

// Unique per drawing, so many can share one file.
function export_name($row) { return c_name($row['name']) . '_' . $row['id']; }

// A single drawing is named userImage so it pastes straight over the sketch's
// mode 1 image; pass a name to override (the all-in-one export needs unique ones).
function export_c($row, $nm = 'userImage') {
  $data = decode_data($row['data']);
  $n = count($data);
  $by = $row['author'] !== '' ? ' by ' . str_replace(['*/', "\n"], '', $row['author']) : '';
  $s  = "// #{$row['id']} \"" . str_replace(['*/', "\n"], '', $row['name']) . "\"{$by}, "
      . date('Y-m-d', $row['created_at']) . "\n";
  $s .= "// $n column" . ($n === 1 ? '' : 's') . "\n";
  $s .= "const uint16_t {$nm}[$n] PROGMEM = {\n";
  foreach ($data as $i => $v) {
    $s .= '  ' . hex3($v) . ($i === $n - 1 ? ' ' : ',') . '  // ' . bin12($v) . "\n";
  }
  return $s . "};\n";
}

// ---------------------------------------------------------------- HTML
function h($s) { return htmlspecialchars((string)$s, ENT_QUOTES, 'UTF-8'); }

// Small SVG preview: lit LEDs on a dark board. Top row is bit 11.
function thumb_svg($data) {
  $cell = 6; $w = max(1, count($data)) * $cell; $hgt = ROWS * $cell;
  $s = "<svg viewBox=\"0 0 $w $hgt\" preserveAspectRatio=\"xMidYMid meet\" role=\"img\" aria-label=\"Drawing preview\">";
  $s .= "<rect width=\"$w\" height=\"$hgt\" fill=\"#050303\"/>";
  foreach ($data as $c => $v) {
    for ($b = 0; $b < ROWS; $b++) {
      $x = $c * $cell + $cell / 2;
      $y = (ROWS - 1 - $b) * $cell + $cell / 2;
      $fill = ($v >> $b) & 1 ? '#ff2b2b' : '#261616';
      $s .= "<circle cx=\"$x\" cy=\"$y\" r=\"2.4\" fill=\"$fill\"/>";
    }
  }
  return $s . '</svg>';
}
