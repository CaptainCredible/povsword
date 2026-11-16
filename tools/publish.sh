#!/bin/bash
# Uploads web/ to the web host over SFTP, then checks the gallery API answers.
#
#   tools/publish.sh            upload what changed
#   tools/publish.sh --config   also overwrite the server's config.php
#   tools/publish.sh --delete   also remove files from the server that are no
#                               longer in web/ (shows them first and asks)
#
# The server keeps its own config.php and database: config.php is only uploaded
# the first time (or with --config), and data/*.sqlite is never touched, not
# even by --delete.
#
# Connection details live in tools/publish.conf (see publish.conf.example).
# Needs lftp: brew install lftp

set -euo pipefail
cd "$(dirname "$0")/.."

CONF=tools/publish.conf
if [ ! -f "$CONF" ]; then
  echo "Missing $CONF. Copy tools/publish.conf.example to $CONF and fill it in." >&2
  exit 1
fi
# shellcheck source=/dev/null
source "$CONF"
: "${SFTP_HOST:?set SFTP_HOST in $CONF}"
: "${SFTP_USER:?set SFTP_USER in $CONF}"
: "${REMOTE_DIR:?set REMOTE_DIR in $CONF}"
URL="https://captaincredible.com/${REMOTE_DIR#/www/}"
URL="${URL%/}"

DELETE=no
CONFIG=no
for arg in "$@"; do
  case "$arg" in
    --delete) DELETE=yes ;;
    --config) CONFIG=yes ;;
    *) echo "Unknown argument '$arg'. Use: --delete, --config" >&2; exit 1 ;;
  esac
done

# The gallery gets a folder of its own. Uploading into the web root itself
# would mix it up with everything else on the domain, and --delete would wipe it.
case "${REMOTE_DIR%/}" in
  ""|"/"|"/www"|"www"|"/public_html"|"public_html")
    echo "REMOTE_DIR in $CONF must be a folder of its own, e.g. /www/povsword — not '$REMOTE_DIR'." >&2
    exit 1 ;;
esac

command -v lftp >/dev/null || { echo "lftp is missing: brew install lftp" >&2; exit 1; }
[ -f web/index.html ] || { echo "web/index.html is missing." >&2; exit 1; }

if grep -q "'admin_password' => 'CHANGE-ME'" web/config.php; then
  echo "Note: admin_password in web/config.php is still CHANGE-ME, so admin.php stays locked."
fi

# Password: asked for each time unless an SSH key is set up (SFTP_KEY=yes).
# The password goes to lftp through the environment, so any character works.
if [ "${SFTP_KEY:-no}" = yes ]; then export LFTP_PASSWORD=""; else
  read -r -s -p "Password for $SFTP_USER@$SFTP_HOST: " LFTP_PASSWORD; echo
  export LFTP_PASSWORD
fi

run_lftp() {
  lftp --env-password -u "$SFTP_USER" "sftp://$SFTP_HOST" -e "
    set sftp:auto-confirm yes
    set net:max-retries 3
    set net:timeout 20
    mkdir -p -f '$REMOTE_DIR/data'
    $1
    bye"
}

# Excluded files are neither uploaded nor deleted. One line: lftp runs each
# line of its script as a separate command.
EXCLUDE="--exclude-glob config.php --exclude-glob *.sqlite* --exclude-glob .DS_Store"
MIRROR="mirror --reverse --no-perms --parallel=4 $EXCLUDE web/ '$REMOTE_DIR'"

echo "== web/ -> $SFTP_HOST:$REMOTE_DIR"

if [ "$DELETE" = yes ]; then
  echo "Files on the server that are no longer in web/:"
  STALE=$(run_lftp "$MIRROR --delete --dry-run" | grep -E '^rm ' || true)
  if [ -z "$STALE" ]; then
    echo "  none"
  else
    echo "$STALE" | sed 's/^/  /'
    read -r -p "Delete these from the server? [y/N] " OK
    [ "$OK" = y ] || [ "$OK" = Y ] || { echo "Nothing deleted; uploading only."; DELETE=no; }
  fi
fi

if [ "$DELETE" = yes ]; then
  run_lftp "$MIRROR --delete --verbose"
else
  run_lftp "$MIRROR --verbose"
fi

# config.php holds the admin password, which may have been changed on the
# server, so it only goes up when there is none yet or when asked.
if [ "$CONFIG" = no ] && run_lftp "cls -1 '$REMOTE_DIR/config.php'" 2>/dev/null | grep -q 'config.php'; then
  echo "Kept the server's config.php (use --config to overwrite it)."
else
  run_lftp "put web/config.php -o '$REMOTE_DIR/config.php'"
  echo "Uploaded config.php."
fi

echo "Done: $SFTP_HOST:$REMOTE_DIR"

# The first request creates the database, so this also checks PHP can write data/.
echo "Checking $URL/api.php…"
BODY="$(curl -s -m 15 -w $'\n%{http_code}' "$URL/api.php" || true)"
CODE="${BODY##*$'\n'}"
if [ "$CODE" = 200 ] && [[ "$BODY" == '{"items"'* ]]; then
  echo "Gallery: $URL/"
  echo "Admin:   $URL/admin.php"
else
  echo "The API check failed (HTTP $CODE)." >&2
  [ "$CODE" = 500 ] && echo "Most likely PHP can't write $REMOTE_DIR/data, or SQLite is missing. Check the error log in the one.com control panel." >&2
  [ "$CODE" = 404 ] && echo "Nothing at $URL/ — check REMOTE_DIR in $CONF." >&2
  exit 1
fi
