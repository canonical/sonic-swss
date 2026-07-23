# resolute: 'tests' dropped from SUBDIRS — GCC15 brace-init hard errors in dashtunnelorch_ut.cpp; images don't need the UT
if GCOV_ENABLED
SUBDIRS = gcovpreload fpmsyncd neighsyncd portsyncd mclagsyncd natsyncd fdbsyncd orchagent swssconfig cfgmgr gearsyncd
else
SUBDIRS = fpmsyncd neighsyncd portsyncd mclagsyncd natsyncd fdbsyncd orchagent swssconfig cfgmgr gearsyncd
endif


if HAVE_LIBTEAM
SUBDIRS += teamsyncd tlm_teamd
endif
