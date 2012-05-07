/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Handles finding and monitoring ALSA Jack controls.  These controls represent
 * external jacks and report back when the plugged state of teh hack changes.
 */

#ifndef CRAS_ALSA_JACK_H_
#define CRAS_ALSA_JACK_H_

#include "cras_types.h"

struct cras_alsa_jack;
struct cras_alsa_jack_list;

/* Callback type for users of jack_list to define, it will be called when the
 * jack state changes.
 * Args:
 *    jack - The jack that has changed.
 *    plugged - non-zero if the jack is attached.
 *    data - User defined pointer passed to cras_alsa_jack_create.
 */
typedef void (jack_state_change_callback)(const struct cras_alsa_jack *jack,
					  int plugged,
					  void *data);

/* Creates a jack list.  The list holds all the interesting ALSA jacks for this
 * device.  These jacks will be for headphones, speakers, HDMI, etc.
 * Args:
 *    card_index - Index ALSA uses to refer to the card.  The X in "hw:X".
 *    device_index - Index ALSA uses to refer to the device.  The Y in "hw:X".
 *    direction - Input or output, look for mic or headphone jacks.
 *    cb - Function to call when a jack state changes.
 *    cb_data - Passed to the callback when called.
 * Returns:
 *    A pointer to a new jack list on success, NULL if there is a failure.
 */
struct cras_alsa_jack_list *cras_alsa_jack_list_create(
		unsigned int card_index,
		unsigned int device_index,
		enum CRAS_STREAM_DIRECTION direction,
		jack_state_change_callback *cb,
		void *cb_data);

/* Destroys a jack list created with cras_alsa_jack_list_create.
 * Args:
 *    jack_list - The list to destroy.
 */
void cras_alsa_jack_list_destroy(struct cras_alsa_jack_list *jack_list);

#endif /* CRAS_ALSA_JACK_H_ */