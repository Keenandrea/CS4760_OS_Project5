#ifndef SHAREDMEM_H_
#define SHAREDMEM_H_

struct time{
	int nanoseconds;
	int seconds;
};

struct resourceDescriptor{
	int request[18];
	int release[18];
	int allocated[18];
	int shareable;//0 for non-shareable, and 1 assigned to shareable
	int instances;//assign 1-10 for intial instance in each resource class
	int availableInstances;
};


struct sharedRes{
	struct resourceDescriptor resources[20];
	struct time time;
};
#endif
