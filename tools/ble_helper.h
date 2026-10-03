#ifndef BLE_HELPER_H
#define BLE_HELPER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* C++/WinRT を隠すための不透明な接続ハンドル。 */
typedef struct zk_ble_helper zk_ble_helper;

/*
 * address は "41:42:E1:92:76:86" または "4142E1927686" 形式。
 * 接続、指定UUIDの service/characteristic 検出、Notify購読までを行う。
 */
int ble_helper_open(zk_ble_helper **out, const char *address);

/* Write Without Response で Write characteristic へ値を書き込む。 */
int ble_helper_write_without_response(void *user, const uint8_t *value, size_t value_len);

/*
 * expected_command が value[1] にある E4 Notify を待つ。無関係な通知は破棄せず
 * キューに残す。成功時は value 全体を out にコピーし、out_len を設定する。
 */
int ble_helper_wait_notification(void *user, uint8_t expected_command,
                                 uint8_t *out, size_t out_capacity, size_t *out_len,
                                 uint32_t timeout_ms);

/*
 * 指定したプレフィックスで始まる Notify を待つ。無関係な通知は破棄せず
 * キューに残す。成功時は value 全体を out にコピーし、out_len を設定する。
 */
int ble_helper_wait_notification_prefix(void *user, const uint8_t *prefix, size_t prefix_len,
                                        uint8_t *out, size_t out_capacity, size_t *out_len,
                                        uint32_t timeout_ms);


/* 転送のパケット間待機に使う。 */
void ble_helper_sleep_ms(void *user, uint32_t milliseconds);

/* Notify購読を解除して接続ハンドルを破棄する。NULLでも安全。 */
void ble_helper_close(zk_ble_helper *helper);

/* 直近の失敗理由。helper が NULL の場合は一般的な失敗理由を返す。 */
const char *ble_helper_last_error(const zk_ble_helper *helper);

#ifdef __cplusplus
}
#endif

#endif
