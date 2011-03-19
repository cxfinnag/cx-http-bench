/*
 * Copyright (c) 2011, Finn Arne Gangstad <finnag@cxense.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <math.h>

#include "expdecay.h"
#include "timeutil.h"

double
expdecay_value(const struct expdecay *ed)
{
	return ed->value / ed->base_value;
}

void
expdecay_update(struct expdecay *ed, double value, double timestamp)
{
	if (!timestamp) {
		timestamp = now();
	}
	double delta = timestamp - ed->last_update;
	if (delta <= 0) {
		ed->value += value;
		ed->last_update = timestamp;
		return;
	}

	/* if delta is 1, we want to do value *= decay_factor */
	/* if delta is 0.5, we want to do value *= sqrt(decay_factor), or 
	 *   *= decay_factor ^ 0.5. .. so decay_factor ^ delta then. */

	double factor = pow(ed->decay_factor, delta);
	
	ed->value *= factor;
	ed->base_value *= factor;
	ed->value += value;
	ed->base_value += delta;
}

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
