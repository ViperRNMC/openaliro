/** @file woz_logquiet.c — PRETTY-gated runtime muting of benign upstream error spam.
 *
 * The stock Matter/BLE stack logs several non-fatal conditions at LOG_ERR/LOG_WRN
 * (red/yellow): mDNS advertiser "incorrect state" churn, "Long dispatch time"
 * perf notes, unsupported-attribute reads, the "No valid legacy adv to stop" BLE
 * double-stop, and the empty-slot "Failed to get Access Document at index: 0" the
 * access layer emits on first contact. All are expected on this bare DK bring-up
 * and every one is proven benign by the healthy unlock that follows.
 *
 * A compile-time level cut can't remove just these: each noisy source shares its
 * CONFIG_*_LOG_LEVEL with a source whose INFO lines drive the demo narrative
 * (access_document shares CONFIG_DOOR_LOCK_APP_LOG_LEVEL with access_manager's
 * "ACCESS GRANTED"/ranging lines; bt_adv shares CONFIG_BT_HCI_CORE_LOG_LEVEL),
 * and a threshold below ERR still lets ERR through. So mute per-source at runtime.
 *
 * Reversible: compiled only under CONFIG_WOZ_PRETTY_SHELL (PRETTY=1). Drop PRETTY
 * and every one of these lines returns for raw diagnosis. Needs
 * CONFIG_LOG_RUNTIME_FILTERING=y (set in integration/overlays/woz-pretty.conf).
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

/* Whole-source mutes: every visible line from these three is upstream noise on
 * this build. chip = Matter core (DIS/DL/DMG/SVR/SC/ZCL); the commissioning QR is
 * re-emitted by the app's own module, so nothing useful is lost. */
static const char *const muted_sources[] = {
	"chip",
	"bt_adv",
	"access_document",
};

// One-shot init that mutes specified log sources by setting their filter to LOG_LEVEL_NONE across all backends, silencing debug output on startup.
static int woz_logquiet_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(muted_sources); i++) {
		int sid = log_source_id_get(muted_sources[i]);

		if (sid >= 0) {
			/* NULL backend + LOG_LEVEL_NONE: drop this source on
			 * every backend/frontend regardless of compiled level. */
			(void)log_filter_set(NULL, 0, (int16_t)sid, LOG_LEVEL_NONE);
		}
	}

	return 0;
}

/* After logging backends are up (APPLICATION), same phase as woz_logfmt_init. */
SYS_INIT(woz_logquiet_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
