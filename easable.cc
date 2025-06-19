#include "easable.h"
#include "easer.h"

Easable::Easable()
{

}

void Easable::update_easers() {
	std::vector<Easer *> to_finalize;
	for (std::vector<Easer *>::iterator it = this->easers.begin(); it != this->easers.end(); ++it) {
		Easer *easer = *it;
		if (easer->active) {
			easer->update_value();
			if (easer->done()) {
				to_finalize.push_back(easer);
			}
		}
	}
	while(to_finalize.size()) {
		Easer *e = to_finalize.back();
        
		e->finalize();
		to_finalize.pop_back();
	}
}

void Easable::add_easer(Easer *value)
{
	this->easers.push_back(value);
}
