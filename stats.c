#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <geometry.h>
#include "dat.h"
#include "fns.h"

void
updatestats(Stats *s, uvlong v)
{
	s->v = v;
	s->n++;
	s->acc += v;
	s->avg = s->acc/s->n;
	s->min = v < s->min || s->n == 1? v: s->min;
	s->max = v > s->max || s->n == 1? v: s->max;
	s->nframes++;
}
