#ifndef EASABLE_H
#define EASABLE_H
#include "easer.h"

class Easable
{
public:
	Easable();
	void update_easers();
	void add_easer(Easer *value);
protected:
	std::vector<Easer *> easers;
};

#endif // EASABLE_H
