#!/bin/sh
set -ue
cd "$(dirname "$0")"

if [ -f ./fancurve ]; then :; else
  echo 'Build ./fancurve first!' >&2
  exit 1
fi

if [ `id -u` -ne 0 ]; then
  echo 'Must run as root!' >&2
  exit 1
fi

set -x

rm -f /tmp/net.clockish.fancurve
touch /tmp/net.clockish.fancurve
chmod 544 /tmp/net.clockish.fancurve
chown root:wheel /tmp/net.clockish.fancurve
cat ./fancurve > /tmp/net.clockish.fancurve

rm -f /tmp/net.clockish.fancurve.plist
touch /tmp/net.clockish.fancurve.plist
chmod 644 /tmp/net.clockish.fancurve.plist
chown root:wheel /tmp/net.clockish.fancurve.plist
cat ./net.clockish.fancurve.plist > /tmp/net.clockish.fancurve.plist

mv -f /tmp/net.clockish.fancurve /Library/PrivilegedHelperTools/
mv -f /tmp/net.clockish.fancurve.plist /Library/LaunchDaemons/

launchctl bootout system/net.clockish.fancurve || true
launchctl disable system/net.clockish.fancurve || true

launchctl enable system/net.clockish.fancurve
launchctl bootstrap system /Library/LaunchDaemons/net.clockish.fancurve.plist
launchctl kickstart -k -p system/net.clockish.fancurve

echo 'Done!' >&2
