<?php
// POV sword gallery settings. Copy to config.php (which git ignores) and fill
// in. This file only returns an array, so requesting it over the web prints
// nothing.
return [
  // Optional word participants must enter to submit. '' = no word needed.
  'submit_secret'  => '',

  // Password for admin.php. The portal stays locked until you change this.
  'admin_password' => 'change-me',

  // Drawings each browser can have in the gallery at once (tracked with a
  // cookie). When they're all used, a new one has to replace one of them.
  'slots_per_browser' => 3,

  // Hard caps on uploads, new drawings and replacements together, for good.
  // Per browser, and per IP address as a looser backstop: a classroom shares
  // one IP, and clearing cookies gives a fresh browser. IPv6 counts per /64.
  'max_writes_per_browser' => 30,
  'max_writes_per_ip'      => 300,

  // Shown on privacy.php: who runs this gallery and how to reach them.
  // The privacy page warns that it's unfinished while these are empty.
  'privacy_name'   => '',
  'privacy_email'  => '',

  // After this many days the link between people and their drawings (hashed
  // browser id and IP) is deleted, and the browser cookie expires.
  'retention_days' => 183,

  // Hard cap on the whole database, so a flood can't fill the disk.
  'max_total'      => 20000,

  // SQLite database. data/ is blocked by data/.htaccess, but a path outside
  // the web root is safer if your host allows it, e.g. '/home/you/povsword.sqlite'.
  'db_path'        => __DIR__ . '/data/drawings.sqlite',

  // IPs are stored only as a salted hash. Keep this secret and don't change it,
  // or everyone's submission count resets.
  'ip_salt'        => 'replace-with-a-long-random-string',
];
