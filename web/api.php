<?php
// Public JSON API for the gallery.
//   GET  api.php                    newest drawings  -> {items: [...], more: bool}
//   GET  api.php?before=ID&limit=N  older page
//   GET  api.php?id=ID              one drawing      -> {item: {...}}
//   GET  api.php?mine=1             your drawings    -> {items, slots, writesLeft, needSecret}
//   POST api.php  {name, author, data, replace?, secret?}
//                                   -> {id, used, slots, writesLeft}
//   POST api.php  {action: "delete", id}   delete one of your drawings -> {ok}
//
// "You" is this browser, known by a cookie set on its first submission (see
// browser_key()). Each browser
// gets `slots_per_browser` drawings in the gallery; when they're all used, a new
// drawing must name one of them in `replace`. Uploads are capped for good per
// browser and, more loosely, per IP address.
require __DIR__ . '/lib.php';

header('Content-Type: application/json; charset=utf-8');
header('X-Content-Type-Options: nosniff');
header('Cache-Control: no-store');

function reply($code, $body) {
  http_response_code($code);
  echo json_encode($body, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
  exit;
}

function public_item($row) {
  return [
    'id'      => (int)$row['id'],
    'name'    => $row['name'],
    'author'  => $row['author'],
    'data'    => decode_data($row['data']),
    'created' => (int)$row['created_at'],
    'mine'    => $row['owner'] !== '' && browser_key() !== '' && hash_equals(browser_key(), $row['owner']),
  ];
}

function uploads($pdo, $who) {
  $st = $pdo->prepare('SELECT n FROM upload_counts WHERE who = ?');
  $st->execute([$who]);
  return (int)$st->fetchColumn();
}

function count_upload($pdo, $who) {
  $pdo->prepare('INSERT OR IGNORE INTO upload_counts (who, n) VALUES (?, 0)')->execute([$who]);
  $pdo->prepare('UPDATE upload_counts SET n = n + 1, updated_at = ? WHERE who = ?')->execute([time(), $who]);
}

function my_drawings($pdo) {
  if (browser_key() === '') return [];
  $st = $pdo->prepare('SELECT * FROM drawings WHERE owner = ? ORDER BY id DESC');
  $st->execute([browser_key()]);
  return $st->fetchAll();
}

try {
  $method   = $_SERVER['REQUEST_METHOD'];
  if ($method === 'POST') browser_key(true);   // only submitting creates the cookie
  $slots    = (int)cfg('slots_per_browser');
  $maxB     = (int)cfg('max_writes_per_browser');
  $maxIp    = (int)cfg('max_writes_per_ip');
  $browser  = 'b:' . browser_key();
  $network  = 'ip:' . client_key();
  $writesLeft = function ($pdo) use ($maxB, $maxIp, $browser, $network) {
    return max(0, min($maxB - uploads($pdo, $browser), $maxIp - uploads($pdo, $network)));
  };

  if ($method === 'GET') {
    if (isset($_GET['id'])) {
      $st = db()->prepare('SELECT * FROM drawings WHERE id = ?');
      $st->execute([(int)$_GET['id']]);
      $row = $st->fetch();
      if (!$row) reply(404, ['error' => 'Drawing not found.']);
      reply(200, ['item' => public_item($row)]);
    }

    if (isset($_GET['mine'])) {
      reply(200, [
        'items'      => array_map('public_item', my_drawings(db())),
        'slots'      => $slots,
        'writesLeft' => $writesLeft(db()),
        'needSecret' => cfg('submit_secret') !== '',
      ]);
    }

    $limit  = max(1, min(60, (int)(isset($_GET['limit']) ? $_GET['limit'] : 24)));
    $before = (int)(isset($_GET['before']) ? $_GET['before'] : 0);
    if ($before <= 0) $before = PHP_INT_MAX;

    $st = db()->prepare('SELECT * FROM drawings WHERE id < ? ORDER BY id DESC LIMIT ' . ($limit + 1));
    $st->execute([$before]);
    $rows = $st->fetchAll();
    $more = count($rows) > $limit;
    if ($more) array_pop($rows);

    reply(200, ['items' => array_map('public_item', $rows), 'more' => $more]);
  }

  if ($method === 'POST') {
    $in = json_decode((string)file_get_contents('php://input', false, null, 0, 65536), true);
    if (!is_array($in)) reply(400, ['error' => 'Bad request.']);
    purge_old();

    if (isset($in['action']) && $in['action'] === 'delete') {
      $st = db()->prepare("DELETE FROM drawings WHERE id = ? AND owner = ? AND owner != ''");
      $st->execute([isset($in['id']) ? (int)$in['id'] : 0, browser_key()]);
      if (!$st->rowCount()) reply(404, ['error' => "That isn't one of your drawings."]);
      reply(200, ['ok' => true]);
    }

    $wanted = (string)cfg('submit_secret');
    if ($wanted !== '') {
      $secret = isset($in['secret']) && is_string($in['secret']) ? trim($in['secret']) : '';
      if (!hash_equals(strtolower($wanted), strtolower($secret))) {
        usleep(500000);   // slow down guessing
        reply(403, ['error' => 'Wrong secret word.']);
      }
    }

    $name    = clean_text(isset($in['name']) ? $in['name'] : '', 40);
    $author  = clean_text(isset($in['author']) ? $in['author'] : '', 40);
    $data    = clean_data(isset($in['data']) ? $in['data'] : null);
    $replace = isset($in['replace']) ? (int)$in['replace'] : 0;
    if ($name === null || $author === null || $data === null) reply(400, ['error' => 'Invalid drawing.']);
    if (!array_filter($data)) reply(400, ['error' => 'Draw something first.']);
    if ($name === '') $name = 'untitled';

    $pdo = db();

    // Check and write in one transaction so parallel requests can't overshoot.
    $pdo->exec('BEGIN IMMEDIATE');
    try {
      $mine    = my_drawings($pdo);
      $mineIds = array_map(function ($r) { return (int)$r['id']; }, $mine);
      $total   = (int)$pdo->query('SELECT COUNT(*) FROM drawings')->fetchColumn();

      $error = null;
      if (uploads($pdo, $browser) >= $maxB) {
        $error = [429, ['error' => "You've used all $maxB uploads from this browser. Thanks for all the drawings!"]];
      } elseif (uploads($pdo, $network) >= $maxIp) {
        $error = [429, ['error' => 'This network has used up its uploads. Thanks for all the drawings!']];
      } elseif ($replace && !in_array($replace, $mineIds, true)) {
        $error = [403, ['error' => 'You can only replace your own drawings.']];
      } elseif (!$replace && count($mine) >= $slots) {
        $error = [409, ['error' => "You already have $slots drawings in the gallery. Pick one to replace.", 'full' => true]];
      } elseif (!$replace && $total >= (int)cfg('max_total')) {
        $error = [503, ['error' => 'The gallery is full right now.']];
      } else {
        // A replacement gets a new id, so it shows up first in the gallery again.
        if ($replace) $pdo->prepare('DELETE FROM drawings WHERE id = ?')->execute([$replace]);
        $pdo->prepare('INSERT INTO drawings (name, author, data, ip_hash, owner, created_at) VALUES (?, ?, ?, ?, ?, ?)')
            ->execute([$name, $author, json_encode($data), client_key(), browser_key(), time()]);
        $id = (int)$pdo->lastInsertId();
        count_upload($pdo, $browser);
        count_upload($pdo, $network);
      }
      $pdo->exec($error ? 'ROLLBACK' : 'COMMIT');
    } catch (Throwable $e) {
      $pdo->exec('ROLLBACK');
      throw $e;
    }

    if ($error) reply($error[0], $error[1]);
    reply(201, [
      'id'         => $id,
      'used'       => count($mine) + ($replace ? 0 : 1),
      'slots'      => $slots,
      'writesLeft' => $writesLeft($pdo),
    ]);
  }

  header('Allow: GET, POST');
  reply(405, ['error' => 'Method not allowed.']);
} catch (Throwable $e) {
  error_log('povsword api: ' . $e->getMessage());
  reply(500, ['error' => 'Server error. Try again later.']);
}
