#!/bin/sh
# $FreeBSD$

desc="symlink returns EEXIST if the name2 argument already exists"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..21"

n0=`namegen`

for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n0}
	expect EEXIST symlink test ${n0}
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}
	else
		expect 0 unlink ${n0}
	fi
done
