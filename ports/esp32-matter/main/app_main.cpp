/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <esp_matter.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <app_priv.h>
#include "app_shell.h"
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>
#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
#include <aliro_reader_delegate.h>
#include <aliro_reader.h>
#include <woz_uwb_facade.h>
#include "door_lock_manager.h"
#include <platform/PlatformManager.h>
#include <vector>
#include "host/ble_gatt.h"                 // struct ble_gatt_svc_def (NimBLE)
#include <platform/ESP32/BLEManagerImpl.h> // BLEMgrImpl().ConfigureExtraServices()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "app_main";
uint16_t door_lock_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;
using namespace chip;

constexpr auto k_timeout_seconds = 300;

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
// BLE coexistence (nRF-style local UWB unlock): the Aliro reader shares Matter's
// NimBLE host — Matter keeps BLE up (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING=n), its
// GATT service is registered via BLEMgrImpl().ConfigureExtraServices() before
// esp_matter::start (see app_main), and the reader's advertiser + L2CAP CoC come
// up once the device is operational (Matter has released the single legacy
// advertiser: after commissioning, or immediately when already paired). A trusted
// UWB range then drives the Matter lock to Unlocked. This replaces the old handoff,
// which crashed because NimBLE can't be re-inited after Matter reclaims BLE.
#define ALIRO_UNLOCK_RANGE_CM 100 // approach threshold: unlock at/under this (bench-tunable)
#define ALIRO_RELOCK_RANGE_CM 150 // depart threshold: relock past this (hysteresis > unlock)

static void aliro_reader_task(void *arg)
{
	// Give Matter's host time to finish syncing and stop advertising before we
	// take over the (single, shared) legacy advertiser.
	vTaskDelay(pdMS_TO_TICKS(1000));

	int rc = aliro_reader_start_attached();
	ESP_LOGI(TAG, "aliro_reader_start_attached() = %d (%s)", rc,
		 rc == 0 ? "reader advertising on shared host" : "reader start FAILED");

	// Approach-unlock: `locked` mirrors the bolt. The fixed Matter auto-relock is
	// disabled (see create_auto_relock_time(..., 0)), so this loop is the only
	// auto-driver: unlock when the peer is within range, hold while it is present,
	// and relock when it leaves (moved past the relock band, or disconnected).
	bool locked = true;
	while (true) {
		int32_t cm = 0;
		bool have = woz_uwb_trusted_range_cm(&cm);

		if (have && locked && cm >= 0 && cm <= ALIRO_UNLOCK_RANGE_CM) {
			ESP_LOGI(TAG, "Aliro trusted range %d cm (<= %d): unlocking", (int)cm,
				 ALIRO_UNLOCK_RANGE_CM);
			chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
				BoltLockMgr().Unlock(door_lock_endpoint_id,
						     chip::app::Clusters::DoorLock::
							     OperationSourceEnum::kAliro);
			});
			// Tell the phone's Wallet the reader granted access: the reader->phone
			// "Reader Status Changed" (Unsecured, Aliro step 23) is what fires the
			// iPhone unlock animation. Marshaled to the BLE-host task; a no-op if the
			// ranging session already dropped.
			aliro_reader_notify_unlock(true);
			locked = false;
		} else if (!locked && (!have || cm > ALIRO_RELOCK_RANGE_CM)) {
			// Hysteresis (RELOCK > UNLOCK) keeps the door open across range jitter
			// while the peer stays near; no trusted range means it disconnected.
			ESP_LOGI(TAG, "Aliro peer %s: relocking",
				 have ? "out of range" : "gone");
			chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
				BoltLockMgr().Lock(door_lock_endpoint_id,
						   chip::app::Clusters::DoorLock::
							   OperationSourceEnum::kAliro);
			});
			aliro_reader_notify_unlock(false); // Reader Status Changed -> Secured
			locked = true;
		}
		vTaskDelay(pdMS_TO_TICKS(200));
	}
}

static void start_aliro_reader_once(void)
{
	static bool started = false;
	if (started) {
		return;
	}
	started = true;
	xTaskCreate(aliro_reader_task, "aliro_reader", 8192, nullptr, 5, nullptr);
	ESP_LOGI(TAG, "Aliro reader (attach mode) task started");
}
#endif // CONFIG_ENABLE_ALIRO_BLE_UWB

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
	switch (event->Type) {
	case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
		ESP_LOGI(TAG, "Interface IP Address changed");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
		ESP_LOGI(TAG, "Commissioning complete");
#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
		// Matter stops advertising when commissioning completes; the reader can
		// now take the advertiser and run the local BLE+UWB Aliro transaction.
		start_aliro_reader_once();
#endif
		break;

	case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
		ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
		ESP_LOGI(TAG, "Commissioning session started");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
		ESP_LOGI(TAG, "Commissioning session stopped");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
		ESP_LOGI(TAG, "Commissioning window opened");
		break;

	case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
		ESP_LOGI(TAG, "Commissioning window closed");
		break;

	case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
		ESP_LOGI(TAG, "Fabric removed successfully");
		if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
			chip::CommissioningWindowManager &commissionMgr =
				chip::Server::GetInstance().GetCommissioningWindowManager();
			constexpr auto kTimeoutSeconds =
				chip::System::Clock::Seconds16(k_timeout_seconds);
			if (!commissionMgr.IsCommissioningWindowOpen()) {
				/* After removing last fabric, this example does not remove the
				 * Wi-Fi credentials and still has IP connectivity so, only
				 * advertising on DNS-SD.
				 */
				CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(
					kTimeoutSeconds,
					chip::CommissioningWindowAdvertisement::kDnssdOnly);
				if (err != CHIP_NO_ERROR) {
					ESP_LOGE(TAG,
						 "Failed to open commissioning window, "
						 "err:%" CHIP_ERROR_FORMAT,
						 err.Format());
				}
			}
		}
		break;
	}

	case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
		ESP_LOGI(TAG, "Fabric will be removed");
		break;

	case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
		ESP_LOGI(TAG, "Fabric is updated");
		break;

	case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
		ESP_LOGI(TAG, "Fabric is committed");
		break;

	case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
		ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
		break;

	default:
		break;
	}
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or
// light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
				       uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
	ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id,
		 effect_variant);
	return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
					 uint32_t cluster_id, uint32_t attribute_id,
					 esp_matter_attr_val_t *val, void *priv_data)
{
	esp_err_t err = ESP_OK;

	if (type == PRE_UPDATE) {
		/* Driver update */
		app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
		err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id,
						  attribute_id, val);
	}

	return err;
}

extern "C" void app_main()
{
	esp_err_t err = ESP_OK;

	/* Initialize the ESP NVS layer */
	nvs_flash_init();

	/* Bolt-state indicator. Before Matter start, so the first LockState update
	 * that lands already has somewhere to go. */
	app_driver_led_init();

#if CONFIG_PM_ENABLE
	esp_pm_config_t pm_config = {.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
				     .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
				     .light_sleep_enable = true
#endif
	};
	err = esp_pm_configure(&pm_config);
#endif

	/* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
	node::config_t node_config;

	// node handle can be used to add/modify other endpoints.
	node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
	ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

	door_lock::config_t door_lock_config;
#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
	AliroReaderDelegate::Instance().Init();
	door_lock_config.door_lock.delegate = &AliroReaderDelegate::Instance();
#endif
	door_lock_config.door_lock.lock_state = chip::to_underlying(DoorLock::DlLockState::kLocked);
	cluster::door_lock::feature::credential_over_the_air_access::config_t cota_config;
	cluster::door_lock::feature::pin_credential::config_t pin_credential_config;
	cluster::door_lock::feature::user::config_t user_config;
	// endpoint handles can be used to add/modify clusters.
	endpoint_t *endpoint = door_lock::create(node, &door_lock_config, ENDPOINT_FLAG_NONE, NULL);
	ABORT_APP_ON_FAILURE(endpoint != nullptr,
			     ESP_LOGE(TAG, "Failed to create door lock endpoint"));
	cluster_t *door_lock_cluster = cluster::get(endpoint, DoorLock::Id);
	cluster::door_lock::feature::credential_over_the_air_access::add(door_lock_cluster,
									 &cota_config);
	cluster::door_lock::feature::pin_credential::add(door_lock_cluster, &pin_credential_config);
	cluster::door_lock::feature::user::add(door_lock_cluster, &user_config);
#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
	cluster::door_lock::feature::aliro_provisioning::add(door_lock_cluster);
	cluster::door_lock::feature::aliro_bleuwb::add(door_lock_cluster);
#endif
	// 0 disables CHIP's fixed auto-relock timer (DoorLockServer skips scheduling when
	// AutoRelockTime == 0). Relock is driven by proximity instead: the reader task
	// relocks when the Aliro peer leaves range (see aliro_reader_task).
	cluster::door_lock::attribute::create_auto_relock_time(door_lock_cluster, 0);

	door_lock_endpoint_id = endpoint::get_id(endpoint);
	ESP_LOGI(TAG, "Door lock created with endpoint_id %d", door_lock_endpoint_id);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
	/* Set OpenThread platform config */
	esp_openthread_platform_config_t config = {
		.radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
		.host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
		.port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
	};
	set_openthread_platform_config(&config);
#endif

#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
	/* Register the Aliro reader's GATT service on Matter's NimBLE host BEFORE the
	 * Matter server builds its GATT table, so the reader's 0xFFF2 service coexists
	 * with CHIPoBLE. Must be called before esp_matter::start (which runs InitServer). */
	{
		const struct ble_gatt_svc_def *aliro_svc =
			static_cast<const struct ble_gatt_svc_def *>(aliro_reader_ble_prepare());
		if (aliro_svc != nullptr) {
			std::vector<struct ble_gatt_svc_def> svcs = {*aliro_svc};
			CHIP_ERROR e =
				chip::DeviceLayer::Internal::BLEMgrImpl().ConfigureExtraServices(
					svcs, true);
			ESP_LOGI(TAG, "Aliro GATT extra-service register: %" CHIP_ERROR_FORMAT,
				 e.Format());
		} else {
			ESP_LOGE(TAG,
				 "aliro_reader_ble_prepare failed; reader GATT not registered");
		}
	}
#endif

	/* Matter start */
	err = esp_matter::start(app_event_cb);
	ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

	/* Print the commissioning QR-code URL + manual pairing code at boot so it is
	 * always in the log (Apple Home / chip-tool). BLE is the initial transport. */
	PrintOnboardingCodes(
		chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

	/* do nothing now */
	door_lock_init();

#ifdef CONFIG_ENABLE_ALIRO_BLE_UWB
	/* Already commissioned (e.g. a reboot): Matter is not advertising over BLE, so
	 * bring the reader up now. A fresh commission starts it on kCommissioningComplete. */
	if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0) {
		ESP_LOGI(TAG, "Already commissioned; starting Aliro reader on shared host");
		start_aliro_reader_once();
	}
#endif

#if CONFIG_ENABLE_ENCRYPTED_OTA
	err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
	ABORT_APP_ON_FAILURE(
		err == ESP_OK,
		ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

	/* Interactive console. This replaces esp_matter::console::init(), whose CHIP
	 * shell has no line editing and loses your input whenever a log line lands.
	 * Only one of the two may run: both read the same console UART. */
	app_shell_start();
}
