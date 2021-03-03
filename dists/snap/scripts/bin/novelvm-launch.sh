#!/bin/sh

set -x

if [ -z "${XDG_CONFIG_HOME}" ]
then
  if [ -z "${HOME}" ]
  then XDG_CONFIG_HOME=$SNAP_USER_DATA/.config
  else XDG_CONFIG_HOME=${HOME}/.config
  fi
fi

# Hook up speech-dispatcher
mkdir -p $XDG_RUNTIME_DIR/speech-dispatcher
$SNAP/usr/bin/speech-dispatcher -d -C "$SNAP/etc/speech-dispatcher" -S "$XDG_RUNTIME_DIR/speech-dispatcher/speechd.sock" -m "$SNAP/usr/lib/speech-dispatcher-modules" -t 30

# Initial setup
if [ ! -f "${XDG_CONFIG_HOME}/novelvm/novelvm.ini" ]; then
  mkdir -p ${XDG_CONFIG_HOME}/novelvm/
  echo "[novelvm]\naspect_ratio=true\n"                                             >> ${XDG_CONFIG_HOME}/novelvm/novelvm.ini
  echo "[cloud]\nrootpath=/home/${USER}/snap/novelvm/current/.local/share/novelvm"  >> ${XDG_CONFIG_HOME}/novelvm/novelvm.ini
fi

# We need to do this for the user that launches novelvm, so
# it can't be done on installation
if [ ! -f "${XDG_CONFIG_HOME}/novelvm/.added-games-bundle" ]; then
  touch ${XDG_CONFIG_HOME}/novelvm/.added-games-bundle
  if ! grep -E "comi|drascula|dreamweb|lure|myst|queen|sky|sword" ${XDG_CONFIG_HOME}/novelvm/novelvm.ini
  then
  # Register the bundled games.
  $SNAP/bin/novelvm -p /usr/share/novelvm/ --recursive --add
  fi
fi

exec $SNAP/bin/novelvm "$@"