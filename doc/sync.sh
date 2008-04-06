#!/bin/sh
#
# sync.sh – Shell script to synchronize a directory over HTTP
#
# This script synchronizes a directory with .bml files on a webserver with a
# local directory via wget. It is meant for Apache’s non-fancy directory
# indexes. E.g. in a .htaccess file use the options
#   Options Indexes
#   IndexOptions -FancyIndexing

NAME="blinkenKUZE-sync"
VERSION="0.2"
SOURCE="http://blinken.kuze-potsdam.de/test/"
TARGET="/var/spool/blinken/"
SUFF="bml"

cd $TARGET
wget --mirror --no-parent --no-directories --accept index.html,.$SUFF \
	--user-agent "Wget/1.10.2 ($NAME/$VERSION)" --quiet --timeout=60 \
	$SOURCE || exit 1
[ -e index.html ] || exit 2
for each in *.$SUFF; do
	if ! grep -i -q "href=\"$each\"" index.html; then
		rm -f $each
	fi
done
