Samuel Woodruff
4/25/19
Project 5

This program creates user processes and then periodically check if they get deadlocked in 
requesting resources. If so, then processes that are deadlocked
will be terminated one at a time until the deadlock no longer exists.
This program is set to end after two real life seconds at which point the some statics
will be printed to stdout and also to the log file which will be named "osslog.txt"

To compile:
	Just type make

To execute:
	./oss
	Executing like this results in verbose being turned off by default.
	This will log when a deadlock is detected and how it was resolved(Which processes are terminated to get out of the deadlock)

To execute with verbose on:
	./oss -v 1
	This will turn verbose on and print much more detailed information about
	the processes such as requests, grants, deadlocks, and also the res

You'll also find information on when a process is created or terminated naturally in the log, no matter if verbose is on or off.
