#!/bin/sh

if test "x$1" = "x"; then
	echo "need arch"
	exit 1
fi

ARCH=$1

OLD_LATENCY=`grep 'CONFIG_RTAI_SCHED_LATENCY' ../../rtai_config.h | sed -e 's/.* \([[:digit:]]\+\)[^[:digit:]]*/\1/'`

# echo "arch:$ARCH latency:$OLD_LATENCY"

if test $OLD_LATENCY -eq 0 ; then
	OUTPUT=`../arch/"$ARCH"/calibration/calibrate -x | grep 'SUMMARY'`
	RC=$?
	if test $RC != 0 ; then
		exit $RC
	fi

	#OUTPUT="1111 3333 4444"
	NEW_LATENCY=`echo $OUTPUT | cut -d ' ' -f 2`
	KERN_BARD=`echo $OUTPUT | cut -d ' ' -f 3`
	USER_BARD=`echo $OUTPUT | cut -d ' ' -f 4`

	# rtai_config.h
	OLD=../../rtai_config.h
	NEW=tmp_rtai_config.h
	awk \
		-v NEW_LATENCY=$NEW_LATENCY -v BUSY_TIME_ALIGN=1 -v KERN_BARD=$KERN_BARD -v USER_BARD=$USER_BARD \
'{
	if ($2 == "CONFIG_RTAI_SCHED_LATENCY") {
		print "#define CONFIG_RTAI_SCHED_LATENCY " NEW_LATENCY;
	} else if ($2 == "CONFIG_RTAI_BUSY_TIME_ALIGN") {
		print "#define CONFIG_RTAI_BUSY_TIME_ALIGN " BUSY_TIME_ALIGN;
	} else if ($2 == "CONFIG_RTAI_KERN_BUSY_ALIGN_RET_DELAY") {
		print "#define CONFIG_RTAI_KERN_BUSY_ALIGN_RET_DELAY " KERN_BARD;
	} else if ($2 == "CONFIG_RTAI_USER_BUSY_ALIGN_RET_DELAY") {
		print "#define CONFIG_RTAI_USER_BUSY_ALIGN_RET_DELAY " USER_BARD;
	} else {
		print $0;
	}
}' $OLD > $NEW

	touch -r $OLD $NEW
	#rm -f $OLD
	#mv $NEW $OLD

	# .rtai_config
	OLD=../../.rtai_config
	NEW=tmp_rtai_config
	awk --field-separator='=' \
		-v NEW_LATENCY=$NEW_LATENCY -v BUSY_TIME_ALIGN="y" -v KERN_BARD=$KERN_BARD -v USER_BARD=$USER_BARD \
'{
	if ($1 == "CONFIG_RTAI_SCHED_LATENCY") {
		print "CONFIG_RTAI_SCHED_LATENCY=\"" NEW_LATENCY "\"";
	} else if (($1 == "CONFIG_RTAI_BUSY_TIME_ALIGN") || ($0 == "# CONFIG_RTAI_BUSY_TIME_ALIGN is not set")) {
		print "CONFIG_RTAI_BUSY_TIME_ALIGN=" BUSY_TIME_ALIGN;
	} else if ($1 == "CONFIG_RTAI_KERN_BUSY_ALIGN_RET_DELAY") {
		print "CONFIG_RTAI_KERN_BUSY_ALIGN_RET_DELAY=\"" KERN_BARD "\"";
	} else if ($1 == "CONFIG_RTAI_USER_BUSY_ALIGN_RET_DELAY") {
		print "CONFIG_RTAI_USER_BUSY_ALIGN_RET_DELAY=\"" USER_BARD "\"";
	} else {
		print $0;
	}
}' $OLD > $NEW

	touch -r $OLD $NEW
	#rm -f $OLD
	#mv $NEW $OLD
fi

