OPTDIRS =

if CONFIG_RTAI_TRACE
OPTDIRS += trace
endif

if CONFIG_RTAI_MATH
OPTDIRS += math
endif

if CONFIG_RTAI_MALLOC
OPTDIRS += malloc
endif

if CONFIG_RTAI_TASKLETS
OPTDIRS += tasklets
endif

if CONFIG_RTAI_USI
OPTDIRS += usi
endif

if CONFIG_RTAI_WD
OPTDIRS += wd
endif

if CONFIG_RTAI_LEDS
OPTDIRS += leds
endif

SUBDIRS = include ipc $(OPTDIRS) sched arch scripts

EXTRA_DIST = config
