<?php
// Privacy notice for the gallery. Contact details and retention come from config.php.
require __DIR__ . '/lib.php';

$name   = trim((string)cfg('privacy_name'));
$email  = trim((string)cfg('privacy_email'));
$days   = (int)cfg('retention_days');
$months = max(1, (int)round($days / 30.4));
$keep   = $months >= 2 ? "about $months months" : "$days days";
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Privacy · Lasersword editor</title>
<style>
@font-face {
  font-family: "Jost"; font-style: normal; font-weight: 400 700; font-display: swap;
  src: url(fonts/jost.woff2) format("woff2");
}
:root {
  color-scheme: dark;
  --bg: #0b0707; --panel: #160d0d; --text: #f5e6e6; --muted: #a07f7f; --line: #3a1a1a; --accent: #e0242c;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 17px/1.55 Futura, "Futura PT", "Jost", system-ui, sans-serif; }
main { max-width: 720px; margin: 0 auto; padding: 32px 16px 56px; }
h1 { font-size: 2rem; margin: 0 0 4px; }
h2 { font-size: 1.15rem; margin: 28px 0 6px; }
p, li { color: var(--text); }
ul { padding-left: 1.2em; }
li { margin: 4px 0; }
a { color: var(--accent); }
.muted { color: var(--muted); }
.todo { border: 1px solid var(--accent); background: var(--panel); border-radius: 8px; padding: 10px 14px; }
code { font-size: 0.9em; }
</style>
</head>
<body>
<main>
  <p><a href="index.html">← Back to the editor</a></p>
  <h1>Privacy</h1>
  <p class="muted">What this gallery stores, why, and for how long.</p>

  <?php if ($name === '' || $email === ''): ?>
    <p class="todo">This notice isn't finished: the person responsible and a contact email still need to be filled in.</p>
  <?php endif; ?>

  <h2>Who is responsible</h2>
  <p>
    This gallery is run by <?= $name !== '' ? h($name) : '<em>(not filled in yet)</em>' ?>.
    Contact: <?= $email !== '' ? '<a href="mailto:' . h($email) . '">' . h($email) . '</a>' : '<em>(not filled in yet)</em>' ?>.
  </p>

  <h2>If you only look around</h2>
  <p>
    Drawing, using the swing preview, browsing the gallery and copying code store nothing
    about you on this site, and set no cookies. There are no ads, analytics or trackers,
    and the fonts are served from this site rather than from Google or anyone else.
  </p>
  <p>
    Like any website, the web host (one.com) keeps ordinary server logs, including
    IP addresses, for a short time for security and operations. They do this on our behalf
    under a data processing agreement.
  </p>

  <h2>If you submit a drawing</h2>
  <ul>
    <li><strong>Your drawing, its title and your nickname</strong> (if you give one) are shown
      publicly in the gallery. Please use a nickname, not your full name.</li>
    <li><strong>A cookie</strong> called <code>povsword_id</code> holding a random number is set in
      your browser. It lets the site recognise your drawings so you can replace or delete them,
      and limits how many uploads one browser can make. It is only set when you submit, it is
      strictly necessary for that, and it expires after <?= h($keep) ?>.</li>
    <li><strong>Scrambled (hashed) copies</strong> of that random number and of your IP address
      are stored with your drawing, to enforce the limits and stop abuse. Your IP address itself
      is never stored by the gallery.</li>
  </ul>

  <h2>Why</h2>
  <p>
    Your drawing and nickname are published because you chose to submit them (consent; you
    can take it back at any time by deleting the drawing). The cookie and hashed identifiers
    are kept to run the upload limits and prevent misuse of the gallery (legitimate interest).
  </p>

  <h2>How long</h2>
  <p>
    After <?= h($keep) ?> the hashed browser number and IP address are deleted from each drawing,
    so it can no longer be linked to anyone. The drawing, title and nickname stay in the gallery
    until you delete them or the organiser takes the gallery down.
  </p>

  <h2>Your rights</h2>
  <ul>
    <li><strong>Delete your drawings yourself:</strong> under “Your drawings” in the editor, in the
      same browser you submitted from.</li>
    <li>Otherwise, email us with the drawing’s title or number and we’ll delete it. You can also ask
      what we have stored, have it corrected, or object to its use.</li>
    <li>You can complain to the Norwegian Data Protection Authority,
      <a href="https://www.datatilsynet.no/" rel="noopener">Datatilsynet</a>.</li>
  </ul>
</main>
</body>
</html>
